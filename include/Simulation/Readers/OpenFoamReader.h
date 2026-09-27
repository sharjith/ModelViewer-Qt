#pragma once

#include "ResultReader.h"

// OpenFOAM case reader (ASCII) - docs/simulation_results_design.md section 11. GUI-free (QtCore only).
//
// Open the case through its ".foam" file (the empty marker file ParaView users create with `touch case.foam`;
// any file with that extension in the case directory works - only its location matters). Read:
//   - constant/polyMesh: points, faces, owner, neighbour (ASCII). The mesh's cells are arbitrary polyhedra; only
//     the BOUNDARY is displayed, and OpenFOAM stores it explicitly (the faces after the internal ones, each
//     belonging to its owner cell), so no polyhedron geometry is needed: the dataset keeps one Polyhedron placeholder
//     per cell plus the ready-made boundary triangles (ResultDataset::boundaryTriangles). Faces with more than 3
//     nodes are fan-triangulated.
//   - every numeric time directory, and in it every volScalarField / volVectorField / volSymmTensorField /
//     volTensorField (ASCII, `internalField` uniform or nonuniform) as a CELL field; each time directory is one step
//     (the directory name is the step's time). The boundaryField section is ignored: a boundary triangle shows its
//     owner cell's value. A symmetric tensor is reordered to XX YY ZZ XY YZ ZX like every other tensor here.
//   - the `dimensions` entry of a field, which fixes its quantity and unit where they are unambiguous (velocity in
//     m/s, pressure in Pa, temperature in K, ...). A kinematic pressure (m^2/s^2, the usual incompressible p) has no
//     quantity here and is left without a unit.
//
// Not supported (reported with a clear message, never silently misread): binary or compressed (.gz) files, decomposed
// cases (reconstruct with reconstructPar), and point fields. All steps are loaded when the case is opened.
ResultReadOutcome readOpenFoamCase(const QString& path, const std::atomic<bool>* cancel = nullptr);
