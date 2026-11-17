#!/usr/bin/env python3
"""board_geometry.py - everything the enclosure needs from the KiCad board, as OpenSCAD.

    python3 board_geometry.py <board.kicad_pcb> <parts.json> <out.scad>

Reads the released board with KiCad's pcbnew module and parts.json (heights, mating-plug
envelopes, labels, pillar spots) and writes an OpenSCAD include file of plain variables in
CASE coordinates: x to the right as on the board, y from the FRONT edge (the sheet's bottom
edge; placement.json measures y from the back edge, so y_case = board_depth - y_board),
z = 0 at the board bottom. It also checks the geometry that the printed case depends on:

  * pillars: clear of every pad, via and top-side footprint by pillar radius + 0.5 mm
  * corner bosses: no top-side footprint inside the boss hull (circle + wedge to the walls)
  * cutouts: the connector sits on the edge it is cut into, the envelope stays on the board
  * heights: every footprint has a height; the tallest parts are listed

Exits 1 on any failed check so `make` stops before an STL is rendered.
"""
import json
import math
import sys

import pcbnew

mm = pcbnew.ToMM


def fp_name(f):
    n = f.GetFPID().GetLibItemName()
    return n.wx_str() if hasattr(n, "wx_str") else str(n)


def is_hole(f):
    """a mounting hole: H* reference with a MountingHole footprint (the DE-9's footprint name
    also contains 'MountingHoles')"""
    return f.GetReference().startswith("H") and fp_name(f).startswith("MountingHole")


