/* Generated from spec/board.json by tools/gen.py -- DO NOT EDIT; edit the spec and run `make gen`. */
/* User-side TRAP stubs. m68k-elf gcc passes every argument on the stack, 4 bytes each,
   first argument at 4(sp). Integer args go to d1..d3 and pointer args to a0..a1 in order;
   d2/d3 are callee-saved so they are preserved around the trap. */
	.text

	.globl sys_exit
	.type sys_exit, @function
sys_exit:
	move.l 4(%sp),%d1
	moveq #0,%d0
	trap #15
	rts

	.globl sys_putchar
	.type sys_putchar, @function
sys_putchar:
	move.l 4(%sp),%d1
	moveq #1,%d0
	trap #15
	rts

	.globl sys_getchar
	.type sys_getchar, @function
sys_getchar:
	moveq #2,%d0
	trap #15
	rts

	.globl sys_pollchar
	.type sys_pollchar, @function
sys_pollchar:
	moveq #3,%d0
	trap #15
	rts

	.globl sys_puts
	.type sys_puts, @function
sys_puts:
	move.l 4(%sp),%a0
	moveq #4,%d0
	trap #15
	rts

	.globl sys_serial_putc
	.type sys_serial_putc, @function
sys_serial_putc:
	move.l 4(%sp),%d1
	moveq #5,%d0
	trap #15
	rts

	.globl sys_serial_getc
	.type sys_serial_getc, @function
sys_serial_getc:
	moveq #6,%d0
	trap #15
	rts

	.globl sys_ticks
	.type sys_ticks, @function
sys_ticks:
	moveq #7,%d0
	trap #15
	rts

	.globl sys_wait_vblank
	.type sys_wait_vblank, @function
sys_wait_vblank:
	moveq #8,%d0
	trap #15
	rts

	.globl sys_video_info
	.type sys_video_info, @function
sys_video_info:
	move.l 4(%sp),%a0
	moveq #9,%d0
	trap #15
	rts

	.globl sys_video_set_page
	.type sys_video_set_page, @function
sys_video_set_page:
	move.l 4(%sp),%d1
	moveq #10,%d0
	trap #15
	rts

	.globl sys_open
	.type sys_open, @function
sys_open:
	move.l 4(%sp),%d1
	move.l 8(%sp),%a0
	moveq #11,%d0
	trap #15
	rts

	.globl sys_close
	.type sys_close, @function
sys_close:
	move.l 4(%sp),%d1
	moveq #12,%d0
	trap #15
	rts

	.globl sys_read
	.type sys_read, @function
sys_read:
	movem.l %d2,-(%sp)
	move.l 8(%sp),%d1
	move.l 12(%sp),%d2
	move.l 16(%sp),%a0
	moveq #13,%d0
	trap #15
	movem.l (%sp)+,%d2
	rts

	.globl sys_write
	.type sys_write, @function
sys_write:
	movem.l %d2,-(%sp)
	move.l 8(%sp),%d1
	move.l 12(%sp),%d2
	move.l 16(%sp),%a0
	moveq #14,%d0
	trap #15
	movem.l (%sp)+,%d2
	rts

	.globl sys_lseek
	.type sys_lseek, @function
sys_lseek:
	movem.l %d2/%d3,-(%sp)
	move.l 12(%sp),%d1
	move.l 16(%sp),%d2
	move.l 20(%sp),%d3
	moveq #15,%d0
	trap #15
	movem.l (%sp)+,%d2/%d3
	rts

	.globl sys_opendir
	.type sys_opendir, @function
sys_opendir:
	move.l 4(%sp),%a0
	moveq #16,%d0
	trap #15
	rts

	.globl sys_readdir
	.type sys_readdir, @function
sys_readdir:
	move.l 4(%sp),%d1
	move.l 8(%sp),%a0
	moveq #17,%d0
	trap #15
	rts

	.globl sys_closedir
	.type sys_closedir, @function
sys_closedir:
	move.l 4(%sp),%d1
	moveq #18,%d0
	trap #15
	rts

	.globl sys_sd_read_block
	.type sys_sd_read_block, @function
