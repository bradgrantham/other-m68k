# emu/ status (session interrupted before the emulator sources were written)

## Done
* `emu/musashi/`: Musashi (upstream HEAD, readme says 4.10, sources say 3.32) cloned, `.git` removed.
  Vendored files are UNMODIFIED so far (see "Required Musashi patch" below).
* `emu/m68kconf.h`: 68000-only configuration. INT_ACK on (callback via
  `m68k_set_int_ack_callback`, return `M68K_INT_ACK_AUTOVECTOR`), RESET callback on
  (do nothing), ADDRESS_ERROR on, INSTRUCTION_HOOK on (install callback only for
  --trace/--break), MONITOR_PC off, PMMU off, `M68K_EMULATE_FC = SPECIFY_HANDLER` with
  `#define M68K_SET_FC_CALLBACK(A) (emu_fc = (A))` -- a zero-cost way to know the
  function code of the current access.  **bus.c must define `unsigned int emu_fc;`.**
* `emu/Makefile`: builds `build/m68kmake` with the host cc, runs
  `build/m68kmake build musashi/m68k_in.c` (writes build/m68kops.c/.h), compiles
  m68kcpu.c, m68kops.c, m68kdasm.c, softfloat/softfloat.c with
  `-DMUSASHI_CNF='"<abs>/emu/m68kconf.h"'` (so the vendored musashi/m68kconf.h is
  bypassed), then links `build/m68k-emu` from `$(wildcard *.c)` + core + `sdl2-config
  --libs -lm`.  Own sources get `-Wall -Wextra -std=gnu11 -O2 -I../generated`.
  `make -C emu` currently builds the core and then fails with
  "emulator sources (emu/main.c etc.) are not written yet" (intended).  `make -C emu
  clean` works.  `make -C emu test` expects `emu/test/Makefile` (not written).

## Not done (nothing of the emulator proper exists yet)
main.c, bus.c/h, io.c/h, video.c/h, serial.c/h, ps2.c/h, sdcard.c/h, audio.c/h,
README.md, test/ (test ROM + Makefile).  Nothing has been run.

## Findings about Musashi that drive the design (verified by reading the source)
1. `m68k_pulse_bus_error()` -> `m68ki_exception_bus_error()` builds the stack frame
   and `longjmp`s to `m68ki_bus_error_jmp_buf`, which is `setjmp`'d inside
   `m68k_execute()`.  So it must ONLY be called from memory handlers invoked inside
   `m68k_execute()`; never from `m68k_pulse_reset()` (reads vectors at 0/4 -> overlay
   ROM, fine) or from the disassembler read functions (`m68k_read_disassembler_*`
   must be side-effect-free peeks that never BERR).  Code after the call never runs.
2. Double fault: if a BERR happens while the frame is being written, Musashi calls
   `m68k_read_memory_8(0x00ffff01)` and HALTs.  That address is in the IACK range
   0xFFFFF0..0xFFFFFF which must never BERR; detect this read to log
   "CPU halted (double bus fault)".
3. **Required Musashi patch**: stock `m68ki_exception_bus_error()` (m68kcpu.h ~line
   1963) pushes the 68010 format-8 29-word frame with fault address 0, even for a
   68000.  The firmware BERR handler prints the faulting address, so patch it to
   `if(CPU_TYPE_IS_000(CPU_TYPE)) m68ki_stack_frame_buserr(sr); else
   m68ki_stack_frame_1000(REG_PPC, sr, EXCEPTION_BUS_ERROR);`.
   `m68ki_stack_frame_buserr` pushes PC, SR, IR, `m68ki_aerr_address`,
   `m68ki_aerr_write_mode | CPU_INSTR_MODE | m68ki_aerr_fc`.  Before calling
   `m68k_pulse_bus_error()` bus.c sets (extern unsigned int) `m68ki_aerr_address =
   addr; m68ki_aerr_write_mode = write ? 0 : 0x10 /*MODE_READ*/; m68ki_aerr_fc =
   emu_fc & 7;`.  Keep the diff in emu/musashi-berr-frame.patch and mention it in
   README.
4. Interrupts: `m68ki_check_interrupts()` runs only at `m68k_execute()` entry and on
   SR writes (no per-instruction check).  So when a memory handler changes IPL
   (INTEN write, W1C, PS2DATA/SERDATA read) call `m68k_set_irq(level)` then
   `m68k_end_timeslice()`, and run each scanline as
   `run=0; while(run<target) run += m68k_execute(target-run);` carrying the overshoot
   (`run-target`) into the next line's target so long-term timing stays exact.
   `m68k_set_irq(7)` from <7 sets `nmi_pending` (edge); level 7 does not re-trigger
   while held (0x700 > 0x700 is false).  With INT_ACK on, Musashi never auto-clears
   CPU_INT_LEVEL: the glue must recompute IPL after every flag/enable change.
5. Cycle timestamp inside a handler: `line_base + m68k_cycles_run()` (consistent even
   after `m68k_end_timeslice()`); it is the instruction start time (+-10 cycles).
6. STOP/HALT: `m68k_execute()` consumes the whole slice when stopped -- no spin.
7. `m68kmake <outdir> <m68k_in.c>` is the generator CLI (done in Makefile).

