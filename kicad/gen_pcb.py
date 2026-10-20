#!/usr/bin/env python3
"""gen_pcb.py - PCB side of the flow, using KiCad's pcbnew Python module.

kicad-cli 9.0.9 has no "import netlist into a board" command, so this script does the
job: it reads the kicadsexpr netlist exported from the schematic, loads each component's
footprint from the system libraries, assigns pad nets, places footprints on a grid inside
a rectangular Edge.Cuts outline and saves a .kicad_pcb.  It also wraps the Specctra
DSN export / SES import used with freerouting.

    python3 gen_pcb.py build <netlist.net> <out.kicad_pcb> [--width MM] [--pitch MM]
    python3 gen_pcb.py dsn   <board.kicad_pcb> <out.dsn>
    python3 gen_pcb.py ses   <board.kicad_pcb> <in.ses> <out.kicad_pcb>

Placement hints: a component field "PcbX"/"PcbY" (mm) or "PcbRot" (degrees) in the
netlist (i.e. a property on the schematic symbol) overrides the grid position.
"""
import argparse
import math
import os
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_sch import parse, find, find_all  # noqa: E402

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
        comps.append({"ref": find(c, "ref")[1], "value": find(c, "value")[1],
                      "footprint": (find(c, "footprint") or [None, ""])[1], "fields": fields})
    nets = []
    for n in find_all(find(top, "nets"), "net"):
        nodes = [(find(nd, "ref")[1], find(nd, "pin")[1]) for nd in find_all(n, "node")]
        nets.append({"code": int(find(n, "code")[1]), "name": find(n, "name")[1], "nodes": nodes})
    return comps, nets


def load_footprint(fpid):
    lib, name = fpid.split(":", 1)
    fp = pcbnew.FootprintLoad(os.path.join(FOOTPRINT_DIR, lib + ".pretty"), name)
    if fp is None:
        raise SystemExit(f"footprint {fpid} not found under {FOOTPRINT_DIR}")
    return fp


def mm(v):
    return pcbnew.FromMM(v)


def build(args):
    comps, nets = read_netlist(args.netlist)
    board = pcbnew.BOARD()
    # nets first so pads can reference them
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
        for pad in fp.Pads():
            net = pad_net.get((c["ref"], pad.GetNumber()))
            if net is not None:
                pad.SetNet(net)
        board.Add(fp)
        fps.append((c, fp))

    # grid placement: largest first, rows of width args.width
    def size(fp):
        bb = fp.GetBoundingBox(False, False)
        return pcbnew.ToMM(bb.GetWidth()), pcbnew.ToMM(bb.GetHeight())

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
        # SetPosition moves the footprint origin; shift so the bounding box's corner is at (x, y)
        bb = fp.GetBoundingBox(False, False)
        off_x = pcbnew.ToMM(fp.GetPosition().x - bb.GetLeft())
        off_y = pcbnew.ToMM(fp.GetPosition().y - bb.GetTop())
        fp.SetPosition(pcbnew.VECTOR2I_MM(round(x + off_x, 2), round(y + off_y, 2)))
        x += w + pitch
        row_h = max(row_h, h)
        max_x = max(max_x, x)
    bw, bh = math.ceil(max(max_x, args.width) + 0.0), math.ceil(y + row_h + pitch)

    outline = pcbnew.PCB_SHAPE(board)
    outline.SetShape(pcbnew.SHAPE_T_RECT)
    outline.SetStart(pcbnew.VECTOR2I_MM(0, 0))
    outline.SetEnd(pcbnew.VECTOR2I_MM(bw, bh))
    outline.SetLayer(pcbnew.Edge_Cuts)
    outline.SetWidth(mm(0.1))
    board.Add(outline)

    pcbnew.SaveBoard(args.out, board)
    print(f"wrote {args.out}: {len(fps)} footprints, {len(nets)} nets, outline {bw} x {bh} mm")


def strip_outlines(text):
    """Remove every balanced "(outline ...)" group from a DSN.  These are footprint
    silkscreen/courtyard drawings; freerouting 2.5.0's native CLI build crashes on them
    (GraalVM serialization of ComponentOutline).  The board boundary is a separate
    "(boundary ...)" element and is kept."""
    out, i, n = [], 0, len(text)
    while i < n:
        if text.startswith("(outline", i) and text[i + 8] in " \t\n(":
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
            # swallow the rest of the line if it is now blank
            while i < n and text[i] in " \t":
                i += 1
            if i < n and text[i] == "\n":
                i += 1
            continue
        out.append(text[i])
        i += 1
    return "".join(out)


def dsn(args):
    board = pcbnew.LoadBoard(args.board)
    ok = pcbnew.ExportSpecctraDSN(board, args.out)
    if ok and args.strip_outlines:
        with open(args.out, encoding="utf-8") as f:
            text = f.read()
        with open(args.out, "w", encoding="utf-8") as f:
            f.write(strip_outlines(text))
    print("dsn", "ok" if ok else "FAILED", args.out)
    sys.exit(0 if ok else 1)


def ses(args):
    board = pcbnew.LoadBoard(args.board)
    ok = pcbnew.ImportSpecctraSES(board, args.ses)
    if not ok:
        sys.exit("SES import failed")
    pcbnew.SaveBoard(args.out, board)
    tracks = board.GetTracks()
    nvia = sum(1 for t in tracks if t.GetClass() == "PCB_VIA")
    print(f"ses ok: {len(tracks) - nvia} track segments, {nvia} vias -> {args.out}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build"); b.add_argument("netlist"); b.add_argument("out")
    b.add_argument("--width", type=float, default=120.0, help="row width in mm")
    b.add_argument("--pitch", type=float, default=4.0, help="gap between footprints in mm")
    b.set_defaults(func=build)
    d = sub.add_parser("dsn"); d.add_argument("board"); d.add_argument("out")
    d.add_argument("--keep-outlines", dest="strip_outlines", action="store_false",
                   help="keep footprint outlines in the DSN (crashes freerouting-cli 2.5.0)")
    d.set_defaults(func=dsn)
    s = sub.add_parser("ses"); s.add_argument("board"); s.add_argument("ses"); s.add_argument("out"); s.set_defaults(func=ses)
    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
