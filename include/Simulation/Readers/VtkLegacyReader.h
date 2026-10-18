#pragma once

#include "ResultReader.h"

// VTK legacy (.vtk) reader. ASCII and BINARY (big-endian) files, file versions 1.0 - 5.1.
//
// Datasets: UNSTRUCTURED_GRID, POLYDATA (vertices, lines, polygons, strips), STRUCTURED_GRID,
// STRUCTURED_POINTS, RECTILINEAR_GRID. Structured datasets get their implicit hexahedron / quad / line
// cells generated. Attributes: SCALARS, COLOR_SCALARS, VECTORS, NORMALS, TENSORS, TEXTURE_COORDINATES and
// FIELD arrays whose tuple count matches the point/cell count (other FIELD arrays are metadata and are
// ignored). Cells are read with both the classic layout (CELLS n size) and the 5.x layout
// (OFFSETS / CONNECTIVITY).
//
// Attribute-only files (no DATASET, e.g. VTKData's blowAttr.vtk) are rejected with a clear message.
// Polygons with more than 4 nodes, polylines and triangle strips are kept as Unsupported placeholders so
// cell indices and cell data stay aligned with the file.
//
// Not part of the public loading API - use readResultFile() in ResultReader.h.
ResultReadOutcome readVtkLegacy(const QString& path, const std::atomic<bool>* cancel);
