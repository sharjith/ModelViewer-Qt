#include "Plot3DGenerate.h"

#include <QFileInfo>
#include <QHash>
#include <QObject>

#include <algorithm>
#include <cmath>
#include <limits>
#include <variant>

bool plot3DSourceFromInt(int value, Plot3DSourceKind& kind)
{
	if (value < 0 || value > 9)
		return false;
	kind = static_cast<Plot3DSourceKind>(value);
	return true;
}

bool plot3DSourceIsGenerated(Plot3DSourceKind kind)
{
	return kind != Plot3DSourceKind::Csv && kind != Plot3DSourceKind::CsvTimeSeries;
}

bool plot3DSourceIsPathline(Plot3DSourceKind kind)
{
	return kind == Plot3DSourceKind::FormulaPathlines || kind == Plot3DSourceKind::CsvTimeSeries;
}

Plot3DPrimitive plot3DSourcePrimitive(Plot3DSourceKind kind)
{
	switch (kind)
	{
	case Plot3DSourceKind::ParametricCurve:
	case Plot3DSourceKind::FormulaStreamlines:
	case Plot3DSourceKind::FormulaPathlines:
	case Plot3DSourceKind::CsvTimeSeries:
		return Plot3DPrimitive::Line;
	case Plot3DSourceKind::FormulaVectorField:
		return Plot3DPrimitive::Quiver;
	default:
		return Plot3DPrimitive::Surface; // CSV (chosen by the user), formula / parametric / implicit surfaces
	}
}

namespace
{
	QHash<QString, double> parameterTable(const Plot3DGeneratedSpec& spec)
	{
		QHash<QString, double> parameters;
		for (const auto& parameter : spec.parameters)
			parameters.insert(parameter.first.toLower(), parameter.second);
		return parameters;
	}
}

