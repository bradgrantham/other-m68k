# Ordering other-m68k rev C3 from PCBWay

This file describes the **rev C3** order (2026-10-21; the rev B4 order of 2026-10-20, git tag
`rev-b`, was stopped before fabrication). Rev C3 is the current design in this directory, spec
rev 11:

* **SD card** (rev C1, spec rev 7): native 4-bit bus. CLK, CMD, DAT0..3 run straight to CPLD2
  pins 15, 24, 8, 16, 58, 4 with 10 k pull-ups to 3.3 V; the three 1.8k/3.3k level dividers of
  rev B4 are gone.
* **Audio low-pass** (rev C1): C31 is 33 nF (4.8 kHz corner) instead of 10 nF.
* **Front and back** (rev C2): the sheet's bottom edge is the front: J3 PS/2, J6 audio, J4
  microSD, then SW1 RESET and SW2 NMI with the LEDs D5 PWR, D3 LED0, D4 LED1 behind them, all
  labelled on the silkscreen. The top edge is the back: J1 USB-C, J2 serial A, J11 serial B,
  J5 composite, J10 S-video. J9 (logic analyzer) and J7 (JTAG) stand on the left edge.
* **S-video** (rev C2): J10, a CUI MD-40SM mini-DIN 4, fed from the AD724's LUMA and CRMA
  outputs through 220 uF and 75 ohm each (two more electrolytics and two resistors).
* **Serial B** (rev C2): J11, a second 6-pin FTDI header on DUART channel B, replaces the 2x8
  expansion header J8.
* **Joystick/paddle port** (rev C3, spec rev 11): J12 DE-9 male (Atari pinout) on the front edge
  between the SD socket and the buttons, with U14 74HC14, Q1 MMDT3904, D6/D7 BAT54W, two 15 nF,
  F2 200 mA polyfuse and nine resistors. The six new part numbers were checked against distributor
  listings on 2026-10-22 (DigiKey/Mouser/Element14/Future): the DE-9 is Amphenol's .318 in backset
  family, which is the footprint used (DigiKey lists it as L717SDE09PA4CH4FA610); the 15 nF Samsung
  capacitor is at Avnet/Future and any 0603 X7R 15 nF 50 V may stand in; same package and ratings
  are acceptable substitutes for the rest.
* The silkscreen carries a dedication under the SRAMs, right of the bus buffers ("Griffin Jr.",
  "October 2026", "I love you, Kris!") and the board revision.

The rev B4 notes below (quantities, stackup, assembly scope, part substitutions, power) still
apply; the fab package in `release/fab/` is regenerated from the rev C2 board.

Decisions taken 2026-10-20. Everything below is produced by `make -C kicad fab` into
`kicad/build/fab/`; upload the Gerber zip for the boards and the PCBWay BOM + centroid for
assembly.

## Boards

| parameter | value |
|---|---|
| quantity | 5 bare boards, 1 of them assembled |
| size | 180 x 125 mm, 3 mm corner radius, four M3 holes (unchanged in rev C2) |
| layers | 4: F.Cu signal, In1 GND plane, In2 VCC plane, B.Cu signal (PCBWay standard 4-layer stackup) |
| thickness / copper | 1.6 mm, 1 oz outer and inner |
| finish | lead-free HASL |
| mask / silk | green / white (defaults) |
| min track / clearance used | 0.25 mm / 0.2 mm; vias 0.8 / 0.4 mm; no blind or buried vias |
| files | `other-m68k-gerbers.zip` (Gerbers, Excellon drills PTH + NPTH, drill maps, job file) |

## Assembly

