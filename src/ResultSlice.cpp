#include "ResultSlice.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace
{
	struct FaceRing
	{
		std::uint8_t count;
		std::uint8_t v[4]; // local corner indices
	};

	// The node rings of the linear cells (the corner nodes come first in every cell layout, so quadratic cells use the same tables). Only the node
	// sets and the cyclic order matter here, not the winding.
	const FaceRing kTet[4] = { { 3, { 0, 1, 3, 0 } }, { 3, { 1, 2, 3, 0 } }, { 3, { 2, 0, 3, 0 } }, { 3, { 0, 2, 1, 0 } } };
	const FaceRing kHex[6] = { { 4, { 0, 4, 7, 3 } }, { 4, { 1, 2, 6, 5 } }, { 4, { 0, 1, 5, 4 } }, { 4, { 3, 7, 6, 2 } }, { 4, { 0, 3, 2, 1 } }, { 4, { 4, 5, 6, 7 } } };
	const FaceRing kWedge[5] = { { 3, { 0, 2, 1, 0 } }, { 3, { 3, 4, 5, 0 } }, { 4, { 0, 1, 4, 3 } }, { 4, { 1, 2, 5, 4 } }, { 4, { 2, 0, 3, 5 } } };
	const FaceRing kPyramid[5] = { { 4, { 0, 3, 2, 1 } }, { 3, { 0, 1, 4, 0 } }, { 3, { 1, 2, 4, 0 } }, { 3, { 2, 3, 4, 0 } }, { 3, { 3, 0, 4, 0 } } };

	int ringsFor(ResultCellType type, const FaceRing*& out)
	{
		switch (resultCellCornerType(type))
		{
		case ResultCellType::Tetra:      out = kTet;     return 4;
		case ResultCellType::Hexahedron: out = kHex;     return 6;
		case ResultCellType::Wedge:      out = kWedge;   return 5;
		case ResultCellType::Pyramid:    out = kPyramid; return 5;
		default: break;
		}
		out = nullptr;
		return 0;
	}

	struct Segment
	{
		std::uint32_t a, b; // vertex indices
		bool used = false;
	};
}

std::vector<float> planeDistances(const ResultDataset& dataset, const double point[3], const double normal[3])
{
	const std::size_t n = dataset.nodeCount();
	std::vector<float> distance(n);
	for (std::size_t i = 0; i < n; ++i)
	{
		const double x = dataset.nodePositions[i * 3] - point[0], y = dataset.nodePositions[i * 3 + 1] - point[1], z = dataset.nodePositions[i * 3 + 2] - point[2];
		distance[i] = static_cast<float>(x * normal[0] + y * normal[1] + z * normal[2]);
	}
	return distance;
}

