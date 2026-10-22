# fw/ + sdk/ status (stage 2, spec revision 9)

**State: stage 2 implemented and verified in the DUART emulator with an SD image: SD card
driver, FatFs R0.15 on two drives (C: in the flash, D: the card; long names, directories),
file/directory/clock system calls, ELF loader with argv, `sys_exec`, monitor file commands,
scripts and search path. Not yet run on real hardware.**
`make -C fw` builds a 262144-byte `rom.bin` with 59050 bytes of text + 918 data + 17388 bss
(about 59 KB of the 256 KB used; FatFs with the LFN tables is ~16 KB of that) and no
warnings; the top-level `make flash` adds `c.img`, `flash.img` and the chip images;
`make -C fw test` passes 590 host checks (the FAT tests need `mkfs.vfat` and `mcopy`,
present here); `make -C sdk` builds `libsys.a` and the examples' `.elf/.srec`. `.data+.bss`
is 0x4780 bytes (ends at 0x4B80), leaving ~13 KB of supervisor stack below 0x8000. See
README.md for usage, the exact emulator commands and their output.

## Spec rev 11 / board rev C3: Atari joystick and paddle port (2026-10-22)

* `joy.c`: `sys_joystick` (40) reads the five switches from the DUART input port (IP0..IP4,
  active low behind the existing 10 k pull-ups); `sys_paddles` (41) returns `struct paddles`
  {a, b 0..1023, valid bits, frame}. Paddle timing: the VBLANK handler shorts the two 15 nF
  capacitors through OP0; the lines that had charged rise, the DUART reports the rise through
  its change interrupt once its 38.4 kHz sampler has seen it, and that level-3 handler releases
  the capacitors and notes the free-running counter/timer (timer mode, X1/16 = 230.4 kHz); the
  74HC14 gates pull IP0/IP1 low at end of charge and the handler timestamps the fall (ACR 0x73,
  IMR.IPC). If nothing rose during a frame the next VBLANK releases directly. 1023 = 12 ms =
  2764 counts. Measuring starts with the first `sys_paddles` call and stops with
  `sys_paddles(NULL)` or program exit. The first version waited 20 us in the VBLANK handler and
  masked IMR around the discharge; the guide review showed the wait could leave a paddle near
  zero undetected (the DUART needs 26..52 us to see a step) and the IMR writes trampled a
  program's own interrupt bits.
* Shared lines: paddle A ends on the up line, paddle B on the down line (a joystick has no
  pots, paddles have no up/down switches); in paddle mode a joystick held up/down keeps its
  line low and reads as no paddle (valid clear) except in the frame the switch closes. A program that uses the DUART counter/timer itself must not use paddles.
* Emulator: `--joy up,fire`, `--paddle A,B|pad`, or an SDL game controller; the DUART model
  gained the input-change detector (IPCR delta bits, ISR.IPC) and OP0.
* Host tests: `joy_scale` (649 checks in all). Verified in the emulator with `joytest`.

## Spec rev 9: drive C: in the flash, long names, directories, clock, scripts (2026-10-21)

* Flash split: the ROM image is 256 KB (`ROM_IMAGE_SIZE` 0x40000; the POST checksum loop
  counts `ROM_IMAGE_SIZE/2` words with a long counter); the other 768 KB of the window, from
  `FLASH_VOL_OFFSET` 0x40000, are a FAT12 volume mounted as C:. `flash` rewrites only the
  first 256 KB, so C: survives a BIOS update. `make -C fw` now makes only `rom.bin`; the
  top-level `make flash` (in `make all`) makes `fw/build/c.img` (`tools/mkflashvol.sh`:
  `flashfs/AUTOEXEC.CMD`, `flashfs/README.TXT` and the stripped SDK examples),
  `fw/build/flash.img` (`mkrom.py --split`: rom.bin + c.img at 256 KB) and
  `rom_even.bin`/`rom_odd.bin` (its byte lanes). `make run` boots `flash.img` with
  `--rom-save` so C: persists across emulator runs.
* RAM: SYSDATA grew from 15 KB to 31 KB (0x400-0x7FFF) for the 8 KB erase-sector cache, the
  second FATFS object, the flash routine (now in `.data`, copied by crt0; nothing is copied
  into user RAM any more) and the script stack. `USER_BASE` is 0x8000; the user region is
  0x8000-0xDA7FF (842 KB), to 0xED3FF without framebuffer page 1; programs link at 0x8000.
