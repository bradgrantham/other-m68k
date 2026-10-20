# Fitting Verilog into ATF15xx CPLDs through the Atmel fitter

Guidance for writing Verilog that fits an ATF1508AS (or ATF1504AS) when the flow
is yosys → EDIF → Atmel `fit1508.exe`.  Everything here was learned by running
the fitter on designs that ship at 118–128 of 128 macrocells.  The one rule above
all others: **the only reliable test of a change is the fitter, and above ~90 %
utilisation the fitter is noisy.**  Reason about structure; measure everything
else.

---

## 1. The silicon, as the fitter sees it

| Resource | ATF1508AS | ATF1504AS |
|---|---|---|
| Macrocells ("logic cells", LC) | 128 | 64 |
| Logic Array Blocks (LABs) | 8 (A–H) × 16 macrocells | 4 × 16 |
| Product terms (PT) per macrocell | 5 | 5 |
| Switch-matrix inputs per LAB (the **fan-in** limit) | 40 | 40 |
| User I/O, PLCC84 / PLCC44 | 64, four of them JTAG when JTAG is on | 32 |
| Dedicated inputs | 4: GCLR, OE1, GCLK1, GCLK2/OE2 | 4 |
| Global clocks | GCLK1, GCLK2, GCLK3 (GCLK3 shares an I/O pin) | GCLK1, GCLK2 |

Each macrocell has a 5-term OR array; an XOR gate one PT can feed (what
`xor_synthesis` uses); a flip-flop configurable as D, T or latch, or bypassed;
a clock from a global clock or a product term; asynchronous reset and preset
from GCLR or a product term; output enable from OE1/OE2 or a product term; a
**foldback** NAND that feeds back into the same LAB; and a **cascade** that
lends product terms to the neighbouring macrocell.  (Foldback = Altera
shareable expander; cascade = parallel expander.)

PLCC84 pins to know when writing a pin block (confirm the dual roles in the
datasheet pin table):

| Pin | Role |
|---|---|
| 1 | GCLR / input — put the asynchronous reset here |
| 2 | OE1 / input (input-only) |
| 83 | GCLK1 / input — put the main clock here |
| 84 | OE2 / GCLK2 / input |
| 81 | I/O that can be GCLK3 |
| 14, 23, 62, 71 | TDI, TMS, TCK, TDO — not available to logic while JTAG is on |

Consequences:

* **A register, a combinational node and an output pin each occupy a
  macrocell.**  A pin used only as an input leaves its macrocell free for
  buried logic.  A constant-driven output still costs a macrocell (0 PT).
* **Fan-in is per LAB and is the limit that bites first on a full chip.**  Every
  distinct signal any of a LAB's 16 macrocells consumes counts once against its
  40.  A LAB holding eight pins of a wide output bus, each pin a mux of many
  sources, is the usual victim.
* **A product term is one AND of literals.**  Width is free up to the fan-in;
  the number of distinct terms per equation is what costs.  Five per macrocell
  before the fitter must borrow (cascade) or copy (foldback).
* **Asynchronous clear/preset from GCLR is free.**  A synchronous reset is a
  literal in every affected equation, and on a full chip that alone has been
  the difference between fitting and not.
* **Inversion is free** on registered outputs; the fitter absorbs `~` into
  register polarity.

---

## 2. The toolchain

```
yosys   (run_yosys.sh NAME)    NAME.v -> NAME.edif
fitter  (run_fitter.sh NAME …) NAME.edif + //PIN: lines -> NAME.pin, NAME.fit, NAME.jed
                               (+ .tt3, .io; .vt/.tmv with -strategy debug)
fuseconv (prjbureau)           NAME.jed -> NAME.svf
openocd                        NAME.svf -> chip over JTAG
```

### 2.1 What yosys does to your Verilog

The wrapper script runs, in order: `read_liberty cells.lib`, `read_verilog`,
`tribuf`, `synth -flatten -noabc -auto-top`, `techmap` to the ATF cell library,
`simplemap`, `dfflibmap`, `abc -liberty cells.lib`, `iopadmap` (INBUF / BUF /
TRI / bibuf), `splitnets -format _`, renames, `write_edif -lsbidx`.

What that means for the code you write:

