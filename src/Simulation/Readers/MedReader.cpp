#include "MedReader.h"

#if MV_HAVE_HDF5

#include "Hdf5Util.h"
#include "ResultDerivedFields.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <utility>

using namespace hdf5util;

namespace
{
	struct Geometry
	{
		const char* code;
		ResultCellType type;
		int dimension;
		int nodes; // nodes per cell, all of them (mid-edge nodes included)
	};

	// MED geometric type codes. Corner nodes come first in every MED cell, like in VTK, and the boundary extractor orients faces
	// itself, so the connectivity is used as stored.
	const Geometry kGeometries[] = {
		{ "SE2", ResultCellType::Line, 1, 2 },        { "TR3", ResultCellType::Triangle, 2, 3 },     { "TR6", ResultCellType::Triangle6, 2, 6 },
		{ "QU4", ResultCellType::Quad, 2, 4 },        { "QU8", ResultCellType::Quad8, 2, 8 },        { "TE4", ResultCellType::Tetra, 3, 4 },
		{ "TE10", ResultCellType::Tetra10, 3, 10 },   { "PY5", ResultCellType::Pyramid, 3, 5 },      { "PY13", ResultCellType::Pyramid13, 3, 13 },
		{ "PE6", ResultCellType::Wedge, 3, 6 },       { "PE15", ResultCellType::Wedge15, 3, 15 },    { "HE8", ResultCellType::Hexahedron, 3, 8 },
		{ "HE20", ResultCellType::Hexahedron20, 3, 20 },
	};

	const Geometry* findGeometry(const QString& code)
	{
		for (const Geometry& g : kGeometries)
			if (code == QLatin1String(g.code))
				return &g;
		return nullptr;
	}

	// A group of the mesh: how many cells it has, where they start in the dataset, and their type.
	struct CellBlock
	{
		const Geometry* geometry = nullptr;
		std::size_t count = 0;
		std::size_t first = 0; // the dataset index of its first cell
		bool kept = false;
	};

	// One (NDT, NOR) pair - a MED time step - ordered numerically (-1, the "no time step" number, first).
	struct StepKey
	{
		int dt = -1, it = -1;
		bool operator<(const StepKey& o) const { return dt != o.dt ? dt < o.dt : it < o.it; }
	};

	QString trimmed(const QString& text)
	{
		QString t = text;
		t.remove(QChar(0));
		return t.trimmed();
	}

	// MED stores a list of names as one string of fixed 16-character fields ("comp1           comp2           ").
	QStringList splitNames(const QString& text, int count)
	{
		QStringList names;
		for (int i = 0; i < count; ++i)
			names << trimmed(text.mid(i * 16, 16));
		return names;
	}

	bool readInt(hid_t object, const char* name, int& value)
	{
		std::vector<int> values;
		if (!readNumericAttribute<int>(object, name, values))
			return false;
		value = values[0];
		return true;
	}

	bool readStepKey(hid_t group, StepKey& key, double& time)
	{
		std::vector<double> pdt;
		if (!readInt(group, "NDT", key.dt) || !readInt(group, "NOR", key.it))
			return false;
		time = readNumericAttribute<double>(group, "PDT", pdt) ? pdt[0] : 0.0;
		return true;
	}

	// The component order the shown fields use for a symmetric tensor: XX YY ZZ XY YZ XZ. Returns the source index of each, or an empty
	// vector when the names do not identify them all.
	std::vector<int> tensorOrder(const QStringList& names)
	{
		if (names.size() != 6)
			return {};
		const char* wanted[6][2] = { { "XX", nullptr }, { "YY", nullptr }, { "ZZ", nullptr }, { "XY", "YX" }, { "YZ", "ZY" }, { "XZ", "ZX" } };
		std::vector<int> order;
		for (const auto& target : wanted)
		{
			int found = -1;
			for (int i = 0; i < names.size() && found < 0; ++i)
			{
				const QString upper = names[i].toUpper();
				for (const char* suffix : target)
					if (suffix && upper.endsWith(QLatin1String(suffix)) && std::find(order.begin(), order.end(), i) == order.end())
						found = i;
			}
			if (found < 0)
				return {};
			order.push_back(found);
		}
		return order;
	}