* `flashdisk.c` (FatFs drive 0): reads straight from the window, writes through one 8 KB
  erase-sector cache (unchanged sectors do not dirty it; dirty reads come from it), flushed
  when it must move, on `CTRL_SYNC`, before every monitor prompt and before a program
  starts. A flush is `flash_write_sector` -> `flashram.S` op 2: erase + program + verify
  with interrupts masked, ~100 ms; the routine counts and clears the VBLANK flags it sees
  (`op->vblanks`) and `flash.c` adds them to `ticks` and the clock. Serial RX bytes beyond
  the 3-byte FIFO are lost meanwhile; audio repeats its pattern. `flashdisk_programs` counts
  cycles (`info`). Emulator: 200 KB copied from D: to a freshly formatted C: = 32 cycles,
  format included; `copy D:*.ELF C:/` of five programs (310 KB) = 44.
* FatFs: `FF_USE_LFN 1` (`ffunicode.c`, CP437), `FF_MAX_LFN 63`, `FF_FS_RPATH 2`,
  `FF_VOLUMES 2` with `"C","D"` volume strings, `FF_USE_CHMOD 1` (`f_utime` keeps stamps on
  copy), `FF_USE_MKFS 1` (`format C:`), `FF_FS_NORTC 0` (`get_fattime` from the clock),
  still `FF_FS_TINY 1`. `fs.c`: both volumes registered in `fs_init` (lazy mount);
  `fs_invalidate` drops only D:'s files/directories; new `fs_sync`, `fs_stat`, `fs_unlink`,
  `fs_rename`, `fs_mkdir`, `fs_chdir`, `fs_getcwd`, `fs_copy` (keeps the stamp, deletes a
  partial destination), `fs_mkfs` (C: only); `fs_close_all` also flushes the cache.
  `struct dirent` gained `mtime`; `name[64]` holds the long name; `E_NAMETOOLONG` now means
  over 63 characters or malformed. Syscalls 33-39: `unlink`, `rename`, `mkdir`, `chdir`,
  `getcwd(len, buf)`, `time`, `set_time`; `exec` takes an explicit path (no search path).
* `clock.c`: seconds since 1970 local time, advanced in the VBLANK ISR by the exact frame
  length (200,168 / 239,616 clocks against 12,000,000), 0 until set; Hinnant's
  days-from-civil, FAT stamps, parse/format; files written before the clock is set get
  2026-01-01 00:00.
* Monitor: prompt is the current directory (`C:/> `, `X:?> ` if unmounted); `C:`/`D:`,
  `cd`, `dir [path|pattern]` (long names, sizes, stamps), `copy` (wildcards; dst file,
  directory or drive), `del`/`rm`, `ren`/`mv`, `mkdir`, `rmdir`, `date`, `format C:`,
  `echo`, `exit`; `info` shows cwd, clock and C: cycles. Unknown words go through the search
  path (NAME, NAME.ELF, NAME.CMD in the current directory, then C:/, then D:/); `.CMD`
  files are scripts (not echoed, `#`/`;`/`rem` comments, a program line resumes the script
  after the program exits, 4 levels, ESC between lines aborts all); `C:/AUTOEXEC.CMD` runs
  at the first prompt unless ESC is waiting in the keyboard queue. `cat`, `dir`, `copy`
  stop on ESC only and keep other type-ahead (`input_has_esc` / `input_flush`).
  `LINE_MAX` is 128.
* Host tests (590): `test_fs.c` runs C: from `fw/build/test/c.img` (made by
  `mkflashvol.sh`) in a RAM array with a simulated `flash_write_sector` (erase to 0xFF,
  programming clears bits, cycles counted) and D: from the FAT32 image: long names,
  directories, chdir/getcwd, rename/unlink, stamps from the clock, cache coalescing, filling
  the volume, `fs_copy` both ways, invalidation. `test_clock` covers the calendar.
  `TEST_SRCS` gained `clock.c`, `flashdisk.c`, `fatfs/ffunicode.c`.
