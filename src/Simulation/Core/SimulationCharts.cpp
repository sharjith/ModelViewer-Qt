#include "SimulationCharts.h"

#include "SimulationResultDisplay.h"
#include "ResultUnits.h"

#include <QObject>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace
{
	// CellLocator::interpolate() is built for streamline tracing: a mandatory per-node vector plus an optional
	// per-node scalar. Sampling a scalar field reuses the scalar channel; the vector channel is a throwaway zero
	// buffer of the right size (interpolate() checks vectors.size() == nodeCount * 3 up front) - one allocation for
	// the whole call, not per sample point.
	bool interpolateScalar(const CellLocator& locator, const std::vector<float>& zeroVectors, const std::vector<float>& nodeValues,
	                       const double p[3], int& hint, float& value)
	{
		double vector[3], scalar = 0.0;
		if (!locator.interpolate(p, zeroVectors, &nodeValues, hint, vector, scalar))
		{
			value = std::numeric_limits<float>::quiet_NaN();
			return false;
		}
		value = static_cast<float>(scalar);
		return true;
	}
}

namespace
{
	// The scalar a chart samples: a cell field is averaged onto the nodes first (the charts interpolate node values).
	bool buildChartScalar(const ResultDataset& dataset, int fieldIndex, int component, int step, DisplayScalar& scalar)
	{
		const bool cellField = fieldIndex >= 0 && static_cast<std::size_t>(fieldIndex) < dataset.fields.size()
		                       && dataset.fields[static_cast<std::size_t>(fieldIndex)].association == ResultFieldAssociation::Cell;
		if (!cellField)
			return buildDisplayScalar(dataset, fieldIndex, component, scalar, step) && !scalar.cellData;
		const CellToNodeAverager averager(dataset);
		return buildDisplayScalar(dataset, fieldIndex, component, scalar, step, &averager) && !scalar.cellData;
	}
}

bool sampleFieldOverLine(const ResultDataset& dataset, const CellLocator& locator, int fieldIndex, int component, int step,
                         const double p0[3], const double p1[3], std::size_t sampleCount, ChartSeries& out)
{
	out = ChartSeries();
	if (sampleCount < 2 || locator.volumeCellCount() == 0)
		return false;
	DisplayScalar scalar;
	if (!buildChartScalar(dataset, fieldIndex, component, step, scalar))
		return false;

	const double dx = p1[0] - p0[0], dy = p1[1] - p0[1], dz = p1[2] - p0[2];
	const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
	const std::vector<float> zeroVectors(dataset.nodeCount() * 3, 0.0f);
	out.title = scalar.label;
	out.xLabel = QStringLiteral("Distance");
	out.xUnit = dataset.lengthUnit;
	out.yLabel = scalar.label;
	out.yUnit = scalar.unit;
	out.x.resize(sampleCount);
	out.y.resize(sampleCount);
	int hint = -1;
	for (std::size_t i = 0; i < sampleCount; ++i)
	{
		const double t = static_cast<double>(i) / static_cast<double>(sampleCount - 1);
		const double p[3] = { p0[0] + t * dx, p0[1] + t * dy, p0[2] + t * dz };
		out.x[i] = t * length;
		interpolateScalar(locator, zeroVectors, scalar.nodeValues, p, hint, out.y[i]);
	}
	return true;
}

bool buildFieldHistogram(const ResultDataset& dataset, int fieldIndex, int component, int step, int binCount,
                         std::vector<float>& edges, std::vector<std::size_t>& counts, QString& label, QString& unit)
{
	edges.clear();
	counts.clear();
	DisplayScalar scalar;
	if (!buildDisplayScalar(dataset, fieldIndex, component, scalar, step))
		return false;
	binCount = std::max(1, binCount);
	float lo = std::numeric_limits<float>::max(), hi = std::numeric_limits<float>::lowest();
	for (float v : scalar.nodeValues)
		if (std::isfinite(v))
		{
			lo = std::min(lo, v);
			hi = std::max(hi, v);
		}
	if (!(hi > lo))
		return false; // constant (or no finite value at all)
	label = scalar.label;
	unit = scalar.unit;
	edges.resize(static_cast<std::size_t>(binCount) + 1);
	for (int i = 0; i <= binCount; ++i)
		edges[static_cast<std::size_t>(i)] = lo + (hi - lo) * static_cast<float>(i) / static_cast<float>(binCount);
	counts.assign(static_cast<std::size_t>(binCount), 0);
	for (float v : scalar.nodeValues)
	{
		if (!std::isfinite(v))
			continue;
		const int bin = std::clamp(static_cast<int>((v - lo) / (hi - lo) * static_cast<float>(binCount)), 0, binCount - 1);
		++counts[static_cast<std::size_t>(bin)];
	}
	return true;
}

