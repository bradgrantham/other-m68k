/* See joy.h and spec "joystick". The 68681 OP pins carry the complement of the OPR bits, so
 * OPR_RESET of the discharge bit drives the pin high = transistors on = capacitors shorted
 * (the reset state), OPR_SET releases them. The counter/timer runs free in timer mode from
 * X1/16 (230.4 kHz) while paddles are measured; reading CUR/CLR gives the live count, which
 * runs down, so elapsed = start - now modulo 2^16.
 *
 * A frame: the VBLANK handler shorts the capacitors. The lines of the paddles that had
 * charged rise; the DUART, which samples its inputs at 38.4 kHz, reports the rise through
 * the change interrupt 26..52 us later, and that handler releases the capacitors and notes
 * the counter: the release happens only once the DUART has registered the low-to-high step,
 * so the following fall is always a change it can see, however fast a paddle at zero
 * charges, and no time is spent waiting. If no line rose during a frame (no paddle charged
 * last frame, or none is plugged in) the next VBLANK handler releases directly. The change
 * interrupt stays unmasked throughout, so IMR is written only when measuring starts and
 * stops. */
#include "bios.h"
#include "joy.h"

#define CT_HZ (DUART_XTAL_HZ / 16)
#define FULL_COUNTS ((uint32_t)((uint64_t)CT_HZ * PADDLE_FULL_SCALE_US / 1000000u))   /* 2764 */

static volatile int paddles_on;
static volatile uint8_t phase;              /* 1 = shorted, waiting for the rise; 2 = charging */
static uint16_t start_count;
static volatile uint16_t pend_count[2];
static volatile uint8_t pend_done[2];
static struct paddles latest;

int joy_read(void)
{
    uint8_t ip = DUART_IP;                  /* a pressed switch pulls its line low */
    int m = 0;
    if (!(ip & JOY_IP_UP)) m |= JOY_UP;
    if (!(ip & JOY_IP_DOWN)) m |= JOY_DOWN;
    if (!(ip & JOY_IP_LEFT)) m |= JOY_LEFT;
    if (!(ip & JOY_IP_RIGHT)) m |= JOY_RIGHT;
    if (!(ip & JOY_IP_FIRE)) m |= JOY_FIRE;
    return m;
}

uint32_t joy_scale(uint32_t counts)
{
    uint32_t v = counts * (uint32_t)PADDLE_FULL / FULL_COUNTS;
    return v > PADDLE_FULL ? PADDLE_FULL : v;
}

/* the live count; the high byte is re-read so a low-byte wrap between the two reads is caught */
static uint16_t ct_read(void)
{
    uint8_t hi, lo, hi2;
    do { hi = DUART_CUR; lo = DUART_CLR; hi2 = DUART_CUR; } while (hi != hi2);
    return (uint16_t)((hi << 8) | lo);
}

int joy_paddles_on(void) { return paddles_on; }

void joy_paddles_set(int on)
{
    uint16_t sr = irq_disable();
    if (on && !paddles_on) {
        DUART_OPR_RESET = JOY_OP_DISCHARGE;         /* shorted while we set up */
        DUART_CTUR = 0xFF;
        DUART_CTLR = 0xFF;                          /* free-running 16-bit count */
        DUART_ACR = JOY_ACR_PADDLES;                /* timer X1/16, change interrupts on IP0/IP1 */
        (void)DUART_START_CTR;
        (void)DUART_IPCR;                           /* drop stale change bits */
        DUART_IMR = IMR_RXRDYA | IMR_IPC;
        pend_done[0] = pend_done[1] = 0;
        memset(&latest, 0, sizeof latest);
        latest.a = latest.b = PADDLE_FULL;
        phase = 1;                                  /* shorted; the next VBLANK releases */
        paddles_on = 1;
    } else if (!on && paddles_on) {
        paddles_on = 0;
        DUART_IMR = IMR_RXRDYA;
        DUART_ACR = SERIAL_ACR_VALUE;
        (void)DUART_STOP_CTR;
        DUART_OPR_RESET = JOY_OP_DISCHARGE;         /* capacitors shorted: up/down are switches again */
    }
    irq_restore(sr);
}

void joy_paddles_get(struct paddles *p)
{
    uint16_t sr = irq_disable();
    *p = latest;
    irq_restore(sr);
}

void joy_vblank(void)
{
    if (!paddles_on) return;
    /* the frame that just ended */
    latest.valid = (pend_done[0] ? PADDLE_A_VALID : 0) | (pend_done[1] ? PADDLE_B_VALID : 0);
    latest.a = pend_done[0] ? joy_scale(pend_count[0]) : PADDLE_FULL;
    latest.b = pend_done[1] ? joy_scale(pend_count[1]) : PADDLE_FULL;
    latest.frame = ticks;
    pend_done[0] = pend_done[1] = 0;
    if (phase == 2) {
        DUART_OPR_RESET = JOY_OP_DISCHARGE;     /* short the capacitors; the rise brings joy_ipc, which releases */
        phase = 1;
    } else {
        start_count = ct_read();                /* nothing rose last frame: already shorted, release now */
        DUART_OPR_SET = JOY_OP_DISCHARGE;
        phase = 2;
    }
}

void joy_ipc(uint8_t ipcr)
{
    if (!paddles_on) return;
    uint16_t now = ct_read();
    if (phase == 1) {                           /* a line rose: the DUART has seen the discharge */
        if ((ipcr & IPCR_DIP0 && ipcr & IPCR_IP0) || (ipcr & IPCR_DIP1 && ipcr & IPCR_IP1)) {
            start_count = now;
            DUART_OPR_SET = JOY_OP_DISCHARGE;   /* release: charging */
            phase = 2;
        }
        return;
    }
    for (int i = 0; i < 2; i++) {
        uint8_t delta = (uint8_t)(IPCR_DIP0 << i), level = (uint8_t)(IPCR_IP0 << i);
        if ((ipcr & delta) && !(ipcr & level) && !pend_done[i]) {    /* this line fell: end of charge */
            pend_count[i] = (uint16_t)(start_count - now);
            pend_done[i] = 1;
        }
    }
}
