#include "ResultDataset.h"

int resultCellNodeCount(ResultCellType type)
{
	switch (type)
	{
	case ResultCellType::Line:       return 2;
	case ResultCellType::Triangle:   return 3;
	case ResultCellType::Quad:       return 4;
	case ResultCellType::Tetra:      return 4;
	case ResultCellType::Hexahedron: return 8;
	case ResultCellType::Wedge:      return 6;
	case ResultCellType::Pyramid:    return 5;
	case ResultCellType::Triangle6:  return 6;
	case ResultCellType::Quad8:      return 8;
	case ResultCellType::Tetra10:    return 10;
	case ResultCellType::Hexahedron20: return 20;
	case ResultCellType::Wedge15:    return 15;
	case ResultCellType::Pyramid13:  return 13;
	case ResultCellType::Unsupported: break;
	}
	return 0;
}

ResultCellType resultCellCornerType(ResultCellType type)
{
	switch (type)
	{
	case ResultCellType::Triangle6:    return ResultCellType::Triangle;
	case ResultCellType::Quad8:        return ResultCellType::Quad;
	case ResultCellType::Tetra10:      return ResultCellType::Tetra;
	case ResultCellType::Hexahedron20: return ResultCellType::Hexahedron;
	case ResultCellType::Wedge15:      return ResultCellType::Wedge;
	case ResultCellType::Pyramid13:    return ResultCellType::Pyramid;
	default: break;
	}
	return type;
}

bool resultCellIsQuadratic(ResultCellType type)
{
	return resultCellCornerType(type) != type;
}

bool resultCellIsVolume(ResultCellType type)
{
	const ResultCellType t = resultCellCornerType(type);
	return t == ResultCellType::Tetra || t == ResultCellType::Hexahedron
		|| t == ResultCellType::Wedge || t == ResultCellType::Pyramid;
}

bool resultCellIsSurface(ResultCellType type)
{
	const ResultCellType t = resultCellCornerType(type);
	return t == ResultCellType::Triangle || t == ResultCellType::Quad;
}

ResultCellType resultCellTypeFromVtk(int vtkCellTypeId)
{
	// vtkCellType.h ids. Polyhedra (42) and anything else not listed stay Unsupported.
	switch (vtkCellTypeId)
	{
	case 3:  return ResultCellType::Line;
	case 5:  return ResultCellType::Triangle;
	case 9:  return ResultCellType::Quad;
	case 10: return ResultCellType::Tetra;
	case 12: return ResultCellType::Hexahedron;
	case 13: return ResultCellType::Wedge;
	case 14: return ResultCellType::Pyramid;
	case 22: return ResultCellType::Triangle6;
	case 23: return ResultCellType::Quad8;
	case 24: return ResultCellType::Tetra10;
	case 25: return ResultCellType::Hexahedron20;
	case 26: return ResultCellType::Wedge15;
	case 27: return ResultCellType::Pyramid13;
	default: break;
	}
	return ResultCellType::Unsupported;
}

QStringList resultCellTypeWarnings(const ResultDataset& dataset)
{
	std::size_t quadratic = 0, unsupported = 0;
	for (ResultCellType t : dataset.cellTypes)
	{
		if (t == ResultCellType::Unsupported)
			++unsupported;
		else if (resultCellIsQuadratic(t))
			++quadratic;
	}
	QStringList warnings;
	if (quadratic > 0)
		warnings << QStringLiteral("%1 quadratic cell(s) are shown through their corner nodes only; mid-edge nodes are ignored, so curved edges are not represented yet.").arg(quadratic);
	if (unsupported > 0)
		warnings << QStringLiteral("%1 cell(s) of unsupported type (e.g. polyhedra, polylines, polygons with more than 4 sides) will not be displayed.").arg(unsupported);
	return warnings;
}

const ResultField* ResultDataset::findField(const QString& name, ResultFieldAssociation association) const
{
	for (const ResultField& f : fields)
		if (f.association == association && f.name == name)
			return &f;
	return nullptr;
}

QString ResultDataset::validate() const
{
	if (nodePositions.size() % 3 != 0)
		return QStringLiteral("node coordinate count is not a multiple of 3");
	if (!nodeIds.empty() && nodeIds.size() != nodeCount())
		return QStringLiteral("node id count does not match node count");

	const std::size_t cells = cellCount();
	if (cellOffsets.size() != cells + 1)
		return QStringLiteral("cell offset count (%1) does not match cell count + 1 (%2)")
			.arg(cellOffsets.size()).arg(cells + 1);
	if (!cellIds.empty() && cellIds.size() != cells)
		return QStringLiteral("cell id count does not match cell count");
	if (cellOffsets.front() != 0)
		return QStringLiteral("first cell offset is not 0");
	if (cellOffsets.back() != cellConnectivity.size())
		return QStringLiteral("last cell offset (%1) does not match connectivity size (%2)")
			.arg(cellOffsets.back()).arg(cellConnectivity.size());

	const std::size_t nodes = nodeCount();
	for (std::size_t c = 0; c < cells; ++c)
	{
		if (cellOffsets[c + 1] < cellOffsets[c])
			return QStringLiteral("cell %1 has decreasing offsets").arg(c);
		const std::size_t count = cellOffsets[c + 1] - cellOffsets[c];
		const int expected = resultCellNodeCount(cellTypes[c]);
		if (expected != 0 && count != static_cast<std::size_t>(expected))
			return QStringLiteral("cell %1 has %2 nodes, expected %3 for its type").arg(c).arg(count).arg(expected);
		for (std::size_t k = cellOffsets[c]; k < cellOffsets[c + 1]; ++k)
			if (cellConnectivity[k] >= nodes)
				return QStringLiteral("cell %1 references node %2 but only %3 nodes exist")
					.arg(c).arg(cellConnectivity[k]).arg(nodes);
	}

	for (const ResultField& f : fields)
	{
		if (f.components <= 0)
			return QStringLiteral("field '%1' has no components").arg(f.name);
		const std::size_t expectedTuples = f.association == ResultFieldAssociation::Node ? nodes : cells;
		for (std::size_t s = 0; s < f.stepData.size(); ++s)
		{
			if (f.stepData[s].empty())
				continue; // not loaded
			if (f.stepData[s].size() != expectedTuples * static_cast<std::size_t>(f.components))
				return QStringLiteral("field '%1' step %2 has %3 values, expected %4")
					.arg(f.name).arg(s).arg(f.stepData[s].size())
					.arg(expectedTuples * static_cast<std::size_t>(f.components));
		}
	}
	return QString();
}
