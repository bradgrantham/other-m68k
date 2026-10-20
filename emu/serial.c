/* Serial port.
 *
 * TX: the firmware bit-bangs GPIO.SERTX.  The bus layer reports every edge
 * with its cycle stamp; the decoder treats the edge list as the waveform,
 * finds a falling (start) edge, samples at 1.5, 2.5, ... 9.5 bit times
 * (CPU_CLOCK_HZ / SERIAL_BAUD = 104.17 clocks per bit, exact in fixed point)
 * and emits the byte if the stop bit is high.  Decoded bytes go to stdout
 * (unbuffered) and to --serial-log.  Timing slop of a few percent per frame
 * is tolerated because sampling happens at bit centres.
 *
 * Optional hardware TX (--hw-serial-tx): a SERDATA write transmits
 * immediately and SERTXBUSY reads 1 for ten bit times.
 *
 * RX: bytes from stdin (raw, non-blocking when a tty) are delivered to
 * SERDATA no faster than one per ten bit times, like a real 115200 link.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "hw.h"
#include "emu.h"
#include "io.h"
#include "serial.h"

unsigned long serial_tx_bytes;
unsigned long serial_tx_framing_errors;

/* ---- TX decoder ---------------------------------------------------------- */

struct edge { uint64_t t; int level; };

static struct edge *edges;
static size_t edge_count, edge_cap, edge_head;
static int base_level = 1;          /* level before edges[edge_head] */
static FILE *logf;

/* Time of sample k (k = 0..7 data, 8 = stop) after the start edge at t0:
 * t0 + (k + 1.5) bit times, in whole clocks. */
static uint64_t sample_time(uint64_t t0, int k)
{
    return t0 + ((uint64_t)(2 * k + 3) * CPU_CLOCK_HZ) / (2 * SERIAL_BAUD);
}

static void emit_byte(uint8_t b)
{
    serial_tx_bytes++;
    fputc(b, stdout);
    if (logf) {
        fputc(b, logf);
        fflush(logf);
    }
}

void serial_tx_edge(uint64_t cycle, int level)
{
    if (edge_count == edge_cap) {
        if (edge_head > 0) {
            memmove(edges, edges + edge_head, (edge_count - edge_head) * sizeof *edges);
            edge_count -= edge_head;
            edge_head = 0;
        }
        if (edge_count == edge_cap) {
            edge_cap = edge_cap ? edge_cap * 2 : 1024;
            edges = realloc(edges, edge_cap * sizeof *edges);
            if (!edges) {
                perror("realloc");
                exit(1);
            }
        }
    }
    edges[edge_count].t = cycle;
    edges[edge_count].level = level;
    edge_count++;
}

/* Level of the line at time t, given edges[from..] (edges[from].t <= t). */
static int level_at(size_t from, uint64_t t)
{
    int level = edges[from].level;
    for (size_t i = from + 1; i < edge_count && edges[i].t <= t; i++)
        level = edges[i].level;
    return level;
}

/* Hardware-TX bytes waiting for their completion time (see serial_hw_tx). */
static struct { uint64_t done; uint8_t byte; } hw_q[256];
static size_t hw_len, hw_head;

static void emit_hw_before(uint64_t t)
{
    while (hw_head < hw_len && hw_q[hw_head].done <= t)
        emit_byte(hw_q[hw_head++].byte);
    if (hw_head == hw_len)
        hw_head = hw_len = 0;
}

static void decode(uint64_t now)
{
    for (;;) {
        /* Hunt for a falling edge (edges alternate, so level 0 is one). */
        size_t i = edge_head;
        while (i < edge_count && !(edges[i].level == 0 && (i > edge_head ? edges[i - 1].level : base_level) == 1))
            i++;
        if (i == edge_count) {
            /* No start bit pending: forget everything but the current level. */
            if (edge_count > 0) {
                base_level = edges[edge_count - 1].level;
                edge_count = edge_head = 0;
            }
            emit_hw_before(now);
            return;
        }
        uint64_t t0 = edges[i].t;
        uint64_t t_stop = sample_time(t0, 8);
        if (now != UINT64_MAX && now < t_stop) {
            emit_hw_before(now);
            return;                 /* frame not complete yet */
        }
        emit_hw_before(t_stop);     /* hardware bytes that finished earlier */

        unsigned byte = 0;
        for (int k = 0; k < 8; k++)
            byte |= (unsigned)level_at(i, sample_time(t0, k)) << k;
        int stop = level_at(i, t_stop);
        if (stop)
            emit_byte((uint8_t)byte);
        else
            serial_tx_framing_errors++;

        /* Consume everything before the stop-bit sample; the next start
         * edge must come after it. */
        size_t j = i;
        while (j < edge_count && edges[j].t <= t_stop)
            j++;
        base_level = edges[j - 1].level;
        edge_head = j;
    }
}

