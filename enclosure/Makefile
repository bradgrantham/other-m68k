# Griffin Jr. enclosure: generated from the released KiCad board. See README.md.
#   make          build/board.scad (geometry + checks), then the STLs in stl/:
#                 top, face, bottom, inserts (to print); board, plugs, fit (to check the fit),
#                 then fit_check.py: nothing on the board or any plug inside the case
#   make check    the pcbnew checks only (pillars, bosses, cutouts, heights)
#   make png      previews (needs a display or xvfb-run)
# The STLs are checked in (binary, so they view on GitHub and open in a slicer without a
# build); rerun make and commit them when the board or the design changes.
PCB      ?= ../kicad/release/other-m68k.kicad_pcb
OPENSCAD ?= openscad
KICAD    ?= kicad-cli
B = build
S = stl
SCAD = $(OPENSCAD) --export-format binstl

.PHONY: all check png clean
all: $(S)/top.stl $(S)/face.stl $(S)/bottom.stl $(S)/inserts.stl $(S)/board.stl $(S)/plugs.stl $(S)/fit.stl $(B)/fit-check.ok

$(B)/board.scad $(B)/components.txt &: board_geometry.py parts.json $(PCB)
	mkdir -p $(B)
	python3 board_geometry.py $(PCB) parts.json $(B)/board.scad 2>&1 | grep -v pcb_track.cpp

check: $(B)/board.scad

$(S)/%.stl: case.scad inserts.scad $(B)/board.scad
	mkdir -p $(S)
	$(SCAD) -o $@ -D 'part="$*"' case.scad

# fit-check models. The board: KiCad's 3D models of the parts listed in components.txt
# (origin at the board's top-left corner, y negated, see merge_stl.py) plus the slab, boxes
# and right-angle headers from case.scad. fit.stl: both halves in place with board and plugs.
$(B)/board-kicad.stl: $(B)/components.txt $(PCB)
	$(KICAD) pcb export stl --subst-models --no-unspecified --no-dnp -f --user-origin 0x0mm \
	    --component-filter "$$(cat $<)" -o $@ $(PCB)
$(B)/board-scad.stl: case.scad $(B)/board.scad
	$(SCAD) -o $@ -D 'part="board"' case.scad
$(B)/fit-case.stl: case.scad $(B)/board.scad
	$(SCAD) -o $@ -D 'part="fit"' case.scad
$(S)/board.stl: merge_stl.py $(B)/board.scad $(B)/board-kicad.stl $(B)/board-scad.stl
	mkdir -p $(S)
	python3 merge_stl.py -o $@ --scad $(B)/board.scad --kicad $(B)/board-kicad.stl $(B)/board-scad.stl
$(S)/fit.stl: merge_stl.py $(B)/fit-case.stl $(S)/board.stl $(S)/plugs.stl
	python3 merge_stl.py -o $@ $(B)/fit-case.stl $(S)/board.stl $(S)/plugs.stl
$(B)/fit-check.ok: fit_check.py merge_stl.py $(B)/fit-case.stl $(S)/board.stl $(S)/plugs.stl
	python3 fit_check.py $(B)/fit-case.stl $(S)/board.stl $(S)/plugs.stl
	touch $@

png: $(B)/top.png $(B)/bottom.png $(B)/assembly.png
$(B)/%.png: case.scad inserts.scad $(B)/board.scad
	$(OPENSCAD) -o $@ -D 'part="$*"' --autocenter --viewall --imgsize 1800,1200 --projection o case.scad

clean:
	rm -rf $(B) $(S)