| item | value |
|---|---|
| BOM | `other-m68k-pcbway-bom.csv` (Item, Designator, Qty, Manufacturer, Mfg Part #, Description, Package, Type SMD/THT, Supplied By) |
| placement | `other-m68k-pcbway-centroid.csv` (KiCad convention: mm, Y up, KiCad rotation, top side only) |
| scope | SMD **and** through-hole: passives, LDO, AD724, supervisor, 74AHCT245 (SOIC), connectors, headers, jacks, switches, crystals, oscillator, electrolytics, every IC socket (plain DIP/PLCC sockets, no ZIF), and four memory chips that PCBWay buys and inserts into their sockets: the two AS6C4008-55PCN SRAMs (U2/U3) and the two SST39SF040 flash chips (U4/U5, inserted blank) |
| consigned | none. U11 (AD724JRZ) is in the BOM as PCBWay-supplied; it is a last-time-buy part (DigiKey LTB date 2027-07-29, about 50 in stock at $25, Mouser/Farnell/Newark also stock it). If PCBWay's BOM review cannot source it, have them leave U11 unfitted and hand-solder it (SOIC-16W, 1.27 mm pitch). Buy spares for the bare boards while stock lasts. |
| customer fits afterwards | `other-m68k-customer-parts.csv`: U1 MC68000P12, U6/U7 ATF1508AS-7JX84, U10 MC68681P, all into their sockets |
| first firmware | the flash chips arrive blank: program `fw/build/rom_even.bin` into U4 (D15..D8) and `rom_odd.bin` into U5 (D7..D0) with the TL866 (SST39SF040, 512 KB images, built by the top-level `make flash`; they carry the 256 KB BIOS image and the C: volume with AUTOEXEC.CMD and the example programs), then later BIOS updates with the monitor's `flash D:ROM.BIN` from the SD card (C: is left intact) |
| not fitted | mounting holes, test pads, the crystal trim capacitor (DNP) |
| loose parts | none (the PAL jumper is gone; no shunts) |

All part numbers were checked against distributor listings on 2026-10-20 (`parts.json` has no
`verify` flags left; `other-m68k-bom-check.txt` lists none). Substitutions made then: Lite-On
LTST-C171GKT/KRKT LEDs (the Everlight 19-217 parts are 0603, the footprint is 0805), Yageo
YC164 arrays (YC124 is the 4x0402 size), Rubycon 35ZLH100MEFC6.3X11 for the 100 uF bulk
capacitor (the 16 V part only comes in a 5 mm can), Mill-Max 110-43 series DIP sockets (gold
contacts, RoHS; the 110-99 codes are tin-lead), ECS-36-20-4XDN. Rev 4 (same day) replaced the
256 KB of KM681000 SRAM with two AS6C4008-55PCN (512Kx8, 55 ns, 2.7-5.5 V; DigiKey, Mouser and
Future all stock it) in the same dual-wipe DIP-32 sockets as the flash. If PCBWay proposes
equivalents in the quote review, same package and ratings are acceptable.

## Power note for the order and for use

The board draws about 1 A at 5 V (68000, 68681, two ATF1508AS, SRAM, flash, AD724,
keyboard, SD card). It is fed through USB-C with 5.1 k CC pull-downs and a 2 A polyfuse, and
the reset supervisor releases at 4.5 V. Use a USB-C wall supply or powered hub, not a
laptop port: the board has no USB-PD or current-advertising logic and relies on the source
allowing more than the 500 mA default.

## Test access

J9 is a 2x15 header with the pinout of gusmanb's LogicAnalyzer channel header (pin 1 = CH0,
pin 2 = CH23, alternating; 25/26 GND; 27-30 unconnected). Every channel passes through 100
ohm. Channels: 0 CPUCLK, 1 AS, 2 UDS, 3 LDS, 4 R/W, 5 DTACK, 6 BERR, 7 VPA, 8-10 IPL0-2,
11 SYSRESET, 12 ROMOE, 13 RAMOE, 14 WEL, 15 WEU, 16 DUARTCS, 17 BUFEN, 18 HSYNC, 19 VSYNC,
20-23 A1-A4. The signals are 5 V TTL: use the analyzer's level-shifter board. Test pads:
TP1 composite video at the jack, TP2 +3.3 V, TP3 +5 V, TP4 GND.
