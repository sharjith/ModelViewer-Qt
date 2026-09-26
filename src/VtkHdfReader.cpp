#include "VtkHdfReader.h"

#if MV_HAVE_HDF5

#include <hdf5.h>

#include "Hdf5Util.h"
#include "ResultDerivedFields.h"

#include <QFile>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>

using namespace hdf5util;

namespace
{
	// ---- The time steps (/VTKHDF/Steps) ------------------------------------------------------------------------------------

	struct StepTable
	{
		bool present = false;
		std::size_t count = 1;
		std::vector<double> values;
		std::vector<long long> partOffsets, numberOfParts, pointOffsets;
		std::vector<long long> cellOffsets, connectivityOffsets; // NSteps x topologies, flattened
		int topologies = 1;
		std::size_t totalParts = 1; // the partitions in the file (NumberOfPoints' length)

		// The first partition of a step, and how many it has.
		std::size_t partOffset(std::size_t step) const
		{
			if (step < partOffsets.size())
				return static_cast<std::size_t>(std::max(0LL, partOffsets[step]));
			// No PartOffsets: a file with one partition per step lists them in step order, otherwise the parts are shared.
			return (count > 1 && totalParts >= count && totalParts % count == 0) ? step * (totalParts / count) : 0;
		}
		std::size_t partCount(std::size_t step) const
		{
			if (step < numberOfParts.size())
				return static_cast<std::size_t>(std::max(1LL, numberOfParts[step]));
			return std::max<std::size_t>(1, present && count > 1 && totalParts >= count && totalParts % count == 0 ? totalParts / count : totalParts);
		}
		long long pointOffset(std::size_t step) const { return step < pointOffsets.size() ? pointOffsets[step] : 0; }
		long long cellOffset(std::size_t step, int topology) const
		{
			const std::size_t at = step * static_cast<std::size_t>(topologies) + static_cast<std::size_t>(topology);
			return at < cellOffsets.size() ? cellOffsets[at] : 0;
		}
		long long connectivityOffset(std::size_t step, int topology) const
		{
			const std::size_t at = step * static_cast<std::size_t>(topologies) + static_cast<std::size_t>(topology);
			return at < connectivityOffsets.size() ? connectivityOffsets[at] : 0;
		}
	};

	StepTable readSteps(hid_t root, int topologies)
	{
		StepTable table;
		table.topologies = topologies;
		if (!linkExists(root, QStringLiteral("Steps")))
			return table;
		Handle group = openGroup(root, QStringLiteral("Steps"));
		if (!group.ok())
			return table;
		table.present = true;
		std::vector<int> nsteps;
		readAll<double>(group, QStringLiteral("Values"), table.values);
		if (readNumericAttribute<int>(group, "NSteps", nsteps) && nsteps[0] > 0)
			table.count = static_cast<std::size_t>(nsteps[0]);
		else if (!table.values.empty())
			table.count = table.values.size();
		readAll<long long>(group, QStringLiteral("PartOffsets"), table.partOffsets);
		readAll<long long>(group, QStringLiteral("NumberOfParts"), table.numberOfParts);
		readAll<long long>(group, QStringLiteral("PointOffsets"), table.pointOffsets);
		readAll<long long>(group, QStringLiteral("CellOffsets"), table.cellOffsets);
		readAll<long long>(group, QStringLiteral("ConnectivityIdOffsets"), table.connectivityOffsets);
		return table;
	}

	// ---- Reading -----------------------------------------------------------------------------------------------------------

	struct Context
	{
		hid_t root = -1;
		ResultDataset* dataset = nullptr;
		ResultReadOutcome* outcome = nullptr;
		const std::atomic<bool>* cancel = nullptr;
		StepTable steps;
		bool cancelled() const { return cancel && cancel->load(std::memory_order_acquire); }
	};

	// The sum of counts[from .. from + n). False when the range is outside the array.
	bool sumRange(const std::vector<long long>& counts, std::size_t from, std::size_t n, std::size_t& sum)
	{
		sum = 0;
		if (from + n > counts.size())
			return false;
		for (std::size_t i = from; i < from + n; ++i)
		{
			if (counts[i] < 0)
				return false;
			sum += static_cast<std::size_t>(counts[i]);
		}
		return true;
	}

