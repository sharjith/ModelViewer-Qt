#include "SimulationCellAveraging.h"

#include "ResultStreamlines.h" // resultCellFaceRings

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
	void sub3(const float* a, const float* b, double out[3])
	{
		for (int k = 0; k < 3; ++k)
			out[k] = static_cast<double>(a[k]) - static_cast<double>(b[k]);
	}

	void cross3(const double a[3], const double b[3], double out[3])
	{
		out[0] = a[1] * b[2] - a[2] * b[1];
		out[1] = a[2] * b[0] - a[0] * b[2];
		out[2] = a[0] * b[1] - a[1] * b[0];
	}

	double dot3(const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

	// The corner nodes of a cell (the first N of a quadratic one; the distinct nodes of the faces of a polyhedron).
	void cornerNodes(const ResultDataset& ds, std::size_t c, std::vector<std::uint32_t>& out)
	{
		out.clear();
		const ResultCellType type = ds.cellTypes[c];
		if (type == ResultCellType::Polyhedron)
		{
			std::vector<std::size_t> ringStart;
			resultCellFaceRings(ds, c, out, ringStart);
			std::sort(out.begin(), out.end());
			out.erase(std::unique(out.begin(), out.end()), out.end());
			return;
		}
		if (type == ResultCellType::Unsupported)
			return;
		const int count = std::min(resultCellNodeCount(resultCellCornerType(type)), static_cast<int>(ds.cellOffsets[c + 1] - ds.cellOffsets[c]));
		for (int i = 0; i < count; ++i)
			out.push_back(ds.cellConnectivity[ds.cellOffsets[c] + static_cast<std::size_t>(i)]);
	}
}

double CellToNodeAverager::cellMeasure(const ResultDataset& ds, std::size_t c)
{
	if (c >= ds.cellTypes.size())
		return 0.0;
	const ResultCellType type = ds.cellTypes[c];
	const float* P = ds.nodePositions.data();
	auto at = [&](std::uint32_t node) { return P + static_cast<std::size_t>(node) * 3; };
	auto valid = [&](std::uint32_t node) { return static_cast<std::size_t>(node) * 3 + 2 < ds.nodePositions.size(); };

	if (type == ResultCellType::Line)
	{
		const std::uint32_t* n = ds.cellConnectivity.data() + ds.cellOffsets[c];
		if (ds.cellOffsets[c + 1] - ds.cellOffsets[c] < 2 || !valid(n[0]) || !valid(n[1]))
			return 0.0;
		double d[3];
		sub3(at(n[1]), at(n[0]), d);
		return std::sqrt(dot3(d, d));
	}
	if (resultCellIsSurface(type))
	{
		std::vector<std::uint32_t> corners;
		cornerNodes(ds, c, corners);
		if (corners.size() < 3)
			return 0.0;
		for (std::uint32_t node : corners)
			if (!valid(node))
				return 0.0;
		double sum[3] = { 0, 0, 0 };
		for (std::size_t i = 1; i + 1 < corners.size(); ++i) // a fan from the first corner
		{
			double a[3], b[3], x[3];
			sub3(at(corners[i]), at(corners[0]), a);
			sub3(at(corners[i + 1]), at(corners[0]), b);
			cross3(a, b, x);
			for (int k = 0; k < 3; ++k)
				sum[k] += x[k];
		}
		return 0.5 * std::sqrt(dot3(sum, sum));
	}
	if (resultCellIsVolume(type) || type == ResultCellType::Polyhedron)
	{
		std::vector<std::uint32_t> ringNodes;
		std::vector<std::size_t> ringStart;
		resultCellFaceRings(ds, c, ringNodes, ringStart);
		if (ringNodes.empty())
			return 0.0;
		double centre[3] = { 0, 0, 0 };
		for (std::uint32_t node : ringNodes)
		{
			if (!valid(node))
				return 0.0;
			for (int k = 0; k < 3; ++k)
				centre[k] += at(node)[k];
		}
		for (double& v : centre)
			v /= static_cast<double>(ringNodes.size());
		// Pyramids from an interior point over every (fanned) face: exact for a convex cell, and the unsigned parts add up to the volume.
		const float centreF[3] = { static_cast<float>(centre[0]), static_cast<float>(centre[1]), static_cast<float>(centre[2]) };
		double volume = 0.0;
		for (std::size_t r = 0; r + 1 < ringStart.size(); ++r)
		{
			const std::size_t begin = ringStart[r], end = ringStart[r + 1];
			for (std::size_t i = begin + 1; i + 1 < end; ++i)
			{
				double a[3], b[3], d[3], x[3];
				sub3(at(ringNodes[begin]), centreF, a);
				sub3(at(ringNodes[i]), centreF, b);
				sub3(at(ringNodes[i + 1]), centreF, d);
				cross3(b, d, x);
				volume += std::fabs(dot3(a, x)) / 6.0;
			}
		}
		return volume;
	}
	return 0.0;
}

