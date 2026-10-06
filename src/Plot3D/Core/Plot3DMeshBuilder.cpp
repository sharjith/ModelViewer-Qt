#include "Plot3DMeshBuilder.h"

#include <CGAL/Delaunay_triangulation_2.h>
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Triangulation_data_structure_2.h>
#include <CGAL/Triangulation_face_base_2.h>
#include <CGAL/Triangulation_vertex_base_with_info_2.h>

#include <QObject>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>

namespace
{
	// A tiny, dependency-free 3-float vector for face-normal math (Surface's grid triangulation below) -
	// deliberately not QVector3D: this Core file has no other reason to need QtGui (Plot3DData.h itself is
	// QtCore-only), so a plain local type keeps that true rather than pulling in QtGui for one cross product
	// (plot3d_tests happens to link Qt6::Gui already for Plot3DAxisController.cpp's QVector3D use, but that's no
	// reason for every Core file to lean on it too).
	struct Vec3f
	{
		float x = 0.0f, y = 0.0f, z = 0.0f;
		Vec3f operator-(const Vec3f& o) const { return { x - o.x, y - o.y, z - o.z }; }
		Vec3f cross(const Vec3f& o) const { return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x }; }
	};

	bool buildUnstructuredSurfaceMesh(const Plot3DSurfaceData& data, Plot3DMeshData& out, QString* error)
	{
		out = Plot3DMeshData();
		const std::vector<Plot3DSample>& samples = data.samples;
		if (samples.size() < 3)
		{
			if (error) *error = QObject::tr("Surface needs at least three non-collinear points; got %1.").arg(samples.size());
			return false;
		}

		using Kernel = CGAL::Exact_predicates_inexact_constructions_kernel;
		using VertexBase = CGAL::Triangulation_vertex_base_with_info_2<std::size_t, Kernel>;
		using FaceBase = CGAL::Triangulation_face_base_2<Kernel>;
		using TriangulationData = CGAL::Triangulation_data_structure_2<VertexBase, FaceBase>;
		using Delaunay = CGAL::Delaunay_triangulation_2<Kernel, TriangulationData>;
		using Point = Kernel::Point_2;
		Delaunay triangulation;
		std::set<std::pair<double, double>> occupiedPositions;
		for (std::size_t i = 0; i < samples.size(); ++i)
		{
			const auto key = std::make_pair(samples[i].position.x, samples[i].position.y);
			if (!occupiedPositions.insert(key).second)
			{
				if (error) *error = QObject::tr("Surface data has more than one point at (x=%1, y=%2).").arg(key.first).arg(key.second);
				return false;
			}
			triangulation.insert(Point(key.first, key.second))->info() = i;
		}
		// CGAL's triangulation API does not expose a number_of_finite_faces()
		// query on every supported version.  Count the iterator range once so
		// both the degeneracy check and the allocation below are portable.
		std::size_t finiteFaceCount = 0;
		for (auto face = triangulation.finite_faces_begin(); face != triangulation.finite_faces_end(); ++face)
			++finiteFaceCount;
		if (finiteFaceCount == 0)
		{
			if (error) *error = QObject::tr("Surface points are collinear and cannot form triangles.");
			return false;
		}

		out.positions.resize(samples.size() * 3);
		out.normals.assign(samples.size() * 3, 0.0f);
		out.values.resize(samples.size());
		for (std::size_t i = 0; i < samples.size(); ++i)
		{
			out.positions[i * 3] = static_cast<float>(samples[i].position.x);
			out.positions[i * 3 + 1] = static_cast<float>(samples[i].position.y);
			out.positions[i * 3 + 2] = static_cast<float>(samples[i].position.z);
			out.values[i] = samples[i].value;
		}
		out.indices.reserve(finiteFaceCount * 3);
		for (auto face = triangulation.finite_faces_begin(); face != triangulation.finite_faces_end(); ++face)
			out.indices.insert(out.indices.end(), { static_cast<unsigned int>(face->vertex(0)->info()),
				static_cast<unsigned int>(face->vertex(1)->info()), static_cast<unsigned int>(face->vertex(2)->info()) });

		auto position = [&out](std::size_t vertex) {
			return Vec3f{ out.positions[vertex * 3], out.positions[vertex * 3 + 1], out.positions[vertex * 3 + 2] };
		};
		for (std::size_t triangle = 0; triangle < out.indices.size(); triangle += 3)
		{
			const unsigned int a = out.indices[triangle], b = out.indices[triangle + 1], c = out.indices[triangle + 2];
			const Vec3f normal = (position(b) - position(a)).cross(position(c) - position(a));
			for (unsigned int vertex : { a, b, c })
			{
				out.normals[vertex * 3] += normal.x;
				out.normals[vertex * 3 + 1] += normal.y;
				out.normals[vertex * 3 + 2] += normal.z;
			}
		}
		for (std::size_t vertex = 0; vertex < samples.size(); ++vertex)
		{
			Vec3f normal{ out.normals[vertex * 3], out.normals[vertex * 3 + 1], out.normals[vertex * 3 + 2] };
			const float squaredLength = normal.x * normal.x + normal.y * normal.y + normal.z * normal.z;
			if (squaredLength > 1.0e-12f)
			{
				const float inverseLength = 1.0f / std::sqrt(squaredLength);
				normal = { normal.x * inverseLength, normal.y * inverseLength, normal.z * inverseLength };
			}
			else normal = { 0.0f, 0.0f, 1.0f };
			out.normals[vertex * 3] = normal.x;
			out.normals[vertex * 3 + 1] = normal.y;
			out.normals[vertex * 3 + 2] = normal.z;
		}
		return true;
	}
}

