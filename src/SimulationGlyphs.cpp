#include "SimulationGlyphs.h"

#include "SimulationResultDisplay.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace
{
	struct Best
	{
		std::uint32_t index = 0;
		double distance2 = 0.0;
	};

	// One point per cell of a grid with edge `cell`: the one nearest the cell's centre.
	std::unordered_map<std::uint64_t, Best> pickPerCell(const std::vector<float>& points, const double lo[3], double cell)
	{
		std::unordered_map<std::uint64_t, Best> cells;
		const std::size_t n = points.size() / 3;
		for (std::size_t i = 0; i < n; ++i)
		{
			const double p[3] = { points[i * 3], points[i * 3 + 1], points[i * 3 + 2] };
			if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]))
				continue;
			std::uint64_t key = 0;
			double distance2 = 0.0;
			for (int a = 0; a < 3; ++a)
			{
				const double cellIndex = std::floor((p[a] - lo[a]) / cell);
				const double centre = lo[a] + (cellIndex + 0.5) * cell;
				distance2 += (p[a] - centre) * (p[a] - centre);
				key = (key << 21) | (static_cast<std::uint64_t>(cellIndex) & 0x1FFFFF);
			}
			auto found = cells.find(key);
			if (found == cells.end())
				cells.emplace(key, Best{ static_cast<std::uint32_t>(i), distance2 });
			else if (distance2 < found->second.distance2)
				found->second = Best{ static_cast<std::uint32_t>(i), distance2 };
		}
		return cells;
	}
}

std::vector<std::uint32_t> selectGlyphSites(const std::vector<float>& points, std::size_t target)
{
	std::vector<std::uint32_t> result;
	const std::size_t n = points.size() / 3;
	if (target == 0 || n == 0)
		return result;

	double lo[3] = { std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max() };
	double hi[3] = { std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest() };
	std::size_t finite = 0;
	for (std::size_t i = 0; i < n; ++i)
	{
		bool ok = true;
		for (int a = 0; a < 3; ++a)
			ok = ok && std::isfinite(points[i * 3 + static_cast<std::size_t>(a)]);
		if (!ok)
			continue;
		++finite;
		for (int a = 0; a < 3; ++a)
		{
			const double v = points[i * 3 + static_cast<std::size_t>(a)];
			lo[a] = std::min(lo[a], v);
			hi[a] = std::max(hi[a], v);
		}
	}
	if (finite == 0)
		return result;

	if (finite <= target)
	{
		for (std::size_t i = 0; i < n; ++i)
			if (std::isfinite(points[i * 3]) && std::isfinite(points[i * 3 + 1]) && std::isfinite(points[i * 3 + 2]))
				result.push_back(static_cast<std::uint32_t>(i));
		return result;
	}

	const double extent = std::max({ hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2] });
	if (!(extent > 0.0))
	{
		result.push_back(0); // all the points coincide
		return result;
	}
	// The points lie on a surface, so about area / cell^2 cells are occupied: start from a square-root guess and
	// correct the cell size by how far the count is off (a few passes are enough; the count need not be exact).
	const double smallest = extent / 1.0e6; // keeps the cell index inside its 21 bits
	double cell = std::max(extent / std::sqrt(static_cast<double>(target)), smallest);
	std::unordered_map<std::uint64_t, Best> cells;
	for (int pass = 0; pass < 8; ++pass)
	{
		cells = pickPerCell(points, lo, cell);
		const double ratio = static_cast<double>(cells.size()) / static_cast<double>(target);
		if (ratio > 0.8 && ratio < 1.25)
			break;
		cell = std::max(cell * std::sqrt(ratio), smallest);
	}

	result.reserve(cells.size());
	for (const auto& entry : cells)
		result.push_back(entry.second.index);
	std::sort(result.begin(), result.end());
	if (result.size() > target + target / 2)
	{
		const std::size_t stride = (result.size() + target - 1) / target;
		std::vector<std::uint32_t> thinned;
		thinned.reserve(target + 1);
		for (std::size_t i = 0; i < result.size(); i += stride)
			thinned.push_back(result[i]);
		result = std::move(thinned);
	}
	return result;
}

std::vector<std::uint32_t> selectSurfaceGlyphSites(const ResultBoundarySurface& surface, bool cellField, std::size_t target)
{
	if (!cellField)
		return selectGlyphSites(surface.positions, target);
	// One site per boundary triangle, at its centre.
	std::vector<float> centres(surface.triangleCount() * 3);
	for (std::size_t t = 0; t < surface.triangleCount(); ++t)
		for (std::size_t a = 0; a < 3; ++a)
		{
			float sum = 0.0f;
			for (std::size_t k = 0; k < 3; ++k)
				sum += surface.positions[static_cast<std::size_t>(surface.triangles[t * 3 + k]) * 3 + a];
			centres[t * 3 + a] = sum / 3.0f;
		}
	return selectGlyphSites(centres, target);
}

