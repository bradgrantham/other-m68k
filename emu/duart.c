/* MC68681 DUART model.
 *
 * Register-level model of the 68681 as the firmware sees it: per channel the
 * MR1/MR2 pair behind the mode-register pointer, CSR, CR commands, SR, a
 * 3-deep receive FIFO with the receive shift register behind it, and the
 * transmit holding + shift registers with time-based completion; shared ISR,
 * IMR, IVR, ACR, the 16-bit counter/timer in both modes, IP/IPCR, OPCR and
 * OPR.  Everything is driven by the CPU cycle counter: a transmitted byte is
 * delivered to the console when its stop bit ends, a console byte takes one
 * frame time to arrive, the counter/timer counts the 3.6864 MHz crystal
 * converted to CPU clocks with an exact remainder.
 *
 * Channel A is the serial console (serial.c: stdout/--serial-log, stdin and
 * --serial-type).  Channel B transmits into --duart-b-log or nowhere and
 * never receives.  Not modelled: parity/framing/break detection (PE, FE, RB
 * and the change-in-break interrupts stay 0), CTS/RTS, loopback and
 * multidrop modes, the input-port change detector (IPC stays 0), 1X/16X clock
 * outputs.  Bit rates come from the data-sheet CSR table: code 0xC is 38400
 * in set 1 (ACR[7] = 0) and 19200 in set 2 (ACR[7] = 1).
 */
#include <stdio.h>
#include <string.h>

#include "hw.h"
#include "emu_map.h"
#include "m68k.h"
#include "emu.h"
#include "io.h"
#include "duart.h"
#include "serial.h"

unsigned long duart_tx_bytes[2];
unsigned long duart_rx_bytes;
unsigned long duart_rx_overruns;
unsigned long duart_rx_dropped;

#define RS(offset) ((offset) >> 1)          /* byte offset (odd) -> RS4..RS1 */
#define RS_CHAN_B RS(DUART_MRB_OFFSET)      /* channel B registers are A + 8 */

/* Command register fields (data sheet 4.2.7). */
#define CR_RX_ENABLE   0x01
#define CR_RX_DISABLE  0x02
#define CR_TX_ENABLE   0x04
#define CR_TX_DISABLE  0x08
#define CR_MISC(v)     (((v) >> 4) & 7)
#define CR_MISC_RESET_MR_PTR  1
#define CR_MISC_RESET_RX      2
#define CR_MISC_RESET_TX      3
#define CR_MISC_RESET_ERR     4
#define CR_MISC_RESET_BREAK   5
#define CR_MISC_START_BREAK   6
#define CR_MISC_STOP_BREAK    7

#define MR1_BITS_MASK   0x03    /* 00 = 5 .. 11 = 8 bits per character */
#define MR1_PARITY_MODE 0x18    /* 00/01 = with/force parity, 10 = none, 11 = multidrop */
#define MR1_RX_FFULL    0x40    /* ISR RXRDY bit reports FFULL instead */
#define MR2_STOP_MASK   0x0F    /* >= 0x8: two stop bits */

#define ACR_BRG_SET2    0x80
#define ACR_CT_TIMER    0x40
#define ACR_CT_SRC(v)   (((v) >> 4) & 3)

#define CSR_CODE_TIMER  0xD
#define CSR_CODE_EXT16  0xE
#define CSR_CODE_EXT1   0xF

#define FIFO_DEPTH 3
#define IVR_RESET  0x0F
#define IP_VALUE   0xFF         /* IP0..IP5 unconnected (pulled up), bits 6..7 read 1 */

struct chan {
    uint8_t mr1, mr2;
    int mr_ptr;                 /* 0: next MR access is MR1, 1: MR2 */
    uint8_t csr;
    int rx_en, tx_en;
    int oe;                     /* SR.OE, cleared by "reset error status" */
    uint8_t fifo[FIFO_DEPTH];
    int fifo_n;
    int shift_valid;            /* byte waiting in the receive shift register */
    uint8_t shift;
    int thr_valid;              /* transmit holding register occupied */
    uint8_t thr;
    int tsr_busy;               /* transmit shift register sending */
    uint8_t tsr;
    uint64_t tsr_done;          /* cycle at which the stop bit ends */
    int baud_warned;
};

