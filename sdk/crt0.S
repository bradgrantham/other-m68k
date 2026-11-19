/* other-m68k user program startup. Entered in user mode by the BIOS (`go`, `run`, or
 * sys_exec) with USP set up as for a call `_start(argc, argv)`: 4(sp) = argc, 8(sp) =
 * argv (NULL-terminated array; argv[0] is the program path for `run`/`exec`, argc = 0
 * and argv[0] == NULL under `go`), and the return address on the stack exits the program,
 * so even a bare `rts` from _start is safe. Clears .bss, runs the static constructors,
 * calls main(argc, argv), then exit(), which runs the static destructors.
 *
 * Constructors and destructors are newlib's job: __libc_init_array walks .preinit_array
 * and .init_array (user.ld) and __libc_fini_array walks .fini_array backwards. The latter
 * is registered with atexit before any constructor runs, so at exit() it comes after the
 * program's own atexit handlers and after the destructors of C++ static objects (which
 * their constructors register through __cxa_atexit), in the reverse order of
 * registration. A program that ends with sys_exit() or _exit() runs none of them. */
	.section .text.startup,"ax"
	.globl	_start
_start:
	lea	__bss_start,%a0
	lea	__bss_end,%a1
1:	cmp.l	%a1,%a0
	bhs	2f
	clr.l	(%a0)+
	bra	1b
2:	pea	__libc_fini_array
	jsr	atexit
	addq.l	#4,%sp
	jsr	__libc_init_array
	move.l	8(%sp),%a0		/* argv */
	move.l	4(%sp),%d0		/* argc */
	move.l	%a0,-(%sp)
	move.l	%d0,-(%sp)
	jsr	main
	addq.l	#8,%sp
	move.l	%d0,-(%sp)
	jsr	exit			/* newlib: runs atexit and the destructors, flushes stdio, then _exit() */
3:	bra	3b

/* The third argument of __cxa_atexit in every C++ static object's constructor is
 * &__dso_handle. crtbegin.o would define it; -nostartfiles leaves that out. Only its
 * address matters. */
	.globl	__dso_handle
	.equ	__dso_handle, _start