namespace
{
	// Groups sorted-but-not-yet-deduplicated coordinates into distinct grid lines, merging values that differ by
	// less than `tolerance` (CSV round-tripping through text can perturb an otherwise-exact grid coordinate in the
	// last decimal place). Returns, for each input sample (in original order), the index of the grid line it
	// belongs to.
	std::vector<double> distinctSortedValues(const std::vector<double>& raw, double tolerance, std::vector<int>& outIndexOfSample)
	{
		std::vector<std::size_t> order(raw.size());
		for (std::size_t i = 0; i < raw.size(); ++i)
			order[i] = i;
		std::sort(order.begin(), order.end(), [&raw](std::size_t a, std::size_t b) { return raw[a] < raw[b]; });

		std::vector<double> distinct;
		std::vector<int> lineOfOrder(order.size());
		for (std::size_t k = 0; k < order.size(); ++k)
		{
			const double value = raw[order[k]];
			if (distinct.empty() || value - distinct.back() > tolerance)
				distinct.push_back(value);
			lineOfOrder[k] = static_cast<int>(distinct.size()) - 1;
		}

		outIndexOfSample.assign(raw.size(), -1);
		for (std::size_t k = 0; k < order.size(); ++k)
			outIndexOfSample[order[k]] = lineOfOrder[k];
		return distinct;
	}
}

