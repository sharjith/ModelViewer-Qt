#!/usr/bin/env python3
"""Writes an asymmetric multi-part IDTF test scene for the 3D PDF spike (feeds IDTFConverter, see the research doc 8.1).

Parts: "LBracket" (one mesh made of two cuboids, an L when seen from the front: the tall leg is on the LEFT, -X) and "Block"
(a separate blue cuboid on the +X side). An L and a left/right-distinct layout show mirroring errors that a symmetric box hides.

    python tools/3dpdf/make_test_scene_idtf.py out/3dpdf/run/lscene.idtf

A third node "Measure" is a LINE_SET (emissive, so it reads as unlit): a 60 mm length dimension under the bracket's base (two
extension lines, a dimension line, two arrowheads, the number "60" drawn as seven-segment strokes = the outline-text approach of
the owner decisions) and a leader line from the tall leg to a label point. It tests line sets, the per-node visibility of a
measurement layer and stroke text in Acrobat.
"""
import sys


def measurement_lines():
    """Returns (positions, segments) for the dimension graphic, all in the plane y = -10 (the bracket's front face)."""
    pos, seg = [], []
    def p(x, z):
        pos.append((x, -10.0, z)); return len(pos) - 1
    def line(a, b):
        seg.append((a, b))
    z0, zd = 0.0, -12.0
    for x in (-30.0, 30.0):                      # extension lines
        line(p(x, z0 - 2), p(x, zd - 2))
    a, b = p(-30.0, zd), p(30.0, zd)             # dimension line + arrowheads
    line(a, b)
    for x, d in ((-30.0, 1), (30.0, -1)):
        tip = p(x, zd)
        line(tip, p(x + d * 4, zd + 1.5)); line(tip, p(x + d * 4, zd - 1.5))
    # seven-segment digits "6" and "0", 4 wide x 7 high, centred under the dimension line
    segs = {"a": ((0, 7), (4, 7)), "b": ((4, 7), (4, 3.5)), "c": ((4, 3.5), (4, 0)), "d": ((0, 0), (4, 0)),
            "e": ((0, 0), (0, 3.5)), "f": ((0, 3.5), (0, 7)), "g": ((0, 3.5), (4, 3.5))}
    for ox, letters in ((-5.0, "acdefg"), (1.0, "abcdef")):   # "6" then "0"
        for l in letters:
            (x0, z0_), (x1, z1_) = segs[l]
            line(p(ox + x0, zd - 11 + z0_), p(ox + x1, zd - 11 + z1_))
    # leader from the tall leg's front face to a label point, with a small cross as the label
    n_dim = len(seg)
    line(p(-25.0, 40.0), p(-45.0, 55.0)); line(p(-45.0, 55.0), p(-60.0, 55.0))
    col = [0] * n_dim + [1] * (len(seg) - n_dim)
    return pos, seg, col


def text_quads(dx=-24.0, w=0.8):
    return ("Q", [(x0, x1, z0, z1) for (x0, x1, _y0, _y1, z0, z1) in text_cuboids(dx, w)])


def text_cuboids(dx=24.0, w=0.8):
    """The seven-segment "60" again, as thin cuboids (stroke width w, depth 0.4 in -Y) shifted by dx: filled geometry that takes a material colour."""
    zd = -12.0
    segs = {"a": ((0, 7), (4, 7)), "b": ((4, 7), (4, 3.5)), "c": ((4, 3.5), (4, 0)), "d": ((0, 0), (4, 0)),
            "e": ((0, 0), (0, 3.5)), "f": ((0, 3.5), (0, 7)), "g": ((0, 3.5), (4, 3.5))}
    out = []
    for ox, letters in ((-5.0, "acdefg"), (1.0, "abcdef")):
        for l in letters:
            (x0, z0), (x1, z1) = segs[l]
            X0, X1 = sorted((ox + x0 + dx, ox + x1 + dx)); Z0, Z1 = sorted((zd - 11 + z0, zd - 11 + z1))
            out.append((X0 - w / 2, X1 + w / 2, -10.4, -10.0, Z0 - w / 2, Z1 + w / 2))
    return out


