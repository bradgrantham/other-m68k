# enclosure/ - printed case for the Griffin Jr. board (rev C3)

A two-part 3D-printed shell, generated from the released KiCad board the way the board is
generated from the schematic. It descends from the Rocinante rev1 case
(`~/trees/rocinante/case/rev1_case.scad`): the same shell and lip, heat-set-insert bosses in
the lid with screws from below, membrane-spring button pressors with glued-on caps, LED light
pipes, vent slots. What changed for this board is listed under "Design", what was wrong with
the hand-measured Rocinante file under "Why generated".

```
make            # build/board.scad from the KiCad board (with checks), the STLs, the fit check
make check      # only the pcbnew checks: pillar clearance, boss clearance, cutouts, heights
make png        # previews (needs a display or xvfb-run; the STLs do not)
openscad -D 'part="assembly"' case.scad      # interactive: both halves, the board model, the plugs
```

Requires OpenSCAD (2021.01 works), KiCad 9 (`kicad-cli` and the `pcbnew` Python module),
numpy, and the released board in `../kicad/release/`. The generated files land in `build/`
(ignored by git); the STLs land in `stl/` and are **checked in** as binary STL so they can be
viewed on GitHub and dropped into a slicer without building. Rerun `make` and commit them when
the board or the design changes. About two minutes, most of it the two lid renders.

| file | what |
|---|---|
| `build/board.scad` | generated include: board size, holes, cutouts, notches, buttons, LEDs, pillars, vents, the tallest part, the fit-model boxes and headers |
| `stl/top.stl` | the lid, already turned face down for printing |
| `stl/labels.stl` | the lid's letters as solids, same placement as `top.stl`, for a second colour (see "Labels") |
| `stl/bottom.stl` | the tray |
| `stl/inserts.stl` | two button caps and three light-pipe rods |
| `stl/board.stl` | fit check: the board with its parts, in case coordinates (see "Fit check") |
| `stl/plugs.stl` | fit check: the mating plugs and serial adapter housings, slop-free, reaching 20 mm out of the walls |
| `stl/fit.stl` | fit check: both halves in place plus the two above, one mesh to open and section in a slicer |

## Files

| file | what |
|---|---|
| `board_geometry.py` | reads `other-m68k.kicad_pcb` with pcbnew and `parts.json`, writes `build/board.scad` in case coordinates, runs the checks, exits 1 on a failure so no STL is rendered from bad geometry |
| `parts.json` | what KiCad cannot know: part heights above the board, the **mating plug** envelope and axis height for each connector, the serial-header notch sizes, button and LED labels, the mid-board pillar spots, the boss radius. Numbers taken from data sheets carry `"verify": true` |
| `case.scad` | the design. Parameters at the top (walls, gap, tray depth, lip, boss and insert sizes, pillar, slop, pressor and pipe sizes, vents, labels); everything positional comes from `build/board.scad` |
| `inserts.scad` | the button caps and light pipes, called from `case.scad` with its dimensions |
| `merge_stl.py` | combines STLs into one binary STL; moves a `kicad-cli` export into case coordinates and drops its board body |
| `fit_check.py` | ray-casts every point of the board and plug meshes against the closed case mesh; fails `make` if anything is inside the plastic by more than 0.05 mm |

## Coordinates

Case coordinates are board coordinates with y flipped: x to the right as on the board, y from
the **front** edge (the sheet's bottom edge: PS/2, audio, SD, joystick, buttons, LEDs), z = 0
at the board bottom. `placement.json` measures y down from the back edge, so
`y_case = 125 - y_board`. The board rests on the tray's corner bosses and pillars; the halves
split at the board bottom, so every connector cutout is a notch in the lid wall closed by the
tray's lip.

## Design

* **Shell.** 2 mm walls, 2 mm lid and floor, 0.5 mm gap around the board, outer corners
  rounded to follow the board's 3 mm corners. The tray is 4 mm deep inside: lead ends, and the
  DE-9's board-lock pegs, which the KiCad model puts 3.8 mm below the board (the fit check
  caught them against a 3 mm floor). The lid interior is the tallest part plus 1 mm: 13.5 mm
  for the two vertical HC-49/U crystals today, so the case is 180 + 5 by 125 + 5 by 24.1 mm.
  An HC-49/US low-profile crystal pair would take 2 mm off the height.
* **Lip.** The tray's wall rises 2.5 mm into the lid along the inner half of the wall
  thickness, 0.15 mm clearance per side (Rocinante used 1 mm, which with a 150 mm box already
  made the halves hard to join; this box is 185 mm).
