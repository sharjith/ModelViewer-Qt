#pragma once

// Derived structural fields - see docs/simulation_results_design.md section 4.3. GUI-free (QtCore only).

#include "ResultDataset.h"

// Symmetric 3x3 tensor [xx, yy, zz, xy, yz, zx] -> eigenvalues sorted e1 >= e2 >= e3 (closed form).
void symmetricPrincipalValues(double xx, double yy, double zz, double xy, double yz, double zx,
                              double& e1, double& e2, double& e3);

// von Mises equivalent of the same tensor.
double vonMisesStress(double xx, double yy, double zz, double xy, double yz, double zx);

// For every 6-component NODE field whose name contains "stress" (case-insensitive, e.g. CalculiX's "STRESS"),
// appends five scalar node fields, for every step that has data: "<name> von Mises", "<name> max principal",
// "<name> mid principal", "<name> min principal" and "<name> max shear" (half the difference between the largest
// and smallest principal stress). The six components must be in the order XX, YY, ZZ, XY, YZ, ZX (CalculiX
// and VTK's symmetric-tensor order). A node whose components are not finite gets NaN. Fields that already exist
// by that name are not added again. Derived fields carry the source field's (empty until confirmed) unit.
void addDerivedStressFields(ResultDataset& dataset);
