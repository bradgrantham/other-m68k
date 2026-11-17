#!/usr/bin/env python3
"""merge_stl.py - combine STL files into one binary STL, for the fit-check models.

    python3 merge_stl.py -o OUT.stl [--scad build/board.scad] [--kicad FILE]... FILE...

Plain FILEs (ASCII or binary) are appended as they are. A --kicad FILE is a kicad-cli STL
export of the board made with --user-origin 0x0mm: its y axis is the board's y negated, so
it is moved by board_d (read from the generated .scad) into case coordinates, and the
triangles of the board body itself (all three vertices within the board thickness) are
dropped, because the OpenSCAD model draws the board slab. The output is one mesh of
separate shells for viewing, not a manifold for printing.
"""
import argparse
import re
import struct
import sys

import numpy as np


def read_stl(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) >= 84:
        n = struct.unpack("<I", data[80:84])[0]
        if len(data) == 84 + 50 * n and not data.startswith(b"solid"):
            tri = np.frombuffer(data[84:], dtype=np.dtype([("n", "<f4", 3), ("v", "<f4", (3, 3)), ("a", "<u2")]))
            return tri["v"].astype(np.float64)
    v = np.array(re.findall(rb"vertex\s+(\S+)\s+(\S+)\s+(\S+)", data), dtype=np.float64)
    return v.reshape(-1, 3, 3)


def write_stl(path, tris):
    n = len(tris)
    out = np.zeros(n, dtype=np.dtype([("n", "<f4", 3), ("v", "<f4", (3, 3)), ("a", "<u2")]))
    e1, e2 = tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0]
    nrm = np.cross(e1, e2)
    length = np.linalg.norm(nrm, axis=1)
    nrm[length > 0] /= length[length > 0, None]
    out["n"], out["v"] = nrm, tris
    with open(path, "wb") as f:
        f.write(b"enclosure/merge_stl.py".ljust(80, b"\0"))
        f.write(struct.pack("<I", n))
        f.write(out.tobytes())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--scad", help="generated board.scad, for board_d and board_t")
    ap.add_argument("--kicad", action="append", default=[], help="kicad-cli STL export to convert")
    ap.add_argument("files", nargs="*")
    a = ap.parse_args()
    parts = []
    if a.kicad:
        scad = open(a.scad, encoding="utf-8").read()
        board_d = float(re.search(r"^board_d = ([\d.]+);", scad, re.M).group(1))
        board_t = float(re.search(r"^board_t = ([\d.]+);", scad, re.M).group(1))
        for path in a.kicad:
            t = read_stl(path)
            t[:, :, 1] += board_d
            z = t[:, :, 2]
            in_slab = ((z > -0.001) & (z < board_t + 0.001)).all(axis=1)
            parts.append(t[~in_slab])
            print("%s: %d triangles, %d of the board body dropped" % (path, len(t), in_slab.sum()))
    for path in a.files:
        t = read_stl(path)
        parts.append(t)
        print("%s: %d triangles" % (path, len(t)))
    tris = np.concatenate(parts)
    write_stl(a.output, tris)
    lo, hi = tris.reshape(-1, 3).min(0), tris.reshape(-1, 3).max(0)
    print("wrote %s: %d triangles, x %.1f..%.1f  y %.1f..%.1f  z %.1f..%.1f" % (a.output, len(tris), lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]))


if __name__ == "__main__":
    main()