* SDK: shim `unlink`, `rename`, `mkdir`, `rmdir`, `chdir`, `getcwd`; `gettimeofday` returns
  the BIOS clock once set (uptime before), `settimeofday` sets it; `environ` is newlib's.
  `ls` lists the current directory with long names, sizes and stamps.
* SD: no card-detect line, the card is fixed media (removal while powered unsupported);
  `sd` still re-initialises.
* Verified in the emulator: banner `ROM 256K ... C: 768K`, `dir` of C: (12 files, 475 KB),
  `D:` + `dir` + `run hello.elf` (loads 008000-010c33), `date`, `info`, `copy D:*.ELF C:/`,
  and `flash D:ROM.BIN` (32 dots, reboot, C: intact). A first rev 9 build had the `flash`
  image update hang: op 2's code, inserted ahead of the image-update block in `flashram.S`,
  captured the dispatch's local label `1f`; the branch targets are named labels now.

## Spec rev 7: native 4-bit SD bus (2026-10-21)

The SPI driver (`sdspi.S`, `sdio_shadow`, `INTSTAT.SDMISO`) is gone. `sd4.S` bit-bangs the
six open-drain card lines through SDIO (which reads back the levels): `sd4_cmd` (CRC7 frames,
Ncr wait, 48/136-bit responses), `sd4_clocks`, `sd4_read512` (86.5 clocks per byte, 3.7 ms
per block, hand count confirmed by the emulator's cycle stamps), `sd4_write512` (112 per
byte) and `sd_crc16x4` (per-line CRC16s, 6.9 ms). `sd.c` keeps the API (`sd_init`,
`sd_read_block`, `sd_write_block`, `sd_card`, `SD_ERR_*`, new `SD_ERR_CRC`, `sd_card.rca`,
`sd_card.bus4`) so diskio, the syscalls and the monitor barely changed; `sd` prints the bus
width and RCA. Init: CMD0, CMD8, CMD55/ACMD41 (voltage window + HCS), CMD2, CMD3, CMD9,
CMD7, CMD55/ACMD6 (4-bit), CMD16 for SDSC; `sd_slow` keeps identification under 400 kHz.

Measured in the emulator: 5.05 ms per 512-byte read (101 KB/s, was 17 ms), 13.3 ms per
write (emulated card busy 16 clocks); `ls`, `cat`, `fstest` (writes + readback), `bmpview`
and `sdplay -pcm ode.pcm` (full-rate PCM from the card, 57 % CPU spare, no underruns) all
run; `--sd-trace` shows no rejected block, bad CRC or illegal command. Host tests: 371 checks;
`test_sd.c` now drives the emulator's own card model (`emu/sdcard.c` compiled into the test
with its lifecycle functions renamed) end to end: SDHC and SDSC init, reads, writes with the
card verifying the CRC16s, out-of-range, re-init, no card, plus `sd_crc16x4` against a
bit-serial reference.

Two things in the emulator that real cards do differently, both handled: the response
starts exactly Ncr = 8 clocks after a command and the data block Nac = 8 clocks after the
response (the driver waits up to 128 and ~50000 clocks); the card is ready on the second
ACMD41 (the driver polls up to ~1 s). Not handled on purpose: read CRC16s are not checked,
multi-block commands are not used, and a card that answers CMD8 but is not high capacity
is addressed by bytes with CMD16.

## Spec rev 6: audio by buffer hand-off (2026-10-21)

* The rev 5 sample queue (`audio_write`, 64 KB ROM encoder table) is gone. The BIOS now owns
  three 512-byte pattern buffers and streams them: `audio_start(bufsize)` / `audio_next(filled,
  block)` / `audio_last(filled, block)` (syscalls 30-32), a producer/consumer FIFO of buffer
  indices between the syscall and the level-4 handler; a blocking `audio_next` paces the
  program to the line rate. PDM encoding moved to the SDK (`sdk/pdm.c`: table built in user
  RAM in 0.4 s, `pdm_encode` 83 clocks per sample). ROM use is back to 39 KB; BIOS RAM is
  0x1C70 bytes (8 KB of supervisor stack).
* `irq_hsync.S` has two handlers and `audio_update_mode()` switches autovector 28 between
  them, so the fast path has no mode test: 242 clocks per line by hand count, 240.6 measured
  by `abench` (68.5 % of the CPU left while streaming). Callbacks: empty 272, C 423, asm 358.
