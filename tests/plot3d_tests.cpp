#include "Plot3DData.h"
#include "Plot3DFormula.h"
#include "Plot3DAxisController.h"
#include "Plot3DGenerate.h"
#include "Plot3DMeshBuilder.h"
#include "Plot3DPathlines.h"
#include "Plot3DSection.h"
#include "Plot3DSessionIO.h"

#include <QImage>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <variant>

namespace
{
	int checks = 0, failures = 0;
	#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (false)

	void testCsv()
	{
		Plot3DCsvTable table;
		QString error;
		CHECK(parsePlot3DCsv(QString::fromUtf8("\xef\xbb\xbfx,y,z,label\r\n1, 2,3,\"a,b\"\r\n4,5,6,\"say \"\"hi\"\"\"\r\n"), {}, table, &error));
		CHECK(error.isEmpty() && table.columnCount() == 4 && table.rows.size() == 2);
		CHECK(table.headers[0] == QLatin1String("x") && table.rows[0][3] == QLatin1String("a,b") && table.rows[1][3] == QLatin1String("say \"hi\""));

		Plot3DCsvOptions semicolon;
		semicolon.delimiter = QLatin1Char(';');
		semicolon.firstRowIsHeader = false;
		CHECK(parsePlot3DCsv(QStringLiteral("1;2;3\n4;5;6"), semicolon, table, &error));
		CHECK(table.headers == QStringList({ QStringLiteral("Column 1"), QStringLiteral("Column 2"), QStringLiteral("Column 3") })
		      && table.rows.size() == 2);
		CHECK(!parsePlot3DCsv(QStringLiteral("x,y\n1,2,3"), {}, table, &error) && error.contains(QStringLiteral("columns")));
		CHECK(!parsePlot3DCsv(QStringLiteral("x,y\n\"1,2"), {}, table, &error) && error.contains(QStringLiteral("Unterminated")));
		CHECK(parsePlot3DCsv(QStringLiteral("x,note\n1,\"two\nlines\""), {}, table, &error) && table.rows[0][1] == QLatin1String("two\nlines"));
	}

	void testDatasets()
	{
		Plot3DCsvTable table;
		QString error;
		CHECK(parsePlot3DCsv(QStringLiteral("x,y,z,value,u,v,w\n0,1,2,20,1,0,-1\n3,4,5,50,0,2,0"), {}, table, &error));
		Plot3DColumnMapping mapping;
		mapping.value = 3;
		Plot3DDataset dataset;
		CHECK(buildPlot3DDataset(table, Plot3DPrimitive::Surface, mapping, dataset, &error));
		CHECK(dataset.primitive == Plot3DPrimitive::Surface && dataset.itemCount() == 2 && std::holds_alternative<Plot3DSurfaceData>(dataset.content));
		CHECK(std::get<Plot3DSurfaceData>(dataset.content).samples[1].value == 50.0);

		mapping.u = 4; mapping.v = 5; mapping.w = 6;
		CHECK(buildPlot3DDataset(table, Plot3DPrimitive::Quiver, mapping, dataset, &error));
		const Plot3DQuiverData& quiver = std::get<Plot3DQuiverData>(dataset.content);
		CHECK(quiver.arrows.size() == 2 && quiver.arrows[0].vector.z == -1.0);
		double lo[3], hi[3];
		CHECK(plot3DDataBounds(dataset, lo, hi));
		CHECK(lo[0] == 0.0 && lo[1] == 1.0 && lo[2] == 2.0 && hi[0] == 3.0 && hi[1] == 4.0 && hi[2] == 5.0);

		CHECK(parsePlot3DCsv(QStringLiteral("x,y,height,base,width,depth\n1,2,4,-1,2,6"), {}, table, &error));
		mapping = Plot3DColumnMapping(); mapping.base = 3; mapping.width = 4; mapping.depth = 5;
		CHECK(buildPlot3DDataset(table, Plot3DPrimitive::Bar, mapping, dataset, &error));
		const Plot3DBar& bar = std::get<Plot3DBarData>(dataset.content).bars[0];
		CHECK(bar.base == -1.0 && bar.height == 4.0 && bar.width == 2.0 && bar.depth == 6.0);
		CHECK(plot3DDataBounds(dataset, lo, hi) && lo[0] == 0.0 && hi[0] == 2.0 && lo[1] == -1.0 && hi[1] == 5.0 && lo[2] == -1.0 && hi[2] == 3.0);
		mapping.y = -1;
		CHECK(buildPlot3DDataset(table, Plot3DPrimitive::Bar, mapping, dataset, &error)
		      && std::get<Plot3DBarData>(dataset.content).bars[0].y == 0.0);

		CHECK(parsePlot3DCsv(QStringLiteral("i,j,k,occupied\n0,1,2,0.75\n3,4,5,1"), {}, table, &error));
		mapping = Plot3DColumnMapping(); mapping.value = 3;
		CHECK(buildPlot3DDataset(table, Plot3DPrimitive::Voxel, mapping, dataset, &error));
		CHECK(std::get<Plot3DVoxelData>(dataset.content).voxels[0].occupancy == 0.75);
		CHECK(plot3DDataBounds(dataset, lo, hi) && lo[0] == 0.0 && hi[0] == 4.0 && lo[2] == 2.0 && hi[2] == 6.0);
		CHECK(parsePlot3DCsv(QStringLiteral("i,j,k\n0.5,1,2"), {}, table, &error));
		CHECK(!buildPlot3DDataset(table, Plot3DPrimitive::Voxel, {}, dataset, &error) && error.contains(QStringLiteral("integer")));
		CHECK(parsePlot3DCsv(QStringLiteral("i,j,k,occupied\n0,1,2,1.1"), {}, table, &error));
		mapping = Plot3DColumnMapping(); mapping.value = 3;
		CHECK(!buildPlot3DDataset(table, Plot3DPrimitive::Voxel, mapping, dataset, &error) && error.contains(QStringLiteral("0 to 1")));

		CHECK(parsePlot3DCsv(QStringLiteral("x,y,z\n0,nope,2"), {}, table, &error));
		CHECK(!buildPlot3DDataset(table, Plot3DPrimitive::Line, {}, dataset, &error) && error.contains(QStringLiteral("invalid Y")));
	}

