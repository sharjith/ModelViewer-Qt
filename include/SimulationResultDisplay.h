#pragma once

// GUI-free pieces of showing a simulation result in the viewport - see docs/simulation_results_design.md
// (sections 5 and 6). QtCore + standard library only, so it is unit-testable and safe on a worker thread.
// The GL/scene glue lives in src/ModelViewerSimulation.cpp.

#include "ResultBoundary.h"
#include "ResultDataset.h"
#include "ResultReader.h"

#include <QString>
#include <QStringList>

#include <atomic>
#include <memory>
#include <vector>

// Everything the worker thread produces: the dataset and its displayable boundary. The dataset stays the
// source of truth (kept alive for later phases: probe, interior sections, time steps).
struct LoadedSimulationResult
{
	std::shared_ptr<ResultDataset> dataset; // null on failure
	ResultBoundarySurface surface;
	QStringList warnings;
	QString error; // set on failure; "cancelled" when cancelled

	bool ok() const { return dataset != nullptr; }
};

// Reads `path` (format chosen by extension) and extracts its boundary surface. Runs on a worker thread.
LoadedSimulationResult loadSimulationResult(const QString& path, const std::atomic<bool>* cancel = nullptr);

// One scalar per dataset node, taken from a node field, ready to be shown as a colour map.
struct DisplayScalar
{
	int fieldIndex = -1;  // index into ResultDataset::fields
	int component = -1;   // -1 = magnitude of a 3-component field, otherwise the component index
	QString label;        // e.g. "von Mises Stress" or "Displacement (magnitude)"
	std::vector<float> nodeValues; // one per dataset node (may contain non-finite values)
	float minValue = 0.0f; // over the finite values of ALL nodes
	float maxValue = 0.0f;

	bool valid() const { return fieldIndex >= 0 && !nodeValues.empty(); }
};

// Builds a DisplayScalar from a node field of step 0. A scalar field ignores `component`; a 3-component field
// uses `component` (0-2) or, with -1, its Euclidean magnitude; fields with other component counts need an
// explicit `component`. Returns false when the field is not a loaded node field or the request does not fit.
bool buildDisplayScalar(const ResultDataset& dataset, int fieldIndex, int component, DisplayScalar& out);

// Picks what to show when the user has not chosen yet: a scalar node field named like "von Mises" if there
// is one, otherwise the first scalar node field, otherwise the magnitude of the first 3-component node field.
// Returns false when the dataset has no usable node field (the geometry is still displayable, uncoloured).
bool chooseDefaultDisplayScalar(const ResultDataset& dataset, DisplayScalar& out);

// nodeValues (one per dataset node) -> one value per boundary-surface vertex.
std::vector<float> boundaryVertexValues(const ResultBoundarySurface& surface, const std::vector<float>& nodeValues);

// Smooth per-vertex normals for the boundary surface (area-weighted average of the adjoining triangles),
// 3 floats per vertex, unit length (falls back to +Z for a vertex whose triangles cancel out).
std::vector<float> computeSmoothVertexNormals(const ResultBoundarySurface& surface);