bool buildPlot3DSurfaceMesh(const Plot3DSurfaceData& data, Plot3DMeshData& out, QString* error)
{
	out = Plot3DMeshData();
	const std::vector<Plot3DSample>& samples = data.samples;
	if (samples.size() < 3)
	{
		if (error)
			*error = QObject::tr("Surface needs at least three non-collinear points; got %1.").arg(samples.size());
		return false;
	}

	std::vector<double> xs(samples.size()), ys(samples.size());
	double xMin = std::numeric_limits<double>::max(), xMax = std::numeric_limits<double>::lowest();
	double yMin = std::numeric_limits<double>::max(), yMax = std::numeric_limits<double>::lowest();
	for (std::size_t i = 0; i < samples.size(); ++i)
	{
		xs[i] = samples[i].position.x;
		ys[i] = samples[i].position.y;
		xMin = std::min(xMin, xs[i]); xMax = std::max(xMax, xs[i]);
		yMin = std::min(yMin, ys[i]); yMax = std::max(yMax, ys[i]);
	}
	// A relative tolerance derived from each axis's own span - an absolute epsilon would be meaningless across the
	// very different coordinate scales a plot's X/Y axes can have.
	const double xTolerance = std::max(1.0e-9, (xMax - xMin) * 1.0e-6);
	const double yTolerance = std::max(1.0e-9, (yMax - yMin) * 1.0e-6);

	std::vector<int> ixOfSample, iyOfSample;
	const std::vector<double> gridX = distinctSortedValues(xs, xTolerance, ixOfSample);
	const std::vector<double> gridY = distinctSortedValues(ys, yTolerance, iyOfSample);
	const std::size_t nx = gridX.size(), ny = gridY.size();

	// A complete regular grid keeps the older deterministic connectivity/order. Every other non-degenerate sample
	// set is an unstructured surface and is triangulated in its X/Y plane below via CGAL's Delaunay implementation.
	if (nx < 2 || ny < 2 || nx * ny != samples.size())
		return buildUnstructuredSurfaceMesh(data, out, error);

	// Map each sample onto its (ix, iy) grid cell, detecting duplicate/missing cells along the way.
	std::vector<int> cellSampleIndex(nx * ny, -1);
	for (std::size_t i = 0; i < samples.size(); ++i)
	{
		const int ix = ixOfSample[i], iy = iyOfSample[i];
		const std::size_t cell = static_cast<std::size_t>(iy) * nx + static_cast<std::size_t>(ix);
		if (cellSampleIndex[cell] != -1)
		{
			if (error)
				*error = QObject::tr("Surface data has more than one point at grid position (x=%1, y=%2).")
					.arg(gridX[static_cast<std::size_t>(ix)]).arg(gridY[static_cast<std::size_t>(iy)]);
			return false;
		}
		cellSampleIndex[cell] = static_cast<int>(i);
	}
	// distinctSortedValues() + the nx*ny count check above already guarantee every cell was filled exactly once
	// (nx*ny distinct-grid-line pairs, nx*ny samples, no duplicate assigned to the same cell), so no separate
	// "missing cell" pass is needed here.

	out.positions.resize(nx * ny * 3);
	out.values.resize(nx * ny);
	out.normals.assign(nx * ny * 3, 0.0f);
	for (std::size_t cell = 0; cell < nx * ny; ++cell)
	{
		const Plot3DSample& sample = samples[static_cast<std::size_t>(cellSampleIndex[cell])];
		out.positions[cell * 3 + 0] = static_cast<float>(sample.position.x);
		out.positions[cell * 3 + 1] = static_cast<float>(sample.position.y);
		out.positions[cell * 3 + 2] = static_cast<float>(sample.position.z);
		out.values[cell] = sample.value;
	}

	auto vertexAt = [nx](std::size_t ix, std::size_t iy) { return iy * nx + ix; };
	auto position = [&out](std::size_t vertex) {
		return Vec3f{ out.positions[vertex * 3], out.positions[vertex * 3 + 1], out.positions[vertex * 3 + 2] };
		};
	auto accumulateNormal = [&out](std::size_t vertex, const Vec3f& n) {
		out.normals[vertex * 3 + 0] += n.x;
		out.normals[vertex * 3 + 1] += n.y;
		out.normals[vertex * 3 + 2] += n.z;
		};

	out.indices.reserve((nx - 1) * (ny - 1) * 6);
	for (std::size_t iy = 0; iy + 1 < ny; ++iy)
	{
		for (std::size_t ix = 0; ix + 1 < nx; ++ix)
		{
			const std::size_t v00 = vertexAt(ix, iy), v10 = vertexAt(ix + 1, iy);
			const std::size_t v01 = vertexAt(ix, iy + 1), v11 = vertexAt(ix + 1, iy + 1);
			// Two triangles per cell, both wound so their face normal (via the right-hand rule) points away from
			// the surface's "underside" for a grid whose X/Y form the base plane and Z the height - matplotlib's
			// own default surface orientation.
			out.indices.insert(out.indices.end(), { static_cast<unsigned int>(v00), static_cast<unsigned int>(v10), static_cast<unsigned int>(v11) });
			out.indices.insert(out.indices.end(), { static_cast<unsigned int>(v00), static_cast<unsigned int>(v11), static_cast<unsigned int>(v01) });

			const Vec3f n1 = (position(v10) - position(v00)).cross(position(v11) - position(v00));
			const Vec3f n2 = (position(v11) - position(v00)).cross(position(v01) - position(v00));
			accumulateNormal(v00, n1); accumulateNormal(v10, n1); accumulateNormal(v11, n1);
			accumulateNormal(v00, n2); accumulateNormal(v11, n2); accumulateNormal(v01, n2);
		}
	}

	for (std::size_t vertex = 0; vertex < nx * ny; ++vertex)
	{
		Vec3f n{ out.normals[vertex * 3], out.normals[vertex * 3 + 1], out.normals[vertex * 3 + 2] };
		const float lengthSquared = n.x * n.x + n.y * n.y + n.z * n.z;
		if (lengthSquared > 1.0e-12f)
		{
			const float invLength = 1.0f / std::sqrt(lengthSquared);
			n = { n.x * invLength, n.y * invLength, n.z * invLength };
		}
		else
		{
			n = { 0.0f, 0.0f, 1.0f };
		}
		out.normals[vertex * 3 + 0] = n.x;
		out.normals[vertex * 3 + 1] = n.y;
		out.normals[vertex * 3 + 2] = n.z;
	}

	return true;
}

