#!/usr/bin/env python3
"""gen_pcb.py - PCB side of the flow, using KiCad's pcbnew Python module.

kicad-cli 9.0.9 has no "import netlist into a board" command, so this script does the
job: it reads the kicadsexpr netlist exported from the schematic, loads each component's
footprint from the system (or project lib/) libraries, assigns pad nets, places the
footprints, draws the outline, adds the inner-layer power zones and saves a .kicad_pcb.
It also wraps the Specctra DSN export / SES import used with freerouting.

    python3 gen_pcb.py build <netlist.net> <out.kicad_pcb> [--placement placement.json]
                             [--width MM] [--pitch MM]
    python3 gen_pcb.py dsn   <board.kicad_pcb> <out.dsn>
    python3 gen_pcb.py ses   <board.kicad_pcb> <in.ses> <out.kicad_pcb>

Placement (see kicad/README.md):
  * placement.json "parts": ref -> {x, y, rot, side}.  x, y are the centre of the
    footprint's courtyard in mm from the board's top-left corner, rot in degrees
    (KiCad sense), side "top" or "bottom".
  * every other footprint is placed automatically: next to the part named in its
    "PcbNear" schematic field (a property of the symbol, carried in the netlist), or in
    the first free spot scanning the board, never overlapping anything already placed.
  * without a placement file the old behaviour remains: a grid of rows args.width wide
    (PcbX/PcbY/PcbRot fields still override), 2 layers, no zones.
"""
import argparse
import json
import math
import os
import re
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_sch import parse, find, find_all, LIB_DIR  # noqa: E402

FOOTPRINT_DIR = os.environ.get("KICAD_FOOTPRINT_DIR", "/usr/share/kicad/footprints")


def read_netlist(path):
    with open(path, encoding="utf-8") as f:
        top = parse(f.read())[0]
    comps = []
    for c in find_all(find(top, "components"), "comp"):
        fields = {}
        fl = find(c, "fields")
        if fl:
            for fld in find_all(fl, "field"):
                fields[find(fld, "name")[1]] = fld[2] if len(fld) > 2 else ""
        dnp = any((find(p, "name") or [None, None])[1] == "dnp" for p in find_all(c, "property"))
        comps.append({"ref": find(c, "ref")[1], "value": find(c, "value")[1],
                      "footprint": (find(c, "footprint") or [None, ""])[1], "fields": fields, "dnp": dnp})
    nets = []
    for n in find_all(find(top, "nets"), "net"):
        nodes = [(find(nd, "ref")[1], find(nd, "pin")[1]) for nd in find_all(n, "node")]
        nets.append({"code": int(find(n, "code")[1]), "name": find(n, "name")[1], "nodes": nodes})
    return comps, nets


def load_footprint(fpid):
    lib, name = fpid.split(":", 1)
    for d in (FOOTPRINT_DIR, LIB_DIR):
        path = os.path.join(d, lib + ".pretty")
        if os.path.isdir(path):
            fp = pcbnew.FootprintLoad(path, name)
            if fp is not None:
                return fp
    raise SystemExit(f"footprint {fpid} not found under {FOOTPRINT_DIR} or {LIB_DIR}")


def mm(v):
    return pcbnew.FromMM(v)


def to_mm(v):
    return pcbnew.ToMM(v)


def natural(ref):
    m = re.match(r"([A-Za-z]+)(\d+)", ref)
    return (m.group(1), int(m.group(2))) if m else (ref, 0)


# ----------------------------------------------------------------------------- geometry
class Rect:
    __slots__ = ("x0", "y0", "x1", "y1")

    def __init__(self, x0, y0, x1, y1):
        self.x0, self.y0, self.x1, self.y1 = x0, y0, x1, y1

    def grow(self, d):
        return Rect(self.x0 - d, self.y0 - d, self.x1 + d, self.y1 + d)

    def overlaps(self, o):
        return not (self.x1 <= o.x0 or o.x1 <= self.x0 or self.y1 <= o.y0 or o.y1 <= self.y0)

    def inside(self, o):
        return self.x0 >= o.x0 and self.y0 >= o.y0 and self.x1 <= o.x1 and self.y1 <= o.y1

    @property
    def w(self):
        return self.x1 - self.x0

    @property
    def h(self):
        return self.y1 - self.y0

    @property
    def cx(self):
        return (self.x0 + self.x1) / 2

    @property
    def cy(self):
        return (self.y0 + self.y1) / 2

    def __repr__(self):
        return f"Rect({self.x0:.1f},{self.y0:.1f},{self.x1:.1f},{self.y1:.1f})"


def fp_rect(fp):
    """Footprint extent in mm for collision checks: union of the pads/graphics bounding
    box, the courtyard (either side) and the reference text, so that placement avoids
    courtyard and silkscreen overlaps.  The Value text (F.Fab) is ignored."""
    boxes = [fp.GetBoundingBox(False, False), fp.Reference().GetBoundingBox()]
    for layer in (pcbnew.F_CrtYd, pcbnew.B_CrtYd):
        cy = fp.GetCourtyard(layer)
        if cy.OutlineCount():
            boxes.append(cy.BBox())
    return Rect(min(to_mm(b.GetLeft()) for b in boxes), min(to_mm(b.GetTop()) for b in boxes),
                max(to_mm(b.GetRight()) for b in boxes), max(to_mm(b.GetBottom()) for b in boxes))


def courtyard_rect(fp):
    for layer in (pcbnew.F_CrtYd, pcbnew.B_CrtYd):
        cy = fp.GetCourtyard(layer)
        if cy.OutlineCount():
            cb = cy.BBox()
            return Rect(to_mm(cb.GetLeft()), to_mm(cb.GetTop()), to_mm(cb.GetRight()), to_mm(cb.GetBottom()))
    bb = fp.GetBoundingBox(False, False)
    return Rect(to_mm(bb.GetLeft()), to_mm(bb.GetTop()), to_mm(bb.GetRight()), to_mm(bb.GetBottom()))


