#include "ResultBoundary.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
	struct FaceTemplate
	{
		std::uint8_t nodeCount; // 3 or 4
		std::uint8_t v[4];      // local node indices within the cell
	};

	// Node sets per face for the VTK cell layouts. Winding here is NOT relied upon - each emitted face is
	// re-oriented against its cell's centroid - only the node sets matter.
	const FaceTemplate kTetFaces[4] = {
		{ 3, { 0, 1, 3, 0 } }, { 3, { 1, 2, 3, 0 } }, { 3, { 2, 0, 3, 0 } }, { 3, { 0, 2, 1, 0 } } };
	const FaceTemplate kHexFaces[6] = {
		{ 4, { 0, 4, 7, 3 } }, { 4, { 1, 2, 6, 5 } }, { 4, { 0, 1, 5, 4 } },
		{ 4, { 3, 7, 6, 2 } }, { 4, { 0, 3, 2, 1 } }, { 4, { 4, 5, 6, 7 } } };
	const FaceTemplate kWedgeFaces[5] = {
		{ 3, { 0, 2, 1, 0 } }, { 3, { 3, 4, 5, 0 } }, { 4, { 0, 1, 4, 3 } }, { 4, { 1, 2, 5, 4 } }, { 4, { 2, 0, 3, 5 } } };
	const FaceTemplate kPyramidFaces[5] = {
		{ 4, { 0, 3, 2, 1 } }, { 3, { 0, 1, 4, 0 } }, { 3, { 1, 2, 4, 0 } }, { 3, { 2, 3, 4, 0 } }, { 3, { 3, 0, 4, 0 } } };

	int faceTemplatesFor(ResultCellType type, const FaceTemplate*& out)
	{
		// Quadratic cells: the corner nodes are the first N nodes in VTK order, so the linear templates apply.
		switch (resultCellCornerType(type))
		{
		case ResultCellType::Tetra:      out = kTetFaces;     return 4;
		case ResultCellType::Hexahedron: out = kHexFaces;     return 6;
		case ResultCellType::Wedge:      out = kWedgeFaces;   return 5;
		case ResultCellType::Pyramid:    out = kPyramidFaces; return 5;
		default: break;
		}
		out = nullptr;
		return 0;
	}

	constexpr std::uint32_t kPad = std::numeric_limits<std::uint32_t>::max();

	// A face keyed by its sorted node indices (padded with kPad for triangles) so the two cells sharing
	// an interior face produce identical keys regardless of their local numbering or winding.
	struct FaceRecord
	{
		std::array<std::uint32_t, 4> key;
		std::uint32_t cell;
		std::uint32_t face; // index within the cell's face list (a polyhedron can have more than 255 faces)
	};

	bool keyLess(const FaceRecord& a, const FaceRecord& b) { return a.key < b.key; }
	bool keyEqual(const FaceRecord& a, const FaceRecord& b) { return a.key == b.key; }

	std::uint64_t hashKey(const std::array<std::uint32_t, 4>& k)
	{
		std::uint64_t h = 1469598103934665603ULL;
		for (std::uint32_t v : k)
		{
			h ^= v;
			h *= 1099511628211ULL;
		}
		h ^= h >> 29;
		return h;
	}

	std::array<std::uint32_t, 4> makeKey(const std::uint32_t* cellNodes, const FaceTemplate& f)
	{
		std::array<std::uint32_t, 4> key = { kPad, kPad, kPad, kPad };
		for (int i = 0; i < f.nodeCount; ++i)
			key[i] = cellNodes[f.v[i]];
		std::sort(key.begin(), key.begin() + f.nodeCount);
		return key;
	}

	struct BoundaryFace
	{
		std::uint32_t cell;
		std::uint32_t face;
	};

	// The key of a polyhedron's face: for a triangle or quad the same key a template face of a regular cell has (so a polyhedron and a tet or
	// hex sharing a face match), for a larger polygon a hash of its sorted nodes and its size, flagged by a value no node index takes.
	std::array<std::uint32_t, 4> makePolygonKey(const std::uint32_t* ring, std::size_t count)
	{
		std::array<std::uint32_t, 4> key = { kPad, kPad, kPad, kPad };
		if (count <= 4)
		{
			for (std::size_t i = 0; i < count; ++i)
				key[i] = ring[i];
			std::sort(key.begin(), key.begin() + static_cast<std::ptrdiff_t>(count));
			return key;
		}
		std::vector<std::uint32_t> sorted(ring, ring + count);
		std::sort(sorted.begin(), sorted.end());
		std::uint64_t h = 1469598103934665603ULL;
		for (std::uint32_t v : sorted)
		{
			h ^= v;
			h *= 1099511628211ULL;
		}
		h ^= static_cast<std::uint64_t>(count) * 0x9E3779B97F4A7C15ULL;
		key[0] = static_cast<std::uint32_t>(h);
		key[1] = static_cast<std::uint32_t>(h >> 32);
		key[2] = static_cast<std::uint32_t>(count);
		key[3] = kPad - 1;
		return key;
	}

	// Splits a polygon (a ring of nodes wound counter-clockwise about `normal`) into triangles: a triangle as it is, a quad into two like the
	// template faces, a larger one by ear clipping (so a concave face stays inside its outline), falling back to a fan when clipping gets stuck.
	void triangulateRing(const std::vector<std::uint32_t>& ring, const float* pos, const double normal[3], std::vector<std::uint32_t>& out)
	{
		out.clear();
		const std::size_t n = ring.size();
		if (n < 3)
			return;
		if (n <= 4)
		{
			out.insert(out.end(), { ring[0], ring[1], ring[2] });
			if (n == 4)
				out.insert(out.end(), { ring[0], ring[2], ring[3] });
			return;
		}
		auto point = [&](std::uint32_t node, int a) { return static_cast<double>(pos[static_cast<std::size_t>(node) * 3 + static_cast<std::size_t>(a)]); };
		auto crossDot = [&](std::uint32_t o, std::uint32_t a, std::uint32_t b) // (a - o) x (b - o) . normal
		{
			const double ax = point(a, 0) - point(o, 0), ay = point(a, 1) - point(o, 1), az = point(a, 2) - point(o, 2);
			const double bx = point(b, 0) - point(o, 0), by = point(b, 1) - point(o, 1), bz = point(b, 2) - point(o, 2);
			return (ay * bz - az * by) * normal[0] + (az * bx - ax * bz) * normal[1] + (ax * by - ay * bx) * normal[2];
		};
		std::vector<std::uint32_t> poly = ring;
		while (poly.size() > 3)
		{
			bool clipped = false;
			for (std::size_t i = 0; i < poly.size() && !clipped; ++i)
			{
				const std::uint32_t a = poly[(i + poly.size() - 1) % poly.size()], b = poly[i], c = poly[(i + 1) % poly.size()];
				if (crossDot(a, b, c) <= 0.0)
					continue; // a reflex (or flat) corner is not an ear
				bool blocked = false;
				for (std::size_t k = 0; k < poly.size() && !blocked; ++k)
				{
					const std::uint32_t p = poly[k];
					if (p == a || p == b || p == c)
						continue;
					blocked = crossDot(a, b, p) >= 0.0 && crossDot(b, c, p) >= 0.0 && crossDot(c, a, p) >= 0.0;
				}
				if (blocked)
					continue;
				out.insert(out.end(), { a, b, c });
				poly.erase(poly.begin() + static_cast<std::ptrdiff_t>(i));
				clipped = true;
			}
			if (!clipped)
				break;
		}
		if (poly.size() == 3)
			out.insert(out.end(), { poly[0], poly[1], poly[2] });
		else
			for (std::size_t k = 1; k + 1 < poly.size(); ++k) // stuck (a degenerate face): a fan still covers it
				out.insert(out.end(), { poly[0], poly[k], poly[k + 1] });
	}
}

