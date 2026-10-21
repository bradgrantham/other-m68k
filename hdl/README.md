# hdl/ -- glue logic for the other-m68k board

Two ATF1508AS CPLDs (PLCC84, 5 V, 128 macrocells each) implement everything between the
68000, the SRAMs, the flash ROMs, the AD724 video encoder and the peripherals. The mechanism is
specified in `docs/architecture.md`; every address, bit position and timing constant comes
from `generated/board_params.vh` (generated from `spec/board.json`), which both modules
`include`. Nothing from the spec is hard-coded here.

| file | contents |
|---|---|
| `cpld1_mmu.v` | CPLD1 "MMU": CPU clock, slot sequencer, decode, overlay, DTACK/BERR/VPA, SRAM address mux and strobes, video counters, soft switches |
| `cpld2_io.v` | CPLD2 "I/O + shifter": pixel path, registers, interrupt controller, PS/2 receiver, DUART IRQ routing, upper SRAM address bits MA18..MA17 |
| `tb/tb_m68k_bus.v` | behavioural 68000 bus master with datasheet delays |
| `tb/tb_sram.v` | 512Kx8 55 ns SRAM model (AS6C4008-55 class) with read timing and write-timing checks |
| `tb/tb_system.v` | whole-board testbench: both CPLDs, 2 SRAM, 2 74AHCT245, 2 flash, bus master, frame monitor, contention checker, MA18..17 edge checker, 15 tests |
| `Makefile` | `sim`, `wave`, `lint`, `synth`, `fit`, `clean` |
| `fit.sh` | yosys -> EDIF -> Atmel fitter (wine); see "Synthesis and fitting" |
| `pins.py` | harvest the fitter's pin choices into the sources' pin blocks / check them |
| `STATUS.md` | what is done, what was found, what is open |

```
make -C hdl sim        # iverilog + vvp; ~20 s; nonzero exit on any failing test
make -C hdl fit        # Atmel fitter: build/fit/*.{fit,jed} (needs the wine setup below)
make -C hdl wave       # writes build/tb_system.vcd (first 2 ms; +vcd_all for everything)
make -C hdl lint       # iverilog -tnull on the two CPLD sources
make -C hdl synth      # yosys generic synthesis + cell counts -> build/synth_*.log
```

## Coding conventions (CPLD-friendly Verilog-2001)

* One clock, `clk24`, posedge only. One asynchronous active-low reset, `sysreset_n`, on every
  flop (the ATF1508AS global clear). No latches, no inferred memories, no `initial` in the
  synthesisable sources.
* Each module is a single `always` process plus continuous assigns. All outputs are
  registered except where `architecture.md` says they are combinational: ROMOE, ROMWE, BERR, VPA,
  the immediate (ROM/SWITCHES) half of DTACK, and the UDS/LDS gating inside WEU/WEL.
* Tri-state only on CPLD2's data bus and the open-drain pins (`pin = drive_low ? 0 : z`).
* Active-low pins carry a `_n` suffix. Internal copies are active-high.
* `board_params.vh` is included *inside* each module (its `localparam`s are illegal at file
  scope in Verilog-2001); the include guard is `undef`'d first so that several modules in one
  compilation unit all see the constants.

## Time base

All timing is on the 24 MHz grid: edges **E0..E7**, the interval after En is **Fn**, eight
fast clocks form one **slot** (4 CPU clocks, 333 ns). CPLD1's phase counter `ph` holds n
during Fn, so "at En" is coded as `if (ph == n-1)`. `cpuclk = ~ph[0]` is high during
F0/F2/F4/F6: E0 is a CPU rising edge, S0 of a zero-wait cycle starting at E0 maps onto
S0=E0 ... S7=E7.

CPLD2 has its own counter `ph2`, loaded with 4 on the edge at which `latch` is sampled high
(LATCH is high only during F3), so it is locked to CPLD1 after the first slot (the testbench
asserts this on every clock).

## Per-edge schedule

