# emu/ status

**State: complete for spec revision 2 (68681 DUART) and passing its self-test.** `make -C emu`
builds `build/m68k-emu` with no warnings in the emulator's own sources (`-Wall -Wextra
-std=gnu11 -O2`); `make -C emu test` runs the test ROM headless and all 44 checks pass; an
ASan/UBSan build passes the same test and runs the firmware ROM clean; `make -C emu clean` works;
the top-level `make emu` works. See README.md for usage, the timing model and the DUART model.

## What exists

| File | Purpose |
|---|---|
| `main.c` | options, Musashi init, per-scanline loop with overshoot carry, real-time pacing, SDL events (F11 reset, F12/Pause NMI), trace/break hook, register and memory dumps, `--type` (PS/2) and `--serial-type` (DUART) injection |
| `bus.c/h` | decode from `generated/hw.h`, RAM/ROM, reset overlay, 68000 group-0 BERR frame, rate-limited BERR log, double-fault HALT detection, DUART region (odd-byte registers, word/even-byte warnings), disassembler peeks |
| `io.c/h` | CPLD2 registers (INTSTAT W1C + live pins incl. DUARTIRQ, write-only INTEN/SDIO/GPIO/USERIO.OUT kept as shadows, PS2DATA, USERIO.IN, ID), IPL recomputation with the level-3 DUART source, soft-switch latches, `--io-trace` |
| `duart.c/h` | MC68681: two channels (MR1/MR2 pointer, CSR, CR, SR, 3-deep RX FIFO + shift register, TX holding + shift register with cycle-timed completion), ISR/IMR, IVR, ACR, counter/timer (counter and timer modes, X1 and X1/16 and TxC sources, START/STOP commands, CUR/CLR), IP/IPCR/OPCR/OPR, data-sheet baud table, reset state |
| `serial.c/h` | console back end: stdout + `--serial-log` for channel A, `--duart-b-log` for channel B, stdin raw mode, `--serial-type` queue |
| `video.c/h` | NTSC/PAL tables, line/flag timing, live HSYNC/DE/VSYNC pins, RGB332 -> ARGB8888, SDL window, title with LEDs |
| `ps2.c/h` | scan code set 2 keyboard with E0/F0, 1 ms per byte, `--type` |
| `sdcard.c/h` | SPI mode 0 SDHC card: CMD0/1/8/9/10/12/13/16/17/18/24/25/55/58/59, ACMD41 |
| `audio.c/h` | AUDIO edges -> 48 kHz mono, SDL queue |
| `m68kconf.h` | 68000-only Musashi config |
| `musashi/` | vendored core with `musashi-berr-frame.patch` applied |
| `test/test.S`, `test.ld`, `Makefile`, `check.sh` | self-test ROM and checker |

Removed in this revision: the GPIO.SERTX bit-bang decoder, `--hw-serial-tx`, SERDATA and the
SERRX/SERRXOVR/SERTXBUSY/SERRXPIN INTSTAT bits (none of them exist on the rev 2 board).

## Verified behaviour (from `make -C emu test` and ad-hoc runs)

* 120 NTSC frames = exactly 120 * 764 * 262 cycles; after SW_PAL the ROM counts 312 HSYNC
  flags per VBLANK, 262 again after SW_NTSC.
* DUART: the spec's init sequence leaves SRA = 0x0C; "HELLO\n" written by polling TXRDY comes
  out on stdout and in `--serial-log`; six bytes fed in while the interrupt is masked give
  SRA = 0x1F (RXRDY, FFULL, OE plus the idle transmitter), the FIFO holds A B C and the shift
  register F; the IRQ pin shows in INTSTAT.DUARTIRQ but is gated by INTEN.DUART; one level-3
  autovector (vector 27) drains the FIFO; reset-error-status clears OE; counter mode counts
  X1/16 to terminal count and STOP_CTR clears CTR; timer mode with preload 72 produces 56 CTR
  interrupts in ~2.5 frames (625 us period); a word read of SRA returns 0xFF0C with one warning.
* VBLANK autovector (level 1, vector 25) fires once per frame; INTSTAT read inside the handler
  shows VBLANK + VSYNCPIN with DEPIN clear.
* Reading 0x400000 gives one BERR; the stacked frame carries fault address 0x400000 and
  access info 0x0015 (read, FC 5); the handler can RTE to a patched PC.
* SD init sequence and CMD17 of block 0 from a 1 MB image succeed (MISO through
  INTSTAT.SDMISO), OCR has CCS.
* The real firmware (`fw/build/rom.bin` from commit cc49c7b / d4e453c, the DUART port) boots:
  POST, banner ("ID 6802  NTSC  DUART 38400 8N1") and monitor prompt appear on stdout; `help`
  fed in with `--serial-type 'help\n' --serial-type-delay 150` or piped on stdin is answered.
  The emulator warns that the firmware's CSRA 0xCC with ACR[7] = 1 selects 19200 baud (see the
  spec issue below); data still flows because the console delivery is rate-independent.
* `--trace`, `--trace-from`, `--break`, `--exit-on-berr` (exit 2), `--dump-regs-on-exit`,
  `--dump-mem`, `--type`, `--serial-type`, stdin pipe, windowed run under the SDL dummy
  driver, ASan/UBSan build clean on the test ROM and the firmware.

## Design decisions as implemented

* Scanline slice starts at the HSYNC leading edge; flags set at slice start; live pins from the
  position inside the slice (see README "Timing model").
* `io_update_ipl()` -> `m68k_set_irq()` + `m68k_end_timeslice()` on every change (Musashi
  only checks interrupts at `m68k_execute()` entry and SR writes; INT_ACK on means it never
  auto-clears the level).  The DUART calls it whenever its IRQ output changes; its ISR is
  derived from channel state, not stored, so it can never go stale.
* DUART timing is all in CPU cycles: bit time = `CPU_CLOCK_HZ` / rate from the CSR table (set 1
  for ACR[7] = 0, set 2 for ACR[7] = 1), frame length from MR1/MR2; the counter/timer advances
  by `N * CPU_CLOCK_HZ / src_hz` with the division remainder carried, so it is exact over any
  interval even though 12 MHz / 3.6864 MHz is not an integer.  The model is synchronised at the
  end of each scanline and before every register access.
* Console input arrives at `SERIAL_BAUD` regardless of the receiver's programmed rate (that is
  what the terminal does); a rate mismatch is warned once rather than turned into framing
  errors, so a firmware with the wrong baud set still works in the emulator.
* `--type` still goes to the PS/2 keyboard (unchanged); the new `--serial-type` /
  `--serial-type-delay` feed the DUART.  Text injected this way is queued ahead of stdin.
* DUART region: odd-byte accesses are the real thing; word accesses act on the odd byte with
  0xFF in the upper half; even-byte accesses read 0xFF and reach no register; both warned once.
  "Do not access" registers (RS 2 and 10 read) return 0xFF with a one-time warning.
* `m68k_pulse_bus_error()` is called only from the memory handlers inside `m68k_execute()`.
  The read of 0x00FFFF01 that Musashi performs before HALTing on a double fault is detected
  in `bus.c` (that address is in the IACK range and never BERRs).
* IO byte access: byte read = its half of the word (register side effects happen), byte write
  = lane-masked half of the shadow.  Write-only registers read 0.  SWITCHES reads 0xFFFF.
  ROM writes ignored.  32-bit = two words.
* Soft switches latched at the start of line 0 (next frame).  Render at end of line 239,
  present at frame end.  `--pal` sets PALJMP only.
* stdout = serial (channel A) bytes only; everything else on stderr.  Exit codes 0/1/2/3.
* Not modelled: RAM slot-alignment penalty (documented), PS/2 host-to-device bit-bang (lines
  read back as driven), ACMD13/CMD6 and other rarely used SD commands (return 0x04), DUART
  parity/framing/break detection, CTS/RTS, loopback/multidrop, input-port change interrupts,
  OP-pin clock outputs, channel B receive.

## Spec / doc issues found (not changed here; emu/ only)

1. **Baud-rate set is inverted in the spec.** `spec/board.json` `serial.acr_brg_set = 1` with
   `csr = 0xCC` and the text "0xC = 38.4k with ACR[7]=1" contradict the MC68681 data sheet
   (Table 4-5, CSR): code 1100 is **38.4k in set 1 (ACR[7] = 0)** and **19.2k in set 2
   (ACR[7] = 1)**.  Set 2 is the one with 75, 150, 2000, 1800 and 19.2k.  For 38400 the
   firmware must write ACR = 0x00 (plus any counter/timer bits), i.e. `SERIAL_ACR_VALUE`
   should be 0x00 and `acr_brg_set` 0.  The emulator follows the data sheet and warns at run
   time ("channel A enabled at RX 19200.0 / TX 19200.0 baud (CSRA CC, ACR[7] = 1)"); the
   current firmware would talk at 19200 to a 38400 terminal on real hardware.
2. The spec does not say what word or even-byte accesses to the DUART region do.  CPLD1 asserts
   DUARTCS for any AS in the region, so they complete; the chip performs a register access on
   whatever is on D0..D7 for an even-byte cycle (undefined data for a write, a popped FIFO byte
   lost to the wrong lane for a read).  The emulator treats an even-byte access as reaching no
   register and a word access as the odd byte; the spec should forbid both or describe them.
3. The "Do Not Access" read locations (RS 0010 / 1010) are not mentioned in `registers.duart`;
   worth a note since reading them disturbs the real chip.
4. `generated/hw.inc` lacks `VIDEO_WIDTH` / `VIDEO_HEIGHT` (and the other `video` constants)
   that `hw.h` has; the test ROM hard-codes 320/240 with a comment.  Suggest gen.py emit them.
5. `HARDWARE.md`/spec say the IO region is "16-bit word access only"; the behaviour of byte
   accesses should be stated (this emulator: lane-masked halves) or the firmware forbidden
   from using them.
6. `SDIO` reset value 0x0003 leaves CLK high; SPI mode 0 wants CLK idle low.  Harmless (the
   driver lowers it) but 0x0001 would be the natural reset value.
7. `docs/architecture.md` section 8 says the 68681 model offers "baud-independent delivery";
   the emulator now times transmit completion and receive arrival by the bit rate, so that
   phrase could be dropped.
