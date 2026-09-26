#include "CgnsReader.h"

#if MV_HAVE_CGNS

#include <cgnslib.h>

#include <QFile>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace
{
	struct CgnsFile
	{
		int id = 0;
		bool open = false;
		~CgnsFile()
		{
			if (open)
				cg_close(id);
		}
	};

	// ---- Element types ----------------------------------------------------------------------------------------------

	int elementDimension(CGNS_ENUMT(ElementType_t) type)
	{
		switch (type)
		{
		case CGNS_ENUMV(NODE): return 0;
		case CGNS_ENUMV(BAR_2):
		case CGNS_ENUMV(BAR_3):
		case CGNS_ENUMV(BAR_4): return 1;
		case CGNS_ENUMV(TRI_3):
		case CGNS_ENUMV(TRI_6):
		case CGNS_ENUMV(TRI_9):
		case CGNS_ENUMV(TRI_10):
		case CGNS_ENUMV(QUAD_4):
		case CGNS_ENUMV(QUAD_8):
		case CGNS_ENUMV(QUAD_9):
		case CGNS_ENUMV(QUAD_12):
		case CGNS_ENUMV(QUAD_16): return 2;
		default: return 3; // tetra, pyramid, prism, hexahedron families (polyhedra are handled before this is asked)
		}
	}

	// The cell type to store and how many of the element's nodes it uses (the higher-order variants use their leading
	// nodes, which are laid out like the smaller type's).
	ResultCellType cellTypeFor(CGNS_ENUMT(ElementType_t) type, int& nodesUsed)
	{
		switch (type)
		{
		case CGNS_ENUMV(TETRA_4):  nodesUsed = 4;  return ResultCellType::Tetra;
		case CGNS_ENUMV(TETRA_10): nodesUsed = 10; return ResultCellType::Tetra10;
		case CGNS_ENUMV(PYRA_5):   nodesUsed = 5;  return ResultCellType::Pyramid;
		case CGNS_ENUMV(PYRA_13):
		case CGNS_ENUMV(PYRA_14):  nodesUsed = 13; return ResultCellType::Pyramid13;
		case CGNS_ENUMV(PENTA_6):  nodesUsed = 6;  return ResultCellType::Wedge;
		case CGNS_ENUMV(PENTA_15):
		case CGNS_ENUMV(PENTA_18): nodesUsed = 15; return ResultCellType::Wedge15;
		case CGNS_ENUMV(HEXA_8):   nodesUsed = 8;  return ResultCellType::Hexahedron;
		case CGNS_ENUMV(HEXA_20):
		case CGNS_ENUMV(HEXA_27):  nodesUsed = 20; return ResultCellType::Hexahedron20;
		case CGNS_ENUMV(TRI_3):    nodesUsed = 3;  return ResultCellType::Triangle;
		case CGNS_ENUMV(TRI_6):    nodesUsed = 6;  return ResultCellType::Triangle6;
		case CGNS_ENUMV(QUAD_4):   nodesUsed = 4;  return ResultCellType::Quad;
		case CGNS_ENUMV(QUAD_8):
		case CGNS_ENUMV(QUAD_9):   nodesUsed = 8;  return ResultCellType::Quad8;
		default: break;
		}
		nodesUsed = 0; // the caller keeps every node of an unsupported element
		return ResultCellType::Unsupported;
	}

	// ---- One zone ----------------------------------------------------------------------------------------------------

	struct CellRecord
	{
		ResultCellType type = ResultCellType::Unsupported;
		std::vector<std::uint32_t> nodes; // zone-local, 0-based
	};

	struct Section
	{
		cgsize_t start = 0;
		std::vector<CellRecord> cells;
	};

	struct Solution
	{
		int index = 0;
		bool cellCenter = false;
		std::vector<std::pair<QString, std::vector<float>>> fields; // name -> one value per node / cell
	};

	struct Zone
	{
		int base = 0, zone = 0;
		std::size_t nodeOffset = 0, cellOffset = 0; // where this zone's nodes and cells start in the merged mesh
		std::size_t nodeCount = 0, cellCount = 0;   // cells actually kept (highest dimension)
		std::vector<Solution> vertexSolutions, cellSolutions;
	};

	struct Accum
	{
		bool cell = false;
		std::vector<std::vector<float>> steps;
	};
}

