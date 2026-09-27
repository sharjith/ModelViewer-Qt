"""Writes a small MED file (mesh + fields) for developing and testing ModelViewer's MED reader.

Run it with the Python that ships with SALOME or with the standalone MEDCoupling package (the one where
`from MEDLoader import *` works). The pip package "medcoupling" does NOT work: it is built without MED file I/O.

    python make_med_sample.py block.med

The file holds one mesh "block" (an n x n x n block of HEXA8 cells) and, over five time steps 0 .. 1:
  - node fields     "temperature" (1 component) and "displacement" (3 components: dx, dy, dz)
  - a cell field    "quality" (one value per cell)
The values are synthetic (a bending, warming cube). This is only a fixture generator: a real result file from Code_Aster or
SALOME (see ELNO / ELGA notes in the MED reader design) is the true test.
"""
import math
import sys

from MEDLoader import (MEDCouplingUMesh, MEDCouplingFieldDouble, DataArrayDouble, WriteUMesh, WriteField,
                       WriteFieldUsingAlreadyWrittenMesh, ON_NODES, ON_CELLS, NORM_HEXA8)

path = sys.argv[1] if len(sys.argv) > 1 else "block.med"
n = int(sys.argv[2]) if len(sys.argv) > 2 else 8
side = n + 1


def node(i, j, k):
    return i + side * (j + side * k)


coords = []
for k in range(side):
    for j in range(side):
        for i in range(side):
            coords += [float(i), float(j), float(k)]

mesh = MEDCouplingUMesh("block", 3)
mesh.allocateCells(n * n * n)
for k in range(n):
    for j in range(n):
        for i in range(n):
            mesh.insertNextCell(NORM_HEXA8, [node(i, j, k), node(i + 1, j, k), node(i + 1, j + 1, k), node(i, j + 1, k),
                                             node(i, j, k + 1), node(i + 1, j, k + 1), node(i + 1, j + 1, k + 1), node(i, j + 1, k + 1)])
mesh.finishInsertingCells()
array = DataArrayDouble(coords, side ** 3, 3)
array.setInfoOnComponents(["X", "Y", "Z"])
mesh.setCoords(array)
WriteUMesh(path, mesh, True)  # True: start a new file

steps = 5
for s in range(steps):
    t = s / (steps - 1)
    temperature, displacement = [], []
    for k in range(side):
        for j in range(side):
            for i in range(side):
                x, y, z = i / n, j / n, k / n
                temperature.append(20.0 + 80.0 * t * x)
                displacement += [-t * 0.15 * x * (z - 0.5), 0.0, t * 0.15 * x * x]
    quality = [0.5 + 0.5 * math.sin(0.1 * c + 2.0 * t) for c in range(n * n * n)]

    def write(field, first):
        # the first field of the first step also writes the mesh reference; later ones reuse it
        if first:
            WriteField(path, field, False)
        else:
            WriteFieldUsingAlreadyWrittenMesh(path, field)

    for name, values, components, support in (("temperature", temperature, ["T"], ON_NODES),
                                              ("displacement", displacement, ["dx", "dy", "dz"], ON_NODES),
                                              ("quality", quality, ["q"], ON_CELLS)):
        field = MEDCouplingFieldDouble(support)
        field.setName(name)
        field.setMesh(mesh)
        count = side ** 3 if support == ON_NODES else n * n * n
        data = DataArrayDouble(values, count, len(components))
        data.setInfoOnComponents(components)
        field.setArray(data)
        field.setTime(t, s, 0)  # time value, iteration, order
        write(field, first=(s == 0 and name == "temperature"))

print("wrote", path)
