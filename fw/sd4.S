/* SD native-bus bit-bang primitives (spec revision 7). The six card lines are the SDIO
 * register bits DAT3 0, CLK 1, CMD 2, DAT0 3, DAT1 4, DAT2 5; a 0 drives a line low, a 1
 * releases it. One clock = write the drive word with CLK low (the card changes CMD/DAT after
 * the falling edge), read the levels, write it with CLK high (the card samples on the rising
 * edge). All I/O is through (An) addressing: one bus cycle = 8 clocks, zero wait states.
 *
 * sd_slow (uint16_t, sd.c) > 0 inserts a delay of sd_slow x 10 clocks around every edge in
 * the command and idle-clock paths: identification must stay under 400 kHz. The data paths
 * (sd4_read512, sd4_write512) ignore it: data phases only happen after identification.
 *
 *   void sd4_clocks(int n)                           n idle clocks, everything released
 *   int  sd4_cmd(const uint8_t *frame, uint8_t *resp, int nbits)
 *         send the 48-bit frame on CMD; nbits == 0: no response expected, returns 0; else wait
 *         up to 128 clocks for the response start bit (CMD low), read nbits (48 or 136) MSB
 *         first into resp (the start bit is bit 7 of resp[0]), returns 0, or -1 if nothing came
 *   void sd4_read512(uint8_t *buf)                   1024 nibbles + 16 CRC clocks + end bit
 *   void sd4_write512(const uint8_t *buf, const uint8_t *crcnib)
 *         start bit, 1024 nibbles, the 16 CRC nibbles (crcnib[0] first), end bit
 *
 * MC68000 UM timings (ROM code, CPLD2 I/O), full speed:
 *   sd4_read512, per byte: move.w Dn,(An) 8 + move.w (An),Dn 8 + move.w Dn,(An) 8
 *     + move.b (d8,An,Xn),Dn 14 for the high nibble, the same with or.b for the low nibble,
 *     + move.b Dn,(An)+ 8 = 84, plus dbf 10 per four bytes: 86.5 clocks a byte, 44.3k clocks
 *     = 3.7 ms per block at 12 MHz (the SPI loop was 386..402 a byte, 17 ms a block).
 *   sd4_write512, per byte: move.b (An)+,Dn 8 + move.b Dn,Dn 4 + lsr.b #4 14 + move.b
 *     (d8,An,Xn),Dn 14 + move.w Dn,(An) 8 + addq.w 4 + move.w Dn,(An) 8 + and.w #imm 8
 *     + move.b (d8,An,Xn),Dn 14 + 8 + 4 + 8 = 102, plus dbf 10: 112 a byte, 57k clocks
 *     = 4.8 ms a block (the C CRC16 adds about 4 ms).
 *   sd4_cmd, per bit sent: moveq 4 + add.b 4 + bcs 8/10 + move.w Dn,(An) 8 + SLOW_DELAY 14
 *     + addq 4 + move.w 8 + SLOW_DELAY 14 + dbf 10 = ~76; per bit received ~100; a 48-bit
 *     command with a 48-bit response is about 9k clocks (0.75 ms) with the Ncr wait. */
#include "hw.inc"

#define LO	0x3D			/* IO_SDIO_RESET & ~SDIO_CLK_MASK: all released, CLK low */
#define HI	0x3F			/* all released, CLK high */
#define CMDLO	0x39			/* CMD driven low, CLK low */
#define STARTLO	0x04			/* DAT3..0 driven low (data start bit), CLK low */

	.text

/* delay used in slow mode: \flag (a register holding sd_slow) x (10-clock dbf loop);
 * 14 clocks when zero */
	.macro	SLOW_DELAY flag
	tst.w	\flag
	beq.s	99f
	move.w	\flag,%d0
	subq.w	#1,%d0
98:	dbf	%d0,98b
99:
	.endm

/* ---- void sd4_clocks(int n) ---------------------------------------------------- */
	.globl	sd4_clocks
