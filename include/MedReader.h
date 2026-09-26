#pragma once

#include "ResultReader.h"

#include <QString>
#include <QStringList>

// MED result reader (.med) - docs/simulation_results_design.md section 11. MED is the file format of Salome, Code_Aster and
// Code_Saturne (also written by MEDCoupling and many mesh tools). It is an HDF5 file with a fixed group layout, read here
// directly on the HDF5 C library (an OPTIONAL dependency: without it, MV_HAVE_HDF5 not defined, the format is not offered and
// readMed() reports that) - the MED library itself is not needed.
//
// Files of MED version 3 and later are read (Salome 6 and newer write them); older 2.x files use another group layout and are
// reported as such (SALOME's medimport converts them). In the file:
//   - the mesh: /ENS_MAA/<mesh>/<step>/NOE/COO (component-major coordinates) and /MAI/<GEOM>/NOD (component-major connectivity,
//     1-based node positions). Of the cell types present only those of the HIGHEST dimension are kept (a 3-D mesh usually also
//     lists its boundary faces and edges for groups; drawing them would double every face). Segments, triangles, quadrangles,
//     tetrahedra, pyramids, wedges and hexahedra, linear and quadratic, are displayed; polygons, polyhedra, points and the
//     27-node hexahedron are not. With several meshes the one the fields belong to is read (the first otherwise).
//   - fields: /CHA/<field>/<step>/NOE (nodes), MAI.<GEOM> (cells) or NOE.<GEOM> (values at the nodes of each cell) with the
//     values of every component one after the other. Time steps are the (NDT, NOR) pairs of the fields, ordered, with PDT as the
//     time. A field on Gauss points or on the nodes of each cell is shown as one value per cell (the mean of its points, said in
//     a warning); a field on a profile (a subset of nodes or cells) is NaN elsewhere. Component names ("SIXX", "DX" ...) order
//     a 6-component tensor; Code_Aster's SIXX SIYY SIZZ SIXY SIXZ SIYZ is put in the usual XX YY ZZ XY YZ XZ order.
//   - the length unit from the mesh's coordinate units when they are all m, cm or mm.
// All steps are read when the file is opened.

bool medSupported();
QStringList medExtensions(); // {"med"}, or empty when this build has no HDF5 library
QString medFileFilter();

ResultReadOutcome readMed(const QString& path, const std::atomic<bool>* cancel = nullptr);
