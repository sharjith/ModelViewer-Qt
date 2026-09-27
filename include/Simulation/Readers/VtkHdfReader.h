#pragma once

#include "ResultReader.h"

#include <QString>
#include <QStringList>

// VTKHDF result reader (.vtkhdf) - docs/simulation_results_design.md section 11. ParaView's HDF5-based VTK format, written on the
// HDF5 C library (an OPTIONAL dependency like NetCDF and CGNS: without it, MV_HAVE_HDF5 not defined, the format is not offered
// and readVtkHdf() reports that).
//
// Read, from the /VTKHDF group of the file:
//   - Type "UnstructuredGrid": Points, Connectivity, Offsets and Types, from every partition of the step (partitions are
//     concatenated into one mesh: each partition keeps its own points and its connectivity is local to them, as the format
//     defines it). Polyhedra keep their place but are not drawn.
//   - Type "PolyData": Points plus the cells of the Vertices, Lines, Polygons and Strips groups (partition by partition, in that
//     order within a partition). Triangles and quads are displayed; other polygons, vertices and strips keep their place only.
//   - Type "ImageData": the regular grid from WholeExtent, Origin, Spacing and Direction, as hexahedra (or quads for a flat
//     image).
//   - PointData arrays become node fields, CellData arrays cell fields (1, 3, 6 or 9 components: a 2-component array is padded to
//     a 3-component vector; strings and other non-numeric arrays are skipped).
//   - Time steps: /VTKHDF/Steps (Values = the times, and the offsets that locate each step's points, cells and arrays); for an
//     ImageData the leading dimension of an array is the step. A moving mesh (the points differ between steps, with the same
//     topology) is shown as the first step's geometry plus a "Mesh displacement" vector field, so it can be shown deformed. A
//     topology that changes between steps is not supported: the first step's is used and later steps' data that no longer
//     fits is left out (with a warning).
// Composite types (MultiBlockDataSet, PartitionedDataSetCollection, OverlappingAMR), HyperTreeGrid, StructuredGrid and
// RectilinearGrid are reported as not supported yet. All steps are read when the file is opened.

bool vtkHdfSupported();
QStringList vtkHdfExtensions(); // {"vtkhdf"}, or empty when this build has no HDF5 library
QString vtkHdfFileFilter();

ResultReadOutcome readVtkHdf(const QString& path, const std::atomic<bool>* cancel = nullptr);