def place_centre(fp, cx, cy, rot=0.0, bottom=False):
    """Orient the footprint and move it so its courtyard centre lands on (cx, cy) mm."""
    if bottom and fp.GetLayer() != pcbnew.B_Cu:
        fp.Flip(fp.GetPosition(), False)
    fp.SetOrientationDegrees(rot if not bottom else -rot)
    fp.BuildCourtyardCaches()
    r = courtyard_rect(fp)
    p = fp.GetPosition()
    fp.SetPosition(pcbnew.VECTOR2I(p.x + mm(cx - r.cx), p.y + mm(cy - r.cy)))
    fp.BuildCourtyardCaches()


# ----------------------------------------------------------------------------- placement
class Placer:
    def __init__(self, board_rect, margin, pitch):
        self.board = board_rect
        self.margin = margin
        self.pitch = pitch
        self.placed = []          # (ref, Rect)

    def free(self, r):
        rr = r.grow(self.margin / 2)
        if not rr.inside(self.board.grow(-self.margin)):
            return False
        return not any(rr.overlaps(o) for _, o in self.placed)

    def add(self, ref, fp):
        self.placed.append((ref, fp_rect(fp)))

    def try_at(self, fp, cx, cy, rot):
        place_centre(fp, cx, cy, rot)
        return self.free(fp_rect(fp))

    def near(self, fp, anchor_rect, rots=(0, 90), target=None):
        """Ring outward from the anchor's edge; among the positions on a ring the one
        closest to `target` (default: the anchor centre) wins."""
        ar = anchor_rect
        tx, ty = target if target else (ar.cx, ar.cy)
        size = fp_rect(fp)
        step = self.pitch
        for ring in range(0, 400):
            d = ring * step
            # candidate centres along a rectangle at distance d outside the anchor
            half_w, half_h = size.w / 2, size.h / 2
            cands = []
            x0, x1 = ar.x0 - d - half_w, ar.x1 + d + half_w
            y0, y1 = ar.y0 - d - half_h, ar.y1 + d + half_h
            nx = max(1, int((x1 - x0) / step))
            ny = max(1, int((y1 - y0) / step))
            for i in range(nx + 1):
                x = x0 + (x1 - x0) * i / nx
                cands.append((x, y0)); cands.append((x, y1))
            for j in range(1, ny):
                y = y0 + (y1 - y0) * j / ny
                cands.append((x0, y)); cands.append((x1, y))
            # nearest to the target first, deterministic tie-break
            cands.sort(key=lambda c: (round(math.hypot(c[0] - tx, c[1] - ty), 3), c[1], c[0]))
            for cx, cy in cands:
                for rot in rots:
                    if self.try_at(fp, round(cx, 2), round(cy, 2), rot):
                        return True
        return False

    def anywhere(self, fp):
        """Scan the board from the top-left; used for parts with no anchor."""
        size = fp_rect(fp)
        step = max(self.pitch, 1.0)
        y = self.board.y0 + self.margin + size.h / 2
        while y + size.h / 2 <= self.board.y1 - self.margin:
            x = self.board.x0 + self.margin + size.w / 2
            while x + size.w / 2 <= self.board.x1 - self.margin:
                if self.try_at(fp, round(x, 2), round(y, 2), 0):
                    return True
                x += step
            y += step
        return False


# ----------------------------------------------------------------------------- board items
def add_outline(board, w, h, radius):
    def seg(x0, y0, x1, y1):
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(pcbnew.VECTOR2I_MM(x0, y0)); s.SetEnd(pcbnew.VECTOR2I_MM(x1, y1))
        s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.1))
        board.Add(s)

    def arc(sx, sy, mx, my, ex, ey):
        a = pcbnew.PCB_SHAPE(board)
        a.SetShape(pcbnew.SHAPE_T_ARC)
        a.SetArcGeometry(pcbnew.VECTOR2I_MM(sx, sy), pcbnew.VECTOR2I_MM(mx, my), pcbnew.VECTOR2I_MM(ex, ey))
        a.SetLayer(pcbnew.Edge_Cuts); a.SetWidth(mm(0.1))
        board.Add(a)

    r = radius
    if r <= 0:
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_RECT)
        s.SetStart(pcbnew.VECTOR2I_MM(0, 0)); s.SetEnd(pcbnew.VECTOR2I_MM(w, h))
        s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.1))
        board.Add(s)
        return
    k = r * (1 - math.sqrt(0.5))       # arc midpoint offset from the corner tangent lines
    seg(r, 0, w - r, 0); seg(w, r, w, h - r); seg(w - r, h, r, h); seg(0, h - r, 0, r)
    arc(w - r, 0, w - k, k, w, r)
    arc(w, h - r, w - k, h - k, w - r, h)
    arc(r, h, k, h - k, 0, h - r)
    arc(0, r, k, k, r, 0)


def layer_id(name):
    """'In1.Cu' -> pcbnew.In1_Cu.  BOARD.GetLayerID() only resolves copper names from Python."""
    try:
        return getattr(pcbnew, name.replace(".", "_"))
    except AttributeError:
        raise SystemExit(f"unknown layer {name!r}")


def add_zone(board, layer_name, net, w, h, inset):
    layer = layer_id(layer_name)
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNet(net)
    z.SetAssignedPriority(0)
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    z.SetMinThickness(mm(0.25))
    z.SetThermalReliefGap(mm(0.4))
    z.SetThermalReliefSpokeWidth(mm(0.5))
    z.SetLocalClearance(mm(0.3))
    z.SetIsFilled(False)
    pts = [(inset, inset), (w - inset, inset), (w - inset, h - inset), (inset, h - inset)]
    outline = z.Outline()
    outline.NewOutline()
    for x, y in pts:
        outline.Append(mm(x), mm(y))
    z.SetZoneName(f"{layer_name}_{net.GetNetname()}")
    board.Add(z)
    return z


