# fw/ + sdk/ status (stage 1) -- written at a forced session stop

**State: design complete, build scaffolding in place, no firmware/SDK source code written yet.**
The session was stopped right after the mandatory reading/toolchain investigation and before
source files were created. Nothing here has run on the emulator (which does not exist yet).

## What exists and is verified

| File | Status |
|---|---|
| `fw/Makefile` | Complete. `make -C fw` fails with "sources not written yet (crt0.S missing)" until `crt0.S` exists; `make -C fw clean` works; `make -C fw test` fails with a clear message until `test/test_main.c` exists. Targets: `build/rom.elf`, `rom.bin` (objcopy `--gap-fill=0xFF --pad-to=0xE20000`), `rom.lst`, `rom_even.bin`/`rom_odd.bin` (via mkrom.py), `rom.map`. Flags: `-m68000 -Os -Wall -Wextra -ffreestanding -nostdlib -fno-delete-null-pointer-checks`, `-I../generated`, links `-lgcc` (needed for 32-bit mul/div on 68000). |
| `fw/rom.ld` | Complete and smoke-tested with a stub object: INCLUDEs `../generated/memory.ld`; `.vectors` (1 KB, KEEP) first in ROM; `.text/.rodata` in ROM; `.data` in SYSDATA `AT> ROM` with `_data_start/_data_end/_data_load`; `.bss` in SYSDATA with `_bss_start/_bss_end`; `.romtail` at `ORIGIN(ROM)+LENGTH(ROM)-2` defines `_rom_checksum` (0xE1FFFE). Verified: rom.bin = 131072 bytes, `_start` at 0xE00400. |
| `fw/mkrom.py` | Complete and tested. Patches the last ROM word so the 16-bit sum of all 65536 big-endian words is 0; writes even (D15..D8) / odd (D7..D0) 64 KB images. |
| `sdk/user.ld` | Complete and smoke-tested: INCLUDEs `../generated/user.ld`, entry 0x4000, one PT_LOAD at USER_BASE, `__bss_start/__bss_end/_end/__heap_start`. ld warns "LOAD segment with RWX permissions" (harmless; silence with `-Wl,--no-warn-rwx-segments` or split text/data into two PT_LOADs). |
| `sdk/Makefile` | Complete structure: builds `build/libsys.a` (generated/syscall_stubs.S + newlib_shim.c), `crt0.o`, `hello.elf`/`plasma.elf` + `.srec`; fails with a clear message until `crt0.S`/`newlib_shim.c` exist. Link line: `-nostartfiles -T user.ld crt0.o prog.o -lc -lsys -lgcc`. |

## Toolchain facts discovered (important for the next person)

* `~/x-tools/m68k-unknown-elf` GCC 15.2, single multilib (`-m68000` is the default and only lib).
* **newlib here expects the libgloss names without underscore**: `libc.a` has undefined
  `write read sbrk open close lseek fstat isatty kill getpid _exit` (and `gettimeofday times
  link unlink stat environ` for rarely-used functions). `_write` etc. are NOT what it calls.
  `newlib_shim.c` must define `write/read/...`; add `_write/...` as aliases only for documentation.
* `__libc_init_array` exists in libc but references `_init` (from crti, absent with
  `-nostartfiles`); either skip constructors in crt0 or define an empty `_init`/`_fini`.
* GCC m68k `__attribute__((interrupt_handler))` works (emits register save + `rte`), verified.
* Host gcc 13.3 available for `fw/test`.

## Design decisions already made (implement exactly this)