namespace
{
	// A point's history across every step, from the node weights found at the point (by either locator).
	bool sampleOverTimeAtStencil(const ResultDataset& dataset, const CellInterpolationStencil& stencil, int fieldIndex, int component, ChartSeries& out)
	{
	out = ChartSeries();
	if (fieldIndex < 0 || static_cast<std::size_t>(fieldIndex) >= dataset.fields.size() || dataset.stepCount() == 0 || stencil.empty())
		return false;
	const ResultField& field = dataset.fields[static_cast<std::size_t>(fieldIndex)];
	if (field.components <= 0)
		return false;
	// A cell field is read through the size-weighted average of the cells around each node of the stencil.
	const bool cellField = field.association == ResultFieldAssociation::Cell;
	const std::unique_ptr<CellToNodeAverager> averager = cellField ? std::make_unique<CellToNodeAverager>(dataset) : nullptr;
	const std::size_t tuples = cellField ? dataset.cellCount() : dataset.nodeCount();
	const bool magnitude = component < 0 && field.components == 3;
	if (field.components != 1 && !magnitude && (component < 0 || component >= field.components))
		return false;

	out.title = field.name;
	if (field.components > 1)
		out.title += magnitude ? QStringLiteral(" (magnitude)") : QStringLiteral(" (component %1)").arg(component);
	out.yLabel = out.title;
	const UnitConversion conversion = unitConversion(field.quantityKind, field.fileUnit,
		field.displayUnit.isEmpty() ? field.fileUnit : field.displayUnit);
	if (!field.fileUnit.isEmpty())
		out.yUnit = conversion.valid ? (field.displayUnit.isEmpty() ? field.fileUnit : field.displayUnit) : field.fileUnit;
	// A modal/frequency result's "time" is a frequency (ResultStep::timeUnit, e.g. "Hz") - label the axis for what
	// it actually is instead of always saying "Time".
	const QString timeUnit = dataset.steps.empty() ? QString() : dataset.steps.front().timeUnit;
	out.xLabel = timeUnit.compare(QStringLiteral("Hz"), Qt::CaseInsensitive) == 0 ? QStringLiteral("Frequency") : QStringLiteral("Time");
	out.xUnit = timeUnit;
	out.x.resize(dataset.stepCount());
	out.y.assign(dataset.stepCount(), std::numeric_limits<float>::quiet_NaN());
	bool any = false;
	for (std::size_t s = 0; s < dataset.stepCount(); ++s)
	{
		out.x[s] = dataset.steps[s].time;
		dataset.ensureStepLoaded(s);
		if (s >= field.stepData.size())
			continue;
		const std::vector<float>& data = field.stepData[s];
		if (data.size() != tuples * static_cast<std::size_t>(field.components))
			continue; // no data at this step (a field may start later): leave it NaN
		std::vector<float> cellValues; // the chosen component (or magnitude) of every cell, for a cell field
		if (cellField)
		{
			cellValues.resize(tuples);
			const std::size_t comps = static_cast<std::size_t>(field.components);
			for (std::size_t c = 0; c < tuples; ++c)
			{
				if (comps == 1)
					cellValues[c] = data[c];
				else if (magnitude)
					cellValues[c] = std::sqrt(data[c * 3] * data[c * 3] + data[c * 3 + 1] * data[c * 3 + 1] + data[c * 3 + 2] * data[c * 3 + 2]);
				else
					cellValues[c] = data[c * comps + static_cast<std::size_t>(component)];
			}
		}
		double sampled = 0.0;
		bool finite = true;
		for (std::size_t i = 0; i < stencil.nodes.size(); ++i)
		{
			const std::size_t base = static_cast<std::size_t>(stencil.nodes[i]) * static_cast<std::size_t>(field.components);
			double value = 0.0;
			if (cellField)
				value = averager->valueAt(stencil.nodes[i], cellValues);
			else if (field.components == 1)
				value = data[base];
			else if (magnitude)
			{
				const double x = data[base], y = data[base + 1], z = data[base + 2];
				value = std::sqrt(x * x + y * y + z * z);
			}
			else
				value = data[base + static_cast<std::size_t>(component)];
			if (!std::isfinite(value))
			{
				finite = false;
				break;
			}
			sampled += stencil.weights[i] * value;
		}
		if (finite)
		{
			if (conversion.valid && !conversion.isIdentity())
				sampled = conversion.apply(sampled);
			out.y[s] = static_cast<float>(sampled);
		}
		if (finite && std::isfinite(out.y[s]))
			any = true;
	}
	return any;
	}
}

