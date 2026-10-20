# hdl/ status

## Done

* `cpld1_mmu.v`, `cpld2_io.v`: complete implementations of architecture.md sections 2-5,
  Verilog-2001, one clock, async reset, no inferred memories. Lint clean (`iverilog -Wall`),
  synthesise with yosys 0.33 with no warnings about latches or undriven signals.
* `tb/`: 68000 bus master, 70 ns SRAM model with timing checks, EPROM and 74AHCT245 models,
  whole-board testbench with 12 tests, a frame/pixel monitor and a bus-contention checker.
  `make -C hdl sim` and `make -C hdl sim-notx` both end in `RESULT: PASS`
  (12 tests, 0 errors; ~20 s each). The contention checker was verified to fire when BUFEN
  is deliberately moved from E5 to E4.
* yosys: CPLD1 386 cells / 70 flops (~76 macrocells); CPLD2 487 cells / 107 flops
  (~124 macrocells) with the hardware serial TX, 404 / 88 (~104) without.

## Not done / needs the real tools

* No Atmel fit (fit1508 is Windows-only, not installed). The macrocell numbers above are
  estimates; CPLD2 with `HW_TX=1` is within 4 of the limit and may need `HW_TX=0`.
* No pin assignment file yet; depends on the KiCad symbol pinout.
* No JED/SVF produced.

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
9. **Macrocell estimate in section 7** (CPLD2 ~105 with RX+PS/2) matches the yosys-based
   estimate with `HW_TX=0` (~104). With the transmitter it is ~124.

## Open questions for the schematic

* Which 68000 inputs go on CPLD1's two spare dedicated-input pins (OE1, GCLK2/OE2) to keep
  I/O free for a composite-sync output.
* AD724 sync polarity: `SYNC_ACTIVE_LOW=1` on both CPLDs by default.