bool extractBoundarySurface(const ResultDataset& ds, ResultBoundarySurface& out,
                            const std::atomic<bool>* cancel, QString* error, std::size_t facesPerPartition)
{
	out = ResultBoundarySurface();
	auto fail = [error](const QString& message)
	{
		if (error)
			*error = message;
		return false;
	};
	auto isCancelled = [cancel]() { return cancel && cancel->load(std::memory_order_acquire); };

	const QString problem = ds.validate();
	if (!problem.isEmpty())
		return fail(QStringLiteral("Invalid dataset: %1").arg(problem));

	const std::size_t cellCount = ds.cellCount();

	// ---- A ready-made boundary (OpenFOAM): compact the nodes it uses into surface vertices ---------
	if (!ds.boundaryTriangles.empty())
	{
		std::vector<std::uint32_t> nodeToVertexReady(ds.nodeCount(), std::numeric_limits<std::uint32_t>::max());
		const float* readyPos = ds.nodePositions.data();
		for (std::size_t t = 0; t < ds.boundaryTriangles.size() / 3; ++t)
		{
			if ((t & 0xFFFF) == 0 && isCancelled())
				return fail(QStringLiteral("cancelled"));
			for (std::size_t k = 0; k < 3; ++k)
			{
				const std::uint32_t node = ds.boundaryTriangles[t * 3 + k];
				std::uint32_t& v = nodeToVertexReady[node];
				if (v == std::numeric_limits<std::uint32_t>::max())
				{
					v = static_cast<std::uint32_t>(out.positions.size() / 3);
					out.positions.push_back(readyPos[node * 3 + 0]);
					out.positions.push_back(readyPos[node * 3 + 1]);
					out.positions.push_back(readyPos[node * 3 + 2]);
					out.vertexNode.push_back(node);
				}
				out.triangles.push_back(v);
			}
			out.triangleCell.push_back(ds.boundaryTriangleCells[t]);
			out.triangleFace.push_back(ResultBoundarySurface::kNoFace);
		}
		return true;
	}

	// ---- Count volume faces to size the partitioning ---------------------------------------------
	std::size_t totalFaces = 0;
	for (std::size_t c = 0; c < cellCount; ++c)
	{
		const FaceTemplate* templates = nullptr;
		totalFaces += static_cast<std::size_t>(faceTemplatesFor(ds.cellTypes[c], templates));
		if (ds.cellTypes[c] == ResultCellType::Polyhedron)
			totalFaces += ds.polyhedronFaceCount(c);
	}
	// ~2M faces is about 64 MB of FaceRecords per pass.
	const std::size_t perPartition = std::max<std::size_t>(1, facesPerPartition);
	const std::size_t partitions = std::max<std::size_t>(1, std::min<std::size_t>(64, totalFaces / perPartition + 1));

	// ---- Find boundary volume faces: faces that occur exactly once ---------------------------------
	std::vector<BoundaryFace> boundaryFaces;
	std::vector<FaceRecord> records;
	for (std::size_t p = 0; p < partitions; ++p)
	{
		records.clear();
		records.reserve(totalFaces / partitions + 16);
		for (std::size_t c = 0; c < cellCount; ++c)
		{
			if ((c & 0xFFFF) == 0 && isCancelled())
				return fail(QStringLiteral("cancelled"));
			const bool polyhedron = ds.cellTypes[c] == ResultCellType::Polyhedron;
			const FaceTemplate* templates = nullptr;
			const int n = polyhedron ? static_cast<int>(ds.polyhedronFaceCount(c)) : faceTemplatesFor(ds.cellTypes[c], templates);
			if (n == 0)
				continue;
			const std::uint32_t* nodes = polyhedron ? nullptr : ds.cellConnectivity.data() + ds.cellOffsets[c];
			for (int f = 0; f < n; ++f)
			{
				FaceRecord r;
				if (polyhedron)
				{
					const std::uint32_t face = ds.cellFaces[ds.cellFaceOffsets[c] + static_cast<std::size_t>(f)];
					const std::size_t size = ds.faceOffsets[face + 1] - ds.faceOffsets[face];
					if (size < 3)
						continue; // not a face
					r.key = makePolygonKey(ds.faceNodes.data() + ds.faceOffsets[face], size);
				}
				else
					r.key = makeKey(nodes, templates[f]);
				if (partitions > 1 && hashKey(r.key) % partitions != p)
					continue;
				r.cell = static_cast<std::uint32_t>(c);
				r.face = static_cast<std::uint32_t>(f);
				records.push_back(r);
			}
		}
		std::sort(records.begin(), records.end(), keyLess);
		for (std::size_t i = 0; i < records.size();)
		{
			std::size_t j = i + 1;
			while (j < records.size() && keyEqual(records[i], records[j]))
				++j;
			if (j - i == 1)
				boundaryFaces.push_back({ records[i].cell, records[i].face });
			i = j;
		}
	}
	records.clear();
	records.shrink_to_fit();

	// Partitioning scrambles the order; sort for a deterministic result.
	std::sort(boundaryFaces.begin(), boundaryFaces.end(),
		[](const BoundaryFace& a, const BoundaryFace& b) { return a.cell != b.cell ? a.cell < b.cell : a.face < b.face; });

	// ---- Emit triangles ---------------------------------------------------------------------------
	std::vector<std::uint32_t> nodeToVertex(ds.nodeCount(), kPad);
	const float* pos = ds.nodePositions.data();

	auto vertexFor = [&](std::uint32_t node) -> std::uint32_t
	{
		std::uint32_t& v = nodeToVertex[node];
		if (v == kPad)
		{
			v = static_cast<std::uint32_t>(out.positions.size() / 3);
			out.positions.push_back(pos[node * 3 + 0]);
			out.positions.push_back(pos[node * 3 + 1]);
			out.positions.push_back(pos[node * 3 + 2]);
			out.vertexNode.push_back(node);
		}
		return v;
	};
	auto emitTriangle = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t cell, std::uint32_t face)
	{
		out.triangles.push_back(vertexFor(a));
		out.triangles.push_back(vertexFor(b));
		out.triangles.push_back(vertexFor(c));
		out.triangleCell.push_back(cell);
		out.triangleFace.push_back(face == ResultBoundarySurface::kNoFace
		                               ? ResultBoundarySurface::kNoFace
		                               : static_cast<std::uint8_t>(std::min<std::uint32_t>(face, ResultBoundarySurface::kNoFace - 1)));
	};

	// A polyhedron's boundary face: oriented away from the cell's centre (the mean of its distinct nodes) by the face's Newell normal, then
	// triangulated.
	std::uint32_t polyCell = kPad;
	double pcx = 0, pcy = 0, pcz = 0;
	std::vector<std::uint32_t> ring, cellNodesTmp, triangles;
	auto emitPolyhedronFace = [&](const BoundaryFace& bf)
	{
		const std::uint32_t face = ds.cellFaces[ds.cellFaceOffsets[bf.cell] + bf.face];
		ring.assign(ds.faceNodes.begin() + ds.faceOffsets[face], ds.faceNodes.begin() + ds.faceOffsets[face + 1]);
		if (polyCell != bf.cell)
		{
			cellNodesTmp.clear();
			for (std::uint32_t k = ds.cellFaceOffsets[bf.cell]; k < ds.cellFaceOffsets[bf.cell + 1]; ++k)
			{
				const std::uint32_t f = ds.cellFaces[k];
				cellNodesTmp.insert(cellNodesTmp.end(), ds.faceNodes.begin() + ds.faceOffsets[f], ds.faceNodes.begin() + ds.faceOffsets[f + 1]);
			}
			std::sort(cellNodesTmp.begin(), cellNodesTmp.end());
			cellNodesTmp.erase(std::unique(cellNodesTmp.begin(), cellNodesTmp.end()), cellNodesTmp.end());
			pcx = pcy = pcz = 0;
			for (std::uint32_t node : cellNodesTmp)
			{
				pcx += pos[node * 3 + 0];
				pcy += pos[node * 3 + 1];
				pcz += pos[node * 3 + 2];
			}
			const double count = static_cast<double>(std::max<std::size_t>(cellNodesTmp.size(), 1));
			pcx /= count;
			pcy /= count;
			pcz /= count;
			polyCell = bf.cell;
		}
		double normal[3] = { 0, 0, 0 }, fx = 0, fy = 0, fz = 0;
		for (std::size_t i = 0; i < ring.size(); ++i)
		{
			const float* a = pos + static_cast<std::size_t>(ring[i]) * 3;
			const float* b = pos + static_cast<std::size_t>(ring[(i + 1) % ring.size()]) * 3;
			normal[0] += (static_cast<double>(a[1]) - b[1]) * (static_cast<double>(a[2]) + b[2]);
			normal[1] += (static_cast<double>(a[2]) - b[2]) * (static_cast<double>(a[0]) + b[0]);
			normal[2] += (static_cast<double>(a[0]) - b[0]) * (static_cast<double>(a[1]) + b[1]);
			fx += a[0];
			fy += a[1];
			fz += a[2];
		}
		const double size = static_cast<double>(ring.size());
		fx /= size;
		fy /= size;
		fz /= size;
		if (normal[0] * (fx - pcx) + normal[1] * (fy - pcy) + normal[2] * (fz - pcz) < 0.0)
		{
			std::reverse(ring.begin(), ring.end());
			for (double& v : normal)
				v = -v;
		}
		triangulateRing(ring, pos, normal, triangles);
		for (std::size_t t = 0; t + 2 < triangles.size(); t += 3)
			emitTriangle(triangles[t], triangles[t + 1], triangles[t + 2], bf.cell, bf.face);
	};

	std::size_t emittedFaces = 0;
	for (const BoundaryFace& bf : boundaryFaces)
	{
		if ((emittedFaces++ & 0xFFFF) == 0 && isCancelled())
			return fail(QStringLiteral("cancelled"));

		if (ds.cellTypes[bf.cell] == ResultCellType::Polyhedron)
		{
			emitPolyhedronFace(bf);
			continue;
		}
		const FaceTemplate* templates = nullptr;
		faceTemplatesFor(ds.cellTypes[bf.cell], templates);
		const FaceTemplate& tf = templates[bf.face];
		const std::uint32_t* nodes = ds.cellConnectivity.data() + ds.cellOffsets[bf.cell];
		// Corner nodes only: for a quadratic cell the mid-edge nodes would bias the centroid on curved cells.
		const std::size_t cellNodeCount = static_cast<std::size_t>(resultCellNodeCount(resultCellCornerType(ds.cellTypes[bf.cell])));

		// Cell centroid, to orient the face away from it.
		double cx = 0, cy = 0, cz = 0;
		for (std::size_t k = 0; k < cellNodeCount; ++k)
		{
			cx += pos[nodes[k] * 3 + 0];
			cy += pos[nodes[k] * 3 + 1];
			cz += pos[nodes[k] * 3 + 2];
		}
		cx /= static_cast<double>(cellNodeCount);
		cy /= static_cast<double>(cellNodeCount);
		cz /= static_cast<double>(cellNodeCount);

		std::uint32_t fn[4];
		double fx = 0, fy = 0, fz = 0;
		for (int i = 0; i < tf.nodeCount; ++i)
		{
			fn[i] = nodes[tf.v[i]];
			fx += pos[fn[i] * 3 + 0];
			fy += pos[fn[i] * 3 + 1];
			fz += pos[fn[i] * 3 + 2];
		}
		fx /= tf.nodeCount;
		fy /= tf.nodeCount;
		fz /= tf.nodeCount;

		// Normal from the first three face nodes (the template's own winding).
		const double ax = pos[fn[1] * 3 + 0] - pos[fn[0] * 3 + 0];
		const double ay = pos[fn[1] * 3 + 1] - pos[fn[0] * 3 + 1];
		const double az = pos[fn[1] * 3 + 2] - pos[fn[0] * 3 + 2];
		const double bx = pos[fn[2] * 3 + 0] - pos[fn[0] * 3 + 0];
		const double by = pos[fn[2] * 3 + 1] - pos[fn[0] * 3 + 1];
		const double bz = pos[fn[2] * 3 + 2] - pos[fn[0] * 3 + 2];
		const double nx = ay * bz - az * by;
		const double ny = az * bx - ax * bz;
		const double nz = ax * by - ay * bx;
		const double outward = nx * (fx - cx) + ny * (fy - cy) + nz * (fz - cz);
		if (outward < 0.0) // degenerate (== 0) faces keep the template winding
			std::reverse(fn, fn + tf.nodeCount);

		emitTriangle(fn[0], fn[1], fn[2], bf.cell, bf.face);
		if (tf.nodeCount == 4)
			emitTriangle(fn[0], fn[2], fn[3], bf.cell, bf.face);
	}

	// ---- Surface (shell) cells are shown as they are -----------------------------------------------
	for (std::size_t c = 0; c < cellCount; ++c)
	{
		const ResultCellType type = ds.cellTypes[c];
		if (!resultCellIsSurface(type))
		{
			// Not drawn: a type that is neither volume nor surface, or a polyhedron whose faces the file did not give.
			if (!resultCellIsVolume(type) || (type == ResultCellType::Polyhedron && ds.polyhedronFaceCount(c) == 0))
				++out.skippedCells;
			continue;
		}
		if ((c & 0xFFFF) == 0 && isCancelled())
			return fail(QStringLiteral("cancelled"));
		const std::uint32_t* nodes = ds.cellConnectivity.data() + ds.cellOffsets[c];
		const std::uint32_t cell = static_cast<std::uint32_t>(c);
		emitTriangle(nodes[0], nodes[1], nodes[2], cell, ResultBoundarySurface::kNoFace);
		if (resultCellCornerType(type) == ResultCellType::Quad)
			emitTriangle(nodes[0], nodes[2], nodes[3], cell, ResultBoundarySurface::kNoFace);
	}

	return true;
}
