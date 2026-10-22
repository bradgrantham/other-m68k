# other-m68k

A complete homebrew 68000 single-board computer: hardware, glue logic, firmware, SDK and
emulator, all generated from one specification.

| | |
|---|---|
| CPU | MC68000 at 12 MHz (24 MHz oscillator divided in the glue) |
| RAM | 1 MB: 2x AS6C4008-55PCN 512Kx8 SRAM, word wide, time-sliced with the video fetch |
| ROM | 1 MB window: 2x SST39SF040 flash, word wide; 256 KB BIOS image (in-system programmable, overlaid at address 0 at reset) plus a 768 KB FAT12 volume the BIOS mounts as drive C: |
| Video | memory-mapped 320x240, 8 bpp RGB332 framebuffer, two pages; AD724 composite encoder, NTSC (PAL by build option) |
| Glue | two ATF1508AS CPLDs ("MMU" and "I/O + shifter"), two 74AHCT245 bus transceivers |
| I/O | PS/2 keyboard, microSD card on a bit-banged 4-bit SD bus, 68681 DUART serial (38400 8N1 on an FTDI header; carries the POST and banner, then belongs to programs as `/dev/ttya`), 8-bit PDM audio (one byte per video line shifted out by the CPLD), 2 LEDs, NMI button |
| Power | USB-C, 5 V |

## Layout

| path | contents |
|---|---|
| `spec/board.json` | **the single source of truth**: memory map, registers, interrupts, video timing, syscalls, ABI |
| `tools/gen.py` | generates everything in `generated/` from the spec (`make gen`; `make check-gen` in CI) |
| `generated/` | C/asm headers, linker scripts, Verilog parameters, syscall stubs/dispatcher, emulator tables, `HARDWARE.md` |
| `docs/architecture.md` | the bus-timing contract: slot schedule, decode, alignment, peripherals |
| `docs/bringup.md` | staged hardware bring-up plan |
| `docs/programmers-guide.md` | programmer's guide: writing applications with the SDK, and programming the hardware directly |
| `hdl/` | Verilog for both CPLDs plus a system testbench with a 68000 bus model |
| `emu/` | SDL2 emulator (Musashi 68000 core), used to run the firmware and user programs |
| `fw/` | BIOS/monitor ROM: POST, serial, video console, PS/2, exceptions, monitor (drives C: and D:, scripts, AUTOEXEC.CMD), syscalls, FatFs with long names, flash volume driver, clock, ELF loader |
| `sdk/` | user-program crt0, linker script, syscall library, newlib shim, examples |
| `flashfs/` | the files that go into the C: volume besides the SDK examples (AUTOEXEC.CMD, README.TXT) |
| `kicad/` | generated schematic and PCB |

## Building

```
make gen        # regenerate from the spec
make fw         # fw/build/rom.bin (256 KB BIOS image, for the monitor's `flash` command)
make flash      # fw/build/flash.img (BIOS + the C: volume from flashfs/ and the SDK examples), rom_even.bin / rom_odd.bin for the two flash chips
make emu        # emu/build/m68k-emu
make sdk        # sdk/build/*.elf user programs
make run        # boot flash.img in the emulator (what the machine writes to C: persists in it)
make -C hdl sim # simulate the glue logic (needs iverilog)
```

Cross toolchain: `~/x-tools/m68k-unknown-elf` (crosstool-ng, GCC 15, `--with-cpu=68000`,
newlib). Override with `XTOOLS=/path/to/bin`.

## How the pieces fit

The spec names every address and bit once. `tools/gen.py` turns it into `hw.h` for C, `hw.inc`
for assembler, `board_params.vh` for the CPLDs, `emu_map.h` for the emulator and `HARDWARE.md`
for humans, plus syscall stubs for user programs and the dispatcher for the BIOS. Changing the
memory map or adding a syscall is one edit followed by `make gen`.

`docs/architecture.md` explains the mechanism the numbers describe: the SRAM is shared on a
fixed 4-clock slot, video takes the first half, the CPU the second, and CPU cycles are
phase-aligned by delaying DTACK by zero or two clocks. The video fetch therefore never costs
the CPU a bus slot.