sys_sd_read_block:
	move.l 4(%sp),%d1
	move.l 8(%sp),%a0
	moveq #19,%d0
	trap #15
	rts

	.globl sys_sd_write_block
	.type sys_sd_write_block, @function
sys_sd_write_block:
	move.l 4(%sp),%d1
	move.l 8(%sp),%a0
	moveq #20,%d0
	trap #15
	rts

	.globl sys_set_irq_handler
	.type sys_set_irq_handler, @function
sys_set_irq_handler:
	move.l 4(%sp),%d1
	move.l 8(%sp),%a0
	moveq #21,%d0
	trap #15
	rts

	.globl sys_mem_info
	.type sys_mem_info, @function
sys_mem_info:
	move.l 4(%sp),%a0
	moveq #22,%d0
	trap #15
	rts

	.globl sys_con_set_cursor
	.type sys_con_set_cursor, @function
sys_con_set_cursor:
	movem.l %d2,-(%sp)
	move.l 8(%sp),%d1
	move.l 12(%sp),%d2
	moveq #23,%d0
	trap #15
	movem.l (%sp)+,%d2
	rts

	.globl sys_con_clear
	.type sys_con_clear, @function
sys_con_clear:
	moveq #24,%d0
	trap #15
	rts

	.globl sys_con_info
	.type sys_con_info, @function
sys_con_info:
	move.l 4(%sp),%a0
	moveq #25,%d0
	trap #15
	rts

	.globl sys_exec
	.type sys_exec, @function
sys_exec:
	move.l 4(%sp),%a0
	move.l 8(%sp),%a1
	moveq #26,%d0
	trap #15
	rts

	.globl sys_key_event
	.type sys_key_event, @function
sys_key_event:
	moveq #27,%d0
	trap #15
	rts

	.globl sys_sleep_ms
	.type sys_sleep_ms, @function
sys_sleep_ms:
	move.l 4(%sp),%d1
	moveq #28,%d0
	trap #15
	rts

	.globl sys_set_audio_cb
	.type sys_set_audio_cb, @function
sys_set_audio_cb:
	move.l 4(%sp),%a0
	moveq #29,%d0
	trap #15
	rts

	.globl sys_audio_start
	.type sys_audio_start, @function
sys_audio_start:
	move.l 4(%sp),%d1
	moveq #30,%d0
	trap #15
	rts

	.globl sys_audio_next
	.type sys_audio_next, @function
sys_audio_next:
	move.l 4(%sp),%a0
	move.l 8(%sp),%d1
	moveq #31,%d0
	trap #15
	rts

	.globl sys_audio_last
	.type sys_audio_last, @function
sys_audio_last:
	move.l 4(%sp),%a0
	move.l 8(%sp),%d1
	moveq #32,%d0
	trap #15
	rts

	.globl sys_unlink
	.type sys_unlink, @function
sys_unlink:
	move.l 4(%sp),%a0
	moveq #33,%d0
	trap #15
	rts

	.globl sys_rename
	.type sys_rename, @function
sys_rename:
	move.l 4(%sp),%a0
	move.l 8(%sp),%a1
	moveq #34,%d0
	trap #15
	rts

	.globl sys_mkdir
	.type sys_mkdir, @function
sys_mkdir:
	move.l 4(%sp),%a0
	moveq #35,%d0
	trap #15
	rts

	.globl sys_chdir
	.type sys_chdir, @function
sys_chdir:
	move.l 4(%sp),%a0
	moveq #36,%d0
	trap #15
	rts

	.globl sys_getcwd
	.type sys_getcwd, @function
sys_getcwd:
	move.l 4(%sp),%d1
	move.l 8(%sp),%a0
	moveq #37,%d0
	trap #15
	rts

	.globl sys_time
	.type sys_time, @function
sys_time:
	moveq #38,%d0
	trap #15
	rts

	.globl sys_set_time
	.type sys_set_time, @function
sys_set_time:
	move.l 4(%sp),%d1
	moveq #39,%d0
	trap #15
	rts

	.globl sys_joystick
	.type sys_joystick, @function
sys_joystick:
	moveq #40,%d0
	trap #15
	rts

	.globl sys_paddles
	.type sys_paddles, @function
sys_paddles:
	move.l 4(%sp),%a0
	moveq #41,%d0
	trap #15
	rts