* Verified in the emulator: `tune.elf` (Ode to Joy, 785 buffers of 512) makes 401,920 AUDIO
  writes over 401,918 lines with no gap over 908 clocks (no underrun), INTEN.HSYNC goes on
  once and off once, and a reconstruction of the pin through the board's RC filter from
  `--audio-edge-log` detects exactly the expected note sequence (the earlier "semitone flat"
  reading was a time-base bug in the analysis script, not in the firmware).
* Host tests: `test/test_audio.c` drives the C line service by hand: start argument checks,
  ownership checks on hand-in, in-order playback, underrun (holds the pattern, interrupt off,
  stream stays open), blocking `audio_next` and `audio_last` through the host wait hook,
  immediate close with nothing queued, smaller bufsize, `audio_stop`, handler selection
  (354 checks in all now).

## Review fixes (2026-10-21)

* `sys_sleep_ms` waits `ms * ticks_per_sec / 1000` whole frames (computed without 32-bit
  overflow) plus `delay_us` for the fraction of a frame; it used a 16 ms frame and ran ~3 %
  long on NTSC. Emulator: `sys_sleep_ms(1000)` = 60 ticks NTSC, 50 PAL.
* `sys_video_info`/`sys_mem_info`/`sys_con_info` range-check the whole structure (`E_FAULT`);
  `sys_puts` needs its string to start in user RAM (`E_FAULT`) and stops at `RAM_END`.
* SDK shim: every ABI `E_*` maps to its newlib `errno` (`ENAMETOOLONG`, `EFAULT`, `ENODEV`,
  `EACCES`, `EROFS` were missing and became `ENOSYS`); `times()`/`clock()` count milliseconds and
  `gettimeofday()` uses the real NTSC/PAL frame length.
* Docs: ACR is 0x00 (baud-rate set 1), `sdhw_read` is 46/48 clocks per bit.

## Stage 2 (what was added)

* (historical, replaced by the rev 7 driver above) `sd.c/h`, `sdspi.S`: SPI mode 0 bit-bang on SDIO (`sdio_shadow`, MISO from
  `INTSTAT.SDMISO`), slow clock through ACMD41, CMD0/8/55/ACMD41(HCS)/58/16/9/10, CMD17 read,
  CMD24 write with data-response and busy handling, CRC7 on every command, timeouts in bytes
  exchanged, `SD_ERR_*`. `sdhw_read` (asm, 46/48 clocks/bit + 18/byte) does the data phases: a
  block is ~17 ms, so card init + mount + loading the 35 KB `hello.elf` is ~2.5 s. Byte-addressed
  SDSC (v1/v2) and block-addressed SDHC both handled (host-tested; the emulator is SDHC).
* `fatfs/`: FatFs R0.15 from elm-chan (ff.c unmodified, `ffconf.h`: RW, LFN off, RPATH 0,
  1 volume, TINY, CP437, NORTC fixed 2026-10-20, no mkfs; rev 9 changed all but RW, TINY and
  CP437, see above). `diskio.c` on `sd.c`. Lazy mount
  on first use; `sd` command and disk errors drop the mount so the next call re-inits.
* `fs.c/h`: 8 `FIL` + 4 `DIR` table (fd 3..10, dir handles 0..3), `O_*` -> FatFs modes
  (`O_TRUNC` without `O_CREAT` = `f_truncate`, `O_APPEND` = seek-to-end per write),
  `FRESULT` -> `E_*`, `struct dirent` fill. `syscall.c`: user pointer checks
  (`USER_BASE..RAM_END`, `E_FAULT` otherwise), fd 0/1/2 stay the console, `sd_read_block`/
  `sd_write_block`, `exec`. `user_cleanup` closes the program's files.
* `elf.c/h`, `exec.c/h`: ELF32 MSB `ET_EXEC` `EM_68K`, `PT_LOAD` segments must fit in
  `[USER_BASE, USER_END)` (`E_NOMEM`; framebuffers and BIOS data rejected), bss zeroed,
  entry inside a segment. Two phases (`exec_prepare` touches no RAM; `exec_load` copies)
  so `sys_exec` reports unloadable files to the caller. `user_launch` builds argv on the
  user stack: `USP -> {user_exit_stub, argc, argv}`, strings and array below
  `USER_STACK_TOP`; `run_user(pc, usp)` in crt0.S takes the prepared stack.
  `sdk/crt0.S` reads `argc`/`argv` from `4(sp)`/`8(sp)`.
