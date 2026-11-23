# Board revision 2: plan

Started 2026-11-23, after the first rev C3 board booted. **Nothing here is decided.** This
file collects what a second board has to fix, what it might add, and what it may cost, so the
decisions can be made in one place. Rev C3 stays the released board until this plan turns into
changes in `spec/board.json`, `kicad/` and `enclosure/`.

Each item says where its details live. The findings themselves stay in those files; this one
is the list.

## Constraints

* **Cost: under $100 for the boards**, counting the parts already in hand as free. The rev
  C3 order was $310.73 for two assembled boards (PCBs $103.27, parts $123.28, assembly
  $88.00; `kicad/ORDER.md`), so this needs real cuts. See "Cost" below.
* **Fabricator: probably JLCPCB.** `kicad/fab_export.py` already writes a JLCPCB BOM and
  placement file (`kicad/release/fab/other-m68k-jlcpcb-*.csv`); they have not been used for
  an order.
* **Every footprint checked against a real part or its drawing before ordering.** One
  footprint on rev C3 was wrong (J5). See "Footprint check".
* **Both CPLDs are nearly full** (CPLD1 111 of 128 macrocells with no pin left, CPLD2 123 of
  128 with six pins left; `hdl/README.md`). Anything that needs new logic or new CPLD pins
  has to make room first.

## 1. Fixes rev 2 must have

| # | item | what is wrong on rev C3 | where the detail is |
|---|---|---|---|
| 1 | J5 composite jack, pads | shell carries the video, centre is ground | `kicad/README.md`, "from video bring-up" |
| 2 | J5 composite jack, part | the jacks in hand have toes that overhang the board edge; another jack is to be chosen | `enclosure/README.md`, "First assembly" item 2 |
| 3 | J3 PS/2 and J10 S-video footprints | drawn the same way as J5 and never checked; the keyboard does not respond yet | `kicad/README.md`; `hdl/STATUS.md`, "Open on the first rev C3 board" item 4 |
| 4 | JTAG TCK | in-circuit programming failed once CPLD1 was running; long unterminated trace | `kicad/README.md`, "from CPLD bring-up" |
| 5 | J7 JTAG header | ByteBlaster pinout does not suit the FT232H; wants GND, TCK, TDI, TDO, TMS in a row | same |
| 6 | J9 analyzer header | wrong size, mirrored columns and ground on the wrong row for the analyzer in hand | same; see also "Debug header" below |
| 7 | scope grounds | one ground pad on the whole board; add wire-loop ground points in each area | same |
| 8 | J2 and J11 serial headers | cannot be reached inside the case | `kicad/README.md`, "from the first case assembly"; "Serial" below |
| 9 | J4 microSD | too far from the edge to latch or release a card through the wall | `enclosure/README.md`, "First assembly" item 4 |
| 10 | J6 audio jack | nose passes the board edge; the case opening is closed under it (a case change alone can fix this) | same, item 5 |
| 11 | case joining | lip has no lead-in, round openings trap connectors | same, item 1 (case only) |

Not board faults, but open on rev C3 and likely to shape rev 2 (all in `hdl/STATUS.md`,
"Open on the first rev C3 board"):

* **Cross-colour on text** over composite. See "Video" below.
* **Picture wider than the screen.** On the monitor used, the left edge is lost and the
  right edge shows no black, so it is cut on both sides. See "Video" below.
* **PS/2 keyboard silent.** Not investigated yet; the result may add a fix to the table.

Two faults found on rev C3 were in the CPLD images, not the board, and are fixed there
(`hdl/STATUS.md`): CPUCLK stopping during reset, and the DTACK enable the fitter built wrong.
Rev 2 needs nothing for them.

## 2. Changes under consideration

### Video

Three separate problems, which may have one answer or three.

