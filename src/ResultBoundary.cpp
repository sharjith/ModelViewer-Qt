#include "ResultBoundary.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

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
		std::uint8_t face;
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
		std::uint8_t face;
	};
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

	// ---- Count volume faces to size the partitioning ---------------------------------------------
	std::size_t totalFaces = 0;
	for (std::size_t c = 0; c < cellCount; ++c)
	{
		const FaceTemplate* templates = nullptr;
		totalFaces += static_cast<std::size_t>(faceTemplatesFor(ds.cellTypes[c], templates));
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
			const FaceTemplate* templates = nullptr;
			const int n = faceTemplatesFor(ds.cellTypes[c], templates);
			if (n == 0)
				continue;
			const std::uint32_t* nodes = ds.cellConnectivity.data() + ds.cellOffsets[c];
			for (int f = 0; f < n; ++f)
			{
				FaceRecord r;
				r.key = makeKey(nodes, templates[f]);
				if (partitions > 1 && hashKey(r.key) % partitions != p)
					continue;
				r.cell = static_cast<std::uint32_t>(c);
				r.face = static_cast<std::uint8_t>(f);
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
	auto emitTriangle = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t cell, std::uint8_t face)
	{
		out.triangles.push_back(vertexFor(a));
		out.triangles.push_back(vertexFor(b));
		out.triangles.push_back(vertexFor(c));
		out.triangleCell.push_back(cell);
		out.triangleFace.push_back(face);
	};

	std::size_t emittedFaces = 0;
	for (const BoundaryFace& bf : boundaryFaces)
	{
		if ((emittedFaces++ & 0xFFFF) == 0 && isCancelled())
			return fail(QStringLiteral("cancelled"));

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
			if (!resultCellIsVolume(type))
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