static struct chan ch[2];
static uint8_t imr, ivr, acr, opcr, opr;
static int ctr_flag;            /* ISR.CTR */

/* Console byte on its way into channel A (one frame time on the wire). */
static int rx_inflight;
static uint8_t rx_inflight_byte;
static uint64_t rx_inflight_done;

/* Counter/timer. */
static struct {
    uint8_t ctur, ctlr;
    int running;                /* counter mode: started */
    int phase;                  /* timer mode: ISR.CTR on every second terminal count */
    uint64_t next;              /* cycle of the next terminal count */
    uint64_t rem;               /* remainder of the last division, in 1/src_hz units */
    uint64_t start;             /* counter mode: cycle of the START command */
    unsigned start_preload;     /* counter mode: value loaded at START */
    unsigned stopped_count;     /* counter mode: count when stopped */
} ct;

static uint64_t last_now;
static int last_irq = -1;

/* ---- bit rates --------------------------------------------------------- */

/* Baud rates (x2, so 134.5 is exact) from the CSR code: set 1 (ACR[7] = 0)
 * and set 2 (ACR[7] = 1), data sheet Table 4-5. */
static const unsigned baud2_set1[13] = {
    100, 220, 269, 400, 600, 1200, 2400, 2100, 4800, 9600, 14400, 19200, 76800 };
static const unsigned baud2_set2[13] = {
    150, 220, 269, 300, 600, 1200, 2400, 4000, 4800, 9600, 3600, 19200, 38400 };

static unsigned ct_preload(void)
{
    unsigned n = ((unsigned)ct.ctur << 8) | ct.ctlr;
    return n ? n : 0x10000;     /* a 16-bit down counter: 0 means 65536 */
}

/* Counter/timer clock source in Hz (0 = none / external). */
static uint64_t ct_src_hz(void);

/* Bit rate x2 for a CSR nibble, 0 if unknown (external clock). */
static unsigned code_baud2(unsigned code)
{
    if (code <= 0xC)
        return (acr & ACR_BRG_SET2) ? baud2_set2[code] : baud2_set1[code];
    if (code == CSR_CODE_TIMER) {
        /* 16X clock from the timer: square wave of period 2N source clocks. */
        uint64_t src = (acr & ACR_CT_TIMER) ? ct_src_hz() : 0;
        if (src)
            return (unsigned)((src * 2) / (2ULL * ct_preload() * 16));
    }
    return 0;
}

/* CPU clocks per bit for a CSR nibble; external clocks are taken as the
 * console rate since nothing else is connected. */
static uint64_t bit_cycles(unsigned code)
{
    unsigned b2 = code_baud2(code);
    if (b2 == 0)
        b2 = 2 * SERIAL_BAUD;
    return (2ULL * CPU_CLOCK_HZ + b2 / 2) / b2;
}

/* Bits per frame from MR1/MR2: start + data + parity + stop. */
static unsigned frame_bits(const struct chan *c)
{
    unsigned bits = 1 + 5 + (c->mr1 & MR1_BITS_MASK);
    if ((c->mr1 & MR1_PARITY_MODE) != 0x10)
        bits++;
    bits += (c->mr2 & MR2_STOP_MASK) >= 0x8 ? 2 : 1;
    return bits;
}

static uint64_t tx_frame_cycles(const struct chan *c)
{
    return frame_bits(c) * bit_cycles(c->csr & 0xF);
}

/* Console frames arrive at the console's rate (SERIAL_BAUD, 8N1) whatever
 * the receiver is set to; a mismatch is reported once. */
static uint64_t console_frame_cycles(void)
{
    return (10ULL * CPU_CLOCK_HZ + SERIAL_BAUD - 1) / SERIAL_BAUD;
}

static void check_baud(struct chan *c, int which)
{
    if (which != 0 || c->baud_warned)
        return;
    unsigned rx2 = code_baud2(c->csr >> 4), tx2 = code_baud2(c->csr & 0xF);
    if ((rx2 && rx2 != 2 * SERIAL_BAUD) || (tx2 && tx2 != 2 * SERIAL_BAUD)) {
        fprintf(stderr, "m68k-emu: duart: channel A enabled at RX %u.%u / TX %u.%u baud "
                "(CSRA %02X, ACR[7] = %d); the console runs at %u\n",
                rx2 / 2, (rx2 & 1) * 5, tx2 / 2, (tx2 & 1) * 5, c->csr,
                (acr & ACR_BRG_SET2) ? 1 : 0, (unsigned)SERIAL_BAUD);
        c->baud_warned = 1;
    }
}