| edge | `ph` before | CPLD1 | CPLD2 |
|---|---|---|---|
| **E0** | 7 | WE phase off, BUFEN off, IOCS off, DTACK(RAM/IO) off, RAMOE off. `serviced <= 1` if this slot serviced a cycle. H counter advances; DE for the new slot; HSYNC/VSYNC/V counter/VA reload/PAL change at the appropriate slot. | `pix <= active ? lo : 0` (second pixel). Write capture: if IOCS low and R/W low, latch D into the selected register (W1C for INTSTAT; AUDIO loads the PDM shifter). Read side effects: PS2DATA read clears PS2RDY. Every 24th E0 after HSYNC rotates the PDM shifter. |
| **E1** | 0 | `ma <= va` | |
| **E2** | 1 | RAMOE on if this slot fetches (`de`) | |
| **E3** | 2 | LATCH <= 1 | |
| **E4** | 3 | LATCH <= 0. `ma <= a[17:1]`. `va <= va+1` if fetching. **Decision**: `svc = AS low & (RAM or IO) & !serviced`; `service <= svc`; DTACK <= svc; RAMOE <= svc & RAM & R/W (read). | `latch` sampled high: `ph2 <= 4`; `pix <= de ? d[15:8] : 0; lo <= d[7:0]; active <= de`. |
| **E5** | 4 | if `service`: BUFEN on, IOCS on if IO | CPLD2 drives D combinationally while IOCS low & R/W high (F5..F7). |
| **E6** | 5 | if `service` & RAM & write: WE phase on. `weu_n = ~(we_phase & ~uds_n)`, `wel_n = ~(we_phase & ~lds_n)`. | |
| **E7** | 6 | nothing | |

Every fast edge (independent of phase): `serviced` clears and the overlay-clear bookkeeping
runs when AS is sampled high; soft-switch flops are set while AS is low in the SWITCHES
region with R/W low (idempotent, so "end of the write cycle" and "during the write cycle"
are the same thing).

### Cycle lengths by alignment (measured by the testbench)

| S0 at | serviced | CPU clocks | note |
|---|---|---|---|
| E0 | this slot | 4 | zero wait |
| E6 (previous slot) | this slot | 5 | DTACK (E4) seen at E5 |
| E2 | next slot | **7** | AS low at 229 ns misses E4 (see STATUS.md) |
| E4 | next slot | 6 | |

ROM, SWITCHES and overlay accesses take 4 clocks at any alignment (immediate DTACK).

### Timing as simulated

The testbench adds 7.5 ns to every CPLD1 output (including CPUCLK), 10 ns to the
transceivers, and models the SRAM and flash access times. Observed: SRAM write pulse 83 ns
(>= 45), data valid to WE high 115 ns (>= 25), SRAM address stable across the whole WE pulse,
no overlap of drivers on either bus at any time during 127 ms of simulation (including the
SRAM -> transceiver hand-off at E4/E5 and the transceiver -> SRAM hand-off at E0/E2).

## CPLD1 `cpld1_mmu` (parameter `SYNC_ACTIVE_LOW = 1`)

| signal | dir | description |
|---|---|---|
| `clk24`, `sysreset_n` | in | 24 MHz oscillator (GCLK pin), system reset (GCLR pin) |
| `a[23:1]`, `as_n`, `uds_n`, `lds_n`, `rw` | in | 68000 |
| `cpuclk` | out | 12 MHz to the 68000 |
| `dtack_n` | out | registered slot-aligned part OR combinational immediate part |
| `berr_n` | out | combinational: AS & unmapped & !IACK |
| `vpa_n` | out | combinational: AS & IACK (A23..A4 all ones) |
| `ma[16:0]` | out | SRAM word address bits 16..0: VA during F1..F3, CPU A17..A1 during F4..F0 (bits 18..17 come from CPLD2, same edges) |
| `ramoe_n`, `weu_n`, `wel_n` | out | SRAM strobes |
| `romoe_n` | out | combinational: AS & (ROM or overlaid RAM) & R/W; the flash chips' CE is tied low |
| `romwe_n` | out | combinational: AS & real ROM region & !R/W & (UDS or LDS): flash write strobe, both chips |
| `iocs_n` | out | to CPLD2, F5..F7 of a serviced I/O cycle |
| `bufen_n` | out | 74AHCT245 enable, F5..F7 of a serviced RAM/IO cycle |
| `latch` | out | F3 of every slot |
| `de` | out | fetch slot (slot < H_ACTIVE on line < V_ACTIVE), presented in the slot of the fetch |
| `hsync`, `vsync` | out | sync, active-low at the pin by default |

Decode (section 3 of the architecture doc) uses `*_DECODE_BITS` and `*_BASE` from the
parameters file. `overlay` is set by reset and cleared on the first AS rising edge after an
AS-low cycle whose address is in the real ROM region. While set, RAM-region accesses are
treated as ROM (immediate DTACK, ROMOE on reads, writes get DTACK but no ROMWE).

