#include "Plot3DData.h"
#include "Plot3DFormula.h"
#include "Plot3DAxisController.h"
#include "Plot3DMeshBuilder.h"

#include <cmath>
#include <cstdio>
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

		Plot3DAxisController controller;
		std::array<Plot3DAxisConfig, 3> axes{ linear, linear, linear };
		const double lo[3] = { 0.0, 10.0, -5.0 }, hi[3] = { 4.0, 20.0, 5.0 };
		Plot3DAxisLayout layout;
		QString error;
		CHECK(controller.buildLayout(axes, lo, hi, layout, &error, QStringLiteral("Axis layout test")));
		CHECK(error.isEmpty() && layout.axisLines.size() == 12 && layout.referencePlanes.size() == 1);
		CHECK(layout.labels.size() == layout.ticks[0].size() + layout.ticks[1].size() + layout.ticks[2].size()
			&& layout.axisTitles.size() == 3);
		CHECK(layout.gridLines.size() == 2 * (layout.ticks[0].size() + layout.ticks[1].size() + layout.ticks[2].size())
			&& layout.title == QStringLiteral("Axis layout test"));
		const double flatLo[3] = { 2.0, 10.0, -5.0 }, flatHi[3] = { 2.0, 20.0, 5.0 };
		CHECK(controller.buildLayout(axes, flatLo, flatHi, layout, &error) && layout.maximum[0] > layout.minimum[0]);

		axes[0] = log;
		const double badLo[3] = { 0.0, 10.0, -5.0 };
		CHECK(!controller.buildLayout(axes, badLo, hi, layout, &error) && !error.isEmpty());
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
	std::printf("%d checks, %d failed\n", checks, failures);
	return failures;
}
