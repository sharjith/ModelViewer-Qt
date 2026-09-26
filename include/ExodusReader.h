#pragma once

#include "ResultReader.h"

#include <QString>
#include <QStringList>

// Exodus II result reader (.e, .exo, .ex2, .g) - docs/simulation_results_design.md section 11. Written on top of the
// NetCDF C library (Exodus is a NetCDF layout, classic or netCDF-4/HDF5), which is an OPTIONAL dependency: when the
// build has no NetCDF (MV_HAVE_NETCDF not defined) these formats are simply not offered and readExodus() reports that.
//
// Read:
//   - coordinates (coordx/coordy/coordz, or the older coord array) and the optional node/element id maps;
//   - the element blocks (connect1 ... connectN), each with its elem_type: HEX, TET/TETRA, WEDGE, PYRAMID,
//     TRI/TRIANGLE, QUAD/SHELL in their linear and quadratic (8/10/13/15/6-node) forms; anything else (beams, spheres,
//     polygons, polyhedra) keeps its place in the element numbering but is not displayed. Node numbering of these
//     types matches what the boundary extraction needs (corner nodes first).
//   - time steps (time_whole) - one step per stored time; a file without time steps is one static step;
//   - node variables (vals_nod_var<i>) and element variables (vals_elem_var<i>eb<j>) as node and cell fields.
//     Exodus stores every component as its own variable, so variables named like <base>_x/_y/_z are gathered into one
//     vector field <base> (a 2-D pair _x/_y gets a zero z), and <base>_xx/_yy/_zz/_xy/_yz/_zx into one symmetric tensor
//     (XX YY ZZ XY YZ ZX order, so von Mises and the principal stresses are derived for names containing "stress").
//     A variable an element block does not define is NaN there.
//
// Exodus files carry no units, so none are assigned beyond the usual guess from the field name. All steps are read
// when the file is opened. Paths with non-ASCII characters may fail to open on Windows (NetCDF takes a narrow string).

// True when this build can read Exodus files (NetCDF was found at configure time).
bool exodusSupported();
// Lower-case extensions and the file-dialog filter for them; empty when Exodus is not supported by this build.
QStringList exodusExtensions();
QString exodusFileFilter();

ResultReadOutcome readExodus(const QString& path, const std::atomic<bool>* cancel = nullptr);