	void testSurfaceMesh()
	{
		// A 3 x 2 grid (x in {0,1,2}, y in {0,1}), deliberately NOT inserted in row-major order - the builder must
		// not depend on input order. value = x + 10*y so each vertex's colour value is independently checkable.
		Plot3DSurfaceData grid;
		auto addPoint = [&grid](double x, double y) {
			Plot3DSample sample;
			sample.position = { x, y, x + 10.0 * y };
			sample.value = x + 10.0 * y;
			grid.samples.push_back(sample);
			};
		addPoint(2, 0); addPoint(0, 1); addPoint(1, 0); addPoint(2, 1); addPoint(0, 0); addPoint(1, 1);

		Plot3DMeshData mesh;
		QString error;
		CHECK(buildPlot3DSurfaceMesh(grid, mesh, &error));
		CHECK(error.isEmpty());
		CHECK(mesh.vertexCount() == 6 && mesh.indices.size() == 12);
		// Every normal must come out unit-length (the flat grid's normals are all +Z, but the length check alone
		// also catches a degenerate/zero accumulation bug regardless of direction).
		for (std::size_t v = 0; v < mesh.vertexCount(); ++v)
		{
			const float nx = mesh.normals[v * 3], ny = mesh.normals[v * 3 + 1], nz = mesh.normals[v * 3 + 2];
			CHECK(std::abs(nx * nx + ny * ny + nz * nz - 1.0f) < 1.0e-4f);
		}
		// Find the vertex placed at (x=1, y=1) by its known value (11) and check its position round-tripped.
		bool foundCenter = false;
		for (std::size_t v = 0; v < mesh.vertexCount(); ++v)
		{
			if (std::abs(mesh.values[v] - 11.0) < 1.0e-9)
			{
				foundCenter = true;
				CHECK(std::abs(mesh.positions[v * 3] - 1.0f) < 1.0e-6f && std::abs(mesh.positions[v * 3 + 1] - 1.0f) < 1.0e-6f);
			}
		}
		CHECK(foundCenter);

		// Ten intermediate levels across this sloped grid each cut its triangles into
		// independent line pairs. The output is unindexed for GL_LINES.
		Plot3DMeshData contour;
		CHECK(buildPlot3DContourMesh(grid, contour, 10, &error));
		CHECK(!contour.empty() && contour.indices.empty() && contour.vertexCount() % 2 == 0);
		for (std::size_t v = 0; v < contour.vertexCount(); ++v)
			CHECK(contour.positions[v * 3 + 2] > 0.0f && contour.positions[v * 3 + 2] < 12.0f);

		// Projected onto the base plane: the same iso-lines (same count, same X/Y, same level values) all flattened to the
		// surface's minimum Z.
		Plot3DMeshData projected;
		CHECK(buildPlot3DContourMesh(grid, projected, 10, &error, true));
		CHECK(projected.vertexCount() == contour.vertexCount() && projected.values == contour.values);
		float surfaceLow = std::numeric_limits<float>::max();
		for (const Plot3DSample& sample : grid.samples)
			surfaceLow = std::min(surfaceLow, static_cast<float>(sample.position.z));
		for (std::size_t v = 0; v < projected.vertexCount(); ++v)
		{
			CHECK(projected.positions[v * 3 + 2] == surfaceLow);
			CHECK(projected.positions[v * 3] == contour.positions[v * 3] && projected.positions[v * 3 + 1] == contour.positions[v * 3 + 1]);
		}

		// The same iso-lines straight from a triangle mesh (what a Surface plot's contour overlay uses): identical geometry, a
		// supplied per-vertex value is interpolated onto the lines, and lift raises only the Z of lines that are not projected.
		Plot3DMeshData surfaceMesh;
		CHECK(buildPlot3DSurfaceMesh(grid, surfaceMesh, &error));
		std::vector<float> vertexValues(surfaceMesh.vertexCount());
		for (std::size_t v = 0; v < vertexValues.size(); ++v)
			vertexValues[v] = 100.0f + surfaceMesh.positions[v * 3 + 2];
		Plot3DMeshData fromMesh, lifted, projectedLifted;
		CHECK(buildPlot3DContourLines(surfaceMesh.positions, surfaceMesh.indices, &vertexValues, fromMesh, 10, &error));
		CHECK(fromMesh.vertexCount() == contour.vertexCount() && fromMesh.positions == contour.positions);
		for (std::size_t v = 0; v < fromMesh.vertexCount(); ++v)
			CHECK(std::abs(fromMesh.values[v] - (100.0 + contour.values[v])) < 1.0e-3);
		CHECK(buildPlot3DContourLines(surfaceMesh.positions, surfaceMesh.indices, nullptr, lifted, 10, &error, false, 0.25f));
		CHECK(buildPlot3DContourLines(surfaceMesh.positions, surfaceMesh.indices, nullptr, projectedLifted, 10, &error, true, 0.25f));
		for (std::size_t v = 0; v < lifted.vertexCount(); ++v)
		{
			CHECK(std::abs(lifted.positions[v * 3 + 2] - (contour.positions[v * 3 + 2] + 0.25f)) < 1.0e-5f);
			CHECK(projectedLifted.positions[v * 3 + 2] == surfaceLow);
		}
		std::vector<float> wrongSize(3, 1.0f);
		CHECK(!buildPlot3DContourLines(surfaceMesh.positions, surfaceMesh.indices, &wrongSize, fromMesh, 10, &error));

		// Section curves (the hover probe): the X / Y / Z planes through a point cut the surface into segments that all lie
		// on the plane, and a plane through grid vertices still yields a consistent curve.
		for (int axis = 0; axis < 3; ++axis)
		{
			const float value = axis == 2 ? 5.0f : 0.9f; // an interior plane, off the vertices for X / Y
			std::vector<float> segments;
			const std::size_t count = plot3DSectionSegments(surfaceMesh.positions, surfaceMesh.indices, axis, value, segments);
			CHECK(count > 0 && segments.size() == count * 6);
			for (std::size_t i = 0; i < segments.size(); i += 3)
				CHECK(std::abs(segments[i + static_cast<std::size_t>(axis)] - value) < 1.0e-4f);
		}
		{
			std::vector<float> onVertices;
			const std::size_t throughVertices = plot3DSectionSegments(surfaceMesh.positions, surfaceMesh.indices, 0, 1.0f, onVertices);
			CHECK(throughVertices > 0);
			for (std::size_t i = 0; i < onVertices.size(); i += 3)
				CHECK(std::abs(onVertices[i] - 1.0f) < 1.0e-4f);
		}
		{
			std::vector<float> none;
			CHECK(plot3DSectionSegments(surfaceMesh.positions, surfaceMesh.indices, 0, 1000.0f, none) == 0 && none.empty());
			CHECK(plot3DSectionSegments(surfaceMesh.positions, surfaceMesh.indices, 3, 0.0f, none) == 0); // not an axis
		}

		// The connected curve through one triangle: on a single-branch cut it is the whole cut, and on a mesh whose cut has two
		// separate branches it keeps only the branch through the start triangle (a saddle's second hyperbola branch).
		{
			const std::vector<int> neighbours = plot3DTriangleNeighbours(surfaceMesh.indices);
			CHECK(neighbours.size() == surfaceMesh.indices.size());
			std::vector<float> all, connected;
			const std::size_t allCount = plot3DSectionSegments(surfaceMesh.positions, surfaceMesh.indices, 2, 5.0f, all);
			// Any triangle that is cut by the plane works as the start; find one from the full cut.
			int startTriangle = -1;
			for (std::size_t t = 0; t * 3 < surfaceMesh.indices.size() && startTriangle < 0; ++t)
			{
				bool low = false, high = false;
				for (int c = 0; c < 3; ++c)
				{
					const float z = surfaceMesh.positions[static_cast<std::size_t>(surfaceMesh.indices[t * 3 + static_cast<std::size_t>(c)]) * 3 + 2];
					(z >= 5.0f ? high : low) = true;
				}
				if (low && high)
					startTriangle = static_cast<int>(t);
			}
			CHECK(startTriangle >= 0);
			CHECK(plot3DSectionCurveThrough(surfaceMesh.positions, surfaceMesh.indices, neighbours, startTriangle, 2, 5.0f, connected) == allCount);
			CHECK(plot3DSectionCurveThrough(surfaceMesh.positions, surfaceMesh.indices, neighbours, 9999, 2, 5.0f, connected) == 0);

			// Two separate strips of triangles, each cut by the plane x = 0.5: starting in one gives only its own segments.
			const std::vector<float> strips = {
				0, 0, 0,  1, 0, 0,  0, 1, 0,  1, 1, 0,      // strip A (x from 0 to 1) at y 0..1
				0, 5, 0,  1, 5, 0,  0, 6, 0,  1, 6, 0 };    // strip B, far away in y
			const std::vector<unsigned int> stripIndices = { 0, 1, 2, 1, 3, 2,  4, 5, 6, 5, 7, 6 };
			const std::vector<int> stripNeighbours = plot3DTriangleNeighbours(stripIndices);
			std::vector<float> both, onlyA, onlyB;
			const std::size_t bothCount = plot3DSectionSegments(strips, stripIndices, 0, 0.5f, both);
			const std::size_t aCount = plot3DSectionCurveThrough(strips, stripIndices, stripNeighbours, 0, 0, 0.5f, onlyA);
			const std::size_t bCount = plot3DSectionCurveThrough(strips, stripIndices, stripNeighbours, 2, 0, 0.5f, onlyB);
			CHECK(bothCount == 4 && aCount == 2 && bCount == 2);
			for (std::size_t i = 1; i < onlyA.size(); i += 3)
				CHECK(onlyA[i] <= 1.0f + 1.0e-5f);
			for (std::size_t i = 1; i < onlyB.size(); i += 3)
				CHECK(onlyB[i] >= 5.0f - 1.0e-5f);
		}

		// An incomplete grid is now a valid unstructured surface: Delaunay
		// triangulation uses the supplied points without inventing the missing
		// corner.
		Plot3DSurfaceData incomplete = grid;
		incomplete.samples.pop_back();
		Plot3DMeshData badMesh;
		CHECK(buildPlot3DSurfaceMesh(incomplete, badMesh, &error)
			&& badMesh.vertexCount() == incomplete.samples.size() && !badMesh.indices.empty());

		// Too few points entirely.
		Plot3DSurfaceData tiny;
		tiny.samples.push_back(Plot3DSample{ { 0, 0, 0 }, 0.0 });
		tiny.samples.push_back(Plot3DSample{ { 1, 0, 0 }, 1.0 });
		CHECK(!buildPlot3DSurfaceMesh(tiny, badMesh, &error) && !error.isEmpty());

		// Five non-grid points use the Delaunay fallback. Their order is deliberately irregular and no 3 x 3 grid
		// can be inferred, but they still form a legitimate surface in the X/Y plane.
		Plot3DSurfaceData scattered;
		scattered.samples = { { { 0, 0, 0 }, 0.0 }, { { 2, 0, 0 }, 1.0 }, { { 0, 2, 0 }, 2.0 },
			{ { 2, 2, 1 }, 3.0 }, { { 0.7, 1.1, 0.4 }, 4.0 } };
		CHECK(buildPlot3DSurfaceMesh(scattered, mesh, &error));
		CHECK(mesh.vertexCount() == scattered.samples.size() && mesh.indices.size() >= 9 && mesh.indices.size() % 3 == 0);
		bool upwardNormals = true;
		for (std::size_t vertex = 0; vertex < mesh.vertexCount(); ++vertex)
			upwardNormals = upwardNormals && mesh.normals[vertex * 3 + 2] > 0.0f;
		CHECK(upwardNormals);

		Plot3DSurfaceData duplicate = scattered;
		duplicate.samples.push_back(scattered.samples.front());
		CHECK(!buildPlot3DSurfaceMesh(duplicate, badMesh, &error) && error.contains(QStringLiteral("more than one")));
		Plot3DSurfaceData collinear;
		collinear.samples = { { { 0, 0, 0 }, 0.0 }, { { 1, 1, 1 }, 1.0 }, { { 2, 2, 2 }, 2.0 } };
		CHECK(!buildPlot3DSurfaceMesh(collinear, badMesh, &error) && error.contains(QStringLiteral("collinear")));
	}

