#pragma once

// GUI-free helpers for the hover "section curves" probe: the curves where the three axis-aligned planes through a point
// cut a triangle mesh. Kept free of Qt GUI / GL so they can be unit tested.

#include <cstddef>
#include <vector>

// Appends, to `out`, the line segments (x,y,z pairs, six floats per segment) where the plane `coordinate[axis] == value`
// cuts the triangles of (positions, indices) - ALL of them, every branch and loop of the cut. positions holds x,y,z per
// vertex. A vertex exactly on the plane counts as being on its positive side, so a plane through grid vertices still yields
// a closed, consistent curve. Returns the number of segments appended.
std::size_t plot3DSectionSegments(const std::vector<float>& positions, const std::vector<unsigned int>& indices,
	int axis, float value, std::vector<float>& out);

// For each triangle, the triangle sharing each of its three edges (edge e runs from corner e to corner e + 1), or -1 for a
// boundary edge. Three entries per triangle. An edge shared by more than two triangles is paired between the first two.
std::vector<int> plot3DTriangleNeighbours(const std::vector<unsigned int>& indices);

// Like plot3DSectionSegments(), but only the ONE connected curve of the cut that passes through `startTriangle` (the
// triangle under the cursor): starting there it follows the cut across the edges the plane crosses, so the other branches
// and loops of the same cut (a saddle's second hyperbola branch, a torus's other ring) are left out. `neighbours` comes
// from plot3DTriangleNeighbours(). Returns the number of segments appended (0 when startTriangle is not cut).
std::size_t plot3DSectionCurveThrough(const std::vector<float>& positions, const std::vector<unsigned int>& indices,
	const std::vector<int>& neighbours, int startTriangle, int axis, float value, std::vector<float>& out);
