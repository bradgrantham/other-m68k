# other-m68k emulator

`m68k-emu` runs the board's ROM on a Musashi 68000 core with a model of the two CPLDs and the
peripherals: 256 KB RAM, 128 KB ROM with the reset overlay, the CPLD2 registers, the soft
switches, the 320x240 RGB332 framebuffer in an SDL2 window, the 68681 DUART (channel A is the
serial console), the PS/2 keyboard, an SDHC card in SPI mode, 1-bit audio and the NMI/reset buttons.
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
  --serial-log FILE   copy of the serial (DUART channel A) output     --duart-b-log FILE  DUART channel B output (dropped otherwise)
  --type STR          type STR on the PS/2 keyboard (\n = Enter)     --type-delay N   ... starting after N frames (default 120)
  --serial-type STR   feed STR into the serial port (\n = CR)         --serial-type-delay N  ... after N frames (default 120)
  --sd-trace          log SD commands to stderr                      --exit-on-berr   exit with status 2 on the first bus error
  --io-trace          log CPLD2 and DUART register accesses, soft switches and serial RX arrivals with cycle stamps
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

Serial RX comes from stdin (and from `--serial-type`): when stdin is a tty it is switched to a
raw-ish mode (no echo, no line editing, Ctrl-C still works) for the duration and restored at
exit; a pipe or file works too. Bytes arrive on the DUART's channel A at the console rate
(`SERIAL_BAUD`, one byte per ten bit times) whatever the firmware programmed, so, as on the real
board, bytes that arrive while the receiver is disabled are lost and a burst longer than the
3-byte FIFO plus the shift register sets OE. Piping input in at start-up therefore loses most of
it during the firmware's POST; `--io-trace` shows each arrival. If channel A is enabled at a
rate other than `SERIAL_BAUD` the emulator says so once on stderr (data still flows).

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
  pending-and-enabled sources (VBLANK 1, PS2 2, DUART 3, HSYNC 4), 7 while the NMI button is
  held. The DUART source is level sensitive: level 3 is requested while the DUART's IRQ output
  ((ISR & IMR) != 0) is asserted and INTEN.DUART is set. All vectors are autovectors (CPLD1 asserts VPA on IACK). Musashi samples IPL only at
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
  the word (with the register's read side effects) and a byte write updates only that half of
  the emulator's shadow. INTEN, SDIO, GPIO and USERIO.OUT are write-only and read as 0 (USERIO
  reads the pin levels in bits 11..8, which follow the open-drain drive since nothing external
  is attached); offset 10 has no register (reads 0, writes ignored); ID reads `IO_ID_RESET`.
  32-bit accesses are two word accesses, high word first.
* SWITCHES region: a write at offsets 0/2/4/6 performs PAGE0/PAGE1/NTSC/PAL; reads return
  0xFFFF.
* DUART region: the 68681 is on D0..D7 with RS4..RS1 = A4..A1, so its registers are the odd
  bytes 0xF20001, 0xF20003, ... 0xF2001F, mirrored every 32 bytes through the region. Byte
  accesses at odd addresses are the real thing. A word access completes (DTACK comes from the
  chip) and behaves as the odd byte with 0xFF in the upper half; an even-byte access completes,
  reads 0xFF and reaches no register. Both are reported once on stderr (what the chip makes of
  them on hardware is undefined).
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