def pad_target(fp, anchor_fp, k):
    """Where next to the anchor a small part wants to be: the anchor pads that share a net
    with the part (ignoring GND when anything else matches), sorted around the anchor; the
    k-th such part anchored here takes the k-th pad.  None if nothing is shared."""
    nets = {p.GetNetname() for p in fp.Pads() if p.GetNetCode() > 0}
    if not nets:
        return None
    pads = [p for p in anchor_fp.Pads() if p.GetNetname() in nets]
    non_gnd = [p for p in pads if p.GetNetname() != "GND"]
    pads = non_gnd or pads
    if not pads:
        return None
    c = anchor_fp.GetPosition()
    pads.sort(key=lambda p: math.atan2(p.GetPosition().y - c.y, p.GetPosition().x - c.x))
    p = pads[k % len(pads)].GetPosition()
    return to_mm(p.x), to_mm(p.y)


def strip_silk_outside(fp, board_rect):
    """Drop footprint silkscreen segments that reach outside the board (connector noses
    that overhang the edge); otherwise DRC reports 'silkscreen clipped by board edge'."""
    inner = board_rect.grow(-0.3)
    for g in list(fp.GraphicalItems()):
        if g.GetLayer() not in (pcbnew.F_SilkS, pcbnew.B_SilkS) or g.GetClass() != "PCB_SHAPE":
            continue
        bb = g.GetBoundingBox()
        r = Rect(to_mm(bb.GetLeft()), to_mm(bb.GetTop()), to_mm(bb.GetRight()), to_mm(bb.GetBottom()))
        if not r.inside(inner):
            fp.Remove(g)


def fanout(board, netnames, via_dia=0.8, via_drill=0.4, track_w=0.25, clearance=0.2, dists=(1.1, 1.3, 1.5, 1.8, 2.1, 2.4, 2.8)):
    """Give every SMD pad on the listed nets a locked via (to the inner planes) and a short
    locked track.  THT pads reach the planes through their barrels.  Returns (placed, failed).
    The via and its stub are checked against every other pad and via for copper and hole
    clearance; candidates are tried outward from the footprint centre first."""
    r_via = via_dia / 2
    pads = [(p, fp) for fp in board.GetFootprints() for p in fp.Pads()]
    pad_boxes = []
    for p, fp in pads:
        bb = p.GetBoundingBox()
        pad_boxes.append((p, Rect(to_mm(bb.GetLeft()), to_mm(bb.GetTop()), to_mm(bb.GetRight()), to_mm(bb.GetBottom())),
                          to_mm(p.GetDrillSize().x) / 2 if p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH) else 0.0))
    outline = board.GetBoardEdgesBoundingBox()
    brd = Rect(to_mm(outline.GetLeft()), to_mm(outline.GetTop()), to_mm(outline.GetRight()), to_mm(outline.GetBottom())).grow(-1.0)
    vias = []           # (x, y)
    stubs = []          # (x0, y0, x1, y1)
    placed, failed = 0, []
    # rule areas that forbid tracks or vias (the microSD socket's footprint keepouts)
    keepouts = []
    for z in list(board.Zones()) + [z for fp in board.GetFootprints() for z in fp.Zones()]:
        if z.GetIsRuleArea() and (z.GetDoNotAllowVias() or z.GetDoNotAllowTracks()):
            bb = z.GetBoundingBox()
            keepouts.append(Rect(to_mm(bb.GetLeft()), to_mm(bb.GetTop()), to_mm(bb.GetRight()), to_mm(bb.GetBottom())))

    def seg_dist(x, y, x0, y0, x1, y1):
        dx, dy = x1 - x0, y1 - y0
        l2 = dx * dx + dy * dy
        u = 0.0 if l2 == 0 else max(0.0, min(1.0, ((x - x0) * dx + (y - y0) * dy) / l2))
        return math.hypot(x - (x0 + u * dx), y - (y0 + u * dy))

    def ok(x, y, pad, net, own_dam=True):
        """(x, y) may hold the via: inside the board, clear of every other via, stub and pad
        (same-net copper included: the router sees each stub as its own net, and assemblers
        want solder dams), and at least a 0.25 mm dam from its own pad."""
        if not Rect(x - r_via, y - r_via, x + r_via, y + r_via).inside(brd):
            return False
        for vx, vy in vias:
            if math.hypot(vx - x, vy - y) < via_dia + clearance:
                return False
        for s in stubs:
            if seg_dist(x, y, *s) < r_via + track_w / 2 + clearance:
                return False
        for k in keepouts:
            if k.grow(r_via + clearance).overlaps(Rect(x, y, x, y)):
                return False
        for q, box, drill_r in pad_boxes:
            if q is pad:
                if own_dam and box.grow(r_via + 0.25).overlaps(Rect(x, y, x, y)):
                    return False
                continue
            if box.grow(r_via + clearance).overlaps(Rect(x, y, x, y)):
                return False
            # ... and hole-to-hole distance to every plated/unplated hole
            if drill_r and math.hypot(to_mm(q.GetPosition().x) - x, to_mm(q.GetPosition().y) - y) < drill_r + via_drill / 2 + 0.3:
                return False
        return True

    for p, fp in pads:
        net = p.GetNetname()
        if net not in netnames or p.GetAttribute() != pcbnew.PAD_ATTRIB_SMD:
            continue
        px, py = to_mm(p.GetPosition().x), to_mm(p.GetPosition().y)
        cx, cy = to_mm(fp.GetPosition().x), to_mm(fp.GetPosition().y)
        ang = math.atan2(py - cy, px - cx) if (px, py) != (cx, cy) else 0.0
        # an elongated pad (SOIC, SOT, connector) fans out along its own long axis, away
        # from the part: that is where the neighbours are not
        sx, sy = to_mm(p.GetSize().x), to_mm(p.GetSize().y)
        if max(sx, sy) > 1.3 * min(sx, sy):
            th = math.radians(p.GetOrientationDegrees())
            ux, uy = (math.cos(th), -math.sin(th)) if sx > sy else (math.sin(th), math.cos(th))
            if ux * (px - cx) + uy * (py - cy) < 0:
                ux, uy = -ux, -uy
            ang = math.atan2(uy, ux)
        found = None
        for d in dists:
            for da in (0, 0.5, -0.5, 1.0, -1.0, 1.57, -1.57, 2.1, -2.1, 3.14):
                x, y = px + d * math.cos(ang + da), py + d * math.sin(ang + da)
                x, y = round(x, 2), round(y, 2)
                # the stub must not run through foreign copper either: test its midpoint
                mx, my = (px + x) / 2, (py + y) / 2
                if ok(x, y, p, net) and ok(mx, my, p, net, own_dam=False):
                    found = (x, y)
                    break
            if found:
                break
        if not found:
            failed.append(f"{fp.GetReference()}-{p.GetNumber()}")
            continue
        x, y = found
        via = pcbnew.PCB_VIA(board)
        via.SetViaType(pcbnew.VIATYPE_THROUGH)
        via.SetPosition(pcbnew.VECTOR2I_MM(x, y))
        via.SetWidth(mm(via_dia))
        via.SetDrill(mm(via_drill))
        via.SetNet(p.GetNet())
        via.SetLocked(True)
        board.Add(via)
        tr = pcbnew.PCB_TRACK(board)
        tr.SetStart(pcbnew.VECTOR2I_MM(px, py))
        tr.SetEnd(pcbnew.VECTOR2I_MM(x, y))
        tr.SetWidth(mm(track_w))
        tr.SetLayer(pcbnew.F_Cu if fp.GetLayer() == pcbnew.F_Cu else pcbnew.B_Cu)
        tr.SetNet(p.GetNet())
        tr.SetLocked(True)
        board.Add(tr)
        vias.append((x, y))
        stubs.append((px, py, x, y))
        placed += 1
    return placed, failed


