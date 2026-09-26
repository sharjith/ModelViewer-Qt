#pragma once

// GUI-free pieces of showing a simulation result in the viewport - see docs/simulation_results_design.md
// (sections 5 and 6). QtCore + standard library only, so it is unit-testable and safe on a worker thread.
// The GL/scene glue lives in src/ModelViewerSimulation.cpp.

#include "ResultBoundary.h"
#include "ResultDataset.h"
#include "ResultReader.h"

#include <QString>
#include <QStringList>
#include <QUuid>

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

struct SimulationSession;
struct SimulationRangeCache;

// One scalar per dataset node - or, for a cell field (cellData), per dataset cell - ready to be shown as a colour map.
// Node data is interpolated smoothly across the surface; cell data is constant over a cell, so every boundary triangle
// takes the value of the cell it belongs to and is drawn flat.
struct DisplayScalar
{
	int fieldIndex = -1;  // index into ResultDataset::fields
	int component = -1;   // -1 = magnitude of a 3-component field, otherwise the component index
	int step = 0;         // the time step the values were taken at
	QString label;        // e.g. "von Mises Stress" or "Displacement (magnitude)"
	std::vector<float> nodeValues; // one per dataset node (per cell when cellData), in `unit` (may contain non-finite values)
	bool cellData = false;         // nodeValues is indexed by cell, not by node
	float minValue = 0.0f; // over the finite values of ALL nodes (cells when cellData), in `unit`
	float maxValue = 0.0f;
	QString unit;              // the unit of nodeValues/minValue/maxValue (the field's display unit); empty = not specified
	bool unitAssumed = false;  // `unit` is a guess the user has not confirmed

	bool valid() const { return fieldIndex >= 0 && !nodeValues.empty(); }
};

// Builds a DisplayScalar from a node field of step 0, converted from the field's file unit to its display unit
// (when both are known; otherwise the numbers are untouched). A scalar field ignores `component`; a 3-component field
// uses `component` (0-2) or, with -1, its Euclidean magnitude; fields with other component counts need an
// explicit `component`. Returns false when the field is not a loaded node field or the request does not fit.
bool buildDisplayScalar(const ResultDataset& dataset, int fieldIndex, int component, DisplayScalar& out, int step = 0);

// The min/max of the field's values (in its display unit) over every step that has data for it. False when no
// step has data. `cachedAllStepsRange()` does the same through the session's cache.
bool computeAllStepsRange(const ResultDataset& dataset, int fieldIndex, int component, float& lo, float& hi);
bool cachedAllStepsRange(SimulationSession& session, int fieldIndex, int component, float& lo, float& hi);
// The same through a cache of the caller's (the arrows' field has its own, so it does not evict the colour range's).
bool cachedAllStepsRange(const ResultDataset& dataset, SimulationRangeCache& cache, int fieldIndex, int component, float& lo, float& hi);

// Text for a step: "Mode 3 - 73971 Hz", "t = 0.5", "0.0194 Hz". Empty for an out-of-range step.
QString stepTimeText(const ResultStep& step);
QString stepDescription(const ResultDataset& dataset, int step);

// Picks what to show when the user has not chosen yet: a scalar node field named like "von Mises" if there
// is one, otherwise the first scalar node field, otherwise the magnitude of the first 3-component node field.
// Returns false when the dataset has no usable node field (the geometry is still displayable, uncoloured).
bool chooseDefaultDisplayScalar(const ResultDataset& dataset, DisplayScalar& out);

// nodeValues (one per dataset node) -> one value per boundary-surface vertex.
std::vector<float> boundaryVertexValues(const ResultBoundarySurface& surface, const std::vector<float>& nodeValues);
// cellValues (one per dataset cell) -> one value per boundary triangle (the value of the cell it belongs to).
std::vector<float> boundaryFaceValues(const ResultBoundarySurface& surface, const std::vector<float>& cellValues);
// One value per boundary vertex for either kind of scalar: the node value, or for cell data the mean of the finite
// values of the triangles meeting at the vertex (an approximation, used where only per-vertex data can be written,
// e.g. the colours baked into a saved file).
std::vector<float> surfaceVertexValues(const ResultBoundarySurface& surface, const DisplayScalar& scalar);

// Size of the boundary surface's bounding box along x, y and z, in the file's own length unit. False when it has no points.
bool surfaceExtents(const ResultBoundarySurface& surface, double& x, double& y, double& z);

