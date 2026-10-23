# other-m68k Programmer's Guide

This guide describes **board rev C3, spec revision 11** (`"revision": 11` in `spec/board.json`,
`BOARD_REVISION` in `generated/hw.h`) and the BIOS/monitor in `fw/` as it stands in this
repository. Revision 5 changed only the audio hardware (an 8-bit PDM shifter behind a new
`AUDIO` register instead of a GPIO bit, a JED-only change for B4 boards). Revision 6 changed
only the audio system calls: the rev 5 sample queue (`audio_write`) and its 64 KB ROM encoder
table are gone, the BIOS now streams buffers of pattern bytes that the program fills
(`audio_start`, `audio_next`, `audio_last`), and the encoding of samples into patterns moved
into the SDK (`pdm_init`, `pdm_encode`). Revision 7 moved the SD card from SPI mode to its
native 4-bit bus: six open-drain lines bit-banged through `SDIO`, which now reads back the
line levels ([2.10](#210-sd-card)); `INTSTAT_SDMISO`, the `USERIO` register and the four user
pins on the expansion header are gone, their CPLD2 macrocells went to the extra SD lines. That
is a wiring change, board rev C1. The rev B4 boards that were ordered are wired for SPI: they
run the CPLD images and firmware at git tag `rev-b` (spec revision 6), or can be modified by
hand to the C1 wiring as `kicad/ORDER.md` describes. Revision 8 changed only the BIOS and the
SDK, not the board: serial port A carries the POST report and the banner and is then released,
so the console is the screen and the keyboard alone, and programs claim the port as the device
file `/dev/ttya` ([1.4](#serial-port-a-is-yours-devttya)). Revision 9 again changed only the
BIOS and the SDK: the BIOS image grew to 256 KB and the other 768 KB of the flash became a
FAT12 volume, drive C:, beside the SD card as drive D: ([1.7](#17-files-and-directories),
[2.12](#212-the-flash-rom)); the BIOS's RAM grew to 31 KB, so programs now load at 0x008000;
long file names, current directories, delete, rename and mkdir arrived with system calls
33-37, and a wall clock (38, 39) stamps the files; the monitor gained DOS-style file commands,
a search path for programs and `.CMD` scripts. Revision 10 is board rev C2, a layout change
that programs do not see: the connectors on a front and a back edge, an S-video output J10
beside the composite one, and DUART channel B on a serial header of its own, J11, in place of
the expansion header J8 ([2.13](#213-leds-user-pins-and-headers)). Revision 11, board rev C3,
adds an Atari joystick/paddle port, J12, on the DUART's input and output pins, and system calls
40 and 41 to read it ([1.4](#joystick-and-paddles), [2.11](#the-input-and-output-ports)).
Every address, bit and syscall name below comes from `spec/board.json` or from the headers generated from it
(`generated/hw.h`, `generated/syscalls.h`, `generated/hw.inc`). Where the spec, the firmware and
the other documents disagree, the text says so; the list is collected in
[Known discrepancies](#known-discrepancies) at the end.

## Preface

### The machine

| | |
|---|---|
| CPU | MC68000 at 12 MHz (24 MHz oscillator divided by 2 in CPLD1) |
| RAM | 1 MB SRAM at 0x000000 (2x AS6C4008, word wide), shared with the video fetch |
| ROM | 1 MB flash window at 0xE00000 (2x SST39SF040); the BIOS image is the first 256 KB, the other 768 KB hold a FAT12 volume, drive C: |
| Video | 320x240, one byte per pixel (RGB332), two pages in RAM, composite out through an AD724 (NTSC only on rev B4 and C1) |
| Glue | two ATF1508AS CPLDs: CPLD1 = decode, DTACK, SRAM address mux, video timing, soft switches; CPLD2 = I/O registers, pixel shifter, interrupt encoder, PS/2 receiver, PDM audio shifter, SD/GPIO pins |
| I/O | 68681 DUART (channel A on the serial A FTDI header, 38400 8N1: the POST report and the banner, then free for programs as `/dev/ttya`; channel B on the identical serial B header, board rev C2), PS/2 keyboard, Atari joystick/paddle port on the DUART's input and output pins (board rev C3), bit-banged microSD (native 4-bit SD bus, about 100 KB/s read; drive D:, FAT via FatFs in the BIOS), 8-bit PDM audio (one pattern byte per video line, shifted out by CPLD2 at about 126 kHz; the BIOS streams pattern bytes from buffers the program fills at the line rate, 15.7 kHz, and the SDK encodes 8-bit samples into them), two LEDs, NMI button; composite and S-video outputs (rev C2) |
| Interrupts | autovectored, vector = 24 + level: VBLANK 1, PS/2 2, DUART 3, HSYNC 4, NMI button 7 |

Programs are ELF files on drive C: (the flash) or D: (the SD card). The monitor loads one into
the user region of RAM (0x008000 upward) and starts it **in user mode**; the program reaches the BIOS through
`TRAP #15` system calls (`sys_*` functions in `generated/syscall_stubs.S`, prototypes in
`generated/syscalls.h`). The SDK links programs against newlib, so ordinary C with
`printf`, `malloc` and `fopen` works.

### Getting a program onto the machine

1. Build it with the SDK: drop `foo.c` into `sdk/examples/`, add `foo` to `EXAMPLES` in
   `sdk/Makefile`, run `make -C sdk`. The result is `sdk/build/foo.elf`.
2. Copy it to a FAT32 (or FAT16) card as `FOO.ELF`, or `foo.elf`: long names work, and case
   does not matter.
3. At the monitor prompt type `foo` on the PS/2 keyboard: a word that is not a monitor command
   is looked up as `FOO`, `FOO.ELF` and `FOO.CMD` in the current directory, then in `C:/`, then
   in `D:/`, and run with the rest of the line as arguments. `run D:/foo.elf args` names the
   file exactly. `copy D:FOO.ELF C:` puts it in the flash, where it stays without the card.
4. Or skip the card: build an image with `fw/test/mkimage.sh` and run the emulator with
   `--sd image`, then type `foo` in its window.

### Which part to read

* **Part 1, Writing applications**, is for a C programmer who wants to write a program
  quickly and does not want to know how the hardware works: build, console, joystick and
  serial port, graphics, files, sound, timing, and a reference table of every system call.
* **Part 2, Programming the hardware directly**, is for someone writing a game engine, an
  interpreter, a demo or bare-metal code who wants every cycle: memory map, bus timing,
  every register bit, interrupts and their cost, the SD/PS/2/DUART/joystick/flash protocols, and
  how to take the machine over from the BIOS.

### A note on measurements

Cycle counts marked *measured* were taken with the emulator (`emu/`), which charges the
68000's nominal instruction timings and does **not** model the slot-alignment penalty of RAM
and I/O accesses (Part 2, [The bus](#23-the-bus-and-video-time-slicing)). On the board, code and
data in RAM run up to roughly 10-25 % slower than in the emulator. The firmware has so far been
verified in the emulator and the CPLD testbench; `fw/STATUS.md` records that it has not yet
run on real hardware.

---

# Part 1: Writing applications

## 1.1 Toolchain and build

The cross compiler is crosstool-NG GCC 15 for `m68k-unknown-elf` with newlib, configured
`--with-cpu=68000`, installed in `~/x-tools/m68k-unknown-elf` (override with
`XTOOLS=/path/to/bin`). From the top of the repository:

```
make            # gen + fw + emu + sdk
make -C sdk     # just the SDK: sdk/build/libsys.a, crt0.o, and every example as .elf and .srec
```

`make -C sdk` compiles each program with
`-m68000 -Os -Wall -Wextra -ffunction-sections -fdata-sections -I../generated` and links it
with

```
-m68000 -nostartfiles -T user.ld -Wl,--gc-sections   crt0.o prog.o   -Wl,--start-group -lc -lsys -Wl,--end-group -lgcc
```

`libsys.a` holds the syscall stubs, `newlib_shim.c` (the `read`/`write`/`sbrk`/`open`...
layer newlib calls) and the PDM audio encoder `pdm.c` (header `sdk/pdm.h`, see
[1.8](#18-sound-the-easy-way)). `user.ld` places everything in one load region at `USER_BASE`
(0x008000; it was 0x004000 before spec revision 9, and the loader refuses ELF files linked
there with `E_NOMEM`, so relink them), entry point `_start`.

**Adding a program.** Put `foo.c` in `sdk/examples/`, add `foo` to the `EXAMPLES` line in
`sdk/Makefile`, and run `make -C sdk`. You get `sdk/build/foo.elf` (what the monitor loads)
and `sdk/build/foo.srec` (for upload over the serial port). File names may be long (up to 63
characters with the `.ELF`), but a program named like a monitor command (`ls`, `cat`, `dir`,
`copy`...) has to be typed with its extension, `ls.elf`, because the command wins.

**Building outside the tree.** The same thing by hand, with `R` the repository and the SDK
already built:

```
X=~/x-tools/m68k-unknown-elf/bin/m68k-unknown-elf-
${X}gcc -m68000 -Os -Wall -I$R/generated -I$R/sdk -nostartfiles -T $R/sdk/user.ld \
    -L$R/sdk -L$R/sdk/build -Wl,--gc-sections -Wl,--no-warn-rwx-segments -o foo.elf \
    $R/sdk/build/crt0.o foo.c -Wl,--start-group -lc -lsys -Wl,--end-group -lgcc
```

(`-L$R/sdk` lets the linker find `../generated/user.ld`, which `user.ld` includes; `-I$R/sdk`
finds `pdm.h`.)

The SDK Makefile has no rule for assembler sources in `examples/`; put small assembler
routines in a C file with a top-level `__asm__("...")` block (Part 2 has an example) or add a
rule.

## 1.2 Running a program

### In the emulator

The emulator is the quickest way to test. Make a FAT32 image holding the program and start
the emulator with it:

```
make                                                      # also fw/build/flash.img and emu/build/m68k-emu
mkdir -p /tmp/card
cp sdk/build/foo.elf /tmp/card/FOO.ELF
sh fw/test/mkimage.sh /tmp/card/sd.img 64 /tmp/card/FOO.ELF   # 64 MB FAT32 (needs dosfstools, mtools)
make run EMUFLAGS="--sd /tmp/card/sd.img"
```

The ROM the emulator boots is `fw/build/flash.img`, the whole 1 MB flash window: the BIOS
image followed by the C: volume, which `make flash` (part of `make`; it needs dosfstools and
mtools too) builds from `flashfs/` and the SDK's example programs. `make run` is
`emu/build/m68k-emu --rom fw/build/flash.img --rom-save fw/build/flash.img $(EMUFLAGS)`:
`--rom-save` writes the window back at exit if the machine changed it, so what a session
writes to C: is still there next time. Rebuilding the BIOS or one of the volume's programs
makes `make` build a fresh `flash.img`, and C: starts over. (`fw/build/rom.bin`, the 256 KB
BIOS image alone, still boots with `--rom`, with an unformatted C:.)

The window shows the video output and takes the PS/2 keyboard, where monitor commands are
typed. Serial port A is the emulator's stdin/stdout: it shows the POST report and the banner,
and after that only what a program sends to `/dev/ttya`. The POST takes about 4 s (the RAM
test); then `C:/AUTOEXEC.CMD` prints a line and the `C:/> ` prompt appears. Type `foo`: the
monitor finds `FOO.ELF` on D:, through the search path. Keys typed during the POST are not
lost, they wait as type-ahead for the first prompt (an ESC among them skips `AUTOEXEC.CMD`
and throws the rest away). F12 (or Pause) is the NMI button, F11 the reset button.

Headless, for scripted tests and CI (without `--rom-save`, so a test leaves `flash.img` as it
was):

```
emu/build/m68k-emu --rom fw/build/flash.img --sd /tmp/card/sd.img --headless --frames 1500 \
    --type "foo arg1 arg2\n" --type-delay 350 --no-audio --no-stdin --text-dump /tmp/run.txt
```

`--type` types on the PS/2 keyboard after `--type-delay` frames: monitor commands and a
program's keyboard input both go this way. Since keys typed during the POST are kept, the
delay need not wait for it; the examples here keep 350, which falls after it. `--serial-type`
(after `--serial-type-delay` frames; `\n` is sent as CR) feeds the serial port instead, which
only a program reading `/dev/ttya` or the monitor's `srec` command listens to. The console is
on the screen only, so `--text-dump FILE` (`-` for stdout) is how a headless run reads what
was printed: at exit it writes the 40x30 console text, decoded from framebuffer page 0 by
matching each 8x8 cell against the BIOS font (a cell that matches no character, graphics for
instance, shows as `?`). Only what is still on the screen at exit is there. stdout and
`--serial-log FILE` carry the POST report, the banner and whatever the program sends to
`/dev/ttya`. `--frames N` stops after N frames (60 per second). Useful debugging options:
`--exit-on-berr`, `--break ADDR`, `--trace`, `--io-trace` (every register access with a cycle stamp),
`--dump-mem ADDR LEN`, `--audio-wav FILE` (the audio output rendered to a 48 kHz mono WAV from
cycle 0; works headless and with `--fast`, so you can check sound without a sound card) and
`--audio-edge-log FILE` (every edge of the audio pin with its cycle stamp). `--joy` and
`--paddle` put a joystick or paddles on the Atari port ([1.4](#joystick-and-paddles)). See
`emu/README.md` for the full list.

Make images and cards 64 MB or larger for FAT32 (a FAT32 volume needs at least 65,525
clusters); smaller ones come out as FAT16, which also works.

### On the board

With the board switched off, copy `FOO.ELF` to the card and insert it, then type `foo` at the
prompt (or `run D:/foo.elf`); `dir D:` lists the card. The socket has no card-detect switch, so
the BIOS treats the card as fixed: change it only with the power off (see
[1.7](#17-files-and-directories)). `copy D:FOO.ELF C:` keeps the program in the flash, where
it runs from any drive and without a card; the flash already holds the SDK's example
programs (`hello`, `plasma`, `bmpview`, `tune`, ...) and `README.TXT`. At reset the monitor runs
`C:/AUTOEXEC.CMD`, a script of monitor commands (hold ESC during the POST to skip it): a
`date` line there, for instance, asks for the time at every power-on. Without a card, type `srec` at the prompt, then send `sdk/build/foo.srec` over
the serial port (38400 8N1, `cat foo.srec > /dev/ttyUSB0`); the monitor reads the records
straight from the port, prints a summary, and `go` starts the program (with `argc` = 0). ESC, on
the keyboard or from the port, abandons the upload; other keys are ignored until it ends. In the
emulator, `--type "srec\n"` with `--serial-type "$(cat sdk/build/foo.srec)"` and a
`--serial-type-delay` that falls after the `srec` prompt does the same (at 38400 baud the
97 KB `hello.srec` takes 25 s, 1,500 frames). Piping the file into stdin from the start does
not work: it arrives during the POST, and `srec` throws away whatever was already waiting.

### What the monitor does with the command line

`run foo.elf a b c` and the shorthand `foo a b c` both call `main(argc, argv)` with
`argv[0]` = the path of the file, then the remaining words split on blanks (at most 32
arguments, 511 bytes). Double quotes group a word (`foo "two words"` gives one argument), and
an unquoted argument containing `*` or `?` is expanded by the monitor against the directory it
names before the program starts, one argument per match in directory order, quoted if the name
has a blank (`cat *.TXT` gives `cat` every text file; `foo "*.TXT"` and a pattern that matches
nothing arrive as typed). Too many matches to fit is an error, not a truncation. For `run` the path is as typed (`"foo.elf"`, relative to the current
directory, no search). For the shorthand it is where the search found the file, as an absolute
path built from the word as typed: `"C:/foo.ELF"` if it is in the current directory `C:/`,
`"D:/foo.ELF"` if it was found on the card. The search tries `WORD`, `WORD.ELF` and
`WORD.CMD` in the current directory, then in `C:/`, then in `D:/`; a word that contains a
drive or a directory (`D:foo`, `games/foo`) is tried only there. So after `D:` (switch to the
card), `bmpview foo.bmp` runs `C:/BMPVIEW.ELF` on the card's `FOO.BMP`: the program is found
on the search path, its argument is relative to the current directory. A `.CMD` file found this
way is a script ([1.10](#110-exiting-and-chaining)). Monitor commands win over programs: `ls`
is the monitor's directory listing, so the example program `LS.ELF` has to be typed as `ls.elf`.

The monitor's file commands, for reference: `C:` and `D:` switch the current drive (each
drive keeps its own current directory, and the prompt shows the current one: `C:/> `,
`D:/GAMES> `, or `D:?> ` when the drive is not mounted, no card or no FAT volume); `cd [path]`;
`dir [path or pattern]` (also `ls`; long names, sizes and `yy-mm-dd hh:mm` stamps; ESC stops
it); `cat file` (also `type`); `copy src dst` (also `cp`; `src` may contain `*` and `?`, `dst`
is a file, a directory or a drive, the copy keeps the source's stamp, ESC stops between files,
and a copy that fails, on a full volume say, leaves no partial file); `del file` (also `rm`;
wildcards allowed); `ren old new` (also `mv`; it moves within a volume); `mkdir dir`;
`rmdir dir` (empty directories only); `format C:` (asks y/N); `date [YYYY-MM-DD [HH:MM[:SS]]]`
(without an argument it shows the clock and asks for a new value, Enter keeps it); `echo
text`; `info`, which also shows the current directory, the clock and how many C: sectors have
been written since reset. Names with blanks are typed in double quotes.

The prompt has a line editor: Left/Right, Home/End, Backspace and Delete anywhere, Ctrl-K/U/W
kills, Up/Down through the last 32 lines (`history` lists them), and Tab completion: the first
word completes against the monitor's commands and the programs and scripts on the search path,
later words against file and directory names in the directory typed so far; one Tab inserts the
only match or the longest common prefix, a second Tab lists the choices.

## 1.3 The runtime model

### Memory

```
0x000000  vector table (1 KB)                 \  BIOS: do not touch
0x000400  BIOS data, supervisor stack to 0x8000 /
0x008000  your program: .text .rodata .data .bss  <- ELF loaded here, entry _start
          heap (malloc/sbrk) grows up from the end of .bss
          ...
0x0D6800  top of heap (user_end - 16 KB)
          user stack, 16 KB reserve, grows down; argv strings at the very top
0x0DA800  framebuffer page 1 (75 KB) - free RAM if you do not double buffer
0x0ED400  framebuffer page 0 (75 KB) - the text console, shown after reset
0x100000  end of RAM
```

The program, its `.bss` and its heap share 842 KB (0x008000-0x0DA7FF). `sbrk` stops 16 KB
below the top of that region; the stack is not checked, so deep recursion past 16 KB runs
into the heap silently. Page 1 (0x0DA800-0x0ED3FF) is ordinary RAM when you do not use it for
graphics; the loader will not put ELF segments there, but you can point at it:
`sys_mem_info()` reports `user_end_without_fb1` = 0x0ED400.

### The C library

* `printf`, `puts`, `putchar`, `fprintf(stderr, ...)`: go to the console, the text screen on
  video page 0 (not to the serial port, [1.4](#14-the-console)). `stdout` is line buffered;
  call `fflush(stdout)` before waiting for input after a prompt that has no newline.
* `fgets(buf, n, stdin)`, `scanf`, `getchar()`: read the PS/2 keyboard through a small line
  editor (echo on the screen, Backspace) in the shim; the line ends with Enter.
* `malloc`/`free`/`realloc`/`calloc`: newlib's allocator on top of `sbrk`.
* `fopen`/`fread`/`fwrite`/`fseek`/`fclose`, `open`/`read`/`write`/`lseek`/`close`,
  `stat`/`fstat`, `remove`/`unlink`, `mkdir`, `rmdir`, `chdir`, `getcwd`: files and directories
  on C: and D: (see [Files](#17-files-and-directories)), and the serial port as `/dev/ttya`
  ([1.4](#serial-port-a-is-yours-devttya)), and `rename`.
* `clock()`, `times()`: count from boot, derived from the VBLANK tick counter and the real
  frame length of the current video standard (764 x 262 clocks, 59.95 Hz, on NTSC; 768 x 312,
  50.08 Hz, on PAL). They count milliseconds, matching newlib's `CLOCKS_PER_SEC` of 1000, so
  `clock() / CLOCKS_PER_SEC` is seconds. They advance in whole frames (16.7 ms or 20 ms
  steps), so for game timing `sys_ticks()` is just as good and cheaper.
* `time()`, `gettimeofday()`: the BIOS clock ([1.6](#16-timing)), seconds since 1970, once
  the monitor's `date` command or a program has set it; until then they count from boot as
  `clock()` does. `tv_usec` comes from the frame counter, in whole frames, and is not in step
  with the clock's seconds, so the pair can step back by up to a second: time intervals with
  `clock()` or `sys_ticks()`. `settimeofday()` sets the clock. `localtime()`, `gmtime()`, `mktime()` and
  `strftime()` work on top of them; there is no time zone (newlib's `TZ` is not set), so treat
  the clock as local time and use `gmtime()`, which does no zone arithmetic.
* Floating point works but is **software emulated** (libgcc soft-float): *measured* about
  3,400 clocks (0.28 ms) for a single-precision multiply-add and about 8,000 clocks (0.67 ms)
  for double precision. Use fixed point in anything that has to be fast. `printf("%f")` and
  `libm` work.
* Not available: `fork`/`exec*` (use `sys_exec`), signals, environment variables.
* Static constructors and destructors are not run (`-nostartfiles`, no crti/crtn); the SDK is
  for C.

### User mode

The program runs in the 68000's user mode. It cannot change the interrupt mask or execute
privileged instructions (`move` to SR, `andi/ori/eori` to SR, `rte`, `stop`, `reset`,
`move` to/from USP); trying gives a privilege-violation exception, and the BIOS prints the
registers and returns to the monitor. Reading SR (`move sr,dn`) and writing the condition
codes are allowed. Everything else about the machine is *not* protected: the hardware does not
look at the 68000's function codes, so a user program can read and write all of RAM, the I/O
registers and even the flash. Part 1 sticks to system calls; Part 2 explains how to use the
hardware directly and what to watch out for.

### Crashes

A bus error (any access outside the mapped regions), an address error (a word or long access
at an odd address, for example through a cast `uint16_t *` pointing at an odd byte), an
illegal instruction or a privilege violation stops the program: the BIOS prints the exception
name, PC, SR and all registers, and the monitor prompt comes back. `regs` prints them again.
The NMI button does the same at any time (that is how you break out of a hung program).

## 1.4 The console

The console is a 40x30 character text screen (8x8 font) drawn on framebuffer page 0, white on
dark blue, and the PS/2 keyboard. The serial port is not part of it: since spec revision 8 the
BIOS sends its POST report and banner to serial port A as well as to the screen and then lets
go of the port, so the monitor, crash reports, `[exit n]` and everything a program prints
appear on the screen only, and console input comes from the keyboard only. The port is the
program's ([below](#serial-port-a-is-yours-devttya)). Characters the screen understands:
printable ASCII, `\n` (new line), `\r`, `\b` (cursor left, not destructive), `\t` (to the
next multiple of 8 columns). Other control characters are dropped, so ANSI escape sequences
are *not* interpreted. At the bottom the screen scrolls (a 74 KB move, about 40 ms).

Printing costs about 2,600-2,900 clocks a character in a string (`sys_write`, `sys_puts`,
`printf`) and 3,000 one at a time through `sys_putchar` (*measured*: 3,900 characters, 39 to a
line, in 50-56 and 59 frames), about 4,000-4,600 characters per second, plus 40 ms for each scroll
(*measured* 100 scrolls in 241 frames). A full screen of text takes about 0.3 s. For fast text
in a game, draw your own glyphs into the framebuffer.

| call | what it does |
|---|---|
| `sys_putchar(c)` / `sys_puts(s)` | print a character / NUL-terminated string (no newline added) |
| `sys_con_clear()` | clear page 0, cursor to 0,0 |
| `sys_con_set_cursor(x, y)` | move the cursor; `x` 0..39, `y` 0..29, otherwise `E_INVAL` |
| `sys_con_info(&ci)` | `ci.cols` = 40, `ci.rows` = 30, `ci.cursor_x`, `ci.cursor_y` |

The text colours cannot be changed through the ABI, and the block cursor is hidden while a
program runs.

### Keyboard input as characters

Console input is the PS/2 keyboard only. Bytes that arrive on the serial port wait in a ring
of their own for a program that reads `/dev/ttya` or calls `sys_serial_getc`.

* `sys_getchar()` waits for a character; `sys_pollchar()` returns -1 if there is none.
* The keyboard is decoded with a US layout, Shift, Caps Lock and Ctrl. Enter gives `'\r'`,
  Backspace 0x08, Tab, Esc 0x1B, Ctrl+letter 0x01-0x1A.
* Cursor and function keys arrive as VT100/xterm escape sequences: arrows `ESC [ A/B/C/D`
  (up, down, right, left), Home/End `ESC [ H` / `ESC [ F`, Insert/Delete/PgUp/PgDn
  `ESC [ 2~` `3~` `5~` `6~`, F1-F4 `ESC O P`..`S`, F5-F12 `ESC [ 15~ 17~ 18~ 19~ 20~ 21~ 23~ 24~`.
* Through stdio (`fgets(stdin)`, `getchar()`), input is line edited and echoed on the screen.

### Keyboard input as key events (for games)

`sys_key_event()` returns the next raw keyboard event, or -1. Bits 7..0 are the PS/2 scan
code (set 2), bit 8 (`KEY_RELEASE`, 256) is set for a release, bit 9 (`KEY_EXTENDED`, 512)
for keys that send an `E0` prefix (arrows, right Ctrl/Alt, Insert/Delete/Home/End/PgUp/PgDn,
keypad Enter and /). Holding a key down repeats its press event (typematic) without releases.

| key | event | key | event |
|---|---|---|---|
| Esc | 0x076 | Enter | 0x05A |
| Space | 0x029 | Backspace | 0x066 |
| Left Shift / Right Shift | 0x012 / 0x059 | Left Ctrl / Right Ctrl | 0x014 / 0x214 |
| Up / Down | 0x275 / 0x272 | Left / Right | 0x26B / 0x274 |
| W A S D | 0x01D 0x01C 0x01B 0x023 | Z X | 0x01A 0x022 |
| F1 F2 F3 F4 | 0x005 0x006 0x004 0x00C | 1 2 3 | 0x016 0x01E 0x026 |

(Add 0x100 for the release.) The character queue and the event queue are filled from the same
keystrokes independently: a game that only reads events leaves characters behind, and the
monitor would read them as a command after the program exits. Drain them before exiting with
`while (sys_pollchar() >= 0) ;`. The event queue holds 64 events and drops new ones when full;
the character queue holds 256 bytes.

```c
/* keys.c: show raw key events until Esc is released. */
#include <stdio.h>
#include "syscalls.h"

int main(void)
{
    printf("press keys, Esc quits\n");
    while (sys_key_event() >= 0) ;             /* drop stale events */
    for (;;) {
        int ev = sys_key_event();
        if (ev < 0) continue;                   /* nothing yet: poll again */
        printf("%s%s %02x\n",
               (ev & KEY_RELEASE) ? "up  " : "down",
               (ev & KEY_EXTENDED) ? " E0" : "   ",
               ev & 0xFF);
        if (ev == (KEY_RELEASE | 0x76)) break;  /* Esc released */
    }
    while (sys_pollchar() >= 0) ;               /* keep typed chars out of the monitor */
    return 0;
}
```

For "is this key held" in a game loop, keep a table: `held[ev & 0x2FF] = !(ev & KEY_RELEASE)`
for every event read (masking with 0x2FF drops the release bit and keeps the `E0` bit), with
`held` sized 0x300.

### Joystick and paddles

Board rev C3 has an Atari-style DE-9 port, J12, on the front edge. It takes a joystick (four
direction switches and a fire button) or a pair of paddles (a pot and a button each), and two
system calls read it:

| call | what it does |
|---|---|
| `sys_joystick()` | the switches as a mask, a bit set while its switch is closed: `JOY_UP` 1, `JOY_DOWN` 2, `JOY_LEFT` 4, `JOY_RIGHT` 8, `JOY_FIRE` 16 |
| `sys_paddles(&p)` | the paddle positions measured in the last complete frame, into a `struct paddles`; switches measuring on the first time. 0, or `E_FAULT` |
| `sys_paddles(NULL)` | switch measuring off; 0 |

`sys_joystick` is one read of the DUART's input port, inverted and masked: nothing to switch
on, no queue and no events. Read it once a frame, after `sys_wait_vblank()`, and compare
with the previous frame's mask to find presses (`now & ~before`).

`struct paddles` is four `uint32_t`: `a` and `b`, the positions 0..`PADDLE_FULL` (1023);
`valid`, with `PADDLE_A_VALID` (1) and `PADDLE_B_VALID` (2) set for the paddles whose
measurement ended within the frame; and `frame`, the `sys_ticks()` value when that frame's
measurement was collected. Measuring is off until the first `sys_paddles` call, which switches
it on and returns `valid` 0 (with `a` and `b` 1023); from then on the BIOS measures both
paddles once a frame from the VBLANK interrupt, so real values appear one or two frames later.
Each further call only copies the latest result. A position is the time the paddle's
capacitor takes to charge, with 1023 at the nominal full scale of 12 ms
(`PADDLE_FULL_SCALE_US`); a longer time reads 1023, and a paddle whose capacitor has not
charged by the next VBLANK (one at the end of its travel, or none plugged in) reads 1023 with
its valid bit clear. The range a real paddle covers depends on its pot and on the gate's
threshold, so scale from the smallest and largest values seen rather than trusting 0 and
1023 (in the emulator, `--paddle 0,1023` reads 5 and 1023, *measured*). `sys_paddles(NULL)`
switches measuring off, and so does the end of the program (`sys_exit`, `sys_exec`, or a
crash).

**The lines are shared.** Besides its two pot pins the port has five signal lines, so the
paddles reuse two of them: paddle A's end of charge pulls the up line low, paddle B's the down
line; paddle A's button is the right switch (pin 4) and paddle B's the left (pin 3). A
joystick has no pots and a
paddle has no up or down switch, so each works on its own, but:

* With paddles plugged in, `JOY_UP` and `JOY_DOWN` mean nothing (while measuring they show
  whether each capacitor has charged yet in this frame); use `JOY_LEFT` and `JOY_RIGHT`, the
  buttons.
* With a joystick plugged in and measuring on, pushing it up or down is taken for a paddle: the
  switch closing ends a "charge" at that moment and gives one reading, and a switch held closed
  reads as no paddle (`--joy up` gives `a` 1023 with `PADDLE_A_VALID` clear, *measured*).
  `sys_joystick` still reports the directions correctly.
* While measuring is on, the BIOS owns the DUART's counter/timer and ACR, and needs `IMR_IPC`
  ([2.11](#the-input-and-output-ports)): a program that uses the counter/timer itself must not
  use the paddles.

Measuring costs about 430 clocks a frame with nothing plugged in and 3,500 with two paddles,
0.2 % and 1.8 % of the CPU (*measured* in the emulator): the VBLANK pass, and with paddles
three level-3 interrupts a frame, one for the discharge and one for each paddle's end of
charge. A program that accepts either controller calls `sys_paddles`
for a few frames and counts the frames with a `valid` bit: paddles set one every frame (unless
both are at the end of their travel), a joystick at most once each time it is pushed up or
down. If none turn up, `sys_paddles(NULL)` gives the timer and the time back:

```c
/* stick.c: report what is on the Atari port until a key is pressed. */
#include <stdio.h>
#include "syscalls.h"

int main(void)
{
    struct paddles p;
    int seen = 0;
    sys_paddles(&p);                            /* switch measuring on (this result is valid 0) */
    for (int i = 0; i < 8; i++) {               /* paddles report every frame; a joystick */
        sys_wait_vblank();                      /* pushed up or down, once at most */
        sys_paddles(&p);
        if (p.valid) seen++;
    }
    int paddles = seen >= 4;
    if (!paddles) sys_paddles(NULL);            /* a joystick: give the timer and the CPU back */
    printf("%s (any key quits)\n", paddles ? "paddles" : "joystick");
    int last_j = -1, last_a = -100;
    while (sys_pollchar() < 0) {
        sys_wait_vblank();
        int j = sys_joystick();
        if (!paddles) {
            if (j != last_j)
                printf("%s%s%s%s%s\n", (j & JOY_UP) ? " up" : "", (j & JOY_DOWN) ? " down" : "",
                       (j & JOY_LEFT) ? " left" : "", (j & JOY_RIGHT) ? " right" : "",
                       (j & JOY_FIRE) ? " fire" : "");
        } else {
            sys_paddles(&p);
            j &= JOY_LEFT | JOY_RIGHT;          /* the paddle buttons; up and down carry the pots */
            int a = (int)p.a;
            if (j != last_j || a > last_a + 4 || a < last_a - 4) {
                printf("A %4d B %4lu%s%s\n", a, (unsigned long)p.b,
                       (j & JOY_LEFT) ? " left" : "", (j & JOY_RIGHT) ? " right" : "");
                last_a = a;
            }
        }
        last_j = j;
    }
    while (sys_pollchar() >= 0) ;
    return 0;                                   /* exit switches measuring off */
}
```

In the emulator, `--joy LIST` holds switches (`up`, `down`, `left`, `right`, `fire`, comma
separated), `--paddle A,B` plugs in paddles at those positions (0..1023), `--paddle pad` takes
the positions from a game controller's triggers, and with a window the first game controller
drives the switches (d-pad or left stick, any face button for fire). The emulator looks at the
lines once per video line, so its paddles resolve 64 us, 0.5 % of full scale. `stick` prints
`paddles` and `A  303 B  705 left` with `--joy fire,left --paddle 300,700`, and `joystick` and
` up fire` with `--joy up,fire` (*measured*). `sdk/examples/joytest.c` (`joytest` at the
prompt; `joytest -j` reads the joystick only and leaves measuring off) prints the mask and
both paddles six times a second: `--L-F   A  303  B  705` with the first options.

### Serial port A is yours: `/dev/ttya`

After the banner nothing in the BIOS reads or writes serial port A (the 6-pin FTDI header,
38400 8N1) except the monitor's `srec` command while it runs, and nothing gives the port back
to the console. A program can use it as it likes: to talk to a host, as a second terminal, or
as the console of an operating-system personality (a CP/M-68K-style system, say, whose
programs expect a bigger terminal than 40x30), while the BIOS's own console stays on the
screen. The receive interrupt runs all the time, so input is not lost while the program is
busy: bytes wait in a 512-byte ring, and the newest are dropped once 511 are waiting. The ring
is not emptied between programs; `while (sys_serial_getc() >= 0) ;` discards old input.

The port is a device file: `fopen("/dev/ttya", ...)`, `open()` or `sys_open(O_RDWR,
"/dev/ttya")` opens it, with or without a card.

* `O_RDONLY`, `O_WRONLY` and `O_RDWR` are honoured (`E_BADF` for the wrong direction);
  `O_CREAT`, `O_TRUNC` and `O_APPEND` are ignored, so `"w"` and `"r+"` both work.
* `read` waits for the first byte, then returns whatever else is already in the ring, up to
  `len`: the same shape as fd 0.
* `write` sends the bytes raw, without turning `\n` into CR LF (send `"\r\n"` yourself), and
  waits for the transmitter: 260 us a byte, so a 40-character line takes 10 ms.
* There is no echo, no line editing and no flow control (CTS and RTS are not connected).
  `lseek`, and so `fseek`, is `E_INVAL`.
* The descriptor takes one of the 8 slots shared with files, stays open through the card
  errors that close files, and is closed at exit.

Through stdio, a `/dev/ttya` stream is **line buffered**: the SDK's `fstat` reports a device
descriptor as a character device and `isatty` says yes, so newlib sends each line when the
`\n` is written (*measured*: a `"\r\n"` line reaches the port without a flush, text after it
waits). Call `fflush` after a prompt that has no newline. Use one stream for each direction, as
below: a single `"r+"` stream needs an `fseek` between reading and
writing, which a device cannot do. Without a descriptor, `sys_serial_putc` sends one raw byte
and `sys_serial_getc` takes the next byte from the ring, or -1 at once if there is none, which
is the way to poll the port.

```c
/* ttyecho.c: a line service on serial port A through stdio. */
#include <stdio.h>
#include <string.h>

int main(void)
{
    FILE *in = fopen("/dev/ttya", "r");         /* one stream each way: no fseek needed */
    FILE *out = fopen("/dev/ttya", "w");        /* between reading and writing */
    if (!in || !out) { perror("/dev/ttya"); return 1; }
    printf("serial A: send \"bye\" to stop\n");
    fprintf(out, "\r\nother-m68k: type a line\r\n> ");
    char line[64];
    unsigned n = 0;
    for (;;) {
        fflush(out);                            /* the prompt and the echo have no newline */
        int c = fgetc(in);                      /* waits for the next byte; no echo, no editing */
        if (c == EOF) break;
        if (c == '\n') continue;                /* Enter sends CR, or CR LF */
        if (c == '\r') {
            line[n] = 0;
            n = 0;
            printf("remote: %s\n", line);       /* to the screen */
            if (!strcmp(line, "bye")) break;
            fprintf(out, "\r\n%u characters\r\n> ", (unsigned)strlen(line));
        } else if ((c == 0x08 || c == 0x7F) && n > 0) {
            n--;
            fputs("\b \b", out);                /* rub out on the terminal */
        } else if (c >= ' ' && c < 0x7F && n < sizeof line - 1) {
            line[n++] = (char)c;
            fputc(c, out);                      /* echo: the port does none */
        }
    }
    fprintf(out, "\r\nbye\r\n");
    fclose(out);
    fclose(in);
    return 0;
}
```

The screen shows `remote:` and each line received; the terminal at the other end gets the
prompt, its own typing echoed and the length of each line. In the emulator, the headless
command of [1.2](#in-the-emulator) with `--type "ttyecho\n" --type-delay 350 --serial-type
"hello\nbye\n" --serial-type-delay 600 --frames 900 --text-dump -` prints `remote: hello`
and `remote: bye` from the screen, and stdout before them has the other side of the
conversation (`5 characters`); in a window, the shell that started the emulator is the
terminal. On the board, any terminal program on the FTDI port does
(`picocom -b 38400 /dev/ttyUSB0`).

`sdk/examples/term.c` (`term` at the prompt, `TERM.ELF` on C:) uses the raw calls: it
opens `/dev/ttya` with `sys_open`, sends a greeting, then copies keys (`sys_pollchar`) to the
port and bytes from the port (`sys_serial_getc`) to the screen until ESC is pressed, which makes
the board a small terminal for whatever is at the other end. An operating-system personality
works the other way round: it opens `/dev/ttya` once at start-up and uses it as its console,
reading its commands from the port and writing its output there, and leaves the BIOS console
on the screen.

## 1.5 Graphics the easy way

The screen is 320x240 pixels, **one byte per pixel**, row after row, 320 bytes per row,
no padding. A byte is a colour in RGB332:

```
bit  7 6 5   4 3 2   1 0
     r r r   g g g   b b
```

`RGB332(r, g, b)` in `hw.h` packs 8-bit components (0..255): `RGB332(255, 0, 0)` = 0xE0 red,
0x1C green, 0x03 blue, 0xFF white, 0x00 black. Pixel (x, y) of a page is at
`page + y * 320 + x`.

There are two pages. Page 0 (0x0ED400) is the text console and is shown after reset; page 1
(0x0DA800) is free. `sys_video_info(&vi)` fills in:

| field | value |
|---|---|
| `width`, `height`, `bpp`, `bytes_per_line` | 320, 240, 8, 320 |
| `page_addr[0]`, `page_addr[1]` | 0x0ED400, 0x0DA800 |
| `current_page` | the page last selected with `sys_video_set_page` |
| `standard` | `VIDEO_STD_NTSC` (0) or `VIDEO_STD_PAL` (1) |

`sys_video_set_page(p)` selects the page that is displayed; the switch happens at the start of
the next frame, never in the middle of one. `sys_wait_vblank()` waits for the next vertical
blanking interrupt (once per frame, ~60 Hz; it fires 3 lines after the last visible line, and
the next frame starts 19 lines, about 1.2 ms, later).

**Double buffering** without tearing: draw into the page that is not shown, select it,
wait for VBLANK, then the old page is yours to draw the next frame into. The flip itself always
takes effect at a frame boundary; waiting for VBLANK guarantees the previous page is no longer
being displayed before you overwrite it.

Writing to page 0 draws over the console, and console output (`printf`) keeps drawing on page
0 even while page 1 is displayed. When you exit, the BIOS selects page 0 again; call
`sys_con_clear()` first if you drew on it.

A complete example: a box bouncing over a colour gradient.

```c
/* bounce.c: a box bouncing over a gradient, double buffered. Any key exits. */
#include <stdint.h>
#include <string.h>
#include "syscalls.h"

#define W 320
#define H 240
#define BOX 24

static uint8_t bg[W * H];                       /* the background, drawn once */

static void fill_rect(uint8_t *fb, int x, int y, int w, int h, uint8_t c)
{
    for (int j = 0; j < h; j++)
        memset(fb + (y + j) * W + x, c, w);
}

int main(void)
{
    struct video_info vi;
    sys_video_info(&vi);
    uint8_t *page[2] = { (uint8_t *)vi.page_addr[0], (uint8_t *)vi.page_addr[1] };

    for (int y = 0; y < H; y++)                 /* RGB332: rrrgggbb */
        for (int x = 0; x < W; x++)
            bg[y * W + x] = (uint8_t)(((x * 8 / W) << 5) | ((y * 8 / H) << 2) | 1);
    memcpy(page[0], bg, sizeof bg);
    memcpy(page[1], bg, sizeof bg);

    int x = 10, y = 10, dx = 2, dy = 1;
    int oldx[2] = { x, x }, oldy[2] = { y, y };
    int back = 1;                               /* page 0 is on screen */
    while (sys_pollchar() < 0) {
        uint8_t *fb = page[back];
        /* erase where the box was the last time this page was drawn */
        for (int j = 0; j < BOX; j++)
            memcpy(fb + (oldy[back] + j) * W + oldx[back], bg + (oldy[back] + j) * W + oldx[back], BOX);
        fill_rect(fb, x, y, BOX, BOX, 0xFF);    /* white */
        oldx[back] = x; oldy[back] = y;

        sys_video_set_page(back);               /* shown from the next frame on */
        sys_wait_vblank();                      /* after this the old page is ours */
        back ^= 1;

        x += dx; y += dy;
        if (x < 0 || x > W - BOX) { dx = -dx; x += 2 * dx; }
        if (y < 0 || y > H - BOX) { dy = -dy; y += 2 * dy; }
    }
    sys_video_set_page(0);
    sys_con_clear();                            /* page 0 is the console */
    return 0;
}
```

How much can you draw per frame? A frame is 200,168 CPU clocks (NTSC). `memcpy` of a whole
page takes about 2 frames (*measured* 43 frames for 20 copies), so full-screen redraws run at
15-30 frames per second at best; redraw only what changes, as above. `sdk/examples/plasma.c`
is another complete example (4x4 blocks, written as longs). It also shows where the time goes
in such a loop: the first version computed four sine terms per block and ran at 12 VBLANKs a
frame (5 fps) at `-Os`, 10 at `-O2`. Tabulating the terms per frame (each depends on x, x+y or
x-y alone), keeping the palette as ready-made 4-byte words, scaling the tables by 4 so the
masked sum is the byte offset into that word table, and making the line stride a constant so
the four stores are displacement-addressed brings it to 6 VBLANKs at `-Os` and 5 at `-O2`
(12 fps): the inner loop is then 12 instructions, about 170 clocks a block, and 4800 blocks
fill 4 frames. Optimisation flags were worth 17 %; the data layout was worth 140 %.

Composite colour is soft: fine horizontal colour detail smears, and the colour subcarrier is not
locked to the pixels, so expect some dot crawl. Large areas of colour and high-contrast
luminance edges look best.

## 1.6 Timing

| call | |
|---|---|
| `sys_ticks()` | frames since boot: incremented by the VBLANK interrupt, 59.95 per second on NTSC (50.08 on PAL) |
| `sys_wait_vblank()` | wait until `sys_ticks()` changes |
| `sys_sleep_ms(ms)` | busy-wait about `ms` milliseconds: whole frames by the tick counter, the rest with a calibrated loop |
| `sys_time()` | the BIOS clock: seconds since 1970-01-01 00:00 local time, 0 until it has been set |
| `sys_set_time(secs)` | set the clock |

`sys_sleep_ms(ms)` waits `ms * 60 / 1000` whole frames (`ms * 50 / 1000` on PAL), rounded
down, then the remaining fraction of a frame with the loop: `sys_sleep_ms(1000)` waits 60
frames on NTSC, 1.0008 s (the nominal 60 Hz against the real 59.95 Hz is the only error).
For game timing, count frames with `sys_ticks()` or `sys_wait_vblank()`; for elapsed seconds
divide ticks by 60 (50 on PAL, `vi.standard` tells you which). Everything keeps running during these calls: interrupts are
enabled, and your interrupt hooks are called.

**The clock.** The BIOS keeps a wall clock as seconds since 1970-01-01 00:00, advanced by the
VBLANK interrupt with the exact frame length (200,168 CPU clocks on NTSC, 239,616 on PAL, so
it keeps the crystal's accuracy, where dividing ticks by 60 would lose 0.08 %). There is
no battery: after power-on or reset it reads 0 until the monitor's `date` command (in
`C:/AUTOEXEC.CMD`, say) or `sys_set_time` sets it, and while it is 0 it does not run. It has
no time zone; treat it as local time. It stamps the files the BIOS writes; files written
while it is not set get 2026-01-01 00:00. Through the C library it is `time()` and
`gettimeofday()` ([1.3](#the-c-library)). While the BIOS writes a sector of the flash
volume, interrupts are masked for about 0.15 s; the routine counts the frames that pass
meanwhile, so `sys_ticks()` and the clock do not lose them ([2.12](#writing-the-c-volume)).

## 1.7 Files and directories

There are two drives, named as in DOS:

* **C:** is a 768 KB FAT12 volume in the flash ROM, after the BIOS image. It is always
  present, keeps its contents without power, and survives a BIOS update with the monitor's
  `flash` command. As `make flash` builds it, it holds `AUTOEXEC.CMD`, `README.TXT` and the
  SDK's example programs (44-63 KB each, about 475 KB in all, 248 KB free). Reading it is
  fast; writing it is slow and stops interrupts for a moment ([below](#writing-to-c)).
* **D:** is the SD card's FAT32 (or FAT16/FAT12) volume.

Each drive has its own current directory, and one of them is the current drive: C: after
reset. The BIOS mounts a volume the first time a call needs it and keeps it mounted; after a
card error it re-initialises the card on the next call that uses D:. The socket has no
card-detect switch, so the card is treated as fixed media: change it with the power off.
Removing it while the machine runs is not supported: a file open for writing loses its data and
the card's FAT can be left inconsistent. (The monitor's `sd` command re-initialises the card
and drops the old mount: a way back after a card error, not a way to change cards.)

Rules:

* **Paths** are `[X:][/]dir/.../name`. Without a drive letter a path is on the current drive;
  without a leading `/` it is relative to that drive's current directory (`..` works). `X:`
  alone is drive X's current directory, so `D:SCORES.DAT` is a file in it. `\` works as a
  separator too. A path must be shorter than 128 bytes, otherwise the call fails with
  `E_FAULT`.
* **Names** are long (VFAT) names of up to 63 characters, ASCII, stored with their case and
  matched without regard to it: `High scores.dat` is also `HIGH SCORES.DAT`, and its 8.3 alias
  (`HIGHSC~1.DAT`) works as well. A longer or malformed name (one with `"*:<>?|` or a control
  character in it) gives `E_NAMETOOLONG`.
* **The current directory** belongs to the machine, not to the program. `chdir("D:/GAMES")`
  sets D:'s directory without making D: current, as in DOS; `chdir("D:")` makes D: current,
  and `getcwd` then gives `"D:/GAMES"`. The setting is still in force at the monitor prompt
  after the program exits, so a program that moves about should save `getcwd` at the start
  and put both the directory and the drive back at the end, or use absolute paths.
* **Create, delete, rename and mkdir** work on both drives, in subdirectories too.
  `remove`/`unlink` deletes a file or an empty directory (`E_EXIST` if it is not empty,
  `E_ACCES` if it is read-only); `rmdir` is the same call. `sys_rename(old, new)` renames or
  moves within one volume and fails with `E_EXIST` if `new` exists. `new` is an ordinary path,
  resolved like any other: a bare name lands in the current directory of the volume, not in
  `old`'s directory, and a drive letter in `new` is ignored, so the file never changes
  volume. To move a file to the other drive, copy it and delete the original. (newlib's
  `rename()` calls this.)
* At most 8 files and 4 directories open at once (an open `/dev/ttya` counts as a file).
  Files are closed for you when the program exits. Nothing stops a file from being opened
  twice; do not write a file that is open on another descriptor.
* Paths that start with `/dev/` name devices, not files. The only one is `/dev/ttya`, serial
  port A ([1.4](#serial-port-a-is-yours-devttya)); any other `/dev/` name gives `E_NOENT`.
* There is no `stat` system call. The SDK's `stat()` opens the file (or the directory) to
  find its size and type; the modification time is only reported by `sys_readdir`.

With stdio, everything works as usual:

```c
FILE *f = fopen("C:/Scores/High scores.dat", "wb");    /* "w" creates or truncates, "a" appends */
fwrite(table, sizeof table, 1, f);
fclose(f);
```

The POSIX layer (`open`, `read`, `write`, `lseek`, `close`, `fstat`, `stat`, `unlink`,
`mkdir`, `rmdir`, `chdir`, `getcwd`) also works and translates newlib's
`O_CREAT`/`O_TRUNC`/`O_APPEND` into the ABI's values. The raw calls
`sys_open(flags, path)` (note the argument order), `sys_read(fd, len, buf)`,
`sys_write(fd, len, buf)`, `sys_lseek(fd, offset, whence)`, `sys_close(fd)`, `sys_unlink`,
`sys_rename`, `sys_mkdir`, `sys_chdir` and `sys_getcwd(len, buf)` return the ABI's negative
`E_*` codes instead of setting `errno`. Use the `O_*` constants from `syscalls.h` with the raw
calls; they differ from newlib's `<fcntl.h>`, so do not include both headers in one source
file. Descriptors 0, 1, 2 are the console; files and devices get 3..10.

Directories are read with the raw calls; `struct dirent` has `name[64]` (the long name, or the
8.3 name if the entry has no long one), `size`, `attr` (`ATTR_DIR`, `ATTR_READONLY`,
`ATTR_HIDDEN`) and `mtime`, the modification time in seconds since 1970-01-01 00:00 local
time (0 if the entry has none). The BIOS stamps what it writes from its clock
([1.6](#16-timing)), or with 2026-01-01 00:00 while the clock is not set.

```c
/* files.c: a directory and a long file name on C:, saved, renamed, read back and listed. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include "syscalls.h"

struct score { char name[4]; unsigned long points; };

int main(void)
{
    struct score table[3] = { { "BRD", 9000 }, { "ABC", 4200 }, { "XYZ", 100 } };

    if (mkdir("C:/Scores", 0) < 0 && errno != EEXIST) { printf("mkdir: %s\n", strerror(errno)); return 1; }
    FILE *f = fopen("C:/Scores/High scores.new", "wb");    /* long name, spaces allowed */
    if (!f) { printf("create: %s\n", strerror(errno)); return 1; }
    fwrite(table, sizeof table, 1, f);
    fclose(f);                                  /* C: is written now */
    remove("C:/Scores/High scores.dat");        /* the previous table, if there is one */
    int r = sys_rename("C:/Scores/High scores.new", "C:/Scores/High scores.dat");
    if (r < 0) { printf("rename: error %d\n", r); return 1; }

    memset(table, 0, sizeof table);
    f = fopen("c:/scores/HIGH SCORES.DAT", "rb");          /* names match in any case */
    if (!f || fread(table, sizeof table, 1, f) != 1) { printf("read back failed\n"); return 1; }
    fclose(f);
    for (int i = 0; i < 3; i++) printf("%d. %s %lu\n", i + 1, table[i].name, table[i].points);

    struct dirent e;
    char stamp[20];
    int h = sys_opendir("C:/Scores");
    if (h < 0) { printf("opendir: %d\n", h); return 1; }
    while (sys_readdir(h, &e) > 0) {
        time_t t = (time_t)e.mtime;             /* local time: gmtime, not localtime */
        strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M", gmtime(&t));
        printf("%-16s %5lu %s\n", e.name, (unsigned long)e.size, e.mtime ? stamp : "");
    }
    sys_closedir(h);
    return 0;
}
```

It prints the table and `High scores.dat     24 2026-01-01 00:00` while the clock is not set;
after `date 2026-10-21 14:30` at the prompt the stamp is the time of the run. Afterwards
`dir C:/Scores` at the prompt shows the file too. `sdk/examples/ls.c` (`ls.elf` at the
prompt, since `ls` is the monitor's own command) lists any directory the same way.

**Speed.** C: is read straight from the ROM window, which needs no card protocol at all:
*measured*, a 200 KB file read in 8 KB pieces takes 8 frames (0.13 s, about 1.5 MB/s) from C:
and 127 frames (2.1 s) from D:. The card is bit-banged, so expect about 100 KB/s for raw block
reads and 80-100 KB/s through the filesystem (*measured*: 5.05 ms per block with
`sys_sd_read_block`; a 76,800-byte screen read with `fread` one 320-byte line at a time loads
in 0.9 s). Card writes are slower, about 13 ms per block plus the card's own programming time.
Read into a buffer in big chunks rather than byte by byte through `fgetc`. Buffers passed to
system calls must lie in user RAM (0x008000-0x0FFFFF, both framebuffer pages included, so you
can read a picture straight into a page); otherwise the call fails with `E_FAULT`.

### Writing to C:

Flash is erased in 8 KB sectors, and a sector must be erased before it is programmed, so the
BIOS writes C: through a cache of one erase sector in its RAM. A 512-byte write loads the
enclosing 8 KB into the cache and patches it there (if the data differ: a sector written with
what it already holds changes nothing). The cache, if it holds changes, goes back to the flash, erased and
reprogrammed as a whole, when a write lands in another erase sector, when a file on C: is
closed, after every delete, rename and mkdir, when a program exits, and before every monitor
prompt. Each such cycle runs from RAM with **interrupts masked**, about 0.15 s for a full
sector (*measured* 155 ms in the emulator; erased words are skipped, so a sector that is
mostly empty is quicker). Meanwhile:

* serial bytes beyond the 3 the DUART's receive FIFO holds are lost (at 38400 baud a cycle
  is about 600 byte times);
* the audio stream repeats its last pattern: an audible gap;
* the PS/2 receiver holds one byte, so a keystroke can be lost;
* interrupt hooks and the audio callback are not called. The tick counter and the clock
  are corrected afterwards (the routine counts the frames that passed).

What it costs, *measured* in the emulator: copying a 200 KB file from D: to a freshly
formatted C: takes 29 cycles (31 with the format) and 7.5 s, C: to C: 5.5 s; a 303-byte copy
into the root takes 4 cycles, a `mkdir` 3, the `files.c` example above 7. So write C: in big pieces, keep a file open rather
than closing and reopening it, and do not write it while sound plays or serial data arrives.
The chips are rated for 100,000 erase cycles per sector: plenty for saving a game's state or
installing programs, but a program that rewrote a small log file every second would wear out
the sectors holding the FAT and the root directory in about a day. Data that changes all the time
belongs on D:. `format C:` at the monitor prompt recreates an empty C:.

`errno`: the SDK shim maps every ABI error to the newlib `errno` of the same name, so a bad
name gives `ENAMETOOLONG`, a buffer outside user RAM `EFAULT`, an existing name `EEXIST`, a
read-only entry `EACCES`, no card or no FAT volume `ENODEV`, a card or flash error `EIO`, and
`strerror(errno)` says which.

Raw blocks: `sys_sd_read_block(lba, buf)` and `sys_sd_write_block(lba, buf)` transfer one
512-byte block at an absolute card address. They bypass the filesystem; do not write blocks
that belong to the FAT volume while files are in use. C: has no raw calls: read it in the ROM
window, and leave writing it to the BIOS ([2.12](#212-the-flash-rom)).

## 1.8 Sound the easy way

The board plays **one 8-bit pattern per video line**: 15,707 a second on NTSC (12 MHz / 764
clocks per line), 15,625 on PAL. A pattern is not a sample but the eight on/off bits the
audio pin shows during that line; the share of ones sets the level ([2.8](#28-audio)).
Programs think in **unsigned 8-bit samples** at the line rate (128 is silence, the resting
level, 0 and 255 the extremes; only the top 7 bits count), turn them into patterns with the
SDK's encoder, and hand the patterns to the BIOS in buffers, which its line (HSYNC)
interrupt plays one byte per line. Nothing above about 7.8 kHz (half the sample rate) can be
reproduced.

The encoder (`#include "pdm.h"`; the code is in `libsys.a`):

* `pdm_init()` builds the encoder's 64 KB table in your program's `.bss`. Call it once at the
  start; it takes about 0.4 s.
* `pdm_encode(dst, samples, n)` turns `n` samples into `n` pattern bytes at about 83 clocks
  each. It carries its state from call to call, so consecutive buffers join without a click;
  `pdm_reset()` clears that state (there is no need to between sounds).
* `pdm_silence()` is the idle pattern 0xAA, the silence level.

The stream (system calls 30-32):

* `uint8_t *b = sys_audio_start(n)` opens it and returns the first buffer to fill. The BIOS
  owns `AUDIO_NBUF` = 3 buffers of `AUDIO_BUF_MAX` = 512 bytes; `n` (1..512) is how many bytes
  of each you fill and the hardware plays: 256 bytes are 16 ms, 512 are 33 ms. It returns
  NULL if `n` is out of range or an audio callback (below) is installed. Nothing plays yet.
* `b = sys_audio_next(b, 1)` hands the filled buffer in (the sound starts with the first) and
  returns the next buffer to fill. When none is free it **waits** until the one being played
  has finished, so a loop of synthesise, `pdm_encode`, `sys_audio_next` runs at exactly the
  audio rate without any timing code of its own. With `block` = 0 it returns NULL instead of
  waiting; the buffer you handed in still plays, and you ask for the next one later with
  `sys_audio_next(NULL, 0)`, which hands nothing in.
* `sys_audio_last(b, 1)` hands in the final buffer (or `NULL` if you have nothing more) and
  waits until everything has played; with `block` = 0 it returns at once and the sound
  finishes on its own. The output then goes back to the silence level and the BIOS switches
  its line interrupt off, so **silence costs no CPU**.
* Buffers play in the order handed in. Fill a buffer completely before handing it in and do
  not touch it afterwards: only the pointer you got back last is yours. The buffers are in
  the BIOS's data area below 0x008000, not in your program.
* While you fill one buffer the other two are playing or queued, so you have about two
  buffers' time for each one. If the program is later than that (an underrun), the last
  pattern repeats until the next buffer arrives: a gap, heard as a click, but nothing
  breaks. A `printf` is the usual cause: a 40-character line takes about 9 ms to draw and a
  scroll at the bottom of the screen about 40 ms, half as long again while the stream's
  interrupt takes its third of the CPU. That is why `tune.c` below uses 512-byte buffers: two
  of them, 66 ms, cover a scroll, two 256-byte ones do not.
* While sound plays the line interrupt takes **about a third of the CPU** (242 of the 764
  clocks in every line) and `pdm_encode` 83 clocks per sample, so about 440 clocks per sample
  are left for making the samples. Synthesise with integer arithmetic (soft float costs
  thousands of clocks per operation), or encode sounds once into arrays of patterns and copy
  those into the buffers when they are needed.
* Exiting (or `sys_exec`) stops the stream at once and drops what is still queued. Call
  `sys_audio_last(NULL, 1)` first, as below, if the end of the sound matters.

```c
/* arp.c: a C major arpeggio, up and down twice, streamed through the BIOS audio buffers. */
#include <stdint.h>
#include <stdio.h>
#include "hw.h"
#include "syscalls.h"
#include "pdm.h"

#define BUFSIZE 256                             /* samples per buffer: 16 ms */

static uint8_t samples[BUFSIZE];

int main(void)
{
    struct video_info vi;
    sys_video_info(&vi);
    uint32_t rate = vi.standard == VIDEO_STD_PAL ? CPU_CLOCK_HZ / VIDEO_PAL_LINE_CLOCKS
                                                 : CPU_CLOCK_HZ / VIDEO_NTSC_LINE_CLOCKS;
    static const uint16_t hz[] = { 262, 330, 392, 523, 659, 523, 392, 330 };

    printf("arpeggio at %lu samples/s\n", (unsigned long)rate);
    pdm_init();                                 /* the encoder's 64 KB table: about 0.4 s */
    uint8_t *buf = sys_audio_start(BUFSIZE);    /* the first of the BIOS's three buffers */
    if (!buf) { printf("sys_audio_start failed\n"); return 1; }
    for (unsigned rep = 0; rep < 2; rep++) {
        for (unsigned i = 0; i < 8; i++) {
            uint16_t phase = 0, step = (uint16_t)(hz[i] * 65536UL / rate);
            for (int c = 0; c < 10; c++) {      /* 10 buffers per note: 0.16 s */
                int amp = 100 - 9 * c;          /* decay, a step per buffer */
                for (unsigned k = 0; k < BUFSIZE; k++) {
                    phase += step;              /* square wave */
                    samples[k] = (uint8_t)(128 + ((phase & 0x8000) ? amp : -amp));
                }
                pdm_encode(buf, samples, BUFSIZE);  /* samples -> patterns, into the buffer */
                buf = sys_audio_next(buf, 1);   /* hand it in, wait for a free one */
            }
        }
    }
    sys_audio_last(NULL, 1);                    /* no more; wait until everything has played */
    printf("done\n");
    return 0;
}
```

Build it like any other program (1.1; the SDK's `-I.` finds `pdm.h` and `-lsys` links the
encoder) and try it in the emulator without a sound card by writing the output to a WAV file:

```
cp sdk/build/arp.elf /tmp/card/ARP.ELF                  # after adding arp to EXAMPLES
sh fw/test/mkimage.sh /tmp/card/sd.img 64 /tmp/card/ARP.ELF
emu/build/m68k-emu --rom fw/build/flash.img --sd /tmp/card/sd.img --headless --frames 700 \
    --type "arp\n" --type-delay 350 --no-stdin --audio-wav /tmp/card/arp.wav --text-dump -
```

It prints `arpeggio at 15706 samples/s`, then, about 3 s later (0.4 s for `pdm_init` and
2.6 s of sound, 16 notes of 2,560 samples), `done` and `[exit 0]`, all on the screen, which
`--text-dump -` copies to stdout at the end; `arp.wav` holds the notes at their pitches (262
to 659 Hz). With `--io-trace` added you can see that the line interrupt
was switched on once and off once, with exactly 40,960 stream writes to AUDIO in between and
never more than 904 clocks between two of them (a line is 764; the rest is interrupt
latency), so no buffer arrived late. In a window (without `--headless`) you hear it.
`sdk/examples/tune.c` (`tune` at the prompt) is a longer example: "Ode to Joy" in two voices,
a square-wave melody with a decaying envelope over a triangle bass, synthesised at the line
rate and streamed in 512-byte buffers while it prints the notes.

What to expect: square and triangle tones and chip music are clean; sampled sound is
band-limited to about 7.8 kHz, has 7-bit resolution and some hiss, good enough for effects,
speech and music. Part 2 ([2.8](#28-audio)) explains why.

For per-line code of your own there is still `sys_set_audio_cb(fn)`: the BIOS calls
`void fn(void)` from the line interrupt (supervisor mode, level 4, so keyboard, serial and
VBLANK interrupts wait while it runs) and the callback writes the audio register itself; the
stream is not played while a callback is installed (and `sys_audio_start` fails), and
`sys_set_audio_cb(NULL)` (or exiting) removes it. It is dearer than the stream (an empty
callback already costs 272 clocks per line, 36 % of the CPU); see [2.8](#28-audio) before
using it.

## 1.9 Interrupt hooks

`sys_set_irq_handler(level, fn)` installs `void fn(void)` to be called from the BIOS's
interrupt routine for that level, after the BIOS has done its own work; it returns the previous
hook, and `NULL` removes it. Hooks are removed when the program exits.

| level | interrupt | rate | BIOS work before the hook |
|---|---|---|---|
| 1 | VBLANK | once per frame | acknowledge, `ticks++`, advance the clock; while paddles are measured, collect the frame's result and short the paddle capacitors, or release them if nothing charged in the frame (about 430 clocks, *measured*) |
| 2 | PS/2 byte received | per keyboard byte | read the byte, decode it into the queues |
| 3 | DUART | per serial event; three times a frame while two paddles are measured | drain the serial receive FIFO; while paddles are measured, read IPCR, release the capacitors once the discharge is seen, and time the paddle lines |
| 4 | HSYNC | 15.7 kHz | acknowledge, play one byte of the audio stream (or call the audio callback instead); installing a level-4 hook also turns the interrupt on and keeps it on while no stream plays, and switches the BIOS to its slower level-4 handler ([2.7](#what-the-bios-installs)) |

Any other level gives `E_INVAL` (returned as the pointer value). The hook runs in
supervisor mode on the BIOS's stack, with the processor's interrupt mask at the hook's level:
higher levels can still interrupt it (a VBLANK hook can be interrupted by the keyboard, the
serial port and HSYNC), lower levels wait. Rules for hooks: keep them short, use `volatile`
variables to share data with the program, do not call `sys_*` functions (the BIOS is not
re-entrant), and do not use much stack (the supervisor stack is about 8 KB, shared with the
BIOS).

```c
/* vbl.c: a VBLANK hook that counts frames and moves a raster bar. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "syscalls.h"

static volatile uint32_t frames;
static volatile int bar_y;

static void on_vblank(void)                     /* supervisor mode, level 1 */
{
    frames++;
    bar_y = (bar_y + 1) % 240;
}

int main(void)
{
    struct video_info vi;
    sys_video_info(&vi);
    uint8_t *fb = (uint8_t *)vi.page_addr[1];
    memset(fb, 0, 320 * 240);

    sys_set_irq_handler(1, on_vblank);
    sys_video_set_page(1);
    int drawn = -1;
    while (sys_pollchar() < 0) {
        int y = bar_y;                          /* read once: the hook may change it */
        if (y == drawn) continue;
        if (drawn >= 0)
            memset(fb + drawn * 320, 0x00, 320);
        memset(fb + y * 320, 0xE0, 320);        /* red */
        drawn = y;
    }
    sys_set_irq_handler(1, NULL);
    sys_video_set_page(0);
    printf("%lu frames\n", (unsigned long)frames);
    return 0;
}
```

A program cannot mask interrupts, so data shared with a hook must be safe to read at any
moment: a 16- or 32-bit variable read or written by one instruction is atomic with respect to
interrupts (the 68000 takes interrupts between instructions); a read-modify-write on both sides,
or a structure of several fields, is not. Single-writer indices, as in the ring buffer above,
avoid the problem.

## 1.10 Exiting and chaining

* Returning from `main(argc, argv)` or calling `exit(n)` flushes stdio and calls
  `sys_exit(n)`. The BIOS closes the program's files (and `/dev/ttya`), stops the audio
  stream at once (buffers still queued are dropped; call `sys_audio_last(NULL, 1)` before
  exiting if the end of the sound matters) and puts the audio output back to the silence level, removes its hooks and
  audio callback, restores its
  interrupt enables, turns off the LEDs, selects page 0, prints
  `[exit n]` and returns to the monitor prompt.
* `sys_exec(path, args)` loads another ELF in place of the running program and starts it with
  `argv[0]` = `path` and `args` split on blanks (at most 16 words, 255 bytes; `NULL` for none).
  `path` is an ordinary path, with the full name (`"C:/HELLO.ELF"`): there is no search path
  and no `.ELF` is added.
  It returns only on failure, and only if the failure is found before anything is overwritten:
  `E_NOENT` (no such file), `E_NOEXEC` (not a loadable 68000 ELF), `E_NOMEM` (does not fit the
  user region), `E_NAMETOOLONG`, `E_FAULT`, `E_NODEV`/`E_IO` (card problems). The caller's open
  files and hooks are released first. When the new program exits, control goes to the monitor,
  not back to the caller; a menu program that wants to come back must be `sys_exec`'d again.
  Flush stdio before calling it (`fflush(stdout)`), since the caller's buffers vanish.
* A program loaded with `run` and restarted with `go` finds its `.data` as the first run left
  it (newlib's stdio state included), so use `run` again.
* The current drive and directories stay as the program left them ([1.7](#17-files-and-directories)).

**Scripts.** A `.CMD` file is a monitor script: its lines are executed as if typed (they are not
echoed), up to 127 characters each. A line whose first word starts with `#` or `;`, or is
`rem`, is a comment; `echo text` prints; `exit` ends the script early. A line that runs a
program does not end the script: the BIOS keeps the script's path and the offset of the next
line, and when the program ends, by exiting or by a crash, the monitor carries on with that
line. So a script can chain programs, which a program cannot do with `sys_exec` (there is no
way back to the caller). A script runs another by naming it, up to 4 deep. ESC aborts all
scripts between lines (an ESC typed while a program runs, and not read by it, counts) and
empties the keyboard queue; without an ESC, keys typed while a script runs stay in the queue
for the next program or the prompt. `name` at the prompt
runs `NAME.CMD` through the search path ([1.2](#what-the-monitor-does-with-the-command-line)),
and `C:/AUTOEXEC.CMD` runs at reset unless ESC is pressed during the POST. The one in
`flashfs/AUTOEXEC.CMD`, which `make flash` puts on C:, only prints a line; `date` (ask for the
time), `D:` (start on the card) and program names are typical additions.

## 1.11 System call reference

All calls are `TRAP #15` with the number in D0.W, integer arguments in D1, D2, D3, pointer
arguments in A0, A1 (in the order listed), the result in D0.L; all other registers are
preserved. The `sys_*` C functions in `libsys.a` do the register shuffling. A trivial call such
as `sys_ticks()` costs about 450 clocks (*measured*), so do not make system calls in inner
loops. Errors are negative `E_*` values from `syscalls.h`. Every pointer argument (buffers,
structures, strings) must point into user RAM, 0x008000-0x0FFFFF, and a buffer must fit there
entirely, otherwise `E_FAULT`; a path must also end within 128 bytes. Paths follow
[1.7](#17-files-and-directories): an optional drive `C:` or `D:`, otherwise the current drive,
and relative to the drive's current directory without a leading `/`. The exception is the audio stream (30-32): its buffers belong
to the BIOS, `sys_audio_next` and `sys_audio_last` accept only the buffer the BIOS last
handed out, and `sys_read` accepts that buffer as its destination while the stream is open,
so a file can stream straight into it (`sdk/examples/sdplay.c` does this for raw PDM files).

| # | prototype | description, results and errors |
|---|---|---|
| 0 | `void sys_exit(int code)` | End the program: close its files, switch paddle measuring off, stop the audio stream (queued buffers are dropped), drop hooks and audio callback, restore interrupt enables, audio to the silence level, LEDs off, select page 0, print `[exit code]`, return to the monitor. Does not return. |
| 1 | `int sys_putchar(int c)` | Write a character to the video console (page 0). Returns `c & 0xFF`. |
| 2 | `int sys_getchar(void)` | Wait for a character from the PS/2 keyboard; returns it (0..255). |
| 3 | `int sys_pollchar(void)` | Next keyboard character, or -1 if none. |
| 4 | `int sys_puts(const char *s)` | Write a NUL-terminated string to the video console, no newline added. Returns the number of characters, or `E_FAULT` if `s` does not start in user RAM (an unterminated string stops at the end of RAM). |
| 5 | `int sys_serial_putc(int c)` | Write a raw byte to serial port A (no `\n` translation), waiting for the transmitter (260 us a byte). The port is free for programs after the POST; see also `/dev/ttya`. Returns `c & 0xFF`. |
| 6 | `int sys_serial_getc(void)` | Next byte from serial port A's receive ring (512 bytes, filled by interrupt; reads of `/dev/ttya` drain the same ring), or -1 if none. Does not wait. |
| 7 | `uint32_t sys_ticks(void)` | Frames since boot (VBLANK count; 59.95 Hz NTSC, 50.08 Hz PAL). |
| 8 | `void sys_wait_vblank(void)` | Wait until the tick count changes (the next VBLANK interrupt, at the start of vertical sync). |
| 9 | `int sys_video_info(struct video_info *info)` | Fill in width, height, bpp, bytes per line, both page addresses, the current page and the video standard. 0, or `E_FAULT` if the structure is not in user RAM (`NULL` included). |
| 10 | `int sys_video_set_page(int page)` | Display page 0 or 1 from the next frame on. 0, or `E_INVAL`. |
| 11 | `int sys_open(int flags, const char *path)` | Open a file on C: or D:. `flags`: `O_RDONLY`, `O_WRONLY` or `O_RDWR`, plus `O_CREAT` (create if missing), `O_TRUNC`, `O_APPEND` (every write goes to the end); the last three need write access, else `E_INVAL`. Returns a descriptor 3..10, or `E_NOENT` (no such file or directory on the way, or the path names a directory), `E_NAMETOOLONG` (a name longer than 63 characters or malformed), `E_NFILE` (8 open), `E_NOSPC` (directory full, or the file is read-only), `E_NODEV` (no card / no FAT volume), `E_INVAL` (bad flags, or a drive other than C: and D:), `E_IO`, `E_FAULT`. A path starting with `/dev/` opens a device instead: `/dev/ttya` (serial port A) needs no card, honours the access mode and ignores `O_CREAT`/`O_TRUNC`/`O_APPEND`; any other `/dev/` name is `E_NOENT`. |
| 12 | `int sys_close(int fd)` | Close a file or device; 0 (also for 0..2), or `E_BADF`. Closing a file written on C: writes the flash (about 0.15 s with interrupts masked, [1.7](#writing-to-c)). |
| 13 | `int sys_read(int fd, unsigned len, void *buf)` | Read up to `len` bytes; returns the count (0 at end of file) or `E_BADF`, `E_IO`, `E_FAULT`. On fd 0 (the keyboard), waits for one character and returns whatever else is already queued (no echo, no line editing); a `/dev/ttya` descriptor does the same on the serial receive ring. `E_BADF` on fd 1, 2. `buf` must be in user RAM, or inside the BIOS audio buffers while an audio stream is open (spec rev 8). |
| 14 | `int sys_write(int fd, unsigned len, const void *buf)` | Write `len` bytes; returns `len`, or `E_NOSPC` (volume full), `E_BADF`, `E_IO`, `E_FAULT`. On fd 1, 2: to the video console. On a `/dev/ttya` descriptor: raw bytes to serial port A, waiting for the transmitter. `E_BADF` on fd 0. On C:, a write that crosses into another 8 KB erase sector writes the previous one to the flash first. |
| 15 | `int sys_lseek(int fd, int offset, int whence)` | `SEEK_SET`/`SEEK_CUR`/`SEEK_END`; returns the new offset, or `E_INVAL` (negative result, bad `whence`, console fds, devices), `E_BADF`. Seeking past the end of a writable file extends it. |
| 16 | `int sys_opendir(const char *path)` | Open a directory (`""` or `"."` for the current one, `"/"` for the root of the current drive, `"D:"` for D:'s current directory, `"D:/"` for its root); returns a handle 0..3, or `E_NOENT`, `E_NAMETOOLONG`, `E_NFILE` (4 open), `E_NODEV`, `E_INVAL`, `E_IO`, `E_FAULT`. |
| 17 | `int sys_readdir(int handle, struct dirent *ent)` | Next entry: 1 and `*ent` filled, 0 at the end, or `E_BADF`, `E_IO`, `E_FAULT`. `name` is the long name (the 8.3 name if the entry has no long one), `mtime` the modification time in seconds since 1970 (0 if none). FatFs leaves out the `.` and `..` entries of subdirectories. |
| 18 | `int sys_closedir(int handle)` | Close a directory handle; 0 or `E_BADF`. |
| 19 | `int sys_sd_read_block(uint32_t lba, void *buf)` | Read one 512-byte block at absolute card address `lba` (initialises the card if needed). 0, or `E_INVAL` (beyond the card), `E_IO`, `E_FAULT`. |
| 20 | `int sys_sd_write_block(uint32_t lba, const void *buf)` | Write one 512-byte block. Same results. Bypasses the filesystem. |
| 21 | `void *sys_set_irq_handler(int level, void *handler)` | Install `void handler(void)` for level 1..4, called from the BIOS interrupt routine in supervisor mode; returns the previous handler; `NULL` removes it. Level 4 also enables the HSYNC interrupt. `(void *)E_INVAL` for another level. |
| 22 | `int sys_mem_info(struct mem_info *info)` | `user_start` 0x008000, `user_end` 0x0DA800, `user_end_without_fb1` 0x0ED400. 0, or `E_FAULT` (structure not in user RAM). |
| 23 | `int sys_con_set_cursor(int x, int y)` | Move the console cursor; 0, or `E_INVAL` outside 40x30. |
| 24 | `int sys_con_clear(void)` | Clear the console (page 0); 0. |
| 25 | `int sys_con_info(struct con_info *info)` | Columns, rows, cursor position; 0, or `E_FAULT` (structure not in user RAM). |
| 26 | `int sys_exec(const char *path, const char *args)` | Replace the program with another ELF (see 1.10). `path` as for `sys_open`, with no search path. Returns only on failure: `E_NOENT`, `E_NOEXEC`, `E_NOMEM`, `E_NAMETOOLONG`, `E_NODEV`, `E_IO`, `E_FAULT`, `E_INVAL`. |
| 27 | `int sys_key_event(void)` | Next raw key event (bits 7..0 set-2 scan code, `KEY_RELEASE` bit 8, `KEY_EXTENDED` bit 9), or -1. |
| 28 | `void sys_sleep_ms(uint32_t ms)` | Busy-wait `ms` milliseconds: `ms * 60 / 1000` whole frames (50 on PAL), then a calibrated loop for the rest. |
| 29 | `int sys_set_audio_cb(void *cb)` | Install `void cb(void)` to be called once per video line (15.7 kHz) from the HSYNC interrupt, in supervisor mode at level 4. The callback owns `IO_AUDIO` (it should write one pattern per call); the audio stream is not played while it is installed, and `sys_audio_start` fails. `NULL` removes it. Returns 0. |
| 30 | `uint8_t *sys_audio_start(unsigned bufsize)` | Open the audio stream: pattern bytes played one per video line (15,707 Hz NTSC, 15,625 PAL) from the BIOS's `AUDIO_NBUF` = 3 buffers of `AUDIO_BUF_MAX` = 512 bytes, of which the program fills `bufsize` (1..512) each. Drops anything queued, sets the output to the idle pattern 0xAA and returns the first buffer to fill; nothing plays until it is handed in. Non-blocking. NULL if `bufsize` is out of range or an audio callback is installed. The buffer is BIOS RAM below `USER_BASE`; `sys_read` may still fill it while the stream is open. |
| 31 | `uint8_t *sys_audio_next(const uint8_t *filled, int block)` | Queue the filled buffer (the one last returned) for playing and return the next free buffer. If none is free: `block` != 0 waits, with interrupts running, until the playing buffer finishes (this paces the program to the audio rate); `block` = 0 returns NULL, and the handed buffer is still queued. `filled` = NULL hands nothing in and only asks for a free buffer. NULL also if the stream is not open or `filled` is not the buffer the program holds. Turns the line interrupt on when a buffer is queued with nothing playing. |
| 32 | `int sys_audio_last(const uint8_t *filled, int block)` | Queue the final buffer (`filled` may be NULL) and close the stream; `block` != 0 waits until everything queued has played. After the last byte the BIOS writes the idle pattern 0xAA and switches the line interrupt off. 0, or `E_INVAL` if the stream is not open. |
| 33 | `int sys_unlink(const char *path)` | Delete a file or an empty directory. 0, or `E_NOENT`, `E_EXIST` (directory not empty), `E_ACCES` (read-only entry), `E_NAMETOOLONG`, `E_NODEV`, `E_INVAL`, `E_IO`, `E_FAULT`. |
| 34 | `int sys_rename(const char *old, const char *new)` | Rename or move a file or directory within `old`'s volume. `new` is resolved as a path on that volume, against its current directory (a bare name does not stay in `old`'s directory), and a drive letter in it is ignored. 0, or `E_NOENT`, `E_EXIST` (`new` exists), `E_NAMETOOLONG`, `E_NODEV`, `E_INVAL`, `E_IO`, `E_FAULT`. |
| 35 | `int sys_mkdir(const char *path)` | Create a directory. 0, or `E_EXIST` (the name is taken), `E_NOENT` (no parent), `E_NOSPC`, `E_NAMETOOLONG`, `E_NODEV`, `E_INVAL`, `E_IO`, `E_FAULT`. |
| 36 | `int sys_chdir(const char *path)` | Set the current directory of the path's volume; `"X:"` alone makes X the current drive instead. `chdir("D:/GAMES")` sets D:'s directory without switching to D:. The setting is global: it outlives the program. 0, or `E_NOENT`, `E_NAMETOOLONG`, `E_NODEV`, `E_INVAL`, `E_IO`, `E_FAULT`. |
| 37 | `int sys_getcwd(unsigned len, char *buf)` | The current drive and directory as `"X:/dir/sub"` into `buf` (`len` bytes, NUL included). Returns the length, or `E_INVAL` (does not fit), `E_NODEV` (the current volume is not mounted), `E_FAULT` (also for `len` 0). |
| 38 | `uint32_t sys_time(void)` | The BIOS clock: seconds since 1970-01-01 00:00, local time; 0 if it has not been set since reset ([1.6](#16-timing)). |
| 39 | `int sys_set_time(uint32_t secs)` | Set the clock (the monitor's `date` command uses it). 0 stops it again ("not set"). Returns 0. |
| 40 | `int sys_joystick(void)` | The Atari port's switches as a mask, 1 = closed: `JOY_UP`, `JOY_DOWN`, `JOY_LEFT`, `JOY_RIGHT`, `JOY_FIRE` (one read of the DUART input port; nothing to set up). With paddles plugged in, left and right are the paddle buttons and up and down are the paddles' charge lines, meaningless as switches ([1.4](#joystick-and-paddles)). |
| 41 | `int sys_paddles(struct paddles *p)` | Copy the last complete paddle measurement into `*p`: `a`, `b` 0..`PADDLE_FULL` (1023 = 12 ms, the nominal full scale; longer reads 1023), `valid` with `PADDLE_A_VALID`/`PADDLE_B_VALID` for the lines that fell during the frame (clear: no paddle, or at the end of its travel; the value is then 1023), `frame` the tick count of the measurement. The first call switches measuring on and returns `valid` 0; results follow one or two frames later. While it is on the BIOS owns the DUART's counter/timer and ACR and needs `IMR_IPC` set. `p` = NULL switches measuring off (capacitors shorted, ACR and IMR back to the serial values); so does program exit. 0, or `E_FAULT`. |

Any other number returns `E_NOSYS`. The constants and structures (`O_*`, `SEEK_*`, `ATTR_*`,
`VIDEO_STD_*`, `KEY_*`, `AUDIO_BUF_MAX`, `AUDIO_NBUF`, `JOY_*`, `PADDLE_FULL`, `PADDLE_A_VALID`,
`PADDLE_B_VALID`, `E_*`, `struct video_info`, `struct mem_info`, `struct con_info`,
`struct dirent`, `struct paddles`) are in `generated/syscalls.h`. The hardware constants of the
port (`JOY_IP_*`, `JOY_OP_DISCHARGE`, `PADDLE_FULL_SCALE_US`, `JOY_ACR_PADDLES`) are in
`generated/hw.h`.

| code | value | meaning here |
|---|---|---|
| `E_NOENT` | -2 | no such file or directory, or no such `/dev/` device |
| `E_IO` | -5 | card error (after a failed file call, the next call re-initialises the card), or a flash erase/program failure on C: |
| `E_NOEXEC` | -8 | not a loadable ELF |
| `E_BADF` | -9 | bad descriptor or wrong direction |
| `E_NOMEM` | -12 | ELF does not fit the user region |
| `E_ACCES` | -13 | `sys_unlink` of a read-only entry |
| `E_FAULT` | -14 | pointer outside user RAM, or a path not ended within 128 bytes |
| `E_EXIST` | -17 | the name exists (`sys_rename`, `sys_mkdir`), or the directory is not empty (`sys_unlink`) |
| `E_NODEV` | -19 | no card, or no FAT volume on it (or an unformatted C:) |
| `E_INVAL` | -22 | bad argument, or a drive letter other than C: and D: |
| `E_NFILE` | -23 | too many open files or directories |
| `E_NOSPC` | -28 | volume or directory full |
| `E_NAMETOOLONG` | -36 | a name longer than 63 characters, or malformed |
| `E_NOSYS` | -38 | no such system call |

`E_NOTDIR`, `E_ISDIR` and `E_ROFS` are defined by the ABI but the current BIOS does not
return them from system calls (`E_NOSPC` also stands for FatFs's "access denied", which
includes opening a read-only file for writing).

---

# Part 2: Programming the hardware directly

## 2.1 Ground rules

### Nothing is protected

The glue logic never looks at the 68000's function-code pins (CPLD1's inputs are A1..A23, AS,
UDS, LDS and R/W), so supervisor and user mode see exactly the same address space. A program
started by the monitor runs in user mode, but it can read and write every RAM location
(including the exception vectors at 0x000000 and the BIOS's data), every I/O register, the
soft switches, the DUART and the flash. User mode only takes away the privileged
instructions: the program cannot change the interrupt mask, cannot `stop`, `reset` or `rte`,
and cannot touch USP.

### Getting into supervisor mode

The vector table lives in RAM (the BIOS copies it there from the ROM after the RAM test), so
a program can install its own TRAP handler and call it. This is the clean way to run code with
interrupts masked:

```c
/* uint32_t super_call(uint32_t (*fn)(uint32_t), uint32_t arg): fn(arg) in supervisor
 * mode at IPL 7, through TRAP #0. The C callee saves what it uses (d2-d7/a2-a6). */
#define TRAP0_VECTOR_ADDR 0x080UL               /* vector 32 */
void trap0_handler(void);
uint32_t super_call(uint32_t (*fn)(uint32_t), uint32_t arg);
__asm__(
    "	.text\n"
    "	.globl	super_call\n"
    "super_call:\n"
    "	movea.l	4(%sp),%a0\n"
    "	move.l	8(%sp),%d1\n"
    "	trap	#0\n"
    "	rts\n"
    "	.globl	trap0_handler\n"
    "trap0_handler:\n"
    "	ori.w	#0x0700,%sr\n"        /* mask levels 1..6 */
    "	move.l	%d1,-(%sp)\n"
    "	jsr	(%a0)\n"
    "	addq.l	#4,%sp\n"
    "	rte\n");                     /* back to user mode, caller's mask */

/* install:  void (*volatile *vec)(void) = (void (*volatile *)(void))TRAP0_VECTOR_ADDR;
 *           old = *vec; *vec = trap0_handler;      ... and put `old` back before exiting */
```

`fn` runs on the BIOS's supervisor stack (about 13 KB below 0x008000), so keep it small. GCC
warns ("array subscript 0 is outside array bounds") about pointers to addresses this close to
zero; `--param=min-pagesize=0` in `CFLAGS` silences it.

### Write-only registers and the BIOS's shadows

INTEN, GPIO and AUDIO cannot be read back (they read as 0: the CPLD had no room for the read
path). SDIO can: a read returns the levels of the six SD lines, so it needs no shadow. The
BIOS keeps private copies of INTEN and GPIO (`inten_shadow`, `gpio_shadow` in `fw/`; they are
not part of the ABI) and rewrites the registers from them:

| register | when the BIOS writes it while your program runs | value |
|---|---|---|
| INTEN | `sys_set_irq_handler`, `sys_set_audio_cb`, `sys_audio_start`, `sys_audio_next`/`sys_audio_last` (when they queue a buffer while none is playing), `sys_exit`, `sys_exec`; and the level-4 handler itself when the stream runs out of buffers | `INTEN_VBLANK \| INTEN_PS2 \| INTEN_DUART` (0x000D), plus `INTEN_HSYNC` while an audio callback or level-4 hook is installed or an audio buffer is playing or queued; the level-4 handler writes `inten_shadow` without `INTEN_HSYNC` |
| GPIO | `sys_exit`/`sys_exec` (and the monitor's `led`) | its shadow with LED0, LED1 cleared: normally `GPIO_PS2CLK \| GPIO_PS2DAT` (0x0003) |
| AUDIO | the level-4 handler once per line while a stream buffer is playing; `sys_audio_start`, `sys_audio_last` (with nothing queued), `sys_exit`/`sys_exec` (and the monitor's `beep`) | the next byte of the playing buffer; the idle pattern 0xAA at `sys_audio_start`, at the end of the stream and at exit (after an underrun the last pattern stays). No shadow is kept |
| SDIO | every card access (file, directory, raw block and exec calls on D:) | 0x3F between transfers (every line released, CLK high); no shadow, the driver writes precomputed words |

So: keep your own copy of anything you write, always write the whole register, assume the
BIOS's next write replaces yours, and restore INTEN through the BIOS when you are done (any
`sys_set_irq_handler` call recomputes and rewrites it). In GPIO always keep `GPIO_PS2CLK` and
`GPIO_PS2DAT` set unless you are deliberately driving the keyboard lines: a 0 there pulls the
line low and stops the keyboard; write bit 2 (the audio bit before revision 5) as 0.

The level-4 handler's INTEN write deserves care: it rebuilds the register from the BIOS's
shadow, so if you have written INTEN directly (to mask the keyboard during a transfer, say)
while a stream is playing, the end of the stream (or an underrun) silently undoes your
write. Conversely, if you clear `INTEN_HSYNC` yourself while buffers are queued, the BIOS
still believes it is on: the stream stalls on its last pattern, and a blocking
`sys_audio_next` or `sys_audio_last` waits for ever, until something recomputes INTEN.
Change INTEN through the BIOS, or only while no stream is open.

Every time the BIOS recomputes INTEN (the calls in the INTEN row above, all but the
handler's own write) it also looks at **vector 28** at 0x000070: if it points at one of the
BIOS's two level-4 handlers, it is pointed at whichever fits ([2.7](#what-the-bios-installs));
if it points anywhere else, the BIOS assumes the program replaced it and leaves it alone.
`sys_exit` puts a BIOS handler back unconditionally.

### What `sys_exit` restores, and what it does not

`sys_exit` (and `sys_exec`, before loading) closes files, switches paddle measuring off if it
is on (which writes IMR = `IMR_RXRDYA` and ACR = `SERIAL_ACR_VALUE`, stops the counter/timer and
shorts the paddle capacitors), stops the audio stream (dropping
queued buffers) and writes the idle pattern 0xAA to AUDIO, removes hooks and the audio
callback, points vector 28 back at its own level-4 handler and rewrites INTEN, clears the
LEDs, and selects display page 0. It does **not**
restore: the other exception vectors you replaced, DUART registers you wrote yourself (IMR,
ACR, the counter/timer, OPR, channel B: the paddle switch-off above happens only if
`sys_paddles` switched measuring on), the NTSC/PAL soft switch, SDIO (or the card's state, if you talked to it yourself), or
the flash. A replaced vector that points into your
program becomes a pointer into whatever is loaded next; a DUART interrupt source you enabled
without the BIOS knowing how to clear it will hang the machine at level 3. Put everything back
before exiting, and remember that a crash (exception report) or the NMI button goes straight to
the monitor without running your cleanup: the reset button is the only full recovery.

## 2.2 Memory map

The 68000 has a 24-bit address bus; addresses wrap modulo 16 MB.

| range | region | access | timing | notes |
|---|---|---|---|---|
| 0x000000-0x0FFFFF | RAM (`RAM_BASE`, 1 MB) | r/w, byte/word/long | slot aligned: 4 or 6 clocks | 2x 512Kx8 SRAM, word wide, shared with the video fetch. At reset the ROM is overlaid here (see below). |
| 0x100000-0xDFFFFF | unmapped | | | bus error |
| 0xE00000-0xEFFFFF | ROM (`ROM_BASE`, 1 MB flash window) | read; writes go to the flash chips | 4 clocks, any alignment | 2x SST39SF040, even bytes in U4 (D15..D8), odd in U5 (D7..D0). BIOS image 0xE00000-0xE3FFFF (`ROM_IMAGE_SIZE` 256 KB, word sum zero); 0xE40000-0xEFFFFF (`FLASH_VOL_OFFSET`, `FLASH_VOL_SIZE` 768 KB) the FAT12 volume C:. |
| 0xF00000-0xF0FFFF | IO (`IO_BASE`): CPLD2 registers | **word** access | slot aligned: 4 or 6 clocks | eight 16-bit registers at offsets 0..0x0E, mirrored every 16 bytes |
| 0xF10000-0xF1FFFF | SWITCHES (`SWITCHES_BASE`): CPLD1 soft switches | write (any value) | 4 clocks | offsets 0, 2, 4, 6; reads return garbage |
| 0xF20000-0xF2FFFF | DUART (`DUART_BASE`): 68681 | **odd-address byte** access | DTACK from the 68681 (0.5-0.8 us) | registers at 0xF20001..0xF2001F (odd), mirrored every 32 bytes |
| 0xF30000-0xFFFFEF | unmapped | | | bus error |
| 0xFFFFF0-0xFFFFFF | interrupt acknowledge | | | A23..A4 all ones: CPLD1 answers with VPA (autovector) instead of BERR; do not access it as memory |

**Unmapped addresses** assert BERR combinationally: the access ends in a bus-error exception
(vector 2), which the BIOS reports with the fault address, read/write and function code before
returning to the monitor.

**ROM overlay.** A flip-flop set by the reset circuit makes the RAM region read as ROM, so the
68000 fetches its reset vectors from the flash. It clears at the end of the first access to the
real ROM region (the first instruction fetch, since the reset PC points into 0xE0xxxx). Writes
to RAM while the overlay is set are dropped. The `RESET` instruction does not reset the glue,
so software cannot bring the overlay back.

**RAM layout** (`ram_layout` in the spec; `*_BASE`/`*_SIZE`/`*_END` in `hw.h`):

| range | name | contents |
|---|---|---|
| 0x000000-0x0003FF | `VECTORS` | exception vectors, copied from ROM at boot |
| 0x000400-0x007FFF | `SYSDATA` | BIOS `.data`/`.bss` (0x4780 bytes, to 0x004B80: the file tables of both volumes, the 8 KB erase-sector cache of C:, the RAM-resident flash routine, the script stack and the three 512-byte audio buffers), supervisor stack (about 13 KB) down from `SYS_STACK_TOP` 0x008000 |
| 0x008000-0x0DA7FF | `USER` | program load region (842 KB); user stack down from `USER_STACK_TOP` 0x0DA800 |
| 0x0DA800-0x0ED3FF | `FB1` | framebuffer page 1 (75 KB), free RAM if unused |
| 0x0ED400-0x0FFFFF | `FB0` | framebuffer page 0, the console |

Both pages sit in the top 256 KB quarter of the RAM because CPLD2 supplies the fixed address
bits 19..18 of every video fetch; the page addresses are wired into the CPLDs and cannot be
moved by software.

**Access width rules.** CPLD2 ignores UDS/LDS, so every access to an I/O register is a word
access: use `uint16_t` (the `IO_*` macros do). A byte access to the IO region is undefined. A
long access is two word cycles to consecutive registers (`move.l` to INTSTAT also writes INTEN).
The DUART is on D7..D0 only: use byte accesses at the odd addresses (the `DUART_*` macros);
an even-byte or word access still selects the chip and returns undefined data (a read can pop
the receive FIFO into the wrong lane), and offsets 5 and 21 (CRA, CRB) must never be read.
Words and longs in RAM must be at even addresses (the 68000 raises an address error
otherwise).

## 2.3 The bus and video time-slicing

### The slot

CPLD1 divides the 24 MHz oscillator by two for the CPU and runs everything else on the 24 MHz
edges E0..E7: eight fast clocks make one **slot** of 4 CPU clocks (333 ns). Every slot does
the same thing:

| edge | SRAM side (`docs/architecture.md` section 2) |
|---|---|
| E1 | video address on the SRAM (CPLD1 drives MA16..MA0, CPLD2 MA18..MA17) |
| E2 | SRAM output enable, if this slot fetches video |
| E4 | CPLD2 latches the video word; the CPU's address goes on the SRAM; **decision**: if AS is low for a RAM or IO address that has not been serviced, service it in this slot (DTACK on) |
| E5 | transceivers on (RAM), IOCS (I/O) |
| E6 | write strobe for a RAM write |
| E0 | everything off; the CPU cycle ends |

The first half of every slot belongs to the video, the second half to the CPU. During the
160 active slots of each visible line the video half reads one word (two pixels); in blanking
it reads nothing; either way the CPU half is always there. **The video never takes a bus cycle
away from the CPU.** The CPU's peak RAM bandwidth is one word per slot, 3 M accesses/s, which
is also the fastest a 68000 bus cycle can go (4 clocks).

### The alignment penalty

A RAM or I/O cycle is serviced in the first slot whose E4 already sees AS low:

| CPU bus cycle starts (S0) at | serviced | length |
|---|---|---|
| E0 (slot boundary) | this slot | 4 clocks |
| E4 (mid-slot) | next slot | 6 clocks |
| E2 or E6 (odd phase) | | 7 or 5 clocks; only right after reset |

Every serviced cycle ends at E0, and 68000 internal operations come in 2-clock units, so in
steady state each RAM or I/O access costs **4 clocks if it starts on a slot boundary and 6 if
it starts half way**. In practice: with code and data in RAM, the gap of internal (non-bus)
clocks before each bus cycle is rounded up to a multiple of 4. Instructions that are pure bus
cycles or have internal time in multiples of 4 run at book speed; the others gain 2 clocks:

| instruction | book | from RAM |
|---|---|---|
| `move.l (a0)+,(a1)+` | 20 | 20 |
| `movem.l d1-d7/a1-a6,-(a0)` (13 regs) | 112 | 112 |
| `addq.l #1,d0` | 8 | 8 |
| `dbf d0,loop` (taken) | 10 | 12 |
| `bra.s`, `bcc.s` taken | 10 | 12 |
| `lsl.w #2,d0` | 10 | 12 |
| `move.w (a0),d0` | 8 | 8 |

ROM and SWITCHES cycles get an immediate DTACK and take 4 clocks at any phase, so **code
fetched from the flash never pays the penalty** for its fetches, only for its RAM and I/O data
accesses (this is why the BIOS's inner loops are fast). `docs/architecture.md` estimates
roughly 10 % loss for code in ROM with data in RAM, 15-25 % for code in RAM, compared with an
unshared bus. The emulator charges book timings only (it does not model the penalty), so
programs tuned in the emulator run correspondingly slower on the board. Flash reads are also
the cheapest data source: a lookup table in the free flash area costs 4 clocks per access with
no alignment rule (see [Flash](#212-the-flash-rom)).

DUART cycles are not slot aligned: CPLD1 releases DTACK and the 68681 answers about 2-3 of
its 3.6864 MHz clocks after chip select (0.5-0.8 us), so each DUART register access costs
several times a RAM access.

**Do not use `TAS`** on RAM or I/O: its read-modify-write cycle holds AS low across both
halves and the slot logic will not service the write half. GCC does not generate it.

## 2.4 The framebuffer

* 320x240 pixels, 8 bits per pixel (`VIDEO_BPP`), RGB332: bits 7..5 red, 4..2 green, 1..0
  blue (`RGB332(r, g, b)` packs 8-bit components). No palette: the byte goes straight to three
  binary-weighted resistor DACs, so red and green have 8 linear levels and blue 4, each
  0..0.714 V into the AD724 (`docs/video-encoder.md`).
* Rows are `VIDEO_BYTES_PER_LINE` = 320 bytes, contiguous; a page is `FB_SIZE` = 76,800 bytes
  (0x12C00). Pixel (x, y) is at `page + 320*y + x`. Both page addresses and 320 are multiples
  of 4, so every row is long aligned.
* `FB_PAGE0_ADDR` = 0x0ED400 (`FB0_BASE`, shown after reset, the console) and `FB_PAGE1_ADDR`
  = 0x0DA800 (`FB1_BASE`).
* The video reads one word per slot: the byte at the even address is the left pixel. Each
  pixel lasts 2 CPU clocks (6 MHz pixel rate); a visible line is 640 clocks, 53.3 us.

### Selecting the displayed page

Writing any value to `SW_PAGE0` (0xF10000) or `SW_PAGE1` (0xF10002) sets CPLD1's page
flip-flop (`SW_PAGE0_SET()`, `SW_PAGE1_SET()` in `hw.h`). The video address counter reloads
from it at the start of line 0, the first visible line, so the switch **always takes effect at
the start of the next frame**, whenever it is written: a write during the visible lines does
not disturb the frame being displayed. `sys_video_set_page` does exactly this write and also
records the page for `sys_video_info`; if you write the switch yourself, `current_page` from
`sys_video_info` is stale. `sys_exit` selects page 0.

Tear-free double buffering therefore needs only one rule: do not draw into a page while it is
being displayed. After writing the switch for page B during frame N, page A is displayed until
the end of frame N's line 239; it is free from line 240 on. The VBLANK interrupt (line 243) is a
convenient, slightly late signal for that. Single-buffered drawing is possible if you update
only during the vertical blank (22 lines = 16,808 clocks on NTSC from line 240; 19 lines =
14,516 clocks after the VBLANK interrupt) or only in rows the beam has already passed (racing
the beam, [2.5](#25-video-timing-and-finding-the-beam)).

### Filling fast

`memset` of a page takes about 2.1 frames (*measured*). `movem.l` moves 13 registers (52 bytes)
per instruction with no internal cycles, so it runs at book speed even from RAM; this fill
takes 0.9 frames per page (*measured* 54 frames for 60 fills):

```c
/* void fill_page(void *page, uint32_t value): FB_SIZE bytes; 76800 = 1476 * 52 + 48 */
void fill_page(void *page, uint32_t value);
__asm__(
    "	.text\n"
    "	.globl	fill_page\n"
    "fill_page:\n"
    "	movem.l	%d2-%d7/%a2-%a6,-(%sp)\n"
    "	movea.l	48(%sp),%a0\n"             /* page */
    "	move.l	52(%sp),%d0\n"             /* value */
    "	adda.l	#76800,%a0\n"              /* fill downwards from the end */
    "	move.l	%d0,%d1\n	move.l	%d0,%d2\n	move.l	%d0,%d3\n"
    "	move.l	%d0,%d4\n	move.l	%d0,%d5\n	move.l	%d0,%d6\n"
    "	move.l	%d0,%d7\n	movea.l	%d0,%a1\n	movea.l	%d0,%a2\n"
    "	movea.l	%d0,%a3\n	movea.l	%d0,%a4\n	movea.l	%d0,%a5\n"
    "	move.w	#1476-1,%a6\n"
    "	exg	%d0,%a6\n"                 /* d0 = count, a6 = value */
    "1:	movem.l	%d1-%d7/%a1-%a6,-(%a0)\n"  /* 13 longs = 52 bytes */
    "	dbf	%d0,1b\n"
    "	movem.l	%d1-%d7/%a1-%a5,-(%a0)\n"  /* the last 48 bytes */
    "	movem.l	(%sp)+,%d2-%d7/%a2-%a6\n"
    "	rts\n");
```

The loop is 112 + 12 clocks per 52 bytes, about 183,000 clocks per page: 91 % of an NTSC frame.

### Page 1 as RAM

A program that does not double buffer can use 0x0DA800-0x0ED3FF as 75 KB of ordinary RAM
(`mem_info.user_end_without_fb1`). The ELF loader will not place segments there; use a
pointer.

## 2.5 Video timing and finding the beam

All horizontal quantities are CPU clocks (12 MHz) and multiples of the 4-clock slot. The
spec's origin is the first visible pixel; the V counter advances at the start of horizontal
sync, so a line as the counters see it runs sync, back porch, visible, front porch:

| | NTSC (default) | PAL |
|---|---|---|
| clocks per line (`VIDEO_*_LINE_CLOCKS`) | 764 (191 slots) | 768 (192 slots) |
| visible (`H_ACTIVE`) | 640 clocks, 160 slots, 320 pixels | 640 |
| front porch / sync / back porch | 16 / 56 / 52 | 16 / 56 / 56 |
| line rate | 15,706.8 Hz (63.67 us) | 15,625 Hz (64.00 us) |
| lines per frame (`LINES`) | 262 | 312 |
| visible lines | 0-239 | 0-239 (letterboxed) |
| bottom border (`V_BOTTOM`) | 240-242 | 240-269 |
| vertical sync (`V_SYNC`) | 243-245 | 270-272 |
| top border (`V_TOP`) | 246-261 | 273-311 |
| clocks per frame (`VIDEO_*_FRAME_CLOCKS`) | 200,168 (16.68 ms, 59.95 Hz) | 239,616 (19.97 ms, 50.08 Hz) |

Progressive ("240p"): every frame is the same field.

**Flags and live pins in INTSTAT:**

* `INTSTAT_HSYNC` (flag): set at the leading edge of every horizontal sync, 262 times per NTSC
  frame including the blanking lines. W1C.
* `INTSTAT_VBLANK` (flag): set at the leading edge of vertical sync, i.e. at the start of line
  243 (NTSC) / 270 (PAL), **not** at the end of the visible area. W1C. The BIOS's level-1
  handler clears it immediately, so a program polling it will rarely see it; poll
  `INTSTAT_VSYNCPIN` instead.
* `INTSTAT_HSYNCPIN`, `INTSTAT_VSYNCPIN`: live levels, 1 during the 56-clock sync and during
  the three sync lines.
* `INTSTAT_DEPIN`: live display enable, 1 during the fetch slots of visible lines (the fetch
  leads the pixel on the pins by about a slot).

There is **no line-counter register**. To know where the beam is, count HSYNC flags from a
known point. The falling edge of VSYNCPIN is the start of line 246 (NTSC), `V_TOP` = 16 lines
before line 0. With the HSYNC interrupt off (the BIOS enables it only while an audio callback or
level-4 hook is installed or an audio buffer is playing or queued), a program can poll:

```c
/* raster.c: find the beam with INTSTAT polling. Needs INTEN.HSYNC off (the BIOS leaves it
 * off unless an audio callback or level-4 hook is installed or sound is playing). */
#include <stdint.h>
#include <stdio.h>
#include "hw.h"
#include "syscalls.h"

static void wait_vsync_end(void)                /* returns at the start of line 246 (NTSC) */
{
    while (!(IO_INTSTAT & INTSTAT_VSYNCPIN)) ;
    while (IO_INTSTAT & INTSTAT_VSYNCPIN) ;
    IO_INTSTAT = INTSTAT_HSYNC;                 /* W1C: start counting from here */
}

static void wait_lines(unsigned n)              /* n line starts (HSYNC leading edges) */
{
    while (n--) {
        while (!(IO_INTSTAT & INTSTAT_HSYNC)) ;
        IO_INTSTAT = INTSTAT_HSYNC;
    }
}

/* Return at the HSYNC that begins line `line` (0..239): 108 clocks before its first pixel. */
static void wait_line(unsigned line, unsigned v_top)
{
    wait_vsync_end();
    wait_lines(v_top + line);
}

int main(void)
{
    unsigned lines = 0;
    wait_vsync_end();
    while (!(IO_INTSTAT & INTSTAT_VSYNCPIN)) {          /* count to the next vsync */
        if (IO_INTSTAT & INTSTAT_HSYNC) { IO_INTSTAT = INTSTAT_HSYNC; lines++; }
    }
    while (IO_INTSTAT & INTSTAT_VSYNCPIN)
        if (IO_INTSTAT & INTSTAT_HSYNC) { IO_INTSTAT = INTSTAT_HSYNC; lines++; }
    printf("%u lines per frame\n", lines);

    uint32_t t = sys_ticks();
    wait_line(120, VIDEO_NTSC_V_TOP);
    int de = (IO_INTSTAT & INTSTAT_DEPIN) != 0;
    printf("at line 120: DEPIN %d (ticks moved %lu)\n", de, (unsigned long)(sys_ticks() - t));
    return 0;
}
```

It prints `262 lines per frame` in the emulator. The flags are sticky, so a VBLANK or keyboard
interrupt that delays the loop by less than a line costs no count. From the HSYNC that starts
line L, the first pixel of line L appears 56 + 52 = 108 clocks later and the line's last pixel
748 clocks later; that is the budget for "racing the beam" tricks.

### NTSC and PAL

`SW_NTSC` (0xF10004) and `SW_PAL` (0xF10006) select CPLD1's timing table; the change takes
effect at the start of the next frame. Reset selects NTSC. The AD724's standard is fixed by
resistors and a subcarrier crystal, and `INTSTAT_PALJMP` reports the board's strapping: rev B4
and rev C1 tie it low (NTSC only, there is no PAL jumper on the board). The BIOS selects PAL
at boot only when PALJMP reads 1. Do not select the PAL table on an NTSC board: the encoder will not lock to
312 lines.

## 2.6 The I/O registers

### CPLD2 registers (`IO_BASE` 0xF00000)

Eight 16-bit registers selected by A1..A3; A4..A15 are not decoded, so the block repeats every
16 bytes through 0xF0FFFF. Word access only.

| offset | address | name | access | reset | |
|---|---|---|---|---|---|
| 0x0 | 0xF00000 | `IO_INTSTAT` | read; write 1 to clear flags | flags 0 | interrupt flags and live pin levels |
| 0x2 | 0xF00002 | `IO_INTEN` | write only (reads 0) | 0x0000 | interrupt enables |
| 0x4 | 0xF00004 | `IO_SDIO` | read/write (reads the line levels) | 0x003F | SD card lines (spec rev 7) |
| 0x6 | 0xF00006 | `IO_GPIO` | write only (reads 0) | 0x0003 | PS/2 drive, LEDs |
| 0x8 | 0xF00008 | `IO_PS2DATA` | read | 0x0000 | last PS/2 byte |
| 0xA | 0xF0000A | `IO_AUDIO` | write only (reads 0) | 0x0000 | PDM audio shifter (spec rev 5) |
| 0xC | 0xF0000C | none | reads 0, writes ignored | | `USERIO` until spec rev 6; removed in rev 7 |
| 0xE | 0xF0000E | `IO_ID` | read | 0x6802 | identification |

**INTSTAT** (0xF00000). Bits not listed read 0.

| bit | name | kind | meaning |
|---|---|---|---|
| 0 | `INTSTAT_VBLANK` | flag, W1C | set at the leading edge of vertical sync (line 243 NTSC, 270 PAL); level 1 when enabled |
| 1 | `INTSTAT_HSYNC` | flag, W1C | set at the leading edge of every horizontal sync; level 4 when enabled |
| 2 | `INTSTAT_PS2RDY` | flag | a PS/2 byte is in PS2DATA; cleared by reading PS2DATA (writing 1 does nothing); level 2 when enabled |
| 3 | `INTSTAT_DUARTIRQ` | live | the 68681's IRQ output, inverted (1 = requesting); level 3 when enabled; cleared only by removing the cause in the DUART |
| 5 | `INTSTAT_PS2ERR` | flag, W1C | framing or parity error in the last PS/2 frame (rewritten at the end of every frame) |
| 7 | `INTSTAT_NMIBTN` | live | 1 while the NMI button is pressed |
| 9 | `INTSTAT_PS2CLK` | live | PS/2 clock line |
| 10 | `INTSTAT_PS2DAT` | live | PS/2 data line |
| 12 | `INTSTAT_PALJMP` | live | 1 = board strapped for PAL; 0 on rev B4 and C1 |
| 13 | `INTSTAT_VSYNCPIN` | live | 1 during the vertical sync lines |
| 14 | `INTSTAT_DEPIN` | live | 1 during visible fetch slots |
| 15 | `INTSTAT_HSYNCPIN` | live | 1 during horizontal sync |

Bit 8 was `INTSTAT_SDMISO` (the SPI card's data out) until spec revision 6; it reads 0 now
that the card answers on `SDIO`. Writing INTSTAT clears exactly the W1C flags written as 1
(VBLANK, HSYNC, PS2ERR); other bits are ignored. If an event sets a flag in the same clock as
a write clears it, the set wins.

**INTEN** (0xF00002), write only, reset 0x0000. A flag whose enable is set drives the 68000's
IPL lines at the flag's level; the CPLD encodes the highest such level.

| bit | name | level |
|---|---|---|
| 0 | `INTEN_VBLANK` | 1 |
| 1 | `INTEN_HSYNC` | 4 |
| 2 | `INTEN_PS2` | 2 |
| 3 | `INTEN_DUART` | 3 |

**SDIO** (0xF00004), read/write, reset 0x003F (every line released, so the bus idles with CLK
high). Six open-drain drivers for the native SD bus: writing 0 to a bit drives that line low,
writing 1 releases it to its 10 k pull-up to 3.3 V. Reading returns the **level** of each line
in the same bit position, which is the AND of what the CPU and the card drive: the card's
responses on CMD and its data on DAT0..3 are read here. Bits 15..6 read 0 and are ignored on
write. Protocol and timing in [2.10](#210-sd-card).

| bit | name | line |
|---|---|---|
| 0 | `SDIO_DAT3` | DAT3 (the pin that was CS in SPI mode), data bit 3 of each nibble |
| 1 | `SDIO_CLK` | card clock, host driven; idle high |
| 2 | `SDIO_CMD` | command line: host commands out, card responses back |
| 3 | `SDIO_DAT0` | DAT0, data bit 0; the only data line in 1-bit mode; busy after a write |
| 4 | `SDIO_DAT1` | DAT1, data bit 1 |
| 5 | `SDIO_DAT2` | DAT2, data bit 2 |

Until spec revision 6 (rev B4 boards) this register was write only with `SDIO_CS`, `SDIO_CLK`
and `SDIO_MOSI` in bits 0..2 and MISO in `INTSTAT`; code for that wiring does not work on C1.

**GPIO** (0xF00006), write only, reset 0x0003.

| bit | name | |
|---|---|---|
| 0 | `GPIO_PS2CLK` | PS/2 clock drive: 1 releases the line (pulled up; read it in INTSTAT), 0 pulls it low |
| 1 | `GPIO_PS2DAT` | PS/2 data drive, same convention |
| 2 | none | unused since revision 5 (it was `GPIO_AUDIO`); stored but drives nothing; write 0 |
| 3 | `GPIO_LED0` | LED 0, 1 = on |
| 4 | `GPIO_LED1` | LED 1, 1 = on |

**PS2DATA** (0xF00008), read: bits 7..0 (`PS2DATA_DATA_MASK`) are the last byte received.
Reading clears `INTSTAT_PS2RDY`; the byte itself stays until the next frame completes.

**AUDIO** (0xF0000A), write only, reset 0x0000. Bits 7..0 (`AUDIO_PATTERN_MASK`) load an 8-bit
rotating shift register whose bit 7 drives the audio pin; bits 15..8 are ignored. CPLD2 rotates
it left one place at the leading edge of HSYNC and then every 24 slots (96 CPU clocks), so a
line carries eight bits, about 126 kHz. A write replaces the register at once and wins over a
rotation in the same clock; a line without a write repeats the previous byte (rotated, not
shifted, so nothing is lost). The fraction of 1 bits sets the output level. Details, timing and
use in [2.8](#28-audio).

Offset 0xC (0xF0000C) was **USERIO**, four open-drain user pins on the expansion header,
until spec revision 6. Revision 7 removed it to make room for the extra SD lines (CPLD2 is now at
123 of 128 macrocells): the offset
reads 0, writes do nothing, and `hw.h` no longer defines `IO_USERIO`.

**ID** (0xF0000E), read: `ID_MAGIC` 0x68 in bits 15..8, `ID_REV` (CPLD2 logic revision) in
bits 7..0; 0x6802 on the DUART board.

### Soft switches (`SWITCHES_BASE` 0xF10000)

Write-only actions in CPLD1: any write (any value, word access) to the address performs it;
4-clock cycles; reads return garbage. `hw.h` provides `SW_PAGE0_SET()` etc.

| address | name | action |
|---|---|---|
| 0xF10000 | `SW_PAGE0` | display page 0 (0x0ED400) from the next frame |
| 0xF10002 | `SW_PAGE1` | display page 1 (0x0DA800) from the next frame |
| 0xF10004 | `SW_NTSC` | NTSC timing table (262 lines) from the next frame; the reset default |
| 0xF10006 | `SW_PAL` | PAL timing table (312 lines) from the next frame |

## 2.7 Interrupts

| level | source | vector | vector address | enable | acknowledge |
|---|---|---|---|---|---|
| 1 | VBLANK flag | 25 | 0x064 (`IRQ_VBLANK_VECTOR_ADDR`) | `INTEN_VBLANK` | write `INTSTAT_VBLANK` to INTSTAT |
| 2 | PS/2 byte (PS2RDY) | 26 | 0x068 | `INTEN_PS2` | read `IO_PS2DATA` |
| 3 | DUART IRQ pin | 27 | 0x06C | `INTEN_DUART` | remove the cause in the 68681 (read RBx, read IPCR, stop the counter, mask in IMR, ...) |
| 4 | HSYNC flag | 28 | 0x070 | `INTEN_HSYNC` | write `INTSTAT_HSYNC` to INTSTAT |
| 7 | NMI button | 31 | 0x07C | always | edge triggered by the 68000; nothing to clear |

Levels 5 and 6 have no source. All interrupts are **autovectored**: CPLD1 asserts VPA for
every interrupt-acknowledge cycle (the 68681's IACK pin is tied inactive and its IVR is unused),
so the 68000 takes vector 24 + level. CPLD2 holds the IPL lines at the highest level whose flag
and enable are both set, so a handler that returns without clearing its flag is entered again
immediately after its `rte`.

### What the BIOS installs

At boot the BIOS copies its vector table from the ROM to 0x000000. Vectors 25-28 point at the
BIOS handlers in ROM, vector 47 (TRAP #15, `SYSCALL_VECTOR`) at the system-call dispatcher,
and every other vector at a stub that reports the exception and enters the monitor. INTEN is
`INTEN_VBLANK | INTEN_PS2 | INTEN_DUART`; HSYNC is enabled only while an audio callback or a
level-4 hook is installed or an audio buffer is playing or queued. The handlers:

| level | BIOS handler |
|---|---|
| 1 | clear VBLANK, `ticks++`, advance the clock; while paddles are measured, store the frame's paddle result and short the capacitors, or, if no line rose after the last short, note the counter/timer and release them ([2.11](#the-input-and-output-ports)); call the level-1 hook |
| 2 | read INTSTAT and PS2DATA; on PS2ERR clear it and count an error, else decode the byte into the character and key-event queues; call the level-2 hook |
| 3 | read the DUART ISR; if RXRDYA, pop RBA into the 512-byte serial ring while SRA.RXRDY (counting overruns, clearing OE); if IPC (enabled only while paddles are measured), read IPCR, which clears it, and the counter/timer; after a short, a rising line means the discharge has been seen, and the handler notes the count and releases the capacitors; after the release, it records the paddle whose line fell; call the level-3 hook. The ring is what `sys_serial_getc` and `/dev/ttya` read; it fills whether or not a program reads it |
| 4 | `irq_hsync.S`, assembler, two handlers. `irq_hsync_fast` (no callback, no level-4 hook): clear HSYNC, write the next byte of the playing stream buffer to AUDIO; at the end of a buffer release it and take the next queued one, or, with none queued, clear `INTEN_HSYNC` (through `inten_shadow`), writing the idle pattern 0xAA first if the stream was closed with `sys_audio_last`. `irq_hsync_slow`: clear HSYNC, call the audio callback if one is installed, otherwise run the same stream step in C (leaving the interrupt on while a hook is installed), then call the level-4 hook |
| 7 | through the exception path: print registers, enter the monitor |

The BIOS points vector 28 at one handler or the other: whenever it recomputes INTEN
(`audio_update_mode()`, called from `irq_update_inten()`), the RAM vector at 0x000070 gets
`irq_hsync_slow` if an audio callback or a level-4 hook is installed and `irq_hsync_fast`
otherwise, so the fast handler carries no test for either. It only does this while the
vector still points at one of those two handlers; a vector the program has replaced is left
untouched until `sys_exit` (`audio_reset_vector()`). The fast handler is the
stream player and is hand-counted in the source: 44 for the exception entry, 24 for
`movem.l d0/a0,-(sp)`, 20 for the W1C, 20 + 8 for loading and testing the play pointer, 4 + 8
for fetching the byte, 16 for the AUDIO write, 20 for storing the pointer, 22 + 8 for
comparing it with the buffer end, 28 for the `movem` back and 20 for `rte`: **242 clocks per
764-clock line**, 32 % of the CPU; `sdk/examples/abench.c` measures 240.6 in the emulator
(68.5 % left). The end-of-buffer exit, taken once per buffer, costs about 100 more. Because
the handler switches its own interrupt off when it runs out of buffers and `sys_audio_next`
switches it back on, a program that is not making sound pays nothing for audio. The slow
handler saves two more registers and makes C-ABI calls, so a callback or a level-4 hook costs
more (see [What an interrupt costs](#what-an-interrupt-costs)).

User code (main program) always runs at mask 0, so any enabled interrupt is taken between its
instructions; system calls also run at the caller's mask, so interrupts continue during file
I/O and waits.

### Three ways to use an interrupt

1. **A hook** (`sys_set_irq_handler`, Part 1): simplest; the BIOS acknowledges and you pay its
   overhead.
2. **Replace the vector.** Write your handler's address into the RAM vector. You must
   acknowledge the source yourself, preserve every register, end with `rte`, keep the enable
   bit set, and put the old vector back before the program exits. Replacing level 1 stops
   `sys_ticks`, `sys_wait_vblank`, `sys_sleep_ms` and the paddle measurement (chain to the old
   handler or count ticks yourself); replacing level 2 starves the BIOS's keyboard queues and
   level 3 its serial ring (`sys_serial_getc`, `/dev/ttya`) and the paddles, and at level 3
   your handler must drain the receiver (or clear RXRDYA from IMR), and read IPCR while paddles
   are measured, or the level-sensitive DUART request never goes away.
3. **Poll the flag** with the interrupt disabled, as in [2.5](#25-video-timing-and-finding-the-beam).

For HSYNC, the tidy way to get the enable bit set and kept consistent with the BIOS is a
dummy level-4 hook plus your own vector, in either order: installing the hook makes the BIOS
set `INTEN_HSYNC`, and once your vector is in, the BIOS never calls the hook, because your
vector bypasses its handler, and it leaves your vector alone when it recomputes INTEN.
Removing the hook clears the enable again. Put the BIOS vector back yourself before you
exit (or let `sys_exit` do it), and do not mix this with a BIOS audio stream.

The BIOS's fast handler is itself a lean stream player, so taking over the vector for audio
saves little: about 16 clocks per line for the C handler below, 74 for the assembler one. It
is worth it for a different data format (16-bit words, one long sound with no buffer
hand-off, several channels interleaved) or to combine the sample write with other per-line
work. The example plays a triangle wave encoded with the SDK's sigma-delta rule (`pdm_line`
below; the algorithm is explained in [2.8](#28-audio)).

```c
/* lean.c: take over the HSYNC autovector with a C interrupt handler that plays
 * pre-encoded PDM patterns; measure what is left of the CPU. */
#include <stdint.h>
#include <stdio.h>
#include "hw.h"
#include "syscalls.h"

#define NWORDS 32768                            /* ~2 s at 15.7 kHz */

static uint16_t snd[NWORDS];
static const uint16_t *volatile snd_ptr;
static const uint16_t *snd_end;

__attribute__((interrupt_handler)) static void hsync_isr(void)
{
    const uint16_t *p = snd_ptr;
    IO_INTSTAT = INTSTAT_HSYNC;                 /* acknowledge first (W1C) */
    if (p != snd_end) {
        IO_AUDIO = *p++;
        snd_ptr = p;
    }
}

/* One line's pattern for sample s (0..255), the SDK encoder's rule (sdk/pdm.c):
 * first-order sigma-delta, 8 steps per line, accumulator carried from line to line. */
static uint8_t pdm_line(uint8_t s, uint8_t *acc)
{
    unsigned a = *acc, v = s & 0xFE, p = 0;     /* 2 * (top 7 bits of s) */
    for (int i = 0; i < 8; i++) {
        a += v;
        p = (p << 1) | (a >> 8);                /* carry out = the bit */
        a &= 0xFF;
    }
    *acc = (uint8_t)a;
    return (uint8_t)p;
}

static void ignore(void) {}

static volatile uint32_t frames;
static void vbl(void) { frames++; }

static uint32_t spin_one_second(void)
{
    uint32_t n = 0, f = frames;
    while (frames == f) ;
    f = frames;
    while (frames - f < 60) n++;
    return n;
}

int main(void)
{
    static const uint8_t tri[32] = { 128, 144, 160, 176, 192, 208, 224, 240, 255, 240, 224, 208,
        192, 176, 160, 144, 128, 112, 96, 80, 64, 48, 32, 16, 1, 16, 32, 48, 64, 80, 96, 112 };
    uint8_t acc = 0;
    for (unsigned i = 0; i < NWORDS; i++)       /* 491 Hz triangle */
        snd[i] = pdm_line(tri[i & 31], &acc);
    sys_set_irq_handler(1, vbl);
    uint32_t idle = spin_one_second();

    void (*volatile *vec)(void) = (void (*volatile *)(void))IRQ_HSYNC_VECTOR_ADDR;
    void (*bios_isr)(void) = *vec;
    snd_end = snd + NWORDS;
    snd_ptr = snd;
    sys_set_irq_handler(4, ignore);             /* the BIOS sets INTEN.HSYNC */
    *vec = hsync_isr;                           /* our handler on vector 28 (the BIOS leaves it alone) */
    uint32_t busy = spin_one_second();
    sys_set_irq_handler(4, NULL);               /* BIOS clears INTEN.HSYNC */
    *vec = bios_isr;                            /* put the BIOS handler back */
    IO_AUDIO = 0xAA;                            /* back to the idle level */

    printf("CPU left: %lu%% (%lu clocks per line)\n", (unsigned long)(busy * 100 / idle),
           (unsigned long)((idle - busy) * VIDEO_NTSC_LINE_CLOCKS / idle));
    return 0;
}
```

GCC's `interrupt_handler` attribute saves exactly the registers the function uses and returns
with `rte`. This one compiles to 12 instructions and prints `CPU left: 70% (226 clocks per
line)` in the emulator, where `--audio-wav` shows one second of a 491 Hz triangle (the
second the measurement runs). With steps 1 and 2 the other way round it prints `CPU left:
55% (338 clocks per line)` and stays silent: the hook call has put the BIOS's slow handler
back on the vector, and that measures the slow handler calling `ignore`. The same job written in assembler, keeping the pointer in
memory and using one register:

```asm
hsync_isr:
        move.w  #INTSTAT_HSYNC_MASK,IO_INTSTAT_ADDR   | acknowledge
        move.l  %a0,-(%sp)
        movea.l snd_ptr,%a0
        move.w  (%a0)+,IO_AUDIO_ADDR
        move.l  %a0,snd_ptr
        movea.l (%sp)+,%a0
        rte
```

costs 168 clocks per line including entry and `rte` (*measured*; it has no end check, so the
buffer must be a ring the main program keeps ahead of it, or long enough).

### What an interrupt costs

| | clocks | of a 764-clock NTSC line |
|---|---|---|
| exception entry (autovector), book timing | 44 | |
| `rte` | 20 | |
| minimal handler: acknowledge + `rte`, *measured* | 84 | 11 % |
| assembler sample player above, *measured* | 168 | 22 % |
| C `interrupt_handler` sample player above, *measured* | 226 | 30 % |
| BIOS fast level-4 handler: the stream player (hand count 242; *measured* by `abench` 240.6) | 242 | 32 % |
| BIOS slow handler + empty audio callback, *measured* | 272 | 36 % |
| BIOS slow handler + assembler callback popping a pattern from a ring, *measured* | 358 | 47 % |
| BIOS slow handler + C callback popping a pattern from a ring, *measured* | 423 | 55 % |

The last four rows are `sdk/examples/abench.c` (`abench` at the prompt), which counts
iterations of a fixed loop with each audio path active (12 frames for the callbacks, 5 for
the stream, inside the 1,536 lines of three queued 512-byte buffers) and prints the CPU left:
68.5 %, 64.4 %, 53.2 % and 44.6 %. The program feeding the stream pays for the encoding:
`pdm_encode` costs about 83 clocks per sample (also *measured* by `abench`), plus one
`sys_audio_next` per buffer, which is negligible per sample. A program that streams sound
continuously therefore spends about 325 clocks of every line on audio, 43 %, and keeps about
440 for computing the samples.

Every clock in a per-line handler is spent 15,707 times a second. On the board, add to these:
the entry's interrupt-acknowledge cycle is a 68000 "VPA" (6800-style) cycle that waits for the
E clock (CPU clock / 10), so it takes roughly 10-20 clocks instead of the 4 the book figure
assumes, depending on E's phase; and the stack pushes, the vector fetch (the table is in RAM)
and every RAM or I/O access in the handler pay the slot-alignment penalty. Neither is modelled
by the emulator.

Advice for lean handlers:

* Acknowledge first (W1C), so an event during the handler is not lost.
* Do the minimum: one prepared value out, one pointer bumped. Precompute in the main loop.
* Save only what you use; `move.l an,-(sp)` (12 clocks) beats `movem` for one or two
  registers. No `jsr` (16 + 16 clocks for the call and return).
* Do not call the BIOS. The supervisor stack is shared and about 8 KB.
* Keep a level-4 handler well under 764 clocks, or lines merge (the flag is sticky, so a late
  handler sees one event for two lines) and the lower levels starve.
* Worst-case latency is the longest instruction (a `divs` can take about 160 clocks), plus any
  higher-level handler that is running, plus time spent at a raised mask.

## 2.8 Audio

### The shifter

Since spec revision 5 the audio pin is bit 7 of an 8-bit rotating shift register in CPLD2,
loaded by writing `IO_AUDIO` (0xF0000A, word access, pattern in bits 7..0, write only, reset
0x0000 so the pin starts low). CPLD2 rotates it left one place at the leading edge of HSYNC and
then every 24 slots (96 CPU clocks) after it, so each video line shows the eight bits for 96
clocks each, about 126 kHz. The slot counter restarts at every HSYNC, so the rotation never
drifts against the line; on NTSC (191 slots per line) the eighth bit of each line lasts 92
clocks instead of 96, on PAL (192 slots) all eight are equal.

* **Pattern semantics.** The pattern is a pulse-density code: the fraction of 1 bits sets the
  level the low-pass filter turns it into. 0x00 is the bottom, 0xFF the top, 0xAA or 0x55
  (four ones) the middle, which is the silence level the BIOS uses. Only the count of ones
  matters for the level; how they are spread decides where the ripple goes (below).
* **Repeat.** The register rotates rather than shifting in zeros, so a line without a write
  plays the same eight bits again. A constant level therefore needs one write, ever.
* **Writes.** A write replaces the register at once, mid-bit if need be, and wins over a
  rotation due in the same clock.
* **Rotation phase.** The written byte leaves bit 7 first only if the write lands in the first
  eighth of the line (within 96 clocks of the HSYNC edge). A write in eighth k (k = 0..7) shows
  bit 7 at once, then keeps rotating, so at the following line starts the register holds the
  byte rotated *right* by k: the order of the bits on the pin is a rotation of the byte. The
  density, hence the level, is unaffected. A player that writes at the same point of every line
  still emits the bits in order, only delayed by k eighths: the end of one line's pattern runs
  into the start of the next write. The BIOS's fast handler writes 120-150 clocks after the
  edge in the emulator (*measured* with `--io-trace` over the 40,960 lines of `arp.c` in
  [1.8](#18-sound-the-easy-way); the spread is interrupt latency, the instruction being
  executed when HSYNC arrives), so k is 1 on all but a few lines; on the board the VPA
  acknowledge and the slot penalties make it a few tens of clocks later. Whenever k changes,
  one bit is held a little longer or shorter than 96 clocks. That is a small extra noise, not
  a change of level.

### Writing patterns directly

Because the shifter repeats its byte, many sounds need no per-line interrupt at all:

* **Fixed levels.** With one pattern per line there are nine levels, k ones out of eight. The
  evenly spread patterns are the best choice: 0x00, 0x80, 0x88, 0x92, 0xAA, 0xB6, 0xEE, 0xFE,
  0xFF. Those that repeat within the line (0x88, 0xAA, 0xEE: every 4 or 2 bits) put their ripple
  at 31 or 63 kHz, where the filter removes it; the others (0x80, 0x92, 0xB6, 0xFE) repeat once
  per line and leave a faint 15.7 kHz whine, as does any lumpy pattern such as 0xF0.
* **Beeps with a volume.** Writing two patterns alternately at twice the tone frequency makes a
  square wave whose amplitude is the difference in density: 0xFF/0x00 is full volume (the old
  1-bit behaviour; the monitor's `beep` does exactly this, 0xFF and 0x00 every 500 us for
  200 ms, then 0xAA), 0xEE/0x88 half, 0xAA/0x88 a quarter. The writes can come from the main
  loop, a VBLANK hook (tones of 30 Hz down) or the 68681 timer
  ([2.11](#211-serial-the-68681-duart)), at any point in the line, since a write takes effect
  at once. Interrupts lengthen software delays; for steady pitch use a timer interrupt.
* **Per-line patterns.** One pattern per line gives full sample playback at the line rate.
  The BIOS stream (below) is the cheapest way; your own HSYNC code (a `sys_set_audio_cb`
  callback, or your own vector as in [2.7](#three-ways-to-use-an-interrupt)) is the
  alternative when the patterns must be computed in the interrupt. Write as early in the line
  as you can and at the same point every line. While you write AUDIO yourself, do not also
  run a BIOS stream: its handler writes the register every line while a buffer plays, and the
  idle pattern at the end.

### The BIOS's audio stream

The stream calls of [1.8](#18-sound-the-easy-way) move pattern bytes, not samples: the BIOS
does no per-sample work outside its interrupt handler. Its side, in `fw/audio.c` and
`fw/irq_hsync.S`:

* **Buffers.** `AUDIO_NBUF` = 3 buffers of `AUDIO_BUF_MAX` = 512 bytes in the BIOS data area,
  of which the first `bufsize` (from `sys_audio_start`) are played. The program holds at most
  one; the others are free, queued or playing. Queued buffers wait in a 4-slot FIFO of buffer
  indices: the system calls append at its tail, the handler plays the head and advances it
  when a buffer is finished, which is what frees the buffer. The two sides share no lock and
  nothing runs with interrupts masked for more than a few instructions.
* **Start.** A buffer handed in while nothing plays starts at once: the call sets the play
  pointer and turns `INTEN_HSYNC` on, and the next HSYNC writes its first byte. From then on
  the fast handler writes one byte per line.
* **Joins.** At the end of a buffer the handler moves to the next queued one in the same
  interrupt, so consecutive buffers join without a missed line. A blocking `sys_audio_next`
  spins in the system call (interrupts running) until that release, so it returns within
  a line of a buffer coming free.
* **Underrun.** If nothing is queued when a buffer ends, the handler clears the play pointer
  and switches `INTEN_HSYNC` off. The shifter keeps repeating the last pattern, a constant
  level near the last sample, so the gap is heard as a click or a short dropout, not as
  noise. The stream stays open and the next `sys_audio_next` with a buffer restarts it at
  once.
* **End.** After `sys_audio_last` the handler plays out what is queued, writes the idle
  pattern 0xAA and switches the interrupt off; the stream is then closed (`sys_audio_next`
  returns NULL) until the next `sys_audio_start`. `sys_audio_start` on an open stream drops
  everything and starts again; so does `sys_exit`, without starting again.
* **Latency.** A buffer handed in is heard after the ones ahead of it: up to two buffers, 66 ms
  with 512 bytes, 33 ms with 256. Smaller buffers react sooner but leave less slack for a slow
  moment in the program; on average the program must hand in one buffer per buffer time
  either way.
* **Hooks and callbacks.** While a level-4 hook is installed the slow handler plays the stream
  (the same steps in C, so it costs more per line) and keeps the interrupt on between buffers.
  While an audio callback is installed the stream is not played at all, and `sys_audio_start`
  refuses to open one.

Because the bytes are raw patterns, the stream also plays anything that is not 8-bit samples
at the cost of the interrupt alone: buffers of the fixed-level or beep patterns above, or
sounds encoded once at load time and copied into the buffers with `memcpy` when they are
needed.

### The SDK's encoder

`pdm_encode` (`sdk/pdm.c`) turns each 8-bit sample into one pattern with a **first-order
sigma-delta** modulator run for the eight bits of the line, carrying its 8-bit accumulator
from sample to sample (and from call to call). For accumulator `acc` (0..255) and the
sample's top seven bits `v7`, each of the eight steps is `s = acc + 2*v7; bit = s >> 8;
acc = s & 255`, and the first step's bit is pattern bit 7. Over many lines the density of
ones is `v7 / 128`, and the error left in the accumulator is carried into the next line
instead of being lost, which is what lets 9 levels per line add up to 128 levels over time.
From an empty accumulator, constant samples give 0x00 for 0, 0x11 for 64, 0x55 for 128 (the
silence level) and 0x77 for 192; 255 gives 0xFF after the first line.

The SDK does this through a table: `pdm_init` fills `pdm_table`, 32,768 `uint16_t` (64 KB) in
the program's `.bss`, index `(acc << 7) | v7`, entry `(new acc << 8) | pattern`. Each entry is
the eight steps run once; the 68000 does a step in two instructions, `add.b` (which leaves
the carry out in the X flag) and `addx.b p,p` (p = 2p + X), 8 clocks a step, so the whole
table takes about 0.4 s (24 frames, *measured* by `abench`). `pdm_encode` keeps the
accumulator shifted left by 8, so each sample costs an OR, a shift, a table load, a byte
store and a mask: about 83 clocks (*measured*; `pdm.c` is compiled with `-O2`).
`pdm_reset()` zeroes the accumulator; a new sound does not need it.

To use the same encoding in your own code:

* `pdm_table` is declared in `pdm.h`, so after `pdm_init` a per-line callback or your own
  vector can encode on the fly, and since a word write of the entry to `IO_AUDIO` delivers
  the pattern in the low byte, each sample is

  ```c
  uint16_t e = pdm_table[(acc << 7) | (s >> 1)];  /* acc 0..255, s 0..255 */
  IO_AUDIO = e;                                   /* pattern in the low byte */
  acc = e >> 8;
  ```

* `pdm_line` in `lean.c` ([2.7](#three-ways-to-use-an-interrupt)) is the algorithm in plain C
  without a table. At `-Os` it takes about 900 clocks per sample (*measured*): fine for
  preparing sounds in advance, too slow for streaming.
* To save the 64 KB of RAM, program the table once into the free flash at 0xE20000
  ([2.12](#212-the-flash-rom)), where reads cost 4 clocks with no slot penalty, and index it
  there in the same way. The BIOS no longer has a copy of its own.

### The analogue chain and what to expect

The pin is a 5 V CMOS output of CPLD2. The analogue chain (`docs/architecture.md` section 5,
`kicad/board.py`) is, on board rev C1:

```
AUDIO pin ---[1k]---+---[10k]---+---[10 uF]---> 3.5 mm jack, tip and ring (mono on both)
(shifter bit 7)     |           |
                 [33 nF]      [2.2k]
                    |           |
                   GND         GND
```

The 1 kΩ/33 nF low-pass has its corner at about 4.8 kHz, the divider scales the 5 V swing to
about 0.8 V peak to peak, and the capacitor removes the DC. The source impedance is about
1.8 kΩ: feed it to an amplifier or a line input rather than headphones. Rev B4 boards were
built with 10 nF (16 kHz corner), which passes most of the modulator's hiss; changing that
one 0603 capacitor to 33 nF is the whole difference. In the emulator's reconstruction of
the `tune` example through each filter (*measured*, `--audio-edge-log` and a model of the
chain), the power above 8 kHz relative to the music band falls from -14 dB with 10 nF to
-20.5 dB with 33 nF, and the 2-8 kHz band from -10.7 dB to -12.8 dB; the notes themselves
are unaffected (a 4.8 kHz first-order corner is about -1 dB at 2.5 kHz).

* Nothing above about 7.8 kHz (half the 15.7 kHz sample rate) can be reproduced.
* Samples have 7 bits. The sigma-delta modulator pushes its quantisation noise up in
  frequency, but with only eight bits per sample it cannot push it far: the parts at 31 and
  63 kHz are filtered out, and the 4.8 kHz corner of rev C1 takes 5-10 dB off the noise
  between 8 and 16 kHz and off the images of the signal mirrored about the line rate (a
  1 kHz tone has a faint partner at 14.7 kHz). **Expect a little hiss**, more on a rev B4
  board with its 16 kHz corner; an external low-pass at 7-8 kHz, or an amplifier with
  little treble, removes most of what is left.
* First-order sigma-delta has idle tones: a constant sample whose 7-bit value is not a
  multiple of 16 (130, say) makes the accumulator cycle with a period of 2 to 16 lines, a
  faint tone between about 1 and 7.8 kHz, about the size of one 7-bit step. Silence (128, a
  steady 0x55 or 0xAA) has none, and because the stream's idle pattern is the same level,
  starting and stopping sound does not click.
* Square-wave tones and chip music are clean, speech and effects good, sampled music
  acceptable with a little hiss.

**Other sample rates.** The 68681's counter/timer can interrupt at another rate on level 3
([2.11](#211-serial-the-68681-duart)); since a write to AUDIO takes effect at once, patterns
written at another rate still set the level, but the bit order within a line is then
unrelated to the write, so the line rate is the natural one.

## 2.9 Keyboard

### The receiver

CPLD2 receives PS/2 frames in hardware: it synchronises PS2_CLK to 24 MHz and on each falling
edge shifts in start bit (0), 8 data bits LSB first, odd parity and stop (1). At the stop bit it
loads `IO_PS2DATA`, sets `INTSTAT_PS2RDY` and sets `INTSTAT_PS2ERR` if the parity or stop
bit was wrong (cleared otherwise). If the clock stops for about three HSYNC periods (~190 us)
in the middle of a frame, the receiver abandons it, so a glitch cannot leave it out of step.

There is a single holding register and no overrun flag: if a second frame completes before
PS2DATA is read, the first byte is lost. A frame takes about 0.7-1.1 ms (11 clocks at
10-16.7 kHz), so a byte must be read within roughly a millisecond. Reading PS2DATA clears
PS2RDY but leaves the byte in the register until the next frame, so a level-2 hook can read
`IO_PS2DATA` again to see the raw byte the BIOS just decoded.

### The BIOS decoder

The level-2 handler drops bytes with PS2ERR (counting them; `info` shows the count) and feeds
the rest to a scan-code-set-2 state machine: `E0` marks an extended key, `F0` a release, the
8-byte Pause sequence (`E1 ...`) is skipped, and `AA` (self-test passed), `FA` (ACK), `FE`
(resend) and `EE` (echo) are ignored. Every key produces a raw event (`sys_key_event`); presses
of printable and control keys also produce ASCII or an escape sequence (Part 1, 1.4) with
Shift, Caps Lock and Ctrl applied; Alt is tracked but not used. The BIOS never sends anything to
the keyboard: it relies on the keyboard's power-on defaults (scan code set 2, typematic on), and
the keyboard's LEDs stay off.

### Owning the keyboard

* Read events with `sys_key_event` (cheapest).
* Or replace vector 26 and read `IO_PS2DATA` yourself; the BIOS queues then stay empty
  (`sys_getchar` and stdin wait for ever: the console has no other input).
* Or turn `INTEN_PS2` off and poll `INTSTAT_PS2RDY`.

### Talking to the keyboard

Host-to-device transfers are bit-banged on the open-drain `GPIO_PS2CLK`/`GPIO_PS2DAT` drives,
reading the lines back through `INTSTAT_PS2CLK`/`INTSTAT_PS2DAT`. The protocol: hold clock
low for at least 100 us, pull data low (start bit), release clock; the keyboard then generates
11 clocks: put each data bit (LSB first), the odd parity bit and a 1 stop bit on data while
clock is low; the keyboard samples on the rising edge and finally pulls data low for one clock
as an acknowledgement, then answers `FA` through the normal receive path. While this happens the
hardware receiver sees the keyboard's clock too and assembles a garbage byte, so disable the
level-2 interrupt during the transfer and discard PS2DATA afterwards.

```c
/* ps2_send: one host-to-device byte. NOT TESTED: the emulator does not model this
 * direction and the board has not run it yet. Returns 0 if the keyboard acknowledged. */
static uint16_t gpio_idle = GPIO_PS2CLK | GPIO_PS2DAT;   /* add the LED bits you want */

static int ps2_wait_clk(uint16_t level)         /* level: 0 or INTSTAT_PS2CLK */
{
    for (uint32_t n = 0; n < 50000; n++)        /* ~50 ms */
        if ((IO_INTSTAT & INTSTAT_PS2CLK) == level) return 0;
    return -1;
}

int ps2_send(uint8_t b)
{
    void *hook = sys_set_irq_handler(2, NULL);  /* (re)writes the BIOS's INTEN value */
    IO_INTEN = INTEN_VBLANK | INTEN_DUART;      /* keep the receiver's garbage away from the BIOS */
    IO_GPIO = gpio_idle & ~GPIO_PS2CLK;         /* 1. inhibit: clock low >= 100 us */
    for (volatile int i = 0; i < 300; i++) ;
    IO_GPIO = gpio_idle & ~(GPIO_PS2CLK | GPIO_PS2DAT);  /* 2. data low (start bit) */
    IO_GPIO = gpio_idle & ~GPIO_PS2DAT;         /* 3. release clock: request to send */
    unsigned parity = 1;
    int rc = 0;
    for (int i = 0; i < 10 && rc == 0; i++) {   /* 8 data bits LSB first, parity, stop */
        unsigned bit = i < 8 ? (b >> i) & 1u : i == 8 ? parity : 1u;
        if (i < 8) parity ^= bit;
        rc = ps2_wait_clk(0);                   /* the keyboard pulls clock low ... */
        IO_GPIO = bit ? gpio_idle : (gpio_idle & ~GPIO_PS2DAT);   /* ... we set the bit */
        if (rc == 0) rc = ps2_wait_clk(INTSTAT_PS2CLK);           /* it samples on the rise */
    }
    if (rc == 0) rc = ps2_wait_clk(0);          /* acknowledge: data low with this clock */
    int ack = rc == 0 && !(IO_INTSTAT & INTSTAT_PS2DAT);
    if (rc == 0) rc = ps2_wait_clk(INTSTAT_PS2CLK);
    IO_GPIO = gpio_idle;
    (void)IO_PS2DATA;                           /* drop whatever the receiver assembled */
    IO_INTSTAT = INTSTAT_PS2ERR;
    sys_set_irq_handler(2, hook);               /* BIOS INTEN back, hook kept */
    return (rc == 0 && ack) ? 0 : -1;
}
```

(The INTEN value written above assumes no audio callback or level-4 hook is active; add
`INTEN_HSYNC` if one is.) The keyboard's `FA` arrives through the BIOS, which ignores it; wait
a few milliseconds before the next byte. Useful commands: `ED` then a byte with bit 0 Scroll
Lock, bit 1 Num Lock, bit 2 Caps Lock to set the LEDs; `F3` then a byte to set the typematic
rate and delay; `FF` to reset (the keyboard answers `FA`, then `AA`).

## 2.10 SD card

### Wiring

Since spec revision 7 (board rev C1) the card is on its **native SD bus, 4 bits wide**, not in
SPI mode. All six lines, CLK, CMD and DAT0..3, run straight from CPLD2 to the microSD socket,
each with a 10 k pull-up to the card's own 3.3 V regulator. CPLD2 drives them open drain: a 0
in `SDIO` pulls the line low, a 1 releases it and the pull-up (or the card) sets the level, so
the 5 V CPLD never drives the 3.3 V card high and no level dividers are needed. Reading `SDIO`
returns all six levels in the same bit positions ([2.6](#26-the-io-registers)). There is no
card-detect or write-protect switch: "no card" shows up as no answer to CMD55 during
initialisation. Rev B4 boards are wired for SPI mode (dividers on CS/CLK/MOSI, MISO in
`INTSTAT`); none of this section applies to them.

### Bit banging

The card samples CMD and DAT on the rising edge of CLK and changes its own outputs after the
falling edge. One clock is therefore three register accesses: write the drive word with CLK
low (CMD/DAT bits as you want them, 1 for every line you are not driving), read `SDIO` (the
card's bits are now valid), write the same word with CLK high.

```c
#define LO (IO_SDIO_RESET & ~SDIO_CLK)          /* 0x3D: every line released, CLK low */

static unsigned clk(unsigned drive)             /* one clock; returns the levels seen with CLK low */
{
    IO_SDIO = drive;                            /* falling edge: the card changes CMD/DAT */
    unsigned v = IO_SDIO;                       /* sample */
    IO_SDIO = drive | SDIO_CLK;                 /* rising edge: the card samples CMD/DAT */
    return v;
}
```

Everything released is 0x3F, the reset value; the bus idles with CLK high. To send, clear the
bits of the lines that carry a 0 (`LO & ~SDIO_CMD` puts a 0 on CMD); to receive, write `LO` so
every data line is released and read the card's bits. Because the register reads back levels,
the drive words can be precomputed and the read value used directly as a table index: bits
15..6 read 0, so `IO_SDIO` is an index 0..63.

The card does not care how slowly or unevenly it is clocked (the SD bus is static: the host
may stop the clock at any point), so interrupts can stay enabled and a transfer can be
paused for as long as you like. It does care about the maximum: 400 kHz until the card has an
address (identification), 25 MHz after that. The second is out of the 68000's reach; the
first is not quite: the BIOS's tightest loop, the 4-bit read, clocks the card at about
280 kHz and plain C as above at about 80 kHz, but an unrolled idle-clock loop of two `move.w`
per clock (16 CPU clocks) would run at 750 kHz, so do not use one before CMD3.

On the board a released line rises through its 10 k pull-up instead of being driven, CLK
included. With a few tens of pF on a line (an estimate, not a measurement) that is a few
hundred nanoseconds, less than the 8 clocks (667 ns) that separate two `move.w` accesses to
`SDIO`, which is what makes the open-drain scheme workable at this speed. The emulator does
not model edge rates.

### The protocol

What follows is the subset of the SD Physical Layer Simplified Specification (chapter 4 and
the timing diagrams in 4.12) that the BIOS uses. All of it is MSB first.

**Commands** are 48-bit frames on CMD, driven by the host:

| bits | content |
|---|---|
| 47 | start bit, 0 |
| 46 | transmission bit, 1 = host to card |
| 45..40 | command index |
| 39..8 | argument |
| 7..1 | CRC7 of bits 47..8: polynomial x^7 + x^3 + 1, initial value 0 |
| 0 | end bit, 1 |

In native mode the card always checks the CRC (SPI mode can switch it off, the native bus
cannot); a frame with a bad CRC gets no response. Two frames to remember: CMD0 is
`40 00 00 00 00 95`, CMD8 with argument 0x1AA is `48 00 00 01 AA 87`. A C version of the CRC,
returning the last byte as sent:

```c
static uint8_t crc7(const uint8_t *p, int n)    /* x^7 + x^3 + 1, as sent: (crc << 1) | 1 */
{
    uint8_t c = 0;
    while (n--) {
        c ^= *p++;
        for (int b = 0; b < 8; b++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x12) : (uint8_t)(c << 1);
    }
    return c | 1;
}
```

**Responses** come back on CMD, driven by the card: start bit 0, transmission bit 0, then

| type | length | after the two leading bits | used for |
|---|---|---|---|
| R1 | 48 | index (6), card status (32), CRC7, end bit | most commands |
| R2 | 136 | `111111`, then the 128-bit CID or CSD, whose last byte is its own CRC7 and end bit | CMD2 (CID), CMD9 (CSD) |
| R3 | 48 | `111111`, OCR (32), `1111111` instead of a CRC, end bit | ACMD41 |
| R6 | 48 | index 3, new RCA (16), condensed status (16), CRC7, end bit | CMD3 |
| R7 | 48 | index 8, reserved, voltage accepted (4) and the echoed check pattern (8) in the low 12 bits, CRC7, end bit | CMD8 |

The response's start bit comes Ncr = 2 to 64 clocks after the command's end bit; between a
response and the next command the host must give at least 8 clocks (Nrc), and as many between
two commands when the first has no response (Ncc). CMD0 has no response at all.

The **card status** in R1 has the error bits OUT_OF_RANGE (31), ADDRESS_ERROR (30),
BLOCK_LEN_ERROR (29), COM_CRC_ERROR (23), ILLEGAL_COMMAND (22), CARD_ECC_FAILED (21), CC_ERROR
(20) and ERROR (19), the CURRENT_STATE field in bits 12..9 (0 idle, 1 ready, 2 ident, 3 stby, 4
tran, 5 data, 6 rcv, 7 prg, 8 dis), READY_FOR_DATA (8) and APP_CMD (5, set when the card has
accepted CMD55 and takes the next command as an application command). A command with a bad CRC
or one that is illegal in the current state is not answered; the error shows in the status of
the next response.

**Initialisation** walks the card from idle to transfer state:

| step | command | response | what for |
|---|---|---|---|
| 1 | at least 74 clocks with everything released | | power-up |
| 2 | CMD0, argument 0 | none | reset to idle state |
| 3 | CMD8, argument 0x1AA | R7, echoes 0x1AA | 2.7-3.6 V accepted; only v2 cards (SDHC/SDXC and newer SDSC) answer |
| 4 | CMD55 (RCA 0), then ACMD41 with 0x00FF8000, plus HCS (bit 30) for a v2 card | R1, then R3 | repeat until OCR bit 31 (power-up done) is set; allow 1 s. OCR bit 30 (CCS) = 1: SDHC/SDXC, block addressed |
| 5 | CMD2 | R2: CID | identification |
| 6 | CMD3 | R6: RCA | the card's relative address, used in the argument (bits 31..16) of CMD7, CMD9, CMD13 and CMD55 from now on; identification ends, full clock speed allowed |
| 7 | CMD9 with RCA | R2: CSD | capacity (CSD version 2: (C_SIZE + 1) x 512 KB) |
| 8 | CMD7 with RCA | R1 | select: stand-by to transfer state |
| 9 | CMD55 with RCA, then ACMD6, argument 2 | R1, R1 | 4-bit data bus (argument 0 = back to 1 bit) |
| 10 | CMD16, argument 512 (byte-addressed cards only) | R1 | block length |

**Data blocks** run on DAT3..0. A read block (CMD17, address = block number on SDHC/SDXC, byte
offset on SDSC) starts Nac clocks after the response with a start bit, all four lines low for
one clock, then 1024 nibbles, byte 0's high nibble first, DAT3 carrying bit 3 of each nibble
and DAT0 bit 0. Then come 16 clocks of CRC: each line carries its own CRC16-CCITT
(polynomial x^16 + x^12 + x^5 + 1 = 0x1021, initial value 0) over the 1024 bits that line
carried, MSB first, and finally an end bit (all lines high). Nac is a read access time: up to
100 ms on an SDHC card. Before ACMD6 a block is 4096 bits on DAT0 alone with one CRC16.

A write (CMD24) is the same block sent by the host, starting at least Nwr = 2 clocks after the
response's end bit. The card then answers on DAT0 with a **CRC status token**: start bit 0, three
bits (`010` accepted, `101` CRC error, `110` write error), end bit 1. After an accepted block it
holds DAT0 low (busy) while it programs, which on SDHC may legitimately take up to 250 ms; keep
clocking and wait for DAT0 to go high before the next command. A block with a wrong CRC is
discarded; there is no way to switch the write CRC off.

### The BIOS's driver (`fw/sd4.S`, `fw/sd.c`)

`fw/sd4.S` does the bit banging: `sd4_cmd` (frame out, up to 128 clocks of Ncr wait, 48 or
136 response bits in), `sd4_clocks`, `sd4_read512`, `sd4_write512` and `sd_crc16x4` (the four
CRC16s of a block). `fw/sd.c` does the framing, the checks and the waits:

* `sd_init`: the table above (80 clocks; CMD8 silence means a v1 card; up to about 1 s of
  ACMD41; a card that never answers CMD55 is "no card"). Until CMD3 it inserts a delay loop
  around every edge (`sd_slow`), which slows identification to about 25 kHz; data phases
  always run at full speed. CMD9's CSD gives the capacity for both CSD versions.
* Every command carries a correct CRC7, and the CRC7 and the index of every R1, R2, R6 and R7
  are checked (`SD_ERR_CRC`); R1's error bits are checked too (out of range and address errors
  are `SD_ERR_RANGE`, COM_CRC_ERROR `SD_ERR_CRC`, the rest `SD_ERR_CMD`).
* Read: CMD17, wait up to about 200 ms for the start bit on DAT0, 1024 nibbles through two
  64-byte tables (`SDIO` value to high nibble, to low nibble), CRC16s clocked past but not
  checked, 8 idle clocks.
* Write: CMD24, the four CRC16s computed first in assembler (6.9 ms), 2 clocks, start bit,
  data, CRCs, end bit; the CRC status token (anything but `010` is `SD_ERR_TOKEN`) and the busy
  wait (up to about 400 ms).
* Single-block commands only; byte-addressed (SDSC) cards take `lba * 512` and are limited to
  2 GB. Interrupts stay enabled throughout, and the driver also works with them masked (it
  polls). The monitor's `sd` command re-initialises the card and prints the type, bus width,
  RCA, capacity, CID and CSD.

What it costs, by the 68000 timing tables and *measured* in the emulator:

| operation | clocks | time |
|---|---|---|
| 4-bit read loop, per byte (3 accesses per nibble, two table lookups, a store) | 86.5 | 7.2 us |
| 512-byte block, data phase only | 44,300 | 3.7 ms |
| a command with a 48-bit response (frame, Ncr, response) | about 9,000 | 0.75 ms |
| `sys_sd_read_block`, *measured* | about 60,600 | 5.05 ms (101 KB/s) |
| write loop, per byte | 112 | 9.3 us |
| CRC16s of a block (`sd_crc16x4`) | 83,000 | 6.9 ms |
| `sys_sd_write_block`, *measured* (emulated card busy 16 clocks) | about 160,000 | 13.3 ms, plus the card's own programming time |

The SPI driver of rev B4 needed about 17 ms per block read. At 5 ms a block the card keeps up
with the audio stream at the full line rate: `sdk/examples/sdplay.c` streams 8-bit PCM from a
file, encodes it on the fly and still spends 57 % of its time waiting in `sys_audio_next`.
On the board the I/O alignment penalty ([2.3](#23-the-bus-and-video-time-slicing)) makes the
loops somewhat slower.

### Writing your own driver

A program can talk to the card itself, from user mode, with or without the BIOS's driver
having initialised it first. The C below is a complete minimal driver (it reuses `clk`, `LO`
and `crc7` from above): 4-bit initialisation, block read and block write, with the error
checks a real card needs and the CRC16s computed bit-serially. *Measured* in the emulator,
built with the SDK's `-Os`: initialisation within one frame (the emulated card is ready on the
second ACMD41), 32 block reads in 27 frames (14 ms a block, a third of the BIOS's speed),
8 block writes in 48 frames (100 ms a block, most of it the bit-serial CRC), and the BIOS's
`sys_sd_read_block` keeps working on the card afterwards; the same with `--sd-sdsc`.

```c
/* sdmin.c: a minimal native-bus SD driver in plain C: 4-bit init, block read, block write. */
#include <stdint.h>
#include "hw.h"
/* LO, clk() and crc7() as above */

/* 8 idle clocks (Ncc/Nrc), the 48-bit frame, then a response of nbits (48 or 136; 0 = none)
 * into r, MSB first, start bit in bit 7 of r[0]. 0, or -1 if the card did not answer. */
static int cmd(uint8_t idx, uint32_t arg, uint8_t *r, int nbits)
{
    uint8_t f[6] = { 0x40 | idx, arg >> 24, arg >> 16, arg >> 8, arg };
    f[5] = crc7(f, 5);
    for (int i = 0; i < 8; i++) clk(LO);
    for (int i = 0; i < 48; i++)
        clk((f[i >> 3] << (i & 7)) & 0x80 ? LO : LO & ~SDIO_CMD);
    if (!nbits) return 0;
    for (int n = 0; clk(LO) & SDIO_CMD; )      /* start bit: Ncr is 2..64 clocks */
        if (++n == 128) return -1;
    for (int i = 0; i < nbits / 8; i++) r[i] = 0;
    for (int i = 1; i < nbits; i++)
        r[i >> 3] = (uint8_t)(r[i >> 3] << 1 | (clk(LO) >> SDIO_CMD_BIT & 1));
    return 0;
}

#define WORD(r) ((uint32_t)(r)[1] << 24 | (uint32_t)(r)[2] << 16 | (r)[3] << 8 | (r)[4])
#define R1_ERRORS 0xFDF98008u                   /* card status error bits */

static uint16_t rca;
static int block_addr;                          /* 1: SDHC/SDXC, addresses are block numbers */

int sdmin_init(void)
{
    uint8_t r[17];
    for (int i = 0; i < 80; i++) clk(LO);      /* >= 74 clocks after power-up */
    cmd(0, 0, r, 0);                            /* GO_IDLE_STATE */
    int v2 = cmd(8, 0x1AA, r, 48) == 0;         /* R7 echoes 0x1AA; v1 cards stay silent */
    for (int n = 0; ; n++) {                    /* ACMD41 until power-up is done (< 1 s) */
        if (n == 10000 || cmd(55, 0, r, 48) || cmd(41, (v2 ? 0x40000000u : 0) | 0xFF8000, r, 48))
            return -1;
        if (r[1] & 0x80) break;                 /* OCR bit 31 */
    }
    block_addr = r[1] >> 6 & 1;                 /* OCR bit 30, CCS */
    if (cmd(2, 0, r, 136) || cmd(3, 0, r, 48)) return -1;      /* CID, then R6: the RCA */
    rca = (uint16_t)(r[1] << 8 | r[2]);
    if (cmd(7, (uint32_t)rca << 16, r, 48) || (WORD(r) & R1_ERRORS)) return -1;   /* select */
    if (cmd(55, (uint32_t)rca << 16, r, 48) || cmd(6, 2, r, 48) || (WORD(r) & R1_ERRORS))
        return -1;                              /* ACMD6 2: 4-bit bus */
    if (!block_addr && (cmd(16, 512, r, 48) || (WORD(r) & R1_ERRORS))) return -1;
    return 0;
}

#define NIB(v) (((v) & SDIO_DAT3 ? 8 : 0) | ((v) & SDIO_DAT2 ? 4 : 0) | \
                ((v) & SDIO_DAT1 ? 2 : 0) | ((v) & SDIO_DAT0 ? 1 : 0))

int sdmin_read(uint32_t lba, uint8_t *buf)
{
    uint8_t r[6];
    if (cmd(17, block_addr ? lba : lba * 512, r, 48) || (WORD(r) & R1_ERRORS)) return -1;
    for (long n = 0; clk(LO) & SDIO_DAT0; )    /* start bit: all four lines low */
        if (++n == 100000) return -1;
    for (int i = 0; i < 512; i++) {
        unsigned hi = clk(LO), lo = clk(LO);   /* high nibble first */
        buf[i] = (uint8_t)(NIB(hi) << 4 | NIB(lo));
    }
    for (int i = 0; i < 17; i++) clk(LO);      /* 16 CRC16 clocks (not checked), end bit */
    return 0;
}

static unsigned drive(unsigned nib)             /* CLK-low drive word putting nib on DAT3..0 */
{
    return LO & ~(nib & 8 ? 0 : SDIO_DAT3) & ~(nib & 4 ? 0 : SDIO_DAT2)
              & ~(nib & 2 ? 0 : SDIO_DAT1) & ~(nib & 1 ? 0 : SDIO_DAT0);
}

int sdmin_write(uint32_t lba, const uint8_t *buf)
{
    uint8_t r[6];
    uint16_t crc[4] = { 0, 0, 0, 0 };           /* CRC16-CCITT per line, polynomial 0x1021, init 0 */
    if (cmd(24, block_addr ? lba : lba * 512, r, 48) || (WORD(r) & R1_ERRORS)) return -1;
    clk(LO); clk(LO);                           /* Nwr >= 2 */
    clk(drive(0));                              /* start bit on all four lines */
    for (int i = 0; i < 1024; i++) {
        unsigned nib = i & 1 ? buf[i >> 1] & 15 : buf[i >> 1] >> 4;
        for (int l = 0; l < 4; l++)
            crc[l] = (uint16_t)(crc[l] << 1) ^ (((crc[l] >> 15) ^ (nib >> l)) & 1 ? 0x1021 : 0);
        clk(drive(nib));
    }
    for (int b = 15; b >= 0; b--)               /* the four CRCs side by side, MSB first */
        clk(drive((crc[3] >> b & 1) << 3 | (crc[2] >> b & 1) << 2 | (crc[1] >> b & 1) << 1 | (crc[0] >> b & 1)));
    clk(LO);                                    /* end bit: released = 1 */
    for (int n = 0; clk(LO) & SDIO_DAT0; )     /* CRC status token on DAT0: start bit ... */
        if (++n == 1000) return -1;
    unsigned tok = 0;
    for (int i = 0; i < 3; i++) tok = tok << 1 | (clk(LO) >> SDIO_DAT0_BIT & 1);
    clk(LO);                                    /* ... 3 status bits, end bit */
    if (tok != 2) return -1;                    /* 010 accepted, 101 CRC error, 110 write error */
    for (long n = 0; !(clk(LO) & SDIO_DAT0); ) /* busy (DAT0 low) while the card programs */
        if (++n == 200000) return -1;
    return 0;
}
```

To go faster, do what `sd4.S` does: keep `IO_SDIO_ADDR` and the two drive words (`LO`, 0x3F)
in registers, unroll, turn the levels into nibbles with a 64-byte table indexed by the raw
`SDIO` value (one for the high nibble, pre-shifted, one for the low), and compute the write
CRCs a byte at a time with tables (`sd_crc16x4` spreads each data byte into four lanes, one
per line, and runs a 256-entry CRC16 table on each). Multi-block transfers (CMD18 read,
CMD25 write, both ended with CMD12) save a command and an access time per block; the BIOS does
not use them, the emulator's card supports them.

Real cards differ from the emulator's in ways a driver must allow for (the BIOS's limits in
brackets): the response start can come anywhere from 2 to 64 clocks after a command (128), the
read start bit up to 100 ms after the response (about 200 ms), the busy time after a write up
to 250 ms (about 400 ms); ACMD41 may need many polls over up to 1 s; the RCA is the card's
choice; v1 and byte-addressed cards exist. Identification must stay under 400 kHz. A driver
tested only against the emulator, which answers everything after a fixed handful of clocks,
is most likely to fail on a real card by not waiting long enough. (The BIOS's driver itself
has so far run only in the emulator and against its card model in the host tests.)

### The emulator's card

`emu/sdcard.c` models a card on the native bus, clocked by the writes to `SDIO.CLK`: it samples
CMD and DAT on the rising edge and changes its outputs after the falling edge, and every line
reads as the AND of both sides. By default it is an SDHC card the size of the `--sd` image
(block addressed, CSD version 2); `--sd-sdsc` makes it a byte-addressed SDSC card with a
version 1 CSD. It checks the CRC7 of every command (a bad one gets no response and
COM_CRC_ERROR in the next status; an unknown command, or one illegal in the current state,
gets no response and ILLEGAL_COMMAND), keeps the card state machine and status bits, and
supports CMD0, 2, 3, 7, 8, 9, 10, 12, 13, 16 (512 only), 17, 18, 24, 25, 55, ACMD6 (1 or 4
bits), ACMD41 (an SDHC card insists on CMD8 first and HCS set) and ACMD51. Its timing is fixed:
Ncr 8 clocks, Nac 8 clocks, the CRC status token 2 clocks after a written block's end bit,
then 16 clocks of busy; ACMD41 reports ready on the second poll; the RCA is always 0x1234. It
verifies the four CRC16s of every written block: only a block with all four right (token
`010`) reaches the image, anything else gets `101`. `--sd-trace` logs every command, state
change and block transfer to stderr, which is the quickest way to see why a driver of your own
gets no answer.

### The filesystem

FatFs R0.15 (`fw/fatfs/`) with `FF_USE_LFN 1` and `FF_MAX_LFN 63` (long names in a static
buffer), `FF_FS_RPATH 2` (current directories, `f_getcwd`), two volumes with the names
`"C"` and `"D"` (`FF_STR_VOLUME_ID 1`; physical drive 0 is the flash, `fw/flashdisk.c`, drive
1 the card), `FF_FS_TINY` (one 512-byte sector window per volume), code page 437, timestamps
from the BIOS clock (`get_fattime` in `fw/clock.c`), `f_mkfs` for `format C:`, no file
locking. Both volumes are mounted on first use; a card error drops D:'s mount and closes the
files open on it, so the next call re-initialises the card. 8 files (descriptors 3..10) and 4
directory handles, shared by both drives. FAT32 needs a volume of at least 65,525 clusters
(64 MB with `mkimage.sh`); FAT12/16 cards work too. C:'s side is in [2.12](#212-the-flash-rom).

### Coexisting with the BIOS

The BIOS keeps no copy of `SDIO` (it writes whole precomputed words), but it does remember the
card: that it is initialised, block or byte addressed, on the 4-bit bus, in transfer state,
with 512-byte blocks. If you drive the card yourself, finish every transaction (data block,
CRC token, busy) and leave `SDIO` at 0x3F, everything released. If you reinitialise the card,
bring it back to the same state (CMD7, ACMD6 2, CMD16 512 on SDSC) and the BIOS carries on
unaware, as the example above shows; the RCA may change, the BIOS uses it only during
initialisation. If you leave the card in another state (idle after a CMD0, 1-bit bus), the
BIOS's calls fail with `E_IO`: a failing file call drops the mount and marks the card
uninitialised, so the call after it starts over, but a failing raw block call does not, and
`sys_sd_read_block` keeps failing until a file call fails or the monitor's `sd` command runs.
FatFs caches a sector (and
the FAT's location) in memory: raw block writes into the FAT volume while files are open, or
between file calls, can be overwritten by that cache or make it stale. Use raw blocks only for
areas the filesystem does not own (a separate partition, or a card used without a filesystem).
Raw `lba` values are absolute card addresses: on a partitioned card, block 0 is the MBR.

## 2.11 Serial: the 68681 DUART

### Registers

The 68681 sits on the CPU's low data byte at `DUART_BASE` 0xF20000, register select from
A1..A4, so its 16 registers are at the odd addresses 0xF20001-0xF2001F (repeating every 32
bytes). Byte access at odd addresses only. Read and write names differ at some addresses.

| address | read | write |
|---|---|---|
| 0xF20001 | `DUART_MRA` (MR1A, then MR2A) | `DUART_MRA` (MR1A, then MR2A) |
| 0xF20003 | `DUART_SRA` status A | `DUART_CSRA` clock select A |
| 0xF20005 | **do not read** | `DUART_CRA` command A |
| 0xF20007 | `DUART_RBA` receive A (pops the FIFO) | `DUART_TBA` transmit A |
| 0xF20009 | `DUART_IPCR` input port change | `DUART_ACR` auxiliary control |
| 0xF2000B | `DUART_ISR` interrupt status | `DUART_IMR` interrupt mask |
| 0xF2000D | `DUART_CUR` counter upper | `DUART_CTUR` counter/timer upper preset |
| 0xF2000F | `DUART_CLR` counter lower | `DUART_CTLR` counter/timer lower preset |
| 0xF20011 | `DUART_MRB` | `DUART_MRB` |
| 0xF20013 | `DUART_SRB` status B | `DUART_CSRB` clock select B |
| 0xF20015 | **do not read** | `DUART_CRB` command B |
| 0xF20017 | `DUART_RBB` receive B | `DUART_TBB` transmit B |
| 0xF20019 | `DUART_IVR` (unused) | `DUART_IVR` |
| 0xF2001B | `DUART_IP` input port | `DUART_OPCR` output port configuration |
| 0xF2001D | `DUART_START_CTR` (read starts the counter/timer) | `DUART_OPR_SET` |
| 0xF2001F | `DUART_STOP_CTR` (read stops it / clears CTR) | `DUART_OPR_RESET` |

Status register bits (`SRA_*`, `SRB_*`): 0 RXRDY, 1 FFULL (3-byte FIFO full), 2 TXRDY,
3 TXEMT, 4 OE (overrun), 5 PE, 6 FE, 7 RB (break). ISR and IMR bits (`ISR_*`, `IMR_*`):
0 TXRDYA, 1 RXRDYA, 2 DBA, 3 CTR (counter/timer ready), 4 TXRDYB, 5 RXRDYB, 6 DBB, 7 IPC.
Command register: bits 1..0 receiver (01 enable, 10 disable), 3..2 transmitter, 7..4 misc:
`DUART_CR_RESET_MR` 0x10, `DUART_CR_RESET_RX` 0x20, `DUART_CR_RESET_TX` 0x30,
`DUART_CR_RESET_ERR` 0x40, `DUART_CR_RESET_BRK` 0x50; `DUART_CR_ENABLE` 0x05 enables both.

### How the BIOS sets up channel A

Before the POST (`duart_reset_init` in `fw/duart.S`): IMR = 0; CRA = reset MR pointer, reset
RX, reset TX, reset error status; ACR = `SERIAL_ACR_VALUE` 0x00 (baud-rate set 1, counter/timer
unused, no input-port interrupts); MR1A = `SERIAL_MR1_VALUE` 0x13 (8 bits, no parity, RxRDY
interrupt select); MR2A = `SERIAL_MR2_VALUE` 0x07 (1 stop bit, normal mode, no CTS/RTS);
CSRA = `SERIAL_CSR_VALUE` 0xCC (38,400 receive and transmit); CRA = 0x05. After the POST it
drains the receiver, resets error status and sets IMR = `IMR_RXRDYA`, and INTEN.DUART routes the
IRQ to level 3. Channel B and the counter/timer are left at their hardware-reset state
(disabled), and so is the output port (OPR 0, every OP pin high, which keeps the paddle
capacitors shorted). The BIOS writes ACR and IMR again only to start and stop paddle
measuring ([below](#the-input-and-output-ports)).

The POST report and the banner go out on channel A (and on the screen); then the BIOS releases
the port (spec `serial.console`, `console_serial` in `fw/main.c`). From the banner on, console
output goes to the screen only and console input comes from the keyboard only. The BIOS
writes channel A again only when a program asks (`sys_serial_putc`, a write to `/dev/ttya`),
and takes bytes out of the receive ring only for a program (`sys_serial_getc`, a read of
`/dev/ttya`) or for the monitor's `srec` command, which reads the records from the port itself
while it runs. The receive interrupt stays on the whole time. (The `flash` command's RAM
routine also prints its progress on the port, [2.12](#the-monitor-commands).) Nothing puts the
console back on the serial port: no system call, no monitor command.

38,400 baud is the top of the 68681's fixed table: CSR code 0xC is 38.4k in set 1 (ACR bit 7
= 0); other set-1 codes include 0xB = 9600 and 0x8 = 2400. In set 2 (ACR bit 7 = 1) code 0xC
means 19.2k, so **keep ACR bit 7 at 0** whenever you write ACR (it is write-only; the BIOS's
value is 0x00).

**Transmit** is polled: `serial_putc` waits for `SRA_TXRDY` and writes TBA, with interrupts
enabled. `sys_serial_putc` and writes to `/dev/ttya` send raw bytes this way, 260 us each at
38400 baud; only the POST and banner output adds `\r` before `\n`. You can write TBA yourself
(poll TXRDY first).

**Receive** is interrupt driven: the level-3 handler moves bytes from the 3-byte FIFO into a
512-byte ring, which `sys_serial_getc` and reads of `/dev/ttya` drain (`sys_getchar` does not
look at it). The ring holds 511 bytes; while it is full, further bytes are dropped without
being counted (only `SRA.OE`, a FIFO overrun, is counted, and `info` shows it). It is not
emptied between programs, so a program can find bytes that arrived before it started. Polling
SRA/RBA yourself races with that handler; to own the receiver, write IMR without
`IMR_RXRDYA` and put `IMR_RXRDYA` back before exiting (IMR is write-only and `sys_exit` does
not restore it).

### Channel B

Channel B's TXDB and RXDB are on the serial B header J11 (board rev C2; the FTDI pinout GND 1,
TXD 4, RXD 5, 5 V TTL levels; rev C1 had them on the expansion header J8, pins 13 and 15).
Since board rev C3 (spec rev 12) J11 also powers its device: pin 3 carries 5 V through a 200 mA
polyfuse and a Schottky diode (so a 5 V FTDI cable cannot back-feed the board), and pin 6 (RTS) is
the DUART's OP1 pin, meant for a serial-mouse adapter whose MAX232 feeds the mouse's RTS/DTR from
it: set OPR bit 1 (`DUART_OPR_SET = 0x02`) to drive the pin low, which the adapter's inverting
driver turns into the positive RTS that powers the mouse, clear it to power the mouse down, and a
Microsoft mouse then sends its identification byte when it comes back up.
Nothing uses it. Setting it up and sending (tested in the emulator with
`--duart-b-log`):

```c
static void duartb_init(void)
{
    DUART_CRB = DUART_CR_RESET_MR;
    DUART_CRB = DUART_CR_RESET_RX;
    DUART_CRB = DUART_CR_RESET_TX;
    DUART_CRB = DUART_CR_RESET_ERR;
    DUART_MRB = SERIAL_MR1_VALUE;               /* MR1B: 8 bits, no parity */
    DUART_MRB = SERIAL_MR2_VALUE;               /* MR2B: 1 stop bit */
    DUART_CSRB = 0xCC;                          /* 38400 RX and TX (ACR[7] = 0, set by the BIOS) */
    DUART_CRB = DUART_CR_ENABLE;
}

static void duartb_putc(uint8_t c)
{
    while (!(DUART_SRB & SRB_TXRDY)) ;
    DUART_TBB = c;
}
```

Receive by polling `SRB_RXRDY` and reading `DUART_RBB`, or enable `IMR_RXRDYB` (together
with `IMR_RXRDYA`) and drain channel B from a level-3 hook: the BIOS's level-3 handler only
drains channel A, and the interrupt is level sensitive, so the hook must empty channel B's FIFO
or the machine stays in the interrupt.

### The input and output ports

The 68681's six inputs IP0..IP5 and eight outputs OP0..OP7 serve the Atari joystick port J12
and its paddle circuit (board rev C3; the wiring is in [2.13](#213-leds-user-pins-and-headers)):

| pin | `hw.h` | use |
|---|---|---|
| IP0 | `JOY_IP_UP` | joystick up; paddle A end of charge |
| IP1 | `JOY_IP_DOWN` | joystick down; paddle B end of charge |
| IP2 | `JOY_IP_LEFT` | joystick left; paddle B's button |
| IP3 | `JOY_IP_RIGHT` | joystick right; paddle A's button |
| IP4 | `JOY_IP_FIRE` | joystick fire |
| IP5 | `JOY_IP_SPARE` | spare: pulled up, reads 1 |
| OP0 | `JOY_OP_DISCHARGE` | shorts both paddle capacitors while the pin is high |
| OP1..OP7 | | not connected |

**Reading.** `DUART_IP` returns the levels of IP0..IP5 in bits 0..5, unlatched (bits 6 and 7
read 1). Every input has a 10 k pull-up and a closed switch pulls it low, so
`~DUART_IP & 0x1F` is the switch mask, already in the bit order of the `JOY_*` constants:
that is all `sys_joystick` does.

**Change detection.** `DUART_IPCR` bits 3..0 are the levels of IP3..IP0, and bits 7..4
(`IPCR_DIP0`..`IPCR_DIP3`) are set when that input has changed since IPCR was last read;
reading IPCR clears them. ACR bits 3..0 (`ACR_IPC0_EN`..`ACR_IPC3_EN`) let a change on that
input set ISR.IPC, which with `IMR_IPC` set holds the level-3 request until IPCR is read. IP4
and IP5 have no change detector. The 68681 samples its inputs at 38.4 kHz, so a change is
detected 26..52 us after it happens; the emulator compares the lines once per video line and at
every DUART access. The BIOS's level-3 handler reads IPCR whenever
ISR.IPC is set, before it calls the level-3 hook, so a hook finds the change bits already
cleared: for input-change interrupts of your own, replace vector 27.

**Outputs.** The OP pins carry the complement of the output port register: `DUART_OPR_SET`
sets the register bits written as 1, which drives those pins low; `DUART_OPR_RESET` clears
them, which drives the pins high. The register is write-only and resets to 0, every pin high;
the BIOS never writes OPCR, so it stays at its reset value 0 and every pin follows the
register. With OPR bit 0 clear (the reset state) OP0 is high and the transistors short both
paddle capacitors; `DUART_OPR_SET = JOY_OP_DISCHARGE` releases them to charge.

**How the BIOS times the paddles** (`fw/joy.c`). The first `sys_paddles` call shorts the
capacitors, presets the counter/timer to 0xFFFF, writes ACR = `JOY_ACR_PADDLES` 0x73 (bit 7 =
0 keeps baud-rate set 1; bits 6..4 = 111, timer mode from X1/16, 230.4 kHz or 4.34 us a count,
so the free-running 16-bit count wraps every 284 ms; bits 1..0 enable change interrupts on IP0
and IP1), starts the counter/timer, reads IPCR to drop stale changes and writes IMR =
`IMR_RXRDYA | IMR_IPC`; the capacitors stay shorted until the next VBLANK. Every VBLANK
interrupt then

1. turns the counts collected in the frame that just ended into the `struct paddles` that
   `sys_paddles` copies out (2,764 counts, 12 ms, scale to 1023; longer clamps to 1023; a line
   that did not fall gives 1023 and a clear valid bit);
2. if the capacitors were released, shorts them (`OPR_RESET`). The lines that had fallen rise
   again, and when the DUART reports that rise (26..52 us later, so the discharge has
   certainly been sampled), the level-3 handler notes the count (CUR, CLR, then CUR again until
   the two upper bytes agree, so a borrow between the reads cannot tear the value) and
   releases the capacitors (`OPR_SET`). There is no timed wait.
3. if they were still shorted because no line rose (no paddle had charged, or none is plugged
   in), notes the count and releases them at once. A frame in which neither paddle charged is
   therefore followed by a frame spent shorted, and with nothing plugged in the capacitors are
   released every other frame.

When a capacitor passes the 74HC14's threshold, the gate pulls its line low; the level-3
handler sees ISR.IPC, reads IPCR (which clears it) and the count, and stores start - now (the
counter counts down) for each line that fell, keeping only the first fall of a frame. With two
paddles the handler runs three times a frame: the rise after the short (one interrupt for both
lines) and the two falls (*measured* in the emulator: 360 interrupts in 120 frames; 240 when
both lines fall together). The counter resolves 4.34 us, a third of a step of the 0..1023 scale (11.7 us); what limits a
reading is the detector's 26..52 us delay, which varies with the sampling phase, and the delay
before the handler reads the count, which waits behind the level-4 line handler: about 8 bits.

**Sharing.** While paddles are measured, ACR and the counter/timer belong to the BIOS, and
`IMR_IPC` must stay set. The BIOS writes ACR and IMR whole only when measuring starts
(`JOY_ACR_PADDLES`, `IMR_RXRDYA | IMR_IPC`) and when it stops (`SERIAL_ACR_VALUE`,
`IMR_RXRDYA`), so a program that adds `IMR_RXRDYB` for channel B writes IMR after its first
`sys_paddles` call (`IMR_RXRDYA | IMR_IPC | IMR_RXRDYB`) and again after `sys_paddles(NULL)`.
The counter/timer cannot be shared: a program uses it ([below](#the-countertimer-as-a-second-timer))
or the paddles, not both.

### The counter/timer as a second timer

The 68681's 16-bit counter/timer, clocked from the 3.6864 MHz crystal (or crystal/16), can
interrupt at level 3 at a rate of your choosing. It is free unless the program measures
paddles: from the first `sys_paddles` call until `sys_paddles(NULL)` or the end of the program
it is the BIOS's paddle timer ([above](#the-input-and-output-ports)), and a program that uses
it as below must not call `sys_paddles`. In timer mode the CTR bit sets once every
2N source clocks (N = the preset), and reading `STOP_CTR` clears it while the timer keeps
running:

```c
/* ctimer.c: the 68681 counter/timer as a second periodic interrupt (level 3). */
#include <stdint.h>
#include <stdio.h>
#include "hw.h"
#include "syscalls.h"

#define ACR_TIMER_X1 0x60       /* ACR[6:4] = 110: timer mode, X1 clock; ACR[7] = 0 keeps 38400 */
static volatile uint32_t count;

static void duart_hook(void)    /* level 3, after the BIOS has drained the RX FIFO */
{
    if (DUART_ISR & ISR_CTR) {
        (void)DUART_STOP_CTR;   /* timer mode: clears CTR, the timer keeps running */
        count++;
    }
}

int main(void)
{
    unsigned rate = 8000;
    unsigned n = (unsigned)(DUART_XTAL_HZ / (2UL * rate));     /* period = 2N X1 clocks */
    sys_set_irq_handler(3, duart_hook);
    DUART_CTUR = (uint8_t)(n >> 8);
    DUART_CTLR = (uint8_t)n;
    DUART_ACR = ACR_TIMER_X1;
    (void)DUART_START_CTR;
    DUART_IMR = IMR_RXRDYA | IMR_CTR;           /* keep the BIOS's serial receive interrupt */
    uint32_t t0 = sys_ticks(); while (sys_ticks() == t0) ;
    uint32_t c0 = count; t0 = sys_ticks();
    while (sys_ticks() - t0 < 60) ;
    uint32_t c = count - c0;
    DUART_IMR = IMR_RXRDYA;                     /* the BIOS's value */
    DUART_ACR = SERIAL_ACR_VALUE;
    (void)DUART_STOP_CTR;
    sys_set_irq_handler(3, NULL);
    printf("N=%u: %lu interrupts in 60 frames\n", n, (unsigned long)c);
    return 0;
}
```

In the emulator it counts about 8,020 interrupts in 60 frames (8,014 Hz expected). The cleanup
at the end matters: with CTR left enabled in IMR after the program exits, nothing clears it and
the machine hangs in the level-3 interrupt. The values it writes back, ACR =
`SERIAL_ACR_VALUE` and IMR = `IMR_RXRDYA`, are also what `sys_paddles(NULL)` and the end of a
paddle-measuring program leave. (The ACR fields, `ACR_CT_MODE0`..`2` for the mode and source
included, are in the spec and `hw.h`, from the MC68681 data sheet.)

### Access cost

A DUART access waits for the 68681's own DTACK, 2-3 of its clocks after chip select. That is
irrelevant at 38,400 baud (one character every 260 us, 3,125 clocks) but makes the DUART a poor
place to poll in a tight loop.

## 2.12 The flash ROM

### Layout and reading

The 1 MB window at 0xE00000 is two SST39SF040 chips side by side: U4 holds the even bytes
(D15..D8), U5 the odd bytes (D7..D0); chip byte address N is CPU word address `ROM_BASE + 2N`.
Reads take 4 clocks at any alignment and never wait for a slot, so the flash is the fastest
memory on the board for code and read-only data. The window is two parts:

| range | sectors | contents |
|---|---|---|
| 0xE00000-0xE3FFFF | 0-31 | the BIOS image, `ROM_IMAGE_SIZE` = 256 KB: vectors, code, data, padded with 0xFF, its last word set so that the 16-bit sum of all words is 0 (`fw/mkrom.py`; the POST checks it). About 60,000 bytes are used, FatFs some 16 KB of that. |
| 0xE40000-0xEFFFFF | 32-127 | drive C: (`FLASH_VOL_OFFSET`, `FLASH_VOL_SIZE` = 768 KB): a FAT12 volume without a partition table, 1536 sectors of 512 bytes, 4 KB clusters, two FATs, 224 root entries |

The volume's layout is chosen for the flash: the boot sector, the two FATs and the root
directory (14 sectors) take volume sectors 0-31, the volume's first two erase sectors (window
sectors 32 and 33), and the data area starts at volume sector 32, so each 4 KB cluster lies
inside one 8 KB erase sector. `format C:` at the monitor prompt (FatFs
`f_mkfs`, with the erase sector as its alignment) uses 2 reserved sectors and pads the FATs to
8 sectors each to get there;
`tools/mkflashvol.sh` on the host uses 16 reserved sectors and one-sector FATs instead
(`mkfs.vfat -F 12 -s 8 -f 2 -r 224 -R 16`, on an image filled with 0xFF beforehand, so that
the unused clusters are erased flash rather than zeros that would have to be erased before
use). Either way the data area starts at the same place. `make flash` builds
`fw/build/c.img` with it from `flashfs/` (`AUTOEXEC.CMD`, `README.TXT`) and the SDK's
example programs, stripped (the loader needs only the program headers and the segments, and
symbols would double the size), and `fw/mkrom.py --split` puts the BIOS image and the volume together as
`fw/build/flash.img`, the whole window, and splits that into `rom_even.bin` (U4) and
`rom_odd.bin` (U5) for the TL866 programmer. Programming the chips from those installs both;
the monitor's `flash` command ([below](#the-monitor-commands)) rewrites only the BIOS image
and leaves C: alone.

### Programming

Commands are JEDEC sequences, issued as **word writes with the byte repeated in both halves**
so that both chips see them at once (`flash` section of the spec; constants in `hw.h`):

| operation | sequence |
|---|---|
| program a word | 0xAAAA -> `FLASH_CMD1` (0xE0AAAA), 0x5555 -> `FLASH_CMD2` (0xE05554), 0xA0A0 -> `FLASH_CMD1`, data -> address; poll until a read of the address returns the data (at most `FLASH_T_BYTE_PROGRAM_US` = 20 us) |
| erase the 8 KB sector containing X | 0xAAAA -> CMD1, 0x5555 -> CMD2, 0x8080 -> CMD1, 0xAAAA -> CMD1, 0x5555 -> CMD2, 0x3030 -> X; poll until X reads 0xFFFF (at most `FLASH_T_SECTOR_ERASE_MS` = 25 ms) |
| read IDs | 0xAAAA -> CMD1, 0x5555 -> CMD2, 0x9090 -> CMD1; `ROM_BASE` reads 0xBFBF (`FLASH_MFR_ID` in both bytes), `ROM_BASE + 2` reads 0xB7B7 (`FLASH_DEV_ID`); 0xF0F0 anywhere returns to read mode |

A sector is 4 KB per chip, `FLASH_SECTOR_BYTES` = 8 KB in the CPU's view, so the window has 128
sectors; the BIOS image is sectors 0-31, C: sectors 32-127. Programming can only clear bits: erase first. While a
chip programs or erases, every read from it returns status (DQ7 inverted, DQ6 toggling)
instead of data. There is no write protection besides the unlock sequence: any write to the ROM
region reaches the chips (CPLD1 pulses ROMWE and gives DTACK), so stray writes are harmless
unless they happen to form a command.

**Why from RAM with interrupts masked.** During a program or erase, nothing can be fetched from
the flash: not the BIOS's code, not its interrupt handlers (the vector table is in RAM but
points into the ROM), not the system-call dispatcher. So the routine that issues commands and
polls must run from RAM, and no interrupt may be taken until the chip is back in read mode.
User programs already run from RAM; they need supervisor mode to mask interrupts, for example
with `super_call` from [2.1](#getting-into-supervisor-mode):

```c
static volatile uint16_t prog_data;
static uint32_t flash_program_word(uint32_t addr)   /* run with super_call: IPL 7 */
{
    volatile uint16_t *cmd1 = (volatile uint16_t *)FLASH_CMD1;
    volatile uint16_t *cmd2 = (volatile uint16_t *)FLASH_CMD2;
    volatile uint16_t *dst = (volatile uint16_t *)addr;
    uint16_t v = prog_data;
    *cmd1 = 0xAAAA;
    *cmd2 = 0x5555;
    *cmd1 = 0xA0A0;
    *dst = v;
    for (uint32_t n = 0; n < 1000; n++)         /* data sheet: <= 20 us */
        if (*dst == v) return 0;
    return 1;
}

/* prog_data = 0x1234; super_call(flash_program_word, ROM_BASE + 0xFFFFE); */
```

In the emulator the word at 0xEFFFFE, the last word of the window, changes from 0xFFFF to
0x1234. It lies in C:'s last cluster, which is free on the volume `make flash` builds (and the
BIOS rewrites a free cluster's whole erase sector before it uses it); on a fuller C: the same
write would damage a file. There is no unused flash any more: everything outside the BIOS
image belongs to C:, so a program that wants its own data in the flash should keep it in a
file and let the BIOS write it. The NMI button must not be pressed while a command is in
progress (its handler is in the flash). Never erase or program sectors 0-31 from a program: a
damaged BIOS image means reprogramming the chips in an external programmer; erasing a sector
of 32-127 destroys part of C: (`format C:` is the way back).

### Writing the C: volume

The BIOS's block driver for C: is `fw/flashdisk.c`. Reads copy 512-byte sectors straight out
of the window (from the cache instead, if it holds a newer copy). Writes go through one 8 KB
cache in BIOS RAM ([1.7](#writing-to-c) has the consequences):

1. A sector write whose erase sector is not in the cache first writes the cache back if it is
   dirty, then copies the new erase sector (16 volume sectors) from the flash into it.
2. The 512 bytes are compared with the cached copy; only if they differ are they copied in
   and the cache marked dirty. FatFs rewrites FAT and directory sectors often with the same
   contents, and those writes cost nothing.
3. Writing back (`flashdisk_flush`) happens when the cache has to move, on FatFs's
   `CTRL_SYNC` (every `f_close`/`f_sync`, and at the end of every delete, rename, mkdir and
   `f_utime`), when a program exits, and before every monitor prompt. It calls
   `flash_write_sector`, which runs operation 2 of `flash_ram_op` (`fw/flashram.S`, assembled
   into `.data`, so it runs from BIOS RAM) with interrupts masked: erase the sector, program
   the 4096 words, skipping those that are 0xFFFF, and verify. The data sheet's figures give
   about 100 ms (erase up to 25 ms, a word program up to 20 us); the routine's own loop,
   which polls the chip and between polls watches `INTSTAT.VBLANK`, makes it about 155 ms for a
   full sector (*measured* in the emulator, whose chips finish after a few status reads). Each
   VBLANK flag the routine sees it clears and counts; afterwards the C side adds the count to
   the tick counter and the clock, as if the interrupts had been taken.
4. A timeout or a verify mismatch drops the cache and fails the FatFs call with a disk error,
   `E_IO`.

`info` at the monitor prompt shows how many erase/program cycles C: has had since reset, and
whether the cache is dirty.

**In the emulator** the window is the `--rom` file, read whole at start (padded with 0xFF to
1 MB), and the chips are modelled down to the command sequences and status reads.
`--rom-save FILE` writes the window to `FILE` at exit if a program or erase changed it, which
is how `make run` keeps C: from one run to the next (it saves to the image it boots,
`fw/build/flash.img`). Without `--rom-save`, whatever the machine wrote is gone at exit.

### The monitor commands

* `flashid` prints the two ID words (`bfbf/b7b7` = two SST39SF040s).
* `flash FILE` reads a 256 KB BIOS image (`fw/build/rom.bin`, from either drive) into RAM at
  `USER_BASE` (0x008000-0x047FFF, clobbering user RAM), checks its size and zero word sum, asks
  `y/N`, then hands over to a routine in RAM (`fw/flashram.S`, operation 1) which, with
  interrupts masked, erases, programs and verifies the 32 sectors of the BIOS image (a `.` per
  sector on the serial port) and jumps through the new image's reset vectors. C:, in sectors
  32-127, is not touched, so the files on it survive a BIOS update. On a timeout or verify mismatch it prints the
  sector and stops the CPU. All of that output goes to serial port A only (the routine cannot
  call the BIOS's screen code, which is in the flash it is rewriting), so attach a terminal to
  follow it; the screen only says that programming has started. Do not press NMI or reset while
  it runs.
* `reboot` jumps through the reset vectors at `ROM_BASE` (SSP and PC) without resetting the
  glue: the way to start a ROM image that was just written.

## 2.13 LEDs, user pins and headers

* `GPIO_LED0`, `GPIO_LED1`: the BIOS uses them as POST codes (LED0 = POST started, both = RAM
  test, LED1 = C initialisation, alternating = RAM failure) and leaves both off; `sys_exit`
  turns them off; the monitor's `led n` sets them.
* There are no general-purpose user pins any more. Up to spec revision 6 (rev B4) the `USERIO`
  register drove four open-drain pins UIO0-UIO3 on the expansion header; revision 7 removed
  them to make room in CPLD2 for the 4-bit SD bus. The DUART's pins went to the joystick port
  in board rev C3 (below): IP5 is the only spare DUART input, pulled up and not on any
  connector, and OP1..OP7 are not connected.
* The joystick port J12 (board rev C3, spec `joystick`) is a DE-9 male on the front edge, between
  the microSD socket and the buttons, with the Atari pinout: 1 up, 2 down, 3 left, 4 right,
  5 paddle B pot, 6 fire, 7 +5 V (through F2, a 200 mA polyfuse), 8 GND, 9 paddle A pot; the
  shell is grounded. The five switch lines go through 1 k series resistors to IP0..IP4 (up,
  down, left, right, fire), which have 10 k pull-ups, so a closed switch reads 0; an Atari
  paddle's button is on the right line (paddle A) or the left (paddle B). Each pot (1 M in an
  Atari paddle) charges a 15 nF capacitor from +5 V through a 1 k series resistor, which limits
  the discharge transistor's current when the pot is at its end stop (about 0 ohm) and is
  negligible for the timing; a 74HC14 Schmitt inverter (U14)
  watches each capacitor and, once it passes the threshold, pulls the up line (paddle A) or the
  down line (paddle B) low through a BAT54W diode (D6, D7). A dual NPN (Q1, MMDT3904), its
  bases driven from OP0 through 4.7 k each, shorts both capacitors while OP0 is high
  ([2.11](#the-input-and-output-ports)). A joystick has no pots, so the gates never pull its
  up and down lines; paddles have no up or down switches.
* The expansion header itself went in board rev C2: with the user pins gone it carried only
  power and DUART channel B, which now has its own FTDI-pinout header J11 ([2.11](#channel-b)).
  Rev C1 boards have J8 (2x8): pin 1 +5 V, 3 +3.3 V, 13 TXDB, 15 RXDB, every even pin ground.
* Headers for the bench: J7 JTAG (ByteBlaster pinout, the two CPLDs in a chain) and J9, a 2x15
  header with gusmanb's LogicAnalyzer pinout carrying the bus control signals and A1-A4 through
  100 ohm (see kicad/README.md); both on the left edge of the rev C2 and C3 boards.

## 2.14 Running without the BIOS

### What the BIOS leaves when your program starts

* Vectors at 0x000000-0x0003FF as described in 2.7 (vector 28 on the BIOS's fast level-4
  handler); BIOS data at 0x000400-0x004B7F; supervisor stack below 0x008000.
* C:'s write cache clean (the monitor writes it back before starting a program); the current
  drive and directories as the last command or program left them.
* INTEN = 0x000D (VBLANK, PS/2, DUART); GPIO = 0x0003; AUDIO = 0xAA (the idle pattern; no
  audio stream open); SDIO = 0x3F (every line released, CLK high); the SD card initialised
  and in transfer state on the 4-bit bus if anything has used D: since reset (a program loaded
  from C: may find the card untouched; one loaded from D:, or found by a search that reached
  `D:/`, does not).
* DUART channel A at 38400 8N1 with IMR = RXRDYA, released: the BIOS writes it only when the
  program asks, and its receive ring may already hold bytes. ACR = 0x00; channel B and the
  counter/timer idle; paddle measuring off and OPR 0 (the paddle capacitors shorted).
* NTSC timing, page 0 displayed with the console on it.
* The CPU in user mode with SR = 0x0000, USP pointing at `{return address, argc, argv}` just
  below the argument strings, D0-D7 and A1-A6 cleared (A0 holds the entry address).

### Taking the machine over

A program can take as much as it wants:

1. Enter supervisor mode through a TRAP vector of its own (`super_call`, or a handler that
   sets the S bit in the stacked SR before `rte` to continue in supervisor mode).
2. `ori.w #0x0700,%sr` while reconfiguring; load SSP with a stack of your own if you will run
   long in supervisor mode.
3. Install your own vectors for the interrupts you use, and for the bus error, address error,
   illegal instruction and TRAP vectors if you want your own crash handling.
4. Program INTEN, GPIO, SDIO, the soft switches and the DUART to your needs, keeping your own
   shadows of INTEN and GPIO (SDIO reads back).
5. Lower the mask to the level you want.

What depends on what, if you still want parts of the BIOS:

| BIOS service | needs |
|---|---|
| any system call | vector 47 intact, BIOS data 0x000400-0x007FFF intact, room on the supervisor stack |
| files, raw blocks, `sys_exec` | the above; works with interrupts masked (the SD driver polls, and C:'s flash routine masks them anyway) |
| console output | the above; page 0 is drawn on (the DUART is not touched) |
| `sys_getchar`, `sys_pollchar`, `sys_key_event`, stdin | the level-2 BIOS handler (vector 26, `INTEN_PS2`, mask below 2) |
| `sys_serial_getc`, reads of `/dev/ttya` | the level-3 BIOS handler (vector 27, `INTEN_DUART`, `IMR_RXRDYA`, mask below 3); a `/dev/ttya` read waits for ever otherwise |
| `sys_serial_putc`, writes to `/dev/ttya` | channel A's transmitter as the BIOS set it up; works with interrupts masked (it polls) |
| `sys_joystick` | nothing but the DUART's input port; works with interrupts masked |
| `sys_paddles` | the level-1 and level-3 BIOS handlers (vectors 25 and 27, `INTEN_VBLANK` and `INTEN_DUART`, mask 0) and the DUART's ACR, IMR, counter/timer and OPR as the BIOS writes them; otherwise the results stop changing |
| `sys_ticks`, `sys_wait_vblank`, `sys_sleep_ms` | the level-1 handler (vector 25, enable on, mask 0); the waits spin forever otherwise |
| `sys_audio_start`, `sys_audio_next`, `sys_audio_last` | the level-4 handler (vector 28 as the BIOS sets it, mask below 4), INTEN written only through the BIOS, and the BIOS data area, which holds the buffers; otherwise nothing plays and a blocking `sys_audio_next` or `sys_audio_last` waits for ever |
| `sys_exit` | returns to the monitor with a fresh supervisor stack and mask 0: vectors and DUART state you changed stay changed |

What you lose by taking it all: the console, keyboard decoding, the serial receive ring, the
tick counter, FAT file access (unless you keep the BIOS data area and call it), and the crash
reports. What you gain: the whole 1 MB of RAM except what you choose to keep (even
0x000400-0x007FFF if you will never call the BIOS again), all interrupt levels, and no
per-interrupt overhead you did not write.

There is no clean way back from a full takeover other than the reset button (the POST takes
about 4 s). A program that wants to return to the monitor must restore the vectors, INTEN, GPIO
(PS/2 lines released), the DUART's IMR, ACR, counter/timer and OPR, page 0 and NTSC timing, leave
the BIOS data area intact, and then call `sys_exit`.

### Other ways in

* **S-records**: type `srec` at the monitor prompt and send an S-record file over the serial
  port ([1.2](#on-the-board)); data is accepted only for 0x008000-0x0FFFFF; `go [addr]` starts
  it in user mode with `argc` 0. This needs no SD card.
* **A different ROM**: the reset vectors always come from `ROM_BASE`, so code that must run
  before the BIOS has to replace the BIOS image (`flash`, or an external programmer with the
  even/odd split images). A broken image can only be recovered with the external programmer.
  The rest of the flash is drive C:, so code and data for a program to call or read in place
  belong in a file there.
* **A script**: `C:/AUTOEXEC.CMD` can start a program at every reset (a line with its name),
  which is the nearest thing to a ROM-resident application without replacing the BIOS.

---

## Appendix A: Cycle budget

NTSC figures unless stated; PAL in parentheses. One CPU clock = 83.3 ns.

| quantity | clocks | time |
|---|---|---|
| slot (one video word or one CPU RAM access) | 4 | 333 ns |
| pixel | 2 | 167 ns |
| line (`VIDEO_NTSC_LINE_CLOCKS`) | 764 (768) | 63.67 us (64.00 us) |
| visible part of a line | 640 | 53.3 us |
| horizontal blanking (front porch + sync + back porch) | 124 (128) | 10.3 us (10.7 us) |
| frame (`VIDEO_NTSC_FRAME_CLOCKS`) | 200,168 (239,616) | 16.68 ms (19.97 ms) |
| visible lines (240) | 183,360 (184,320) | |
| vertical blank, line 240 to line 0 | 16,808, 22 lines (55,296, 72 lines) | 1.40 ms (4.61 ms) |
| VBLANK interrupt (line 243) to line 0 | 14,516, 19 lines (32,256, 42 lines) | 1.21 ms (2.69 ms) |
| HSYNC to first pixel of the line | 108 (112) | 9.0 us |

| memory and I/O access | clocks |
|---|---|
| RAM or CPLD2 register, cycle starting on a slot boundary | 4 |
| RAM or CPLD2 register, cycle starting mid-slot | 6 |
| ROM (flash) read, soft-switch write | 4, any alignment |
| 68681 register | 4 + the 68681's DTACK delay (0.5-0.8 us) |
| peak RAM accesses | 3,000,000 per second (one per slot) |

| operation | clocks |
|---|---|
| interrupt entry (autovector), book / plus VPA IACK on the board | 44 / roughly 50-60 |
| `rte` | 20 |
| `trap #n` entry | 34 |
| trivial system call (`sys_ticks`), *measured* | about 450 |
| BIOS HSYNC handler playing the audio stream (fast handler), hand count / *measured* | 242 / 240.6 per line, 32 % of the CPU (68.5 % left) |
| BIOS slow handler + empty audio callback, *measured* | 272 per line, 36 % (64 % left) |
| BIOS slow handler + C / assembler callback popping a pattern from a ring, *measured* | 423 / 358 per line, 55 % / 47 % |
| BIOS HSYNC interrupt while no stream plays and no callback or hook is installed | 0 (the interrupt is off) |
| `pdm_encode` (SDK), *measured* | about 83 per sample |
| `pdm_init` (SDK), *measured* | about 4.8 million (0.4 s, 24 frames), once |
| own HSYNC vector, C `interrupt_handler` / assembler, *measured* | 226 / 168 per line |
| `memcpy` of a page, *measured* | about 430,000 (2.15 frames) |
| `memset` of a page, *measured* | about 420,000 (2.1 frames) |
| `movem.l` fill of a page (2.4), *measured* | about 180,000 (0.9 frame) |
| console character, in a string / through `sys_putchar`, *measured* | about 2,600-2,900 / 3,000 |
| console scroll, *measured* | about 480,000 (40 ms) |
| serial byte at 38400 baud (`sys_serial_putc`, `/dev/ttya`) | 3,125 (260 us) |
| paddle measuring, per frame, nothing plugged in / two paddles, *measured* | about 430 / 3,500 (0.2 % / 1.8 % of the CPU) |
| SD 4-bit read loop (BIOS, `sd4_read512`), per byte | 86.5 |
| SD block read, data phase / `sys_sd_read_block`, *measured* | 44,300 / about 60,600 (3.7 ms / 5.05 ms, 101 KB/s) |
| SD block write, `sys_sd_write_block`, *measured* (emulated card busy 16 clocks) | about 160,000 (13.3 ms, of which 6.9 ms CRC16s), plus the card's programming time |
| C: read, 200 KB in 8 KB `sys_read`s, *measured* | 8 frames (0.13 s, about 1.5 MB/s) |
| C: erase/program cycle of a full 8 KB sector, interrupts masked, *measured* (emulated chips) | about 1,860,000 (155 ms); data sheet worst case about 100 ms of chip time |
| single-precision soft-float multiply-add, *measured* | about 3,400 |

At 12,000,000 clocks per second: 15,707 lines and 59.95 frames per second (15,625 and 50.08
on PAL).

## Known discrepancies

Places where the repository disagrees with itself, found while writing this guide. The guide
follows the code. Every item found for earlier revisions (eleven for spec revision 4, one each
for revisions 5 and 6, three for revision 7) has been fixed in the repository. The revision 8
pass found five more, all fixed the same day: `/dev/ttya` streams were fully buffered because
the SDK's `fstat` failed on a device descriptor (it now reports a character device, so stdio
line-buffers them); spec `serial.console` now names the `flash` command's RAM routine as the
other BIOS user of the port after the banner; `fw/README.md` and `sdk/README.md` no longer
suggest piping an S-record file into the emulator's stdin from start-up (bytes sent before
`srec` runs are discarded); a stale serial-era comment in `sdk/examples/tune.c` and the
`input.c` row of `fw/STATUS.md`; and the "4 GPIO" left in `README.md`'s machine table from
revision 7.

The revision 9 pass found these; all five were fixed the same day:

* **`rename()` did not link.** `sdk/newlib_shim.c` defined `rename`, but newlib's `libc.a`
  has its own `rename` (stdio `rename.c`, which calls `_rename_r` and then `_rename`), so the
  link failed with a multiple definition. Fixed: the shim provides `_rename`; `rename()`,
  `remove()`, `mkdir`, `rmdir`, `chdir` and `getcwd` all link and work (*verified in the emulator*).
* **`rename`'s new name.** Spec syscall 34 said "a bare name keeps the directory"; FatFs's
  `f_rename` resolves `new` against the volume's current directory and drops a drive letter from
  it (with the current directory `C:/`, `ren T/R.TXT X.TXT` moves the file to `C:/X.TXT`,
  *measured*). Fixed in the spec; the guide describes what the code does.
* **`getcwd` with `len` 0** returned `E_FAULT` where the spec promised `E_INVAL`. Fixed in
  `fw/syscall.c`.
* **The C: layout.** `tools/mkflashvol.sh` claimed the same layout as `format C:`; the data
  area starts at sector 32 in both, but the reserved-sector and FAT sizes differ (harmless).
  The script's comment now says so.
* **The cost of a sector write.** Spec `flash.volume_desc` and `fw/flash.h` give about 100 ms
  with interrupts masked, the data sheet's sum; the routine takes about 155 ms in the emulator
  (*measured*), which is its own polling loop since the emulator's chips finish after a few
  status reads. The spec now quotes both; the board's figure has not been measured.

The revision 11 pass (the joystick port) found these; all were fixed the same day:

* **The discharge.** The first sequence shorted the capacitors for a timed `delay_us(20)`
  inside the VBLANK interrupt (which ran long: `delay_us`, `fw/serial.c:41`, computes its loop
  count with a 32-bit division), and the lines' rise then cost a level-3 interrupt that
  recorded nothing; with the 68681 noticing a change only 26..52 us late, a short discharge
  could also go unseen on the board. Fixed: `fw/joy.c` now releases the capacitors from the
  interrupt that reports the rise, so there is no timed wait and the DUART has always seen the
  discharge. The cost figures above are measured with this sequence.
* **A joystick on the paddle lines.** `fw/STATUS.md` said a joystick held up or down reads as a
  paddle at zero; it reads as no paddle (1023, valid bit clear) except in the frame the switch
  closes (*measured* with `--joy up`). Fixed in `fw/STATUS.md` and the spec.
* **The emulator's DUART notes** (`emu/duart.c` header, `emu/README.md`) listed the input-port
  change detector as not modelled. Fixed. The spec now gives the 68681's 38.4 kHz input
  sampling (26..52 us to notice a change) and the resulting 8-bit resolution.
* **Stale board notes.** `kicad/board.py:32` called IP0..IP5 unused inputs, and
  `kicad/ORDER.md` was titled "rev C2" over the rev C3 order. Fixed.
* **Full scale in counts.** The spec said about 2765 counts where `fw/joy.c` uses 2764. Fixed.
* **Paddle buttons and the pot's end stop.** The spec now names the buttons (paddle A's on pin
  4, right; paddle B's on pin 3, left), and the board has a 1 k resistor in series with each
  pot, so a pot at its end stop no longer puts +5 V straight onto the discharge transistor.