* **Cross-colour.** Pixels are 6 MHz, so one-pixel strokes are 3 MHz luminance next to the
  3.58 MHz subcarrier, and the subcarrier is not locked to the line, so the false colour
  crawls. Options, cheapest first:
  1. A wider font or fewer columns for composite (firmware only; works on rev C3).
  2. Use S-video where the display has it (no change; J10 has to be verified first).
  3. A luminance-only mode for text: a switch that feeds the composite jack from the
     AD724's LUMA output instead of COMP. One analog switch and one control line. A CPLD2
     pin is available; a soft switch or register bit has to be found for it.
  4. Suppress the colour burst from CPLD1 by changing the HSYNC width. No board change,
     but unproven: the datasheet does not give the width limits, and the chip's input clamp
     runs during the burst period.
  5. A subcarrier locked to the line rate, which stops the crawl but not the false colour.
     It needs the subcarrier derived from the system clock, so it goes with a new encoder.
* **Picture width.** The active line is 53.3 us and standard is 52.7 us, with nothing to
  spare for overscan. The pixel rate is fixed by the bus slot (two pixels per 333 ns), so
  the line cannot be made narrower without changing the timing contract in
  `docs/architecture.md`. The practical options are a margin in the console (firmware only)
  and, for a small shift, moving slots between the porches in `spec/board.json`.
* **The encoder itself.** The AD724 is on last-time-buy (DigiKey: 2027-07-29, about $25;
  `kicad/ORDER.md`). Alternatives: keep it and add the luma switch (small, known), or
  replace it with a circuit built from parts we control. A discrete encoder has to make the
  subcarrier, the burst and the two modulated colour-difference signals, and the CPLDs have
  almost no room to help, so it is a design project of its own with its own bring-up risk.
  In hand (2026-11-23): two AD724s not yet soldered, three bought in all. That covers two
  more boards, so rev 2 does not have to replace the encoder; whether it should depends on
  the RP2350 question below, which could make composite the second output instead of the
  only one.

### Serial

* **Channel A on the USB-C connector.** A USB-serial bridge on J1's data pins, so one cable
  carries power and the console. J1 becomes a USB 2.0 receptacle (it is power-only now, with
  the two 5.1 k CC resistors already fitted). To decide: the bridge chip (availability and
  price at the assembler matter more than the brand), whether the J2 header stays as a
  fallback, and what the bridge does while the board is held in reset.
* **Channel B as RS-232.** A DE-9 and a 5 V RS-232 transceiver, so a serial mouse plugs in
  directly and gets its power from RTS/DTR. This replaces the adapter described in
  `kicad/README.md` ("Serial mouse"). It adds a second DE-9 next to the joystick port, so the
  two need different genders or clear labelling, and the case needs another opening.

### Debug header

Replace J9 with one or two connectors the analyzer plugs straight into.

* The analyzer's level-shifter board has a right-angle 2x16 pin header (24 channels, then
  3V3, 5V0, TRG/VRF and GND rows). A direct plug needs a 2x16 **socket** at a board edge,
  with the rows and columns matched to the analyzer by meter, not from a photo.
* Two sockets give 48 channels. To decide: the signal list (rev C3 carries 24: CPUCLK, AS,
  the strobes, DTACK, chip selects, sync and a few address lines), and whether the second
  socket is fitted or left as an empty footprint.
* Both sockets want a clear edge and room for the analyzer boards side by side; that
  competes with the connectors already on the back and front edges and with the case.
* Keep the 100 ohm series resistors. The level shifters handle 5 V.

### An RP2350 as the board's peripheral adapter (proposed 2026-11-23, to be reviewed)

The idea: one RP2350 takes over whatever it can. Candidates: the serial console as a USB
device, a USB host port for keyboard and mouse, perhaps audio, and as a stretch a digital
video output. It would stand in for three parts otherwise on the list (USB-serial bridge,
RS-232 transceiver with its DE-9, and possibly the PS/2 jack).

What follows is a first assessment from memory of the RP2350 and its software, not checked
against the datasheet. Each "to check" is a real open point.

**The low-risk form: a protocol converter.** The 68000 side keeps the interfaces it has, and
the RP2350 speaks them:

| job | RP2350 side | 68000 side | changes to CPLDs or BIOS |
|---|---|---|---|
| console | USB device (serial port) on the USB-C connector | TTL serial to DUART channel A | none |
| keyboard | USB host | PS/2 clock and data into CPLD2, as a keyboard would drive them | none |
| mouse | USB host (same port, through a hub) | Microsoft serial mouse bytes at 1200 baud into DUART channel B | none beyond the channel-B mouse driver already planned |