def add_text(board, text, x, y, size=2.0, layer="F.SilkS"):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(text)
    t.SetPosition(pcbnew.VECTOR2I_MM(x, y))
    t.SetLayer(layer_id(layer))
    t.SetTextSize(pcbnew.VECTOR2I_MM(size, size))
    t.SetTextThickness(mm(size * 0.15))
    board.Add(t)


def fill_zones(board):
    board.BuildConnectivity()
    filler = pcbnew.ZONE_FILLER(board)
    filler.Fill(board.Zones())


def fill_zones_in_file(path):
    """ZONE_FILLER segfaults on a BOARD() built in memory (no project behind it); a board
    loaded from disk has one, so fill in a second pass."""
    board = pcbnew.LoadBoard(path)
    if len(board.Zones()):
        fill_zones(board)
        pcbnew.SaveBoard(path, board)
    return board


def set_layers(board, n):
    """Copper layer count.  The physical stackup table (BOARD_STACKUP) is not reachable from
    the pcbnew Python API in 9.0.9; KiCad applies its default 4-layer stackup when the
    board is opened or plotted, which is what the Gerbers need."""
    board.GetDesignSettings().SetCopperLayerCount(n)


# ----------------------------------------------------------------------------- build
def build(args):
    comps, nets = read_netlist(args.netlist)
    board = pcbnew.BOARD()
    netinfo = {}
    for n in sorted(nets, key=lambda n: n["code"]):
        item = pcbnew.NETINFO_ITEM(board, n["name"], n["code"])
        board.Add(item)
        netinfo[n["name"]] = item
    pad_net = {}
    for n in nets:
        for ref, pin in n["nodes"]:
            pad_net[(ref, pin)] = netinfo[n["name"]]

    fps = []
    for c in comps:
        if not c["footprint"]:
            print(f"warning: {c['ref']} has no footprint, skipped")
            continue
        fp = load_footprint(c["footprint"])
        fp.SetReference(c["ref"])
        fp.SetValue(c["value"])
        # some connector footprints carry a "board edge here" line on Edge.Cuts; it would
        # break the real outline, so drop it (the courtyard still says where the part is)
        for g in list(fp.GraphicalItems()):
            if g.GetLayer() == pcbnew.Edge_Cuts:
                fp.Remove(g)
        if c["dnp"]:
            fp.SetAttributes(fp.GetAttributes() | pcbnew.FP_DNP | pcbnew.FP_EXCLUDE_FROM_POS_FILES)
        for pad in fp.Pads():
            net = pad_net.get((c["ref"], pad.GetNumber()))
            if net is not None:
                pad.SetNet(net)
        board.Add(fp)
        fps.append((c, fp))

    if args.placement:
        with open(args.placement, encoding="utf-8") as f:
            pl = json.load(f)
        bw, bh = pl["board"]["width"], pl["board"]["height"]
        build_placed(board, fps, pl, bw, bh)
        set_layers(board, int(pl["board"].get("layers", 4)))
        add_outline(board, bw, bh, float(pl["board"].get("corner_radius", 0)))
        for layer_name, netname in pl["board"].get("zones", {}).items():
            if netname not in netinfo:
                raise SystemExit(f"zone net {netname!r} is not in the netlist")
            add_zone(board, layer_name, netinfo[netname], bw, bh, 0.5)
        for t in pl.get("texts", []):
            add_text(board, t["text"], t["x"], t["y"], t.get("size", 2.0), t.get("layer", "F.SilkS"))
        fo = pl.get("fanout")
        if fo:
            n, failed = fanout(board, set(fo["nets"]), fo.get("via_diameter", 0.8), fo.get("via_drill", 0.4),
                               fo.get("track_width", 0.25))
            print(f"fanout: {n} vias for {', '.join(fo['nets'])}" + (f"; no room for {failed}" if failed else ""))
    else:
        bw, bh = build_grid(board, fps, args)
        add_outline(board, bw, bh, 0)

    pcbnew.SaveBoard(args.out, board)
    nzones = len(board.Zones())
    if nzones:
        fill_zones_in_file(args.out)
    print(f"wrote {args.out}: {len(fps)} footprints, {len(nets)} nets, outline {bw} x {bh} mm, "
          f"{board.GetCopperLayerCount()} copper layers, {nzones} zones")