	struct FieldAccum
	{
		ResultField field;
		bool any = false;
	};
}

bool medSupported() { return true; }
QStringList medExtensions() { return { QStringLiteral("med") }; }
QString medFileFilter() { return QStringLiteral("MED (*.med)"); }

ResultReadOutcome readMed(const QString& path, const std::atomic<bool>* cancel)
{
	ResultReadOutcome outcome;
	auto cancelled = [cancel]() { return cancel && cancel->load(std::memory_order_acquire); };
	auto fail = [&outcome](const QString& message) {
		outcome.error = message;
		return std::move(outcome);
	};
	const float nan = std::numeric_limits<float>::quiet_NaN();
	ErrorSilencer silence;

	const QByteArray native = path.toUtf8();
	Handle file(H5Fopen(native.constData(), H5F_ACC_RDONLY, H5P_DEFAULT), H5Fclose);
	if (!file.ok())
		return fail(QStringLiteral("Cannot open '%1' as an HDF5 file.").arg(path));
	if (!linkExists(file, QStringLiteral("ENS_MAA")))
		return fail(QStringLiteral("'%1' is an HDF5 file but not a MED file (it has no /ENS_MAA group).").arg(path));

	// ---- Version: 3.x and later have the layout read below.
	int major = 0, minor = 0, release = 0;
	if (linkExists(file, QStringLiteral("INFOS_GENERALES")))
	{
		Handle info = openGroup(file, QStringLiteral("INFOS_GENERALES"));
		readInt(info, "MAJ", major);
		readInt(info, "MIN", minor);
		readInt(info, "REL", release);
	}
	if (major > 0 && major < 3)
		return fail(QStringLiteral("This is a MED %1.%2.%3 file. Only MED 3 and later (written by Salome 6 and newer) can be read; convert it with medimport "
		                           "(SALOME) or re-export it from Salome.").arg(major).arg(minor).arg(release));

	auto dataset = std::make_unique<ResultDataset>();
	dataset->sourcePath = path;
	dataset->solverName = QStringLiteral("MED");

	// ---- Which mesh: the one most of the fields belong to, else the first.
	Handle meshes = openGroup(file, QStringLiteral("ENS_MAA"));
	const QStringList meshNames = childNames(meshes);
	if (meshNames.isEmpty())
		return fail(QStringLiteral("The MED file contains no mesh."));
	const bool haveFields = linkExists(file, QStringLiteral("CHA"));
	Handle fieldsGroup = haveFields ? openGroup(file, QStringLiteral("CHA")) : Handle();
	const QStringList fieldNames = haveFields ? childNames(fieldsGroup) : QStringList();
	QString meshName = meshNames.first();
	{
		std::map<QString, int> votes;
		for (const QString& f : fieldNames)
		{
			Handle group = openGroup(fieldsGroup, f);
			QString mesh;
			if (group.ok() && readStringAttribute(group, "MAI", mesh))
				++votes[trimmed(mesh)];
		}
		int best = 0;
		for (const auto& vote : votes)
			for (const QString& candidate : meshNames)
				if (trimmed(candidate) == vote.first && vote.second > best)
				{
					best = vote.second;
					meshName = candidate;
				}
	}
	if (meshNames.size() > 1)
		outcome.warnings << QStringLiteral("The file has %1 meshes; only '%2' (and the fields on it) is shown.").arg(meshNames.size()).arg(trimmed(meshName));

	Handle mesh = openGroup(meshes, meshName);
	if (!mesh.ok())
		return fail(QStringLiteral("Cannot open the mesh '%1'.").arg(meshName));
	int spaceDimension = 0;
	readInt(mesh, "ESP", spaceDimension);

	// The mesh's first computation step (a mesh that changes over time is read at its first step).
	QString stepName;
	{
		StepKey best;
		bool have = false;
		for (const QString& name : childNames(mesh))
		{
			Handle group = openGroup(mesh, name);
			StepKey key;
			double time = 0.0;
			if (group.ok() && readStepKey(group, key, time) && (!have || key < best))
			{
				best = key;
				stepName = name;
				have = true;
			}
		}
		if (!have)
			return fail(QStringLiteral("The mesh '%1' has no computation step group (a MED 2.x layout, or a structured mesh).").arg(trimmed(meshName)));
	}
	Handle step = openGroup(mesh, stepName);

	// ---- Nodes
	{
		Handle coordinates = openDataset(step, QStringLiteral("NOE/COO"));
		if (!coordinates.ok())
			return fail(QStringLiteral("The mesh has no node coordinates (structured meshes are not supported)."));
		std::vector<double> values;
		const std::vector<hsize_t> dims = datasetDims(coordinates);
		if (dims.empty() || !readRows<double>(coordinates, 0, dims[0], values))
			return fail(QStringLiteral("Cannot read the node coordinates."));
		int nodeCount = 0;
		if (!readInt(coordinates, "NBR", nodeCount) || nodeCount < 1)
			nodeCount = spaceDimension > 0 ? static_cast<int>(values.size()) / spaceDimension : 0;
		if (nodeCount < 1 || values.size() % static_cast<std::size_t>(nodeCount) != 0)
			return fail(QStringLiteral("The node coordinate array does not fit its node count."));
		const std::size_t space = values.size() / static_cast<std::size_t>(nodeCount);
		if (space < 1 || space > 3)
			return fail(QStringLiteral("Unexpected coordinate dimension %1.").arg(space));
		dataset->nodePositions.assign(static_cast<std::size_t>(nodeCount) * 3, 0.0f);
		for (std::size_t c = 0; c < space; ++c) // component-major: every x, then every y ...
			for (std::size_t n = 0; n < static_cast<std::size_t>(nodeCount); ++n)
				dataset->nodePositions[n * 3 + c] = static_cast<float>(values[c * static_cast<std::size_t>(nodeCount) + n]);
		std::vector<long long> numbers;
		if (readAll<long long>(step, QStringLiteral("NOE/NUM"), numbers) && numbers.size() == static_cast<std::size_t>(nodeCount))
			dataset->nodeIds.assign(numbers.begin(), numbers.end());
		// The length unit, when every coordinate has the same one.
		QString units;
		if (readStringAttribute(mesh, "UNI", units) && space >= 1)
		{
			const QStringList list = splitNames(units, static_cast<int>(space));
			const QString unit = list.first().toLower();
			if (std::all_of(list.begin(), list.end(), [&](const QString& u) { return u.toLower() == unit; }))
			{
				if (unit == QLatin1String("m") || unit == QLatin1String("cm") || unit == QLatin1String("mm"))
					dataset->lengthUnit = unit;
			}
		}
	}
	const std::size_t nodeTotal = dataset->nodePositions.size() / 3;

	// ---- Cells: the types of the highest dimension present
	std::vector<CellBlock> blocks;
	{
		if (!linkExists(step, QStringLiteral("MAI")))
			return fail(QStringLiteral("The mesh has no cells."));
		Handle cells = openGroup(step, QStringLiteral("MAI"));
		int highest = 0;
		std::size_t unsupportedGroups = 0;
		for (const QString& code : childNames(cells))
		{
			const Geometry* g = findGeometry(code);
			if (!g)
			{
				++unsupportedGroups;
				continue;
			}
			CellBlock block;
			block.geometry = g;
			Handle nodes = openDataset(cells, code + QStringLiteral("/NOD"));
			int count = 0;
			if (!nodes.ok() || !readInt(nodes, "NBR", count) || count < 1)
				continue;
			block.count = static_cast<std::size_t>(count);
			blocks.push_back(block);
			highest = std::max(highest, g->dimension);
		}
		if (highest == 0)
			return fail(QStringLiteral("The mesh has no cells that can be displayed (points, polygons and polyhedra are not supported)."));
		std::size_t dropped = 0;
		dataset->cellOffsets.push_back(0);
		std::vector<long long> numbers;
		bool allNumbers = true;
		for (CellBlock& block : blocks)
		{
			if (block.geometry->dimension != highest)
			{
				dropped += block.count;
				continue;
			}
			const QString code = QString::fromLatin1(block.geometry->code);
			std::vector<long long> connectivity;
			if (!readAll<long long>(cells, code + QStringLiteral("/NOD"), connectivity) || connectivity.size() != block.count * static_cast<std::size_t>(block.geometry->nodes))
			{
				outcome.warnings << QStringLiteral("The %1 cells were skipped: their connectivity does not have %2 nodes per cell.").arg(code).arg(block.geometry->nodes);
				continue;
			}
			block.kept = true;
			block.first = dataset->cellTypes.size();
			const std::size_t per = static_cast<std::size_t>(block.geometry->nodes);
			for (std::size_t c = 0; c < block.count; ++c)
			{
				for (std::size_t k = 0; k < per; ++k) // component-major: node k of every cell, then node k + 1 ...
				{
					const long long node = connectivity[k * block.count + c];
					if (node < 1 || static_cast<std::size_t>(node) > nodeTotal)
						return fail(QStringLiteral("A %1 cell references node %2 but the mesh has %3 nodes.").arg(code).arg(node).arg(nodeTotal));
					dataset->cellConnectivity.push_back(static_cast<std::uint32_t>(node - 1));
				}
				dataset->cellTypes.push_back(block.geometry->type);
				dataset->cellOffsets.push_back(static_cast<std::uint32_t>(dataset->cellConnectivity.size()));
			}
			std::vector<long long> ids;
			if (readAll<long long>(cells, code + QStringLiteral("/NUM"), ids) && ids.size() == block.count)
				numbers.insert(numbers.end(), ids.begin(), ids.end());
			else
				allNumbers = false;
		}
		if (dataset->cellTypes.empty())
			return fail(QStringLiteral("No cell of the mesh could be read."));
		if (allNumbers && numbers.size() == dataset->cellTypes.size())
			dataset->cellIds.assign(numbers.begin(), numbers.end());
		if (dropped > 0)
			outcome.warnings << QStringLiteral("%1 lower-dimension cell(s) (boundary faces, edges) were left out; only the cells of the mesh's highest dimension are shown.")
			                        .arg(dropped);
		if (unsupportedGroups > 0)
			outcome.warnings << QStringLiteral("%1 cell group(s) of unsupported types (points, polygons, polyhedra, 27-node hexahedra ...) were skipped.").arg(unsupportedGroups);
	}
	if (cancelled())
		return fail(QStringLiteral("cancelled"));

	// ---- Time steps: every (NDT, NOR) of every field of this mesh
	struct FieldRef
	{
		QString name;
		int components = 1;
		QStringList componentNames;
		std::vector<std::pair<StepKey, QString>> steps; // step key -> its group name
	};
	std::vector<FieldRef> refs;
	std::map<StepKey, double> stepTimes;
	for (const QString& fieldName : fieldNames)
	{
		Handle group = openGroup(fieldsGroup, fieldName);
		if (!group.ok())
			continue;
		QString onMesh;
		if (readStringAttribute(group, "MAI", onMesh) && !onMesh.isEmpty() && trimmed(onMesh) != trimmed(meshName))
			continue; // a field of another mesh
		FieldRef ref;
		ref.name = trimmed(fieldName);
		readInt(group, "NCO", ref.components);
		if (ref.components < 1)
			continue;
		QString names;
		if (readStringAttribute(group, "NOM", names))
			ref.componentNames = splitNames(names, ref.components);
		for (const QString& stepGroupName : childNames(group))
		{
			Handle stepGroup = openGroup(group, stepGroupName);
			StepKey key;
			double time = 0.0;
			if (!stepGroup.ok() || !readStepKey(stepGroup, key, time))
				continue; // not a step group
			ref.steps.emplace_back(key, stepGroupName);
			stepTimes.emplace(key, time);
		}
		if (!ref.steps.empty())
			refs.push_back(std::move(ref));
	}
	std::map<StepKey, std::size_t> stepIndex;
	{
		std::vector<double> times;
		for (const auto& entry : stepTimes)
		{
			stepIndex[entry.first] = dataset->steps.size();
			ResultStep s;
			s.time = entry.second;
			dataset->steps.push_back(s);
			times.push_back(entry.second);
		}
		if (dataset->steps.empty())
			dataset->steps.push_back(ResultStep()); // a mesh without fields
		// No time values (all equal, e.g. 0 with several numbered steps): number the steps instead.
		if (dataset->steps.size() > 1 && std::adjacent_find(times.begin(), times.end(), std::not_equal_to<double>()) == times.end())
			for (std::size_t i = 0; i < dataset->steps.size(); ++i)
				dataset->steps[i].time = static_cast<double>(i);
	}
	const std::size_t stepCount = dataset->steps.size();

	// ---- Fields
	std::size_t averaged = 0, skippedEntities = 0;
	QStringList unplaced; // "field (entity)" of value blocks whose layout did not fit the mesh
	std::map<std::pair<QString, int>, FieldAccum> accum; // (name, association) -> field
	std::vector<std::pair<QString, int>> order;
	for (const FieldRef& ref : refs)
	{
		if (cancelled())
			return fail(QStringLiteral("cancelled"));
		// The group name as stored (it may carry trailing blanks the trimmed name lost).
		QString storedName;
		for (const QString& candidate : fieldNames)
			if (trimmed(candidate) == ref.name)
			{
				storedName = candidate;
				break;
			}
		Handle fieldGroup = openGroup(fieldsGroup, storedName);
		if (!fieldGroup.ok())
			continue;
		// How the file's components map onto the shown fields. Normally one field: a symmetric tensor in the standard order, a 2-D vector
		// padded to 3 components. A shell's DX DY DZ DRX DRY DRZ becomes a translation vector (which can be shown deformed) and a
		// rotation vector.
		struct Target
		{
			QString name;
			int components = 1;
		};
		std::vector<Target> targets;
		std::vector<std::pair<int, int>> mapping(static_cast<std::size_t>(ref.components), std::make_pair(0, 0)); // component -> (target, component in it)
		{
			const std::vector<int> tensor = ref.components == 6 ? tensorOrder(ref.componentNames) : std::vector<int>();
			std::vector<int> shell(6, -1); // DX DY DZ DRX DRY DRZ
			if (ref.components == 6 && tensor.empty())
			{
				const char* wanted[6] = { "DX", "DY", "DZ", "DRX", "DRY", "DRZ" };
				for (int k = 0; k < 6; ++k)
					for (int i = 0; i < ref.componentNames.size(); ++i)
						if (ref.componentNames[i].toUpper() == QLatin1String(wanted[k]))
							shell[static_cast<std::size_t>(k)] = i;
				if (std::find(shell.begin(), shell.end(), -1) != shell.end())
					shell.clear();
			}
			else
				shell.clear();
			if (!tensor.empty())
			{
				targets.push_back({ ref.name, 6 });
				for (int t = 0; t < 6; ++t)
					mapping[static_cast<std::size_t>(tensor[static_cast<std::size_t>(t)])] = std::make_pair(0, t);
			}
			else if (!shell.empty())
			{
				targets.push_back({ ref.name, 3 });
				targets.push_back({ ref.name + QStringLiteral(" rotation"), 3 });
				for (int k = 0; k < 6; ++k)
					mapping[static_cast<std::size_t>(shell[static_cast<std::size_t>(k)])] = std::make_pair(k / 3, k % 3);
			}
			else
			{
				targets.push_back({ ref.name, ref.components == 2 ? 3 : ref.components });
				for (int c = 0; c < ref.components; ++c)
					mapping[static_cast<std::size_t>(c)] = std::make_pair(0, c);
			}
		}

		for (const auto& stepEntry : ref.steps)
		{
			const std::size_t stepSlot = stepIndex[stepEntry.first];
			Handle stepGroup = openGroup(fieldGroup, stepEntry.second);
			for (const QString& entity : childNames(stepGroup))
			{
				// NOE = nodes; MAI.<G> = cells of a type; NOE.<G> = the nodes of each cell of a type.
				const bool onNodes = entity == QLatin1String("NOE");
				const Geometry* geometry = nullptr;
				bool perCellNodes = false;
				if (entity.startsWith(QLatin1String("MAI.")))
					geometry = findGeometry(entity.mid(4));
				else if (entity.startsWith(QLatin1String("NOE.")))
				{
					geometry = findGeometry(entity.mid(4));
					perCellNodes = true;
				}
				const CellBlock* block = nullptr;
				if (geometry)
					for (const CellBlock& b : blocks)
						if (b.geometry == geometry && b.kept)
							block = &b;
				if (!onNodes && !block)
				{
					++skippedEntities; // faces, edges, types that are not shown
					continue;
				}
				const std::size_t entities = onNodes ? nodeTotal : block->count;
				const int association = onNodes ? 0 : 1;
				const std::size_t tuples = onNodes ? nodeTotal : dataset->cellTypes.size();

				Handle entityGroup = openGroup(stepGroup, entity);
				for (const QString& profileName : childNames(entityGroup))
				{
					Handle profileGroup = openGroup(entityGroup, profileName);
					Handle values = openDataset(profileGroup, QStringLiteral("CO"));
					if (!profileGroup.ok() || !values.ok())
						continue;
					int points = 1;
					readInt(profileGroup, "NGA", points);
					std::vector<double> data;
					const std::vector<hsize_t> dims = datasetDims(values);
					if (dims.empty() || !readRows<double>(values, 0, dims[0], data))
						continue;
					if (points < 1)
						points = 1;
					// The entity count comes from the data itself: on a profile group the NBR attribute is the count of the mesh's entities
					// (Code_Aster writes 11126 there for a profile of 11125 nodes), not of the entities in the profile.
					const std::size_t perEntity = static_cast<std::size_t>(points) * static_cast<std::size_t>(ref.components);
					const std::size_t n = data.size() / perEntity;
					if (n == 0 || data.size() % perEntity != 0 || (onNodes && points != 1))
					{
						unplaced << ref.name + QLatin1Char(' ') + QLatin1Char('(') + entity + QLatin1Char(')');
						continue;
					}
					// Which entities the values belong to: all of them, or the profile's list (1-based positions).
					std::vector<long long> profile;
					if (profileName != QLatin1String("MED_NO_PROFILE_INTERNAL"))
					{
						if (!readAll<long long>(file, QStringLiteral("PROFILS/") + profileName + QStringLiteral("/PFL"), profile) || profile.size() != n)
						{
							unplaced << ref.name + QLatin1String(" (profile ") + profileName + QLatin1Char(')');
							continue;
						}
					}
					else if (n != entities)
					{
						unplaced << ref.name + QLatin1Char(' ') + QLatin1Char('(') + entity + QLatin1Char(')'); // a full field must cover every entity
						continue;
					}

					std::vector<std::vector<float>*> outs;
					for (const Target& aim : targets)
					{
						const std::pair<QString, int> key(aim.name, association);
						FieldAccum& a = accum[key];
						if (a.field.name.isEmpty())
						{
							a.field.name = aim.name;
							a.field.association = onNodes ? ResultFieldAssociation::Node : ResultFieldAssociation::Cell;
							a.field.components = aim.components;
							a.field.stepData.resize(stepCount);
							order.push_back(key);
						}
						std::vector<float>& out = a.field.stepData[stepSlot];
						if (out.empty())
							out.assign(tuples * static_cast<std::size_t>(aim.components), nan);
						a.any = true;
						outs.push_back(&out);
					}
					for (std::size_t e = 0; e < n; ++e)
					{
						const long long position = profile.empty() ? static_cast<long long>(e) : profile[e] - 1;
						if (position < 0 || static_cast<std::size_t>(position) >= entities)
							continue;
						const std::size_t tuple = onNodes ? static_cast<std::size_t>(position) : block->first + static_cast<std::size_t>(position);
						for (int c = 0; c < ref.components; ++c)
						{
							double sum = 0.0;
							const std::size_t base = static_cast<std::size_t>(c) * n * static_cast<std::size_t>(points) + e * static_cast<std::size_t>(points);
							for (int g = 0; g < points; ++g)
								sum += data[base + static_cast<std::size_t>(g)];
							const std::pair<int, int> place = mapping[static_cast<std::size_t>(c)];
							(*outs[static_cast<std::size_t>(place.first)])[tuple * static_cast<std::size_t>(targets[static_cast<std::size_t>(place.first)].components)
							                                              + static_cast<std::size_t>(place.second)] = static_cast<float>(sum / points);
						}
						if (ref.components == 2)
							(*outs[0])[tuple * 3 + 2] = 0.0f; // a 2-D vector gets a zero z
					}
					if (points > 1 || perCellNodes)
						++averaged;
				}
			}
		}
	}
	for (const auto& key : order)
	{
		FieldAccum& a = accum[key];
		if (!a.any)
			continue;
		// A tensor whose component names identified the standard order keeps the standard names; other multi-component
		// fields keep the names the file gave them.
		const FieldRef* ref = nullptr;
		for (const FieldRef& r : refs)
			if (r.name == a.field.name)
				ref = &r;
		if (ref && ref->components > 3 && ref->components != 6 && ref->components != 9 && ref->componentNames.size() == ref->components)
			for (const QString& name : ref->componentNames)
				a.field.componentNames.push_back(name);
		dataset->fields.push_back(std::move(a.field));
	}
	if (averaged > 0)
		outcome.warnings << QStringLiteral("%1 field(s) on Gauss points or on the nodes of each cell are shown as one value per cell (the mean over its points).").arg(averaged);
	(void)skippedEntities;
	unplaced.removeDuplicates();
	if (!unplaced.isEmpty())
		outcome.warnings << QStringLiteral("Some field values could not be placed on the mesh and were left out: %1.").arg(unplaced.join(QStringLiteral(", ")));

	addDerivedStressFields(*dataset);
	outcome.warnings << resultCellTypeWarnings(*dataset);
	const QString invalid = dataset->validate();
	if (!invalid.isEmpty())
		return fail(QStringLiteral("The MED file is inconsistent: %1").arg(invalid));
	outcome.dataset = std::move(dataset);
	return outcome;
}

#else // no HDF5 library in this build

bool medSupported() { return false; }
QStringList medExtensions() { return {}; }
QString medFileFilter() { return QString(); }

ResultReadOutcome readMed(const QString&, const std::atomic<bool>*)
{
	ResultReadOutcome outcome;
	outcome.error = QStringLiteral("This build of ModelViewer was made without the HDF5 library, so MED result files cannot be read.");
	return outcome;
}

#endif
