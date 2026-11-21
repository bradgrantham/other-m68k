# docs/users-guide/ - the Griffin Jr. user's guide

A manual for the person at the keyboard, in the manner of the home-computer manuals of the
early 1980s. `CLAUDE.md` at the top of the tree has the writing rules (plain language, the
audience, what must be re-run when the firmware or the program list changes).

```
make            # build/griffin-jr-users-guide.pdf
make screens    # capture every screen again, then build
make clean
```

## How it is put together

| file | what |
|---|---|
| `NN-name.md` | one chapter each, in file-name order |
| `sessions.json` | the emulator sessions the chapters show: what is typed, how long to run |
| `screens.py` | runs the sessions in `emu/build/m68k-emu` on `fw/build/flash.img` and writes `build/screens/NAME.txt` (the console text) and `build/shots/NAME.png` (the picture) |
| `card/` | files for the SD card (drive D:) that the sessions use. The `.BAS` listings and `GREET.CMD` also go into the card package (`make card` at the top level); `screens.py` builds its card from that package, the rest of `card/`, and a picture and a sound it makes itself |
| `card/*.BAS` | the BASIC listings of chapter 5: the chapter prints these files and the sessions run them |
| `figures/board.py` | draws the board tour from `kicad/release/other-m68k.kicad_pcb` |
| `figures/diagrams.py` | draws the line diagrams (colour chart, colour byte, screen addresses, memory map, port pinouts, sound patterns); addresses come from `generated/hw.h` |
| `filter.lua` | pandoc filter: captured screens, the three kinds of box, key caps, picture paths |
| `guide.typ` | the page design, title page and contents (Typst) |

Nothing a reader sees on a screen is typed into a chapter. A chapter names a session:

````
```{.screen session=help}
```
````

and the build puts in what the emulator showed. `make` runs the sessions again whenever
`flash.img`, the emulator, `sessions.json` or `card/` is newer than the last capture, so a
firmware change shows up in the PDF. The words around a screen still have to be read
against it by hand.

Other chapter markup:

- ```` ```{.listing file=card/NAME.BAS} ```` prints a file as a listing.
- The first appendix starts with a raw Typst block that calls `#appendices()`; the chapters
  after it are lettered.

- `::: curious`, `::: try`, `::: careful` open a box; `:::` closes it.
- `[ESC]{.key}` draws a key cap.
- `![caption](shots/NAME.png){width=62%}` is a captured picture (a session with
  `"shot": true`); `figures/NAME.svg` is a drawn one.
- A table without column headings starts with an empty heading row (`| | |`). The number of
  dashes under each column sets its share of the width.

## Tools

pandoc 3.x (Markdown to Typst) and Typst 0.13 (Typst to PDF). Neither is an Ubuntu 24.04
package in a usable version; both are single binaries from their GitHub release pages. The
Makefile looks for `~/.local/opt/pandoc-*/bin/pandoc` and `~/.local/opt/typst-*/typst`, then
for `pandoc` and `typst` on the PATH (`make PANDOC=... TYPST=...` overrides). Also needed:
python3 with Pillow and the `pcbnew` module (KiCad 9), `mkfs.vfat` and `mcopy` for the card
image, and the built emulator, flash image and card package (`make emu flash card` at the top level).

Fonts come from the machine: C059 for the text, Nimbus Sans for headings (both in
`fonts-urw-base35`), DejaVu Sans Mono for listings.

For a banner without `-dirty` in the version line, commit before `make screens`.