Video: `h` counts slots, advancing at E0; `v` counts lines, advancing at the slot where
HSYNC begins; `vact` tracks `v < V_ACTIVE`; `de` is set at the slot wrap (if `vact`) and
cleared when `h` reaches H_ACTIVE. VA reloads from the `page` flop when `v` wraps to 0, and
the `pal` flop takes the value of `pal_req` at the same moment, so a standard change never
lands mid-frame. All compare constants are 2:1 muxes between the NTSC and PAL values
(identical values fold away).

Reset state: `ph=0`, `h=0`, `v=0`, `de=1`, `va=FB_PAGE0_WORD`, `overlay=1`, `page=0`,
`pal=0`, all strobes inactive.

## CPLD2 `cpld2_io` (parameter `SYNC_ACTIVE_LOW = 1`)

| signal | dir | description |
|---|---|---|
| `clk24`, `sysreset_n` | in | |
| `a[3:1]`, `iocs_n`, `rw` | in | register select, I/O strobe from CPLD1, 68000 R/W |
| `latch`, `de`, `hsync`, `vsync` | in | from CPLD1 |
| `d[15:0]` | inout | SRAM-side data bus; driven while `iocs_n` low and `rw` high |
| `ipl_n[2:0]` | out | registered, active-low encoded level |
| `rgb[7:0]` | out | R2 R1 R0 G2 G1 G0 B1 B0, registered pixel |
| `sd_cs`, `sd_clk`, `sd_mosi` / `sd_miso` | out / in | SDIO register bits |
| `ps2_clk`, `ps2_dat` | inout | open drain, driven low when the GPIO bit is 0 |
| `a18`, `a19` | in | 68000 A18, A19 (spec rev 4) |
| `ma17`, `ma18` | out | SRAM word address bits 17..18: `{a19,a18}` loaded at E4 (LATCH high), `FB_PAGE_HI` loaded at E1 (`ph2 == 0`); the same edges at which CPLD1 loads `ma[16:0]` |
| `audio` | out | bit 7 of the 8-bit PDM shifter loaded by the AUDIO register (spec rev 5): rotated left at the HSYNC leading edge and every 24 slots after it, so 8 bits per line MSB first (~126 kHz); a line without a write repeats the byte |
| `led[1:0]` | out | GPIO bits 4..3 |
| `paljmp`, `nmibtn` | in | read in INTSTAT; `nmibtn` high forces IPL 7 |
| `uio[3:0]` | inout | open drain; USERIO bits 3..0 drive, pin levels read in bits 11..8 |

Registers (word offsets `IO_*_OFF`, bit positions `*_BIT`, all from the parameters file):
INTSTAT (flags + live pins; W1C on VBLANK, HSYNC, PS2ERR), INTEN, SDIO, GPIO, PS2DATA,
AUDIO, USERIO, ID (`ID_VALUE`). Reset values INTEN 0x0000 / SDIO 0x0001 / GPIO 0x0003 /
AUDIO 0x0000 / USERIO 0x000F as in the spec. Address bits A4..A15 are not decoded (the register block is mirrored).

* **Interrupts**: VBLANK flag on the rising edge of the (positive-logic) VSYNC, HSYNC flag
  on the rising edge of HSYNC, PS2RDY on a completed PS/2 frame.
  `ipl_n` is the complement of the highest `IRQ_*_LEVEL` whose flag and enable are both set
  (computed as a max, so it follows the levels in the spec), or `IRQ_NMI_LEVEL` while the
  synchronised `nmibtn` is high. Flag set wins over a simultaneous clear.
* **PS/2 receiver**: two-flop synchroniser on PS2_CLK plus one more flop for falling-edge
  detection. Bit 0 is the start bit (must be 0), bits 1..8 data LSB first into `ps2_sh`,
  bit 9 parity (accumulated into `ps2_par`), bit 10 stop: `ps2data <= ps2_sh`,
  `PS2RDY <= 1`, `PS2ERR <= !(stop & odd-parity-ok)`. A 3-bit watchdog counts HSYNC rising
  edges since the last PS/2 clock edge and aborts the frame after 3 (about 190 us).
* **Serial**: none. The 68681 DUART on the CPU bus does it (spec rev 2); CPLD2 takes the
  DUART's open-drain IRQ on `duart_irq_n`, synchronises it, shows it as INTSTAT.DUARTIRQ and
  encodes level 3 when INTEN.DUART is set.
