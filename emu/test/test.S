/* Emulator self-test ROM (68000, GNU as syntax).
 *
 * Exercises RAM, the LEDs, the framebuffer, the 68681 DUART (init, polled
 * transmit, receive FIFO and overrun, level-3 interrupt gated by INTEN.DUART,
 * counter and timer modes), the VBLANK autovector interrupt, the bus-error
 * frame and the SD card, and leaves its findings in a result block at
 * RES_BASE that check.sh reads back with --dump-mem.
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
	.equ RES_SDR1,     RES_BASE+24     /* .b x4: R7 index (08), ACMD41 polls, CMD7 R1 index, CMD17 R1 index */
	.equ RES_SDOCR,    RES_BASE+28     /* .l OCR */
	.equ RES_SDDATA,   RES_BASE+32     /* 16 bytes: start of block 0 */
	.equ RES_LINES0,   RES_BASE+48     /* .w lines per frame measured (NTSC) */
	.equ RES_LINES1,   RES_BASE+50     /* .w ... after SW_PAL (312) */
	.equ RES_LINES2,   RES_BASE+52     /* .w ... after SW_NTSC again (262) */
	.equ RES_SRA0,     RES_BASE+54     /* .b SRA right after enabling RX+TX (0x0C) */
	.equ RES_SRA1,     RES_BASE+55     /* .b SRA after two back-to-back TBA writes (0x00) */
	.equ RES_SRARX,    RES_BASE+56     /* .b SRA after 6 bytes arrived unread (0x13) */
	.equ RES_RXFIRST,  RES_BASE+57     /* .b first byte popped ('A') */
	.equ RES_RXBUF,    RES_BASE+58     /* .b x4 bytes popped by the level-3 handler ("BCF") */
	.equ RES_IRQCNT,   RES_BASE+62     /* .w DUART RX interrupts taken (1) */
	.equ RES_ISRVAL,   RES_BASE+64     /* .b ISR sampled in the handler (bit 1 set) */
	.equ RES_INTSTAT3, RES_BASE+65     /* .b INTSTAT low byte in the handler (bit 3 set) */
	.equ RES_GATED,    RES_BASE+66     /* .w INTSTAT with IMR set but INTEN.DUART clear */
	.equ RES_GATEDCNT, RES_BASE+68     /* .w interrupts taken while gated (0) */
	.equ RES_CTCNT,    RES_BASE+70     /* .w timer-mode CTR interrupts during the VBLANK wait */
	.equ RES_CTPOLL,   RES_BASE+72     /* .w counter mode: polls until ISR.CTR */
	.equ RES_ISRSTOP,  RES_BASE+74     /* .b ISR after STOP_CTR (CTR clear) */
	.equ RES_ISRTC,    RES_BASE+75     /* .b ISR at terminal count (CTR set) */
	.equ RES_SRAERR,   RES_BASE+76     /* .b SRA after reset-error-status (0x0C) */
	.equ RES_MR1RD,    RES_BASE+77     /* .b MR1A read back */
	.equ RES_MR2RD,    RES_BASE+78     /* .b MR2A read back */
	.equ RES_IVR,      RES_BASE+79     /* .b IVR before anything wrote it (0x0F) */
	.equ RES_CUR,      RES_BASE+80     /* .w CUR:CLR after the counter stopped */
	.equ RES_WORDRD,   RES_BASE+82     /* .w word read of SRA (0xFF in the upper byte) */
	.equ GPIO_SHADOW,  0x000900        /* .w GPIO is write-only: software shadow */
	.equ RXBUF_PTR,    0x000904        /* .l fill pointer into RES_RXBUF */
	.equ SD_BUF,       0x001000        /* 512-byte block buffer */
	.equ RESP_BUF,     0x001201        /* 17-byte response buffer (odd, so the fields after the index byte are word aligned) */

	.equ SD_OK_CMD0,   0x0001
	.equ SD_OK_CMD8,   0x0002
	.equ SD_OK_ACMD41, 0x0004
	.equ SD_OK_IDENT,  0x0008
	.equ SD_OK_CMD7,   0x0010
	.equ SD_OK_DATA,   0x0020

	.equ GPIO_IDLE,    IO_GPIO_RESET   /* PS/2 released */
	.equ ACR_CT_COUNTER_X16, 0x30      /* ACR[6:4] = 011: counter mode, X1/16 */
	.equ ACR_CT_TIMER_X16,   0x70      /* ACR[6:4] = 111: timer mode, X1/16 */
	.equ SRA_RXFULL_OE, SRA_FFULL_MASK+SRA_OE_MASK

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
	.long vec_unexpected               /* 26 */
	.long vec_duart                    /* 27: level 3 autovector (DUART) */
	.rept 228                          /* 28..255 */
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
	moveq	#23,%d0
