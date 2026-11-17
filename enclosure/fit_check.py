#!/usr/bin/env python3
"""fit_check.py - does anything on the board, or any plug, run into the case?

    python3 fit_check.py CASE.stl OTHER.stl...

CASE.stl must be a closed mesh (the "fit" render of case.scad: both halves in place). For
every triangle vertex, edge midpoint and centroid of the other meshes a ray is cast along +x
through the case; an odd number of crossings means the point is inside the plastic. Points
that merely touch a surface (the board resting on its bosses) must not count, so each point
is tested shifted by +-EPS in y and in z and is reported only if all four shifted points
are inside: a penetration deeper than EPS. Hits are clustered and listed with their
extents, and the exit status is 1 if any exist. A plug envelope inside the wall means a
cutout is too small or misplaced; a board part inside means the part hits the shell, a
boss, a pillar or the lid.
"""
import sys
from collections import defaultdict

import numpy as np

from merge_stl import read_stl

EPS = 0.05


def sample_points(tris):
    v = tris.reshape(-1, 3)
    mids = ((tris + np.roll(tris, 1, axis=1)) / 2).reshape(-1, 3)
    cen = tris.mean(axis=1)
    return np.concatenate([v, mids, cen])


def inside(case, pts, cell=2.0):
    """even-odd test with +x rays; case triangles binned on (y, z)"""
    tri_min, tri_max = case.min(axis=1), case.max(axis=1)
    bins = defaultdict(list)
    for i, (lo, hi) in enumerate(zip(tri_min, tri_max)):
        for by in range(int(np.floor(lo[1] / cell)), int(np.floor(hi[1] / cell)) + 1):
            for bz in range(int(np.floor(lo[2] / cell)), int(np.floor(hi[2] / cell)) + 1):
                bins[(by, bz)].append(i)
    result = np.zeros(len(pts), dtype=bool)
    keys = np.floor(pts[:, 1:] / cell).astype(int)
    groups = defaultdict(list)
    for i, k in enumerate(map(tuple, keys)):
        groups[k].append(i)
    for k, idx in groups.items():
        cand = bins.get(k)
        if not cand:
            continue
        T = case[cand]                               # (m, 3, 3)
        P = pts[idx]                                 # (n, 3)
        # Moller-Trumbore for direction (1, 0, 0)
        e1, e2 = T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]
        h = np.cross(np.array([1.0, 0, 0]), e2)       # (m, 3)
        a = (e1 * h).sum(1)                          # (m,)
        ok = np.abs(a) > 1e-9
        f = np.where(ok, 1.0 / np.where(ok, a, 1), 0)
        s = P[:, None, :] - T[None, :, 0, :]          # (n, m, 3)
        u = f * (s * h).sum(2)
        q = np.cross(s, e1[None])
        vv = f * q[:, :, 0]                          # dot with (1,0,0)
        t = f * (q * e2[None]).sum(2)
        hit = ok[None] & (u >= 0) & (vv >= 0) & (u + vv <= 1) & (t > 1e-6)
        result[idx] = hit.sum(1) % 2 == 1
    return result


def clusters(pts, r=3.0):
    out = []
    left = list(range(len(pts)))
    while left:
        seed = left.pop()
        members = [seed]
        frontier = [seed]
        while frontier:
            p = pts[frontier.pop()]
            near = [i for i in left if np.linalg.norm(pts[i] - p) < r]
            left = [i for i in left if i not in near]
            members += near
            frontier += near
        out.append(pts[members])
    return out


def main():
    case = read_stl(sys.argv[1])
    bad = 0
    for path in sys.argv[2:]:
        pts = np.unique(sample_points(read_stl(path)), axis=0)
        hit = np.ones(len(pts), dtype=bool)
        for d in ([0, EPS, 0], [0, -EPS, 0], [0, 0, EPS], [0, 0, -EPS]):
            hit &= inside(case, pts + np.array(d))
        n = int(hit.sum())
        print("%s: %d points sampled, %d inside the case" % (path, len(pts), n))
        for c in clusters(pts[hit]):
            lo, hi = c.min(0), c.max(0)
            print("   %4d points in x %6.1f..%6.1f  y %6.1f..%6.1f  z %5.1f..%5.1f" % (len(c), lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]))
        bad += n
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
