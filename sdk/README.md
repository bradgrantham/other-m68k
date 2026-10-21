# sdk/ -- other-m68k user programs

Programs are plain C with newlib, linked at `USER_BASE` (0x4000) and started by the BIOS
monitor in user mode: `run prog.elf [args]` from the SD card, `go` after an S-record upload,
or `sys_exec` from another program. Output goes to the video console; input comes from the
PS/2 keyboard; files live on the FAT32 SD card. Serial port A is free after the POST: open
`/dev/ttya` (or use `sys_serial_putc`/`sys_serial_getc`) to talk to it.

```
make -C sdk      # build/libsys.a, build/crt0.o, build/{hello,plasma,ls,cat,fstest,bmpview,tune,abench,sdplay,term}.elf + .srec
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
| `pdm.c`, `pdm.h` | PDM encoder for the audio stream (`#include "pdm.h"`, in `build/libsys.a`): `pdm_init` builds a 64 KB first-order sigma-delta table in `.bss` (0.4 s), `pdm_encode(dst, samples, n)` turns unsigned 8-bit samples into pattern bytes (83 clocks each) with the accumulator carried across calls, `pdm_reset`, `pdm_silence` |
| `examples/hello.c` | printf, `sys_video_info`, `sys_mem_info`, `sys_con_info`, wait for a key |
| `examples/plasma.c` | double-buffered RGB332 effect in 4x4 blocks, page flip + `sys_wait_vblank`, any key exits |
| `examples/ls.c` | `sys_opendir`/`sys_readdir`/`sys_closedir`, prints `argv`; `run ls.elf [path]` |
| `examples/cat.c` | stdio `fopen`/`fread` on SD files (-> `sys_open`/`sys_read`); `run cat.elf FILE...` |
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
* Files: `fopen`/`open`/`read`/`write`/`lseek`/`close`/`fstat`/`stat` work on the SD card
  through `sys_open` etc. (8 files open at once, 8.3 names only, paths from the root, no
  current directory; `unlink`/`mkdir`/`rename` have no system calls). Directories are read
  with `sys_opendir`/`sys_readdir`(`struct dirent`: `name[64]`, `size`, `attr` with
  `ATTR_DIR`/`ATTR_READONLY`/`ATTR_HIDDEN`)/`sys_closedir` directly, see `examples/ls.c`.
  Errors come back as `errno` (`ENOENT`, `ENAMETOOLONG` for a name that is not 8.3,
  `EFAULT` for a pointer outside user RAM, `ENODEV` for no card / no FAT volume, `EIO` for a
  card error, `ENFILE`, `ENOSPC`, `EBADF`; the shim maps every `E_*` of the ABI to the
  newlib `errno` of the same name).
  `sys_exec(path, args)` replaces the running program; it returns a negative error only if
  the file could not be opened or is not a loadable ELF.
* `syscalls.h` defines `O_RDONLY`...`O_APPEND` and `SEEK_*` with the ABI's values, which
  differ from newlib's `<fcntl.h>`; do not include both in one file (the shim does the
  translation for `open()`).
* Heap: `sbrk` hands out `__heap_start` .. `mem_info.user_end - 16 KB`; the top 16 KB of the
  user region is the stack (USP starts at 0xDA7FC; the user region is 0x4000-0xDA7FF, 858 KB).
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
* Time: `clock()` and `times()` count milliseconds (newlib's `CLOCKS_PER_SEC` is 1000) and
  `gettimeofday()` gives seconds/microseconds since boot, both derived from `sys_ticks()` and
  the frame length of the video standard (`video_info.standard`: 764 x 262 clocks = 59.95 Hz
  NTSC, 768 x 312 = 50.08 Hz PAL), so they advance in whole frames (16.7 / 20 ms steps).
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
row-major, top-down) and just copies it in. Any name works; 8.3 names only on the card.

## Running

SD card: copy the `.elf` to a FAT32 card and type `run hello.elf` (`dir` lists the card).
Emulator: build an image with `fw/test/mkimage.sh` (see `fw/README.md`) and pass it with
`--sd`.

Serial: type `srec` at the `> ` prompt (on the keyboard), then send `build/hello.srec`
(e.g. `cat build/hello.srec > /dev/ttyUSB0`, 38400 8N1). The monitor prints a summary and
`go` runs the program. Emulator: `--type "srec\n"` and a delayed `--serial-type "$(cat
build/hello.srec)" --serial-type-delay 450` (bytes sent before `srec` runs are discarded).
