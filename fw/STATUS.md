# fw/ + sdk/ status (stage 1, spec revision 2)

**State: stage 1 ported to spec revision 2 (68681 DUART, write-only CPLD2 registers);
builds and passes host tests, not yet re-run in an emulator that models the DUART.**
`make -C fw` builds a 131072-byte `rom.bin` (about 18.7 KB used) with no warnings;
`make -C fw test` passes 97 host checks; `make -C sdk` builds `libsys.a`, `hello.elf/.srec`
and `plasma.elf/.srec`. The revision-1 ROM was verified in `emu/build/m68k-emu` (POST,
banner, monitor, exceptions, S-record upload typed and serial, `go`, `sys_exit`, PAL); the
emulator's DUART model was being written concurrently with this port, so the revision-2
ROM has only been checked by inspection of `rom.lst` (byte accesses at the odd DUART
addresses, init order, `rte` in `irq_duart`, vector 27 -> `irq_duart`). Re-run the
emulator check below once `emu/README.md` mentions the DUART. Nothing has run on real
hardware yet. See README.md for usage.

## Revision 2 port (what changed)

* Serial is MC68681 channel A (`DUART_*` in `generated/hw.h`/`hw.inc`, byte accesses only).
  `sertx.S`/`serbit.inc` (cycle-counted bit-bang TX) are gone; `duart.S` holds
  `duart_reset_init` (registers only, called from `_start` before the POST), the stack-less
  `post_putc/puts/puthex` (poll `SRA.TXRDY`, write `TBA`) and `delay_loops`. `duart.h` has
  the MR1/MR2/CR/ACR values with data-sheet section references (shared by C and `.S`).
* Init order (data sheet 4.2): IMR=0; CRA = 0x10 reset MR pointer, 0x20 reset RX, 0x30 reset
  TX, 0x40 reset error status; ACR = `SERIAL_ACR_BRG_SET << 7` (0x80, only legal after the
  RX/TX resets); MR1A = 0x13; MR2A = 0x07; CSRA = `SERIAL_CSR_VALUE` (0xCC); CRA = 0x05.
* `serial_init` (C) drains anything received during the POST, resets error status and sets
  `IMR = RXRDYA`; `irq_init` enables `INTEN.DUART`. `irq_duart` (vector 27) reads `ISR`,
  then pops `RBA` while `SRA.RXRDY`, counting `SRA.OE` and clearing it with CRA = 0x40.
  `serial_putc` polls `TXRDY`; no interrupt masking anywhere on the TX path.
* GPIO bit map from `hw.h` (PS2CLK 0, PS2DAT 1, AUDIO 2, LED0 3, LED1 4); the idle value is
  `GPIO_PS2CLK_MASK + GPIO_PS2DAT_MASK` (= `IO_GPIO_RESET`). `INTSTAT_SERRXOVR`/`SERTXBUSY`,
  `IO_SERDATA`, `INTEN_SERRX` no longer exist; `irq_init` clears only VBLANK/HSYNC/PS2ERR.
* Banner line: `ID 6802  NTSC  DUART 38400 8N1`; `info` adds `DUART SRA xx`. `bios_main`
  zeroes `inten_shadow` with INTEN.
* The S-record receiver still does not echo: at 38400 a record arrives every ~11 ms and
  echoing one (console glyphs, 35 ms scroll, serial copy) takes longer, so the 512-byte
  ring would overflow. The ISR keeps draining the DUART's 3-byte FIFO meanwhile.
* `sdk/README.md`: upload at 38400 8N1. No SDK code touched the serial registers.

## What exists

| file | status |
|---|---|
| `crt0.S` | vectors (256 entries, `.rept`-generated 10-byte stubs, asserted size), `_start`, `post_return` (vector copy, .data/.bss), `exception_common` (group-0 decode), `trap15_handler`, `run_user`, `user_exit_stub`, `monitor_restart`, `cpu_reset`, `irq_disable/restore` |
| `post.S` | ROM word sum, RAM test (address, 55/AA/00/FF longs, byte lanes both ways), failure report + blink; registers only |
| `duart.S`, `duart.h` | `duart_reset_init` (stack-less 68681 channel A set-up), `post_putc/puts/puthex` (poll TXRDY), `delay_loops`; MR/CR/ACR values |
| `memops.S`, `string.c` | memset/memcpy/memmove (long moves when alignment allows), str*/memcmp |
| `main.c` | `bios_main`, banner, `putchar_both` (console + serial, `\n` -> CRLF) |
| `console.c`, `font8x8_basic.h` | 40x30 console, nibble table renderer, scroll, cursor, shadow text; renders through `con_fb` (host-testable) |
| `serial.c` | GPIO shadow + atomic set/clear, DUART RX interrupt enable, polled TX, `delay_us` |
| `input.c`, `queue.c` | serial 512 B, keyboard 256 B, key events 64 x 16-bit rings; merged `input_pollchar` |
| `ps2.c` | set-2 decoder: F0/E0/E1, shift/ctrl/caps, arrows/Home/End/Ins/Del/PgUp/PgDn/F1-F12 escape sequences, raw events |
| `irq.c` | ISRs (`interrupt_handler` attribute, verified `rte` in rom.lst), DUART RX FIFO drain, user hooks, INTEN shadow |
| `exception.c` | names, register dump, `exception_entry` |
| `syscall.c` | `syscall_dispatch` with the generated `.inc`, all 30 `bios_*` (fs/SD/exec -> E_NOSYS), `user_cleanup` |
| `monitor.c`, `srec.c`, `hexdump.c`, `printf.c` | monitor, S-record parser, dump line, kprintf/ksnprintf |
| `test/test_main.c` | host tests: printf, queue, srec (good/bad checksum, S1/S2/S3/S5/S7/S8/S9, CRLF, junk), ps2 (shift/caps/ctrl/arrows/F-keys/pause/ack), console (glyphs, tab, backspace, wrap, scroll, cursor, colours), hexdump |
| `sdk/crt0.S`, `sdk/newlib_shim.c`, `sdk/examples/{hello,plasma}.c`, `sdk/README.md` | as planned; link uses `--start-group -lc -lsys --end-group -lgcc` |

