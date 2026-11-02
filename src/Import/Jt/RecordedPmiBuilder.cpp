#include "Jt/RecordedPmiBuilder.h"
#include "Jt/RecordedPmi.h"
#include "RecordedPmiSanity.h"
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

// One mesh of a part, with its geometry copied once.
struct PartMeshData
{
	const SceneMeshRecord* record = nullptr;
	std::vector<float> points;
	std::vector<unsigned> indices;
	double diagonal = 0.0;
};

// The meshes that share one part's PMI, and the part's PMI set.
struct PartGroup
{
	NodePropertySetPtr set;
	std::vector<PartMeshData> meshes;
	double tolerance = 0.0;
};

// Groups the import's meshes by the nearest part that carries PMI rows, with geometry copied once per mesh.
std::vector<PartGroup> groupParts(const std::vector<SceneMeshRecord>& meshes)
{
	std::map<const NodePropertySet*, size_t> index;
	std::map<const NodePropertySet*, bool> pmiCache;
	std::vector<PartGroup> groups;
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
			auto found = index.find(set.get());
			if (found == index.end())
			{
				found = index.emplace(set.get(), groups.size()).first;
				groups.push_back({ set, {}, 0.0 });
			}
			PartMeshData data;
			data.record = &record;
			data.points = record.mesh->getPoints();
			data.indices = record.mesh->indices();
			float low[3] = { 1e30f, 1e30f, 1e30f }, high[3] = { -1e30f, -1e30f, -1e30f };
			for (size_t i = 0; i + 2 < data.points.size(); i += 3)
				for (int k = 0; k < 3; ++k)
				{
					low[k] = std::min(low[k], data.points[i + k]);
					high[k] = std::max(high[k], data.points[i + k]);
				}
			if (!data.points.empty())
				data.diagonal = std::sqrt(double(high[0] - low[0]) * (high[0] - low[0]) + double(high[1] - low[1]) * (high[1] - low[1])
					+ double(high[2] - low[2]) * (high[2] - low[2]));
			groups[found->second].meshes.push_back(std::move(data));
			break; // the nearest part with PMI owns it
		}
	}
	for (PartGroup& group : groups)
	{
		double largest = 0.0;
		for (const PartMeshData& m : group.meshes)
			largest = std::max(largest, m.diagonal);
		// An end point may sit off the tessellated surface: dimensions to a chamfered or rounded corner end at the original
		// sharp corner, 1 to 2 percent of the part's size away in the authentic controls. Within 3 percent the nearest surface
		// point places it (the mesh then shows its own, slightly different number); beyond that the part is more likely in
		// another frame than the item is on a virtual corner, and it is skipped.
		group.tolerance = 0.03 * largest + 1e-9;
	}
	return groups;
}

// A point of a part placed on its nearest mesh surface: the anchor, the owning mesh, and the surface point in the mesh's frame.
struct Placement
{
	MeasurementAnchorRef anchor;
	const PartMeshData* mesh = nullptr;
	QVector3D surfacePoint;
};

bool place(const PartGroup& group, const QVector3D& point, Placement& out)
{
	RecordedPmi::TriangleHit best;
	const PartMeshData* owner = nullptr;
	for (const PartMeshData& m : group.meshes)
	{
		const RecordedPmi::TriangleHit hit = RecordedPmi::closestTriangle(m.points, m.indices, point);
		if (hit.triangle >= 0 && hit.distance < best.distance)
		{
			best = hit;
			owner = &m;
		}
	}
	if (!owner || best.distance > group.tolerance)
		return false;
	out.mesh = owner;
	out.anchor.meshUuid = owner->record->uuid;
	out.anchor.triangleIndex = best.triangle;
	out.anchor.barycentric = best.barycentric;
	QVector3D surface;
	const size_t base = size_t(best.triangle) * 3;
	const float weight[3] = { best.barycentric.x(), best.barycentric.y(), best.barycentric.z() };
	for (int c = 0; c < 3; ++c)
	{
		const size_t v = owner->indices[base + size_t(c)] * 3;
		surface += QVector3D(owner->points[v], owner->points[v + 1], owner->points[v + 2]) * weight[c];
	}
	out.surfacePoint = surface;
	return true;
}

