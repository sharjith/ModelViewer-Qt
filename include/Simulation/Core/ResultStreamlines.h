#pragma once

// Streamlines of a simulation result: the curves a massless particle follows through a steady vector field (velocity ...) of a volume mesh. GUI-free
// (QtCore + the standard library), unit-tested; the drawing lives in SimulationStreamlineController, the colours and seeds in ModelViewer.
//
// Finding the field at a point: a uniform grid of cell bounding boxes narrows a point to a few candidate cells, and inside one the point is located in
// a tetrahedral decomposition of the cell (cell centre, the centre of each face of more than three nodes, and the face's edges). Every face is split
// the same way from both sides, so the interpolated field is continuous across cells, and the same rule serves tetrahedra, hexahedra, wedges,
// pyramids (quadratic ones through their corner nodes) and polyhedra (through their explicit faces). The field is the linear (barycentric)
// interpolation of the node values; only node fields are traced.
//
// Tracing: classical fourth-order Runge-Kutta in arc length (the direction of the field, unit speed) with a step of a fraction of the current cell, in both
// directions from a seed, until the line leaves the mesh, the field vanishes, or a length / step limit is reached.

#include "ResultDataset.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

// Finds the volume cell around a point and interpolates node values there. Built once per dataset (O(cells)) and reused for every field and time step.
class CellLocator
{
public:
	explicit CellLocator(const ResultDataset& dataset, const std::atomic<bool>* cancel = nullptr);

	// How many volume cells the locator indexes (0: nothing to trace in - a shell or surface result).
	std::size_t volumeCellCount() const { return _cells.size(); }
	// Diagonal of the bounding box of the volume cells.
	double diagonal() const { return _diagonal; }

	// Interpolates a per-node vector field (`vectors`, 3 floats per node) and, when given, a per-node scalar at `p`. `hint` is the cell the last point was found in
	// (or -1): it is tried first and updated. False when the point is in no cell or a node of its cell has no finite value.
	bool interpolate(const double p[3], const std::vector<float>& vectors, const std::vector<float>* scalar, int& hint, double vector[3], double& scalarValue) const;

	// The size (bounding-box diagonal) of a cell, 0 for a cell that is not indexed.
	double cellSize(int cell) const;

	// `count` random points, each inside a random volume cell (a random convex combination of its nodes), for seeding streamlines. Deterministic for a `seed`.
	std::vector<float> randomPoints(std::size_t count, std::uint32_t seed) const;

private:
	bool evalCell(std::size_t cell, const double p[3], const std::vector<float>& vectors, const std::vector<float>* scalar, double vector[3], double& scalarValue) const;

	const ResultDataset& _ds;
	std::vector<std::uint32_t> _cells; // the indexed (volume) cells
	std::vector<float> _boxes;         // 6 floats per indexed cell: min xyz, max xyz
	double _origin[3] = { 0, 0, 0 };
	double _binSize[3] = { 1, 1, 1 };
	int _dims[3] = { 1, 1, 1 };
	std::vector<std::uint32_t> _binStart; // CSR: bin b holds _binCells[_binStart[b] .. _binStart[b + 1]) (indices into _cells)
	std::vector<std::uint32_t> _binCells;
	double _diagonal = 0.0;
};

struct StreamlineSet
{
	std::vector<float> points;                // xyz per point (the dataset's own coordinates)
	std::vector<float> values;                // one per point: the scalar given to traceStreamlines (else the speed) interpolated there
	std::vector<std::uint32_t> lineOffsets;   // line l is points [lineOffsets[l], lineOffsets[l + 1]); lineCount() + 1 entries when not empty
	std::size_t lineCount() const { return lineOffsets.empty() ? 0 : lineOffsets.size() - 1; }
	std::size_t pointCount() const { return values.size(); }
};

struct StreamlineOptions
{
	int maxStepsPerDirection = 2000;
	double stepFactor = 0.35;        // a step is this fraction of the cell it is in
	double maxLengthFactor = 4.0;    // a line is at most this many model diagonals long (both directions together: each way gets it)
	double stagnationFraction = 1e-6; // stop where the speed falls below this fraction of the largest speed
};

// Traces a streamline in both directions from every seed (3 floats each). `vectors` holds 3 floats per node (a node field of the dataset at one step); `scalar` (one
// per node, may be null) is what the points are valued with. A seed outside the mesh, or where the field is zero, gives no line; a line of a single point is dropped.
// False when `vectors` does not have 3 floats per node, or on cancellation (`cancel`, polled per line).
bool traceStreamlines(const ResultDataset& dataset, const CellLocator& locator, const std::vector<float>& vectors, const std::vector<float>* scalar,
                      const std::vector<float>& seeds, const StreamlineOptions& options, StreamlineSet& out, const std::atomic<bool>* cancel = nullptr);

// The node field streamlines start on: velocity (a 3-component node field named like it), else the first 3-component node field with data. -1 when there is none.
int chooseDefaultStreamlineField(const ResultDataset& dataset);
// Whether a field can be traced: a 3-component node field with data at some step.
bool isStreamlineField(const ResultField& field);

// `count` random points on a triangle set (area-weighted), e.g. a section's cut face, to seed streamlines on a plane. Deterministic for a `seed`.
std::vector<float> randomPointsOnTriangles(const std::vector<float>& positions, const std::vector<std::uint32_t>& triangles, std::size_t count, std::uint32_t seed);