	void testLineAndScatterMesh()
	{
		// Line/Scatter are flat, UNINDEXED vertex lists (drawn as native GL_LINE_STRIP/GL_POINTS - see
		// Plot3DMeshBuilder.h) - one vertex per sample, in input order, values copied through unchanged.
		Plot3DLineData line;
		line.samples.push_back(Plot3DSample{ { 0, 0, 0 }, 1.0 });
		line.samples.push_back(Plot3DSample{ { 0, 0, 10 }, 5.0 });
		line.samples.push_back(Plot3DSample{ { 3, 0, 10 }, 7.0 });
		Plot3DMeshData lineMesh;
		QString error;
		CHECK(buildPlot3DLineMesh(line, lineMesh, &error));
		CHECK(lineMesh.vertexCount() == 3 && lineMesh.indices.empty());
		CHECK(lineMesh.values[0] == 1.0 && lineMesh.values[1] == 5.0 && lineMesh.values[2] == 7.0);
		CHECK(lineMesh.positions[1 * 3 + 2] == 10.0f); // second point's z round-tripped

		Plot3DLineData tooFew;
		tooFew.samples.push_back(Plot3DSample{ { 0, 0, 0 }, 0.0 });
		CHECK(!buildPlot3DLineMesh(tooFew, lineMesh, &error) && !error.isEmpty());

		Plot3DScatterData scatter;
		scatter.samples.push_back(Plot3DSample{ { 0, 0, 0 }, 2.0 });
		scatter.samples.push_back(Plot3DSample{ { 10, 0, 0 }, 4.0 });
		Plot3DMeshData scatterMesh;
		CHECK(buildPlot3DScatterMesh(scatter, scatterMesh, &error));
		CHECK(scatterMesh.vertexCount() == 2 && scatterMesh.indices.empty());
		CHECK(scatterMesh.values[0] == 2.0 && scatterMesh.values[1] == 4.0);

		Plot3DMeshData stemMesh;
		CHECK(buildPlot3DStemMesh(scatter, -3.0, stemMesh, &error));
		CHECK(stemMesh.vertexCount() == 4 && stemMesh.indices.empty());
		CHECK(stemMesh.positions[2] == -3.0f && stemMesh.positions[5] == 0.0f);
		CHECK(stemMesh.positions[8] == -3.0f && stemMesh.positions[11] == 0.0f);
		CHECK(stemMesh.values[0] == 2.0 && stemMesh.values[1] == 2.0 && stemMesh.values[2] == 4.0 && stemMesh.values[3] == 4.0);

		scatter.errors = { 0.5, 1.25 };
		Plot3DMeshData errorBarMesh;
		CHECK(buildPlot3DErrorBarMesh(scatter, errorBarMesh, &error));
		CHECK(errorBarMesh.vertexCount() == 16 && errorBarMesh.indices.empty());
		CHECK(errorBarMesh.positions[2] == -0.5f && errorBarMesh.positions[5] == 0.5f);
		CHECK(errorBarMesh.values.front() == 2.0 && errorBarMesh.values.back() == 4.0);
		Plot3DScatterData incompleteErrors = scatter;
		incompleteErrors.errors.pop_back();
		CHECK(!buildPlot3DErrorBarMesh(incompleteErrors, errorBarMesh, &error) && !error.isEmpty());
		Plot3DScatterData negativeError = scatter;
		negativeError.errors[0] = -0.5;
		CHECK(!buildPlot3DErrorBarMesh(negativeError, errorBarMesh, &error) && !error.isEmpty() && errorBarMesh.empty());

		Plot3DMeshData fillMesh;
		CHECK(buildPlot3DScatterFillMesh(scatter, -3.0, fillMesh, &error));
		CHECK(fillMesh.vertexCount() == 8 && fillMesh.indices.size() == 12);
		CHECK(fillMesh.positions[2] == -3.0f && fillMesh.positions[8] == 0.0f);
		CHECK(fillMesh.values[0] == 2.0 && fillMesh.values[4] == 4.0);
		CHECK(!buildPlot3DScatterFillMesh(scatter, std::numeric_limits<double>::quiet_NaN(), fillMesh, &error) && !error.isEmpty());
		Plot3DScatterData signedFill;
		signedFill.samples.push_back(Plot3DSample{ { 0, 0, 1 }, 1.0 });
		signedFill.samples.push_back(Plot3DSample{ { 1, 0, -1 }, 2.0 });
		CHECK(buildPlot3DScatterFillMesh(signedFill, 0.0, fillMesh, &error));
		for (std::size_t first : { std::size_t(0), std::size_t(4) })
		{
			const float ax = fillMesh.positions[(first + 1) * 3] - fillMesh.positions[first * 3];
			const float az = fillMesh.positions[(first + 1) * 3 + 2] - fillMesh.positions[first * 3 + 2];
			const float bx = fillMesh.positions[(first + 2) * 3] - fillMesh.positions[first * 3];
			const float bz = fillMesh.positions[(first + 2) * 3 + 2] - fillMesh.positions[first * 3 + 2];
			CHECK(az * bx - ax * bz > 0.0f); // geometric normal points +Y above and below the base
		}

		Plot3DLineData fillLine;
		fillLine.samples.push_back(Plot3DSample{ { 0, 0, 1 }, 1.0 });
		fillLine.samples.push_back(Plot3DSample{ { 1, 0, 2 }, 2.0 });
		fillLine.samples.push_back(Plot3DSample{ { 2, 1, 3 }, 3.0 });
		Plot3DMeshData lineFill;
		CHECK(buildPlot3DLineFillMesh(fillLine, -1.0, lineFill, &error));
		CHECK(lineFill.vertexCount() == 6 && lineFill.indices.size() == 24); // 2 segments x 2 windings x 2 triangles
		CHECK(lineFill.positions[2] == 1.0f && lineFill.positions[5] == -1.0f && lineFill.values[0] == 1.0 && lineFill.values[1] == 1.0);
		// A second curve: the ribbon runs between the line and that curve, and the box includes it.
		Plot3DLineData band = fillLine;
		band.fillTo = { 5.0, 6.0, std::numeric_limits<double>::quiet_NaN() }; // a NaN falls back to the base plane
		Plot3DMeshData bandMesh;
		CHECK(buildPlot3DLineFillMesh(band, -1.0, bandMesh, &error));
		CHECK(bandMesh.positions[2] == 1.0f && bandMesh.positions[5] == 5.0f && bandMesh.positions[8] == 2.0f && bandMesh.positions[11] == 6.0f
			&& bandMesh.positions[17] == -1.0f);
		Plot3DDataset bandData;
		bandData.primitive = Plot3DPrimitive::Line;
		bandData.content = band;
		Plot3DMeshOptions bandOptions;
		bandOptions.filled = true;
		bandOptions.baseZ = -1.0;
		double bandLo[3], bandHi[3];
		CHECK(plot3DDatasetBounds(bandData, bandOptions, bandLo, bandHi));
		CHECK(bandLo[2] == 1.0 && bandHi[2] == 6.0); // the second curve, not the base plane (-1), widens the box
		Plot3DCsvTable bandTable;
		CHECK(parsePlot3DCsv(QStringLiteral("x,y,z,z2\n0,0,1,4\n1,0,2,5\n2,0,3,7\n"), {}, bandTable, &error));
		Plot3DColumnMapping bandMapping;
		bandMapping.fillTo = 3;
		Plot3DDataset fromTable;
		CHECK(buildPlot3DDataset(bandTable, Plot3DPrimitive::Line, bandMapping, fromTable, &error));
		CHECK(std::get<Plot3DLineData>(fromTable.content).fillTo == std::vector<double>({ 4.0, 5.0, 7.0 }));
		bandMapping.fillTo = -1;
		CHECK(buildPlot3DDataset(bandTable, Plot3DPrimitive::Line, bandMapping, fromTable, &error));
		CHECK(std::get<Plot3DLineData>(fromTable.content).fillTo.empty());

		// A scatter takes the second column too: ribbons between the points and the second set, the box includes it, a NaN falls back to the base plane.
		Plot3DScatterData scatterBand;
		scatterBand.samples = { Plot3DSample{ { 0, 0, 1 }, 1.0 }, Plot3DSample{ { 1, 0, 2 }, 2.0 }, Plot3DSample{ { 2, 0, 3 }, 3.0 } };
		scatterBand.fillTo = { 5.0, 6.0, std::numeric_limits<double>::quiet_NaN() };
		Plot3DMeshData scatterBandMesh;
		CHECK(buildPlot3DScatterFillMesh(scatterBand, -1.0, scatterBandMesh, &error) && scatterBandMesh.vertexCount() == 12);
		bool lowZs = true; // each ribbon's lower pair of corners sits at the second set's Z (5, 6) or at the base (-1) for the NaN one
		const double expectedLow[3] = { 5.0, 6.0, -1.0 };
		for (int ribbon = 0; ribbon < 3; ++ribbon)
		{
			double lo = 1.0e9;
			for (int corner = 0; corner < 4; ++corner)
				lo = std::min(lo, static_cast<double>(scatterBandMesh.positions[static_cast<std::size_t>(ribbon * 4 + corner) * 3 + 2]));
			lowZs = lowZs && lo <= expectedLow[ribbon] + 1.0e-6 && (ribbon == 2 ? lo == -1.0 : lo == std::min(expectedLow[ribbon], static_cast<double>(1 + ribbon)));
		}
		CHECK(lowZs);
		Plot3DDataset scatterBandData;
		scatterBandData.primitive = Plot3DPrimitive::Scatter;
		scatterBandData.content = scatterBand;
		Plot3DMeshOptions scatterBandOptions;
		scatterBandOptions.filled = true;
		scatterBandOptions.baseZ = -1.0;
		double sbLo[3], sbHi[3];
		CHECK(plot3DDatasetBounds(scatterBandData, scatterBandOptions, sbLo, sbHi) && sbLo[2] == 1.0 && sbHi[2] == 6.0);
		Plot3DDataset scatterFromTable;
		bandMapping.fillTo = 3;
		CHECK(buildPlot3DDataset(bandTable, Plot3DPrimitive::Scatter, bandMapping, scatterFromTable, &error));
		CHECK(std::get<Plot3DScatterData>(scatterFromTable.content).fillTo == std::vector<double>({ 4.0, 5.0, 7.0 }));
		bandMapping.fillTo = -1;
		CHECK(buildPlot3DDataset(bandTable, Plot3DPrimitive::Scatter, bandMapping, scatterFromTable, &error));
		CHECK(std::get<Plot3DScatterData>(scatterFromTable.content).fillTo.empty());

		Plot3DLineData onePoint;
		onePoint.samples.push_back(Plot3DSample{ { 0, 0, 0 }, 0.0 });
		CHECK(!buildPlot3DLineFillMesh(onePoint, 0.0, lineFill, &error) && !error.isEmpty());
		CHECK(!buildPlot3DLineFillMesh(fillLine, std::numeric_limits<double>::quiet_NaN(), lineFill, &error));

		Plot3DScatterData empty;
		CHECK(!buildPlot3DScatterMesh(empty, scatterMesh, &error) && !error.isEmpty());

		Plot3DQuiverData quiver;
		quiver.arrows.push_back(Plot3DQuiver{ { 1, 2, 3 }, { 0, 0, 1 }, 9.0 });
		quiver.arrows.push_back(Plot3DQuiver{ { 4, 5, 6 }, { 1, 0, 0 }, 3.0 });
		Plot3DMeshData quiverMesh;
		CHECK(buildPlot3DQuiverSiteMesh(quiver, quiverMesh, &error));
		CHECK(quiverMesh.vertexCount() == 2 && quiverMesh.indices.empty());
		CHECK(quiverMesh.positions[0] == 1.0f && quiverMesh.positions[3] == 4.0f);
		CHECK(quiverMesh.values[0] == 9.0 && quiverMesh.values[1] == 3.0);

		Plot3DQuiverData emptyQuiver;
		CHECK(!buildPlot3DQuiverSiteMesh(emptyQuiver, quiverMesh, &error) && !error.isEmpty());
	}

	void testBarMesh()
	{
		Plot3DBarData bars;
		bars.bars.push_back({ 1.0, 2.0, -1.0, 4.0, 2.0, 6.0, 9.0 });
		bars.bars.push_back({ -2.0, 0.0, 3.0, -5.0, 1.0, 2.0, -7.0 });
		Plot3DMeshData mesh;
		QString error;
		CHECK(buildPlot3DBarMesh(bars, mesh, &error));
		CHECK(error.isEmpty() && mesh.vertexCount() == 48 && mesh.indices.size() == 72);
		CHECK(mesh.positions[0] == 0.0f && mesh.positions[1] == -1.0f && mesh.positions[2] == -1.0f);
		CHECK(mesh.values[0] == 9.0 && mesh.values[23] == 9.0 && mesh.values[24] == -7.0);
		bool allNormalsUnit = true;
		for (std::size_t i = 0; i < mesh.vertexCount(); ++i)
		{
			const float nx=mesh.normals[i*3], ny=mesh.normals[i*3+1], nz=mesh.normals[i*3+2];
			allNormalsUnit = allNormalsUnit && std::abs(nx*nx + ny*ny + nz*nz - 1.0f) < 1.0e-6f;
		}
		CHECK(allNormalsUnit);
		Plot3DBarData bad;
		bad.bars.push_back({0,0,0,1,0,1,1});
		CHECK(!buildPlot3DBarMesh(bad, mesh, &error) && !error.isEmpty() && mesh.empty());
	}

	void testVoxelGrid()
	{
		Plot3DVoxelData voxels;
		voxels.voxels.push_back({ 4, 2, 7, 1.0 });
		voxels.voxels.push_back({ 6, 3, 8, 0.25 });
		Plot3DVoxelGrid grid;
		QString error;
		CHECK(buildPlot3DVoxelGrid(voxels, grid, &error));
		CHECK(grid.dimX == 3 && grid.dimY == 2 && grid.dimZ == 2);
		CHECK(grid.origin[0] == 4.0f && grid.origin[1] == 2.0f && grid.origin[2] == 7.0f);
		CHECK(grid.values.size() == 12);
		CHECK(grid.values[0] == 1.0f); // (4,2,7), the minimum corner
		CHECK(std::isnan(grid.values[1])); // sparse cells retain transparent NaN values
		CHECK(grid.values[11] == 0.25f); // (6,3,8), the maximum corner

		voxels.voxels.push_back({ 4, 2, 7, 0.5 });
		CHECK(!buildPlot3DVoxelGrid(voxels, grid, &error) && error.contains(QStringLiteral("more than one")));
	}