bool cutVolume(const ResultDataset& ds, const std::vector<float>& distance, const std::vector<float>* nodeValues, SliceMesh& out, const std::atomic<bool>* cancel)
{
	out = SliceMesh();
	if (distance.size() != ds.nodeCount() || (nodeValues && nodeValues->size() != ds.nodeCount()))
		return false;
	const float nan = std::numeric_limits<float>::quiet_NaN();

	// One vertex per cut edge, shared by every cell around the edge. The key is the edge's two nodes, lower first.
	std::unordered_map<std::uint64_t, std::uint32_t> vertexOfEdge;
	auto edgeVertex = [&](std::uint32_t a, std::uint32_t b) -> std::uint32_t
	{
		if (a > b)
			std::swap(a, b);
		const std::uint64_t key = (static_cast<std::uint64_t>(a) << 32) | b;
		const auto found = vertexOfEdge.find(key);
		if (found != vertexOfEdge.end())
			return found->second;
		const double da = distance[a], db = distance[b]; // opposite sides: da - db cannot be 0
		const double t = da / (da - db);
		for (std::size_t k = 0; k < 3; ++k)
		{
			const double pa = ds.nodePositions[static_cast<std::size_t>(a) * 3 + k], pb = ds.nodePositions[static_cast<std::size_t>(b) * 3 + k];
			out.positions.push_back(static_cast<float>(pa + t * (pb - pa)));
		}
		if (nodeValues)
		{
			const double va = (*nodeValues)[a], vb = (*nodeValues)[b];
			out.values.push_back(static_cast<float>(va + t * (vb - va)));
		}
		else
			out.values.push_back(nan);
		out.edgeNodes.push_back(a);
		out.edgeNodes.push_back(b);
		out.edgeT.push_back(static_cast<float>(t));
		const std::uint32_t index = static_cast<std::uint32_t>(out.values.size() - 1);
		vertexOfEdge.emplace(key, index);
		return index;
	};

	std::vector<std::uint32_t> ringNodes; // the rings of the cell being cut, one after the other
	std::vector<std::size_t> ringStarts;  // ring r is ringNodes[ringStarts[r] .. ringStarts[r + 1])
	std::vector<Segment> segments;
	std::vector<std::uint32_t> loop;
	const std::size_t cellCount = ds.cellCount();
	for (std::size_t c = 0; c < cellCount; ++c)
	{
		if ((c & 0x3FFF) == 0 && cancel && cancel->load(std::memory_order_acquire))
			return false;
		const ResultCellType type = ds.cellTypes[c];
		ringNodes.clear();
		ringStarts.assign(1, 0);
		if (type == ResultCellType::Polyhedron)
		{
			for (std::size_t k = 0; k < ds.polyhedronFaceCount(c); ++k)
			{
				const std::uint32_t face = ds.cellFaces[ds.cellFaceOffsets[c] + k];
				ringNodes.insert(ringNodes.end(), ds.faceNodes.begin() + ds.faceOffsets[face], ds.faceNodes.begin() + ds.faceOffsets[face + 1]);
				ringStarts.push_back(ringNodes.size());
			}
		}
		else
		{
			const FaceRing* rings = nullptr;
			const int count = ringsFor(type, rings);
			if (count == 0)
				continue; // a surface or line cell, or an unsupported one
			const std::uint32_t* nodes = ds.cellConnectivity.data() + ds.cellOffsets[c];
			for (int r = 0; r < count; ++r)
			{
				for (int i = 0; i < rings[r].count; ++i)
					ringNodes.push_back(nodes[rings[r].v[i]]);
				ringStarts.push_back(ringNodes.size());
			}
		}
		if (ringNodes.empty())
			continue;

		// Does the cell straddle the cut at all? (Also skips a cell with a non-finite value.)
		bool anyPositive = false, anyNegative = false, finite = true;
		for (std::uint32_t node : ringNodes)
		{
			const float d = distance[node];
			if (!std::isfinite(d))
			{
				finite = false;
				break;
			}
			(d >= 0.0f ? anyPositive : anyNegative) = true;
		}
		if (!finite || !anyPositive || !anyNegative)
			continue;

		// One segment per run of negative nodes on each face: between the two edges the run leaves through.
		segments.clear();
		for (std::size_t r = 0; r + 1 < ringStarts.size(); ++r)
		{
			const std::uint32_t* ring = ringNodes.data() + ringStarts[r];
			const std::size_t k = ringStarts[r + 1] - ringStarts[r];
			auto negative = [&](std::size_t i) { return distance[ring[i % k]] < 0.0f; };
			for (std::size_t i = 0; i < k; ++i)
			{
				if (!negative(i) || negative(i + k - 1))
					continue; // not the start of a run
				std::size_t j = i;
				while (negative(j + 1) && j + 1 < i + k)
					++j;
				Segment s;
				s.a = edgeVertex(ring[(i + k - 1) % k], ring[i]);
				s.b = edgeVertex(ring[j % k], ring[(j + 1) % k]);
				segments.push_back(s);
			}
		}

		// The direction from the negative to the positive nodes: the cut's triangles are wound to face it.
		double reference[3] = { 0, 0, 0 };
		{
			double positive[3] = { 0, 0, 0 }, negative[3] = { 0, 0, 0 };
			std::size_t nPositive = 0, nNegative = 0;
			for (std::uint32_t node : ringNodes)
			{
				double* sum = distance[node] >= 0.0f ? positive : negative;
				(distance[node] >= 0.0f ? nPositive : nNegative) += 1;
				for (std::size_t k = 0; k < 3; ++k)
					sum[k] += ds.nodePositions[static_cast<std::size_t>(node) * 3 + k];
			}
			for (std::size_t k = 0; k < 3; ++k)
				reference[k] = positive[k] / static_cast<double>(nPositive) - negative[k] / static_cast<double>(nNegative);
		}

		// Chain the segments into loops (each vertex belongs to two segments) and fan-triangulate them.
		for (Segment& start : segments)
		{
			if (start.used)
				continue;
			start.used = true;
			loop.assign({ start.a, start.b });
			for (bool extended = true; extended;)
			{
				extended = false;
				for (Segment& s : segments)
				{
					if (s.used)
						continue;
					if (s.a == loop.back())
						loop.push_back(s.b);
					else if (s.b == loop.back())
						loop.push_back(s.a);
					else
						continue;
					s.used = true;
					extended = true;
					break;
				}
			}
			if (loop.size() > 1 && loop.back() == loop.front())
				loop.pop_back(); // closed
			if (loop.size() < 3)
				continue;
			// Orient by the first triangle against the reference direction.
			const float* p0 = &out.positions[loop[0] * 3];
			const float* p1 = &out.positions[loop[1] * 3];
			const float* p2 = &out.positions[loop[2] * 3];
			const double ax = p1[0] - p0[0], ay = p1[1] - p0[1], az = p1[2] - p0[2];
			const double bx = p2[0] - p0[0], by = p2[1] - p0[1], bz = p2[2] - p0[2];
			const double normal[3] = { ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx };
			if (normal[0] * reference[0] + normal[1] * reference[1] + normal[2] * reference[2] < 0.0)
				std::reverse(loop.begin(), loop.end());
			for (std::size_t i = 1; i + 1 < loop.size(); ++i)
			{
				out.triangles.insert(out.triangles.end(), { loop[0], loop[i], loop[i + 1] });
				out.triangleCell.push_back(static_cast<std::uint32_t>(c));
			}
		}
	}
	return true;
}

