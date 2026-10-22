# fw/ -- other-m68k BIOS / monitor ROM (stage 2)

256 KB ROM image (the first 256 KB of the 1 MB window of the two SST39SF040 flash chips; the
other 768 KB are drive C:): power-on self test, 68681 DUART serial (38400 8N1), 40x30 video
console, PS/2 keyboard, exception reporting, a wall clock, machine monitor with S-record
upload, scripts and file commands, two FAT volumes through FatFs R0.15 with long names and
directories (C:, the FAT12 volume in the flash, and D:, the SD card on the native 4-bit bus,
bit-banged), an ELF loader (`run`, `sys_exec`, a DOS-like search path) and the TRAP #15
system call interface used by user programs (see `../sdk/`), including the file, directory
and clock calls.

## Build

```
make -C fw            # build/rom.elf, rom.bin (262144 bytes: what `flash` programs), rom.lst, rom.map
make flash            # (top level, part of `make all`) fw/build/c.img: the C: volume image;
                      # fw/build/flash.img: the 1 MB window, rom.bin + c.img at 256 KB;
                      # fw/build/rom_even.bin (U4, D15..D8) and rom_odd.bin (U5, D7..D0): its
                      # byte lanes, the two 512 KB chip images for the TL866
make run              # (top level) the emulator on flash.img, --rom-save flash.img: C: persists
make -C fw test       # host unit tests (590 checks): printf, queue, srec, ps2, console, hexdump,
                      # the SD driver against the emulator's card model, ELF parser
                      # (+ ../sdk/build/hello.elf), the clock's calendar arithmetic, FatFs + fd
                      # table on both drives: D: a FAT32 image, C: a FAT12 image in a simulated
                      # flash array (images made with mkfs.vfat/mcopy; skipped if those are missing)
make -C fw clean
```

Toolchain: `~/x-tools/m68k-unknown-elf` (override `XTOOLS=/path/to/bin`). The BIOS is
freestanding (`-ffreestanding -nostdlib`, own string/printf), `-m68000 -Os -Wall -Wextra`,
linked with `-lgcc` only. All addresses, register bits, syscall numbers and the dispatcher
come from `../generated/` (`hw.h`, `hw.inc`, `syscalls.h`, `bios_syscalls.h`,
`syscall_dispatch.inc`, `memory.ld`); nothing is duplicated here. `fatfs/` is vendored
FatFs R0.15 (ff.c/ff.h/ffunicode.c/diskio.h unmodified, `ffconf.h` configured, `LICENSE.txt` kept).

`mkrom.py rom.bin` pads the image to `ROM_IMAGE_SIZE` (256 KB) and sets its last word so that
the 16-bit big-endian sum of all 131072 words is zero. The POST sums the image (a long
counter of `ROM_IMAGE_SIZE / 2` words) and expects 0; the C: volume above it is not part of
the sum. `mkrom.py --split flash.img rom_even.bin rom_odd.bin rom.bin c.img` builds the 1 MB
window (rom.bin, 0xFF, the volume image at `FLASH_VOL_OFFSET`) and splits it into the two
`FLASH_CHIP_SIZE` byte-lane images (sizes from `generated/hw.h`).

`tools/mkflashvol.sh IMAGE file...` makes the C: image the way `format C:` lays out the
volume: an 0xFF-filled 768 KB file (erased flash), `mkfs.vfat -F 12 -s 8 -f 2 -r 224 -R 16`
(4 KB clusters, two FATs, 224 root entries, 16 reserved sectors so the data area starts on
an 8 KB erase sector), then `mcopy` of the files, upper-cased. The top-level `make flash`
puts `flashfs/AUTOEXEC.CMD`, `flashfs/README.TXT` and the SDK examples on it, each stripped
with `m68k-unknown-elf-strip` into `fw/build/cfiles/NAME.ELF` (43-62 KB each instead of
54-75 KB; the loader needs only the program headers): about 475 KB of the 768 KB.

## Programming the ROM

* **First time, or after a failed update**: TL866 (minipro) with device `SST39SF040`,
  `fw/build/rom_even.bin` into U4 (even bytes, D15..D8) and `rom_odd.bin` into U5 (odd bytes,
  D7..D0). They hold the whole window, so this installs the BIOS and the initial C: volume.
* **Afterwards, in system**: copy `rom.bin` to the SD card (or C:), then in the monitor
  `flashid` shows the chip IDs (`bfbf/b7b7` = two SST39SF040) and `flash D:ROM.BIN` reads the
  image into RAM, checks its size and word sum, asks `y/N`, and hands over to the routine in
  BIOS RAM (`flashram.S`, linked into `.data`, so crt0 copies it there at boot) that erases,
  programs and verifies the thirty-two 8 KB sectors of the image (one `.` per sector on the
  serial port) and jumps through the new image's reset vectors: the new ROM's POST banner
  is the confirmation. Only the first `ROM_IMAGE_SIZE` bytes are rewritten: C: survives a
  BIOS update. `reboot` does that jump on its own (SP and PC from `ROM_BASE`).
  Interrupts are masked during the update and nothing runs from the flash, because the running
  ROM is being replaced under its own feet; **do not press the NMI button** during a flash (the
  vectors point into the ROM being rewritten). On an erase/program timeout or a verify mismatch
  the routine prints which sector failed and stops the CPU: reset and retry `flash` if the
  monitor still boots, otherwise program the chips in the TL866. `flash` stages the image at
  `USER_BASE` (256 KB, 0x8000-0x47FFF, inside the user region below both framebuffer pages),
  so it clobbers user RAM. The same `rom.bin` serves `flash` and, inside `flash.img`, the
  emulator and the TL866.

  Tested in the emulator (rev 9): `--rom flash.img --sd sd.img --type "flashid\nflash D:ROM.BIN\ny" --rom-save out.img`
  with `ROM.BIN` copied onto the card image: 32 dots and "image verified" on the serial port,
  the new banner, and `out.img` identical to `flash.img` byte for byte (the BIOS image rewritten
  with the same contents, the C: volume untouched).

