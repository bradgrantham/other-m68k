/* other-m68k BIOS: the part of the flash driver that must not run from the flash.
 *
 * While an SST39SF040 programs or erases, reads from it return status bits instead of
 * data, and once the first sector of the image has been rewritten the code of the ROM
 * that started the update is gone.  So everything from the first erase to the end of the
 * operation runs here.  The routine is assembled into .data, so crt0 copies it into
 * SYSDATA at boot and it is simply called by its symbol; it is position independent all
 * the same (register and PC-relative addressing, no absolute references except the DUART
 * and INTSTAT registers), interrupts masked by the caller.  The two chips (even and odd
 * byte lanes) see every command at once because every write is a word with the byte
 * repeated in both halves.
 *
 * int flash_ram_op(struct flash_op *op)    -- see flash.h / flash_layout.h:
 *   op 0: software ID: reads the two ID words at dst into id_mfr/id_dev and returns 0.
 *   op 1: whole image update: for each sector of sector_words starting at dst: erase,
 *         program the words from src (0xFFFF words are skipped), verify; a '.' per
 *         sector on the serial port.  On success it prints a line and jumps through
 *         the reset vector pair at dst (the new ROM boots); it never returns.  On a
 *         failure it prints which sector and stops the CPU (the ROM is incomplete:
 *         reset and retry, or program the chips in the TL866).
 *   op 2: one sector (the C: volume's write path): erase the sector at dst, program
 *         sector_words from src, verify, and return 0 (ok), 1 (erase timeout), 2 (program
 *         timeout) or 3 (verify mismatch).  While it polls it watches INTSTAT.VBLANK: each
 *         flag it sees it clears and counts into op->vblanks, so the caller can add the
 *         frames the masked VBLANK interrupt missed to the tick counter and the clock.
 */
#include "hw.inc"
#include "flash_layout.h"
	.data
	.globl	flash_ram_op
	.even
flash_ram_op:
	movem.l	%d2-%d7/%a2-%a5,-(%sp)		/* 10 registers = 40 bytes */
	move.l	44(%sp),%a4			/* op */
	move.l	FO_CMD1(%a4),%a2
	move.l	FO_CMD2(%a4),%a3
	move.l	FO_DST(%a4),%a0
	move.w	FO_OP(%a4),%d0
	cmpi.w	#2,%d0
	beq	one_sector
	tst.w	%d0
	bne	image_update
	/* ---- software ID ---- */
	move.w	#0xAAAA,(%a2)
	move.w	#0x5555,(%a3)
	move.w	#0x9090,(%a2)
	move.w	(%a0),FO_ID_MFR(%a4)
	move.w	2(%a0),FO_ID_DEV(%a4)
	move.w	#0xF0F0,(%a2)			/* back to read mode */
	moveq	#0,%d0
	movem.l	(%sp)+,%d2-%d7/%a2-%a5
	rts

	/* ---- op 2: one sector at a0 from src, then return (d6 counts VBLANK flags) ---- */
one_sector:
	move.l	FO_SRC(%a4),%a1
	moveq	#0,%d6
	move.l	%a0,%a5				/* sector start, for the verify pass */
	move.w	#0xAAAA,(%a2)
	move.w	#0x5555,(%a3)
	move.w	#0x8080,(%a2)
	move.w	#0xAAAA,(%a2)
	move.w	#0x5555,(%a3)
	move.w	#0x3030,(%a0)
	move.l	FO_ERASE_LOOPS(%a4),%d1
1:	bsr	chkvb
	cmpi.w	#0xFFFF,(%a0)
	beq	2f
	subq.l	#1,%d1
	bne	1b
	moveq	#1,%d0
	bra	9f
2:	moveq	#0,%d2
	move.w	FO_SECTOR_WORDS(%a4),%d2
3:	move.w	(%a1)+,%d3
	cmpi.w	#0xFFFF,%d3
	beq	5f				/* erased value: nothing to program */
	move.w	#0xAAAA,(%a2)
	move.w	#0x5555,(%a3)
	move.w	#0xA0A0,(%a2)
	move.w	%d3,(%a0)
	move.l	FO_PROG_LOOPS(%a4),%d1
4:	cmp.w	(%a0),%d3
	beq	5f
	bsr	chkvb
	subq.l	#1,%d1
	bne	4b
	moveq	#2,%d0
	bra	9f
5:	addq.l	#2,%a0
	subq.l	#1,%d2
	bne	3b
	move.w	FO_SECTOR_WORDS(%a4),%d2	/* verify */
	move.l	%a5,%a0
	move.l	FO_SRC(%a4),%a1
6:	cmpm.w	(%a0)+,(%a1)+
	bne	7f
	subq.w	#1,%d2
	bne	6b
	moveq	#0,%d0
	bra	9f
7:	moveq	#3,%d0
9:	move.l	%d6,FO_VBLANKS(%a4)
	movem.l	(%sp)+,%d2-%d7/%a2-%a5
	rts