bool generatePlot3D(const Plot3DGeneratedSpec& spec, Plot3DPrimitive formulaPrimitive, const Plot3DCsvTable* table,
	const Plot3DTimeSeriesColumns* columns, Plot3DGenerated& out, QString* error)
{
	out = Plot3DGenerated();
	Plot3DSourceKind kind;
	if (!plot3DSourceFromInt(spec.sourceMode, kind) || kind == Plot3DSourceKind::Csv)
	{
		if (error) *error = QObject::tr("This data source cannot be generated from a definition.");
		return false;
	}
	const QHash<QString, double> parameters = parameterTable(spec);

	switch (kind)
	{
	case Plot3DSourceKind::FormulaSurface:
	{
		Plot3DSurfaceData surface;
		if (!buildPlot3DFormulaSurface(spec.expression, spec.xMinimum, spec.xMaximum, spec.xSamples, spec.yMinimum, spec.yMaximum, spec.ySamples,
			parameters, surface, error))
			return false;
		out.primitive = formulaPrimitive == Plot3DPrimitive::Contour ? Plot3DPrimitive::Contour : Plot3DPrimitive::Surface;
		out.hasDataset = true;
		out.dataset.name = spec.title;
		out.dataset.primitive = out.primitive;
		out.dataset.content = std::move(surface);
		return true;
	}
	case Plot3DSourceKind::ParametricSurface:
		out.primitive = Plot3DPrimitive::Surface;
		out.primitiveMode = Plot3DGl::kTriangles;
		return buildPlot3DParametricSurface(spec.xExpression, spec.yExpression, spec.zExpression, spec.xMinimum, spec.xMaximum, spec.xSamples,
			spec.yMinimum, spec.yMaximum, spec.ySamples, parameters, out.mesh, error);
	case Plot3DSourceKind::ParametricCurve:
	{
		Plot3DLineData curve;
		if (!buildPlot3DParametricCurve(spec.xExpression, spec.yExpression, spec.zExpression, spec.xMinimum, spec.xMaximum, spec.xSamples,
			parameters, curve, error))
			return false;
		out.primitive = Plot3DPrimitive::Line;
		out.hasDataset = true;
		out.dataset.name = spec.title;
		out.dataset.primitive = Plot3DPrimitive::Line;
		out.dataset.content = std::move(curve);
		return true;
	}
	case Plot3DSourceKind::FormulaVectorField:
	{
		Plot3DQuiverData vectors;
		if (!buildPlot3DFormulaVectorField(spec.xExpression, spec.yExpression, spec.zExpression, spec.xMinimum, spec.xMaximum, spec.xSamples,
			spec.yMinimum, spec.yMaximum, spec.ySamples, parameters, vectors, error))
			return false;
		out.primitive = Plot3DPrimitive::Quiver;
		out.hasDataset = true;
		out.dataset.name = spec.title;
		out.dataset.primitive = Plot3DPrimitive::Quiver;
		out.dataset.content = std::move(vectors);
		return true;
	}
	case Plot3DSourceKind::ImplicitSurface:
		out.primitive = Plot3DPrimitive::Surface;
		out.primitiveMode = Plot3DGl::kTriangles;
		return buildPlot3DImplicitSurface(spec.expression, spec.xMinimum, spec.xMaximum, spec.xSamples, spec.yMinimum, spec.yMaximum, spec.ySamples,
			spec.zMinimum, spec.zMaximum, spec.zSamples, parameters, out.mesh, error);
	case Plot3DSourceKind::FormulaStreamlines:
		out.primitive = Plot3DPrimitive::Line;
		out.primitiveMode = Plot3DGl::kLines;
		return buildPlot3DFormulaStreamlines(spec.xExpression, spec.yExpression, spec.zExpression, spec.xMinimum, spec.xMaximum, spec.yMinimum,
			spec.yMaximum, spec.ySamples, parameters, out.mesh, error);
	case Plot3DSourceKind::FormulaPathlines:
		out.primitive = Plot3DPrimitive::Line;
		out.primitiveMode = Plot3DGl::kLines;
		return buildPlot3DFormulaPathlines(spec.xExpression, spec.yExpression, spec.zExpression, spec.xMinimum, spec.xMaximum, spec.yMinimum,
			spec.yMaximum, spec.ySamples, spec.zMinimum, spec.zMaximum, spec.zSamples, parameters, out.mesh, error);
	case Plot3DSourceKind::ImageSurface:
	{
		if (spec.imagePath.trimmed().isEmpty() || !QFileInfo::exists(spec.imagePath))
		{
			if (error) *error = QObject::tr("Choose an image file.");
			return false;
		}
		// The plane's two ranges, and where it sits along its normal.
		const int plane = std::clamp(spec.imagePlane, 0, 2);
		const double uLo = plane == 2 ? spec.yMinimum : spec.xMinimum, uHi = plane == 2 ? spec.yMaximum : spec.xMaximum;
		const double vLo = plane == 0 ? spec.yMinimum : spec.zMinimum, vHi = plane == 0 ? spec.yMaximum : spec.zMaximum;
		const double offset = plane == 0 ? spec.zMinimum : (plane == 1 ? spec.yMinimum : spec.xMinimum);
		if (!(uHi > uLo) || !(vHi > vLo) || !std::isfinite(uLo) || !std::isfinite(uHi) || !std::isfinite(vLo) || !std::isfinite(vHi) || !std::isfinite(offset))
		{
			if (error) *error = QObject::tr("The image's ranges must have a maximum above the minimum.");
			return false;
		}
		const double corners[4][2] = { { uLo, vLo }, { uHi, vLo }, { uHi, vHi }, { uLo, vHi } };
		const float uvs[4][2] = { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f } }; // the picture's top edge is the larger v
		const float normal[3] = { plane == 2 ? 1.0f : 0.0f, plane == 1 ? -1.0f : 0.0f, plane == 0 ? 1.0f : 0.0f };
		Plot3DMeshData quad;
		for (int i = 0; i < 4; ++i)
		{
			const double u = corners[i][0], v = corners[i][1];
			const double xyz[3] = { plane == 2 ? offset : u, plane == 0 ? v : (plane == 1 ? offset : u), plane == 0 ? offset : v };
			quad.positions.insert(quad.positions.end(), { static_cast<float>(xyz[0]), static_cast<float>(xyz[1]), static_cast<float>(xyz[2]) });
			quad.normals.insert(quad.normals.end(), { normal[0], normal[1], normal[2] });
			quad.values.push_back(std::numeric_limits<double>::quiet_NaN()); // no colour value: the picture is the colour
			quad.uvs.insert(quad.uvs.end(), { uvs[i][0], uvs[i][1] });
		}
		// One winding only: two coplanar copies fight for the depth test and the back-facing one is shaded dark. The picture faces
		// +Z (XY), -Y (XZ) or +X (YZ); from the other side it reads mirrored.
		quad.indices = { 0, 1, 2, 0, 2, 3 };
		out.primitive = Plot3DPrimitive::Surface;
		out.primitiveMode = Plot3DGl::kTriangles;
		out.mesh = std::move(quad);
		out.imagePath = spec.imagePath;
		return true;
	}
	case Plot3DSourceKind::CsvTimeSeries:
		if (!table || !columns)
		{
			if (error) *error = QObject::tr("Open or paste tabular data first.");
			return false;
		}
		out.primitive = Plot3DPrimitive::Line;
		out.primitiveMode = Plot3DGl::kLines;
		return buildPlot3DTimeSeriesPathlines(*table, *columns, spec.ySamples, spec.zSamples, out.mesh, error);
	default:
		break;
	}
	if (error) *error = QObject::tr("This data source cannot be generated from a definition.");
	return false;
}

