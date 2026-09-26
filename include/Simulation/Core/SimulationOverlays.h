#pragma once

// The drawable extras of a simulation result that are not scene meshes: cut faces and iso-surfaces (SliceDisplay), streamlines (StreamlineDisplay).
// Plain data (standard library only), so a snapshot can store them and the GL controllers in UI/ can draw them.

#include <cstddef>
#include <cstdint>
#include <vector>

// A coloured triangle set cut out of a result's volume (a data-coloured section, an iso-surface), ready to draw. Positions are in the result
// mesh's own frame (the dataset's coordinates), colours are final RGB.
struct SliceDisplay
{
	std::vector<float> positions;         // 3 per vertex
	std::vector<float> colors;            // 3 per vertex
	std::vector<std::uint32_t> triangles; // 3 vertex indices per triangle
	bool lit = false;                     // headlight shading by each triangle's normal (an iso-surface); off = the colours as they are (a data section)
};

// The streamlines of one result, ready to draw: line segments (already trimmed to what the Clipping Planes leave visible) between vertices with a baked colour.
// Positions are in the dataset's own coordinates (the undeformed mesh), like the cut faces.
struct StreamlineDisplay
{
	std::vector<float> positions;              // xyz per vertex
	std::vector<float> colors;                 // rgb per vertex
	std::vector<std::uint32_t> segments;       // 2 vertex indices per segment
	std::size_t segmentCount() const { return segments.size() / 2; }
};

// One axis-aligned Clipping Plane as it was when the overlays were made: the plane perpendicular to `axis` (0 = X, 1 = Y, 2 = Z) at `position` (scene coordinates),
// keeping the +axis side when `keepPositive`.
struct OverlayClipCut
{
	int axis = 0;
	double position = 0.0;
	bool keepPositive = false;
};

// What a snapshot keeps of the volume features when the volume itself is not stored: the cut faces, iso-surfaces and streamlines exactly as they were
// displayed (frozen: they do not follow a moved plane or another time step), and the Clipping Planes they were trimmed to.
struct SnapshotOverlays
{
	std::vector<SliceDisplay> slices;
	StreamlineDisplay streamlines;
	std::vector<OverlayClipCut> cuts;
	bool empty() const { return slices.empty() && streamlines.segmentCount() == 0; }
};
