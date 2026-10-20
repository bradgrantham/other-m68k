/* other-m68k BIOS power-on self test. Runs before any RAM is trusted: registers only,
 * no stack, serial output through the stack-less routines in sertx.S.
 *
 * Register discipline (POST only):
 *   d7     GPIO shadow (LEDs, idle lines)         a0  IO_GPIO_ADDR (serial macro)
 *   d0-d2  serial scratch                         a4  return address for puts/puthex
 *   d3,d5  puthex value / nibble count            a5  string pointer
 *   d6     16-bit ROM word sum (0 = good)         a6  return address for putc
 *   d4     RAM test pattern                       a1  RAM test pointer
 *   d1     RAM test loop counter                  a2  pattern table pointer
 *                                                 a3  return address for verify_longs
 * LED codes: LED0 = POST started / ROM checksum; LED0+LED1 = RAM test;
 *            LED1 = C initialisation (set by bios_main); both off = monitor idle;
 *            LED0/LED1 alternating at ~2 Hz forever = RAM test failed.
 * Exit: jmp post_return (crt0.S) with d6/d7 as above.
 */
#include "hw.inc"

	.text
	.globl	post_run
post_run:
	lea	IO_GPIO_ADDR,%a0
	lea	str_hello,%a5
	lea	1f,%a4
	jmp	post_puts
1:
/* ---- ROM checksum: 16-bit sum of all 65536 words must be 0 (mkrom.py) ----------- */
	lea	ROM_BASE,%a1
	moveq	#0,%d6
	move.w	#(ROM_SIZE / 2) - 1,%d1
2:	add.w	(%a1)+,%d6
	dbf	%d1,2b
	lea	str_romsum,%a5
	lea	3f,%a4
	jmp	post_puts
3:	move.l	%d6,%d3
	swap	%d3			/* 16-bit value: first nibble is bits 15..12 */
	moveq	#4,%d5
	lea	4f,%a4
	jmp	post_puthex
4:	lea	str_ok,%a5
	tst.w	%d6
	beq	5f
	lea	str_bad,%a5
5:	lea	6f,%a4
	jmp	post_puts
6:
/* ---- RAM test, all of RAM_BASE..RAM_END --------------------------------------------- */
	bset	#GPIO_LED1_BIT,%d7
	move.w	%d7,(%a0)
	lea	str_ram,%a5
	lea	7f,%a4
	jmp	post_puts
7:
	/* pass 1: address in address, longs */
	lea	RAM_BASE,%a1
	move.w	#(RAM_SIZE / 16) - 1,%d1
10:	move.l	%a1,(%a1)+
	move.l	%a1,(%a1)+
	move.l	%a1,(%a1)+
	move.l	%a1,(%a1)+
	dbf	%d1,10b
	lea	RAM_BASE,%a1
	move.w	#(RAM_SIZE / 4) - 1,%d1
11:	move.l	(%a1),%d0
	cmp.l	%a1,%d0
	bne	fail_addr
	addq.l	#4,%a1
	dbf	%d1,11b
	moveq	#'.',%d0
	lea	12f,%a6
	jmp	post_putc
12:
	/* pass 2: four long patterns */
	lea	patterns,%a2
	moveq	#3,%d5
13:	move.l	(%a2)+,%d4
	lea	RAM_BASE,%a1
	move.w	#(RAM_SIZE / 16) - 1,%d1
15:	move.l	%d4,(%a1)+
	move.l	%d4,(%a1)+
	move.l	%d4,(%a1)+
	move.l	%d4,(%a1)+
	dbf	%d1,15b
	lea	16f,%a3
	jmp	verify_longs
16:	dbf	%d5,13b

30:	/* pass 3: byte lanes. 0x55 on even (upper, D15..D8) bytes, 0xAA on odd, then inverse. */
	lea	RAM_BASE,%a1
	move.w	#(RAM_SIZE / 8) - 1,%d1
