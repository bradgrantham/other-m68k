/* other-m68k user program startup. Entered in user mode by the BIOS (`go`, later `run`)
 * with USP set up and a return address on the stack that exits the program, so even a
 * bare `rts` from _start is safe. Clears .bss, calls main(0, NULL), then exit(). Static
 * constructors are not run (no crti/crtn with -nostartfiles). */
	.section .text.startup,"ax"
	.globl	_start
_start:
	lea	__bss_start,%a0
	lea	__bss_end,%a1
1:	cmp.l	%a1,%a0
	bhs	2f
	clr.l	(%a0)+
	bra	1b
2:	clr.l	-(%sp)			/* argv = NULL */
	clr.l	-(%sp)			/* argc = 0 */
	jsr	main
	addq.l	#8,%sp
	move.l	%d0,-(%sp)
	jsr	exit			/* newlib: flushes stdio, runs atexit, then _exit() */
3:	bra	3b
