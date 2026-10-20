/* Emulator self-test ROM (68000, GNU as syntax).
 *
 * Exercises RAM, the LEDs, the framebuffer, a cycle-counted serial bit-bang,
 * the VBLANK autovector interrupt, the bus-error frame and the SD card, and
 * leaves its findings in a result block at RES_BASE that check.sh reads back
 * with --dump-mem.
 */
#include "hw.inc"

	.equ RES_BASE,     0x000800
	.equ RES_RAMOK,    RES_BASE+0      /* .w 0x0001 when RAM pattern passed */
	.equ RES_BERRCNT,  RES_BASE+2      /* .w bus errors seen */
	.equ RES_BERRADR,  RES_BASE+4      /* .l fault address from the frame */
	.equ RES_VBLANKS,  RES_BASE+8      /* .l VBLANK interrupts counted */
	.equ RES_SD,       RES_BASE+12     /* .w SD step flags */
	.equ RES_MARKER,   RES_BASE+14     /* .w 0xBEEF when the test completed */
	.equ RES_BERRINFO, RES_BASE+16     /* .w access info word from the frame */
	.equ RES_ID,       RES_BASE+18     /* .w ID register */
	.equ RES_UNEXP,    RES_BASE+20     /* .w 0xDEAD / 0xAE00 on unexpected exceptions */
	.equ RES_INTSTAT,  RES_BASE+22     /* .w INTSTAT sampled inside the VBLANK handler */
	.equ RES_SDR1,     RES_BASE+24     /* .b x4: R1 of CMD0, CMD8, ACMD41 (final), CMD58 */
	.equ RES_SDOCR,    RES_BASE+28     /* .l OCR */
	.equ RES_SDDATA,   RES_BASE+32     /* 16 bytes: start of block 0 */
	.equ RES_LINES0,   RES_BASE+48     /* .w lines per frame measured (NTSC) */
	.equ RES_LINES1,   RES_BASE+50     /* .w ... after SW_PAL (312) */
	.equ RES_LINES2,   RES_BASE+52     /* .w ... after SW_NTSC again (262) */
	.equ SD_BUF,       0x001000        /* 512-byte block buffer */

	.equ SD_OK_CMD0,   0x0001
	.equ SD_OK_CMD8,   0x0002
	.equ SD_OK_ACMD41, 0x0004
	.equ SD_OK_CMD58,  0x0008
	.equ SD_OK_CMD17,  0x0010
	.equ SD_OK_DATA,   0x0020

	.equ GPIO_IDLE,    0x0007          /* PS/2 released, SERTX high */

/* ---------------------------------------------------------------------- */
	.section .vectors,"a"
	.long SYS_STACK_TOP
	.long _start
	.long vec_berr                     /* 2: bus error */
	.long vec_aerr                     /* 3: address error */
	.rept 21                           /* 4..24 */
	.long vec_unexpected
	.endr
	.long vec_vblank                   /* 25: level 1 autovector */
	.rept 230                          /* 26..255 */
	.long vec_unexpected
	.endr

/* ---------------------------------------------------------------------- */
	.text
	.globl _start
_start:
	move.w	#0x2700,%sr
	/* The fetch of this instruction cleared the overlay: copy the vector
	 * table from ROM into RAM so exceptions work. */
	lea	ROM_BASE,%a0
	suba.l	%a1,%a1
	move.w	#255,%d0
1:	move.l	(%a0)+,(%a1)+
	dbra	%d0,1b
	lea	SYS_STACK_TOP,%sp

	/* Clear the result block. */
	lea	RES_BASE,%a0
	moveq	#15,%d0
1:	clr.l	(%a0)+
	dbra	%d0,1b

	/* Glue ID. */
	move.w	IO_ID_ADDR,RES_ID

	/* LED0 on. */
	move.w	#GPIO_IDLE+GPIO_LED0_MASK,IO_GPIO_ADDR

	/* ---- RAM pattern: long, word, two bytes, top of RAM ---- */
	move.l	#0x12345678,0x2000
	move.w	#0xA55A,0x2004
	move.b	#0x5A,0x2006
	move.b	#0xA5,0x2007
	move.l	#0xCAFEF00D,RAM_END-4
	moveq	#0,%d1
	cmp.l	#0x12345678,0x2000
	bne.s	ram_done
	cmp.w	#0xA55A,0x2004
	bne.s	ram_done
	cmp.w	#0x5AA5,0x2006
	bne.s	ram_done
	cmp.l	#0xCAFEF00D,RAM_END-4
	bne.s	ram_done
	moveq	#1,%d1