	void testAxes()
	{
		Plot3DAxisConfig linear;
		linear.targetTicks = 5;
		const auto linearTicks = plot3DGenerateAxisTicks(-2.0, 8.0, linear);
		CHECK(linearTicks.size() == 6 && linearTicks.front().value == -2.0 && linearTicks.back().value == 8.0);

		Plot3DAxisConfig log;
		log.scale = Plot3DAxisScale::Log10;
		const auto logTicks = plot3DGenerateAxisTicks(0.1, 1000.0, log);
		CHECK(logTicks.size() == 5 && std::abs(logTicks[0].value - 0.1) < 1.0e-12
		      && std::abs(logTicks[4].value - 1000.0) < 1.0e-9);
		bool valid = true;
		plot3DTransformAxisValue(0.0, log, &valid);
		CHECK(!valid);

		Plot3DAxisConfig symlog;
		symlog.scale = Plot3DAxisScale::SymLog;
		symlog.symlogLinearThreshold = 2.0;
		const double transformed = plot3DTransformAxisValue(-18.0, symlog, &valid);
		CHECK(valid && std::abs(plot3DInverseAxisValue(transformed, symlog) + 18.0) < 1.0e-10);

		Plot3DAxisConfig logAxis = linear;
		logAxis.scale = Plot3DAxisScale::Log10;
		CHECK(plot3DSameAxisScale(linear, linear) && !plot3DSameAxisScale(linear, logAxis));
		CHECK(plot3DAxisScaleSlope(123.0, linear) == 1.0);
		CHECK(std::abs(plot3DAxisScaleSlope(100.0, logAxis) - 1.0 / (100.0 * std::log(10.0))) < 1.0e-7);
		CHECK(plot3DAxisScaleSlope(-5.0, logAxis) == 1.0); // not placeable: neutral
		Plot3DAxisController controller;
		std::array<Plot3DAxisConfig, 3> axes{ linear, linear, linear };
		const double lo[3] = { 0.0, 10.0, -5.0 }, hi[3] = { 4.0, 20.0, 5.0 };
		Plot3DAxisLayout layout;
		QString error;
		CHECK(controller.buildLayout(axes, lo, hi, layout, &error, QStringLiteral("Axis layout test")));
		CHECK(error.isEmpty() && layout.axisLines.size() == 12 && layout.referencePlanes.size() == 1);
		CHECK(std::abs(layout.referencePlaneOpacity - 0.08f) < 1.0e-6f);
		CHECK(layout.labels.size() == layout.ticks[0].size() + layout.ticks[1].size() + layout.ticks[2].size()
			&& layout.axisTitles.size() == 3);
		CHECK(layout.gridLines.size() == 2 * (layout.ticks[0].size() + layout.ticks[1].size() + layout.ticks[2].size())
			&& layout.title == QStringLiteral("Axis layout test"));
		const double flatLo[3] = { 2.0, 10.0, -5.0 }, flatHi[3] = { 2.0, 20.0, 5.0 };
		CHECK(controller.buildLayout(axes, flatLo, flatHi, layout, &error) && layout.maximum[0] > layout.minimum[0]);
		// A flat axis is padded in proportion to the data (10 % of the largest extent), not by a fixed unit: a 0.1 m wide plane stays a thin box.
		const double smallLo[3] = { 0.0, 0.0, 0.0105 }, smallHi[3] = { 0.1, 0.1, 0.0105 };
		CHECK(controller.buildLayout(axes, smallLo, smallHi, layout, &error));
		CHECK(std::abs((layout.maximum[2] - layout.minimum[2]) - 0.02) < 1.0e-9 && std::abs(layout.minimum[2] - 0.0005) < 1.0e-9);
		const double allFlat[3] = { 3.0, 3.0, 3.0 };
		CHECK(controller.buildLayout(axes, allFlat, allFlat, layout, &error) && std::abs((layout.maximum[0] - layout.minimum[0]) - 2.0) < 1.0e-9);
		controller.setReferencePlanesVisible(true, true, true);
		controller.setReferencePlaneOpacity(0.2f);
		CHECK(controller.buildLayout(axes, lo, hi, layout, &error) && layout.referencePlanes.size() == 3
			&& std::abs(layout.referencePlaneOpacity - 0.2f) < 1.0e-6f);

		axes[0] = log;
		const double badLo[3] = { 0.0, 10.0, -5.0 };
		CHECK(!controller.buildLayout(axes, badLo, hi, layout, &error) && !error.isEmpty());
	}