* The chip has one USB controller, so device and host cannot both use it. The usual answer
  is the native port as the device and a second, software-driven port on two ordinary pins
  as the host (full and low speed, enough for keyboards and mice). To check: that this
  works on the RP2350 with a hub, and what it costs in processor time.
* The host port has to supply 5 V with a current limit; a USB-A socket and a small switch
  or polyfuse.
* The DUART and the PS/2 lines are 5 V. The RP2350's pins are described as 5 V tolerant
  while its I/O supply is up, with exceptions. To check: which pins, and the early-stepping
  erratum about inputs with pull-downs.
* The emulator and the user's guide are unaffected: the machine still has "a keyboard" and
  "a serial port".
* A real PS/2 keyboard or a real serial mouse could still be supported by keeping the jack
  or a header in parallel, if that matters. To decide.

**The stretch: digital video.** The RP2350 has a serialiser that can drive a DVI display
through an HDMI connector. It would take the picture from the pixel bus CPLD2 already drives
to the resistor DACs (eight bits, plus HSYNC, VSYNC and a pixel clock), hold a frame, and
send it doubled as 640 x 480. That is an exact 2x of 320 x 240.

* It removes both composite problems for anyone with a digital display: no cross-colour and
  no overscan. Composite through the AD724 stays for a television.
* It needs about eleven input pins and eight output pins on top of the adapter jobs, which
  points to the 48-pin-I/O version of the chip. To check: the pin budget, and that capture
  at 6 MHz and output can run together.
* The input is 59.95 Hz and non-standard; the output would be timed to it or would repeat a
  frame now and then. To check on a display.
* The board cost is a connector and footprints. The firmware can come later; fitting the
  footprints in rev 2 and leaving the feature for afterwards is a reasonable split.

**Audio.** Not a natural fit for the converter form: the sample stream is inside CPLD2 (one
byte per video line), and the RP2350 would need either that byte stream brought out or a
place on the 68000 bus. Leave audio where it is unless the RP2350 goes on the bus.

**The other form: on the 68000 bus.** The RP2350 could instead answer bus cycles as a
peripheral (data bus, a few address lines, a select, DTACK). That opens audio and anything
else, but it needs about fifteen more pins, a new register block in the spec, the emulator
and the BIOS, and bus timing proven on the bench. Not recommended for rev 2.

**Costs of the idea.**

* A second firmware (C, on the Pico SDK), with its own build, its own flashing and its own
  faults. It is the first part of this machine that is not generated from `spec/board.json`.
* A choice between a Pico 2 module on headers (cheap, in hand or easy to get, no fine-pitch
  assembly, but its own USB connector and a fixed 26 I/O) and the bare chip with its flash,
  crystal and regulator parts (fabricator-assembled, full pin count, uses the board's own
  USB-C).
* Character: a processor far faster than the 68000 sits on a 1980s-style board. As a
  converter it does nothing the 68000 can see, which keeps the machine honest; on the bus it
  would not.

**Leaning (2026-11-23, not decided):** digital video through an HDMI connector, and USB for
keyboard and mouse.

**Recommendation so far:** the converter form for console, keyboard and mouse; footprints
and pins for digital video with the firmware to follow; audio unchanged. This would drop
the USB-serial bridge and the RS-232 port from the serial section above.

### Smaller things worth doing while the board is open

* Test points for the nets that were probed during bring-up: RESET, HALT, DTACK, DUARTCS,
  the DUART crystal, FIN.
* Silkscreen pin-1 and signal names on every header.
* A meter-checkable pinout note in `kicad/README.md` for each connector, written when the
  footprint is verified.

## 3. Footprint check

Method for each row: compare the footprint with the manufacturer's land pattern, print the
board 1:1 and lay the part on it, and for connectors check each pin number against the
netlist with a meter on a real part. Record the result here.

"Works" below means the first rev C3 board works with that part fitted, which proves the
pads and the pin numbering but not the fit in the case.

