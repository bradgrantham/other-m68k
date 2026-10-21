# other-m68k Programmer's Guide

This guide describes **board rev B4 with the spec revision 5 CPLD2 image, spec revision 6**
(`"revision": 6` in `spec/board.json`, `BOARD_REVISION` in `generated/hw.h`) and the
BIOS/monitor in `fw/` as it stands in this repository. Revision 5 changed only the audio
hardware (an 8-bit PDM shifter behind a new `AUDIO` register instead of a GPIO bit, a JED-only
change for B4 boards). Revision 6 changed only the audio system calls: the rev 5 sample queue
(`audio_write`) and its 64 KB ROM encoder table are gone, the BIOS now streams buffers of
pattern bytes that the program fills (`audio_start`, `audio_next`, `audio_last`), and the
encoding of samples into patterns moved into the SDK (`pdm_init`, `pdm_encode`); the hardware
and the CPLD images are those of revision 5. Every address, bit and syscall
name below comes from `spec/board.json` or from the headers generated from it
(`generated/hw.h`, `generated/syscalls.h`, `generated/hw.inc`). Where the spec, the firmware and
the other documents disagree, the text says so; the list is collected in
[Known discrepancies](#known-discrepancies) at the end.

## Preface

### The machine

| | |
|---|---|
| CPU | MC68000 at 12 MHz (24 MHz oscillator divided by 2 in CPLD1) |
| RAM | 1 MB SRAM at 0x000000 (2x AS6C4008, word wide), shared with the video fetch |
| ROM | 1 MB flash window at 0xE00000 (2x SST39SF040); the BIOS image is the first 128 KB, the rest is erased and free |
| Video | 320x240, one byte per pixel (RGB332), two pages in RAM, composite out through an AD724 (NTSC on rev B4) |
| Glue | two ATF1508AS CPLDs: CPLD1 = decode, DTACK, SRAM address mux, video timing, soft switches; CPLD2 = I/O registers, pixel shifter, interrupt encoder, PS/2 receiver, PDM audio shifter, SD/GPIO pins |
| I/O | 68681 DUART (channel A = console, 38400 8N1; channel B on the expansion header), PS/2 keyboard, bit-banged microSD (SPI mode, FAT via FatFs in the BIOS), 8-bit PDM audio (one pattern byte per video line, shifted out by CPLD2 at about 126 kHz; the BIOS streams pattern bytes from buffers the program fills at the line rate, 15.7 kHz, and the SDK encodes 8-bit samples into them), two LEDs, four open-drain user pins, NMI button |
| Interrupts | autovectored, vector = 24 + level: VBLANK 1, PS/2 2, DUART 3, HSYNC 4, NMI button 7 |

Programs are ELF files on the SD card. The monitor loads one into the user region of RAM
(0x004000 upward) and starts it **in user mode**; the program reaches the BIOS through
`TRAP #15` system calls (`sys_*` functions in `generated/syscall_stubs.S`, prototypes in
`generated/syscalls.h`). The SDK links programs against newlib, so ordinary C with
`printf`, `malloc` and `fopen` works.

### Getting a program onto the machine

1. Build it with the SDK: drop `foo.c` into `sdk/examples/`, add `foo` to `EXAMPLES` in
   `sdk/Makefile`, run `make -C sdk`. The result is `sdk/build/foo.elf`.
2. Copy it to a FAT32 (or FAT16) card as `FOO.ELF` (8.3 names only).
3. At the monitor prompt type `foo` (any word that is not a monitor command runs
   `WORD.ELF` from the card, with the rest of the line as arguments), or `run foo.elf args`.
4. Or skip the card: build an image with `fw/test/mkimage.sh` and run the emulator with
   `--sd image`, then type `foo` in its window.

### Which part to read

* **Part 1, Writing applications**, is for a C programmer who wants to write a program
  quickly and does not want to know how the hardware works: build, console, graphics,
  files, sound, timing, and a reference table of every system call.
* **Part 2, Programming the hardware directly**, is for someone writing a game engine, an
  interpreter, a demo or bare-metal code who wants every cycle: memory map, bus timing,
  every register bit, interrupts and their cost, the SD/PS/2/DUART/flash protocols, and
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
(0x004000), entry point `_start`.

**Adding a program.** Put `foo.c` in `sdk/examples/`, add `foo` to the `EXAMPLES` line in
`sdk/Makefile`, and run `make -C sdk`. You get `sdk/build/foo.elf` (what the monitor loads)
and `sdk/build/foo.srec` (for upload over the serial port). Keep the base name at most 8
characters: the card only stores 8.3 names.

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
make -C emu                                               # emu/build/m68k-emu
mkdir -p /tmp/card
cp sdk/build/foo.elf /tmp/card/FOO.ELF
sh fw/test/mkimage.sh /tmp/card/sd.img 64 /tmp/card/FOO.ELF   # 64 MB FAT32 (needs dosfstools, mtools)
emu/build/m68k-emu --rom fw/build/rom.bin --sd /tmp/card/sd.img
```

The window shows the video output and takes the PS/2 keyboard; the serial console is the
emulator's stdin/stdout. Wait for the POST (about 4 s: the RAM test) and the `> ` prompt, then
type `foo`. F12 (or Pause) is the NMI button, F11 the reset button.

Headless, for scripted tests and CI:

```
emu/build/m68k-emu --rom fw/build/rom.bin --sd /tmp/card/sd.img --headless --frames 1500 \
    --type "foo arg1 arg2\n" --type-delay 350 --no-audio --no-stdin --serial-log /tmp/run.log
```

`--type` types on the PS/2 keyboard after `--type-delay` frames (350 leaves the POST time to
finish); `--serial-type` does the same on the serial port. Everything the console prints is
also on stdout and in the serial log. `--frames N` stops after N frames (60 per second).
Use `--type "foo\n"` for the command and `--serial-type` for later input if you need two
inputs at different times (there is one of each). Useful debugging options: `--exit-on-berr`,
`--break ADDR`, `--trace`, `--io-trace` (every register access with a cycle stamp),
`--dump-mem ADDR LEN`, `--audio-wav FILE` (the audio output rendered to a 48 kHz mono WAV from
cycle 0; works headless and with `--fast`, so you can check sound without a sound card) and
`--audio-edge-log FILE` (every edge of the audio pin with its cycle stamp). See
`emu/README.md` for the full list.

Make images and cards 64 MB or larger for FAT32 (a FAT32 volume needs at least 65,525
clusters); smaller ones come out as FAT16, which also works.

### On the board

Copy `FOO.ELF` to the card, insert it, and type `foo` at the prompt (or `run foo.elf`). `dir`
lists the card. You can also send `sdk/build/foo.srec` over the serial port (38400 8N1) after
the prompt appears (`cat foo.srec > /dev/ttyUSB0`); the monitor recognises the S-records,
prints a summary, and `go` starts the program (with `argc` = 0).

### What the monitor does with the command line

`run foo.elf a b c` and the shorthand `foo a b c` both call `main(argc, argv)` with
`argv[0]` = the path as typed (`"foo.elf"`), then the remaining words split on blanks (at
most 16 arguments, 255 bytes). Monitor commands win over programs: `ls` is the monitor's
directory listing, so a program called `LS.ELF` needs `run ls.elf`.

## 1.3 The runtime model

### Memory

```
0x000000  vector table (1 KB)                 \  BIOS: do not touch
0x000400  BIOS data, supervisor stack to 0x4000 /
0x004000  your program: .text .rodata .data .bss  <- ELF loaded here, entry _start
          heap (malloc/sbrk) grows up from the end of .bss
          ...
0x0D6800  top of heap (user_end - 16 KB)
          user stack, 16 KB reserve, grows down; argv strings at the very top
0x0DA800  framebuffer page 1 (75 KB) - free RAM if you do not double buffer
0x0ED400  framebuffer page 0 (75 KB) - the text console, shown after reset
0x100000  end of RAM
```

The program, its `.bss` and its heap share 858 KB (0x004000-0x0DA7FF). `sbrk` stops 16 KB
below the top of that region; the stack is not checked, so deep recursion past 16 KB runs
into the heap silently. Page 1 (0x0DA800-0x0ED3FF) is ordinary RAM when you do not use it for
graphics; the loader will not put ELF segments there, but you can point at it:
`sys_mem_info()` reports `user_end_without_fb1` = 0x0ED400.

### The C library

* `printf`, `puts`, `putchar`, `fprintf(stderr, ...)`: go to the console (video page 0 and
  the serial port). `stdout` is line buffered; call `fflush(stdout)` before waiting for input
  after a prompt that has no newline.
* `fgets(buf, n, stdin)`, `scanf`, `getchar()`: read the console through a small line editor
  (echo, Backspace) in the shim; the line ends with Enter.
* `malloc`/`free`/`realloc`/`calloc`: newlib's allocator on top of `sbrk`.
* `fopen`/`fread`/`fwrite`/`fseek`/`fclose`, `open`/`read`/`write`/`lseek`/`close`,
  `stat`/`fstat`: files on the SD card (see [Files](#17-files-and-directories)).
* `time()`, `gettimeofday()`, `clock()`, `times()`: count from boot, derived from the VBLANK
  tick counter and the real frame length of the current video standard (764 x 262 clocks,
  59.95 Hz, on NTSC; 768 x 312, 50.08 Hz, on PAL). `clock()` and `times()` count milliseconds,
  matching newlib's `CLOCKS_PER_SEC` of 1000, so `clock() / CLOCKS_PER_SEC` is seconds;
  `gettimeofday()` gives seconds and microseconds. All of them advance in whole frames
  (16.7 ms or 20 ms steps), so for game timing `sys_ticks()` is just as good and cheaper.
* Floating point works but is **software emulated** (libgcc soft-float): *measured* about
  3,400 clocks (0.28 ms) for a single-precision multiply-add and about 8,000 clocks (0.67 ms)
  for double precision. Use fixed point in anything that has to be fast. `printf("%f")` and
  `libm` work.
* Not available: deleting, renaming and making directories (no system calls: `unlink` and
  `remove` fail with `ENOSYS`, `rename` and `mkdir` do not link), `fork`/`exec*` (use
  `sys_exec`), signals, environment variables, a current directory.
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
dark blue, mirrored to the serial port. Characters it understands: printable ASCII,
`\n` (new line, CR+LF on serial), `\r`, `\b` (cursor left, not destructive), `\t` (to the next
multiple of 8 columns). Other control characters are dropped, so ANSI escape sequences are
*not* interpreted on the video screen (a terminal on the serial port will interpret them).
At the bottom the screen scrolls (a 74 KB move, about 35-40 ms).

Printing is slow because every character is also sent to the serial port at 38400 baud and the
BIOS waits for the transmitter: about 3,840 characters per second, *measured* 400
`sys_putchar` calls in 6 frames. A full screen of text takes about 0.3 s. For fast text in a
game, draw your own glyphs into the framebuffer.

| call | what it does |
|---|---|
| `sys_putchar(c)` / `sys_puts(s)` | print a character / NUL-terminated string (no newline added) |
| `sys_con_clear()` | clear page 0, cursor to 0,0 |
| `sys_con_set_cursor(x, y)` | move the cursor; `x` 0..39, `y` 0..29, otherwise `E_INVAL` |
| `sys_con_info(&ci)` | `ci.cols` = 40, `ci.rows` = 30, `ci.cursor_x`, `ci.cursor_y` |

The text colours cannot be changed through the ABI, and the block cursor is hidden while a
program runs.

### Keyboard input as characters

Input is merged from the PS/2 keyboard and the serial port (keyboard first).

* `sys_getchar()` waits for a character; `sys_pollchar()` returns -1 if there is none.
* The keyboard is decoded with a US layout, Shift, Caps Lock and Ctrl. Enter gives `'\r'`,
  Backspace 0x08, Tab, Esc 0x1B, Ctrl+letter 0x01-0x1A.
* Cursor and function keys arrive as VT100/xterm escape sequences: arrows `ESC [ A/B/C/D`
  (up, down, right, left), Home/End `ESC [ H` / `ESC [ F`, Insert/Delete/PgUp/PgDn
  `ESC [ 2~` `3~` `5~` `6~`, F1-F4 `ESC O P`..`S`, F5-F12 `ESC [ 15~ 17~ 18~ 19~ 20~ 21~ 23~ 24~`.
* Through stdio (`fgets(stdin)`, `getchar()`), input is line edited and echoed.

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
is another complete example (4x4 blocks, written as longs).

Composite colour is soft: fine horizontal colour detail smears, and the colour subcarrier is not
locked to the pixels, so expect some dot crawl. Large areas of colour and high-contrast
luminance edges look best.

## 1.6 Timing

| call | |
|---|---|
| `sys_ticks()` | frames since boot: incremented by the VBLANK interrupt, 59.95 per second on NTSC (50.08 on PAL) |
| `sys_wait_vblank()` | wait until `sys_ticks()` changes |
| `sys_sleep_ms(ms)` | busy-wait about `ms` milliseconds: whole frames by the tick counter, the rest with a calibrated loop |

`sys_sleep_ms(ms)` waits `ms * 60 / 1000` whole frames (`ms * 50 / 1000` on PAL), rounded
down, then the remaining fraction of a frame with the loop: `sys_sleep_ms(1000)` waits 60
frames on NTSC, 1.0008 s (the nominal 60 Hz against the real 59.95 Hz is the only error).
For game timing, count frames with `sys_ticks()` or `sys_wait_vblank()`; for elapsed seconds
divide ticks by 60 (50 on PAL, `vi.standard` tells you which). Everything keeps running during these calls: interrupts are
enabled, and your interrupt hooks are called.

## 1.7 Files and directories

Files live on the SD card's FAT32 (or FAT16/FAT12) volume. The BIOS mounts it the first time
a file call needs it and keeps it mounted; after a card error it re-initialises the card on the
next call. After swapping cards, type `sd` at the monitor prompt, which re-initialises the card
and drops the old mount.

Rules:

* **8.3 names only**: up to 8 characters, a dot, up to 3 characters. Case does not matter.
  Files with long names on the card appear under their 8.3 alias (`LONGNA~1.TXT`). Anything
  that is not a valid 8.3 name fails with `E_NAMETOOLONG`.
* Paths are from the root; a leading `/` is optional; subdirectories work (`/GAMES/LEVEL1.DAT`).
  There is no current directory.
* At most 8 files and 4 directories open at once. Files are closed for you when the program
  exits.
* Cannot delete, rename or create directories (no system calls for these).

With stdio, everything works as usual:

```c
FILE *f = fopen("SCORES.DAT", "wb");      /* "w" creates or truncates, "a" appends */
fwrite(table, sizeof table, 1, f);
fclose(f);
```

The POSIX layer (`open`, `read`, `write`, `lseek`, `close`, `fstat`, `stat`) also works and
translates newlib's `O_CREAT`/`O_TRUNC`/`O_APPEND` into the ABI's values. The raw calls
`sys_open(flags, path)` (note the argument order), `sys_read(fd, len, buf)`,
`sys_write(fd, len, buf)`, `sys_lseek(fd, offset, whence)` and `sys_close(fd)` return the
ABI's negative `E_*` codes instead of setting `errno`. Use the `O_*` constants from
`syscalls.h` with the raw calls; they differ from newlib's `<fcntl.h>`, so do not include both
headers in one source file. Descriptors 0, 1, 2 are the console; files get 3..10.

Directories are read with the raw calls; `struct dirent` has `name[64]` (the 8.3 name),
`size` and `attr` (`ATTR_DIR`, `ATTR_READONLY`, `ATTR_HIDDEN`).

```c
/* files.c: save and load a high-score table, then list the card's root directory. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "syscalls.h"

struct score { char name[4]; unsigned long points; };

int main(void)
{
    struct score table[3] = { { "BRD", 9000 }, { "ABC", 4200 }, { "XYZ", 100 } };

    FILE *f = fopen("SCORES.DAT", "wb");        /* 8.3 name, created in the root */
    if (!f) { printf("create: %s\n", strerror(errno)); return 1; }
    fwrite(table, sizeof table, 1, f);
    fclose(f);

    memset(table, 0, sizeof table);
    f = fopen("SCORES.DAT", "rb");
    if (!f || fread(table, sizeof table, 1, f) != 1) { printf("read back failed\n"); return 1; }
    fclose(f);
    for (int i = 0; i < 3; i++) printf("%d. %s %lu\n", i + 1, table[i].name, table[i].points);

    struct dirent e;
    int h = sys_opendir("/");
    if (h < 0) { printf("opendir: %d\n", h); return 1; }
    while (sys_readdir(h, &e) > 0)
        printf("%-12s %s%lu\n", e.name, (e.attr & ATTR_DIR) ? "<DIR> " : "", (unsigned long)e.size);
    sys_closedir(h);
    return 0;
}
```

Speed: the card is bit-banged, so expect about 25-30 KB/s (*measured* 16 KB read through
`sys_read` in 36 frames; a 76,800-byte screen loads in about 3 s). Read into a buffer in big
chunks rather than byte by byte through `fgetc`. Buffers passed to system calls must lie in
user RAM (0x004000-0x0FFFFF, both framebuffer pages included, so you can read a picture
straight into a page); otherwise the call fails with `E_FAULT`.

`errno`: the SDK shim maps every ABI error to the newlib `errno` of the same name, so a name
that is not 8.3 gives `ENAMETOOLONG`, a buffer outside user RAM `EFAULT`, no card or no FAT
volume `ENODEV`, a card error `EIO`, and `strerror(errno)` says which.

Raw blocks: `sys_sd_read_block(lba, buf)` and `sys_sd_write_block(lba, buf)` transfer one
512-byte block at an absolute card address. They bypass the filesystem; do not write blocks
that belong to the FAT volume while files are in use.

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
  the BIOS's data area below 0x004000, not in your program.
* While you fill one buffer the other two are playing or queued, so you have about two
  buffers' time for each one. If the program is later than that (an underrun), the last
  pattern repeats until the next buffer arrives: a gap, heard as a click, but nothing
  breaks. A `printf` is the usual cause, because the console waits for the 38400-baud serial
  port (about 14 ms for a full 40-character line); that is why `tune.c` below uses 512-byte
  buffers, after 256-byte ones underran once at a line break.
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
emu/build/m68k-emu --rom fw/build/rom.bin --sd /tmp/card/sd.img --headless --frames 700 \
    --type "arp\n" --type-delay 350 --no-stdin --audio-wav /tmp/card/arp.wav
```

It prints `arpeggio at 15706 samples/s`, then, about 3 s later (0.4 s for `pdm_init` and
2.6 s of sound, 16 notes of 2,560 samples), `done` and `[exit 0]`; `arp.wav` holds the notes
at their pitches (262 to 659 Hz). With `--io-trace` added you can see that the line interrupt
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
| 1 | VBLANK | once per frame | acknowledge, `ticks++` |
| 2 | PS/2 byte received | per keyboard byte | read the byte, decode it into the queues |
| 3 | DUART | per serial event | drain the serial receive FIFO |
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
  `sys_exit(n)`. The BIOS closes the program's files, stops the audio stream at once (buffers
  still queued are dropped; call `sys_audio_last(NULL, 1)` before exiting if the end of the
  sound matters) and puts the audio output back to the silence level, removes its hooks and
  audio callback, restores its
  interrupt enables, turns off the LEDs, selects page 0, prints
  `[exit n]` and returns to the monitor prompt.
* `sys_exec(path, args)` loads another ELF in place of the running program and starts it with
  `argv[0]` = `path` and `args` split on blanks (at most 16 words, 255 bytes; `NULL` for none).
  It returns only on failure, and only if the failure is found before anything is overwritten:
  `E_NOENT` (no such file), `E_NOEXEC` (not a loadable 68000 ELF), `E_NOMEM` (does not fit the
  user region), `E_NAMETOOLONG`, `E_FAULT`, `E_NODEV`/`E_IO` (card problems). The caller's open
  files and hooks are released first. When the new program exits, control goes to the monitor,
  not back to the caller; a menu program that wants to come back must be `sys_exec`'d again.
  Flush stdio before calling it (`fflush(stdout)`), since the caller's buffers vanish.
* A program loaded with `run` and restarted with `go` finds its `.data` as the first run left
  it (newlib's stdio state included), so use `run` again.

## 1.11 System call reference

All calls are `TRAP #15` with the number in D0.W, integer arguments in D1, D2, D3, pointer
arguments in A0, A1 (in the order listed), the result in D0.L; all other registers are
preserved. The `sys_*` C functions in `libsys.a` do the register shuffling. A trivial call such
as `sys_ticks()` costs about 450 clocks (*measured*), so do not make system calls in inner
loops. Errors are negative `E_*` values from `syscalls.h`. Every pointer argument (buffers,
structures, strings) must point into user RAM, 0x004000-0x0FFFFF, and a buffer must fit there
entirely, otherwise `E_FAULT`. The exception is the audio stream (30-32): its buffers belong
to the BIOS, and `sys_audio_next` and `sys_audio_last` accept only the buffer the BIOS last
handed out.

| # | prototype | description, results and errors |
|---|---|---|
| 0 | `void sys_exit(int code)` | End the program: close its files, stop the audio stream (queued buffers are dropped), drop hooks and audio callback, restore interrupt enables, audio to the silence level, LEDs off, select page 0, print `[exit code]`, return to the monitor. Does not return. |
| 1 | `int sys_putchar(int c)` | Write a character to the console (video page 0 and serial). Returns `c & 0xFF`. Blocks while the serial transmitter is busy. |
| 2 | `int sys_getchar(void)` | Wait for a character from the keyboard (first) or serial port; returns it (0..255). |
| 3 | `int sys_pollchar(void)` | Next character, or -1 if none. |
| 4 | `int sys_puts(const char *s)` | Write a NUL-terminated string to the console, no newline added. Returns the number of characters, or `E_FAULT` if `s` does not start in user RAM (an unterminated string stops at the end of RAM). |
| 5 | `int sys_serial_putc(int c)` | Write a raw byte to the serial port only (no `\n` translation). Returns `c & 0xFF`. |
| 6 | `int sys_serial_getc(void)` | Next byte from the serial receive buffer (512 bytes, filled by interrupt), or -1. |
| 7 | `uint32_t sys_ticks(void)` | Frames since boot (VBLANK count; 59.95 Hz NTSC, 50.08 Hz PAL). |
| 8 | `void sys_wait_vblank(void)` | Wait until the tick count changes (the next VBLANK interrupt, at the start of vertical sync). |
| 9 | `int sys_video_info(struct video_info *info)` | Fill in width, height, bpp, bytes per line, both page addresses, the current page and the video standard. 0, or `E_FAULT` if the structure is not in user RAM (`NULL` included). |
| 10 | `int sys_video_set_page(int page)` | Display page 0 or 1 from the next frame on. 0, or `E_INVAL`. |
| 11 | `int sys_open(int flags, const char *path)` | Open a file. `flags`: `O_RDONLY`, `O_WRONLY` or `O_RDWR`, plus `O_CREAT` (create if missing), `O_TRUNC`, `O_APPEND` (every write goes to the end); the last three need write access, else `E_INVAL`. Returns a descriptor 3..10, or `E_NOENT`, `E_NAMETOOLONG` (not 8.3), `E_NFILE` (8 open), `E_NOSPC` (directory full, or the file is read-only), `E_NODEV` (no card / no FAT), `E_IO`, `E_FAULT`, `E_INVAL`. |
| 12 | `int sys_close(int fd)` | Close a file; 0 (also for 0..2), or `E_BADF`. |
| 13 | `int sys_read(int fd, unsigned len, void *buf)` | Read up to `len` bytes; returns the count (0 at end of file) or `E_BADF`, `E_IO`, `E_FAULT`. On fd 0, waits for one character and returns whatever else is already queued (no echo, no line editing). `E_BADF` on fd 1, 2. |
| 14 | `int sys_write(int fd, unsigned len, const void *buf)` | Write `len` bytes; returns `len`, or `E_NOSPC` (card full), `E_BADF`, `E_IO`, `E_FAULT`. On fd 1, 2: to the console. `E_BADF` on fd 0. |
| 15 | `int sys_lseek(int fd, int offset, int whence)` | `SEEK_SET`/`SEEK_CUR`/`SEEK_END`; returns the new offset, or `E_INVAL` (negative result, bad `whence`, console fds), `E_BADF`. Seeking past the end of a writable file extends it. |
| 16 | `int sys_opendir(const char *path)` | Open a directory (`"/"` for the root); returns a handle 0..3, or `E_NOENT`, `E_NFILE` (4 open), `E_NODEV`, `E_IO`, `E_FAULT`. |
| 17 | `int sys_readdir(int handle, struct dirent *ent)` | Next entry: 1 and `*ent` filled, 0 at the end, or `E_BADF`, `E_IO`, `E_FAULT`. |
| 18 | `int sys_closedir(int handle)` | Close a directory handle; 0 or `E_BADF`. |
| 19 | `int sys_sd_read_block(uint32_t lba, void *buf)` | Read one 512-byte block at absolute card address `lba` (initialises the card if needed). 0, or `E_INVAL` (beyond the card), `E_IO`, `E_FAULT`. |
| 20 | `int sys_sd_write_block(uint32_t lba, const void *buf)` | Write one 512-byte block. Same results. Bypasses the filesystem. |
| 21 | `void *sys_set_irq_handler(int level, void *handler)` | Install `void handler(void)` for level 1..4, called from the BIOS interrupt routine in supervisor mode; returns the previous handler; `NULL` removes it. Level 4 also enables the HSYNC interrupt. `(void *)E_INVAL` for another level. |
| 22 | `int sys_mem_info(struct mem_info *info)` | `user_start` 0x004000, `user_end` 0x0DA800, `user_end_without_fb1` 0x0ED400. 0, or `E_FAULT` (structure not in user RAM). |
| 23 | `int sys_con_set_cursor(int x, int y)` | Move the console cursor; 0, or `E_INVAL` outside 40x30. |
| 24 | `int sys_con_clear(void)` | Clear the console (page 0); 0. |
| 25 | `int sys_con_info(struct con_info *info)` | Columns, rows, cursor position; 0, or `E_FAULT` (structure not in user RAM). |
| 26 | `int sys_exec(const char *path, const char *args)` | Replace the program with another ELF (see 1.10). Returns only on failure: `E_NOENT`, `E_NOEXEC`, `E_NOMEM`, `E_NAMETOOLONG`, `E_NODEV`, `E_IO`, `E_FAULT`, `E_INVAL`. |
| 27 | `int sys_key_event(void)` | Next raw key event (bits 7..0 set-2 scan code, `KEY_RELEASE` bit 8, `KEY_EXTENDED` bit 9), or -1. |
| 28 | `void sys_sleep_ms(uint32_t ms)` | Busy-wait `ms` milliseconds: `ms * 60 / 1000` whole frames (50 on PAL), then a calibrated loop for the rest. |
| 29 | `int sys_set_audio_cb(void *cb)` | Install `void cb(void)` to be called once per video line (15.7 kHz) from the HSYNC interrupt, in supervisor mode at level 4. The callback owns `IO_AUDIO` (it should write one pattern per call); the audio stream is not played while it is installed, and `sys_audio_start` fails. `NULL` removes it. Returns 0. |
| 30 | `uint8_t *sys_audio_start(unsigned bufsize)` | Open the audio stream: pattern bytes played one per video line (15,707 Hz NTSC, 15,625 PAL) from the BIOS's `AUDIO_NBUF` = 3 buffers of `AUDIO_BUF_MAX` = 512 bytes, of which the program fills `bufsize` (1..512) each. Drops anything queued, sets the output to the idle pattern 0xAA and returns the first buffer to fill; nothing plays until it is handed in. Non-blocking. NULL if `bufsize` is out of range or an audio callback is installed. |
| 31 | `uint8_t *sys_audio_next(const uint8_t *filled, int block)` | Queue the filled buffer (the one last returned) for playing and return the next free buffer. If none is free: `block` != 0 waits, with interrupts running, until the playing buffer finishes (this paces the program to the audio rate); `block` = 0 returns NULL, and the handed buffer is still queued. `filled` = NULL hands nothing in and only asks for a free buffer. NULL also if the stream is not open or `filled` is not the buffer the program holds. Turns the line interrupt on when a buffer is queued with nothing playing. |
| 32 | `int sys_audio_last(const uint8_t *filled, int block)` | Queue the final buffer (`filled` may be NULL) and close the stream; `block` != 0 waits until everything queued has played. After the last byte the BIOS writes the idle pattern 0xAA and switches the line interrupt off. 0, or `E_INVAL` if the stream is not open. |

Any other number returns `E_NOSYS`. The constants and structures (`O_*`, `SEEK_*`, `ATTR_*`,
`VIDEO_STD_*`, `KEY_*`, `AUDIO_BUF_MAX`, `AUDIO_NBUF`, `E_*`, `struct video_info`, `struct mem_info`, `struct con_info`,
`struct dirent`) are in `generated/syscalls.h`.

| code | value | meaning here |
|---|---|---|
| `E_NOENT` | -2 | no such file or directory |
| `E_IO` | -5 | card error (the next call re-initialises the card) |
| `E_NOEXEC` | -8 | not a loadable ELF |
| `E_BADF` | -9 | bad descriptor or wrong direction |
| `E_NOMEM` | -12 | ELF does not fit the user region |
| `E_FAULT` | -14 | pointer outside user RAM |
| `E_NODEV` | -19 | no card, or no FAT volume on it |
| `E_INVAL` | -22 | bad argument |
| `E_NFILE` | -23 | too many open files or directories |
| `E_NOSPC` | -28 | card or directory full |
| `E_NAMETOOLONG` | -36 | not an 8.3 name |
| `E_NOSYS` | -38 | no such system call |

`E_ACCES`, `E_EXIST`, `E_NOTDIR`, `E_ISDIR` and `E_ROFS` are defined by the ABI but the
current BIOS does not return them in practice (`E_NOSPC` also stands for FatFs's "access
denied", which includes writing a read-only file).

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

`fn` runs on the BIOS's supervisor stack (about 8 KB below 0x004000), so keep it small. GCC
warns ("array subscript 0 is outside array bounds") about pointers to addresses this close to
zero; `--param=min-pagesize=0` in `CFLAGS` silences it.

### Write-only registers and the BIOS's shadows

INTEN, SDIO, GPIO, AUDIO and the output half of USERIO cannot be read back (they read as 0:
the CPLD had no room for the read path). The BIOS keeps private copies (`inten_shadow`,
`gpio_shadow`, `sdio_shadow` in `fw/`; they are not part of the ABI) and rewrites the
registers from them:

| register | when the BIOS writes it while your program runs | value |
|---|---|---|
| INTEN | `sys_set_irq_handler`, `sys_set_audio_cb`, `sys_audio_start`, `sys_audio_next`/`sys_audio_last` (when they queue a buffer while none is playing), `sys_exit`, `sys_exec`; and the level-4 handler itself when the stream runs out of buffers | `INTEN_VBLANK \| INTEN_PS2 \| INTEN_DUART` (0x000D), plus `INTEN_HSYNC` while an audio callback or level-4 hook is installed or an audio buffer is playing or queued; the level-4 handler writes `inten_shadow` without `INTEN_HSYNC` |
| GPIO | `sys_exit`/`sys_exec` (and the monitor's `led`) | its shadow with LED0, LED1 cleared: normally `GPIO_PS2CLK \| GPIO_PS2DAT` (0x0003) |
| AUDIO | the level-4 handler once per line while a stream buffer is playing; `sys_audio_start`, `sys_audio_last` (with nothing queued), `sys_exit`/`sys_exec` (and the monitor's `beep`) | the next byte of the playing buffer; the idle pattern 0xAA at `sys_audio_start`, at the end of the stream and at exit (after an underrun the last pattern stays). No shadow is kept |
| SDIO | every card access (file, directory, raw block and exec calls) | its shadow: CS high (deselected), CLK low between transfers |
| USERIO | never | reset value 0x000F |

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
handler's own write) it also **rewrites vector 28** at 0x000070, pointing it at whichever of
its two level-4 handlers fits ([2.7](#what-the-bios-installs)). A level-4 handler of your own
on that vector is therefore replaced by any of those calls; install it after the last of
them, as in [2.7](#three-ways-to-use-an-interrupt).

### What `sys_exit` restores, and what it does not

`sys_exit` (and `sys_exec`, before loading) closes files, stops the audio stream (dropping
queued buffers) and writes the idle pattern 0xAA to AUDIO, removes hooks and the audio
callback, rewrites INTEN (and with it vector 28, which points at the BIOS's fast level-4
handler again), clears the LEDs, and selects display page 0. It does **not**
restore: the other exception vectors you replaced, DUART registers (IMR, ACR, the counter/timer, channel
B), the NTSC/PAL soft switch, USERIO, or the flash. A replaced vector that points into your
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
| 0xE00000-0xEFFFFF | ROM (`ROM_BASE`, 1 MB flash window) | read; writes go to the flash chips | 4 clocks, any alignment | 2x SST39SF040, even bytes in U4 (D15..D8), odd in U5 (D7..D0). BIOS image 0xE00000-0xE1FFFF (`ROM_IMAGE_SIZE` 128 KB, word sum zero); 0xE20000-0xEFFFFF erased (0xFF) and free. |
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
| 0x000400-0x003FFF | `SYSDATA` | BIOS `.data`/`.bss` (0x1C70 bytes, to 0x002070, including the three 512-byte audio buffers), supervisor stack (about 8 KB) down from `SYS_STACK_TOP` 0x004000 |
| 0x004000-0x0DA7FF | `USER` | program load region (858 KB); user stack down from `USER_STACK_TOP` 0x0DA800 |
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
ties it low (NTSC only, there is no PAL jumper on the board). The BIOS selects PAL at boot only
when PALJMP reads 1. Do not select the PAL table on an NTSC board: the encoder will not lock to
312 lines.

## 2.6 The I/O registers

### CPLD2 registers (`IO_BASE` 0xF00000)

Eight 16-bit registers selected by A1..A3; A4..A15 are not decoded, so the block repeats every
16 bytes through 0xF0FFFF. Word access only.

| offset | address | name | access | reset | |
|---|---|---|---|---|---|
| 0x0 | 0xF00000 | `IO_INTSTAT` | read; write 1 to clear flags | flags 0 | interrupt flags and live pin levels |
| 0x2 | 0xF00002 | `IO_INTEN` | write only (reads 0) | 0x0000 | interrupt enables |
| 0x4 | 0xF00004 | `IO_SDIO` | write only (reads 0) | 0x0001 | SD card pins |
| 0x6 | 0xF00006 | `IO_GPIO` | write only (reads 0) | 0x0003 | PS/2 drive, LEDs |
| 0x8 | 0xF00008 | `IO_PS2DATA` | read | 0x0000 | last PS/2 byte |
| 0xA | 0xF0000A | `IO_AUDIO` | write only (reads 0) | 0x0000 | PDM audio shifter (spec rev 5) |
| 0xC | 0xF0000C | `IO_USERIO` | bits 3..0 write only, 11..8 read | 0x000F | user pins |
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
| 8 | `INTSTAT_SDMISO` | live | SD card MISO |
| 9 | `INTSTAT_PS2CLK` | live | PS/2 clock line |
| 10 | `INTSTAT_PS2DAT` | live | PS/2 data line |
| 12 | `INTSTAT_PALJMP` | live | 1 = board strapped for PAL; 0 on rev B4 |
| 13 | `INTSTAT_VSYNCPIN` | live | 1 during the vertical sync lines |
| 14 | `INTSTAT_DEPIN` | live | 1 during visible fetch slots |
| 15 | `INTSTAT_HSYNCPIN` | live | 1 during horizontal sync |

Writing INTSTAT clears exactly the W1C flags written as 1 (VBLANK, HSYNC, PS2ERR); other bits
are ignored. If an event sets a flag in the same clock as a write clears it, the set wins.

**INTEN** (0xF00002), write only, reset 0x0000. A flag whose enable is set drives the 68000's
IPL lines at the flag's level; the CPLD encodes the highest such level.

| bit | name | level |
|---|---|---|
| 0 | `INTEN_VBLANK` | 1 |
| 1 | `INTEN_HSYNC` | 4 |
| 2 | `INTEN_PS2` | 2 |
| 3 | `INTEN_DUART` | 3 |

**SDIO** (0xF00004), write only, reset 0x0001 (card deselected, CLK low, MOSI low: SPI mode 0
idle). The bits are the pin levels.

| bit | name | |
|---|---|---|
| 0 | `SDIO_CS` | chip select, 0 = card selected |
| 1 | `SDIO_CLK` | SPI clock |
| 2 | `SDIO_MOSI` | data to the card (MISO is `INTSTAT_SDMISO`) |

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

**USERIO** (0xF0000C), reset 0x000F: four open-drain pins with pull-ups on the expansion
header. Bits 3..0 (`USERIO_OUT_MASK`, write only): 1 releases the pin, 0 pulls it low. Bits
11..8 (`USERIO_IN_MASK`, read only): the pin levels.

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
| 3 | DUART IRQ pin | 27 | 0x06C | `INTEN_DUART` | remove the cause in the 68681 (read RBx, stop the counter, mask in IMR, ...) |
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
| 1 | clear VBLANK, `ticks++`, call the level-1 hook |
| 2 | read INTSTAT and PS2DATA; on PS2ERR clear it and count an error, else decode the byte into the character and key-event queues; call the level-2 hook |
| 3 | read the DUART ISR; if RXRDYA, pop RBA into the 512-byte serial ring while SRA.RXRDY (counting overruns, clearing OE); call the level-3 hook |
| 4 | `irq_hsync.S`, assembler, two handlers. `irq_hsync_fast` (no callback, no level-4 hook): clear HSYNC, write the next byte of the playing stream buffer to AUDIO; at the end of a buffer release it and take the next queued one, or, with none queued, clear `INTEN_HSYNC` (through `inten_shadow`), writing the idle pattern 0xAA first if the stream was closed with `sys_audio_last`. `irq_hsync_slow`: clear HSYNC, call the audio callback if one is installed, otherwise run the same stream step in C (leaving the interrupt on while a hook is installed), then call the level-4 hook |
| 7 | through the exception path: print registers, enter the monitor |

The BIOS points vector 28 at one handler or the other: whenever it recomputes INTEN
(`audio_update_mode()`, called from `irq_update_inten()`), the RAM vector at 0x000070 gets
`irq_hsync_slow` if an audio callback or a level-4 hook is installed and `irq_hsync_fast`
otherwise, so the fast handler carries no test for either. The fast handler is the
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
   `sys_ticks`, `sys_wait_vblank` and `sys_sleep_ms` (chain to the old handler or count ticks
   yourself); replacing level 2 or 3 starves the keyboard or serial input of the BIOS, and at
   level 3 your handler must drain the receiver (or clear RXRDYA from IMR), or the level-sensitive
   DUART request never goes away.
3. **Poll the flag** with the interrupt disabled, as in [2.5](#25-video-timing-and-finding-the-beam).

For HSYNC, the tidy way to get the enable bit set and kept consistent with the BIOS is a
dummy level-4 hook **first** and your vector **second**: installing the hook makes the BIOS
set `INTEN_HSYNC` and also rewrite vector 28 (to its slow handler, see above), so a vector
written before the hook would be lost. Once your vector is in, the BIOS never calls the hook,
because your vector bypasses its handler. Removing the hook clears the enable again and
points vector 28 back at the BIOS's fast handler (unless a BIOS stream is still playing; do
not mix the two). While your vector is installed, make no call that recomputes INTEN
(`sys_set_irq_handler` for any level, `sys_set_audio_cb`, the audio stream calls), or
install the vector again after it.

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
    sys_set_irq_handler(4, ignore);             /* 1: the BIOS sets INTEN.HSYNC (and vector 28) */
    *vec = hsync_isr;                           /* 2: then our handler on vector 28 */
    uint32_t busy = spin_one_second();
    sys_set_irq_handler(4, NULL);               /* BIOS clears INTEN.HSYNC, rewrites vector 28 */
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

The pin is a 5 V CMOS output of CPLD2. The analogue chain is unchanged from earlier
revisions (`docs/architecture.md` section 5, `kicad/board.py`):

```
AUDIO pin ---[1k]---+---[10k]---+---[10 uF]---> 3.5 mm jack, tip and ring (mono on both)
(shifter bit 7)     |           |
                 [10 nF]      [2.2k]
                    |           |
                   GND         GND
```

The 1 kΩ/10 nF low-pass has its corner at about 15.9 kHz, the divider scales the 5 V swing to
about 0.8 V peak to peak, and the capacitor removes the DC. The source impedance is about
1.8 kΩ: feed it to an amplifier or a line input rather than headphones.

* Nothing above about 7.8 kHz (half the 15.7 kHz sample rate) can be reproduced.
* Samples have 7 bits. The sigma-delta modulator pushes its quantisation noise up in
  frequency, but with only eight bits per sample it cannot push it far: the parts at 31 and
  63 kHz are filtered out, but the noise between 8 and 16 kHz, and the images of the signal
  mirrored about the line rate (a 1 kHz tone has a faint partner at 14.7 kHz), pass a filter
  whose corner is at 16 kHz. **Expect some hiss.** An external low-pass at 7-8 kHz, or an
  amplifier with little treble, removes most of it.
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
  (`sys_getchar` sees serial input only).
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

### Pins

The card is in SPI mode on `IO_SDIO`: bit 0 `SDIO_CS` (0 selects the card), bit 1 `SDIO_CLK`,
bit 2 `SDIO_MOSI`; MISO is read in `INTSTAT_SDMISO` (bit 8). The card runs from its own 3.3 V
regulator; CS, CLK and MOSI go through resistor dividers, MISO directly into a TTL-threshold
CPLD input. There is no card-detect or write-protect switch: "no card" shows up as no answer to
CMD0.

### Bit banging

SPI mode 0: put the bit on MOSI with CLK low, raise CLK (the card samples), read MISO, lower
CLK (the card presents its next bit). That is three I/O cycles per bit at the very least (two
writes and a read, 12-18 clocks), plus the work of assembling the byte:

```c
/* one byte both ways; cs_bits = 0 to talk to the card, SDIO_CS to idle it */
static uint8_t spi_xfer(uint8_t out, uint16_t cs_bits)
{
    uint8_t in = 0;
    for (int i = 0; i < 8; i++, out <<= 1) {
        uint16_t v = cs_bits | ((out & 0x80) ? SDIO_MOSI : 0);
        IO_SDIO = v;                            /* data with CLK low */
        IO_SDIO = v | SDIO_CLK;                 /* rising edge: card samples */
        in = (uint8_t)((in << 1) | ((IO_INTSTAT >> INTSTAT_SDMISO_BIT) & 1));
    }
    IO_SDIO = cs_bits;                          /* CLK low: card shifts out its next bit */
    return in;
}
```

The BIOS's C version of this loop (fully unrolled, in `fw/sd.c`) takes about 50 clocks per bit;
its receive-only path (`sdhw_read` in `fw/sdspi.S`: MOSI held high, two writes, a read, `btst`,
`sne`, `add`, `sub`) is 46 or 48 clocks per bit plus 18 per byte by the 68000 timing tables,
about 17 ms per 512-byte block plus command overhead. *Measured* in the emulator: 32 raw
blocks with `sys_sd_read_block` in 34 frames (about 29 KB/s), and 27 KB/s reading a file through
FatFs. The board adds the I/O alignment penalty.

### The BIOS's card protocol (`fw/sd.c`)

Initialisation: slow clock (about 400 kHz or less, delay loops around each edge); 80 clocks
with CS high; CMD0 (GO_IDLE, up to 8 tries, expecting R1 = 0x01); CMD8 with 0x1AA (a v2 card
echoes the pattern in R7, a v1 card rejects it as an illegal command); CMD55 + ACMD41 (HCS
set for v2 cards) repeated until the idle bit clears, about 1 s at most, falling back to CMD1
for cards that reject ACMD41; CMD58 to read the OCR (CCS bit 30 set = SDHC/SDXC, block
addressed); CMD16 512 for byte-addressed cards; then full speed, CMD9 (CSD: capacity) and CMD10
(CID). Every command carries a correct CRC7; data CRC16 is not checked (CMD59 is never sent).
One idle byte precedes each command, R1 is awaited for up to 8 bytes, and after each command
CS goes high followed by 8 clocks to release MISO.

Blocks: CMD17 for a read (wait up to about 160 ms for the `FE` start token; an error token is
a byte with the top nibble 0), 512 data bytes, 2 CRC bytes; CMD24 for a write (one idle byte,
`FE`, 512 bytes, 2 dummy CRC bytes, data response `xxx00101` = accepted, then MISO low while
the card is busy, up to about 500 ms). Multi-block commands are not used. Byte-addressed cards
take `lba * 512` as the address and are limited to 2 GB.

### The filesystem

FatFs R0.15 (`fw/fatfs/`) with `FF_USE_LFN 0` (8.3 names; long names show their aliases),
`FF_FS_RPATH 0` (no current directory), one volume, `FF_FS_TINY` (one shared 512-byte
sector window), code page 437, no RTC (files are stamped 2026-10-20), no `mkfs`. The volume is
mounted on first use; a disk error drops the mount, so the next call re-initialises the card.
8 files (descriptors 3..10) and 4 directory handles. FAT32 needs a volume of at least 65,525
clusters (64 MB with `mkimage.sh`); FAT12/16 cards work too.

### Coexisting with the BIOS

If you drive SDIO yourself, leave it as the BIOS expects between your transfers: CS high, CLK
low. The BIOS rebuilds every SDIO write from its own shadow and keeps believing the card is
initialised; if you reset or reinitialise the card, do it the same way (same addressing mode)
or the BIOS's next access fails once with `E_IO` and it starts over. FatFs caches a sector (and
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
(disabled).

38,400 baud is the top of the 68681's fixed table: CSR code 0xC is 38.4k in set 1 (ACR bit 7
= 0); other set-1 codes include 0xB = 9600 and 0x8 = 2400. In set 2 (ACR bit 7 = 1) code 0xC
means 19.2k, so **keep ACR bit 7 at 0** whenever you write ACR (it is write-only; the BIOS's
value is 0x00).

**Transmit** is polled: `serial_putc` waits for `SRA_TXRDY` and writes TBA, with interrupts
enabled. `sys_serial_putc` sends a raw byte this way; the console calls add `\r` before `\n`.
You can write TBA yourself (poll TXRDY first).

**Receive** is interrupt driven: the level-3 handler moves bytes from the 3-byte FIFO into a
512-byte ring, which `sys_serial_getc` and `sys_getchar` read. Polling SRA/RBA yourself races
with that handler; to own the receiver, write IMR without `IMR_RXRDYA` and put
`IMR_RXRDYA` back before exiting (IMR is write-only and `sys_exit` does not restore it).

### Channel B

Channel B's TXDB and RXDB are on the expansion header J8 (pins 13 and 15, 5 V TTL levels; even
pins are ground). Nothing uses it. Setting it up and sending (tested in the emulator with
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

### The counter/timer as a second timer

The 68681's 16-bit counter/timer, clocked from the 3.6864 MHz crystal (or crystal/16), can
interrupt at level 3 at a rate of your choosing. In timer mode the CTR bit sets once every
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
the machine hangs in the level-3 interrupt. (The ACR mode values are from the MC68681 data
sheet; the spec only describes ACR bit 7.)

### Access cost

A DUART access waits for the 68681's own DTACK, 2-3 of its clocks after chip select. That is
irrelevant at 38,400 baud (one character every 260 us, 3,125 clocks) but makes the DUART a poor
place to poll in a tight loop.

## 2.12 The flash ROM

### Layout and reading

The 1 MB window at 0xE00000 is two SST39SF040 chips side by side: U4 holds the even bytes
(D15..D8), U5 the odd bytes (D7..D0); chip byte address N is CPU word address `ROM_BASE + 2N`.
Reads take 4 clocks at any alignment and never wait for a slot, so the flash is the fastest
memory on the board for code and read-only data. The BIOS occupies 0xE00000-0xE1FFFF (about
39,700 of its 131,072 bytes are used; FatFs is roughly 12 KB of that); the other 896 KB are
erased (0xFF) and available.

### Programming

Commands are JEDEC sequences, issued as **word writes with the byte repeated in both halves**
so that both chips see them at once (`flash` section of the spec; constants in `hw.h`):

| operation | sequence |
|---|---|
| program a word | 0xAAAA -> `FLASH_CMD1` (0xE0AAAA), 0x5555 -> `FLASH_CMD2` (0xE05554), 0xA0A0 -> `FLASH_CMD1`, data -> address; poll until a read of the address returns the data (at most `FLASH_T_BYTE_PROGRAM_US` = 20 us) |
| erase the 8 KB sector containing X | 0xAAAA -> CMD1, 0x5555 -> CMD2, 0x8080 -> CMD1, 0xAAAA -> CMD1, 0x5555 -> CMD2, 0x3030 -> X; poll until X reads 0xFFFF (at most `FLASH_T_SECTOR_ERASE_MS` = 25 ms) |
| read IDs | 0xAAAA -> CMD1, 0x5555 -> CMD2, 0x9090 -> CMD1; `ROM_BASE` reads 0xBFBF (`FLASH_MFR_ID` in both bytes), `ROM_BASE + 2` reads 0xB7B7 (`FLASH_DEV_ID`); 0xF0F0 anywhere returns to read mode |

A sector is 4 KB per chip, `FLASH_SECTOR_BYTES` = 8 KB in the CPU's view, so the window has 128
sectors; the BIOS image is sectors 0-15. Programming can only clear bits: erase first. While a
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

/* prog_data = 0x1234; super_call(flash_program_word, ROM_BASE + 0x20000); */
```

In the emulator this reads IDs `bfbf/b7b7`, and the word at 0xE20000 changes from 0xFFFF to
0x1234. The NMI button must not be pressed while a command is in progress (its handler is in the
flash). Never erase or program sectors 0-15 from a program: a damaged BIOS image means
reprogramming the chips in an external programmer.

### The monitor commands

* `flashid` prints the two ID words (`bfbf/b7b7` = two SST39SF040s).
* `flash FILE` reads a 128 KB BIOS image (`fw/build/rom.bin`, any 8.3 name) from the card into
  RAM at `USER_BASE` (clobbering user RAM), checks its size and zero word sum, asks `y/N`, then
  hands over to a routine copied into RAM (`fw/flashram.S`) which, with interrupts masked,
  erases, programs and verifies the sixteen 8 KB sectors (a `.` per sector on the serial port)
  and jumps through the new image's reset vectors. On a timeout or verify mismatch it prints the
  sector and stops the CPU. Do not press NMI or reset while it runs.
* `reboot` jumps through the reset vectors at `ROM_BASE` (SSP and PC) without resetting the
  glue: the way to start a ROM image that was just written.

## 2.13 LEDs, user pins and the expansion header

* `GPIO_LED0`, `GPIO_LED1`: the BIOS uses them as POST codes (LED0 = POST started, both = RAM
  test, LED1 = C initialisation, alternating = RAM failure) and leaves both off; `sys_exit`
  turns them off; the monitor's `led n` sets them.
* `IO_USERIO`: four open-drain pins UIO0-UIO3 with pull-ups, nothing else attached; the BIOS
  never writes the register. Write 0 in bit n to pull UIOn low, 1 to release it; read the levels
  in bits 11..8.
* Expansion header J8 (2x8): pin 1 +5 V, 3 +3.3 V, 5 UIO0, 7 UIO1, 9 UIO2, 11 UIO3, 13 TXDB,
  15 RXDB; every even pin is ground.

## 2.14 Running without the BIOS

### What the BIOS leaves when your program starts

* Vectors at 0x000000-0x0003FF as described in 2.7 (vector 28 on the BIOS's fast level-4
  handler); BIOS data at 0x000400-0x00206F; supervisor stack below 0x004000.
* INTEN = 0x000D (VBLANK, PS/2, DUART); GPIO = 0x0003; AUDIO = 0xAA (the idle pattern; no
  audio stream open); SDIO = CS high, CLK low.
* DUART channel A at 38400 8N1 with IMR = RXRDYA; channel B and the counter/timer idle.
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
   shadows.
5. Lower the mask to the level you want.

What depends on what, if you still want parts of the BIOS:

| BIOS service | needs |
|---|---|
| any system call | vector 47 intact, BIOS data 0x000400-0x003FFF intact, room on the supervisor stack |
| files, raw blocks, `sys_exec` | the above; works with interrupts masked (the SD driver polls) |
| console output | the above; page 0 is drawn on and DUART channel A is written |
| `sys_getchar`, `sys_pollchar`, `sys_key_event`, `sys_serial_getc` | the level-2 and level-3 BIOS handlers (vectors 26, 27, enables on, mask below 2) |
| `sys_ticks`, `sys_wait_vblank`, `sys_sleep_ms` | the level-1 handler (vector 25, enable on, mask 0); the waits spin forever otherwise |
| `sys_audio_start`, `sys_audio_next`, `sys_audio_last` | the level-4 handler (vector 28 as the BIOS sets it, mask below 4), INTEN written only through the BIOS, and the BIOS data area, which holds the buffers; otherwise nothing plays and a blocking `sys_audio_next` or `sys_audio_last` waits for ever |
| `sys_exit` | returns to the monitor with a fresh supervisor stack and mask 0: vectors and DUART state you changed stay changed |

What you lose by taking it all: the console, keyboard decoding, the serial receive ring, the
tick counter, FAT file access (unless you keep the BIOS data area and call it), and the crash
reports. What you gain: the whole 1 MB of RAM except what you choose to keep (even
0x000400-0x003FFF if you will never call the BIOS again), all interrupt levels, and no
per-interrupt overhead you did not write.

There is no clean way back from a full takeover other than the reset button (the POST takes
about 4 s). A program that wants to return to the monitor must restore the vectors, INTEN, GPIO
(PS/2 lines released), the DUART's IMR, ACR and counter/timer, page 0 and NTSC timing, leave
the BIOS data area intact, and then call `sys_exit`.

### Other ways in

* **S-records**: send an S-record file over the serial port at the monitor prompt; data is
  accepted only for 0x004000-0x0FFFFF; `go [addr]` starts it in user mode with `argc` 0. This
  needs no SD card.
* **A different ROM**: the reset vectors always come from `ROM_BASE`, so code that must run
  before the BIOS has to replace the BIOS image (`flash`, or an external programmer with the
  even/odd split images). A broken image can only be recovered with the external programmer.
  The free part of the flash can hold code and data for a program to call or read, but it is
  never started by the reset sequence.

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
| console character (serial-bound) | about 3,125 (260 us) |
| SD block read, BIOS | about 17 ms |
| single-precision soft-float multiply-add, *measured* | about 3,400 |

At 12,000,000 clocks per second: 15,707 lines and 59.95 frames per second (15,625 and 50.08
on PAL).

## Known discrepancies

Places where the repository disagrees with itself, found while writing this guide. The guide
follows the code. (The eleven items listed for spec revision 4 have all been fixed.)

1. `hdl/README.md`'s note on the PDM shifter says the last bit period of an NTSC line "is 7
   slots short". In `hdl/cpld2_io.v` the slot counter restarts at the HSYNC edge and rotates on
   every 24th slot, so the seven timed rotations come at slots 24..168 of the 191-slot line and
   the eighth bit lasts 23 slots: one slot (4 clocks) short, as the emulator's model
   (`emu/README.md`) and [2.8](#the-shifter) say.
2. The spec's description of `audio_last` (syscall 32) ends "Exiting the program does the
   same without waiting", which reads as if buffers still queued at exit play out. The BIOS
   (`user_cleanup` in `fw/syscall.c`, calling `audio_stop`) drops them and writes the idle
   pattern at once, as [1.10](#110-exiting-and-chaining) says. The two agree that exit does
   not wait.
