#pragma once

#include <cstddef>
#include <vector>
#include <QString>

#include "SubTriangleGrid.h"

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
// How thickness is estimated.
//  * NormalRay - the original estimate: one ray per triangle, from its centroid along the inward normal, to the
//    first opposite wall. Fast, but it measures the part's size along that normal rather than local wall
//    thickness, and on a CAD tessellation (a few huge triangles) the result follows the triangulation.
//  * LocalThickness - many sample points per triangle (sized to the model, not the triangle), each casting a
//    ray straight along the inward normal (plus, at a non-zero spread, a cone of rays around it). A ray counts only if it exits through a wall that faces back at
//    the surface. Each hit gives the diameter of the sphere tangent to the surface at the sample that just
//    reaches it (distance / cos(angle)): a flat wall reads its thickness on the axis ray and larger elsewhere,
//    a round bar reads its diameter on every ray. The sample's value is the smallest of its rays, a triangle's
//    the smallest of its samples. Finds ribs, slots and fins a single normal ray misses, and does not depend on how
//    the surface was tessellated. Still an estimate of the medial-sphere definition, not an exact evaluation
//    of it.
enum class WallThicknessMethod { NormalRay, LocalThickness };

struct WallThicknessParams
{
	WallThicknessMethod method = WallThicknessMethod::LocalThickness;

	// ---- LocalThickness tuning (fixed by the UI today; exposed so a future option can adjust them) ----
	// Half angle of the ray cone around the inward normal, 0 - 45 degrees. 0 casts only the straight-in ray from
	// each sample (the distance to the wall directly behind it); larger values also probe obliquely, which finds
	// thin features that sit off to the side of a sample at the cost of reading a flat wall's sloped neighbours
	// as thin (an oblique ray always reads >= the wall thickness, so this never under-reports a flat wall).
	double coneHalfAngleDegrees = 0.0;
	// Target sample spacing as a fraction of the mesh's bounding-box diagonal (1/N).
	int samplesAcrossBoundingBox = 64;
	// Hard cap on total sample points, so a very fine mesh cannot run away; the spacing widens to fit.
	std::size_t sampleBudget = 200000;
	// A ray's exit wall must face back at the surface: dot(exit-wall normal, inward normal) >= this. Rejects
	// rays that leave through an adjacent side wall (near a convex edge), which would otherwise read as
	// spuriously thin.
	double minExitAlignment = 0.5;
};

// How a sample's ray(s) ended - Valid, or why the sample has no value.
enum class WallThicknessSampleStatus : unsigned char
{
	Valid = 0,
	NoHit,          // the ray met nothing (or nothing usable) behind the surface
	OtherSolid,     // every hit belonged to a different solid body of the mesh
	EnteringFace,   // the ray met a surface from outside (overlapping / touching bodies)
	GlancingExit,   // the ray leaves through a wall too steep to measure straight through (edge, curved wall)
	DegenerateHit   // the wall behind is a degenerate triangle
};

// The ray behind one sample - "where was this measured" - in the same (world-space) units as the mesh points.
// For a valid sample it is the ray that produced the value (the value is distance / cos(angleDegrees)); for a sample
// without a value it is the axis ray and the last hit that ruled it out, which is what explains the gap.
// hitTriangle is the triangle (index into the mesh's own index buffer) that was hit, -1 if none. facing is the dot
// product of the hit wall's outward normal and the inward direction (1 = wall squarely behind, 0 = edge-on).
struct WallThicknessWitness
{
	float origin[3] = { 0, 0, 0 };
	float hit[3] = { 0, 0, 0 };
	float angleDegrees = 0.0f;
	float distance = 0.0f;
	float facing = 0.0f;
	int hitTriangle = -1;
	WallThicknessSampleStatus status = WallThicknessSampleStatus::NoHit;
};

struct WallThicknessResult
{
	// One entry per triangle of the mesh's OWN index buffer (getIndices()
	// order), populated only if succeeded is true.
	std::vector<float> thicknessPerFace;
	std::vector<bool> validPerFace;
	// LocalThickness only (empty for NormalRay): the value at each sub-triangle sample of each triangle, so a
	// display can show where within a large triangle the thickness changes instead of one colour per triangle.
	// thicknessPerFace[t] is the minimum of triangle t's samples.
	SubTriangleField samples;
	// LocalThickness only: for each entry of samples.values, the ray behind it (same indexing) - for a sample
	// without a value, the ray that ruled it out and why. Empty for NormalRay.
	std::vector<WallThicknessWitness> sampleWitness;
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
	static WallThicknessResult computeThickness(SceneMesh* mesh, const WallThicknessParams& params = WallThicknessParams());

	// Snapshot-based entry point - identical computation, but reads world-
	// space points/indices directly instead of a live SceneMesh*, so it's
	// safe to call from a background thread against an AnalysisMeshSnapshot's
	// copied-out geometry. The SceneMesh* overload above is now a thin
	// wrapper around this one.
	static WallThicknessResult computeThickness(
		const std::vector<float>& points, const std::vector<unsigned int>& indices,
		const WallThicknessParams& params = WallThicknessParams());
};