ram_done:
	move.w	%d1,RES_RAMOK

	/* ---- Gradient into framebuffer page 0: pixel = x + y ---- */
	lea	FB_PAGE0_ADDR,%a0
	moveq	#0,%d0				/* y */
yloop:	moveq	#0,%d1				/* x */
xloop:	move.w	%d1,%d2
	add.w	%d0,%d2
	move.b	%d2,(%a0)+
	addq.w	#1,%d1
	cmp.w	#320,%d1			/* VIDEO_WIDTH (not in hw.inc) */
	bne.s	xloop
	addq.w	#1,%d0
	cmp.w	#240,%d0			/* VIDEO_HEIGHT */
	bne.s	yloop

	/* ---- Bit-bang "HELLO\n" on GPIO.SERTX at 115200 ---- */
	lea	msg,%a1
	lea	IO_GPIO_ADDR,%a0
	move.w	#(GPIO_IDLE+GPIO_LED0_MASK)&~GPIO_SERTX_MASK,%d4
nextch:	moveq	#0,%d0
	move.b	(%a1)+,%d0
	beq.s	serial_done
	bsr	ser_putc
	bra.s	nextch
serial_done:
	/* Hardware transmitter (only with --hw-serial-tx; otherwise the writes
	 * are ignored and SERTXBUSY reads 0). */
	lea	hwmsg,%a1
hwnext:	moveq	#0,%d0
	move.b	(%a1)+,%d0
	beq.s	hw_done
	move.w	%d0,IO_SERDATA_ADDR
1:	btst	#INTSTAT_SERTXBUSY_BIT,IO_INTSTAT_ADDR+1
	bne.s	1b
	bra.s	hwnext
hw_done:

	/* ---- VBLANK interrupt: count three frames ---- */
	move.w	#INTSTAT_VBLANK_MASK,IO_INTSTAT_ADDR	/* clear a stale flag */
	move.w	#INTEN_VBLANK_MASK,IO_INTEN_ADDR
	move.w	#0x2000,%sr
wait3:	cmp.l	#3,RES_VBLANKS
	blo.s	wait3

	/* ---- Soft switches: measure lines per frame in NTSC, PAL, NTSC ---- */
	bsr	count_lines
	move.w	%d0,RES_LINES0
	move.w	#0,SW_PAL_ADDR
	bsr	count_lines			/* straddles the switch */
	bsr	count_lines
	move.w	%d0,RES_LINES1
	move.w	#0,SW_NTSC_ADDR
	bsr	count_lines
	bsr	count_lines
	move.w	%d0,RES_LINES2
	move.w	#0,SW_PAGE1_ADDR		/* exercise the page switch too */
	move.w	#0,SW_PAGE0_ADDR

	/* ---- Bus error: read unmapped 0x400000 ---- */
	move.w	0x400000,%d0
berr_cont:

	/* ---- SD card ---- */
	bsr	sd_test

	/* ---- Done ---- */
	move.w	#0xBEEF,RES_MARKER
	move.w	#GPIO_IDLE+GPIO_LED0_MASK+GPIO_LED1_MASK,IO_GPIO_ADDR
idle:	stop	#0x2000				/* VBLANK keeps counting */
	bra.s	idle

/* ---------------------------------------------------------------------- */
/* ser_putc: d0.b = character.  a0 = IO_GPIO_ADDR, d4 = GPIO value with
 * SERTX low.  Each bit takes exactly 104 clocks (115385 baud, +0.16%):
 * moveq 4 + lsr 8 + addx 4 + lsl 10 + or 4 + move 8 + moveq 4 + dbra
 * 3*10+14 + nop 4 + nop 4 + dbra 10 = 104.  Clobbers d1-d3, d5. */
ser_putc:
	move.w	%d0,%d1
	lsl.w	#1,%d1				/* start bit 0 at bit 0 */
	ori.w	#0x0200,%d1			/* stop bit at bit 9 */
	moveq	#9,%d5
bitloop:
	moveq	#0,%d3
	lsr.w	#1,%d1				/* X = C = bit */
	addx.w	%d3,%d3				/* d3 = bit */
	lsl.w	#GPIO_SERTX_BIT,%d3
	or.w	%d4,%d3
	move.w	%d3,(%a0)
	moveq	#3,%d2
