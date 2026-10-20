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
   |  H/V timing -> HSYNC,VSYNC         |  VS    |  PS/2 rx, serial rx         |
   |  soft switches (page, PAL)         |        |  SD / GPIO / LED / audio    |
   +---+---------------+----------------+        +------+----------------------+
       |A1..A23,AS,     |MA0..16, RAMOE, WEU/WEL         | D0..D15 (SRAM-side bus)
       |UDS,LDS,RW      |                                |
   +---+------+     +---v----------------+    +----------+-----+
   |  68000   |     | 2x KM681000 SRAM   |====| 2x 74AHCT245   |==== CPU data bus ==== 2x 27C512 EPROM
   | 12 MHz   |==== | (SRAM-side bus)    |    | (BUFEN, dir=RW)|
   +----------+     +--------------------+    +----------------+
```

Two data buses:

* **CPU bus**: 68000, EPROMs, A-side of the transceivers.
* **SRAM-side bus**: SRAMs, CPLD2, B-side of the transceivers.

The transceivers are enabled only during the CPU half of a slot for RAM and I/O cycles, so
the video fetch (SRAM -> CPLD2) never shares wires with the CPU driving write data or the
EPROMs driving read data. This is what makes 12 MHz with 70 ns parts close cleanly.

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
| E0 | WEU/WEL off, BUFEN off, IOCS off, DTACK(RAM/IO) off. "Serviced" latch set if a cycle was serviced. |
| E1 | MA <- video address (VA). |
| E2 | RAMOE on (video read). |
| E3 | LATCH <- 1 (tells CPLD2 to capture at E4). |
| E4 | LATCH <- 0. MA <- CPU address A1..A17. VA <- VA+1 if fetching. RAMOE stays on only if this slot services a CPU **read** of RAM, else off. **Decision point**: if AS is low, the address decodes to RAM or I/O, and this cycle has not been serviced, service it this slot: DTACK on. |
| E5 | If servicing: BUFEN on (direction from R/W), IOCS on (if I/O). |
| E6 | If servicing a write to RAM: WEU on if UDS low, WEL on if LDS low (gated combinationally by UDS/LDS, bounded by phase F6|F7). |
| E7 | nothing. |

CPLD2 on the same grid (its phase counter is synchronised by LATCH, which is high only
during F3): at E4 capture D and DE; at E0 capture D into a register if IOCS and write.

### Why this works (margins at 12 MHz, 70 ns SRAM, 74AHCT245)

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
* ROM: CE = region & AS & R/W, combinational. AS low by 131 ns (S0 at E0) -> data by 208
  ns, sampled at 289. Zero wait states at any phase; the EPROM only ever drives the CPU bus.
* Transceiver B-side stops driving 22 ns after E0; next video OE at E2 (91 ns later).

### Alignment (the "cycle stealing" cost)

A RAM or I/O cycle is serviced in the first slot whose E4 finds AS already low. Because AS
asserts at most 47.5 ns after the S2 edge (7.5 clock lag + 40):

| S0 at | AS low by | serviced at | cycle length |
|---|---|---|---|
| E0 | 131 (this slot) | this slot | 4 clocks (zero wait) |
| E6 of previous slot | 48 (this slot) | this slot | 5 clocks (DTACK at E4 is first seen at E5) |
| E2 | 214 (after E4) | next slot | 6 clocks |
| E4 | 298 | next slot | 6 clocks |

Every serviced cycle ends at E0, so after the first RAM or I/O access the CPU is realigned to
the slot. 68000 bus cycles then start on even CPU clocks relative to each other (cycles are 4
clocks, internal operations come in 2-clock units), so in steady state RAM accesses cost 4 or
6 clocks; the odd-phase cases (E2, E6) occur only once, coming out of reset. ROM accesses
always take 4 clocks. Expect code in ROM with data in RAM to run roughly 10%
slower than an unshared system, code in RAM 15-25%. The video fetch itself never costs the
CPU a slot.

DTACK for a serviced cycle is asserted at E4 and recognised at the E5 falling edge, so S6
lands at E6 and the cycle ends at E0 regardless of where S0 was. During waits the CPU just
sits in S4 with the address and (for writes) data stable, which is what the schedule needs.

## 3. Address decode (CPLD1)

| region | condition | DTACK | notes |
|---|---|---|---|
| RAM 0x000000-0x03FFFF | A23..A18 = 0 | slot-aligned | overlaid by ROM at reset |
| ROM 0xE00000-0xE1FFFF | A23..A17 = 1110000 | immediate | read only; writes get DTACK and are ignored |
| IO 0xF00000-0xF0FFFF | A23..A16 = 0xF0 | slot-aligned | CPLD2 registers, A1..A3 select |
| SWITCHES 0xF10000-0xF1FFFF | A23..A16 = 0xF1 | immediate | write performs action; A1..A2 select |
| IACK | A23..A4 all 1 (and FC=7, not checked) | VPA instead | autovector |
| anything else | | **BERR** | bus error exception |

**Overlay**: a flip-flop set by SYSRESET. While set, the RAM condition is treated as ROM
(reads return EPROM contents, EPROM address = A1..A16). It clears when AS rises at the end
of any cycle whose address is in the real ROM region. The reset vector's initial PC must
therefore point into 0xE0xxxx, and nothing else is required of software. The RESET
instruction does not reset the CPLDs, so the overlay does not come back under software.

**BERR** is asserted combinationally (AS & unmapped & !IACK) so the CPU sees it at its first
DTACK sample. The firmware's bus-error handler prints the faulting address.

**Soft switches** (0xF10000 + 2n): decoded from A1..A2 on a write; PAGE and PAL flip-flops.
PAGE takes effect when VA is reloaded at the start of the frame; PAL swaps the H/V
comparison constants at the start of the next frame.

## 4. Video (CPLD1 counters, CPLD2 pixel path)

* H counter counts **slots** 0..190 (NTSC) or 0..191 (PAL), advancing at E0. Slot 0..159 are
  active (DE high, fetch), then front porch, sync (HSYNC high), back porch per the spec.
* V counter counts lines, advancing at the slot where HSYNC begins. Lines 0..239 active.
  VSYNC high for the spec'd lines. VA reloads with the selected page base (as a word
  address, A1..A17) at the start of line 0 (the first active line; blanking lines never fetch).
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
CPLD2 latches D at E0 when IOCS low & R/W low.

* **Interrupt controller**: flags VBLANK (edge of VSYNC), HSYNC (edge of HSYNC), PS2RDY,
  SERRX; enables in INTEN. IPL2..0 encodes the highest enabled pending level (1 VBLANK,
  2 PS2, 3 SERRX, 4 HSYNC); NMIBTN pressed forces level 7. VPA is asserted by CPLD1 on
  IACK so all vectors are autovectors (24 + level).
* **PS/2 receiver**: PS2_CLK synchronised to CLK24, falling-edge detect, 11-bit frame shift
  (start 0, 8 data LSB first, odd parity, stop 1). On the 11th bit: PS2DATA <- data, PS2RDY
  <- 1, PS2ERR <- frame/parity bad. Bit counter resets if no PS/2 clock edge for 4 HSYNC
  periods (~250 us) so a glitch cannot desynchronise it permanently. Host-to-device
  transmission is bit-banged by software through GPIO.PS2CLK/PS2DAT (open-drain pins).
* **Serial receiver**: fixed 115200 8N1. Start edge, sample at 1.5 then every 1.0 bit times
  (208.33 fast clocks per bit; use 208, +0.16%). SERDATA holding register, SERRX flag,
  SERRXOVR if a byte arrives before the previous was read.
* **Serial transmitter**: software bit-bang on GPIO.SERTX from ROM (ROM fetches are
  jitter-free; the GPIO write itself jitters 0/2 clocks, fine for 115200). If the fitter has
  room, a hardware transmitter behind SERDATA/SERTXBUSY may be added; firmware detects it
  by writing SERDATA and seeing SERTXBUSY go high.
* **SD card**: SDIO register pins. Software SPI mode 0. 3.3 V card; CS/CLK/MOSI via
  resistor dividers, MISO 3.3 V straight into a TTL-threshold input.
* **Audio**: GPIO.AUDIO -> 1 k / 10 nF low-pass -> 10 k : 2.2 k divider -> 10 uF -> 3.5 mm
  jack. Toggle it for beeps, or use the HSYNC interrupt at 15.7 kHz as a sample clock and
  output 1-bit PDM/PWM audio.
* **USERIO**: four open-drain pins with pull-ups on the expansion header.

## 6. Reset

Open-drain supervisor (plus pushbutton) -> SYSRESET, held >= 150 ms at power-up. Diodes
from SYSRESET to the 68000's RESET and HALT (both pulled up separately, so the RESET
instruction cannot pull HALT). SYSRESET is also the asynchronous reset of both CPLDs
(overlay set, PAGE 0, NTSC, INTEN 0, GPIO released/idle, SDIO CS high).

## 7. Pin budget (ATF1508AS PLCC84: 60 I/O with JTAG kept, plus 4 input-only)

CPLD1: 28 inputs (A1..A23, AS, UDS, LDS, RW, SYSRESET) + 31 outputs = 59 -> 56 regular I/O
with three inputs on input-only pins. Spare ~4 (candidate: composite sync out).

CPLD2: 54 signals -> ~51 regular I/O. Spare ~9.

Macrocell estimate: CPLD1 ~80 of 128 (17 VA + 17 mux + 8 H + 9 V + control). CPLD2 ~105
of 128 with hardware serial RX and PS/2 RX; hardware serial TX only if it fits.

## 8. Emulator timing model

Musashi runs 764 x 262 = 200,168 CPU clocks per NTSC frame (768 x 312 = 239,616 for PAL).
Per line: HSYNC flag at the line's sync start; VBLANK flag at the
first vsync line. RAM accesses may optionally add the alignment penalty (average +1 clock)
for realism; ROM accesses none. Serial TX is decoded from GPIO.SERTX edges using the cycle
counter at 104.17 cycles per bit.
