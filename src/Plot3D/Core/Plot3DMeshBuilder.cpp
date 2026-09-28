#include "Plot3DMeshBuilder.h"

#include <QObject>

#include <algorithm>
#include <cmath>
#include <limits>

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
	if (samples.size() < 4)
	{
		if (error)
			*error = QObject::tr("Surface needs at least a 2 x 2 grid (4 points); got %1.").arg(samples.size());
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

	if (nx < 2 || ny < 2)
	{
		if (error)
			*error = QObject::tr("Surface data does not form a grid with at least 2 distinct X and Y values (found %1 x %2).").arg(nx).arg(ny);
		return false;
	}
	if (nx * ny != samples.size())
	{
		if (error)
			*error = QObject::tr("Surface data must be a COMPLETE regular grid: found %1 distinct X and %2 distinct Y values "
				"(%3 grid points) but %4 rows. Scattered/unstructured surface data is not supported yet.")
				.arg(nx).arg(ny).arg(nx * ny).arg(samples.size());
		return false;
	}

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