bool plot3DMeshForDataset(const Plot3DDataset& dataset, const Plot3DMeshOptions& options, Plot3DMeshData& out,
	unsigned int& primitiveMode, QString* error)
{
	out = Plot3DMeshData();
	primitiveMode = Plot3DGl::kTriangles;
	switch (dataset.primitive)
	{
	case Plot3DPrimitive::Surface:
		return std::holds_alternative<Plot3DSurfaceData>(dataset.content)
			&& buildPlot3DSurfaceMesh(std::get<Plot3DSurfaceData>(dataset.content), out, error);
	case Plot3DPrimitive::Contour:
		primitiveMode = Plot3DGl::kLines;
		return std::holds_alternative<Plot3DSurfaceData>(dataset.content)
			&& buildPlot3DContourMesh(std::get<Plot3DSurfaceData>(dataset.content), out, options.contourLevels, error, options.contourProjected);
	case Plot3DPrimitive::Line:
		if (!std::holds_alternative<Plot3DLineData>(dataset.content))
			return false;
		if (options.filled)
		{
			primitiveMode = Plot3DGl::kTriangles;
			return buildPlot3DLineFillMesh(std::get<Plot3DLineData>(dataset.content), options.baseZ, out, error);
		}
		primitiveMode = Plot3DGl::kLineStrip;
		return buildPlot3DLineMesh(std::get<Plot3DLineData>(dataset.content), out, error);
	case Plot3DPrimitive::Scatter:
	{
		if (!std::holds_alternative<Plot3DScatterData>(dataset.content))
			return false;
		const Plot3DScatterData& scatter = std::get<Plot3DScatterData>(dataset.content);
		if (options.filled)
		{
			primitiveMode = Plot3DGl::kTriangles;
			return buildPlot3DScatterFillMesh(scatter, options.baseZ, out, error);
		}
		if (options.errorBars)
		{
			primitiveMode = Plot3DGl::kLines;
			return buildPlot3DErrorBarMesh(scatter, out, error);
		}
		if (options.stems)
		{
			primitiveMode = Plot3DGl::kLines;
			return buildPlot3DStemMesh(scatter, options.baseZ, out, error);
		}
		primitiveMode = Plot3DGl::kPoints;
		return buildPlot3DScatterMesh(scatter, out, error);
	}
	case Plot3DPrimitive::Bar:
	{
		if (!std::holds_alternative<Plot3DBarData>(dataset.content))
			return false;
		Plot3DBarData bars = std::get<Plot3DBarData>(dataset.content);
		for (Plot3DBar& bar : bars.bars)
		{
			bar.width *= options.barWidthScale;
			bar.depth *= options.barDepthScale;
		}
		return buildPlot3DBarMesh(bars, out, error);
	}
	default:
		break;
	}
	return false; // Quiver / Voxel are drawn by their own renderers
}

