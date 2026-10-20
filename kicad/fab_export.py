#!/usr/bin/env python3
"""fab_export.py - assembly package for JLCPCB and PCBWay from the generated board.

    python3 fab_export.py <netlist.xml> <board.kicad_pcb> <outdir> [--parts parts.json] [--name NAME]

Reads the sourcing fields board.py attached to every symbol (Manufacturer, MPN, LCSC,
Assembly, Desc, Socket_*; see parts.json) from the XML netlist and the footprint
positions from the board, and writes into <outdir>:

    NAME-jlcpcb-bom.csv       Comment, Designator, Footprint, LCSC Part #     (assembled parts)
    NAME-jlcpcb-cpl.csv       Designator, Mid X, Mid Y, Layer, Rotation        (assembled parts)
    NAME-pcbway-bom.csv       Item, Designator, Qty, Manufacturer, Mfg Part #, Description,
                              Package, Type (SMD/THT), Supplied By             (assembled parts)
    NAME-pcbway-centroid.csv  Designator, Mid X, Mid Y, Layer, Rotation, Footprint
    NAME-customer-parts.csv   what we fit ourselves after assembly (socketed chips, consigned)
    NAME-bom-check.txt        part numbers still marked verify, assembled parts without an MPN,
                              parts without an LCSC number (JLCPCB only)
    NAME-gerbers.zip          every Gerber/drill file already in <outdir>

"Assembled" means Assembly = pcba, or the socket of an Assembly = socket part (the chip is a
separate line: supplied by us, or by the assembler and inserted after reflow when
Chip_Supply = pcba).  Positions follow KiCad's own position-file convention (mm, Y up,
KiCad rotation) which both houses accept from KiCad users; the JLCPCB rotation offsets in
parts.json "jlc_rotation" are applied to the CPL only.
"""
import argparse
import csv
import json
import os
import re
import sys
import xml.etree.ElementTree as ET
import zipfile

try:
    import pcbnew
except ImportError:
    pcbnew = None


def natural(ref):
    m = re.match(r"([A-Za-z_]+)(\d+)", ref)
    return (m.group(1), int(m.group(2))) if m else (ref, 0)


def read_components(xml_path):
    root = ET.parse(xml_path).getroot()
    comps = {}
    for c in root.iter("comp"):
        ref = c.get("ref")
        fields = {f.get("name"): (f.text or "") for f in c.iter("field")}
        fp = c.findtext("footprint") or fields.get("Footprint", "")
        comps[ref] = {"ref": ref, "value": c.findtext("value") or "", "footprint": fp, "fields": fields,
                      "dnp": any(p.get("name") == "dnp" for p in c.iter("property"))}
    return comps


def read_positions(pcb_path):
    if pcbnew is None:
        sys.exit("pcbnew module not available")
    board = pcbnew.LoadBoard(pcb_path)
    pos = {}
    for fp in board.GetFootprints():
        p = fp.GetPosition()
        attr = fp.GetAttributes()
        kind = "SMD" if attr & pcbnew.FP_SMD else ("THT" if attr & pcbnew.FP_THROUGH_HOLE else "other")
        pos[fp.GetReference()] = {"x": pcbnew.ToMM(p.x), "y": -pcbnew.ToMM(p.y), "rot": fp.GetOrientationDegrees(),
                                  "side": "bottom" if fp.GetLayer() == pcbnew.B_Cu else "top", "type": kind,
                                  "fpname": str(fp.GetFPID().GetLibItemName())}
    return pos