/* chkvb: if INTSTAT.VBLANK is set, clear it (W1C) and count it in d6; clobbers d0 */
chkvb:	move.w	IO_INTSTAT_ADDR,%d0
	btst	#INTSTAT_VBLANK_BIT,%d0
	beq	1f
	move.w	#INTSTAT_VBLANK_MASK,IO_INTSTAT_ADDR
	addq.l	#1,%d6
1:	rts

	/* ---- image update: a0 = flash address, a1 = source, d4 = words left ---- */
image_update:
	move.l	FO_SRC(%a4),%a1
	move.l	FO_NWORDS(%a4),%d4
	moveq	#0,%d5				/* sector counter (for the failure message) */
sector:
	tst.l	%d4
	beq	done
	addq.l	#1,%d5
	/* sector erase at a0 */
	move.w	#0xAAAA,(%a2)
	move.w	#0x5555,(%a3)
	move.w	#0x8080,(%a2)
	move.w	#0xAAAA,(%a2)
	move.w	#0x5555,(%a3)
	move.w	#0x3030,(%a0)
	move.l	FO_ERASE_LOOPS(%a4),%d1
2:	cmpi.w	#0xFFFF,(%a0)			/* DQ7 reads 0 while erasing */
	beq	3f
	subq.l	#1,%d1
	bne	2b
	lea	msg_erase(%pc),%a5
	bra	fail
	/* program sector_words from a1 */
3:	moveq	#0,%d2
	move.w	FO_SECTOR_WORDS(%a4),%d2
	move.l	%a0,%a5				/* sector start, for the verify pass */
4:	move.w	(%a1)+,%d3
	cmpi.w	#0xFFFF,%d3
	beq	6f				/* erased value: nothing to program */
	move.w	#0xAAAA,(%a2)
	move.w	#0x5555,(%a3)
	move.w	#0xA0A0,(%a2)
	move.w	%d3,(%a0)
	move.l	FO_PROG_LOOPS(%a4),%d1
5:	cmp.w	(%a0),%d3			/* DQ7 is the complement of the data until done */
	beq	6f
	subq.l	#1,%d1
	bne	5b
	lea	msg_program(%pc),%a5
	bra	fail
6:	addq.l	#2,%a0
	subq.l	#1,%d4
	subq.l	#1,%d2
	bne	4b
	/* verify the sector just written */
	move.w	FO_SECTOR_WORDS(%a4),%d2
	move.l	%a5,%a0
	move.l	%a1,%d3
	sub.l	%d2,%d3
	sub.l	%d2,%d3
	exg	%d3,%a1				/* a1 back to the sector's source, d3 = end of source */
7:	cmpm.w	(%a0)+,(%a1)+
	bne	8f
	subq.w	#1,%d2
	bne	7b
	moveq	#'.',%d0
	bsr	putc
	bra	sector
8:	lea	msg_verify(%pc),%a5
	bra	fail

done:	lea	msg_done(%pc),%a5
	bsr	puts
	move.l	FO_DST(%a4),%a0			/* reset vectors of the new image */
	movea.l	(%a0),%sp
	movea.l	4(%a0),%a0
	jmp	(%a0)

/* a5 = reason text; d5 = sector number (1-based) */
fail:	move.l	%a5,%a1
	lea	msg_fail(%pc),%a5
	bsr	puts
	move.l	%a1,%a5
	bsr	puts
	move.l	%d5,%d0
	bsr	putdec
	lea	msg_fail2(%pc),%a5
	bsr	puts
	stop	#0x2700
	bra	.

/* puts: a5 = string ('\n' sent as CR LF); clobbers d0, a5 */
puts:	move.b	(%a5)+,%d0
	beq	1f
	cmpi.b	#'\n',%d0
	bne	2f
	moveq	#'\r',%d0
	bsr	putc
	moveq	#'\n',%d0
2:	bsr	putc
	bra	puts
1:	rts

/* putdec: d0 = value 0..65535; clobbers d0-d1, a5 */
putdec:	lea	-8(%sp),%sp
	move.l	%sp,%a5
	addq.l	#7,%a5
	clr.b	(%a5)
1:	divu	#10,%d0
	swap	%d0
	addi.b	#'0',%d0
	move.b	%d0,-(%a5)
	clr.w	%d0
	swap	%d0
	tst.w	%d0
	bne	1b
	bsr	puts
	lea	8(%sp),%sp
	rts

/* putc: d0.b on DUART channel A, polling TXRDY */
putc:	btst	#SRA_TXRDY_BIT,DUART_SRA_ADDR
	beq	putc
	move.b	%d0,DUART_TBA_ADDR
	rts

msg_done:	.asciz	"\nflash: image verified, starting the new ROM\n"
msg_fail:	.asciz	"\nflash: FAILED: "
msg_erase:	.asciz	"erase timeout in sector "
msg_program:	.asciz	"program timeout in sector "
msg_verify:	.asciz	"verify mismatch in sector "
msg_fail2:	.asciz	"\nThe ROM is incomplete. Press reset and run `flash` again if the monitor still starts;\notherwise program the chips in the TL866 (rom_even.bin / rom_odd.bin).\n"
	.even
