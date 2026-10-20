# hdl/ status

## Done

* `cpld1_mmu.v`, `cpld2_io.v`: complete implementations of architecture.md sections 2-5,
  Verilog-2001, one clock, async reset, no inferred memories. Lint clean (`iverilog -Wall`).
* `tb/`: 68000 bus master, 70 ns SRAM model with timing checks, EPROM and 74AHCT245 models,
  whole-board testbench with 12 tests, a frame/pixel monitor and a bus-contention checker.
  `make -C hdl sim` ends in `RESULT: PASS` (12 tests, 0 errors; ~20 s).
* **Real Atmel fits** (`make fit`, `fit.sh`; results table in README.md):
  CPLD1 fits at 98/128 on pass 1. CPLD2 as written (no hardware TX, write-only control
  registers, slot-tick UART counter, sentinel PS/2 shifter) reaches 127/128: it "fits" but
  has no margin; the original with the hardware transmitter is refused outright.

## Decision pending (CPLD2 budget)

Either frame engine must leave CPLD2 (both measured at ~105/128):

* `-DNO_PS2HW`: PS/2 becomes an interrupt per clock edge (PS2RDY = W1C edge flag, data bit
  read live); the BIOS shifts bits in the level-2 ISR and bit-bangs host->device commands
  with interrupts off. No board change. Emulator must model PS/2 at bit level.
* `-DNO_SERHW`: serial moves to an external UART (68681 DUART): one more chip and crystal,
  a chip-select and open-drain DTACK sharing in CPLD1, but hardware TX/RX with FIFOs, a
  second port and a timer. The CPLD2 serial receiver and SERDATA go away.

Already decided by the fitter (spec changes to make once the above is chosen): no hardware
serial TX (SERDATA write ignored, SERTXBUSY reads 0); INTEN, SDIO, GPIO, USERIO.OUT are
write-only (read as 0); PS/2 watchdog is ~3 HSYNC periods.

## Not done

* Pin assignment blocks (`//PIN:`) and `-preassign keep` fits; depend on the board pinout.
* SVF conversion and programming.

## Findings against docs/architecture.md (please review)

1. **Alignment table, S0 at E2: 7 clocks, not 6.** S0 at E2 puts S2 at E4, so AS is low at
   229 ns, after the E4 decision; the cycle is serviced in the next slot with DTACK at
   E4' (500 ns). S4 began at E6 (250 ns) and the CPU samples DTACK at E7, E1', E3', E5'; it
   sees it at E5' (541 ns), so S5..S7 run E5'..E7' and the cycle ends at E0'' (667 ns):
   (667 - 83) / 83.3 = 7 CPU clocks. The E4 case (S0 at 167, end at 667) is 6 as stated.
   The sim measures 4/5/7/6 for E0/E6/E2/E4. Since every serviced cycle ends at E0 and the
   68000 issues cycles on even clocks thereafter, the E2 (and E6) case still only occurs once
   after reset, so the performance statements stand; only the table entry is wrong.
2. **E0 row does not mention RAMOE.** The schedule turns RAMOE on at E2 (video) and leaves it
   on through E4 for a CPU read, but never says when it goes off after a CPU read. The HDL
   turns it off at E0 (data was sampled at E7); leaving it on until the next E4 would also be
   contention-free. Worth one line in the table.
3. **RAMOE at E2 "on (video read)".** Implemented as "on at E2 only in fetch slots" (DE
   high). The doc could be read as unconditional. Either works; unconditional costs nothing
   in logic but toggles the SRAM in blanking for no reason.
4. **ROMCE glitch on writes.** `ROMCE = region & AS & R/W` is combinational; the 68000
   drives R/W low up to 20 ns *after* asserting AS on a write (spec #20A), so a write to a
   ROM-region address (or to RAM while the overlay is set) can produce a sub-20 ns ROMCE
   pulse. Harmless for the EPROM, and such writes are software errors anyway, but the doc's
   "the EPROM only ever drives the CPU bus" margin analysis assumes no such pulse. The
   simulation drives R/W and AS simultaneously and does not exercise this.
5. **TAS / read-modify-write.** With `serviced` set until AS goes high, the second half of a
   TAS cycle (AS stays low) would never receive DTACK. The 68000 only emits RMW cycles for
   TAS; the firmware must not use TAS on RAM or I/O. Worth a sentence in section 2 or in
   the firmware notes.
6. **PS2ERR semantics.** The spec says both "framing/parity error on last byte" and "W1C".
   Implemented as: assigned (set or cleared) on every completed frame, and clearable by W1C.
7. **Section 5 register select says A1..A4 in `spec/board.json` glue inputs** ("A1..A4") while
   there are eight registers (A1..A3). CPLD2 takes `a[3:1]`; the A4 input is unused and
   could be dropped from the pin list, or kept as a spare.
8. **Soft switches** "registered at the end of the write cycle": implemented as set while
   AS is low in the SWITCHES region with R/W low (idempotent, same visible behaviour since
   they only take effect at the frame start).
9. **Macrocell estimate in section 7** (CPLD2 ~105 with RX+PS/2) was optimistic: the real
   fitter needs 127 for that feature set and refuses it with the transmitter (see README).

## Open questions for the schematic

* Which 68000 inputs go on CPLD1's two spare dedicated-input pins (OE1, GCLK2/OE2) to keep
  I/O free for a composite-sync output.
* AD724 sync polarity: `SYNC_ACTIVE_LOW=1` on both CPLDs by default.