/* ---- status and interrupts --------------------------------------------- */

static uint8_t chan_sr(const struct chan *c)
{
    uint8_t sr = 0;
    if (c->fifo_n > 0)
        sr |= SRA_RXRDY;
    if (c->fifo_n == FIFO_DEPTH)
        sr |= SRA_FFULL;
    if (c->tx_en) {
        if (!c->thr_valid)
            sr |= SRA_TXRDY;
        if (!c->thr_valid && !c->tsr_busy)
            sr |= SRA_TXEMT;
    }
    if (c->oe)
        sr |= SRA_OE;
    return sr;
}

static uint8_t isr_value(void)
{
    uint8_t isr = 0;
    uint8_t sra = chan_sr(&ch[0]), srb = chan_sr(&ch[1]);
    if (sra & SRA_TXRDY)
        isr |= ISR_TXRDYA;
    if (sra & ((ch[0].mr1 & MR1_RX_FFULL) ? SRA_FFULL : SRA_RXRDY))
        isr |= ISR_RXRDYA;
    if (srb & SRB_TXRDY)
        isr |= ISR_TXRDYB;
    if (srb & ((ch[1].mr1 & MR1_RX_FFULL) ? SRB_FFULL : SRB_RXRDY))
        isr |= ISR_RXRDYB;
    if (ctr_flag)
        isr |= ISR_CTR;
    return isr;
}

int duart_irq(void)
{
    return (isr_value() & imr) != 0;
}

uint8_t duart_opr(void)
{
    return opr;
}

static void irq_update(void)
{
    int irq = duart_irq();
    if (irq != last_irq) {
        last_irq = irq;
        io_update_ipl();
    }
}

/* ---- transmitter --------------------------------------------------------- */

static void deliver(int which, uint8_t b)
{
    duart_tx_bytes[which]++;
    if (which == 0)
        serial_tx_byte(b);
    else
        serial_b_tx_byte(b);
}

static void tx_advance(struct chan *c, int which, uint64_t now)
{
    while (c->tsr_busy && c->tsr_done <= now) {
        deliver(which, c->tsr);
        c->tsr_busy = 0;
        if (c->thr_valid) {
            c->tsr = c->thr;
            c->thr_valid = 0;
            c->tsr_busy = 1;
            c->tsr_done += tx_frame_cycles(c);
        }
    }
}

static void tx_load(struct chan *c, uint8_t b, uint64_t now)
{
    if (!c->tx_en)
        return;                 /* "will not be transmitted" */
    if (!c->tsr_busy) {
        c->tsr = b;
        c->tsr_busy = 1;
        c->tsr_done = now + tx_frame_cycles(c);
    } else {
        c->thr = b;             /* TXRDY drops until the shift register takes it */
        c->thr_valid = 1;
    }
}

/* ---- receiver ------------------------------------------------------------ */

static void fifo_push(struct chan *c, uint8_t b)
{
    c->fifo[c->fifo_n++] = b;
}

/* A frame has completed on the channel's RX line. */
static void rx_receive(struct chan *c, uint8_t b)
{
    if (!c->rx_en) {
        duart_rx_dropped++;
        return;
    }
    duart_rx_bytes++;
    if (c->fifo_n < FIFO_DEPTH) {
        fifo_push(c, b);
    } else {
        if (c->shift_valid) {
            c->oe = 1;          /* the waiting character is lost */
            duart_rx_overruns++;
        }
        c->shift = b;
        c->shift_valid = 1;
    }
}

static uint8_t rx_pop(struct chan *c)
{
    if (c->fifo_n == 0)
        return 0;               /* undefined on the chip */
    uint8_t b = c->fifo[0];
    memmove(c->fifo, c->fifo + 1, (size_t)(c->fifo_n - 1));
    c->fifo_n--;
    if (c->shift_valid) {
        fifo_push(c, c->shift);
        c->shift_valid = 0;
    }
    return b;
}

