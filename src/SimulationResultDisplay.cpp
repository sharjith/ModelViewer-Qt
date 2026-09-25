#include "SimulationResultDisplay.h"

#include "ResultUnits.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>

LoadedSimulationResult loadSimulationResult(const QString& path, const std::atomic<bool>* cancel)
{
	LoadedSimulationResult result;

	ResultReadOutcome read = readResultFile(path, cancel);
	result.warnings = read.warnings;
	if (!read.ok())
	{
		result.error = read.error;
		return result;
	}

	std::shared_ptr<ResultDataset> dataset(read.dataset.release());
	assignGuessedUnits(*dataset); // labelled, unconfirmed guesses (see ResultUnits.h)
	QString boundaryError;
	if (!extractBoundarySurface(*dataset, result.surface, cancel, &boundaryError))
	{
		result.error = boundaryError.isEmpty() ? QStringLiteral("Could not extract the boundary surface.") : boundaryError;
		return result;
	}
	result.dataset = std::move(dataset);
	return result;
}

bool buildDisplayScalar(const ResultDataset& dataset, int fieldIndex, int component, DisplayScalar& out, int step)
{
	out = DisplayScalar();
	if (fieldIndex < 0 || static_cast<std::size_t>(fieldIndex) >= dataset.fields.size())
		return false;
	const ResultField& field = dataset.fields[static_cast<std::size_t>(fieldIndex)];
	if (field.association != ResultFieldAssociation::Node || step < 0
		|| static_cast<std::size_t>(step) >= field.stepData.size() || field.stepData[static_cast<std::size_t>(step)].empty())
		return false; // not a node field, or no data for it at this step

	const std::size_t nodes = dataset.nodeCount();
	const int comps = field.components;
	const std::vector<float>& data = field.stepData[static_cast<std::size_t>(step)];
	if (comps <= 0 || data.size() != nodes * static_cast<std::size_t>(comps))
		return false;

	QString label = field.name;
	std::vector<float> values(nodes);
	if (comps == 1)
	{
		values = data;
	}
	else if (component >= 0)
	{
		if (component >= comps)
			return false;
		for (std::size_t n = 0; n < nodes; ++n)
			values[n] = data[n * static_cast<std::size_t>(comps) + static_cast<std::size_t>(component)];
		label += QStringLiteral(" (component %1)").arg(component);
	}
	else if (comps == 3)
	{
		for (std::size_t n = 0; n < nodes; ++n)
		{
			const float x = data[n * 3], y = data[n * 3 + 1], z = data[n * 3 + 2];
			values[n] = std::sqrt(x * x + y * y + z * z);
		}
		label += QStringLiteral(" (magnitude)");
	}
	else
		return false; // a 6/9-component tensor needs an explicit component

	// Numbers are converted only when the file unit and a different display unit are both known: a guessed file
	// unit alone never changes them.
	const UnitConversion conversion = unitConversion(field.quantityKind, field.fileUnit,
		field.displayUnit.isEmpty() ? field.fileUnit : field.displayUnit);
	if (conversion.valid && !conversion.isIdentity())
		for (float& v : values)
			v = static_cast<float>(conversion.apply(static_cast<double>(v)));

	float lo = std::numeric_limits<float>::max();
	float hi = std::numeric_limits<float>::lowest();
	for (float v : values)
	{
		if (!std::isfinite(v))
			continue;
		lo = std::min(lo, v);
		hi = std::max(hi, v);
	}
	if (lo > hi) // no finite value at all
		return false;

	out.fieldIndex = fieldIndex;
	out.component = comps == 1 ? -1 : component;
	out.label = label;
	out.nodeValues = std::move(values);
	out.minValue = lo;
	out.maxValue = hi;
	if (!field.fileUnit.isEmpty())
	{
		out.unit = conversion.valid ? (field.displayUnit.isEmpty() ? field.fileUnit : field.displayUnit) : field.fileUnit;
		out.unitAssumed = !field.unitConfirmed;
	}
	return true;
}