	bool skippedArrayName(const QString& name)
	{
		return name == QLatin1String("vtkGhostType") || name == QLatin1String("vtkValidPointMask");
	}

	// The arrays of /VTKHDF/<groupName> (PointData or CellData) as fields, every step. `rowsPerStep` is the tuple count of the group's
	// data at each step and `defaultOffsets` the first row of each step when the array has no offsets of its own; an array whose
	// tuple count differs from `expectedTuples` (the mesh's) is left empty at that step.
	void readArrays(Context& c, const QString& groupName, const QString& offsetsGroup, ResultFieldAssociation association,
	                const std::vector<std::size_t>& rowsPerStep, const std::vector<long long>& defaultOffsets, std::size_t expectedTuples,
	                bool& mismatch)
	{
		if (!linkExists(c.root, groupName))
			return;
		Handle group = openGroup(c.root, groupName);
		if (!group.ok())
			return;
		for (const QString& name : childNames(group))
		{
			if (c.cancelled())
				return;
			if (skippedArrayName(name))
				continue;
			Handle dataset = openDataset(group, name);
			if (!dataset.ok())
				continue; // a subgroup, or unreadable
			const std::vector<hsize_t> dims = datasetDims(dataset);
			if (!datasetIsNumeric(dataset) || dims.empty())
			{
				c.outcome->warnings << QStringLiteral("Skipped %1/%2 (not a numeric array).").arg(groupName, name);
				continue;
			}
			hsize_t components = 1;
			for (std::size_t d = 1; d < dims.size(); ++d)
				components *= dims[d];
			if (components < 1)
				continue;
			std::vector<long long> ownOffsets;
			if (c.steps.present)
				readAll<long long>(c.root, offsetsGroup + QLatin1Char('/') + name, ownOffsets);

			ResultField field;
			field.name = name;
			field.association = association;
			field.components = components == 2 ? 3 : static_cast<int>(components);
			bool any = false;
			for (std::size_t s = 0; s < c.steps.count; ++s)
			{
				std::vector<float> data;
				const std::size_t rows = s < rowsPerStep.size() ? rowsPerStep[s] : 0;
				if (rows != expectedTuples)
				{
					mismatch = true;
				}
				else
				{
					const long long start = !c.steps.present ? 0 : (s < ownOffsets.size() ? ownOffsets[s] : (s < defaultOffsets.size() ? defaultOffsets[s] : 0));
					if (start < 0 || !readRows<float>(dataset, static_cast<hsize_t>(start), rows, data))
					{
						c.outcome->warnings << QStringLiteral("%1/%2: step %3 could not be read.").arg(groupName, name).arg(s);
						data.clear();
					}
					else if (components == 2)
					{
						std::vector<float> padded(rows * 3, 0.0f);
						for (std::size_t t = 0; t < rows; ++t)
						{
							padded[t * 3] = data[t * 2];
							padded[t * 3 + 1] = data[t * 2 + 1];
						}
						data = std::move(padded);
					}
				}
				any = any || !data.empty();
				field.stepData.push_back(std::move(data));
			}
			if (any)
				c.dataset->fields.push_back(std::move(field));
		}
	}

	void addSteps(Context& c)
	{
		for (std::size_t s = 0; s < c.steps.count; ++s)
		{
			ResultStep step;
			step.time = s < c.steps.values.size() ? c.steps.values[s] : static_cast<double>(s);
			c.dataset->steps.push_back(step);
		}
	}

	struct CellSink
	{
		ResultDataset& dataset;
		void add(ResultCellType type, const std::vector<std::uint32_t>& nodes)
		{
			dataset.cellConnectivity.insert(dataset.cellConnectivity.end(), nodes.begin(), nodes.end());
			dataset.cellTypes.push_back(type);
			dataset.cellOffsets.push_back(static_cast<std::uint32_t>(dataset.cellConnectivity.size()));
		}
	};