Run in the emulator:

```
make run                                                        # window + PS/2 keyboard, C: kept
emu/build/m68k-emu --rom fw/build/flash.img --headless --frames 500 --type "help\n"
```

`--rom fw/build/flash.img` without `--rom-save` starts from the built C: every time;
`--rom fw/build/rom.bin` alone boots with C: unformatted (all 0xFF: `C:?> `, `format C:`).

The serial port is the emulator's stdin/stdout (`--serial-log FILE` keeps a copy;
`--serial-type "text\n"` injects serial input, `--type` types on the PS/2 keyboard). Only
the POST and the banner reach serial: after that the console is video only, so headless
runs read the screen with `--text-dump FILE` (the 40x30 text decoded from page 0 at exit).

With an SD card image as D: (see "SD card" below for making one):

```
make flash
S=/tmp/sdimg; mkdir -p $S
for p in hello plasma ls cat fstest; do cp sdk/build/$p.elf $S/$(echo $p | tr a-z A-Z).ELF; done
sh fw/test/mkimage.sh $S/sd.img 64 $S/*.ELF fw/test/data/README.TXT     # mkfs.vfat + mcopy
./emu/build/m68k-emu --rom fw/build/flash.img --sd $S/sd.img --headless --frames 1500 \
    --text-dump $S/screen.txt --type "sd\nD:\ndir\nrun hello.elf\nx\n" --type-delay 350 \
    --no-audio --no-stdin
```