static void rx_console_advance(uint64_t now)
{
    if (rx_inflight && rx_inflight_done <= now) {
        rx_receive(&ch[0], rx_inflight_byte);
        rx_inflight = 0;
        if (opts.io_trace)
            fprintf(stderr, "m68k-emu: duart: %10llu RX A byte %02X%s\n", (unsigned long long)rx_inflight_done,
                    rx_inflight_byte, ch[0].rx_en ? (ch[0].shift_valid ? " (FIFO full, in shift register)" : "")
                                                  : " (receiver disabled, lost)");
    }
    if (!rx_inflight) {
        int b = serial_rx_byte();
        if (b >= 0) {
            rx_inflight = 1;
            rx_inflight_byte = (uint8_t)b;
            rx_inflight_done = now + console_frame_cycles();
        }
    }
}

/* ---- counter/timer -------------------------------------------------------- */

static uint64_t ct_src_hz(void)
{
    unsigned src = ACR_CT_SRC(acr);
    if (acr & ACR_CT_TIMER) {
        if (src == 2) return DUART_XTAL_HZ;             /* X1/CLK */
        if (src == 3) return DUART_XTAL_HZ / 16;        /* X1/CLK / 16 */
        return 0;                                       /* IP2: nothing connected */
    }
    if (src == 3) return DUART_XTAL_HZ / 16;
    if (src == 1) return (uint64_t)code_baud2(ch[0].csr & 0xF) / 2;  /* TxCA 1X */
    if (src == 2) return (uint64_t)code_baud2(ch[1].csr & 0xF) / 2;  /* TxCB 1X */
    return 0;
}

/* Advance ct.next by n source clocks, keeping the division remainder. */
static void ct_advance(unsigned n, uint64_t src_hz)
{
    uint64_t num = (uint64_t)n * CPU_CLOCK_HZ + ct.rem;
    ct.next += num / src_hz;
    ct.rem = num % src_hz;
}

static int ct_active(void)
{
    return (acr & ACR_CT_TIMER) ? 1 : ct.running;
}

static void ct_restart(uint64_t now, unsigned preload)
{
    uint64_t src = ct_src_hz();
    ct.next = now;
    ct.rem = 0;
    if (src)
        ct_advance(preload, src);
}

static void ct_run(uint64_t now)
{
    uint64_t src = ct_src_hz();
    if (!src || !ct_active())
        return;
    while (ct.next <= now) {
        if (acr & ACR_CT_TIMER) {
            ct.phase ^= 1;
            if (!ct.phase)
                ctr_flag = 1;
            ct_advance(ct_preload(), src);
        } else {
            ctr_flag = 1;       /* terminal count; roll over and keep counting */
            ct_advance(0x10000, src);
        }
    }
}

static unsigned ct_count(uint64_t now)
{
    if (!(acr & ACR_CT_TIMER) && !ct.running)
        return ct.stopped_count;
    uint64_t src = ct_src_hz();
    if (!src)
        return ct.start_preload & 0xFFFF;
    uint64_t ticks = ((now - ct.start) * src) / CPU_CLOCK_HZ;
    return (unsigned)((ct.start_preload - ticks) & 0xFFFF);
}

static void ct_start_cmd(uint64_t now)
{
    unsigned preload = ct_preload();
    if (acr & ACR_CT_TIMER) {
        ct.phase = 0;           /* output inverted, countdown restarted */
    } else {
        ct.running = 1;
        ct.start_preload = preload;
    }
    ct.start = now;
    ct_restart(now, preload);
}

static void ct_stop_cmd(uint64_t now)
{
    if (!(acr & ACR_CT_TIMER) && ct.running) {
        ct.stopped_count = ct_count(now);
        ct.running = 0;
    }
    ctr_flag = 0;
}

/* ---- sync ---------------------------------------------------------------- */

void duart_tick(uint64_t now)
{
    if (now < last_now)
        now = last_now;
    last_now = now;
    tx_advance(&ch[0], 0, now);
    tx_advance(&ch[1], 1, now);
    rx_console_advance(now);
    ct_run(now);
    irq_update();
}

/* ---- reset --------------------------------------------------------------- */