bool chooseDefaultDisplayScalar(const ResultDataset& dataset, DisplayScalar& out)
{
	int firstScalar = -1, firstVector = -1, vonMises = -1;
	for (std::size_t i = 0; i < dataset.fields.size(); ++i)
	{
		const ResultField& f = dataset.fields[i];
		if (f.association != ResultFieldAssociation::Node || f.stepData.empty() || f.stepData[0].empty())
			continue;
		const int index = static_cast<int>(i);
		if (f.components == 1)
		{
			const QString lower = f.name.toLower();
			if (vonMises < 0 && (lower.contains(QLatin1String("von mises")) || lower.contains(QLatin1String("von_mises"))
			                     || lower.contains(QLatin1String("vonmises"))))
				vonMises = index;
			if (firstScalar < 0)
				firstScalar = index;
		}
		else if (f.components == 3 && firstVector < 0)
			firstVector = index;
	}

	const int candidates[] = { vonMises, firstScalar, firstVector };
	for (int index : candidates)
		if (index >= 0 && buildDisplayScalar(dataset, index, -1, out))
			return true;
	out = DisplayScalar();
	return false;
}

std::vector<float> boundaryVertexValues(const ResultBoundarySurface& surface, const std::vector<float>& nodeValues)
{
	std::vector<float> values(surface.vertexCount(), std::numeric_limits<float>::quiet_NaN());
	for (std::size_t v = 0; v < surface.vertexNode.size() && v < values.size(); ++v)
		if (surface.vertexNode[v] < nodeValues.size())
			values[v] = nodeValues[surface.vertexNode[v]];
	return values;
}

std::vector<float> computeSmoothVertexNormals(const ResultBoundarySurface& surface)
{
	return computeSmoothVertexNormals(surface.positions, surface.triangles);
}

std::vector<float> computeSmoothVertexNormals(const std::vector<float>& positions, const std::vector<std::uint32_t>& triangles)
{
	const std::size_t vertexCount = positions.size() / 3;
	std::vector<double> sum(vertexCount * 3, 0.0);
	for (std::size_t t = 0; t + 2 < triangles.size(); t += 3)
	{
		const std::uint32_t a = triangles[t], b = triangles[t + 1], c = triangles[t + 2];
		const float* pa = &positions[a * 3];
		const float* pb = &positions[b * 3];
		const float* pc = &positions[c * 3];
		const double ux = pb[0] - pa[0], uy = pb[1] - pa[1], uz = pb[2] - pa[2];
		const double vx = pc[0] - pa[0], vy = pc[1] - pa[1], vz = pc[2] - pa[2];
		// Un-normalised cross product = area-weighted face normal.
		const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
		for (std::uint32_t idx : { a, b, c })
		{
			sum[idx * 3 + 0] += nx;
			sum[idx * 3 + 1] += ny;
			sum[idx * 3 + 2] += nz;
		}
	}
	std::vector<float> normals(vertexCount * 3);
	for (std::size_t v = 0; v < vertexCount; ++v)
	{
		const double x = sum[v * 3], y = sum[v * 3 + 1], z = sum[v * 3 + 2];
		const double len = std::sqrt(x * x + y * y + z * z);
		if (len > 1e-20)
		{
			normals[v * 3 + 0] = static_cast<float>(x / len);
			normals[v * 3 + 1] = static_cast<float>(y / len);
			normals[v * 3 + 2] = static_cast<float>(z / len);
		}
		else
		{
			normals[v * 3 + 0] = 0.0f;
			normals[v * 3 + 1] = 0.0f;
			normals[v * 3 + 2] = 1.0f;
		}
	}
	return normals;
}

int simulationShaderBands(const SimulationViewState& state)
{
	return state.bands >= 2 ? state.bands : kSimulationSmoothBands;
}