* Monitor: `sd` (type, blocks/MB, CID, CSD), `sd rd lba`, `dir [path]`/`ls`, `cat`/`type`,
  `run file [args]` (raw remainder of the line becomes the args). `go` goes through
  `user_launch` with `argc = 0`.
* SDK: `newlib_shim.c` translates newlib's `O_CREAT/O_TRUNC/O_APPEND` (0x200/0x400/0x8) to
  the ABI's (256/512/1024); `fstat` fills `st_size` via lseek; `stat` via open/opendir.
  New examples `ls.c`, `cat.c`, `fstest.c` (write + readback + `sys_exec` chain).
* Host tests: `test_sd.c` (rev 7: the emulator's card model; originally a fake SPI card checking start bits, CRC7, args, HCS, ACMD41 polling,
  CMD16 only for SDSC, byte vs block addresses, data tokens, write data response, timeouts,
  no-card), `test_elf.c` (synthetic ELF, rejections, range checks, `sdk/build/hello.elf`),
  `test_fs.c` (fd table + FatFs on a 64 MB image from `test/mkimage.sh`: read/seek/create/
  write across sectors/append/truncate/open-always, table limits, listing, remount).

## Spec rev 3: flash ROM (2026-10-20)

* `flash.c/h`, `flash_layout.h`, `flashram.S`: in-system update of the two SST39SF040
  (`flashid`, `flash FILE`, `reboot`; `cpu_reboot` in crt0.S). The image is staged at
  `USER_BASE` (128 KB, inside the user region), size and word sum checked, chip IDs checked, then a
  position-independent routine copied to `USER_BASE + ROM_IMAGE_SIZE` erases/programs/verifies
  the 16 sectors with interrupts masked and jumps through the new reset vectors. (Rev 9: a
  256 KB image, 32 sectors, staged at 0x8000; the routine lives in `.data` in BIOS RAM.) Nothing may run
  from the flash after the first erase (first attempt printed progress from C and read garbled
  `.rodata` once the sector holding it had been rewritten: the emulator models this). The POST
  checksum and the banner use `ROM_IMAGE_SIZE`; `mkrom.py` pads the chip images to
  `FLASH_CHIP_SIZE` (sizes read from `generated/hw.h`).

## Verified in the emulator (revision-2 ROM, DUART emulator, `--sd` image)

Commands are in README.md ("Run in the emulator"). Seen: `sd` -> SDHC, 131072 blocks,
CID `EM M68K0`; `dir` lists the five/six files with sizes matching `mdir`; `run hello.elf`
loads 004000-00cb33 and runs newlib printf, waits for the typed key, `[exit 0]`;
`run ls.elf /` prints `argc 2 [ls.elf] [/]` and the listing; `run cat.elf README.TXT
nope.txt` prints the file and `No such file or directory`; `run fstest.elf exec ls.elf`
prints `fstest: OK (1080 bytes, 41 lines)` (TEST.TXT created, appended, re-read; `mtype` on
the host agrees) then `argc 3 [ls.elf] [from] [fstest]`; `cat README.TXT`; `sd rd 0` dumps
the boot sector (ends `55 aa`); `run plasma.elf` animates page 0/1; `run nope.elf: not
found`, `run README.TXT: not a runnable ELF`, `dir /x: not found`. 0 bus errors, 0 serial
overruns. `--sd-trace` shows CMD0, CMD8, CMD55/ACMD41 x2, CMD58, CMD9, CMD10, then CMD17s.

## Known limitations

* 8.3 names only (`FF_USE_LFN 0`); long names show as `~1` aliases, lookups are
  case-insensitive, creating a non-8.3 name is `E_NAMETOOLONG`. No current directory (`cd`),
  no unlink/mkdir/rename (no syscalls in the ABI). (Resolved in spec rev 9: long names up to
  63 characters, a current directory per drive and a current drive, `cd`, syscalls 33-37.)