## Design decisions already taken (follow these when writing the code)
* Scanline slice starts at the HSYNC leading edge (where the V counter advances).
  Per slice v: set INTSTAT.HSYNC; live pins computed lazily on INTSTAT read from
  pos = cycles into the slice: HSYNCPIN = pos < h_sync; DEPIN = (v < 240) &&
  pos in [h_sync+h_back, h_sync+h_back+h_active); VSYNCPIN = v in
  [v_active+v_bottom, +v_sync).  VBLANK flag set at the start of slice
  v_active+v_bottom (243 NTSC / 270 PAL) = leading edge of VSYNC (follows the
  INTSTAT register text and the task; NOTE spec `video.line_origin` says "start of
  line v_active" -- inconsistent, report as a spec fix).
* PAGE and PAL soft switches are latched at the start of slice 0 (next frame).
  Render the framebuffer at the end of the last active slice (239) from the latched
  page; present/pace at frame end.  `--pal` sets INTSTAT.PALJMP only; timing still
  starts NTSC (spec: NTSC default at reset) and firmware selects PAL via SW_PAL.
* IO byte access: byte read = corresponding half of the word; byte write =
  lane-masked write of that half (undefined on hardware: CPLD2 has no UDS/LDS).
  SWITCHES reads return 0xFFFF.  ROM writes ignored (DTACK, no BERR).  Overlay
  cleared at the END of any access in the ROM region.  32-bit accesses = two word
  accesses (high first) through the same path.
* IPL = max(VBLANK&en->1, PS2RDY&en->2, SERRX&en->3, HSYNC&en->4, NMI held->7).
* Serial TX decoder: record SERTX edges (cycle, level); frame starts at a falling
  edge t0 while hunting; when now >= t0 + 9.5 bit (bit = 12e6/115200 = 104.17 cy,
  sample i at t0 + (2i+3)*12000000/(2*115200)) sample 8 bits + stop, emit, then hunt
  from the first falling edge >= t0+9.5 bit.  Run the decoder at each edge, at each
  line end, and flush at exit (assume line idle high).  stdout unbuffered + --serial-log.
* Serial RX: stdin raw (keep ISIG so Ctrl-C quits; restore tty at exit), non-blocking;
  one byte per 1042 cycles into SERDATA; SERRXOVR if SERRX still set (overwrite).
* PS/2: SDL scancode -> set-2 make/break (E0 prefix, F0 break); queue; deliver one
  byte per 12000 cycles only when PS2RDY is clear.  `--type` after `--type-delay`
  frames via shift+key sequences; `\n` and `\\` escapes.  NMI = Pause or F12 (live
  NMIBTN bit while held).  Optional extra: emulate host->device bit-bang
  (request-to-send on PS2CLK/PS2DAT) so a firmware keyboard init cannot hang.
* SD (SPI mode 0 slave on SDIO writes): on CLK rising with CS low shift MOSI in
  (MSB first); on CLK falling present MISO = bit (7-bitcount) of out byte; after 8
  bits feed the card FSM; CS high or no `--sd` => MISO 1.  Command FSM: 6-byte
  frames (0x40|cmd, arg, crc), one 0xFF Ncr then R1/R2/R3/R7; CMD0,1,8,9(CSD v2,
  C_SIZE = ceil(blocks/1024)-1),10(CID),12,13,16,17,18,24,25,55,ACMD41(0x01 first
  poll then 0x00),58(OCR 0xC0FF8000),59; unknown -> 0x04; data token 0xFE, real
  CRC16-CCITT; write: 0x05 then busy 0x00 x3 then 0xFF; `--sd-trace`.  Image file
  r+b, size multiple of 512, block addressed, pread/pwrite.
* Audio: AUDIO edges with cycle stamps; per frame box-filter to 48 kHz int16 mono,
  SDL_QueueAudio; `--no-audio`, off in --headless/--fast.
* BERR log rate limit: <=10 lines per emulated second, then a suppressed count.
  `--exit-on-berr` exit 2.  HALT in headless => exit 3.  All diagnostics/dumps to
  stderr; stdout carries serial only.  F11 = reset button (CPU + glue: overlay set).
* Flags: --rom (req) --sd --scale(3) --pal --fast --headless --frames --trace
  --trace-from --break --serial-log --hw-serial-tx --type --type-delay(120)
  --sd-trace --exit-on-berr --dump-regs-on-exit --dump-mem ADDR LEN --no-audio.

## Exact next steps
1. Apply the Musashi patch (finding 3); save diff as emu/musashi-berr-frame.patch.
2. Write bus.c/h (RAM/ROM/overlay/decode/BERR/peek + m68k_read/write_memory_*,
   disassembler reads, `emu_fc`), io.c/h (registers, IPL, soft switches, live pins),
   main.c (args, reset, per-line loop with overshoot carry, SDL window/texture,
   pacing, trace/break hooks, dumps), then serial.c, ps2.c, sdcard.c, video.c,
   audio.c.  Keep stdout for serial only.
3. Write emu/test/Makefile + test.S (vectors SSP=SYS_STACK_TOP, PC=ROM_BASE+0x400;
   RAM pattern; LEDs; gradient to FB_PAGE0; cycle-counted "HELLO\n" bit-bang at
   104 cycles/bit on GPIO.SERTX; INTEN.VBLANK + autovector 25 handler counting into
   a RAM word; read 0x400000 -> BERR handler sets a RAM flag and RTEs past (adjust
   SP by 8 for the 68000 group-0 frame); a few SD commands; STOP #$2700).
   Build with ~/x-tools/m68k-unknown-elf/bin/m68k-unknown-elf-{gcc,ld,objcopy}
   -m68000 -nostdlib; `make -C emu test` runs
   `build/m68k-emu --rom test/build/test.bin --headless --fast --frames 120
   --serial-log out.txt --dump-mem <counter addr> 16` and greps HELLO/BERR/counter.
4. README.md: usage, timing model, byte-access note, Musashi patch, stdout/stderr
   convention.