sd4_clocks:
	movem.l	%d2-%d4,-(%sp)
	move.l	16(%sp),%d1
	lea	IO_SDIO_ADDR,%a0
	move.w	sd_slow,%d2
	moveq	#LO,%d3
	moveq	#HI,%d4
	subq.l	#1,%d1
	bmi.s	2f
1:	move.w	%d3,(%a0)
	SLOW_DELAY %d2
	move.w	%d4,(%a0)
	SLOW_DELAY %d2
	subq.l	#1,%d1
	bpl.s	1b
2:	movem.l	(%sp)+,%d2-%d4
	rts

/* ---- int sd4_cmd(const uint8_t *frame, uint8_t *resp, int nbits) -------------- */
	.globl	sd4_cmd
sd4_cmd:
	movem.l	%d2-%d7/%a2,-(%sp)
	move.l	32(%sp),%a0			/* frame */
	move.l	36(%sp),%a1			/* resp */
	move.l	40(%sp),%d5			/* nbits */
	lea	IO_SDIO_ADDR,%a2
	move.w	sd_slow,%d6
	moveq	#LO,%d7
	moveq	#6-1,%d3
1:	move.b	(%a0)+,%d2			/* byte to send */
	moveq	#8-1,%d4
2:	moveq	#LO,%d1
	add.b	%d2,%d2				/* MSB into carry */
	bcs.s	3f
	moveq	#CMDLO,%d1
3:	move.w	%d1,(%a2)			/* CLK low, CMD = bit */
	SLOW_DELAY %d6
	addq.w	#2,%d1
	move.w	%d1,(%a2)			/* rising edge: card samples */
	SLOW_DELAY %d6
	dbf	%d4,2b
	dbf	%d3,1b
	tst.l	%d5
	beq.s	9f				/* no response wanted (CMD0) */

	/* wait up to 128 clocks for the start bit (CMD low after a falling edge) */
	move.w	#128-1,%d3
4:	move.w	%d7,(%a2)
	SLOW_DELAY %d6
	move.w	(%a2),%d1
	addq.w	#2,%d7
	move.w	%d7,(%a2)
	subq.w	#2,%d7
	SLOW_DELAY %d6
	btst	#SDIO_CMD_BIT,%d1
	beq.s	5f
	dbf	%d3,4b
	moveq	#-1,%d0
	bra.s	8f

	/* start bit seen: it is bit 7 of resp[0]; read nbits-1 more */
5:	moveq	#0,%d2				/* byte being assembled (start bit = 0) */
	moveq	#1,%d4				/* bits in it */
	subq.l	#1,%d5
6:	move.w	%d7,(%a2)
	SLOW_DELAY %d6
	move.w	(%a2),%d1
	addq.w	#2,%d7
	move.w	%d7,(%a2)
	subq.w	#2,%d7
	SLOW_DELAY %d6
	add.b	%d2,%d2
	btst	#SDIO_CMD_BIT,%d1
	sne	%d1				/* 0xFF if the line is high */
	sub.b	%d1,%d2				/* byte = byte*2 + bit */
	addq.w	#1,%d4
	cmpi.w	#8,%d4
	bne.s	7f
	move.b	%d2,(%a1)+
	moveq	#0,%d4
7:	subq.l	#1,%d5
	bne.s	6b
9:	moveq	#0,%d0
8:	movem.l	(%sp)+,%d2-%d7/%a2
	rts

/* ---- void sd4_read512(uint8_t *buf) --------------------------------------------- */
	.globl	sd4_read512
sd4_read512:
	movem.l	%d2-%d4/%a2-%a3,-(%sp)
	move.l	24(%sp),%a0			/* buf */
	lea	IO_SDIO_ADDR,%a3
	lea	nib_hi,%a1
	lea	nib_lo,%a2
	moveq	#LO,%d3
	moveq	#HI,%d4
	moveq	#0,%d0
	move.w	#128-1,%d1
