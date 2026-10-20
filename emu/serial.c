/* Serial console back end.
 *
 * The DUART model (duart.c) owns all timing; this file only moves bytes
 * between the host and the model.  Channel A output goes to stdout
 * (unbuffered) and to --serial-log; channel A input comes from --serial-type
 * text first, then from stdin (raw-ish, non-blocking when a tty).  Channel B
 * output goes to --duart-b-log or is dropped.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "emu.h"
#include "serial.h"

static FILE *logf;
static FILE *b_logf;

static int stdin_enabled;
static int stdin_eof;
static int tty_saved;
static struct termios tty_orig;
static int saved_flags = -1;

static const char *inject;          /* remaining --serial-type text */

int serial_init(const char *log_path, const char *b_log_path, int use_stdin)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (log_path) {
        logf = fopen(log_path, "wb");
        if (!logf) {
            fprintf(stderr, "m68k-emu: cannot open serial log '%s'\n", log_path);
            return -1;
        }
    }
    if (b_log_path) {
        b_logf = fopen(b_log_path, "wb");
        if (!b_logf) {
            fprintf(stderr, "m68k-emu: cannot open channel B log '%s'\n", b_log_path);
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
    if (b_logf) {
        fclose(b_logf);
        b_logf = NULL;
    }
}

void serial_flush(void)
{
    fflush(stdout);
    if (logf)
        fflush(logf);
    if (b_logf)
        fflush(b_logf);
}

void serial_tx_byte(uint8_t b)
{
    fputc(b, stdout);
    if (logf) {
        fputc(b, logf);
        fflush(logf);
    }
}

void serial_b_tx_byte(uint8_t b)
{
    if (b_logf) {
        fputc(b, b_logf);
        fflush(b_logf);
    }
}

void serial_inject_text(const char *s)
{
    inject = s;
}

int serial_rx_byte(void)
{
    if (inject) {
        if (*inject) {
            unsigned char c = (unsigned char)*inject++;
            if (c == '\\' && *inject == 'n') {       /* "\n" in the argument = Enter */
                inject++;
                c = '\r';
            }
            return c;
        }
        inject = NULL;
    }
    if (!stdin_enabled || stdin_eof)
        return -1;
    unsigned char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n == 1)
        return c;
    if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
        stdin_eof = 1;
    return -1;
}
