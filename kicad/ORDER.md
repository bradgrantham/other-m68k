# Ordering other-m68k rev B from PCBWay

Decisions taken 2026-10-20. Everything below is produced by `make -C kicad fab` into
`kicad/build/fab/`; upload the Gerber zip for the boards and the PCBWay BOM + centroid for
assembly.

## Boards

| parameter | value |
|---|---|
| quantity | 5 bare boards, 2 of them assembled |
| size | 180 x 125 mm, 3 mm corner radius, four M3 holes |
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
| scope | SMD **and** through-hole: passives, LDO, AD724, supervisor, 74AHCT245 (SOIC), connectors, headers, jacks, switches, crystals, oscillator, electrolytics, every IC socket (plain DIP/PLCC sockets, no ZIF), and the two SST39SF040 flash chips, which PCBWay buys and inserts blank into their sockets |
| consigned | AD724JRZ (U11): last-time-buy part, we buy five and ship them to PCBWay |
| customer fits afterwards | `other-m68k-customer-parts.csv`: U1 MC68000P12, U2/U3 KM681000BLP-7, U6/U7 ATF1508AS-7JX84, U10 MC68681P, all into their sockets |
| first firmware | the flash chips arrive blank: program `fw/build/rom_even.bin` into U4 (D15..D8) and `rom_odd.bin` into U5 (D7..D0) with the TL866 (SST39SF040, 512 KB images), then later updates with the monitor's `flash ROM.BIN` from the SD card and `reboot` |
| not fitted | mounting holes, test pads, the crystal trim capacitor (DNP) |
| loose parts | none (the PAL jumper is gone; no shunts) |

All part numbers were checked against distributor listings on 2026-10-20 (`parts.json` has no
`verify` flags left; `other-m68k-bom-check.txt` lists none). Substitutions made then: Lite-On
LTST-C171GKT/KRKT LEDs (the Everlight 19-217 parts are 0603, the footprint is 0805), Yageo
YC164 arrays (YC124 is the 4x0402 size), Rubycon 35ZLH100MEFC6.3X11 for the 100 uF bulk
capacitor (the 16 V part only comes in a 5 mm can), Mill-Max 110-43 series DIP sockets (gold
contacts, RoHS; the 110-99 codes are tin-lead), ECS-36-20-4XDN. If PCBWay proposes
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