1:	clr.l	(%a0)+
	dbra	%d0,1b
	move.l	#RES_RXBUF,RXBUF_PTR

	/* Glue ID. */
	move.w	IO_ID_ADDR,RES_ID

	/* PDM audio: pattern 1011 0001 shifts out 8 bits per line, bit 7 first
	 * (check.sh verifies the edge timing from --audio-edge-log). */
	move.w	#0xB1,IO_AUDIO_ADDR

	/* LED0 on (GPIO is write-only: keep a shadow). */
	move.w	#GPIO_IDLE+GPIO_LED0_MASK,GPIO_SHADOW
	move.w	GPIO_SHADOW,IO_GPIO_ADDR

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

	/* ---- DUART channel A: the spec's init sequence ---- */
	move.b	DUART_IVR_ADDR,RES_IVR
	move.b	#DUART_CR_RESET_MR,DUART_CRA_ADDR
	move.b	#DUART_CR_RESET_RX,DUART_CRA_ADDR
	move.b	#DUART_CR_RESET_TX,DUART_CRA_ADDR
	move.b	#SERIAL_MR1_VALUE,DUART_MRA_ADDR
	move.b	#SERIAL_MR2_VALUE,DUART_MRA_ADDR
	move.b	#SERIAL_ACR_VALUE,DUART_ACR_ADDR
	move.b	#SERIAL_CSR_VALUE,DUART_CSRA_ADDR
	clr.b	DUART_IMR_ADDR
	move.b	#DUART_CR_RESET_MR,DUART_CRA_ADDR
	move.b	DUART_MRA_ADDR,RES_MR1RD	/* pointer back at MR1 */
	move.b	DUART_MRA_ADDR,RES_MR2RD
	move.b	#DUART_CR_ENABLE,DUART_CRA_ADDR
	move.b	DUART_SRA_ADDR,RES_SRA0		/* TXRDY + TXEMT */
	move.w	DUART_SRA_ADDR-1,RES_WORDRD	/* word access: only the odd byte is real */

	/* "HELLO\n" polled on TXRDY; SRA sampled after the second write while
	 * the first byte is still shifting out (holding register occupied). */
	lea	msg,%a1
	moveq	#0,%d2
nextch:	move.b	(%a1)+,%d0
	beq.s	serial_done
1:	btst	#SRA_TXRDY_BIT,DUART_SRA_ADDR
	beq.s	1b
	move.b	%d0,DUART_TBA_ADDR
	addq.w	#1,%d2
	cmp.w	#2,%d2
	bne.s	nextch
	move.b	DUART_SRA_ADDR,RES_SRA1
	bra.s	nextch
serial_done:

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

	/* ---- DUART receive: check.sh injects "ABCDEF" after frame 1.  With the
	 * interrupt masked the FIFO holds A B C, F waits in the shift register
	 * and D, E were lost (OE).  Wait (bounded) for FFULL + OE. ---- */
	move.l	#400000,%d7
1:	move.b	DUART_SRA_ADDR,%d0
	and.b	#SRA_RXFULL_OE,%d0
	cmp.b	#SRA_RXFULL_OE,%d0
	beq.s	2f
	subq.l	#1,%d7
	bne.s	1b
2:	move.b	DUART_SRA_ADDR,RES_SRARX
	move.b	DUART_RBA_ADDR,RES_RXFIRST	/* pops A; F enters the FIFO */

	/* IRQ pin asserted but gated: IMR passes RXRDYA, INTEN.DUART clear. */
	move.b	#IMR_RXRDYA_MASK,DUART_IMR_ADDR
	clr.w	IO_INTEN_ADDR
	move.w	#0x2000,%sr
	nop
	nop
	nop
	nop
	move.w	IO_INTSTAT_ADDR,RES_GATED
	move.w	RES_IRQCNT,RES_GATEDCNT
	/* Open the gate: level 3 fires at once and the handler drains B C F. */
	move.w	#INTEN_DUART_MASK,IO_INTEN_ADDR
	nop
	nop
	nop
	nop
	move.w	#0x2700,%sr
	move.b	#DUART_CR_RESET_ERR,DUART_CRA_ADDR
	move.b	DUART_SRA_ADDR,RES_SRAERR	/* OE gone, FIFO empty, TX idle */

	/* ---- Counter mode, X1/16 (230.4 kHz): 64 ticks, poll for terminal
	 * count, stop, read the count (rolled past 0 -> 0xFFxx). ---- */
	move.b	#SERIAL_ACR_VALUE+ACR_CT_COUNTER_X16,DUART_ACR_ADDR
	clr.b	DUART_CTUR_ADDR
	move.b	#0x40,DUART_CTLR_ADDR
	tst.b	DUART_START_CTR_ADDR
	moveq	#0,%d7
