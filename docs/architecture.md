# other-m68k architecture and bus timing

This document is the contract between the glue logic (hdl/), the emulator (emu/) and the
firmware (fw/). Addresses, register bits and timing constants come from `spec/board.json`;
this file explains the mechanism. If the two disagree, fix the spec and regenerate.

## 1. Block diagram

```
                 24 MHz osc
                     |
   +-----------------v------------------+        +-----------------------------+
   |  CPLD1  "MMU"  (ATF1508AS)         |        |  CPLD2  "I/O + shifter"     |
   |  /2 -> CPUCLK (12 MHz)             | LATCH  |  (ATF1508AS)                |
   |  decode, overlay, DTACK/BERR/VPA   |------->|  video word latch -> RGB332 |--> 8 R-DACs --> AD724 --> composite
   |  SRAM addr mux + video counter     | DE,HS, |  interrupt ctrl -> IPL0..2  |
   |  H/V timing -> HSYNC,VSYNC         |  VS    |  PS/2 rx, DUART IRQ -> IPL  |
   |  soft switches (page, PAL)         |        |  SD / GPIO / LED / audio    |
   |  DUARTCS                           |        |  MA17..18 <- A18..19 / FB   |
   +---+---------------+----------------+        +------+------------+---------+
       |A1..A23,AS,     |MA0..16, RAMOE, WEU/WEL         | D0..D15    | MA17..18
       |UDS,LDS,RW      |                                | (SRAM-side bus)
   +---+------+     +---v----------------+    +----------+-----+
   |  68000   |     | 2x AS6C4008 SRAM   |====| 2x 74AHCT245   |==== CPU data bus ==== 2x SST39SF040 flash
   | 12 MHz   |==== | 1 MB (SRAM-side    |    | (BUFEN, dir=RW)|                  \== 68681 DUART (D0..D7) -> serial
   +----------+     |  bus)              |    +----------------+
                    +--------------------+
```

Two data buses:

* **CPU bus**: 68000, the two flash chips, the 68681 DUART (low byte only), A-side of the transceivers.
* **SRAM-side bus**: SRAMs, CPLD2, B-side of the transceivers.

The transceivers are enabled only during the CPU half of a slot for RAM and I/O cycles, so
the video fetch (SRAM -> CPLD2) never shares wires with the CPU driving write data or the
flash chips driving read data. This is what makes 12 MHz with 55-70 ns parts close cleanly.

**SRAM address (spec rev 4, 1 MB = 2x 512Kx8).** The 19-bit word address is split between
the CPLDs because CPLD1 ran out of pins: CPLD1 drives MA16..MA0 (A17..A1 or the video
counter), CPLD2 drives MA18..MA17. Both run from the same 24 MHz clock and load on the same
edges: at E4 CPLD2 loads the CPU's A19..A18 while CPLD1 loads A17..A1; at E1 CPLD2 loads
`FB_PAGE_HI` (the fixed address bits 19..18 of both framebuffer pages) while CPLD1 loads the
17-bit video counter. The two framebuffer pages therefore sit in the same 256 KB quarter,
the top one (0xDA800 and 0xED400), which leaves 0x004000-0xDA7FF (858 KB) contiguous for
user programs. Nothing in the timing analysis below changes: the extra bits have the same
clock-to-output as CPLD1's and the SRAM sees one address change per edge.

## 2. Clocks and the slot

CPLD1 divides the 24 MHz oscillator by two and drives the 68000's CLK pin. Everything is
described on the 24 MHz grid. Edges are **E0..E7** (41.67 ns apart); the interval after
edge En is **Fn**. Eight fast clocks form one **slot** = 4 CPU clocks = 333.3 ns.

CPU clock C0 = F0+F1, C1 = F2+F3, C2 = F4+F5, C3 = F6+F7. CPU rising edges are E0, E2, E4,
E6. A zero-wait 68000 bus cycle starting at E0 has S0 at E0, S1 at E1, ... S7 at E7:

| state begins at | S0 | S1 | S2 | S3 | S4 | S5 | S6 | S7 | next S0 |
|---|---|---|---|---|---|---|---|---|---|
| edge | E0 | E1 | E2 | E3 | E4 | E5 | E6 | E7 | E0' |

