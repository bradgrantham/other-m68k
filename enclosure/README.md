# enclosure/ - printed case for the Griffin Jr. board (rev C3)

A two-part 3D-printed shell, generated from the released KiCad board the way the board is
generated from the schematic. It descends from the Rocinante rev1 case
(`~/trees/rocinante/case/rev1_case.scad`): the same shell and lip, heat-set-insert bosses in
the lid with screws from below, membrane-spring button pressors with glued-on caps, LED light
pipes, vent slots. What changed for this board is listed under "Design", what was wrong with
the hand-measured Rocinante file under "Why generated".

```
make            # build/board.scad from the KiCad board (with checks), then the three STLs
make check      # only the pcbnew checks: pillar clearance, boss clearance, cutouts, heights
make png        # previews (needs a display or xvfb-run; the STLs do not)
openscad -D 'part="assembly"' case.scad      # interactive: both halves and a board slab
```

Requires OpenSCAD (2021.01 works), KiCad 9's `pcbnew` Python module and the released board in
`../kicad/release/`. Outputs land in `build/` (ignored by git):

| file | what |
|---|---|
| `build/board.scad` | generated include: board size, holes, cutouts, notches, buttons, LEDs, pillars, vents, the tallest part |
| `build/top.stl` | the lid, already turned face down for printing |
| `build/bottom.stl` | the tray |
| `build/inserts.stl` | two button caps and three light-pipe rods |

## Files

| file | what |
|---|---|
| `board_geometry.py` | reads `other-m68k.kicad_pcb` with pcbnew and `parts.json`, writes `build/board.scad` in case coordinates, runs the checks, exits 1 on a failure so no STL is rendered from bad geometry |
| `parts.json` | what KiCad cannot know: part heights above the board, the **mating plug** envelope and axis height for each connector, the serial-header notch sizes, button and LED labels, the mid-board pillar spots, the boss radius. Numbers taken from data sheets carry `"verify": true` |
| `case.scad` | the design. Parameters at the top (walls, gap, tray depth, lip, boss and insert sizes, pillar, slop, pressor and pipe sizes, vents, labels); everything positional comes from `build/board.scad` |
| `inserts.scad` | the button caps and light pipes, called from `case.scad` with its dimensions |

## Coordinates

Case coordinates are board coordinates with y flipped: x to the right as on the board, y from
the **front** edge (the sheet's bottom edge: PS/2, audio, SD, joystick, buttons, LEDs), z = 0
at the board bottom. `placement.json` measures y down from the back edge, so
`y_case = 125 - y_board`. The board rests on the tray's corner bosses and pillars; the halves
split at the board bottom, so every connector cutout is a notch in the lid wall closed by the
tray's lip.

## Design

* **Shell.** 2 mm walls, 2 mm lid and floor, 0.5 mm gap around the board, outer corners
  rounded to follow the board's 3 mm corners. The tray is 3 mm deep inside (only solder and
  lead ends hang below the board). The lid interior is the tallest part plus 1 mm: 13.5 mm for
  the two vertical HC-49/U crystals today, so the case is 180 + 5 by 125 + 5 by 23.1 mm.
  An HC-49/US low-profile crystal pair would take 2 mm off the height.
* **Lip.** The tray's wall rises 2.5 mm into the lid along the inner half of the wall
  thickness, 0.15 mm clearance per side (Rocinante used 1 mm, which with a 150 mm box already
  made the halves hard to join; this box is 185 mm).
* **Corner bosses fused into the walls.** The holes are 5 mm from both edges, and three of
  them have a part within 4.6 mm, so a 10 mm Rocinante boss would land on parts. Each boss is
  the hull of a 4.2 mm radius cylinder at the hole and the wedge to the two adjoining walls,
  which gives the heat-set insert a full wall on every side. The bore is 4.0 mm for the short
  M3 inserts (OD about 4.6); set `insert_d = 5.3` for the long OD 5 type. The tray boss has a
  3.4 mm screw hole and a 6.2 mm head recess 3 mm deep, leaving 2 mm under the board:
  **M3 x 8** screws. `make check` fails if any top-side footprint falls inside a boss.
* **Mid-board pillars.** Two 5 mm posts rise from the tray floor to the board underside, and
  two from the lid stop 0.3 mm short of the board top, at board (64, 69) between the CPLD2
  socket and the CPU and board (130, 104) between the CPU and the front parts. The script
  checks each spot is at least pillar radius + 0.5 mm from every pad, via and top-side
  footprint; add a third spot in `parts.json` if the lid still sags (board (162, 47) beside
  the SRAM sockets is clear).