* **Write-only control registers**: INTEN, SDIO, GPIO, AUDIO and USERIO.OUT read as 0 (the BIOS
  keeps shadows).
* **PDM audio** (spec rev 5): `aud_sh[7:0]` with `audio = aud_sh[7]`; a write to AUDIO loads it,
  otherwise it rotates left at `hs_rise` and whenever the 5-bit slot counter `aud_cnt` (reset at
  `hs_rise`) reaches 23 at an E0 edge, i.e. 8 rotations per 191-slot NTSC line (the last bit
  period is 7 slots short). A write wins over a coincident rotate. GPIO bit 2 is stored but
  drives nothing. Cost: 13 flops, +12 macrocells; CPLD2 is at 127/128 nodes, so this is the
  last thing that fits. Every readable bit costs a product term and a fan-in on the data-bus LAB.
* **Pixel path**: `pix` (the RGB pins) and `lo` as in the architecture doc (both zeroed when
  DE is low, so no `active` flop); pixels are 4 fast clocks each, upper byte first.

## Testbench

`tb_system` instantiates the two CPLDs, the bus master, two SST39SF040 flash lanes (512Kx8,
tACC 70, tOE 35, float 20 ns after OE; JEDEC program/erase/ID with datasheet busy times
and write-timing checks), two 512Kx8 SRAMs (AS6C4008-55: tAA 55, tOE 25, tHZOE 20, tWP 45, tDW 25),
and two 74AHCT245 (10 ns). The 19-bit SRAM address is `{ma18, ma17}` from CPLD2 and `ma[16:0]`
from CPLD1, both through the same pin delay. CPLD1 outputs pass through
7.5 ns delays before reaching anything (including CPUCLK, so the CPU/CPLD skew matches the
analysis in the architecture doc). The bus master samples DTACK/BERR/VPA through a 20 ns
"setup" delay and read data through a 10 ns one, i.e. a signal has to be valid that long
before the sampling edge to count.

Monitors running throughout:

* **Contention**: driver counts for the CPU bus (68000 write data, flash, transceiver A side)
  and the SRAM-side bus (SRAM, transceiver B side, CPLD2) using each model's drive flag; any
  overlap lasting >= 1 ns is an error. The byte halves are also required to agree.
* **Phase lock**: `u1.ph == u2.ph2` on every clock after the first slot.
* **MA18..17**: CPLD2's two address bits may only change at the E1 and E4 edges (the edges
  at which CPLD1 loads `ma[16:0]`), must equal `FB_PAGE_HI` after E1 and the CPU's A19..A18
  after E4; also counts the changes (non-vacuity).
* **Frame monitor**: slots per line, fetches per line, lines and VSYNC lines per frame,
  every fetch address against `page base + v*160 + h`, and (when enabled) every pixel on
  the RGB pins against the SRAM models' contents.
* **SRAM model**: tWP, tDW, data X/Z at WE rise, address movement during WE low; tracks
  the minima seen.

Tests (all pass; the suite runs ~143 ms of board time in ~30 s):

1. reset vector fetch through the overlay (4 clocks), write under overlay ignored (no ROMWE),
   overlay clears on the first E0xxxx access, RAM visible afterwards
2. RAM word/byte write/read at S0 = E0/E2/E4/E6 with write-data release 0 and 60 ns; cycle
   lengths 4/7/6/5; back-to-back 4-clock cycles
   (2b, spec rev 4) RAM above 256 KB: a word in each 256 KB quarter lands at the right
   19-bit SRAM word (no aliasing through MA18..17), top word 0xFFFFE and bytes 0xFFFFF /
   0x80000 at two alignments, an untouched word keeps its pattern, a write to framebuffer
   page 0 (0xED400) lands at `{FB_PAGE_HI, FB_PAGE0_WORD}`
3. ROM word/byte reads 4 clocks at every alignment; a stray write to ROM gets DTACK in 4
   clocks, one ROMWE pulse per lane (>= 40 ns), ROMOE high throughout, array unchanged
   (3b) flash: software ID on both lanes, program a word (busy status seen, reads back,
   neighbour untouched), sector erase (both lanes busy, 4 KB per chip erased, neighbours
   and the vector sector untouched), program after erase; no flash timing violations
4. BERR above RAM (0x100000), below ROM, above SWITCHES, at 0x800000; VPA for IACK; edges of
   regions (top of the 1 MB RAM and of the 1 MB ROM window mapped)
