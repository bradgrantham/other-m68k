#!/bin/bash -e
# Synthesise one CPLD with yosys (hoglet67 atf15xx_yosys flow) and run the Atmel fitter.
#
#   hdl/fit.sh <variant> <source.v> <top> [chparam args] -- [fitter options]
#
# e.g.  hdl/fit.sh cpld2_io_notx cpld2_io.v cpld2_io -set HW_TX 0 -- -preassign ignore
# DEFS="-DNO_ID ..." adds Verilog defines.  Output in build/fit/<variant>.{edif,fit,pin,jed}.  Pin block: lines starting
# with //PIN: in the source (see docs/atf15xx.md); without one the fitter places freely.
# Run only one fit at a time (the fitter shares temp files in vendor/).

HERE=$(cd "$(dirname "$0")" && pwd)
FLOW=${ATF15XX_YOSYS:-$HOME/.local/src/atf15xx_yosys}
export WINEPREFIX=${WINEPREFIX:-$HOME/.wine_atf}
export WINEDEBUG=-all
DEVICE=${DEVICE:-ATF1508AS}; PACKAGE=${PACKAGE:-PLCC84}; SPEED=${SPEED:-15}

VARIANT=$1; SRC=$2; TOP=$3; shift 3
case "$SRC" in /*) ;; *) SRC="$SRC" ;; esac
CHPARAM=()
while [ $# -gt 0 ] && [ "$1" != "--" ]; do CHPARAM+=("$1"); shift; done
[ "$1" = "--" ] && shift
FITOPTS="$*"

# The fitter writes a scratch file and looks for atmel.std at the ROOT of the
# current drive, so the workspace lives under wine's C: drive (root writable) and
# hdl/build/fit is a symlink to it.  Z:\ (= /) is not writable.
WORK=$WINEPREFIX/drive_c/m68k_fit
mkdir -p "$HERE/build" "$WORK"
[ -L "$HERE/build/fit" ] || { rm -rf "$HERE/build/fit"; ln -s "$WORK" "$HERE/build/fit"; }
OUT=$WORK
ln -sfn "$FLOW/vendor" "$OUT/vendor"
cp -u "$FLOW/vendor/atmel.std" "$OUT/atmel.std"
CHP=""; [ ${#CHPARAM[@]} -gt 0 ] && CHP="chparam ${CHPARAM[*]} $TOP"

yosys -q -l "$OUT/$VARIANT.yosys.log" <<YS >/dev/null
read_liberty -lib $FLOW/cells.lib
read_verilog $DEFS -I$HERE/../generated $SRC
$CHP
hierarchy -top $TOP
tribuf
synth -flatten -noabc -top $TOP
techmap -map $FLOW/techmap.v -D skip_DFFE_XX_
simplemap
dfflibmap -liberty $FLOW/cells.lib
abc -liberty $FLOW/cells.lib
iopadmap -bits -inpad INBUF Q:A -outpad BUF A:Q -toutpad TRI ENA:A:Q -tinoutpad bibuf EN:Q:A:PAD
clean
hierarchy
splitnets -format _
rename -wire -suffix _reg t:*DFF*
rename -wire -suffix _comb
delete t:\$scopeinfo
write_edif -lsbidx $OUT/$VARIANT.edif
YS

# pin block: lines "//PIN: net : pin" in the source; PINFILE env overrides
if [ -n "$PINFILE" ]; then cp "$PINFILE" "$OUT/$VARIANT.pin"
else grep '//PIN:' "$SRC" | cut -d' ' -f2- > "$OUT/$VARIANT.pin" || true; fi

cd "$OUT"
rm -f "$VARIANT.fit" "$VARIANT.jed"
FITTER=fit${DEVICE//[!0-9]/}.exe
VEND='C:\m68k_fit\vendor'
wine "$VEND\\$FITTER" -i "$VARIANT.edif" -ifmt edif -o "$VARIANT.jed" \
     -lib "$VEND\\aprim.lib" -tech $DEVICE -device $PACKAGE -tpd $SPEED $FITOPTS \
     > "$VARIANT.fitter.out" 2>&1 || true
grep -E "Fitter_Pass|Warning|ERROR|Error|refused|Fanin" "$VARIANT.fit" "$VARIANT.fitter.out" 2>/dev/null | sort -u | head -20
echo "Logic Array Block  Macro Cells  I/O Pins  Foldbacks  TotalPT  FanIN  Cascades"
grep "^[A-H]:" "$VARIANT.fit" 2>/dev/null
grep -E "^Total|^\\\$Device" "$VARIANT.fit" 2>/dev/null
[ -s "$VARIANT.jed" ] && echo "JED written: $OUT/$VARIANT.jed" || { echo "NO JED"; exit 1; }