bool plot3DDatasetBounds(const Plot3DDataset& dataset, const Plot3DMeshOptions& options, double minimum[3], double maximum[3])
{
	Plot3DDataset scaled = dataset;
	if (dataset.primitive == Plot3DPrimitive::Bar && std::holds_alternative<Plot3DBarData>(dataset.content))
	{
		Plot3DBarData bars = std::get<Plot3DBarData>(dataset.content);
		for (Plot3DBar& bar : bars.bars)
		{
			bar.width *= options.barWidthScale;
			bar.depth *= options.barDepthScale;
		}
		scaled.content = std::move(bars);
	}
	if (!plot3DDataBounds(scaled, minimum, maximum))
		return false;
	if ((dataset.primitive == Plot3DPrimitive::Scatter && (options.stems || options.filled)) || (dataset.primitive == Plot3DPrimitive::Line && options.filled))
	{
		// A line filled to a second curve reaches that curve's Z; otherwise the ribbon reaches the base plane.
		const Plot3DLineData* line = dataset.primitive == Plot3DPrimitive::Line && std::holds_alternative<Plot3DLineData>(dataset.content)
			? &std::get<Plot3DLineData>(dataset.content) : nullptr;
		if (line && !line->fillTo.empty() && line->fillTo.size() == line->samples.size())
		{
			for (double z : line->fillTo)
				if (std::isfinite(z))
				{
					minimum[2] = std::min(minimum[2], z);
					maximum[2] = std::max(maximum[2], z);
				}
		}
		else
		{
			minimum[2] = std::min(minimum[2], options.baseZ);
			maximum[2] = std::max(maximum[2], options.baseZ);
		}
	}
	return true;
}

bool plot3DMeshBounds(const Plot3DMeshData& mesh, double minimum[3], double maximum[3])
{
	if (mesh.vertexCount() == 0)
		return false;
	for (int axis = 0; axis < 3; ++axis)
	{
		minimum[axis] = std::numeric_limits<double>::max();
		maximum[axis] = std::numeric_limits<double>::lowest();
	}
	for (std::size_t i = 0; i < mesh.vertexCount(); ++i)
		for (int axis = 0; axis < 3; ++axis)
		{
			const double value = static_cast<double>(mesh.positions[i * 3 + static_cast<std::size_t>(axis)]);
			minimum[axis] = std::min(minimum[axis], value);
			maximum[axis] = std::max(maximum[axis], value);
		}
	return true;
}

namespace
{
	std::vector<std::pair<QString, double>> parametersOf(const QVector<Plot3DFormulaParameter>& source)
	{
		std::vector<std::pair<QString, double>> out;
		for (const Plot3DFormulaParameter& parameter : source)
			out.emplace_back(parameter.name, parameter.value);
		return out;
	}

	Plot3DGeneratedSpec baseSpec(Plot3DSourceKind kind, int index, const QString& title)
	{
		Plot3DGeneratedSpec spec;
		spec.valid = true;
		spec.sourceMode = plot3DSourceInt(kind);
		spec.presetIndex = index;
		spec.title = title;
		return spec;
	}
}

