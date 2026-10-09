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
#include "SimulationSurfaceLocator.h"

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

// Reads an extra curve for a chart from CSV text: two or more columns, the first is x, the second y (a third and later are ignored). The
// delimiter (comma, semicolon or tab) is detected; a first row whose cells are not numbers is a header and names the axes and the curve
// (otherwise `fallbackTitle` is used). Rows that are not numeric are skipped; the points are sorted by x. False, with a message, when fewer
// than two usable rows remain. Meant for test data or a frequency-response curve drawn over a result's own chart.
bool parseChartCurveCsv(const QString& text, const QString& fallbackTitle, ChartSeries& out, QString* error = nullptr);

// The data of a chart as CSV text (comma separated, '.' decimals, a header row, one row per sample, a sample with no value left blank). One pair of
// columns (x, y) per curve, side by side; a shorter curve leaves its cells blank. The first pair reads back with parseChartCurveCsv().
QString chartToCsv(const ChartSeries& main, const std::vector<ChartSeries>& extras);

// A curve belongs on a SECOND (right-hand) y axis when its unit is known and differs from the main curve's (a temperature drawn with a stress); a curve
// whose unit is unknown (a CSV) shares the main axis.
bool chartNeedsSecondaryAxis(const ChartSeries& main, const ChartSeries& curve);

// Zooms the range [lo, hi] by `factor` (< 1 zooms in) keeping the point at `anchor` (0 = lo, 1 = hi) where it is.
void chartZoomRange(double lo, double hi, double anchor, double factor, double& newLo, double& newHi);

// The same two charts for a SHELL / SURFACE result (triangles and quads, no volume cells): a point is read on the closest point of the surface
// (SurfaceLocator) instead of inside a cell. The over-line chart samples the straight chord between the two picked points and projects each sample onto
// the surface; a sample farther than `maxDistanceFraction` of the surface's diagonal from it (the chord left a curved shell) is NaN, so a gap shows where
// the line really left the surface. Node fields only, like the volume versions.
bool sampleFieldOverLine(const ResultDataset& dataset, const SurfaceLocator& locator, int fieldIndex, int component, int step,
                         const double p0[3], const double p1[3], std::size_t sampleCount, ChartSeries& out, double maxDistanceFraction = 0.02);
bool sampleFieldOverTime(const ResultDataset& dataset, const SurfaceLocator& locator, int fieldIndex, int component, const double point[3], ChartSeries& out,
                         double maxDistanceFraction = 0.02);

// Samples `fieldIndex` (component `component`) at the fixed point `point`, once per step of the dataset (a lazy
// result reads every step - see ResultDataset::ensureStepLoaded - so this can be slow; show a busy cursor around
// it). `x` is each step's time (or mode/frequency label's numeric value, i.e. ResultStep::time). False for the same
// reasons as sampleFieldOverLine; a step without data or where the point is outside every cell is y = NaN.
// `locator` is the caller's own (as with sampleFieldOverLine): pass the SAME shape-aware, cached one used for
// streamlines/plot-over-line (see SimulationSession::locator) rather than building a fresh rest-shape locator here,
// so a point picked on a deformed result is sampled at the same material point at every step, not just at the shape
// the locator happened to be built on.
bool sampleFieldOverTime(const ResultDataset& dataset, const CellLocator& locator, int fieldIndex, int component, const double point[3], ChartSeries& out);