`--type-delay 350` leaves the POST (~250 frames: the 1 MB RAM test) time to finish before typing starts; the
trailing `x` is the key `hello` waits for. Expected log (abridged; the stamps are the host
files' times, which mcopy keeps):

```
C:/> sd
SDHC/SDXC (block addressed), 4-bit bus,
RCA 1234
131072 blocks = 64 MB
CID mid 01 oid EM pnm M68K0 rev 1.0
sn 00006800 mdt 2024/10
CSD v2 400e00325b590000007f7f800a400051
C:/> D:
D:/> dir
CAT.ELF            61328 26-10-21 21:15
FSTEST.ELF         61548 26-10-21 21:15
...
README.TXT            33 26-10-21 21:15
6 entries, 310549 bytes
D:/> run hello.elf
run hello.elf: 008000-010c33 entry 008000
Hello from newlib on other-m68k!
...
press a key: got 0x78

[exit 0]
```

Without the `D:` and `run`, plain `hello` finds `C:/hello.ELF` on the search path.

Other things verified the same way: `run ls.elf /` (argv), `run cat.elf README.TXT nope.txt`
(newlib `fopen` -> `sys_open`, `ENOENT` for the missing file), `run fstest.elf exec ls.elf`
(creates and appends to `TEST.TXT` through stdio, reads it back with the raw calls, then
`sys_exec` chains into `ls.elf` with `argv = {"ls.elf", "from", "fstest"}`; the file is
readable with `mtype` on the host afterwards), `cat README.TXT`, `sd rd 0`, `run plasma.elf`,
and the error paths (`run nope.elf`, `run README.TXT`, `dir /x`). Card init plus mounting
plus loading the 35 KB `hello.elf` takes about 2.5 s of emulated time.

## Boot sequence and LED codes

| LEDs | meaning |
|---|---|
| LED0 on | reset reached `_start`; DUART programmed; ROM checksum running |
| LED0 + LED1 on | RAM test running (~4 s for 1 MB) |
| LED1 on | RAM passed; C initialisation (vectors, .data/.bss, console, interrupts) |
| both off | monitor idle / running normally |
| LED0 / LED1 alternating at ~2 Hz forever | RAM test failed (details on serial) |

`_start` masks interrupts, writes INTEN = 0 and the GPIO idle value (PS/2 lines released,
LED0 on), then programs DUART channel A from registers (`duart_reset_init` in `duart.S`):
IMR = 0; CRA = reset MR pointer, reset RX, reset TX, reset error status; ACR = 0x00 (baud
set 1, in which CSR code 0xC is 38.4k); MR1A = 0x13 (8 bits, no parity); MR2A = 0x07 (1 stop bit); CSRA = 0xCC (38400 RX and
TX); CRA = 0x05 (enable TX + RX). The data sheet requires the RX/TX reset commands before
ACR[7] is changed, and the pointer reset makes the first MRA write hit MR1A even on a warm
restart (`reset` command) where the DUART may not have seen a hardware reset. Only byte
accesses at the odd addresses in `hw.inc` are used.

Nothing in the POST uses RAM: the ROM checksum, the RAM test and the serial output run
from registers only (`post.S`, `duart.S`; `post_putc` polls SRA.TXRDY and writes TBA),
returning through address registers. Do not press the NMI button during the POST: the
vectors live in RAM, which is being tested.

POST serial output, 38400 8N1 (`SERIAL_BAUD`):

```
other-m68k POST
ROM sum 0000 OK          <- 16-bit word sum of the 256 KB BIOS image, must be 0000
RAM ... OK               <- one dot per pass: address-in-address, inverted address, byte lanes
```

A RAM failure prints `RAM FAIL @aaaaaaaa exp xxxxxxxx got yyyyyyyy` and blinks forever.
Pass 1 writes every long's address into it (address-line and aliasing faults), pass 2 the
inverted address (with pass 1 every cell has held both states of every bit), pass 3 writes
0x55 to even (D15..D8) and 0xAA to odd (D7..D0) bytes with byte moves and verifies as longs.
The 68000 drives a byte on both halves of the data bus, so a stuck WEU/WEL lets the first
write of a pair clobber the other byte and the second repair it: the lower half of RAM
writes even then odd (catches WEU stuck), the upper half odd then even (catches WEL stuck);
either shows up as `exp 55AA55AA got ...`. The three sweeps take about 50 M clocks, 4 s at
12 MHz.

After the POST the banner shows board name, firmware version, the ROM image size and its
stored checksum word, the size of C:, RAM size, ID register (0x6802 on the DUART board),
NTSC/PAL (PAL is selected with the soft switch when `INTSTAT.PALJMP` is set), the DUART baud
rate and the user RAM range. At the first prompt after reset the monitor runs
`C:/AUTOEXEC.CMD` (see "Scripts") unless an ESC is waiting in the keyboard queue: hold or
press ESC during the POST to skip it.

## Memory use

| range | use |
|---|---|
| 0x000000-0x0003FF | vector table, copied from ROM after the RAM test (programs may hook vectors) |
| 0x000400-0x004B80 | BIOS `.data` + `.bss`: the flash RAM routine (0.9 KB, `.data`), the 8 KB C: erase-sector cache, two FATFS objects (C: and D:, with their 512-byte windows), file table, input rings 512+256+128 B, console shadow text 1200 B, exception record, the script stack, two 512 B block buffers, three 512 B audio buffers |
| 0x004B80-0x008000 | supervisor stack, grows down from `SYS_STACK_TOP` (0x8000) |
| 0x008000-0x0DA7FF | user program (842 KB); USP starts at 0x0DA7FC |
| 0x0DA800-0x0ED3FF | framebuffer page 1 (user programs may use it as RAM if they do not double buffer) |
| 0x0ED400-0x0FFFFF | framebuffer page 0 = the console |

ROM use: about 59 KB of 256 KB (`make` prints the sizes: text ~59 KB, data ~0.9 KB, bss
~17.4 KB; `mkrom` prints the bytes used), of which FatFs with the long-name and code-page
437 tables is roughly 16 KB. BIOS `.data`+`.bss` is 0x4780 bytes in SYSDATA (31 KB,
0x400-0x7FFF; it ends at 0x4B80; the 8 KB erase-sector cache, the three 512-byte audio
buffers, the two FATFS objects, the flash routine, the script stack, 8 `FIL` and 4 `DIR`
slots and the monitor's block buffers are the largest items), leaving about 13 KB of
supervisor stack.

## Vectors and the overlay

The first 1 KB of ROM is the 256-entry vector table. Vector 0 = `SYS_STACK_TOP`, vector 1 =
`_start` in ROM (0xE0xxxx), so the very first instruction fetch clears the ROM overlay at
address 0. Autovectors 25-28 (VBLANK, PS2, DUART, HSYNC) and TRAP #15 (47) point at their
handlers; every other vector points at a 10-byte stub that pushes its number and jumps to
`exception_common`. The table is copied to RAM at 0 once RAM has passed; `go` programs can
install their own handlers by writing RAM vectors (they run in supervisor mode).

## Interrupts

| level | source | handler |
|---|---|---|
| 1 | VBLANK | `ticks++`, the wall clock (`clock_tick`, see "Clock"), user hook (`sys_set_irq_handler(1, fn)`) |
| 2 | PS/2 byte | set-2 decode to ASCII / escape sequences, raw event queue, user hook |
| 3 | DUART IRQ pin (RXRDYA unmasked) | drains the RX FIFO into a 512-byte ring (overruns counted, `info` shows them), user hook |
| 4 | HSYNC (15.7 kHz) | `irq_hsync.S`: the audio stream (see Audio); enabled while a buffer is playing or queued or an audio callback or level-4 hook is installed |
| 7 | NMI button | prints the registers and enters the monitor | With paddles being measured, IMR.IPC is also set and the level-3 handler feeds `joy_ipc`; the VBLANK handler calls `joy_vblank` every frame (a few register writes and a 20 us wait while paddles are on, nothing otherwise).

One exception to "interrupts stay enabled": each erase-sector write to C: (see "Drive C:")
runs with all interrupts masked for about 100 ms. The routine counts the `INTSTAT.VBLANK`
flags it sees and clears them, and `ticks` and the clock are advanced by that count
afterwards; serial receive bytes beyond the DUART's 3-byte FIFO are lost meanwhile and
audio repeats its last pattern.

## Audio

Spec rev 6: the board's audio pin is driven by an 8-bit rotating PDM shifter in CPLD2 that
is loaded by writing `IO_AUDIO` (offset 10) and shifts one bit out every 1/8 of a video
line, bit 7 first, repeating the byte until it is replaced. The BIOS streams pattern bytes,
one per video line (15707/s NTSC, 15625/s PAL), from buffers the program fills; it does no
per-sample work outside the interrupt. Turning samples into patterns is the program's job
(the SDK's `pdm_init`/`pdm_encode`, a first-order sigma-delta with a 64 KB table in user RAM).

* The BIOS owns `AUDIO_NBUF` = 3 buffers of `AUDIO_BUF_MAX` = 512 bytes (`audio.c`).
  `audio_start(bufsize)` (TRAP #15 number 30) opens the stream and returns the first buffer;
  `audio_next(filled, block)` (31) hands a filled buffer in and returns the next free one,
  blocking if asked until the handler has finished the one it is playing (which paces the
  program to the line rate; `block == 0` returns NULL instead and the handed buffer stays
  queued); `audio_last(filled, block)` (32) hands in the final buffer and, if asked, waits
  for the end. Buffers play in hand-in order through a 4-slot FIFO of indices; the producer
  writes `tail`, the handler writes `head`, and `INTEN.HSYNC` is on while a buffer is
  playing or queued.
* Two level-4 handlers in `irq_hsync.S`; `audio_update_mode()` points autovector 28 (RAM
  copy at 0x70) at one of them, so the common case has no mode test. The fast handler writes
  the next byte of the playing buffer to `IO_AUDIO`: 242 clocks of the 764 in a line (32 % of
  the CPU while sound plays; hand count in the source, `abench` measures 240.6). At the end
  of a buffer it releases it and takes the next queued one, or, with nothing queued, switches
  `INTEN.HSYNC` off through `inten_shadow`; an underrun holds the last pattern, the end of
  the stream (`audio_last`) writes the idle pattern 0xAA and closes it. The slow handler runs
  while `set_audio_cb` or a level-4 hook is installed: the callback, if any, owns `IO_AUDIO`
  and replaces the stream; otherwise `audio_isr_service()` (the C twin of the fast path)
  runs, then the hook.
* The monitor's `beep` writes 0xFF/0x00 patterns at 1 kHz for 200 ms (a 1-bit square wave).
  `audio_init` (boot) and `sys_exit` leave the shifter on the idle pattern; `sys_exit` also
  drops queued buffers.

## Serial port

Channel A of the MC68681 DUART at `DUART_BASE`, 38400 8N1, byte registers at odd
addresses (`generated/hw.h`: `DUART_SRA`, `DUART_TBA`, ...). Transmit is polled:
`serial_putc` waits for `SRA.TXRDY` (transmit holding register empty) and writes `TBA`;
nothing masks interrupts, so audio callbacks and the keyboard are unaffected by printing.
Receive is interrupt driven: `IMR = RXRDYA` (set in `serial_init`), the DUART's IRQ pin is
gated by `INTEN.DUART` to level 3, and `irq_duart` reads the DUART `ISR`, then pops `RBA`
while `SRA.RXRDY`, counting `SRA.OE` as an overrun (and issuing the "reset error status"
command 0x40 to clear it). The interrupt is level sensitive, so draining the 3-byte FIFO
is what releases it. `INTSTAT.DUARTIRQ` shows the live pin. Channel B is not used by the BIOS.

The BIOS uses channel A only for the POST report and the banner (`console_serial` in
`main.c`). After the banner the port is released (spec `serial.console`): `putchar_both`
writes video only, `input_pollchar` reads the keyboard only, and nothing in the BIOS
touches the port again except the monitor's `srec` command while it runs and the `flash`
command's RAM routine, which prints its progress dots and result on serial because the ROM
holding the console code is being rewritten. The RX interrupt
keeps filling the 512-byte ring, which `sys_serial_getc` and reads of `/dev/ttya` drain.
A program claims the port by `sys_open`ing `/dev/ttya` (no card needed; `fs.c` gives it a
descriptor slot): `read` blocks for one byte and returns what else is queued, `write`
sends raw bytes, `lseek` is `E_INVAL`. An operating system personality that wants a
serial terminal (CP/M-68K style) opens that and keeps the BIOS console on the screen.

INTEN, GPIO and AUDIO are write-only in CPLD2 (they read as 0); the BIOS keeps
`inten_shadow` and `gpio_shadow` and every write goes through `irq_update_inten` /
`gpio_write/set/clear`. SDIO reads back the six card lines, so the SD driver needs no shadow.
(USERIO was removed in spec revision 7.)

## SD card

Spec revision 7: the card sits on the native SD bus (not SPI), bit-banged through SDIO. The
six lines DAT3 (bit 0), CLK (1), CMD (2), DAT0 (3), DAT1 (4), DAT2 (5) are open-drain outputs
of CPLD2 with 10 k pull-ups to 3.3 V: writing 0 drives a line low, 1 releases it, and a read
returns the level of every line (host AND card), so the driver keeps no shadow. One clock is
three register accesses: the drive word with CLK low (the card changes CMD/DAT after the
falling edge), a read, the same word with CLK high (the card samples on the rising edge).

`sd4.S` holds the bit-banging: `sd4_cmd` (48-bit frame with CRC7, wait up to 128 clocks for
the response start bit, read 48 or 136 bits), `sd4_clocks`, `sd4_read512` (1024 nibbles on
DAT3..0 through two 64-byte lookup tables: 86.5 clocks per byte, 3.7 ms per block, measured
exactly that in the emulator), `sd4_write512` (112 clocks per byte) and `sd_crc16x4` (the
four per-line CRC16s of a block, 6.9 ms). `sd.c` does the framing, the state machine and the
waits. `sd_slow` (set during identification) inserts a delay around every edge in the command
paths to stay under the 400 kHz the spec requires until the card has an RCA; data phases run
at full speed. Interrupts stay enabled throughout: the card tolerates clock pauses.

`sd_init`: 80 idle clocks, CMD0 (no response), CMD8 0x1AA (R7; v1 cards do not answer),
CMD55 + ACMD41 with the voltage window and HCS for v2 until OCR bit 31 is set (~1 s limit;
a card that never answers CMD55 is "no card"), CCS from the OCR (SDHC = block addressing),
CMD2 (CID, R2), CMD3 (R6: RCA), then full speed, CMD9 (CSD, capacity for both CSD versions),
CMD7 (select), CMD55 + ACMD6 2 (4-bit bus), CMD16 512 for byte-addressed cards. Every
command carries a correct CRC7 and every R1/R2/R6/R7 CRC7 is checked (`SD_ERR_CRC`). Reads:
CMD17, wait for the DAT0 start bit (up to ~200 ms), 1024 nibbles, CRCs ignored. Writes:
CMD24, the four CRC16s, start bit, data, CRCs, end bit, then the CRC status token on DAT0
(010 accepted, 101 rejected = `SD_ERR_TOKEN`) and the busy wait (up to ~400 ms). Card status
error bits (OUT_OF_RANGE, ADDRESS_ERROR, ILLEGAL_COMMAND, COM_CRC_ERROR ...) map to
`SD_ERR_RANGE/CMD/CRC`. Errors are `SD_ERR_*` (`sd_strerror`), mapped to `E_IO`/`E_NODEV`/
`E_INVAL` at the syscall boundary. The `sd` monitor command re-initialises the card (dropping
D:'s mount and its open files and directories) and prints type, bus width, RCA, capacity,
CID and CSD; `sd rd <lba>` hex-dumps a block. Multi-block commands are not used.

The socket has no card-detect line, so the card is treated as fixed media: removing it
while powered is unsupported (open files lose their data, the FAT may be left
inconsistent). `sd` still re-initialises the card on request.

Measured in the emulator (`sdbench`, 300 reads / 60 writes through the syscalls): 5.05 ms per
block read (101 KB/s; the SPI driver took 17 ms), 13.3 ms per block write (data 4.9 ms, CRCs
6.9 ms, the emulated card is busy for only 16 clocks; real cards add their programming
time). `sdplay -pcm` streaming 8-bit PCM at the line rate from the card now has 57 % of the
CPU to spare (it fell 2-3 % short with SPI).

## Filesystem

Spec rev 9 (`filesystem` in `spec/board.json`): two volumes with DOS drive letters. C: is
the 768 KB FAT12 volume in the flash (FatFs physical drive 0, `flashdisk.c`, see "Drive
C:"), D: the SD card (drive 1, `sd.c`); `diskio.c` routes between them. FatFs R0.15
(`fatfs/`, `ffconf.h`): read/write, `FF_USE_LFN 1` with `FF_MAX_LFN 63` (static name
buffer; `ffunicode.c`, code page 437), `FF_FS_RPATH 2` (current directory, `f_chdir`,
`f_chdrive`, `f_getcwd`), `FF_VOLUMES 2` with `FF_STR_VOLUME_ID 1` and `FF_VOLUME_STRS
"C","D"`, `FF_USE_CHMOD 1` (for `f_utime`), `FF_USE_MKFS 1` (`format C:`), `FF_FS_NORTC 0`
with `get_fattime()` from the clock, `FF_FS_TINY 1` (all partial-sector traffic goes through
each volume's 512-byte window). The monitor's wildcards are its own case-insensitive glob,
not `f_findfirst`.

Paths are `[X:][/]dir/.../name`: without a drive letter they refer to the current drive,
without a leading slash to that drive's current directory, and `X:` alone names that
drive's current directory. Each volume keeps its own current directory and there is a
current drive, C: after reset. Names are long names of up to 63 characters (case kept,
matched case-insensitively), 8.3 aliases included; a longer or malformed name is
`E_NAMETOOLONG` ("bad name (max 63 chars)"). `/dev/ttya` is unchanged (see "Serial port").

`fs.c` registers both volumes in `fs_init`; each is mounted lazily on first use. It keeps a
table of 8 files (fd 3..10) and 4 directory handles (0..3) shared by both drives, maps
`FRESULT` to the ABI's `E_*` codes and implements `O_CREAT` (open-always), `O_CREAT|O_TRUNC`
(create-always), `O_TRUNC` alone (`f_truncate`) and `O_APPEND` (seek to the end before every
write). Besides open/close/read/write/lseek/size and opendir/readdir/closedir it has
`fs_stat`, `fs_unlink` (file or empty directory; `E_ACCES` for read-only, `E_EXIST` for a
non-empty directory), `fs_rename` (also moves within a volume), `fs_mkdir`, `fs_chdir` (a
bare `X:` switches the current drive), `fs_getcwd` (`X:/dir`), `fs_copy` (keeps the
source's stamp with `f_utime`; a failed copy deletes the partial destination), `fs_mkfs`
(C: only), `fs_sync` (writes back the C: cache) and `fs_close_all` (a program ended: closes
every slot and flushes the cache; `user_cleanup` calls it). `readdir`'s `struct dirent`
carries the long name in `name[64]`, `size`, `attr` and `mtime` (seconds since 1970 from
the FAT stamp, 0 if none). A disk error on D: or `sd` calls `fs_invalidate`, which forgets
the card state and drops only D:'s open files and directories; the next call re-initialises
and remounts. C: and `/dev/ttya` descriptors are not affected.

System calls 33-39 (spec rev 9) expose the rest: `unlink`, `rename`, `mkdir`, `chdir`,
`getcwd(len, buf)`, `time` and `set_time` (see `generated/syscalls.h`). `exec` takes an
explicit path (no search path; relative paths use the current directory).

## Drive C: (flash volume)

FAT12, 512-byte sectors, 1536 of them, 4 KB clusters, two FATs, 224 root entries, the data
area aligned to the 8 KB erase sector (`format C:` and `tools/mkflashvol.sh` make the same
layout), at `ROM_BASE + FLASH_VOL_OFFSET` (0xE40000). `flashdisk.c` reads sectors straight
from the ROM window. Writes go through one 8 KB erase-sector cache: a write loads the
enclosing erase sector into RAM and patches the 512-byte sector there (a sector that
compares equal does not dirty the cache); reads of a sector in the dirty cache come from
it. The cache is flushed when a write lands in another erase sector, on `CTRL_SYNC`
(`f_sync`, `f_close`), before every monitor prompt and before a program starts. FatFs
writes a file's data in order (16 sectors per erase sector, one cycle) and rewrites FAT and
directory sectors with mostly identical contents, so the cache coalesces well: copying
200 KB from D: to a freshly formatted C: took 32 cycles in the emulator, the format
included. `flashdisk_programs` counts the cycles (`info` shows it).

A flush is `flash_write_sector` (`flash.c`): op 2 of `flashram.S`, which runs from BIOS RAM
with interrupts masked, erases the 8 KB sector, programs its 4096 words (0xFFFF words
skipped) and verifies them; about 100 ms (erase up to 25 ms, each word program up to
20 us). While it polls it counts and clears the `INTSTAT.VBLANK` flags (`op->vblanks`);
`flash.c` adds them to `ticks` and the clock afterwards. Serial RX bytes beyond the DUART's
3-byte FIFO are lost during a flush and audio repeats its last pattern. The volume lives in
its own erase sectors above `ROM_IMAGE_SIZE`, so `flash` never touches it; the emulator's
`--rom-save` keeps it across runs.

## Clock

`clock.c`: seconds since 1970-01-01 00:00 local time (no zone), 0 until set (`date`,
`sys_set_time`; there is no battery, so typically from `AUTOEXEC.CMD`). The VBLANK ISR
calls `clock_tick`, which adds the exact frame length in CPU clocks (764 x 262 = 200,168
NTSC, 768 x 312 = 239,616 PAL) to an accumulator and counts a second each time it passes
12,000,000, so the clock does not drift with the 59.95/50.08 Hz frame rate. Calendar
conversion is H. Hinnant's days-from-civil / civil-from-days (1970-2106), plus FAT stamp
conversion both ways and parse/format of `YYYY-MM-DD HH:MM[:SS]`. `get_fattime()` stamps
files FatFs writes; with the clock unset they get 2026-01-01 00:00. Host-tested
(`test_clock` in `test/test_main.c`).

## Joystick and paddles (spec rev 11, board rev C3)

`joy.c`. The Atari port J12 hangs off the DUART's I/O pins: the five switches pull IP0..IP4
low (up, down, left, right, fire) against the 10 k pull-ups, so `joy_read` (`sys_joystick`)
is one register read, inverted and masked. The paddles are timed: each pot charges a 15 nF
capacitor, a 74HC14 gate pulls the up (A) or down (B) line low through a diode when it passes
the threshold, and a dual transistor on OP0 shorts both capacitors (OP0 high, the reset state
= shorted). `joy_paddles_set(1)` (the first `sys_paddles` call) runs the counter/timer free in
timer mode from X1/16 (230.4 kHz), enables change-of-state interrupts on IP0/IP1 (ACR 0x73,
IMR.IPC) and lets `irq_vblank` drive a measurement every frame: short the capacitors; the lines of the paddles that had
charged rise, the DUART reports the rise through the change interrupt once its 38.4 kHz
sampler has seen it, and that handler releases the capacitors and notes the count (if nothing
rose during a frame the next VBLANK releases directly). So the release follows the DUART's
own view of the discharge and the fall is always a change it can detect. When a line falls the level-3 handler
reads IPCR (which clears ISR.IPC) and the count; `joy_ipc` stores start - now for that paddle.
At the next VBLANK the results become the `struct paddles` a program reads: 0..1023 with 1023
= 12 ms (2764 counts), a valid bit per line that fell, the frame's tick. Off again at program
exit (`user_cleanup`), which also puts ACR and IMR back to the serial defaults; a program that
uses the counter/timer itself (guide 2.11) must leave the paddles alone. Cost (*measured* in the emulator): about 430 clocks a frame with no paddle (0.2 %), about
3,500 with two paddles (1.8 %: the rise and the two falls each cost a level-3 interrupt and
its DUART accesses); the DUART samples its inputs at 38.4 kHz (26..52 us to notice a fall) and the interrupt
latency behind the level-4 line handler adds up to ~20 us, so the resolution is about 8 bits.
Verified in the emulator (`--joy`, `--paddle`): `joytest` shows the held switches and the
paddle values within 0.5 % of the settings.

## ELF loader

`elf.c` parses ELF32 big-endian `ET_EXEC` for `EM_68K` with 32-byte program headers;
`exec.c` reads the headers (`exec_prepare`: every `PT_LOAD` must fit in
`[USER_BASE, USER_END)`, else `E_NOMEM`; a bad header is `E_NOEXEC`; the entry point must
lie in a loaded segment), then copies the file parts into place and zeroes `memsz - filesz`
(`exec_load`). Nothing is written to RAM until all headers have passed, so `sys_exec` can
report a bad file to the caller; an I/O error during the copy ends in the monitor.

Launch convention (`user_launch`, shared with `sdk/crt0.S`): the argument strings and the
NULL-terminated `argv` array are placed just below `USER_STACK_TOP`, then the user stack is
set up as for a call `_start(argc, argv)`: `USP -> {return address = user_exit_stub, argc,
argv}`. `argv[0]` is the path given to `run`/`sys_exec` (for a command found on the search
path, the path it was found under, e.g. `C:/hello.ELF`), the remaining words of the argument
string are split on blanks (at most 16 arguments). `go` uses the same path with `argc = 0`
and `argv[0] == NULL`. The return address makes a bare `rts` from the entry point a
`sys_exit(0)`. `run` sets the `go` default to the new entry point, but a newlib program
re-run with `go` finds its `.data` (stdio state) as the first run left it, so use `run`
again rather than `go` to restart a loaded program.

## Console

40 columns x 30 rows, 8x8 font (public-domain `font8x8_basic.h` from dhepper/font8x8),
white on dark blue, page 0. Handles `\n` (CR+LF), `\r`, `\b` (non-destructive), `\t`
(8 columns); scrolls at the bottom (a scroll is a 74 KB move, ~40 ms). Everything printed
on the console during the POST and the banner is also sent to the serial port with `\n`
-> `\r\n`; after the banner the console is video only (see "Serial"). Input is the
keyboard (ASCII); `sys_key_event` reads raw PS/2 events.

Keyboard: US layout, shift/caps/ctrl. Arrows -> `ESC [ A/B/C/D`, Home/End -> `ESC [ H/F`,
Ins/Del/PgUp/PgDn -> `ESC [ 2~/3~/5~/6~`, F1-F4 -> `ESC O P..S`, F5-F12 -> `ESC [ 15~ 17~
18~ 19~ 20~ 21~ 23~ 24~`.

## Exceptions

Any unexpected exception prints the vector number and name, PC, SR, D0-D7, A0-A7, SSP and
USP, and for bus/address errors the fault address, read/write and function code, then
restarts the monitor on a fresh stack. `regs` prints the record again. A crashing user
program therefore drops you back in the monitor with its registers on the screen.

## Monitor

All numbers are hex (`0x` optional). The line editor (`lineedit.c`) takes lines up to 127
characters: Left/Right, Home/End (also ^A/^E) move the cursor, Backspace and Delete (^D) edit
anywhere in the line, ^K kills to the end, ^U/^C the whole line, ^W the word before the cursor,
and a line may wrap over several screen rows. Up/Down browse the last 32 typed lines (a line
equal to the previous one is not stored twice; script lines are not recorded); `history`
lists them. Tab completes the word under the cursor: the first word against the built-in
commands and the `NAME.ELF`/`NAME.CMD` files on the search path (a name an earlier directory
also has is shown once), later words against the files in the directory the word names. One
Tab inserts the only match (a directory with a trailing `/`, a file with a trailing blank) or
the longest common prefix; when nothing more can be added, Tab lists the matches and redraws the
line. Arguments are split on blanks, and double quotes group a word (`copy "A long name.txt"
C:`); completion quotes a name that needs it. When a program is launched (`run` or the search
path), its unquoted arguments containing `*` or `?` are expanded against the directory they
name, each match becoming one word (quoted if it has a blank; a pattern with no match stays as
typed), up to 32 words in 511 bytes (`args.c`); the built-in commands handle their own
patterns. The prompt is the current drive and directory (`C:/> `, `D:/GAMES> `), or
`X:?> ` when the current drive cannot be mounted (no card, unformatted C:). Before every
prompt the monitor writes back the C: cache.

| command | action |
|---|---|
| `help` | command list |
| `md [addr] [len]` | dump memory, 8 bytes per line (continues from the last address; dumps over 256 bytes stop on a key) |
| `mb addr v...` / `mw addr v...` / `ml addr v...` | write bytes / words / longs |
| `fill addr len v` | fill bytes |
| `go [addr]` | run a program in user mode (default: the last S-record or `run` entry point, else 0x8000) |
| `regs` | registers of the last exception |
| `srec` | receive S-records on serial port A (ESC on the keyboard or the port aborts) |
| `reset` | RESET instruction, then the reset path |
| `led n` | LEDs (bit 0 = LED0, bit 1 = LED1) |
| `page n` | display framebuffer page 0 or 1 |
| `ticks` | VBLANK counter |
| `ramtest` | destructive test of 0x8000-0xFFFFF (user RAM and both framebuffers) |
| `beep` | 1 kHz for 200 ms (0xFF/0x00 patterns to `IO_AUDIO`) |
| `cls` | clear the console |
| `info` | banner, tick rate, INTSTAT, INTEN and GPIO shadows, DUART SRA, RX overruns, PS/2 errors, `go` default, current directory, clock, C: erase/program cycles since boot (and whether the cache is dirty) |
| `sd` | re-initialise the card; print type, bus width, RCA, capacity, CID, CSD (also drops D:'s mount and open files) |
| `sd rd lba` | hex dump of one 512-byte block |
| `C:` / `D:` | make that drive current |
| `cd [path]` | no argument: print the current drive and directory; else change a directory (`cd D:/GAMES` sets D:'s without switching drives, `cd D:` switches) |
| `dir [path\|pattern]` (`ls`) | list a directory, or the entries matching `*`/`?` in the last component (`dir D:*.BMP`): long name (a name over 16 characters gets its own line), size or `<DIR>`, stamp `yy-mm-dd hh:mm`, then entries and bytes; ESC stops |
| `cat file` (`type`) | print a file (CRs dropped; ESC stops) |
| `copy src dst` (`cp`) | copy files; `src` may hold `*`/`?` in its last component (then `dst` must be a directory or a drive); `dst` is a file, a directory or a drive (`copy D:*.ELF C:`). Stamps are kept; a failed copy (full volume) leaves no partial file; ESC stops between files |
| `del file` (`rm`) | delete files (wildcards ok; not directories) |
| `ren old new` (`mv`) | rename, or move to another directory on the same drive |
| `mkdir dir` / `rmdir dir` | create / remove an empty directory |
| `date [YYYY-MM-DD [HH:MM[:SS]]]` | set the clock; no argument: show it and prompt for a new value (Enter keeps it) |
| `format C:` | after `y/N`, recreate C: as an empty FAT12 volume (D: is not supported) |
| `echo text` | print the text (for scripts) |
| `exit` | end the innermost script |
| `history` | the last 32 typed lines, numbered (Up/Down recall them at the prompt) |
| `run file [args...]` | load an ELF into user RAM and run it with `argv` (see "ELF loader") |
| `name [args...]` | any word that is not a built-in command is looked up as `NAME`, `NAME.ELF`, `NAME.CMD` in the current directory (taken as an absolute path), then in `C:/`, then `D:/`; a word with a drive or directory in it is tried only there. An ELF runs with the rest of the line as arguments (`bmpview adobe2.bmp` is `run C:/bmpview.ELF adobe2.bmp` when the program lives on C:), a `.CMD` file runs as a script. Only a missing file gives "unknown command"; other loader errors are reported as by `run`. Built-ins win: `ls` is the monitor's directory listing, `ls.elf` or `run ls.elf` the program. |
| `flashid`, `flash file`, `reboot` | see "Programming the ROM" |

`help` lists all of these. Long-running output (`cat`, `dir`, `copy`) stops only on ESC and
leaves other typed-ahead keys in the queue for the next command (`input_has_esc` looks into
the keyboard queue without draining it; `input_flush` drains it after an ESC).

### Scripts

A `.CMD` file found on the search path is a script: its lines are executed as if typed but
not echoed (the screen is 40 columns); lines starting with `#` or `;`, or the word `rem`,
are comments; `echo` prints. A line that runs a program resumes the script after the
program exits: `user_launch` never returns (the exit lands in `monitor_restart`), so a
script's state is its path and byte offset in static data and the file is reopened for each
line. Scripts nest 4 deep (a script line naming another `.CMD`); `exit` ends the innermost
one, ESC between lines aborts all of them. `C:/AUTOEXEC.CMD` runs at the first prompt after
reset unless ESC was pressed during the POST; the one `make flash` installs comes from
`flashfs/AUTOEXEC.CMD`.

### S-record upload

Type `srec` in the monitor (on the keyboard: serial is not console input), then send the
`.srec` produced by the SDK (`objcopy -O srec`) over the serial port, for example
`cat prog.srec > /dev/ttyUSB0`. The monitor reads serial port A directly without echoing
(at 38400 baud a record arrives every ~11 ms and echoing one would take longer, so the
512-byte ring would overflow), stores S1/S2/S3 data (only into 0x8000-0xFFFFF; records
outside that are counted and dropped), and finishes at the S7/S8/S9 record or at the first
non-record line, printing `srec: N records, M bytes, start-end, entry E`. Then `go` runs
it. In the emulator, type `srec` with `--type` and send the file with a delayed
`--serial-type "$(cat prog.srec)" --serial-type-delay 450`: bytes that arrive before `srec`
is typed (a pipe into stdin from start-up) land during the POST and are discarded.

### Running programs

`run`/`go` enter user mode with the stack described under "ELF loader" (`argc`/`argv` and a
return address that performs `sys_exit(0)`, so a program may simply `rts`). Programs cannot touch SR; they use TRAP #15 (see
`generated/HARDWARE.md`, "System calls") and `sys_set_irq_handler` for interrupt hooks,
which are called from the BIOS ISRs in supervisor mode at the interrupt's own level (higher
levels can still interrupt them). `sys_exit` closes the program's
files (flushing the C: cache), restores the BIOS interrupt enables, drops user hooks, the audio callback and any
queued audio buffers, selects page 0 and prints `[exit N]`. `sys_exec(path, args)` loads another ELF over the
current program and starts it (`argv[0] = path`, `args` split on blanks); it returns only
on failure, before anything was overwritten.

System calls check user pointers: buffers and strings must lie in RAM above the BIOS area
(`USER_BASE`..`RAM_END`, which includes both framebuffer pages so a file can be read
straight into one); otherwise `E_FAULT`. Strings must also be NUL-terminated inside that
range (`sys_puts` prints at most up to `RAM_END`). fd 0/1/2 are the
console; `read`/`write` on them from the file calls return `E_BADF` for the wrong
direction and `lseek` on them `E_INVAL`.

## Files

| file | contents |
|---|---|
| `crt0.S` | vector table, stubs, `_start` (GPIO idle, DUART init, POST), `post_return`, `exception_common`, `trap15_handler`, `run_user`, `monitor_restart` |
| `post.S` | ROM checksum and RAM test, registers only |
| `duart.S`, `duart.h` | `duart_reset_init` (stack-less DUART bring-up), `post_putc/puts/puthex` (poll TXRDY), `delay_loops`; MR/CR/ACR values with data-sheet references |
| `memops.S`, `string.c` | memcpy/memmove/memset, str* |
| `main.c` | `bios_main`, banner, `putchar_both` |
| `console.c`, `font8x8_basic.h` | video console |
| `serial.c`, `input.c`, `queue.c`, `ps2.c`, `irq.c` | GPIO shadow, polled DUART TX, input rings, PS/2 decoder, ISRs (DUART RX drain) |
| `exception.c`, `syscall.c`, `monitor.c`, `srec.c`, `hexdump.c`, `printf.c` | as named; `syscall.c` holds the user pointer checks and the fs/SD/exec call wrappers |
| `sd.c`, `sd.h`, `sd4.S` | SD driver on the native 4-bit bus (init, CMD17/CMD24, CSD/CID); asm command, idle-clock, read, write and CRC16 paths |
| `fatfs/` | FatFs R0.15 (`ff.c`, `ff.h`, `ffunicode.c`, `diskio.h`, configured `ffconf.h`, `LICENSE.txt`) |
| `diskio.c` | FatFs media layer: drive 0 (C:) on `flashdisk.c`, drive 1 (D:) on `sd.c` |
| `flashdisk.c`, `flashdisk.h` | C: block layer: reads from the ROM window, writes through the 8 KB erase-sector cache |
| `flash.c`, `flash.h`, `flash_layout.h`, `flashram.S` | `flashid`, `flash` (image update) and `flash_write_sector` (one 8 KB sector for C:); the RAM-resident routine (in `.data`) and the `struct flash_op` offsets it shares with C |
| `clock.c`, `clock.h` | wall clock, calendar and FAT stamp conversions, `get_fattime` |
| `joy.c`, `joy.h` | Atari joystick switches from the DUART input port; paddle timing with the counter/timer and the input-change interrupt |
| `args.c`, `args.h` | command-line words: quote-aware split, `*`/`?` matching, expansion of program arguments against the filesystem |
| `lineedit.c`, `lineedit.h` | the monitor's line editor: cursor keys, 32-line history, Tab completion through a callback |
| `fs.c`, `fs.h` | two volumes, fd table, `O_*` to FatFs mode mapping, `FRESULT` to `E_*`, `struct dirent`, unlink/rename/mkdir/chdir/getcwd/copy/mkfs |
| `elf.c`, `elf.h`, `exec.c`, `exec.h` | ELF32 parsing, `exec_prepare`/`exec_load`, `user_launch` (argv convention) |
| `test/test_main.c`, `test_sd.c`, `test_elf.c`, `test_fs.c`, `test_audio.c`, `test.h` | host unit tests (`make test`, including `test_clock`); `test/mkimage.sh` builds the FAT32 image (D:), `../tools/mkflashvol.sh` the FAT12 image (C:) that `test_fs.c` loads into a RAM array with a simulated `flash_write_sector` (erase to 0xFF, programming clears bits, cycles counted); `test/data/` their files |
| `rom.ld`, `mkrom.py`, `Makefile` | link, checksum, window image and chip split, build |
| `../tools/mkflashvol.sh`, `../flashfs/` | the C: volume image and the files the top-level `make flash` puts on it besides the programs |