	// One cell category of one step (an UnstructuredGrid has one, a PolyData four): the arrays of all its partitions.
	struct CellGroup
	{
		QString prefix; // "" or "Vertices/" ...
		std::vector<long long> cellsPerPart, idsPerPart; // NumberOfCells, NumberOfConnectivityIds
		std::vector<long long> offsets, connectivity;
		std::vector<unsigned char> types; // UnstructuredGrid only
		std::size_t cellCursor = 0, offsetCursor = 0, connectivityCursor = 0;
	};

	bool loadCellGroup(Context& c, const QString& prefix, int topology, std::size_t step, std::size_t partFrom, std::size_t partCount,
	                   bool withTypes, CellGroup& group, QString& error)
	{
		group = CellGroup();
		group.prefix = prefix;
		if (!readAll<long long>(c.root, prefix + QStringLiteral("NumberOfCells"), group.cellsPerPart)
		    || !readAll<long long>(c.root, prefix + QStringLiteral("NumberOfConnectivityIds"), group.idsPerPart))
		{
			if (prefix.isEmpty())
				error = QStringLiteral("The file has no NumberOfCells / NumberOfConnectivityIds.");
			return false; // a PolyData category may simply be absent
		}
		std::size_t cells = 0, ids = 0;
		if (!sumRange(group.cellsPerPart, partFrom, partCount, cells) || !sumRange(group.idsPerPart, partFrom, partCount, ids))
		{
			error = QStringLiteral("The partitions of step %1 are outside the cell counts.").arg(step);
			return false;
		}
		const long long cellBase = c.steps.cellOffset(step, topology), idBase = c.steps.connectivityOffset(step, topology);
		Handle offsets = openDataset(c.root, prefix + QStringLiteral("Offsets"));
		Handle connectivity = openDataset(c.root, prefix + QStringLiteral("Connectivity"));
		if (!offsets.ok() || !connectivity.ok())
		{
			error = QStringLiteral("The file has no %1Offsets / %1Connectivity.").arg(prefix);
			return false;
		}
		// Offsets has one extra entry per partition (each partition's list starts at 0).
		if (cellBase < 0 || idBase < 0
		    || !readRows<long long>(offsets, static_cast<hsize_t>(cellBase) + partFrom, cells + partCount, group.offsets)
		    || !readRows<long long>(connectivity, static_cast<hsize_t>(idBase), ids, group.connectivity))
		{
			error = QStringLiteral("Cannot read %1Offsets / %1Connectivity for step %2.").arg(prefix).arg(step);
			return false;
		}
		if (withTypes)
		{
			Handle types = openDataset(c.root, QStringLiteral("Types"));
			if (!types.ok() || cellBase < 0 || !readRows<unsigned char>(types, static_cast<hsize_t>(cellBase), cells, group.types))
			{
				error = QStringLiteral("Cannot read the cell Types for step %1.").arg(step);
				return false;
			}
		}
		return true;
	}

	// The cell `index` of partition `part` (relative to the step's first partition) as node numbers, shifted by the partition's
	// first point. False on an id out of range.
	bool cellNodes(const CellGroup& group, std::size_t part, std::size_t partFrom, std::size_t index, std::size_t pointBase,
	               std::size_t pointsInPart, std::vector<std::uint32_t>& nodes)
	{
		nodes.clear();
		const std::size_t cellsInPart = static_cast<std::size_t>(group.cellsPerPart[partFrom + part]);
		const std::size_t idsInPart = static_cast<std::size_t>(group.idsPerPart[partFrom + part]);
		const long long begin = group.offsets[group.offsetCursor + index], end = group.offsets[group.offsetCursor + index + 1];
		(void)cellsInPart;
		if (begin < 0 || end < begin || static_cast<std::size_t>(end) > idsInPart)
			return false;
		for (long long k = begin; k < end; ++k)
		{
			const long long id = group.connectivity[group.connectivityCursor + static_cast<std::size_t>(k)];
			if (id < 0 || static_cast<std::size_t>(id) >= pointsInPart)
				return false;
			nodes.push_back(static_cast<std::uint32_t>(pointBase + static_cast<std::size_t>(id)));
		}
		return true;
	}