static void chan_reset(struct chan *c)
{
    memset(c, 0, sizeof *c);
}

void duart_reset(void)
{
    chan_reset(&ch[0]);
    chan_reset(&ch[1]);
    imr = 0;
    ivr = IVR_RESET;
    acr = 0;
    opcr = 0;
    opr = 0;
    ctr_flag = 0;
    rx_inflight = 0;
    memset(&ct, 0, sizeof ct);
    last_irq = -1;
    irq_update();
}

/* ---- register access ------------------------------------------------------ */

static const char *reg_name(unsigned rs, int write)
{
    static char buf[16];
    const char *n = emu_duart_reg_names[rs & 0xF];
    const char *slash = strchr(n, '/');
    if (!slash)
        return n;
    if (write)
        return slash + 1;
    size_t len = (size_t)(slash - n);
    if (len >= sizeof buf)
        len = sizeof buf - 1;
    memcpy(buf, n, len);
    buf[len] = 0;
    return buf;
}

static void trace(unsigned rs, int write, uint8_t v)
{
    fprintf(stderr, "m68k-emu: duart: %10llu PC=%06X %s %-9s %02X\n", (unsigned long long)emu_cycles(),
            m68k_get_reg(NULL, M68K_REG_PPC) & 0xFFFFFF, write ? "wr" : "rd", reg_name(rs, write), v);
}

static void warn_once(int *flag, const char *what, unsigned rs)
{
    if (*flag)
        return;
    *flag = 1;
    fprintf(stderr, "m68k-emu: duart: %s (RS %u, PC=%06X); further occurrences not shown\n", what, rs,
            m68k_get_reg(NULL, M68K_REG_PPC) & 0xFFFFFF);
}

static void command(struct chan *c, int which, uint8_t v, uint64_t now)
{
    switch (CR_MISC(v)) {
    case CR_MISC_RESET_MR_PTR:
        c->mr_ptr = 0;
        break;
    case CR_MISC_RESET_RX:
        c->rx_en = 0;
        c->fifo_n = 0;
        c->shift_valid = 0;
        break;
    case CR_MISC_RESET_TX:
        c->tx_en = 0;
        c->thr_valid = 0;
        c->tsr_busy = 0;
        break;
    case CR_MISC_RESET_ERR:
        c->oe = 0;
        break;
    case CR_MISC_RESET_BREAK:
    case CR_MISC_START_BREAK:
    case CR_MISC_STOP_BREAK:
        break;                  /* break detection/generation not modelled */
    default:
        break;
    }
    if (v & CR_RX_ENABLE) {
        c->rx_en = 1;
        check_baud(c, which);
    } else if (v & CR_RX_DISABLE) {
        c->rx_en = 0;
    }
    if (v & CR_TX_ENABLE) {
        c->tx_en = 1;
        check_baud(c, which);
    } else if (v & CR_TX_DISABLE) {
        c->tx_en = 0;
        c->thr_valid = 0;       /* a character in flight still completes */
    }
    (void)now;
}

uint8_t duart_peek(unsigned rs)
{
    rs &= 0xF;
    struct chan *c = &ch[(rs >= RS_CHAN_B) ? 1 : 0];
    switch (rs & 7) {
    case RS(DUART_MRA_OFFSET):   return c->mr_ptr ? c->mr2 : c->mr1;
    case RS(DUART_SRA_OFFSET):   return chan_sr(c);
    case RS(DUART_CRA_OFFSET):   return 0xFF;        /* do not access */
    case RS(DUART_RBA_OFFSET):   return c->fifo_n ? c->fifo[0] : 0;
    }
    switch (rs) {
    case RS(DUART_IPCR_OFFSET):  return IP_VALUE & 0x0F;
    case RS(DUART_ISR_OFFSET):   return isr_value();
    case RS(DUART_CUR_OFFSET):   return (uint8_t)(ct_count(last_now) >> 8);
    case RS(DUART_CLR_OFFSET):   return (uint8_t)ct_count(last_now);
    case RS(DUART_IVR_OFFSET):   return ivr;
    case RS(DUART_IP_OFFSET):    return IP_VALUE;
    case RS(DUART_START_CTR_OFFSET):
    case RS(DUART_STOP_CTR_OFFSET):
        return 0xFF;            /* address-triggered commands: data undefined */
    }
    return 0xFF;
}

