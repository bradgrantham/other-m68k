# Tool setup

What this project needs on the development machine, where it lives, and what still has to be
fetched by hand.

## Installed and verified (2026-10-19)

| tool | version / location | used by |
|---|---|---|
| m68k-unknown-elf-gcc | 15.2, `~/x-tools/m68k-unknown-elf/bin` (crosstool-ng, `--with-cpu=68000`, newlib) | fw/, sdk/, emu/test |
| SDL2 dev | 2.30 (apt) | emu/ |
| iverilog / vvp | 12.0 (apt) | hdl/ simulation |
| yosys | 0.33 (apt) | hdl/ synthesis sanity checks, and the ATF15xx flow below |
| KiCad | 9.0.9 (PPA), `kicad-cli` and the `pcbnew` Python module | kicad/ |
| Java | OpenJDK 21 (apt) | freerouting |
| freerouting CLI | 2.5.0, `~/.local/share/freerouting/freerouting-cli` (self-contained binary) | kicad/ autorouting |
| wine + winetricks | 9.0 (apt) | ATF15xx fitter |
| python3 | 3.12, stdlib only | tools/gen.py, kicad/ |

Reference documents kept in `~/.local/share/datasheets/`: ATF1508AS datasheet, SN74AHCT245,
M68000 User's Manual (section 10.10 has the AC timing used in `docs/architecture.md`).

## Still needed, by hand

### ATF1508AS fitter (to turn Verilog into a .jed)

The flow is yosys -> EDIF -> Atmel `fit1508.exe` under wine, scripted by
`~/.local/src/atf15xx_yosys` (hoglet67's atf15xx_yosys, already cloned). The fitter itself is
only shipped inside Microchip's Windows packages:

1. Download **WinCUPL** from Microchip (free; the PLD design resources page under
   "FPGAs and PLDs -> SPLD/CPLD -> PLD design resources", installer `awincupl.exe`). ProChip
   Designer also contains it but needs a license request.
2. Put the installer in `~/Downloads`. Then, in a terminal:
   ```
   WINEPREFIX=$HOME/.wine_atf WINEARCH=win32 winetricks -q vcrun6sp6
   WINEPREFIX=$HOME/.wine_atf wine ~/Downloads/awincupl.exe       # default options
   ```
3. Copy `fit1508.exe`, `fit1504.exe`, `fit1502.exe`, `atmel.std` and `aprim.lib` from the
   WinCUPL install (`~/.wine_atf/drive_c/Wincupl/WinCupl/Fitters/` or similar) into
   `~/.local/src/atf15xx_yosys/vendor/`.

I will handle step 3 and wire `make -C hdl jed` to the scripts once the installer is present.

### Programming the CPLDs

JED -> SVF conversion is done by Microchip's **ATMISP** (also a Windows download from the same
page; runs under the same wine prefix) and the SVF is played with OpenOCD through the FT232H:
`sudo apt install openocd` when we get there.

### AD724 datasheet

analog.com refuses non-browser downloads. Please save
`https://www.analog.com/media/en/technical-documentation/data-sheets/AD724.pdf` into
`~/.local/share/datasheets/AD724.pdf`; the schematic needs its application circuit values.