bool resolveViewRange(const DisplayScalar& scalar, const SimulationViewState& state, float& lo, float& hi)
{
	if (state.customRange)
	{
		if (!std::isfinite(state.rangeMin) || !std::isfinite(state.rangeMax))
			return false;
		lo = static_cast<float>(state.rangeMin);
		hi = static_cast<float>(state.rangeMax);
	}
	else
	{
		lo = scalar.minValue;
		hi = scalar.maxValue;
	}
	if (!(hi > lo))
		hi = lo + std::max(1.0e-6f, std::fabs(lo) * 1.0e-6f);
	return true;
}

SimulationViewState defaultViewState(const ResultDataset& dataset, DisplayScalar* outScalar)
{
	SimulationViewState state;
	DisplayScalar scalar;
	if (chooseDefaultDisplayScalar(dataset, scalar))
	{
		state.fieldIndex = scalar.fieldIndex;
		state.component = scalar.component;
	}
	if (outScalar)
		*outScalar = std::move(scalar);
	return state;
}

bool computeAllStepsRange(const ResultDataset& dataset, int fieldIndex, int component, float& lo, float& hi)
{
	bool any = false;
	lo = std::numeric_limits<float>::max();
	hi = std::numeric_limits<float>::lowest();
	for (std::size_t step = 0; step < dataset.stepCount(); ++step)
	{
		DisplayScalar scalar;
		if (!buildDisplayScalar(dataset, fieldIndex, component, scalar, static_cast<int>(step)))
			continue;
		lo = std::min(lo, scalar.minValue);
		hi = std::max(hi, scalar.maxValue);
		any = true;
	}
	return any;
}

bool cachedAllStepsRange(SimulationSession& session, int fieldIndex, int component, float& lo, float& hi)
{
	if (!session.dataset || fieldIndex < 0 || static_cast<std::size_t>(fieldIndex) >= session.dataset->fields.size())
		return false;
	const ResultField& field = session.dataset->fields[static_cast<std::size_t>(fieldIndex)];
	SimulationRangeCache& cache = session.rangeCache;
	const bool hit = cache.valid && cache.fieldIndex == fieldIndex && cache.component == component
		&& cache.kindId == field.quantityKind && cache.fileUnit == field.fileUnit && cache.displayUnit == field.displayUnit;
	if (!hit)
	{
		float a = 0.0f, b = 1.0f;
		if (!computeAllStepsRange(*session.dataset, fieldIndex, component, a, b))
		{
			cache.valid = false;
			return false;
		}
		cache = SimulationRangeCache{ true, fieldIndex, component, field.quantityKind, field.fileUnit, field.displayUnit, a, b };
	}
	lo = cache.lo;
	hi = cache.hi;
	return true;
}

QString stepTimeText(const ResultStep& step)
{
	QString text = QString::number(step.time, 'g', 6);
	if (!step.timeUnit.isEmpty())
		text += QLatin1Char(' ') + step.timeUnit;
	return text;
}

QString stepDescription(const ResultDataset& dataset, int step)
{
	if (step < 0 || static_cast<std::size_t>(step) >= dataset.steps.size())
		return QString();
	const ResultStep& s = dataset.steps[static_cast<std::size_t>(step)];
	if (!s.label.isEmpty())
		return s.label + QStringLiteral(" - ") + stepTimeText(s); // "Mode 3 - 73971 Hz"
	if (!s.timeUnit.isEmpty())
		return stepTimeText(s);                                    // "0.0194 Hz"
	return QStringLiteral("t = ") + stepTimeText(s);               // "t = 0.5"
}

int findDisplacementField(const ResultDataset& dataset)
{
	int best = -1;
	for (std::size_t i = 0; i < dataset.fields.size(); ++i)
	{
		const ResultField& f = dataset.fields[i];
		if (f.association != ResultFieldAssociation::Node || f.components != 3 || f.stepData.empty())
			continue;
		const QString name = f.name.toLower();
		if (name.contains(QStringLiteral("magnitude")))
			continue;
		if (name.contains(QStringLiteral("displacement")))
			return static_cast<int>(i); // an explicit name wins outright
		if (best < 0 && (name == QStringLiteral("disp") || name.startsWith(QStringLiteral("disp")) || name.contains(QStringLiteral("deformation"))))
			best = static_cast<int>(i);
	}
	return best;
}

