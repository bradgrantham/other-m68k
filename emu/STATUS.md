# emu/ status

**State: complete and passing its self-test.** `make -C emu` builds `build/m68k-emu` with no
warnings in the emulator's own sources (`-Wall -Wextra -std=gnu11 -O2`); `make -C emu test`
runs the test ROM headless and all 26 checks pass; `make -C emu clean` works; the top-level
`make emu` works. See README.md for usage and the timing model.

## What exists

| File | Purpose |
|---|---|
| `main.c` | options, Musashi init, per-scanline loop with overshoot carry, real-time pacing, SDL events (F11 reset, F12/Pause NMI), trace/break hook, register and memory dumps |
| `bus.c/h` | decode from `generated/hw.h`, RAM/ROM, reset overlay, 68000 group-0 BERR frame, rate-limited BERR log, double-fault HALT detection, disassembler peeks |
| `io.c/h` | CPLD2 registers (INTSTAT W1C + live pins, INTEN, SDIO, GPIO, PS2DATA, SERDATA, USERIO, ID), IPL recomputation, soft-switch latches, `--io-trace` |
| `video.c/h` | NTSC/PAL tables, line/flag timing, live HSYNC/DE/VSYNC pins, RGB332 -> ARGB8888, SDL window, title with LEDs |
| `serial.c/h` | bit-bang TX decoder (edge list, centre sampling), `--hw-serial-tx`, stdin RX with tty raw mode |
| `ps2.c/h` | scan code set 2 keyboard with E0/F0, 1 ms per byte, `--type` |
| `sdcard.c/h` | SPI mode 0 SDHC card: CMD0/1/8/9/10/12/13/16/17/18/24/25/55/58/59, ACMD41 |
| `audio.c/h` | AUDIO edges -> 48 kHz mono, SDL queue |
| `m68kconf.h` | 68000-only Musashi config (unchanged from the previous session) |
| `musashi/` | vendored core with `musashi-berr-frame.patch` applied |
| `test/test.S`, `test.ld`, `Makefile`, `check.sh` | self-test ROM and checker |

## Verified behaviour (from `make -C emu test` and ad-hoc runs)

* 120 NTSC frames = exactly 120 * 764 * 262 cycles; after SW_PAL the ROM counts 312 HSYNC
  flags per VBLANK, 262 again after SW_NTSC.
* "HELLO\n" bit-banged at 104 clocks/bit is decoded; hardware TX bytes merge in time order.
* VBLANK autovector (level 1, vector 25) fires once per frame; INTSTAT read inside the handler
  shows VBLANK + VSYNCPIN with DEPIN clear.
* Reading 0x400000 gives one BERR; the stacked frame carries fault address 0x400000 and
  access info 0x0015 (read, FC 5); the handler can RTE to a patched PC.
* SD init sequence and CMD17 of block 0 from a 1 MB image succeed, OCR has CCS.
* The real firmware (`fw/build/rom.bin` as built by the fw agent) boots: POST, RAM test,
  banner and monitor prompt appear on the decoded serial output; `help` typed into stdin after
  boot is answered.  Input piped in during POST is overwritten in the holding register (as on
  hardware) -- `--io-trace` makes that visible.
* `--trace`, `--trace-from`, `--break`, `--exit-on-berr` (exit 2), `--dump-regs-on-exit`,
  `--dump-mem`, `--type`, stdin pipe, paced windowed run (SDL dummy driver: 60 frames in
  1.0 s), ASan/UBSan build clean on the headless and windowed test runs.

## Design decisions as implemented

* Scanline slice starts at the HSYNC leading edge; flags set at slice start; live pins from the
  position inside the slice (see README "Timing model").
* `io_update_ipl()` -> `m68k_set_irq()` + `m68k_end_timeslice()` on every change (Musashi
  only checks interrupts at `m68k_execute()` entry and SR writes; INT_ACK on means it never
  auto-clears the level).
* `m68k_pulse_bus_error()` is called only from the memory handlers inside `m68k_execute()`.
  The read of 0x00FFFF01 that Musashi performs before HALTing on a double fault is detected
  in `bus.c` (that address is in the IACK range and never BERRs).
* IO byte access: byte read = its half of the word (register side effects happen), byte write
  = lane-masked half.  SWITCHES reads 0xFFFF.  ROM writes ignored.  32-bit = two words.
* Soft switches latched at the start of line 0 (next frame).  Render at end of line 239,
  present at frame end.  `--pal` sets PALJMP only.
* stdout = serial bytes only; everything else on stderr.  Exit codes 0/1/2/3.
* Not modelled: RAM slot-alignment penalty (documented), PS/2 host-to-device bit-bang (lines
  read back as driven), ACMD13/CMD6 and other rarely used SD commands (return 0x04).

## Spec / doc issues found (not changed here; emu/ only)

1. `spec/board.json` `video.line_origin` text says the VBLANK flag sets at "line v_active +
   v_bottom" which matches the INTSTAT bit description and this emulator; `docs/architecture.md`
   section 4 ("VBLANK flag = rising edge of VSYNC") agrees.  No conflict remains in the current
   files, but STATUS from the previous session flagged an older wording; nothing to do.
2. `generated/hw.inc` lacks `VIDEO_WIDTH` / `VIDEO_HEIGHT` (and the other `video` constants)
   that `hw.h` has; the test ROM hard-codes 320/240 with a comment.  Suggest gen.py emit them.
3. `HARDWARE.md`/spec say the IO region is "16-bit word access only"; the behaviour of byte
   accesses should be stated (this emulator: lane-masked halves) or the firmware forbidden
   from using them.
4. `SDIO` reset value 0x0003 leaves CLK high; SPI mode 0 wants CLK idle low.  Harmless (the
   driver lowers it) but 0x0001 would be the natural reset value.
5. `docs/architecture.md` section 8 says "12e6/59.95 ≈ 200,167 cycles per frame ...
   764*262/..." -- the exact figure is 764*262 = 200,168; minor wording.
