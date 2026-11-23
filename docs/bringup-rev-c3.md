# Rev C3 bring-up record

What was tested on the first rev C3 board, what failed, how each fault was found, and what
it teaches. Written 2026-11-23 with bring-up still in progress; add to it as the remaining
stages are done. The step-by-step procedure is `docs/bringup-checklist.md`; the plan for the
next board is `docs/rev2-plan.md`.

The board, its glue logic, its firmware and its case were designed in conversation with an
AI assistant and generated from `spec/board.json`. This record is also meant to show what
that way of working got right, what it got wrong, and which checks would have caught the
faults before the bench did.

## State on 2026-11-23

| stage | result |
|---|---|
| power, 3.3 V, reset, 24 MHz oscillator | work |
| both CPLDs programmed | work, but only one chip at a time in another board's socket (fault 1) |
| CPU out of reset, running from flash | works since the CPLD1 fix of 2026-11-22 (fault 2) |
| DUART, serial A at 38400 | works since the CPLD1 fix of 2026-11-23 (fault 3) |
| POST, 1 MB RAM test, banner (`842K RAM available`) | work |
| composite video | a good picture through a cable wired tip-to-shell (fault 4); text has moving false colour and the picture is wider than the screen (open) |
| PS/2 keyboard | no response (open, not investigated) |
| S-video, SD card, audio, flash drive C:, joystick, NMI | not tested yet |

## What worked the first time

Worth recording, because these are the parts of the method that held up:

* The two CPLD pinouts (62 and 56 pins) against the board netlist.
* The bus timing contract in `docs/architecture.md`: the RAM shared between the CPU and the
  video fetch on a fixed slot, DTACK alignment, the ROM overlay at reset. The RAM test passes
  and the firmware runs.
* The flash pair, the 68681 wiring, its crystal, and the serial header.
* The AD724 circuit and the sync timing. HSYNC measured 15.7065 kHz and VSYNC 59.9486 Hz,
  the design values.
* Power, reset supervisor and the USB-C supply.

## Faults, in the order found

### 1. In-circuit JTAG stops working once CPLD1 is running (2026-11-20/21)

* **Seen:** with two blank chips the chain scanned and CPLD1 programmed in place. With the
  CPLD1 image running, OpenOCD reported `IR capture error; saw 0x3ff not 0x1`, the CPLD2
  program failed verify, and that chip stopped answering until it was erased alone.
* **Worked around:** each chip is programmed alone in the Griffin Rev 1 board's ENGINE socket.
* **Cause:** not established. TCK is about 152 mm of unterminated trace to both chips.
* **Kind of fault:** board layout. Details and options: `kicad/README.md`.

### 2. The CPU started only about half the time (2026-11-22)

* **Seen:** no LED0, HALT low; pressing RESET let HALT rise for about a microsecond.
  Sometimes it booted.
* **Dead ends:** a floating AS upsetting the ROM overlay (a 10 k pull-up on AS changed
  nothing), marginal flash programming (reprogrammed, one chip swapped), solder joints
  (reflowed), missing RAM and DUART (fitted). A logic analyzer capture looked random.
* **Cause:** CPLD1's phase counter was cleared by reset, so the CPU clock stood still while
  reset was asserted. The 68000 needs its clock running during reset.
* **Fix:** the counter free-runs (`hdl/cpld1_mmu.v`, commit d35af39).
* **Kind of fault:** design error in the HDL. The simulation passed because the bus model in
  the testbench does not need a clock to come out of reset. A test now counts clock edges
  during reset.

### 3. The CPU hung on its first DUART access (2026-11-23)

* **Seen:** LED0 on and nothing else; TXD idle high; no serial output.
* **Narrowing:** supply, ground, RESET, CS, R/W and IACK at the 68681 were right. DTACK sat
  at a steady 2.0 V with CS low: something was driving it high against the 68681. No solder
  bridges by ohmmeter. With both chips pulled, 1 k to ground gave 0.87 V, so the pull-up and
  the board were right. A second 68681 behaved the same. That left CPLD1.
* **Cause:** the Atmel fitter built the DTACK pin's output enable wrong. Its report showed
  the right equation; the fuse file did not match the report. Found by decoding the
  product-term fuses and comparing them with the equations.
