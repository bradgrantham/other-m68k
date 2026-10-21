# kicad/ - generated schematic and PCB for other-m68k

The board is not drawn by hand. `board.py` describes parts and nets in Python (the API is
in `gen_sch.py`), the KiCad 9 schematic files are generated from it, verified with
`kicad-cli` (ERC) and against the description itself (`check_netlist.py`), then carried to a
PCB with `gen_pcb.py` (KiCad's `pcbnew` Python module), placed from `placement.json`,
autorouted with freerouting and checked with DRC. Everything is Python 3 standard library
plus `pcbnew`.

```
make board    # schematic (7 sheets), ERC, netlist check, PDF, PCB from placement.json, DRC, DSN
make route    # freerouting (ROUTE_PASSES=40) under route_watch.py, SES import, DRC, unrouted count
make fab      # Gerbers (+zip), drill files, BOM, JLCPCB/PCBWay assembly files into build/fab/ (routed board if present)
make test     # the small spike board (test_small.py): regression for the generators
make clean
```

Requires KiCad 9.0.9 (`kicad-cli`, `python3 -c "import pcbnew"`), the system libraries in
`/usr/share/kicad/{symbols,footprints}` (override with `KICAD_SYMBOL_DIR` /
`KICAD_FOOTPRINT_DIR`) and, for `make route`, a Java 25 runtime at `~/.local/jre25` plus the
freerouting 2.5.0 jar at `~/.local/share/freerouting/freerouting-2.5.0.jar` (`JAVA=...`,
`FREEROUTING_JAR=...` to point elsewhere).

Outputs land in `build/other-m68k/`:

| file | what |
|---|---|
| `other-m68k.kicad_pro`, `other-m68k.kicad_sch`, `other-m68k-<sheet>.kicad_sch` | the project; open `other-m68k.kicad_pro` in KiCad |
| `other-m68k.pdf` | the schematic, 8 pages (root + 7 sheets) |
| `other-m68k.kicad_pcb` | placed board with GND/VCC fanout vias, signals unrouted (DRC 0, 4 layers, GND/VCC planes filled) |
| `other-m68k-placement.svg` | top view of the placement |
| `other-m68k.dsn`, `other-m68k.ses`, `other-m68k-routed.kicad_pcb`, `other-m68k-routed.svg` | routing |
| `erc.json`, `drc.json`, `drc-routed.json` | the reports the Makefile evaluates |
| `../fab/` | Gerbers (`*.gbr`, zipped in `other-m68k-gerbers.zip`), drills (`*.drl` + PDF map), `other-m68k-pos.csv`, `other-m68k-bom.csv` (every field), and the assembly files below |

## Reading the schematic PDF

The generator draws each part once with a short wire stub and a **global label** on every
connected pin; nets are joined by name, not by drawn wires, so read it like a netlist with
pictures. Page 1 is the root with the seven sheet symbols; the power-net anchors
(power symbol + PWR_FLAG per rail) are at the top of the first sheet with parts (cpu).

| page | sheet | contents |
|---|---|---|
| 2 | cpu | U1 MC68000P12, DTACK/BR/BGACK/RESET/HALT pull-ups, the two reset diodes, decoupling |
| 3 | memory | U2/U3 SRAM (hi/lo byte), U4/U5 SST39SF040 flash (even/odd), U8/U9 74AHCT245 (hi/lo) |
| 4 | cpld1 | U6 ATF1508AS "MMU" with every pin labelled from `hdl/pins_cpld1_mmu.json`, X1 24 MHz oscillator, J7 JTAG, JTAG pull-ups, 8+1 decoupling capacitors |
| 5 | cpld2 | U7 ATF1508AS "I/O", J3 PS/2, J4 microSD with level dividers and pull-ups, SW2 NMI, D3/D4 LEDs, J6 audio and its filter, decoupling |
| 6 | video | U11 AD724 (AD725 symbol), the three resistor DACs and terminations, Y2 subcarrier crystal, STND pull-up (NTSC)JMP jumpers, FB1, J5 RCA, 220 uF/75 ohm output |
| 7 | io | U10 MC68681 DUART, Y1 3.6864 MHz, J2 FTDI, J8 expansion, U13 MCP130 + SW1 reset, DUART pull-ups |
| 8 | power | J1 USB-C, F1 polyfuse, CC pull-downs, bulk capacitors, D5 power LED, U12 AP2112K-3.3, H1..H4 |

Net names follow the spec (`A1..A23`, `D0..D15` CPU data bus, `MD0..MD15` SRAM-side bus,
`MA0..MA16` SRAM address, `AS`, `UDS`, `LDS`, `RW`, `DTACK`, `BERR`, `VPA`, `ROMOE`, `ROMWE`,
`RAMOE`, `WEU`, `WEL`, `BUFEN`, `IOCS`, `LATCH`, `DE`, `HSYNC`, `VSYNC`, `DUARTCS`,
`SYSRESET`, `CLK24`, `CPUCLK`, `IPL0..2`, `PS2_CLK/DAT`, `SD_*`, `UIO0..3`, `LED0/1`,
`AUDIO`, `R0..R2 G0..G2 B0..B1`, `PALJMP`, `NMIBTN`, `DUART_IRQ`). Active-low signals keep
their plain spec names; the polarity is in `spec/board.json`. Rails: `VCC` (+5 V), `GND`,
`+3V3` (SD card only), `VBUS` (USB side of the polyfuse), `+5VA` (AD724 analog supply
behind FB1).

## Decisions taken here (not spelled out in the spec/docs)

* **74AHCT245 sides.** DIR is R/W straight from the CPU. The TI function table says DIR
  high = A->B, and a CPU read (R/W high) must move SRAM->CPU, so the SRAM-side bus
  (`MD*`) is on the **A** side and the CPU bus (`D*`) on the **B** side. This is the same
  behaviour as the testbench's `dir = ~rw` with A = CPU; `docs/architecture.md` just says
  "direction from R/W".
* **Flash CE tied to GND**; CPLD1 drives OE (`ROMOE`, the old ROMCE expression) and WE
  (`ROMWE`, pin 50) so the firmware can reprogram the SST39SF040 pair in place (spec rev 3).
  Data appears 35 ns after OE instead of 70 ns after CE; the flash symbol's pin 31 is
  named PGM and is /WE.
* **AD724 ENCD** tied directly to +5 V as in the data sheet's typical circuit
  (`docs/video-encoder.md` says "through 10 k"; the AD725 stand-in symbol types that pin
  power_in, which ERC would flag behind a resistor). SELECT: 10 k to GND. STND: 10 k to
  VCC + jumper.
* **RGB terminations** (75 ohm) sit on the DAC side of the 0.1 uF coupling capacitors: they
  are the load the resistor values in `docs/video-encoder.md` were computed for, and the
  data sheet terminates at the source too. Coupling caps and terminations are anchored next
  to U11 on the board.
* **PALJMP / STND.** NTSC only (decision 2026-10-20): STND is pulled up through 10 k and
  CPLD2's PALJMP input pulled down through 10 k, so INTSTAT.PALJMP reads 0. A PAL board
  would move both resistors and swap Y2 for 4.433619 MHz; there is no jumper.
* **68681 unused inputs** IP0..IP5: 10 k to VCC each (data sheet: "operation is enhanced if
  unused inputs are tied to an appropriate logic level"). OP0..OP7 open. IACK: 4.7 k to
  VCC. Crystal per section 2.2/2.3: 15 pF from X1/CLK to GND, 0-5 pF from X2 to GND as a
  DNP 0603 footprint (C43, marked DNP in the BOM and left out of the position file).
* **ATF1508 dedicated inputs** OE1 (pin 84) and OE2/GCLK2 (pin 2) are unused and tied to
  GND on both chips; unused I/O pins are left open (no-connect) - the fitter configures
  them and the parts have pin-keepers. Every VCC pin (3, 43 VCCINT; 13, 26, 38, 53, 66,
  78 VCCIO) has its own 100 nF, plus 10 uF per chip.
* **JTAG header** J7 (ByteBlaster pinout): 1 TCK, 2 GND, 3 TDO, 4 VCC, 5 TMS, 6 GND,
  7 n/c, 8 GND, 9 TDI, 10 GND. Chain: header TDI -> U6 (TDI 14, TDO 71) -> U7 TDI -> U7
  TDO -> header. 10 k pull-ups on TMS and on both TDIs.
* **Expansion header** J8 (2x8): odd row 1 +5V, 3 +3V3, 5 UIO0, 7 UIO1, 9 UIO2, 11 UIO3,
  13 DUART TXDB, 15 DUART RXDB; every even pin GND.
* **FTDI header** J2: 1 GND, 2 CTS (n/c), 3 VCC (n/c: the board is USB-C powered; do not
  bridge it to the cable's 5 V), 4 TXD -> DUART RXDA, 5 RXD <- DUART TXDA, 6 RTS (n/c).
* **USB-C**: 6-pin power-only receptacle (GCT USB4125 footprint), 5.1 k on CC1 and CC2,
  2 A 1206 polyfuse (the board draws about 1 A: see ORDER.md), 100 uF electrolytic + 10 uF, no reverse protection (USB-C cannot be
  plugged reversed).
* **Mounting holes** H1..H4 (M3, 3.2 mm) are tied to GND.
* **Reset**: MCP130T-450I/TT in SOT-23 (1 RST, 2 VDD, 3 VSS in the KiCad symbol), 4.5 V
  threshold because the USB-C supply can legitimately sit at 4.75 V; open-drain output on
  SYSRESET with a 10 k pull-up and the reset button in parallel; SYSRESET pulls the CPU's
  RESET and HALT low through separate diodes.

## Library choices

All symbols and footprints are KiCad 9 system libraries except:

| part | symbol used | footprint | note |
|---|---|---|---|
| MC68681 (U10) | `Interface:68681` | `Package_DIP:DIP-40_W15.24mm_Socket` | pin numbers checked against MC68681 Table 2-1 |
| AD724 (U11) | `Video:AD725` stand-in, value AD724JR | `Package_SO:SOIC-16W_7.5x10.3mm_P1.27mm` | same pinout; pin 3 shows "4FSC_CLK" (= FIN) and pin 12 "TRAP" (= SELECT) |
| KM681000BLP-7 (U2/U3) | `Memory_RAM:AS6C1008-xxP` | `Package_DIP:DIP-32_W15.24mm_Socket` | pin compatible |
| SST39SF040 (U4/U5) | `Memory_Flash:SST39SF040` | `Package_DIP:DIP-32_W15.24mm_Socket` | plain dual-wipe socket (Assmann A 32-LC-TT); the TL866 programs the first image, later ones go in from the SD card |
| 74AHCT245 (U8/U9) | `74xx:74HC245` | `Package_SO:SOIC-20W_7.5x12.8mm_P1.27mm` | no AHCT symbol, pins identical; SN74AHCT245DW |
| MCP130 (U13) | `Power_Supervisor:MCP130-xxxxTT` | `Package_TO_SOT_SMD:SOT-23` | MCP130T-450I/TT |
| ATF1508AS (U6/U7) | `CPLD_Microchip:ATF1508ASx-xxJx84` | `Package_LCC:PLCC-84_THT-Socket` | all I/O pins are named "I/O", so they are wired by number from `hdl/pins_*.json` |
| PS/2 (J3) | `other-m68k:Mini-DIN-6_Shield` (project library `lib/other-m68k.kicad_sym`: the system Mini-DIN-6 plus a shield pin) | `other-m68k:MiniDIN-6_CUI_MD-60SM_Horizontal` (project library `lib/other-m68k.pretty`) | no mini-DIN footprint in the system libraries; dimensions from the CUI/Same Sky MD-60SM drawing ("date codes prior to 03/03/2022" layout) |
| RCA (J5) | `Connector:Conn_Coaxial` | `other-m68k:RCA_CUI_RCJ-041_Horizontal` | no RCA footprint in the system libraries; dimensions from the RCJ-04x drawing |
| analyzer header (J9) | `Connector_Generic:Conn_02x15_Odd_Even` | `Connector_PinHeader_2.54mm:PinHeader_2x15_P2.54mm_Vertical` | gusmanb LogicAnalyzer channel pinout, 100 ohm arrays `Device:R_Pack04` / `Resistor_SMD:R_Array_Convex_4x0603` in series |

The project libraries are referenced from the generated `sym-lib-table` / `fp-lib-table`
relative to the project directory (`${KIPRJMOD}/../../lib/...`), so the build directory
must stay under `kicad/`.

## Assembly package (`parts.json`, `fab_export.py`)

The board goes to PCBWay (see `ORDER.md`) as bare boards plus assembly of everything except
the chips from the bin. `parts.json` holds the sourcing data for every part, keyed by
`value|footprint` (or by reference for the socketed chips): manufacturer, MPN, optional LCSC
number, a description, and an `assembly` class:

| class | meaning |
|---|---|
| `pcba` | the assembler buys and mounts it (passives, LDO, AD724, supervisor, 74AHCT245, connectors, switches, crystals ...) |
| `socket` | the assembler mounts the **socket** (`socket.mfr/mpn`); the chip is a separate line, supplied by `chip_supply` = `customer` (68000, 68681, SRAM, CPLDs) or `pcba` (the SST39SF040 flash, inserted blank) |
| `customer` | we supply and fit it ourselves; not in the assembly files |
| `dnp` | not fitted (the crystal trim capacitor, mounting holes, test pads) |

`board.py` attaches these as symbol fields (`Manufacturer`, `MPN`, `LCSC`, `Assembly`,
`Desc`, `Socket_*`, `Chip_Supply`), so they reach the schematic, the XML netlist and KiCad's
own BOM export, and it refuses to build when a part has no entry. `make fab` then runs
`fab_export.py`, which writes into `build/fab/`:

| file | for |
|---|---|
| `other-m68k-jlcpcb-bom.csv`, `-jlcpcb-cpl.csv` | JLCPCB: Comment/Designator/Footprint/LCSC Part #, and Designator/Mid X/Mid Y/Layer/Rotation |
| `other-m68k-pcbway-bom.csv`, `-pcbway-centroid.csv` | PCBWay: Item/Designator/Qty/Manufacturer/Mfg Part #/Description/Package/Type/Supplied By, and the centroid file |
| `other-m68k-customer-parts.csv` | the chips we fit after assembly (and who supplies each) |
| `other-m68k-bom-check.txt` | part numbers still marked `verify` in `parts.json`, assembled parts without an MPN, parts without an LCSC number |
| `other-m68k-gerbers.zip` | every Gerber and drill file in the directory |

Positions follow KiCad's own position-file convention (mm, Y up, KiCad rotation), which
both houses accept from KiCad users; `parts.json` `jlc_rotation.rules` (footprint regex ->
degrees) corrects the CPL for footprints JLCPCB's DFM preview shows turned. `verify: true`
marks a part number written from a data sheet or memory that has not been checked against a
distributor listing; clear the flag once it has.

## Editing the placement (`placement.json`)

```json
"board": {"width": 180, "height": 125, "corner_radius": 3, "layers": 4,
          "zones": {"In1.Cu": "GND", "In2.Cu": "VCC"}},
"checks": {"sockets": true},
"parts": {"U1": {"x": 100, "y": 85.5, "rot": 90},
          "U4": {"x": 126, "y": 113, "rot": 90, "keepout": [[x0, y0, x1, y1, "why"], ...]}, ...}
```

* `x`, `y` are the **centre of the footprint courtyard** in mm from the board's top-left
  corner (x to the right, y down). `rot` is in degrees in KiCad's sense (positive turns
  counter-clockwise on screen; a DIP is vertical at 0 and horizontal at 90). `side` is
  `top` (default) or `bottom`.
* Parts that are **not listed** are placed automatically next to the part named in their
  `PcbNear` schematic field (set in `board.py`: decoupling capacitors next to their IC,
  pull-ups next to the connector or IC they serve, the DAC resistors next to U11 ...),
  steered toward the anchor pads they connect to and never overlapping anything placed
  before them. Add an entry to pin one down.
* `auto.margin` is the gap kept between parts (mm), `auto.pitch` the search step.
* `texts` adds silkscreen text. Zones are full-board pours on the named layers.
* `fanout` names the plane nets whose SMD pads get a via each (see "Flow details").
* **Sockets are part of the placement.** Every DIP and PLCC part is placed with its
  *socket* footprint (`Package_DIP:*_Socket`, `Package_LCC:PLCC-84_THT-Socket`).
  `checks.sockets: true` makes `gen_pcb.py build` fail on any DIP/PLCC part with a
  bare-chip footprint; an optional `checks.zif: [refs]` demands a zero-insertion-force
  socket for the listed parts (rev B used it for the EPROMs; a ZIF footprint's courtyard
  must include the lever travel, e.g. the 3M Textool one in the `Socket` library is
  25.2 x 63.8 mm against 18.4 x 36.3 for a plain DIP-28 socket). The build prints the
  socket table (ref, footprint, courtyard size) so the sizes used for placement are visible.
* **Collisions are errors.** `gen_pcb.py build` stops when two listed parts' extents
  (courtyard plus reference text) overlap, naming both rectangles; set
  `"allow_overlap": true` on one of them if it is intended. A note is printed for
  courtyards past the outline (normal for edge connectors). KiCad's DRC checks courtyard
  overlaps again on the finished board, so a keepout that gets something placed in
  it by hand in KiCad is still reported.
* **Keepouts.** `"keepout": [[x0, y0, x1, y1, "why"], ...]` on a part (footprint-local mm,
  unrotated, y down, origin at the footprint anchor) adds a KiCad rule area on the part's
  copper side that forbids *footprints* (tracks, vias and pads stay allowed). Listed parts
  inside it fail the build, auto-placed parts avoid it, and DRC reports anything moved into
  it later. Use it for mechanical clearance a footprint does not model (an extractor tool,
  a heatsink, a lever on a socket whose footprint stops at the body).

Layout rev B (180 x 125 mm, 4 layers: F.Cu / GND / VCC / B.Cu, all parts on top):

* Left edge, top to bottom: J1 USB-C, J2 FTDI, J3 PS/2, J4 microSD. Bottom edge: J5 RCA
  and J6 audio at the left. Top edge: SW1 reset, SW2 NMI, J7 JTAG, J8 expansion, J9 logic
  analyzer (2x15, gusmanb pinout, 100 ohm series arrays RN1-RN6 beside it; see ORDER.md).
* U10 DUART stands vertically between the connectors and CPLD2, near the FTDI header; Y1,
  U13 and U12 below it; U11/Y2 (video) in the bottom-left corner next to the RCA jack.
* U7 (CPLD2, I/O) left of centre next to the connectors it serves; U6 (CPLD1, bus and
  memory) right of centre next to the SRAM pair U2/U3 (top right, horizontal) and the two
  bus transceivers U8/U9 (SOIC-20, between U6 and the SRAMs). X1 above and between the CPLDs.
* U1 lies horizontally under both CPLDs.
* The two flash chips U5 (odd) and U4 (even) lie horizontally side by side along the
  bottom edge under the CPU (rev 3: SST39SF040 in plain DIP-32 sockets; rev B had 3M ZIF
  sockets for EPROMs there, with the levers lifting at the board edge).

Rev A had CPLD1 on the left and the memory stacked down the right edge, which put the
whole bus across the board; rev B is what made room for the memory along the bottom.

Knobs the author may want to turn: board size, which edge the video connectors use,
whether the SMD passives should go to the bottom side under the sockets (`"side":
"bottom"` per part, not yet exercised), and the netclass (0.25 mm tracks, 0.2 mm
clearance, 0.8/0.4 mm vias in `gen_sch.project_json`).

## Description API (`gen_sch.py`)

```python
from gen_sch import Board

b  = Board("other-m68k", title="...", rev="A")
b.sheet("cpu", "68000 CPU")                                  # optional titles
u1 = b.part("U1", "CPU_NXP_68000", "MC68000P",               # ref, library, symbol
            "Package_DIP:DIP-64_W22.86mm_Socket", "MC68000P12",  # footprint, value
            sheet="cpu", PcbNear="U6", dnp=False)            # sheet, extra fields, DNP

u1.pin("A1")        # by number ("29"), by name ("~{AS}") or cleaned name ("AS", "R/W", "VCC")
u1.pins("VCC")      # every pin matching (the 68000 has two VCC and two GND pins)

b.net("ROMCS", u1.pin("AS"), u2.pin("CE"))                  # a named net
b.bus("A", 1, 23, u1, (u3, "A{}", -1))                       # nets A1..A23 with pin templates
b.power("VCC", u1.pins("VCC"))                               # power net: symbols + one PWR_FLAG
b.power("+3V3", u12.pin("VOUT"), flag=False)                 # driven by a power_out pin: no flag
u1.nc("BR", "BG"); u6.nc_unused = True                        # no-connect flags

b.check()            # warnings: unconnected pins, single-pin nets, off-grid pins, hidden-pin traps
b.write("build/other-m68k")
b.expected_nets()    # {net: {(ref, pin number)}} for check_netlist.py
```

Rules enforced: a pin is in one net only; pins stacked at the same symbol location (hidden
second VCC/GND pins of the 68000, the ATF1508's six VCCIO and eight GND pins, USB-C B9/B12)
must all be in the net because KiCad connects them by position; a hidden pin not stacked on
a visible one cannot be wired. Symbol libraries are looked up in the system directory first
and then in `kicad/lib/`.

## Flow details

* `gen_pcb.py build --placement` loads footprints from the system `.pretty` libraries or
  `lib/`, strips footprint graphics on Edge.Cuts (the CUI audio jack footprint carries a
  "board edge here" line that would break the outline) and silkscreen that would cross the
  board edge, places, draws the rounded outline, adds the zones, saves, then **reloads the
  file to fill the zones** (`ZONE_FILLER` segfaults on a board built in memory without a
  project behind it).
* The physical stackup table is not reachable from the Python API in 9.0.9; the board has
  4 copper layers and KiCad applies its default 4-layer stackup when plotting.
* **Power planes and freerouting.** freerouting cannot be trusted with copper areas (the
  2.5.0 native CLI dies in GraalVM serialization as soon as the DSN contains a plane or a
  footprint keepout) and ignores `(type power)` layers. So the planes live in KiCad only:
  * `gen_pcb.py build` adds a **fanout**: every SMD pad on GND or VCC gets a locked via
    (0.8/0.4 mm) and a short locked stub, placed with copper and hole clearance against all
    other pads and vias (`"fanout"` in `placement.json`, 133 vias on this board). THT pads
    reach the planes through their barrels, so after the zone fill GND and VCC are complete
    before routing starts (464 -> 331 ratsnest lines).
  * `gen_pcb.py dsn --power-layers In1.Cu,In2.Cu --plane-nets GND,VCC` exports a
    **2-layer DSN**: planes dropped, inner layers removed, GND/VCC taken out of the routed
    network, footprint `(outline)` and `(keepout)` groups stripped. Each fanout stub (SMD
    pad + track + via) is kept as its own one-pin net (`GND__12` ...) so the router sees a
    finished connection (one net per pad, `GND__C12-2`; a footprint's GND pads are kept
    apart, or the router wires them to each other through the part) instead of netless
    items on top of each other, which freerouting counted as 309 "unfixable clearance
    violations" and scored against every pass. The fanout itself keeps every via 0.25 mm
    off its own pad (solder dam) and clear of all other pads, stubs and footprint
    keepouts. The board outline in the DSN is shrunk by 0.6 mm because freerouting
    routes right up to it while KiCad wants 0.5 mm edge clearance. Three cosmetic
    violations remain in freerouting's report: the microSD socket's keepout obstacles
    overlap its own shield pad. What KiCad does not export is synthesised as netless pins
    (= obstacles): NPTH holes (circle, drill + 0.6 mm) and footprint keepout zones (the
    Hirose microSD socket's), so the router keeps the hole and keepout clearances.
  * `gen_pcb.py ses` removes the board's unlocked tracks (the session carries them again),
    renames the session's `Via[0-1]` padstacks to the 4-layer form, forces every via to a
    through via (KiCad would otherwise read them as blind vias) and refills the zones.
* **Which freerouting.** The 2.5.0 native CLI crashes (`java.lang.Number` serialization)
  when it snapshots the board after a pass, then sleeps forever at 0 % CPU; the 2.1.0 jar
  runs on Java 21 but ignores the pass limit and never finishes (it plateaued at 14-25
  unrouted for 40 passes here). The 2.5.0 **jar on Java 25** (portable Temurin JRE in
  `~/.local/jre25`, `JAVA=...` to override) honours `--router.autorouter.max_passes`,
  writes the session at the end and reports its pre-existing clearance violations up
  front; `--router.fanout.enabled=false` keeps its own SMD fanout stage off (it only adds
  dangling vias here). About 30-40 s per pass on this board. `make route` runs it through
  `route_watch.py`, which every `ROUTE_WATCH` seconds (default 900) compares the number of
  `pass #` lines in `build/other-m68k/route.log` and the process's CPU time with the
  previous check and kills a run that moved on neither, printing the log tail and exiting
  3; otherwise it prints a one-line progress status per check.
* DRC is run with `--severity-all`; the Makefile fails on any violation including warnings.
  Before routing the ratsnest shows up as `unconnected_items` and is allowed; after routing
  `make route` reports the remaining unrouted count (it does not fail on it).

## Known limitations

* **Serial mouse (decided 2026-10-20): an adapter, not a board change.** A Microsoft/Logitech
  serial mouse (1200 baud 7N1, RS-232 levels, powered from RTS/DTR) hangs off DUART channel
  B on the expansion header J8 (TXDB pin 13, RXDB pin 15, +5 V pin 1, GND even pins) through
  a small MAX232 adapter: one receiver mouse TXD -> RXDB, one driver TXDB -> mouse RXD (idle
  negative = the mouse's negative rail), the other driver(s) holding RTS and DTR positive,
  ideally from a UIO bit so firmware can power-cycle the mouse for detection. The adapter
  schematic and the channel-B mouse driver are on the list; the main board is unchanged.

* The schematic is a generated, label-per-pin drawing: correct and ERC-clean, but not a
  pretty hand-drawn sheet. Everything connects by global label name.
* The two project footprints (mini-DIN, RCA) were drawn from the manufacturers' PCB layout
  drawings and have not been checked against physical parts; compare with the jack you buy
  (the MD-60SM changed its PCB layout in March 2022, the footprint follows the older,
  larger-hole drawing which also fits most generic PS/2 jacks).
* The AD724 is drawn with the AD725 symbol (same package and pinout, two pin names differ).
* Placement is a first proposal: decoupling capacitors and pull-ups are auto-placed next to
  their anchors, not hand-optimised, and every SMD part is on the top side.
* Autorouting results depend on freerouting's run; see the numbers in the final report
  and `build/other-m68k/drc-routed.json`.

## Pitfalls found (kept from the spike, still true)

* **Library tables.** A fresh account has no `~/.config/kicad/9.0/sym-lib-table`, so the
  generator writes project-local tables using `${KICAD9_SYMBOL_DIR}` / `${KICAD9_FOOTPRINT_DIR}`.
* **Y axis.** Library symbols have y up, schematics y down. Global label rotation per outward
  direction: left = 180/right, right = 0/left, down = 270/right, up = 90/left.
* **Power symbols** only fit at the end of upward (VCC-style) or downward (GND-style) stubs;
  sideways pins get a global label instead. A power net whose sideways pins produced exactly
  one label gets a partner label at its anchor, or ERC reports "global label only once".
* **PWR_FLAG + power_out** on one net is an ERC error, hence `power(..., flag=False)` for +3V3.
* **Hidden stacked pins** must be listed in the net (`check()` enforces it).
* **No-connects.** Every unconnected visible pin needs a `(no_connect)` marker.
* **Instance paths.** Child-sheet symbols use `/<root-uuid>/<sheet-symbol-uuid>`.
* **pcbnew Python quirks.** `BOARD.GetLayerID("F.SilkS")` returns -1 (use the `pcbnew.F_SilkS`
  constants); `PCB_TEXT` on an undefined layer makes the saved board unloadable;
  `ZONE_FILLER` needs a board loaded from disk.
* **kicad-cli pcb has no netlist import**; `pcbnew` does it.