	ResultCellType polyCellType(int category, std::size_t nodeCount)
	{
		switch (category)
		{
		case 1: return nodeCount == 2 ? ResultCellType::Line : ResultCellType::Unsupported; // a polyline keeps only its place
		case 2: return nodeCount == 3 ? ResultCellType::Triangle : (nodeCount == 4 ? ResultCellType::Quad : ResultCellType::Unsupported);
		default: return ResultCellType::Unsupported; // vertices and strips
		}
	}

	// The geometry of the first step and the arrays of every step, for the UnstructuredGrid and PolyData types.
	bool readPointBased(Context& c, bool polyData, QString& error)
	{
		ResultDataset& dataset = *c.dataset;
		std::vector<long long> pointsPerPart;
		if (!readAll<long long>(c.root, QStringLiteral("NumberOfPoints"), pointsPerPart) || pointsPerPart.empty())
		{
			error = QStringLiteral("The file has no NumberOfPoints.");
			return false;
		}
		c.steps.totalParts = pointsPerPart.size();
		const std::size_t count = c.steps.count;

		// Points and cells of every step, as counts.
		std::vector<std::size_t> pointsPerStep(count, 0), cellsPerStep(count, 0);
		std::vector<std::vector<long long>> groupCells; // per category: NumberOfCells
		const std::array<QString, 4> categoryNames = { QStringLiteral("Vertices/"), QStringLiteral("Lines/"), QStringLiteral("Polygons/"), QStringLiteral("Strips/") };
		const int categories = polyData ? 4 : 1;
		groupCells.resize(static_cast<std::size_t>(categories));
		if (polyData)
		{
			for (int k = 0; k < categories; ++k)
				readAll<long long>(c.root, categoryNames[static_cast<std::size_t>(k)] + QStringLiteral("NumberOfCells"), groupCells[static_cast<std::size_t>(k)]);
		}
		else if (!readAll<long long>(c.root, QStringLiteral("NumberOfCells"), groupCells[0]))
		{
			error = QStringLiteral("The file has no NumberOfCells.");
			return false;
		}
		for (std::size_t s = 0; s < count; ++s)
		{
			const std::size_t from = c.steps.partOffset(s), n = c.steps.partCount(s);
			if (!sumRange(pointsPerPart, from, n, pointsPerStep[s]))
			{
				error = QStringLiteral("The partitions of step %1 are outside NumberOfPoints.").arg(s);
				return false;
			}
			for (const std::vector<long long>& cells : groupCells)
			{
				std::size_t part = 0;
				if (!cells.empty() && sumRange(cells, from, n, part))
					cellsPerStep[s] += part;
			}
		}

		// ---- The first step's geometry
		Handle points = openDataset(c.root, QStringLiteral("Points"));
		if (!points.ok())
		{
			error = QStringLiteral("The file has no Points.");
			return false;
		}
		const std::size_t from0 = c.steps.partOffset(0), parts0 = c.steps.partCount(0);
		std::vector<float> positions;
		if (c.steps.pointOffset(0) < 0 || !readRows<float>(points, static_cast<hsize_t>(c.steps.pointOffset(0)), pointsPerStep[0], positions)
		    || positions.size() != pointsPerStep[0] * 3)
		{
			error = QStringLiteral("Cannot read the Points of the first step.");
			return false;
		}
		dataset.nodePositions = std::move(positions);
		dataset.cellOffsets.push_back(0);

		CellSink sink{ dataset };
		std::vector<std::uint32_t> nodes;
		std::vector<CellGroup> groups(static_cast<std::size_t>(categories));
		std::vector<bool> haveGroup(static_cast<std::size_t>(categories), false);
		for (int k = 0; k < categories; ++k)
		{
			QString groupError;
			haveGroup[static_cast<std::size_t>(k)] = loadCellGroup(c, polyData ? categoryNames[static_cast<std::size_t>(k)] : QString(), k, 0, from0, parts0,
			                                                       !polyData, groups[static_cast<std::size_t>(k)], groupError);
			if (!haveGroup[static_cast<std::size_t>(k)] && !polyData)
			{
				error = groupError;
				return false;
			}
			if (!haveGroup[static_cast<std::size_t>(k)] && polyData && !groupError.isEmpty())
			{
				error = groupError; // present but unreadable
				return false;
			}
		}
		// Partition by partition; within a partition the categories in their fixed order (which is also how CellData is laid out).
		std::size_t pointBase = 0, badCells = 0;
		for (std::size_t p = 0; p < parts0; ++p)
		{
			const std::size_t pointsInPart = static_cast<std::size_t>(pointsPerPart[from0 + p]);
			for (int k = 0; k < categories; ++k)
			{
				if (!haveGroup[static_cast<std::size_t>(k)])
					continue;
				CellGroup& group = groups[static_cast<std::size_t>(k)];
				const std::size_t cells = static_cast<std::size_t>(group.cellsPerPart[from0 + p]);
				for (std::size_t i = 0; i < cells; ++i)
				{
					const bool ok = cellNodes(group, p, from0, i, pointBase, pointsInPart, nodes);
					ResultCellType type;
					if (polyData)
						type = polyCellType(k, nodes.size());
					else
						type = resultCellTypeFromVtk(static_cast<int>(group.types[group.cellCursor + i]));
					if (!ok)
					{
						if (type != ResultCellType::Unsupported)
						{
							error = QStringLiteral("A cell references a point outside its partition.");
							return false;
						}
						nodes.clear(); // a cell type that is not drawn (polyhedra store their faces differently)
						++badCells;
					}
					else if (type == ResultCellType::Unsupported && !polyData)
						nodes.clear();
					sink.add(type, nodes);
				}
				group.cellCursor += cells;
				group.offsetCursor += cells + 1;
				group.connectivityCursor += static_cast<std::size_t>(group.idsPerPart[from0 + p]);
			}
			pointBase += pointsInPart;
		}
		(void)badCells;
		if (dataset.cellTypes.size() != cellsPerStep[0])
		{
			error = QStringLiteral("The cells read (%1) do not match the counts in the file (%2).").arg(dataset.cellTypes.size()).arg(cellsPerStep[0]);
			return false;
		}
		addSteps(c);

		// ---- Fields
		std::vector<long long> pointDefaults(count, 0), cellDefaults(count, 0);
		for (std::size_t s = 0; s < count; ++s)
		{
			pointDefaults[s] = c.steps.pointOffset(s);
			cellDefaults[s] = c.steps.cellOffset(s, 0);
		}
		if (polyData) // CellData rows of a PolyData step start at the sum of its categories' cell offsets
			for (std::size_t s = 0; s < count; ++s)
			{
				long long total = 0;
				for (int k = 0; k < categories; ++k)
					total += c.steps.cellOffset(s, k);
				cellDefaults[s] = total;
			}
		bool mismatch = false;
		readArrays(c, QStringLiteral("PointData"), QStringLiteral("Steps/PointDataOffsets"), ResultFieldAssociation::Node, pointsPerStep, pointDefaults,
		           pointsPerStep[0], mismatch);
		readArrays(c, QStringLiteral("CellData"), QStringLiteral("Steps/CellDataOffsets"), ResultFieldAssociation::Cell, cellsPerStep, cellDefaults,
		           cellsPerStep[0], mismatch);
		if (mismatch)
			c.outcome->warnings << QStringLiteral("The mesh changes size between time steps, which is not supported: the first step's mesh is shown and the data of steps "
			                                      "with a different number of points or cells is left out.");

		// ---- A moving mesh: same points count, different rows -> a displacement field, so it can be shown deformed.
		if (c.steps.present && count > 1)
		{
			ResultField displacement;
			displacement.name = QStringLiteral("Mesh displacement");
			displacement.association = ResultFieldAssociation::Node;
			displacement.components = 3;
			displacement.stepData.resize(count);
			bool moving = false;
			for (std::size_t s = 1; s < count; ++s)
			{
				if (c.cancelled())
					return true;
				if (pointsPerStep[s] != pointsPerStep[0] || c.steps.pointOffset(s) == c.steps.pointOffset(0) || c.steps.pointOffset(s) < 0)
					continue;
				std::vector<float> moved;
				if (!readRows<float>(points, static_cast<hsize_t>(c.steps.pointOffset(s)), pointsPerStep[s], moved) || moved.size() != dataset.nodePositions.size())
					continue;
				for (std::size_t i = 0; i < moved.size(); ++i)
					moved[i] -= dataset.nodePositions[i];
				displacement.stepData[s] = std::move(moved);
				moving = true;
			}
			if (moving)
			{
				displacement.stepData[0].assign(dataset.nodePositions.size(), 0.0f); // the reference shape
				for (std::vector<float>& d : displacement.stepData)
					if (d.empty())
						d.assign(dataset.nodePositions.size(), 0.0f);
				dataset.fields.push_back(std::move(displacement));
				c.outcome->warnings << QStringLiteral("The points move between time steps: the first step's shape is the geometry and the movement is the field "
				                                      "\"Mesh displacement\" (show it deformed to follow the motion).");
			}
		}
		return true;
	}