1:	addq.w	#1,%d7
	cmp.w	#0x7FFF,%d7
	beq.s	2f
	btst	#ISR_CTR_BIT,DUART_ISR_ADDR
	beq.s	1b
2:	move.w	%d7,RES_CTPOLL
	move.b	DUART_ISR_ADDR,RES_ISRTC
	tst.b	DUART_STOP_CTR_ADDR
	move.b	DUART_ISR_ADDR,RES_ISRSTOP
	move.b	DUART_CUR_ADDR,RES_CUR
	move.b	DUART_CLR_ADDR,RES_CUR+1

	/* ---- Timer mode, X1/16, preload 72: ISR.CTR every 2*72 ticks = 625 us.
	 * Counted by the level-3 handler while waiting for three VBLANKs. ---- */
	move.b	#SERIAL_ACR_VALUE+ACR_CT_TIMER_X16,DUART_ACR_ADDR
	clr.b	DUART_CTUR_ADDR
	move.b	#72,DUART_CTLR_ADDR
	tst.b	DUART_START_CTR_ADDR
	move.b	#IMR_RXRDYA_MASK+IMR_CTR_MASK,DUART_IMR_ADDR

	/* ---- VBLANK interrupt: count three frames ---- */
	move.w	#INTSTAT_VBLANK_MASK,IO_INTSTAT_ADDR	/* clear a stale flag */
	move.w	#INTEN_VBLANK_MASK+INTEN_DUART_MASK,IO_INTEN_ADDR
	move.w	#0x2000,%sr
wait3:	cmp.l	#3,RES_VBLANKS
	blo.s	wait3
	move.b	#IMR_RXRDYA_MASK,DUART_IMR_ADDR	/* timer interrupts off */
	tst.b	DUART_STOP_CTR_ADDR

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
	move.w	#GPIO_IDLE+GPIO_LED0_MASK+GPIO_LED1_MASK,GPIO_SHADOW
	move.w	GPIO_SHADOW,IO_GPIO_ADDR
idle:	stop	#0x2000				/* VBLANK keeps counting */
	bra.s	idle

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
/* ---- SD card on the native bus (spec rev 7), 1-bit data ----
 * SDIO bits: DAT3 0, CLK 1, CMD 2, DAT0 3, DAT1 4, DAT2 5; 1 = released, 0 = driven low.
 * sd_tick: d0.w = drive bits (CLK ignored).  Lowers CLK with them, samples the lines into
 * d1.w (the card changes its outputs after the falling edge), raises CLK (the card samples
 * on the rising edge).  a0 = IO_SDIO_ADDR.  Clobbers d2. */
sd_tick:
	move.w	%d0,%d2
	andi.w	#0x3D,%d2
	move.w	%d2,(%a0)			/* CLK low */
	move.w	(%a0),%d1			/* line levels */
	ori.w	#SDIO_CLK_MASK,%d2
	move.w	%d2,(%a0)			/* CLK high */
	rts

/* sd_send: a1 -> 48-bit command frame (6 bytes, CRC7 and end bit included), MSB first.
 * Clobbers d0-d4, d7. */
sd_send:
	moveq	#5,%d7
1:	move.b	(%a1)+,%d3
	moveq	#7,%d4
2:	move.w	#0x3F,%d0
	add.b	%d3,%d3				/* MSB -> C */
	bcs.s	3f
	andi.w	#0x3B,%d0			/* CMD low */
3:	bsr	sd_tick
	dbra	%d4,2b
	dbra	%d7,1b
	rts

/* sd_resp: d5.w = response length in bits (48 or 136), a2 -> buffer.  Waits up to 64
 * clocks for the start bit, then stores the frame MSB first (start bit included).
 * d0.l = 0, or -1 on timeout.  Clobbers d1-d4, d6, d7. */