* **The design is flattened** to AND/OR/NAND/NOR/XOR/MUX gates and the register
  cells DFF, DFFE, DFFAR, DFFAS, DFFARS, DFFEARS, LATCH.  The fitter then
  re-minimises to sum-of-products per macrocell.  Module boundaries,
  parameters, functions and `generate` are conveniences only; they neither
  cost nor save anything.  "Instantiate twice" is fine.
* **Buses are split into `NAME_N` nets numbered from 0 regardless of the
  declared range.**  `input [23:18] A_hi` becomes `A_hi_0 … A_hi_5` with `A_hi_0`
  = bit 18.  Write the pin block in those names, and prefer declaring buses
  `[N-1:0]` so the names mean what they say.
* **Tristate works**: `assign A = mastering ? dma_addr : 23'bz;` on an `inout`
  or `output` maps to TRI / bibuf, with a product-term output enable unless the
  enable is literally a dedicated OE pin.
* **Latches are mappable but avoid them**; an incomplete assignment in
  `always @(*)` silently gives you one.
* **yosys re-encodes state machines to one-hot** unless you pin the encoding:
  `(* fsm_encoding = "none" *) reg [1:0] state;`
  One-hot costs ~40 % more product terms and several more macrocells on these
  parts; binary has never lost.  **Always pin `fsm_encoding = "none"` and use
  binary encoding.**
* **`-auto-top`** picks the top; with more than one module in a file, mark it:
  `(* top *) module …`.  `` `include `` paths are relative to the design
  directory.
* **Silent width truncation**: `3'd8` is 0.  When a fit improves suspiciously,
  check literal widths first.

### 2.2 The `//PIN:` block

`run_fitter.sh` does `grep '//PIN:' NAME.v | cut -d' ' -f2- > NAME.pin`, so:

```verilog
//PIN: CHIP "name" ASSIGNED TO AN PLCC84
//PIN: SYSCLK            : 83
//PIN: nRESET            : 1
//PIN: A_hi_5            : 4
//PIN: D_7               : 40
```

* One `CHIP "name" ASSIGNED TO AN PACKAGE` line, then `net : pin`, one per
  net, in **post-splitnets names** (`D_7`, not `D[7]`).
* **Never write the `//PIN:` prefix in prose anywhere in the file**: every
  matching line lands in the pin file.  Say "the pin block".
* **An empty pin file is free placement**, even under `-preassign keep`, and
  the `.fit` still says "Preassign = KEEP" (its `Preassign_file =` field is
  always blank).  If a pinout "moved", diff `NAME.pin` against the block
  before suspecting nondeterminism.
* **On a near-full chip the fitter will not choose a pin for a port missing
  from the block**: it reports Placement fail then Grouping fail on every pass
  although a free fit of the same netlist succeeds.  Give new ports explicit
  pins, chosen in a LAB with free cells and low fan-in.
* JTAG pins are not assignable.  Dedicated inputs can only be inputs; clocks
  on GCLK1, asynchronous reset on GCLR.
* Which LAB a pin belongs to is fixed by the package; read it off the pin map
  and placement lines in the `.fit`.

---

## 3. Running the fitter

### 3.1 The command

```
run_fitter.sh NAME -preassign keep \
    -strategy TDI_pullup = on -strategy TMS_pullup = on \
    -strategy power_reset on -strategy JTAG = on \
    [-strategy xor_synthesis = on]
```

(`run_fitter.sh` supplies `-tech ATF1508AS -device PLCC84 -tpd 15`; `-d`, `-p`,
`-s` override them.)  **Always fit with the same flag set.**  A fit with
different strategies or a different `-preassign` is a different experiment
whose numbers are not comparable.

### 3.2 `-preassign`

| Value | Behaviour |
|---|---|
| `keep` | Honour the pin file exactly or fail.  **Use this.** |
| `ignore` | Free placement.  Use only to **harvest** a pinout, then write it back and re-verify under `keep`. |
| `try` (default) | Honour pins if possible, **otherwise silently move them**.  The board will not match.  Never use it. |

Harvesting: empty the pin block (or fit with `ignore`), copy the pins from
`NAME.pin`, write them into the block, re-fit under `keep`, confirm identical
totals.  Expect a frozen pinout to cost a few LC and tens of PT over the free
fit.  A pinout harvested from a near-full chip is a fit of **one netlist**, not
a contract the next netlist inherits.