5. INTEN/GPIO/SDIO/USERIO/ID read/write and reset values, pins follow registers, INTSTAT
   live pins, IO mirror, IO cycle alignment, NMI button -> IPL 7
6. PAGE1 soft switch: first fetch of the next frame from `{FB_PAGE_HI, FB_PAGE1_WORD}` (all
   19 address bits checked), back to PAGE0
7. one full NTSC frame: 262 lines, 240 active lines of 160 fetches, 3 VSYNC lines, 191
   slots per line, every fetch address and every pixel correct (part of line 5 written over
   the bus first); VBLANK/HSYNC flags, W1C, IPL levels 1 and 4
   (7b) PAL soft switch: in effect at the next frame start; 312 lines, 192 slots
8. PS/2 frame -> PS2DATA, PS2RDY, IPL 2, cleared by the read; bad parity -> PS2ERR (W1C);
   truncated frame + silence -> watchdog resynchronises
   (10b) AUDIO PDM shifter: a byte written just after the HSYNC edge appears on the pin MSB
   first, one bit per 24 slots, 8 bits per line; the next line repeats it without a write; a
   mid-line write replaces the register at once and shifting continues from the new byte
9. DUART cycles (stub 68681 with open-drain DTACK 2.5 X1 clocks after CS): DUARTCS, byte
   read/write data and RS, CPLD1 releases DTACK, A5..A15 mirror, no IOCS/BUFEN/ROMOE/ROMWE
10. DUART IRQ pin -> INTSTAT.DUARTIRQ, IPL 3 gated by INTEN.DUART
11. no contention events, no SRAM or flash timing violations, phase lock, MA18..17 edge/value
    checker clean and exercised, every driver exercised

## Synthesis and fitting

