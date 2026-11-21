# The Griffin Jr. user's guide: Markdown chapters -> Typst -> PDF.  See README.md.
#
#   make            build/griffin-jr-users-guide.pdf
#   make screens    run the guide's sessions in the emulator again (after a firmware or
#                   program change; `make` does this by itself when flash.img is newer)
#   make clean

TOP    := ../..
LOCAL  := $(HOME)/.local/opt
PANDOC ?= $(firstword $(wildcard $(LOCAL)/pandoc-*/bin/pandoc) pandoc)
TYPST  ?= $(firstword $(wildcard $(LOCAL)/typst-*/typst) typst)
BOARD  := $(TOP)/kicad/release/other-m68k.kicad_pcb

CHAPTERS := $(sort $(wildcard [0-9][0-9]-*.md))
TYP      := $(patsubst %.md,build/ch/%.typ,$(CHAPTERS))
PDF      := build/griffin-jr-users-guide.pdf

all: $(PDF)

$(PDF): build/guide.typ build/figures/board.svg
	$(TYPST) compile --root . build/guide.typ $@

# One Typst file: the page design, then the chapters in file-name order.  (A chapter pulled in
# with #include would not see the design's definitions.)
build/guide.typ: guide.typ $(TYP)
	cat guide.typ $(TYP) > $@

# The sed drops pandoc's centring so that tables and their cells sit at the left margin.
build/ch/%.typ: %.md filter.lua build/screens.stamp Makefile
	@mkdir -p build/ch
	$(PANDOC) --from markdown --to typst --lua-filter filter.lua --wrap=preserve $< \
	    | sed -e 's/^  align(center)\[#table(/  [#table(/' -e '/^    align: (\(auto,\)*),$$/d' > $@

# Every screen and picture in the guide comes out of the emulator.
build/screens.stamp: sessions.json screens.py $(wildcard card/*) $(TOP)/fw/build/flash.img $(TOP)/emu/build/m68k-emu
	python3 screens.py
	@touch $@

screens:
	rm -f build/screens.stamp
	$(MAKE) all

build/figures/board.svg: figures/board.py $(BOARD)
	@mkdir -p build/figures
	python3 figures/board.py $(BOARD) $@

clean:
	rm -rf build

.PHONY: all screens clean