// The stored drawing of entity `number` of a part, placed by the render transform of `mesh`, or null when the importer kept none.
std::shared_ptr<const RecordedDrawing> drawingOf(const PartGroup& group, int number, const QUuid& mesh)
{
	if (!group.set->pmiGeometry || number < 0 || size_t(number) >= group.set->pmiGeometry->size())
		return nullptr;
	const PmiEntityGeometry& g = (*group.set->pmiGeometry)[size_t(number)];
	if (!g.hasFrame || g.vertices.size() < 6 || g.indices.size() < 2)
		return nullptr;
	auto drawing = std::make_shared<RecordedDrawing>();
	drawing->meshUuid = mesh;
	drawing->geometry = std::shared_ptr<const PmiEntityGeometry>(group.set->pmiGeometry, &g); // shares the part's data
	return drawing;
}

// A direction or offset of a part's frame carried to world space by the mesh's current render transform.
QVector3D toWorldVector(const PartMeshData& mesh, const QVector3D& local)
{
	return mesh.record->mesh->combinedRenderTransform().mapVector(local);
}
}

namespace RecordedPmi
{
BuildResult buildDimensions(const std::vector<SceneMeshRecord>& meshes, LengthUnit fileUnit)
{
	BuildResult result;
	int skippedOffMesh = 0;
	std::vector<double> ratios;
	for (const PartGroup& group : groupParts(meshes))
	{
		for (const Dimension& d : dimensions(*group.set))
		{
			if (result.measurements.size() >= MaxRecordedPerImport)
			{
				result.notes.append(QObject::tr("Recorded PMI dimensions capped at %1 for this import.").arg(MaxRecordedPerImport));
				return result;
			}
			Measurement m;
			m.id = QUuid::createUuid();
			m.origin = MeasurementOrigin::Recorded;
			m.recordedSource = QStringLiteral("JT PMI %1").arg(d.entity);
			m.recordedPrecision = d.precision;

			Placement first, second;
			bool placed = false;
			switch (d.kind)
			{
			case DimensionKind::Linear:
			case DimensionKind::Diameter:
			case DimensionKind::Radius:
				placed = place(group, d.from, first) && place(group, d.to, second);
				m.type = MeasurementType::Distance;
				m.anchors = { first.anchor, second.anchor };
				break;
			case DimensionKind::Angle:
				placed = place(group, d.vertex, first);
				m.type = MeasurementType::Point;
				m.anchors = { first.anchor };
				break;
			}
			if (!placed)
			{
				++skippedOffMesh;
				continue;
			}
			switch (d.kind)
			{
			case DimensionKind::Linear:
				m.recordedKind = RecordedKind::Distance;
				m.name = QObject::tr("Recorded dimension %1").arg(d.entity);
				// Where the author put the dimension line: the stored text's offset from the measured segment.
				if (d.hasTextOrigin && first.mesh)
				{
					const QVector3D axis = (d.to - d.from);
					if (axis.lengthSquared() > 1e-12f)
					{
						const QVector3D unit = axis.normalized();
						const QVector3D toText = d.textOrigin - d.from;
						const QVector3D offset = toText - unit * QVector3D::dotProduct(toText, unit);
						if (offset.lengthSquared() > 1e-12f)
							m.offsetVector = toWorldVector(*first.mesh, offset);
					}
				}
				break;
			case DimensionKind::Diameter:
				m.recordedKind = RecordedKind::Diameter;
				m.name = QObject::tr("Recorded diameter %1").arg(d.entity);
				break;
			case DimensionKind::Radius:
				m.recordedKind = RecordedKind::Radius;
				m.name = QObject::tr("Recorded radius %1").arg(d.entity);
				if (d.hasTextOrigin)
				{
					const QVector3D centre = (d.from + d.to) * 0.5f;
					m.recordedTextInside = (d.textOrigin - centre).length() < (d.from - centre).length();
				}
				break;
			case DimensionKind::Angle:
				m.recordedKind = RecordedKind::Angle;
				m.name = QObject::tr("Recorded angle %1").arg(d.entity);
				break;
			}

			if (d.kind == DimensionKind::Angle)
			{
				// Observed on one exporter: the stored angle is in radians.
				m.recordedValue = d.value * 180.0 / 3.14159265358979323846;
				m.recordedUnit = QStringLiteral("deg");
			}
			else
			{
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
			}
			m.recordedDrawing = drawingOf(group, d.entity, first.anchor.meshUuid);
			// For the import's consistency check: authored and mesh length in the part's own units, when both ends lie on one mesh.
			if (d.kind != DimensionKind::Angle && first.mesh && first.mesh == second.mesh)
			{
				const double mesh = double((second.surfacePoint - first.surfacePoint).length());
				const double authored = d.kind == DimensionKind::Radius ? d.value * 2.0 : d.value;
				if (mesh > 1e-9 && authored > 0.0)
					ratios.push_back(authored / mesh);
			}
			result.measurements.append(m);
		}
	}
	if (skippedOffMesh)
		result.notes.append(QObject::tr("%1 recorded PMI dimension(s) skipped: an end point does not lie on the mesh.").arg(skippedOffMesh));
	const SanityReport sanity = checkAgainstMesh(ratios);
	result.notes.append(sanity.notes);
	result.warning = sanity.warning;
	return result;
}

AnnotationResult buildAnnotations(const std::vector<SceneMeshRecord>& meshes)
{
	AnnotationResult result;
	int skippedOffMesh = 0;
	for (const PartGroup& group : groupParts(meshes))
	{
		for (const Note& n : notes(*group.set))
		{
			if (result.annotations.size() >= MaxRecordedPerImport)
			{
				result.notes.append(QObject::tr("Recorded PMI annotations capped at %1 for this import.").arg(MaxRecordedPerImport));
				return result;
			}
			Placement at;
			if (!place(group, n.anchor, at))
			{
				++skippedOffMesh;
				continue;
			}
			Annotation a;
			a.id = QUuid::createUuid();
			a.anchor = at.anchor;
			a.visible = true;
			a.recordedSource = QStringLiteral("JT PMI %1").arg(n.entity);
			a.text = n.text;
			switch (n.entityType)
			{
			case TypeCode::Note: a.recordedKind = QStringLiteral("Note"); break;
			case TypeCode::BalloonNote: a.recordedKind = QStringLiteral("Balloon note"); break;
			case TypeCode::DatumTarget: a.recordedKind = QStringLiteral("Datum target"); break;
			case TypeCode::Region: a.recordedKind = QStringLiteral("Region"); break;
			case TypeCode::SurfaceFinish:
				a.recordedKind = QStringLiteral("Surface finish");
				if (a.text.isEmpty())
					a.text = QObject::tr("Surface texture symbol");
				break;
			default: continue;
			}
			if (a.text.isEmpty())
				a.text = a.recordedKind;
			// Where the author put the text: its offset from the point the symbol marks (only symbols that have a leader).
			if (n.hasLeader && n.hasTextOrigin)
			{
				const QVector3D offset = n.textOrigin - at.surfacePoint;
				if (offset.lengthSquared() > 1e-12f)
					a.leaderOffset = toWorldVector(*at.mesh, offset);
			}
			a.recordedDrawing = drawingOf(group, n.entity, at.anchor.meshUuid);
			result.annotations.append(a);
		}
	}
	if (skippedOffMesh)
		result.notes.append(QObject::tr("%1 recorded PMI annotation(s) skipped: the point they mark does not lie on the mesh.").arg(skippedOffMesh));
	return result;
}
}