bool buildPlot3DContourLines(const std::vector<float>& positions, const std::vector<unsigned int>& indices,
	const std::vector<float>* vertexValues, Plot3DMeshData& out, int levelCount, QString* error, bool projectToBase, float lift)
{
	out = Plot3DMeshData();
	if (levelCount < 1)
	{
		if (error) *error = QObject::tr("Contour needs at least one level.");
		return false;
	}
	const std::size_t vertexCount = positions.size() / 3;
	if (vertexCount == 0 || indices.size() < 3 || (vertexValues && vertexValues->size() != vertexCount))
	{
		if (error) *error = QObject::tr("Contour needs a triangle surface.");
		return false;
	}
	float low = std::numeric_limits<float>::max(), high = std::numeric_limits<float>::lowest();
	for (std::size_t v = 0; v < vertexCount; ++v)
	{
		low = std::min(low, positions[v * 3 + 2]);
		high = std::max(high, positions[v * 3 + 2]);
	}
	if (!(high > low))
	{
		if (error) *error = QObject::tr("Contour needs a Surface with a non-zero Z range.");
		return false;
	}

	// Each crossing is interpolated along its edge. The vertex value is the surface's own value there when the caller
	// supplies one (so lines match the colours beneath them), otherwise the contour level.
	auto appendPoint = [&](unsigned int a, unsigned int b, float level) {
		const float za = positions[static_cast<std::size_t>(a) * 3 + 2];
		const float zb = positions[static_cast<std::size_t>(b) * 3 + 2];
		const float t = (level - za) / (zb - za);
		for (int axis = 0; axis < 3; ++axis)
		{
			const float pa = positions[static_cast<std::size_t>(a) * 3 + axis];
			const float pb = positions[static_cast<std::size_t>(b) * 3 + axis];
			float coordinate = pa + t * (pb - pa);
			if (axis == 2)
				coordinate = projectToBase ? low : coordinate + lift;
			out.positions.push_back(coordinate);
			out.normals.push_back(axis == 2 ? 1.0f : 0.0f);
		}
		out.values.push_back(vertexValues
			? static_cast<double>((*vertexValues)[a] + t * ((*vertexValues)[b] - (*vertexValues)[a]))
			: static_cast<double>(level));
	};
	for (int i = 1; i <= levelCount; ++i)
	{
		const float level = low + (high - low) * static_cast<float>(i) / static_cast<float>(levelCount + 1);
		for (std::size_t t = 0; t + 2 < indices.size(); t += 3)
		{
			const unsigned int tri[3] = { indices[t], indices[t + 1], indices[t + 2] };
			unsigned int edgeA[2], edgeB[2]; int crossings = 0;
			for (int e = 0; e < 3; ++e)
			{
				const unsigned int a = tri[e], b = tri[(e + 1) % 3];
				const float da = positions[static_cast<std::size_t>(a) * 3 + 2] - level;
				const float db = positions[static_cast<std::size_t>(b) * 3 + 2] - level;
				if ((da < 0.0f && db > 0.0f) || (da > 0.0f && db < 0.0f))
				{
					if (crossings < 2) { edgeA[crossings] = a; edgeB[crossings] = b; }
					++crossings;
				}
			}
			if (crossings == 2) { appendPoint(edgeA[0], edgeB[0], level); appendPoint(edgeA[1], edgeB[1], level); }
		}
	}
	return !out.empty();
}

bool buildPlot3DContourMesh(const Plot3DSurfaceData& data, Plot3DMeshData& out, int levelCount, QString* error, bool projectToBase)
{
	out = Plot3DMeshData();
	if (levelCount < 1)
	{
		if (error) *error = QObject::tr("Contour needs at least one level.");
		return false;
	}
	Plot3DMeshData surface;
	if (!buildPlot3DSurfaceMesh(data, surface, error))
		return false;
	return buildPlot3DContourLines(surface.positions, surface.indices, nullptr, out, levelCount, error, projectToBase, 0.0f);
}