* **Fix:** the enable comes from a flip-flop (commit 1e2b8f2). `hdl/fit.sh` now refuses a fit
  with this pattern. Full account: `docs/atf15xx.md` section 3.6 and `hdl/STATUS.md`.
* **Found on the way:** the synthesis script deleted logic that fed only a tri-state enable.
  Fixed and checked for in the same commit.
* **Kind of fault:** tool fault. No reading of the source, the simulation or the fit report
  could show it.

### 4. No sync on the composite output (2026-11-23)

* **Seen:** colours plausible, no horizontal or vertical hold. HSYNC and VSYNC present at
  the right rates.
* **Cause:** the RCA jack's footprint has its two pads numbered the wrong way round, so the
  shell carries the video and the centre is ground. The television saw the signal inverted.
  Found with an ohmmeter in under a minute once suspected.
* **Kind of fault:** a footprint drawn from a drawing and never checked against a part.
  Details: `kicad/README.md`.

### Bench tools that did not fit the board

* The JTAG header follows the ByteBlaster pinout; the adapter in hand (FT232H) wants five
  pins in a row.
* The logic analyzer header was laid out from an assumed pinout of the analyzer. The real
  one differs in size, column order and ground position, so it is used with loose wires.
* One ground point on the whole board for scope probes.
* The case: five problems on first assembly (`enclosure/README.md`).

## What the faults have in common

| fault | where the error was | what would have caught it before the bench |
|---|---|---|
| CPU clock stopped in reset | HDL, a requirement of the 68000 that was not written down anywhere in the project | a review of each chip's datasheet for "must be true during reset"; a bus model that enforces it |
| DTACK enable | fitter | a check on the fitter's output; there was none |
| enable logic deleted | synthesis script | the same |
| RCA pads | footprint | a meter on a real part; a 1:1 print |
| analyzer header, JTAG header | assumptions about tools on the bench | measuring the tool before drawing the header |
| JTAG signal quality | layout | a rule for programming lines: series resistor, short daisy chain |
| case | geometry of real parts | a first print before the board was ordered |

Three things stand out.

1. **Nothing here was caught by the simulation**, though it has 17 tests and passed
   throughout. It tests the source against a model of the board. Two faults were below the
   source (fitter, script) and one was in the model (a bus master that did not need what the
   real CPU needs).
2. **Every fault outside the logic was a fact about a physical object taken from a document
   or from memory** instead of from the object: a jack, an analyzer, a programmer, a card
   socket. The generated flow makes it easy to carry such a fact through schematic, layout,
   order and case without anyone touching the part.
3. **The dead ends on fault 2 cost more than the fault.** Plausible theories were offered
   and tested one at a time on the bench. The quicker path, in hindsight, was to list what
   the CPU needs to leave reset and check each against the HDL.

## Carrying this to Griffin

Griffin is a separate repository with the same CPLDs and the same fitter flow. Checked on
2026-11-23 (in a scratch copy; nothing in Griffin was changed):

* **Its designs do not show the fitter fault.** Four of seven fit here with their frozen pins
  and are clean; two more are clean with free placement; the last has a single-signal
  enable. This machine's yosys differs from the one Griffin is built with, so the real check
  is on Griffin's own `.fit` files.
* **Its fit flow has the same script flaw**, dormant because every tri-state pin there is
  declared `inout`, and it has no check on the fitter's output.
* **Its documents do not mention either problem.**

Worth doing there, in rough order of value:

1. Run the two fit checks from `hdl/fit.sh` on every Griffin fit, and add them to its
   Makefile so a bad fit stops the build.
2. Confirm the CPU clock runs while reset is asserted, and look for the same class of
   unwritten requirement in each chip's datasheet.
3. Check every footprint that was drawn for the project against a part, with a meter for
   pin numbers. Connectors first.
4. Griffin's second board has six CPLDs in one JTAG chain (GLUE, PORTS, AUDIO, ENGINE,
   PIXEL, COMPOSITOR). Give the programming lines
   series resistors and a short daisy-chain route, and keep a way to program one chip alone.
5. Measure the analyzer and the JTAG adapter before drawing their headers; add ground loops
   for scope probes.
6. Treat any pin shared between a CPLD and another driver (as DTACK is here) as a place to
   look twice, in the source and in the fit.
7. Where a testbench stands in for a real chip, write down what the real chip needs that the
   model does not enforce.