bool buildDeformedPositions(const ResultDataset& dataset, const ResultBoundarySurface& surface, int fieldIndex, int step,
                            double scale, std::vector<float>& out)
{
	if (fieldIndex < 0 || static_cast<std::size_t>(fieldIndex) >= dataset.fields.size() || step < 0)
		return false;
	const ResultField& field = dataset.fields[static_cast<std::size_t>(fieldIndex)];
	if (field.association != ResultFieldAssociation::Node || field.components != 3
		|| static_cast<std::size_t>(step) >= field.stepData.size())
		return false;
	const std::vector<float>& data = field.stepData[static_cast<std::size_t>(step)];
	if (data.size() < dataset.nodeCount() * 3)
		return false;

	out = surface.positions;
	const float k = static_cast<float>(scale);
	for (std::size_t v = 0; v < surface.vertexNode.size() && v * 3 + 2 < out.size(); ++v)
	{
		const std::size_t n = surface.vertexNode[v];
		if (n * 3 + 2 >= data.size())
			continue;
		const float dx = data[n * 3], dy = data[n * 3 + 1], dz = data[n * 3 + 2];
		if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz))
			continue; // no value at this node: it does not move
		out[v * 3] += k * dx;
		out[v * 3 + 1] += k * dy;
		out[v * 3 + 2] += k * dz;
	}
	return true;
}

double maxDisplacementMagnitude(const ResultDataset& dataset, int fieldIndex)
{
	if (fieldIndex < 0 || static_cast<std::size_t>(fieldIndex) >= dataset.fields.size())
		return 0.0;
	const ResultField& field = dataset.fields[static_cast<std::size_t>(fieldIndex)];
	if (field.components != 3)
		return 0.0;
	double best = 0.0;
	for (const std::vector<float>& data : field.stepData)
	{
		for (std::size_t i = 0; i + 2 < data.size(); i += 3)
		{
			const double x = data[i], y = data[i + 1], z = data[i + 2];
			const double m = std::sqrt(x * x + y * y + z * z);
			if (std::isfinite(m))
				best = std::max(best, m);
		}
	}
	return best;
}

namespace
{
	double surfaceDiagonal(const ResultBoundarySurface& surface)
	{
		if (surface.positions.size() < 3)
			return 0.0;
		double lo[3] = { std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max() };
		double hi[3] = { std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest() };
		for (std::size_t i = 0; i + 2 < surface.positions.size(); i += 3)
			for (int k = 0; k < 3; ++k)
			{
				lo[k] = std::min<double>(lo[k], surface.positions[i + static_cast<std::size_t>(k)]);
				hi[k] = std::max<double>(hi[k], surface.positions[i + static_cast<std::size_t>(k)]);
			}
		return std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) + (hi[2] - lo[2]) * (hi[2] - lo[2]));
	}
}

bool isModalResult(const ResultDataset& dataset)
{
	if (dataset.steps.empty())
		return false;
	for (const ResultStep& step : dataset.steps)
		if (step.timeUnit != QStringLiteral("Hz"))
			return false;
	return true;
}

double modalDisplayFactor(const ResultDataset& dataset, const ResultBoundarySurface& surface, int fieldIndex, int step)
{
	if (fieldIndex < 0 || static_cast<std::size_t>(fieldIndex) >= dataset.fields.size() || step < 0)
		return 1.0;
	const ResultField& field = dataset.fields[static_cast<std::size_t>(fieldIndex)];
	if (static_cast<std::size_t>(step) >= field.stepData.size() || field.components != 3)
		return 1.0;
	double maxDisp = 0.0;
	const std::vector<float>& data = field.stepData[static_cast<std::size_t>(step)];
	for (std::size_t i = 0; i + 2 < data.size(); i += 3)
	{
		const double x = data[i], y = data[i + 1], z = data[i + 2];
		const double m = std::sqrt(x * x + y * y + z * z);
		if (std::isfinite(m))
			maxDisp = std::max(maxDisp, m);
	}
	const double diagonal = surfaceDiagonal(surface);
	return (maxDisp > 0.0 && diagonal > 0.0) ? 0.1 * diagonal / maxDisp : 1.0;
}

