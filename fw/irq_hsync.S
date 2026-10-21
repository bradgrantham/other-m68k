/* Level-4 (HSYNC) interrupt: the PDM sample player (spec rev 5).
 *
 * Fast path (no audio callback, no level-4 user hook): clear the flag, pop one pre-encoded
 * pattern byte from the queue and write it to IO_AUDIO; on an empty queue write the idle
 * pattern and switch INTEN.HSYNC off (audio_push turns it back on).
 *
 * Clock count of the fast path, 68000 user's manual tables (read/write cycles 4 clocks,
 * no RAM slot penalty):
 *   exception entry (autovector)               44
 *   movem.l d0/a0,-(sp)                        24   8+8n
 *   move.w #imm,(xxx).l        W1C HSYNC       20
 *   lea (xxx).l,a0                             12
 *   tst.w d16(a0)                              12
 *   bne.s (not taken)                           8
 *   move.w d16(a0),d0                          12
 *   cmp.w d16(a0),d0                           12
 *   beq.s (not taken)                           8
 *   addq.w #1,d16(a0)                          16
 *   and.w #imm,d0                               8
 *   move.b (a0,d0.w),d0                        14
 *   move.w d0,(xxx).l          IO_AUDIO        16   (low byte = pattern)
 *   movem.l (sp)+,d0/a0                        28   12+8n
 *   rte                                        20
 *                                             254  of the 764 clocks in an NTSC line (33 %)
 * The empty-queue exit is taken once per drain and costs about 70 more.
 * Slow path (callback or hook installed): the C-ABI call needs d1/a1 saved too; the
 * callback, if any, replaces the queue service (it owns IO_AUDIO), then the hook runs. */
#include "hw.inc"
#include "audio.h"

	.text
	.globl	irq_hsync
irq_hsync:
	movem.l	%d0/%a0,-(%sp)
	move.w	#INTSTAT_HSYNC_MASK,IO_INTSTAT_ADDR	/* W1C */
	lea	audio_state,%a0
	tst.w	AUD_SLOW(%a0)
	bne.s	slow
	move.w	AUD_TAIL(%a0),%d0
	cmp.w	AUD_HEAD(%a0),%d0
	beq.s	empty
	addq.w	#1,AUD_TAIL(%a0)
	and.w	#AUD_RING_MASK,%d0
	move.b	(%a0,%d0.w),%d0			/* ring[] is at offset 0 */
	move.w	%d0,IO_AUDIO_ADDR		/* pattern in the low byte; high byte ignored */
done:
	movem.l	(%sp)+,%d0/%a0
	rte

empty:
	move.w	#AUDIO_IDLE_PATTERN,IO_AUDIO_ADDR
	andi.w	#~INTEN_HSYNC_MASK,inten_shadow
	move.w	inten_shadow,IO_INTEN_ADDR
	bra.s	done

slow:
	movem.l	%d1/%a1,-(%sp)
	move.l	audio_cb,%d0
	beq.s	1f
	move.l	%d0,%a1
	jsr	(%a1)				/* callback owns IO_AUDIO */
	bra.s	2f
1:	jsr	audio_isr_queue
2:	move.l	irq_user_hook+4*IRQ_HSYNC_LEVEL,%d0
	beq.s	3f
	move.l	%d0,%a1
	jsr	(%a1)
3:	movem.l	(%sp)+,%d1/%a1
	bra.s	done
