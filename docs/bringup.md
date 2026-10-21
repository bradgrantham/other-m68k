# Bring-up plan

Each stage has a ROM image or test that proves one thing. Do them in order; every stage
assumes the previous ones pass.

## 0. Before power
* Visual inspection, continuity on all power pins, no shorts 5V-GND / 3V3-GND.
* No ICs in sockets. Apply 5 V from USB-C; check 5 V and 3.3 V rails, reset supervisor output
  goes high after ~150 ms, 24 MHz oscillator running.

## 1. CPLDs alone
* Program CPLD1 and CPLD2 through the JTAG header (FT232H + OpenOCD, SVF files from `hdl/`).
* With no CPU fitted: CPUCLK is 12 MHz; HSYNC and VSYNC have the right periods (63.6 us,
  16.7 ms); LATCH pulses every 333 ns; the AD724 produces a black composite picture that a TV
  syncs to.

## 2. Free-running CPU
* Fit the 68000 and the flash chips (programmed in the TL866) with `fw/test/freerun` (a ROM of NOPs / `bra` loop that never
  touches RAM). Address lines toggle; AS pulses; no BERR. A ROM that only blinks LED0 via the
  GPIO register proves the I/O path through the transceivers.

## 3. RAM
* Fit the SRAMs. Boot the real ROM. The POST runs the RAM test before using a stack and
  reports over serial; LED codes in `fw/README.md` tell you how far it got without serial.

## 4. Video
* The banner appears. Check the four alignment cases with a scope if the picture has
  columns of garbage: that would be the transceiver enable or RAMOE phase.

## 5. Keyboard, SD, audio
* Type in the monitor; `dir` on a FAT32 card; `beep`.

## 6. Run a program
* `run hello.elf` from the SD card, or upload an S-record over serial and `go`.

## Programming
* ROM: the two SST39SF040 flash chips are programmed once in the TL866 (minipro device
  `SST39SF040`): `fw/build/rom_even.bin` into U4 (D15..D8) and `rom_odd.bin` into U5 (D7..D0).
  Every later update is done in system: `rom.bin` on the SD card, `flashid`, `flash ROM.BIN`,
  answer `y`, watch the 16 dots on the serial port and the new banner (see `fw/README.md`).
  Do not press NMI or reset while it runs; a failed update is recovered with the TL866.
* CPLDs: FT232H as JTAG adapter; `openocd -f interface/ftdi/um232h.cfg` with the SVF produced
  from the JED.
