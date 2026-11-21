#!/usr/bin/env python3
"""Draw the guide's line diagrams as SVG.   usage: diagrams.py OUTDIR

  colours.svg    all 256 colours with their hex numbers
  byte.svg       how the eight bits of a colour byte are shared out
  screenmem.svg  the screen with the addresses of its corners
  memmap.svg     the memory map
  joyport.svg    the joystick port, looking into the socket on the board
  serial.svg     a serial header
  pdm.svg        sound patterns and the levels they give

Addresses come from generated/hw.h where it has them, so the pictures follow the spec.
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
HW = open(os.path.join(HERE, "..", "..", "..", "generated", "hw.h")).read()
SPOT, INK, PAPER = "#c2410c", "#1a1a1a", "#fbf7ef"
SANS = "Nimbus Sans, DejaVu Sans, sans-serif"
MONO = "DejaVu Sans Mono, monospace"


def hw(name):
    m = re.search(r"#define\s+%s\s+(0x[0-9A-Fa-f]+)" % name, HW)
    if not m:
        sys.exit("diagrams.py: %s is not in generated/hw.h" % name)
    return int(m.group(1), 16)


def svg(w, h, body):
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %g %g" font-family="%s">\n%s\n</svg>\n'
            % (w, h, SANS, "\n".join(body)))


def text(x, y, s, size=4, anchor="middle", fill=INK, weight="normal", font=None):
    return ('<text x="%g" y="%g" font-size="%g" text-anchor="%s" fill="%s" font-weight="%s"%s '
            'dominant-baseline="central">%s</text>'
            % (x, y, size, anchor, fill, weight, ' font-family="%s"' % font if font else "", s))


def rect(x, y, w, h, fill="white", stroke=INK, sw=0.4, rx=0):
    return ('<rect x="%g" y="%g" width="%g" height="%g" rx="%g" fill="%s" stroke="%s" stroke-width="%g"/>'
            % (x, y, w, h, rx, fill, stroke, sw))


def rgb332(b):
    return ((b >> 5) * 255 // 7, ((b >> 2) & 7) * 255 // 7, (b & 3) * 255 // 3)


def colours():
    c, out = 11, []
    for i in range(16):
        out.append(text(12 + i * c + c / 2, 5, "%x" % i, 3.6, font=MONO, fill=SPOT, weight="bold"))
        out.append(text(5, 10 + i * c + c / 2, "%x" % i, 3.6, font=MONO, fill=SPOT, weight="bold"))
    for b in range(256):
        r, g, bl = rgb332(b)
        x, y = 12 + (b & 15) * c, 10 + (b >> 4) * c
        out.append(rect(x, y, c, c, "#%02x%02x%02x" % (r, g, bl), "none", 0))
        light = 0.3 * r + 0.59 * g + 0.11 * bl > 128
        out.append(text(x + c / 2, y + c / 2, "%02x" % b, 3.3, font=MONO,
                        fill="black" if light else "white"))
    out.append(rect(12, 10, 16 * c, 16 * c, "none", INK, 0.4))
    return svg(12 + 16 * c + 2, 10 + 16 * c + 2, out)


def byte():
    out, w = [], 16
    groups = (("RED", 0, 3, "#d92a1c"), ("GREEN", 3, 3, "#1f9d3a"), ("BLUE", 6, 2, "#2456d6"))
    weights = (4, 2, 1, 4, 2, 1, 2, 1)
    for i in range(8):
        col = [g for g in groups if g[1] <= i < g[1] + g[2]][0][3]
        out.append(rect(6 + i * w, 16, w, w, "white", col, 0.9))
        out.append(text(6 + i * w + w / 2, 16 + w / 2, str(weights[i]), 6, fill=col, weight="bold"))
        out.append(text(6 + i * w + w / 2, 11, "bit %d" % (7 - i), 3.2))
    for name, start, n, col in groups:
        x0, x1 = 6 + start * w + 1.5, 6 + (start + n) * w - 1.5
        out.append('<path d="M%g 36 v3 H%g v-3" fill="none" stroke="%s" stroke-width="0.7"/>' % (x0, x1, col))
        out.append(text((x0 + x1) / 2, 45, name, 4.2, fill=col, weight="bold"))
        out.append(text((x0 + x1) / 2, 50.5, "0 to %d" % (2 ** n - 1), 3.4))
    return svg(140, 55, out)


def screenmem():
    p0 = hw("FB_PAGE0_ADDR")
    out = [rect(30, 10, 128, 96, "#000055", INK, 0.6, 2)]
    corners = ((30, 10, p0, "end", -2, -4), (158, 10, p0 + 319, "start", 2, -4),
               (30, 106, p0 + 239 * 320, "end", -2, 4), (158, 106, p0 + 240 * 320 - 1, "start", 2, 4))
    for x, y, a, anchor, dx, dy in corners:
        out.append('<circle cx="%g" cy="%g" r="1.3" fill="%s"/>' % (x, y, SPOT))
        out.append(text(x + dx, y + dy, "%x" % a, 4, anchor, font=MONO, fill=SPOT, weight="bold"))
    out.append(text(94, 50, "320 pixels across", 4.5, fill="white"))
    out.append(text(94, 58, "240 rows down", 4.5, fill="white"))
    out.append(text(94, 70, "each row is hex 140 bytes", 3.6, fill="#aab"))
    return svg(188, 116, out)


def memmap():
    rows = (   # (start, end, name, note, colour); a None start draws a gap
        (hw("RAM_BASE"), 0x400, "Vectors", "the processor's table of addresses", "#e9e4d6"),
        (0x400, hw("USER_BASE"), "BIOS working memory", "", "#e9e4d6"),
        (hw("USER_BASE"), hw("FB_PAGE1_ADDR"), "Your programs", "842 KB", "#ffffff"),
        (hw("FB_PAGE1_ADDR"), hw("FB_PAGE0_ADDR"), "Picture, page 1", "", "#c9d7f5"),
        (hw("FB_PAGE0_ADDR"), 0x100000, "Picture, page 0", "the page you normally see", "#c9d7f5"),
        (None, None, "nothing here", "", None),
        (hw("ROM_BASE"), hw("ROM_BASE") + hw("FLASH_VOL_OFFSET"), "ROM: the BIOS", "256 KB", "#f6d9c9"),
        (hw("ROM_BASE") + hw("FLASH_VOL_OFFSET"), hw("ROM_BASE") + 0x100000, "ROM: drive C:", "768 KB", "#f6d9c9"),
        (hw("IO_BASE"), hw("IO_BASE") + 0x10000, "Keyboard, SD card, lights, sound", "CPLD 2", "#d8ecd5"),
        (hw("SW_BASE"), hw("SW_BASE") + 0x10000, "Picture switches", "CPLD 1", "#d8ecd5"),
        (hw("DUART_BASE"), hw("DUART_BASE") + 0x10000, "Serial ports and joystick", "the 68681", "#d8ecd5"),
        (None, None, "nothing here", "", None),
    )
    out, y, h = [], 4, 11
    for start, end, name, note, col in rows:
        if start is None:
            out.append('<path d="M40 %g l5 2.5 l-5 2.5 M110 %g l-5 2.5 l5 2.5" fill="none" stroke="%s" stroke-width="0.4"/>'
                       % (y, y, INK))
            out.append(text(75, y + 2.6, name, 3.2, fill="#777"))
            y += 6
            continue
        out.append(rect(40, y, 70, h, col))
        out.append(text(75, y + h / 2, name, 3.8, weight="bold"))
        out.append(text(37, y + 2.4, "%06x" % start, 3.4, "end", font=MONO, fill=SPOT, weight="bold"))
        out.append(text(37, y + h - 2.2, "%06x" % (end - 1), 3.0, "end", font=MONO, fill="#777"))
        if note:
            out.append(text(113, y + h / 2, note, 3.3, "start"))
        y += h
    return svg(196, y + 4, out)


def joyport():
    # male DE-9 on the board, looking into it: pins 1..5 on top from left, 6..9 below
    out = ['<path d="M14 8 H96 Q102 8 100.5 14 L95 36 Q93.5 42 87 42 H23 Q16.5 42 15 36 L9.5 14 Q8 8 14 8 Z" '
           'fill="%s" stroke="%s" stroke-width="0.7"/>' % (PAPER, INK)]
    for i in range(5):
        x = 23 + i * 16
        out.append('<circle cx="%g" cy="19" r="3.2" fill="white" stroke="%s" stroke-width="0.5"/>' % (x, INK))
        out.append(text(x, 19, str(i + 1), 3.6, weight="bold"))
    for i in range(4):
        x = 31 + i * 16
        out.append('<circle cx="%g" cy="32" r="3.2" fill="white" stroke="%s" stroke-width="0.5"/>' % (x, INK))
        out.append(text(x, 32, str(i + 6), 3.6, weight="bold"))
    return svg(110, 50, out)


def serial():
    names = ("GND", "CTS", "5 V", "TXD", "RXD", "RTS")
    out = [rect(8, 14, 6 * 14 + 4, 14, PAPER, INK, 0.6, 1.5)]
    for i, n in enumerate(names):
        x = 17 + i * 14
        out.append(rect(x - 2.2, 18.8, 4.4, 4.4, "#d4a017", INK, 0.4))
        out.append(text(x, 9, str(i + 1), 3.6, weight="bold", fill=SPOT))
        out.append(text(x, 34, n, 3.6))
    return svg(100, 40, out)


def pdm():
    pats = ((0x00, "0 of 8", "lowest"), (0x11, "2 of 8", ""), (0x55, "4 of 8", "middle"),
            (0x77, "6 of 8", ""), (0xFF, "8 of 8", "highest"))
    out, y = [], 6
    for p, frac, note in pats:
        out.append(text(4, y + 6, "%02x" % p, 4.2, "start", font=MONO, fill=SPOT, weight="bold"))
        d, x = "M18 %g" % (y + 10), 18
        for rep in range(2):
            for bit in range(7, -1, -1):
                lvl = y + (2 if (p >> bit) & 1 else 10)
                d += " V%g H%g" % (lvl, x + 6)
                x += 6
        out.append('<path d="%s" fill="none" stroke="%s" stroke-width="0.6"/>' % (d, INK))
        out.append('<line x1="66" y1="%g" x2="66" y2="%g" stroke="#999" stroke-width="0.3" stroke-dasharray="1 1"/>'
                   % (y, y + 12))
        ones = bin(p).count("1")
        out.append(rect(122, y + 1, 30, 10, "white", "#999", 0.3))
        out.append(rect(122, y + 1, 30 * ones / 8, 10, SPOT, "none", 0))
        out.append(text(156, y + 6, frac + (", " + note if note else ""), 3.4, "start"))
        y += 16
    out.append(text(42, y + 2, "one line", 3.2))
    out.append(text(90, y + 2, "the next line", 3.2))
    out.append(text(137, y + 2, "level", 3.2))
    return svg(196, y + 7, out)


def main():
    outdir = sys.argv[1]
    os.makedirs(outdir, exist_ok=True)
    for name, fn in (("colours", colours), ("byte", byte), ("screenmem", screenmem), ("memmap", memmap),
                     ("joyport", joyport), ("serial", serial), ("pdm", pdm)):
        open(os.path.join(outdir, name + ".svg"), "w").write(fn())


if __name__ == "__main__":
    main()
