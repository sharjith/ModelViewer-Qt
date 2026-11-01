#include "RecordedPmiBuilder.h"
#include "RecordedPmi.h"
#include "SceneMesh.h"

#include <QObject>
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

namespace
{
// A recorded set per import is bounded: dimensions are drawn as overlays, and very PMI-heavy assemblies would otherwise add
// tens of thousands of them.
constexpr int MaxRecordedPerImport = 2000;

LengthUnit displayUnitFromName(const QString& name)
{
	const QString lower = name.trimmed().toLower();
	if (lower == QLatin1String("millimeters")) return LengthUnit::Millimeter;
	if (lower == QLatin1String("centimeters")) return LengthUnit::Centimeter;
	if (lower == QLatin1String("meters")) return LengthUnit::Meter;
	if (lower == QLatin1String("inches")) return LengthUnit::Inch;
	if (lower == QLatin1String("feet")) return LengthUnit::Foot;
	return LengthUnit::Unknown;
}

bool carriesPmi(const NodePropertySet& set)
{
	for (const NodePropertyRow& row : set.rows)
		if (row.key.startsWith(QLatin1String("PMI ")))
			return true;
	return false;
}

struct PartMeshData
{
	QUuid uuid;
	std::vector<float> points;
	std::vector<unsigned> indices;
	double diagonal = 0.0;
};
}

namespace RecordedPmi
{
BuildResult buildLinearDimensions(const std::vector<SceneMeshRecord>& meshes, LengthUnit fileUnit)
{
	BuildResult result;

	// Meshes of one part share one flattened PMI set; group them so each dimension is anchored among its own part's meshes.
	std::map<const NodePropertySet*, NodePropertySetPtr> sets;
	std::map<const NodePropertySet*, std::vector<const SceneMeshRecord*>> groups;
	std::map<const NodePropertySet*, bool> pmiCache;
	for (const SceneMeshRecord& record : meshes)
	{
		if (!record.mesh)
			continue;
		const NodePropertyChainPtr& chain = record.mesh->getNodeProperties();
		if (!chain)
			continue;
		for (const NodePropertySetPtr& set : *chain)
		{
			auto cached = pmiCache.find(set.get());
			if (cached == pmiCache.end())
				cached = pmiCache.emplace(set.get(), carriesPmi(*set)).first;
			if (!cached->second)
				continue;
			sets[set.get()] = set;
			groups[set.get()].push_back(&record);
			break; // the nearest part with PMI owns it
		}
	}

	int skippedOffMesh = 0;
	for (const auto& group : groups)
	{
		const std::vector<LinearDimension> dimensions = linearDimensions(*sets[group.first]);
		if (dimensions.empty())
			continue;
		std::vector<PartMeshData> geometry;
		geometry.reserve(group.second.size());
		double largest = 0.0;
		for (const SceneMeshRecord* record : group.second)
		{
			PartMeshData g;
			g.uuid = record->uuid;
			g.points = record->mesh->getPoints();
			g.indices = record->mesh->indices();
			float low[3] = { 1e30f, 1e30f, 1e30f }, high[3] = { -1e30f, -1e30f, -1e30f };
			for (size_t i = 0; i + 2 < g.points.size(); i += 3)
				for (int k = 0; k < 3; ++k)
				{
					low[k] = std::min(low[k], g.points[i + k]);
					high[k] = std::max(high[k], g.points[i + k]);
				}
			if (!g.points.empty())
				g.diagonal = std::sqrt(double(high[0] - low[0]) * (high[0] - low[0]) + double(high[1] - low[1]) * (high[1] - low[1])
					+ double(high[2] - low[2]) * (high[2] - low[2]));
			largest = std::max(largest, g.diagonal);
			geometry.push_back(std::move(g));
		}
		// An end point may sit off the tessellated surface: dimensions to a chamfered or rounded corner end at the original sharp
		// corner, 1 to 2 percent of the part's size away in the authentic controls. Within 3 percent the nearest surface point
		// places it (the mesh then shows its own, slightly different number); beyond that the part is more likely in another
		// frame than the dimension is on a virtual corner, and the dimension is skipped.
		const double tolerance = 0.03 * largest + 1e-9;

		auto anchorFor = [&](const QVector3D& point, MeasurementAnchorRef& ref) {
			TriangleHit best;
			const PartMeshData* owner = nullptr;
			for (const PartMeshData& g : geometry)
			{
				const TriangleHit hit = closestTriangle(g.points, g.indices, point);
				if (hit.triangle >= 0 && hit.distance < best.distance)
				{
					best = hit;
					owner = &g;
				}
			}
			if (!owner || best.distance > tolerance)
				return false;
			ref.meshUuid = owner->uuid;
			ref.triangleIndex = best.triangle;
			ref.barycentric = best.barycentric;
			return true;
		};

		for (const LinearDimension& d : dimensions)
		{
			if (result.measurements.size() >= MaxRecordedPerImport)
			{
				result.notes.append(QObject::tr("Recorded PMI dimensions capped at %1 for this import.").arg(MaxRecordedPerImport));
				return result;
			}
			Measurement m;
			MeasurementAnchorRef first, second;
			if (!anchorFor(d.from, first) || !anchorFor(d.to, second))
			{
				++skippedOffMesh;
				continue;
			}
			m.anchors = { first, second };
			m.id = QUuid::createUuid();
			m.type = MeasurementType::Distance;
			m.origin = MeasurementOrigin::Recorded;
			m.name = QObject::tr("Recorded dimension %1").arg(d.entity);
			m.recordedSource = QStringLiteral("JT PMI %1").arg(d.entity);
			m.recordedPrecision = d.precision;
			const LengthUnit shown = displayUnitFromName(d.unit);
			if (fileUnit != LengthUnit::Unknown && shown != LengthUnit::Unknown)
			{
				m.recordedValue = d.value * lengthUnitToMillimeters(fileUnit) / lengthUnitToMillimeters(shown);
				m.recordedUnit = lengthUnitToString(shown);
			}
			else if (fileUnit != LengthUnit::Unknown)
			{
				m.recordedValue = d.value;
				m.recordedUnit = lengthUnitToString(fileUnit);
			}
			else
				m.recordedValue = d.value; // file unit undeclared: the raw stored number, shown without a unit
			result.measurements.append(m);
		}
	}
	if (skippedOffMesh)
		result.notes.append(QObject::tr("%1 recorded PMI dimension(s) skipped: an end point does not lie on the mesh.").arg(skippedOffMesh));
	return result;
}
}
