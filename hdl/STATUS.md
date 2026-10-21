# hdl/ status

## Done

* `cpld1_mmu.v`, `cpld2_io.v`: complete implementations of architecture.md sections 2-5,
  Verilog-2001, one clock, async reset, no inferred memories. Lint clean (`iverilog -Wall`).
* `tb/`: 68000 bus master, 512Kx8 55 ns SRAM model (AS6C4008-55 timings) with timing
  checks, SST39SF040 flash model (JEDEC program/erase/ID, busy status, write-timing checks)
  and 74AHCT245 models, whole-board testbench with 15 tests, a frame/pixel monitor, a
  bus-contention checker and an MA18..17 edge checker.
  `make -C hdl sim` ends in `RESULT: PASS` (15 tests, 0 errors; ~30 s).
* **Real Atmel fits** (`make fit`, `fit.sh`, `pins.py`; tables in README.md): CPLD1 111/128
  (pass 2, fan-in 37/40, no I/O left) and CPLD2 119/128 (pass 1, 60/64 I/O, 127/128 nodes) with
  the pin blocks kept. JEDs in `build/fit/`.
* **Spec rev 5 (2026-10-21)**: audio became an 8-bit rotating PDM shifter fed by the new
  write-only AUDIO register (byte offset 10): bit 7 drives pin 25, the register rotates at the
  HSYNC leading edge and every 24 slots after it (8 bits per line, ~126 kHz, resynchronised
  each line; a line without a write repeats the byte; a write loads immediately and wins over
  a coincident rotate). GPIO bit 2 is stored but unused. Fit: 107 -> 119 macrocells, 73 -> 85
  flops, 107 -> 127 nodes+foldbacks, pins identical to the rev 4 fit, so the rev B4 boards
  take it as a JED update. New testbench test 14 checks the bit sequence, repeat and replace.
* **Spec rev 4 (2026-10-20)**: the SRAMs became two AS6C4008-55 (512Kx8), 1 MB in all. The
  two new address lines MA18..MA17 come from CPLD2 (CPLD1 has no pin left): CPU A19..A18 are
  inputs on pins 54/55, MA17/MA18 outputs on pins 56/57, loaded at the same E4/E1 edges as
  CPLD1's MA16..0 (both CPLDs run on clk24, so the SRAM sees one address change per edge).
  During the video fetch they carry `FB_PAGE_HI`, the fixed bits 19..18 of both framebuffer
  pages, which therefore live in the top 256 KB (0xDA800 and 0xED400); `tools/gen.py`
  asserts that both pages share those bits. The timing analysis does not change: the extra
  bits have the same clock-to-output as CPLD1's.
* **Spec rev 3 (2026-10-20)**: the EPROMs became two SST39SF040 flash chips (CE tied low).
  CPLD1's ROMCE is now ROMOE (same logic) and a new combinational ROMWE (real ROM region,
  write, UDS|LDS) on pin 50 lets the firmware program the flash in place; the ROM window is
  1 MB (A23..A20). The bus-master model now honours the 68000's data hold after DS (#25),
  which the flash's tDH needs and the earlier t_rel = 0 case violated.
* **Spec rev 2 (2026-10-20)**: the user chose a 68681 DUART for serial after the fitter
  showed CPLD2 could not hold both frame engines. CPLD1 decodes DUARTCS and releases DTACK in
  the DUART region; CPLD2 lost the serial receiver and gained the DUART IRQ input; INTEN,
  SDIO, GPIO, USERIO.OUT are write-only. The testbench has a 68681 bus-interface stub.

## Not done

* The pin blocks are a free-placement harvest, not yet reconciled with board routing.
* SVF conversion and programming (ATMISP under wine, or prjbureau fuseconv; openocd not
  installed).

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
   pulse. Harmless for the flash (OE pulses shorter than tOE produce no output, and the
   write itself is only recognised through the WE strobe, which follows the data strobes),
   but the doc's "the flash only ever drives the CPU bus" margin analysis assumes no such pulse. The
   simulation drives R/W and AS simultaneously and does not exercise this.
5. **TAS / read-modify-write.** With `serviced` set until AS goes high, the second half of a
   TAS cycle (AS stays low) would never receive DTACK. The 68000 only emits RMW cycles for
   TAS; the firmware must not use TAS on RAM or I/O. Worth a sentence in section 2 or in
   the firmware notes.
6. **PS2ERR semantics.** The spec says both "framing/parity error on last byte" and "W1C".
   Implemented as: assigned (set or cleared) on every completed frame, and clearable by W1C.
7. (resolved) CPLD2 takes `a[3:1]`; the spec glue list says A1..A3.
8. **Soft switches** "registered at the end of the write cycle": implemented as set while
   AS is low in the SWITCHES region with R/W low (idempotent, same visible behaviour since
   they only take effect at the frame start).
9. **Macrocell estimate in section 7** (CPLD2 ~105 with RX+PS/2) was optimistic: the real
   fitter needs 127 for that feature set and refuses it with the transmitter (see README).

## Open questions for the schematic

* Which 68000 inputs go on CPLD1's two spare dedicated-input pins (OE1, GCLK2/OE2) to keep
  I/O free for a composite-sync output.
* AD724 sync polarity: `SYNC_ACTIVE_LOW=1` on both CPLDs by default.