sd_resp:
	moveq	#63,%d7
1:	move.w	#0x3F,%d0
	bsr	sd_tick
	btst	#SDIO_CMD_BIT,%d1
	beq.s	2f
	dbra	%d7,1b
	moveq	#-1,%d0
	rts
2:	move.w	%d5,%d7
	subq.w	#2,%d7				/* d5-1 more bits */
	moveq	#0,%d6				/* accumulator, start bit = 0 already in */
	moveq	#1,%d3				/* bits in the accumulator */
3:	move.w	#0x3F,%d0
	bsr	sd_tick
	add.b	%d6,%d6
	btst	#SDIO_CMD_BIT,%d1
	beq.s	4f
	addq.b	#1,%d6
4:	addq.w	#1,%d3
	cmpi.w	#8,%d3
	bne.s	5f
	move.b	%d6,(%a2)+
	moveq	#0,%d3
	moveq	#0,%d6
5:	dbra	%d7,3b
	moveq	#0,%d0
	rts

/* sd_read1: one 512-byte block in 1-bit mode into SD_BUF (start bit on DAT0 within 1000
 * clocks, 4096 data bits, 16 CRC bits and the end bit are clocked but not checked).
 * d0.l = 0 / -1.  Clobbers d1-d4, d6, d7, a2. */
sd_read1:
	move.w	#999,%d7
1:	move.w	#0x3F,%d0
	bsr	sd_tick
	btst	#SDIO_DAT0_BIT,%d1
	beq.s	2f
	dbra	%d7,1b
	moveq	#-1,%d0
	rts
2:	lea	SD_BUF,%a2
	move.w	#511,%d7
3:	moveq	#7,%d4
	moveq	#0,%d6
4:	move.w	#0x3F,%d0
	bsr	sd_tick
	add.b	%d6,%d6
	btst	#SDIO_DAT0_BIT,%d1
	beq.s	5f
	addq.b	#1,%d6
5:	dbra	%d4,4b
	move.b	%d6,(%a2)+
	dbra	%d7,3b
	moveq	#16,%d7				/* 16 CRC bits + end bit */
6:	move.w	#0x3F,%d0
	bsr	sd_tick
	dbra	%d7,6b
	moveq	#0,%d0
	rts

/* sd_cmd48: a1 -> frame; sends it and reads a 48-bit response into RESP_BUF.  d0.l = 0/-1. */
sd_cmd48:
	bsr	sd_send
	lea	RESP_BUF,%a2
	moveq	#48,%d5
	bra	sd_resp

sd_test:
	lea	IO_SDIO_ADDR,%a0
	moveq	#79,%d7				/* 80 clocks, everything released */
1:	move.w	#0x3F,%d0
	bsr	sd_tick
	dbra	%d7,1b

	/* CMD0: must get NO response */
	lea	cmd0,%a1
	bsr	sd_cmd48
	tst.l	%d0
	beq	sd_fail
	ori.w	#SD_OK_CMD0,RES_SD

	/* CMD8 -> R7: index 8, echo 0x000001AA */
	lea	cmd8,%a1
	bsr	sd_cmd48
	tst.l	%d0
	bne	sd_fail
	move.b	RESP_BUF,RES_SDR1+0
	cmpi.b	#0x08,RESP_BUF
	bne	sd_fail
	cmpi.b	#0x01,RESP_BUF+3
	bne	sd_fail
	cmpi.b	#0xAA,RESP_BUF+4
	bne	sd_fail
	ori.w	#SD_OK_CMD8,RES_SD

	/* CMD55 + ACMD41 (HCS) until OCR bit 31, at most 10 times */
	moveq	#9,%d7
acmd41_loop:
	move.w	%d7,-(%sp)
	lea	cmd55,%a1
	bsr	sd_cmd48
	lea	acmd41,%a1
	bsr	sd_cmd48
	move.w	(%sp)+,%d7
	tst.l	%d0
	bne	sd_fail
	addq.b	#1,RES_SDR1+1			/* polls */
	btst	#7,RESP_BUF+1			/* OCR bit 31: ready */
	bne.s	acmd41_ok
	dbra	%d7,acmd41_loop
	bra	sd_fail
