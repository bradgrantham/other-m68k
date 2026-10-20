# other-m68k emulator

`m68k-emu` runs the board's ROM on a Musashi 68000 core with a model of the two CPLDs and the
peripherals: 256 KB RAM, 128 KB ROM with the reset overlay, the CPLD2 registers, the soft
switches, the 320x240 RGB332 framebuffer in an SDL2 window, the bit-banged serial port (both
directions), the PS/2 keyboard, an SDHC card in SPI mode, 1-bit audio and the NMI/reset buttons.
Addresses, bits and timing constants come from `generated/hw.h` / `generated/emu_map.h`; nothing
is hard-coded that the spec defines.

## Building and running

```
make -C emu            # emu/build/m68k-emu  (needs sdl2-config, host cc)
make -C emu test       # build emu/test/test.S with the m68k cross toolchain and run it headless
make -C emu clean
make emu / make run    # from the top level
```

```
m68k-emu --rom fw/build/rom.bin [options]

  --rom FILE          ROM image (<= 128 KB, padded with 0xFF)        --sd FILE        SD card image, any multiple of 512 bytes (read/write)
  --scale N           window scale (default 3, nearest-neighbour)    --pal            fit the PAL jumper (INTSTAT.PALJMP = 1)
  --fast              no real-time pacing (audio off)                --headless       no window, no audio (works without a display)
  --frames N          exit after N frames                            --trace          disassemble each instruction to stderr
  --trace-from ADDR   start tracing when PC first reaches ADDR       --break ADDR     dump registers and exit when PC reaches ADDR
  --serial-log FILE   copy of the decoded serial TX bytes            --hw-serial-tx   emulate a hardware transmitter behind SERDATA
  --type STR          type STR on the PS/2 keyboard (\n = Enter)     --type-delay N   ... starting after N frames (default 120)
  --sd-trace          log SD commands to stderr                      --exit-on-berr   exit with status 2 on the first bus error
  --io-trace          log CPLD2 register accesses, soft switches and serial RX deliveries with cycle stamps
  --dump-regs-on-exit                                                --dump-mem ADDR LEN   hex dump at exit (repeatable)
  --no-audio          disable audio                                  --no-stdin       do not read serial RX from stdin
```

Numbers accept `0x` prefixes. Exit status: 0 normal (frame count reached, window closed,
breakpoint, Ctrl-C), 1 usage/file error, 2 `--exit-on-berr`, 3 CPU halted by a double bus fault
in headless mode.

**stdout carries only the serial output** (unbuffered), so `m68k-emu ... | tee log` or a
terminal program can be attached to it. Everything else (bus errors, SD trace, breakpoints,
dumps, the end-of-run summary) goes to stderr.

Keys in the window: **F11** = reset button (CPU and glue, overlay re-armed), **F12** or
**Pause** = NMI button (INTSTAT.NMIBTN while held, level 7 edge), every other key goes to the
PS/2 keyboard as scan code set 2 (letters, digits, punctuation, space, Enter, Backspace, Tab,
Escape, Shift/Ctrl/Alt/GUI, Caps Lock, arrows, F1-F10, Home/End/PgUp/PgDn/Insert/Delete, keypad).
F11/F12 never reach the keyboard. Window title shows the standard and LED0/LED1.

Serial RX comes from stdin: when stdin is a tty it is switched to a raw-ish mode (no echo, no
line editing, Ctrl-C still works) for the duration and restored at exit; a pipe or file works
too. Bytes are delivered to SERDATA no faster than one per ten bit times, and as on the real
board a byte that arrives before the previous one was read overwrites it (SERRXOVR). Piping
input in at start-up therefore loses most of it during the firmware's POST; `--io-trace` shows
each delivery.

## Timing model