bool sampleFieldOverTime(const ResultDataset& dataset, const CellLocator& locator, int fieldIndex, int component, const double point[3], ChartSeries& out)
{
	out = ChartSeries();
	if (locator.volumeCellCount() == 0)
		return false;
	int hint = -1;
	CellInterpolationStencil stencil;
	if (!locator.interpolationStencil(point, hint, stencil))
		return false;
	return sampleOverTimeAtStencil(dataset, stencil, fieldIndex, component, out);
}

bool sampleFieldOverTime(const ResultDataset& dataset, const SurfaceLocator& locator, int fieldIndex, int component, const double point[3], ChartSeries& out,
                         double maxDistanceFraction)
{
	out = ChartSeries();
	if (locator.triangleCount() == 0)
		return false;
	CellInterpolationStencil stencil;
	if (!locator.nearestStencil(point, std::max(0.0, maxDistanceFraction) * locator.diagonal(), stencil))
		return false;
	return sampleOverTimeAtStencil(dataset, stencil, fieldIndex, component, out);
}

bool sampleFieldOverLine(const ResultDataset& dataset, const SurfaceLocator& locator, int fieldIndex, int component, int step,
                         const double p0[3], const double p1[3], std::size_t sampleCount, ChartSeries& out, double maxDistanceFraction)
{
	out = ChartSeries();
	if (sampleCount < 2 || locator.triangleCount() == 0)
		return false;
	DisplayScalar scalar;
	if (!buildChartScalar(dataset, fieldIndex, component, step, scalar))
		return false;
	const double dx = p1[0] - p0[0], dy = p1[1] - p0[1], dz = p1[2] - p0[2];
	const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
	const double tolerance = std::max(0.0, maxDistanceFraction) * locator.diagonal();
	out.title = scalar.label;
	out.xLabel = QStringLiteral("Distance");
	out.xUnit = dataset.lengthUnit;
	out.yLabel = scalar.label;
	out.yUnit = scalar.unit;
	out.x.resize(sampleCount);
	out.y.assign(sampleCount, std::numeric_limits<float>::quiet_NaN());
	bool any = false;
	for (std::size_t i = 0; i < sampleCount; ++i)
	{
		const double t = static_cast<double>(i) / static_cast<double>(sampleCount - 1);
		const double p[3] = { p0[0] + t * dx, p0[1] + t * dy, p0[2] + t * dz };
		out.x[i] = t * length;
		CellInterpolationStencil stencil;
		if (!locator.nearestStencil(p, tolerance, stencil))
			continue; // off the surface here: a gap
		double value = 0.0;
		bool finite = true;
		for (std::size_t k = 0; k < stencil.nodes.size(); ++k)
		{
			const float nodeValue = scalar.nodeValues[stencil.nodes[k]];
			finite = finite && std::isfinite(nodeValue);
			value += stencil.weights[k] * nodeValue;
		}
		if (finite)
		{
			out.y[i] = static_cast<float>(value);
			any = true;
		}
	}
	return any;
}

