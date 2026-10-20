# fw/ + sdk/ status (stage 1)

**State: stage 1 implemented and running in the emulator.** `make -C fw` builds a 131072-byte
`rom.bin` (about 19 KB used) with no warnings; `make -C fw test` passes 97 host checks;
`make -C sdk` builds `libsys.a`, `hello.elf/.srec` and `plasma.elf/.srec`. In `emu/build/m68k-emu`
the ROM passes POST, prints the banner, and the monitor, exceptions (bus error with fault
address, illegal instruction in user mode), S-record upload (keyboard-typed and serial),
`go` into user mode, `sys_exit`, PAL selection and hardware-TX detection all work.
Nothing has run on real hardware yet. See README.md for usage.

## What exists

| file | status |
|---|---|
| `crt0.S` | vectors (256 entries, `.rept`-generated 10-byte stubs, asserted size), `_start`, `post_return` (vector copy, .data/.bss), `exception_common` (group-0 decode), `trap15_handler`, `run_user`, `user_exit_stub`, `monitor_restart`, `cpu_reset`, `irq_disable/restore` |
| `post.S` | ROM word sum, RAM test (address, 55/AA/00/FF longs, byte lanes both ways), failure report + blink; registers only |
| `sertx.S`, `serbit.inc` | SERBIT macro with per-instruction clock counts (104/bit), `post_putc/puts/puthex`, `serial_tx_byte`, `delay_loops` |
| `memops.S`, `string.c` | memset/memcpy/memmove (long moves when alignment allows), str*/memcmp |
| `main.c` | `bios_main`, banner, `putchar_both` (console + serial, `\n` -> CRLF) |
| `console.c`, `font8x8_basic.h` | 40x30 console, nibble table renderer, scroll, cursor, shadow text; renders through `con_fb` (host-testable) |
| `serial.c` | GPIO shadow + atomic set/clear, hw TX probe, TX path, `delay_us` |
| `input.c`, `queue.c` | serial 512 B, keyboard 256 B, key events 64 x 16-bit rings; merged `input_pollchar` |
| `ps2.c` | set-2 decoder: F0/E0/E1, shift/ctrl/caps, arrows/Home/End/Ins/Del/PgUp/PgDn/F1-F12 escape sequences, raw events |
| `irq.c` | ISRs (`interrupt_handler` attribute, verified `rte` in rom.lst), user hooks, INTEN management |
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
* Asm `.S` files cannot use `|` for OR (it is the m68k gas comment character); `+` is used.
  `hw.inc` has no `IO_GPIO_RESET` value, so the idle GPIO value is built from the bit masks.

## Verified in the emulator (serial logs in the final report)

* POST: `ROM sum 0000 OK`, `RAM ....... OK`; banner; `help`, `info`, `ticks`, `md`, `mb/mw/ml`,
  `fill`, `led`, `page`, `dir` (stage 2 message), unknown command.
* `ml 400000 0` -> `Exception 2: Bus error ... fault 00400000 write FC=5`, monitor continues;
  `regs` repeats it.
* `go 4000` on `nop nop illegal` -> `Exception 4` with SR=0000 (user mode), USP=0001a7fc.
* `go 4000` on `trap #15` (exit) -> `[exit 0]`.
* Typed S-records (auto-detect, no echo) and `srec` command; hello.srec (2198 records) over
  the serial port with no bad lines, `go` runs newlib `printf`.
* `--pal` -> banner `PAL`, 50 ticks/s; `--hw-serial-tx` -> `serial TX hw`.

## Known limitations / stage 2

* No SD/FAT32/ELF loader (`dir run cat sd`, `sys_open..sys_closedir`, `sys_sd_*`, `sys_exec`).
* No PS/2 host-to-device commands (LEDs, typematic); the keyboard's power-on defaults are used.
* Bit-banged TX masks interrupts for ~87 us per byte: a host that sends while the BIOS prints
  can lose a byte (counted in `info` as rx overruns). Audio callbacks jitter during printing.
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
* `spec/board.json` `video.line_origin` says the VBLANK flag sets at "line v_active + v_bottom"
  while the INTSTAT text says "leading edge of vertical sync"; they agree numerically but the
  emulator STATUS flagged the wording.