### Vectors / entry (`fw/crt0.S`)
* 256-entry table in `.vectors`: [0]=SYS_STACK_TOP (0x4000), [1]=`_start`; vectors 25,26,27,28
  (VBLANK/PS2/SERRX/HSYNC ISRs) and 47 (TRAP #15) point straight at their handlers; every other
  vector -> `vec_stub_N: move.w #N,-(%sp); jmp exception_common` (10 bytes each, generated with
  an assembler `.irp`/macro loop). NMI (31) deliberately goes through the stub so a break prints
  registers and enters the monitor.
* `exception_common`: `ori.w #0x0700,%sr`; `movem.l %d0-%d7/%a0-%a6,exc_regs`; pop vector word;
  if vector 2 or 3 pop the group-0 extension (FC word, fault address long, IR word); pop SR, PC;
  record SSP (after frame) and USP (`move.l %usp,%a0`); `lea _sys_stack_top,%sp`;
  `jsr exception_entry` (C prints and enters monitor; never returns). 68000 group-0 frame:
  +0 FC (bit4=R/W,1=read; bit3=I/N; bits2..0 FC), +2 access address, +6 IR, +8 SR, +10 PC.
* After RAM passes: copy 1 KB from ROM_BASE to 0, copy `.data` (`_data_load`->`_data_start`),
  zero `.bss`, store POST results from registers into globals, `lea _sys_stack_top,%sp`, `jsr bios_main`.

### POST (`fw/post.S`, registers only, no RAM)
* d7 = GPIO shadow (start 0x0007|LED0=0x0017). Serial subroutines return via registers:
  `post_putc` (d0.b in, returns `jmp (%a6)`), `post_puts` (a5 string, returns `jmp (%a4)`),
  `post_puthex32` (d3, returns `jmp (%a4)`).
* ROM checksum: `move.w #0xFFFF,%d2; 1: add.w (%a0)+,%d0; dbf %d2,1b` over 64K words, expect 0.
  Report "ROM sum xxxx OK/BAD" but do not stop on BAD (stored word also kept for the banner).
* RAM test 0x000000-0x03FFFF (overlay is already clear since we execute from 0xE0xxxx):
  (1) address-in-address longs (`move.l %a0,(%a0)+`, verify), (2) long fills 0x55555555,
  0xAAAAAAAA, 0x00000000, 0xFFFFFFFF with `movem.l` of 12 registers (48 bytes/iteration) and
  unrolled `cmp.l (%a0)+,%dN` verification, (3) byte-lane pass: `move.b` 0x55 to even and 0xAA to
  odd bytes, verify longs 0x55AA55AA, then inverse. Estimated ~2 s total at 12 MHz.
  On failure print `RAM FAIL @aaaaaaaa exp xxxxxxxx got yyyyyyyy` and blink forever.
* LED codes (document in README): LED0 on = POST started/ROM checksum; LED0+LED1 = RAM test;
  LED1 only = C init; both off = monitor idle; LED0/LED1 alternating ~2 Hz forever = RAM failure.

### Serial TX cycle counting (`fw/serial.S`)
Unrolled 10 bits (macro), frame in d0 with the current bit at bit position 2 (= GPIO.SERTX):
`d0 = (data << 3) | 0x800` (start 0, 8 data, stop 1). d7 = shadow with SERTX cleared, a0 = GPIO.
```
move.w %d0,%d2      4       and.w #4,%d2   8       or.w %d7,%d2  4
move.w %d2,(%a0)    8 (4-clock I/O write; +2 if slot-misaligned)   <- bit edge
lsr.w  #1,%d0       8       moveq #5,%d1   4
1: dbf %d1,1b       5x10 + 14 = 64         nop 4           total = 104 clocks
```
104 is a multiple of the 4-clock slot, so the slot-alignment penalty (+2) can hit at most one
bit per byte (phase self-corrects); the average lands between 104.0 and 104.2 vs the ideal
104.17. Interrupts masked for the byte (`move.w %sr,-(%sp); ori.w #0x0700,%sr ... move.w (%sp)+,%sr`).
Same macro used by the POST routine. Hardware TX detection: write SERDATA (0x0D), read
INTSTAT.SERTXBUSY; if set, use `while(SERTXBUSY); SERDATA=c` from then on.
Keep a `gpio_shadow` word in .bss; every GPIO write writes the whole word.

### Console / input / interrupts
* 40x30 text, 8x8 font (vendor `font8x8_basic.h` from github.com/dhepper/font8x8 -- was about
  to WebFetch it; not yet in the tree). Glyph rendering via a 16-entry nibble->4-pixel-long
  table rebuilt when fg/bg change (2 long writes per glyph row). Scroll = asm `memmove`
  (movem) of 232 lines + clear last 8 lines (~30 ms). Console code renders into a `con_fb`
  pointer so host tests can render into a malloc'd buffer. '\b' is non-destructive; the line
  editor emits "\b \b". '\n' on serial becomes "\r\n".
* Input: two SPSC rings (serial RX 512 B, keyboard ASCII 64 B) + raw key event ring (uint16);
  `pollchar` checks keyboard then serial; `serial_getc` reads only the serial ring.
* PS/2 set 2 decoder (pure C `ps2.c`, `ps2_feed(byte)`): F0 release, E0 extended, E1 ignored;
  shift/ctrl/caps; arrows -> ESC[A/B/C/D, Home ESC[H, End ESC[F, Ins/Del/PgUp/PgDn ESC[2~/3~/5~/6~,
  F1-F4 ESC O P/Q/R/S, F5-F12 ESC[15~ 17~ 18~ 19~ 20~ 21~ 23~ 24~; raw events bit8 release, bit9 E0.
* ISRs as `__attribute__((interrupt_handler))` C functions: VBLANK (W1C bit0, ticks++, user hook),
  PS2 (read PS2DATA; if INTSTAT.PS2ERR, W1C and drop), SERRX (if SERRXOVR W1C+count; read SERDATA),
  HSYNC (W1C; audio cb) enabled only while an audio cb / level-4 hook is installed.
  INTEN default = VBLANK|PS2|SERRX. `set_irq_handler` accepts levels 1-4.
* Serial TX masks IRQs ~87 us per byte: do NOT echo while receiving S-records (RX holding
  register is 1 byte deep). Line editor holds the echo of a leading 'S' until the next char;
  'S'+hex digit enters srec mode (no echo) until S7/S8/S9 or a non-S line.

### Syscalls / user programs
* `trap15_handler: movem.l %d0-%d3/%a0-%a1,-(%sp); pea (%sp); jsr syscall_dispatch; addq.l #4,%sp;
  movem.l (%sp)+,...; rte`. `struct trap_frame { uint32_t d0,d1,d2,d3,a0,a1; }` (movem order),
  dispatcher includes `generated/syscall_dispatch.inc` and stores `ret` into `f->d0`.
* **User programs run in user mode**: `go addr` sets USP = USER_STACK_TOP, pushes the address of
  a ROM stub (`moveq #0,%d1; moveq #0,%d0; trap #15`) so a stray `rts` exits cleanly, then
  `rte`s into user mode. Consequence: syscalls/ISRs run on the supervisor stack at
  _sys_stack_top; user code cannot touch SR (use sys_set_irq_handler). `bios_exit` prints the
  code, resets INTEN/hooks/page 0 and `lea _sys_stack_top,%sp; jmp monitor_main`.
* `bios_mem_info`: USER_BASE, USER_END, FB0_BASE. Stage-2 calls return E_NOSYS.
* SDK heap: `_end` .. `mem_info.user_end - 16 KB` stack reserve. `read(0)` is a tiny line editor.

### Monitor command list (planned)
`help, md [addr] [len] (8 bytes/line, fits 40 cols), mb/mw/ml addr val..., fill addr len val,
go addr, regs, reset, led n, page n, ticks, ramtest (0x4000-0x3FFFF, destructive), beep, cls, info,
srec, dir/run/cat/sd -> "not implemented (stage 2)"`. All numbers hex.

## Exact next steps, in order
1. `fw/crt0.S` (vectors, `_start`, `exception_common`, `trap15_handler`, `monitor_restart`, go stub).
2. `fw/serial.S` (macro + `post_putc` + C-callable `serial_tx_byte`), `fw/post.S`.
3. Pure-C modules with host tests first: `printf.c`, `srec.c`, `hexdump.c`, `queue.c`, `ps2.c`,
   `console.c` + `font8x8_basic.h`; `fw/test/test_main.c`; get `make -C fw test` green.
4. `string.c`/`memops.S`, `serial.c`, `input.c`, `irq.c`, `exception.c`, `syscall.c`, `monitor.c`,
   `main.c`; build, inspect `rom.lst` for the serial loop and the reset vector; check rom.bin size.
5. `sdk/crt0.S`, `sdk/newlib_shim.c` (libgloss names!), `examples/hello.c`, `examples/plasma.c`
   (4x4 blocks, double-buffered); `fw/README.md`, `sdk/README.md`.

## Spec / architecture notes to raise
* architecture.md alignment table says S0-at-E6 cycles take 4 clocks, but with DTACK asserted
  at E4 and sampled at the S4->S5 falling edge (E3, then E5) that cycle would be 5 clocks; this
  also conflicts with "the cycle ends at E0 regardless of where S0 was". Only affects jitter
  bookkeeping for the bit-banged serial (bounded either way).
* The spec's SDK text names `_write` etc.; this newlib build calls `write` etc. (libgloss style).
