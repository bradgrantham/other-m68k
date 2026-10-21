# other-m68k Programmer's Guide

This guide describes **board rev B4, spec revision 4** (`"revision": 4` in `spec/board.json`,
`BOARD_REVISION` in `generated/hw.h`) and the BIOS/monitor in `fw/` as it stands in this
repository. Every address, bit and syscall name below comes from `spec/board.json` or from the
headers generated from it (`generated/hw.h`, `generated/syscalls.h`, `generated/hw.inc`).
Where the spec, the firmware and the other documents disagree, the text says so; the list is
collected in [Known discrepancies](#known-discrepancies) at the end.

## Preface

### The machine

| | |
|---|---|
| CPU | MC68000 at 12 MHz (24 MHz oscillator divided by 2 in CPLD1) |
| RAM | 1 MB SRAM at 0x000000 (2x AS6C4008, word wide), shared with the video fetch |
| ROM | 1 MB flash window at 0xE00000 (2x SST39SF040); the BIOS image is the first 128 KB, the rest is erased and free |
| Video | 320x240, one byte per pixel (RGB332), two pages in RAM, composite out through an AD724 (NTSC on rev B4) |
| Glue | two ATF1508AS CPLDs: CPLD1 = decode, DTACK, SRAM address mux, video timing, soft switches; CPLD2 = I/O registers, pixel shifter, interrupt encoder, PS/2 receiver, SD/GPIO pins |
| I/O | 68681 DUART (channel A = console, 38400 8N1; channel B on the expansion header), PS/2 keyboard, bit-banged microSD (SPI mode, FAT via FatFs in the BIOS), 1-bit audio, two LEDs, four open-drain user pins, NMI button |
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

`libsys.a` holds the syscall stubs and `newlib_shim.c` (the `read`/`write`/`sbrk`/`open`...
layer newlib calls). `user.ld` places everything in one load region at `USER_BASE`
(0x004000), entry point `_start`.

**Adding a program.** Put `foo.c` in `sdk/examples/`, add `foo` to the `EXAMPLES` line in
`sdk/Makefile`, and run `make -C sdk`. You get `sdk/build/foo.elf` (what the monitor loads)
and `sdk/build/foo.srec` (for upload over the serial port). Keep the base name at most 8
characters: the card only stores 8.3 names.

**Building outside the tree.** The same thing by hand, with `R` the repository and the SDK
already built:

```
X=~/x-tools/m68k-unknown-elf/bin/m68k-unknown-elf-
${X}gcc -m68000 -Os -Wall -I$R/generated -nostartfiles -T $R/sdk/user.ld \
    -L$R/sdk -L$R/sdk/build -Wl,--gc-sections -o foo.elf \
    $R/sdk/build/crt0.o foo.c -Wl,--start-group -lc -lsys -Wl,--end-group -lgcc
```

(`-L$R/sdk` lets the linker find `../generated/user.ld`, which `user.ld` includes.)

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
`--dump-mem ADDR LEN`. See `emu/README.md` for the full list.

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
* `time()`, `gettimeofday()`, `clock()`, `times()`: count from boot, using the VBLANK tick
  counter. Beware: `clock()` returns 60 Hz ticks while newlib's `CLOCKS_PER_SEC` is 1000, so
  `clock() / CLOCKS_PER_SEC` is wrong; use `sys_ticks()`.
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

`sys_sleep_ms` counts a frame as `1000 / 60` = 16 ms (integer), so on NTSC it runs about 3 %
long: `sys_sleep_ms(1000)` waits 62 frames, 1.03 s. For game timing, count frames with
`sys_ticks()` or `sys_wait_vblank()`; for elapsed seconds divide ticks by 60 (50 on PAL,
`vi.standard` tells you which). Everything keeps running during these calls: interrupts are
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

`errno` caveat: the SDK shim maps most ABI errors to `errno`, but not `E_NAMETOOLONG`,
`E_FAULT` or `E_NODEV` (no card or no FAT volume); those come back as `ENOSYS`
("Function not implemented"). If you need to tell them apart, call `sys_open` directly.

Raw blocks: `sys_sd_read_block(lba, buf)` and `sys_sd_write_block(lba, buf)` transfer one
512-byte block at an absolute card address. They bypass the filesystem; do not write blocks
that belong to the FAT volume while files are in use.

## 1.8 Sound the easy way

The board has a 1-bit audio output: one GPIO bit (`GPIO_AUDIO`) drives the 3.5 mm jack
through a filter. You make sound by flipping that bit at audio rates.

`sys_set_audio_cb(fn)` installs a function that the BIOS calls on every horizontal sync
interrupt: 15,707 times a second on NTSC (12 MHz / 764 clocks per line), 15,625 on PAL.
`sys_set_audio_cb(NULL)` removes it (exiting does too). The callback must write the GPIO
register itself:

```c
IO_GPIO = GPIO_PS2CLK | GPIO_PS2DAT | (on ? GPIO_AUDIO : 0);
```

`IO_GPIO` is write-only and also holds the keyboard lines and the LEDs, so always write
`GPIO_PS2CLK | GPIO_PS2DAT` (keyboard lines released; writing 0 there blocks the keyboard)
plus the LED bits you want. The BIOS's own copy of the register is restored when the program
exits.

**The callback must be short.** It runs in supervisor mode at interrupt level 4, the highest
maskable level, so while it runs the keyboard, serial and VBLANK interrupts wait. It runs on
every one of the 764-clock NTSC lines, so every clock it takes is taken 15,707 times a second:

* The BIOS interrupt routine around an *empty* callback costs about 278 of the 764 clocks in
  each line: 36 % of the CPU, before your callback does anything. (This figure is the 68000's
  nominal timing of `irq_hsync` and `jsr`/`rts`, and is what the emulator *measures*.)
* With a C callback that takes a byte from a ring buffer and writes the audio bit, the total is
  about 470 clocks per line, leaving about 38 % of the CPU for the program.

So sampled audio is expensive on this machine. Keep the callback to "load one prepared value,
write it": precompute in the main loop, never compute in the callback, and remove the callback
when nothing is playing. Do not make system calls from it. Part 2 shows how to cut the cost to
about 170 clocks by replacing the interrupt vector.

```c
/* tone.c: a square wave from the audio callback, then a 1-bit sigma-delta sine fed
 * through a ring buffer. */
#include <stdint.h>
#include <stdio.h>
#include "hw.h"
#include "syscalls.h"

#define GPIO_IDLE (GPIO_PS2CLK | GPIO_PS2DAT)  /* keyboard lines released, LEDs off */

static uint32_t line_hz;                        /* audio callback rate */

/* ---- 1: square wave by phase accumulator ---------------------------------------- */
static volatile uint16_t phase, step;

static void square_cb(void)
{
    phase += step;
    IO_GPIO = (phase & 0x8000) ? (GPIO_IDLE | GPIO_AUDIO) : GPIO_IDLE;
}

static void play_note(unsigned hz, unsigned ms)
{
    step = (uint16_t)((hz * 65536UL) / line_hz);
    sys_sleep_ms(ms);
}

/* ---- 2: ring buffer of ready-made GPIO values ------------------------------------- */
static volatile uint8_t ring[256];
static volatile uint8_t head, tail;             /* uint8_t: wraps at 256 by itself */

static void ring_cb(void)
{
    uint8_t t = tail;
    if (t != head) {                            /* underrun: leave the pin alone */
        IO_GPIO = ring[t];
        tail = t + 1;
    }
}

static const int8_t sine16[16] = { 0, 48, 89, 117, 127, 117, 89, 48,
                                   0, -48, -89, -117, -127, -117, -89, -48 };

int main(void)
{
    struct video_info vi;
    sys_video_info(&vi);
    line_hz = vi.standard == VIDEO_STD_PAL ? CPU_CLOCK_HZ / VIDEO_PAL_LINE_CLOCKS
                                           : CPU_CLOCK_HZ / VIDEO_NTSC_LINE_CLOCKS;

    printf("scale\n");
    sys_set_audio_cb(square_cb);
    static const uint16_t notes[] = { 262, 294, 330, 349, 392, 440, 494, 523 };
    for (unsigned i = 0; i < 8; i++) play_note(notes[i], 250);
    sys_set_audio_cb(0);

    printf("sigma-delta sine, ~980 Hz\n");
    sys_set_audio_cb(ring_cb);
    int err = 0;
    uint32_t end = sys_ticks() + 60, n = 0;
    while (sys_ticks() < end) {
        while ((uint8_t)(head + 1) != tail) {   /* fill until full */
            int s = sine16[n++ & 15];           /* -127..127 */
            err += s;                           /* first-order sigma-delta */
            uint8_t bit = err >= 0;
            err -= bit ? 127 : -127;
            ring[head] = bit ? (GPIO_IDLE | GPIO_AUDIO) : GPIO_IDLE;
            head++;
        }
    }
    sys_set_audio_cb(0);                        /* sys_exit would also do this */
    return 0;
}
```

The ring buffer needs no locking because the main program only writes `head` and the callback
only writes `tail`, and each is a single byte. What to expect: square-wave tones and
chip-music are clean; sampled sound through 1 bit at 15.7 kHz is noisy and band-limited
(nothing above about 7.8 kHz), good enough for effects and speech.

## 1.9 Interrupt hooks

`sys_set_irq_handler(level, fn)` installs `void fn(void)` to be called from the BIOS's
interrupt routine for that level, after the BIOS has done its own work; it returns the previous
hook, and `NULL` removes it. Hooks are removed when the program exits.

| level | interrupt | rate | BIOS work before the hook |
|---|---|---|---|
| 1 | VBLANK | once per frame | acknowledge, `ticks++` |
| 2 | PS/2 byte received | per keyboard byte | read the byte, decode it into the queues |
| 3 | DUART | per serial event | drain the serial receive FIFO |
| 4 | HSYNC | 15.7 kHz | acknowledge, call the audio callback; installing a level-4 hook also turns the interrupt on |

Any other level gives `E_INVAL` (returned as the pointer value). The hook runs in
supervisor mode on the BIOS's stack, with the processor's interrupt mask at the hook's level:
higher levels can still interrupt it (a VBLANK hook can be interrupted by the keyboard, the
serial port and HSYNC), lower levels wait. Rules for hooks: keep them short, use `volatile`
variables to share data with the program, do not call `sys_*` functions (the BIOS is not
re-entrant), and do not use much stack (the supervisor stack is about 9.8 KB, shared with the
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
  `sys_exit(n)`. The BIOS closes the program's files, removes its hooks and audio callback,
  restores its interrupt enables, turns off the audio bit and the LEDs, selects page 0, prints
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
loops. Errors are negative `E_*` values from `syscalls.h`. Every pointer argument checked by the
BIOS must point into 0x004000-0x0FFFFF, otherwise `E_FAULT`.

| # | prototype | description, results and errors |
|---|---|---|
| 0 | `void sys_exit(int code)` | End the program: close its files, drop hooks and audio callback, restore interrupt enables, audio and LEDs off, select page 0, print `[exit code]`, return to the monitor. Does not return. |
| 1 | `int sys_putchar(int c)` | Write a character to the console (video page 0 and serial). Returns `c & 0xFF`. Blocks while the serial transmitter is busy. |
| 2 | `int sys_getchar(void)` | Wait for a character from the keyboard (first) or serial port; returns it (0..255). |
| 3 | `int sys_pollchar(void)` | Next character, or -1 if none. |
| 4 | `int sys_puts(const char *s)` | Write a NUL-terminated string to the console, no newline added. Returns the number of characters. (The pointer is not range-checked.) |
| 5 | `int sys_serial_putc(int c)` | Write a raw byte to the serial port only (no `\n` translation). Returns `c & 0xFF`. |
| 6 | `int sys_serial_getc(void)` | Next byte from the serial receive buffer (512 bytes, filled by interrupt), or -1. |
| 7 | `uint32_t sys_ticks(void)` | Frames since boot (VBLANK count; 59.95 Hz NTSC, 50.08 Hz PAL). |
| 8 | `void sys_wait_vblank(void)` | Wait until the tick count changes (the next VBLANK interrupt, at the start of vertical sync). |
| 9 | `int sys_video_info(struct video_info *info)` | Fill in width, height, bpp, bytes per line, both page addresses, the current page and the video standard. 0, or `E_INVAL` for `NULL`. |
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
| 22 | `int sys_mem_info(struct mem_info *info)` | `user_start` 0x004000, `user_end` 0x0DA800, `user_end_without_fb1` 0x0ED400. 0, or `E_INVAL` for `NULL`. |
| 23 | `int sys_con_set_cursor(int x, int y)` | Move the console cursor; 0, or `E_INVAL` outside 40x30. |
| 24 | `int sys_con_clear(void)` | Clear the console (page 0); 0. |
| 25 | `int sys_con_info(struct con_info *info)` | Columns, rows, cursor position; 0, or `E_INVAL` for `NULL`. |
| 26 | `int sys_exec(const char *path, const char *args)` | Replace the program with another ELF (see 1.10). Returns only on failure: `E_NOENT`, `E_NOEXEC`, `E_NOMEM`, `E_NAMETOOLONG`, `E_NODEV`, `E_IO`, `E_FAULT`, `E_INVAL`. |
| 27 | `int sys_key_event(void)` | Next raw key event (bits 7..0 set-2 scan code, `KEY_RELEASE` bit 8, `KEY_EXTENDED` bit 9), or -1. |
| 28 | `void sys_sleep_ms(uint32_t ms)` | Busy-wait about `ms` milliseconds (frames, then a loop; ~3 % long on NTSC). |
| 29 | `int sys_set_audio_cb(void *cb)` | Install `void cb(void)` to be called at the line rate (15.7 kHz) from the HSYNC interrupt, in supervisor mode at level 4; `NULL` removes it. Returns 0. |

Any other number returns `E_NOSYS`. The constants and structures (`O_*`, `SEEK_*`, `ATTR_*`,
`VIDEO_STD_*`, `KEY_*`, `E_*`, `struct video_info`, `struct mem_info`, `struct con_info`,
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

`fn` runs on the BIOS's supervisor stack (about 9.8 KB below 0x004000), so keep it small. GCC
warns ("array subscript 0 is outside array bounds") about pointers to addresses this close to
zero; `--param=min-pagesize=0` in `CFLAGS` silences it.

### Write-only registers and the BIOS's shadows

INTEN, SDIO, GPIO and the output half of USERIO cannot be read back (they read as 0: the CPLD
had no room for the read path). The BIOS keeps private copies (`inten_shadow`,
`gpio_shadow`, `sdio_shadow` in `fw/`; they are not part of the ABI) and rewrites the
registers from them:

| register | when the BIOS writes it while your program runs | value |
|---|---|---|
| INTEN | `sys_set_irq_handler`, `sys_set_audio_cb`, `sys_exit`, `sys_exec` | `INTEN_VBLANK \| INTEN_PS2 \| INTEN_DUART` (0x000D), plus `INTEN_HSYNC` while an audio callback or level-4 hook is installed |
| GPIO | `sys_exit`/`sys_exec` (and the monitor's `beep`, `led`) | its shadow with AUDIO, LED0, LED1 cleared: normally `GPIO_PS2CLK \| GPIO_PS2DAT` (0x0003) |
| SDIO | every card access (file, directory, raw block and exec calls) | its shadow: CS high (deselected), CLK low between transfers |
| USERIO | never | reset value 0x000F |

So: keep your own copy of anything you write, always write the whole register, assume the
BIOS's next write replaces yours, and restore INTEN through the BIOS when you are done (any
`sys_set_irq_handler` call recomputes and rewrites it). In GPIO always keep `GPIO_PS2CLK` and
`GPIO_PS2DAT` set unless you are deliberately driving the keyboard lines: a 0 there pulls the
line low and stops the keyboard.

### What `sys_exit` restores, and what it does not

`sys_exit` (and `sys_exec`, before loading) closes files, removes hooks and the audio callback,
rewrites INTEN, clears AUDIO and the LEDs, and selects display page 0. It does **not**
restore: exception vectors you replaced, DUART registers (IMR, ACR, the counter/timer, channel
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
| 0x000400-0x003FFF | `SYSDATA` | BIOS `.data`/`.bss` (to about 0x001994), supervisor stack down from `SYS_STACK_TOP` 0x004000 |
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
level-4 hook is installed), a program can poll:

```c
/* raster.c: find the beam with INTSTAT polling. Needs INTEN.HSYNC off (the BIOS leaves it
 * off unless an audio callback or level-4 hook is installed). */
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
| 0x6 | 0xF00006 | `IO_GPIO` | write only (reads 0) | 0x0003 | PS/2 drive, audio, LEDs |
| 0x8 | 0xF00008 | `IO_PS2DATA` | read | 0x0000 | last PS/2 byte |
| 0xA | 0xF0000A | none | | | reads 0, writes ignored |
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
| 2 | `GPIO_AUDIO` | 1-bit audio output |
| 3 | `GPIO_LED0` | LED 0, 1 = on |
| 4 | `GPIO_LED1` | LED 1, 1 = on |

**PS2DATA** (0xF00008), read: bits 7..0 (`PS2DATA_DATA_MASK`) are the last byte received.
Reading clears `INTSTAT_PS2RDY`; the byte itself stays until the next frame completes.

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
level-4 hook is installed. The handlers:

| level | BIOS handler |
|---|---|
| 1 | clear VBLANK, `ticks++`, call the level-1 hook |
| 2 | read INTSTAT and PS2DATA; on PS2ERR clear it and count an error, else decode the byte into the character and key-event queues; call the level-2 hook |
| 3 | read the DUART ISR; if RXRDYA, pop RBA into the 512-byte serial ring while SRA.RXRDY (counting overruns, clearing OE); call the level-3 hook |
| 4 | clear HSYNC, call the audio callback, call the level-4 hook |
| 7 | through the exception path: print registers, enter the monitor |

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

For HSYNC, the tidy way to get the enable bit set and kept consistent with the BIOS is to
install your vector first and then a dummy level-4 hook: the BIOS sets `INTEN_HSYNC` and will
never call the hook, because your vector bypasses its handler. Removing the hook clears the
enable again.

```c
/* lean.c: take over the HSYNC autovector with a C interrupt handler and play
 * pre-rendered GPIO words; measure what is left of the CPU. */
#include <stdint.h>
#include <stdio.h>
#include "hw.h"
#include "syscalls.h"

#define GPIO_IDLE (GPIO_PS2CLK | GPIO_PS2DAT)
#define NWORDS 32768                            /* ~2 s at 15.7 kHz */

static uint16_t snd[NWORDS];
static const uint16_t *volatile snd_ptr;
static const uint16_t *snd_end;

__attribute__((interrupt_handler)) static void hsync_isr(void)
{
    const uint16_t *p = snd_ptr;
    IO_INTSTAT = INTSTAT_HSYNC;                 /* acknowledge first (W1C) */
    if (p != snd_end) {
        IO_GPIO = *p++;
        snd_ptr = p;
    }
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
    for (unsigned i = 0; i < NWORDS; i++)       /* 491 Hz square wave */
        snd[i] = GPIO_IDLE | ((i & 16) ? GPIO_AUDIO : 0);
    sys_set_irq_handler(1, vbl);
    uint32_t idle = spin_one_second();

    void (*volatile *vec)(void) = (void (*volatile *)(void))IRQ_HSYNC_VECTOR_ADDR;
    void (*bios_isr)(void) = *vec;
    snd_end = snd + NWORDS;
    snd_ptr = snd;
    *vec = hsync_isr;                           /* 1: our handler on vector 28 */
    sys_set_irq_handler(4, ignore);             /* 2: lets the BIOS set INTEN.HSYNC */
    uint32_t busy = spin_one_second();
    sys_set_irq_handler(4, NULL);               /* BIOS clears INTEN.HSYNC */
    *vec = bios_isr;                            /* put the BIOS handler back */

    printf("CPU left: %lu%% (%lu clocks per line)\n", (unsigned long)(busy * 100 / idle),
           (unsigned long)((idle - busy) * VIDEO_NTSC_LINE_CLOCKS / idle));
    return 0;
}
```

GCC's `interrupt_handler` attribute saves exactly the registers the function uses and returns
with `rte`. This one compiles to 12 instructions and prints `CPU left: 70% (226 clocks per
line)` in the emulator. The same job written in assembler, keeping the pointer in memory and
using one register:

```asm
hsync_isr:
        move.w  #INTSTAT_HSYNC_MASK,IO_INTSTAT_ADDR   | acknowledge
        move.l  %a0,-(%sp)
        movea.l snd_ptr,%a0
        move.w  (%a0)+,IO_GPIO_ADDR
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
| BIOS level-4 handler + empty audio callback | 278 | 36 % |
| BIOS handler + C callback writing one byte from a buffer, *measured* | about 390 | 51 % |
| BIOS handler + C callback dequeuing from a ring with checks | about 470 | 62 % |

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
* Do not call the BIOS. The supervisor stack is shared and about 9.8 KB.
* Keep a level-4 handler well under 764 clocks, or lines merge (the flag is sticky, so a late
  handler sees one event for two lines) and the lower levels starve.
* Worst-case latency is the longest instruction (a `divs` can take about 160 clocks), plus any
  higher-level handler that is running, plus time spent at a raised mask.

## 2.8 Audio

`GPIO_AUDIO` (GPIO bit 2) is a 5 V CMOS output of CPLD2. The analogue chain
(`docs/architecture.md` section 5, `kicad/board.py`):

```
GPIO.AUDIO ---[1k]---+---[10k]---+---[10 uF]---> 3.5 mm jack, tip and ring (mono on both)
                     |           |
                  [10 nF]      [2.2k]
                     |           |
                    GND         GND
```

The 1 kΩ/10 nF low-pass has its corner at about 15.9 kHz, the divider scales the 5 V swing to
about 0.8 V peak to peak, and the capacitor removes the DC. The source impedance is about
1.8 kΩ: feed it to an amplifier or a line input rather than headphones.

The GPIO register is write-only and shared with the keyboard lines and LEDs: every write must
carry `GPIO_PS2CLK | GPIO_PS2DAT` and your LED bits (see [2.1](#write-only-registers-and-the-bioss-shadows)).

**Beeps by toggling.** Any loop that flips the bit at an audio rate makes a square wave. The
monitor's `beep` toggles every 500 us for 200 ms (1 kHz) with a calibrated delay loop.
Interrupts lengthen software delays; for steady pitch use a timer interrupt.

**Sampled playback from HSYNC.** The line interrupt is a 15.7 kHz sample clock (15,625 Hz on
PAL). With one bit per sample the choices are square-wave synthesis (phase accumulators; several
voices can be mixed by alternating them or by OR/XOR), or 1-bit pulse-density (sigma-delta)
encoding of PCM done in the main loop. Pulse-width modulation inside a line would need the
handler to wait within the line, which costs far too much. Store prepared GPIO words (fastest
handler, 2 bytes per sample, 31 KB per second of sound) or bit-packed samples (8 times smaller,
a few more clocks per interrupt).

**What to expect.** Nothing above about 7.8 kHz (half the sample rate); a strong 15.7 kHz
component that the 15.9 kHz filter barely attenuates; the noise of 1-bit quantisation, pushed
up in frequency by sigma-delta but with little room to push it into. Square-wave tones are
clean; speech and effects are recognisable; music from samples sounds rough.

**Other sample rates.** The 68681's counter/timer can interrupt at another rate on level 3;
see [2.11](#211-serial-the-68681-duart).

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
`sne`, `add`, `sub`) is 46-48 clocks per bit by the 68000 timing tables (the source comment says
44), about 16 ms per 512-byte block plus command overhead. *Measured* in the emulator: 32 raw
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
memory on the board for code and read-only data. The BIOS occupies 0xE00000-0xE1FFFF; the
other 896 KB are erased (0xFF) and available.

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

* Vectors at 0x000000-0x0003FF as described in 2.7; BIOS data at 0x000400-0x001994; supervisor
  stack below 0x004000.
* INTEN = 0x000D (VBLANK, PS/2, DUART); GPIO = 0x0003; SDIO = CS high, CLK low.
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
| BIOS HSYNC handler with an empty audio callback | 278 per line, 36 % of the CPU |
| BIOS handler + C ring-buffer audio callback | about 470 per line, 62 % |
| own HSYNC vector, C `interrupt_handler` / assembler, *measured* | 226 / 168 per line |
| `memcpy` of a page, *measured* | about 430,000 (2.15 frames) |
| `memset` of a page, *measured* | about 420,000 (2.1 frames) |
| `movem.l` fill of a page (2.4), *measured* | about 180,000 (0.9 frame) |
| console character (serial-bound) | about 3,125 (260 us) |
| SD block read, BIOS | about 16-18 ms |
| single-precision soft-float multiply-add, *measured* | about 3,400 |

At 12,000,000 clocks per second: 15,707 lines and 59.95 frames per second (15,625 and 50.08
on PAL).

## Known discrepancies

Places where the repository disagrees with itself, found while writing this guide. The guide
follows the code.

1. `README.md` describes the ROM as "2x M27C512 EPROM"; the spec, `fw/` and `kicad/` use two
   SST39SF040 flash chips (1 MB window, 128 KB image).
2. The comments in `fw/duart.S` and `fw/duart.h`, the boot description in `fw/README.md` and
   `fw/STATUS.md` say the BIOS writes ACR = 0x80 (baud-rate set 2). The spec and the code write
   `SERIAL_ACR_VALUE` = 0x00 (set 1), which is what makes CSR code 0xC mean 38,400; in set 2 it
   would be 19,200.
3. `fw/README.md` says bad user pointers give `E_INVAL` ("the ABI has no E_FAULT") and that
   non-8.3 names give `E_INVAL`; `fw/STATUS.md` repeats the latter. The code returns `E_FAULT`
   and `E_NAMETOOLONG`, as the spec defines.
4. `sdk/README.md` says `errno` is `EINVAL` for a non-8.3 name or a bad pointer and `EIO` for
   no card; `sdk/newlib_shim.c` has no mapping for `E_NAMETOOLONG`, `E_FAULT` or `E_NODEV`,
   so those set `errno` to `ENOSYS`.
5. `times()`/`clock()` in the shim count 60 Hz ticks, but newlib's `CLOCKS_PER_SEC` is 1000;
   `gettimeofday()` divides by 60 even on PAL, where the BIOS ticks at 50 Hz.
6. The spec's IO region text says address bits A5..A15 are ignored; CPLD2 decodes only A1..A3
   (`hdl/cpld2_io.v`), so A4 is ignored too and the registers repeat every 16 bytes, as the
   emulator and `hdl/README.md` say.
7. `hdl/README.md` still mentions SERDATA, SERRX and SERRXOVR (removed in spec revision 2) and
   gives register reset values "0x0000 / 0x0003 / 0x0007 / 0x000F"; SDIO resets to 0x0001 in
   the spec and in `cpld2_io.v`. Its note that `architecture.md` gives 6 clocks for an
   S0-at-E2 cycle is stale (it now says 7).
8. The spec's `set_irq_handler` text says hooks are called "with interrupts masked"; the BIOS
   handlers run at the interrupt's own level, so higher levels can still interrupt a hook.
9. `sys_sleep_ms` computes the frame length as `1000 / ticks_per_sec` in integer arithmetic
   (16 ms on NTSC), so sleeps run about 3 % long.
10. The comment in `fw/sdspi.S` gives 44 clocks per received bit; the 68000 timing tables give
    46 or 48 for that loop body (`btst #imm,dn` is 10 clocks, `sne` 4 or 6).
11. `sys_video_info`, `sys_mem_info` and `sys_con_info` reject only `NULL`; unlike the other
    pointer-taking calls they do not check that the structure lies in user RAM. `sys_puts`
    does not check its pointer at all.