QString chartToCsv(const ChartSeries& main, const std::vector<ChartSeries>& extras)
{
	std::vector<const ChartSeries*> curves = { &main };
	for (const ChartSeries& extra : extras)
		curves.push_back(&extra);
	auto labelled = [](const QString& label, const QString& unit) {
		QString text = unit.isEmpty() ? label : label + QStringLiteral(" (") + unit + QLatin1Char(')');
		if (text.contains(QLatin1Char(',')) || text.contains(QLatin1Char('"')))
			text = QLatin1Char('"') + text.replace(QLatin1Char('"'), QStringLiteral("\"\"")) + QLatin1Char('"');
		return text;
	};
	QStringList lines;
	QStringList header;
	std::size_t rows = 0;
	for (const ChartSeries* curve : curves)
	{
		header << labelled(curve->xLabel, curve->xUnit) << labelled(curve->title.isEmpty() ? curve->yLabel : curve->title, curve->yUnit);
		rows = std::max(rows, curve->x.size());
	}
	lines << header.join(QLatin1Char(','));
	for (std::size_t r = 0; r < rows; ++r)
	{
		QStringList cells;
		for (const ChartSeries* curve : curves)
		{
			if (r < curve->x.size() && r < curve->y.size())
			{
				cells << QString::number(curve->x[r], 'g', 9);
				cells << (std::isfinite(curve->y[r]) ? QString::number(static_cast<double>(curve->y[r]), 'g', 9) : QString());
			}
			else
				cells << QString() << QString();
		}
		lines << cells.join(QLatin1Char(','));
	}
	return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

bool chartNeedsSecondaryAxis(const ChartSeries& main, const ChartSeries& curve)
{
	return !main.yUnit.isEmpty() && !curve.yUnit.isEmpty() && main.yUnit.compare(curve.yUnit, Qt::CaseInsensitive) != 0;
}

void chartZoomRange(double lo, double hi, double anchor, double factor, double& newLo, double& newHi)
{
	anchor = std::clamp(anchor, 0.0, 1.0);
	const double span = hi - lo, anchorValue = lo + anchor * span, newSpan = span * factor;
	newLo = anchorValue - anchor * newSpan;
	newHi = newLo + newSpan;
}

bool parseChartCurveCsv(const QString& text, const QString& fallbackTitle, ChartSeries& out, QString* error)
{
	out = ChartSeries();
	const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
	if (lines.isEmpty())
	{
		if (error) *error = QObject::tr("The file is empty.");
		return false;
	}
	// The delimiter: whichever of tab, semicolon or comma the first line has (comma by default).
	QChar delimiter = QLatin1Char(',');
	for (QChar candidate : { QLatin1Char('\t'), QLatin1Char(';'), QLatin1Char(',') })
		if (lines.first().contains(candidate))
		{
			delimiter = candidate;
			break;
		}
	auto number = [](const QString& cell, double& value) {
		bool ok = false;
		value = cell.trimmed().toDouble(&ok);
		return ok && std::isfinite(value);
	};
	std::vector<std::pair<double, double>> points;
	QString xName, yName;
	for (int i = 0; i < lines.size(); ++i)
	{
		const QStringList cells = lines[i].trimmed().split(delimiter);
		if (cells.size() < 2)
			continue;
		double x = 0.0, y = 0.0;
		if (number(cells[0], x) && number(cells[1], y))
			points.emplace_back(x, y);
		else if (i == 0) // a header row names the axes
		{
			xName = cells[0].trimmed();
			yName = cells[1].trimmed();
		}
	}
	if (points.size() < 2)
	{
		if (error) *error = QObject::tr("The file needs at least two rows of numbers (x, y).");
		return false;
	}
	std::stable_sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
	// A header cell written as "Name (unit)" gives the curve that unit - what makes a curve in another unit land on the chart's second axis.
	auto splitUnit = [](QString& name, QString& unit) {
		name = name.trimmed();
		if (name.endsWith(QLatin1Char(')')))
		{
			const int open = name.lastIndexOf(QLatin1Char('('));
			if (open > 0)
			{
				unit = name.mid(open + 1, name.size() - open - 2).trimmed();
				name = name.left(open).trimmed();
			}
		}
	};
	splitUnit(xName, out.xUnit);
	splitUnit(yName, out.yUnit);
	out.title = yName.isEmpty() ? fallbackTitle : yName;
	out.xLabel = xName.isEmpty() ? QObject::tr("x") : xName;
	out.yLabel = yName.isEmpty() ? QObject::tr("y") : yName;
	out.x.reserve(points.size());
	out.y.reserve(points.size());
	for (const auto& point : points)
	{
		out.x.push_back(point.first);
		out.y.push_back(static_cast<float>(point.second));
	}
	return true;
}