### 3.3 Strategies

Fitter defaults: optimize ON, Xor_synthesis OFF, Foldback_logic OFF,
Cascade_logic OFF, Output_fast ON, JTAG ON, TDI/TMS pullup OFF, power_reset
OFF, Pin-Keeper OFF, Open_collector OFF, ITD ON, Fast_inlatch off,
Latch_synthesis off, Push_gate on, Logic Doubling off.  The fitter escalates
some of these itself as passes fail.

| Strategy | What it does | Guidance |
|---|---|---|
| `xor_synthesis = on` | Lets the fitter use the macrocell XOR | The only strategy that has ever rescued a fit (designs with wide decoders / parity-like logic).  Zero effect on others.  Never combine with `debug`: the fitter crashes writing simulation files.  Does not rescue a design that is simply over. |
| `Foldback_logic` | Allow foldback NANDs | The fitter enables it itself when a pass fails; forcing it changes nothing. |
| `Cascade_logic` | Allow parallel expanders | Tried automatically at pass 2. |
| `logic_doubling` | Duplicate logic to relieve routing | Tried at pass 5; on a full chip it makes things worse. |
| `Push_gate`, `Soft_buffer`, `Optimize` | Minimiser knobs | All inside the fitter's noise or worse.  Leave at default. |
| `debug = on` | Writes `.vt`/`.tmv` | Only if something consumes them; never with `xor_synthesis`. |
| `JTAG = on`, `TDI_pullup`, `TMS_pullup` | Keep ISP alive | Always on; costs four pins. |
| `power_reset on` | Registers reset at power-up | On; measured free. |
| `pin_keep` | Weak keepers on unused I/O | Fine for unused spare pins. |
| `security` | Blow the security fuse | Never during development. |
| `output_fast`, `open_collector`, `MC_power`, `ITDn` | Slew, OC outputs, per-cell power, input transition detect | Leave alone unless the board needs them. |

### 3.4 The five passes

```
Fitter_Pass 1  LOGIC_DOUBLING : OFF
Fitter_Pass 2  CASCADE_LOGIC  : (TRY)
Fitter_Pass 3  CASCADE_LOGIC  : (OFF)
Fitter_Pass 4  NODE ASSIGN    : OFF
Fitter_Pass 5  LOGIC_DOUBLING : ON
```

A failing pass prints `## Warning : Placement fail`, `Grouping fail` or
`Routing fail` and the next is tried.  "Fits on pass 1" versus "fits on pass 5"
is a margin signal in its own right; record the pass number.

### 3.5 Failure modes

| Message | Meaning | What to try |
|---|---|---|
| `Grouping fail` on every pass | Logic cannot be partitioned into LABs under 16 cells / 40 fan-in / 80 PT each.  **Not** a raw macrocell overflow; starts around 88 % LC with wide muxes or widely shared signals. | Reduce the worst LAB's fan-in (§6), pipeline wide muxes, move pins. |
| `Placement fail` | Cannot place the macrocells the pins demand | A port missing from the pin block; or the pinout is wrong for this netlist — harvest. |
| `Routing fail` | Switch matrix cannot route the placement | Often one pin in the wrong LAB.  Try `xor_synthesis`; else move that pin. |
| `Unable to Keep Fanin of 45 on Block E` | That LAB needs 45 distinct signals, has 40 | Count what feeds that LAB's pins; §6. |
| `INTERNAL ERROR` | (a) `debug` + `xor_synthesis`; (b) **the design is at the limit** — near full, INTERNAL ERROR is the fitter's way of saying no; (c) two fitters running at once. | Re-run alone without `debug`; if it persists, the design is over. |
| `refused, N flip-flops` | More registers than macrocells; no pass attempted | Remove state. |
| Fitter dies mentioning `vendor/*.tmp` | **Parallel fitters share temp files and collide** | Only ever run one fit at a time. |

The verdict is the line `$Device PLCC84 fits` near the end of the `.fit` and a
freshly written `.jed`; do not trust the process exit status.  Delete the old
`.jed` before a fit you intend to check that way.

---

## 4. Reading the `.fit`

Read the whole file, not just the summary the wrapper prints.

