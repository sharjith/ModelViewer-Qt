#!/usr/bin/env python3
"""Throwaway spike: does Acrobat play a U3D keyframe animation (MOTION resource + ANIMATION modifier) in a hand-built 3D PDF?

Takes the test box (box_origin.idtf: node "Box01", translated to z = -20), adds a motion that lifts it 40 units and lowers it again
while spinning 90 degrees about Z, converts it with IDTFConverter.exe and wraps the U3D in a PDF with make_u3d_pdf.py. Key frame
values are the node's local transform (displacement, rotation quaternion w x y z, scale), not offsets. Standard library only.

    python tools/3dpdf/make_animation_spike.py out/3dpdf/run/box_origin.idtf out/3dpdf/run/anim_box.idtf
    (then IDTFConverter.exe -input anim_box.idtf -output anim_box.u3d, then make_u3d_pdf.py anim_box.u3d spike_anim.pdf --center ...)
"""
import math
import sys


def main():
    src = open(sys.argv[1], encoding="utf-8").read()
    out = sys.argv[2]
    half = math.radians(90.0) / 2.0
    q = (math.cos(half), 0.0, 0.0, math.sin(half))   # 90 degrees about Z: w x y z

    def key(i, t, z, rot):
        return ("\t\t\t\t\tKEY_FRAME %d {\n\t\t\t\t\t\tKEY_FRAME_TIME %.3f\n\t\t\t\t\t\tKEY_FRAME_DISPLACEMENT 0.0 0.0 %.3f\n"
                "\t\t\t\t\t\tKEY_FRAME_ROTATION %.6f %.6f %.6f %.6f\n\t\t\t\t\t\tKEY_FRAME_SCALE 1.0 1.0 1.0\n\t\t\t\t\t}\n"
                % (i, t, z, rot[0], rot[1], rot[2], rot[3]))

    frames = key(0, 0.0, -20.0, (1.0, 0.0, 0.0, 0.0)) + key(1, 1.5, 20.0, q) + key(2, 3.0, -20.0, (1.0, 0.0, 0.0, 0.0))
    motion = ('RESOURCE_LIST "MOTION" {\n\tRESOURCE_COUNT 1\n\tRESOURCE 0 {\n\t\tRESOURCE_NAME "Box01-Key"\n\t\tMOTION_TRACK_COUNT 1\n'
              '\t\tMOTION_TRACK_LIST {\n\t\t\tMOTION_TRACK 0 {\n\t\t\t\tMOTION_TRACK_NAME "Box01"\n\t\t\t\tMOTION_TRACK_SAMPLE_COUNT 3\n'
              '\t\t\t\tKEY_FRAME_LIST {\n' + frames + '\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n}\n\n')
    modifier = ('MODIFIER "ANIMATION" {\n\tMODIFIER_NAME "Box01"\n\tPARAMETERS {\n\t\tATTRIBUTE_ANIMATION_PLAYING "TRUE"\n'
                '\t\tATTRIBUTE_ROOT_BONE_LOCKED "FALSE"\n\t\tATTRIBUTE_SINGLE_TRACK "TRUE"\n\t\tATTRIBUTE_AUTO_BLEND "TRUE"\n'
                '\t\tTIME_SCALE 1.0\n\t\tBLEND_TIME 0.5\n\t\tMOTION_COUNT 1\n\t\tMOTION_INFO_LIST {\n\t\t\tMOTION_INFO 0 {\n'
                '\t\t\t\tMOTION_NAME "Box01-Key"\n\t\t\t\tATTRIBUTE_LOOP "TRUE"\n\t\t\t\tATTRIBUTE_SYNC "FALSE"\n'
                '\t\t\t\tTIME_OFFSET 0.0\n\t\t\t\tTIME_SCALE 1.0\n\t\t\t}\n\t\t}\n\t}\n}\n\n')
    i = src.index('MODIFIER "SHADING"')
    open(out, "w", encoding="utf-8").write(src[:i] + motion + modifier + src[i:])
    print("wrote", out)


if __name__ == "__main__":
    main()
