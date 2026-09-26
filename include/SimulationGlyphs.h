#pragma once

// Vector glyphs (arrows) of a simulation result: one arrow per sampled site of the boundary surface, pointing along a
// vector field (velocity, displacement ...) and coloured by its magnitude. GUI-free - QtCore + the standard library only,
// so it is unit-testable; the GL drawing lives in SimulationGlyphController, the colours in ModelViewer.

#include "ResultBoundary.h"
#include "ResultDataset.h"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <vector>

// The arrows of one result, ready to draw. Positions are given as vertices of the result's mesh (so the arrows follow its
// current, possibly deformed and transformed, shape); vectors are in the mesh's own frame.
struct GlyphSet
{
	std::vector<std::uint32_t> anchors; // 3 mesh-vertex indices per arrow: the base is their mean (a node arrow repeats one vertex)
	std::vector<float> vectors;         // 3 floats per arrow: base -> tip, mesh frame, length included
	std::vector<float> values;          // magnitude per arrow, in the field's display unit
	std::vector<float> colors;          // 3 floats per arrow (r, g, b), filled by the owner from `values`
	float fieldMin = 0.0f, fieldMax = 0.0f; // range of the field's magnitude over the whole result at this step (display unit)
	QString unit;                           // that display unit (empty = not specified)

	std::size_t count() const { return values.size(); }
	void clear()
	{
		anchors.clear();
		vectors.clear();
		values.clear();
		colors.clear();
		fieldMin = fieldMax = 0.0f;
		unit.clear();
	}
};

struct GlyphOptions
{
	std::size_t target = 800;      // about this many arrows
	double scale = 1.0;            // 1 = an arrow of the largest magnitude is 5 % of the model diagonal long
	bool scaleByMagnitude = true;  // false: every arrow has the full length
};

// Which of `points` (3 floats per site) get an arrow: about `target` of them, evenly spread in space (one per cell of a
// grid, the point nearest the cell centre), ascending. All of them when there are no more than `target`. Non-finite
// points are never picked.
std::vector<std::uint32_t> selectGlyphSites(const std::vector<float>& points, std::size_t target);

// The sampled sites of a result's boundary surface: vertex indices for a node field (`cellField` false), triangle indices
// for a cell (element-wise) field - the arrow of a cell sits at the centre of its boundary triangle.
std::vector<std::uint32_t> selectSurfaceGlyphSites(const ResultBoundarySurface& surface, bool cellField, std::size_t target);

// Diagonal of the surface's bounding box (0 when it has no points).
double surfaceDiagonal(const ResultBoundarySurface& surface);

// Whether a field can be drawn as arrows: 3 components, with data at some step.
bool isGlyphField(const ResultField& field);

// The first field worth drawing as arrows: velocity, then displacement, then any other 3-component field (node
// fields before cell fields). -1 when there is none.
int chooseDefaultGlyphField(const ResultDataset& dataset);

// Arrows of field `fieldIndex` at `step` at the given sites (from selectSurfaceGlyphSites for the field's association).
// Arrow length is scale * 5 % of `diagonal`, times magnitude / referenceMax when scaling by magnitude (referenceMax, in
// the display unit, is normally the largest magnitude over all steps so animation frames stay comparable; <= 0 = the
// largest magnitude of this step). A site with no value, a non-finite or a zero vector gets no arrow. `colors` is left
// empty. False when the field/step has no data or no arrow results.
bool buildGlyphSet(const ResultDataset& dataset, const ResultBoundarySurface& surface, int fieldIndex, int step,
                   const std::vector<std::uint32_t>& sites, double diagonal, const GlyphOptions& options,
                   float referenceMax, GlyphSet& out);