| refs | part | footprint | state |
|---|---|---|---|
| J5 | RCJ-041 RCA | project: `RCA_CUI_RCJ-041_Horizontal` | **wrong**: pads swapped; part to be replaced |
| J3 | MD-60SM mini-DIN 6 | project: `MiniDIN-6_CUI_MD-60SM_Horizontal` | unchecked; keyboard not working |
| J10 | MD-40SM mini-DIN 4 | project: `MiniDIN-4_CUI_MD-40SM_Horizontal` | unchecked; S-video not tried |
| J4 | DM3AT-SF-PEJM5 microSD | KiCad library | untested; position wrong for the case |
| J6 | SJ1-3523N audio jack | KiCad library | untested; nose passes the edge |
| J12 | L717SDE09PA4CH4F DE-9 | KiCad library | untested |
| J1 | USB4125-GF-A-0190 USB-C | KiCad library | works (power only); changes if it gains data |
| J2 | 1x6 header | KiCad library | works (serial A) |
| J11 | 1x6 header | KiCad library | untested |
| J7, J9 | 2x5, 2x15 headers | KiCad library | work; both to be replaced |
| U1 | DIP-64 socket | KiCad library | works |
| U2-U5 | DIP-32 sockets | KiCad library | work |
| U6, U7 | PLCC-84 sockets | KiCad library | work |
| U10 | DIP-40 socket | KiCad library | works |
| U8, U9 | SN74AHCT245DW, SOIC-20W | KiCad library | work |
| U11 | AD724JRZ, SOIC-16W | KiCad library | works |
| U12, U13 | AP2112K (SOT-23-5), MCP130T (SOT-23) | KiCad library | work (3.3 V present, reset works) |
| U14, Q1, D6-D8 | 74HC14, MMDT3904, BAT54W | KiCad library | untested (paddle and serial B circuits) |
| X1, Y1, Y2 | oscillator, two HC-49 crystals | KiCad library | work |
| C37-C39, C51 | 6.3 mm radial electrolytics | KiCad library | C37 works (composite); others untested |
| SW1, SW2 | B3F-1000 | KiCad library | SW1 works; SW2 untested |
| passives, LEDs, fuses, arrays | 0603, 0805, 1206, 4x0603 | KiCad library | assembled by the fabricator; no faults seen |

Every part new in rev 2 (USB-serial bridge, RS-232 transceiver, second DE-9, analyzer
sockets, the new RCA jack, any video switch) gets a row before it gets a footprint.

## 4. Cost

What rev C3 cost is in `kicad/ORDER.md`. What rev 2 will cost has to be quoted; no figure
here is a quote. The levers, largest first as far as I can judge:

* **Assembly scope.** Rev C3 had the fabricator fit the sockets, connectors and all
  through-hole parts. Fitting only the surface-mount passives and small ICs, and soldering
  sockets, headers, jacks and electrolytics by hand, removes most of the assembly charge and
  the dear connector lines (three connectors were quoted at about $9 each against $2 to $4
  at a distributor).
* **Board size and layers.** 180 x 125 mm on four layers. Fabricators price small boards
  much lower, and the usual step is at 100 x 100 mm, which this board cannot reach with DIP
  sockets and a DIP-64. Two layers would be cheaper but give up the ground and power planes
  under a 24 MHz bus; not recommended without a reason.
* **Parts the assembler stocks.** At JLCPCB, parts from its basic list avoid a per-part
  loading fee. Choosing the new parts (bridge, transceiver, switch) and re-choosing the
  existing small ones from that list is worth a pass.
* **Quantity.** The minimum order is usually five bare boards, with assembly on two or
  more. One assembled board and four bare ones may not be the cheapest combination.
* **Parts in hand.** To be listed: which of the socketed chips, crystals, connectors and
  sockets are already here for a second board, and how many AD724s.

## 5. Order of work

1. Finish bring-up on rev C3 (keyboard, SD card, audio, joystick), because each stage can
   add a row to the fixes.
2. Verify J3 and J10 by meter; fill in the footprint table.
3. Review the RP2350 proposal (check its open points against the datasheet), then decide
   the video question (AD724 with a luma switch, digital video, a new encoder), since
   together they set how much of the board changes.
4. Decide serial (USB bridge, RS-232) and the debug header signal list.
5. Get a JLCPCB quote for the unchanged rev C3 files, hand-fitted parts excluded, as the
   cost baseline.
6. Only then: `spec/board.json`, `kicad/board.py`, the enclosure, and the guide's board
   tour and pinouts.