1:	dbra	%d2,1b
	nop
	nop
	dbra	%d5,bitloop
	rts

/* ---------------------------------------------------------------------- */
/* count_lines: d0.l = number of HSYNC flags between two VBLANK flags, i.e.
 * the lines per frame (262 NTSC, 312 PAL).  Polls with interrupts masked
 * (the loop is ~50 clocks, a line is 764).  Clobbers d1. */
count_lines:
	move.w	#0x2700,%sr
	move.w	#INTSTAT_VBLANK_MASK+INTSTAT_HSYNC_MASK,IO_INTSTAT_ADDR
1:	btst	#INTSTAT_VBLANK_BIT,IO_INTSTAT_ADDR+1
	beq.s	1b
	move.w	#INTSTAT_VBLANK_MASK+INTSTAT_HSYNC_MASK,IO_INTSTAT_ADDR
	moveq	#0,%d0
2:	move.w	IO_INTSTAT_ADDR,%d1
	btst	#INTSTAT_HSYNC_BIT,%d1
	beq.s	3f
	addq.l	#1,%d0
	move.w	#INTSTAT_HSYNC_MASK,IO_INTSTAT_ADDR
3:	btst	#INTSTAT_VBLANK_BIT,%d1
	beq.s	2b
	move.w	#INTSTAT_VBLANK_MASK,IO_INTSTAT_ADDR
	move.w	#0x2000,%sr
	rts

/* ---------------------------------------------------------------------- */
/* spi_byte: d0.b out -> d0.b in.  a0 = IO_SDIO_ADDR, d6 = CS bit (0 =
 * selected).  Mode 0: set MOSI with CLK low, raise CLK, sample MISO, lower
 * CLK.  Clobbers d1-d5. */
spi_byte:
	moveq	#7,%d2
	moveq	#0,%d3
spi_bit:
	move.w	%d6,%d1
	add.b	%d0,%d0				/* MSB -> C */
	bcc.s	1f
	ori.w	#SDIO_MOSI_MASK,%d1
1:	move.w	%d1,(%a0)			/* data out, CLK low */
	move.w	%d1,%d5
	ori.w	#SDIO_CLK_MASK,%d5
	move.w	%d5,(%a0)			/* CLK high: card samples MOSI */
	move.w	(%a0),%d4
	add.b	%d3,%d3
	btst	#SDIO_MISO_BIT,%d4
	beq.s	2f
	addq.b	#1,%d3
2:	move.w	%d1,(%a0)			/* CLK low */
	dbra	%d2,spi_bit
	move.b	%d3,%d0
	rts

/* sd_cmd: a1 -> 6 command bytes.  Sends them, polls up to 8 bytes for a
 * response (first byte != 0xFF) and returns it in d0.b (0xFF = none). */
sd_cmd:
	moveq	#5,%d7
1:	move.b	(%a1)+,%d0
	bsr	spi_byte
	dbra	%d7,1b
	moveq	#7,%d7
2:	moveq	#-1,%d0
	bsr	spi_byte
	cmp.b	#0xFF,%d0
	bne.s	3f
	dbra	%d7,2b
3:	rts

sd_test:
	lea	IO_SDIO_ADDR,%a0
	/* 80 clocks with CS high. */
	moveq	#SDIO_CS_MASK,%d6
	moveq	#9,%d7
1:	moveq	#-1,%d0
	bsr	spi_byte
	dbra	%d7,1b
	moveq	#0,%d6				/* select */

	/* CMD0 -> 0x01 */
	lea	cmd0,%a1
	bsr	sd_cmd
	move.b	%d0,RES_SDR1+0
	cmp.b	#0x01,%d0
	bne	sd_fail
	ori.w	#SD_OK_CMD0,RES_SD

	/* CMD8 -> 0x01 + 00 00 01 AA */
	lea	cmd8,%a1
	bsr	sd_cmd
	move.b	%d0,RES_SDR1+1
	cmp.b	#0x01,%d0
	bne	sd_fail
	moveq	#3,%d7
1:	moveq	#-1,%d0
	bsr	spi_byte
	dbra	%d7,1b
	cmp.b	#0xAA,%d0
	bne	sd_fail
	ori.w	#SD_OK_CMD8,RES_SD

	/* CMD55 + ACMD41 until R1 == 0 (at most 100 tries) */
	move.w	#99,%d7
