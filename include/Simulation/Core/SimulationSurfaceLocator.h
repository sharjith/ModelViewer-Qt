#pragma once

// The surface counterpart of CellLocator: a shell or surface result (triangles and quads, no volume cells) has no inside to find a point in, so a
// point is matched to the CLOSEST point of the surface and the field is interpolated there from the corner nodes of that triangle (a quad is split
// into two triangles; the quadratic cells are taken through their corner nodes, as everywhere else). GUI-free, unit-tested.
//
// A uniform grid of triangle bounding boxes narrows a query to a few candidates, searched ring by ring outward from the point's bin until nothing
// closer can exist or `maxDistance` is passed. The point picked on the displayed surface is on it already (distance ~0); the tolerance lets a chart
// line that leaves a curved shell by a little still read the surface under it, and gives NaN where it really leaves.

#include "ResultDataset.h"
#include "ResultStreamlines.h" // CellInterpolationStencil

#include <cstddef>
#include <cstdint>
#include <vector>

class SurfaceLocator
{
public:
	// `positions` (3 floats per node, may be empty) replace the dataset's node coordinates (the deformed shape); the locator keeps its own copy.
	explicit SurfaceLocator(const ResultDataset& dataset, std::vector<float> positions = std::vector<float>());

	std::size_t triangleCount() const { return _triangles.size() / 3; }
	// Diagonal of the bounding box of the indexed surface (0 when there is none).
	double diagonal() const { return _diagonal; }

	// The node weights at the point of the surface closest to `p`. False when there is no surface, or (maxDistance > 0) when that point is farther
	// than maxDistance. `distance`, when given, receives the distance found.
	bool nearestStencil(const double p[3], double maxDistance, CellInterpolationStencil& out, double* distance = nullptr) const;

private:
	const std::vector<float>& coordinates() const { return _positions.empty() ? _ds.nodePositions : _positions; }
	void closestOnTriangle(std::size_t triangle, const double p[3], double& distanceSquared, double weights[3]) const;

	const ResultDataset& _ds;
	std::vector<float> _positions;
	std::vector<std::uint32_t> _triangles; // 3 node indices per triangle
	double _origin[3] = { 0, 0, 0 };
	double _binSize = 1.0;
	int _dims[3] = { 1, 1, 1 };
	std::vector<std::uint32_t> _binStart; // CSR: bin b holds _binTriangles[_binStart[b] .. _binStart[b + 1])
	std::vector<std::uint32_t> _binTriangles;
	double _diagonal = 0.0;
};
