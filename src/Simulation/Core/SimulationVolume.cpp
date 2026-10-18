#include "SimulationVolume.h"

#include "ResultStreamlines.h"
#include "SimulationResultDisplay.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

bool isVolumeField(const ResultField& field)
{
	// A cell field qualifies too: it is averaged onto the nodes first (SimulationCellAveraging.h).
	return (field.components == 1 || field.components == 3) && resultFieldHasData(field);
}

int chooseDefaultVolumeField(const ResultDataset& dataset)
{
	// A node field first (nothing to average), then a cell field.
	for (ResultFieldAssociation association : { ResultFieldAssociation::Node, ResultFieldAssociation::Cell })
		for (int components : { 1, 3 })
			for (std::size_t i = 0; i < dataset.fields.size(); ++i)
				if (dataset.fields[i].association == association && dataset.fields[i].components == components && isVolumeField(dataset.fields[i]))
					return static_cast<int>(i);
	return -1;
}

bool buildVolumeGrid(const ResultDataset& dataset, const CellLocator& locator, int fieldIndex, int component, int step,
                     int targetResolution, VolumeGrid& out)
{
	out = VolumeGrid();
	if (locator.volumeCellCount() == 0)
		return false;
	DisplayScalar scalar;
	// A cell field is averaged onto the nodes first (the grid is resampled from node values).
	const bool cellField = fieldIndex >= 0 && static_cast<std::size_t>(fieldIndex) < dataset.fields.size()
	                       && dataset.fields[static_cast<std::size_t>(fieldIndex)].association == ResultFieldAssociation::Cell;
	std::unique_ptr<CellToNodeAverager> averager;
	if (cellField)
		averager = std::make_unique<CellToNodeAverager>(dataset);
	if (!buildDisplayScalar(dataset, fieldIndex, component, scalar, step, averager.get()) || scalar.cellData)
		return false;

	double lo[3], hi[3];
	if (!locator.bounds(lo, hi))
		return false;
	double extent[3] = { hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2] };
	const double longest = std::max({ extent[0], extent[1], extent[2] });
	if (!(longest > 0.0) || !std::isfinite(longest))
		return false;
	targetResolution = std::clamp(targetResolution, 16, 256);
	// Voxels per axis scale with the bounding box's own proportions (the longest axis gets targetResolution, the
	// others less) - correct for a roughly cubical model, but an elongated, thin-walled one (an I-beam: ~1000 mm
	// long, 60-100 mm across) would otherwise crush a short axis to a handful of voxels, too coarse to represent a
	// thin web/flange at all (observed: 64 x 4 x 9 on the sample I-beam at "Medium"). The floor is a QUARTER of
	// targetResolution (at least 16), not a flat constant: a flat floor equal to the lowest tier's own resolution
	// would sit right at (or above) a ~17:1 model's natural proportional value at EVERY quality tier, so raising
	// quality would only ever improve the long axis and the short one would never get any sharper - confirmed for
	// real (still "somewhat better", not fixed, after a flat 16 floor). Scaling the floor with targetResolution lets
	// a higher quality choice still sharpen a thin cross-section, while never letting any axis collapse to a
	// handful of voxels at the coarsest setting either.
	const int minVoxelsPerAxis = std::max(16, targetResolution / 4);
	int dims[3];
	for (int axis = 0; axis < 3; ++axis)
	{
		dims[axis] = std::clamp(static_cast<int>(std::lround(targetResolution * extent[axis] / longest)), minVoxelsPerAxis, targetResolution);
		out.origin[axis] = static_cast<float>(lo[axis]);
		out.voxelSize[axis] = dims[axis] > 0 ? static_cast<float>(extent[axis] / dims[axis]) : 0.0f;
	}
	out.dimX = dims[0]; out.dimY = dims[1]; out.dimZ = dims[2];
	out.label = scalar.label;
	out.unit = scalar.unit;
	out.values.assign(out.voxelCount(), std::numeric_limits<float>::quiet_NaN());

	const std::vector<float> zeroVectors(dataset.nodeCount() * 3, 0.0f);
	float minimum = std::numeric_limits<float>::max(), maximum = std::numeric_limits<float>::lowest();
	bool any = false;
	for (int z = 0; z < out.dimZ; ++z)
		for (int y = 0; y < out.dimY; ++y)
		{
			int hint = -1; // reuse along each x-fast scan line; do not carry a distant row's last cell into the next
			for (int x = 0; x < out.dimX; ++x)
			{
				const double p[3] = { lo[0] + (x + 0.5) * out.voxelSize[0],
				                      lo[1] + (y + 0.5) * out.voxelSize[1],
				                      lo[2] + (z + 0.5) * out.voxelSize[2] };
				double vector[3], value = 0.0;
				if (!locator.interpolate(p, zeroVectors, &scalar.nodeValues, hint, vector, value) || !std::isfinite(value))
					continue;
				const std::size_t at = (static_cast<std::size_t>(z) * out.dimY + y) * out.dimX + x;
				out.values[at] = static_cast<float>(value);
				minimum = std::min(minimum, out.values[at]);
				maximum = std::max(maximum, out.values[at]);
				any = true;
			}
		}
	if (!any)
	{
		out = VolumeGrid();
		return false;
	}
	out.fieldMin = minimum;
	out.fieldMax = maximum;
	if (!(out.fieldMax > out.fieldMin))
		out.fieldMax = out.fieldMin + std::max(1.0e-6f, std::fabs(out.fieldMin) * 1.0e-6f);
	return true;
}
