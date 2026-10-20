/* MC68681 DUART model (register level, both channels, counter/timer). */
#ifndef DUART_H
#define DUART_H

#include <stdint.h>

/* Power-on / reset-button state (data sheet 2.4). */
void duart_reset(void);

/* Register access by register select RS1..RS4 (rs = 0..15 = A4..A1). */
uint8_t duart_read(unsigned rs);
void    duart_write(unsigned rs, uint8_t val);
uint8_t duart_peek(unsigned rs);        /* no side effects (dumps, disassembler) */

/* Advance the model to `now` (CPU clocks): completes transmit frames, moves
 * received bytes into the FIFO, runs the counter/timer, polls the console.
 * Called at the end of each scanline and before each register access. */
void duart_tick(uint64_t now);

/* IRQ output: 1 while (ISR & IMR) != 0 (the pin is active low; this is the
 * asserted state as INTSTAT.DUARTIRQ shows it). */
int duart_irq(void);

/* Output port pins as the OPR register holds them (OPn pin = ~OPR[n]). */
uint8_t duart_opr(void);

/* Statistics. */
extern unsigned long duart_tx_bytes[2];     /* delivered per channel */
extern unsigned long duart_rx_bytes;        /* received into channel A */
extern unsigned long duart_rx_overruns;     /* channel A OE events */
extern unsigned long duart_rx_dropped;      /* console bytes lost while RX A disabled */

#endif
