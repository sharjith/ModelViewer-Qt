"""Writes stress_states.vtu: a small block with THREE symmetric stress tensor fields, to try the "Tensor field" choice of the stress ellipsoids.

No prerequisites (Python 3.8+, standard library only).   Usage:  python make_stress_states_sample.py [output.vtu]

The block is 12 x 4 x 4 hexahedra, 120 x 40 x 40 (no unit is stored in the file; think mm and MPa). The values are SYNTHETIC (closed-form
stress states, no solver was run):
  Stress_bending   NODE field. A cantilever bent about z: sigma_xx = 100 (1 - x/L) (y/h), falling to zero at the free end (x = L), with a small
                   parabolic shear sigma_xy. Ellipsoids are needles along x, longest at the root's top and bottom faces, colour hot there.
  Stress_torsion   NODE field. Pure shear about the x axis (sigma_xy = 50 z/h, sigma_zx = -50 y/h): the principal directions are 45 degrees off the
                   axes and the middle principal value is zero, so the ellipsoids are tilted flat discs, strongest at the outer corners.
  Stress_element   CELL field (one tensor per cell). A two-way pull, sigma_xx = 80, sigma_yy = 40 x/L: flattened ellipsoids at the cell centres.
All three have the 'stress' name the viewer looks for, so all three are offered in the Tensor field list (the cell one marked [cells]).
"""
import sys

NX, NY, NZ = 12, 4, 4
LX, LY, LZ = 120.0, 40.0, 40.0


def main(path):
    px = [LX * i / NX for i in range(NX + 1)]
    py = [-LY / 2 + LY * j / NY for j in range(NY + 1)]
    pz = [-LZ / 2 + LZ * k / NZ for k in range(NZ + 1)]
    h = LY / 2

    def nid(i, j, k):
        return i + (NX + 1) * (j + (NY + 1) * k)

    points = [(px[i], py[j], pz[k]) for k in range(NZ + 1) for j in range(NY + 1) for i in range(NX + 1)]

    def bending(x, y, z):
        sxx = 100.0 * (1.0 - x / LX) * (y / h)
        sxy = 20.0 * (1.0 - (y / h) ** 2) * (1.0 - x / LX)
        return (sxx, 0.0, 0.0, sxy, 0.0, 0.0)  # XX YY ZZ XY YZ ZX

    def torsion(x, y, z):
        return (0.0, 0.0, 0.0, 50.0 * z / h, 0.0, -50.0 * y / h)

    def element(x, y, z):
        return (80.0, 40.0 * x / LX, 0.0, 0.0, 0.0, 0.0)

    cells, centres = [], []
    for k in range(NZ):
        for j in range(NY):
            for i in range(NX):
                cells.append([nid(i, j, k), nid(i + 1, j, k), nid(i + 1, j + 1, k), nid(i, j + 1, k),
                              nid(i, j, k + 1), nid(i + 1, j, k + 1), nid(i + 1, j + 1, k + 1), nid(i, j + 1, k + 1)])
                centres.append(((px[i] + px[i + 1]) / 2, (py[j] + py[j + 1]) / 2, (pz[k] + pz[k + 1]) / 2))

    def array(name, comps, rows, kind='Float32', fmt='%.6g'):
        body = '\n'.join(' '.join(fmt % v for v in row) for row in rows)
        label = ' Name="%s"' % name if name else ''
        return '<DataArray type="%s"%s NumberOfComponents="%d" format="ascii">\n%s\n</DataArray>' % (kind, label, comps, body)

    out = []
    out.append('<?xml version="1.0"?>')
    out.append('<VTKFile type="UnstructuredGrid" version="0.1" byte_order="LittleEndian">')
    out.append('<UnstructuredGrid>')
    out.append('<Piece NumberOfPoints="%d" NumberOfCells="%d">' % (len(points), len(cells)))
    out.append('<PointData>')
    out.append(array('Stress_bending', 6, [bending(*p) for p in points]))
    out.append(array('Stress_torsion', 6, [torsion(*p) for p in points]))
    out.append('</PointData>')
    out.append('<CellData>')
    out.append(array('Stress_element', 6, [element(*c) for c in centres]))
    out.append('</CellData>')
    out.append('<Points>')
    out.append(array('', 3, points))
    out.append('</Points>')
    out.append('<Cells>')
    out.append('<DataArray type="Int64" Name="connectivity" format="ascii">\n' + '\n'.join(' '.join(str(v) for v in c) for c in cells) + '\n</DataArray>')
    out.append('<DataArray type="Int64" Name="offsets" format="ascii">\n' + ' '.join(str(8 * (n + 1)) for n in range(len(cells))) + '\n</DataArray>')
    out.append('<DataArray type="UInt8" Name="types" format="ascii">\n' + ' '.join('12' for _ in cells) + '\n</DataArray>')
    out.append('</Cells>')
    out.append('</Piece>')
    out.append('</UnstructuredGrid>')
    out.append('</VTKFile>')
    with open(path, 'w', newline='\n') as f:
        f.write('\n'.join(out) + '\n')
    print('wrote', path, len(points), 'points,', len(cells), 'cells')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'stress_states.vtu')
