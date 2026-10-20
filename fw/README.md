# fw/ -- other-m68k BIOS / monitor ROM (stage 1)

128 KB EPROM image: power-on self test, 68681 DUART serial (38400 8N1), 40x30 video console, PS/2
keyboard, exception reporting, machine monitor with S-record upload, and the TRAP #15
system call interface used by user programs (see `../sdk/`). Stage 2 (SD card, FAT32, ELF
loader, `dir`/`run`/`cat`/`sd`) is not implemented; those calls return `E_NOSYS`.

## Build

```
make -C fw            # build/rom.elf, rom.bin (131072 bytes), rom.lst, rom.map,
                      # rom_even.bin (D15..D8 EPROM), rom_odd.bin (D7..D0 EPROM)
make -C fw test       # host unit tests: printf, queue, srec, ps2, console, hexdump
make -C fw clean
```

Toolchain: `~/x-tools/m68k-unknown-elf` (override `XTOOLS=/path/to/bin`). The BIOS is
freestanding (`-ffreestanding -nostdlib`, own string/printf), `-m68000 -Os -Wall -Wextra`,
linked with `-lgcc` only. All addresses, register bits, syscall numbers and the dispatcher
come from `../generated/` (`hw.h`, `hw.inc`, `syscalls.h`, `bios_syscalls.h`,
`syscall_dispatch.inc`, `memory.ld`); nothing is duplicated here.

`mkrom.py` sets the last ROM word so that the 16-bit big-endian sum of all 65536 words is
zero, then writes the even/odd split. The POST sums the whole ROM and expects 0.

Run in the emulator:

```
emu/build/m68k-emu --rom fw/build/rom.bin                       # window + PS/2 keyboard
emu/build/m68k-emu --rom fw/build/rom.bin --headless --frames 300 --type "help\n"
```

The serial port is the emulator's stdin/stdout (`--serial-log FILE` keeps a copy).

## Boot sequence and LED codes

| LEDs | meaning |
|---|---|
| LED0 on | reset reached `_start`; DUART programmed; ROM checksum running |
| LED0 + LED1 on | RAM test running (~1.7 s) |
| LED1 on | RAM passed; C initialisation (vectors, .data/.bss, console, interrupts) |
| both off | monitor idle / running normally |
| LED0 / LED1 alternating at ~2 Hz forever | RAM test failed (details on serial) |