def fp_description(fp):
    for name in ("GetLibDescription", "GetDescription"):
        if hasattr(fp, name):
            return getattr(fp, name)()
    return ""


def is_zif(fp):
    name = str(fp.GetFPID().GetLibItemName())
    return "zero insertion" in fp_description(fp).lower() or "zif" in name.lower()


def check_sockets(fps, checks):
    """placement.json "checks": {"sockets": true} demands a socket footprint for every DIP
    and PLCC part (the socket body is bigger than the chip, and a ZIF socket's lever
    travel is part of its courtyard, so placement must be done with the socket, not the
    chip); "zif": [refs] demands a zero-insertion-force socket for those parts.  Prints the
    socket table and raises SystemExit on the first class of failure."""
    errors = []
    rows = []
    for c, fp in fps:
        name = str(fp.GetFPID().GetLibItemName())
        lib = str(fp.GetFPID().GetLibNickname())
        dip = name.startswith(("DIP-", "DIP_", "PLCC-", "CERDIP-"))
        socket = lib == "Socket" or "socket" in name.lower()
        if dip or socket:
            r = courtyard_rect(fp)
            rows.append((c["ref"], name, r.w, r.h, "ZIF, lever travel inside courtyard" if is_zif(fp) else ("socket" if socket else "NO SOCKET")))
        if checks.get("sockets") and dip and not socket:
            errors.append(f"{c['ref']}: {name} is a bare chip footprint, a socket footprint is required")
        if c["ref"] in checks.get("zif", []) and not is_zif(fp):
            errors.append(f"{c['ref']}: {name} is not a ZIF socket footprint")
    if rows:
        print("sockets (courtyard w x h mm):")
        for ref, name, w, h, note in sorted(rows, key=lambda r: natural(r[0])):
            print(f"  {ref:4s} {name[:52]:52s} {w:5.1f} x {h:5.1f}  {note}")
    if errors:
        raise SystemExit("socket check failed:\n  " + "\n  ".join(errors))


def local_to_board(fp, x, y):
    """Footprint-local mm (unrotated, y down, origin at the footprint anchor) -> board mm."""
    a = math.radians(fp.GetOrientationDegrees())
    px, py = to_mm(fp.GetPosition().x), to_mm(fp.GetPosition().y)
    return px + x * math.cos(a) + y * math.sin(a), py - x * math.sin(a) + y * math.cos(a)


def add_keepout(board, fp, rect, why):
    """A rule area on the footprint's copper side that forbids other footprints (tracks,
    vias and pads stay allowed): DRC then reports anything placed under a lever or an
    extractor.  rect = (x0, y0, x1, y1) in footprint-local mm.  Returns the board Rect."""
    x0, y0, x1, y1 = rect
    pts = [local_to_board(fp, x, y) for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1))]
    z = pcbnew.ZONE(board)
    z.SetIsRuleArea(True)
    z.SetDoNotAllowFootprints(True)
    z.SetDoNotAllowTracks(False)
    z.SetDoNotAllowVias(False)
    z.SetDoNotAllowPads(False)
    z.SetDoNotAllowCopperPour(False)
    z.SetLayer(fp.GetLayer())
    outline = z.Outline()
    outline.NewOutline()
    for x, y in pts:
        outline.Append(mm(x), mm(y))
    z.SetZoneName(f"keepout {fp.GetReference()} {why}")
    board.Add(z)
    return Rect(min(p[0] for p in pts), min(p[1] for p in pts), max(p[0] for p in pts), max(p[1] for p in pts))


def build_placed(board, fps, pl, bw, bh):
    parts = pl.get("parts", {})
    auto = pl.get("auto", {})
    check_sockets(fps, pl.get("checks", {}))
    placer = Placer(Rect(0, 0, bw, bh), float(auto.get("margin", 0.8)), float(auto.get("pitch", 0.5)))
    by_ref = {c["ref"]: (c, fp) for c, fp in fps}
    unknown = sorted(set(parts) - set(by_ref))
    if unknown:
        print("warning: placement.json names parts not on the board:", ", ".join(unknown))
    # 1. explicitly placed parts; their courtyards (which include a ZIF socket's lever travel)
    #    and keepouts must not collide
    overlaps = []
    for ref in sorted(parts, key=natural):
        if ref not in by_ref:
            continue
        p = parts[ref]
        c, fp = by_ref[ref]
        place_centre(fp, float(p["x"]), float(p["y"]), float(p.get("rot", 0)), p.get("side", "top") == "bottom")
        strip_silk_outside(fp, placer.board)
        r = fp_rect(fp)
        for oref, orect in placer.placed:
            if r.overlaps(orect):
                msg = f"{ref} overlaps {oref} in placement.json ({r} vs {orect})"
                if p.get("allow_overlap") or parts.get(oref.split()[0], {}).get("allow_overlap"):
                    print("warning:", msg)
                else:
                    overlaps.append(msg)
        if not courtyard_rect(fp).inside(placer.board):
            print(f"note: {ref} courtyard extends past the board outline (fine for an edge connector)")
        placer.add(ref, fp)
        for k, ko in enumerate(p.get("keepout", [])):
            why = ko[4] if len(ko) > 4 else f"keepout{k}"
            kr = add_keepout(board, fp, ko[:4], why)
            for oref, orect in placer.placed:
                if oref != ref and kr.overlaps(orect):
                    overlaps.append(f"{oref} is inside the {why} keepout of {ref} ({orect} vs {kr})")
            placer.placed.append((f"{ref} {why}", kr))
    if overlaps:
        raise SystemExit("placement check failed (hand-placed parts collide; move them in placement.json, "
                         "or set \"allow_overlap\": true on one of them if it is intended):\n  " + "\n  ".join(overlaps))
    # 2. everything else, next to its PcbNear anchor, steered toward the anchor pads it connects to
    rest = [(c, fp) for c, fp in fps if c["ref"] not in parts]
    rest.sort(key=lambda cf: natural(cf[0]["ref"]))
    anchors = {ref: r for ref, r in placer.placed}
    anchor_fp = {c["ref"]: fp for c, fp in fps}
    per_anchor = {}
    for c, fp in rest:
        near = c["fields"].get("PcbNear")
        ok = False
        if near and near in anchors:
            k = per_anchor.get(near, 0)
            per_anchor[near] = k + 1
            ok = placer.near(fp, anchors[near], target=pad_target(fp, anchor_fp[near], k))
        elif near:
            print(f"warning: {c['ref']}: PcbNear anchor {near!r} is not placed yet; scanning the board")
        if not ok:
            ok = placer.anywhere(fp)
        if not ok:
            raise SystemExit(f"no room for {c['ref']} on a {bw} x {bh} board")
        placer.add(c["ref"], fp)
        anchors[c["ref"]] = fp_rect(fp)