namespace
{
	// Shared by Line/Scatter below: both are just a flat, unindexed vertex list drawn via glDrawArrays with a
	// native point/line GL primitive mode (see Plot3DPanel::buildPlot()'s SceneMesh construction) - the SAME
	// GL_POINTS/GL_LINE_STRIP path glTF point-cloud/line-set import already uses (SceneMesh::draw()), which draws
	// at a fixed PIXEL point size / line width (glPointSize()/glLineWidth(), not scaled by camera distance) rather
	// than real 3D geometry. That is deliberately what Line/Scatter use here instead of solid tube/marker meshes
	// (an earlier version of this builder did that): it matches matplotlib's own scatter/line markers, which stay
	// a constant size on screen regardless of 3D zoom, and it needs no new rendering code at all. The `normals`
	// array is filled with an arbitrary unit vector only because Plot3DMeshData/the Vertex struct always carries
	// one - it is never read for POINTS/LINE_STRIP (the shader's analysis-overlay path this mesh always uses
	// returns unlit, before normals matter, and there is no lighting fallback to worry about either).
	void appendFlatPointList(Plot3DMeshData& out, const std::vector<Plot3DSample>& samples)
	{
		out.positions.reserve(samples.size() * 3);
		out.normals.reserve(samples.size() * 3);
		out.values.reserve(samples.size());
		for (const Plot3DSample& sample : samples)
		{
			out.positions.insert(out.positions.end(), { static_cast<float>(sample.position.x), static_cast<float>(sample.position.y), static_cast<float>(sample.position.z) });
			out.normals.insert(out.normals.end(), { 0.0f, 0.0f, 1.0f });
			out.values.push_back(sample.value);
		}
	}
}

bool buildPlot3DLineMesh(const Plot3DLineData& data, Plot3DMeshData& out, QString* error)
{
	out = Plot3DMeshData();
	if (data.samples.size() < 2)
	{
		if (error)
			*error = QObject::tr("Line needs at least 2 points; got %1.").arg(data.samples.size());
		return false;
	}
	appendFlatPointList(out, data.samples);
	return true;
}

bool buildPlot3DScatterMesh(const Plot3DScatterData& data, Plot3DMeshData& out, QString* error)
{
	out = Plot3DMeshData();
	if (data.samples.empty())
	{
		if (error)
			*error = QObject::tr("Scatter data has no points.");
		return false;
	}
	appendFlatPointList(out, data.samples);
	return true;
}

bool buildPlot3DStemMesh(const Plot3DScatterData& data, double baseZ, Plot3DMeshData& out, QString* error)
{
	out = Plot3DMeshData();
	if (data.samples.empty())
	{
		if (error) *error = QObject::tr("Stem plot data has no points.");
		return false;
	}
	if (!std::isfinite(baseZ))
	{
		if (error) *error = QObject::tr("Stem base Z must be finite.");
		return false;
	}
	out.positions.reserve(data.samples.size() * 6);
	out.normals.reserve(data.samples.size() * 6);
	out.values.reserve(data.samples.size() * 2);
	for (const Plot3DSample& sample : data.samples)
	{
		// GL_LINES consumes pairs, so every stem is independent and the CSV row order remains irrelevant.
		for (double z : { baseZ, sample.position.z })
		{
			out.positions.insert(out.positions.end(), { static_cast<float>(sample.position.x), static_cast<float>(sample.position.y), static_cast<float>(z) });
			out.normals.insert(out.normals.end(), { 0.0f, 0.0f, 1.0f });
			out.values.push_back(sample.value);
		}
	}
	return true;
}