1. **Header**: the command line and every strategy value in force.  Check the
   flags it used are the ones you meant.
2. **Pass log**: which passes failed and how.
3. **Placement lines**: `X is placed at pin N (MC m)` / `feedback node` /
   `foldback expander node`.  Names are yosys ids unless a net kept its name;
   `fit_report.py NAME.fit NAME.edif` maps them back and prints utilisation,
   per-LAB placement, foldback copies, fan-in detail, PT usage and equations.
4. **Pin map**: an ASCII package picture; together with the placement lines
   it tells you which LAB each pin is in.
5. **`FanIn assignment for block X [n]`**: the n signals feeding LAB X; `[40]`
   is full.
6. **Block table**:

   ```
   Logic Array Block  Logic Cells  I/O Pins  Foldbacks  TotalPT     FanIN  Cascades
   A: LC1  - LC16     16/16(100%)  8/16(50%) 4/16(25%)  71/80(88%)  (35)   0
   ```
   Cells of 16, pins (8 per LAB on PLCC84, so 8/16 is "all"), foldbacks,
   PT of 80, fan-in of 40, cascades.
7. **Totals**:

   ```
   Total dedicated input used:  3/4
   Total I/O pins used          63/64     <- includes the 4 JTAG pins
   Total Logic cells used       120/128   <- THE margin number
   Total Flip-Flop used         72/128
   Total Foldback logic used    17/128
   Total Nodes+FB/MCells        136/128   <- may exceed 100 % and still fit; not enforced
   Total Pts                    334
   $Device PLCC84 fits
   ```
   Record **LC / FF / PT / pass / tightest fan-in** for every rung; those are
   the comparable numbers.
8. **Equations**: look for equations of 15+ PTs and nodes copied as foldback
   into three or more LABs.  That is where the waste is.

---

## 5. What things cost

### Constants and arithmetic
* **A constant costs about the number of 1 bits in it.**  `cnt - 1` is
  `cnt + 16'hFFFF`, the worst case.  **Downcounters with compare-to-zero are
  disproportionately expensive.  Use upcounters that terminate on all-ones**:
  `cnt <= cnt + 1; if (&cnt) …`.  Have the CPU load `~N` or `-N` so the chip
  counts up.  When a fit is just over, audit every literal for needless 1 bits,
  down to `{2'b00, …}` versus `{2'b11, …}`.
* `>=` against a constant is cheaper than `>`.
* Behavioural counters beat hand-built toggle chains.  Let the minimiser do
  the gates; you choose the direction (up), the terminal (all-ones) and the
  reset (GCLR).
* Adders cost about two macrocells per bit.  An operand mux in front of a
  shared adder costs more than the second adder it removes.  Gating an operand
  to zero with `~bit` literals splits product terms.
* Counter width is a hard budget line: one more bit on a nearly-full chip is
  the difference between fitting, INTERNAL ERROR and "refused".  Precompute in
  the CPU rather than widen a counter.

### Registers, resets, synchronisers
* **Budget nodes + foldback, not flip-flops.**  Read muxes, status bits and
  foldback copies add ~40 % on top of the FF count in a bus-attached
  peripheral.  "100 FF, fits" can be "140 nodes, does not fit anywhere".
* Asynchronous reset on GCLR: free.  Synchronous reset: a literal in every
  equation.  An asynchronous clear from a bus signal
  (`always @(posedge CLK or posedge nAS)`) uses the macrocell's product-term
  AR and costs that PT.
* **Every asynchronous input that feeds state-transition logic gets a 2-FF
  synchroniser** (+2 FF, +1–2 LC).  Without it a state machine will hang at
  random in a state the logic says it must leave.  Inputs the CPU merely reads
  back can stay raw; that saving has been what made a chip fit.

### Muxes and fan-in
* A 4:1 mux on each bit of a 12-bit output bus pushes that bus's LAB past 40
  fan-in with cells to spare.  Pipeline: resolve one select bit a cycle early,
  the other at the output register.
* Any new per-bit path into an output bus adds one fan-in **per bit** to the
  LAB holding those pins.  A design that cannot fit on one pinout under any
  strategy can fit with ten cells to spare after swapping four bus pins into
  another LAB.  **When grouping fails, look at which pins share a LAB before
  touching logic.**