double autoDeformScale(const ResultDataset& dataset, const ResultBoundarySurface& surface, int fieldIndex)
{
	if (isModalResult(dataset))
		return 1.0; // one unit = the normalised amplitude
	const double maxDisp = maxDisplacementMagnitude(dataset, fieldIndex);
	if (!(maxDisp > 0.0) || surface.positions.size() < 3)
		return 1.0;
	double lo[3] = { std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max() };
	double hi[3] = { std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest() };
	for (std::size_t i = 0; i + 2 < surface.positions.size(); i += 3)
		for (int k = 0; k < 3; ++k)
		{
			lo[k] = std::min<double>(lo[k], surface.positions[i + static_cast<std::size_t>(k)]);
			hi[k] = std::max<double>(hi[k], surface.positions[i + static_cast<std::size_t>(k)]);
		}
	const double diagonal = std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) + (hi[2] - lo[2]) * (hi[2] - lo[2]));
	const double raw = 0.1 * diagonal / maxDisp;
	if (!(raw > 1.0) || !std::isfinite(raw))
		return 1.0; // already visible at true scale
	const double decade = std::pow(10.0, std::floor(std::log10(raw)));
	const double mantissa = raw / decade;
	return decade * (mantissa >= 5.0 ? 5.0 : (mantissa >= 2.0 ? 2.0 : 1.0));
}

ProbeSample sampleSurfaceScalar(const ResultDataset& dataset, const ResultBoundarySurface& surface, const DisplayScalar& scalar,
                                std::size_t triangle, float u, float v, float w, float lo, float hi)
{
	ProbeSample out;
	if (triangle * 3 + 2 >= surface.triangles.size())
		return out;
	const float weights[3] = { u, v, w };
	std::uint32_t nodes[3];
	float values[3];
	int nearest = 0;
	for (int k = 0; k < 3; ++k)
	{
		const std::uint32_t vertex = surface.triangles[triangle * 3 + static_cast<std::size_t>(k)];
		if (vertex >= surface.vertexNode.size())
			return out;
		nodes[k] = surface.vertexNode[vertex];
		values[k] = nodes[k] < scalar.nodeValues.size() ? scalar.nodeValues[nodes[k]] : std::numeric_limits<float>::quiet_NaN();
		if (weights[k] > weights[nearest])
			nearest = k;
	}
	out.node = nodes[nearest];
	out.nodeId = dataset.nodeId(nodes[nearest]);
	if (std::isfinite(values[0]) && std::isfinite(values[1]) && std::isfinite(values[2]))
		out.value = weights[0] * values[0] + weights[1] * values[1] + weights[2] * values[2];
	else if (std::isfinite(values[nearest]))
		out.value = values[nearest];
	else
		return out;
	out.valid = true;
	out.normalized = hi > lo ? std::clamp((out.value - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
	return out;
}

bool findScalarExtrema(const std::vector<float>& vertexValues, std::size_t& minVertex, std::size_t& maxVertex)
{
	bool any = false;
	float lo = 0.0f, hi = 0.0f;
	for (std::size_t i = 0; i < vertexValues.size(); ++i)
	{
		const float v = vertexValues[i];
		if (!std::isfinite(v))
			continue;
		if (!any || v < lo)
		{
			lo = v;
			minVertex = i;
		}
		if (!any || v > hi)
		{
			hi = v;
			maxVertex = i;
		}
		any = true;
	}
	return any;
}
