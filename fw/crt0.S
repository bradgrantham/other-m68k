/* other-m68k BIOS: vectors, reset entry, exception/trap entry points, user-mode launch.
 *
 * Layout (see README.md):
 *   .vectors  256 longs at ROM start (overlaid at 0 while the reset overlay is set).
 *             [0] initial SSP, [1] initial PC (_start, in ROM so the overlay clears on the
 *             first instruction fetch). IRQ autovectors 25..28 and TRAP #15 (47) point
 *             directly at their handlers; every other vector points at a 10-byte stub
 *             that pushes its vector number and jumps to exception_common.
 *   After POST the table is copied to RAM at 0 so programs can hook vectors.
 */
#include "hw.inc"

	.equ	STUB_SIZE, 10		/* move.w #imm,-(sp) (4) + jmp abs.l (6) */
/* GPIO value with the open-drain PS/2 lines released (= IO_GPIO_RESET, which hw.inc lacks). */
	.equ	GPIO_IDLE, GPIO_PS2CLK_MASK + GPIO_PS2DAT_MASK

/* ------------------------------------------------------------------------- */
	.section .vectors,"a"
	.globl	_vectors
_vectors:
	.long	SYS_STACK_TOP		/* 0: initial SSP */
	.long	_start			/* 1: initial PC  */
	.set	vn, 2
	.rept	254
	.if	vn == 25
	.long	irq_vblank		/* level 1 autovector */
	.elseif	vn == 26
	.long	irq_ps2			/* level 2 autovector */
	.elseif	vn == 27
	.long	irq_duart		/* level 3 autovector */
	.elseif	vn == 28
	.long	irq_hsync		/* level 4 autovector */
	.elseif	vn == 47
	.long	trap15_handler		/* TRAP #15 = system call */
	.else
	.long	vec_stubs + (vn - 2) * STUB_SIZE
	.endif
	.set	vn, vn + 1
	.endr

/* ------------------------------------------------------------------------- */
	.text

/* Per-vector stubs. Each is exactly STUB_SIZE bytes; the table above relies on that. */
	.globl	vec_stubs
vec_stubs:
	.set	vn, 2
	.rept	254
	move.w	#vn,-(%sp)
	jmp	(exception_common).l
	.set	vn, vn + 1
	.endr
	.if	(. - vec_stubs) != 254 * STUB_SIZE
	.error	"vector stub size mismatch"
	.endif

/* ------------------------------------------------------------------------- */
/* Reset entry. No RAM may be assumed until post_run has passed. The DUART is set up
 * first (duart.S, registers only) so that the POST can report through it. */
	.globl	_start
_start:
	move.w	#0x2700,%sr
	move.w	#0,IO_INTEN_ADDR		/* no interrupts */
	move.w	#(GPIO_IDLE + GPIO_LED0_MASK),%d7	/* GPIO shadow: PS/2 lines released, LED0 on */
	move.w	%d7,IO_GPIO_ADDR
	lea	1f,%a6
	jmp	duart_reset_init		/* 38400 8N1, TX+RX enabled, IMR = 0 */
1:	jmp	post_run			/* registers only; comes back to post_return */

/* post.S jumps here on success: d7 = GPIO shadow, d6.w = 16-bit ROM word sum (0 = good). */
	.globl	post_return
post_return:
	lea	_sys_stack_top,%sp
	/* copy the vector table ROM -> RAM 0 */
	lea	ROM_BASE,%a0
	suba.l	%a1,%a1
	move.w	#(VECTORS_SIZE / 4) - 1,%d0
1:	move.l	(%a0)+,(%a1)+
	dbf	%d0,1b
	/* copy .data */
	lea	_data_load,%a0
	lea	_data_start,%a1
	lea	_data_end,%a2
2:	cmp.l	%a2,%a1
	bhs	3f
	move.l	(%a0)+,(%a1)+
	bra	2b
3:	/* zero .bss */
	lea	_bss_start,%a1
	lea	_bss_end,%a2
4:	cmp.l	%a2,%a1
	bhs	5f
	clr.l	(%a1)+
	bra	4b
5:	/* hand POST results to C */
	move.w	%d6,post_rom_sum
	move.w	%d7,gpio_shadow
	jsr	bios_main
	bra	monitor_restart