* **Cutouts from mating plugs.** Every jack face sits at or just past the board edge, 2.5 mm
  behind the outer wall surface, so the plug must reach through the wall. Each hole is therefore
  the plug's envelope (USB-C overmould, mini-DIN grip, RCA barrel, 3.5 mm plug body, DE-9 hood,
  microSD card) plus 1 mm `slop` on every side, centred on the connector from KiCad at the axis
  height in `parts.json`. A rectangular cutout whose top would leave under 1.5 mm of wall beneath
  the lid runs up into the lid instead (the joystick hood does). The RCA jack body protrudes
  9.4 mm through the back wall.
* **Serial headers.** J2 and J11 are vertical 1x6 footprints fitted with right-angle headers:
  pins 6.3 mm above the board (top of the pin), 6.55 mm from the bend to the tip, so the tips
  end about 1 mm past the board edge, inside the wall. An FTDI or mouse adapter's socket housing
  passes through a notch in the back wall to reach them. The notch is open at the split so the
  lid lifts off over the pins, 18 mm wide and 9 mm tall above the board top; the two notches
  are adjacent and merge. The tray's wall rises into the merged notch as a tab up to 3.5 mm
  above the board top, below the housing.
* **Buttons.** Omron B3F-1000 (actuator 4.3 mm above the board). A 6 mm pressor column comes
  down from the lid to 0.2 mm above the actuator inside a 12 mm ring cavity that leaves a
  0.4 mm membrane at the lid top as the spring; a 3 mm hole from the top takes the cap's
  shaft, with 2 mm of solid pressor foot left. RESET and NMI are debossed on the front wall
  below them, because the spring rings fill the lid above.
* **LEDs.** 0805 top-emitting LEDs, 1.1 mm tall. A 6.25 mm tube comes down to 0.4 mm above
  the LED with a 4.25 mm bore for TAP rod stock; the insert rods are printed to length and
  trimmed.
* **Vents.** 2 mm slots at 4 mm pitch over the footprints of U1, U6 and U7 (the 68000 and the
  two ATF1508s, the hottest parts), 2 mm inside each outline, kept 3 mm clear of the pillars.
* **Labels.** Port names debossed 0.5 mm into the lid top 4 mm in from the wall above each
  cutout and behind each LED, 2.6 mm bold Liberation Sans; small for a 0.4 mm nozzle, so treat
  them as a guide for printed labels. No badge: glue one on or use a sticker.

## Measurements still to verify against real parts

`parts.json` flags these `"verify": true`; `board_geometry.py` prints them. Measure and clear
the flag, or just correct the number.

| item | value used | source |
|---|---|---|
| DIP-64 socket + 68000 | 8.5 | Mill-Max 110 height 3.3 + body 5.1 |
| PLCC-84 socket + CPLD | 10.0 | Assmann A-CCS-084 |
| DIP-32 / DIP-40 socket + chip | 9.0 | Assmann A xx-LC |
| HC-49/U crystal | 13.5 | sets the lid height |
| mini-DIN jack body, plug axis | 12.5, 6.5 | CUI MD-60SM / MD-40SM |
| RCA jack body, axis | 10.5, 5.5 | CUI RCJ-041 |
| DE-9 body, axis | 12.6, 6.1 | Amphenol L717SDE09 |
| 3.5 mm jack body, axis | 6.0, 3.0 | CUI SJ1-3523N |

The axis heights matter most: a 1 mm error moves a hole 1 mm, and the mini-DIN holes have
only 1.5 mm of wall left under the lid.

## Printing

Five prints, as for Rocinante: the lid face down in black PETG (the STL is already turned; the
button membranes are its first layers, so print them at 0.2 mm), the tray in PETG, the two
button caps in silver PLA, the three light pipes in clear or white. A brim helps the long
walls. Hardware: four M3 x 8 screws, four M3 heat-set inserts, rubber feet.

## Why generated

The Rocinante file carried every cutout as a caliper reading, and its notes record three
print rounds spent moving them by a millimetre. Here the positions come from the board file,
the heights and plug sizes from one table, and the checks run before any STL exists. When the
board changes, `make` regenerates the case; when a measurement changes, edit `parts.json`.

## Known limitations

* The case has not been printed yet; the plug envelopes and the verify-flagged heights are
  data-sheet values.
* `make png` needs a display (OpenSCAD 2021 has no software renderer); section views can be
  made with `projection(cut = true)` and exported to SVG without one.
* Rubber-foot recesses and the louvred vent design of Rocinante were not carried over; the
  vents are plain slots.