bool cgnsSupported() { return true; }
QStringList cgnsExtensions() { return { QStringLiteral("cgns") }; }
QString cgnsFileFilter() { return QStringLiteral("CGNS (*.cgns)"); }

ResultReadOutcome readCgns(const QString& path, const std::atomic<bool>* cancel)
{
	ResultReadOutcome outcome;
	auto cancelled = [cancel]() { return cancel && cancel->load(std::memory_order_acquire); };
	auto fail = [&outcome](const QString& message) {
		outcome.error = message;
		return std::move(outcome);
	};
	const float nan = std::numeric_limits<float>::quiet_NaN();

	CgnsFile file;
	const QByteArray native = QFile::encodeName(path);
	if (cg_open(native.constData(), CG_MODE_READ, &file.id) != CG_OK)
		return fail(QStringLiteral("Cannot open '%1' as a CGNS file: %2").arg(path, QString::fromLatin1(cg_get_error())));
	file.open = true;
	const int fn = file.id;

	int baseCount = 0;
	if (cg_nbases(fn, &baseCount) != CG_OK || baseCount < 1)
		return fail(QStringLiteral("The CGNS file contains no base."));

	auto dataset = std::make_unique<ResultDataset>();
	dataset->sourcePath = path;
	dataset->solverName = QStringLiteral("CGNS");
	dataset->cellOffsets.push_back(0);

	std::vector<Zone> zones;
	std::size_t structuredSkipped = 0, polyhedralSections = 0, boundarySections = 0, badSolutions = 0;
	std::vector<double> baseTimes;

	// ---- Geometry: every unstructured zone of every base ------------------------------------------------------------
	for (int base = 1; base <= baseCount; ++base)
	{
		char baseName[64] = {};
		int cellDim = 0, physDim = 0;
		if (cg_base_read(fn, base, baseName, &cellDim, &physDim) != CG_OK)
			continue;

		if (baseTimes.empty())
		{
			// BaseIterativeData/TimeValues, when present: the time of each step.
			char iterName[64] = {};
			int iterSteps = 0;
			if (cg_biter_read(fn, base, iterName, &iterSteps) == CG_OK && iterSteps > 0
			    && cg_goto(fn, base, "BaseIterativeData_t", 1, "end") == CG_OK)
			{
				int arrays = 0;
				cg_narrays(&arrays);
				for (int a = 1; a <= arrays; ++a)
				{
					char arrayName[64] = {};
					CGNS_ENUMT(DataType_t) dataType;
					int dataDim = 0;
					cgsize_t dims[12] = {};
					if (cg_array_info(a, arrayName, &dataType, &dataDim, dims) != CG_OK || QLatin1String(arrayName) != QLatin1String("TimeValues")
					    || dataDim != 1 || dims[0] < 1)
						continue;
					std::vector<double> values(static_cast<std::size_t>(dims[0]));
					if (cg_array_read_as(a, CGNS_ENUMV(RealDouble), values.data()) == CG_OK)
						baseTimes = std::move(values);
					break;
				}
			}
			// The base's length unit (when metres or millimetres).
			CGNS_ENUMT(MassUnits_t) mass;
			CGNS_ENUMT(LengthUnits_t) length;
			CGNS_ENUMT(TimeUnits_t) time;
			CGNS_ENUMT(TemperatureUnits_t) temperature;
			CGNS_ENUMT(AngleUnits_t) angle;
			if (cg_goto(fn, base, "end") == CG_OK && cg_units_read(&mass, &length, &time, &temperature, &angle) == CG_OK && dataset->lengthUnit.isEmpty())
			{
				if (length == CGNS_ENUMV(Meter))
					dataset->lengthUnit = QStringLiteral("m");
				else if (length == CGNS_ENUMV(Millimeter))
					dataset->lengthUnit = QStringLiteral("mm");
			}
		}

		int zoneCount = 0;
		if (cg_nzones(fn, base, &zoneCount) != CG_OK)
			continue;
		for (int z = 1; z <= zoneCount; ++z)
		{
			if (cancelled())
				return fail(QStringLiteral("cancelled"));
			CGNS_ENUMT(ZoneType_t) zoneType;
			if (cg_zone_type(fn, base, z, &zoneType) != CG_OK)
				continue;
			if (zoneType != CGNS_ENUMV(Unstructured))
			{
				++structuredSkipped;
				continue;
			}
			char zoneName[64] = {};
			cgsize_t size[9] = {};
			if (cg_zone_read(fn, base, z, zoneName, size) != CG_OK || size[0] < 1)
				continue;
			Zone zone;
			zone.base = base;
			zone.zone = z;
			zone.nodeCount = static_cast<std::size_t>(size[0]);
			zone.nodeOffset = dataset->nodePositions.size() / 3;

			// Coordinates.
			std::vector<double> axis[3];
			int coordCount = 0;
			if (cg_ncoords(fn, base, z, &coordCount) != CG_OK)
				continue;
			for (int c = 1; c <= coordCount; ++c)
			{
				char coordName[64] = {};
				CGNS_ENUMT(DataType_t) dataType;
				if (cg_coord_info(fn, base, z, c, &dataType, coordName) != CG_OK)
					continue;
				const QString name = QString::fromLatin1(coordName);
				const int axisIndex = name.endsWith(QLatin1Char('X'), Qt::CaseInsensitive) ? 0
					: (name.endsWith(QLatin1Char('Y'), Qt::CaseInsensitive) ? 1 : (name.endsWith(QLatin1Char('Z'), Qt::CaseInsensitive) ? 2 : -1));
				if (axisIndex < 0 || !name.startsWith(QLatin1String("Coordinate")))
					continue;
				axis[axisIndex].assign(zone.nodeCount, 0.0);
				const cgsize_t rmin = 1, rmax = static_cast<cgsize_t>(zone.nodeCount);
				if (cg_coord_read(fn, base, z, coordName, CGNS_ENUMV(RealDouble), &rmin, &rmax, axis[axisIndex].data()) != CG_OK)
					axis[axisIndex].clear();
			}
			if (axis[0].empty() || axis[1].empty())
				return fail(QStringLiteral("Zone '%1' has no readable X/Y coordinates.").arg(QString::fromLatin1(zoneName)));
			for (std::size_t n = 0; n < zone.nodeCount; ++n)
			{
				dataset->nodePositions.push_back(static_cast<float>(axis[0][n]));
				dataset->nodePositions.push_back(static_cast<float>(axis[1][n]));
				dataset->nodePositions.push_back(axis[2].empty() ? 0.0f : static_cast<float>(axis[2][n]));
			}

			// Elements of the base's highest dimension, in element-number order.
			int sectionCount = 0;
			cg_nsections(fn, base, z, &sectionCount);
			std::vector<Section> sections;
			for (int s = 1; s <= sectionCount; ++s)
			{
				char sectionName[64] = {};
				CGNS_ENUMT(ElementType_t) type;
				cgsize_t start = 0, end = 0;
				int boundary = 0, parentFlag = 0;
				if (cg_section_read(fn, base, z, s, sectionName, &type, &start, &end, &boundary, &parentFlag) != CG_OK || end < start)
					continue;
				if (type == CGNS_ENUMV(NGON_n) || type == CGNS_ENUMV(NFACE_n))
				{
					++polyhedralSections;
					continue;
				}
				if (type != CGNS_ENUMV(MIXED) && elementDimension(type) != cellDim)
				{
					++boundarySections;
					continue;
				}
				cgsize_t dataSize = 0;
				if (cg_ElementDataSize(fn, base, z, s, &dataSize) != CG_OK || dataSize < 1)
					continue;
				std::vector<cgsize_t> data(static_cast<std::size_t>(dataSize));
				const std::size_t elementCount = static_cast<std::size_t>(end - start + 1);
				// A MIXED section is read through the polyhedral API (CGNS 4: one offset per element into data that
				// carries each element's type before its nodes); the older interleaved read is the fallback.
				std::vector<cgsize_t> offsets;
				bool byOffsets = false;
				if (type == CGNS_ENUMV(MIXED))
				{
					offsets.assign(elementCount + 1, 0);
					byOffsets = cg_poly_elements_read(fn, base, z, s, data.data(), offsets.data(), nullptr) == CG_OK;
				}
				if (!byOffsets && cg_elements_read(fn, base, z, s, data.data(), nullptr) != CG_OK)
					continue;

				Section section;
				section.start = start;
				std::size_t at = 0;
				for (std::size_t e = 0; e < elementCount && at < data.size(); ++e)
				{
					CGNS_ENUMT(ElementType_t) elementType = type;
					int nodesPerElement = 0;
					if (byOffsets)
					{
						at = static_cast<std::size_t>(offsets[e]);
						const std::size_t next = static_cast<std::size_t>(offsets[e + 1]);
						if (at >= data.size() || next <= at + 1 || next > data.size())
							break;
						elementType = static_cast<CGNS_ENUMT(ElementType_t)>(data[at++]); // the type precedes the nodes
						nodesPerElement = static_cast<int>(next - at);
					}
					else
					{
						if (type == CGNS_ENUMV(MIXED))
							elementType = static_cast<CGNS_ENUMT(ElementType_t)>(data[at++]); // older interleaved layout
						if (cg_npe(elementType, &nodesPerElement) != CG_OK)
							break;
					}
					if (nodesPerElement < 1 || at + static_cast<std::size_t>(nodesPerElement) > data.size())
						break;
					const bool wanted = elementDimension(elementType) == cellDim && elementType != CGNS_ENUMV(NGON_n) && elementType != CGNS_ENUMV(NFACE_n);
					if (wanted)
					{
						CellRecord cell;
						int used = 0;
						cell.type = cellTypeFor(elementType, used);
						if (used == 0)
							used = nodesPerElement;
						for (int k = 0; k < used; ++k)
						{
							const cgsize_t node = data[at + static_cast<std::size_t>(k)];
							if (node < 1 || static_cast<std::size_t>(node) > zone.nodeCount)
								return fail(QStringLiteral("An element of zone '%1' references node %2 but the zone has %3 nodes.")
								                .arg(QString::fromLatin1(zoneName)).arg(static_cast<long long>(node)).arg(zone.nodeCount));
							cell.nodes.push_back(static_cast<std::uint32_t>(node - 1));
						}
						section.cells.push_back(std::move(cell));
					}
					if (!byOffsets)
						at += static_cast<std::size_t>(nodesPerElement);
				}
				if (!section.cells.empty())
					sections.push_back(std::move(section));
			}
			std::sort(sections.begin(), sections.end(), [](const Section& a, const Section& b) { return a.start < b.start; });
			zone.cellOffset = dataset->cellTypes.size();
			for (const Section& section : sections)
				for (const CellRecord& cell : section.cells)
				{
					for (std::uint32_t node : cell.nodes)
						dataset->cellConnectivity.push_back(static_cast<std::uint32_t>(zone.nodeOffset) + node);
					dataset->cellTypes.push_back(cell.type);
					dataset->cellOffsets.push_back(static_cast<std::uint32_t>(dataset->cellConnectivity.size()));
					++zone.cellCount;
				}
			if (zone.cellCount == 0)
				continue; // nothing displayable in this zone

			// Solutions: Vertex -> node fields, CellCenter -> cell fields.
			int solutionCount = 0;
			cg_nsols(fn, base, z, &solutionCount);
			for (int s = 1; s <= solutionCount; ++s)
			{
				char solutionName[64] = {};
				CGNS_ENUMT(GridLocation_t) location;
				if (cg_sol_info(fn, base, z, s, solutionName, &location) != CG_OK)
					continue;
				const bool vertex = location == CGNS_ENUMV(Vertex), cellCenter = location == CGNS_ENUMV(CellCenter);
				if (!vertex && !cellCenter)
				{
					++badSolutions;
					continue;
				}
				Solution solution;
				solution.index = s;
				solution.cellCenter = cellCenter;
				// A CellCenter array has one value per CELL of the zone (its volume elements); when the kept cells do not
				// match that count (boundary sections mixed in, polyhedra), the solution cannot be aligned.
				const std::size_t tuples = vertex ? zone.nodeCount : zone.cellCount;
				if (cellCenter && static_cast<std::size_t>(size[1]) != zone.cellCount)
				{
					++badSolutions;
					continue;
				}
				int fieldCount = 0;
				cg_nfields(fn, base, z, s, &fieldCount);
				for (int f = 1; f <= fieldCount; ++f)
				{
					char fieldName[64] = {};
					CGNS_ENUMT(DataType_t) dataType;
					if (cg_field_info(fn, base, z, s, f, &dataType, fieldName) != CG_OK)
						continue;
					std::vector<double> values(tuples);
					const cgsize_t rmin = 1, rmax = static_cast<cgsize_t>(tuples);
					if (cg_field_read(fn, base, z, s, fieldName, CGNS_ENUMV(RealDouble), &rmin, &rmax, values.data()) != CG_OK)
						continue;
					std::vector<float> floats(values.begin(), values.end());
					solution.fields.emplace_back(QString::fromLatin1(fieldName), std::move(floats));
				}
				if (!solution.fields.empty())
					(vertex ? zone.vertexSolutions : zone.cellSolutions).push_back(std::move(solution));
			}
			zones.push_back(std::move(zone));
		}
	}

	if (zones.empty())
		return fail(structuredSkipped > 0
			? QStringLiteral("The CGNS file has only structured zones, which are not supported yet (unstructured zones are).")
			: QStringLiteral("The CGNS file has no unstructured zone with displayable elements."));

	// ---- Steps and fields --------------------------------------------------------------------------------------------
	const std::size_t totalNodes = dataset->nodePositions.size() / 3, totalCells = dataset->cellTypes.size();
	std::size_t steps = 1;
	for (const Zone& zone : zones)
		steps = std::max({ steps, zone.vertexSolutions.size(), zone.cellSolutions.size() });
	for (std::size_t s = 0; s < steps; ++s)
	{
		ResultStep step;
		step.time = s < baseTimes.size() && baseTimes.size() == steps ? baseTimes[s] : static_cast<double>(s);
		dataset->steps.push_back(step);
	}

	std::vector<QString> order; // field names in order of first appearance
	std::map<QString, Accum> accum;
	for (const Zone& zone : zones)
	{
		for (int kind = 0; kind < 2; ++kind)
		{
			const std::vector<Solution>& solutions = kind == 0 ? zone.vertexSolutions : zone.cellSolutions;
			const std::size_t total = kind == 0 ? totalNodes : totalCells;
			const std::size_t offset = kind == 0 ? zone.nodeOffset : zone.cellOffset;
			for (std::size_t s = 0; s < solutions.size() && s < steps; ++s)
				for (const auto& field : solutions[s].fields)
				{
					const QString key = (kind == 0 ? QStringLiteral("n:") : QStringLiteral("c:")) + field.first;
					auto found = accum.find(key);
					if (found == accum.end())
					{
						found = accum.emplace(key, Accum()).first;
						found->second.cell = kind == 1;
						found->second.steps.assign(steps, std::vector<float>());
						order.push_back(key);
					}
					std::vector<float>& target = found->second.steps[s];
					if (target.empty())
						target.assign(total, nan); // a zone without this field leaves NaN
					std::copy(field.second.begin(), field.second.end(), target.begin() + static_cast<std::ptrdiff_t>(offset));
				}
		}
	}

	// Vector fields: <base>X, <base>Y, <base>Z (or only X and Y) of one location become one field <base>.
	auto baseOf = [](const QString& key, QChar& axisOut) -> QString {
		const QString name = key.mid(2);
		if (name.size() < 2)
			return QString();
		const QChar last = name.back();
		if (last != QLatin1Char('X') && last != QLatin1Char('Y') && last != QLatin1Char('Z'))
			return QString();
		axisOut = last;
		return key.left(2) + name.left(name.size() - 1);
	};
	std::map<QString, std::map<QChar, QString>> vectorGroups; // location+base -> axis -> key
	for (const QString& key : order)
	{
		QChar axis;
		const QString base = baseOf(key, axis);
		if (!base.isEmpty())
			vectorGroups[base][axis] = key;
	}
	std::vector<bool> consumed(order.size(), false);
	auto indexOf = [&order](const QString& key) {
		return static_cast<std::size_t>(std::find(order.begin(), order.end(), key) - order.begin());
	};
	for (std::size_t i = 0; i < order.size(); ++i)
	{
		if (consumed[i])
			continue;
		QChar axis;
		const QString base = baseOf(order[i], axis);
		std::vector<QString> parts; // keys of the components, X Y (Z)
		if (!base.isEmpty())
		{
			const std::map<QChar, QString>& group = vectorGroups[base];
			if (group.count(QLatin1Char('X')) && group.count(QLatin1Char('Y')))
			{
				parts = { group.at(QLatin1Char('X')), group.at(QLatin1Char('Y')) };
				if (group.count(QLatin1Char('Z')))
					parts.push_back(group.at(QLatin1Char('Z')));
			}
		}
		ResultField field;
		if (parts.empty())
		{
			field.name = order[i].mid(2);
			field.association = accum[order[i]].cell ? ResultFieldAssociation::Cell : ResultFieldAssociation::Node;
			field.components = 1;
			consumed[i] = true;
			for (std::size_t s = 0; s < steps; ++s)
				field.stepData.push_back(accum[order[i]].steps[s]);
		}
		else
		{
			field.name = base.mid(2);
			field.association = accum[parts[0]].cell ? ResultFieldAssociation::Cell : ResultFieldAssociation::Node;
			field.components = 3; // a 2-D pair gets a zero z
			for (const QString& part : parts)
				consumed[indexOf(part)] = true;
			const std::size_t total = field.association == ResultFieldAssociation::Cell ? totalCells : totalNodes;
			for (std::size_t s = 0; s < steps; ++s)
			{
				bool any = false;
				for (const QString& part : parts)
					any = any || !accum[part].steps[s].empty();
				if (!any)
				{
					field.stepData.emplace_back();
					continue;
				}
				std::vector<float> values(total * 3, 0.0f);
				for (std::size_t c = 0; c < parts.size(); ++c)
				{
					const std::vector<float>& component = accum[parts[c]].steps[s];
					for (std::size_t t = 0; t < component.size() && t < total; ++t)
						values[t * 3 + c] = component[t];
				}
				field.stepData.push_back(std::move(values));
			}
		}
		bool any = false;
		for (const std::vector<float>& data : field.stepData)
			any = any || !data.empty();
		if (any)
			dataset->fields.push_back(std::move(field));
	}

	if (structuredSkipped > 0)
		outcome.warnings << QStringLiteral("%1 structured zone(s) were skipped (only unstructured zones are supported).").arg(structuredSkipped);
	if (polyhedralSections > 0)
		outcome.warnings << QStringLiteral("%1 polyhedral (NGON/NFACE) section(s) were skipped; they cannot be displayed yet.").arg(polyhedralSections);
	if (badSolutions > 0)
		outcome.warnings << QStringLiteral("%1 solution(s) were skipped: only Vertex and CellCenter solutions that match the displayed cells are read.").arg(badSolutions);
	(void)boundarySections; // boundary-condition sections are left out on purpose

	const QString invalid = dataset->validate();
	if (!invalid.isEmpty())
		return fail(QStringLiteral("The CGNS file is inconsistent: %1").arg(invalid));
	outcome.dataset = std::move(dataset);
	return outcome;
}

#else // no CGNS library in this build

bool cgnsSupported() { return false; }
QStringList cgnsExtensions() { return {}; }
QString cgnsFileFilter() { return QString(); }

ResultReadOutcome readCgns(const QString&, const std::atomic<bool>*)
{
	ResultReadOutcome outcome;
	outcome.error = QStringLiteral("This build of ModelViewer was made without the CGNS library, so CGNS result files cannot be read.");
	return outcome;
}

#endif
