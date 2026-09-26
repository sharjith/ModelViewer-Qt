#pragma once

// Entry point for loading simulation result files - see docs/simulation_results_design.md (section 11).
// GUI-free: QtCore + standard library only, safe to call from a worker thread.

#include "ResultDataset.h"

#include <QString>
#include <QStringList>

#include <atomic>
#include <memory>

struct ResultReadOutcome
{
	std::unique_ptr<ResultDataset> dataset; // null on failure
	QString error;                          // set on failure (includes "cancelled")
	QStringList warnings;                   // non-fatal, e.g. "3 unsupported cells skipped"

	bool ok() const { return dataset != nullptr; }
};

// Lower-case extensions (without the dot) this build can read: "vtu", "vtk", "frd", "foam" (an OpenFOAM case) and,
// when built with NetCDF, the Exodus II ones ("e", "exo", "ex2", "g") and, with the CGNS library, "cgns".
QStringList supportedResultExtensions();

// True when the file extension is one this build can read (.vtu, .vtk, .frd, .foam).
bool isSupportedResultFile(const QString& path);

// File-dialog name filters for the supported formats, e.g. "VTK Unstructured Grid (*.vtu)".
QStringList supportedResultFileFilters();

// Reads a result file by extension. `cancel`, when given, is polled between arrays; a cancelled read
// fails with error "cancelled". Never throws; every problem is reported through `error`.
ResultReadOutcome readResultFile(const QString& path, const std::atomic<bool>* cancel = nullptr);