* A both-edges XOR detector doubles the PTs of everything it enables; detect
  one edge.
* When a LAB runs out of pins the fitter makes combinational copies of buried
  registers in other LABs; pin scarcity per LAB costs cells.

### Outputs and pins
* Constant outputs: a 0-PT macrocell each.  Tie such pins on the board.
* Registered versus combinational copies of a signal for a pin: both
  directions have won; measure.
* Register merging and "group by dataflow" placement are unpredictable;
  measure, do not reason.

### Encodings
* Binary state encoding, `fsm_encoding = "none"`.
* One-hot on the *wire* only when the consumer saves more than the producer
  spends; a 74HC138/139 on the board is the cheap one-hot decoder.
* Choose record encodings so the CPU precomputes complements and offsets and
  the chip only compares and counts up.

### Fitter noise
* Above ~90 % LC the same RTL lands anywhere within ±3 cells or fails grouping
  depending on which literal is removed — including on **declaration order** of
  otherwise identical logic.  Treat any edit as a re-fit.  A chip with 0–3
  spare cells has no room for a bug fix; aim to ship at or under ~120/128.

---

## 6. Diagnosing a design that does not fit

One change per fit; record every rung.

1. **Totals.**  FF > macrocells → remove state; nothing else helps.  LC ≤ ~115
   and still failing → grouping / fan-in / pins, not size.
2. **Block table and `FanIn assignment` lines.**  Any LAB at `(40)` or an
   `Unable to Keep Fanin` line names the problem.  List that LAB's pins (pin
   map) and fan-in signals (`fit_report.py`).  Which pins could live elsewhere?
   Which sources could be registered a stage earlier so the LAB sees one node
   instead of eight?
3. **Foldback and equation report.**  A node copied into 3–4 LABs: register
   once and distribute.  An equation of 15–20 PTs: pipeline, or move the
   computation into a CPU-precomputed record.
4. **Pins**: every port in the block, no JTAG pin, clock on GCLK, reset on
   GCLR, no stray prefix in prose, bus names `_N` from 0.
5. **Audit**: constants (1-bit count), downcounters, synchronous resets,
   both-edge detectors, wide enables on counters, nested if/else (flatten to
   SOP; the minimiser prefers flat logic).
6. **Only then try strategies**, one at a time, serially: `xor_synthesis`
   first; the rest are noise.
7. **Fits only with `-preassign ignore`?**  A pinout problem, not a logic
   problem: harvest if pins are still fluid, else find the pin swap that frees
   the LAB.
8. **Fits at 126–128 only sometimes?**  It does not fit.  Remove a feature or
   add a chip.

---

## 7. Patterns that fit well

* **Device-clocked, flat, registered.**  One clock on GCLK1; asynchronous
  reset on GCLR; every output a register or a one-PT decode of registers.
  Outputs that change once per clock are also the ones whose timing you can
  analyse.
* **Upcounters to all-ones**, complemented or negated loads, `&cnt` terminal
  detect.
* **Precomputed records the chip walks** rather than arithmetic the chip does.
  The CPU writes strides, complemented counts and offsets; the CPLD compares
  and strobes.  Divides, multiplies and table lookups belong in the CPU or in
  a ROM/RAM beside the chip.
* **Address decode as one strobe per register**, blocks aligned so decode is a
  few literals; byte-wide registers on one half of the data bus rather than
  16-bit registers that double input fan-in.
* **Read muxes are expensive.**  Every readable bit adds fan-in to the data-bus
  LAB.  Read back only what the CPU needs.
* **2-FF synchronise anything asynchronous that drives state**; leave
  CPU-polled inputs raw.
* **Whole-frame engines, one interrupt per frame**, shifting on the device
  clock; never a per-bit interrupt the CPU must service in time.
* **One function per chip.**  Two serial frame engines in one ATF1508 do not
  fit; an eight-voice mixer with a noise generator does not fit.  Split by
  measurement, merge back by measurement.
* **Use the board** for one-hot decode (74HC138/139) and constants (tied pins).

---

## 8. Workflow discipline

* **Fit before anything else.**  A Verilog change is incomplete until it fits
  with the production flags against the current pin block; if it does not,
  change nothing downstream.