* One Musashi slice per scanline: `line_clocks` CPU clocks (764 NTSC / 768 PAL). The slice
  begins at the HSYNC leading edge, where CPLD1's V counter advances: INTSTAT.HSYNC is set
  there, and INTSTAT.VBLANK at the start of line `v_active + v_bottom` (243 NTSC, 270 PAL),
  the leading edge of VSYNC. The instruction that overruns the budget is charged to the next
  line, so a frame is exactly 764*262 = 200,168 (NTSC) or 768*312 = 239,616 (PAL) clocks and
  the cycle counter stays exact over any interval.
* Live pins in INTSTAT are computed from the position inside the slice: HSYNCPIN during the
  first `h_sync` clocks, DEPIN during `[h_sync + h_back, + h_active)` on active lines,
  VSYNCPIN on the `v_sync` lines.
* The soft switches (PAGE0/1, NTSC/PAL) are latched at the start of line 0 of the next frame,
  as CPLD1 reloads VA. Reset selects NTSC; `--pal` only fits the jumper (PALJMP); firmware
  picks the PAL table with SW_PAL.
* Interrupt priority is recomputed after every flag or enable change: the highest level among
  pending-and-enabled sources (VBLANK 1, PS2 2, SERRX 3, HSYNC 4), 7 while the NMI button is
  held. All vectors are autovectors (CPLD1 asserts VPA on IACK). Musashi samples IPL only at
  the start of a slice and on SR writes, so a memory handler that changes the level ends the
  slice and the CPU sees it after the current instruction.
* The framebuffer is converted from the displayed page at the end of the last active line (239)
  and presented at the end of the frame, paced to 59.95 Hz (NTSC) or 50.08 Hz (PAL) unless
  `--fast`. `--headless` runs flat out with no SDL at all.
* Memory accesses cost Musashi's nominal cycle counts. The slot alignment penalty of RAM
  accesses on the real board (0 or 2 extra clocks, see `docs/architecture.md` section 2) is not
  modelled, so RAM-heavy code runs 10-25% faster in emulation than on hardware.

## Memory and bus behaviour

* RAM at `RAM_BASE`, ROM at `ROM_BASE` (padded to 128 KB with 0xFF; images larger than the ROM
  are rejected). ROM writes are ignored with DTACK.
* Overlay: set at reset; while set, reads in the RAM region return ROM and writes are dropped.
  It clears at the end of any access whose address is in the ROM region, so the fetch of the
  first instruction at the reset PC clears it.
