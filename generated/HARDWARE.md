<!-- Generated from spec/board.json by tools/gen.py -- DO NOT EDIT; edit the spec and run `make gen`. -->
# other-m68k hardware reference

68000 SBC: 1 MB SRAM, 1 MB flash ROM window (128 KB BIOS image), memory-mapped 320x240x8 composite colour framebuffer that time-slices the SRAM with the CPU, PS/2 keyboard, bit-banged SD, 68681 DUART serial, 8-bit PDM audio (one byte per video line shifted out by CPLD2).

CPU clock 12.000 MHz from a 24 MHz oscillator; one slot = 4 CPU clocks.

## Memory map

| Region | Start | End | Size | Kind | Notes |
|---|---|---|---|---|---|
| RAM | 0x000000 | 0x0FFFFF | 1024 KB | ram | 1 MB SRAM. Time-sliced with the video fetch; CPU accesses are phase-aligned by DTACK (0 or 2 wait clocks). CPLD1 drives MA0..MA16 (CPU A1..A17 or the video counter); CPLD2 drives MA17..MA18 (CPU A18..A19 in the CPU half of a slot, the framebuffer pages' fixed bits 19..18 during the video fetch). At reset the ROM is overlaid here until the first access to the ROM region. |
| ROM | 0xE00000 | 0xEFFFFF | 1024 KB | rom | 1 MB flash window; the BIOS image occupies the first 128 KB (ROM_IMAGE_SIZE, checksum in its last word), the rest is free (erased 0xFF). Reads: zero wait states, not subject to slot alignment. Writes get DTACK and reach the chips (ROMWE), so the firmware can reprogram itself in place with the JEDEC command sequence (see "flash"). Vectors at offset 0 (overlaid at 0x000000 at reset). |
| IO | 0xF00000 | 0xF0FFFF | 64 KB | io | CPLD2 peripheral registers. 16-bit word access only; address bits A5..A15 ignored (mirrored). INTEN, SDIO, GPIO and USERIO.OUT are write-only (read as 0): the fitter budget does not allow read-back; software keeps shadows. |
| SWITCHES | 0xF10000 | 0xF1FFFF | 64 KB | io | CPLD1 soft switches. Write-only; writing any value to the address performs the action. Reads return garbage. |
| DUART | 0xF20000 | 0xF2FFFF | 64 KB | io | 68681 DUART. Byte registers on D7..D0 at ODD addresses (A1..A4 -> RS1..RS4); A5..A15 ignored. Use odd-address byte accesses only: an even-byte or word cycle still selects the chip (undefined data; a read pops the FIFO into the wrong lane). DTACK comes from the DUART (open drain, shared with CPLD1); accesses are not slot aligned. |

BERR asserted (bus error exception) for any access outside the regions above, except the interrupt-acknowledge pattern A23..A4 all ones, which is autovectored via VPA.

## Flash ROM

SST39SF040-70-4C-PHE, one per byte lane (U4 = D15..D8 even bytes, U5 = D7..D0 odd bytes). Chip 512 KB, sectors 4096 bytes per chip (8192 bytes in the CPU's word view).

Both chips see the same command sequence because every command is issued as a WORD write with the byte repeated in both halves: chip byte address N is CPU address ROM_BASE + 2N, so the JEDEC unlock addresses 0x5555/0x2AAA become FLASH_CMD1 = ROM_BASE + 0xAAAA and FLASH_CMD2 = ROM_BASE + 0x5554. Program a word: 0xAAAA->CMD1, 0x5555->CMD2, 0xA0A0->CMD1, data->address, then poll until a read returns the data (each byte shows toggling DQ6 / inverted DQ7 while busy; at most 20 us). Erase the 8 KB (CPU view) sector holding address X: 0xAAAA->CMD1, 0x5555->CMD2, 0x8080->CMD1, 0xAAAA->CMD1, 0x5555->CMD2, 0x3030->X, poll until 0xFFFF (at most 25 ms). ID: 0xAAAA->CMD1, 0x5555->CMD2, 0x9090->CMD1; read ROM_BASE -> 0xBFBF, ROM_BASE+2 -> 0xB7B7; 0xF0F0 anywhere exits. Programming only clears bits: erase first. The chips cannot be read (code cannot run from them) while an operation is in progress, so the routine that issues commands runs from RAM with interrupts masked; the NMI button must not be pressed meanwhile. Only word writes are used; a byte write would present the same byte to both chips (the 68000 duplicates write data on both bus halves) and is not part of the protocol.

| Constant | Value |
|---|---|
| FLASH_CHIP_SIZE | 0x080000UL |
| FLASH_SECTOR_BYTES | 0x002000UL |
| FLASH_CMD1 | 0xE0AAAAUL |
| FLASH_CMD2 | 0xE05554UL |
| FLASH_MFR_ID | 0xBF |
| FLASH_DEV_ID | 0xB7 |
| FLASH_T_BYTE_PROGRAM_US | 20 |
| FLASH_T_SECTOR_ERASE_MS | 25 |
| FLASH_T_CHIP_ERASE_MS | 100 |

## RAM layout

| Area | Start | End | Size | Purpose |
|---|---|---|---|---|
| VECTORS | 0x000000 | 0x0003FF | 1024 bytes | Exception vector table (RAM after overlay clears). |
| SYSDATA | 0x000400 | 0x003FFF | 15360 bytes | BIOS .data/.bss; supervisor stack grows down from the top (0x004000). |
| USER | 0x004000 | 0x0DA7FF | 878592 bytes | User program load region (ELF link address 0x004000). User stack grows down from the top (0x0DA800). Extends to 0x0ED400 if page 1 of the framebuffer is not used. |
| FB1 | 0x0DA800 | 0x0ED3FF | 76800 bytes | Framebuffer page 1 (optional double buffer). |
| FB0 | 0x0ED400 | 0x0FFFFF | 76800 bytes | Framebuffer page 0 (default, displayed after reset). |

## Video

320x240, 8 bpp RGB332. bits 7..5 red, 4..2 green, 1..0 blue. Byte at the even (upper, D15..D8) address is the left pixel of the word.

Pages: page 0 at 0x0ED400, page 1 at 0x0DA800

Encoder: AD724 (RGB in, composite out). Standard (NTSC/PAL) fixed by two pull resistors (STND, PALJMP) plus the matching subcarrier crystal; the CPLD timing table is selected by soft switch; jumper state readable in INTSTAT.PALJMP.

| Standard | Line clocks | H active/front/sync/back | Lines | V active/bottom/sync/top | Rates |
|---|---|---|---|---|---|
| NTSC | 764 | 640/16/56/52 | 262 | 240/3/3/16 | 15.707 kHz, 59.95 Hz |
| PAL | 768 | 640/16/56/56 | 312 | 240/30/3/39 | 15.625 kHz, 50.08 Hz |

All horizontal quantities are CPU clocks and are multiples of the 4-clock slot, so the active region never jitters against the slot grid. Video fetches one 16-bit word (two pixels) per slot during the active region.

Horizontal count 0 = first active pixel; vertical count 0 = first active line. The VBLANK interrupt flag sets at the leading edge of vertical sync (line v_active + v_bottom), not at the end of active video.

## IO registers (base 0xF00000, 16-bit)

CPLD2 peripheral registers. 16-bit word access only; address bits A4..A15 are not decoded, so the 16-byte register block repeats through the region. INTEN, SDIO, GPIO, AUDIO and USERIO.OUT are write-only (read as 0): the fitter budget does not allow read-back; software keeps shadows.

### INTSTAT @ 0xF00000 (RW)

Interrupt and pin status. Reading returns flags and live pin levels; bits not listed read 0. Writing 1 to a clearable bit clears it.

| Bit | Name | Description |
|---|---|---|
| 0 | VBLANK | Set at the leading edge of vertical sync (3 lines after the last active line on NTSC). W1C. |
| 1 | HSYNC | Set at start of each horizontal sync. W1C. |
| 2 | PS2RDY | PS/2 byte received; cleared by reading PS2DATA. |
| 3 | DUARTIRQ | Live level of the DUART IRQ pin, inverted (1 = DUART requesting). Read the DUART ISR for the cause. |
| 5 | PS2ERR | PS/2 framing/parity error on last byte. W1C. |
| 7 | NMIBTN | Live level of the NMI/break pushbutton (1 = pressed). |
| 8 | SDMISO | Live level of SD MISO. |
| 9 | PS2CLK | Live level of PS/2 clock line. |
| 10 | PS2DAT | Live level of PS/2 data line. |
| 12 | PALJMP | 1 = board built for PAL (PALJMP pulled high); rev B pulls it low (NTSC). |
| 13 | VSYNCPIN | Live vertical sync (1 during vsync lines). |
| 14 | DEPIN | Live display enable (1 during active video). |
| 15 | HSYNCPIN | Live horizontal sync (1 during sync). |

### INTEN @ 0xF00002 (W) reset 0x0000

Interrupt enables (write-only, reads 0). A pending flag with its enable set drives IPL at the level listed in interrupts[].

| Bit | Name | Description |
|---|---|---|
| 0 | VBLANK | Enable level-1 interrupt on VBLANK flag. |
| 1 | HSYNC | Enable level-4 interrupt on HSYNC flag. |
| 2 | PS2 | Enable level-2 interrupt on PS2RDY. |
| 3 | DUART | Enable level-3 interrupt while the DUART IRQ pin is asserted. |

### SDIO @ 0xF00004 (W) reset 0x0001

SD card SPI bit-bang port (write-only, reads 0). Bits are pin levels; reset = CS high, CLK low, MOSI low (SPI mode 0 idle). MISO is read through INTSTAT.SDMISO.

| Bit | Name | Description |
|---|---|---|
| 0 | CS | SD chip select pin (card selected when 0). |
| 1 | CLK | SD clock pin. |
| 2 | MOSI | SD MOSI pin. |

### GPIO @ 0xF00006 (W) reset 0x0003

Misc outputs (write-only, reads 0). PS/2 lines are open-drain: 1 releases the line (read it via INTSTAT), 0 pulls it low. Bit 2 is unused since revision 5 (audio moved to the AUDIO register) and must be written 0.

| Bit | Name | Description |
|---|---|---|
| 0 | PS2CLK | PS/2 clock drive (1 = release). |
| 1 | PS2DAT | PS/2 data drive (1 = release). |
| 3 | LED0 | Diagnostic LED 0 (1 = on). |
| 4 | LED1 | Diagnostic LED 1 (1 = on). |

### PS2DATA @ 0xF00008 (R)

Last PS/2 byte received (bits 7..0). Reading clears INTSTAT.PS2RDY.

| Bit | Name | Description |
|---|---|---|
| 7..0 | DATA | Received scan code byte. |

### AUDIO @ 0xF0000A (W) reset 0x0000

PDM audio shifter (write-only, reads 0). The low byte is loaded into an 8-bit rotating shift register whose bit 7 drives the audio pin. CPLD2 rotates it left one place at the leading edge of HSYNC and then every 24 bus slots (192 CLK24 cycles = 96 CPU clocks), i.e. 8 bits per video line, bit 7 first, ~126 kHz PDM; the rotation is resynchronised to HSYNC so it never drifts. Because the register rotates rather than shifting in zeros, a line without a write repeats the previous byte. Write once per line from the HSYNC interrupt, as early in the line as possible; the write replaces the register immediately. The bit pattern is a pulse-density code: the fraction of 1 bits sets the output level (0x00 silence/low, 0xAA or 0x55 mid-level, 0xFF high). The BIOS encodes 8-bit unsigned samples into patterns with a first-order sigma-delta table (see audio_write); a program may also write patterns directly. Writing 0x00 or 0xFF for whole lines and toggling between them gives the old 1-bit behaviour.

| Bit | Name | Description |
|---|---|---|
| 7..0 | PATTERN | PDM bit pattern, bit 7 is shifted out first. |

### USERIO @ 0xF0000C (RW) reset 0x000F

Four open-drain user GPIO pins on the expansion header. Write 1 to release, 0 to pull low (OUT is write-only, reads 0); read the pin levels in bits 11..8.

| Bit | Name | Description |
|---|---|---|
| 3..0 | OUT | Drive bits (1 = release). (write only) |
| 11..8 | IN | Pin levels (read only). (read only) |

### ID @ 0xF0000E (R) reset 0x6802

Board/glue identification: 0x68 in the upper byte, CPLD2 logic revision in the lower byte (2 = DUART board).

| Bit | Name | Description |
|---|---|---|
| 7..0 | REV | Glue logic revision. |
| 15..8 | MAGIC | Always 0x68. |

## SWITCHES registers (base 0xF10000, 16-bit)

### PAGE0 @ 0xF10000 (W)

Display framebuffer page 0 (0x0ED400) starting next frame.

### PAGE1 @ 0xF10002 (W)

Display framebuffer page 1 (0x0DA800) starting next frame.

### NTSC @ 0xF10004 (W)

Select NTSC (262-line) timing table. Default at reset.

### PAL @ 0xF10006 (W)

Select PAL (312-line) timing table.

## DUART registers (base 0xF20000, 8-bit, byte access at odd addresses)

MC68681 registers; one name per direction where read and write differ at the same offset. Offsets 5 and 21 must not be READ (factory test locations: CRA/CRB are write-only). Even-address bytes and word accesses in this region do hit the chip (DUARTCS is asserted for any cycle in the region) with undefined data: do not use them.

### MRA @ 0xF20001 (RW)

Mode register: first access after reset or a 'reset MR pointer' command is MR1 (bits: 1..0 bits per char 00=5..11=8, 2 parity mode, 4..3 parity type, 5 error mode, 6 RX interrupt select, 7 RX RTS control), the next is MR2 (bits: 3..0 stop bits (0x7 = 1 stop), 4 CTS enable TX, 5 RTS control, 7..6 channel mode 00 = normal).

### SRA @ 0xF20003 (R)

Status register A.

| Bit | Name | Description |
|---|---|---|
| 0 | RXRDY | Receiver ready: a character is in the FIFO. |
| 1 | FFULL | Receiver FIFO full (3 characters). |
| 2 | TXRDY | Transmitter ready to accept a character. |
| 3 | TXEMT | Transmitter empty. |
| 4 | OE | Overrun error. |
| 5 | PE | Parity error (of the character at the FIFO top). |
| 6 | FE | Framing error. |
| 7 | RB | Received break. |

### CSRA @ 0xF20003 (W)

Clock select: bits 7..4 receiver, 3..0 transmitter baud code from the data-sheet table (set 1, ACR[7]=0: 0xC = 38.4k, 0xB = 9600, 0x8 = 2400; set 2 swaps some rows, e.g. 0xC = 19.2k).

### CRA @ 0xF20005 (W)

Command register: bits 1..0 RX enable/disable (01 enable, 10 disable), 3..2 TX enable/disable, 7..4 misc command (0001 reset MR pointer, 0010 reset RX, 0011 reset TX, 0100 reset error status, 0101 reset break-change interrupt).

### RBA @ 0xF20007 (R)

Receiver buffer A (FIFO top); reading pops the FIFO.

### TBA @ 0xF20007 (W)

Transmitter buffer A.

### IPCR @ 0xF20009 (R)

Input port change register.

### ACR @ 0xF20009 (W)

Auxiliary control: bit 7 baud-rate set (0 = set 1, used here: 0xC = 38.4k), bits 6..4 counter/timer mode and source, bits 3..0 IP change interrupt enables.

### ISR @ 0xF2000B (R)

Interrupt status register.

| Bit | Name | Description |
|---|---|---|
| 0 | TXRDYA | TxRDY A |
| 1 | RXRDYA | RxRDY/FFULL A |
| 2 | DBA | Change in break A |
| 3 | CTR | Counter/timer ready |
| 4 | TXRDYB | TxRDY B |
| 5 | RXRDYB | RxRDY/FFULL B |
| 6 | DBB | Change in break B |
| 7 | IPC | Input port change |

### IMR @ 0xF2000B (W)

Interrupt mask register (same bits as ISR).

| Bit | Name | Description |
|---|---|---|
| 0 | TXRDYA | TxRDY A |
| 1 | RXRDYA | RxRDY/FFULL A |
| 2 | DBA | Change in break A |
| 3 | CTR | Counter/timer ready |
| 4 | TXRDYB | TxRDY B |
| 5 | RXRDYB | RxRDY/FFULL B |
| 6 | DBB | Change in break B |
| 7 | IPC | Input port change |

### CUR @ 0xF2000D (R)

Counter upper byte.

### CTUR @ 0xF2000D (W)

Counter/timer upper preset.

### CLR @ 0xF2000F (R)

Counter lower byte.

### CTLR @ 0xF2000F (W)

Counter/timer lower preset.

### MRB @ 0xF20011 (RW)

Mode register: first access after reset or a 'reset MR pointer' command is MR1B (bits: 1..0 bits per char 00=5..11=8, 2 parity mode, 4..3 parity type, 5 error mode, 6 RX interrupt select, 7 RX RTS control), the next is MR2B (bits: 3..0 stop bits (0x7 = 1 stop), 4 CTS enable TX, 5 RTS control, 7..6 channel mode 00 = normal).

### SRB @ 0xF20013 (R)

Status register B.

| Bit | Name | Description |
|---|---|---|
| 0 | RXRDY | Receiver ready: a character is in the FIFO. |
| 1 | FFULL | Receiver FIFO full (3 characters). |
| 2 | TXRDY | Transmitter ready to accept a character. |
| 3 | TXEMT | Transmitter empty. |
| 4 | OE | Overrun error. |
| 5 | PE | Parity error (of the character at the FIFO top). |
| 6 | FE | Framing error. |
| 7 | RB | Received break. |

### CSRB @ 0xF20013 (W)

Clock select: bits 7..4 receiver, 3..0 transmitter baud code from the data-sheet table (set 1, ACR[7]=0: 0xC = 38.4k, 0xB = 9600, 0x8 = 2400; set 2 swaps some rows, e.g. 0xC = 19.2k).

### CRB @ 0xF20015 (W)

Command register: bits 1..0 RX enable/disable (01 enable, 10 disable), 3..2 TX enable/disable, 7..4 misc command (0001 reset MR pointer, 0010 reset RX, 0011 reset TX, 0100 reset error status, 0101 reset break-change interrupt).

### RBB @ 0xF20017 (R)

Receiver buffer B.

### TBB @ 0xF20017 (W)

Transmitter buffer B.

### IVR @ 0xF20019 (RW)

Interrupt vector register. Unused: IACK is tied high and interrupts are autovectored.

### IP @ 0xF2001B (R)

Input port IP0..IP6 (unlatched).

### OPCR @ 0xF2001B (W)

Output port configuration register.

### START_CTR @ 0xF2001D (R)

Address-triggered command: start counter/timer.

### OPR_SET @ 0xF2001D (W)

Output port register: set the bits written as 1.

### STOP_CTR @ 0xF2001F (R)

Address-triggered command: stop counter/timer.

### OPR_RESET @ 0xF2001F (W)

Output port register: clear the bits written as 1.

## Interrupts

autovector (VPA asserted by CPLD1 on every IACK; the DUART's IACK pin is tied inactive); vector = 24 + level

| Level | Name | Vector | Source | Description |
|---|---|---|---|---|
| 1 | VBLANK | 25 (0x064) | INTSTAT.VBLANK & INTEN.VBLANK | Leading edge of vertical sync, ~60 Hz (NTSC) / 50 Hz (PAL). |
| 2 | PS2 | 26 (0x068) | INTSTAT.PS2RDY & INTEN.PS2 | PS/2 byte available. |
| 3 | DUART | 27 (0x06C) | DUART IRQ pin & INTEN.DUART | 68681 interrupt (receiver ready, transmitter ready, counter/timer, ...): read the DUART ISR. Level sensitive; the DUART IACK pin is tied high, so this is autovectored like the others. |
| 4 | HSYNC | 28 (0x070) | INTSTAT.HSYNC & INTEN.HSYNC | Line tick, 15.7 kHz; audio sample timer. |
| 7 | NMI | 31 (0x07C) | pushbutton | Break into monitor. Edge-triggered by the 68000. |

## Peripherals

**serial**: device: MC68681 DUART channel A (channel B TXD/RXD brought to the expansion header); baud: 38400; format: 8N1; xtal_hz: 3686400; csr: 0xCC; acr_brg_set: 0; mr1: 0x13; mr2: 0x07; cr_commands: OrderedDict({'RESET_MR': '0x10', 'RESET_RX': '0x20', 'RESET_TX': '0x30', 'RESET_ERR': '0x40', 'RESET_BRK': '0x50', 'ENABLE': '0x05'}); init: MR1A=0x13 (8 bits, no parity), MR2A=0x07 (1 stop), ACR=0x00 (baud-rate set 1), CSRA=0xCC (38.4k RX and TX), CRA=0x05 (enable RX+TX); IMR bit 1 (RXRDYA) for receive interrupts; note: 38400 is the highest rate in the 68681's fixed tables: CSR code 0xC is 38.4k in set 1 (ACR[7]=0) and 19.2k in set 2 (ACR[7]=1), data sheet Table 4-5. The counter/timer or an external 16x clock could give more but are not used.; connector: 6-pin FTDI header (GND, CTS, VCC, TXD, RXD, RTS); 5V TTL levels, FT232H inputs are 5V tolerant; CTS/RTS not connected

**ps2**: rx: hardware 11-bit frame receiver in CPLD2 (start, 8 data, odd parity, stop) -> PS2DATA + PS2RDY; tx: software bit-bang via GPIO.PS2CLK/PS2DAT open-drain bits; connector: 6-pin mini-DIN, 5V

**sd**: mode: SPI bit-bang via SDIO register; connector: microSD push-push socket; power: 3.3V LDO; CLK/MOSI/CS through resistor dividers, MISO 3.3V direct into 5V TTL-threshold input; filesystem: FAT12/16/32 via FatFs (LFN off). A FAT32 volume needs >= 65525 clusters: use cards/images of 64 MB or more for FAT32 (smaller ones are FAT16, also fine).

**audio**: enabled: True; desc: AUDIO register -> 8-bit rotating PDM shifter in CPLD2 (8 bits per video line, ~126 kHz) -> 1 k / 10 nF RC low-pass -> 10 k : 2.2 k divider -> 10 uF DC-blocking cap -> 3.5mm jack. Sampled audio: the program hands the BIOS buffers of PDM pattern bytes (audio_start / audio_next / audio_last, AUDIO_NBUF buffers of up to AUDIO_BUF_MAX bytes, producer/consumer, optionally blocking so the program is paced to the line rate); the BIOS HSYNC interrupt writes one byte per line (15.7 kHz NTSC, 15.6 kHz PAL) and does no other per-sample work. Encoding 8-bit unsigned samples (128 = silence) into patterns is the program's job, normally through the SDK's pdm_init/pdm_encode (first-order sigma-delta, 8-bit accumulator x 7-bit sample table built in RAM). The pattern of a line is repeated by the hardware if no new byte arrives. Beeps: write 0x00/0xFF patterns at the tone rate. Idle level: the mid pattern 0xAA (50 % density) so starting and stopping playback does not click.

**power**: input: USB-C receptacle, 5V only (5.1k CC pull-downs); rails: 5V, 3.3V (SD card only)

**reset**: supervisor: open-drain voltage supervisor + pushbutton -> SYSRESET; diodes to 68000 RESET and HALT; SYSRESET also resets both CPLDs. The RESET instruction does not reset the glue (so the ROM overlay is not re-armed by software).

## System calls

TRAP #15 with the call number in D0.W. Integer args in D1..D3, pointer args in A0..A1 in the order listed. Result in D0.L. All other registers preserved.

| # | Name | Prototype | Description |
|---|---|---|---|
| 0 | exit | `void sys_exit(int code)` | Terminate the program and return to the monitor. |
| 1 | putchar | `int sys_putchar(int c)` | Write a character to the console (video + serial). |
| 2 | getchar | `int sys_getchar(void)` | Blocking read of one character from keyboard or serial. |
| 3 | pollchar | `int sys_pollchar(void)` | Non-blocking read; -1 if no character is available. |
| 4 | puts | `int sys_puts(const char * s)` | Write a NUL-terminated string to the console (no newline added). |
| 5 | serial_putc | `int sys_serial_putc(int c)` | Write a byte to the serial port only. |
| 6 | serial_getc | `int sys_serial_getc(void)` | Non-blocking serial read; -1 if none. |
| 7 | ticks | `uint32_t sys_ticks(void)` | Frames since boot (VBLANK counter, ~60 Hz). |
| 8 | wait_vblank | `void sys_wait_vblank(void)` | Block until the next vertical blanking interval. |
| 9 | video_info | `int sys_video_info(struct video_info * info)` | Fill in framebuffer geometry, page addresses and current page. |
| 10 | video_set_page | `int sys_video_set_page(int page)` | Select displayed framebuffer page (0 or 1); takes effect at next frame. |
| 11 | open | `int sys_open(int flags, const char * path)` | Open a file on the SD card (8.3 names, O_* flags from the ABI; O_CREAT/O_TRUNC/O_APPEND values differ from newlib's, the SDK shim translates). Returns fd >= 3 or a negative error; fds 0..2 are the console. |
| 12 | close | `int sys_close(int fd)` | Close a file. |
| 13 | read | `int sys_read(int fd, unsigned len, void * buf)` | Read up to len bytes. Returns count or negative error. |
| 14 | write | `int sys_write(int fd, unsigned len, const void * buf)` | Write len bytes. Returns count or negative error. |
| 15 | lseek | `int sys_lseek(int fd, int offset, int whence)` | Reposition; returns new offset or negative error. |
| 16 | opendir | `int sys_opendir(const char * path)` | Open a directory; returns handle or negative error. |
| 17 | readdir | `int sys_readdir(int handle, struct dirent * ent)` | Next entry; 1 if filled, 0 at end, negative error. |
| 18 | closedir | `int sys_closedir(int handle)` | Close a directory handle. |
| 19 | sd_read_block | `int sys_sd_read_block(uint32_t lba, void * buf)` | Raw 512-byte block read. |
| 20 | sd_write_block | `int sys_sd_write_block(uint32_t lba, const void * buf)` | Raw 512-byte block write. |
| 21 | set_irq_handler | `void * sys_set_irq_handler(int level, void * handler)` | Install a user interrupt handler for level 1..4 (called from the BIOS ISR in supervisor mode at that interrupt's own priority, so higher levels can still interrupt it); returns the previous one. NULL restores default. |
| 22 | mem_info | `int sys_mem_info(struct mem_info * info)` | Report the user RAM region available to the program. |
| 23 | con_set_cursor | `int sys_con_set_cursor(int x, int y)` | Move the console text cursor. |
| 24 | con_clear | `int sys_con_clear(void)` | Clear the console. |
| 25 | con_info | `int sys_con_info(struct con_info * info)` | Console columns, rows, cursor position. |
| 26 | exec | `int sys_exec(const char * path, const char * args)` | Load and run another ELF executable from the SD card with argument string `args`. Returns a negative error only if the file cannot be opened or does not fit user RAM, before any user memory is overwritten; otherwise the new program replaces the caller and its exit returns to the monitor. |
| 27 | key_event | `int sys_key_event(void)` | Next raw keyboard event: bit 8 = release, bits 7..0 = PS/2 set-2 scan code (E0-prefixed codes have bit 9 set); -1 if none. |
| 28 | sleep_ms | `void sys_sleep_ms(uint32_t ms)` | Delay for approximately ms milliseconds. |
| 29 | set_audio_cb | `int sys_set_audio_cb(void * cb)` | Install a function called from the HSYNC interrupt once per video line (15.7 kHz NTSC) in supervisor mode; it takes over the AUDIO register (the buffer stream of audio_start/next/last is not serviced while a callback is installed, and audio_start fails while one is) and should write one PDM pattern per call. NULL disables. |
| 30 | audio_start | `uint8_t * sys_audio_start(unsigned bufsize)` | Begin streaming PDM pattern bytes to the AUDIO register, one byte per video line (15.7 kHz NTSC, 15.6 kHz PAL). The BIOS owns AUDIO_NBUF buffers of AUDIO_BUF_MAX bytes; bufsize (1..AUDIO_BUF_MAX) is how many bytes of each the program will fill and the hardware will play. Resets the stream (anything queued is dropped, the output goes to the idle pattern) and returns the first buffer to fill. Non-blocking. Returns NULL if bufsize is out of range or an audio callback is installed. The bytes are raw hardware patterns: encode samples with the SDK's pdm_encode (first-order sigma-delta) or write patterns directly. Playback starts at the first audio_next. |
| 31 | audio_next | `uint8_t * sys_audio_next(const uint8_t * filled, int block)` | Hand the BIOS a buffer the program has filled (one obtained from audio_start or a previous audio_next) and get the next buffer to fill. Buffers play in the order handed in; the HSYNC interrupt is enabled while any are queued. If no buffer is free: block != 0 waits (interrupts enabled) until the one being played finishes, which paces the program to the audio rate; block == 0 returns NULL at once and the program retries later (the handed buffer is still accepted). filled == NULL hands nothing in and only tries to obtain a free buffer. Returns NULL also when the stream is not started, or when filled is not one of the BIOS buffers. |
| 32 | audio_last | `int sys_audio_last(const uint8_t * filled, int block)` | Hand in the final buffer (filled may be NULL for none) and end the stream. block != 0 waits until everything queued has played; block == 0 returns at once and the queued buffers finish playing on their own. When the last byte has played the handler writes the idle pattern 0xAA and disables the HSYNC interrupt, so idle audio costs no CPU. Returns 0, or E_INVAL if the stream was not started. Exiting the program stops the stream at once instead: queued buffers are dropped, the idle pattern is written and the interrupt is disabled. |

### ABI constants

| Name | Value |
|---|---|
| O_RDONLY | 0 |
| O_WRONLY | 1 |
| O_RDWR | 2 |
| O_CREAT | 256 |
| O_TRUNC | 512 |
| O_APPEND | 1024 |
| SEEK_SET | 0 |
| SEEK_CUR | 1 |
| SEEK_END | 2 |
| ATTR_DIR | 1 |
| ATTR_READONLY | 2 |
| ATTR_HIDDEN | 4 |
| VIDEO_STD_NTSC | 0 |
| VIDEO_STD_PAL | 1 |
| E_NOENT | -2 |
| E_ACCES | -13 |
| E_FAULT | -14 |
| E_IO | -5 |
| E_NOEXEC | -8 |
| E_BADF | -9 |
| E_NOMEM | -12 |
| E_EXIST | -17 |
| E_NOTDIR | -20 |
| E_NODEV | -19 |
| E_ISDIR | -21 |
| E_INVAL | -22 |
| E_NFILE | -23 |
| E_NOSPC | -28 |
| E_ROFS | -30 |
| E_NAMETOOLONG | -36 |
| E_NOSYS | -38 |
| KEY_RELEASE | 256 |
| KEY_EXTENDED | 512 |
| AUDIO_BUF_MAX | 512 |
| AUDIO_NBUF | 3 |

### ABI structures

`struct video_info { uint32_t width; uint32_t height; uint32_t bpp; uint32_t bytes_per_line; uint32_t page_addr[2]; uint32_t current_page; uint32_t standard; }`

`struct mem_info { uint32_t user_start; uint32_t user_end; uint32_t user_end_without_fb1; }`

`struct con_info { uint32_t cols; uint32_t rows; uint32_t cursor_x; uint32_t cursor_y; }`

`struct dirent { char name[64]; uint32_t size; uint32_t attr; }`

## Glue logic

**CPLD1** (ATF1508AS-7JX84): MMU: CPU clock divider, address decode, ROM overlay, flash read/write strobes, DTACK/BERR/VPA, SRAM address mux and strobes, video address counter, H/V timing, soft switches, DUART chip select (DTACK released during DUART cycles)

* inputs: CLK24, SYSRESET, A1..A23, AS, UDS, LDS, RW
* outputs: CPUCLK, MA0..MA16, DTACK, BERR, VPA, RAMOE, WEU, WEL, ROMOE, ROMWE, IOCS, BUFEN, LATCH, DE, HSYNC, VSYNC, DUARTCS

**CPLD2** (ATF1508AS-7JX84): I/O and shifter: video word latch and RGB332 pixel output, interrupt controller (incl. DUART IRQ), PS/2 receiver, SD/GPIO bits, 8-bit PDM audio shifter, registers, upper SRAM address bits MA17..MA18 (CPU A18..A19 in the CPU half of each slot, the framebuffer page bits during the video fetch)

* inputs: CLK24, SYSRESET, A1..A3, A18, A19, IOCS, RW, LATCH, DE, HSYNC, VSYNC, SD_MISO, PALJMP, NMIBTN, DUART_IRQ
* bidir: D0..D15 (SRAM-side bus), PS2_CLK, PS2_DAT, UIO0..UIO3
* outputs: IPL0, IPL1, IPL2, R2, R1, R0, G2, G1, G0, B1, B0, SD_CS, SD_CLK, SD_MOSI, AUDIO, LED0, LED1, MA17, MA18

2x 74AHCT245 between the CPU data bus (CPU, flash, DUART on D0..D7) and the SRAM-side data bus (SRAMs, CPLD2). Enabled only during the CPU half of each slot for RAM and IO cycles; direction from R/W.

DTACK is an open-drain bus with a 4.7k pull-up: CPLD1 drives it low (and high again) for RAM, ROM, IO and SWITCHES cycles and releases it (high-Z) whenever AS is low in the DUART region; the DUART's open-drain DTACK answers those cycles. The DUART IRQ (open drain, pull-up) is an input of CPLD2; its IACK pin is tied high.
