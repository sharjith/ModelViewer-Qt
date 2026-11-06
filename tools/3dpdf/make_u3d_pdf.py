#!/usr/bin/env python3
"""Phase 1 spike: wrap a U3D file in a one-page drawing-style PDF (3D annotation + 2D title block).

Throwaway research tool, not production code: it answers "does Acrobat accept a hand-built 3D PDF" before the C++
writer is designed. Standard library only.

    python tools/3dpdf/make_u3d_pdf.py model.u3d out/3dpdf/spike.pdf [--title "Part name"]

The 3D annotation follows ISO 32000-1 section 13.6 (/Subtype /3D, /3DD stream with /Subtype /U3D, /3DV view, /3DA activation).
"""
import argparse
import math
import zlib


class Pdf:
    def __init__(self):
        self.objs = []  # index = object number - 1; entry is bytes (without "n 0 obj"/"endobj")

    def add(self, body=b""):
        self.objs.append(body)
        return len(self.objs)

    def set(self, num, body):
        self.objs[num - 1] = body

    def stream(self, dict_entries, data, compress=True):
        raw = zlib.compress(data, 9) if compress else data
        flt = b"/Filter /FlateDecode " if compress else b""
        return b"<< " + dict_entries + b" " + flt + b"/Length " + str(len(raw)).encode() + b" >>\nstream\n" + raw + b"\nendstream"

    def write(self, path, root):
        out = bytearray(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n")
        offsets = []
        for i, body in enumerate(self.objs, 1):
            offsets.append(len(out))
            out += str(i).encode() + b" 0 obj\n" + body + b"\nendobj\n"
        xref = len(out)
        out += b"xref\n0 " + str(len(self.objs) + 1).encode() + b"\n0000000000 65535 f \n"
        for off in offsets:
            out += b"%010d 00000 n \n" % off
        out += b"trailer\n<< /Size " + str(len(self.objs) + 1).encode() + b" /Root " + str(root).encode() + b" 0 R >>\n"
        out += b"startxref\n" + str(xref).encode() + b"\n%%EOF\n"
        with open(path, "wb") as f:
            f.write(out)


def look_at(eye, target, up):
    """PDF /C2W as Acrobat reads it (verified in Acrobat, see the research doc 8.6): the rows are the camera's right, up and
    FORWARD axes in world coordinates (the camera looks along +z of its own frame, unlike the U3D view node), then the eye."""
    def sub(a, b): return [a[i] - b[i] for i in range(3)]
    def cross(a, b): return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
    def norm(a):
        n = math.sqrt(sum(x * x for x in a))
        return [x / n for x in a]
    z = norm(sub(target, eye))
    x = norm(cross(z, up))
    y = cross(x, z)
    return x + y + z + list(eye)


def num(v):
    """PDF numbers have no exponent notation."""
    t = ("%.6f" % v).rstrip("0").rstrip(".")
    return "0" if t in ("", "-0") else t


# NOTE: a /3DNode /M entry REPLACES the node's own transform in that view, so it is left out here; only visibility and opacity are set.
def view_dict(name, eye, target, up, nodes):
    c2w = look_at(eye, target, up)
    co = math.dist(eye, target)
    # nodes: (name, visible[, opacity[, (tx, ty, tz) translation]]); a translation writes /M = identity rotation + that offset
    # (the node's own authored transform is identity in the test scenes, so the offset IS its full transform)
    def entry(t):
        n, vis = t[0], t[1]
        op = t[2] if len(t) > 2 else 1.0
        m = b" /M [1 0 0 0 1 0 0 0 1 %s %s %s]" % tuple(num(v).encode() for v in t[3]) if len(t) > 3 else b""
        return b"<< /Type /3DNode /N (%s) /V %s /O %s%s >>" % (n.encode(), b"true" if vis else b"false", num(op).encode(), m)
    na = b" ".join(entry(t) for t in nodes)
    return (b"<< /Type /3DView /XN (%s) /IN (%s) /MS /M /C2W [%s] /CO %s "
            b"/P << /Type /3DProjection /Subtype /P /PS /Min /FOV 30 >> "
            b"/BG << /Type /3DBG /Subtype /SC /C [1 1 1] >> /RM << /Type /3DRenderMode /Subtype /Solid >> "
            b"/LS << /Type /3DLightingScheme /Subtype /CAD >> /NA [%s] >>"
            % (name.encode(), name.encode(), " ".join(num(v) for v in c2w).encode(), num(co).encode(), na))


def view_from_c2w(name, c2w, co, fov, nodes):
    na = b" ".join(b"<< /Type /3DNode /N (%s) /V true /O 1 >>" % n.encode() for n in nodes)
    return (b"<< /Type /3DView /XN (%s) /IN (%s) /MS /M /C2W [%s] /CO %s "
            b"/P << /Type /3DProjection /Subtype /P /PS /Min /FOV %s >> "
            b"/BG << /Type /3DBG /Subtype /SC /C [1 1 1] >> /RM << /Type /3DRenderMode /Subtype /Solid >> "
            b"/LS << /Type /3DLightingScheme /Subtype /CAD >> /NA [%s] >>"
            % (name.encode(), name.encode(), " ".join(num(v) for v in c2w).encode(), num(co).encode(), num(fov).encode(), na))


def diag_views(nodes):
    """Same camera as the U3D's own DefaultView of the sample box (eye (0,-241.48,64.70) looking at the origin, FOV 34.5),
    written as a PDF /C2W in several candidate conventions. Compare each with 'U3D own view'."""
    eye, tgt, up, fov = [0.0, -241.481461, 64.704765], [0.0, 0.0, 0.0], [0, 0, 1], 34.515877
    co = math.dist(eye, tgt)
    rows = look_at(eye, tgt, up)
    R = [rows[0:3], rows[3:6], rows[6:9]]
    Rt = [[R[j][i] for j in range(3)] for i in range(3)]
    flat = lambda m: m[0] + m[1] + m[2]
    out = []
    out.append(view_from_c2w("A rows eye (as v2)", flat(R) + eye, co, fov, nodes))
    out.append(view_from_c2w("B transposed rotation", flat(Rt) + eye, co, fov, nodes))
    w2c_t = [-sum(eye[k] * R[i][k] for k in range(3)) for i in range(3)]   # translation of the inverse transform
    out.append(view_from_c2w("C inverse (W2C)", flat(Rt) + w2c_t, co, fov, nodes))
    out.append(view_from_c2w("D rows, eye at origin (CO only)", flat(R) + [0, 0, 0], co, fov, nodes))
    out.append(view_from_c2w("E rows, CO 0", flat(R) + eye, 0, fov, nodes))
    flipz = [R[0], R[1], [-v for v in R[2]]]
    out.append(view_from_c2w("F camera looks along +Z", flat(flipz) + eye, co, fov, nodes))
    return out


def esc(s):
    return s.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)").encode("latin-1", "replace")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("u3d")
    ap.add_argument("pdf")
    ap.add_argument("--title", default="3D PDF spike")
    ap.add_argument("--center", nargs=3, type=float, help="model centre; with --radius and --node writes named views (/VA)")
    ap.add_argument("--radius", type=float, default=50.0)
    ap.add_argument("--u3d-view", help="name of a U3D view node; adds a first view that uses it (/MS /U3D) for comparison")
    ap.add_argument("--det-test", action="store_true", help="add Front views whose camera matrix has det +1 (right row negated, or up row negated)")
    ap.add_argument("--diag", action="store_true", help="add camera-convention diagnostic views (needs --center)")
    ap.add_argument("--node", action="append", default=[], help="U3D node name(s) to list in each view's /NA")
    a = ap.parse_args()
    u3d = open(a.u3d, "rb").read()

    W, H = 842, 595  # A4 landscape, points
    margin = 28
    area = (margin, 120, W - margin, H - margin - 20)  # 3D rect, leaves a title-block strip at the bottom

    pdf = Pdf()
    catalog = pdf.add()
    pages = pdf.add()
    page = pdf.add()
    font = pdf.add(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
    fontb = pdf.add(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>")

    # 2D content: border, title block and a small parts-list table, drawn with plain vector operators.
    c = []
    c.append("0.5 w %d %d %d %d re S" % (margin - 8, margin - 8, W - 2 * margin + 16, H - 2 * margin + 16))
    c.append("%d %d %d %d re S" % (margin, margin, W - 2 * margin, 80))              # title block
    c.append("%d %d m %d %d l S" % (W - 300, margin, W - 300, margin + 80))
    c.append("%d %d m %d %d l S" % (W - 300, margin + 40, W - margin, margin + 40))
    c.append("BT /F2 14 Tf %d %d Td (%s) Tj ET" % (margin + 10, margin + 48, esc(a.title).decode("latin-1")))
    c.append("BT /F1 9 Tf %d %d Td (Spike: U3D in hand-built PDF, no library) Tj ET" % (margin + 10, margin + 30))
    c.append("BT /F1 8 Tf %d %d Td (Scale: model units) Tj ET" % (W - 290, margin + 52))
    c.append("BT /F1 8 Tf %d %d Td (Sheet 1 of 1) Tj ET" % (W - 290, margin + 14))
    # parts list table (4 rows)
    ty = margin + 70
    tx = margin + 330  # right of the title text so the two never overlap
    c.append("BT /F2 8 Tf %d %d Td (ITEM) Tj 40 0 Td (QTY) Tj 40 0 Td (DESCRIPTION) Tj ET" % (tx, ty - 12))
    for i, (q, d) in enumerate([(1, "Part A"), (1, "Part B"), (2, "Part C")], 1):
        c.append("BT /F1 8 Tf %d %d Td (%d) Tj 40 0 Td (%d) Tj 40 0 Td (%s) Tj ET" % (tx, ty - 12 - i * 11, i, q, d))
    content = pdf.add(pdf.stream(b"", "\n".join(c).encode("latin-1"), compress=False))

    # 3D annotation: stream, default view, activation and a poster appearance for readers without 3D support.
    va = b""
    if a.center:
        # distance at which the bounding sphere fits the smaller viewport dimension (/PS /Min, /FOV 30), with 25% margin
        ctr, d = a.center, a.radius / math.sin(math.radians(15.0)) * 1.25
        def eye(dx, dy, dz):
            n = math.sqrt(dx * dx + dy * dy + dz * dz)
            return [ctr[0] + d * dx / n, ctr[1] + d * dy / n, ctr[2] + d * dz / n]
        names = a.node or []
        vs = [view_dict("Iso", eye(1, -1, 0.8), ctr, [0, 0, 1], [(n, True) for n in names]),
              view_dict("Front", eye(0, -1, 0), ctr, [0, 0, 1], [(n, True) for n in names]),
              view_dict("Top", eye(0, 0, 1), ctr, [0, 1, 0], [(n, True) for n in names]),
              view_dict("Right", eye(1, 0, 0), ctr, [0, 0, 1], [(n, True) for n in names]),
              view_dict("First part hidden", eye(1, -1, 0.8), ctr, [0, 0, 1], [(n, i > 0) for i, n in enumerate(names)]),
              # a measurement layer (last node, when there are 3+) can be switched off per view: tests visibility of line sets
              view_dict("Block moved +Z 40", eye(1, -1, 0.8), ctr, [0, 0, 1], [(n, True, 1.0, (0, 0, 40)) if n == "Block" else (n, True) for n in names]),
              view_dict("Block 40% opaque", eye(1, -1, 0.8), ctr, [0, 0, 1], [(n, True, 0.4) if n == "Block" else (n, True) for n in names]),
              view_dict("Bracket 40% opaque", eye(1, -1, 0.8), ctr, [0, 0, 1], [(n, True, 0.4) if n == "LBracket" else (n, True) for n in names]),
              view_dict("Last node hidden", eye(1, -1, 0.8), ctr, [0, 0, 1], [(n, i < len(names) - 1) for i, n in enumerate(names)])]
        if a.det_test:
            # commercial /M views (Tetra4D, NX, Creo samples) all have det(rows 1-3) = +1 whereas look_at() gives -1: check which is mirrored
            c = look_at(eye(0, -1, 0), ctr, [0, 0, 1])
            ne = [b" ".join(b"<< /Type /3DNode /N (%s) /V true /O 1 >>" % n.encode() for n in names)]
            for nm, mat in (("Front det-1 (ours)", c), ("Front det+1 right negated", [-v for v in c[0:3]] + c[3:]),
                            ("Front det+1 up negated", c[0:3] + [-v for v in c[3:6]] + c[6:]),
                            ("Front det+1 fwd negated", c[0:6] + [-v for v in c[6:9]] + c[9:])):
                vs.append(view_from_c2w(nm, mat, d, 30.0, names))
        if a.u3d_view:
            vs.insert(0, b"<< /Type /3DView /XN (U3D own view) /IN (U3D own view) /MS /U3D /U3DPath (%s) >>" % a.u3d_view.encode())
        if a.diag:
            vs[1:1] = diag_views(names)
        va = b" /VA [" + b" ".join(vs) + b"]"
    stream3d = pdf.add(pdf.stream(b"/Type /3D /Subtype /U3D" + va, u3d))
    view = 0 if a.center else pdf.add(
        b"<< /Type /3DView /XN (Default) /IN (Default) /MS /M "
        b"/C2W [1 0 0 0 0 1 0 -1 0 0 0 0] /CO 5 "  # placeholder camera: looking along +Y; Acrobat refits with /CO unless /MS /U3D
        b"/P << /Type /3DProjection /Subtype /P /PS /Min /FOV 30 >> "
        b"/BG << /Type /3DBG /Subtype /SC /C [1 1 1] >> "
        b"/RM << /Type /3DRenderMode /Subtype /Solid >> "
        b"/LS << /Type /3DLightingScheme /Subtype /CAD >> >>"
    )
    w3, h3 = area[2] - area[0], area[3] - area[1]
    poster = pdf.add(
        pdf.stream(
            b"/Type /XObject /Subtype /Form /BBox [0 0 %d %d] /Resources << /Font << /F1 %d 0 R >> >>" % (w3, h3, font),
            ("0.93 g 0 0 %d %d re f 0.4 g BT /F1 12 Tf 12 %d Td (3D content - open in Adobe Acrobat/Reader and enable 3D) Tj ET" % (w3, h3, h3 - 24)).encode("latin-1"),
            compress=False,
        )
    )
    annot = pdf.add(
        b"<< /Type /Annot /Subtype /3D /Rect [%d %d %d %d] /F 4 /Contents (3D model) "
        b"/3DD %d 0 R /3DV %s /3DA << /A /PO /D /PC /TB true /NP false >> "
        b"/AP << /N %d 0 R >> >>" % (area[0], area[1], area[2], area[3], stream3d, (b"0" if a.center else b"%d 0 R" % view), poster)
    )

    pdf.set(page, b"<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %d %d] /Contents %d 0 R "
                  b"/Resources << /Font << /F1 %d 0 R /F2 %d 0 R >> >> /Annots [%d 0 R] >>" % (pages, W, H, content, font, fontb, annot))
    pdf.set(pages, b"<< /Type /Pages /Kids [%d 0 R] /Count 1 >>" % page)
    pdf.set(catalog, b"<< /Type /Catalog /Pages %d 0 R >>" % pages)
    pdf.write(a.pdf, catalog)
    print("wrote", a.pdf, "with", len(u3d), "bytes of U3D")


if __name__ == "__main__":
    main()