CellToNodeAverager::CellToNodeAverager(const ResultDataset& ds)
	: _nodeCount(ds.nodeCount())
{
	const std::size_t cells = ds.cellCount();
	if (_nodeCount == 0 || cells == 0)
		return;
	// The measure of every cell, then the corners into CSR form (count, prefix sum, fill).
	std::vector<double> measure(cells, 0.0);
	double total = 0.0;
	std::size_t measured = 0;
	for (std::size_t c = 0; c < cells; ++c)
	{
		measure[c] = cellMeasure(ds, c);
		if (measure[c] > 0.0 && std::isfinite(measure[c]))
		{
			total += measure[c];
			++measured;
		}
		else
			measure[c] = 0.0;
	}
	const double fallback = measured > 0 ? total / static_cast<double>(measured) : 1.0; // a cell without a size weighs as the average one

	_start.assign(_nodeCount + 1, 0);
	std::vector<std::uint32_t> corners;
	for (std::size_t c = 0; c < cells; ++c)
	{
		cornerNodes(ds, c, corners);
		for (std::uint32_t node : corners)
			if (node < _nodeCount)
				++_start[node + 1];
	}
	for (std::size_t n = 0; n < _nodeCount; ++n)
		_start[n + 1] += _start[n];
	_cell.resize(_start[_nodeCount]);
	_weight.resize(_start[_nodeCount]);
	std::vector<std::uint32_t> fill(_start.begin(), _start.end() - 1);
	for (std::size_t c = 0; c < cells; ++c)
	{
		cornerNodes(ds, c, corners);
		for (std::uint32_t node : corners)
			if (node < _nodeCount)
			{
				_cell[fill[node]] = static_cast<std::uint32_t>(c);
				_weight[fill[node]] = static_cast<float>(measure[c] > 0.0 ? measure[c] : fallback);
				++fill[node];
			}
	}
}

float CellToNodeAverager::valueAt(std::uint32_t node, const std::vector<float>& cellValues) const
{
	if (node >= _nodeCount)
		return std::numeric_limits<float>::quiet_NaN();
	double sum = 0.0, weights = 0.0;
	for (std::uint32_t k = _start[node]; k < _start[node + 1]; ++k)
	{
		const std::uint32_t cell = _cell[k];
		if (cell >= cellValues.size() || !std::isfinite(cellValues[cell]))
			continue;
		sum += static_cast<double>(_weight[k]) * static_cast<double>(cellValues[cell]);
		weights += static_cast<double>(_weight[k]);
	}
	return weights > 0.0 ? static_cast<float>(sum / weights) : std::numeric_limits<float>::quiet_NaN();
}

void CellToNodeAverager::average(const std::vector<float>& cellValues, std::vector<float>& nodeValues) const
{
	nodeValues.assign(_nodeCount, std::numeric_limits<float>::quiet_NaN());
	for (std::size_t n = 0; n < _nodeCount; ++n)
		nodeValues[n] = valueAt(static_cast<std::uint32_t>(n), cellValues);
}