// Smooth per-vertex normals for the boundary surface (area-weighted average of the adjoining triangles),
// 3 floats per vertex, unit length (falls back to +Z for a vertex whose triangles cancel out). The second form
// takes the positions explicitly (a deformed shape).
std::vector<float> computeSmoothVertexNormals(const ResultBoundarySurface& surface);
std::vector<float> computeSmoothVertexNormals(const std::vector<float>& positions, const std::vector<std::uint32_t>& triangles);

// ---- Min/max markers ---------------------------------------------------------------------------------------------

// The boundary-surface vertices carrying the smallest and largest finite value of `vertexValues` (one per surface
// vertex). Only the visible surface is searched, so this can differ from the field's overall range when the true
// extreme lies in the interior. False when no vertex has a finite value; ties resolve to the lowest index.
bool findScalarExtrema(const std::vector<float>& vertexValues, std::size_t& minVertex, std::size_t& maxVertex);

// ---- Probe -------------------------------------------------------------------------------------------------------

// The shown scalar under a point of the boundary surface (triangle + barycentric weights u, v, w of its three
// vertices): interpolated between the vertices' node values. When a vertex has no value (NaN) the value of the
// nearest vertex is used instead, and the sample is invalid if that has none either.
struct ProbeSample
{
	bool valid = false;
	float value = 0.0f;         // in the scalar's unit
	bool cell = false;          // the value belongs to a whole cell (constant over it), not interpolated between nodes
	std::uint32_t node = 0;     // dataset node index of the vertex nearest the point (the cell index when `cell`)
	std::int64_t nodeId = 0;    // its id as written in the file (the cell's id when `cell`)
	float normalized = 0.0f;    // (value - lo) / (hi - lo) clamped to 0..1, for choosing the readout colour
};
ProbeSample sampleSurfaceScalar(const ResultDataset& dataset, const ResultBoundarySurface& surface, const DisplayScalar& scalar,
                                std::size_t triangle, float u, float v, float w, float lo, float hi);

// ---- Deformation -------------------------------------------------------------------------------------------------
// Displacements are added to the node coordinates in the FILE's own numbers (coordinates and displacements share a
// length unit), independent of the field's display unit.

// The node field holding the displacement vector: a 3-component node field named like "displacement"/"DISP"
// (not a derived one). -1 when there is none.
int findDisplacementField(const ResultDataset& dataset);

// surface.positions + scale * displacement(step) for every boundary vertex; a node without a value (NaN) does not
// move. False when the field/step has no data.
bool buildDeformedPositions(const ResultDataset& dataset, const ResultBoundarySurface& surface, int fieldIndex, int step,
                            double scale, std::vector<float>& out);

// The largest displacement magnitude over every step (file units); 0 when there is none.
double maxDisplacementMagnitude(const ResultDataset& dataset, int fieldIndex);

// A scale factor that makes the largest displacement about a tenth of the model's diagonal: 1 when the
// displacement is already that large, otherwise rounded down to 1, 2 or 5 times a power of ten.
double autoDeformScale(const ResultDataset& dataset, const ResultBoundarySurface& surface, int fieldIndex);

// A modal result (every step is a frequency, e.g. "Mode 3 - 73971 Hz"). Its mode shapes have arbitrary amplitude
// (mass-normalised eigenvectors, not physical displacements) and each mode has a different one, so they are
// displayed NORMALISED: every mode's largest displacement is drawn as a tenth of the model's diagonal, times the
// user's scale factor (1 = that tenth). autoDeformScale() is therefore 1 for a modal result.
bool isModalResult(const ResultDataset& dataset);
// Factor turning the displacements of `step` into that normalised amplitude (1 when the step has none).
double modalDisplayFactor(const ResultDataset& dataset, const ResultBoundarySurface& surface, int fieldIndex, int step);

// ---- View state and sessions (what the Simulation dock tab edits) -----------------------------------------------

// How a result is currently shown. Owned by a SimulationSession, edited by the Simulation dock panel and applied
// to the result mesh by ModelViewer::applySimulationViewState().
struct SimulationViewState
{
	int fieldIndex = -1;      // index into ResultDataset::fields (a node field); -1 = nothing to colour by
	int component = -1;       // -1 = magnitude of a 3-component field / the value of a scalar field
	bool customRange = false; // false = the data range of the shown values
	double rangeMin = 0.0;    // used when customRange
	double rangeMax = 1.0;
	int colormap = 0;         // AnalysisColormap: 0 sequential, 1 diverging (2, threshold, is not offered here)
	int bands = 0;            // 0 = smooth; >= 2 = that many contour bands
	int step = 0;             // the time step shown (owned by the timeline, not by the panel)
	// Automatic range only: true = the range over ALL steps (a fixed colour scale, so animation frames stay
	// comparable - the default), false = the range of the step shown. Ignored for a single-step result.
	bool allStepsRange = true;
	// Deformed shape: the displacement field (see findDisplacementField) times `deformScale` added to the geometry.
	bool deform = false;
	double deformScale = 1.0;
	bool markExtrema = false; // label the smallest and largest value on the visible surface
	// Vector arrows: one arrow per sampled point of the surface along a 3-component field, coloured by its magnitude.
	bool glyphs = false;
	int glyphField = -1;              // index into ResultDataset::fields; -1 = chosen automatically (chooseDefaultGlyphField)
	double glyphScale = 1.0;          // 1 = the largest arrow is 5 % of the model diagonal
	int glyphCount = 800;             // about this many arrows
	bool glyphScaleByMagnitude = true; // false = all arrows the same length
};

