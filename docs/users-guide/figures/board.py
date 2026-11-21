#!/usr/bin/env python3
"""Draw the board tour figure from the released KiCad board.

usage: board.py BOARD.kicad_pcb OUT.svg

Part outlines come from the board's courtyards, so the drawing follows the board.  The
numbered callouts and the names are the table below; the chapter's legend uses the same
numbers.  The back edge (power, serial, video) is at the top, as on the board's silkscreen.
"""
import sys
import pcbnew

SPOT = "#c2410c"            # the guide's second colour (guide.typ: spot)
INK = "#1a1a1a"
FONT = "Nimbus Sans, DejaVu Sans, sans-serif"

# reference -> text drawn inside the outline (a list is one line per item)
NAMES = {
    "U1": ["68000", "processor"],
    "U2": ["RAM"], "U3": ["RAM"],
    "U4": ["ROM", "even"], "U5": ["ROM", "odd"],
    "U6": ["CPLD 1", "memory", "and video"], "U7": ["CPLD 2", "input", "and output"],
    "U10": ["68681", "serial", "chip"],
    "X1": ["24 MHz"],
}
# reference -> (callout number, side the number sits on)
CALLOUTS = {
    "J1": (1, "top"), "J2": (2, "top"), "J11": (3, "top"), "J5": (4, "top"), "J10": (5, "top"),
    "J3": (6, "bottom"), "J6": (7, "bottom"), "J4": (8, "bottom"), "J12": (9, "bottom"),
    "D5": (10, "leftof"), "D3": (10, "leftof"), "D4": (10, "leftof"),
    "SW1": (11, "bottom"), "SW2": (12, "bottom"),
    "J9": (13, "left"), "J7": (14, "left"),
    "U11": (15, "above"), "U8": (16, "rightof"), "U9": (16, "rightof"),
}
DRAW_PLAIN = {"Y1", "Y2", "U14", "U12", "U13"}      # outline only


def main():
    board = pcbnew.LoadBoard(sys.argv[1])
    mm = pcbnew.ToMM
    edge = board.GetBoardEdgesBoundingBox()
    ox, oy = mm(edge.GetX()), mm(edge.GetY())
    bw, bh = mm(edge.GetWidth()), mm(edge.GetHeight())

    out = []
    add = out.append
    add('<svg xmlns="http://www.w3.org/2000/svg" viewBox="-14 -13 %.1f %.1f" '
        'font-family="%s">' % (bw + 22, bh + 26, FONT))
    add('<rect x="0" y="0" width="%.1f" height="%.1f" rx="3" fill="#fbf7ef" stroke="%s" '
        'stroke-width="0.7"/>' % (bw, bh, INK))
    for x, y in ((4, 4), (bw - 4, 4), (4, bh - 4), (bw - 4, bh - 4)):
        add('<circle cx="%.1f" cy="%.1f" r="1.6" fill="white" stroke="%s" stroke-width="0.4"/>'
            % (x, y, INK))

    numbered = {}
    for fp in board.GetFootprints():
        ref = fp.GetReference()
        if ref not in NAMES and ref not in CALLOUTS and ref not in DRAW_PLAIN:
            continue
        bb = fp.GetCourtyard(pcbnew.F_CrtYd).BBox()
        x, y = mm(bb.GetX()) - ox, mm(bb.GetY()) - oy
        w, h = mm(bb.GetWidth()), mm(bb.GetHeight())
        round_part = ref.startswith("D")
        add('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" rx="%.1f" fill="white" '
            'stroke="%s" stroke-width="0.45"/>' % (x, y, w, h, 1.0 if round_part else 0.6, INK))
        if ref in NAMES:
            lines = NAMES[ref]
            size = 5.2 if ref == "U1" else 3.6
            if w < 16 and len(lines) == 1:
                size = 2.6
            upright = h > 1.6 * w                     # tall narrow part: turn the text
            cx, cy = x + w / 2, y + h / 2
            if upright:
                add('<g transform="translate(%.1f %.1f) rotate(-90)">' % (cx, cy))
                cx = cy = 0
            y0 = cy - (len(lines) - 1) * size * 0.6
            for i, t in enumerate(lines):
                add('<text x="%.1f" y="%.1f" font-size="%.1f" text-anchor="middle" '
                    'dominant-baseline="central" fill="%s"%s>%s</text>'
                    % (cx, y0 + i * size * 1.2, size if i == 0 else size * 0.72,
                       INK, ' font-weight="bold"' if i == 0 else "", t))
            if upright:
                add("</g>")
        if ref in CALLOUTS:
            n, side = CALLOUTS[ref]
            numbered.setdefault((n, side), []).append((x, y, w, h))

    for (n, side), boxes in sorted(numbered.items()):
        x0 = min(b[0] for b in boxes)
        y0 = min(b[1] for b in boxes)
        x1 = max(b[0] + b[2] for b in boxes)
        y1 = max(b[1] + b[3] for b in boxes)
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        if side == "top":
            px, py, lx, ly = cx, -7.5, cx, y0
        elif side == "bottom":
            px, py, lx, ly = cx, bh + 7.5, cx, y1
        elif side == "left":
            px, py, lx, ly = -8.5, cy, x0, cy
        elif side == "above":
            px, py, lx, ly = cx, y0 - 6, cx, y0
        elif side == "rightof":
            px, py, lx, ly = x1 + 6.5, cy, x1, cy
        else:                                           # leftof
            px, py, lx, ly = x0 - 6.5, cy, x0, cy
        add('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="0.4"/>'
            % (px, py, lx, ly, SPOT))
        add('<circle cx="%.1f" cy="%.1f" r="3.4" fill="%s"/>' % (px, py, SPOT))
        add('<text x="%.1f" y="%.1f" font-size="4.2" font-weight="bold" text-anchor="middle" '
            'dominant-baseline="central" fill="white">%d</text>' % (px, py + 0.1, n))

    add('<text x="%.1f" y="-10.2" font-size="3" fill="%s" text-anchor="end">BACK</text>'
        % (bw, INK))
    add('<text x="%.1f" y="%.1f" font-size="3" fill="%s" text-anchor="end">FRONT</text>'
        % (bw, bh + 11.5, INK))
    add("</svg>")
    open(sys.argv[2], "w").write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
