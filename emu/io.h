/* CPLD2 registers, interrupt controller, soft switches. */
#ifndef IO_H
#define IO_H

#include <stdint.h>

void io_reset(void);

/* Register access by offset (0..14, even).  lanes: bit 1 = upper byte valid,
 * bit 0 = lower byte valid (byte accesses are lane-masked halves). */
uint16_t io_read16(uint32_t offset, int lanes);
void     io_write16(uint32_t offset, uint16_t val, int lanes);
uint16_t io_peek16(uint32_t offset);       /* no side effects */

/* Soft switch write at SWITCHES_BASE + offset (0,2,4,6). */
void io_switch_write(uint32_t offset);

/* Interrupt flag sources (set by video, ps2). */
void io_set_flag(uint16_t mask);           /* INTSTAT flag bits */
int  io_flag_set(uint16_t mask);
void io_set_nmi(int held);                 /* NMI button level */

/* Recompute IPL from flags, enables, the DUART IRQ pin and the NMI button
 * and tell the CPU. */
void io_update_ipl(void);
int  io_current_ipl(void);

/* Peripheral deliveries. */
void io_ps2_deliver(uint8_t byte, int frame_error);
int  io_ps2_ready(void);                   /* PS2RDY still set (byte unread) */

/* Current GPIO output register (LEDs for the window title, etc.).  The
 * register is write-only for the CPU; this is the emulator's view of the pins. */
uint16_t io_gpio(void);

/* PDM audio shifter (AUDIO register): HSYNC leading edge at `cycle`; apply the
 * rotations due before `until` (call before rendering audio); current register. */
void io_pdm_line_start(uint64_t cycle);
void io_pdm_advance(uint64_t until);
uint8_t io_pdm_reg(void);

/* Soft-switch requests pending for the next frame. */
int  io_pending_page(void);                /* 0 or 1 */
int  io_pending_pal(void);                 /* 0 = NTSC, 1 = PAL */

#endif