* **Corner bosses fused into the walls.** The holes are 5 mm from both edges, and three of
  them have a part within 4.6 mm, so a 10 mm Rocinante boss would land on parts. Each boss is
  the hull of a 4.2 mm radius cylinder at the hole and the wedge to the two adjoining walls,
  which gives the heat-set insert a full wall on every side. The bore is 4.0 mm for the short
  M3 inserts (OD about 4.6); set `insert_d = 5.3` for the long OD 5 type. The tray boss has a
  3.4 mm screw hole and a 6.2 mm head recess 3 mm deep, leaving 3 mm under the board:
  **M3 x 8** screws reach 5 mm into the insert. `make check` fails if any top-side footprint
  falls inside a boss.
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
  above the board top, below the housing; the tab is confined to the wall thickness so it
  cannot touch the board edge.
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
* **Labels.** Port names sunk 0.6 mm into the lid top 4 mm in from the wall above each cutout
  and behind each LED, RESET and NMI into the front wall, 2.6 mm bold Liberation Sans. The lid
  prints face down, so the letters are in the first three 0.2 mm layers against the bed, the
  cleanest surface a printer makes. `labels.stl` is the same letters as solids that fill the
  pockets exactly, placed like `top.stl`: on a multi-material printer load the two as one
  multi-part object and give the labels the second extruder, and the letters print in their
  own colour within the same layers. With one extruder, a filament change at 0.6 mm colours
  the pocket floors (visible through 0.6 mm of the face colour) instead; or fill the pockets
  with paint. The letters are small for a 0.4 mm nozzle; `label_size` in `case.scad` can go
  to 3.2 before the back-edge labels touch. No badge: glue one on or use a sticker.

## Fit check

`stl/board.stl` is the board as the case will meet it, in case coordinates. `kicad-cli pcb
export stl` supplies the 3D models of the parts at least `model_min_h` (3 mm) tall that have
one in the library and are not sockets: the connectors, switches, crystals, oscillator and
electrolytics. `merge_stl.py` moves that export into case coordinates (its origin is the
board's top-left corner with y negated) and drops the board body, which `kicad-cli` tessellates
with every via. `case.scad` (`part = "board"`) adds the slab, a box for each part without a
usable model or marked `"box"` in `parts.json` (the two PLCC-84 sockets, both mini-DIN jacks
and the RCA jack have no library model; the DIP sockets have one, but of the empty socket, so
they are boxes to the socket-plus-chip height) and the right-angle serial headers drawn from
the measured pin geometry. `stl/plugs.stl` is every mating plug envelope without slop,
reaching 20 mm out of the wall, plus the serial adapter housings over the header pins.
`stl/fit.stl` is both halves in place with both of those: open it in PrusaSlicer and use the
cut tool, or import `top.stl` turned back over, `bottom.stl` raised 6 mm, `board.stl` and
`plugs.stl` as one multi-part object.

`fit_check.py` then runs as part of `make`: for every vertex, edge midpoint and centroid of
the board and plug meshes it counts crossings of a ray through the closed case mesh, and
reports (and fails on) any point inside the plastic by more than 0.05 mm, clustered with its
extents. On the first run it found the DE-9 board locks in the 3 mm tray floor and the serial
tab touching the board edge; both are fixed above. Interpret a hit by its position: a plug
point inside means the cutout is too small or misplaced, a board point inside means a part
meets the shell, a boss, a pillar or the lid.

The single-part exports also give the library model heights to compare with `parts.json`:

| part | parts.json | KiCad model | note |
|---|---|---|---|
| HC-49/U crystal | 13.5 | 13.1 | parts.json keeps the data-sheet maximum; sets the lid height |
| DE-9 | 12.6 | 12.5 | the shell face reaches 2.58 mm past the board edge, flush with the outer wall |
| DIP-8 oscillator | 5.9 | 5.9 | updated from the model |
| 3.5 mm jack | 6.0 | 5.5 | nose 2.3 mm past the board edge |
| USB-C | 3.3 | 3.3 | flush with the edge |
| 6 mm tactile switch | 4.3 | 4.3 | |
| 6.3 mm electrolytic | 11.5 | 6.3 | the library model is a short can; the BOM part decides |
| DIP sockets | 8.5 / 9.0 | 3.7 / 4.0 | empty socket in the library, hence boxes |

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
button membranes and the label pockets are its first layers, so print them at 0.2 mm), the
tray in PETG, the two button caps in silver PLA, the three light pipes in clear or white.
`labels.stl` goes with the lid on a multi-material printer. A brim helps the long walls.
Hardware: four M3 x 8 screws, four M3 heat-set inserts, rubber feet.

## Why generated

The Rocinante file carried every cutout as a caliper reading, and its notes record three
print rounds spent moving them by a millimetre. Here the positions come from the board file,
the heights and plug sizes from one table, and the checks run before any STL exists. When the
board changes, `make` regenerates the case; when a measurement changes, edit `parts.json`.

## Known limitations

* The case has not been printed yet; the plug envelopes and the verify-flagged heights are
  data-sheet values. The fit check tests the case against those same numbers plus the KiCad
  library models, so it catches geometry errors, not wrong data.
* In the fit model the mini-DIN, RCA and PLCC parts are boxes (no library model), the sockets
  are boxes to their seated height, and the RCA barrel beyond the board edge is represented
  only by the plug envelope.
* `make png` needs a display (OpenSCAD 2021 has no software renderer); section views can be
  made with `projection(cut = true)` and exported to SVG without one.
* Rubber-foot recesses and the louvred vent design of Rocinante were not carried over; the
  vents are plain slots.
