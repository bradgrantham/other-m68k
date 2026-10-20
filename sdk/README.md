# sdk/ -- other-m68k user programs

Programs are plain C with newlib, linked at `USER_BASE` (0x4000) and started by the BIOS
monitor (`go`, or stage 2 `run`) in user mode. Output goes to the video console and the
serial port; input comes from the PS/2 keyboard and the serial port.

```
make -C sdk      # build/libsys.a, build/crt0.o, build/hello.elf + .srec, build/plasma.elf + .srec
```

## What is here

| file | contents |
|---|---|
| `crt0.S` | `_start` at 0x4000: clears `.bss`, calls `main(0, NULL)`, then newlib `exit()` (flushes stdio) and `sys_exit` |
| `user.ld` | one load region from `../generated/user.ld`; `__bss_start/__bss_end/_end/__heap_start` |
| `newlib_shim.c` | the libgloss-style functions this newlib calls (`write read sbrk open close lseek fstat isatty kill getpid _exit stat link unlink times gettimeofday`, no leading underscore) mapped onto TRAP #15 |
| `../generated/syscall_stubs.S` | `sys_*` functions (`#include "syscalls.h"`); compiled into `build/libsys.a` |
| `examples/hello.c` | printf, `sys_video_info`, `sys_mem_info`, `sys_con_info`, wait for a key |
| `examples/plasma.c` | double-buffered RGB332 effect in 4x4 blocks, page flip + `sys_wait_vblank`, any key exits |

Link line (see the Makefile): `-m68000 -nostartfiles -T user.ld crt0.o prog.o
--start-group -lc -lsys --end-group -lgcc`. Add a program by dropping `foo.c` into
`examples/` and adding `foo` to `EXAMPLES`.

## Runtime model

* fd 0 = console input (`read()` is a small line editor with echo), fd 1/2 = console
  output. `stdout` is line buffered (isatty); call `fflush(stdout)` before waiting for input
  without a newline. Files (fd >= 3) need the stage 2 BIOS; until then they fail with
  `ENOSYS`.
* Heap: `sbrk` hands out `__heap_start` .. `mem_info.user_end - 16 KB`; the top 16 KB of the
  user region is the stack (USP starts at 0x1A7FC). Framebuffer page 1
  (0x1A800-0x2D3FF) is free RAM if you do not double buffer.
* The program runs in user mode: it cannot change SR or use privileged instructions.
  Interrupt hooks are installed with `sys_set_irq_handler(level, fn)` (levels 1-4; the hook
  is a plain `void fn(void)` called from the BIOS ISR in supervisor mode) and
  `sys_set_audio_cb(fn)` (15.7 kHz from HSYNC).
* Returning from `main` or calling `exit()` ends in `sys_exit`, which prints `[exit N]` and
  returns to the monitor. Page 0 is the console; a program that draws on it should
  `sys_con_clear()` before exiting.
* Static constructors are not run (no crti/crtn); C only.

## Uploading

Serial: wait for the `> ` prompt, then send `build/hello.srec` (e.g. `cat build/hello.srec >
/dev/ttyUSB0`, 38400 8N1). The monitor detects the S-records, prints a summary and `go`
runs the program. Emulator: pipe the file to stdin after the prompt appears, or type it
with `--type` for small programs.
