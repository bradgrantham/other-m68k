#!/usr/bin/env python3
"""board.py - the other-m68k single-board computer as a gen_sch.Board.

    python3 board.py [outdir]      writes outdir/other-m68k*.kicad_sch, .kicad_pro, lib tables

Sources of truth (read these before changing anything here):
  spec/board.json, generated/HARDWARE.md       what the board does
  docs/architecture.md                          buses, DTACK sharing, reset (section 6)
  docs/video-encoder.md                         AD724 wiring and DAC resistor values
  hdl/pins_cpld1_mmu.json, hdl/pins_cpld2_io.json  CPLD signal -> PLCC84 pin (loaded below)
  data sheets                                   every pin number below was checked against
                                                MC68000UM (DIP-64), MC68681 Table 2-1,
                                                AD724 Rev B, ATF1508AS pinout tables, SN74AHCT245

Net naming: active-low signals keep the plain spec name (AS, DTACK, ROMOE, SYSRESET ...);
the polarity is documented in the spec.  D0..D15 is the CPU data bus, MD0..MD15 the SRAM-side
bus (SRAMs, CPLD2, transceiver A side), MA0..MA16 the SRAM word address from CPLD1.

Decisions taken here that the documents leave open (also listed in kicad/README.md):
  * 74AHCT245 sides: DIR = R/W straight from the CPU, so (DIR high = A->B per the TI
    function table) a CPU read (R/W high) must move A->B = SRAM->CPU: A side = SRAM-side
    bus, B side = CPU bus.  docs/architecture.md says "direction from R/W"; the testbench
    models dir = ~rw with A = CPU, which is the same thing.
  * Flash CE tied to GND; CPLD1 drives OE (ROMOE, reads) and WE (ROMWE, writes: in-system
    programming, spec rev 3).  Data 35 ns after OE instead of 70 ns after CE.
  * AD724 ENCD tied straight to +5 V (as in the data sheet's typical circuit; the doc says
    "through 10 k" but the KiCad AD725 stand-in types that pin power_in).
  * RGB 75 ohm terminations sit on the DAC side of the 0.1 uF coupling capacitors (they are
    the DAC load the resistor values in docs/video-encoder.md were computed for; the data
    sheet terminates at the source too).
  * PALJMP: tied low through 10k (NTSC only; the board has no PAL jumper), STND pulled high.
  * Unused 68681 inputs IP0..IP5: 10 k to VCC each (data sheet: tie unused inputs).
    Crystal: 15 pF on X1 to GND, 0-5 pF on X2 marked DNP (data sheet section 2.2/2.3).
  * ATF1508 dedicated inputs OE1 (84) and OE2/GCLK2 (2) are unused: tied to GND.
  * JTAG header: 1 TCK, 2 GND, 3 TDO, 4 VCC, 5 TMS, 6 GND, 7 n/c, 8 GND, 9 TDI, 10 GND.
  * Expansion header (2x8, odd row = signals): 1 +5V, 3 +3V3, 5/7/9/11 no connect (the
    UIO bits went in spec rev 7 to make room in CPLD2 for the 4-bit SD bus), 13 TXDB,
    15 RXDB; every even pin is GND.
  * FTDI header: VCC pin left open (board is USB-C powered); CTS/RTS open.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from gen_sch import Board  # noqa: E402

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
CP_RADIAL = "Capacitor_THT:CP_Radial_D6.3mm_P2.50mm"
LED0805 = "LED_SMD:LED_0805_2012Metric"
SOD123 = "Diode_SMD:D_SOD-123"


def cpld_pins(name):
    with open(os.path.join(HERE, "..", "hdl", f"pins_{name}.json"), encoding="utf-8") as f:
        return {k: str(v) for k, v in json.load(f).items()}


class Counter:
    def __init__(self, prefix):
        self.prefix, self.n = prefix, 0

    def __call__(self):
        self.n += 1
        return f"{self.prefix}{self.n}"


def build():
    b = Board("other-m68k", title="other-m68k: 68000 SBC with 68681 DUART, framebuffer, PS/2, SD, audio",
              rev="C1")
    for name, title in (("cpu", "68000 CPU"), ("memory", "SRAM, EPROM, bus transceivers"),
                        ("cpld1", "CPLD1 MMU, clock, JTAG"), ("cpld2", "CPLD2 I/O, PS/2, SD, audio, LEDs"),
                        ("video", "AD724 composite encoder"), ("io", "DUART, serial, headers, reset"),
                        ("power", "USB-C power, 3.3 V LDO")):
        b.sheet(name, title)

    newR, newC, newD = Counter("R"), Counter("C"), Counter("D")

    def R(value, sheet, near=None):
        return b.part(newR(), "Device", "R", R0603, value, sheet=sheet, **({"PcbNear": near} if near else {}))

    def C(value, sheet, near=None, fp=C0603, symbol="C", dnp=False):
        return b.part(newC(), "Device", symbol, fp, value, sheet=sheet, dnp=dnp,
                      **({"PcbNear": near} if near else {}))

    def decouple(part, pins, sheet, bulk=None):
        """One 100 nF per VCC pin (pins = list of pin numbers), optional bulk capacitor."""
        caps = []
        for p in pins:
            c = C("100nF", sheet, near=part.ref)
            b.power("VCC", c.pin(1))
            b.power("GND", c.pin(2))
            caps.append(c)
        if bulk:
            c = C(bulk, sheet, near=part.ref, fp=C0805)
            b.power("VCC", c.pin(1))
            b.power("GND", c.pin(2))
            caps.append(c)
        return caps

    # ------------------------------------------------------------------ parts
    u1 = b.part("U1", "CPU_NXP_68000", "MC68000P", "Package_DIP:DIP-64_W22.86mm_Socket", "MC68000P12", sheet="cpu")
    u2 = b.part("U2", "Memory_RAM", "AS6C4008-55PCN", "Package_DIP:DIP-32_W15.24mm_Socket", "AS6C4008-55PCN (upper, D15..D8)", sheet="memory")
    u3 = b.part("U3", "Memory_RAM", "AS6C4008-55PCN", "Package_DIP:DIP-32_W15.24mm_Socket", "AS6C4008-55PCN (lower, D7..D0)", sheet="memory")
    u4 = b.part("U4", "Memory_Flash", "SST39SF040", "Package_DIP:DIP-32_W15.24mm_Socket", "SST39SF040-70 (even, D15..D8)", sheet="memory")
    u5 = b.part("U5", "Memory_Flash", "SST39SF040", "Package_DIP:DIP-32_W15.24mm_Socket", "SST39SF040-70 (odd, D7..D0)", sheet="memory")
    u6 = b.part("U6", "CPLD_Microchip", "ATF1508ASx-xxJx84", "Package_LCC:PLCC-84_THT-Socket", "ATF1508AS-7JX84 (MMU)", sheet="cpld1")
    u7 = b.part("U7", "CPLD_Microchip", "ATF1508ASx-xxJx84", "Package_LCC:PLCC-84_THT-Socket", "ATF1508AS-7JX84 (I/O)", sheet="cpld2")
    u8 = b.part("U8", "74xx", "74HC245", "Package_SO:SOIC-20W_7.5x12.8mm_P1.27mm", "74AHCT245 (hi)", sheet="memory")
    u9 = b.part("U9", "74xx", "74HC245", "Package_SO:SOIC-20W_7.5x12.8mm_P1.27mm", "74AHCT245 (lo)", sheet="memory")
    u10 = b.part("U10", "Interface", "68681", "Package_DIP:DIP-40_W15.24mm_Socket", "MC68681P", sheet="io")
    u11 = b.part("U11", "Video", "AD725", "Package_SO:SOIC-16W_7.5x10.3mm_P1.27mm", "AD724JR", sheet="video")
    u12 = b.part("U12", "Regulator_Linear", "AP2112K-3.3", "Package_TO_SOT_SMD:SOT-23-5", "AP2112K-3.3", sheet="power")
    u13 = b.part("U13", "Power_Supervisor", "MCP130-xxxxTT", "Package_TO_SOT_SMD:SOT-23", "MCP130T-450I/TT", sheet="io")
    x1 = b.part("X1", "Oscillator", "CXO_DIP8", "Oscillator:Oscillator_DIP-8", "24MHz", sheet="cpld1")
    y1 = b.part("Y1", "Device", "Crystal", "Crystal:Crystal_HC49-U_Vertical", "3.6864MHz", sheet="io")
    y2 = b.part("Y2", "Device", "Crystal", "Crystal:Crystal_HC49-U_Vertical", "3.579545MHz", sheet="video")
    j1 = b.part("J1", "Connector", "USB_C_Receptacle_PowerOnly_6P",
                "Connector_USB:USB_C_Receptacle_GCT_USB4125-xx-x_6P_TopMnt_Horizontal", "USB-C power", sheet="power")
    j2 = b.part("J2", "Connector_Generic", "Conn_01x06", "Connector_PinHeader_2.54mm:PinHeader_1x06_P2.54mm_Vertical",
                "FTDI (GND CTS VCC TXD RXD RTS)", sheet="io")
    j3 = b.part("J3", "other-m68k", "Mini-DIN-6_Shield", "other-m68k:MiniDIN-6_CUI_MD-60SM_Horizontal", "PS/2", sheet="cpld2")
    j4 = b.part("J4", "Connector", "Micro_SD_Card_Det_Hirose_DM3AT", "Connector_Card:microSD_HC_Hirose_DM3AT-SF-PEJM5",
                "microSD DM3AT-SF-PEJM5", sheet="cpld2")
    j5 = b.part("J5", "Connector", "Conn_Coaxial", "other-m68k:RCA_CUI_RCJ-041_Horizontal", "RCA composite", sheet="video")
    j6 = b.part("J6", "Connector_Audio", "AudioJack3", "Connector_Audio:Jack_3.5mm_CUI_SJ1-3523N_Horizontal",
                "3.5mm audio", sheet="cpld2")
    j7 = b.part("J7", "Connector_Generic", "Conn_02x05_Odd_Even", "Connector_PinHeader_2.54mm:PinHeader_2x05_P2.54mm_Vertical",
                "JTAG (ByteBlaster)", sheet="cpld1")
    j8 = b.part("J8", "Connector_Generic", "Conn_02x08_Odd_Even", "Connector_PinHeader_2.54mm:PinHeader_2x08_P2.54mm_Vertical",
                "Expansion", sheet="io")
    j9 = b.part("J9", "Connector_Generic", "Conn_02x15_Odd_Even", "Connector_PinHeader_2.54mm:PinHeader_2x15_P2.54mm_Vertical",
                "Logic analyzer (gusmanb 2x15)", sheet="cpu")
    sw1 = b.part("SW1", "Switch", "SW_Push", "Button_Switch_THT:SW_PUSH_6mm", "RESET", sheet="io")
    sw2 = b.part("SW2", "Switch", "SW_Push", "Button_Switch_THT:SW_PUSH_6mm", "NMI", sheet="cpld2")
    f1 = b.part("F1", "Device", "Polyfuse", "Fuse:Fuse_1206_3216Metric", "2A", sheet="power")
    fb1 = b.part("FB1", "Device", "FerriteBead", "Inductor_SMD:L_0603_1608Metric", "600R@100MHz", sheet="video")
    holes = [b.part(f"H{i}", "Mechanical", "MountingHole_Pad", "MountingHole:MountingHole_3.2mm_M3_Pad", "M3", sheet="power")
             for i in range(1, 5)]

    p1 = cpld_pins("cpld1_mmu")
    p2 = cpld_pins("cpld2_io")

    # ------------------------------------------------------------------ CPU buses
    # A1..A23: CPU -> CPLD1; A1..A3 and A18..A19 also CPLD2; A1..A4 also DUART RS1..RS4; A1..A19 -> flash A0..A18
    for i in range(1, 24):
        pins = [u1.pin(f"A{i}"), u6.pin(p1[f"A{i}"])]
        if i <= 3 or i in (18, 19):
            pins.append(u7.pin(p2[f"A{i}"]))
        if i <= 4:
            pins.append(u10.pin(f"RS{i}"))
        if i <= 19:
            pins += [u4.pin(f"A{i-1}"), u5.pin(f"A{i-1}")]
        b.net(f"A{i}", *pins)
    # CPU data bus D0..D15: CPU, flash (even = D15..8, odd = D7..0), DUART D0..7, '245 B side
    for i in range(16):
        pins = [u1.pin(f"D{i}")]
        if i >= 8:
            pins += [u4.pin(f"D{i-8}"), u8.pin(f"B{i-8}")]
        else:
            pins += [u5.pin(f"D{i}"), u9.pin(f"B{i}"), u10.pin(f"D{i}")]
        b.net(f"D{i}", *pins)
    # SRAM-side bus MD0..MD15: SRAMs (U2 = upper byte), CPLD2 D0..D15, '245 A side
    for i in range(16):
        if i >= 8:
            pins = [u2.pin(f"DQ{i-8}"), u8.pin(f"A{i-8}")]
        else:
            pins = [u3.pin(f"DQ{i}"), u9.pin(f"A{i}")]
        pins.append(u7.pin(p2[f"D{i}"]))
        b.net(f"MD{i}", *pins)
    # SRAM address MA0..MA16 from CPLD1, MA17..MA18 from CPLD2 (A18/A19 in the CPU half, FB_PAGE_HI in the video half)
    for i in range(17):
        b.net(f"MA{i}", u6.pin(p1[f"MA{i}"]), u2.pin(f"A{i}"), u3.pin(f"A{i}"))
    for i in (17, 18):
        b.net(f"MA{i}", u7.pin(p2[f"MA{i}"]), u2.pin(f"A{i}"), u3.pin(f"A{i}"))

    # ------------------------------------------------------------------ CPU control
    b.net("AS", u1.pin("AS"), u6.pin(p1["AS"]))
    b.net("UDS", u1.pin("UDS"), u6.pin(p1["UDS"]))
    b.net("LDS", u1.pin("LDS"), u6.pin(p1["LDS"]))
    b.net("RW", u1.pin("R/W"), u6.pin(p1["RW"]), u7.pin(p2["RW"]), u10.pin("R/W"), u8.pin("A->B"), u9.pin("A->B"))
    r_dtack = R("4.7k", "cpu", near="U1")
    b.net("DTACK", u1.pin("DTACK"), u6.pin(p1["DTACK"]), u10.pin("DTACK"), r_dtack.pin(2))   # open-drain bus
    b.power("VCC", r_dtack.pin(1))
    b.net("BERR", u1.pin("BERR"), u6.pin(p1["BERR"]))
    b.net("VPA", u1.pin("VPA"), u6.pin(p1["VPA"]))
    b.net("CPUCLK", u1.pin("CLK"), u6.pin(p1["CPUCLK"]))
    for i in range(3):
        b.net(f"IPL{i}", u1.pin(f"IPL{i}"), u7.pin(p2[f"IPL{i}"]))
    for name in ("BR", "BGACK"):               # no bus masters: pulled high
        r = R("4.7k", "cpu", near="U1")
        b.net(name, u1.pin(name), r.pin(2))
        b.power("VCC", r.pin(1))
    # reset: SYSRESET pulls RESET and HALT low through separate diodes (architecture.md section 6)
    for name, dref in (("RESET", "D1"), ("HALT", "D2")):
        r = R("4.7k", "cpu", near="U1")
        d = b.part(dref, "Diode", "1N4148W", SOD123, "1N4148W", sheet="cpu", PcbNear="U1")
        b.net(name, u1.pin(name), r.pin(2), d.pin("A"))
        b.power("VCC", r.pin(1))
        b.net("SYSRESET", d.pin("K"))
    u1.nc("BG", "VMA", "E", "FC0", "FC1", "FC2")
    b.power("VCC", u1.pins("VCC"))             # 14 and the hidden stacked 49
    b.power("GND", u1.pins("GND"))             # 16 and the hidden stacked 53
    decouple(u1, ["14", "49"], "cpu", bulk="10uF")

    # ------------------------------------------------------------------ memory
    for u in (u2, u3):
        b.power("GND", u.pin("CE#"))            # pin 22, active low: always enabled (OE/WE gate)
        b.power("VCC", u.pin("VCC"))
        b.power("GND", u.pin("VSS"))
    b.net("RAMOE", u6.pin(p1["RAMOE"]), u2.pin("OE#"), u3.pin("OE#"))
    b.net("WEU", u6.pin(p1["WEU"]), u2.pin("WE#"))
    b.net("WEL", u6.pin(p1["WEL"]), u3.pin("WE#"))
    for u in (u4, u5):
        b.power("GND", u.pin("CE"))             # CE tied low; OE/WE from CPLD1 do the gating
        b.power("VCC", u.pin("VCC"))
        b.power("GND", u.pin("GND"))
    b.net("ROMOE", u6.pin(p1["ROMOE"]), u4.pin("OE"), u5.pin("OE"))
    b.net("ROMWE", u6.pin(p1["ROMWE"]), u4.pin("PGM"), u5.pin("PGM"))   # pin 31 = /WE
    b.net("BUFEN", u6.pin(p1["BUFEN"]), u8.pin("CE"), u9.pin("CE"))   # pin 19 = OE on the '245
    for u in (u8, u9):
        b.power("VCC", u.pin("VCC"))
        b.power("GND", u.pin("GND"))
    for u in (u2, u3, u4, u5, u8, u9):
        decouple(u, [u.pin("VCC").pin.number], "memory")

    # ------------------------------------------------------------------ CPLD1
    b.net("SYSRESET", u6.pin(p1["SYSRESET"]), u7.pin(p2["SYSRESET"]))          # GCLR, pin 1
    b.net("CLK24", x1.pin("OUT"), u6.pin(p1["CLK24"]), u7.pin(p2["CLK24"]))    # GCLK1, pin 83
    b.power("VCC", x1.pin("Vcc"), x1.pin("EN"))
    b.power("GND", x1.pin("GND"))
    decouple(x1, ["8"], "cpld1")
    for name in ("IOCS", "LATCH", "DE", "HSYNC", "VSYNC"):
        b.net(name, u6.pin(p1[name]), u7.pin(p2[name]))
    b.net("HSYNC", u11.pin("16"))              # AD724 HSYNC
    b.net("VSYNC", u11.pin("15"))              # AD724 VSYNC
    b.net("DUARTCS", u6.pin(p1["DUARTCS"]), u10.pin("CS"))
    for u, sheet in ((u6, "cpld1"), (u7, "cpld2")):
        b.power("VCC", u.pins("VCCINT"), u.pins("VCCIO"))   # 3,43 + 13,26,38,53,66,78 (stacked hidden)
        b.power("GND", u.pins("GND"))                        # 7,19,32,42,47,59,72,82
        b.power("GND", u.pin("84"), u.pin("2"))              # unused dedicated inputs OE1, OE2/GCLK2
        decouple(u, ["3", "43", "13", "26", "38", "53", "66", "78"], sheet, bulk="10uF")
        u.nc_unused = True
    # JTAG chain: header -> U6 -> U7 -> header.  TDI 14, TMS 23, TCK 62, TDO 71 on both.
    r_tms, r_tdi1, r_tdi2 = R("10k", "cpld1", near="U6"), R("10k", "cpld1", near="U6"), R("10k", "cpld2", near="U7")
    b.net("JTAG_TCK", j7.pin(1), u6.pin("62"), u7.pin("62"))
    b.net("JTAG_TMS", j7.pin(5), u6.pin("23"), u7.pin("23"), r_tms.pin(2))
    b.net("JTAG_TDI", j7.pin(9), u6.pin("14"), r_tdi1.pin(2))
    b.net("JTAG_TDO1", u6.pin("71"), u7.pin("14"), r_tdi2.pin(2))
    b.net("JTAG_TDO", u7.pin("71"), j7.pin(3))
    b.power("VCC", r_tms.pin(1), r_tdi1.pin(1), r_tdi2.pin(1), j7.pin(4))
    b.power("GND", j7.pin(2), j7.pin(6), j7.pin(8), j7.pin(10))
    j7.nc(7)

    # ------------------------------------------------------------------ CPLD2 peripherals
    # PS/2 (mini-DIN 6: 1 DATA, 3 GND, 4 VCC, 5 CLK; 2 and 6 unused)
    for name, pin in (("PS2_DAT", 1), ("PS2_CLK", 5)):
        r = R("4.7k", "cpld2", near="J3")
        b.net(name, u7.pin(p2[name]), j3.pin(pin), r.pin(2))
        b.power("VCC", r.pin(1))
    b.power("VCC", j3.pin(4))
    b.power("GND", j3.pin(3), j3.pin("SH"))
    j3.nc(2, 6)
    # NMI button: active high, 10k pull-down, 100 nF debounce
    r = R("10k", "cpld2", near="SW2")
    c = C("100nF", "cpld2", near="SW2")
    b.net("NMIBTN", u7.pin(p2["NMIBTN"]), sw2.pin(2), r.pin(1), c.pin(1))
    b.power("GND", r.pin(2), c.pin(2))
    b.power("VCC", sw2.pin(1))
    # LEDs
    for name, dref in (("LED0", "D3"), ("LED1", "D4")):
        r = R("1k", "cpld2", near="U7")
        d = b.part(dref, "Device", "LED", LED0805, "LED green", sheet="cpld2", PcbNear="U7")
        b.net(name, u7.pin(p2[name]), r.pin(1))
        b.net(f"{name}_A", r.pin(2), d.pin("A"))
        b.power("GND", d.pin("K"))
    # expansion header pins 5/7/9/11 were UIO0..3 until spec rev 7; now unused
    j8.nc(5, 7, 9, 11)
    # DUART interrupt (open drain) into CPLD2
    r = R("4.7k", "io", near="U10")
    b.net("DUART_IRQ", u10.pin("IRQ"), u7.pin(p2["DUART_IRQ"]), r.pin(2))
    b.power("VCC", r.pin(1))
    # microSD, native 4-bit SD bus (spec rev 7): CLK, CMD and DAT0..3 go straight from CPLD2
    # to the card. CPLD2 drives each line open-drain (low or released) and reads it back, so
    # the 10 k pull-ups to +3V3 set the high level; no 5 V -> 3.3 V dividers (the card
    # answers on CMD and drives DAT0..3 during reads, which a divider would fight).
    for name, sdpin in (("SD_DAT3", "DAT3/CD"), ("SD_CLK", "CLK"), ("SD_CMD", "CMD"),
                        ("SD_DAT0", "DAT0"), ("SD_DAT1", "DAT1"), ("SD_DAT2", "DAT2")):
        r = R("10k", "cpld2", near="J4")
        b.net(name, u7.pin(p2[name]), j4.pin(sdpin), r.pin(2))
        b.power("+3V3", r.pin(1))
    b.power("+3V3", j4.pin("VDD"))
    b.power("GND", j4.pin("VSS"), j4.pin("SHIELD"))
    j4.nc("DET_A", "DET_B")
    c = C("100nF", "cpld2", near="J4")
    b.power("+3V3", c.pin(1))
    b.power("GND", c.pin(2))
    # audio: 1k / 10nF low-pass -> 10k:2.2k divider -> 10uF -> jack (both channels)
    r_lp, c_lp = R("1k", "cpld2", near="J6"), C("10nF", "cpld2", near="J6")
    r_div1, r_div2 = R("10k", "cpld2", near="J6"), R("2.2k", "cpld2", near="J6")
    c_out = C("10uF", "cpld2", near="J6", fp=C0805)
    b.net("AUDIO", u7.pin(p2["AUDIO"]), r_lp.pin(1))
    b.net("AUDIO_LP", r_lp.pin(2), c_lp.pin(1), r_div1.pin(1))
    b.net("AUDIO_DIV", r_div1.pin(2), r_div2.pin(1), c_out.pin(1))
    b.net("AUDIO_OUT", c_out.pin(2), j6.pin("T"), j6.pin("R"))
    b.power("GND", c_lp.pin(2), r_div2.pin(2), j6.pin("S"))

    # ------------------------------------------------------------------ video (docs/video-encoder.md)
    # AD724 (AD725 symbol stand-in): 1 STND, 2 AGND, 3 FIN, 4 APOS, 5 ENCD, 6 RIN, 7 GIN, 8 BIN,
    # 9 CRMA, 10 COMP, 11 LUMA, 12 SELECT, 13 DGND, 14 DPOS, 15 VSYNC, 16 HSYNC
    dacs = (("R", ["R2", "R1", "R0"], ["787", "1.58k", "3.16k"], "6"),
            ("G", ["G2", "G1", "G0"], ["787", "1.58k", "3.16k"], "7"),
            ("B", ["B1", "B0"], ["681", "1.37k"], "8"))
    for colour, bits, values, adpin in dacs:
        node = f"{colour}_DAC"
        for bit, val in zip(bits, values):
            r = R(val + " 1%", "video", near="U11")
            b.net(bit, u7.pin(p2[bit]), r.pin(1))
            b.net(node, r.pin(2))
        term = R("75", "video", near="U11")
        cc = C("100nF", "video", near="U11")
        b.net(node, term.pin(1), cc.pin(1))
        b.power("GND", term.pin(2))
        b.net(f"{colour}IN", cc.pin(2), u11.pin(adpin))
    # NTSC only (decision 2026-10-20): STND pulled up through 10k, PALJMP (CPLD2 input) pulled
    # down through 10k; both resistors stay so a PAL board is a two-resistor mod, not a new layout
    r_stnd, r_pal = R("10k", "video", near="U11"), R("10k", "cpld2", near="U7")
    b.net("STND", u11.pin("1"), r_stnd.pin(2))
    b.power("VCC", r_stnd.pin(1))
    b.net("PALJMP", u7.pin(p2["PALJMP"]), r_pal.pin(1))
    b.power("GND", r_pal.pin(2))
    # subcarrier crystal on FIN with ~10 pF shunt; FSC mode (SELECT low via 10k); encode enabled
    c_fin = C("10pF", "video", near="Y2")
    b.net("FIN", u11.pin("3"), y2.pin(1), c_fin.pin(1))
    b.power("GND", y2.pin(2), c_fin.pin(2))
    r_sel = R("10k", "video", near="U11")
    b.net("SELECT", u11.pin("12"), r_sel.pin(1))
    b.power("GND", r_sel.pin(2))
    b.power("VCC", u11.pin("5"))                                   # ENCD
    # composite out: 220 uF (+ at the chip) then 75 ohm series to the RCA centre
    c_comp = C("220uF", "video", near="J5", fp=CP_RADIAL, symbol="C_Polarized")
    r_ser = R("75", "video", near="J5")
    b.net("COMP", u11.pin("10"), c_comp.pin(1))
    b.net("COMP_AC", c_comp.pin(2), r_ser.pin(1))
    b.net("VIDEO", r_ser.pin(2), j5.pin(1))
    b.power("GND", j5.pin(2))
    u11.nc("9", "11")                                              # CRMA, LUMA
    # supplies: APOS through a ferrite bead (+5VA), DPOS from VCC, 100 nF + 10 uF each
    b.power("VCC", fb1.pin(1), u11.pin("14"))
    b.power("+5VA", fb1.pin(2), u11.pin("4"))
    for v, fp in (("100nF", C0603), ("10uF", C0805)):
        c = C(v, "video", near="U11", fp=fp)
        b.power("+5VA", c.pin(1))
        b.power("GND", c.pin(2))
    decouple(u11, ["14"], "video", bulk="10uF")
    b.power("GND", u11.pin("2"), u11.pin("13"))

    # ------------------------------------------------------------------ DUART, serial, reset
    b.net("SYSRESET", u10.pin("RESET"))
    r_iack = R("4.7k", "io", near="U10")
    b.net("DUART_IACK", u10.pin("IACK"), r_iack.pin(2))
    b.power("VCC", r_iack.pin(1))
    # crystal: 3.6864 MHz across X1/CLK and X2, 10-15 pF X1 to GND, 0-5 pF X2 to GND (optional)
    c_x1 = C("15pF", "io", near="Y1")
    c_x2 = C("DNP (0-5pF)", "io", near="Y1", dnp=True)
    b.net("DUART_X1", u10.pin("X1/CLK"), y1.pin(1), c_x1.pin(1))
    b.net("DUART_X2", u10.pin("X2"), y1.pin(2), c_x2.pin(1))
    b.power("GND", c_x1.pin(2), c_x2.pin(2))
    # channel A to the FTDI header (1 GND, 2 CTS, 3 VCC, 4 TXD, 5 RXD, 6 RTS); channel B to J8
    b.net("FTDI_TXD", j2.pin(4), u10.pin("RXDA"))
    b.net("FTDI_RXD", j2.pin(5), u10.pin("TXDA"))
    b.power("GND", j2.pin(1))
    j2.nc(2, 3, 6)
    b.net("DUART_TXDB", u10.pin("TXDB"), j8.pin(13))
    b.net("DUART_RXDB", u10.pin("RXDB"), j8.pin(15))
    for i in range(6):                                             # unused inputs tied high
        r = R("10k", "io", near="U10")
        b.net(f"DUART_IP{i}", u10.pin(f"IP{i}"), r.pin(2))
        b.power("VCC", r.pin(1))
    u10.nc(*[f"OP{i}" for i in range(8)])
    b.power("VCC", u10.pin("VCC"))
    b.power("GND", u10.pin("GND"))
    decouple(u10, ["40"], "io")
    # expansion header: odd row signals (TXDB/RXDB above, 5..11 n/c), 1 = +5V, 3 = +3V3, even row GND
    b.power("VCC", j8.pin(1))
    b.power("+3V3", j8.pin(3))
    b.power("GND", *[j8.pin(i) for i in range(2, 17, 2)])
    # reset supervisor + button -> SYSRESET (10k pull-up)
    r_rst = R("10k", "io", near="U13")
    b.net("SYSRESET", u13.pin("RST"), sw1.pin(1), r_rst.pin(2))
    b.power("VCC", u13.pin("VDD"), r_rst.pin(1))
    b.power("GND", u13.pin("VSS"), sw1.pin(2))
    decouple(u13, ["2"], "io")

    # ------------------------------------------------------------------ power
    b.power("VBUS", j1.pins("VBUS"), f1.pin(1))                    # A9 + hidden B9
    b.power("VCC", f1.pin(2))
    b.power("GND", j1.pins("GND"), j1.pin("SHIELD"))               # A12 + hidden B12, S1
    for cc in ("CC1", "CC2"):
        r = R("5.1k", "power", near="J1")
        b.net(cc, j1.pin(cc), r.pin(1))
        b.power("GND", r.pin(2))
    c_bulk = C("100uF", "power", near="J1", fp=CP_RADIAL, symbol="C_Polarized")
    c_bulk2 = C("10uF", "power", near="J1", fp=C0805)
    b.power("VCC", c_bulk.pin(1), c_bulk2.pin(1))
    b.power("GND", c_bulk.pin(2), c_bulk2.pin(2))
    r_pwr = R("1k", "power", near="J1")
    d_pwr = b.part("D5", "Device", "LED", LED0805, "LED red (power)", sheet="power", PcbNear="J1")
    b.power("VCC", r_pwr.pin(1))
    b.net("PWRLED_A", r_pwr.pin(2), d_pwr.pin("A"))
    b.power("GND", d_pwr.pin("K"))
    # 3.3 V LDO for the SD card, 1 uF in and out; VOUT is a power_out pin, so no PWR_FLAG
    b.power("VCC", u12.pin("VIN"), u12.pin("EN"))
    b.power("GND", u12.pin("GND"))
    b.power("+3V3", u12.pin("VOUT"), flag=False)
    for net in ("VCC", "+3V3"):
        c = C("1uF", "power", near="U12")
        b.power(net, c.pin(1))
        b.power("GND", c.pin(2))
    for h in holes:
        b.power("GND", h.pin(1))

    # ------------------------------------------------------------------ test access
    # J9: 2x15 header with the pinout of gusmanb's LogicAnalyzer channel header (pin 1 = CH0,
    # pin 2 = CH23, odd pins CH0..CH11, even pins CH23..CH12, 25/26 GND, 27/29 EXT_T, 28 3V3,
    # 30 5V/VRef).  Every channel goes through 100 ohm (4x0603 arrays RN1..RN6) so a slipped
    # probe cannot short a bus line.  27-30 are left open: the analyzer's shifter keeps its own
    # 5 V reference and the trigger pins are its business.
    la = ["CPUCLK", "AS", "UDS", "LDS", "RW", "DTACK", "BERR", "VPA", "IPL0", "IPL1", "IPL2", "SYSRESET",
          "ROMOE", "RAMOE", "WEL", "WEU", "DUARTCS", "BUFEN", "HSYNC", "VSYNC", "A1", "A2", "A3", "A4"]
    rn = [b.part(f"RN{i + 1}", "Device", "R_Pack04", "Resistor_SMD:R_Array_Convex_4x0603", "4x100", sheet="cpu", PcbNear="J9")
          for i in range(6)]
    for k, net in enumerate(la):
        pack, elem = rn[k // 4], k % 4 + 1
        b.net(net, pack.pin(str(elem)))                     # R1.1 = pin 1 .. R4.1 = pin 4
        j9pin = 2 * k + 1 if k <= 11 else 2 * (23 - k) + 2
        b.net(f"LA{k}", pack.pin(str(9 - elem)), j9.pin(str(j9pin)))   # R1.2 = pin 8 .. R4.2 = pin 5
    b.power("GND", j9.pin("25"), j9.pin("26"))
    j9.nc("27", "28", "29", "30")
    # test pads: composite video at the jack, the two rails and ground
    for ref, net, near in (("TP1", "VIDEO", "J5"), ("TP2", "+3V3", "U12"), ("TP3", "VCC", "J1"), ("TP4", "GND", "J5")):
        tp = b.part(ref, "Connector", "TestPoint", "TestPoint:TestPoint_Pad_D1.5mm", net, sheet="power", PcbNear=near)
        (b.power if net in ("+3V3", "VCC", "GND") else b.net)(net, tp.pin("1"))
    apply_parts(b)
    return b


def apply_parts(b, path=os.path.join(HERE, "parts.json")):
    """Attach sourcing fields from parts.json to every part: Manufacturer, MPN, LCSC, Desc,
    Assembly (pcba | customer | socket | dnp), and for socketed chips Socket_Manufacturer,
    Socket_MPN, Chip_Supply.  A part without an entry is an error: the assembly BOM must
    be complete."""
    with open(path, encoding="utf-8") as f:
        db = json.load(f)
    by_value, by_ref = db.get("by_value", {}), db.get("by_ref", {})
    missing = []
    for ref, part in b.parts.items():
        entry = by_ref.get(ref) or by_value.get(f"{part.value}|{part.footprint}") or by_value.get(f"{part.value}|*")
        if entry is None:
            missing.append(f"{ref} ({part.value}, {part.footprint})")
            continue
        fields = {"Manufacturer": entry.get("mfr", ""), "MPN": entry.get("mpn", ""), "LCSC": entry.get("lcsc", ""),
                  "Desc": entry.get("desc", ""), "Assembly": entry.get("assembly", "pcba")}
        if entry.get("assembly") == "socket":
            s = entry["socket"]
            fields.update({"Socket_Manufacturer": s.get("mfr", ""), "Socket_MPN": s.get("mpn", ""),
                           "Socket_LCSC": s.get("lcsc", ""), "Socket_Desc": s.get("desc", ""),
                           "Chip_Supply": entry.get("chip_supply", "customer")})
        if entry.get("assembly") == "dnp":
            part.dnp = True
        part.props.update({k: v for k, v in fields.items() if v})
    if missing:
        raise SystemExit("parts.json has no entry for:\n  " + "\n  ".join(missing))


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "build", "other-m68k")
    board = build()
    warnings = board.check()
    for w in warnings:
        print("warning:", w)
    for path in board.write(out):
        print("wrote", path)
    print(f"{len(board.parts)} parts, {len(board.nets)} nets, {len(warnings)} warnings")
    if warnings:
        sys.exit(1)
