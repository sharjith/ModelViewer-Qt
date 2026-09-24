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
	const std::size_t vertexCount = surface.vertexCount();
	std::vector<double> sum(vertexCount * 3, 0.0);
	for (std::size_t t = 0; t + 2 < surface.triangles.size(); t += 3)
	{
		const std::uint32_t a = surface.triangles[t], b = surface.triangles[t + 1], c = surface.triangles[t + 2];
		const float* pa = &surface.positions[a * 3];
		const float* pb = &surface.positions[b * 3];
		const float* pc = &surface.positions[c * 3];
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