* **Ladders.**  Each experiment is a sequence of rungs, each fitted alone,
  each recorded as LC / FF / PT, pass, tightest fan-in; rejected alternatives
  are recorded too.  Testbench expectations are derived before simulating,
  never tuned to the output.
* **One structural change per rung**; combined changes cannot be attributed
  when the fitter is noisy.
* **Serial fits only.**
* **Verify the pinout after every fit**:
  `diff <(grep '//PIN:' NAME.v | cut -d' ' -f2-) NAME.pin`
* **State in the pin block header** whether the pinout is frozen or proposed,
  and list differences from the previous assignment.
* **Keep a testbench per chip** with negative-control mutants that must fail.
  The bench is the spec; the `.fit` is the budget.
* **Testbenches prove cycles, not nanoseconds.**  See §9.

---

## 9. Timing, briefly

Take every number from the datasheet AC table for your speed grade; the shape
of the argument is:

* A registered output on a global clock changes tCO after the edge and must be
  sampled tSU before the next.  At a 70 ns clock a same-clock neighbour has
  tens of ns to spare; at a 40 ns pixel clock it is ~20 ns, and a one-clock
  pulse is narrower than many consumers' minimum pulse width — emit square
  waves or multi-clock pulses instead.
* Product-term clocks, both-edge sampling, gated clocks and reliance on output
  hold are sub-cycle cleverness.  Write a margin table at the worst corner,
  require at least 25 % of the period or 10 ns, whichever is larger, and have
  a boring fallback.  Default to the fallback.
* Any claim about an external part (FIFO access time, SRAM write pulse, ATA
  IORDY) names the parameter, part and speed grade and shows the inequality.
* An asynchronous input is only as good as its 2-FF synchroniser; design the
  protocol so one clock of latency is harmless.

---

## 10. Sizing rules of thumb (ATF1508AS)

| Thing | Approximate cost |
|---|---|
| Bus-attached peripheral: decode, a dozen readable bits, DTACK | 15–20 LC before it does anything |
| Serial frame engine (PS/2-class RX+TX, shifters, counters, status) | 40–50 LC; two of them do not fit one chip |
| 8-bit counter with load and compare | 10–12 LC |
| 2-FF synchroniser | 2–3 LC |
| One address-decode strobe | ~1 LC |
| 12-bit output bus with a three-way source mux | one whole LAB's fan-in |
| Constant output pin | 1 LC, 0 PT |
| Full 68000-class system glue (decode, wait states, bus error, IRQ encoder, one frame engine) | ~120 LC — the whole chip |

A realistic ceiling for a shippable design is ~120/128 LC with no LAB above
~36/40 fan-in; beyond that every edit is a gamble.

---

## 11. Checklist before calling the fitter

- [ ] Clock on GCLK1; asynchronous reset on GCLR; dedicated inputs carry inputs only.
- [ ] `(* fsm_encoding = "none" *)` on every state register; binary encoding.
- [ ] No `cnt - 1 … == 0`; upcounters, all-ones terminal, complemented loads.
- [ ] Every asynchronous input into a state machine is 2-FF synchronised; CPU-polled inputs are not.
- [ ] Constants audited for 1 bits.
- [ ] Wide muxes onto output buses pipelined; output-bus pins spread across LABs when the fan-in table says so.
- [ ] Every port in the pin block, `_N` bus names, no JTAG pins, no stray prefix in prose.
- [ ] Production flags; `-preassign keep`; one fit at a time; old `.jed` removed.
- [ ] Afterwards: `$Device … fits`, pass number, LC / FF / PT / fan-in recorded, `.pin` diffed against the block, testbench still passes.

---

## 12. References

* ATF1508AS and ATF1504AS datasheets (Atmel / Microchip): architecture, pin
  tables, AC characteristics.
* Atmel ATF15xx fitter documentation; `fit1508.exe` with no arguments prints
  the option list.
* hoglet67/atf15xx_yosys: `run_yosys.sh`, `run_fitter.sh`, `cells.lib`,
  `techmap.v`.
* whitequark/prjbureau: `util.fuseconv` JED→SVF and the fuse-map description
  of the ATF15xx macrocell.
* `cpld/fit_report.py` in this repo for readable `.fit` reports.
