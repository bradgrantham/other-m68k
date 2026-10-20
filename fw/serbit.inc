/* SERBIT: shift one bit of a serial frame out on GPIO.SERTX and wait one bit time.
 *
 * Register contract (same for POST and the C-callable transmitter):
 *   d0.w  frame, current bit at bit position GPIO_SERTX_BIT (2): (data << 3) | 0x800
 *         = start bit 0, 8 data bits LSB first, stop bit 1
 *   d1    delay counter (clobbered)        d2  scratch (clobbered)
 *   d7.w  GPIO shadow with SERTX cleared   a0  = IO_GPIO_ADDR
 *
 * 68000 clock counts (code fetched from ROM: zero wait states, no slot alignment):
 *   move.w %d0,%d2         4
 *   andi.w #4,%d2          8
 *   or.w   %d7,%d2         4
 *   move.w %d2,(%a0)       8    <- the I/O write is the bit edge. The write cycle is slot
 *                               aligned (+0 or +2 clocks); because the loop is a multiple of
 *                               4 clocks every following write lands on the same phase, so
 *                               the penalty can hit only the first bit of a byte.
 *   lsr.w  #1,%d0          8    (6 + 2n, n = 1)
 *   moveq  #5,%d1          4
 * 1: dbf   %d1,1b         64    (5 taken x 10 + 1 fall-through x 14)
 *   nop                    4
 *                        ---
 *                        104 clocks = 12 MHz / 115200 baud (ideal 104.17, -0.16%)
 */
	.macro	SERBIT
	move.w	%d0,%d2
	andi.w	#GPIO_SERTX_MASK,%d2
	or.w	%d7,%d2
	move.w	%d2,(%a0)
	lsr.w	#1,%d0
	moveq	#5,%d1
1:	dbf	%d1,1b
	nop
	.endm

/* Build the 10-bit frame in d0 from the byte in d0.b. */
	.macro	SERFRAME
	andi.w	#0xFF,%d0
	lsl.w	#3,%d0
	ori.w	#0x800,%d0
	.endm

/* Ten SERBITs: start, 8 data, stop. */
	.macro	SERBYTE
	SERBIT
	SERBIT
	SERBIT
	SERBIT
	SERBIT
	SERBIT
	SERBIT
	SERBIT
	SERBIT
	SERBIT
	.endm
