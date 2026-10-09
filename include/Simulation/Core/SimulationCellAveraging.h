#pragma once

// Cell (element) data -> node data. A cell field has ONE value per cell, constant over it; everything that interpolates (the charts, the volume
// rendering, iso-surfaces, a smooth colour map) needs values at the nodes. A node takes the average of the values of the cells around it, weighted by
// the SIZE of each cell (volume of a volume cell, area of a surface cell, length of a line), so a few huge cells do not count as much as many tiny
// ones: node value = sum(measure_c * value_c) / sum(measure_c) over the cells that touch the node and have a finite value. A node no cell touches, or
// whose cells have no finite value, gets NaN. Only the corner nodes of a quadratic cell take part. GUI-free, unit-tested.
//
// The weights depend on the geometry only, so one averager serves every step and every field of a result.

#include "ResultDataset.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class CellToNodeAverager
{
public:
	explicit CellToNodeAverager(const ResultDataset& dataset);

	// False when no cell references a node (nothing to average).
	bool empty() const { return _cell.empty(); }

	// One value per node from one value per cell (a wrong-sized `cellValues` leaves `nodeValues` all NaN).
	void average(const std::vector<float>& cellValues, std::vector<float>& nodeValues) const;

	// The averaged value at a single node (NaN as above) - cheap, for a point's history that needs only a few nodes of a field.
	float valueAt(std::uint32_t node, const std::vector<float>& cellValues) const;

	// The size used to weight a cell: volume, area or length; 0 for a cell that has none (those weigh as the average cell). Exposed for the tests.
	static double cellMeasure(const ResultDataset& dataset, std::size_t cell);

private:
	std::size_t _nodeCount = 0;
	std::vector<std::uint32_t> _start; // CSR over nodes: node n has the cells _cell[_start[n] .. _start[n + 1])
	std::vector<std::uint32_t> _cell;
	std::vector<float> _weight;        // the measure of each of those cells
};