* A write to C: masks interrupts for ~100 ms per erase sector: serial RX beyond the
  DUART's 3-byte FIFO is lost and audio holds its pattern meanwhile (VBLANK ticks and the
  clock are corrected). There is no wear levelling: the first two erase sectors (boot
  sector, FATs, root directory) take a cycle on nearly every file change; the cache keeps
  the count down (`info` shows it).
* The clock has no battery: 0 after every power-on until `date` (or `AUTOEXEC.CMD`) sets
  it; no time zone.
* The SD card is fixed media (no card-detect line); removing it while powered can lose
  open files' data and leave the FAT inconsistent.
* Write support: `sd_write_block`, `O_CREAT/O_TRUNC/O_APPEND`, `write`, `lseek` past EOF
  are implemented and tested on the host (fake card, FAT image) and in the emulator
  (`fstest.elf` creates/appends `TEST.TXT`). Not tested on a real card; data CRC16 is not
  checked and multi-block commands are not used (one CMD17/CMD24 per 512 bytes).
* A program's files are closed at exit; the monitor's `cat`/`run` use the same table.
  D: descriptors do not survive `sd` (remount); C: and `/dev/ttya` ones do (rev 9).
* `sd rd` dumps all 64 lines; it is not key-abortable because the emulator (and a fast
  terminal) type ahead and the abort would eat the next command's first character (same
  reason `md` is only abortable above 256 bytes). Rev 9: `cat`, `dir` and `copy` stop on
  ESC only, which they find in the keyboard queue without consuming other keys.
* Argument string for `sys_exec` is limited to 255 bytes, 16 words; path 127 bytes.
* `go` after `run` re-enters the image without reloading `.data`; a newlib program's stdio
  state is stale and nothing prints (seen in the emulator). Use `run` again.
* `exec` chaining: if a load fails after headers passed (I/O error mid-copy), the caller is
  gone and the monitor is entered with a message.

## Revision 2 port (stage 1 -> DUART, for reference)

* Serial is MC68681 channel A (`DUART_*` in `generated/hw.h`/`hw.inc`, byte accesses only).
  `sertx.S`/`serbit.inc` (cycle-counted bit-bang TX) are gone; `duart.S` holds
  `duart_reset_init` (registers only, called from `_start` before the POST), the stack-less
  `post_putc/puts/puthex` (poll `SRA.TXRDY`, write `TBA`) and `delay_loops`. `duart.h` has
  the MR1/MR2/CR/ACR values with data-sheet section references (shared by C and `.S`).
* Init order (data sheet 4.2): IMR=0; CRA = 0x10 reset MR pointer, 0x20 reset RX, 0x30 reset
  TX, 0x40 reset error status; ACR = `SERIAL_ACR_VALUE` (0x00: baud-rate set 1, where
  CSR code 0xC is 38.4k; ACR[7] may only change after the RX/TX resets); MR1A = 0x13; MR2A = 0x07; CSRA = `SERIAL_CSR_VALUE` (0xCC); CRA = 0x05.
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
  echoing one (console glyphs, 35 ms scroll) takes longer, so the 512-byte ring would
  overflow. The ISR keeps draining the DUART's 3-byte FIFO meanwhile.
* Spec rev 8 (2026-10-21): serial A is released after the banner (`console_serial = 0` at
  the end of `bios_main`): console output video only, console input keyboard only, `srec`
  reads the port itself. Programs claim the port as `/dev/ttya` through `fs_open` (device
  slots in the fd table, no card needed, survive `fs_invalidate`). Host tests cover it.
* `sdk/README.md`: upload at 38400 8N1. No SDK code touched the serial registers.

## What exists