// Cache of the all-steps data range of one (field, component, units) so playback does not rescan every step on
// every frame.
struct SimulationRangeCache
{
	bool valid = false;
	int fieldIndex = -1;
	int component = -1;
	QString kindId, fileUnit, displayUnit;
	float lo = 0.0f, hi = 1.0f;
};

// One entry of the Simulation panel's result list (a document can hold several results).
struct SimulationResultItem
{
	QUuid meshUuid;
	QString name;
	bool visible = true;
};

// A min/max label pinned to a vertex of a result's mesh (GUI-free form; the viewport draws it).
struct SimulationMarker
{
	int vertex = -1;
	float normal[3] = { 0.0f, 0.0f, 0.0f }; // vertex normal in the mesh's frame, to hide the marker on the far side
	QString text;
	bool lightText = true;                  // white text (else black), chosen against the colour under it
};

// One loaded result inside a document: the dataset (source of truth), its boundary surface, the scene mesh that
// displays it, and the current view state.
struct SimulationSession
{
	QUuid meshUuid;
	std::shared_ptr<ResultDataset> dataset;
	std::shared_ptr<ResultBoundarySurface> surface;
	QString filePath;
	QStringList warnings;
	SimulationViewState state;
	SimulationRangeCache rangeCache;
	// The colour range this result has on its own (before a shared compare range widens it).
	float ownLo = 0.0f, ownHi = 1.0f;
	bool ownRangeValid = false;
	std::vector<SimulationMarker> markers; // the min/max labels of the current display (empty when they are off)
	// What is currently painted (the probe reads it, so a hover does not rebuild the scalar).
	DisplayScalar shownScalar;
	float shownLo = 0.0f, shownHi = 1.0f;
	int displacementField = -1;   // findDisplacementField(), -1 = the result cannot be deformed
	bool modal = false;           // isModalResult(): mode shapes are shown normalised, see modalDisplayFactor()
	double autoDeformScale = 1.0; // autoDeformScale() for it
	// Vector arrows (see SimulationGlyphs.h). The sampled sites depend only on the field's association and the arrow count,
	// so they are kept between refreshes (a playback frame only rebuilds the vectors).
	std::vector<std::uint32_t> glyphSites;
	int glyphSitesField = -1;
	int glyphSitesCount = 0;
	bool glyphSitesCell = false;
	double surfaceDiagonal = -1.0; // surfaceDiagonal(), computed on first use
	bool extentsValid = false;     // surfaceExtents(), computed once (the panel shows it)
	double extents[3] = { 0.0, 0.0, 0.0 };
	SimulationRangeCache glyphRangeCache;
	QString glyphInfo;             // what the panel shows about the arrows' colours (empty when they are off)
	// What the mesh geometry currently shows, so a recolour does not re-upload the vertices.
	bool deformApplied = false;
	int deformAppliedStep = 0;
	double deformAppliedScale = 1.0;
};

// Levels the shader quantizes into: the chosen band count, or a fine 256 for "smooth".
constexpr int kSimulationSmoothBands = 256;
int simulationShaderBands(const SimulationViewState& state);

// The value range the colormap spans: the state's custom range, or the data range of `scalar`. A degenerate
// range (min >= max, e.g. a constant field) is widened slightly so the normalisation stays defined. Returns false
// only if a custom range is not finite.
bool resolveViewRange(const DisplayScalar& scalar, const SimulationViewState& state, float& lo, float& hi);

// The view state a freshly opened result starts with (default field, automatic range, sequential colormap,
// smooth). `outScalar`, when given, receives the corresponding DisplayScalar. fieldIndex stays -1 when the
// dataset has no node field to colour by.
SimulationViewState defaultViewState(const ResultDataset& dataset, DisplayScalar* outScalar = nullptr);
