"""Generates large multi-step result files for trying the lazy step loading (nothing here is committed except this script and the README).

    python make_large_samples.py [--result-tests <path to result_tests.exe>] [--only exodus|cgns|frd] [--small]

  exodus_1M_hex.exo     1 000 000 hexahedra, 5 steps (about 360 MB of data in memory if read eagerly)
  cgns_1M_hex.cgns      1 000 000 hexahedra, 5 steps, vertex vectors and a cell field
  ibeam_cantilever_fine.frd   the I-beam of ../ibeam_cantilever.frd on a fine mesh (about 90 000 nodes, 65 MB, 4 steps)

--small writes a tenth of the size (100 000 cells; the I-beam at mesh scale 2.0) for a quick try.
The files are written next to this script and are ignored by git. See README.txt for the prerequisites.
"""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))


def find_result_tests(explicit):
    candidates = [explicit] if explicit else []
    for build in ('ninja_release_vcpkg', 'ninja_debug_vcpkg'):
        candidates.append(os.path.join(ROOT, 'out', 'build', build, 'tests', 'result_tests.exe'))
        candidates.append(os.path.join(ROOT, 'out', 'build', build, 'tests', 'result_tests'))
    for c in candidates:
        if c and os.path.isfile(c):
            return c
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--result-tests', help='path to the built result_tests executable')
    ap.add_argument('--only', choices=['exodus', 'cgns', 'frd'])
    ap.add_argument('--small', action='store_true')
    a = ap.parse_args()
    n = 46 if a.small else 100  # n^3 cells
    tool = find_result_tests(a.result_tests)
    for kind, name, args in (('exodus', 'exodus_1M_hex.exo', ['--write-exodus-sample']),
                             ('cgns', 'cgns_1M_hex.cgns', ['--write-cgns-sample'])):
        if a.only and a.only != kind:
            continue
        if not tool:
            print('skipping %s: build the result_tests target first, or pass --result-tests' % kind)
            continue
        out = os.path.join(HERE, name)
        print('writing', out)
        r = subprocess.run([tool] + args + [out, str(n)])
        if r.returncode != 0:
            print('failed:', name)
    if not a.only or a.only == 'frd':
        out = os.path.join(HERE, 'frd_work')
        r = subprocess.run([sys.executable, os.path.join(HERE, '..', 'make_structural_samples.py'), out, '2.0' if a.small else '1.0'])
        src = os.path.join(out, 'ibeam_cantilever.frd')
        if r.returncode == 0 and os.path.isfile(src):
            dst = os.path.join(HERE, 'ibeam_cantilever_fine.frd')
            os.replace(src, dst)
            print('wrote', dst)
            shutil.rmtree(out, ignore_errors=True)


if __name__ == '__main__':
    main()
