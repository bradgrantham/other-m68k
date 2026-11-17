# Griffin Jr. enclosure: generated from the released KiCad board. See README.md.
#   make          build/board.scad (geometry + checks), then top/bottom/inserts STLs
#   make check    the pcbnew checks only (pillars, bosses, cutouts, heights)
#   make png      previews (needs a display or xvfb-run)
PCB      ?= ../kicad/release/other-m68k.kicad_pcb
OPENSCAD ?= openscad
B = build

.PHONY: all check png clean
all: $(B)/top.stl $(B)/bottom.stl $(B)/inserts.stl

$(B)/board.scad: board_geometry.py parts.json $(PCB)
	mkdir -p $(B)
	python3 board_geometry.py $(PCB) parts.json $@ 2>&1 | grep -v pcb_track.cpp

check: $(B)/board.scad

$(B)/%.stl: case.scad inserts.scad $(B)/board.scad
	$(OPENSCAD) -o $@ -D 'part="$*"' case.scad

png: $(B)/top.png $(B)/bottom.png $(B)/assembly.png
$(B)/%.png: case.scad inserts.scad $(B)/board.scad
	$(OPENSCAD) -o $@ -D 'part="$*"' --autocenter --viewall --imgsize 1800,1200 --projection o case.scad

clean:
	rm -rf $(B)