def build_grid(board, fps, args):
    """Original spike behaviour: largest first, rows of width args.width."""
    def size(fp):
        bb = fp.GetBoundingBox(False, False)
        return to_mm(bb.GetWidth()), to_mm(bb.GetHeight())

    fps.sort(key=lambda cf: (-size(cf[1])[1], -size(cf[1])[0], cf[0]["ref"]))
    pitch = args.pitch
    x = y = pitch
    row_h = 0.0
    max_x = 0.0
    for c, fp in fps:
        w, h = size(fp)
        hint = c["fields"]
        if "PcbRot" in hint:
            fp.SetOrientationDegrees(float(hint["PcbRot"]))
            w, h = size(fp)
        if "PcbX" in hint and "PcbY" in hint:
            fp.SetPosition(pcbnew.VECTOR2I_MM(float(hint["PcbX"]), float(hint["PcbY"])))
            continue
        if x > pitch and x + w > args.width:
            x = pitch
            y += row_h + pitch
            row_h = 0.0
        bb = fp.GetBoundingBox(False, False)
        off_x = to_mm(fp.GetPosition().x - bb.GetLeft())
        off_y = to_mm(fp.GetPosition().y - bb.GetTop())
        fp.SetPosition(pcbnew.VECTOR2I_MM(round(x + off_x, 2), round(y + off_y, 2)))
        x += w + pitch
        row_h = max(row_h, h)
        max_x = max(max_x, x)
    return math.ceil(max(max_x, args.width) + 0.0), math.ceil(y + row_h + pitch)


# ----------------------------------------------------------------------------- DSN / SES
STRIP_DSN_GROUPS = ("outline", "keepout")


def strip_outlines(text, heads=STRIP_DSN_GROUPS):
    """Remove every balanced "(outline ...)" and "(keepout ...)" group from a DSN.  Outlines
    are footprint silkscreen/courtyard drawings, keepouts the rule areas some footprints
    carry (the Hirose microSD socket); freerouting 2.5.0's native CLI build crashes on both
    (GraalVM serialization of ComponentOutline / ObstacleArea) and then never finishes.
    The board boundary is a separate "(boundary ...)" element and is kept."""
    out, i, n = [], 0, len(text)
    while i < n:
        head = next((h for h in heads if text.startswith("(" + h, i) and text[i + 1 + len(h)] in " \t\n("), None)
        if head is not None:
            depth = 0
            j = i
            while j < n:
                if text[j] == "(":
                    depth += 1
                elif text[j] == ")":
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            i = j + 1
            while out and out[-1] in " \t":      # drop the indentation that preceded it
                out.pop()
            while i < n and text[i] in " \t":
                i += 1
            if i < n and text[i] == "\n":
                i += 1
            continue
        out.append(text[i])
        i += 1
    return "".join(out)


def strip_groups(text, pred):
    """Remove every balanced group for which pred(head_text) is true; head_text is the
    text from '(' up to the first newline or ')' of the group."""
    out, i, n = [], 0, len(text)
    while i < n:
        if text[i] == "(":
            j = i + 1
            while j < n and text[j] not in "\n)":
                j += 1
            if pred(text[i:j]):
                depth, k = 0, i
                while k < n:
                    if text[k] == "(":
                        depth += 1
                    elif text[k] == ")":
                        depth -= 1
                        if depth == 0:
                            break
                    k += 1
                i = k + 1
                while out and out[-1] in " \t":
                    out.pop()
                while i < n and text[i] in " \t":
                    i += 1
                if i < n and text[i] == "\n":
                    i += 1
                continue
        out.append(text[i])
        i += 1
    return "".join(out)


def planes_to_kicad_only(text, power_layers, plane_nets):
    """freerouting 2.5.0 (native build) cannot hold copper areas at all, so the zones stay
    in KiCad only: drop the (plane ...) groups, mark the plane layers (type power) so the
    router keeps signals off them, and take the plane nets out of the routed network (the
    locked fanout vias and the THT barrels connect them in KiCad's zone fill)."""
    text = strip_groups(text, lambda h: h.startswith("(plane "))
    for layer in power_layers:
        text = re.sub(r"(\(layer %s\s*\n\s*\(type )signal\)" % re.escape(layer), r"\1power)", text)
    for net in plane_nets:
        text = strip_groups(text, lambda h, net=net: re.match(r"\(net %s(\s|$)" % re.escape(net), h) is not None)
        # drop the net from every (class ...) member list
        def fix_class(m, net=net):
            return re.sub(r"(?<=\s)%s(?=\s)" % re.escape(net), "", m.group(0))
        text = re.sub(r"\(class [^()]*", fix_class, text)
    return text


