#!/bin/sh
# Run the test ROM headless and verify the results it left in RAM.
# usage: check.sh <emulator> <builddir>
set -u
EMU=$1
B=$2
ROM=$B/test.bin
SD=$B/sd.img
RES=0x800
RESLEN=96
HW_H=$(dirname "$0")/../../generated/hw.h

rm -f "$B/serial.txt" "$B/stdout.txt" "$B/stderr.txt"
# "ABCDEF" is fed into the DUART's channel A after frame 1, while the ROM
# has the receiver enabled but the interrupt masked: A B C fill the FIFO,
# D and E are lost (overrun), F waits in the shift register.
"$EMU" --rom "$ROM" --sd "$SD" --headless --fast --no-stdin --frames 120 \
    --serial-type ABCDEF --serial-type-delay 1 \
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
# Spec constants from the generated header, as upper-case hex without 0x.
hwconst() { sed -n "s/^#define $1 0x\([0-9A-Fa-f]*\).*/\1/p" "$HW_H" | tr 'a-f' 'A-F'; }

RAMOK=$(field 0 2);    BERRCNT=$(field 2 2);  BERRADR=$(field 4 4);  VBLANKS=$(field 8 4)
SDFLAGS=$(field 12 2); MARKER=$(field 14 2);  BERRINFO=$(field 16 2); ID=$(field 18 2)
UNEXP=$(field 20 2);   INTSTAT=$(field 22 2); SDR1=$(field 24 4);    OCR=$(field 28 4)
SDDATA=$(field 32 16)
LINES0=$(field 48 2); LINES1=$(field 50 2); LINES2=$(field 52 2)
SRA0=$(field 54 1); SRA1=$(field 55 1); SRARX=$(field 56 1); RXFIRST=$(field 57 1)
RXBUF=$(field 58 4); IRQCNT=$(field 62 2); ISRVAL=$(field 64 1); INTSTAT3=$(field 65 1)
GATED=$(field 66 2); GATEDCNT=$(field 68 2); CTCNT=$(field 70 2); CTPOLL=$(field 72 2)
ISRSTOP=$(field 74 1); ISRTC=$(field 75 1); SRAERR=$(field 76 1); MR1RD=$(field 77 1)
MR2RD=$(field 78 1); IVR=$(field 79 1); CUR=$(field 80 2); WORDRD=$(field 82 2)

echo "results: RAMOK=$RAMOK BERRCNT=$BERRCNT BERRADR=$BERRADR BERRINFO=$BERRINFO VBLANKS=$VBLANKS"
echo "         SD=$SDFLAGS R1s=$SDR1 OCR=$OCR MARKER=$MARKER ID=$ID UNEXP=$UNEXP INTSTAT=$INTSTAT"
echo "         block0=$SDDATA lines/frame NTSC=$LINES0 PAL=$LINES1 NTSC=$LINES2"
echo "duart:   IVR=$IVR MR1=$MR1RD MR2=$MR2RD SRA0=$SRA0 SRA1=$SRA1 WORDRD=$WORDRD SRARX=$SRARX first=$RXFIRST"
echo "         isr-buf=$RXBUF irqs=$IRQCNT ISR=$ISRVAL INTSTAT.lo=$INTSTAT3 gated=$GATED/$GATEDCNT SRAERR=$SRAERR"
echo "         counter: polls=$CTPOLL ISR tc/stop=$ISRTC/$ISRSTOP CUR:CLR=$CUR  timer irqs=$CTCNT"
echo "serial:  $(tr '\n' ' ' < "$B/serial.txt" 2>/dev/null)"
grep 'duart: channel A enabled' "$B/stderr.txt" | sed 's/^/note:    /'

check "result block dumped" [ "${#HEX}" -eq $(( RESLEN * 2 )) ]
check "test ROM ran to completion (marker BEEF)" [ "$MARKER" = "BEEF" ]
check "no unexpected exception" [ "$UNEXP" = "0000" ]
check "RAM pattern" [ "$RAMOK" = "0001" ]
check "ID register reads IO_ID_RESET ($(hwconst IO_ID_RESET))" [ "$ID" = "$(hwconst IO_ID_RESET)" ]
check "serial output is exactly HELLO" [ "$(cat "$B/serial.txt")" = "HELLO" ]
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

# DUART
check "DUART: IVR reads 0F after reset" [ "$IVR" = "0F" ]
check "DUART: MR1A/MR2A read back after reset-MR-pointer" \
    [ "$MR1RD" = "$(hwconst SERIAL_MR1_VALUE)" ] && [ "$MR2RD" = "$(hwconst SERIAL_MR2_VALUE)" ]
check "DUART: SRA = TXRDY+TXEMT (0C) right after enable" [ "$SRA0" = "0C" ]
check "DUART: SRA = 00 with a byte shifting and one in the holding register" [ "$SRA1" = "00" ]
check "DUART: word read of SRA gives FF in the upper byte (got $WORDRD)" [ "$WORDRD" = "FF0C" ]
check "DUART: word access warned once on stderr" [ "$(grep -c 'word read of F20002' "$B/stderr.txt")" -eq 1 ]
check "DUART: 6 bytes unread -> SRA RXRDY+FFULL+OE with the transmitter idle (1F)" [ "$SRARX" = "1F" ]
check "DUART: first byte popped is A" [ "$RXFIRST" = "41" ]
check "DUART: IRQ pin visible in INTSTAT.DUARTIRQ while INTEN.DUART is clear" \
    [ $(( $(hexval "$GATED") & 0x8 )) -ne 0 ]
check "DUART: no interrupt taken while gated" [ "$GATEDCNT" = "0000" ]
check "DUART: exactly one level-3 interrupt once INTEN.DUART is set" [ "$IRQCNT" = "0001" ]
check "DUART: ISR in the handler shows RXRDYA" [ $(( $(hexval "$ISRVAL") & 0x2 )) -ne 0 ]
check "DUART: INTSTAT in the handler shows DUARTIRQ" [ $(( $(hexval "$INTSTAT3") & 0x8 )) -ne 0 ]
check "DUART: handler drained B C F (shift register refilled the FIFO)" [ "$RXBUF" = "42434600" ]
check "DUART: reset-error-status clears OE (SRA 0C)" [ "$SRAERR" = "0C" ]
check "DUART: counter mode reached terminal count (ISR.CTR after $(hexval "$CTPOLL") polls)" \
    [ $(( $(hexval "$ISRTC") & 0x8 )) -ne 0 ] && [ "$(hexval "$CTPOLL")" -gt 10 ] && [ "$(hexval "$CTPOLL")" -lt 32767 ]
check "DUART: STOP_CTR clears ISR.CTR (ISR $ISRSTOP)" [ $(( $(hexval "$ISRSTOP") & 0x8 )) -eq 0 ]
check "DUART: stopped counter rolled past 0 (CUR = FF)" [ "$(echo "$CUR" | cut -c1-2)" = "FF" ]
check "DUART: timer mode interrupts during ~2.5 frames (40..100, got $(hexval "$CTCNT"))" \
    [ "$(hexval "$CTCNT")" -ge 40 ] && [ "$(hexval "$CTCNT")" -le 100 ]
check "summary counts 2 overruns and nothing lost" grep -q "6 in (2 overruns, 0 lost)" "$B/stderr.txt"

if [ $fail -eq 0 ]; then
    echo "ALL TESTS PASSED"
else
    echo "---- stderr ----"; cat "$B/stderr.txt"
    exit 1
fi
