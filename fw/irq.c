/* Interrupt service routines (autovectors 25..28) and user hook management.
 * NMI (vector 31) goes through the exception path and lands in the monitor. */
#include "bios.h"

volatile uint32_t ticks;
volatile uint32_t ticks_per_sec = 60;
irq_hook_fn irq_user_hook[8];
irq_hook_fn audio_cb;

void irq_update_inten(void)
{
    uint16_t en = INTEN_VBLANK | INTEN_PS2 | INTEN_SERRX;
    if (audio_cb || irq_user_hook[IRQ_HSYNC_LEVEL]) en |= INTEN_HSYNC;
    IO_INTEN = en;
}

void irq_init(void)
{
    IO_INTSTAT = INTSTAT_VBLANK | INTSTAT_HSYNC | INTSTAT_PS2ERR | INTSTAT_SERRXOVR;  /* W1C */
    irq_update_inten();
    __asm__ volatile ("move.w #0x2000,%sr");
}

void irq_reset_user(void)
{
    uint16_t sr = irq_disable();
    for (int i = 0; i < 8; i++) irq_user_hook[i] = 0;
    audio_cb = 0;
    irq_update_inten();
    irq_restore(sr);
}

__attribute__((interrupt_handler)) void irq_vblank(void)
{
    IO_INTSTAT = INTSTAT_VBLANK;
    ticks++;
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

__attribute__((interrupt_handler)) void irq_serrx(void)
{
    uint16_t st = IO_INTSTAT;
    if (st & INTSTAT_SERRXOVR) {
        IO_INTSTAT = INTSTAT_SERRXOVR;
        serial_rx_overruns++;
    }
    uint8_t b = (uint8_t)IO_SERDATA;          /* clears SERRX */
    queue_put(&serial_rx_q, b);
    if (irq_user_hook[IRQ_SERRX_LEVEL]) irq_user_hook[IRQ_SERRX_LEVEL]();
}

__attribute__((interrupt_handler)) void irq_hsync(void)
{
    IO_INTSTAT = INTSTAT_HSYNC;
    if (audio_cb) audio_cb();
    if (irq_user_hook[IRQ_HSYNC_LEVEL]) irq_user_hook[IRQ_HSYNC_LEVEL]();
}