bool buildPlot3DErrorBarMesh(const Plot3DScatterData& data, Plot3DMeshData& out, QString* error)
{
	out = Plot3DMeshData();
	if (data.samples.empty() || data.errors.size() != data.samples.size())
	{
		if (error)
			*error = QObject::tr("Error-bar data is incomplete.");
		return false;
	}

	double xmin = data.samples.front().position.x;
	double xmax = xmin;
	for (const Plot3DSample& sample : data.samples)
	{
		xmin = std::min(xmin, sample.position.x);
		xmax = std::max(xmax, sample.position.x);
	}
	const double cap = std::max((xmax - xmin) * 0.015, 1.0e-6);
	for (std::size_t i = 0; i < data.samples.size(); ++i)
	{
		const Plot3DSample& sample = data.samples[i];
		const double uncertainty = data.errors[i];
		if (!std::isfinite(uncertainty))
			continue;
		if (uncertainty < 0.0)
		{
			if (error)
				*error = QObject::tr("Error values must be non-negative.");
			out = Plot3DMeshData();
			return false;
		}

		const double low = sample.position.z - uncertainty;
		const double high = sample.position.z + uncertainty;
		// Besides the endpoint caps, give the measured Z value a centre tick. Native
		// GL_POINTS are intentionally tiny in this renderer, so a geometric tick is
		// the reliable visible marker at every zoom level.
		const Plot3DPoint ends[] = {
			{ sample.position.x, sample.position.y, low },
			{ sample.position.x, sample.position.y, high },
			{ sample.position.x - cap, sample.position.y, low },
			{ sample.position.x + cap, sample.position.y, low },
			{ sample.position.x - cap, sample.position.y, high },
			{ sample.position.x + cap, sample.position.y, high },
			{ sample.position.x - cap, sample.position.y, sample.position.z },
			{ sample.position.x + cap, sample.position.y, sample.position.z }
		};
		for (const Plot3DPoint& point : ends)
		{
			out.positions.insert(out.positions.end(), { float(point.x), float(point.y), float(point.z) });
			out.normals.insert(out.normals.end(), { 0, 0, 1 });
			out.values.push_back(sample.value);
		}
	}
	return !out.empty();
}

bool buildPlot3DScatterFillMesh(const Plot3DScatterData& data, double baseZ, Plot3DMeshData& out, QString* error)
{
	out = Plot3DMeshData();
	if (data.samples.empty())
	{
		if (error) *error = QObject::tr("Filled scatter data has no points.");
		return false;
	}
	if (!std::isfinite(baseZ))
	{
		if (error) *error = QObject::tr("Filled scatter base Z must be finite.");
		return false;
	}

	double minimumX = data.samples.front().position.x;
	double maximumX = minimumX;
	for (const Plot3DSample& sample : data.samples)
	{
		minimumX = std::min(minimumX, sample.position.x);
		maximumX = std::max(maximumX, sample.position.x);
	}
	const double halfWidth = std::max((maximumX - minimumX) * 0.0125, 1.0e-6);
	out.positions.reserve(data.samples.size() * 12);
	out.normals.reserve(data.samples.size() * 12);
	out.values.reserve(data.samples.size() * 4);
	out.indices.reserve(data.samples.size() * 6);
	for (const Plot3DSample& sample : data.samples)
	{
		const Plot3DPoint leftBase{ sample.position.x - halfWidth, sample.position.y, baseZ };
		const Plot3DPoint rightBase{ sample.position.x + halfWidth, sample.position.y, baseZ };
		const Plot3DPoint leftSample{ sample.position.x - halfWidth, sample.position.y, sample.position.z };
		const Plot3DPoint rightSample{ sample.position.x + halfWidth, sample.position.y, sample.position.z };
		// Keep every quad counter-clockwise when viewed from +Y. Without this sign-aware ordering, ribbons above the
		// base had -Y geometric winding but +Y vertex normals, while ribbons below the base had +Y for both. The
		// renderer consequently treated the positive-Z cluster as back-facing and reduced its green ramp to black.
		const std::array<Plot3DPoint, 4> points = sample.position.z >= baseZ
			? std::array<Plot3DPoint, 4>{ leftBase, leftSample, rightSample, rightBase }
			: std::array<Plot3DPoint, 4>{ leftBase, rightBase, rightSample, leftSample };
		const unsigned int first = static_cast<unsigned int>(out.vertexCount());
		for (const Plot3DPoint& point : points)
		{
			out.positions.insert(out.positions.end(), {
				static_cast<float>(point.x), static_cast<float>(point.y), static_cast<float>(point.z) });
			out.normals.insert(out.normals.end(), { 0.0f, 1.0f, 0.0f });
			out.values.push_back(sample.value);
		}
		out.indices.insert(out.indices.end(), { first, first + 1, first + 2, first, first + 2, first + 3 });
	}
	return true;
}