1:	.rept	4
	move.w	%d3,(%a3)			/* falling edge: card presents the nibble */
	move.w	(%a3),%d0			/* levels (bits 5..0) */
	move.w	%d4,(%a3)			/* rising edge */
	move.b	(%a1,%d0.w),%d2			/* high nibble << 4 */
	move.w	%d3,(%a3)
	move.w	(%a3),%d0
	move.w	%d4,(%a3)
	or.b	(%a2,%d0.w),%d2			/* + low nibble */
	move.b	%d2,(%a0)+
	.endr
	dbf	%d1,1b
	moveq	#17-1,%d1			/* 16 CRC clocks + end bit */
2:	move.w	%d3,(%a3)
	move.w	%d4,(%a3)
	dbf	%d1,2b
	movem.l	(%sp)+,%d2-%d4/%a2-%a3
	rts

/* ---- void sd4_write512(const uint8_t *buf, const uint8_t *crcnib) --------------- */
	.globl	sd4_write512
sd4_write512:
	movem.l	%d2/%a2-%a3,-(%sp)
	move.l	16(%sp),%a0			/* buf */
	move.l	20(%sp),%a1			/* 16 CRC nibbles */
	lea	IO_SDIO_ADDR,%a3
	lea	nibdrv,%a2
	move.w	#STARTLO,(%a3)			/* start bit on all four lines */
	move.w	#STARTLO+2,(%a3)
	moveq	#0,%d0
	moveq	#0,%d2
	move.w	#512-1,%d1
1:	move.b	(%a0)+,%d0
	move.b	%d0,%d2
	lsr.b	#4,%d0				/* high nibble */
	move.b	(%a2,%d0.w),%d0			/* drive word, CLK low */
	move.w	%d0,(%a3)
	addq.w	#2,%d0
	move.w	%d0,(%a3)			/* rising edge */
	and.w	#15,%d2				/* low nibble */
	move.b	(%a2,%d2.w),%d2
	move.w	%d2,(%a3)
	addq.w	#2,%d2
	move.w	%d2,(%a3)
	dbf	%d1,1b
	moveq	#16-1,%d1
2:	move.b	(%a1)+,%d0
	move.b	(%a2,%d0.w),%d0
	move.w	%d0,(%a3)
	addq.w	#2,%d0
	move.w	%d0,(%a3)
	dbf	%d1,2b
	move.w	#LO,(%a3)			/* end bit: everything released */
	move.w	#HI,(%a3)
	movem.l	(%sp)+,%d2/%a2-%a3
	rts

/* ---- void sd_crc16x4(const uint8_t *data, int n, uint16_t crc[4]) ---------------
 * CRC16-CCITT (x^16 + x^12 + x^5 + 1, init 0) of each DAT line of a 4-bit block: line L
 * carries bit L of every nibble, high nibble first. TD[b] spreads a data byte into four
 * byte lanes (lane L = the two bits of line L), four data bytes make one byte of every
 * line's stream in a long, and each lane byte goes through the 256-entry CRC table.
 * n must be a multiple of 4. Per four bytes: 4 x (moveq 4 + move.b 8 + 2 x add.w 8 +
 * move.l (d8,An,Xn),Dn 18) + lsl.l #6/#4/#2 48 + 3 or.l 24 + 4 x CRC step 86 + 3 ror.l 72
 * + dbf 10 = 650 clocks: 83k clocks, 6.9 ms for a 512-byte block. */
	.macro	CRCSTEP crc			/* crc = (crc << 8) ^ T[(crc >> 8) ^ lane]; lane = low byte of d2 */
	move.w	\crc,%d3
	lsr.w	#8,%d3
	eor.b	%d2,%d3
	and.w	#0xFF,%d3
	add.w	%d3,%d3
	move.w	(%a3,%d3.w),%d3
	lsl.w	#8,\crc
	eor.w	%d3,\crc
	.endm

	.globl	sd_crc16x4
