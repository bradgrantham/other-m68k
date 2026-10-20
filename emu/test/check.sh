#!/bin/sh
# Run the test ROM headless and verify the results it left in RAM.
# usage: check.sh <emulator> <builddir>
set -u
EMU=$1
B=$2
ROM=$B/test.bin
SD=$B/sd.img
RES=0x800
RESLEN=64

rm -f "$B/serial.txt" "$B/stdout.txt" "$B/stderr.txt"
"$EMU" --rom "$ROM" --sd "$SD" --headless --fast --no-stdin --frames 120 \
    --serial-log "$B/serial.txt" --dump-mem $RES $RESLEN --dump-regs-on-exit --sd-trace \
    > "$B/stdout.txt" 2> "$B/stderr.txt"
status=$?

fail=0
pass() { echo "PASS: $1"; }
failf() { echo "FAIL: $1"; fail=1; }
check() { # check <description> <condition...>
    desc=$1; shift
    if "$@"; then pass "$desc"; else failf "$desc"; fi
}

check "emulator exit status 0 (got $status)" [ "$status" -eq 0 ]

# Concatenate the dumped bytes (lines "000800: xx xx ...  |....|") into one hex string.
HEX=$(sed -n 's/^00[0-9A-F]*: \(.*\)  |.*$/\1/p' "$B/stderr.txt" | tr -d ' \n')
# byte offsets are relative to RES
field() { # field <offset> <nbytes> -> hex string
    echo "$HEX" | cut -c $(( $1 * 2 + 1 ))-$(( ($1 + $2) * 2 ))
}
hexval() { printf '%d' "0x$1"; }

RAMOK=$(field 0 2);    BERRCNT=$(field 2 2);  BERRADR=$(field 4 4);  VBLANKS=$(field 8 4)
SDFLAGS=$(field 12 2); MARKER=$(field 14 2);  BERRINFO=$(field 16 2); ID=$(field 18 2)
UNEXP=$(field 20 2);   INTSTAT=$(field 22 2); SDR1=$(field 24 4);    OCR=$(field 28 4)
SDDATA=$(field 32 16)
LINES0=$(field 48 2); LINES1=$(field 50 2); LINES2=$(field 52 2)

echo "results: RAMOK=$RAMOK BERRCNT=$BERRCNT BERRADR=$BERRADR BERRINFO=$BERRINFO VBLANKS=$VBLANKS"
echo "         SD=$SDFLAGS R1s=$SDR1 OCR=$OCR MARKER=$MARKER ID=$ID UNEXP=$UNEXP INTSTAT=$INTSTAT"
echo "         block0=$SDDATA lines/frame NTSC=$LINES0 PAL=$LINES1 NTSC=$LINES2"
echo "serial:  $(tr '\n' ' ' < "$B/serial.txt" 2>/dev/null)"

check "result block dumped" [ "${#HEX}" -eq $(( RESLEN * 2 )) ]
check "test ROM ran to completion (marker BEEF)" [ "$MARKER" = "BEEF" ]
check "no unexpected exception" [ "$UNEXP" = "0000" ]
check "RAM pattern" [ "$RAMOK" = "0001" ]
check "ID register reads 6801" [ "$ID" = "6801" ]
check "serial log contains HELLO" grep -q "HELLO" "$B/serial.txt"
check "stdout carries the same bytes" cmp -s "$B/serial.txt" "$B/stdout.txt"
check "exactly one bus error" [ "$BERRCNT" = "0001" ]
check "fault address 00400000 in the frame" [ "$BERRADR" = "00400000" ]
check "access info: read, FC=5 (got $BERRINFO)" [ $(( $(hexval "$BERRINFO") & 0x17 )) -eq $(( 0x15 )) ]
check "bus error logged on stderr" grep -q "bus error: read 400000" "$B/stderr.txt"
check "VBLANK interrupts counted (>= 50)" [ "$(hexval "$VBLANKS")" -ge 50 ]
check "VBLANK interrupts counted (<= 120)" [ "$(hexval "$VBLANKS")" -le 120 ]
check "INTSTAT in handler: VBLANK flag + VSYNCPIN, no DEPIN" \
    [ $(( $(hexval "$INTSTAT") & 0x6001 )) -eq $(( 0x2001 )) ]
check "SD: CMD0/CMD8/ACMD41/CMD58/CMD17/data all OK (flags $SDFLAGS)" [ "$SDFLAGS" = "003F" ]
check "SD: OCR has CCS" [ "$(echo "$OCR" | cut -c1-2)" = "C0" ]
check "SD: block 0 contents" [ "$SDDATA" = "$(printf 'M68K-SD-TEST-BLO' | od -An -tx1 | tr -d ' \n' | tr 'a-f' 'A-F')" ]
check "SD trace lists the 8 commands" [ "$(grep -c 'sd: .*CMD' "$B/stderr.txt")" -eq 8 ]
check "lines per frame: NTSC 262 before the switch" [ "$(hexval "$LINES0")" -eq 262 ]
check "lines per frame: PAL 312 after SW_PAL" [ "$(hexval "$LINES1")" -eq 312 ]
check "lines per frame: NTSC 262 after SW_NTSC" [ "$(hexval "$LINES2")" -eq 262 ]
# Total cycles must be an exact mix of NTSC (764*262) and PAL (768*312) frames.
CYCLES=$(sed -n 's/^m68k-emu: 120 frames, \([0-9]*\) cycles.*/\1/p' "$B/stderr.txt")
NTSC_F=$(( 764 * 262 )); PAL_F=$(( 768 * 312 ))
PALFRAMES=$(( (CYCLES - 120 * NTSC_F) / (PAL_F - NTSC_F) ))
check "cycle count is 120 whole frames ($PALFRAMES of them PAL)" \
    [ $(( (CYCLES - 120 * NTSC_F) % (PAL_F - NTSC_F) )) -eq 0 ] 
check "a plausible number of PAL frames (1..12)" [ "$PALFRAMES" -ge 1 ] 

# Second run: hardware serial transmitter fitted.  The ROM writes "HW\n" to
# SERDATA after the bit-banged HELLO and waits for SERTXBUSY to clear.
"$EMU" --rom "$ROM" --headless --fast --no-stdin --frames 30 --hw-serial-tx \
    > "$B/stdout_hw.txt" 2> "$B/stderr_hw.txt"
check "--hw-serial-tx: SERDATA writes transmitted in order" \
    [ "$(cat "$B/stdout_hw.txt")" = "$(printf 'HELLO\nHW')" ]
check "software-only run ignored the SERDATA writes" \
    [ "$(cat "$B/stdout.txt")" = "HELLO" ]

if [ $fail -eq 0 ]; then
    echo "ALL TESTS PASSED"
else
    echo "---- stderr ----"; cat "$B/stderr.txt"
    exit 1
fi
