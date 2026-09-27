"""Generates structural-analysis result files (CalculiX .frd) with gmsh and ccx, both shipped with FreeCAD 1.1.

  python make_structural_samples.py <output directory> [mesh scale]

The samples in this folder use a mesh scale of 3.5 (coarse, small files); 1.0 gives about 90 000 nodes and 65 MB files, a good large result for the lazy step loading.
See README.txt in this folder for the prerequisites.

Cases (mm, N, MPa; steel E = 210000, nu = 0.3), each solved in four load steps (25, 50, 75, 100 %):
  ibeam_cantilever   an I-beam (100 x 60 x 1000, flanges 8, web 5) fixed at one end, a vertical tip load
  ibeam_torsion      the same beam, a torque at the free end (a couple of side forces on the flange tips)
  plate_with_hole    a 100 x 50 x 5 plate with a hole of radius 10 pulled at one end: the stress concentration
"""
import math
import os
import re
import subprocess
import sys

BIN = r'C:\Program Files\FreeCAD 1.1\bin'
GMSH = os.path.join(BIN, 'gmsh.exe')
CCX = os.path.join(BIN, 'ccx.exe')
E, NU = 210000.0, 0.3


def run(cmd, cwd):
    env = dict(os.environ)
    env['PATH'] = BIN + os.pathsep + env.get('PATH', '')
    r = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True, timeout=900)
    return r


def mesh(name, geo, workdir):
    open(os.path.join(workdir, name + '.geo'), 'w').write(geo)
    r = run([GMSH, name + '.geo', '-3', '-order', '2', '-format', 'inp', '-save_all', '-o', name + '_mesh.inp'], workdir)
    if r.returncode != 0:
        print(r.stdout[-2000:], r.stderr[-2000:])
        raise SystemExit('gmsh failed for ' + name)
    return os.path.join(workdir, name + '_mesh.inp')


def read_inp(path):
    """nodes {id: (x, y, z)}, tets [(id, [10 node ids])] of the C3D10 elements."""
    nodes, tets = {}, []
    mode, etype = None, None
    pending = None
    for raw in open(path):
        line = raw.strip()
        if not line or line.startswith('**'):
            continue
        if line.startswith('*'):
            key = line.split(',')[0].upper()
            mode = key
            etype = None
            m = re.search(r'TYPE=([A-Za-z0-9]+)', line, re.I)
            etype = m.group(1).upper() if m else None
            pending = None
            continue
        parts = [p for p in line.replace(',', ' ').split() if p]
        if mode == '*NODE':
            nodes[int(parts[0])] = tuple(float(v) for v in parts[1:4])
        elif mode == '*ELEMENT' and etype in ('C3D10', 'C3D4'):
            need = 11 if etype == 'C3D10' else 5
            data = ([] if pending is None else pending) + [int(p) for p in parts]
            if len(data) >= need:
                tets.append((data[0], data[1:need]))
                pending = None
            else:
                pending = data
    return nodes, tets


def write_deck(path, nodes, tets, fixed, loads_per_step, note):
    """`loads_per_step` = list of {node id: (fx, fy, fz)} (cumulative per step, replaced each step)."""
    used = sorted({n for _, t in tets for n in t})
    with open(path, 'w') as f:
        f.write('** %s\n*NODE\n' % note)
        for n in used:
            x, y, z = nodes[n]
            f.write('%d, %.6f, %.6f, %.6f\n' % (n, x, y, z))
        f.write('*ELEMENT, TYPE=C3D10, ELSET=EALL\n')
        for eid, t in tets:
            f.write('%d, %s\n' % (eid, ', '.join(str(n) for n in t)))
        f.write('*NSET, NSET=FIXED\n')
        for i in range(0, len(fixed), 8):
            f.write(', '.join(str(n) for n in fixed[i:i + 8]) + ',\n')
        f.write('*MATERIAL, NAME=STEEL\n*ELASTIC\n%g, %g\n*SOLID SECTION, ELSET=EALL, MATERIAL=STEEL\n*BOUNDARY\nFIXED, 1, 3\n' % (E, NU))
        for k, loads in enumerate(loads_per_step):
            f.write('*STEP\n*STATIC\n*CLOAD, OP=NEW\n')
            for n, (fx, fy, fz) in sorted(loads.items()):
                for d, v in ((1, fx), (2, fy), (3, fz)):
                    if v != 0.0:
                        f.write('%d, %d, %.6g\n' % (n, d, v))
            f.write('*NODE FILE\nU\n*EL FILE\nS\n*END STEP\n')


def solve(name, workdir):
    r = run([CCX, name], workdir)
    if not os.path.exists(os.path.join(workdir, name + '.frd')):
        print(r.stdout[-3000:], r.stderr[-1500:])
        raise SystemExit('ccx failed for ' + name)
    return r.stdout