def island_nets(text, board, plane_nets):
    """Keep every locked fanout stub (SMD pad + track + via) on a plane net together under
    its own DSN net name per footprint (GND__C12 ...).  Without this the stubs become netless obstacles
    once the plane net is dropped, and freerouting reports each via sitting on its own
    track as an 'unfixable clearance violation' (309 of them on this board).  THT pads on
    the plane nets stay netless: their barrels reach the planes in KiCad.  Returns
    (text, number of islands)."""
    pads = {}
    for fp in board.GetFootprints():
        seen = {}
        for p in fp.Pads():
            # KiCad's DSN exporter names repeated pad numbers 11, 11@1, 11@2 ... in pad order
            n = p.GetNumber()
            k = seen.get(n, 0)
            seen[n] = k + 1
            if p.GetNetname() in plane_nets:
                pos = p.GetPosition()
                pads[(round(pos.x / 1000), round(pos.y / 1000))] = f"{fp.GetReference()}-{n}" + (f"@{k}" if k else "")

    def key(xu, yu):          # DSN um, y up  ->  board um, y down
        return (round(float(xu)), -round(float(yu)))
    num = r"(-?\d+(?:\.\d+)?)"          # the export writes half-um pad positions as 57302.5
    wire_re = re.compile(r"^(\s*\(wire \(path \S+ \d+\s+)%s %s\s+%s %s\)\(net (\S+)\)(\(type fix\)\))\s*$" % (num, num, num, num), re.M)
    via_re = re.compile(r"^(\s*\(via \S+\s+)%s %s \(net (\S+)\)(\(type fix\)\))\s*$" % (num, num), re.M)
    islands = {}              # via position -> (name, pin)
    nets = set(plane_nets)

    def wire(m):
        net = m.group(6)
        if net not in nets:
            return m.group(0)
        a, b = key(m.group(2), m.group(3)), key(m.group(4), m.group(5))
        pin, via = (pads.get(a), b) if a in pads else (pads.get(b), a)
        if pin is None:
            return m.group(0)
        # one island per pad: pad, stub and via are one finished net (GND__C12-2); pads of
        # the same footprint stay separate, or the router would wire them to each other
        name = islands.setdefault(via, (f"{net}__{pin.replace('@', '_')}", pin))[0]
        return f"{m.group(1)}{m.group(2)} {m.group(3)}  {m.group(4)} {m.group(5)})(net {name}){m.group(7)}"
    text = wire_re.sub(wire, text)

    def via(m):
        isl = islands.get(key(m.group(2), m.group(3)))
        if m.group(4) not in nets or isl is None:
            return m.group(0)
        return f"{m.group(1)}{m.group(2)} {m.group(3)} (net {isl[0]}){m.group(5)}"
    text = via_re.sub(via, text)
    pins_by_net = {}
    for name, pin in islands.values():
        if pin not in pins_by_net.setdefault(name, []):
            pins_by_net[name].append(pin)
    entries = "".join(f"    (net {name}\n      (pins {' '.join(pins)})\n    )\n" for name, pins in pins_by_net.items())
    text = text.replace("  (network\n", "  (network\n" + entries, 1)
    return text, len(islands)


def drop_layers(text, layers):
    """Turn the DSN into a board without the given copper layers: freerouting ignores
    (type power), so the plane layers are simply not offered to it.  Remaining layers are
    re-indexed in order of appearance."""
    for layer in layers:
        text = strip_groups(text, lambda h, layer=layer: h.startswith("(layer %s" % layer))
        text = re.sub(r"^[ \t]*\(shape \((?:circle|rect|polygon|path) %s[^\n]*\)\)\n" % re.escape(layer), "", text, flags=re.M)
        text = re.sub(r"^[ \t]*\((?:wire|plane) [^\n]*\(path %s[^\n]*\n(?:[^\n]*\n)*?[ \t]*\)\n" % re.escape(layer), "", text, flags=re.M)
    idx = [0]

    def renumber(m):
        s = m.group(1) + str(idx[0]) + m.group(3)
        idx[0] += 1
        return s
    text = re.sub(r"(\(layer \S+\s*\n\s*\(type \w+\)\s*\n\s*\(property\s*\n\s*\(index )(\d+)(\))", renumber, text)
    return text


def dsn_coord(v):
    return f"{v:g}"


def synth_obstacles(text, board):
    """Add netless pins to the footprint images for what KiCad does not export: NPTH holes
    (as copper circles drill + 0.6 mm on both outer layers) and footprint keepout zones (as
    copper rectangles on the zone's layers).  freerouting keeps clear of pins without a net.
    DSN image coordinates are footprint-local, unrotated, with y pointing up (um)."""
    padstacks = {}
    image_pins = {}          # image base name -> list of pin lines
    for fp in board.GetFootprints():
        base = str(fp.GetFPID().GetLibItemName())
        pins = []
        for i, p in enumerate(fp.Pads()):
            if p.GetAttribute() != pcbnew.PAD_ATTRIB_NPTH:
                continue
            d = to_mm(max(p.GetDrillSize().x, p.GetDrillSize().y)) + 0.6
            name = f"NPTH_{int(round(d * 1000))}um"
            padstacks[name] = f'(padstack {name}\n      (shape (circle F.Cu {d*1000:g}))\n      (shape (circle B.Cu {d*1000:g}))\n      (attach off)\n    )'
            rel = p.GetFPRelativePosition()
            pins.append(f"(pin {name} NPTH{i} {to_mm(rel.x)*1000:g} {-to_mm(rel.y)*1000:g})")
        for k, z in enumerate(fp.Zones()):
            if not z.GetIsRuleArea():
                continue
            bb = z.GetBoundingBox()
            corners = [(bb.GetLeft(), bb.GetTop()), (bb.GetRight(), bb.GetBottom())]
            loc = []
            for x, y in corners:
                v = pcbnew.VECTOR2I(x - fp.GetPosition().x, y - fp.GetPosition().y)
                rad = math.radians(fp.GetOrientationDegrees())   # same sense as GetFPRelativePosition()
                lx = v.x * math.cos(rad) - v.y * math.sin(rad)
                ly = v.x * math.sin(rad) + v.y * math.cos(rad)
                loc.append((to_mm(lx) * 1000, -to_mm(ly) * 1000))
            x0, x1 = sorted((loc[0][0], loc[1][0]))
            y0, y1 = sorted((loc[0][1], loc[1][1]))
            cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
            w, h = x1 - x0, y1 - y0
            layers = [ln for ln in ("F.Cu", "B.Cu") if z.GetLayerSet().Contains(layer_id(ln))]
            if not layers:
                continue
            name = f"KEEPOUT_{int(round(w))}x{int(round(h))}_{''.join(l[0] for l in layers)}"
            shapes = "\n".join(f"      (shape (rect {ln} {-w/2:g} {-h/2:g} {w/2:g} {h/2:g}))" for ln in layers)
            padstacks[name] = f"(padstack {name}\n{shapes}\n      (attach off)\n    )"
            pins.append(f"(pin {name} KEEPOUT{k} {cx:g} {cy:g})")
        if pins:
            image_pins.setdefault(base, pins)
    if not image_pins:
        return text
    # insert the pins at the end of every matching image block (names may be quoted / "::N")
    for base, pins in image_pins.items():
        pattern = re.compile(r'(\(image "?%s(?:::\d+)?"?\n(?:.*\n)*?)([ \t]*\)\n)' % re.escape(base))
        text = pattern.sub(lambda m: m.group(1) + "".join(f"      {p}\n" for p in pins) + m.group(2), text)
    # padstack definitions go in the library next to the via padstacks
    block = "".join(f"    {ps}\n" for ps in padstacks.values())
    text = text.replace("    (padstack \"Via[", block + "    (padstack \"Via[", 1)
    return text


