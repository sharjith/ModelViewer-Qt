#pragma once

#include <vector>
#include <QString>

class SceneMesh;

// ---------------------------------------------------------------------------
// WallThicknessAnalyzer
//
// Per-face inward-ray-distance wall-thickness ESTIMATE (not a guaranteed true
// minimum wall thickness - the real minimum can occur along a direction
// other than the surface normal; this is disclosed in the dialog UI, not
// just here). The riskiest of this app's Surface Analysis modes - see the
// implementation plan's own "hardest, built last" ordering.
//
// Deliberately does NOT run MeshRepair - same reasoning DraftAngleAnalyzer/
// DeviationAnalyzer already apply: altering the very walls being measured
// defeats the purpose. Instead:
//  1. A whole-mesh validity gate (closed, non-self-intersecting, bounds a
//     volume - same 3-step CGAL predicate order as MeshProperties.cpp, for
//     the same undefined-behavior-on-bad-input reason) runs on the ORIGINAL
//     triangles first. A mesh failing this is rejected WHOLE, with the
//     specific reason - never partially colored, and never per-region
//     "this bit is fine" - see this app's plan for why v1 only supports
//     this simpler whole-mesh gate.
//  2. Only on a mesh that already passed step 1 (so orientation IS resolved
//     to a real one, not undefined behavior) is orient_to_bound_a_volume()
//     run, to establish which side is genuinely "inward" - passing the
//     closed/non-self-intersecting/bounds-a-volume gate does NOT by itself
//     establish that; consistently-oriented faces can still all point
//     either way.
//  3. Rays are filtered by SOLID REGION (CGAL::Polygon_mesh_processing::
//     volume_connected_components(), which correctly treats a hollow
//     solid's inner+outer wall as ONE region), not by surface-connected-
//     component (which would wrongly reject exactly the outer-to-inner hit
//     this analysis needs - a correction to this plan's own earlier,
//     wrong advice). This still correctly excludes a hit on a genuinely
//     different, disconnected solid body sharing the same SceneMesh.
// ---------------------------------------------------------------------------
struct WallThicknessResult
{
	// One entry per triangle of the mesh's OWN index buffer (getIndices()
	// order), populated only if succeeded is true.
	std::vector<float> thicknessPerFace;
	std::vector<bool> validPerFace;
	// Non-empty only when the whole mesh was rejected (succeeded == false) -
	// the specific reason (open boundary / self-intersecting / unresolved
	// orientation / invalid indices), for the dialog to surface directly
	// rather than a bare failure.
	QString rejectionReason;
	bool succeeded = false;
};

class WallThicknessAnalyzer
{
public:
	static WallThicknessResult computeThickness(SceneMesh* mesh);
};