	// ImageData: the regular grid. Point p of the grid is Origin + Direction * (Spacing * index).
	bool readImageData(Context& c, QString& error)
	{
		ResultDataset& dataset = *c.dataset;
		std::vector<int> extent;
		std::vector<double> origin, spacing, direction;
		if (!readNumericAttribute<int>(c.root, "WholeExtent", extent) || extent.size() < 6)
		{
			error = QStringLiteral("The ImageData has no WholeExtent.");
			return false;
		}
		if (!readNumericAttribute<double>(c.root, "Origin", origin) || origin.size() < 3)
			origin = { 0.0, 0.0, 0.0 };
		if (!readNumericAttribute<double>(c.root, "Spacing", spacing) || spacing.size() < 3)
			spacing = { 1.0, 1.0, 1.0 };
		if (!readNumericAttribute<double>(c.root, "Direction", direction) || direction.size() < 9)
			direction = { 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0 };
		const std::size_t n[3] = { static_cast<std::size_t>(std::max(1, extent[1] - extent[0] + 1)),
		                           static_cast<std::size_t>(std::max(1, extent[3] - extent[2] + 1)),
		                           static_cast<std::size_t>(std::max(1, extent[5] - extent[4] + 1)) };
		const std::size_t pointCount = n[0] * n[1] * n[2];
		if (pointCount > 400000000)
		{
			error = QStringLiteral("The image is too large (%1 points).").arg(pointCount);
			return false;
		}
		dataset.nodePositions.reserve(pointCount * 3);
		for (std::size_t k = 0; k < n[2]; ++k)
			for (std::size_t j = 0; j < n[1]; ++j)
				for (std::size_t i = 0; i < n[0]; ++i)
				{
					const double index[3] = { static_cast<double>(extent[0]) + static_cast<double>(i), static_cast<double>(extent[2]) + static_cast<double>(j),
					                          static_cast<double>(extent[4]) + static_cast<double>(k) };
					const double scaled[3] = { spacing[0] * index[0], spacing[1] * index[1], spacing[2] * index[2] };
					for (std::size_t a = 0; a < 3; ++a)
						dataset.nodePositions.push_back(static_cast<float>(origin[a] + direction[a * 3] * scaled[0] + direction[a * 3 + 1] * scaled[1]
						                                                  + direction[a * 3 + 2] * scaled[2]));
				}

		// Cells: hexahedra of a 3-D grid, quads of a flat one (the two axes with more than one point; the first is fastest).
		dataset.cellOffsets.push_back(0);
		CellSink sink{ dataset };
		std::vector<std::size_t> active;
		for (std::size_t a = 0; a < 3; ++a)
			if (n[a] > 1)
				active.push_back(a);
		auto nodeAt = [&](std::size_t i, std::size_t j, std::size_t k) { return static_cast<std::uint32_t>(i + n[0] * (j + n[1] * k)); };
		std::size_t cellCount = 0;
		if (active.size() == 3)
		{
			for (std::size_t k = 0; k + 1 < n[2]; ++k)
				for (std::size_t j = 0; j + 1 < n[1]; ++j)
					for (std::size_t i = 0; i + 1 < n[0]; ++i)
					{
						std::vector<std::uint32_t> ids;
						for (std::size_t dk = 0; dk < 2; ++dk)
						{
							ids.push_back(nodeAt(i, j, k + dk));
							ids.push_back(nodeAt(i + 1, j, k + dk));
							ids.push_back(nodeAt(i + 1, j + 1, k + dk));
							ids.push_back(nodeAt(i, j + 1, k + dk));
						}
						sink.add(ResultCellType::Hexahedron, ids);
						++cellCount;
					}
		}
		else if (active.size() == 2)
		{
			const std::size_t a = active[0], b = active[1];
			for (std::size_t v = 0; v + 1 < n[b]; ++v)
				for (std::size_t u = 0; u + 1 < n[a]; ++u)
				{
					auto corner = [&](std::size_t du, std::size_t dv) {
						std::size_t at[3] = { 0, 0, 0 };
						at[a] = u + du;
						at[b] = v + dv;
						return nodeAt(at[0], at[1], at[2]);
					};
					sink.add(ResultCellType::Quad, { corner(0, 0), corner(1, 0), corner(1, 1), corner(0, 1) });
					++cellCount;
				}
		}
		if (cellCount == 0)
		{
			error = QStringLiteral("The ImageData has no cells (it is a line or a single point).");
			return false;
		}
		addSteps(c);

		// Arrays: [z, y, x, components] per step - x fastest, like the node numbering; a temporal file has the step first.
		const std::size_t count = c.steps.count;
		std::size_t cellsPerDim = 1;
		for (std::size_t a = 0; a < 3; ++a)
			cellsPerDim *= std::max<std::size_t>(n[a] - 1, 1);
		const std::array<std::pair<QString, ResultFieldAssociation>, 2> groups = {
			std::make_pair(QStringLiteral("PointData"), ResultFieldAssociation::Node), std::make_pair(QStringLiteral("CellData"), ResultFieldAssociation::Cell)
		};
		for (const auto& entry : groups)
		{
			if (!linkExists(c.root, entry.first))
				continue;
			Handle group = openGroup(c.root, entry.first);
			if (!group.ok())
				continue;
			const std::size_t tuples = entry.second == ResultFieldAssociation::Node ? pointCount : cellsPerDim;
			for (const QString& name : childNames(group))
			{
				if (c.cancelled())
					return true;
				if (skippedArrayName(name))
					continue;
				Handle dataset2 = openDataset(group, name);
				if (!dataset2.ok())
					continue;
				const std::vector<hsize_t> dims = datasetDims(dataset2);
				if (!datasetIsNumeric(dataset2) || dims.empty())
				{
					c.outcome->warnings << QStringLiteral("Skipped %1/%2 (not a numeric array).").arg(entry.first, name);
					continue;
				}
				hsize_t total = 1;
				for (hsize_t d : dims)
					total *= d;
				const std::size_t perStep = total / count;
				if (perStep == 0 || perStep % tuples != 0 || perStep * count != total)
				{
					c.outcome->warnings << QStringLiteral("Skipped %1/%2 (its size does not fit the image).").arg(entry.first, name);
					continue;
				}
				const std::size_t components = perStep / tuples;
				std::vector<float> all;
				if (!readRows<float>(dataset2, 0, dims[0], all) || all.size() != total)
				{
					c.outcome->warnings << QStringLiteral("Skipped %1/%2 (unreadable).").arg(entry.first, name);
					continue;
				}
				ResultField field;
				field.name = name;
				field.association = entry.second;
				field.components = components == 2 ? 3 : static_cast<int>(components);
				for (std::size_t s = 0; s < count; ++s)
				{
					const float* begin = all.data() + s * perStep;
					std::vector<float> data;
					if (components == 2)
					{
						data.assign(tuples * 3, 0.0f);
						for (std::size_t t = 0; t < tuples; ++t)
						{
							data[t * 3] = begin[t * 2];
							data[t * 3 + 1] = begin[t * 2 + 1];
						}
					}
					else
						data.assign(begin, begin + perStep);
					field.stepData.push_back(std::move(data));
				}
				dataset.fields.push_back(std::move(field));
			}
		}
		return true;
	}
}

