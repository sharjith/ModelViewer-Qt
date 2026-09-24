#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// ---------------------------------------------------------------------------
// SubTriangleGrid
//
// A triangle split into an n x n grid of congruent sub-triangles (n*n of them: n(n+1)/2 pointing the same way as
// the parent, n(n-1)/2 pointing the opposite way), numbered in one fixed order. An analysis that samples a
// triangle at each sub-triangle's centroid (WallThicknessAnalyzer), the renderer that draws the sampled field
// (flat per cell or interpolated through reconciled corner values), and the hover readout that asks "which sample
// is under the cursor" all share this one numbering, so they cannot drift apart.
//
// Positions are barycentric relative to the triangle's own vertex order (v0, v1, v2) as stored in the mesh's index
// buffer: P = v0 + u * (v1 - v0) + v * (v2 - v0). Barycentric coordinates survive any affine transform, so the same
// (u, v) is valid in world space and in mesh-local space.
//
// Numbering: rows r = 0..n-1 from the v0-v1 edge towards v2; within row r, for c = 0..n-r-1: the "up" sub-triangle
// with corners (c,r), (c+1,r), (c,r+1) (in units of 1/n), then - if c < n-r-1 - the "down" one with corners
// (c+1,r), (c+1,r+1), (c,r+1).
// ---------------------------------------------------------------------------
namespace SubTriangleGrid
{
	// Calls fn(index, u0, v0, u1, v1, u2, v2, centroidU, centroidV) for every sub-triangle in numbering order.
	template <typename Fn>
	void forEach(int n, Fn&& fn)
	{
		const double s = 1.0 / n;
		int index = 0;
		for (int r = 0; r < n; ++r)
		{
			for (int c = 0; c < n - r; ++c)
			{
				fn(index++, c * s, r * s, (c + 1) * s, r * s, c * s, (r + 1) * s,
				   (c + 1.0 / 3.0) * s, (r + 1.0 / 3.0) * s);
				if (c < n - r - 1)
				{
					fn(index++, (c + 1) * s, r * s, (c + 1) * s, (r + 1) * s, c * s, (r + 1) * s,
					   (c + 2.0 / 3.0) * s, (r + 2.0 / 3.0) * s);
				}
			}
		}
	}

	// The sub-triangle containing the point (u, v) of an n-grid triangle (u, v >= 0, u + v <= 1; slightly outside
	// values are clamped). Always a valid index in [0, n*n).
	inline int indexAt(int n, double u, double v)
	{
		if (n <= 1)
			return 0;
		const double x = std::clamp(u, 0.0, 1.0) * n;
		const double y = std::clamp(v, 0.0, 1.0) * n;
		int r = std::clamp(static_cast<int>(std::floor(y)), 0, n - 1);
		int c = std::clamp(static_cast<int>(std::floor(x)), 0, n - 1 - r);
		const double fx = x - c, fy = y - r;
		const bool down = (fx + fy > 1.0) && (c < n - r - 1);
		// Index of the first sub-triangle of row r: sum over earlier rows of (2*(n-k) - 1) = r * (2n - r).
		return r * (2 * n - r) + 2 * c + (down ? 1 : 0);
	}

	// The inverse of forEach()'s numbering: the three barycentric corners of sub-triangle `index` directly, with no
	// need to iterate every earlier sub-triangle to find it (a caller that already knows an index - e.g. a hover
	// readout locating the sample under the cursor via indexAt() first - previously had to re-scan the whole grid
	// with forEach() just to recover its corners). `index` must be in [0, n*n); behavior is undefined otherwise,
	// same as indexAt()'s own contract on (u, v).
	inline void cornersAt(int n, int index, double& u0, double& v0, double& u1, double& v1, double& u2, double& v2)
	{
		if (n <= 1)
		{
			u0 = 0.0; v0 = 0.0; u1 = 1.0; v1 = 0.0; u2 = 0.0; v2 = 1.0;
			return;
		}
		const double s = 1.0 / n;
		// rowStart(r) = r * (2n - r) - the same formula indexAt() derives its row index from, inverted here by a
		// linear scan over rows (n is always small - kMaxSubdivisions is 12 - so this is O(n), not the O(n^2) a
		// full-grid forEach() scan costs).
		int r = 0;
		while (r + 1 < n && (r + 1) * (2 * n - (r + 1)) <= index)
			++r;
		const int rem = index - r * (2 * n - r);
		const int c = rem / 2;
		const bool down = (rem % 2) != 0;
		if (!down)
		{
			u0 = c * s; v0 = r * s; u1 = (c + 1) * s; v1 = r * s; u2 = c * s; v2 = (r + 1) * s;
		}
		else
		{
			u0 = (c + 1) * s; v0 = r * s; u1 = (c + 1) * s; v1 = (r + 1) * s; u2 = c * s; v2 = (r + 1) * s;
		}
	}
}

// Per-triangle sub-triangle samples of a scalar analysis result, in the mesh's own triangle order. `gridN[t]` is
// the grid resolution of triangle t (0 = the triangle has no samples); its samples occupy values[offset[t] ..
// offset[t] + gridN[t]^2), in SubTriangleGrid numbering. NaN marks a sub-triangle with no value.
struct SubTriangleField
{
	std::vector<unsigned char> gridN;
	std::vector<unsigned int> offset;
	std::vector<float> values;
	// Optional continuous DISPLAY field: three scalar values for every
	// sub-triangle in `values`, in the same corner order emitted by
	// forEach(). Adjacent cells share a reconciled corner value, so the
	// renderer can interpolate without exposing the sampling triangles.
	// `values` remains the untouched measured field used for statistics;
	// empty cornerValues keeps the legacy flat-per-sample representation.
	std::vector<float> cornerValues;

	bool empty() const { return gridN.empty() || values.empty(); }
};
