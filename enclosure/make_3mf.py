#!/usr/bin/env python3
"""make_3mf.py - one PrusaSlicer object from several STLs, each its own part with an extruder.

    python3 make_3mf.py -o OUT.3mf --name NAME FILE.stl:PARTNAME:EXTRUDER ...

Writes the 3MF the way PrusaSlicer saves a multi-part object: a single mesh in
3D/3dmodel.model and Metadata/Slic3r_PE_model.config naming the triangle range of each
part with its extruder. Open it in PrusaSlicer (File > Import, or drag it in; answer
"geometry only" if asked about the project) and the parts are already assigned, so a
single-extruder multi-material profile prints the second part in its own colour in the
same layers as the first.
"""
import argparse
import zipfile

import numpy as np

from merge_stl import read_stl

CONTENT_TYPES = """<?xml version="1.0" encoding="UTF-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
 <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
 <Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>
</Types>
"""
RELS = """<?xml version="1.0" encoding="UTF-8"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
 <Relationship Target="/3D/3dmodel.model" Id="rel-1" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>
</Relationships>
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--name", default="object")
    ap.add_argument("parts", nargs="+", help="FILE.stl:PARTNAME:EXTRUDER")
    a = ap.parse_args()

    meshes = []
    for spec in a.parts:
        path, pname, ext = spec.rsplit(":", 2)
        meshes.append((read_stl(path), pname, int(ext)))

    # one shared vertex list, triangles of each part contiguous
    all_tris = np.concatenate([m[0] for m in meshes])
    verts, inv = np.unique(all_tris.reshape(-1, 3).round(5), axis=0, return_inverse=True)
    tri_idx = inv.reshape(-1, 3)

    vx = "\n".join('     <vertex x="%.5f" y="%.5f" z="%.5f"/>' % tuple(v) for v in verts)
    tx = "\n".join('     <triangle v1="%d" v2="%d" v3="%d"/>' % tuple(t) for t in tri_idx)
    model = """<?xml version="1.0" encoding="UTF-8"?>
<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02" xmlns:slic3rpe="http://schemas.slic3r.org/3mf/2017/06">
 <metadata name="slic3rpe:Version3mf">1</metadata>
 <metadata name="Title">%s</metadata>
 <metadata name="Application">enclosure/make_3mf.py</metadata>
 <resources>
  <object id="1" type="model">
   <mesh>
    <vertices>
%s
    </vertices>
    <triangles>
%s
    </triangles>
   </mesh>
  </object>
 </resources>
 <build>
  <item objectid="1" transform="1 0 0 0 1 0 0 0 1 0 0 0" printable="1"/>
 </build>
</model>
""" % (a.name, vx, tx)

    vols, first = [], 0
    for tris, pname, ext in meshes:
        last = first + len(tris) - 1
        vols.append("""  <volume firstid="%d" lastid="%d">
   <metadata type="volume" key="name" value="%s"/>
   <metadata type="volume" key="volume_type" value="ModelPart"/>
   <metadata type="volume" key="extruder" value="%d"/>
   <mesh edited="0"/>
  </volume>""" % (first, last, pname, ext))
        first = last + 1
    config = """<?xml version="1.0" encoding="UTF-8"?>
<config>
 <object id="1" instances_count="1">
  <metadata type="object" key="name" value="%s"/>
%s
 </object>
</config>
""" % (a.name, "\n".join(vols))

    with zipfile.ZipFile(a.output, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", CONTENT_TYPES)
        z.writestr("_rels/.rels", RELS)
        z.writestr("3D/3dmodel.model", model)
        z.writestr("Metadata/Slic3r_PE_model.config", config)
    print("wrote %s: %d vertices, %d triangles, parts %s" % (a.output, len(verts), len(tri_idx),
          ", ".join("%s (extruder %d, %d triangles)" % (p, e, len(t)) for t, p, e in meshes)))


if __name__ == "__main__":
    main()