def inset_boundary(text, inset_mm=0.6):
    """Shrink the (boundary (path pcb ...)) polygon toward its centre by inset_mm so the
    router's zero-clearance-to-outline becomes KiCad's edge clearance (0.5 mm + margin).
    Good enough for a rounded rectangle: corners move slightly inside the true arc."""
    m = re.search(r"\(boundary\s*\(path pcb (\d+)\s+([-\d.\s]+)\)\s*\)", text)
    if not m:
        return text
    nums = [float(v) for v in m.group(2).split()]
    pts = list(zip(nums[0::2], nums[1::2]))
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
    d = inset_mm * 1000
    sx = (max(xs) - min(xs) - 2 * d) / (max(xs) - min(xs))
    sy = (max(ys) - min(ys) - 2 * d) / (max(ys) - min(ys))
    new = " ".join(f"{cx + (x - cx) * sx:.0f} {cy + (y - cy) * sy:.0f}" for x, y in pts)
    return text[:m.start(2)] + new + text[m.end(2):]


def dsn(args):
    board = pcbnew.LoadBoard(args.board)
    ok = pcbnew.ExportSpecctraDSN(board, args.out)
    if ok:
        with open(args.out, encoding="utf-8") as f:
            text = f.read()
        if args.strip_outlines:
            text = strip_outlines(text)
        if args.power_layers or args.plane_nets:
            plane_nets = [s for s in args.plane_nets.split(",") if s]
            text, n = island_nets(text, board, plane_nets)
            print(f"dsn: {n} fanout islands kept as their own nets")
            text = planes_to_kicad_only(text, [], plane_nets)
            text = drop_layers(text, [s for s in args.power_layers.split(",") if s])
        text = synth_obstacles(text, board)
        text = inset_boundary(text)
        with open(args.out, "w", encoding="utf-8") as f:
            f.write(text)
    print("dsn", "ok" if ok else "FAILED", args.out)
    sys.exit(0 if ok else 1)


def ses(args):
    board = pcbnew.LoadBoard(args.board)
    # the DSN was a 2-layer board, so the session's via padstacks are "Via[0-1]_...": on the
    # 4-layer board KiCad would read them as blind vias F.Cu-In1.Cu; rename them first
    with open(args.ses, encoding="utf-8") as f:
        text = f.read()
    fixed = re.sub(r"Via\[0-\d\]_", "Via[0-%d]_" % (board.GetCopperLayerCount() - 1), text)
    ses_path = args.ses
    if fixed != text:
        ses_path = args.out + ".ses"
        with open(ses_path, "w", encoding="utf-8") as f:
            f.write(fixed)
    # the session carries every routed (unlocked) item again and KiCad's import keeps the
    # old ones: drop the board's copy first (keeping the locked fanout).  Removed tracks
    # leave dangling SWIG wrappers that break the board after an import, so go through a
    # saved copy.
    unlocked = [t for t in board.GetTracks() if not t.IsLocked()]
    if unlocked:
        for t in unlocked:
            board.Remove(t)
        pcbnew.SaveBoard(args.out, board)
        board = pcbnew.LoadBoard(args.out)
    ok = pcbnew.ImportSpecctraSES(board, ses_path)
    if not ok:
        sys.exit("SES import failed")
    for t in board.GetTracks():
        if t.GetClass() == "PCB_VIA":
            t.SetViaType(pcbnew.VIATYPE_THROUGH)
            t.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
    pcbnew.SaveBoard(args.out, board)
    if len(board.Zones()):
        board = fill_zones_in_file(args.out)
    tracks = board.GetTracks()
    nvia = sum(1 for t in tracks if t.GetClass() == "PCB_VIA")
    print(f"ses ok: {len(tracks) - nvia} track segments, {nvia} vias -> {args.out}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build"); b.add_argument("netlist"); b.add_argument("out")
    b.add_argument("--placement", help="placement.json (positions, outline, layers, zones)")
    b.add_argument("--width", type=float, default=120.0, help="row width in mm (grid mode)")
    b.add_argument("--pitch", type=float, default=4.0, help="gap between footprints in mm (grid mode)")
    b.set_defaults(func=build)
    d = sub.add_parser("dsn"); d.add_argument("board"); d.add_argument("out")
    d.add_argument("--keep-outlines", dest="strip_outlines", action="store_false",
                   help="keep footprint outlines in the DSN (crashes freerouting-cli 2.5.0)")
    d.add_argument("--power-layers", default="", help="comma list of plane layers to leave out of the DSN, e.g. In1.Cu,In2.Cu")
    d.add_argument("--plane-nets", default="", help="comma list of nets served by zones: removed from the routed network")
    d.set_defaults(func=dsn)
    s = sub.add_parser("ses"); s.add_argument("board"); s.add_argument("ses"); s.add_argument("out"); s.set_defaults(func=ses)
    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