void interpolateSliceValues(SliceMesh& mesh, const std::vector<float>& nodeValues)
{
	mesh.values.assign(mesh.vertexCount(), std::numeric_limits<float>::quiet_NaN());
	for (std::size_t v = 0; v < mesh.vertexCount() && v * 2 + 1 < mesh.edgeNodes.size(); ++v)
	{
		const std::size_t a = mesh.edgeNodes[v * 2], b = mesh.edgeNodes[v * 2 + 1];
		if (a >= nodeValues.size() || b >= nodeValues.size())
			continue;
		const double va = nodeValues[a], vb = nodeValues[b];
		mesh.values[v] = static_cast<float>(va + static_cast<double>(mesh.edgeT[v]) * (vb - va));
	}
}

void clipTrianglesToHalfSpace(std::vector<float>& positions, std::vector<float>& attributes, int stride, std::vector<std::uint32_t>& triangles,
                              std::vector<std::uint32_t>& triangleCell, const double point[3], const double normal[3])
{
	const std::size_t width = static_cast<std::size_t>(std::max(stride, 0));
	std::vector<float> outPositions, outAttributes;
	std::vector<std::uint32_t> outTriangles, outCells;
	struct Corner
	{
		double p[3];
		std::vector<float> a;
		double d;
	};
	auto corner = [&](std::uint32_t v) {
		Corner c;
		for (std::size_t k = 0; k < 3; ++k)
			c.p[k] = positions[static_cast<std::size_t>(v) * 3 + k];
		c.a.assign(attributes.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(v) * width),
		           attributes.begin() + static_cast<std::ptrdiff_t>((static_cast<std::size_t>(v) + 1) * width));
		c.d = (c.p[0] - point[0]) * normal[0] + (c.p[1] - point[1]) * normal[1] + (c.p[2] - point[2]) * normal[2];
		return c;
	};
	auto addCorner = [&](const Corner& c) {
		outPositions.insert(outPositions.end(), { static_cast<float>(c.p[0]), static_cast<float>(c.p[1]), static_cast<float>(c.p[2]) });
		outAttributes.insert(outAttributes.end(), c.a.begin(), c.a.end());
		return static_cast<std::uint32_t>(outPositions.size() / 3 - 1);
	};
	for (std::size_t t = 0; t + 2 < triangles.size(); t += 3)
	{
		const Corner tri[3] = { corner(triangles[t]), corner(triangles[t + 1]), corner(triangles[t + 2]) };
		// Sutherland-Hodgman against the one plane: the kept polygon has at most 4 corners.
		std::vector<Corner> polygon;
		for (int i = 0; i < 3; ++i)
		{
			const Corner& a = tri[i];
			const Corner& b = tri[(i + 1) % 3];
			if (a.d >= 0.0)
				polygon.push_back(a);
			if ((a.d >= 0.0) != (b.d >= 0.0))
			{
				const double u = a.d / (a.d - b.d);
				Corner c;
				for (std::size_t k = 0; k < 3; ++k)
					c.p[k] = a.p[k] + u * (b.p[k] - a.p[k]);
				c.a.resize(width);
				for (std::size_t k = 0; k < width; ++k)
					c.a[k] = static_cast<float>(a.a[k] + u * (b.a[k] - a.a[k]));
				c.d = 0.0;
				polygon.push_back(c);
			}
		}
		if (polygon.size() < 3)
			continue;
		const std::uint32_t first = addCorner(polygon[0]);
		std::uint32_t previous = addCorner(polygon[1]);
		for (std::size_t k = 2; k < polygon.size(); ++k)
		{
			const std::uint32_t next = addCorner(polygon[k]);
			outTriangles.insert(outTriangles.end(), { first, previous, next });
			if (!triangleCell.empty())
				outCells.push_back(triangleCell[t / 3]);
			previous = next;
		}
	}
	positions = std::move(outPositions);
	attributes = std::move(outAttributes);
	triangles = std::move(outTriangles);
	triangleCell = std::move(outCells);
}

