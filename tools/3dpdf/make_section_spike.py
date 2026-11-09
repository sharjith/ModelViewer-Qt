#!/usr/bin/env python3
"""Throwaway spike: which side / orientation does an Acrobat 3D cross section (/SA) keep for a given /O (rotation) and /C (centre)?

Writes one PDF with the 40 x 40 x 40 test box (box_origin.u3d: x -20..20, y -20..20, z 0..40) and views that each cut at a plane
offset +10 from the box centre along one axis, so the kept side is obvious (a thin slab vs a big block). Capping (/SC true), a
semi-transparent plane and the intersection outline are switched on. Names of the views say what was written; the owner reports
what each one shows. Standard library only (uses make_u3d_pdf.Pdf).

    python tools/3dpdf/make_section_spike.py out/3dpdf/run/box_origin.u3d out/3dpdf/run/spike_sections.pdf
"""
import math
import sys

from make_u3d_pdf import Pdf, look_at, num

CENTER = [0.0, 0.0, 20.0]
OFFSET = 10.0


def section(centre, angles, cap=True, show_plane=True):
    o = " ".join("null" if a is None else num(a) for a in angles)
    return ("<< /Type /3DCrossSection /C [%s] /O [%s] /PO 0.25 /PC [/DeviceRGB 0.2 0.4 1] /IV true /IC [/DeviceRGB 1 0 0] "
            "/PV %s /SC %s >>" % (" ".join(num(v) for v in centre), o, "true" if show_plane else "false", "true" if cap else "false")).encode()


def view(name, eye, target, up, sec=None):
    c2w = look_at(eye, target, up)
    co = math.dist(eye, target)
    sa = (b" /SA [" + sec + b"]") if sec else b""
    return (b"<< /Type /3DView /XN (%s) /IN (%s) /MS /M /C2W [%s] /CO %s /P << /Type /3DProjection /Subtype /P /PS /Min /FOV 30 >> "
            b"/BG << /Type /3DBG /Subtype /SC /C [1 1 1] >> /RM << /Type /3DRenderMode /Subtype /Solid >> "
            b"/LS << /Type /3DLightingScheme /Subtype /CAD >>%s >>"
            % (name.encode(), name.encode(), " ".join(num(v) for v in c2w).encode(), num(co).encode(), sa))


def main():
    u3d = open(sys.argv[1], "rb").read()
    out = sys.argv[2]
    d = 40 / math.sin(math.radians(15.0)) * 1.25
    n = math.sqrt(3.0)
    eye = [CENTER[0] + d / n, CENTER[1] - d / n, CENTER[2] + d / n]   # isometric-ish from -Y, +X, +Z

    cases = []
    cx, cy, cz = CENTER
    # (label, centre, O angles about X, Y, Z in degrees)
    import os
    if os.environ.get("SECTION_SPIKE") == "2":
        # second round: only the two-angle form (one angle null), which Acrobat accepted for Y in round 1
        cases = [("1 Z  O[0 0 null]", [cx, cy, cz + OFFSET], [0, 0, None]),
                 ("2 Z  O[180 0 null]", [cx, cy, cz + OFFSET], [180, 0, None]),
                 ("3 Y  O[90 0 null]", [cx, cy + OFFSET, cz], [90, 0, None]),
                 ("4 Y  O[-90 0 null]", [cx, cy + OFFSET, cz], [-90, 0, None]),
                 ("5 X  O[null 90 0]", [cx + OFFSET, cy, cz], [None, 90, 0]),
                 ("6 X  O[null -90 0]", [cx + OFFSET, cy, cz], [None, -90, 0]),
                 ("7 X  O[0 90 null]", [cx + OFFSET, cy, cz], [0, 90, None]),
                 ("8 X  O[0 -90 null]", [cx + OFFSET, cy, cz], [0, -90, None]),
                 ("9 Z  O[null 0 0]", [cx, cy, cz + OFFSET], [None, 0, 0]),
                 ("10 Z  O[null 180 0]", [cx, cy, cz + OFFSET], [None, 180, 0])]
    else:
        cases += [("Z plane at z+10  O[0 0 0]", [cx, cy, cz + OFFSET], [0, 0, 0]),
              ("Z plane at z+10  O[180 0 0]", [cx, cy, cz + OFFSET], [180, 0, 0]),
              ("Z plane at z+10  O[0 180 0]", [cx, cy, cz + OFFSET], [0, 180, 0]),
              ("Y plane at y+10  O[90 0 0]", [cx, cy + OFFSET, cz], [90, 0, 0]),
              ("Y plane at y+10  O[-90 0 0]", [cx, cy + OFFSET, cz], [-90, 0, 0]),
              ("X plane at x+10  O[0 90 0]", [cx + OFFSET, cy, cz], [0, 90, 0]),
              ("X plane at x+10  O[0 -90 0]", [cx + OFFSET, cy, cz], [0, -90, 0]),
              ("Sample style Z  O[null -180 90]", [cx, cy, cz + OFFSET], [None, -180, 90]),
              ("Sample style Y  O[90 0 null]", [cx, cy + OFFSET, cz], [90, 0, None]),
              ("Sample style Y  O[-90 0 null]", [cx, cy + OFFSET, cz], [-90, 0, None])]

    vs = [view("No section", eye, CENTER, [0, 0, 1])]
    for label, centre, angles in cases:
        vs.append(view(label, eye, CENTER, [0, 0, 1], section(centre, angles)))
    if os.environ.get("SECTION_SPIKE") != "2":
        vs.append(view("Z O[0 0 0] no cap, no plane", eye, CENTER, [0, 0, 1], section([cx, cy, cz + OFFSET], [0, 0, 0], cap=False, show_plane=False)))

    pdf = Pdf()
    catalog, pages, page = pdf.add(), pdf.add(), pdf.add()
    stream3d = pdf.add(pdf.stream(b"/Type /3D /Subtype /U3D /VA [" + b" ".join(vs) + b"]", u3d))
    annot = pdf.add(b"<< /Type /Annot /Subtype /3D /Rect [30 30 812 565] /F 4 /Contents (3D model) /3DD %d 0 R /3DV 0 "
                    b"/3DA << /A /PO /D /PC /TB true /NP false >> >>" % stream3d)
    pdf.set(page, b"<< /Type /Page /Parent %d 0 R /MediaBox [0 0 842 595] /Annots [%d 0 R] >>" % (pages, annot))
    pdf.set(pages, b"<< /Type /Pages /Kids [%d 0 R] /Count 1 >>" % page)
    pdf.set(catalog, b"<< /Type /Catalog /Pages %d 0 R >>" % pages)
    pdf.write(out, catalog)
    print("wrote", out, "with", len(vs), "views")


if __name__ == "__main__":
    main()
