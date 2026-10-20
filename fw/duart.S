/* other-m68k BIOS: 68681 DUART bring-up and the stack-less POST serial output.
 *
 * Everything here runs before RAM is trusted: registers only, return addresses passed
 * in address registers. The DUART is channel A at the odd byte addresses in hw.inc;
 * only byte accesses are used.
 */
#include "hw.inc"
#include "duart.h"

	.text

/* duart_reset_init: program channel A for SERIAL_BAUD 8N1 (spec serial.init). Uses d0.
 * Returns with jmp (%a6).
 *
 * Order (MC68681 data sheet 4.2 "Register Description" and 4.2.7.2 command register):
 *   IMR = 0                      no DUART interrupt sources until the C code wants them
 *   CRA = reset MR pointer       MRA accesses start at MR1A regardless of history
 *   CRA = reset RX, reset TX     "ACR[7] should only be changed after the receiver(s)
 *                                and transmitter(s) have been issued software Rx and
 *                                Tx reset commands"; also puts them in a known state
 *   CRA = reset error status     clear stale RB/PE/FE/OE from before the reset
 *   ACR = BRG set 2              needed for the 38.4k CSR code 0xC
 *   MRA = MR1A, MRA = MR2A       first access hits MR1A, the pointer then stays at MR2A
 *   CSRA = SERIAL_CSR_VALUE      RX and TX clock select (0xCC = 38400 with ACR[7]=1)
 *   CRA = enable TX + RX         TxRDY becomes set; receiver starts looking for a start bit
 * A hardware RESET already leaves IMR=0 and the pointers at MR1, but the 68000 RESET
 * instruction path (`reset` command) and a warm restart cannot rely on that.
 */
	.globl	duart_reset_init
duart_reset_init:
	moveq	#0,%d0
	move.b	%d0,DUART_IMR_ADDR
	move.b	#DUART_CR_RESET_MR,DUART_CRA_ADDR
	move.b	#DUART_CR_RESET_RX,DUART_CRA_ADDR
	move.b	#DUART_CR_RESET_TX,DUART_CRA_ADDR
	move.b	#DUART_CR_RESET_ERR,DUART_CRA_ADDR
	move.b	#DUART_ACR_VALUE,DUART_ACR_ADDR
	move.b	#DUART_MR1_8N,DUART_MRA_ADDR
	move.b	#DUART_MR2_1STOP,DUART_MRA_ADDR
	move.b	#SERIAL_CSR_VALUE,DUART_CSRA_ADDR
	move.b	#DUART_CR_ENABLE,DUART_CRA_ADDR
	jmp	(%a6)

/* ---- POST routines: no stack, return through an address register ------------------ */

/* post_putc: d0.b = character. Waits for SRA.TXRDY (the transmit holding register is
 * empty), then writes TBA. Clobbers nothing else. Returns with jmp (%a6). */
	.globl	post_putc
post_putc:
1:	btst	#SRA_TXRDY_BIT,DUART_SRA_ADDR
	beq	1b
	move.b	%d0,DUART_TBA_ADDR
	jmp	(%a6)

/* post_puts: a5 = NUL-terminated string. Uses d0, a5, a6. Returns with jmp (%a4). */
	.globl	post_puts
post_puts:
	move.b	(%a5)+,%d0
	beq	1f
	lea	post_puts,%a6
	jmp	post_putc
1:	jmp	(%a4)

/* post_puthex: d3 = value (left aligned: the first nibble printed is bits 31..28),
 * d5.w = number of nibbles. Uses d0, d3, d5, a6. Returns with jmp (%a4). */
	.globl	post_puthex
post_puthex:
	rol.l	#4,%d3
	move.w	%d3,%d0
	andi.w	#0xF,%d0
	addi.b	#'0',%d0
	cmpi.b	#'9',%d0
	bls	1f
	addq.b	#7,%d0			/* 'A'..'F' */
1:	lea	2f,%a6
	jmp	post_putc
2:	subq.w	#1,%d5
	bne	post_puthex
	jmp	(%a4)

/* ---- C-callable ----------------------------------------------------------------- */

/* void delay_loops(uint32_t n): n iterations of an 18-clock loop fetched from ROM
 * (subq.l 8 + bhi taken 10); 1 us = 2/3 iteration at 12 MHz. Interrupts lengthen it. */
	.globl	delay_loops
delay_loops:
	move.l	4(%sp),%d0
1:	subq.l	#1,%d0
	bhi	1b
	rts