* IO region: the eight 16-bit registers repeat every 16 bytes (A1..A3 select). CPLD2 has no
  UDS/LDS, so byte accesses are undefined on hardware; here a byte read returns its half of
  the word (with the register's read side effects) and a byte write updates only that half.
  32-bit accesses are two word accesses, high word first.
* SWITCHES region: a write at offsets 0/2/4/6 performs PAGE0/PAGE1/NTSC/PAL; reads return
  0xFFFF.
* Anything else raises BERR with the 68000's 7-word group-0 frame: access info word
  (R/W bit 4 = 1 for read, I/N bit 3, FC bits 2..0), fault address, IR, SR, PC. The emulator
  logs `bus error: read 400000 at PC=E00500 (FC=5, ...)` to stderr, at most 10 lines per
  emulated second. Addresses 0xFFFFF0..0xFFFFFF (the IACK pattern) never BERR. A bus error
  while the frame is being written halts the CPU (double fault); headless runs exit with
  status 3, a windowed run says so and waits for F11.
* The RESET instruction does not reset the glue (architecture.md section 6).

### Musashi patch

Stock Musashi always pushes the 68010 format-8 frame on a bus error. `musashi-berr-frame.patch`
(applied to `musashi/m68kcpu.h`) makes `m68ki_exception_bus_error()` push the 68000 frame via
`m68ki_stack_frame_buserr()` when the CPU type is 68000; `bus.c` fills `m68ki_aerr_address`,
`m68ki_aerr_write_mode` and `m68ki_aerr_fc` before calling `m68k_pulse_bus_error()`. The
function code comes from the zero-cost `M68K_SET_FC_CALLBACK` macro in `m68kconf.h`
(`emu_fc`). If Musashi is ever re-vendored, re-apply the patch.

## Peripherals

* **Serial TX** (`serial.c`): every GPIO.SERTX edge is recorded with its cycle stamp. The
  decoder finds the start (falling) edge and samples at 1.5, 2.5, ... 9.5 bit times
  (`CPU_CLOCK_HZ / SERIAL_BAUD` = 104.17 clocks per bit, exact fixed point), accepting the byte
  if the stop bit is high; a transmitter that is off by a few percent still decodes. With
  `--hw-serial-tx`, a write to SERDATA transmits the byte, SERTXBUSY reads 1 for ten bit times,
  and hardware and bit-banged bytes are merged in emulated-time order.
* **PS/2** (`ps2.c`): make/break sequences queued and delivered one byte per 1 ms of emulated
  time, only while PS2RDY is clear (the register is a single holding latch). Host-to-device
  traffic on GPIO.PS2CLK/PS2DAT is not emulated; the lines read back as the GPIO drives them
  (pulled up when released).
* **SD card** (`sdcard.c`): SPI mode 0 slave on the SDIO bits (MOSI sampled on the rising CLK
  edge, MISO updated after the falling edge). SDHC, block addressed: CMD0, 1, 8 (R7), 9 (CSD
  v2 with C_SIZE from the image size), 10 (CID), 12, 13, 16, 17, 18, 24, 25, 55, ACMD41 (idle
  on the first poll, ready afterwards), 58 (OCR with CCS), 59; unknown commands get 0x04. Data
  tokens 0xFE/0xFC/0xFD, CRC16 on data, one 0xFF before each response. MISO is 1 when CS is
  high or no image is attached.
* **Audio** (`audio.c`): GPIO.AUDIO edges are box-filtered into 48 kHz mono samples once per
  frame (250 clocks per sample exactly), high-passed like the board's coupling capacitor and
  queued to SDL. Off with `--no-audio`, `--fast` or `--headless`.

## Test

`make -C emu test` assembles `test/test.S` (vectors with SSP = SYS_STACK_TOP and PC in ROM),
runs it with `--headless --fast --frames 120 --serial-log ... --dump-mem 0x800 64` plus an SD
image whose block 0 carries a signature, and `test/check.sh` verifies: RAM pattern, ID register,
the gradient fill runs, "HELLO\n" bit-banged with a 104-clock loop is decoded, VBLANK autovector
interrupts are counted (and INTSTAT inside the handler shows VSYNCPIN without DEPIN), lines per
frame measured by counting HSYNC flags between VBLANKs are 262/312/262 across SW_PAL and
SW_NTSC, a read of 0x400000 produces exactly one bus error whose frame carries fault address
0x400000 and access info 0x0015 (read, supervisor data), the SD card answers CMD0/CMD8/ACMD41/
CMD58/CMD17 and block 0 matches, stdout equals the serial log, and the total cycle count is a
whole number of frames. A second run with `--hw-serial-tx` checks that SERDATA writes are
transmitted in order with the bit-banged bytes and ignored otherwise.

## Layout

| file | contents |
|---|---|
| `main.c` | options, Musashi setup, per-line loop, pacing, SDL events, dumps |
| `bus.c/h` | decode, RAM/ROM, overlay, BERR frame, side-effect-free peeks, hex dump |
| `io.c/h` | CPLD2 registers, W1C flags, IPL, soft-switch latches, `--io-trace` |
| `video.c/h` | NTSC/PAL timing, live pins, RGB332 -> ARGB8888, SDL window |
| `serial.c/h` | bit-bang decoder, hardware TX, stdin RX |
| `ps2.c/h` | scan code set 2 keyboard, `--type` |
| `sdcard.c/h` | SPI-mode SDHC card |
| `audio.c/h` | 1-bit audio renderer |
| `m68kconf.h` | Musashi configuration (68000 only, INT_ACK, FC macro) |
| `musashi/` | vendored Musashi core (+ `musashi-berr-frame.patch`) |
| `test/` | self-test ROM, linker script, `check.sh` |