void unweldSlice(SliceMesh& mesh)
{
	std::vector<float> positions, values;
	positions.reserve(mesh.triangles.size() * 3);
	values.reserve(mesh.triangles.size());
	std::vector<std::uint32_t> triangles(mesh.triangles.size()), edgeNodes;
	std::vector<float> edgeT;
	for (std::size_t i = 0; i < mesh.triangles.size(); ++i)
	{
		const std::size_t v = mesh.triangles[i];
		if (v * 2 + 1 < mesh.edgeNodes.size())
		{
			edgeNodes.insert(edgeNodes.end(), { mesh.edgeNodes[v * 2], mesh.edgeNodes[v * 2 + 1] });
			edgeT.push_back(mesh.edgeT[v]);
		}
		positions.insert(positions.end(), { mesh.positions[v * 3], mesh.positions[v * 3 + 1], mesh.positions[v * 3 + 2] });
		values.push_back(v < mesh.values.size() ? mesh.values[v] : std::numeric_limits<float>::quiet_NaN());
		triangles[i] = static_cast<std::uint32_t>(i);
	}
	mesh.positions = std::move(positions);
	mesh.values = std::move(values);
	mesh.triangles = std::move(triangles);
	mesh.edgeNodes = std::move(edgeNodes);
	mesh.edgeT = std::move(edgeT);
}