def short_fp(fp):
    return fp.split(":", 1)[1] if ":" in fp else fp


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("xml"); ap.add_argument("pcb"); ap.add_argument("outdir")
    ap.add_argument("--parts", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "parts.json"))
    ap.add_argument("--name", default="other-m68k")
    a = ap.parse_args()
    comps = read_components(a.xml)
    pos = read_positions(a.pcb)
    with open(a.parts, encoding="utf-8") as f:
        db = json.load(f)
    rot_rules = [(re.compile(k), float(v)) for k, v in db.get("jlc_rotation", {}).get("rules", {}).items()]
    os.makedirs(a.outdir, exist_ok=True)
    out = lambda suffix: os.path.join(a.outdir, f"{a.name}-{suffix}")

    assembled = []      # dicts: ref, mfr, mpn, lcsc, desc, footprint, type, comment
    customer = []       # chips we fit ourselves
    problems = {"verify": [], "no_mpn": [], "no_lcsc": []}
    for ref in sorted(comps, key=natural):
        c = comps[ref]
        f = c["fields"]
        asm = f.get("Assembly", "pcba")
        if c["dnp"] or asm == "dnp":
            continue
        p = pos.get(ref)
        if p is None:
            print(f"warning: {ref} is not on the board", file=sys.stderr)
            continue
        if asm == "customer":
            customer.append({"ref": ref, "mfr": f.get("Manufacturer", ""), "mpn": f.get("MPN", ""), "desc": f.get("Desc", c["value"]),
                             "note": "customer supplies and fits"})
            continue
        if asm == "socket":
            assembled.append({"ref": ref, "mfr": f.get("Socket_Manufacturer", ""), "mpn": f.get("Socket_MPN", ""), "lcsc": f.get("Socket_LCSC", ""),
                              "desc": f.get("Socket_Desc", "socket for " + f.get("Desc", c["value"])), "footprint": c["footprint"],
                              "type": p["type"], "comment": f.get("Socket_MPN", "")})
            supply = f.get("Chip_Supply", "customer")
            customer.append({"ref": ref, "mfr": f.get("Manufacturer", ""), "mpn": f.get("MPN", ""), "desc": f.get("Desc", c["value"]),
                             "note": "customer supplies and inserts into socket" if supply == "customer"
                             else "ASSEMBLER supplies loose, inserted into socket after assembly"})
            continue
        assembled.append({"ref": ref, "mfr": f.get("Manufacturer", ""), "mpn": f.get("MPN", ""), "lcsc": f.get("LCSC", ""),
                          "desc": f.get("Desc", c["value"]), "footprint": c["footprint"], "type": p["type"], "comment": c["value"]})
        if not f.get("MPN"):
            problems["no_mpn"].append(ref)
        if not f.get("LCSC"):
            problems["no_lcsc"].append(ref)

    # parts.json "verify" flags, looked up the same way board.py does
    for ref in sorted(comps, key=natural):
        c = comps[ref]
        e = db.get("by_ref", {}).get(ref) or db.get("by_value", {}).get(f"{c['value']}|{c['footprint']}") or db.get("by_value", {}).get(f"{c['value']}|*")
        if e and (e.get("verify") or e.get("socket", {}).get("verify")):
            problems["verify"].append(f"{ref}: {e.get('mpn', '')}" + (f" / socket {e['socket'].get('mpn', '')}" if e.get("socket") else ""))

    def grouped(rows):
        g = {}
        for r in rows:
            g.setdefault((r["mpn"] or r["comment"], short_fp(r["footprint"])), []).append(r)
        return [v for _, v in sorted(g.items(), key=lambda kv: natural(kv[1][0]["ref"]))]

    # JLCPCB
    with open(out("jlcpcb-bom.csv"), "w", newline="") as f:
        w = csv.writer(f); w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
        for rows in grouped(assembled):
            w.writerow([rows[0]["comment"], ",".join(r["ref"] for r in rows), short_fp(rows[0]["footprint"]), rows[0]["lcsc"]])
    with open(out("jlcpcb-cpl.csv"), "w", newline="") as f:
        w = csv.writer(f); w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for r in assembled:
            p = pos[r["ref"]]
            rot = p["rot"]
            for rx, add in rot_rules:
                if rx.search(p["fpname"]):
                    rot = (rot + add) % 360
            w.writerow([r["ref"], f"{p['x']:.4f}mm", f"{p['y']:.4f}mm", p["side"], f"{rot:.1f}"])
    # PCBWay
    with open(out("pcbway-bom.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Item", "Designator", "Qty", "Manufacturer", "Mfg Part #", "Description", "Package", "Type", "Supplied By"])
        for i, rows in enumerate(grouped(assembled), 1):
            r = rows[0]
            w.writerow([i, ",".join(x["ref"] for x in rows), len(rows), r["mfr"], r["mpn"], r["desc"], short_fp(r["footprint"]), r["type"], "PCBWay"])
        for e in db.get("extras", []):
            i += 1
            w.writerow([i, "", e["qty"], e.get("mfr", ""), e.get("mpn", ""), e.get("desc", ""), "", "loose", "PCBWay"])
    with open(out("pcbway-centroid.csv"), "w", newline="") as f:
        w = csv.writer(f); w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation", "Footprint"])
        for r in assembled:
            p = pos[r["ref"]]
            w.writerow([r["ref"], f"{p['x']:.4f}", f"{p['y']:.4f}", p["side"], f"{p['rot']:.1f}", short_fp(r["footprint"])])
    with open(out("customer-parts.csv"), "w", newline="") as f:
        w = csv.writer(f); w.writerow(["Designator", "Manufacturer", "MPN", "Description", "Note"])
        for r in customer:
            w.writerow([r["ref"], r["mfr"], r["mpn"], r["desc"], r["note"]])
    with open(out("bom-check.txt"), "w") as f:
        f.write(f"assembled: {len(assembled)} placements, {len(grouped(assembled))} BOM lines; fitted by us: {len(customer)}\n")
        f.write("\nPart numbers written from memory, not yet checked against a distributor listing:\n")
        f.writelines(f"  {s}\n" for s in problems["verify"])
        f.write(f"\nAssembled parts without an MPN: {', '.join(problems['no_mpn']) or 'none'}\n")
        f.write(f"Assembled parts without an LCSC number (JLCPCB matches basic passives itself): {len(problems['no_lcsc'])}\n")
        f.write("  " + ", ".join(problems["no_lcsc"]) + "\n")
    # Gerber zip
    gerbers = sorted(x for x in os.listdir(a.outdir) if x.endswith((".gbr", ".drl", ".gbrjob")) or x.endswith("-drl_map.pdf"))
    if gerbers:
        with zipfile.ZipFile(out("gerbers.zip"), "w", zipfile.ZIP_DEFLATED) as z:
            for g in gerbers:
                z.write(os.path.join(a.outdir, g), g)
    smd = sum(1 for r in assembled if r["type"] == "SMD")
    print(f"fab_export: {len(assembled)} assembled placements ({smd} SMD, {len(assembled) - smd} THT), "
          f"{len(grouped(assembled))} BOM lines, {len(customer)} parts fitted by us, {len(gerbers)} Gerber/drill files zipped; "
          f"{len(problems['verify'])} part numbers to verify (see {out('bom-check.txt')})")


if __name__ == "__main__":
    main()
