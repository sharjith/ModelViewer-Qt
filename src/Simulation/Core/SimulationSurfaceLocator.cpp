#include "SimulationSurfaceLocator.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
	double dot3(const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
}

SurfaceLocator::SurfaceLocator(const ResultDataset& dataset, std::vector<float> positions)
	: _ds(dataset)
	, _positions(positions.size() == dataset.nodePositions.size() ? std::move(positions) : std::vector<float>())
{
	const std::vector<float>& P = coordinates();
	const std::size_t nodeCount = _ds.nodeCount();
	auto finite = [&](std::uint32_t node) {
		for (int k = 0; k < 3; ++k)
			if (!std::isfinite(P[static_cast<std::size_t>(node) * 3 + k]))
				return false;
		return true;
	};
	for (std::size_t c = 0; c < _ds.cellCount(); ++c)
	{
		const ResultCellType type = _ds.cellTypes[c];
		if (!resultCellIsSurface(type))
			continue;
		const std::uint32_t begin = _ds.cellOffsets[c];
		const std::uint32_t count = _ds.cellOffsets[c + 1] - begin;
		const bool quad = type == ResultCellType::Quad || type == ResultCellType::Quad8;
		if (count < (quad ? 4u : 3u))
			continue;
		const std::uint32_t* ids = &_ds.cellConnectivity[begin];
		if (ids[0] >= nodeCount || ids[1] >= nodeCount || ids[2] >= nodeCount || (quad && ids[3] >= nodeCount))
			continue;
		if (!finite(ids[0]) || !finite(ids[1]) || !finite(ids[2]) || (quad && !finite(ids[3])))
			continue;
		_triangles.insert(_triangles.end(), { ids[0], ids[1], ids[2] });
		if (quad)
			_triangles.insert(_triangles.end(), { ids[0], ids[2], ids[3] });
	}
	if (_triangles.empty())
		return;

	// Bounds, and the average triangle extent that sizes the grid.
	double lo[3] = { std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max() };
	double hi[3] = { std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest() };
	double extentSum = 0.0;
	std::vector<float> boxes(triangleCount() * 6);
	for (std::size_t t = 0; t < triangleCount(); ++t)
	{
		float box[6] = { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
			std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
		for (int v = 0; v < 3; ++v)
			for (int k = 0; k < 3; ++k)
			{
				const float x = P[static_cast<std::size_t>(_triangles[t * 3 + static_cast<std::size_t>(v)]) * 3 + static_cast<std::size_t>(k)];
				box[k] = std::min(box[k], x);
				box[3 + k] = std::max(box[3 + k], x);
			}
		for (int k = 0; k < 3; ++k)
		{
			boxes[t * 6 + static_cast<std::size_t>(k)] = box[k];
			boxes[t * 6 + 3 + static_cast<std::size_t>(k)] = box[3 + k];
			lo[k] = std::min(lo[k], static_cast<double>(box[k]));
			hi[k] = std::max(hi[k], static_cast<double>(box[3 + k]));
		}
		extentSum += std::max({ static_cast<double>(box[3] - box[0]), static_cast<double>(box[4] - box[1]), static_cast<double>(box[5] - box[2]) });
	}
	double extent[3], maxExtent = 0.0;
	for (int k = 0; k < 3; ++k)
	{
		extent[k] = hi[k] - lo[k];
		maxExtent = std::max(maxExtent, extent[k]);
	}
	if (!(maxExtent > 0.0))
		maxExtent = 1.0;
	_diagonal = std::sqrt(extent[0] * extent[0] + extent[1] * extent[1] + extent[2] * extent[2]);
	if (!(_diagonal > 0.0))
		_diagonal = maxExtent;

	// A bin a couple of triangles wide, but never so small that the grid gets huge.
	const double average = extentSum / static_cast<double>(triangleCount());
	_binSize = std::max({ 2.0 * average, maxExtent / 128.0, maxExtent * 1.0e-6 });
	for (int k = 0; k < 3; ++k)
	{
		_origin[k] = lo[k];
		_dims[k] = std::clamp(static_cast<int>(std::floor(std::max(extent[k], 0.0) / _binSize)) + 1, 1, 256);
	}

	auto bin = [&](double value, int k) { return std::clamp(static_cast<int>(std::floor((value - _origin[k]) / _binSize)), 0, _dims[k] - 1); };
	const std::size_t binCount = static_cast<std::size_t>(_dims[0]) * static_cast<std::size_t>(_dims[1]) * static_cast<std::size_t>(_dims[2]);
	_binStart.assign(binCount + 1, 0);
	for (int pass = 0; pass < 2; ++pass)
	{
		std::vector<std::uint32_t> cursor;
		if (pass == 1)
		{
			for (std::size_t b = 0; b < binCount; ++b)
				_binStart[b + 1] += _binStart[b];
			_binTriangles.assign(_binStart[binCount], 0);
			cursor.assign(_binStart.begin(), _binStart.end() - 1);
		}
		for (std::size_t t = 0; t < triangleCount(); ++t)
		{
			int a[3], b[3];
			for (int k = 0; k < 3; ++k)
			{
				a[k] = bin(boxes[t * 6 + static_cast<std::size_t>(k)], k);
				b[k] = bin(boxes[t * 6 + 3 + static_cast<std::size_t>(k)], k);
			}
			for (int z = a[2]; z <= b[2]; ++z)
				for (int y = a[1]; y <= b[1]; ++y)
					for (int x = a[0]; x <= b[0]; ++x)
					{
						const std::size_t index = (static_cast<std::size_t>(z) * static_cast<std::size_t>(_dims[1]) + static_cast<std::size_t>(y)) * static_cast<std::size_t>(_dims[0]) + static_cast<std::size_t>(x);
						if (pass == 0)
							++_binStart[index + 1];
						else
							_binTriangles[cursor[index]++] = static_cast<std::uint32_t>(t);
					}
		}
	}
}

// The closest point of triangle `triangle` to p (Ericson, Real-Time Collision Detection): its squared distance and barycentric weights of the corners.
void SurfaceLocator::closestOnTriangle(std::size_t triangle, const double p[3], double& distanceSquared, double weights[3]) const
{
	const std::vector<float>& P = coordinates();
	double a[3], b[3], c[3];
	for (int k = 0; k < 3; ++k)
	{
		a[k] = P[static_cast<std::size_t>(_triangles[triangle * 3 + 0]) * 3 + static_cast<std::size_t>(k)];
		b[k] = P[static_cast<std::size_t>(_triangles[triangle * 3 + 1]) * 3 + static_cast<std::size_t>(k)];
		c[k] = P[static_cast<std::size_t>(_triangles[triangle * 3 + 2]) * 3 + static_cast<std::size_t>(k)];
	}
	const double ab[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
	const double ac[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
	const double ap[3] = { p[0] - a[0], p[1] - a[1], p[2] - a[2] };
	double u, v, w; // weights of a, b, c
	const double d1 = dot3(ab, ap), d2 = dot3(ac, ap);
	const double bp[3] = { p[0] - b[0], p[1] - b[1], p[2] - b[2] };
	const double d3 = dot3(ab, bp), d4 = dot3(ac, bp);
	const double cp[3] = { p[0] - c[0], p[1] - c[1], p[2] - c[2] };
	const double d5 = dot3(ab, cp), d6 = dot3(ac, cp);
	const double vc = d1 * d4 - d3 * d2, vb = d5 * d2 - d1 * d6, va = d3 * d6 - d5 * d4;
	if (d1 <= 0.0 && d2 <= 0.0)
	{
		u = 1.0; v = 0.0; w = 0.0;
	}
	else if (d3 >= 0.0 && d4 <= d3)
	{
		u = 0.0; v = 1.0; w = 0.0;
	}
	else if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0)
	{
		const double t = d1 / (d1 - d3);
		u = 1.0 - t; v = t; w = 0.0;
	}
	else if (d6 >= 0.0 && d5 <= d6)
	{
		u = 0.0; v = 0.0; w = 1.0;
	}
	else if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0)
	{
		const double t = d2 / (d2 - d6);
		u = 1.0 - t; v = 0.0; w = t;
	}
	else if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0)
	{
		const double t = (d4 - d3) / ((d4 - d3) + (d5 - d6));
		u = 0.0; v = 1.0 - t; w = t;
	}
	else
	{
		const double sum = va + vb + vc;
		if (std::abs(sum) < 1.0e-300)
		{
			u = 1.0; v = 0.0; w = 0.0; // a degenerate (zero-area) triangle: its first corner
		}
		else
		{
			const double inverse = 1.0 / sum;
			v = vb * inverse;
			w = vc * inverse;
			u = 1.0 - v - w;
		}
	}
	const double q[3] = { a[0] * u + b[0] * v + c[0] * w, a[1] * u + b[1] * v + c[1] * w, a[2] * u + b[2] * v + c[2] * w };
	const double d[3] = { p[0] - q[0], p[1] - q[1], p[2] - q[2] };
	distanceSquared = dot3(d, d);
	weights[0] = u;
	weights[1] = v;
	weights[2] = w;
}

