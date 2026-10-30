#!/bin/sh
# run.sh [host]  -- SSTV encoder tests. Host part: host_test.c (sample budgets, 9 WAVs) and
# decode.py on three of them. Emulator part: SSTV.ELF on C: sends the console in Robot 36 to
# --audio-wav (PDM + the board's RC filter model) and writes a WAV to D:; both are decoded.
# Prints PASS:/FAIL: lines and ALL TESTS PASSED; exit 1 on any failure.
cd "$(dirname "$0")"
MODE=$1
TOP=../..
B=build
EMU=$TOP/emu/build/m68k-emu
FLASH=$TOP/fw/build/flash.img
fails=0
pass() { echo "PASS: $1"; }
fail() { echo "FAIL: $1"; fails=$((fails + 1)); }
step() { if "$@" > "$B/last.log" 2>&1; then return 0; else cat "$B/last.log"; return 1; fi; }

mkdir -p $B
echo "== host encoder"
gcc -std=gnu11 -O2 -Wall -Wextra -I$TOP/sdk -I$TOP/fw -o $B/host_test host_test.c || { fail "host_test builds"; exit 1; }
if ./$B/host_test $B > $B/host_test.log 2>&1; then pass "host_test ($(tail -1 $B/host_test.log))"; else cat $B/host_test.log; fail "host_test"; fi
for t in "r36-ntsc 12000000/764" "r72-pal 12000000/768" "m1-48k 48000/1"; do
    set -- $t
    if python3 decode.py $B/$1.wav $B/$1.png --rate $2 --ref $B/card.ppm > $B/decode-$1.log 2>&1; then pass "decode $1 ($(grep -c '^PASS' $B/decode-$1.log) checks)"
    else cat $B/decode-$1.log; fail "decode $1"; fi
done
[ "$MODE" = host ] && { echo "host part only"; [ $fails = 0 ] && echo "ALL TESTS PASSED"; exit $((fails != 0)); }

echo "== emulator"
[ -x $EMU ] && [ -f $FLASH ] || { echo "run.sh: build the emulator and 'make flash' first" >&2; exit 1; }
# Robot 36 from the console (38 s of audio): about 2300 frames after the 350-frame boot delay
step "$EMU" --rom $FLASH --headless --no-stdin --exit-on-berr --frames 2900 \
    --type 'sstv --no-ui\n' --type-delay 350 --audio-wav $B/emu-r36.wav --text-dump $B/emu-r36.txt
grep -q "CPU idle" $B/emu-r36.txt && pass "sstv ran: $(grep 'CPU idle' $B/emu-r36.txt)" || { cat $B/emu-r36.txt; fail "sstv summary line"; }
if python3 decode.py $B/emu-r36.wav $B/emu-r36.png --rate 48000/1 > $B/decode-emu.log 2>&1; then pass "decode emulator audio ($(grep -c '^PASS' $B/decode-emu.log) checks)"
else cat $B/decode-emu.log; fail "decode emulator audio"; fi
# --wav onto D:
sh $TOP/fw/test/mkimage.sh $B/sd.img 64 > /dev/null 2>&1 || fail "sd image (needs mkfs.vfat and mcopy)"
step "$EMU" --rom $FLASH --sd $B/sd.img --headless --fast --no-stdin --exit-on-berr --frames 3000 \
    --type 'sstv --no-ui --wav D:OUT.WAV\n' --type-delay 350 --no-audio --text-dump $B/emu-wav.txt
grep -q "OUT.WAV in" $B/emu-wav.txt && pass "wav written: $(grep 'OUT.WAV in' $B/emu-wav.txt)" || { cat $B/emu-wav.txt; fail "sstv --wav summary line"; }
rm -f $B/target.wav
MTOOLS_SKIP_CHECK=1 mcopy -i $B/sd.img ::/OUT.WAV $B/target.wav 2>/dev/null || fail "OUT.WAV on the card"
if [ -f $B/target.wav ] && python3 decode.py $B/target.wav $B/target.png --rate 12000000/764 > $B/decode-target.log 2>&1; then pass "decode target WAV ($(grep -c '^PASS' $B/decode-target.log) checks)"
else [ -f $B/target.wav ] && cat $B/decode-target.log; fail "decode target WAV"; fi
# the flash volume is refused
step "$EMU" --rom $FLASH --headless --fast --no-stdin --frames 500 \
    --type 'sstv --no-ui --wav C:OUT.WAV\n' --type-delay 350 --no-audio --text-dump $B/emu-cwav.txt
grep -q "too small" $B/emu-cwav.txt && pass "C: refused for --wav" || { cat $B/emu-cwav.txt; fail "C: refused for --wav"; }

[ $fails = 0 ] && echo "ALL TESTS PASSED" || echo "$fails FAILED"
exit $((fails != 0))