def lineset_resource(name):
    pos, seg, col = measurement_lines()
    n = len(seg)
    F = lambda t: "\t\t\t\t" + " ".join("%.6f" % v for v in t) + "\n"
    s = ('\t\tRESOURCE_NAME "%s"\n\t\tMODEL_TYPE "LINE_SET"\n\t\tLINE_SET {\n\t\t\tLINE_COUNT %d\n\t\t\tMODEL_POSITION_COUNT %d\n'
         '\t\t\tMODEL_NORMAL_COUNT 1\n\t\t\tMODEL_DIFFUSE_COLOR_COUNT 0\n\t\t\tMODEL_SPECULAR_COLOR_COUNT 0\n'
         '\t\t\tMODEL_TEXTURE_COORD_COUNT 0\n\t\t\tMODEL_SHADING_COUNT 1\n'
         '\t\t\tMODEL_SHADING_DESCRIPTION_LIST {\n\t\t\t\tSHADING_DESCRIPTION 0 {\n\t\t\t\t\tTEXTURE_LAYER_COUNT 0\n'
         '\t\t\t\t\tSHADER_ID 0\n\t\t\t\t}\n\t\t\t}\n') % (name, n, len(pos))
    s += "\t\t\tLINE_POSITION_LIST {\n" + "".join("\t\t\t\t%d %d\n" % t for t in seg) + "\t\t\t}\n"
    s += "\t\t\tLINE_NORMAL_LIST {\n" + "\t\t\t\t0 0\n" * n + "\t\t\t}\n"
    s += "\t\t\tLINE_SHADING_LIST {\n" + "\t\t\t\t0\n" * n + "\t\t\t}\n"
    s += "\t\t\tMODEL_POSITION_LIST {\n" + "".join(F(q) for q in pos) + "\t\t\t}\n"
    s += "\t\t\tMODEL_NORMAL_LIST {\n" + F((0, -1, 0)) + "\t\t\t}\n\t\t}\n"
    return s


ANCHORS = [(-30, -10, 0), (30, -10, 0), (-25, -10, 40), (35, -10, 20), (55, -10, 20)]   # measurement anchor points on the front faces


def pointset_resource(name):
    n = len(ANCHORS)
    F = lambda t: "\t\t\t\t" + " ".join("%.6f" % v for v in t) + "\n"
    s = ('\t\tRESOURCE_NAME "%s"\n\t\tMODEL_TYPE "POINT_SET"\n\t\tPOINT_SET {\n\t\t\tPOINT_COUNT %d\n\t\t\tMODEL_POSITION_COUNT %d\n'
         '\t\t\tMODEL_NORMAL_COUNT 1\n\t\t\tMODEL_DIFFUSE_COLOR_COUNT 0\n\t\t\tMODEL_SPECULAR_COLOR_COUNT 0\n'
         '\t\t\tMODEL_TEXTURE_COORD_COUNT 0\n\t\t\tMODEL_SHADING_COUNT 1\n'
         '\t\t\tMODEL_SHADING_DESCRIPTION_LIST {\n\t\t\t\tSHADING_DESCRIPTION 0 {\n\t\t\t\t\tTEXTURE_LAYER_COUNT 0\n'
         '\t\t\t\t\tSHADER_ID 0\n\t\t\t\t}\n\t\t\t}\n') % (name, n, n)
    s += "\t\t\tPOINT_POSITION_LIST {\n" + "".join("\t\t\t\t%d\n" % i for i in range(n)) + "\t\t\t}\n"
    s += "\t\t\tPOINT_NORMAL_LIST {\n" + "\t\t\t\t0\n" * n + "\t\t\t}\n"
    s += "\t\t\tPOINT_SHADING_LIST {\n" + "\t\t\t\t0\n" * n + "\t\t\t}\n"
    s += "\t\t\tMODEL_POSITION_LIST {\n" + "".join(F(q) for q in ANCHORS) + "\t\t\t}\n"
    s += "\t\t\tMODEL_NORMAL_LIST {\n" + F((0, -1, 0)) + "\t\t\t}\n\t\t}\n"
    return s


# cuboid corners in the order of the sample box.idtf (bit0 = x, bit1 = y, bit2 = z) and its 12 outward triangles
TRIS = [(0, 2, 3), (3, 1, 0), (4, 5, 7), (7, 6, 4), (0, 1, 5), (5, 4, 0), (1, 3, 7), (7, 5, 1), (3, 2, 6), (6, 7, 3), (2, 0, 4), (4, 6, 2)]
NORMALS = [(0, 0, -1), (0, 0, -1), (0, 0, 1), (0, 0, 1), (0, -1, 0), (0, -1, 0), (1, 0, 0), (1, 0, 0), (0, 1, 0), (0, 1, 0), (-1, 0, 0), (-1, 0, 0)]