`make synth` runs yosys's generic `synth` and prints cell counts (`build/synth_*.log`).
`make fit` (or `./fit.sh <variant> <source> <top> [chparam...] -- <fitter options>`) runs the
real flow: yosys (hoglet67's atf15xx_yosys scripts: `read_liberty cells.lib`, `synth
-flatten -noabc`, `techmap`, `dfflibmap`, `abc`, `iopadmap`, `splitnets`, `write_edif
-lsbidx`) and then Atmel's `fit1508.exe` under wine. Results in `build/fit/<variant>.fit`
(read the whole file), `.pin` (pins actually used) and `.jed`.

Setup (once): `~/.local/src/atf15xx_yosys` with `vendor/` holding `fit1508.exe`,
`aprim.lib` and `atmel.std` from a ProChip Designer installation (the WinCUPL fitters lack
`aprim.lib`); a win32 wine prefix `~/.wine_atf` with `vcrun6sp6`. Two fitter quirks that
`fit.sh` works around: it writes a scratch file and looks for `atmel.std` at the **root of
the current drive**, so the workspace is `~/.wine_atf/drive_c/m68k_fit` (`build/fit` is a
symlink to it) with `atmel.std` copied in; and a missing `.pin` file makes it exit silently.
Only run one fit at a time. `docs/atf15xx.md` is the guide to reading `.fit` files and to
what costs what on this part.

### Results

Current design (spec rev 5: DUART board, flash ROM, 1 MB SRAM, PDM audio), `-preassign keep` with the
pin blocks in the sources, JTAG on, power_reset on, TDI/TMS pull-ups on (CPLD1 also
xor_synthesis):

| chip | pass | macrocells | flops | foldbacks | nodes+FB | PTs | worst LAB fan-in | I/O pins |
|---|---|---|---|---|---|---|---|---|
| `cpld1_mmu` | 2 | **111 / 128** | 70 | 13 | 124 | 400 | 37 / 40 | 64 / 64 (incl. 4 JTAG) |
| `cpld2_io` | 1 | **119 / 128** | 85 | 15 | 127 | 369 | 28 / 40 | 60 / 64 (incl. 4 JTAG) |
| `cpld2_io` before rev 5 (no PDM) | 1 | 107 / 128 | 73 | 9 | 107 | 322 | 28 / 40 | 60 / 64 |

Spec rev 4 (2x AS6C4008 = 1 MB) needed two more SRAM address lines; CPLD1 had no pin left,
so CPLD2 drives MA18..MA17 from A19..A18 (inputs on pins 54/55, outputs on 56/57, fixed
before fitting because the board netlist uses them): +3 macrocells, +4 I/O, still pass 1.
CPLD1's RAM decode shrank from A23..A18 to A23..A20 but the kept-pin fit stays at 111 cells.

Spec rev 5 (2026-10-21) added the 8-bit PDM audio shifter to CPLD2: +12 macrocells, +12 flops,
no pin change (all 60 placements identical to the rev 4 fit, audio still on pin 25), so it is
a JED-only change for rev B4 boards. Nodes+foldbacks are at 127/128: CPLD2 is full.

Spec rev 3 added ROMWE (flash write strobe) to CPLD1 on its last free I/O (pin 50) and
widened the ROM window to 1 MB. The fitter now needs pass 2 (cascade logic) with the pin
block kept, because pass 1 cannot place the new output in the LAB pin 50 belongs to; free
placement fits the same netlist in pass 1 at 112 cells with fan-in 30. Without
`xor_synthesis` the kept-pin fit is 112 cells, 409 PTs, fan-in 39/40. Rev 2 was 103 cells,
fan-in 34, pass 1. CPLD1 is full: no I/O left, 124 of 128 nodes.

(Free placement gives 99 and 104.)  The pin blocks were harvested from a free fit with
`pins.py harvest` and are **proposed**: nothing on the board depends on them yet; re-harvest
after the schematic if routing wants pins moved, then re-verify with `pins.py check`.
`hdl/pins_<chip>.json` maps board signal names to pin numbers for the schematic.

Ladder that led here (free placement, same strategies), for the record:

| variant | pass | macrocells | flops | foldbacks | nodes+FB | PTs | worst LAB fan-in |
|---|---|---|---|---|---|---|---|
| `cpld1_mmu`, rev 1 (no DUARTCS) | 1 | 98 / 128 | 70 | 18 | 102 | 362 | 27 / 40 |
| `cpld2_io` rev 1, HW_TX=1 (107 flops) | - | refused ("design is large") | | | | | |
| `cpld2_io` rev 1, HW_TX=0 | fail | grouping fail at 125 | 88 | 70 | 195 | | 40 |
| rev 1 restructured (no TX, write-only ctrl regs, slot-tick UART, sentinel PS/2) | 1 | 127 / 128 | 99 | 32 | 159 | 357 | 39 / 40 |
| ... minus ID register | 1 | 127 | 99 | 32 | 159 | 353 | 39 |
| ... minus ID, USERIO | 1 | 126 | 95 | 14 | 131 | 421 | 36 |
| ... minus ID, USERIO, LED1 | 1 | 125 | 94 | 14 | 130 | 416 | 36 |
| ... minus ID, USERIO, LED1, DE/VSYNC pin bits | 1 | 125 | 94 | 14 | 130 | 414 | 35 |
| byte-wide registers on D7..D0 (all features) | fail | grouping fail at 124 | | 28 | 152 | | 40 |
| PS/2 receiver removed (clock-edge interrupt instead) | 2 | 105 / 128 | 76 | 11 | 116 | 343 | 27 |
| serial receiver removed (external UART) -> chosen | 1 | 104 / 128 | 70 | 8 | 103 | 309 | 29 |
| both removed | 1 | 79 / 128 | 47 | 4 | 74 | 236 | 25 |

Reading: 127/128 "fits" but is not shippable (docs/atf15xx.md: aim for <= ~120 with no LAB
above ~36 fan-in). Removing the small features buys 2 cells because the fitter spends freed
cells converting foldbacks into nodes. Only the frame engines matter; byte-wide registers
made it worse (eight pins carrying six sources each exceed one LAB's fan-in).

CPLD1 pins: 27 inputs + 33 outputs = 60 of the 60 regular I/O with JTAG on (none spare), plus
`clk24` on GCLK1 and `sysreset_n` on GCLR. CPLD2: 13 inputs, 22 bidirectional, 17 outputs =
52 (8 spare).

### Flow to the device

1. `make fit` (pin blocks `//PIN: net : pin` in the sources, `-preassign keep`); `make
   pins-check` confirms the fitter honoured them.
2. JED -> SVF with Atmel's ATMISP (wine) or `prjbureau`'s `fuseconv`.
3. Program through the JTAG header with the FT232H:
   `openocd -f interface/ftdi/um232h.cfg -c 'adapter speed 1000; transport select jtag;
   jtag newtap atf1508 tap -irlen 3 -expected-id 0x0150803f; init; svf cpld1.svf; shutdown'`
   (the IDCODE is the ATF1508AS's; adjust if OpenOCD reports a different one). Keep JTAG
   enabled so the parts stay reprogrammable in circuit.