/* ------------------------------------------------------------------------- */
/* Common exception entry. On entry the stack holds: vector.w, [group-0 extension],
 * SR.w, PC.l. Group-0 (vectors 2 and 3) 68000 frame after the vector word:
 *   +0 FC word (bit4 = R/W, 1 = read; bit3 = I/N; bits 2..0 = function code)
 *   +2 access address (long)
 *   +6 instruction register
 *   +8 SR, +10 PC
 * Everything is saved into `struct exc_frame exc` (exception.c) and C takes over on a
 * fresh supervisor stack. Never returns to the faulting code.
 */
	.globl	exception_common
exception_common:
	ori.w	#0x0700,%sr
	movem.l	%d0-%d7/%a0-%a6,exc		/* exc.d[8], exc.a[7] at offset 0..59 */
	move.w	(%sp)+,%d0			/* vector number */
	move.w	%d0,exc+78
	clr.w	exc+84				/* group0 = 0 */
	cmp.w	#2,%d0
	beq	1f
	cmp.w	#3,%d0
	bne	2f
1:	move.w	#1,exc+84
	move.w	(%sp)+,exc+80			/* fc */
	move.l	(%sp)+,exc+72			/* fault address */
	move.w	(%sp)+,exc+82			/* ir */
2:	move.w	(%sp)+,exc+76			/* sr */
	move.l	(%sp)+,exc+68			/* pc */
	move.l	%sp,exc+60			/* ssp after the frame */
	move.l	%usp,%a0
	move.l	%a0,exc+64			/* usp */
	lea	_sys_stack_top,%sp
	jsr	exception_entry
	bra	monitor_restart

/* ------------------------------------------------------------------------- */
/* TRAP #15 system call. struct trap_frame { d0,d1,d2,d3,a0,a1 } is the movem image;
 * syscall_dispatch() writes the result into f->d0. Runs with the caller's interrupt mask
 * (user programs: all enabled) on the supervisor stack.
 */
	.globl	trap15_handler
trap15_handler:
	movem.l	%d0-%d3/%a0-%a1,-(%sp)
	pea	(%sp)
	jsr	syscall_dispatch
	addq.l	#4,%sp
	movem.l	(%sp)+,%d0-%d3/%a0-%a1
	rte

/* ------------------------------------------------------------------------- */
/* void run_user(uint32_t pc) -- start a program in user mode. USP = USER_STACK_TOP - 4
 * with the address of user_exit_stub on the stack, so a stray `rts` from the entry point
 * becomes sys_exit(0). Supervisor stack is reset; bios_exit returns to the monitor. */
	.globl	run_user
run_user:
	move.l	4(%sp),%a0
	lea	_sys_stack_top,%sp
	lea	USER_END - 4,%a1
	move.l	#user_exit_stub,(%a1)
	move.l	%a1,%usp
	moveq	#0,%d0
	moveq	#0,%d1
	move.l	%d0,%d2
	move.l	%d0,%d3
	move.l	%d0,%d4
	move.l	%d0,%d5
	move.l	%d0,%d6
	move.l	%d0,%d7
	suba.l	%a1,%a1
	suba.l	%a2,%a2
	suba.l	%a3,%a3
	suba.l	%a4,%a4
	suba.l	%a5,%a5
	suba.l	%a6,%a6
	move.l	%a0,-(%sp)			/* PC */
	move.w	#0x0000,-(%sp)			/* SR: user mode, interrupts enabled */
	rte

	.globl	user_exit_stub
user_exit_stub:
	moveq	#0,%d1
	moveq	#SYS_exit,%d0
	trap	#SYSCALL_TRAP
	bra	user_exit_stub

/* ------------------------------------------------------------------------- */
/* void monitor_restart(void) -- fresh supervisor stack, interrupts on, into the monitor. */
	.globl	monitor_restart
monitor_restart:
	move.w	#0x2700,%sr
	lea	_sys_stack_top,%sp
	move.w	#0x2000,%sr
	jsr	monitor_main
	bra	monitor_restart

/* void cpu_reset(void) -- RESET instruction then the reset path. The glue is not reset
 * (overlay stays clear), which is fine because _start lives in ROM. */
	.globl	cpu_reset
cpu_reset:
	move.w	#0x2700,%sr
	reset
	jmp	_start

/* uint16_t irq_disable(void) -- returns previous SR; void irq_restore(uint16_t sr). */
	.globl	irq_disable
irq_disable:
	move.w	%sr,%d0
	ori.w	#0x0700,%sr
	rts
	.globl	irq_restore
irq_restore:
	move.w	6(%sp),%sr
	rts