acmd41_ok:
	move.l	RESP_BUF+1,RES_SDOCR
	ori.w	#SD_OK_ACMD41,RES_SD

	/* CMD2 -> R2 (136 bits), CMD3 -> R6 with RCA 0x1234 */
	lea	cmd2,%a1
	bsr	sd_send
	lea	RESP_BUF,%a2
	move.w	#136,%d5
	bsr	sd_resp
	tst.l	%d0
	bne	sd_fail
	cmpi.b	#0x3F,RESP_BUF
	bne	sd_fail
	lea	cmd3,%a1
	bsr	sd_cmd48
	tst.l	%d0
	bne	sd_fail
	cmpi.w	#0x1234,RESP_BUF+1
	bne	sd_fail
	ori.w	#SD_OK_IDENT,RES_SD

	/* CMD7 (RCA 0x1234) -> R1, state tran */
	lea	cmd7,%a1
	bsr	sd_cmd48
	tst.l	%d0
	bne	sd_fail
	move.b	RESP_BUF,RES_SDR1+2
	cmpi.b	#0x07,RESP_BUF
	bne	sd_fail
	ori.w	#SD_OK_CMD7,RES_SD

	/* CMD17 block 0 -> R1, then the block on DAT0 */
	lea	cmd17,%a1
	bsr	sd_cmd48
	tst.l	%d0
	bne	sd_fail
	move.b	RESP_BUF,RES_SDR1+3
	cmpi.b	#0x11,RESP_BUF
	bne	sd_fail
	bsr	sd_read1
	tst.l	%d0
	bne	sd_fail
	ori.w	#SD_OK_DATA,RES_SD
	lea	SD_BUF,%a2
	lea	RES_SDDATA,%a3
	moveq	#3,%d7
4:	move.l	(%a2)+,(%a3)+
	dbra	%d7,4b

sd_fail:
	moveq	#7,%d7				/* leave the bus released, 8 clocks */
1:	move.w	#0x3F,%d0
	bsr	sd_tick
	dbra	%d7,1b
	rts

/* ---------------------------------------------------------------------- */
/* Exception handlers */

/* Level-1 autovector: count, sample INTSTAT, clear the flag. */
vec_vblank:
	addq.l	#1,RES_VBLANKS
	move.w	GPIO_SHADOW,IO_GPIO_ADDR	/* (audio left on its PDM pattern) */
	move.w	IO_INTSTAT_ADDR,RES_INTSTAT
	move.w	#INTSTAT_VBLANK_MASK,IO_INTSTAT_ADDR
	rte

/* Level-3 autovector (DUART): drain the channel A FIFO into RES_RXBUF,
 * acknowledge a counter/timer tick with STOP_CTR. */
vec_duart:
	movem.l	%d0-%d1/%a0,-(%sp)
	move.b	DUART_ISR_ADDR,%d1
	btst	#ISR_RXRDYA_BIT,%d1
	beq.s	3f
	addq.w	#1,RES_IRQCNT
	move.b	%d1,RES_ISRVAL
	move.b	IO_INTSTAT_ADDR+1,RES_INTSTAT3
	move.l	RXBUF_PTR,%a0
1:	btst	#SRA_RXRDY_BIT,DUART_SRA_ADDR
	beq.s	2f
	move.b	DUART_RBA_ADDR,%d0
	cmpa.l	#RES_RXBUF+4,%a0
	bhs.s	1b				/* buffer full: discard */
	move.b	%d0,(%a0)+
	bra.s	1b
2:	move.l	%a0,RXBUF_PTR
3:	btst	#ISR_CTR_BIT,%d1
	beq.s	4f
	tst.b	DUART_STOP_CTR_ADDR		/* clears ISR.CTR (timer keeps running) */
	addq.w	#1,RES_CTCNT
4:	movem.l	(%sp)+,%d0-%d1/%a0
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
	.even
cmd0:	.byte	0x40,0x00,0x00,0x00,0x00,0x95
cmd8:	.byte	0x48,0x00,0x00,0x01,0xAA,0x87
cmd55:	.byte	0x77,0x00,0x00,0x00,0x00,0x65
acmd41:	.byte	0x69,0x40,0x00,0x00,0x00,0x77
cmd2:	.byte	0x42,0x00,0x00,0x00,0x00,0x4D
cmd3:	.byte	0x43,0x00,0x00,0x00,0x00,0x21
cmd7:	.byte	0x47,0x12,0x34,0x00,0x00,0x59
cmd17:	.byte	0x51,0x00,0x00,0x00,0x00,0x55