bool vtkHdfSupported() { return true; }
QStringList vtkHdfExtensions() { return { QStringLiteral("vtkhdf") }; }
QString vtkHdfFileFilter() { return QStringLiteral("VTK HDF (*.vtkhdf)"); }

ResultReadOutcome readVtkHdf(const QString& path, const std::atomic<bool>* cancel)
{
	ResultReadOutcome outcome;
	auto fail = [&outcome](const QString& message) {
		outcome.error = message;
		return std::move(outcome);
	};
	ErrorSilencer silence;

	const QByteArray native = path.toUtf8();
	Handle file(H5Fopen(native.constData(), H5F_ACC_RDONLY, H5P_DEFAULT), H5Fclose);
	if (!file.ok())
		return fail(QStringLiteral("Cannot open '%1' as an HDF5 file.").arg(path));
	Handle root = openGroup(file, QStringLiteral("VTKHDF"));
	if (!root.ok())
		return fail(QStringLiteral("'%1' is an HDF5 file but not a VTKHDF file (it has no /VTKHDF group).").arg(path));

	QString type;
	if (!readStringAttribute(root, "Type", type))
		return fail(QStringLiteral("The /VTKHDF group has no Type attribute."));
	std::vector<int> version;
	readNumericAttribute<int>(root, "Version", version);

	auto dataset = std::make_unique<ResultDataset>();
	dataset->sourcePath = path;
	dataset->solverName = QStringLiteral("VTKHDF");

	Context context;
	context.root = root;
	context.dataset = dataset.get();
	context.outcome = &outcome;
	context.cancel = cancel;

	QString error;
	bool ok = false;
	if (type == QLatin1String("UnstructuredGrid") || type == QLatin1String("PolyData"))
	{
		const bool polyData = type == QLatin1String("PolyData");
		context.steps = readSteps(root, polyData ? 4 : 1);
		ok = readPointBased(context, polyData, error);
	}
	else if (type == QLatin1String("ImageData"))
	{
		context.steps = readSteps(root, 1);
		ok = readImageData(context, error);
	}
	else if (type == QLatin1String("MultiBlockDataSet") || type == QLatin1String("PartitionedDataSetCollection") || type == QLatin1String("OverlappingAMR"))
		return fail(QStringLiteral("This VTKHDF file is a composite dataset (%1); composite files are not supported yet. Export a single block, or merge the blocks, "
		                           "in ParaView first.").arg(type));
	else
		return fail(QStringLiteral("The VTKHDF type '%1' is not supported yet (supported: UnstructuredGrid, PolyData, ImageData).").arg(type));
	if (context.cancelled())
		return fail(QStringLiteral("cancelled"));
	if (!ok)
		return fail(error.isEmpty() ? QStringLiteral("The VTKHDF file could not be read.") : error);
	if (!version.empty() && version[0] > 2)
		outcome.warnings << QStringLiteral("The file uses VTKHDF version %1.%2, newer than this reader knows (2.x); unknown parts are ignored.")
		                        .arg(version[0]).arg(version.size() > 1 ? version[1] : 0);

	outcome.warnings << resultCellTypeWarnings(*dataset);
	addDerivedStressFields(*dataset);
	const QString invalid = dataset->validate();
	if (!invalid.isEmpty())
		return fail(QStringLiteral("The VTKHDF file is inconsistent: %1").arg(invalid));
	outcome.dataset = std::move(dataset);
	return outcome;
}

#else // no HDF5 library in this build

bool vtkHdfSupported() { return false; }
QStringList vtkHdfExtensions() { return {}; }
QString vtkHdfFileFilter() { return QString(); }

ResultReadOutcome readVtkHdf(const QString&, const std::atomic<bool>*)
{
	ResultReadOutcome outcome;
	outcome.error = QStringLiteral("This build of ModelViewer was made without the HDF5 library, so VTKHDF result files cannot be read.");
	return outcome;
}

#endif