	// generatePlot3D(): every source builds through its spec, matching what the direct builders produce; the shipped presets all
	// convert to specs that generate; dataset helpers (mesh, bounds) and the source-kind helpers behave.
	void testGenerate()
	{
		QString error;

		// Source kinds: the persisted numbers, the classification helpers.
		Plot3DSourceKind kind = Plot3DSourceKind::Csv;
		CHECK(plot3DSourceFromInt(7, kind) && kind == Plot3DSourceKind::FormulaPathlines && plot3DSourceInt(kind) == 7);
		CHECK(plot3DSourceFromInt(9, kind) && kind == Plot3DSourceKind::ImageSurface && !plot3DSourceFromInt(10, kind) && !plot3DSourceFromInt(-1, kind));
		CHECK(plot3DSourceIsGenerated(Plot3DSourceKind::ImplicitSurface) && !plot3DSourceIsGenerated(Plot3DSourceKind::Csv)
		      && !plot3DSourceIsGenerated(Plot3DSourceKind::CsvTimeSeries));
		CHECK(plot3DSourceIsPathline(Plot3DSourceKind::FormulaPathlines) && plot3DSourceIsPathline(Plot3DSourceKind::CsvTimeSeries)
		      && !plot3DSourceIsPathline(Plot3DSourceKind::FormulaStreamlines));
		CHECK(plot3DSourcePrimitive(Plot3DSourceKind::FormulaVectorField) == Plot3DPrimitive::Quiver
		      && plot3DSourcePrimitive(Plot3DSourceKind::ParametricCurve) == Plot3DPrimitive::Line
		      && plot3DSourcePrimitive(Plot3DSourceKind::ImplicitSurface) == Plot3DPrimitive::Surface);

		// An image plane: a textured quad (4 vertices with UVs, one winding), placed by the plane and the ranges.
		{
			Plot3DGeneratedSpec image;
			image.valid = true;
			image.sourceMode = 9;
			image.imagePath = QString::fromUtf8(__FILE__); // any existing file stands in for a picture: the Core only checks it exists
			image.xMinimum = 1.0; image.xMaximum = 3.0; image.yMinimum = 10.0; image.yMaximum = 14.0; image.zMinimum = -2.0; image.zMaximum = 5.0;
			Plot3DGenerated generated;
			CHECK(generatePlot3D(image, Plot3DPrimitive::Surface, nullptr, nullptr, generated, &error));
			CHECK(!generated.hasDataset && generated.imagePath == image.imagePath && generated.mesh.vertexCount() == 4
			      && generated.mesh.uvs.size() == 8 && generated.mesh.indices.size() == 6 && generated.primitiveMode == Plot3DGl::kTriangles);
			CHECK(generated.mesh.positions[2] == -2.0f && generated.mesh.positions[3] == 3.0f && generated.mesh.positions[4] == 10.0f); // XY: at the Z minimum
			image.imagePlane = 1; // XZ: x by z, at the Y minimum
			CHECK(generatePlot3D(image, Plot3DPrimitive::Surface, nullptr, nullptr, generated, &error));
			CHECK(generated.mesh.positions[1] == 10.0f && generated.mesh.positions[2] == -2.0f && generated.mesh.positions[8] == 5.0f);
			image.imagePlane = 2; // YZ: y by z, at the X minimum
			CHECK(generatePlot3D(image, Plot3DPrimitive::Surface, nullptr, nullptr, generated, &error));
			CHECK(generated.mesh.positions[0] == 1.0f && generated.mesh.positions[1] == 10.0f && generated.mesh.positions[7] == 14.0f);
			image.imagePath = QStringLiteral("no/such/picture.png");
			CHECK(!generatePlot3D(image, Plot3DPrimitive::Surface, nullptr, nullptr, generated, &error) && !error.isEmpty());
			image.imagePath = QString::fromUtf8(__FILE__);
			image.yMaximum = image.yMinimum;
			CHECK(!generatePlot3D(image, Plot3DPrimitive::Surface, nullptr, nullptr, generated, &error) && !error.isEmpty());
		}

		// "Readable from behind": a second quad a hair behind the first, facing the other way, its U mirrored - for an opaque picture only.
		{
			QTemporaryDir dir;
			CHECK(dir.isValid());
			QImage opaque(4, 2, QImage::Format_RGB32);
			opaque.fill(Qt::red);
			QImage see(4, 2, QImage::Format_ARGB32);
			see.fill(QColor(0, 0, 255, 128));
			const QString opaquePath = dir.filePath(QStringLiteral("opaque.png")), seePath = dir.filePath(QStringLiteral("see.png"));
			CHECK(opaque.save(opaquePath) && see.save(seePath));
			Plot3DGeneratedSpec image;
			image.valid = true;
			image.sourceMode = 9;
			image.imagePath = opaquePath;
			image.xMinimum = 0.0; image.xMaximum = 4.0; image.yMinimum = 0.0; image.yMaximum = 2.0; image.zMinimum = 1.0; image.zMaximum = 2.0;
			Plot3DGenerated generated;
			CHECK(generatePlot3D(image, Plot3DPrimitive::Surface, nullptr, nullptr, generated, &error));
			CHECK(generated.mesh.vertexCount() == 4 && generated.mesh.indices.size() == 6 && generated.imageOpacity == 1.0); // off by default: one mirrored back
			image.imageBackReadable = true;
			CHECK(generatePlot3D(image, Plot3DPrimitive::Surface, nullptr, nullptr, generated, &error));
			CHECK(generated.mesh.vertexCount() == 8 && generated.mesh.indices.size() == 12 && generated.mesh.uvs.size() == 16 && generated.mesh.normals.size() == 24);
			if (generated.mesh.vertexCount() == 8)
			{
				CHECK(generated.mesh.normals[2] == 1.0f && generated.mesh.normals[14] == -1.0f); // XY plane: the front faces +Z, the back -Z
				CHECK(generated.mesh.positions[2] == 1.0f && generated.mesh.positions[14] < 1.0f && generated.mesh.positions[14] > 0.99f); // a hair behind (below) the front
				CHECK(generated.mesh.uvs[8] == 1.0f - generated.mesh.uvs[0] && generated.mesh.uvs[9] == generated.mesh.uvs[1]); // U mirrored, V the same
				CHECK(generated.mesh.indices[6] == 4 && generated.mesh.indices[7] == 6 && generated.mesh.indices[8] == 5); // reversed winding
			}
			image.imageOpacity = 0.5; // see-through: both sides would show through each other, so the back stays mirrored
			CHECK(generatePlot3D(image, Plot3DPrimitive::Surface, nullptr, nullptr, generated, &error) && generated.mesh.vertexCount() == 4 && generated.imageOpacity == 0.5);
			image.imageOpacity = 1.0;
			image.imagePath = seePath; // a picture with transparent areas: the same
			CHECK(generatePlot3D(image, Plot3DPrimitive::Surface, nullptr, nullptr, generated, &error) && generated.mesh.vertexCount() == 4 && generated.mesh.indices.size() == 6);
			image.imageOpacity = 7.0; // out of range is clamped
			image.imagePath = opaquePath;
			CHECK(generatePlot3D(image, Plot3DPrimitive::Surface, nullptr, nullptr, generated, &error) && generated.imageOpacity == 1.0);
		}

		// Every preset of every generated source converts to a spec that generates (and the entries mirror the preset lists).
		CHECK(plot3DPresetEntries(Plot3DSourceKind::FormulaSurface).size() == plot3DFormulaPresets().size());
		CHECK(plot3DPresetEntries(Plot3DSourceKind::ParametricSurface).size() == plot3DParametricPresets().size());
		CHECK(plot3DPresetEntries(Plot3DSourceKind::ParametricCurve).size() == plot3DParametricCurvePresets().size());
		CHECK(plot3DPresetEntries(Plot3DSourceKind::FormulaVectorField).size() == plot3DFormulaVectorPresets().size());
		CHECK(plot3DPresetEntries(Plot3DSourceKind::FormulaStreamlines).size() == plot3DFormulaVectorPresets().size());
		CHECK(plot3DPresetEntries(Plot3DSourceKind::ImplicitSurface).size() == plot3DImplicitPresets().size());
		CHECK(plot3DPresetEntries(Plot3DSourceKind::FormulaPathlines).size() == plot3DPathlinePresets().size());
		CHECK(plot3DPresetEntries(Plot3DSourceKind::Csv).isEmpty() && plot3DPresetEntries(Plot3DSourceKind::CsvTimeSeries).isEmpty());
		for (Plot3DSourceKind source : { Plot3DSourceKind::FormulaSurface, Plot3DSourceKind::ParametricSurface, Plot3DSourceKind::ParametricCurve,
			Plot3DSourceKind::FormulaVectorField, Plot3DSourceKind::FormulaStreamlines, Plot3DSourceKind::ImplicitSurface, Plot3DSourceKind::FormulaPathlines })
		{
			const QVector<Plot3DPresetEntry> entries = plot3DPresetEntries(source);
			CHECK(!entries.isEmpty());
			for (int i = 0; i < entries.size(); ++i)
			{
				const Plot3DPresetEntry& entry = entries[i];
				CHECK(!entry.name.isEmpty() && entry.spec.valid && entry.spec.sourceMode == plot3DSourceInt(source) && entry.spec.presetIndex == i);
				Plot3DGenerated generated;
				const bool ok = generatePlot3D(entry.spec, Plot3DPrimitive::Surface, nullptr, nullptr, generated, &error);
				CHECK(ok);
				if (!ok)
					std::fprintf(stderr, "  preset %d of source %d failed: %s\n", i, plot3DSourceInt(source), qPrintable(error));
				CHECK(generated.primitive == plot3DSourcePrimitive(source));
				CHECK(generated.hasDataset ? !generated.dataset.empty() : !generated.mesh.empty());
			}
		}

		// Parity with the direct builders (the Saddle formula surface, the Torus, the Double Gyre pathlines).
		{
			const Plot3DPresetEntry saddle = plot3DPresetEntries(Plot3DSourceKind::FormulaSurface)[1];
			Plot3DGenerated generated;
			CHECK(generatePlot3D(saddle.spec, Plot3DPrimitive::Surface, nullptr, nullptr, generated, &error));
			Plot3DSurfaceData direct;
			QHash<QString, double> parameters;
			for (const auto& parameter : saddle.spec.parameters) parameters.insert(parameter.first.toLower(), parameter.second);
			CHECK(buildPlot3DFormulaSurface(saddle.spec.expression, saddle.spec.xMinimum, saddle.spec.xMaximum, saddle.spec.xSamples,
				saddle.spec.yMinimum, saddle.spec.yMaximum, saddle.spec.ySamples, parameters, direct, &error));
			CHECK(generated.hasDataset && std::get<Plot3DSurfaceData>(generated.dataset.content).samples.size() == direct.samples.size());
			// as a Contour it carries the contour primitive and meshes to GL_LINES
			CHECK(generatePlot3D(saddle.spec, Plot3DPrimitive::Contour, nullptr, nullptr, generated, &error)
			      && generated.primitive == Plot3DPrimitive::Contour && generated.dataset.primitive == Plot3DPrimitive::Contour);
			Plot3DMeshOptions options;
			Plot3DMeshData mesh;
			unsigned int mode = 0;
			CHECK(plot3DMeshForDataset(generated.dataset, options, mesh, mode, &error) && mode == Plot3DGl::kLines && !mesh.empty());
			options.contourProjected = true;
			Plot3DMeshData flat;
			CHECK(plot3DMeshForDataset(generated.dataset, options, flat, mode, &error) && flat.vertexCount() == mesh.vertexCount());

			const Plot3DPresetEntry torus = plot3DPresetEntries(Plot3DSourceKind::ParametricSurface)[0];
			CHECK(generatePlot3D(torus.spec, Plot3DPrimitive::Surface, nullptr, nullptr, generated, &error));
			Plot3DMeshData directTorus;
			parameters.clear();
			for (const auto& parameter : torus.spec.parameters) parameters.insert(parameter.first.toLower(), parameter.second);
			CHECK(buildPlot3DParametricSurface(torus.spec.xExpression, torus.spec.yExpression, torus.spec.zExpression, torus.spec.xMinimum,
				torus.spec.xMaximum, torus.spec.xSamples, torus.spec.yMinimum, torus.spec.yMaximum, torus.spec.ySamples, parameters, directTorus, &error));
			CHECK(!generated.hasDataset && generated.primitiveMode == Plot3DGl::kTriangles && generated.mesh.positions == directTorus.positions
			      && generated.mesh.indices == directTorus.indices);

			const Plot3DPresetEntry gyre = plot3DPresetEntries(Plot3DSourceKind::FormulaPathlines)[1];
			CHECK(generatePlot3D(gyre.spec, Plot3DPrimitive::Surface, nullptr, nullptr, generated, &error));
			Plot3DMeshData directGyre;
			parameters.clear();
			for (const auto& parameter : gyre.spec.parameters) parameters.insert(parameter.first.toLower(), parameter.second);
			CHECK(buildPlot3DFormulaPathlines(gyre.spec.xExpression, gyre.spec.yExpression, gyre.spec.zExpression, gyre.spec.xMinimum, gyre.spec.xMaximum,
				gyre.spec.yMinimum, gyre.spec.yMaximum, gyre.spec.ySamples, gyre.spec.zMinimum, gyre.spec.zMaximum, gyre.spec.zSamples, parameters, directGyre, &error));
			CHECK(generated.primitiveMode == Plot3DGl::kLines && generated.mesh.positions == directGyre.positions && generated.mesh.values == directGyre.values);
		}

		// The CSV time series needs its table; the CSV source is not generated.
		{
			Plot3DGeneratedSpec spec;
			spec.valid = true;
			spec.sourceMode = plot3DSourceInt(Plot3DSourceKind::CsvTimeSeries);
			spec.ySamples = 3; spec.zSamples = 40;
			Plot3DGenerated generated;
			CHECK(!generatePlot3D(spec, Plot3DPrimitive::Line, nullptr, nullptr, generated, &error) && !error.isEmpty());

			QString csv = QStringLiteral("t,x,y,u,v,w\n");
			for (int t = 0; t <= 4; ++t)
				for (int x = 0; x <= 4; ++x)
					for (int y = 0; y <= 2; ++y)
						csv += QStringLiteral("%1,%2,%3,%1,0,0\n").arg(t).arg(x).arg(y);
			Plot3DCsvTable table;
			CHECK(parsePlot3DCsv(csv, {}, table, &error));
			Plot3DTimeSeriesColumns columns;
			columns.time = 0; columns.x = 1; columns.y = 2; columns.u = 3; columns.v = 4; columns.w = 5;
			CHECK(generatePlot3D(spec, Plot3DPrimitive::Line, &table, &columns, generated, &error)
			      && generated.primitiveMode == Plot3DGl::kLines && !generated.mesh.empty());

			spec.sourceMode = plot3DSourceInt(Plot3DSourceKind::Csv);
			CHECK(!generatePlot3D(spec, Plot3DPrimitive::Surface, &table, &columns, generated, &error));
			spec.sourceMode = 99;
			CHECK(!generatePlot3D(spec, Plot3DPrimitive::Surface, &table, &columns, generated, &error));
		}

		// A broken definition reports its message; the mesh bounds helper agrees with the vertices.
		{
			Plot3DGeneratedSpec spec = plot3DPresetEntries(Plot3DSourceKind::FormulaSurface)[0].spec;
			spec.expression = QStringLiteral("nosuch*x");
			Plot3DGenerated generated;
			CHECK(!generatePlot3D(spec, Plot3DPrimitive::Surface, nullptr, nullptr, generated, &error) && error.contains(QStringLiteral("Unknown")));

			Plot3DMeshData mesh;
			mesh.positions = { 0, 0, 0, 2, -1, 5 };
			mesh.normals = { 0, 0, 1, 0, 0, 1 };
			mesh.values = { 0.0, 1.0 };
			double minimum[3], maximum[3];
			CHECK(plot3DMeshBounds(mesh, minimum, maximum) && minimum[0] == 0.0 && maximum[0] == 2.0 && minimum[1] == -1.0 && maximum[2] == 5.0);
			CHECK(!plot3DMeshBounds(Plot3DMeshData(), minimum, maximum));
		}

		// Dataset helpers: scatter variants pick their mesh and widen the bounds to the base plane; bars scale; renderer plots have no mesh.
		{
			Plot3DScatterData scatter;
			scatter.samples = { { { 0.0, 0.0, 1.0 }, 1.0 }, { { 1.0, 1.0, 2.0 }, 2.0 } };
			Plot3DDataset dataset;
			dataset.primitive = Plot3DPrimitive::Scatter;
			dataset.content = scatter;
			Plot3DMeshOptions options;
			Plot3DMeshData mesh;
			unsigned int mode = 99;
			CHECK(plot3DMeshForDataset(dataset, options, mesh, mode, &error) && mode == Plot3DGl::kPoints);
			options.stems = true; options.baseZ = -3.0;
			CHECK(plot3DMeshForDataset(dataset, options, mesh, mode, &error) && mode == Plot3DGl::kLines);
			double minimum[3], maximum[3];
			CHECK(plot3DDatasetBounds(dataset, options, minimum, maximum) && minimum[2] == -3.0 && maximum[2] == 2.0);
			options.stems = false; options.filled = true;
			CHECK(plot3DMeshForDataset(dataset, options, mesh, mode, &error) && mode == Plot3DGl::kTriangles);
			options.filled = false;
			CHECK(plot3DDatasetBounds(dataset, options, minimum, maximum) && minimum[2] == 1.0); // no base plane without stems / fill

			Plot3DBarData bars;
			bars.bars.push_back({ 0.0, 0.0, 0.0, 2.0, 1.0, 1.0, 2.0 });
			Plot3DDataset barDataset;
			barDataset.primitive = Plot3DPrimitive::Bar;
			barDataset.content = bars;
			Plot3DMeshOptions barOptions;
			barOptions.barWidthScale = 2.0;
			CHECK(plot3DDatasetBounds(barDataset, barOptions, minimum, maximum) && minimum[0] == -1.0 && maximum[0] == 1.0); // 1.0 wide, scaled 2x
			CHECK(plot3DMeshForDataset(barDataset, barOptions, mesh, mode, &error) && mode == Plot3DGl::kTriangles);

			Plot3DDataset quiver;
			quiver.primitive = Plot3DPrimitive::Quiver;
			quiver.content = Plot3DQuiverData();
			CHECK(!plot3DMeshForDataset(quiver, Plot3DMeshOptions(), mesh, mode, &error));
			// a primitive with the wrong data is rejected, not read as garbage
			Plot3DDataset mismatched;
			mismatched.primitive = Plot3DPrimitive::Surface;
			mismatched.content = Plot3DLineData();
			CHECK(!plot3DMeshForDataset(mismatched, Plot3DMeshOptions(), mesh, mode, &error));
		}
	}