double surfaceDiagonal(const ResultBoundarySurface& surface)
{
	if (surface.positions.size() < 3)
		return 0.0;
	double lo[3] = { std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max() };
	double hi[3] = { std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest() };
	for (std::size_t i = 0; i + 2 < surface.positions.size(); i += 3)
		for (std::size_t a = 0; a < 3; ++a)
		{
			const double v = surface.positions[i + a];
			if (!std::isfinite(v))
				continue;
			lo[a] = std::min(lo[a], v);
			hi[a] = std::max(hi[a], v);
		}
	double sum = 0.0;
	for (int a = 0; a < 3; ++a)
		if (hi[a] >= lo[a])
			sum += (hi[a] - lo[a]) * (hi[a] - lo[a]);
	return std::sqrt(sum);
}

bool isGlyphField(const ResultField& field)
{
	return field.components == 3 && resultFieldHasData(field);
}

int chooseDefaultGlyphField(const ResultDataset& dataset)
{
	int velocity = -1, displacement = -1, other = -1;
	for (int pass = 0; pass < 2; ++pass) // node fields first, then cell fields
	{
		const ResultFieldAssociation wanted = pass == 0 ? ResultFieldAssociation::Node : ResultFieldAssociation::Cell;
		for (std::size_t i = 0; i < dataset.fields.size(); ++i)
		{
			const ResultField& f = dataset.fields[i];
			if (f.association != wanted || !isGlyphField(f) || f.derivedFromField >= 0)
				continue;
			const QString name = f.name.toLower();
			const int index = static_cast<int>(i);
			if (velocity < 0 && (name.startsWith(QLatin1String("vel")) || name == QLatin1String("u")))
				velocity = index;
			else if (displacement < 0 && (name.contains(QLatin1String("displ")) || name.startsWith(QLatin1String("disp"))))
				displacement = index;
			else if (other < 0)
				other = index;
		}
		if (velocity >= 0 || displacement >= 0 || other >= 0)
			break;
	}
	return velocity >= 0 ? velocity : (displacement >= 0 ? displacement : other);
}

bool buildGlyphSet(const ResultDataset& dataset, const ResultBoundarySurface& surface, int fieldIndex, int step,
                   const std::vector<std::uint32_t>& sites, double diagonal, const GlyphOptions& options,
                   float referenceMax, GlyphSet& out)
{
	out.clear();
	if (fieldIndex < 0 || static_cast<std::size_t>(fieldIndex) >= dataset.fields.size() || step < 0)
		return false;
	const ResultField& field = dataset.fields[static_cast<std::size_t>(fieldIndex)];
	if (field.components != 3 || static_cast<std::size_t>(step) >= field.stepData.size() || field.stepData[static_cast<std::size_t>(step)].empty())
		return false;
	DisplayScalar magnitudes;
	if (!buildDisplayScalar(dataset, fieldIndex, -1, magnitudes, step))
		return false;
	const std::vector<float>& raw = field.stepData[static_cast<std::size_t>(step)];
	out.fieldMin = magnitudes.minValue;
	out.fieldMax = magnitudes.maxValue;
	out.unit = magnitudes.unit;
	const bool cellField = field.association == ResultFieldAssociation::Cell;
	const float largest = referenceMax > 0.0f ? referenceMax : magnitudes.maxValue;
	const double fullLength = std::max(options.scale, 0.0) * 0.05 * diagonal;
	if (!(largest > 0.0f) || !(fullLength > 0.0))
		return false;

	for (std::uint32_t site : sites)
	{
		std::uint32_t anchor[3];
		std::size_t tuple;
		if (cellField)
		{
			if (static_cast<std::size_t>(site) >= surface.triangleCount() || static_cast<std::size_t>(site) >= surface.triangleCell.size())
				continue;
			for (std::size_t k = 0; k < 3; ++k)
				anchor[k] = surface.triangles[static_cast<std::size_t>(site) * 3 + k];
			tuple = surface.triangleCell[site];
		}
		else
		{
			if (static_cast<std::size_t>(site) >= surface.vertexCount() || static_cast<std::size_t>(site) >= surface.vertexNode.size())
				continue;
			anchor[0] = anchor[1] = anchor[2] = site;
			tuple = surface.vertexNode[site];
		}
		if (tuple >= magnitudes.nodeValues.size() || tuple * 3 + 2 >= raw.size())
			continue;
		const float magnitude = magnitudes.nodeValues[tuple];
		const double x = raw[tuple * 3], y = raw[tuple * 3 + 1], z = raw[tuple * 3 + 2];
		const double length = std::sqrt(x * x + y * y + z * z);
		if (!std::isfinite(magnitude) || !(magnitude > 0.0f) || !std::isfinite(length) || !(length > 0.0))
			continue;
		// Never shorter than a few percent of the full length, so a small vector is still a visible arrow.
		const double fraction = options.scaleByMagnitude ? std::clamp(static_cast<double>(magnitude) / largest, 0.04, 1.0) : 1.0;
		const double factor = fullLength * fraction / length;
		out.anchors.insert(out.anchors.end(), { anchor[0], anchor[1], anchor[2] });
		out.vectors.insert(out.vectors.end(), { static_cast<float>(x * factor), static_cast<float>(y * factor), static_cast<float>(z * factor) });
		out.values.push_back(magnitude);
	}
	if (out.count() == 0)
		return false;
	return true;
}
