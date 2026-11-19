#!/bin/sh
# run.sh -- static constructors and destructors in SDK programs. Puts sdk/build/ctors.elf
# (sdk/examples/ctors.cpp) on a C: volume of its own behind the BIOS, runs it in the emulator
# and compares what it printed with the order the start-up and exit code must keep:
# .preinit_array, constructors by priority, static objects in definition order, main, then
# at exit the atexit handler, the objects in reverse, the destructor functions.
# Prints PASS:/FAIL: lines and ALL TESTS PASSED; exit 1 on any failure.
cd "$(dirname "$0")"
TOP=../..
B=build
EMU=$TOP/emu/build/m68k-emu
ROM=$TOP/fw/build/rom.bin
ELF=$TOP/sdk/build/ctors.elf
XTOOLS=${XTOOLS:-$HOME/x-tools/m68k-unknown-elf/bin}
fails=0
pass() { echo "PASS: $1"; }
fail() { echo "FAIL: $1"; fails=$((fails + 1)); }

mkdir -p $B
[ -x $EMU ] && [ -f $ROM ] && [ -f $ELF ] || { echo "run.sh: build the emulator, the firmware and the SDK first (make all)" >&2; exit 1; }

# the tables made it into the program: 1 preinit entry, 3 constructors (two functions and
# the one that builds both objects), 2 destructor functions
sizes=
for s in .preinit_array .init_array .fini_array; do
    hex=$($XTOOLS/m68k-unknown-elf-objdump -h $ELF | awk -v s=$s '$2 == s { print $3 }')
    sizes="$sizes$s=$(( 0x${hex:-0} / 4 )) "
done
[ "$sizes" = ".preinit_array=1 .init_array=3 .fini_array=2 " ] && pass "tables in ctors.elf ($sizes)" || fail "tables in ctors.elf ($sizes)"

# the SDK headers from C++: they compile, and what they declare has C linkage (unmangled names)
printf '#include "syscalls.h"\n#include "pdm.h"\n#include "bmp.h"\n#include "sstv.h"\nint use(struct bmp *b) { pdm_reset(); bmp_open(b, "X.BMP"); return (int)sys_ticks() + sstv_mode_lines(SSTV_ROBOT36); }\n' > $B/hdr.cpp
if $XTOOLS/m68k-unknown-elf-g++ -m68000 -Os -Wall -Wextra -Werror -fno-exceptions -fno-rtti -I$TOP/generated -I$TOP/sdk -c -o $B/hdr.o $B/hdr.cpp 2> $B/hdr.log &&
   [ "$($XTOOLS/m68k-unknown-elf-nm -u $B/hdr.o | awk '{ print $2 }' | sort | tr '\n' ' ')" = "bmp_open pdm_reset sstv_mode_lines sys_ticks " ]
then pass "syscalls.h, pdm.h, bmp.h and sstv.h from C++"; else cat $B/hdr.log; $XTOOLS/m68k-unknown-elf-nm -u $B/hdr.o; fail "SDK headers from C++"; fi

$XTOOLS/m68k-unknown-elf-strip -o $B/CTORS.ELF $ELF &&
sh $TOP/tools/mkflashvol.sh $B/c.img $B/CTORS.ELF > $B/mkvol.log 2>&1 &&
python3 $TOP/fw/mkrom.py --split $B/flash.img $B/even.bin $B/odd.bin $ROM $B/c.img > $B/mkrom.log 2>&1 ||
    { cat $B/mkvol.log $B/mkrom.log 2>/dev/null; fail "flash image with CTORS.ELF on C: (needs mkfs.vfat and mcopy)"; echo "$fails FAILED"; exit 1; }

"$EMU" --rom $B/flash.img --headless --fast --no-stdin --exit-on-berr --frames 600 \
    --type 'ctors\n' --type-delay 350 --text-dump $B/screen.txt > $B/serial.txt 2> $B/emu.log
status=$?
[ $status = 0 ] && grep -q ', 0 bus errors' $B/emu.log && pass "emulator ran ($(tail -1 $B/emu.log | sed 's/^m68k-emu: //'))" || { cat $B/emu.log; fail "emulator exit status $status"; }

cat > $B/expected.txt <<'END'
 1 .preinit_array entry
 2 constructor(101)
 3 constructor(102)
 4 first constructed
 5 second constructed
 6 main
 7 atexit handler
 8 second destroyed
 9 first destroyed
10 destructor
11 destructor(101)
END
# what the program printed: the lines between its command line and the next prompt
sed 's/ *$//' $B/screen.txt | awk '/^C:\/> ctors$/ { on = 1; next } /^C:\/>/ { on = 0 } on' > $B/got.txt
if cmp -s $B/expected.txt $B/got.txt; then pass "constructors, main and destructors in order (11 steps)"
else fail "order of the steps"; diff $B/expected.txt $B/got.txt; cat $B/screen.txt; fi
# the program came back to the monitor through exit(): a prompt after the last line, no [exit N]
sed 's/ *$//' $B/screen.txt | awk '/^11 destructor\(101\)$/ { getline; print }' | grep -q '^C:/>$' && ! grep -q '\[exit' $B/screen.txt &&
    pass "back at the prompt with exit status 0" || fail "prompt after the program"

[ $fails = 0 ] && echo "ALL TESTS PASSED" || echo "$fails FAILED"
exit $((fails != 0))
