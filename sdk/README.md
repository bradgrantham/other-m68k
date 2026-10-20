# sdk/ -- other-m68k user programs

Programs are plain C with newlib, linked at `USER_BASE` (0x4000) and started by the BIOS
monitor in user mode: `run prog.elf [args]` from the SD card, `go` after an S-record upload,
or `sys_exec` from another program. Output goes to the video console and the serial port;
input comes from the PS/2 keyboard and the serial port; files live on the FAT32 SD card.

```
make -C sdk      # build/libsys.a, build/crt0.o, build/{hello,plasma,ls,cat,fstest}.elf + .srec
```

The `.elf` files are what `run` loads (ELF32 big-endian m68k, one `PT_LOAD` segment at
0x4000, entry `_start` = 0x4000); copy them onto a FAT32 card (8.3 names: `HELLO.ELF`).
The `.srec` files are for the serial upload path.

## What is here

| file | contents |
|---|---|
| `crt0.S` | `_start` at 0x4000: clears `.bss`, reads `argc`/`argv` from the stack the BIOS built, calls `main(argc, argv)`, then newlib `exit()` (flushes stdio) and `sys_exit` |
| `user.ld` | one load region from `../generated/user.ld`; `__bss_start/__bss_end/_end/__heap_start` |
| `newlib_shim.c` | the libgloss-style functions this newlib calls (`write read sbrk open close lseek fstat isatty kill getpid _exit stat link unlink times gettimeofday`, no leading underscore) mapped onto TRAP #15; translates newlib's `O_CREAT/O_TRUNC/O_APPEND` values to the ABI's |
| `../generated/syscall_stubs.S` | `sys_*` functions (`#include "syscalls.h"`); compiled into `build/libsys.a` |
| `examples/hello.c` | printf, `sys_video_info`, `sys_mem_info`, `sys_con_info`, wait for a key |
| `examples/plasma.c` | double-buffered RGB332 effect in 4x4 blocks, page flip + `sys_wait_vblank`, any key exits |
| `examples/ls.c` | `sys_opendir`/`sys_readdir`/`sys_closedir`, prints `argv`; `run ls.elf [path]` |
| `examples/cat.c` | stdio `fopen`/`fread` on SD files (-> `sys_open`/`sys_read`); `run cat.elf FILE...` |
| `examples/fstest.c` | writes `TEST.TXT` with stdio (`O_CREAT`, `O_APPEND`), reads it back with the raw calls, reports OK/FAIL; `run fstest.elf exec ls.elf` then chains with `sys_exec` |

Link line (see the Makefile): `-m68000 -nostartfiles -T user.ld crt0.o prog.o
--start-group -lc -lsys --end-group -lgcc`. Add a program by dropping `foo.c` into
`examples/` and adding `foo` to `EXAMPLES`.

## Runtime model

* `main(argc, argv)`: under `run file args...` and `sys_exec(path, args)`, `argv[0]` is the
  path as typed and the rest of the line is split on blanks (max 16); under `go`, `argc` is
  0 and `argv[0]` is `NULL`. The BIOS places the strings and the array just below the stack
  top; they are the program's to keep.
* fd 0 = console input (`read()` is a small line editor with echo), fd 1/2 = console
  output. `stdout` is line buffered (isatty); call `fflush(stdout)` before waiting for input
  without a newline.
* Files: `fopen`/`open`/`read`/`write`/`lseek`/`close`/`fstat`/`stat` work on the SD card
  through `sys_open` etc. (8 files open at once, 8.3 names only, paths from the root, no
  current directory; `unlink`/`mkdir`/`rename` have no system calls). Directories are read
  with `sys_opendir`/`sys_readdir`(`struct dirent`: `name[64]`, `size`, `attr` with
  `ATTR_DIR`/`ATTR_READONLY`/`ATTR_HIDDEN`)/`sys_closedir` directly, see `examples/ls.c`.
  Errors come back as `errno` (`ENOENT`, `EINVAL` for a name that is not 8.3 or a pointer
  outside user RAM, `EIO` for no card / no FAT volume, `ENFILE`, `ENOSPC`, `EBADF`).
  `sys_exec(path, args)` replaces the running program; it returns a negative error only if
  the file could not be opened or is not a loadable ELF.
* `syscalls.h` defines `O_RDONLY`...`O_APPEND` and `SEEK_*` with the ABI's values, which
  differ from newlib's `<fcntl.h>`; do not include both in one file (the shim does the
  translation for `open()`).
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

## Running

SD card: copy the `.elf` to a FAT32 card and type `run hello.elf` (`dir` lists the card).
Emulator: build an image with `fw/test/mkimage.sh` (see `fw/README.md`) and pass it with
`--sd`.

Serial: wait for the `> ` prompt, then send `build/hello.srec` (e.g. `cat build/hello.srec >
/dev/ttyUSB0`, 38400 8N1). The monitor detects the S-records, prints a summary and `go`
runs the program. Emulator: pipe the file to stdin after the prompt appears, or type it
with `--type` for small programs.