def mesh(cuboids):
    pos, faces, fnorm, norms = [], [], [], []
    if isinstance(cuboids, tuple) and cuboids[0] == "Q":
        # double-sided flat quads in the plane y = -10.1 (4 triangles each): front side faces -Y, back side +Y
        for (x0, x1, z0, z1) in cuboids[1]:
            b = len(pos)
            pos += [(x0, -10.1, z0), (x1, -10.1, z0), (x1, -10.1, z1), (x0, -10.1, z1)]
            for tri, n in (((0, 1, 2), (0, -1, 0)), ((0, 2, 3), (0, -1, 0)), ((0, 2, 1), (0, 1, 0)), ((0, 3, 2), (0, 1, 0))):
                faces.append(tuple(b + v for v in tri)); fnorm.append(tuple(len(norms) + k for k in range(3))); norms += [n, n, n]
        return pos, faces, fnorm, norms
    for (x0, x1, y0, y1, z0, z1) in cuboids:
        base = len(pos)
        pos += [(x1 if i & 1 else x0, y1 if i & 2 else y0, z1 if i & 4 else z0) for i in range(8)]
        for t, n in zip(TRIS, NORMALS):
            faces.append(tuple(base + v for v in t))
            fnorm.append(tuple(len(norms) + k for k in range(3)))
            norms += [n, n, n]
    return pos, faces, fnorm, norms


def model_resource(name, cuboids):
    pos, faces, fnorm, norms = mesh(cuboids)
    L = lambda t: "\t\t\t\t" + " ".join(str(v) for v in t) + "\n"
    F = lambda t: "\t\t\t\t" + " ".join("%.6f" % v for v in t) + "\n"
    s = '\tRESOURCE %s {\n'
    s = ('\t\tRESOURCE_NAME "%s"\n\t\tMODEL_TYPE "MESH"\n\t\tMESH {\n\t\t\tFACE_COUNT %d\n\t\t\tMODEL_POSITION_COUNT %d\n'
         '\t\t\tMODEL_NORMAL_COUNT %d\n\t\t\tMODEL_DIFFUSE_COLOR_COUNT 0\n\t\t\tMODEL_SPECULAR_COLOR_COUNT 0\n'
         '\t\t\tMODEL_TEXTURE_COORD_COUNT 0\n\t\t\tMODEL_BONE_COUNT 0\n\t\t\tMODEL_SHADING_COUNT 1\n'
         '\t\t\tMODEL_SHADING_DESCRIPTION_LIST {\n\t\t\t\tSHADING_DESCRIPTION 0 {\n\t\t\t\t\tTEXTURE_LAYER_COUNT 0\n'
         '\t\t\t\t\tSHADER_ID 0\n\t\t\t\t}\n\t\t\t}\n') % (name, len(faces), len(pos), len(norms))
    s += "\t\t\tMESH_FACE_POSITION_LIST {\n" + "".join(L(f) for f in faces) + "\t\t\t}\n"
    s += "\t\t\tMESH_FACE_NORMAL_LIST {\n" + "".join(L(f) for f in fnorm) + "\t\t\t}\n"
    s += "\t\t\tMESH_FACE_SHADING_LIST {\n" + "\t\t\t\t0\n" * len(faces) + "\t\t\t}\n"
    s += "\t\t\tMODEL_POSITION_LIST {\n" + "".join(F(p) for p in pos) + "\t\t\t}\n"
    s += "\t\t\tMODEL_NORMAL_LIST {\n" + "".join(F(n) for n in norms) + "\t\t\t}\n\t\t}\n"
    return s


def node(kind, name, res, tm):
    rows = "\n".join("\t\t\t\t" + " ".join("%.6f" % v for v in tm[i * 4:i * 4 + 4]) for i in range(4))
    return ('NODE "%s" {\n\tNODE_NAME "%s"\n\tPARENT_LIST {\n\t\tPARENT_COUNT 1\n\t\tPARENT 0 {\n\t\t\tPARENT_NAME "<NULL>"\n'
            '\t\t\tPARENT_TM {\n%s\n\t\t\t}\n\t\t}\n\t}\n\tRESOURCE_NAME "%s"\n%s}\n\n') % (kind, name, rows, res, "")


PARTS = [
    # name, cuboids as (xmin,xmax,ymin,ymax,zmin,zmax), diffuse rgb, opacity
    ("LBracket", [(-30, 30, -10, 10, 0, 10), (-30, -20, -10, 10, 10, 50)], (0.88, 0.56, 0.34), 1.0),
    ("Block", [(35, 55, -10, 10, 0, 20)], (0.25, 0.45, 0.85), 1.0),
    ("TextMesh", text_cuboids(), (0.9, 0.1, 0.1), 1.0),     # filled "60" (thin cuboids), material colour
    ("TextQuads", text_quads(), (0.1, 0.55, 0.15), 1.0),    # same "60" as double-sided flat quads, shifted -24 (green)
    ("Anchors", "P", (0.9, 0.2, 0.9), 1.0),     # point set: the measurement anchor points
    ("Measure", None, (0.05, 0.05, 0.05), 1.0),     # None = the line-set measurement layer; emissive in the material below
]