* **DUART** (`duart.c`): register-level MC68681. Per channel: MR1/MR2 behind the mode-register
  pointer (reset by the CR command), CSR, CR (enable/disable RX and TX, reset MR pointer, reset
  RX, reset TX, reset error status; break commands accepted and ignored), SR (RXRDY, FFULL,
  TXRDY, TXEMT, OE; PE/FE/RB always 0), a 3-byte receive FIFO with the shift register behind
  it (a fourth byte waits there, a fifth sets OE and replaces it), transmit holding + shift
  registers. Shared: ISR (TXRDYA/B, RXRDYA/B or FFULLA/B per MR1[6], CTR; DBA/DBB/IPC always
  0), IMR, IVR (stored, reset 0x0F), ACR, IP (reads 0xFF), IPCR, OPCR, OPR set/reset. The
  counter/timer works in counter mode (START loads the preload and counts, terminal count sets
  CTR and rolls over, STOP stops and clears CTR, CUR/CLR read the count) and timer mode (runs
  continuously, CTR every 2*N source clocks, START restarts the phase, STOP only clears CTR)
  from the crystal (X1) or X1/16; the IP2 sources never tick; TxCA/TxCB 1X sources count at
  the channel's bit rate. Bit times come from the data-sheet CSR table (set 1 with ACR[7] = 0,
  set 2 with ACR[7] = 1; code 0xD from the timer; 0xE/0xF external = console rate) and the
  frame length from MR1/MR2, converted to CPU clocks. A byte written to TBA goes straight into
  the shift register if that is idle (TXRDY stays set, TXEMT clears) or waits in the holding
  register (TXRDY clears); it reaches stdout/`--serial-log` when its stop bit ends, and
  back-to-back bytes keep the line busy. Channel B transmits into `--duart-b-log` (or nowhere)
  and never receives. IRQ = (ISR & IMR) != 0 feeds INTSTAT.DUARTIRQ and the level-3 request.
  Reset per the data sheet: both channels disabled, IMR/ISR/OPR/OPCR 0, MR pointers at MR1.
  Not modelled: parity/framing/break detection, CTS/RTS, loopback and multidrop modes, the
  input-port change detector, 1X/16X clock outputs on OP pins, the "do not access" test
  registers (they read 0xFF with a one-time warning).
* **Console** (`serial.c`): the host side of channel A (stdout, `--serial-log`, stdin raw
  mode, `--serial-type` injection) and channel B's log file; no timing of its own.
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
runs it with `--headless --fast --frames 120 --serial-type ABCDEF --serial-type-delay 1
--serial-log ... --dump-mem 0x800 96` plus an SD image whose block 0 carries a signature, and
`test/check.sh` verifies (44 checks): RAM pattern, ID register, the gradient fill runs, the
DUART init sequence from the spec (IVR 0x0F at reset, MR1A/MR2A read back through the pointer,
SRA = TXRDY+TXEMT after enable), "HELLO\n" sent by polling TXRDY appears on stdout and in the
serial log (and SRA reads 0 while a byte shifts with another in the holding register), a word
read of SRA gives 0xFF in the upper byte and one warning, six bytes injected while the interrupt
is masked leave SRA = RXRDY+FFULL+OE with A B C in the FIFO and F in the shift register (D and E
overrun), the IRQ pin shows in INTSTAT.DUARTIRQ but no interrupt is taken until INTEN.DUART is
set, then exactly one level-3 autovector interrupt drains B C F, reset-error-status clears OE,
counter mode (X1/16, preload 64) reaches terminal count under polling and STOP_CTR clears CTR
and leaves a rolled-over count, timer mode (preload 72, 625 us) delivers 40..100 CTR interrupts
during the three-frame VBLANK wait, VBLANK autovector interrupts are counted (and INTSTAT inside
the handler shows VSYNCPIN without DEPIN), lines per frame measured by counting HSYNC flags
between VBLANKs are 262/312/262 across SW_PAL and SW_NTSC, a read of 0x400000 produces exactly
one bus error whose frame carries fault address 0x400000 and access info 0x0015 (read,
supervisor data), the SD card answers CMD0/CMD8/ACMD41/CMD58/CMD17 and block 0 matches (MISO
read through INTSTAT.SDMISO), stdout equals the serial log, the end-of-run summary counts 6 bytes
in with 2 overruns, and the total cycle count is a whole number of frames.

## Layout

| file | contents |
|---|---|
| `main.c` | options, Musashi setup, per-line loop, pacing, SDL events, dumps |
| `bus.c/h` | decode, RAM/ROM, overlay, BERR frame, side-effect-free peeks, hex dump |
| `io.c/h` | CPLD2 registers, W1C flags, write-only shadows, IPL, soft-switch latches, `--io-trace` |
| `video.c/h` | NTSC/PAL timing, live pins, RGB332 -> ARGB8888, SDL window |
| `duart.c/h` | MC68681 model: channels, FIFOs, ISR/IMR, counter/timer, bit timing |
| `serial.c/h` | console back end: stdout, logs, stdin raw mode, `--serial-type` |
| `ps2.c/h` | scan code set 2 keyboard, `--type` |
| `sdcard.c/h` | SPI-mode SDHC card |
| `audio.c/h` | 1-bit audio renderer |
| `m68kconf.h` | Musashi configuration (68000 only, INT_ACK, FC macro) |
| `musashi/` | vendored Musashi core (+ `musashi-berr-frame.patch`) |
| `test/` | self-test ROM, linker script, `check.sh` |