sd_crc16x4:
	movem.l	%d2-%d7/%a2-%a3,-(%sp)
	move.l	36(%sp),%a0			/* data */
	move.l	40(%sp),%d1			/* n */
	move.l	44(%sp),%a1			/* crc[4] */
	lea	TD,%a2
	lea	T16,%a3
	moveq	#0,%d4				/* line 0 */
	moveq	#0,%d5				/* line 1 */
	moveq	#0,%d6				/* line 2 */
	moveq	#0,%d7				/* line 3 */
	lsr.l	#2,%d1
	subq.l	#1,%d1
	bmi	2f
1:	moveq	#0,%d0
	move.b	(%a0)+,%d0
	add.w	%d0,%d0
	add.w	%d0,%d0
	move.l	(%a2,%d0.w),%d2
	lsl.l	#6,%d2
	moveq	#0,%d0
	move.b	(%a0)+,%d0
	add.w	%d0,%d0
	add.w	%d0,%d0
	move.l	(%a2,%d0.w),%d3
	lsl.l	#4,%d3
	or.l	%d3,%d2
	moveq	#0,%d0
	move.b	(%a0)+,%d0
	add.w	%d0,%d0
	add.w	%d0,%d0
	move.l	(%a2,%d0.w),%d3
	lsl.l	#2,%d3
	or.l	%d3,%d2
	moveq	#0,%d0
	move.b	(%a0)+,%d0
	add.w	%d0,%d0
	add.w	%d0,%d0
	or.l	(%a2,%d0.w),%d2			/* d2: lane bytes, line 0 lowest */
	CRCSTEP	%d4
	ror.l	#8,%d2
	CRCSTEP	%d5
	ror.l	#8,%d2
	CRCSTEP	%d6
	ror.l	#8,%d2
	CRCSTEP	%d7
	dbf	%d1,1b
2:	move.w	%d4,(%a1)+
	move.w	%d5,(%a1)+
	move.w	%d6,(%a1)+
	move.w	%d7,(%a1)
	movem.l	(%sp)+,%d2-%d7/%a2-%a3
	rts

/* tables ('|' is the m68k comment character, hence '+') */
	.section .rodata
	.balign	2
/* nibble = DAT3<<3 + DAT2<<2 + DAT1<<1 + DAT0 for every 6-bit SDIO value, as the high / low half */
nib_hi:
	.irp v,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63
	.byte	((((\v)&1)<<3) + ((((\v)>>5)&1)<<2) + ((((\v)>>4)&1)<<1) + (((\v)>>3)&1)) << 4
	.endr
nib_lo:
	.irp v,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63
	.byte	(((\v)&1)<<3) + ((((\v)>>5)&1)<<2) + ((((\v)>>4)&1)<<1) + (((\v)>>3)&1)
	.endr
/* drive word (CLK low, CMD released) for a nibble on DAT3..0: a 0 bit drives its line low */
nibdrv:
	.irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	.byte	0x04 + (((\n)>>3)&1) + ((((\n)>>0)&1)<<3) + ((((\n)>>1)&1)<<4) + ((((\n)>>2)&1)<<5)
	.endr
/* TD[b]: lane L (byte L, L = 0 lowest) = bit L of the high nibble, then bit L of the low nibble */
	.balign	4
TD:
	.set	i, 0
	.rept	256
	.long	((((i>>4)&1)<<1)+(i&1)) + (((((i>>5)&1)<<1)+((i>>1)&1))<<8) + (((((i>>6)&1)<<1)+((i>>2)&1))<<16) + (((((i>>7)&1)<<1)+((i>>3)&1))<<24)
	.set	i, i+1
	.endr
/* CRC16-CCITT byte table */
	.balign	2
T16:
	.set	i, 0
	.rept	256
	.set	c, i<<8
	.rept	8
	.set	c, ((c<<1) ^ (0x1021 * ((c>>15)&1))) & 0xFFFF
	.endr
	.short	c
	.set	i, i+1
	.endr
