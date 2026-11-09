#!/usr/bin/env python3
"""Many-node stress scene for the 3D PDF spike: an N x N grid of separate cubes (one node, mesh, shader and material each),
coloured in a gradient. Tests node count, file size and Acrobat's responsiveness, and per-view visibility with many nodes.

    python tools/3dpdf/make_stress_scene_idtf.py out/3dpdf/run/stress.idtf 20
Then convert and wrap as in the research doc; pass --node for every "CubeNNN" or use --nodes-file (see make_u3d_pdf.py).
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_test_scene_idtf as m

n = int(sys.argv[2]) if len(sys.argv) > 2 else 20
m.PARTS = [("Cube%03d" % (i * n + j), [(j * 12, j * 12 + 8, i * 12, i * 12 + 8, 0, 8)],
            (i / (n - 1), j / (n - 1), 0.5), 1.0) for i in range(n) for j in range(n)]
m.main(sys.argv[1])
print("centre %.1f %.1f 4, radius %.1f, nodes %d" % ((n - 1) * 6 + 4, (n - 1) * 6 + 4, (n - 1) * 6 * 1.45 + 6, n * n))
