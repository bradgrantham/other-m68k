/* other-m68k BIOS: bit-banged serial transmitter (115200 8N1 on GPIO.SERTX) and the
 * stack-less POST serial routines. Timing is in serbit.inc. */
#include "hw.inc"
#include "serbit.inc"

	.text

/* ---- POST routines: no stack, return through an address register ------------------ */

/* post_putc: d0.b = character. Uses d0,d1,d2; d7 = GPIO shadow (SERTX restored to 1 on
 * exit); a0 = IO_GPIO_ADDR. Returns with jmp (%a6). */
	.globl	post_putc
post_putc:
	bclr	#GPIO_SERTX_BIT,%d7
	SERFRAME
	SERBYTE
	bset	#GPIO_SERTX_BIT,%d7
	jmp	(%a6)

/* post_puts: a5 = NUL-terminated string. Uses d0-d2, a5, a6. Returns with jmp (%a4). */
	.globl	post_puts
post_puts:
	move.b	(%a5)+,%d0
	beq	1f
	lea	post_puts,%a6
	jmp	post_putc
1:	jmp	(%a4)

/* post_puthex: d3 = value (left aligned: the first nibble printed is bits 31..28),
 * d5.w = number of nibbles. Uses d0-d3, d5, a6. Returns with jmp (%a4). */
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

/* void serial_tx_byte(int c): one byte on GPIO.SERTX with interrupts masked (~87 us). */
	.globl	serial_tx_byte
serial_tx_byte:
	movem.l	%d2/%d7,-(%sp)
	move.w	%sr,-(%sp)
	ori.w	#0x0700,%sr
	move.w	gpio_shadow,%d7
	bclr	#GPIO_SERTX_BIT,%d7
	lea	IO_GPIO_ADDR,%a0
	move.l	14(%sp),%d0		/* 4 return + 8 movem + 2 sr */
	SERFRAME
	SERBYTE
	move.w	(%sp)+,%sr
	movem.l	(%sp)+,%d2/%d7
	rts

/* void delay_loops(uint32_t n): n iterations of an 18-clock loop fetched from ROM
 * (subq.l 8 + bhi taken 10); 1 us = 2/3 iteration at 12 MHz. Interrupts lengthen it. */
	.globl	delay_loops
delay_loops:
	move.l	4(%sp),%d0
1:	subq.l	#1,%d0
	bhi	1b
	rts
