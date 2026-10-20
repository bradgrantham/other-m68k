/* other-m68k BIOS: memcpy / memmove / memset. Long moves when both pointers have the same
 * alignment (the 68000 needs even addresses for word/long access); byte loop otherwise. */
	.text

/* void *memset(void *dst, int c, size_t n) */
	.globl	memset
memset:
	move.l	%d2,-(%sp)
	move.l	8(%sp),%a0
	move.l	12(%sp),%d0
	move.l	16(%sp),%d1
	move.l	%a0,%a1
	andi.l	#0xFF,%d0
	cmpi.l	#8,%d1
	blo	3f
	move.l	%a1,%d2
	btst	#0,%d2
	beq	1f
	move.b	%d0,(%a1)+		/* align to even */
	subq.l	#1,%d1
1:	move.l	%d0,%d2			/* replicate the byte into a long */
	lsl.w	#8,%d2
	or.w	%d0,%d2
	move.w	%d2,-(%sp)
	swap	%d2
	move.w	(%sp)+,%d2
	bra	2f
4:	move.l	%d2,(%a1)+
2:	subq.l	#4,%d1
	bcc	4b
	addq.l	#4,%d1
3:	bra	6f
5:	move.b	%d0,(%a1)+
6:	subq.l	#1,%d1
	bcc	5b
	move.l	%a0,%d0
	move.l	(%sp)+,%d2
	rts

/* void *memcpy(void *dst, const void *src, size_t n) -- forward copy. */
	.globl	memcpy
memcpy:
	move.l	4(%sp),%a0
	move.l	8(%sp),%a1
	move.l	12(%sp),%d0
	movem.l	%a0/%d2,-(%sp)
	jsr	copy_fwd
	movem.l	(%sp)+,%a0/%d2
	move.l	%a0,%d0
	rts

/* void *memmove(void *dst, const void *src, size_t n) */
	.globl	memmove
memmove:
	move.l	4(%sp),%a0
	move.l	8(%sp),%a1
	move.l	12(%sp),%d0
	movem.l	%a0/%d2,-(%sp)
	cmp.l	%a0,%a1
	bhi	1f			/* src > dst: forward is safe */
	beq	2f
	/* src < dst: copy backwards */
	adda.l	%d0,%a0
	adda.l	%d0,%a1
	jsr	copy_bwd
	bra	2f
1:	jsr	copy_fwd
2:	movem.l	(%sp)+,%a0/%d2
	move.l	%a0,%d0
	rts

/* copy_fwd: a0 = dst, a1 = src, d0 = count. Clobbers d0-d2, a0, a1. */
copy_fwd:
	cmpi.l	#16,%d0
	blo	5f
	move.l	%a0,%d1
	move.l	%a1,%d2
	eor.b	%d2,%d1
	btst	#0,%d1
	bne	5f			/* different alignment: bytes only */
	btst	#0,%d2
	beq	1f
	move.b	(%a1)+,(%a0)+
	subq.l	#1,%d0
1:	move.l	%d0,%d1
	lsr.l	#4,%d1			/* 16-byte blocks */
	bra	3f
2:	move.l	(%a1)+,(%a0)+
	move.l	(%a1)+,(%a0)+
	move.l	(%a1)+,(%a0)+
	move.l	(%a1)+,(%a0)+
3:	subq.l	#1,%d1
	bcc	2b
	andi.l	#15,%d0
	bra	5f
4:	move.b	(%a1)+,(%a0)+
5:	subq.l	#1,%d0
	bcc	4b
	rts

/* copy_bwd: a0 = dst end, a1 = src end, d0 = count. */
copy_bwd:
	cmpi.l	#16,%d0
	blo	5f
	move.l	%a0,%d1
	move.l	%a1,%d2
	eor.b	%d2,%d1
	btst	#0,%d1
	bne	5f
	btst	#0,%d2
	beq	1f
	move.b	-(%a1),-(%a0)
	subq.l	#1,%d0
1:	move.l	%d0,%d1
	lsr.l	#4,%d1
	bra	3f
2:	move.l	-(%a1),-(%a0)
	move.l	-(%a1),-(%a0)
	move.l	-(%a1),-(%a0)
	move.l	-(%a1),-(%a0)
3:	subq.l	#1,%d1
	bcc	2b
	andi.l	#15,%d0
	bra	5f
4:	move.b	-(%a1),-(%a0)
5:	subq.l	#1,%d0
	bcc	4b
	rts