QVector<Plot3DPresetEntry> plot3DPresetEntries(Plot3DSourceKind kind)
{
	QVector<Plot3DPresetEntry> entries;
	switch (kind)
	{
	case Plot3DSourceKind::FormulaSurface:
	{
		int index = 0;
		for (const Plot3DFormulaPreset& p : plot3DFormulaPresets())
		{
			Plot3DGeneratedSpec spec = baseSpec(kind, index++, p.title);
			spec.expression = p.expression;
			spec.xMinimum = p.xMinimum; spec.xMaximum = p.xMaximum; spec.xSamples = p.xSamples;
			spec.yMinimum = p.yMinimum; spec.yMaximum = p.yMaximum; spec.ySamples = p.ySamples;
			spec.parameters = parametersOf(p.parameters);
			entries.push_back({ p.name, spec });
		}
		break;
	}
	case Plot3DSourceKind::ParametricSurface:
	{
		int index = 0;
		for (const Plot3DParametricPreset& p : plot3DParametricPresets())
		{
			Plot3DGeneratedSpec spec = baseSpec(kind, index++, p.title);
			spec.xExpression = p.xExpression; spec.yExpression = p.yExpression; spec.zExpression = p.zExpression;
			spec.xMinimum = p.uMinimum; spec.xMaximum = p.uMaximum; spec.xSamples = p.uSamples; // u runs along "x", v along "y"
			spec.yMinimum = p.vMinimum; spec.yMaximum = p.vMaximum; spec.ySamples = p.vSamples;
			spec.parameters = parametersOf(p.parameters);
			entries.push_back({ p.name, spec });
		}
		break;
	}
	case Plot3DSourceKind::ParametricCurve:
	{
		int index = 0;
		for (const Plot3DParametricCurvePreset& p : plot3DParametricCurvePresets())
		{
			Plot3DGeneratedSpec spec = baseSpec(kind, index++, p.title);
			spec.xExpression = p.xExpression; spec.yExpression = p.yExpression; spec.zExpression = p.zExpression;
			spec.xMinimum = p.tMinimum; spec.xMaximum = p.tMaximum; spec.xSamples = p.samples; // t runs along "x"
			spec.parameters = parametersOf(p.parameters);
			entries.push_back({ p.name, spec });
		}
		break;
	}
	case Plot3DSourceKind::FormulaVectorField:
	case Plot3DSourceKind::FormulaStreamlines:
	{
		int index = 0;
		for (const Plot3DFormulaVectorPreset& p : plot3DFormulaVectorPresets())
		{
			Plot3DGeneratedSpec spec = baseSpec(kind, index++, p.title);
			spec.xExpression = p.uExpression; spec.yExpression = p.vExpression; spec.zExpression = p.wExpression; // u, v, w
			spec.xMinimum = p.xMinimum; spec.xMaximum = p.xMaximum; spec.xSamples = p.xSamples;
			spec.yMinimum = p.yMinimum; spec.yMaximum = p.yMaximum; spec.ySamples = p.ySamples; // streamlines: ySamples is the seed count
			spec.parameters = parametersOf(p.parameters);
			entries.push_back({ p.name, spec });
		}
		break;
	}
	case Plot3DSourceKind::ImplicitSurface:
	{
		int index = 0;
		for (const Plot3DImplicitPreset& p : plot3DImplicitPresets())
		{
			Plot3DGeneratedSpec spec = baseSpec(kind, index++, p.title);
			spec.expression = p.expression;
			spec.xMinimum = p.xMinimum; spec.xMaximum = p.xMaximum; spec.xSamples = p.xSamples;
			spec.yMinimum = p.yMinimum; spec.yMaximum = p.yMaximum; spec.ySamples = p.ySamples;
			spec.zMinimum = p.zMinimum; spec.zMaximum = p.zMaximum; spec.zSamples = p.zSamples;
			spec.parameters = parametersOf(p.parameters);
			entries.push_back({ p.name, spec });
		}
		break;
	}
	case Plot3DSourceKind::FormulaPathlines:
	{
		int index = 0;
		for (const Plot3DPathlinePreset& p : plot3DPathlinePresets())
		{
			Plot3DGeneratedSpec spec = baseSpec(kind, index++, p.title);
			spec.xExpression = p.uExpression; spec.yExpression = p.vExpression; spec.zExpression = p.wExpression; // u, v, w
			spec.xMinimum = p.xMinimum; spec.xMaximum = p.xMaximum;
			spec.yMinimum = p.yMinimum; spec.yMaximum = p.yMaximum; spec.ySamples = p.seeds;
			spec.zMinimum = p.tMinimum; spec.zMaximum = p.tMaximum; spec.zSamples = p.steps; // the "z" range row is the time range
			spec.parameters = parametersOf(p.parameters);
			entries.push_back({ p.name, spec });
		}
		break;
	}
	default:
		break;
	}
	return entries;
}
