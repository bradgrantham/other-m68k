/* Level-4 (HSYNC) interrupt handlers: the PDM pattern streamer (spec rev 6).
 *
 * Two handlers; audio_update_mode() (audio.c) points autovector 28 at one of them, so the
 * common case carries no mode test:
 *   irq_hsync_fast  no audio callback, no level-4 user hook: write the next pattern byte of
 *                   the playing buffer to IO_AUDIO; at the end of a buffer release it and
 *                   take the next queued one, or go idle and switch INTEN.HSYNC off.
 *   irq_hsync_slow  a callback or hook is installed: C-ABI calls, so d1/a1 are saved too.
 *                   The callback, if any, owns IO_AUDIO and replaces the streamer; otherwise
 *                   audio_isr_service() (the C twin of the fast path) runs; then the hook.
 *
 * Clock count of the fast path, 68000 user's manual tables (4-clock bus cycles, no RAM
 * slot penalty):
 *   exception entry (autovector)               44
 *   movem.l d0/a0,-(sp)                        24   8+8n
 *   move.w #imm,(xxx).l        W1C HSYNC       20
 *   move.l (xxx).l,d0          play pointer    20
 *   beq.s (not taken)                           8
 *   movea.l d0,a0                               4
 *   move.b (a0)+,d0                             8
 *   move.w d0,(xxx).l          IO_AUDIO        16   (low byte = pattern, high byte ignored)
 *   move.l a0,(xxx).l          play pointer    20
 *   cmpa.l (xxx).l,a0          end pointer     22
 *   beq.s (not taken)                           8
 *   movem.l (sp)+,d0/a0                        28   12+8n
 *   rte                                        20
 *                                             242  of the 764 clocks in an NTSC line (32 %)
 * The end-of-buffer exit is taken once per buffer and costs about 100 more. */
#include "hw.inc"
#include "audio.h"

	.text
	.globl	irq_hsync
	.globl	irq_hsync_fast
irq_hsync:
irq_hsync_fast:
	movem.l	%d0/%a0,-(%sp)
	move.w	#INTSTAT_HSYNC_MASK,IO_INTSTAT_ADDR	/* W1C */
	move.l	audio_state+AUD_PLAY,%d0
	beq.s	off
	move.l	%d0,%a0
	move.b	(%a0)+,%d0
	move.w	%d0,IO_AUDIO_ADDR		/* pattern in the low byte; high byte ignored */
	move.l	%a0,audio_state+AUD_PLAY
	cmpa.l	audio_state+AUD_END,%a0
	beq.s	endbuf
done:
	movem.l	(%sp)+,%d0/%a0
	rte

	/* the buffer just finished: release it (head++) and take the next one if queued */
endbuf:
	lea	audio_state,%a0
	addq.b	#1,AUD_HEAD(%a0)
	moveq	#0,%d0
	move.b	AUD_HEAD(%a0),%d0
	cmp.b	AUD_TAIL(%a0),%d0
	beq.s	drained
	and.w	#AUD_FIFO_MASK,%d0
	add.w	#AUD_FIFO,%d0
	move.b	(%a0,%d0.w),%d0			/* buffer index 0..AUDIO_NBUF-1 */
	and.w	#0xFF,%d0
	lsl.w	#8,%d0
	add.w	%d0,%d0				/* * AUDIO_BUF_MAX (512) */
	add.l	%a0,%d0				/* buf[idx] (AUD_BUF == 0) */
	move.l	%d0,AUD_PLAY(%a0)
	add.l	AUD_BUFSIZE(%a0),%d0
	move.l	%d0,AUD_END(%a0)
	bra.s	done

	/* nothing queued: an underrun holds the last pattern; the end of the stream goes to
	 * the idle pattern and closes it. Either way the interrupt is switched off (no hook
	 * exists in fast mode); audio_next switches it back on when it queues a buffer. */
drained:
	clr.l	AUD_PLAY(%a0)
	tst.b	AUD_ENDING(%a0)
	beq.s	off
	move.w	#AUDIO_IDLE_PATTERN,IO_AUDIO_ADDR
	clr.b	AUD_STARTED(%a0)
off:
	andi.w	#~INTEN_HSYNC_MASK,inten_shadow
	move.w	inten_shadow,IO_INTEN_ADDR
	bra.s	done

	.globl	irq_hsync_slow
irq_hsync_slow:
	movem.l	%d0-%d1/%a0-%a1,-(%sp)
	move.w	#INTSTAT_HSYNC_MASK,IO_INTSTAT_ADDR	/* W1C */
	move.l	audio_cb,%d0
	beq.s	1f
	move.l	%d0,%a1
	jsr	(%a1)				/* callback owns IO_AUDIO */
	bra.s	2f
1:	jsr	audio_isr_service
2:	move.l	irq_user_hook+4*IRQ_HSYNC_LEVEL,%d0
	beq.s	3f
	move.l	%d0,%a1
	jsr	(%a1)
3:	movem.l	(%sp)+,%d0-%d1/%a0-%a1
	rte
