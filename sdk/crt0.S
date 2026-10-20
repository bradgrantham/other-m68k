/* other-m68k user program startup. Entered in user mode by the BIOS (`go`, `run`, or
 * sys_exec) with USP set up as for a call `_start(argc, argv)`: 4(sp) = argc, 8(sp) =
 * argv (NULL-terminated array; argv[0] is the program path for `run`/`exec`, argc = 0
 * and argv[0] == NULL under `go`), and the return address on the stack exits the program,
 * so even a bare `rts` from _start is safe. Clears .bss, calls main(argc, argv), then
 * exit(). Static constructors are not run (no crti/crtn with -nostartfiles). */
	.section .text.startup,"ax"
	.globl	_start
_start:
	lea	__bss_start,%a0
	lea	__bss_end,%a1
1:	cmp.l	%a1,%a0
	bhs	2f
	clr.l	(%a0)+
	bra	1b
2:	move.l	8(%sp),%a0		/* argv */
	move.l	4(%sp),%d0		/* argc */
	move.l	%a0,-(%sp)
	move.l	%d0,-(%sp)
	jsr	main
	addq.l	#8,%sp
	move.l	%d0,-(%sp)
	jsr	exit			/* newlib: flushes stdio, runs atexit, then _exit() */
3:	bra	3b