| file | status |
|---|---|
| `crt0.S` | vectors (256 entries, `.rept`-generated 10-byte stubs, asserted size), `_start`, `post_return` (vector copy, .data/.bss), `exception_common` (group-0 decode), `trap15_handler`, `run_user`, `user_exit_stub`, `monitor_restart`, `cpu_reset`, `irq_disable/restore` |
| `post.S` | ROM word sum, RAM test (address, inverted address, byte lanes with both write orders), failure report + blink; registers only |
| `duart.S`, `duart.h` | `duart_reset_init` (stack-less 68681 channel A set-up), `post_putc/puts/puthex` (poll TXRDY), `delay_loops`; MR/CR/ACR values |
| `memops.S`, `string.c` | memset/memcpy/memmove (long moves when alignment allows), str*/memcmp |
| `main.c` | `bios_main`, banner, `putchar_both` (console, plus serial with `\n` -> CRLF only while `console_serial` is set: POST and banner) |
| `console.c`, `font8x8_basic.h` | 40x30 console, nibble table renderer, scroll, cursor, shadow text; renders through `con_fb` (host-testable) |
| `serial.c` | GPIO shadow + atomic set/clear, DUART RX interrupt enable, polled TX, `delay_us` |
| `input.c`, `queue.c` | serial 512 B (drained by `/dev/ttya` and `serial_getc`), keyboard 256 B, key events 64 x 16-bit rings; `input_pollchar` reads the keyboard only |
| `ps2.c` | set-2 decoder: F0/E0/E1, shift/ctrl/caps, arrows/Home/End/Ins/Del/PgUp/PgDn/F1-F12 escape sequences, raw events |
| `irq.c` | ISRs (`interrupt_handler` attribute, verified `rte` in rom.lst), DUART RX FIFO drain, user hooks, INTEN shadow |
| `exception.c` | names, register dump, `exception_entry` |
| `syscall.c` | `syscall_dispatch` with the generated `.inc`, all 40 `bios_*` (0-39; fs/SD/exec real since stage 2, unlink..set_time since rev 9), user pointer checks, `user_cleanup` |
| `sd.c/h`, `sd4.S` (was `sdspi.S`), `diskio.c`, `fatfs/`, `fs.c/h`, `elf.c/h`, `exec.c/h` | stage 2 (see above; SD rewritten for spec rev 7; two volumes, LFN and the new fs calls in rev 9) |
| `flash.c/h`, `flash_layout.h`, `flashram.S`, `flashdisk.c/h`, `clock.c/h` | rev 3 ROM update; rev 9 C: block layer, sector write (op 2), wall clock |
| `monitor.c`, `srec.c`, `hexdump.c`, `printf.c` | monitor (file commands, scripts, search path since rev 9), S-record parser, dump line, kprintf/ksnprintf |
| `lineedit.c`, `args.c` | line editor (cursor keys, 32-line history, Tab completion) and command-line words (quotes, `*`/`?` expansion; host tested) |
| `joy.c` | joystick switches, paddle timing from the VBLANK and DUART interrupts (rev 11) |
| `test/test_main.c` (+ `test_sd.c`, `test_elf.c`, `test_fs.c`, `test_audio.c`) | host tests: printf, queue, srec (good/bad checksum, S1/S2/S3/S5/S7/S8/S9, CRLF, junk), ps2 (shift/caps/ctrl/arrows/F-keys/pause/ack), console (glyphs, tab, backspace, wrap, scroll, cursor, colours), hexdump, SD framing, ELF, clock, FatFs/fd table on C: and D:, audio |
| `sdk/crt0.S`, `sdk/newlib_shim.c`, `sdk/examples/{hello,plasma,ls,cat,fstest,bmpview,tune,abench,sdplay,term}.c`, `sdk/README.md` | link uses `--start-group -lc -lsys --end-group -lgcc`; crt0 passes argc/argv |
| `../tools/mkflashvol.sh`, `../flashfs/` | C: volume image and its text files (rev 9) |

## Design as built (differences from the original plan)

* Every RAM-test loop handles 16 bytes per iteration so the `dbf` count fits 16 bits for 1 MB
  (rev 4; the first cut filled only half of RAM: caught by the emulator run). Rev 4 also cut
  the four fixed long patterns to one inverted-address pass and the two byte-lane passes to one
  with the write order swapped between the halves of RAM, keeping full cell coverage.
* Fills/verifies use plain `move.l`/`cmp.l` unrolled x4 instead of `movem`: on this bus every
  word is one slot-aligned bus cycle either way, so `movem` buys almost nothing. POST takes
  about 4 s for 1 MB at 12 MHz (50 M clocks measured in the emulator). The same applies to the console scroll (~40 ms measured).
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

## Verified in the emulator, stage 1 features (revision-1 ROM; POST, banner, help, info,
## S-records, go and exit re-checked with the DUART emulator during the stage 2 runs)

