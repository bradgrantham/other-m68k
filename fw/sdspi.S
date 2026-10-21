/* SD SPI receive fast path. void sdhw_read(uint8_t *buf, int n): clock n bytes in with
 * MOSI held high (0xFF out) at full speed, CS as left by sdhw_select. Per bit: CLK low,
 * CLK high, read INTSTAT, test SDMISO (sne/sub trick instead of shifting the bit down from
 * position 8). MC68000 UM timing tables, zero wait states (ROM code, CPLD2 I/O):
 *   move.w Dn,(An) 8 + move.w Dn,(An) 8 + move.w (An),Dn 8 + btst #imm,Dn 10
 *   + sne Dn 4 (bit clear) or 6 (bit set) + add.b 4 + sub.b 4 = 46 or 48 clocks per bit;
 *   per byte also move.b Dn,(An)+ 8 + dbf (taken) 10 = 18, so 386..402 clocks a byte,
 * about 17 ms (~240 kbit/s) for a 512-byte block at 12 MHz. The slow (identification)
 * clock is never needed for data phases, so this ignores sd_slow. */
#include "hw.inc"

	.text
	.globl	sdhw_read
sdhw_read:
	movem.l	%d2-%d4/%a2-%a3,-(%sp)
	move.l	24(%sp),%a0			/* buf */
	move.l	28(%sp),%d1			/* n   */
	lea	IO_SDIO_ADDR,%a3
	lea	IO_INTSTAT_ADDR,%a2
	move.w	sdio_shadow,%d3
	andi.w	#SDIO_CS_MASK,%d3		/* keep CS, CLK low */
	ori.w	#SDIO_MOSI_MASK,%d3		/* MOSI high */
	move.w	%d3,%d4
	ori.w	#SDIO_CLK_MASK,%d4		/* same with CLK high */
	subq.l	#1,%d1
	bmi	2f
1:	.rept	8
	move.w	%d3,(%a3)			/* falling edge: card presents the bit */
	move.w	%d4,(%a3)			/* rising edge */
	move.w	(%a2),%d0
	btst	#INTSTAT_SDMISO_BIT,%d0
	sne	%d0				/* 0xFF if the bit is set */
	add.b	%d2,%d2
	sub.b	%d0,%d2				/* in = in * 2 + bit */
	.endr
	move.b	%d2,(%a0)+
	dbf	%d1,1b
2:	move.w	%d3,(%a3)			/* leave CLK low */
	move.w	%d3,sdio_shadow
	movem.l	(%sp)+,%d2-%d4/%a2-%a3
	rts