bool buildPlot3DLineFillMesh(const Plot3DLineData& data, double baseZ, Plot3DMeshData& out, QString* error)
{
	out = Plot3DMeshData();
	if (data.samples.size() < 2)
	{
		if (error) *error = QObject::tr("A filled line needs at least two points.");
		return false;
	}
	if (!std::isfinite(baseZ))
	{
		if (error) *error = QObject::tr("Filled line base Z must be finite.");
		return false;
	}
	const bool between = data.fillTo.size() == data.samples.size(); // a second curve: fill between the two, not down to the base plane
	out.positions.reserve(data.samples.size() * 6);
	out.normals.reserve(data.samples.size() * 6);
	out.values.reserve(data.samples.size() * 2);
	// Two vertices per sample (on the line and on the base plane, or on the second curve); each segment is a quad between neighbouring samples, drawn with
	// both windings so the ribbon is visible from either side whatever the render pass culls.
	for (std::size_t i = 0; i < data.samples.size(); ++i)
	{
		const Plot3DSample& sample = data.samples[i];
		const double otherZ = between && std::isfinite(data.fillTo[i]) ? data.fillTo[i] : baseZ;
		out.positions.insert(out.positions.end(), { static_cast<float>(sample.position.x), static_cast<float>(sample.position.y), static_cast<float>(sample.position.z) });
		out.positions.insert(out.positions.end(), { static_cast<float>(sample.position.x), static_cast<float>(sample.position.y), static_cast<float>(otherZ) });
		out.normals.insert(out.normals.end(), { 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f });
		out.values.push_back(sample.value);
		out.values.push_back(sample.value);
	}
	for (unsigned int i = 0; i + 1 < static_cast<unsigned int>(data.samples.size()); ++i)
	{
		const unsigned int a = i * 2, b = a + 1, c = a + 2, d = a + 3; // a/c on the line, b/d on the base
		out.indices.insert(out.indices.end(), { a, b, d, a, d, c, a, d, b, a, c, d });
	}
	return true;
}

bool buildPlot3DBarMesh(const Plot3DBarData& data, Plot3DMeshData& out, QString* error)
{
	out = Plot3DMeshData();
	if (data.bars.empty())
	{
		if (error) *error = QObject::tr("Bar data has no bars.");
		return false;
	}
	out.positions.reserve(data.bars.size() * 24 * 3);
	out.normals.reserve(data.bars.size() * 24 * 3);
	out.values.reserve(data.bars.size() * 24);
	out.indices.reserve(data.bars.size() * 36);

	auto addFace = [&out](const Vec3f& a, const Vec3f& b, const Vec3f& c, const Vec3f& d,
		const Vec3f& normal, double value) {
		const unsigned int first = static_cast<unsigned int>(out.vertexCount());
		for (const Vec3f& p : { a, b, c, d })
		{
			out.positions.insert(out.positions.end(), { p.x, p.y, p.z });
			out.normals.insert(out.normals.end(), { normal.x, normal.y, normal.z });
			out.values.push_back(value);
		}
		out.indices.insert(out.indices.end(), { first, first + 1, first + 2, first, first + 2, first + 3 });
		};

	for (std::size_t i = 0; i < data.bars.size(); ++i)
	{
		const Plot3DBar& bar = data.bars[i];
		if (!(bar.width > 0.0) || !(bar.depth > 0.0) || !std::isfinite(bar.x) || !std::isfinite(bar.y)
			|| !std::isfinite(bar.base) || !std::isfinite(bar.height) || !std::isfinite(bar.width) || !std::isfinite(bar.depth))
		{
			if (error) *error = QObject::tr("Bar %1 has invalid geometry.").arg(i + 1);
			out = Plot3DMeshData();
			return false;
		}
		const float x0 = static_cast<float>(bar.x - bar.width * 0.5), x1 = static_cast<float>(bar.x + bar.width * 0.5);
		const float y0 = static_cast<float>(bar.y - bar.depth * 0.5), y1 = static_cast<float>(bar.y + bar.depth * 0.5);
		const float z0 = static_cast<float>(std::min(bar.base, bar.base + bar.height));
		const float z1 = static_cast<float>(std::max(bar.base, bar.base + bar.height));
		const double value = std::isfinite(bar.value) ? bar.value : bar.height;
		addFace({x0,y0,z0},{x0,y1,z0},{x1,y1,z0},{x1,y0,z0},{0,0,-1},value);
		addFace({x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1},{0,0, 1},value);
		addFace({x0,y0,z0},{x0,y0,z1},{x0,y1,z1},{x0,y1,z0},{-1,0,0},value);
		addFace({x1,y0,z0},{x1,y1,z0},{x1,y1,z1},{x1,y0,z1},{ 1,0,0},value);
		addFace({x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1},{0,-1,0},value);
		addFace({x0,y1,z0},{x0,y1,z1},{x1,y1,z1},{x1,y1,z0},{0, 1,0},value);
	}
	return true;
}