def ibeam_geo(L, H, B, tf, tw, size):
    return '''SetFactory("OpenCASCADE");
Box(1) = {0, %g, %g, %g, %g, %g};
Box(2) = {0, %g, %g, %g, %g, %g};
Box(3) = {0, %g, %g, %g, %g, %g};
BooleanUnion{ Volume{1}; Delete; }{ Volume{2, 3}; Delete; }
Mesh.MeshSizeMax = %g;
Mesh.MeshSizeMin = %g;
Mesh.Algorithm3D = 1;
''' % (-tw / 2, -H / 2 + tf, L, tw, H - 2 * tf,
       -B / 2, -H / 2, L, B, tf,
       -B / 2, H / 2 - tf, L, B, tf, size, size / 2)


def main(outdir, scale=1.0):
    os.makedirs(outdir, exist_ok=True)
    work = os.path.join(outdir, '_work')
    os.makedirs(work, exist_ok=True)

    # ---------------- I-beam
    L, H, B, tf, tw = 1000.0, 100.0, 60.0, 8.0, 5.0
    inp = mesh('ibeam', ibeam_geo(L, H, B, tf, tw, 6.0 * scale), work)
    nodes, tets = read_inp(inp)
    used = {n for _, t in tets for n in t}
    print('I-beam mesh: %d nodes, %d C3D10' % (len(used), len(tets)))
    tol = 1e-3
    fixed = sorted(n for n in used if abs(nodes[n][0]) < tol)
    tip = sorted(n for n in used if abs(nodes[n][0] - L) < tol)
    P = 4000.0  # N, downwards
    steps = [0.25, 0.5, 0.75, 1.0]
    cant = [{n: (0.0, 0.0, -P * s / len(tip)) for n in tip} for s in steps]
    write_deck(os.path.join(work, 'ibeam_cantilever.inp'), nodes, tets, fixed, cant, 'I-beam cantilever, tip load %g N' % P)
    solve('ibeam_cantilever', work)
    # torsion: the flange tips at the free end pushed sideways in opposite directions (a couple of T / H)
    T = 300000.0  # N mm
    top = [n for n in tip if nodes[n][2] > H / 2 - tf - tol]
    bottom = [n for n in tip if nodes[n][2] < -H / 2 + tf + tol]
    f = T / (H - tf)
    torsion = []
    for s in steps:
        loads = {}
        for n in top:
            loads[n] = (0.0, f * s / len(top), 0.0)
        for n in bottom:
            loads[n] = (0.0, -f * s / len(bottom), 0.0)
        torsion.append(loads)
    write_deck(os.path.join(work, 'ibeam_torsion.inp'), nodes, tets, fixed, torsion, 'I-beam cantilever, torque %g N mm' % T)
    solve('ibeam_torsion', work)

    # theory check for the cantilever: delta = P L^3 / (3 E I), I about the strong axis
    I = (tw * (H - 2 * tf) ** 3) / 12.0 + 2 * (B * tf ** 3 / 12.0 + B * tf * ((H - tf) / 2.0) ** 2)
    print('I = %.0f mm^4, tip deflection by beam theory: %.3f mm (shear adds a little)' % (I, P * L ** 3 / (3 * E * I)))

    # ---------------- plate with a hole
    plate_geo = '''SetFactory("OpenCASCADE");
Box(1) = {0, -25, 0, 100, 50, 5};
Cylinder(2) = {50, 0, -1, 0, 0, 7, 10};
BooleanDifference{ Volume{1}; Delete; }{ Volume{2}; Delete; }
Mesh.MeshSizeMax = %g;
Mesh.MeshSizeMin = %g;
Mesh.MeshSizeFromCurvature = 24;
Mesh.Algorithm3D = 1;
''' % (4.0 * scale, 1.2 * scale)
    inp = mesh('plate', plate_geo, work)
    nodes, tets = read_inp(inp)
    used = {n for _, t in tets for n in t}
    print('plate mesh: %d nodes, %d C3D10' % (len(used), len(tets)))
    fixed = sorted(n for n in used if abs(nodes[n][0]) < tol)
    pull = sorted(n for n in used if abs(nodes[n][0] - 100.0) < tol)
    sigma = 100.0  # MPa on the 50 x 5 end face
    total = sigma * 50.0 * 5.0
    plate = [{n: (total * s / len(pull), 0.0, 0.0) for n in pull} for s in steps]
    write_deck(os.path.join(work, 'plate_with_hole.inp'), nodes, tets, fixed, plate, 'plate with a hole, pulled with %g MPa' % sigma)
    solve('plate_with_hole', work)

    for name in ('ibeam_cantilever', 'ibeam_torsion', 'plate_with_hole'):
        src = os.path.join(work, name + '.frd')
        dst = os.path.join(outdir, name + '.frd')
        open(dst, 'wb').write(open(src, 'rb').read())
        print('wrote', dst, os.path.getsize(dst) // 1024, 'KB')


if __name__ == '__main__':
    main(sys.argv[1], float(sys.argv[2]) if len(sys.argv) > 2 else 1.0)