31:	move.b	#0x55,(%a1)+		/* 8 bytes per iteration */
	move.b	#0xAA,(%a1)+
	move.b	#0x55,(%a1)+
	move.b	#0xAA,(%a1)+
	move.b	#0x55,(%a1)+
	move.b	#0xAA,(%a1)+
	move.b	#0x55,(%a1)+
	move.b	#0xAA,(%a1)+
	dbf	%d1,31b
	move.l	#0x55AA55AA,%d4
	lea	32f,%a3
	jmp	verify_longs
32:	lea	RAM_BASE,%a1
	move.w	#(RAM_SIZE / 8) - 1,%d1
33:	move.b	#0xAA,(%a1)+
	move.b	#0x55,(%a1)+
	move.b	#0xAA,(%a1)+
	move.b	#0x55,(%a1)+
	move.b	#0xAA,(%a1)+
	move.b	#0x55,(%a1)+
	move.b	#0xAA,(%a1)+
	move.b	#0x55,(%a1)+
	dbf	%d1,33b
	move.l	#0xAA55AA55,%d4
	lea	34f,%a3
	jmp	verify_longs
34:
	lea	str_ok,%a5
	lea	35f,%a4
	jmp	post_puts
35:	bclr	#GPIO_LED0_BIT,%d7	/* LED1 only: C init */
	move.w	%d7,(%a0)
	jmp	post_return

/* verify_longs: compare all of RAM against d4; a3 = return. Prints '.' on success. */
verify_longs:
	lea	RAM_BASE,%a1
	move.w	#(RAM_SIZE / 16) - 1,%d1
1:	cmp.l	(%a1)+,%d4
	bne	fail_pat
	cmp.l	(%a1)+,%d4
	bne	fail_pat
	cmp.l	(%a1)+,%d4
	bne	fail_pat
	cmp.l	(%a1)+,%d4
	bne	fail_pat
	dbf	%d1,1b
	moveq	#'.',%d0
	lea	2f,%a6
	jmp	post_putc
2:	jmp	(%a3)

/* ---- failure: "RAM FAIL @aaaaaaaa exp xxxxxxxx got yyyyyyyy", then blink forever ---- */
fail_addr:				/* a1 = address, expected = a1, got = d0 */
	move.l	%a1,%d4
	move.l	%d0,%d6
	move.l	%a1,%d3
	bra	fail
fail_pat:				/* a1 = address + 4, expected = d4 */
	subq.l	#4,%a1
	move.l	(%a1),%d6
	move.l	%a1,%d3
fail:
	lea	str_fail,%a5
	lea	1f,%a4
	jmp	post_puts
1:	moveq	#8,%d5
	lea	2f,%a4
	jmp	post_puthex		/* d3 = address */
2:	lea	str_exp,%a5
	lea	3f,%a4
	jmp	post_puts
3:	move.l	%d4,%d3
	moveq	#8,%d5
	lea	4f,%a4
	jmp	post_puthex
4:	lea	str_got,%a5
	lea	5f,%a4
	jmp	post_puts
5:	move.l	%d6,%d3
	moveq	#8,%d5
	lea	6f,%a4
	jmp	post_puthex
6:	lea	str_crlf,%a5
	lea	blink,%a4
	jmp	post_puts

blink:	bchg	#GPIO_LED0_BIT,%d7	/* alternate LED0 / LED1 at ~2 Hz */
	bchg	#GPIO_LED1_BIT,%d7
	move.w	%d7,(%a0)
	move.l	#160000,%d0		/* 160000 x 18 clocks = 240 ms */
1:	subq.l	#1,%d0
	bhi	1b
	bra	blink

	.section .rodata
	.align	2
patterns:
	.long	0x55555555, 0xAAAAAAAA, 0x00000000, 0xFFFFFFFF
str_hello:	.asciz	"\r\n\r\nother-m68k POST\r\n"
str_romsum:	.asciz	"ROM sum "
str_ok:		.asciz	" OK\r\n"
str_bad:	.asciz	" BAD\r\n"
str_ram:	.asciz	"RAM "
str_fail:	.asciz	"\r\nRAM FAIL @"
str_exp:	.asciz	" exp "
str_got:	.asciz	" got "
str_crlf:	.asciz	"\r\n"