bool SurfaceLocator::nearestStencil(const double p[3], double maxDistance, CellInterpolationStencil& out, double* distance) const
{
	out = CellInterpolationStencil();
	if (_triangles.empty() || !std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]))
		return false;

	// How far the point is outside the grid (the grid starts at the surface's own bounds): the bins searched are around the nearest bin, and every
	// triangle is at least that far away when the point is outside.
	double outside = 0.0;
	int base[3];
	for (int k = 0; k < 3; ++k)
	{
		const double upper = _origin[k] + _binSize * _dims[k];
		const double over = p[k] < _origin[k] ? _origin[k] - p[k] : (p[k] > upper ? p[k] - upper : 0.0);
		outside = std::max(outside, over);
		base[k] = std::clamp(static_cast<int>(std::floor((p[k] - _origin[k]) / _binSize)), 0, _dims[k] - 1);
	}
	if (maxDistance > 0.0 && outside > maxDistance)
		return false;

	double bestSquared = std::numeric_limits<double>::max();
	double bestWeights[3] = { 0, 0, 0 };
	std::size_t bestTriangle = 0;
	bool found = false;
	const int maxRing = std::max({ _dims[0], _dims[1], _dims[2] });
	for (int ring = 0; ring <= maxRing; ++ring)
	{
		for (int z = base[2] - ring; z <= base[2] + ring; ++z)
			for (int y = base[1] - ring; y <= base[1] + ring; ++y)
				for (int x = base[0] - ring; x <= base[0] + ring; ++x)
				{
					if (x < 0 || y < 0 || z < 0 || x >= _dims[0] || y >= _dims[1] || z >= _dims[2])
						continue;
					if (std::max({ std::abs(x - base[0]), std::abs(y - base[1]), std::abs(z - base[2]) }) != ring)
						continue; // only this ring's bins: the inner ones were searched already
					const std::size_t index = (static_cast<std::size_t>(z) * static_cast<std::size_t>(_dims[1]) + static_cast<std::size_t>(y)) * static_cast<std::size_t>(_dims[0]) + static_cast<std::size_t>(x);
					for (std::uint32_t i = _binStart[index]; i < _binStart[index + 1]; ++i)
					{
						double squared, weights[3];
						closestOnTriangle(_binTriangles[i], p, squared, weights);
						if (squared < bestSquared)
						{
							bestSquared = squared;
							bestTriangle = _binTriangles[i];
							bestWeights[0] = weights[0]; bestWeights[1] = weights[1]; bestWeights[2] = weights[2];
							found = true;
						}
					}
				}
		// Anything in a bin further out is at least ring * binSize away from the point (less the distance the point is outside the grid).
		const double unreached = std::max(0.0, static_cast<double>(ring) * _binSize - outside);
		if (found && std::sqrt(bestSquared) <= unreached)
			break;
		if (maxDistance > 0.0 && unreached > maxDistance)
			break;
	}
	if (!found)
		return false;
	const double best = std::sqrt(bestSquared);
	if (maxDistance > 0.0 && best > maxDistance)
		return false;
	if (distance)
		*distance = best;
	for (int v = 0; v < 3; ++v)
	{
		out.nodes.push_back(_triangles[bestTriangle * 3 + static_cast<std::size_t>(v)]);
		out.weights.push_back(bestWeights[v]);
	}
	return true;
}
