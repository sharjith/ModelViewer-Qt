#pragma once

#include <cstdint>
#include <vector>

// The drawing a PMI entity stored in its file, in the part's own frame: the 2D reference frame that defines its annotation
// plane, and its polylines (arrows, extension lines, leaders and the glyph strokes of its text). Kept apart from the property
// rows (NodeProperties.h) because it is bulky and only recorded-PMI drawing reads it.
struct PmiEntityGeometry
{
	bool hasFrame = false;
	float frame[9] = {};              // origin, X-axis point, Y-axis point
	std::vector<std::int32_t> indices; // start vertex of each polyline, then the vertex count: polyline k spans [indices[k], indices[k+1])
	std::vector<std::int16_t> types;   // pairs (up to polyline index, type): JT 10.6 Table 59; type 8 marks text strokes
	std::vector<float> vertices;       // x y z per vertex
};

// One part's recorded PMI drawings, indexed by entity number (the "PMI <n>" of the flattened rows).
using PmiGeometry = std::vector<PmiEntityGeometry>;