## Design as built (differences from the original plan)

* The byte-lane RAM pass writes 8 bytes per loop iteration (the first cut filled only half
  of RAM: caught by the emulator run).
* Fills/verifies use plain `move.l`/`cmp.l` unrolled x4 instead of `movem`: on this bus every
  word is one slot-aligned bus cycle either way, so `movem` buys almost nothing. POST takes
  about 1.7 s at 12 MHz. The same applies to the console scroll (~35 ms).
* `md` dumps are only key-abortable when longer than 256 bytes; a short `md` must not consume
  the first character of the next typed line (the emulator types ahead).
* Keyboard ASCII ring is 256 B (64 B overflowed when the emulator typed through a long,
  scrolling exception dump).
* Hex dump line has no `|` bars so it fits 40 columns: `004000 aa 55 .. .U..`.
* `bios_read(0)`/`bios_write(1,2)` work on the console so the newlib shim does one trap per
  `write()`; other fds return `E_NOSYS` (stage 2).
* `delay_loops` is an 18-clock `subq/bhi` loop; `delay_us(us)` = `us*2/3` loops.
* `fw/duart.h` carries the 68681 mode/command bytes (MR1A=0x13, MR2A=0x07, CR commands)
  because `gen.py` emits only `SERIAL_CSR_VALUE` and `SERIAL_ACR_BRG_SET`; see the spec notes.
* Asm `.S` files cannot use `|` for OR (it is the m68k gas comment character); `+` is used.
  `hw.inc` has no `IO_GPIO_RESET` value, so the idle GPIO value is built from the bit masks.

## Verified in the emulator (revision-1 ROM; to be repeated with the DUART emulator)

Command to repeat once `emu/README.md` mentions the DUART:
`./emu/build/m68k-emu --rom fw/build/rom.bin --headless --frames 400 --serial-log LOG
--type "help\n" --no-audio --no-stdin`, then check the POST lines, the banner
(`DUART 38400 8N1`), `help` and `info`.

* POST: `ROM sum 0000 OK`, `RAM ....... OK`; banner; `help`, `info`, `ticks`, `md`, `mb/mw/ml`,
  `fill`, `led`, `page`, `dir` (stage 2 message), unknown command.
* `ml 400000 0` -> `Exception 2: Bus error ... fault 00400000 write FC=5`, monitor continues;
  `regs` repeats it.
* `go 4000` on `nop nop illegal` -> `Exception 4` with SR=0000 (user mode), USP=0001a7fc.
* `go 4000` on `trap #15` (exit) -> `[exit 0]`.
* Typed S-records (auto-detect, no echo) and `srec` command; hello.srec (2198 records) over
  the serial port with no bad lines, `go` runs newlib `printf`.
* `--pal` -> banner `PAL`, 50 ticks/s (the revision-1 `--hw-serial-tx` option is obsolete).

## Known limitations / stage 2

* No SD/FAT32/ELF loader (`dir run cat sd`, `sys_open..sys_closedir`, `sys_sd_*`, `sys_exec`).
* No PS/2 host-to-device commands (LEDs, typematic); the keyboard's power-on defaults are used.
* Serial TX is polled (one `TXRDY` wait per byte, ~260 us at 38400); a long `md` holds the
  monitor but no interrupts are masked. The DUART's TXRDY interrupt is not used.
* SDIO and USERIO.OUT are write-only; no shadows exist yet because stage 1 does not touch
  them (add `sdio_shadow`/`userio_shadow` with the SD driver).
* ESC sequences from the keyboard are swallowed by the line editor (no history/cursor editing).
* NMI path is exercised only through the generic stub mechanism (the emulator needs a window
  for F12); the same path handled bus error / illegal instruction.

## Spec / architecture notes to raise

* architecture.md alignment table: an S0-at-E6 cycle with DTACK asserted at E4 and sampled at
  E5 is 5 clocks, conflicting with "the cycle ends at E0 regardless of where S0 was" (which
  implies 6). Only affects jitter bookkeeping; the serial loop is insensitive to it.
* The spec's SDK text names `_write` etc.; this newlib calls `write` etc. (libgloss style).
* `gen.py` could emit register reset values (`IO_GPIO_RESET`...) into `hw.inc` as it does in
  `hw.h`; the BIOS currently composes the idle GPIO value from the bit masks.
* `serial.init` in the spec is prose ("MR1A=0x13 ... CRA=0x05"); `gen.py` emits only
  `SERIAL_CSR_VALUE` and `SERIAL_ACR_BRG_SET`. Emitting `SERIAL_MR1_VALUE`, `SERIAL_MR2_VALUE`,
  `SERIAL_ACR_VALUE` and the CR command codes (and `CSRA`/`CRA` bit fields) would let
  `fw/duart.h` go away and keep the emulator and firmware on one definition.
* `hw.h` names the odd-address registers `DUART_SRA`/`DUART_CSRA` etc. for the same offset;
  `rom.lst` therefore labels reads of SRA as `<DUART_CSRA_ADDR>` and writes to TBA as
  `<DUART_RBA_ADDR>` (cosmetic, from the duplicated `.equ`s in `hw.inc`).
* `spec/board.json` `video.line_origin` says the VBLANK flag sets at "line v_active + v_bottom"
  while the INTSTAT text says "leading edge of vertical sync"; they agree numerically but the
  emulator STATUS flagged the wording.
