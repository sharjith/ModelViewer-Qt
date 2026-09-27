#pragma once

// XY charts of a simulation result's field: a spatial profile along a line ("plot over line") and a point's history
// across every step ("plot over time") - see docs/simulation_results_design.md. GUI-free, unit-testable: the paint
// widget lives in src/Simulation/UI/.
//
// Both sample a NODE field only, through CellLocator's tetrahedral interpolation (the same one streamlines use) -
// a cell (element) field has no interpolation, so it is not supported yet; buildDisplayScalar/computeStepRange
// already report it as unsupported by returning false. A field with more than one component is reduced first
// (a chosen component, or a vector's magnitude), exactly as the coloured display does (buildDisplayScalar), so the
// charted values and unit match what is shown on the model.

#include "ResultDataset.h"
#include "ResultStreamlines.h"

#include <QString>

#include <vector>

// One sampled series: `x` is the independent axis (arc length along the line, or time), `y` the field value at it
// (NaN where the point had no data - outside the mesh, or a step without data). `xLabel`/`yLabel` are ready to show
// on a chart's axes; `yUnit` is the field's display unit (may be empty).
struct ChartSeries
{
	QString title;    // the field's name (plus "(component k)"/"(magnitude)" as buildDisplayScalar labels it)
	QString xLabel;
	QString xUnit; // "Distance": the dataset's length unit (may be empty - not every reader confirms one); "Time": empty, or "Hz" for a modal/frequency result
	QString yLabel;
	QString yUnit;
	std::vector<double> x;
	std::vector<float> y;

	bool empty() const { return x.empty(); }
};

// Samples `fieldIndex` (component `component`, -1 = magnitude of a vector / the only component of a scalar) along
// the straight segment p0->p1 at `step`, at `sampleCount` evenly spaced points (at least 2). `x` is the arc length
// from p0, in the dataset's own length unit. False when the field cannot be sampled this way at all (wrong
// association, no data at the step, or the result has no volume cells); a point outside every cell is still a
// sample, with y = NaN, so a broken line shows exactly where the line left the mesh.
bool sampleFieldOverLine(const ResultDataset& dataset, const CellLocator& locator, int fieldIndex, int component, int step,
                         const double p0[3], const double p1[3], std::size_t sampleCount, ChartSeries& out);

// A bin-count histogram of `fieldIndex` (component `component`) at `step`: `binCount` (at least 1) evenly spaced
// bins covering the field's own min..max at that step. `edges` gets binCount+1 entries (bin i is
// [edges[i], edges[i+1]), the last bin closed at both ends), `counts` gets binCount entries. False when the field
// has no data at this step, or is constant there (nothing to bin).
bool buildFieldHistogram(const ResultDataset& dataset, int fieldIndex, int component, int step, int binCount,
                         std::vector<float>& edges, std::vector<std::size_t>& counts, QString& label, QString& unit);

// Samples `fieldIndex` (component `component`) at the fixed point `point`, once per step of the dataset (a lazy
// result reads every step - see ResultDataset::ensureStepLoaded - so this can be slow; show a busy cursor around
// it). `x` is each step's time (or mode/frequency label's numeric value, i.e. ResultStep::time). False for the same
// reasons as sampleFieldOverLine; a step without data or where the point is outside every cell is y = NaN.
// `locator` is the caller's own (as with sampleFieldOverLine): pass the SAME shape-aware, cached one used for
// streamlines/plot-over-line (see SimulationSession::locator) rather than building a fresh rest-shape locator here,
// so a point picked on a deformed result is sampled at the same material point at every step, not just at the shape
// the locator happened to be built on.
bool sampleFieldOverTime(const ResultDataset& dataset, const CellLocator& locator, int fieldIndex, int component, const double point[3], ChartSeries& out);