* POST: `ROM sum 0000 OK`, `RAM ... OK`; banner; `help`, `info`, `ticks`, `md`, `mb/mw/ml`,
  `fill`, `led`, `page`, `dir` (stage 2 message), unknown command.
* `ml 400000 0` -> `Exception 2: Bus error ... fault 00400000 write FC=5`, monitor continues;
  `regs` repeats it.
* `go 4000` on `nop nop illegal` -> `Exception 4` with SR=0000 (user mode), USP=0001a7fc.
* `go 4000` on `trap #15` (exit) -> `[exit 0]`.
* Typed S-records (auto-detect, no echo) and `srec` command; hello.srec (2198 records) over
  the serial port with no bad lines, `go` runs newlib `printf`.
* `--pal` -> banner `PAL`, 50 ticks/s (the revision-1 `--hw-serial-tx` option is obsolete).

## Stage 1 leftovers

* No PS/2 host-to-device commands (LEDs, typematic); the keyboard's power-on defaults are used.
* Serial TX is polled (one `TXRDY` wait per byte, ~260 us at 38400); a long `md` holds the
  monitor but no interrupts are masked. The DUART's TXRDY interrupt is not used.
* USERIO.OUT is write-only and still has no shadow (nothing writes it).
* ESC sequences from the keyboard were swallowed by the line editor. (Resolved 2026-10-21:
  `lineedit.c` has cursor movement, Home/End/Delete, ^K/^U/^W, a 32-line history with Up/Down
  and `history`, Tab completion of commands, programs and paths with a match listing on the
  second Tab; `args.c` splits with double quotes and expands `*`/`?` program arguments, 32
  words / 511 bytes, `EXEC_MAX_ARGS` 32.)
* NMI path is exercised only through the generic stub mechanism (the emulator needs a window
  for F12); the same path handled bus error / illegal instruction.

## Spec / architecture notes to raise

Stage 2:

* The ABI needed an error code for bad user pointers (`E_INVAL` was used at first)
  (resolved in spec rev 2: `E_FAULT -14` for bad pointers, `E_NODEV -19` for no card / no FAT volume, `E_NAMETOOLONG -36` for non-8.3 names are in the ABI and used.) Previously suggested: `E_NODEV -19`,
  `E_ROFS -30` (write-protected card), `E_NAMETOOLONG -36` (not 8.3, `E_INVAL` before rev 2),
  `E_ACCES -13`.
* The argv convention should be in the spec (`abi` or `syscalls.exec`): user stack at entry
  = `{return address, argc, argv}` as for a C call `_start(argc, argv)`, `argv[0]` = path,
  `args` split on blanks, max 16 args / 255 bytes; `go` gives `argc 0`, `argv[0] NULL`.
* `open` is `(flags, path)`: the spec's `O_*` values (256/512/1024) differ from newlib's
  `fcntl.h` (0x200/0x400/0x8); the SDK shim translates. Either adopt newlib's values in
  the spec or note that `syscalls.h` and `<fcntl.h>` must not be included together.
* `readdir`'s `struct dirent.name[64]` is far more than 8.3 needs (13); fine as is but
  only if LFN is ever enabled would it fill up; a `mtime` field could be added later.
  (Resolved in spec rev 9: LFN is on, `name[64]` holds up to 63 characters, `mtime` added.)
* The spec has no `unlink`/`mkdir`/`rename`/`stat` syscalls; FatFs provides them and the
  SDK's `stat()` is emulated with open+lseek. Suggest adding `stat(path, struct stat_info*)`
  and `unlink` at least. (Rev 9 added `unlink`, `rename`, `mkdir`, `chdir`, `getcwd`; there
  is still no `stat` syscall, so the shim's `stat()` still opens the file or directory.)
* `exec`'s "does not return on success" should say it returns a negative error only before
  any user memory is overwritten (headers checked first), and that `sys_exit` of the new
  program returns to the monitor, not to the caller. (Resolved: the spec's `exec` text says
  so.)
* A 32 MB FAT32 volume is below the FAT32 cluster minimum (65525 clusters at 512 B/cluster);
  `mkfs.fat -F 32` and FatFs both need >= ~33 MB with 512-byte clusters. Test images are 64 MB.

Stage 1:

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
