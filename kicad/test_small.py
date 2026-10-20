#!/usr/bin/env python3
"""test_small.py - a small board exercising gen_sch.py: 68000, one EPROM, one '245, an LED.

    python3 test_small.py [outdir]       writes outdir/test_small.kicad_sch + .kicad_pro
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_sch import Board  # noqa: E402


def build():
    b = Board("test_small", title="gen_sch spike: small test board")

    u1 = b.part("U1", "CPU_NXP_68000", "MC68000P", "Package_DIP:DIP-64_W22.86mm_Socket", "MC68000P12")
    u2 = b.part("U2", "Memory_EPROM", "27C512", "Package_DIP:DIP-28_W15.24mm_Socket", "27C512")
    u3 = b.part("U3", "74xx", "74HC245", "Package_DIP:DIP-20_W7.62mm_Socket", "74AHCT245")
    r1 = b.part("R1", "Device", "R", "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal", "330")
    d1 = b.part("D1", "Device", "LED", "LED_THT:LED_D5.0mm", "LED_GREEN")

    # buses: CPU A1..A4 land on EPROM A0..A3; CPU D0..D7 on EPROM D0..D7 and '245 A0..A7
    b.bus("A", 1, 4, u1, (u2, "A{}", -1))
    b.bus("D", 0, 7, u1, u2, (u3, "A{}"))
    # single nets, pins by name (with or without the ~{} markup) or by number
    b.net("ROMCS", u1.pin("AS"), u2.pin("~{CE}"))
    b.net("RW", u1.pin("R/W"), u2.pin("OE"), u3.pin("A->B"))
    b.net("BUFEN", u1.pin("LDS"), u3.pin("CE"))
    b.net("LED0", u3.pin("B0"), r1.pin(1))
    b.net("LED0_A", r1.pin(2), d1.pin("A"))

    b.power("VCC", u1.pins("VCC"), u2.pin("VCC"), u3.pin("VCC"))
    b.power("GND", u1.pins("GND"), u2.pin("GND"), u3.pin("GND"), d1.pin("K"))

    # intentionally unconnected pins
    u1.nc("BR", "BG", "BGACK", "VPA", "VMA", "E")
    u3.nc("B1", "B2", "B3", "B4", "B5", "B6", "B7")
    u2.nc("A4", "A5", "A6", "A7", "A8", "A9", "A10", "A11", "A12", "A13", "A14", "A15")
    u1.nc_unused = True      # everything else on the CPU gets a no-connect too
    return b


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "build", "test_small")
    board = build()
    for w in board.check():
        print("warning:", w)
    for path in board.write(out):
        print("wrote", path)
