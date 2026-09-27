#pragma once

#include "ResultDataset.h"

#include <QString>

#include <cstddef>
#include <vector>

class CellLocator;

// A node field resampled on a regular grid. Samples lie at voxel centres; origin is the minimum corner of the
// represented box and voxelSize * dimensions is its full extent. Non-volume space is NaN and renders transparent.
struct VolumeGrid
{
	std::vector<float> values;
	int dimX = 0, dimY = 0, dimZ = 0;
	float origin[3] = { 0.0f, 0.0f, 0.0f };
	float voxelSize[3] = { 0.0f, 0.0f, 0.0f };
	float fieldMin = 0.0f, fieldMax = 1.0f;
	QString label, unit;

	std::size_t voxelCount() const { return static_cast<std::size_t>(dimX) * dimY * dimZ; }
	bool empty() const { return dimX <= 0 || dimY <= 0 || dimZ <= 0 || values.size() != voxelCount(); }
};

// Resamples one scalar/node component (or a three-component vector's magnitude) over the locator's current shape.
// targetResolution is clamped to 16..256 and applies to the longest axis; the other dimensions scale with the
// bounding box's aspect ratio, floored at targetResolution/4 (at least 16) so an elongated, thin-walled model's
// short axis is never crushed to a handful of voxels - see buildVolumeGrid's own comment for why a flat floor was
// not enough (it stopped a higher quality choice from ever sharpening a thin cross-section).
bool buildVolumeGrid(const ResultDataset& dataset, const CellLocator& locator, int fieldIndex, int component, int step,
                     int targetResolution, VolumeGrid& out);

// First node scalar or vector field with data, preferring scalar fields. -1 when none is volume-renderable.
int chooseDefaultVolumeField(const ResultDataset& dataset);
bool isVolumeField(const ResultField& field);