	void testSessionRoundTrip()
	{
		// A fully-populated session: every scalar control, a Bar source and Contour source (the two table-shaped
		// blobs), CSV edit state, and per-vertex colour data with a deliberately invalid entry.
		Plot3DSession session;
		session.meshUuid = QUuid::createUuid();
		session.markerMeshUuid = QUuid::createUuid();
		session.name = QStringLiteral("Plot3D Scatter");
		session.title = QStringLiteral("Cluster étude");
		session.primitive = Plot3DPrimitive::Scatter;
		session.axes[0].label = QStringLiteral("Time (s)");
		session.axes[1].scale = Plot3DAxisScale::Log10;
		session.axes[2].scale = Plot3DAxisScale::SymLog;
		session.axes[2].symlogLinearThreshold = 2.5;
		session.axes[2].automaticRange = false;
		session.axes[2].minimum = -4.0;
		session.axes[2].maximum = 9.0;
		session.axes[2].targetTicks = 8;
		session.dataMinimum = { -1.0, 0.5, -2.0 };
		session.dataMaximum = { 3.0, 7.5, 6.0 };
		session.values = { 0.25f, 0.5f, 0.75f, 0.0f };
		session.valid = { true, true, true, false };
		session.markerValues = { 1.0f, 2.0f };
		session.markerValid = { true, false };
		session.dataMinimumValue = 0.25f; session.dataMaximumValue = 0.75f;
		session.colourMinimum = 0.1f; session.colourMaximum = 0.9f;
		session.colormap = 1; session.bands = 6;
		session.lineWidth = 2.5f; session.markerSize = 7.0f; session.arrowScale = 1.5f;
		session.barWidthScale = 0.5f; session.barDepthScale = 2.0f;
		session.isStem = true; session.isErrorBars = false; session.isFilledScatter = true;
		session.scatterBaseZ = -3.5;
		session.contourLevels = 14;
		session.contourProjected = true;
		session.generated.valid = true; session.generated.sourceMode = 7; session.generated.title = QStringLiteral("Wave"); session.generated.presetIndex = 2;
		session.generated.expression = QStringLiteral("a*x"); session.generated.xExpression = QStringLiteral("-y*(1+a*sin(t))");
		session.generated.yExpression = QStringLiteral("x"); session.generated.zExpression = QStringLiteral("0");
		session.generated.xMinimum = -4.5; session.generated.xMaximum = 4.5; session.generated.yMinimum = -2; session.generated.yMaximum = 2;
		session.generated.zMinimum = 0; session.generated.zMaximum = 12; session.generated.xSamples = 33; session.generated.ySamples = 9;
		session.generated.zSamples = 240;
		session.generated.fillExpression = QStringLiteral("sin(t)");
		session.generated.imageOpacity = 0.4; session.generated.imageBackReadable = true; // only written for an image plane (source 9): checked below
		session.generated.parameters = { { QStringLiteral("s"), 0.6 }, { QStringLiteral("a"), 0.8 } };
		session.automaticColourRange = false;
		session.contourOverlayMode = 2; session.contourOverlayLevels = 7;
		session.contourOverlayMeshUuid = QUuid::createUuid();
		session.axesVisible = false;
		session.referencePlanes = { false, true, true };
		session.referencePlaneOpacity = 0.4f;
		session.textLabels.push_back(Plot3DTextLabel{ QStringLiteral("peak"), 1.5, -2.0, 3.25 });
		session.textLabels.push_back(Plot3DTextLabel{ QStringLiteral("second"), 0.0, 0.0, 0.0 });
		session.editableCsv = true;
		session.csvSource = QStringLiteral("x;y;z\n1;2;3\n\"quoted;cell\";5;6\n");
		session.csvOptions.delimiter = QLatin1Char(';');
		session.csvOptions.firstRowIsHeader = false;
		session.columnMapping.x = 2; session.columnMapping.value = 4; session.columnMapping.error = 5;
		session.columnMapping.base = 1; session.columnMapping.width = 7; session.columnMapping.depth = 8;
		Plot3DBar bar; bar.x = 1; bar.y = 2; bar.base = -1; bar.height = 4; bar.width = 0.6; bar.depth = 0.9; bar.value = 3.5;
		session.barSource.bars = { bar, bar };
		session.barSource.bars[1].x = 9.0;
		session.contourSource.samples.push_back(Plot3DSample{ { 1, 2, 3 }, 4.0 });
		session.contourSource.samples.push_back(Plot3DSample{ { 5, 6, 7 }, 8.0 });

		Plot3DRendererPayload payload;
		payload.hasGlyphs = true;
		payload.glyphVectors = { 1, 0, 0, 0, 2, 0 };
		payload.glyphValues = { 1.0f, 2.0f };
		payload.glyphReferenceLength = 0.75f; payload.glyphFieldMinimum = 1.0f; payload.glyphFieldMaximum = 2.0f;
		payload.hasVolume = true;
		payload.volumeDimensions[0] = 2; payload.volumeDimensions[1] = 2; payload.volumeDimensions[2] = 1;
		payload.volumeValues = { 0.0f, 0.5f, 1.0f, 0.25f };
		payload.volumeOrigin[0] = 3.0f; payload.volumeVoxelSize[2] = 2.0f;
		payload.volumeFieldMinimum = 0.0f; payload.volumeFieldMaximum = 1.0f;
		payload.volumeLabel = QStringLiteral("Occupancy");

		std::vector<QByteArray> blobs;
		const QJsonObject json = plot3DSessionToJson(session, payload, blobs);
		CHECK(!blobs.empty());

		Plot3DSession restored;
		Plot3DRendererPayload restoredPayload;
		QString error;
		CHECK(plot3DSessionFromJson(json, blobs, restored, restoredPayload, &error) && error.isEmpty());
		CHECK(restored.meshUuid == session.meshUuid && restored.markerMeshUuid == session.markerMeshUuid);
		CHECK(restored.name == session.name && restored.title == session.title && restored.primitive == session.primitive);
		CHECK(restored.axes[0].label == session.axes[0].label && restored.axes[1].scale == Plot3DAxisScale::Log10);
		CHECK(restored.axes[2].scale == Plot3DAxisScale::SymLog && restored.axes[2].symlogLinearThreshold == 2.5);
		CHECK(!restored.axes[2].automaticRange && restored.axes[2].minimum == -4.0 && restored.axes[2].maximum == 9.0
			&& restored.axes[2].targetTicks == 8);
		CHECK(restored.dataMinimum == session.dataMinimum && restored.dataMaximum == session.dataMaximum);
		CHECK(restored.values == session.values && restored.valid == session.valid);
		CHECK(restored.markerValues == session.markerValues && restored.markerValid == session.markerValid);
		CHECK(restored.colourMinimum == 0.1f && restored.colourMaximum == 0.9f && restored.colormap == 1 && restored.bands == 6);
		CHECK(restored.lineWidth == 2.5f && restored.markerSize == 7.0f && restored.arrowScale == 1.5f);
		CHECK(restored.barWidthScale == 0.5f && restored.barDepthScale == 2.0f);
		CHECK(restored.isStem && !restored.isErrorBars && restored.isFilledScatter && restored.scatterBaseZ == -3.5);
		CHECK(restored.generated.fillExpression == QStringLiteral("sin(t)") && restored.generated.imageOpacity == 1.0 && !restored.generated.imageBackReadable);
		{
			// an image plane keeps its opacity and back side; an older file without them reads as opaque with a mirrored back
			Plot3DSession picture = session;
			picture.generated.sourceMode = 9;
			std::vector<QByteArray> pictureBlobs;
			const QJsonObject pictureJson = plot3DSessionToJson(picture, payload, pictureBlobs);
			Plot3DSession pictureBack;
			Plot3DRendererPayload pictureBackPayload;
			QString pictureError;
			CHECK(plot3DSessionFromJson(pictureJson, pictureBlobs, pictureBack, pictureBackPayload, &pictureError));
			CHECK(pictureBack.generated.imageOpacity == 0.4 && pictureBack.generated.imageBackReadable);
		}
		CHECK(restored.contourLevels == 14 && restored.contourProjected && restored.generated.valid && restored.generated.sourceMode == 7 && restored.generated.presetIndex == 2
		      && restored.generated.title == QLatin1String("Wave") && restored.generated.xExpression == session.generated.xExpression
		      && restored.generated.xMinimum == -4.5 && restored.generated.zSamples == 240 && restored.generated.parameters == session.generated.parameters
		      && !restored.automaticColourRange && restored.contourOverlayMode == 2 && restored.contourOverlayLevels == 7
		      && restored.contourOverlayMeshUuid == session.contourOverlayMeshUuid && !restored.axesVisible && restored.referencePlaneOpacity == 0.4f);
		CHECK(restored.textLabels.size() == 2 && restored.textLabels[0].text == QStringLiteral("peak") && restored.textLabels[0].x == 1.5
			&& restored.textLabels[0].y == -2.0 && restored.textLabels[0].z == 3.25 && restored.textLabels[1].text == QStringLiteral("second"));
		CHECK(restored.referencePlanes == session.referencePlanes);
		CHECK(restored.editableCsv && restored.csvSource == session.csvSource);
		CHECK(restored.csvOptions.delimiter == QLatin1Char(';') && !restored.csvOptions.firstRowIsHeader);
		CHECK(restored.columnMapping.x == 2 && restored.columnMapping.value == 4 && restored.columnMapping.error == 5
			&& restored.columnMapping.base == 1 && restored.columnMapping.width == 7 && restored.columnMapping.depth == 8);
		CHECK(restored.barSource.bars.size() == 2 && restored.barSource.bars[1].x == 9.0
			&& restored.barSource.bars[0].width == 0.6 && restored.barSource.bars[0].depth == 0.9
			&& restored.barSource.bars[0].base == -1.0 && restored.barSource.bars[0].value == 3.5);
		CHECK(restored.contourSource.samples.size() == 2 && restored.contourSource.samples[1].position.z == 7.0
			&& restored.contourSource.samples[1].value == 8.0);
		CHECK(restoredPayload.hasGlyphs && restoredPayload.glyphVectors == payload.glyphVectors
			&& restoredPayload.glyphValues == payload.glyphValues && restoredPayload.glyphReferenceLength == 0.75f
			&& restoredPayload.glyphFieldMinimum == 1.0f && restoredPayload.glyphFieldMaximum == 2.0f);
		CHECK(restoredPayload.hasVolume && restoredPayload.volumeValues == payload.volumeValues
			&& restoredPayload.volumeDimensions[0] == 2 && restoredPayload.volumeDimensions[2] == 1
			&& restoredPayload.volumeOrigin[0] == 3.0f && restoredPayload.volumeVoxelSize[2] == 2.0f
			&& restoredPayload.volumeLabel == QStringLiteral("Occupancy"));

		// A plot with neither renderer payload must not invent one.
		Plot3DSession plain = session;
		plain.editableCsv = false; plain.csvSource.clear();
		plain.barSource.bars.clear(); plain.contourSource.samples.clear();
		std::vector<QByteArray> plainBlobs;
		const QJsonObject plainJson = plot3DSessionToJson(plain, Plot3DRendererPayload(), plainBlobs);
		Plot3DSession plainBack; Plot3DRendererPayload plainPayload;
		CHECK(plot3DSessionFromJson(plainJson, plainBlobs, plainBack, plainPayload, &error));
		CHECK(!plainPayload.hasGlyphs && !plainPayload.hasVolume && !plainBack.editableCsv);
		CHECK(plainBack.barSource.bars.empty() && plainBack.contourSource.samples.empty());

		// Damaged input is rejected rather than half-restored: a missing blob, a corrupt blob, an unknown plot
		// type, a missing identity, and inconsistent array sizes.
		Plot3DSession bad; Plot3DRendererPayload badPayload;
		CHECK(!plot3DSessionFromJson(json, std::vector<QByteArray>(), bad, badPayload, &error) && !error.isEmpty());
		std::vector<QByteArray> corrupt = blobs;
		for (QByteArray& blob : corrupt)
			blob = QByteArray("not a compressed blob");
		error.clear();
		CHECK(!plot3DSessionFromJson(json, corrupt, bad, badPayload, &error) && !error.isEmpty());
		QJsonObject unknown = json; unknown.insert(QStringLiteral("primitive"), 99);
		CHECK(!plot3DSessionFromJson(unknown, blobs, bad, badPayload, &error));
		QJsonObject anonymous = json; anonymous.insert(QStringLiteral("meshUuid"), QString());
		CHECK(!plot3DSessionFromJson(anonymous, blobs, bad, badPayload, &error));
		Plot3DRendererPayload mismatched = payload;
		mismatched.volumeDimensions[0] = 5; // 5*2*1 != 4 stored values
		std::vector<QByteArray> mismatchedBlobs;
		const QJsonObject mismatchedJson = plot3DSessionToJson(session, mismatched, mismatchedBlobs);
		CHECK(!plot3DSessionFromJson(mismatchedJson, mismatchedBlobs, bad, badPayload, &error) && !error.isEmpty());

		// A Quiver or Voxel plot IS its renderer data (the mesh is only an anchor / bounds proxy), so a session for one
		// that carries no arrows / volume must be rejected rather than reopen as a successfully empty plot.
		Plot3DSession quiver = plain;
		quiver.primitive = Plot3DPrimitive::Quiver;
		std::vector<QByteArray> quiverBlobs;
		const QJsonObject quiverNoGlyphs = plot3DSessionToJson(quiver, Plot3DRendererPayload(), quiverBlobs);
		error.clear();
		CHECK(!plot3DSessionFromJson(quiverNoGlyphs, quiverBlobs, bad, badPayload, &error) && !error.isEmpty());
		Plot3DRendererPayload arrowsOnly;
		arrowsOnly.hasGlyphs = true;
		arrowsOnly.glyphVectors = { 0, 0, 1 };
		arrowsOnly.glyphValues = { 1.0f };
		quiverBlobs.clear();
		const QJsonObject quiverWithGlyphs = plot3DSessionToJson(quiver, arrowsOnly, quiverBlobs);
		CHECK(plot3DSessionFromJson(quiverWithGlyphs, quiverBlobs, bad, badPayload, &error) && badPayload.hasGlyphs);

		Plot3DSession voxel = plain;
		voxel.primitive = Plot3DPrimitive::Voxel;
		std::vector<QByteArray> voxelBlobs;
		const QJsonObject voxelNoVolume = plot3DSessionToJson(voxel, Plot3DRendererPayload(), voxelBlobs);
		error.clear();
		CHECK(!plot3DSessionFromJson(voxelNoVolume, voxelBlobs, bad, badPayload, &error) && !error.isEmpty());
		Plot3DRendererPayload volumeOnly;
		volumeOnly.hasVolume = true;
		volumeOnly.volumeDimensions[0] = 1; volumeOnly.volumeDimensions[1] = 1; volumeOnly.volumeDimensions[2] = 2;
		volumeOnly.volumeValues = { 0.5f, 1.0f };
		voxelBlobs.clear();
		const QJsonObject voxelWithVolume = plot3DSessionToJson(voxel, volumeOnly, voxelBlobs);
		CHECK(plot3DSessionFromJson(voxelWithVolume, voxelBlobs, bad, badPayload, &error) && badPayload.hasVolume);
	}