void serial_tx_decode(uint64_t now)
{
    decode(now);
}

void serial_flush(void)
{
    decode(UINT64_MAX);
    fflush(stdout);
    if (logf)
        fflush(logf);
}

/* ---- hardware TX ----------------------------------------------------------- */

static uint64_t hw_busy_until;

void serial_hw_tx(uint8_t byte, uint64_t now)
{
    /* Queue the byte with the time its stop bit ends, so it interleaves
     * correctly with bit-banged frames still in flight. */
    hw_busy_until = now + (10 * CPU_CLOCK_HZ + SERIAL_BAUD - 1) / SERIAL_BAUD;
    if (hw_len == sizeof hw_q / sizeof hw_q[0]) {
        if (hw_head) {
            memmove(hw_q, hw_q + hw_head, (hw_len - hw_head) * sizeof hw_q[0]);
            hw_len -= hw_head;
            hw_head = 0;
        } else {
            emit_hw_before(UINT64_MAX);     /* overflow: just flush */
        }
    }
    hw_q[hw_len].done = hw_busy_until;
    hw_q[hw_len].byte = byte;
    hw_len++;
}

int serial_hw_busy(uint64_t now)
{
    return opts.hw_serial_tx && now < hw_busy_until;
}

/* ---- RX from stdin --------------------------------------------------------- */

static int stdin_enabled;
static int stdin_eof;
static int tty_saved;
static struct termios tty_orig;
static int saved_flags = -1;
static uint64_t next_rx_cycle;

int serial_init(const char *log_path, int use_stdin)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (log_path) {
        logf = fopen(log_path, "wb");
        if (!logf) {
            fprintf(stderr, "m68k-emu: cannot open serial log '%s'\n", log_path);
            return -1;
        }
    }
    stdin_enabled = use_stdin;
    if (stdin_enabled) {
        saved_flags = fcntl(STDIN_FILENO, F_GETFL);
        if (saved_flags >= 0)
            fcntl(STDIN_FILENO, F_SETFL, saved_flags | O_NONBLOCK);
        if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &tty_orig) == 0) {
            struct termios t = tty_orig;
            /* Raw-ish: no echo, no line editing, no CR translation, but keep
             * ISIG so Ctrl-C still terminates the emulator. */
            t.c_lflag &= (tcflag_t)~(ECHO | ICANON | IEXTEN);
            t.c_iflag &= (tcflag_t)~(ICRNL | INLCR | IXON);
            t.c_cc[VMIN] = 0;
            t.c_cc[VTIME] = 0;
            if (tcsetattr(STDIN_FILENO, TCSANOW, &t) == 0)
                tty_saved = 1;
        }
    }
    return 0;
}

void serial_shutdown(void)
{
    serial_flush();
    if (tty_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &tty_orig);
        tty_saved = 0;
    }
    if (saved_flags >= 0) {
        fcntl(STDIN_FILENO, F_SETFL, saved_flags);
        saved_flags = -1;
    }
    if (logf) {
        fclose(logf);
        logf = NULL;
    }
}

void serial_reset(void)
{
    decode(UINT64_MAX);             /* a reset does not un-send bytes */
    edge_count = edge_head = 0;
    base_level = 1;
    hw_busy_until = 0;
}

void serial_rx_poll(uint64_t now)
{
    if (!stdin_enabled || stdin_eof || now < next_rx_cycle)
        return;
    unsigned char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n == 1) {
        io_serial_deliver(c);
        next_rx_cycle = now + (10 * CPU_CLOCK_HZ + SERIAL_BAUD - 1) / SERIAL_BAUD;
    } else if (n == 0) {
        stdin_eof = 1;
    } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        stdin_eof = 1;
    }
}