bool buildPlot3DVoxelGrid(const Plot3DVoxelData& data, Plot3DVoxelGrid& out, QString* error)
{
	out = Plot3DVoxelGrid();
	if (data.voxels.empty())
	{
		if (error) *error = QObject::tr("Voxel data has no occupied cells.");
		return false;
	}
	int minimum[3] = { std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), std::numeric_limits<int>::max() };
	int maximum[3] = { std::numeric_limits<int>::lowest(), std::numeric_limits<int>::lowest(), std::numeric_limits<int>::lowest() };
	for (std::size_t row = 0; row < data.voxels.size(); ++row)
	{
		const Plot3DVoxel& voxel = data.voxels[row];
		if (voxel.x < 0 || voxel.y < 0 || voxel.z < 0 || !std::isfinite(voxel.occupancy)
			|| voxel.occupancy < 0.0 || voxel.occupancy > 1.0)
		{
			if (error) *error = QObject::tr("Voxel %1 has invalid indices or occupancy.").arg(row + 1);
			return false;
		}
		const int coordinate[3] = { voxel.x, voxel.y, voxel.z };
		for (int axis = 0; axis < 3; ++axis)
		{
			minimum[axis] = std::min(minimum[axis], coordinate[axis]);
			maximum[axis] = std::max(maximum[axis], coordinate[axis]);
		}
	}
	const long long dimension64[3] = { static_cast<long long>(maximum[0]) - minimum[0] + 1,
		static_cast<long long>(maximum[1]) - minimum[1] + 1, static_cast<long long>(maximum[2]) - minimum[2] + 1 };
	if (dimension64[0] > 256 || dimension64[1] > 256 || dimension64[2] > 256)
	{
		if (error) *error = QObject::tr("Voxel grid is %1 x %2 x %3. Each dimension must be at most 256 cells.")
			.arg(dimension64[0]).arg(dimension64[1]).arg(dimension64[2]);
		return false;
	}
	const int dimensions[3] = { static_cast<int>(dimension64[0]), static_cast<int>(dimension64[1]), static_cast<int>(dimension64[2]) };
	out.dimX = dimensions[0]; out.dimY = dimensions[1]; out.dimZ = dimensions[2];
	for (int axis = 0; axis < 3; ++axis) out.origin[axis] = static_cast<float>(minimum[axis]);
	out.values.assign(out.voxelCount(), std::numeric_limits<float>::quiet_NaN());
	for (std::size_t row = 0; row < data.voxels.size(); ++row)
	{
		const Plot3DVoxel& voxel = data.voxels[row];
		const int x = voxel.x - minimum[0], y = voxel.y - minimum[1], z = voxel.z - minimum[2];
		const std::size_t index = (static_cast<std::size_t>(z) * out.dimY + static_cast<std::size_t>(y)) * out.dimX + static_cast<std::size_t>(x);
		if (std::isfinite(out.values[index]))
		{
			if (error) *error = QObject::tr("Voxel data has more than one value at (%1, %2, %3).").arg(voxel.x).arg(voxel.y).arg(voxel.z);
			out = Plot3DVoxelGrid();
			return false;
		}
		out.values[index] = static_cast<float>(voxel.occupancy);
	}
	return true;
}

bool buildPlot3DQuiverSiteMesh(const Plot3DQuiverData& data, Plot3DMeshData& out, QString* error)
{
	out = Plot3DMeshData();
	if (data.arrows.empty())
	{
		if (error)
			*error = QObject::tr("Quiver data has no points.");
		return false;
	}
	out.positions.reserve(data.arrows.size() * 3);
	out.normals.reserve(data.arrows.size() * 3);
	out.values.reserve(data.arrows.size());
	for (const Plot3DQuiver& arrow : data.arrows)
	{
		out.positions.insert(out.positions.end(), { static_cast<float>(arrow.position.x), static_cast<float>(arrow.position.y), static_cast<float>(arrow.position.z) });
		out.normals.insert(out.normals.end(), { 0.0f, 0.0f, 1.0f }); // unused for GL_POINTS - see buildPlot3DLineMesh()'s doc comment
		out.values.push_back(arrow.value);
	}
	return true;
}