	void testFormula()
	{
		double value = 0.0;
		QString error;
		QHash<QString, double> parameters;
		parameters.insert(QStringLiteral("a"), 4.0);
		CHECK(evaluatePlot3DFormula(QStringLiteral("a*x + y^2"), 2.0, 3.0, parameters, value, &error)
			&& std::abs(value - 17.0) < 1.0e-12);
		Plot3DSurfaceData surface;
		CHECK(buildPlot3DFormulaSurface(QStringLiteral("sin(x)*cos(y)"), -1.0, 1.0, 5, -2.0, 2.0, 4, {}, surface, &error)
			&& surface.samples.size() == 20);
		CHECK(!evaluatePlot3DFormula(QStringLiteral("unknown + x"), 0.0, 0.0, {}, value, &error) && error.contains(QStringLiteral("Unknown")));
		CHECK(evaluatePlot3DFormula(QStringLiteral("sign(-2) + cosh(0) + asin(0)"), 0.0, 0.0, {}, value, &error)
			&& std::abs(value) < 1.0e-12);
		CHECK(evaluatePlot3DFormula3D(QStringLiteral("x+y+z"), 1.0, 2.0, 3.0, {}, value, &error)
			&& std::abs(value - 6.0) < 1.0e-12);

		Plot3DMeshData parametric;
		parameters.clear();
		parameters.insert(QStringLiteral("r"), 3.0);
		parameters.insert(QStringLiteral("a"), 1.0);
		CHECK(buildPlot3DParametricSurface(QStringLiteral("(r+a*cos(v))*cos(u)"),
			QStringLiteral("(r+a*cos(v))*sin(u)"), QStringLiteral("a*sin(v)"),
			0.0, 6.283185307179586, 11, 0.0, 6.283185307179586, 7, parameters, parametric, &error)
			&& parametric.vertexCount() == 77 && parametric.indices.size() == 360 && parametric.normals.size() == 231);
		CHECK(!buildPlot3DParametricSurface(QStringLiteral("u"), QStringLiteral("v"), QStringLiteral("missing"),
			0.0, 1.0, 2, 0.0, 1.0, 2, {}, parametric, &error) && error.contains(QStringLiteral("Unknown")));

		Plot3DLineData curve;
		CHECK(buildPlot3DParametricCurve(QStringLiteral("r*cos(t)"), QStringLiteral("r*sin(t)"), QStringLiteral("t"),
			0.0, 6.283185307179586, 17, parameters, curve, &error)
			&& curve.samples.size() == 17 && std::abs(curve.samples.front().position.x - 3.0) < 1.0e-12
			&& std::abs(curve.samples.back().position.z - 6.283185307179586) < 1.0e-12);
		CHECK(!buildPlot3DParametricCurve(QStringLiteral("t"), QStringLiteral("0"), QStringLiteral("missing"),
			0.0, 1.0, 2, {}, curve, &error) && error.contains(QStringLiteral("Unknown")));
		// A second z(t) over the same x(t), y(t): one fill-to value per sample; an empty expression leaves none; a bad one fails with its t.
		CHECK(buildPlot3DParametricCurve(QStringLiteral("t"), QStringLiteral("0"), QStringLiteral("t"), 0.0, 4.0, 5, {}, curve, &error, QStringLiteral("t*t"))
			&& curve.fillTo.size() == 5 && curve.fillTo[0] == 0.0 && curve.fillTo[3] == 9.0 && curve.fillTo[4] == 16.0);
		CHECK(buildPlot3DParametricCurve(QStringLiteral("t"), QStringLiteral("0"), QStringLiteral("t"), 0.0, 4.0, 5, {}, curve, &error, QStringLiteral("  ")) && curve.fillTo.empty());
		CHECK(!buildPlot3DParametricCurve(QStringLiteral("t"), QStringLiteral("0"), QStringLiteral("t"), 0.0, 4.0, 5, {}, curve, &error, QStringLiteral("nope(t)"))
			&& !error.isEmpty() && curve.fillTo.empty() && curve.samples.empty());

		Plot3DQuiverData field;
		CHECK(buildPlot3DFormulaVectorField(QStringLiteral("-y"), QStringLiteral("x"), QStringLiteral("0"),
			-1.0, 1.0, 3, -2.0, 2.0, 5, {}, field, &error)
			&& field.arrows.size() == 15 && field.arrows.front().position.x == -1.0
			&& field.arrows.front().vector.x == 2.0);
		CHECK(!buildPlot3DFormulaVectorField(QStringLiteral("unknown"), QStringLiteral("0"), QStringLiteral("0"),
			0.0, 1.0, 2, 0.0, 1.0, 2, {}, field, &error) && error.contains(QStringLiteral("Unknown")));

		Plot3DMeshData streamlines;
		CHECK(buildPlot3DFormulaStreamlines(QStringLiteral("1"), QStringLiteral("0"), QStringLiteral("0"),
			-1.0, 1.0, -1.0, 1.0, 5, {}, streamlines, &error));
		CHECK(!streamlines.empty() && streamlines.indices.empty() && streamlines.vertexCount() % 2 == 0
			&& streamlines.normals.size() == streamlines.positions.size());
		bool unitMagnitude = true;
		for (double magnitude : streamlines.values)
			unitMagnitude = unitMagnitude && std::abs(magnitude - 1.0) < 1.0e-12;
		CHECK(unitMagnitude);
		CHECK(!buildPlot3DFormulaStreamlines(QStringLiteral("1"), QStringLiteral("0"), QStringLiteral("0"),
			1.0, -1.0, -1.0, 1.0, 5, {}, streamlines, &error) && !error.isEmpty());
		CHECK(!buildPlot3DFormulaStreamlines(QStringLiteral("unknown"), QStringLiteral("0"), QStringLiteral("0"),
			-1.0, 1.0, -1.0, 1.0, 5, {}, streamlines, &error) && error.contains(QStringLiteral("Unknown")));

		// ---- pathlines: particles carried through a time-dependent field --------------------------------------------------------
		// `t` is the time in the 4-D evaluator but still an alias of x in the 3-D one (parametric curves rely on that).
		double timeValue = 0.0;
		CHECK(evaluatePlot3DFormula4D(QStringLiteral("t + 10*x"), 2.0, 0.0, 0.0, 5.0, {}, timeValue, &error) && timeValue == 25.0);
		CHECK(evaluatePlot3DFormula3D(QStringLiteral("t"), 2.0, 0.0, 0.0, {}, timeValue, &error) && timeValue == 2.0);

		// Steady rigid rotation (u = -y, v = x) seeded at y = -2, -1, 0, 1, 2 along x = 0: an RK4 pathline keeps its radius, so every
		// vertex lies on a circle of radius 0, 1 or 2 (the seed at the origin is stationary and draws nothing), and the vertex values
		// are the times, rising from tMinimum.
		Plot3DMeshData rotation;
		CHECK(buildPlot3DFormulaPathlines(QStringLiteral("-y"), QStringLiteral("x"), QStringLiteral("0"),
			-3.0, 3.0, -2.0, 2.0, 5, 0.0, 3.0, 300, {}, rotation, &error));
		CHECK(!rotation.empty() && rotation.indices.empty() && rotation.vertexCount() % 2 == 0
			&& rotation.normals.size() == rotation.positions.size() && rotation.values.size() == rotation.vertexCount());
		double firstTime = 1.0e9, lastTime = -1.0e9;
		for (std::size_t v = 0; v < rotation.vertexCount(); ++v)
		{
			firstTime = std::min(firstTime, rotation.values[v]);
			lastTime = std::max(lastTime, rotation.values[v]);
			const double x = rotation.positions[v * 3], y = rotation.positions[v * 3 + 1];
			const double radius = std::sqrt(x * x + y * y);
			CHECK(std::abs(radius - std::round(radius)) < 1.0e-3);
		}
		CHECK(std::abs(firstTime) < 1.0e-9 && std::abs(lastTime - 3.0) < 1.0e-9);
		CHECK(std::abs(rotation.positions[0]) < 1.0e-6f && std::abs(rotation.positions[1] + 2.0f) < 1.0e-6f); // first vertex is the first seed

		// Playback helpers: the rotation trails split back into one trail per seed that moved, every trail starts at the first time, the
		// segment counts add up, and the number of elapsed segments grows with time from none to all.
		{
			std::vector<float> vertexTimes(rotation.values.begin(), rotation.values.end());
			const std::vector<Plot3DPathlineTrail> trails = plot3DPathlineTrails(vertexTimes);
			CHECK(trails.size() >= 2 && trails.size() <= 5);
			int segmentTotal = 0;
			for (const Plot3DPathlineTrail& trail : trails)
			{
				segmentTotal += trail.segments;
				CHECK(trail.segments > 0 && vertexTimes[static_cast<std::size_t>(trail.firstVertex)] == 0.0f);
				CHECK(plot3DElapsedSegments(vertexTimes, trail, -1.0) == 0 && plot3DElapsedSegments(vertexTimes, trail, 0.0) == 0);
				CHECK(plot3DElapsedSegments(vertexTimes, trail, 1.0e9) == trail.segments);
				int previous = 0;
				for (double now = 0.0; now <= 3.0; now += 0.25)
				{
					const int elapsed = plot3DElapsedSegments(vertexTimes, trail, now);
					CHECK(elapsed >= previous && elapsed <= trail.segments);
					previous = elapsed;
				}
				CHECK(plot3DElapsedSegments(vertexTimes, trail, 1.5) > 0); // 150 of 300 steps, so about half the trail
			}
			CHECK(segmentTotal == static_cast<int>(rotation.vertexCount() / 2));
			CHECK(plot3DPathlineTrails({}).empty());
		}

		// A field that depends on time only: u = t, so x(t) = x0 + t^2 / 2 exactly (RK4 integrates a quadratic exactly).
		Plot3DMeshData accelerating;
		CHECK(buildPlot3DFormulaPathlines(QStringLiteral("t"), QStringLiteral("0"), QStringLiteral("0"),
			-1.0, 5.0, -1.0, 1.0, 2, 0.0, 2.0, 20, {}, accelerating, &error));
		bool foundEnd = false;
		for (std::size_t v = 0; v < accelerating.vertexCount(); ++v)
			if (std::abs(accelerating.values[v] - 2.0) < 1.0e-9 && std::abs(accelerating.positions[v * 3 + 1] + 1.0f) < 1.0e-6f)
			{
				foundEnd = true; // the seed at y = -1 (x0 = 2) after t = 2
				CHECK(std::abs(accelerating.positions[v * 3] - (2.0f + 2.0f)) < 1.0e-4f);
			}
		CHECK(foundEnd);

		// A trail ends where its particle leaves the x / y domain, and bad input is rejected with a message.
		Plot3DMeshData leaving;
		CHECK(buildPlot3DFormulaPathlines(QStringLiteral("1"), QStringLiteral("0"), QStringLiteral("0"),
			-1.0, 1.0, -1.0, 1.0, 2, 0.0, 10.0, 100, {}, leaving, &error));
		for (std::size_t v = 0; v < leaving.vertexCount(); ++v)
			CHECK(leaving.positions[v * 3] <= 1.0f + 1.0e-6f); // x never goes past the right edge
		CHECK(!buildPlot3DFormulaPathlines(QStringLiteral("1"), QStringLiteral("0"), QStringLiteral("0"),
			-1.0, 1.0, -1.0, 1.0, 2, 5.0, 5.0, 100, {}, leaving, &error) && !error.isEmpty()); // empty time range
		CHECK(!buildPlot3DFormulaPathlines(QStringLiteral("1"), QStringLiteral("0"), QStringLiteral("0"),
			-1.0, 1.0, -1.0, 1.0, 1, 0.0, 1.0, 100, {}, leaving, &error)); // too few seeds
		CHECK(!buildPlot3DFormulaPathlines(QStringLiteral("nosuch"), QStringLiteral("0"), QStringLiteral("0"),
			-1.0, 1.0, -1.0, 1.0, 2, 0.0, 1.0, 10, {}, leaving, &error) && error.contains(QStringLiteral("Unknown")));

		// Every shipped pathline preset must evaluate and produce trails with its own defaults.
		for (const Plot3DPathlinePreset& preset : plot3DPathlinePresets())
		{
			QHash<QString, double> presetParameters;
			for (const Plot3DFormulaParameter& parameter : preset.parameters)
				presetParameters.insert(parameter.name.toLower(), parameter.value);
			Plot3DMeshData presetTrails;
			CHECK(buildPlot3DFormulaPathlines(preset.uExpression, preset.vExpression, preset.wExpression, preset.xMinimum, preset.xMaximum,
				preset.yMinimum, preset.yMaximum, preset.seeds, preset.tMinimum, preset.tMaximum, preset.steps, presetParameters, presetTrails, &error));
			CHECK(!presetTrails.empty());
		}
		CHECK(plot3DPathlinePresets().size() >= 4);

		// ---- CSV time-series pathlines: a vector field sampled on a regular (t, x, y) grid ----------------------------------------------
		{
			// u = t on a 5 x 3 x 5 grid (x 0..4, y 0..2, t 0..4): linear in t, so the time interpolation is exact and x(t) = 2 + t^2 / 2.
			QString csv = QStringLiteral("t,x,y,u,v,w\n");
			for (int t = 0; t <= 4; ++t)
				for (int x = 0; x <= 4; ++x)
					for (int y = 0; y <= 2; ++y)
						csv += QStringLiteral("%1,%2,%3,%1,0,0\n").arg(t).arg(x).arg(y);
			Plot3DCsvTable table;
			CHECK(parsePlot3DCsv(csv, {}, table, &error));
			Plot3DTimeSeriesColumns columns;
			columns.time = 0; columns.x = 1; columns.y = 2; columns.u = 3; columns.v = 4; columns.w = 5; // z left out: a planar field
			Plot3DMeshData trails;
			CHECK(buildPlot3DTimeSeriesPathlines(table, columns, 3, 40, trails, &error));
			CHECK(!trails.empty() && trails.indices.empty() && trails.vertexCount() % 2 == 0 && trails.values.size() == trails.vertexCount());
			bool reachedEnd = false;
			for (std::size_t v = 0; v < trails.vertexCount(); ++v)
				if (std::abs(trails.values[v] - 4.0) < 1.0e-9)
				{
					reachedEnd = true; // x(4) = 2 + 8 = 10 would leave the grid (x <= 4), so no trail survives to t = 4 ...
					CHECK(trails.positions[v * 3] <= 4.0f + 1.0e-6f);
				}
			CHECK(!reachedEnd);
			// ... but x(2) = 2 + 2 = 4 does: the trail's last vertex is at about t = 2, x = 4 (time varies along it and is exact).
			double lastTime = 0.0; float lastX = 0.0f;
			for (std::size_t v = 0; v < trails.vertexCount(); ++v)
				if (trails.values[v] > lastTime) { lastTime = trails.values[v]; lastX = trails.positions[v * 3]; }
			CHECK(std::abs(lastX - (2.0f + static_cast<float>(lastTime * lastTime / 2.0))) < 1.0e-3f);

			// The field object itself: exact on the nodes, linear between them, clamped outside, planar z is 0.
			Plot3DTimeSeriesField field;
			CHECK(field.load(table, columns, &error) && field.timeSteps() == 5 && field.nodeCount() == 75);
			Plot3DVec3 velocity;
			CHECK(field.sample({ 1.0, 1.0, 0.0 }, 3.0, velocity) && std::abs(velocity.x - 3.0) < 1.0e-12 && velocity.y == 0.0);
			CHECK(field.sample({ 1.5, 0.5, 0.0 }, 2.5, velocity) && std::abs(velocity.x - 2.5) < 1.0e-12);
			CHECK(field.sample({ 99.0, -9.0, 0.0 }, 99.0, velocity) && std::abs(velocity.x - 4.0) < 1.0e-12);

			// A 3-D table (z column) is a plain extension: u = z here, so the field is the z coordinate.
			QString csv3d = QStringLiteral("time,px,py,pz,ux,uy,uz\n");
			for (int t = 0; t <= 1; ++t)
				for (int z = 0; z <= 2; ++z)
					for (int x = 0; x <= 2; ++x)
						for (int y = 0; y <= 1; ++y)
							csv3d += QStringLiteral("%1,%2,%3,%4,%4,0,0\n").arg(t).arg(x).arg(y).arg(z);
			Plot3DCsvTable table3d;
			CHECK(parsePlot3DCsv(csv3d, {}, table3d, &error));
			Plot3DTimeSeriesColumns columns3d{ 0, 1, 2, 3, 4, 5, 6 };
			Plot3DTimeSeriesField field3d;
			CHECK(field3d.load(table3d, columns3d, &error));
			CHECK(field3d.sample({ 1.0, 0.5, 1.5 }, 0.5, velocity) && std::abs(velocity.x - 1.5) < 1.0e-12);
			Plot3DMeshData trails3d;
			CHECK(buildPlot3DTimeSeriesPathlines(table3d, columns3d, 2, 20, trails3d, &error));

			// Rows in a shuffled order give the same field.
			Plot3DCsvTable shuffled = table;
			std::reverse(shuffled.rows.begin(), shuffled.rows.end());
			Plot3DTimeSeriesField shuffledField;
			CHECK(shuffledField.load(shuffled, columns, &error) && shuffledField.sample({ 1.5, 0.5, 0.0 }, 2.5, velocity) && std::abs(velocity.x - 2.5) < 1.0e-12);

			// Bad input is rejected with a message.
			Plot3DCsvTable incomplete = table;
			incomplete.rows.pop_back();
			CHECK(!buildPlot3DTimeSeriesPathlines(incomplete, columns, 3, 40, trails, &error) && error.contains(QStringLiteral("complete")));
			Plot3DCsvTable repeated = table;
			repeated.rows.back() = repeated.rows.front();
			CHECK(!buildPlot3DTimeSeriesPathlines(repeated, columns, 3, 40, trails, &error) && !error.isEmpty());
			Plot3DCsvTable notNumeric = table;
			notNumeric.rows[7][3] = QStringLiteral("abc");
			CHECK(!buildPlot3DTimeSeriesPathlines(notNumeric, columns, 3, 40, trails, &error) && error.contains(QStringLiteral("Row 8")));
			Plot3DTimeSeriesColumns missing = columns;
			missing.w = -1;
			CHECK(!buildPlot3DTimeSeriesPathlines(table, missing, 3, 40, trails, &error) && !error.isEmpty());
			CHECK(!buildPlot3DTimeSeriesPathlines(table, columns, 1, 40, trails, &error)); // too few seeds
			CHECK(!buildPlot3DTimeSeriesPathlines(Plot3DCsvTable(), columns, 3, 40, trails, &error));
		}

		Plot3DMeshData implicit;
		parameters.clear();
		parameters.insert(QStringLiteral("r"), 1.0);
		CHECK(buildPlot3DImplicitSurface(QStringLiteral("x^2+y^2+z^2-r^2"),
			-1.5, 1.5, 13, -1.5, 1.5, 13, -1.5, 1.5, 13, parameters, implicit, &error)
			&& !implicit.empty() && implicit.indices.size() % 3 == 0 && implicit.normals.size() == implicit.positions.size());
		CHECK(!buildPlot3DImplicitSurface(QStringLiteral("x^2+y^2+z^2+1"),
			-1.0, 1.0, 3, -1.0, 1.0, 3, -1.0, 1.0, 3, {}, implicit, &error) && error.contains(QStringLiteral("does not cross")));
	}
}

int main()
{
	testCsv();
	testDatasets();
	testSurfaceMesh();
	testLineAndScatterMesh();
	testBarMesh();
	testVoxelGrid();
	testAxes();
	testFormula();
	testSessionRoundTrip();
	testGenerate();
	std::printf("%d checks, %d failed\n", checks, failures);
	return failures;
}