def main(out):
    I = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    t = "FILE_FORMAT \"IDTF\"\nFORMAT_VERSION 100\n\n"
    # camera and light: the sample's DefaultView (eye on -Y, tilted 15 degrees, looking at the origin) and an omni light
    t += ('NODE "VIEW" {\n\tNODE_NAME "DefaultView"\n\tPARENT_LIST {\n\t\tPARENT_COUNT 1\n\t\tPARENT 0 {\n\t\t\tPARENT_NAME "<NULL>"\n'
          '\t\t\tPARENT_TM {\n\t\t\t\t1.000000 0.000000 0.000000 0.000000\n\t\t\t\t0.000000 0.258819 0.965926 0.000000\n'
          '\t\t\t\t0.000000 -0.965926 0.258819 0.000000\n\t\t\t\t12.500000 -241.481461 89.704765 1.000000\n\t\t\t}\n\t\t}\n\t}\n'
          '\tRESOURCE_NAME "SceneViewResource"\n\tVIEW_DATA {\n\t\tVIEW_TYPE "PERSPECTIVE"\n\t\tVIEW_PROJECTION 34.515877\n\t}\n}\n\n')
    for name, _, _, _ in PARTS:
        t += node("MODEL", name, name, I)
    t += node("LIGHT", "Omni01", "DefaultPointLight", [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 60, -150, 120, 1])
    t += ('RESOURCE_LIST "VIEW" {\n\tRESOURCE_COUNT 1\n\tRESOURCE 0 {\n\t\tRESOURCE_NAME "SceneViewResource"\n\t\tVIEW_PASS_COUNT 1\n'
          '\t\tVIEW_ROOT_NODE_LIST {\n\t\t\tROOT_NODE 0 {\n\t\t\t\tROOT_NODE_NAME "<NULL>"\n\t\t\t}\n\t\t}\n\t}\n}\n\n')
    t += ('RESOURCE_LIST "LIGHT" {\n\tRESOURCE_COUNT 1\n\tRESOURCE 0 {\n\t\tRESOURCE_NAME "DefaultPointLight"\n\t\tLIGHT_TYPE "POINT"\n'
          '\t\tLIGHT_COLOR 1.000000 1.000000 1.000000\n\t\tLIGHT_ATTENUATION 1.000000 0.000000 0.000000\n\t\tLIGHT_INTENSITY 1.000000\n\t}\n}\n\n')
    t += 'RESOURCE_LIST "MODEL" {\n\tRESOURCE_COUNT %d\n' % len(PARTS)
    for i, (name, cub, _, _) in enumerate(PARTS):
        t += "\tRESOURCE %d {\n" % i + (lineset_resource(name) if cub is None else pointset_resource(name) if cub == "P" else model_resource(name, cub)) +"\t}\n"
    t += "}\n\n"
    t += 'RESOURCE_LIST "SHADER" {\n\tRESOURCE_COUNT %d\n' % len(PARTS)
    for i, (name, _, _, _) in enumerate(PARTS):
        t += '\tRESOURCE %d {\n\t\tRESOURCE_NAME "%sShader"\n\t\tSHADER_MATERIAL_NAME "%sMat"\n\t\tSHADER_ACTIVE_TEXTURE_COUNT 0\n\t}\n' % (i, name, name)
    t += "}\n\n"
    t += 'RESOURCE_LIST "MATERIAL" {\n\tRESOURCE_COUNT %d\n' % len(PARTS)
    for i, (name, cub, c, op) in enumerate(PARTS):
        amb = " ".join("%.6f" % (v * 0.2) for v in c)
        t += ('\tRESOURCE %d {\n\t\tRESOURCE_NAME "%sMat"\n\t\tMATERIAL_AMBIENT %s\n\t\tMATERIAL_DIFFUSE %s\n'
              '\t\tMATERIAL_SPECULAR 0.072000 0.072000 0.072000\n\t\tMATERIAL_EMISSIVE %s\n'
              '\t\tMATERIAL_REFLECTIVITY 0.100000\n\t\tMATERIAL_OPACITY %.6f\n\t}\n') % (i, name, amb, " ".join("%.6f" % v for v in c), "1.000000 0.100000 0.100000" if cub is None else "0.000000 0.000000 0.000000", op)
    t += "}\n\n"
    for name, _, _, _ in PARTS:
        t += ('MODIFIER "SHADING" {\n\tMODIFIER_NAME "%s"\n\tPARAMETERS {\n\t\tSHADER_LIST_COUNT 1\n\t\tSHADER_LIST_LIST {\n'
              '\t\t\tSHADER_LIST 0 {\n\t\t\t\tSHADER_COUNT 1\n\t\t\t\tSHADER_NAME_LIST {\n\t\t\t\t\tSHADER 0 NAME: "%sShader"\n'
              '\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n}\n\n') % (name, name)
    open(out, "w", newline="\n").write(t)
    print("wrote", out)


if __name__ == "__main__":
    main(sys.argv[1])
