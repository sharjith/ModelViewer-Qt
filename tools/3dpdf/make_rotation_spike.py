#!/usr/bin/env python3
"""Throwaway spike: which quaternion convention does a U3D key frame use, relative to a node's static PARENT_TM?

Writes four boxes in a row, all 40 x 40 x 40 (from box_origin.idtf):
  Ref_plus   static, PARENT_TM rotated +30 degrees about Z (row-vector layout, i.e. the transpose of the usual column matrix)
  Key_q      animated by constant key frames holding the quaternion (cos 15, 0, 0, sin 15)   [w x y z]
  Key_conj   animated by constant key frames holding the conjugate (cos 15, 0, 0, -sin 15)
  Ref_minus  static, PARENT_TM rotated -30 degrees
At 30 degrees a box looks different from its mirror (-30), so the animated box that matches Ref_plus shows which convention
matches a matrix whose PARENT_TM is the row-vector layout of a +30 degree rotation about Z. Standard library only.

    python tools/3dpdf/make_rotation_spike.py out/3dpdf/run/box_origin.idtf out/3dpdf/run/rot_box.idtf
"""
import math
import sys


def tm(angle_deg, tx, ty, tz):
    c, s = math.cos(math.radians(angle_deg)), math.sin(math.radians(angle_deg))
    rows = [[c, s, 0, 0], [-s, c, 0, 0], [0, 0, 1, 0], [tx, ty, tz, 1]]   # row-vector layout of a rotation about Z by +angle
    return "\n".join("\t\t\t\t" + " ".join("%.6f" % v for v in r) for r in rows)


def main():
    src = open(sys.argv[1], encoding="utf-8").read()
    out = sys.argv[2]
    i0 = src.index('NODE "MODEL"')
    i1 = src.index("RESOURCE_LIST", i0)
    node_tpl = src[i0:i1]
    m0 = src.index('MODIFIER "SHADING"')
    shading_tpl = src[m0:]
    head = src[:i0]
    mid = src[i1:m0]

    names = ["Ref_plus", "Key_q", "Key_conj", "Ref_minus"]
    xs = [-100.0, -33.0, 33.0, 100.0]
    angles = [30.0, 30.0, 30.0, -30.0]

    nodes = ""
    for n, x, a in zip(names, xs, angles):
        t = node_tpl.replace('"Box01"', '"%s"' % n, 1)   # node name only; the resource name stays Box01
        # parent TM block
        s0 = t.index("PARENT_TM {") + len("PARENT_TM {")
        s1 = t.index("}", s0)
        t = t[:s0] + "\n" + tm(a, x, 0.0, 0.0) + "\n\t\t\t" + t[s1:]
        nodes += t

    h = math.radians(30.0) / 2.0
    q = (math.cos(h), 0.0, 0.0, math.sin(h))
    qc = (q[0], 0.0, 0.0, -q[3])

    def motion(res, node, quat, x):
        def key(i, t):
            return ("\t\t\t\t\tKEY_FRAME %d {\n\t\t\t\t\t\tKEY_FRAME_TIME %.3f\n\t\t\t\t\t\tKEY_FRAME_DISPLACEMENT %.3f 0.0 0.0\n"
                    "\t\t\t\t\t\tKEY_FRAME_ROTATION %.6f %.6f %.6f %.6f\n\t\t\t\t\t\tKEY_FRAME_SCALE 1.0 1.0 1.0\n\t\t\t\t\t}\n"
                    % (i, t, x, quat[0], quat[1], quat[2], quat[3]))
        return ('\tRESOURCE %d {\n\t\tRESOURCE_NAME "%s"\n\t\tMOTION_TRACK_COUNT 1\n\t\tMOTION_TRACK_LIST {\n\t\t\tMOTION_TRACK 0 {\n'
                '\t\t\t\tMOTION_TRACK_NAME "%s"\n\t\t\t\tMOTION_TRACK_SAMPLE_COUNT 2\n\t\t\t\tKEY_FRAME_LIST {\n' % (res, "M_" + node, node)
                + key(0, 0.0) + key(1, 1.0) + '\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n')

    motions = 'RESOURCE_LIST "MOTION" {\n\tRESOURCE_COUNT 2\n' + motion(0, "Key_q", q, xs[1]) + motion(1, "Key_conj", qc, xs[2]) + "}\n\n"

    def anim(node):
        return ('MODIFIER "ANIMATION" {\n\tMODIFIER_NAME "%s"\n\tPARAMETERS {\n\t\tATTRIBUTE_ANIMATION_PLAYING "TRUE"\n'
                '\t\tATTRIBUTE_ROOT_BONE_LOCKED "FALSE"\n\t\tATTRIBUTE_SINGLE_TRACK "TRUE"\n\t\tATTRIBUTE_AUTO_BLEND "TRUE"\n'
                '\t\tTIME_SCALE 1.0\n\t\tBLEND_TIME 0.5\n\t\tMOTION_COUNT 1\n\t\tMOTION_INFO_LIST {\n\t\t\tMOTION_INFO 0 {\n'
                '\t\t\t\tMOTION_NAME "M_%s"\n\t\t\t\tATTRIBUTE_LOOP "TRUE"\n\t\t\t\tATTRIBUTE_SYNC "FALSE"\n'
                '\t\t\t\tTIME_OFFSET 0.0\n\t\t\t\tTIME_SCALE 1.0\n\t\t\t}\n\t\t}\n\t}\n}\n\n' % (node, node))

    mods = anim("Key_q") + anim("Key_conj")
    for n in names:
        mods += shading_tpl.replace('"Box01"', '"%s"' % n, 1)
        mods += "\n"
    open(out, "w", encoding="utf-8").write(head + nodes + mid + motions + mods)
    print("wrote", out)


if __name__ == "__main__":
    main()
