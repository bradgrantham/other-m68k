# sdk/ -- other-m68k user programs

Programs are plain C with newlib, linked at `USER_BASE` (0x8000) and started by the BIOS
monitor in user mode: by name (`prog [args]`, found on the search path: the current
directory, then C:, then D:), `run prog.elf [args]`, `go` after an S-record upload, or
`sys_exec` from another program. Output goes to the video console; input comes from the
PS/2 keyboard; files live on drive C: (the 768 KB FAT12 volume in the flash) and drive D:
(the SD card). Serial port A is free after the POST: open `/dev/ttya` (or use
`sys_serial_putc`/`sys_serial_getc`) to talk to it.

```
make -C sdk      # build/libsys.a, build/crt0.o, build/{hello,plasma,ls,cat,fstest,bmpview,tune,abench,sdplay,term}.elf + .srec
```

The `.elf` files are what `run` loads (ELF32 big-endian m68k, one `PT_LOAD` segment at
0x8000, entry `_start` = 0x8000); copy them onto the card or onto C: (`HELLO.ELF`, so that
`hello` finds it). The top-level `make flash` puts all the examples on C:, stripped
(43-62 KB each; the loader reads only the program headers). The `.srec` files are for the
serial upload path.

## What is here

| file | contents |
|---|---|
| `crt0.S` | `_start` at 0x8000: clears `.bss`, reads `argc`/`argv` from the stack the BIOS built, calls `main(argc, argv)`, then newlib `exit()` (flushes stdio) and `sys_exit` |
| `user.ld` | one load region from `../generated/user.ld`; `__bss_start/__bss_end/_end/__heap_start` |
| `newlib_shim.c` | the libgloss-style functions this newlib calls (`write read sbrk open close lseek fstat isatty kill getpid _exit stat link unlink rename mkdir rmdir chdir getcwd times gettimeofday settimeofday`, no leading underscore) mapped onto TRAP #15; translates newlib's `O_CREAT/O_TRUNC/O_APPEND` values to the ABI's. `environ` is newlib's own (time functions pull in `getenv` for `TZ`, and with it newlib's `environ.o`) |
| `../generated/syscall_stubs.S` | `sys_*` functions (`#include "syscalls.h"`); compiled into `build/libsys.a` |
| `pdm.c`, `pdm.h` | PDM encoder for the audio stream (`#include "pdm.h"`, in `build/libsys.a`): `pdm_init` builds a 64 KB first-order sigma-delta table in `.bss` (0.4 s), `pdm_encode(dst, samples, n)` turns unsigned 8-bit samples into pattern bytes (83 clocks each) with the accumulator carried across calls, `pdm_reset`, `pdm_silence` |
| `examples/hello.c` | printf, `sys_video_info`, `sys_mem_info`, `sys_con_info`, wait for a key |
| `examples/plasma.c` | double-buffered RGB332 effect in 4x4 blocks, page flip + `sys_wait_vblank`, any key exits |
| `examples/ls.c` | `sys_opendir`/`sys_readdir`/`sys_closedir`, prints `argv` and `sys_getcwd`; lists the current directory by default (`ls.elf [path]`; plain `ls` is the monitor's built-in) with long names, sizes and stamps (`strftime`/`gmtime` of `dirent.mtime`) |
| `examples/cat.c` | stdio `fopen`/`fread` on files (-> `sys_open`/`sys_read`); `cat.elf FILE...` (plain `cat` is the monitor's) |
| `examples/fstest.c` | writes `TEST.TXT` with stdio (`O_CREAT`, `O_APPEND`), reads it back with the raw calls, reports OK/FAIL; `run fstest.elf exec ls.elf` then chains with `sys_exec` |
| `examples/tune.c` | plays "Ode to Joy" (two synthesised voices, square + triangle): 512-sample buffers encoded with `pdm_encode` and handed to the BIOS with a blocking `sys_audio_next`, printing the notes; any key stops; `tune` at the prompt |
| `examples/abench.c` | measures the CPU left to a program with each audio path active (BIOS stream, empty/C/asm `sys_set_audio_cb` callbacks) in clocks per video line, plus `pdm_init` time and `pdm_encode` cost; `abench` at the prompt |
| `examples/sdplay.c` | streams a sound file from the card: `sdplay FILE.PDM` plays raw pattern bytes (one per line, made on a host), `sdplay -pcm FILE.PCM` encodes unsigned 8-bit PCM at the line rate on the fly, `sdplay -half FILE.PCM` plays 8-bit PCM at half the line rate (7854 Hz); reports the elapsed time against the ideal and the share of time spent blocked in `sys_audio_next` (the CPU left over). With the 4-bit SD bus (spec rev 7, 5 ms per block) all three keep up; full-rate PCM with on-the-fly encoding has 57 % of the CPU to spare (with the SPI bus of rev B boards it fell 2-3 % short). `ODE.PDM`/`ODE.PCM` on the test image are the tune |
| `examples/bmpview.c` | BMP viewer: streaming separable box scaler + Floyd-Steinberg dither to RGB332 on page 1; also shows raw 320x240 RGB332 blobs; `make host-bmpview` builds it for the host (see below) |

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
* Files: `fopen`/`open`/`read`/`write`/`lseek`/`close`/`fstat`/`stat`, `unlink`,
  `rename`, `mkdir`, `rmdir`, `chdir` and `getcwd` work on both drives through `sys_open`
  etc. (8 files open at once). Paths are `[X:][/]dir/name`: `C:` is the flash volume, `D:`
  the card; without a drive letter a path is on the current drive, without a leading `/`
  relative to that drive's current directory. Each drive has its own current directory and
  there is a current drive (C: after reset); `chdir("D:")` switches drives,
  `chdir("D:/GAMES")` sets D:'s directory without switching, and the setting outlives the
  program. Names are long names up to 63 characters, case kept, matched case-insensitively.
  Writes to C: are cached per 8 KB erase sector and programmed when the file is closed or
  synced (or the cache must move): each program cycle masks interrupts for ~100 ms (audio
  holds its last pattern, serial input beyond the DUART's 3-byte FIFO is lost; `sys_ticks`
  and the clock are corrected afterwards), so close the files you write to C:.
  Directories are read with `sys_opendir`/`sys_readdir`(`struct dirent`: `name[64]` (the
  long name), `size`, `attr` with `ATTR_DIR`/`ATTR_READONLY`/`ATTR_HIDDEN`, `mtime` in
  seconds since 1970, 0 if none)/`sys_closedir` directly, see `examples/ls.c`. Errors come
  back as `errno` (`ENOENT`, `ENAMETOOLONG` for a name over 63 characters or malformed,
  `EEXIST`, `EACCES` for a read-only entry, `EFAULT` for a pointer outside user RAM,
  `ENODEV` for no card / no FAT volume, `EIO` for a card or flash error, `ENFILE`,
  `ENOSPC`, `EBADF`; the shim maps every `E_*` of the ABI to the newlib `errno` of the same
  name; `getcwd` gives `ERANGE` when the buffer is too small). The SD card is fixed media:
  do not pull it while the machine is on.
  `sys_exec(path, args)` replaces the running program (`path` is a file path; no search
  path); it returns a negative error only if the file could not be opened or is not a
  loadable ELF.
* `syscalls.h` defines `O_RDONLY`...`O_APPEND` and `SEEK_*` with the ABI's values, which
  differ from newlib's `<fcntl.h>`; do not include both in one file (the shim does the
  translation for `open()`).
* Heap: `sbrk` hands out `__heap_start` .. `mem_info.user_end - 16 KB`; the top 16 KB of the
  user region is the stack (USP starts at 0xDA7FC; the user region is 0x8000-0xDA7FF, 842 KB).
  Framebuffer page 1 (0xDA800-0xED3FF) is free RAM if you do not double buffer.
* The program runs in user mode: it cannot change SR or use privileged instructions.
  Interrupt hooks are installed with `sys_set_irq_handler(level, fn)` (levels 1-4; the hook
  is a plain `void fn(void)` called from the BIOS ISR in supervisor mode, at the interrupt's
  own level, so higher levels can still interrupt it) and
  `sys_set_audio_cb(fn)` (once per video line from HSYNC; the callback owns `IO_AUDIO`).
* Sound (spec rev 6): the BIOS streams bytes to the PDM shifter, one per video line (15707 Hz
  NTSC), from buffers you fill. `b = sys_audio_start(n)` (n up to `AUDIO_BUF_MAX` = 512)
  returns the first buffer; fill it with pattern bytes (`pdm_encode` from unsigned 8-bit
  samples, 128 = silence) and `b = sys_audio_next(b, 1)` hands it in and blocks until the next
  of the `AUDIO_NBUF` = 3 buffers is free, so the loop runs at the audio rate; `block == 0`
  returns NULL instead when none is free (the handed buffer still plays). `sys_audio_last(b,
  1)` hands in the last one and waits for the end. Streaming costs the program about a third
  of the CPU (242 of the 764 clocks in each line go to the interrupt) plus 83 clocks per sample
  for `pdm_encode`; silence costs nothing. See `examples/tune.c`.
* Returning from `main` or calling `exit()` ends in `sys_exit`, which prints `[exit N]` and
  returns to the monitor. Page 0 is the console; a program that draws on it should
  `sys_con_clear()` before exiting.
* Time: `clock()` and `times()` count milliseconds since boot (newlib's `CLOCKS_PER_SEC` is
  1000), derived from `sys_ticks()` and the frame length of the video standard
  (`video_info.standard`: 764 x 262 clocks = 59.95 Hz NTSC, 768 x 312 = 50.08 Hz PAL), so
  they advance in whole frames (16.7 / 20 ms steps). `gettimeofday()` (and so `time()`,
  `localtime()`, `strftime`) returns the BIOS clock (`sys_time`: seconds since 1970-01-01
  00:00 local time, no time zone, set with the monitor's `date` or `settimeofday()` /
  `sys_set_time`) once it is set and the uptime before that; `tv_usec` comes from the frame
  counter. Files written while the clock is unset get the stamp 2026-01-01 00:00.
  `sys_sleep_ms(ms)` waits `ms * ticks_per_sec / 1000` whole frames (nominal 60 or 50 per
  second) plus a calibrated loop for the rest of a frame.
* Static constructors are not run (no crti/crtn); C only.

## bmpview: pictures from the card

`run bmpview.elf PHOTO.BMP` fits the picture into the screen (aspect kept, letterboxed;
`-s` stretches) and shows it on the framebuffer page the console is not using; any key
returns. Uncompressed BMP with 1/4/8-bit palette, 16-bit (555 or bit fields), 24-bit and
32-bit (xRGB or bit fields) pixels, bottom-up or top-down, up to 4096x4096. The file is
read one row at a time; each row is box-filtered horizontally, then rows are box-filtered
vertically, with 8.8 fixed-point weight plans computed once per image (exact box filter
up to rounding, any ratio up or down), and every finished screen row is Floyd-Steinberg
dithered (serpentine, 7/16 3/16 5/16 1/16) to 3-3-2. It prints a time breakdown when done.

Speed measured on the rev B4 design (12 MHz 68000, SPI-mode SD at roughly 24 KB/s through FatFs);
with the rev 7 native 4-bit bus (about 85 KB/s through FatFs) the reading parts are about 3.5x
faster: the raw screen loads in 0.9 s in the emulator, the scale and dither times are unchanged.

| input | time |
|---|---|
| raw RGB332 screen, 76800 bytes | 3 s |
| 320x240 24-bit BMP (230 KB) | 25 s (read 10, scale 7, dither 8) |
| 100x75 8-bit BMP, upscaled | 13 s |
| 800x600 24-bit BMP (1.4 MB) | 95 s, 60 of them reading |

So anything larger than the screen is better converted on the host first. The host build
runs the identical pipeline and writes either a viewable PPM or the raw screen blob:

```
make -C sdk host-bmpview
sdk/build/host/bmpview photo.bmp photo.ppm            # look at it (320x240 PPM, dithered)
sdk/build/host/bmpview photo.bmp PHOTO.332 320 240    # 76800-byte blob for the card
```

`run bmpview.elf PHOTO.332` recognises the blob by its size (exactly width*height bytes,
row-major, top-down) and just copies it in. Any name works (long names are fine).

## Running

From C:: `make flash` (top level) puts the examples on the C: volume; type `hello` at the
`C:/> ` prompt (`dir` lists the drive). `make run` boots the emulator on that image.
SD card: copy the `.elf` to a FAT card and type `D:` then `hello` (or `D:hello`, or
`copy D:HELLO.ELF C:` to keep it on C:). Emulator: build an image with
`fw/test/mkimage.sh` (see `fw/README.md`) and pass it with `--sd`.

Serial: type `srec` at the prompt (on the keyboard), then send `build/hello.srec`
(e.g. `cat build/hello.srec > /dev/ttyUSB0`, 38400 8N1). The monitor prints a summary and
`go` runs the program. Emulator: `--type "srec\n"` and a delayed `--serial-type "$(cat
build/hello.srec)" --serial-type-delay 450` (bytes sent before `srec` runs are discarded).