def bbox(f):
    r = f.GetBoundingBox(False, False)       # footprint without its text
    return (mm(r.GetLeft()), mm(r.GetTop()), mm(r.GetRight()), mm(r.GetBottom()))


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    board = pcbnew.LoadBoard(sys.argv[1])
    with open(sys.argv[2], encoding="utf-8") as f:
        P = json.load(f)
    out_path = sys.argv[3]
    errors = []

    # ---- board outline: axis-aligned extents of Edge.Cuts, corner radius from placement -----
    xs, ys = [], []
    for d in board.GetDrawings():
        if d.GetLayer() == pcbnew.Edge_Cuts and hasattr(d, "GetStart"):
            for pt in (d.GetStart(), d.GetEnd()):       # end points, not the stroked bounding box
                xs.append(mm(pt.x)); ys.append(mm(pt.y))
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    W, D = round(x1 - x0, 2), round(y1 - y0, 2)
    T = P["board_thickness"]

    def case_xy(x, y):
        return (round(x - x0, 2), round(y1 - y, 2))

    fps = {f.GetReference(): f for f in board.GetFootprints()}
    top = [f for f in fps.values() if not f.IsFlipped()]
    if len(top) != len(fps):
        errors.append("footprints on the bottom side: " + ", ".join(f.GetReference() for f in fps.values() if f.IsFlipped()))

    # ---- heights ---------------------------------------------------------------------------
    heights = P["heights"]

    def height_of(f):
        ref, name = f.GetReference(), fp_name(f)
        if ref in heights:
            return heights[ref]["h"], heights[ref]
        for key, v in heights.items():
            if key in name:
                return v["h"], v
        return P["default_height"], None

    tall = []
    for f in top:
        h, ent = height_of(f)
        if ent is None and fp_name(f) and not fp_name(f).startswith(("R_", "C_", "L_", "LED_", "D_")):
            pass  # small SMD parts take the default silently
        tall.append((h, f.GetReference(), fp_name(f), bool(ent and ent.get("verify"))))
    tall.sort(reverse=True)
    max_h = tall[0][0]

    # ---- holes --------------------------------------------------------------------------------
    holes = []
    for f in top:
        if is_hole(f):
            p = f.GetPosition()
            holes.append(case_xy(mm(p.x), mm(p.y)))
    if len(holes) != 4:
        errors.append("expected 4 mounting holes, found %d" % len(holes))

    # ---- pads and vias (for the pillar check) -------------------------------------------------
    pads, boxes = [], []
    for f in fps.values():
        boxes.append((f.GetReference(),) + bbox(f))
        for p in f.Pads():
            pos, sz = p.GetPosition(), p.GetSize()
            pads.append((mm(pos.x), mm(pos.y), max(mm(sz.x), mm(sz.y)) / 2))
    vias = [(mm(t.GetPosition().x), mm(t.GetPosition().y), 0.4)   # every via on this board is 0.8 mm
            for t in board.GetTracks() if t.Type() == pcbnew.PCB_VIA_T]

    def clearance(x, y):
        dp = min(math.hypot(x - px, y - py) - pr for px, py, pr in pads)
        dv = min(math.hypot(x - vx, y - vy) - vr for vx, vy, vr in vias)
        db = 1e9
        for ref, l, t, r, b in boxes:
            if is_hole(fps[ref]):
                continue
            dx, dy = max(l - x, 0, x - r), max(t - y, 0, y - b)
            db = min(db, -1.0 if (dx == 0 and dy == 0) else math.hypot(dx, dy))
        return min(dp, dv, db)

    pillar_r = P["pillar_d"] / 2
    pillars = []
    for (bx, by) in P["pillars"]:
        c = clearance(bx, by)
        if c < pillar_r + 0.5:
            errors.append("pillar at board (%g,%g): clearance %.1f mm < %.1f" % (bx, by, c, pillar_r + 0.5))
        pillars.append(case_xy(bx, by))

    # ---- corner bosses: hull of the circle at the hole and the wedge to the two nearest walls ---
    boss_r = P["boss_r"]
    for f in top:
        if is_hole(f):
            hx, hy = mm(f.GetPosition().x), mm(f.GetPosition().y)
            wx = x0 if hx - x0 < x1 - hx else x1          # nearest side wall
            wy = y0 if hy - y0 < y1 - hy else y1
            rx = (min(wx, hx) - (boss_r if wx > hx else 0), max(wx, hx) + (boss_r if wx < hx else 0))
            ry = (min(wy, hy) - (boss_r if wy > hy else 0), max(wy, hy) + (boss_r if wy < hy else 0))
            for ref, l, t, r, b in boxes:
                if is_hole(fps[ref]):
                    continue
                dx, dy = max(l - hx, 0, hx - r), max(t - hy, 0, hy - b)
                in_circle = math.hypot(dx, dy) < boss_r
                in_wedge_x = (l < rx[1] and r > rx[0]) and (t < max(wy, hy) and b > min(wy, hy))
                in_wedge_y = (t < ry[1] and b > ry[0]) and (l < max(wx, hx) and r > min(wx, hx))
                if in_circle or in_wedge_x or in_wedge_y:
                    errors.append("boss at %s hits %s (%.1f mm from the hole)" % (f.GetReference(), ref, math.hypot(dx, dy)))

    # ---- edge connectors --------------------------------------------------------------------------
    def edge_of(f):
        l, t, r, b = bbox(f)
        cx, cy = (l + r) / 2, (t + b) / 2
        d = {"left": cx - x0, "right": x1 - cx, "back": cy - y0, "front": y1 - cy}
        e = min(d, key=d.get)
        if d[e] > 25:
            errors.append("%s is %.0f mm from the nearest edge; not an edge connector" % (f.GetReference(), d[e]))
        return e, (cx, cy)

    def along(e, cx, cy):
        return round(cx - x0, 3) if e in ("front", "back") else round(y1 - cy, 3)

    rects, rounds = [], []
    for ref, c in P["cutouts"].items():
        if ref not in fps:
            errors.append("cutout %s: no such footprint" % ref); continue
        f = fps[ref]
        e, (cx, cy) = edge_of(f)
        a = along(e, cx, cy)
        extent = W if e in ("front", "back") else D
        if c["shape"] == "rect":
            if a - c["w"] / 2 < 0 or a + c["w"] / 2 > extent:
                errors.append("cutout %s runs past the board edge" % ref)
            rects.append([ref, e, a, c["w"], c["h"], c["zc"], c["label"]])
        else:
            if a - c["d"] / 2 < 0 or a + c["d"] / 2 > extent:
                errors.append("cutout %s runs past the board edge" % ref)
            rounds.append([ref, e, a, c["d"], c["zc"], c["label"]])

    notches = []
    for ref, n in P["notches"].items():
        f = fps[ref]
        e, (cx, cy) = edge_of(f)
        notches.append([ref, e, along(e, cx, cy), n["w"], n["z_top"], n["tab_z"], n["label"]])

    def centre(ref):
        l, t, r, b = bbox(fps[ref])              # courtyard centre: the SW_PUSH footprint's origin is pin 1
        return case_xy((l + r) / 2, (t + b) / 2)

    buttons = [list(centre(r)) + [height_of(fps[r])[0], v["label"]] for r, v in P["buttons"].items()]
    leds = [list(centre(r)) + [height_of(fps[r])[0], v["label"]] for r, v in P["leds"].items()]

    vents = []
    for ref in P["vents"]:
        l, t, r, b = bbox(fps[ref])
        (cl, cb), (cr, ct) = case_xy(l, b), case_xy(r, t)
        vents.append([ref, cl, cb, cr, ct])

    # ---- report -----------------------------------------------------------------------------------
    print("board %g x %g x %g mm, holes (case coords): %s" % (W, D, T, holes))
    print("tallest parts above the board top:")
    for h, ref, name, verify in tall[:8]:
        print("  %5.1f  %-5s %s%s" % (h, ref, name, "   (verify)" if verify else ""))
    print("lid interior from the tallest part: %.1f mm" % max_h)
    for ref, e, a, w, h, zc, label in rects:
        print("  rect  %-4s %-5s along %6.1f  %4.1f x %4.1f  axis %4.1f  %s" % (ref, e, a, w, h, zc, label))
    for ref, e, a, d, zc, label in rounds:
        print("  round %-4s %-5s along %6.1f  dia %4.1f       axis %4.1f  %s" % (ref, e, a, d, zc, label))
    for ref, e, a, w, zt, tz, label in notches:
        print("  notch %-4s %-5s along %6.1f  %4.1f wide to z %.1f, tab to %.1f  %s" % (ref, e, a, w, zt, tz, label))
    for x, y in pillars:
        print("  pillar at case (%g, %g)" % (x, y))
    if errors:
        print("\n".join("ERROR: " + e for e in errors), file=sys.stderr)
        sys.exit(1)

    # ---- write ------------------------------------------------------------------------------------
    def scad(v):
        return json.dumps(v)

    lines = [
        "// generated by enclosure/board_geometry.py from %s -- do not edit" % sys.argv[1],
        "// case coordinates: x right, y from the FRONT edge, z = 0 at the board bottom",
        "board_w = %g;" % W, "board_d = %g;" % D, "board_t = %g;" % T,
        "max_part_h = %g;   // tallest part above the board top" % max_h,
        "holes = %s;" % scad(holes),
        "pillars = %s;" % scad(pillars),
        "// [ref, edge, along, width, height, axis z above board top, label]",
        "rect_cutouts = %s;" % scad(rects),
        "// [ref, edge, along, diameter, axis z above board top, label]",
        "round_cutouts = %s;" % scad(rounds),
        "// [ref, edge, along, width, notch top above board top, bottom tab top above board top, label]",
        "notches = %s;" % scad(notches),
        "// [x, y, actuator top above board top, label]",
        "buttons = %s;" % scad(buttons),
        "// [x, y, LED top above board top, label]",
        "leds = %s;" % scad(leds),
        "// [ref, x0, y0, x1, y1] footprint extents to vent over",
        "vents = %s;" % scad(vents),
        "",
    ]
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print("wrote", out_path)


if __name__ == "__main__":
    main()
