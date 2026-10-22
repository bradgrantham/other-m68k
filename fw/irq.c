/* Interrupt service routines (autovectors 25..27; 28 = HSYNC is in irq_hsync.S) and user hook management.
 * NMI (vector 31) goes through the exception path and lands in the monitor. */
#include "bios.h"
#include "clock.h"

volatile uint32_t ticks;
volatile uint32_t ticks_per_sec = 60;
irq_hook_fn irq_user_hook[8];
irq_hook_fn audio_cb;
uint16_t inten_shadow;      /* INTEN is write-only (reads 0); every write goes through here */

/* INTEN.HSYNC is on while there is a reason for the line interrupt: an audio callback, a
 * level-4 user hook, or an audio buffer playing or queued. irq_hsync.S clears the bit itself
 * (through inten_shadow) when the stream runs out of buffers on the fast path. */
void irq_update_inten(void)
{
    uint16_t en = INTEN_VBLANK | INTEN_PS2 | INTEN_DUART;
    audio_update_mode();
    if (audio_cb || irq_user_hook[IRQ_HSYNC_LEVEL] || audio_active())
        en |= INTEN_HSYNC;
    inten_shadow = en;
    IO_INTEN = en;
}

void irq_init(void)
{
    IO_INTSTAT = INTSTAT_VBLANK | INTSTAT_HSYNC | INTSTAT_PS2ERR;  /* W1C flags */
    irq_update_inten();
    __asm__ volatile ("move.w #0x2000,%sr");
}

void irq_reset_user(void)
{
    uint16_t sr = irq_disable();
    for (int i = 0; i < 8; i++) irq_user_hook[i] = 0;
    audio_cb = 0;
    audio_reset_vector();                   /* the program may have replaced vector 28 */
    irq_update_inten();
    irq_restore(sr);
}

__attribute__((interrupt_handler)) void irq_vblank(void)
{
    IO_INTSTAT = INTSTAT_VBLANK;
    ticks++;
    clock_tick();
    if (irq_user_hook[IRQ_VBLANK_LEVEL]) irq_user_hook[IRQ_VBLANK_LEVEL]();
}

__attribute__((interrupt_handler)) void irq_ps2(void)
{
    uint16_t st = IO_INTSTAT;
    uint8_t b = (uint8_t)IO_PS2DATA;          /* clears PS2RDY */
    if (st & INTSTAT_PS2ERR) {
        IO_INTSTAT = INTSTAT_PS2ERR;
        ps2_errors++;
    } else {
        ps2_feed(b);
    }
    if (irq_user_hook[IRQ_PS2_LEVEL]) irq_user_hook[IRQ_PS2_LEVEL]();
}

/* Level 3: the 68681's IRQ pin (level sensitive, gated by INTEN.DUART). Only RXRDYA is
 * unmasked in IMR, so draining the 3-byte RX FIFO is what releases the pin; the ISR
 * register is read first as the spec asks and so that a future source can be added.
 * SRA.OE means a 4th character arrived while the FIFO was full and the shift register's
 * character was lost; it stays set until a "reset error status" command. */
__attribute__((interrupt_handler)) void irq_duart(void)
{
    uint8_t isr = DUART_ISR;
    if (isr & ISR_RXRDYA) {
        uint8_t st;
        while ((st = DUART_SRA) & SRA_RXRDY) {
            if (st & SRA_OE) {
                serial_rx_overruns++;
                DUART_CRA = DUART_CR_RESET_ERR;
            }
            queue_put(&serial_rx_q, DUART_RBA);
        }
    }
    if (irq_user_hook[IRQ_DUART_LEVEL]) irq_user_hook[IRQ_DUART_LEVEL]();
}

/* Level 4 (HSYNC) is irq_hsync.S: the PDM sample player, with the audio callback and the
 * level-4 hook called from its slow path. */