`_start` masks interrupts, writes INTEN = 0 and the GPIO idle value (PS/2 lines released,
LED0 on), then programs DUART channel A from registers (`duart_reset_init` in `duart.S`):
IMR = 0; CRA = reset MR pointer, reset RX, reset TX, reset error status; ACR = 0x80 (baud
set 2); MR1A = 0x13 (8 bits, no parity); MR2A = 0x07 (1 stop bit); CSRA = 0xCC (38400 RX and
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
ROM sum 0000 OK          <- 16-bit word sum of the whole ROM, must be 0000
RAM ....... OK           <- one dot per pass: address-in-address, 55, AA, 00, FF, byte lanes x2
```

A RAM failure prints `RAM FAIL @aaaaaaaa exp xxxxxxxx got yyyyyyyy` and blinks forever.
The byte-lane passes write 0x55 to even (D15..D8) and 0xAA to odd (D7..D0) bytes with
byte moves and verify as longs, then the inverse, so a stuck WEU/WEL shows up as
`exp 55AA55AA got ...`.

After the POST the banner shows board name, firmware version, the stored checksum word,
RAM size, ID register (0x6802 on the DUART board), NTSC/PAL (PAL is selected with the soft
switch when `INTSTAT.PALJMP` is set) and the DUART baud rate.

## Memory use

| range | use |
|---|---|
| 0x000000-0x0003FF | vector table, copied from ROM after the RAM test (programs may hook vectors) |
| 0x000400-~0x000F60 | BIOS `.data` + `.bss` (input rings 512+256+128 B, console shadow text 1200 B, exception record) |
| ~0x000F60-0x004000 | supervisor stack, grows down from `SYS_STACK_TOP` (0x4000) |
| 0x004000-0x01A7FF | user program; USP starts at 0x01A7FC |
| 0x01A800-0x02D3FF | framebuffer page 1 (user programs may use it as RAM if they do not double buffer) |
| 0x02D400-0x03FFFF | framebuffer page 0 = the console |

ROM use: about 19 KB of 128 KB (`make` prints the sizes; `mkrom` prints the bytes used).

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
| 1 | VBLANK | `ticks++`, user hook (`sys_set_irq_handler(1, fn)`) |
| 2 | PS/2 byte | set-2 decode to ASCII / escape sequences, raw event queue, user hook |
| 3 | DUART IRQ pin (RXRDYA unmasked) | drains the RX FIFO into a 512-byte ring (overruns counted, `info` shows them), user hook |
| 4 | HSYNC (15.7 kHz) | enabled only while an audio callback or level-4 hook is installed |
| 7 | NMI button | prints the registers and enters the monitor |

## Serial port

Channel A of the MC68681 DUART at `DUART_BASE`, 38400 8N1, byte registers at odd
addresses (`generated/hw.h`: `DUART_SRA`, `DUART_TBA`, ...). Transmit is polled:
`serial_putc` waits for `SRA.TXRDY` (transmit holding register empty) and writes `TBA`;
nothing masks interrupts, so audio callbacks and the keyboard are unaffected by printing.
Receive is interrupt driven: `IMR = RXRDYA` (set in `serial_init`), the DUART's IRQ pin is
gated by `INTEN.DUART` to level 3, and `irq_duart` reads the DUART `ISR`, then pops `RBA`
while `SRA.RXRDY`, counting `SRA.OE` as an overrun (and issuing the "reset error status"
command 0x40 to clear it). The interrupt is level sensitive, so draining the 3-byte FIFO
is what releases it. `INTSTAT.DUARTIRQ` shows the live pin. The monitor does not echo
while receiving S-records (printing a line takes longer than a line takes to arrive);
hosts should wait for the prompt before sending. Channel B is not used by the BIOS.

INTEN, SDIO, GPIO and USERIO.OUT are write-only in CPLD2 (they read as 0); the BIOS keeps
`inten_shadow` and `gpio_shadow` and every write goes through `irq_update_inten` /
`gpio_write/set/clear`. SDIO and USERIO are not touched by stage 1.

## Console

40 columns x 30 rows, 8x8 font (public-domain `font8x8_basic.h` from dhepper/font8x8),
white on dark blue, page 0. Handles `\n` (CR+LF), `\r`, `\b` (non-destructive), `\t`
(8 columns); scrolls at the bottom (a scroll is a 74 KB move, ~35 ms). Everything printed
on the console is also sent to the serial port with `\n` -> `\r\n`. Input is merged from
the keyboard (ASCII) and the serial port; `sys_key_event` reads raw PS/2 events.

Keyboard: US layout, shift/caps/ctrl. Arrows -> `ESC [ A/B/C/D`, Home/End -> `ESC [ H/F`,
Ins/Del/PgUp/PgDn -> `ESC [ 2~/3~/5~/6~`, F1-F4 -> `ESC O P..S`, F5-F12 -> `ESC [ 15~ 17~
18~ 19~ 20~ 21~ 23~ 24~`.

## Exceptions

Any unexpected exception prints the vector number and name, PC, SR, D0-D7, A0-A7, SSP and
USP, and for bus/address errors the fault address, read/write and function code, then
restarts the monitor on a fresh stack. `regs` prints the record again. A crashing user
program therefore drops you back in the monitor with its registers on the screen.

## Monitor

All numbers are hex (`0x` optional). Backspace/DEL edit, ^U/^C clear the line.

| command | action |
|---|---|
| `help` | command list |
| `md [addr] [len]` | dump memory, 8 bytes per line (continues from the last address; dumps over 256 bytes stop on a key) |
| `mb addr v...` / `mw addr v...` / `ml addr v...` | write bytes / words / longs |
| `fill addr len v` | fill bytes |
| `go [addr]` | run a program in user mode (default: the last S-record entry point, else 0x4000) |
| `regs` | registers of the last exception |
| `srec` | receive S-records (also auto-detected: any line starting with `S` and a digit) |
| `reset` | RESET instruction, then the reset path |
| `led n` | LEDs (bit 0 = LED0, bit 1 = LED1) |
| `page n` | display framebuffer page 0 or 1 |
| `ticks` | VBLANK counter |
| `ramtest` | destructive test of 0x4000-0x3FFFF (user RAM and both framebuffers) |
| `beep` | 1 kHz for 200 ms on GPIO.AUDIO |
| `cls` | clear the console |
| `info` | banner, tick rate, INTSTAT, INTEN and GPIO shadows, DUART SRA, RX overruns, PS/2 errors |
| `dir` `run` `cat` `sd` | "not implemented (stage 2)" |

### S-record upload

Send the `.srec` produced by the SDK (`objcopy -O srec`) over the serial port, for example
`cat prog.srec > /dev/ttyUSB0` after the prompt appears. The monitor recognises the first
`S<digit>` line, stops echoing, stores S1/S2/S3 data (only into 0x4000-0x3FFFF; records
outside that are counted and dropped), and finishes at the S7/S8/S9 record, printing
`srec: N records, M bytes, start-end, entry E`. Then `go` runs it. In the emulator, pipe
the file to stdin once the prompt is visible (the emulator reads stdin as serial RX).

### Running programs

`go` enters user mode with USP = 0x1A7FC and a return address that performs `sys_exit(0)`,
so a program may simply `rts`. Programs cannot touch SR; they use TRAP #15 (see
`generated/HARDWARE.md`, "System calls") and `sys_set_irq_handler` for interrupt hooks,
which are called from the BIOS ISRs in supervisor mode. `sys_exit` restores the BIOS
interrupt enables, drops user hooks and the audio callback, selects page 0 and prints
`[exit N]`.

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
| `exception.c`, `syscall.c`, `monitor.c`, `srec.c`, `hexdump.c`, `printf.c` | as named |
| `test/test_main.c` | host unit tests (`make test`) |
| `rom.ld`, `mkrom.py`, `Makefile` | link, checksum/split, build |