acmd41_loop:
	move.w	%d7,-(%sp)
	lea	cmd55,%a1
	bsr	sd_cmd
	lea	acmd41,%a1
	bsr	sd_cmd
	move.w	(%sp)+,%d7
	move.b	%d0,RES_SDR1+2
	tst.b	%d0
	beq.s	acmd41_ok
	dbra	%d7,acmd41_loop
	bra	sd_fail
acmd41_ok:
	ori.w	#SD_OK_ACMD41,RES_SD

	/* CMD58 -> 0x00 + OCR (CCS set) */
	lea	cmd58,%a1
	bsr	sd_cmd
	move.b	%d0,RES_SDR1+3
	tst.b	%d0
	bne	sd_fail
	lea	RES_SDOCR,%a2
	moveq	#3,%d7
1:	moveq	#-1,%d0
	bsr	spi_byte
	move.b	%d0,(%a2)+
	dbra	%d7,1b
	btst	#6,RES_SDOCR
	beq	sd_fail
	ori.w	#SD_OK_CMD58,RES_SD

	/* CMD17 block 0 -> 0x00, then 0xFE + 512 bytes + CRC */
	lea	cmd17,%a1
	bsr	sd_cmd
	tst.b	%d0
	bne	sd_fail
	ori.w	#SD_OK_CMD17,RES_SD
	move.w	#999,%d7
1:	moveq	#-1,%d0
	bsr	spi_byte
	cmp.b	#0xFE,%d0
	beq.s	2f
	dbra	%d7,1b
	bra	sd_fail
2:	lea	SD_BUF,%a2
	move.w	#511,%d7
3:	moveq	#-1,%d0
	bsr	spi_byte
	move.b	%d0,(%a2)+
	dbra	%d7,3b
	moveq	#-1,%d0
	bsr	spi_byte			/* CRC */
	moveq	#-1,%d0
	bsr	spi_byte
	ori.w	#SD_OK_DATA,RES_SD
	lea	SD_BUF,%a2
	lea	RES_SDDATA,%a3
	moveq	#3,%d7
4:	move.l	(%a2)+,(%a3)+
	dbra	%d7,4b

sd_fail:
	/* Deselect and clock once. */
	moveq	#SDIO_CS_MASK,%d6
	moveq	#-1,%d0
	bsr	spi_byte
	rts

/* ---------------------------------------------------------------------- */
/* Exception handlers */

/* Level-1 autovector: count, sample INTSTAT, clear the flag. */
vec_vblank:
	addq.l	#1,RES_VBLANKS
	eori.w	#GPIO_AUDIO_MASK,IO_GPIO_ADDR	/* 30 Hz square wave on AUDIO */
	move.w	IO_INTSTAT_ADDR,RES_INTSTAT
	move.w	#INTSTAT_VBLANK_MASK,IO_INTSTAT_ADDR
	rte

/* Bus error.  68000 group-0 frame: +0 access info, +2 fault address,
 * +6 IR, +8 SR, +10 PC.  Record, then resume at berr_cont. */
vec_berr:
	addq.w	#1,RES_BERRCNT
	move.w	(%sp),RES_BERRINFO
	move.l	2(%sp),RES_BERRADR
	addq.l	#8,%sp
	move.l	#berr_cont,2(%sp)
	rte

vec_aerr:
	move.w	#0xAE00,RES_UNEXP
	stop	#0x2700
	bra.s	vec_aerr

vec_unexpected:
	move.w	#0xDEAD,RES_UNEXP
	stop	#0x2700
	bra.s	vec_unexpected

/* ---------------------------------------------------------------------- */
	.section .rodata
msg:	.asciz	"HELLO\n"
hwmsg:	.asciz	"HW\n"
	.even
cmd0:	.byte	0x40,0x00,0x00,0x00,0x00,0x95
cmd8:	.byte	0x48,0x00,0x00,0x01,0xAA,0x87
cmd55:	.byte	0x77,0x00,0x00,0x00,0x00,0x65
acmd41:	.byte	0x69,0x40,0x00,0x00,0x00,0x77
cmd58:	.byte	0x7A,0x00,0x00,0x00,0x00,0xFD
cmd17:	.byte	0x51,0x00,0x00,0x00,0x00,0xFF