uint8_t duart_read(unsigned rs)
{
    static int warned_dna;
    uint64_t now = emu_cycles();
    duart_tick(now);
    rs &= 0xF;
    struct chan *c = &ch[(rs >= RS_CHAN_B) ? 1 : 0];
    uint8_t v;

    switch (rs) {
    case RS(DUART_MRA_OFFSET):
    case RS(DUART_MRB_OFFSET):
        v = c->mr_ptr ? c->mr2 : c->mr1;
        c->mr_ptr = 1;
        break;
    case RS(DUART_SRA_OFFSET):
    case RS(DUART_SRB_OFFSET):
        v = chan_sr(c);
        break;
    case RS(DUART_CRA_OFFSET):
    case RS(DUART_CRB_OFFSET):
        warn_once(&warned_dna, "read of a \"do not access\" register", rs);
        v = 0xFF;
        break;
    case RS(DUART_RBA_OFFSET):
    case RS(DUART_RBB_OFFSET):
        v = rx_pop(c);
        break;
    case RS(DUART_IPCR_OFFSET):
        v = IP_VALUE & 0x0F;    /* no changes detected: delta bits 0, levels high */
        break;
    case RS(DUART_ISR_OFFSET):
        v = isr_value();
        break;
    case RS(DUART_CUR_OFFSET):
        v = (uint8_t)(ct_count(now) >> 8);
        break;
    case RS(DUART_CLR_OFFSET):
        v = (uint8_t)ct_count(now);
        break;
    case RS(DUART_IVR_OFFSET):
        v = ivr;
        break;
    case RS(DUART_IP_OFFSET):
        v = IP_VALUE;
        break;
    case RS(DUART_START_CTR_OFFSET):
        ct_start_cmd(now);
        v = 0xFF;
        break;
    case RS(DUART_STOP_CTR_OFFSET):
        ct_stop_cmd(now);
        v = 0xFF;
        break;
    default:
        v = 0xFF;
        break;
    }
    if (opts.io_trace)
        trace(rs, 0, v);
    irq_update();
    return v;
}

void duart_write(unsigned rs, uint8_t v)
{
    uint64_t now = emu_cycles();
    duart_tick(now);
    rs &= 0xF;
    struct chan *c = &ch[(rs >= RS_CHAN_B) ? 1 : 0];
    int which = (rs >= RS_CHAN_B) ? 1 : 0;
    if (opts.io_trace)
        trace(rs, 1, v);

    switch (rs) {
    case RS(DUART_MRA_OFFSET):
    case RS(DUART_MRB_OFFSET):
        if (c->mr_ptr)
            c->mr2 = v;
        else
            c->mr1 = v;
        c->mr_ptr = 1;
        break;
    case RS(DUART_CSRA_OFFSET):
    case RS(DUART_CSRB_OFFSET):
        c->csr = v;
        break;
    case RS(DUART_CRA_OFFSET):
    case RS(DUART_CRB_OFFSET):
        command(c, which, v, now);
        break;
    case RS(DUART_TBA_OFFSET):
    case RS(DUART_TBB_OFFSET):
        tx_load(c, v, now);
        break;
    case RS(DUART_ACR_OFFSET): {
        uint8_t old = acr;
        acr = v;
        if ((old ^ v) & 0x70) {
            /* Mode or source changed: re-base the countdown from now. */
            ct.running = 0;
            ct.phase = 0;
            ct_restart(now, ct_preload());
        }
        break;
    }
    case RS(DUART_IMR_OFFSET):
        imr = v;
        break;
    case RS(DUART_CTUR_OFFSET):
        ct.ctur = v;
        break;
    case RS(DUART_CTLR_OFFSET):
        ct.ctlr = v;
        break;
    case RS(DUART_IVR_OFFSET):
        ivr = v;
        break;
    case RS(DUART_OPCR_OFFSET):
        opcr = v;
        break;
    case RS(DUART_OPR_SET_OFFSET):
        opr |= v;
        break;
    case RS(DUART_OPR_RESET_OFFSET):
        opr &= (uint8_t)~v;
        break;
    default:
        break;
    }
    irq_update();
}