68000 facts used below, from the M68000 User's Manual section 10.10, 12.5 MHz column
(worst case): address valid <= 50 ns after the S1 (clock-low) edge (#6); AS and DS asserted
3..40 ns after their state edge (#9; read: DS at S2, write: DS at S4); AS/DS negated <= 40 ns
after the S7 edge (#12); write data valid <= 50 ns after the S3 edge (#23), held >= 20 ns
after AS/DS negate (#25) and released <= 60 ns after the S7-ending clock edge (#7);
asynchronous inputs DTACK/BERR/VPA need 10 ns setup to the sampling falling edge (#47) and
are sampled at S4->S5 (E5 for a cycle starting at E0) and every following falling edge while
waiting; read data needs 10 ns setup to the S6->S7 falling edge (#27) and 0 ns hold (#29);
DTACK must be negated within 150 ns of AS negating (#28); RESET and HALT must be held 100 ms
at power-up. CPLD outputs change ~7.5 ns after their clock edge, and the CPU's own clock
lags the CPLD's internal edge by that much too.

### Slot schedule

| edge | CPLD1 action |
|---|---|
| E0 | WEU/WEL off, BUFEN off, IOCS off, DTACK(RAM/IO) off, RAMOE off (it was on for a CPU read). "Serviced" latch set if a cycle was serviced. |
| E1 | MA16..0 <- video address (VA); CPLD2: MA18..17 <- FB_PAGE_HI. |
| E2 | RAMOE on (video read) in fetch slots (DE); blanking slots skip the read. |
| E3 | LATCH <- 1 (tells CPLD2 to capture at E4). |
| E4 | LATCH <- 0. MA16..0 <- CPU address A1..A17; CPLD2: MA18..17 <- A18..A19. VA <- VA+1 if fetching. RAMOE stays on only if this slot services a CPU **read** of RAM, else off. **Decision point**: if AS is low, the address decodes to RAM or I/O, and this cycle has not been serviced, service it this slot: DTACK on. |
| E5 | If servicing: BUFEN on (direction from R/W), IOCS on (if I/O). |
| E6 | If servicing a write to RAM: WEU on if UDS low, WEL on if LDS low (gated combinationally by UDS/LDS, bounded by phase F6|F7). |
| E7 | nothing. |

CPLD2 on the same grid (its phase counter is synchronised by LATCH, which is high only
during F3): at E4 capture D and DE; at E0 capture D into a register if IOCS and write.

### Why this works (margins at 12 MHz, 70 ns SRAM, 74AHCT245)

(Worked for the original 70 ns KM681000; the AS6C4008-55 now fitted is faster on every
parameter: tAA 55, tOE 25, tWP 45, tDW 25, so each margin below grows by 10-15 ns.)

* Video read: MA valid at E1+7.5 = 49 ns; OE at E2+7.5 = 91. Data valid at
  max(49+70, 91+35) = 126 ns. CPLD2 captures at E4 = 166.7. **Margin 40 ns.**
* OE off at E4 -> SRAM Hi-Z by 199 ns. BUFEN on at E5+7.5+8 = 224. **Margin 25 ns.**
* CPU read: MA <- CPU at E4 (174), data at 244 + 10 (245) = 254; CPU samples at E7 (+7.5
  clock lag) - 10 setup = 289. **Margin 35 ns.**
* CPU write: WE low from E6+7.5 = 258 to E0+7.5 = 341 -> tWP 83 ns (>= 50). Data reaches the
  SRAM by 276 ns at the latest (see alignment cases) -> tDW 65 ns (>= 30). Address changes
  to VA at E1 -> tWR 41 ns. CPU holds data >= 0 ns after E0 and the transceiver adds 10 ns ->
  tDH >= 10 ns (>= 0).
* UDS/LDS for a write assert at S4: for a serviced cycle S4 is at E4 at the latest, so they
  are low by 214 ns, before the E6 WE gate (250). **Margin 36 ns.**
* AS is asserted no earlier than 3 ns after the CPU's clock edge, which itself lags the
  CPLD's edge, so a cycle whose S2 falls exactly on E4 is always sampled "AS high" at that
  E4 and serviced in the next slot: the decision is deterministic, never a race.
* ROM read: the flash chips' CE is tied low, so the only read strobe is ROMOE = region & AS
  & R/W, combinational. Address valid by 105 ns (S1 + 50), AS low by 131 ns (S0 at E0) ->
  data by max(105 + 70 tACC, 131 + 7.5 + 35 tOE) = 175 ns, sampled at 289. Zero wait states
  at any phase; the flash only ever drives the CPU bus (ROMOE is high whenever R/W is low).
* ROM write (flash programming): ROMWE = real-ROM region & AS & !R/W & (UDS | LDS),
  combinational, so it follows the data strobes: low from S4 + 47.5 ns, high 57.5 ns after
  the S7 edge -> tWP about 125 ns (>= 40). Write data is valid from S3 + 50, i.e. about 40
  ns before WE falls and 170 ns before it rises (tDS >= 40); the 68000 holds it >= 20 ns
  after DS negates (tDH 0). The address is stable from S1 to after S7 (tAH 30 after the WE
  falling edge). ROMOE is high throughout because R/W is low, so the chips never drive
  against the CPU's write data. The immediate DTACK makes it a 4-clock cycle.
* Transceiver B-side stops driving 22 ns after E0; next video OE at E2 (91 ns later).

### Alignment (the "cycle stealing" cost)

A RAM or I/O cycle is serviced in the first slot whose E4 finds AS already low. Because AS
asserts at most 47.5 ns after the S2 edge (7.5 clock lag + 40):

| S0 at | AS low by | serviced at | cycle length |
|---|---|---|---|
| E0 | 131 (this slot) | this slot | 4 clocks (zero wait) |
| E6 of previous slot | 48 (this slot) | this slot | 5 clocks (DTACK at E4 is first seen at E5) |
| E2 | 214 (after E4) | next slot | 7 clocks (S4 began at E6; DTACK at E4' is first seen at E5') |
| E4 | 298 | next slot | 6 clocks |

Every serviced cycle ends at E0, so after the first RAM or I/O access the CPU is realigned to
the slot. 68000 bus cycles then start on even CPU clocks relative to each other (cycles are 4
clocks, internal operations come in 2-clock units), so in steady state RAM accesses cost 4 or
6 clocks; the odd-phase cases (E2, E6) occur only once, coming out of reset. The TAS instruction (read-modify-write with AS held low) is not supported on RAM/IO: the serviced flag would block its write half, so firmware must not use TAS. ROM accesses
always take 4 clocks. Expect code in ROM with data in RAM to run roughly 10%
slower than an unshared system, code in RAM 15-25%. The video fetch itself never costs the
CPU a slot.

DTACK for a serviced cycle is asserted at E4 and recognised at the E5 falling edge, so S6
lands at E6 and the cycle ends at E0 regardless of where S0 was. During waits the CPU just
sits in S4 with the address and (for writes) data stable, which is what the schedule needs.

## 3. Address decode (CPLD1)

| region | condition | DTACK | notes |
|---|---|---|---|
| RAM 0x000000-0x0FFFFF | A23..A20 = 0 | slot-aligned | 1 MB, overlaid by ROM at reset |
| ROM 0xE00000-0xEFFFFF | A23..A20 = 1110 | immediate | reads: ROMOE; writes: ROMWE pulse to both flash chips (JEDEC command protocol, see below) |
| IO 0xF00000-0xF0FFFF | A23..A16 = 0xF0 | slot-aligned | CPLD2 registers, A1..A3 select |
| SWITCHES 0xF10000-0xF1FFFF | A23..A16 = 0xF1 | immediate | write performs action; A1..A2 select |
| DUART 0xF20000-0xF2FFFF | A23..A16 = 0xF2 | **from the DUART** | DUARTCS asserted; byte registers at odd addresses, A1..A4 -> RS1..RS4 |
| IACK | A23..A4 all 1 (and FC=7, not checked) | VPA instead | autovector |
| anything else | | **BERR** | bus error exception |

**Overlay**: a flip-flop set by SYSRESET. While set, the RAM condition is treated as ROM
(reads return flash contents, flash address = A1..A19; writes get DTACK but no ROMWE). It clears when AS rises at the end
of any cycle whose address is in the real ROM region. The reset vector's initial PC must
therefore point into 0xE0xxxx, and nothing else is required of software. The RESET
instruction does not reset the CPLDs, so the overlay does not come back under software.

**BERR** is asserted combinationally (AS & unmapped & !IACK) so the CPU sees it at its first
DTACK sample. The firmware's bus-error handler prints the faulting address.

**DTACK sharing**: DTACK is an open-drain bus with a 4.7 k pull-up. CPLD1 drives it (low,
then high again at the end of the cycle) for RAM, ROM, IO and SWITCHES cycles and releases it
(high-Z) while AS is low in the DUART region; the 68681's open-drain DTACK answers those
cycles, about 2-3 of its 3.6864 MHz clocks after DUARTCS (the CPU simply waits). The DUART
sits on the CPU bus (D0..D7), so BUFEN stays off and the video fetch is undisturbed.

**Soft switches** (0xF10000 + 2n): decoded from A1..A2 on a write; PAGE and PAL flip-flops.
PAGE takes effect when VA is reloaded at the start of the frame; PAL swaps the H/V
comparison constants at the start of the next frame.

**Flash programming**: the two SST39SF040 (512K x 8, one per byte lane, CE tied low) are
written through the JEDEC command sequence described in the `flash` section of
`spec/board.json` (constants `FLASH_*` in `generated/hw.h`): every command is a word write,
so both chips see it at once, and chip byte address N is CPU address ROM_BASE + 2N. The
glue adds nothing but the ROMWE strobe; the unlock sequence is the only write protection.
While a program or erase is in progress the chips return status instead of data, so the
routine that issues the commands runs from RAM with interrupts masked (the vector table and
the handlers live in ROM), and the NMI button must not be pressed meanwhile. The BIOS
image occupies the first 128 KB of the window (`ROM_IMAGE_SIZE`); the TL866 programs blank
chips with the even/odd images for the first boot, after that `flash` in the monitor
rewrites them in place.

## 4. Video (CPLD1 counters, CPLD2 pixel path)

* H counter counts **slots** 0..190 (NTSC) or 0..191 (PAL), advancing at E0. Slot 0..159 are
  active (DE high, fetch), then front porch, sync (HSYNC high), back porch per the spec.
* V counter counts lines, advancing at the slot where HSYNC begins. Lines 0..239 active.
  VSYNC high for the spec'd lines. VA reloads with the selected page base (as a word
  address, A1..A17) at the start of line 0 (the first active line; blanking lines never fetch).
  Address bits A19..A18 of every fetch are `FB_PAGE_HI`, driven by CPLD2 (section 1), so both
  pages must lie in the same 256 KB quarter; `tools/gen.py` checks this.
* DE is presented to CPLD2 **in the slot of the fetch** (so that CPLD2 captures data and DE
  together at E4). CPLD2: at E4 with LATCH high: `pix <= DE ? D[15:8] : 0; lo <= D[7:0];
  active <= DE;` and at E0: `pix <= active ? lo : 0`. Each pixel is therefore 4 fast clocks =
  166.7 ns; 320 pixels fill 53.3 us.
* HSYNC and VSYNC (positive logic inside, inverted at the pins as the AD724 wants) go to the
  AD724 which generates composite sync and burst. CPLD2 also receives them for the HSYNC
  and VBLANK interrupt flags (VBLANK flag = rising edge of VSYNC).
* The eight pixel bits drive three resistor DACs (3+3+2 bits) scaled to 0.714 V full scale
  into the AD724's RGB inputs.

## 5. CPLD2 peripherals

Registers are 16-bit at `IO_BASE + 2n`, selected by A1..A3; see the spec for bits.
Reads: CPLD2 drives D during F5..F7 of a serviced I/O read (IOCS low & R/W high). Writes:
CPLD2 latches D at E0 when IOCS low & R/W low. INTEN, GPIO and AUDIO are write-only (they
read as 0): every readable bit is a product term and a fan-in on the data-bus LAB, and the
fitter budget (section 7) does not allow them. SDIO reads back the six card lines (rev 7).

* **Interrupt controller**: flags VBLANK (edge of VSYNC), HSYNC (edge of HSYNC), PS2RDY,
  and the live DUART IRQ pin; enables in INTEN. IPL2..0 encodes the highest enabled pending
  level (1 VBLANK, 2 PS2, 3 DUART, 4 HSYNC); NMIBTN pressed forces level 7. VPA is asserted by CPLD1 on
  IACK so all vectors are autovectors (24 + level).
* **PS/2 receiver**: PS2_CLK synchronised to CLK24, falling-edge detect, 11-bit frame shift
  (start 0, 8 data LSB first, odd parity, stop 1). On the 11th bit: PS2DATA <- data, PS2RDY
  <- 1, PS2ERR <- frame/parity bad. The frame aborts if no PS/2 clock edge for 3 HSYNC
  periods (~190 us) so a glitch cannot desynchronise it permanently. Host-to-device
  transmission is bit-banged by software through GPIO.PS2CLK/PS2DAT (open-drain pins).
* **Serial**: not in the CPLDs. A 68681 DUART on the CPU bus provides channel A (FTDI
  header, 38400 8N1, the top of the chip's fixed baud table) and channel B (expansion
  header). Its open-drain IRQ is an input of CPLD2 and maps to level 3 when INTEN.DUART is
  set; INTSTAT.DUARTIRQ shows the pin. The DUART's IACK is tied high: all interrupts are
  autovectored and the ISR reads the DUART's own ISR register.
* **SD card** (spec rev 7, board rev C1): native 4-bit SD bus, bit-banged. The six lines
  CLK, CMD, DAT0..3 are open-drain outputs of CPLD2 (write 0 = drive low, 1 = release) with
  10 k pull-ups to 3.3 V, and SDIO reads their levels back, so the 3.3 V card needs no level
  dividers. Two clock pulses move a byte (a nibble per pulse) instead of eight in SPI mode.
  Rev B4 boards have SPI wiring instead (dividers on CS/CLK/MOSI, MISO straight in, DAT1/2
  only pulled up): see the `rev-b` tag.
* **Audio** (spec rev 5+): the AUDIO register loads an 8-bit rotating PDM shifter whose MSB
  drives the pin, one rotation at the HSYNC edge and every 24 slots (8 bits per line, about
  126 kHz) -> 1 k / 33 nF low-pass (4.8 kHz; rev B4 has 10 nF = 16 kHz, which passes most of
  the sigma-delta hiss) -> 10 k : 2.2 k divider -> 10 uF -> 3.5 mm jack. The BIOS
  streams pattern buffers from the HSYNC interrupt (audio_start/next/last); the SDK encodes
  8-bit samples with a sigma-delta table.
* **USERIO**: removed in spec rev 7; its four macrocells and pins went to the SD data lines.
  The expansion header keeps +5V, +3V3, GND and DUART channel B.

## 6. Reset

Open-drain supervisor (plus pushbutton) -> SYSRESET, held >= 150 ms at power-up. Diodes
from SYSRESET to the 68000's RESET and HALT (both pulled up separately, so the RESET
instruction cannot pull HALT). SYSRESET is also the asynchronous reset of both CPLDs
(overlay set, PAGE 0, NTSC, INTEN 0, GPIO released/idle, SDIO 0x3F = all six card lines released).

## 7. Pin and macrocell budget (ATF1508AS PLCC84: 60 I/O with JTAG kept, plus 4 input-only)

Measured with the Atmel fitter (hdl/README.md has the full ladder):

* CPLD1: 27 inputs (A1..A23, AS, UDS, LDS, RW) + 32 outputs (incl. DUARTCS) = 59 regular
  I/O of 60, plus CLK24 on GCLK1 and SYSRESET on GCLR. 98 of 128 macrocells before DUARTCS.
* CPLD2: 14 inputs (incl. A18, A19), 24 bidirectional (D0..D15, PS/2, six SD lines), 16
  outputs (incl. MA17, MA18) = 54 regular I/O. 107 of 128 macrocells at spec rev 4 (PS/2
  receiver, no serial engine, the two upper SRAM address bits); 119/128 with the PDM audio
  shifter (rev 5); 123/128 macrocells and 126/128 logic nodes with the six open-drain SD
  lines read back and USERIO removed (rev 7). CPLD2 is full.

History: the original plan (serial RX and PS/2 RX both in CPLD2, hardware serial TX
optional) does not fit. The fitter refuses the transmitter outright and lands at 127/128
without it even after the control registers were made write-only; removing small features
(ID, USERIO, a LED) buys two cells. Either frame engine had to leave, and the serial side
left for a 68681 (which also removes the interrupt-masking bit-banged transmitter).

## 8. Emulator timing model

Musashi runs 764 x 262 = 200,168 CPU clocks per NTSC frame (768 x 312 = 239,616 for PAL).
Per line: HSYNC flag at the line's sync start; VBLANK flag at the
first vsync line. RAM accesses may optionally add the alignment penalty (average +1 clock)
for realism; ROM accesses none. The 68681 is modelled at register level (FIFOs, status,
interrupts, counter/timer, bit-rate-timed transmit and receive); channel A is the emulator's
stdin/stdout, carrying the POST and banner and then whatever a program sends through
`/dev/ttya` (the BIOS console is video only after the banner, spec rev 8).
