#pragma once

#include "ResultReader.h"

// CalculiX result file (.frd) reader - ASCII, short and long formats (the ones CalculiX writes with
// "*NODE FILE"/"*EL FILE"; binary .frd is rejected with a clear message).
//
// The file is fixed-width text (numbers such as -4.29349E+02 can touch each other), so it is parsed by column, not
// by whitespace. Read:
//   - the node block (ids need not be contiguous; original ids are kept),
//   - the element block (types 1-12: HE8, PE6, TE4, HE20, PE15, TE10, TR3, TR6, QU4, QU8, BE2; BE3 is kept as an
//     Unsupported placeholder; quadratic cells are displayed through their corner nodes like everywhere else),
//   - every nodal result block ("100C" records: DISP, STRESS, STRAIN, FORC, ... any name), stored as a node field
//     with the components CalculiX actually wrote (calculated entries such as "ALL" are not stored).
// A step is a distinct result time, in order of appearance; a modal analysis's "time" is the frequency and each
// mode is one step labelled "Mode n". A node absent from a result block gets NaN.
// Stress fields additionally get derived von Mises / principal / max-shear fields (see ResultDerivedFields.h).
//
// Values are exactly as written by the solver: CalculiX is unit-less, so no unit is assigned.
//
// Not part of the public loading API - use readResultFile() in ResultReader.h.
ResultReadOutcome readCalculixFrd(const QString& path, const std::atomic<bool>* cancel);
