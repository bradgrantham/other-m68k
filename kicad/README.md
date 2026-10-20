# kicad/ - generated schematic and PCB

The board is not drawn by hand. A Python description of parts and nets (`test_small.py`
today, the full board later) is turned into KiCad 9 files by `gen_sch.py`, verified with
`kicad-cli`, and carried on to a PCB with `gen_pcb.py` (pcbnew Python module) and
freerouting. Everything is Python 3 standard library plus KiCad's own `pcbnew` module.

```
make test     # generate build/test_small, ERC, netlist check, PDF, PCB, DRC, DSN
make route    # autoroute with freerouting, import the SES, DRC again (slow, optional)
make clean
```

Requires KiCad 9.0.9 (`kicad-cli`, `python3 -c "import pcbnew"`), system libraries in
`/usr/share/kicad/{symbols,footprints}` (override with `KICAD_SYMBOL_DIR` /
`KICAD_FOOTPRINT_DIR`), and for `make route` the freerouting CLI at
`~/.local/share/freerouting/freerouting-cli`.

## Description API (`gen_sch.py`)

```python
from gen_sch import Board

b  = Board("other-m68k", title="other-m68k rev A", rev="A")

u1 = b.part("U1", "CPU_NXP_68000", "MC68000P",                 # ref, library, symbol
            "Package_DIP:DIP-64_W22.86mm_Socket", "MC68000P12", # footprint, value
            sheet="cpu")                                         # optional named sheet
u3 = b.part("U3", "Memory_RAM", "AS6C1008-55PCN", "Package_DIP:DIP-32_W15.24mm_Socket", "KM681000")

u1.pin("A1")        # a pin, by number ("29"), by name ("~{AS}") or by cleaned name ("AS", "R/W", "VCC")
u1.pins("VCC")      # every pin matching (the 68000 has two VCC and two GND pins)

b.net("ROMCS", u1.pin("AS"), u2.pin("CE"))                  # a named net
b.bus("A", 1, 23, u1, (u3, "A{}", -1))                       # nets A1..A23: U1.A<i>, U3.A<i-1>
b.bus("D", 0, 15, u1, u2, (u4, "A{}"))                       # pin-name template per member
b.power("VCC", u1.pins("VCC"), u2.pin("VCC"))                # power net: gets power symbols + PWR_FLAG
b.power("+3V3", u9.pin("VOUT"))                              # any name in power.kicad_sym works
b.power("AVCC", u5.pin("VCC"), symbol="power:VCC")           # or pick the symbol explicitly

u1.nc("BR", "BG", "BGACK")   # no-connect flags
u1.nc_unused = True          # flag every remaining unconnected pin of U1

b.check()                    # -> list of warnings (unconnected pins, single-pin nets)
b.write("build/other-m68k")  # writes .kicad_sch files, .kicad_pro, sym-lib-table, fp-lib-table
b.expected_nets()            # {net: {(ref, pin number)}} for the netlist checker
```

Rules enforced by `check()` / `write()`:

* a pin may be in one net only;
* pins stacked at the same symbol location (the 68000's hidden second VCC/GND) must all be
  on the same net, because KiCad connects them by position;
* a hidden pin that is not stacked on a visible one cannot be wired: KiCad connects hidden
  power pins implicitly to a net named after the pin.

Sheets: `sheet="name"` on a part puts it on `<board>-name.kicad_sch`; the root sheet gets a
sheet symbol per child. Nets are connected with **global labels**, so a net may span sheets
freely and the sheet is purely an organisational choice.

## What is generated

For every placed symbol unit: the flattened library symbol in `lib_symbols` (`extends`
resolved, sub-units renamed), a `(symbol ...)` instance with uuid, properties positioned
from the library and an `(instances (project (path ...)))` block. For every connected pin: a
wire stub of 3.81 mm from the pin end outward and a global label with the net name; power
nets instead get a `power:<net>` symbol when the stub leaves the part upward (VCC-style) or
downward (GND-style), and a label otherwise. One power symbol + `PWR_FLAG` pair per power
net sits in a strip at the top of the first sheet so ERC sees a driver. Unconnected pins
flagged with `nc()` get `(no_connect)` markers.

Layout is deterministic: units sorted by symbol height, packed into rows, each in a cell of
symbol bbox + 30 mm each side for labels; the smallest A-series paper that fits is used.
Coordinates are snapped to the 1.27 mm grid so pin ends are on-grid.

uuids are uuid5 of (board, kind, ref, pin...), so regenerating gives identical files and
diffs stay small.

## Flow and exact commands (`make test`)

```
python3 test_small.py build/test_small
kicad-cli sch erc --format json --severity-all -o build/test_small/erc.json build/test_small/test_small.kicad_sch
kicad-cli sch export netlist --format kicadxml  -o build/test_small/test_small.xml build/test_small/test_small.kicad_sch
kicad-cli sch export netlist --format kicadsexpr -o build/test_small/test_small.net build/test_small/test_small.kicad_sch
python3 check_netlist.py test_small.py build/test_small/test_small.xml
kicad-cli sch export pdf -o build/test_small/test_small.pdf build/test_small/test_small.kicad_sch
python3 gen_pcb.py build build/test_small/test_small.net build/test_small/test_small.kicad_pcb
kicad-cli pcb drc --format json --severity-all -o build/test_small/drc.json build/test_small/test_small.kicad_pcb
python3 gen_pcb.py dsn build/test_small/test_small.kicad_pcb build/test_small/test_small.dsn
```

`make route` continues with

```
~/.local/share/freerouting/freerouting-cli -de build/test_small/test_small.dsn -do build/test_small/test_small.ses -mp 20
python3 gen_pcb.py ses build/test_small/test_small.kicad_pcb build/test_small/test_small.ses build/test_small/test_small-routed.kicad_pcb
kicad-cli pcb drc --format json --severity-all -o build/test_small/drc-routed.json build/test_small/test_small-routed.kicad_pcb
```

`check_netlist.py` parses the XML netlist and compares the set of (net -> {(ref, pin)})
against `Board.expected_nets()`, ignoring `unconnected-...` nets and `#PWR`/`#FLG` symbols.

`gen_pcb.py build` is the netlist import: kicad-cli 9.0.9 has no such command, so the
script loads each footprint from the system `.pretty` libraries, assigns pad nets from the
s-expression netlist, places footprints on a grid (optional `PcbX`/`PcbY`/`PcbRot` symbol
properties override), draws a rectangular Edge.Cuts outline and saves the board next to the
`.kicad_pro` (which carries the netclass used by DRC).

## Pitfalls found

* **Library tables.** ERC reports `lib_symbol_issues` ("configuration does not include the
  symbol library") for every symbol unless the libraries are resolvable. A fresh account has
  no `~/.config/kicad/9.0/sym-lib-table`, so the generator writes a project-local
  `sym-lib-table` and `fp-lib-table` next to the schematic using `${KICAD9_SYMBOL_DIR}` /
  `${KICAD9_FOOTPRINT_DIR}`. With those, ERC is clean and the flattened derived symbols
  (e.g. `74HC245` extends `74LS245`) pass the library-mismatch check.
* **Y axis.** Library symbols have y up, schematics y down: a pin at lib `(x, y)` lands at
  `(X + x, Y - y)`. A pin's `(at ...)` is its connection point; its `angle` points toward the
  body, so the wire stub goes the opposite way. Global label rotation / justification per
  outward direction: left = 180/right, right = 0/left, down = 270/right, up = 90/left.
* **Power symbol orientation.** `power:VCC` has its pin at angle 90 (body drawn above the
  origin) and `power:GND` at 270 (body below). Put VCC-style symbols only at the end of
  stubs leaving a part upward, GND-style downward; everything else gets a label.
* **Hidden stacked pins.** The MC68000P symbol has pins 49 (VCC) and 53 (GND) hidden and
  stacked on 14/16. They connect by position and appear in the netlist, so the description
  must list both or the netlist check fails. `check()` enforces this.
* **No-connects.** Every unconnected visible pin needs `(no_connect)` at its exact end
  coordinate or ERC reports `pin_not_connected`. `nc_unused = True` does this for a part.
* **Instance paths.** Root-sheet symbols use `(path "/<root-uuid>")`; symbols on a child
  sheet use `/<root-uuid>/<uuid of the (sheet) element in the root>`, not the child file's
  own uuid. Only the root carries `(sheet_instances (path "/" (page "1")))`.
* **kicad-cli pcb has no netlist import** (only `drc`, `export`, `render`); use `pcbnew`.
* **DRC before routing** reports the ratsnest as `unconnected_items` (pins - nets), not as
  violations; `make test` only fails on violations.
* **Specctra DSN / freerouting.** `pcbnew.ExportSpecctraDSN` and `ImportSpecctraSES` work
  from Python. The installed freerouting 2.5.0 native CLI dies with a GraalVM
  `SerializationConstructorAccessor ... ComponentOutline` error and then never finishes when
  the DSN contains footprint `(outline ...)` graphics, so `gen_pcb.py dsn` strips them
  (`--keep-outlines` to disable). The board `(boundary ...)` is untouched. Its `-mp` flag is
  deprecated in favour of `--router.autorouter.max_passes` but still works.
* **CvPcb banner.** The first `kicad-cli` run prints a "You have run CvPcb for the first
  time" notice and creates `~/.config/kicad/9.0/fp-lib-table`; harmless.
