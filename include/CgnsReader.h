#pragma once

#include "ResultReader.h"

#include <QString>
#include <QStringList>

// CGNS result reader (.cgns) - docs/simulation_results_design.md section 11. Written on the CGNS library (HDF5 or the older
// ADF files), an OPTIONAL dependency exactly like NetCDF is for Exodus: without it (MV_HAVE_CGNS not defined) the format is
// not offered and readCgns() reports that.
//
// Read, for every zone of every base - unstructured or structured (user-defined zones are skipped with a warning):
//   - a STRUCTURED zone (multi-block CFD grids) has no element sections: its cells are the hexahedra (3-D base) or quads (2-D
//     base) between neighbouring grid points, numbered i fastest like CGNS's own coordinate and solution arrays. Adjoining
//     blocks keep their own points, so a block interface shows as a pair of coincident faces (as it does for several
//     unstructured zones); the block faces are the boundary the viewer draws.
//   - the vertex coordinates (CoordinateX/Y/Z) and the elements of the highest dimension of the base - volume cells of a 3-D
//     base, faces of a 2-D one - from every section, fixed-type or MIXED, in element-number order. Boundary-condition
//     sections (lower-dimension elements) are not read: they would draw the same faces twice. HEXA/PENTA/TETRA/PYRA,
//     TRI and QUAD in their linear and quadratic forms are displayed (the 27/18/14/9-node variants through their first 20/15/13/8
//     nodes); polyhedral (NGON_n/NFACE_n) and other element types keep their place but are not drawn.
//   - flow solutions: a FlowSolution_t at Vertex location gives node fields, at CellCenter location cell fields (other
//     locations are skipped). The i-th Vertex solution and the i-th CellCenter solution of a zone are one time step; the time
//     values come from BaseIterativeData/TimeValues when present, otherwise the step index. Fields named <base>X/<base>Y/
//     <base>Z (VelocityX, VelocityY, VelocityZ ...) are gathered into one vector field <base>.
//   - the base's length unit when it is metres or millimetres.
// Several zones are concatenated into one mesh (their solutions with them). All steps are read when the file is opened.
// Paths with non-ASCII characters may fail to open on Windows (the library takes a narrow string).

bool cgnsSupported();
QStringList cgnsExtensions(); // {"cgns"}, or empty when this build has no CGNS library
QString cgnsFileFilter();

ResultReadOutcome readCgns(const QString& path, const std::atomic<bool>* cancel = nullptr);
