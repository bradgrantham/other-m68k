#!/bin/sh
# run.sh [host]  -- editor tests. Host part: test_core.c (fixed cases and random operations
# against a string model). Emulator part: EDIT.ELF from C: edits files on a card with keys
# typed by --type; the files are then read back from the card image and compared byte for byte.
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

mkdir -p $B
echo "== host: the core"
if gcc -std=gnu11 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I$TOP/sdk -o $B/test_core test_core.c $TOP/sdk/edit_core.c \
   && ./$B/test_core > $B/test_core.log 2>&1; then pass "test_core ($(tail -1 $B/test_core.log))"
else cat $B/test_core.log 2>/dev/null; fail "test_core"; fi
[ "$MODE" = host ] && { echo "host part only"; [ $fails = 0 ] && echo "ALL TESTS PASSED"; exit $((fails != 0)); }

echo "== emulator: the editor"
[ -x $EMU ] && [ -f $FLASH ] || { echo "run.sh: build the emulator and 'make flash' first" >&2; exit 1; }
printf 'one\r\ntwo\r\nthree\r\n' > $B/CRLF.TXT
printf 'alpha\nbeta\n' > $B/UNIX.TXT
sh $TOP/fw/test/mkimage.sh $B/sd.img 64 $B/CRLF.TXT $B/UNIX.TXT > /dev/null 2>&1 || fail "sd image (needs mkfs.vfat and mcopy)"

# run KEYS: type them at the monitor; \p waits half a second (the editor drops keys typed
# before it has started)
run() {
    "$EMU" --rom $FLASH --sd $B/sd.img --headless --fast --no-audio --no-stdin --exit-on-berr \
        --frames ${2:-1500} --type-delay 350 --type "$1" --text-dump $B/screen.txt > $B/last.log 2>&1 \
        || { cat $B/last.log; fail "emulator run"; }
    grep -v '^$' $B/screen.txt | tail -1 | grep -q "^C:/>" || { cat $B/screen.txt; fail "back at the prompt"; }
}
# expect FILE BYTES: the file on the card holds exactly these bytes (printf format)
expect() {
    rm -f $B/got
    MTOOLS_SKIP_CHECK=1 mcopy -i $B/sd.img ::/$1 $B/got 2>/dev/null
    printf "$2" > $B/want
    if cmp -s $B/got $B/want; then pass "$3"; else echo "want:"; od -c $B/want; echo "got:"; od -c $B/got 2>/dev/null; fail "$3"; fi
}

# typing, Enter, a selection cut and pasted, an undo, save and quit; CR LF kept
run 'edit D:CRLF.TXT\n\p\p\p\<end> plus\nNEW\<down>\<home>\<S-end>\^x\<C-end>\^vX\^z\^s\^q\p\p\n'
expect CRLF.TXT 'one plus\r\nNEW\r\n\r\nthree\r\ntwo' "edit, cut, paste, undo, save: CR LF file"

# find selects the match, typing replaces it; quitting with changes asks, y saves; LF kept
run 'edit D:UNIX.TXT\n\p\p\p\^fBETA\ngamma\^q\p y\p\p\n'
expect UNIX.TXT 'alpha\ngamma\n' "find and replace, save on quit: LF file"

# Shift-select across lines, copy, paste twice, select all and type over, undo it
run 'edit D:UNIX.TXT\n\p\p\p\<S-down>\^c\<C-end>\^v\^v\^ax\^z\^s\^q\p\p\n'
expect UNIX.TXT 'alpha\ngamma\nalpha\nalpha\n' "select lines, copy, paste, select all, undo"

# word movement, Delete, Backspace, go to line, Tab
run 'edit D:UNIX.TXT\n\p\p\p\^g2\n\<C-right>\<C-left>\<del>\b\t!\^s\^q\p\p\n'
expect UNIX.TXT 'alpha   !amma\nalpha\nalpha\n' "go to line, word keys, Delete, Backspace, Tab"

# a new file that is not saved is not created
run 'edit D:NEW.TXT\n\p\p\phello\^q\p n\p\p\n'
if MTOOLS_SKIP_CHECK=1 mdir -i $B/sd.img ::/NEW.TXT > /dev/null 2>&1; then fail "an unsaved new file was created"; else pass "quit without saving creates nothing"; fi

# a new file that is saved
run 'edit D:NEW.TXT\n\p\p\phello\nworld\^s\^q\p\p\n'
expect NEW.TXT 'hello\nworld' "new file saved"

# paging through a long file and typing at the end
awk 'BEGIN { for (i = 1; i <= 300; i++) printf "line %d of the long file\n", i }' > $B/LONG.TXT
MTOOLS_SKIP_CHECK=1 mcopy -o -i $B/sd.img $B/LONG.TXT ::/LONG.TXT
run 'edit D:LONG.TXT\n\p\p\p\<pgdn>\<pgdn>\<pgdn>\<pgup>\<C-end>END\<C-home>TOP \^g150\n\<S-end>\^x\^s\^q\p\p\n' 2200
awk 'BEGIN { for (i = 1; i <= 300; i++) printf "%s%s\n", (i == 1 ? "TOP " : ""), (i == 150 ? "" : "line " i " of the long file"); printf "END" }' > $B/want.long
rm -f $B/got; MTOOLS_SKIP_CHECK=1 mcopy -i $B/sd.img ::/LONG.TXT $B/got 2>/dev/null
if cmp -s $B/got $B/want.long; then pass "long file: paging, both ends, go to line"; else cmp $B/got $B/want.long; fail "long file"; fi

[ $fails = 0 ] && echo "ALL TESTS PASSED" || echo "$fails FAILED"
exit $((fails != 0))
