#pragma once

#include <vector>

class SceneMesh;

// ---------------------------------------------------------------------------
// DeviationAnalyzer
//
// Unsigned rev-to-rev / scan-vs-CAD deviation: for each vertex of
// `sampledMesh` (world-space, via getTrsfPoints()), the nearest-point
// distance to `referenceMesh`'s own surface (a CGAL AABB tree built directly
// over `referenceMesh`'s ORIGINAL triangles - no MeshRepair pass, since
// repair can alter geometry and would mean measuring repaired-vs-original
// rather than the actual imported surfaces on both sides).
//
// Explicit limitations, by design for this first pass (see this app's
// Surface Analysis implementation plan):
//  - UNSIGNED magnitude only. A nearest-point query gives distance, not
//    "over vs. under material" - fabricating a sign without a real
//    reference-orientation definition would be presenting a guess as a
//    measurement.
//  - Both meshes must already be spatially aligned (same coordinate frame)
//    and use consistent units - this is a precondition the caller/UI must
//    state, not something this analyzer can detect or correct. A pure
//    transform offset between two otherwise-identical meshes will read
//    exactly like a real deviation.
//  - Per-vertex SAMPLED deviation, not exhaustive surface coverage - a
//    genuine deviation entirely inside a large reference triangle, between
//    two of the sampled mesh's vertices, can be missed.
//  - Zero-area (degenerate) reference triangles are skipped when building
//    the tree rather than left to corrupt nearest-point queries.
// ---------------------------------------------------------------------------
class DeviationAnalyzer
{
public:
	// Returns one distance per vertex of sampledMesh (world-space units,
	// same convention as every other Surface Analysis mode), or an empty
	// vector if either mesh is null/empty or referenceMesh has no non-
	// degenerate triangles to measure against.
	static std::vector<float> computeDeviation(SceneMesh* sampledMesh, SceneMesh* referenceMesh);

	// Snapshot-based entry point - identical computation, but reads world-
	// space points/indices directly instead of live SceneMesh* pointers, so
	// it's safe to call from a background thread against two
	// AnalysisMeshSnapshots' copied-out geometry. The SceneMesh* overload
	// above is now a thin wrapper around this one.
	static std::vector<float> computeDeviation(
		const std::vector<float>& sampledPoints, const std::vector<unsigned int>& sampledIndices,
		const std::vector<float>& referencePoints, const std::vector<unsigned int>& referenceIndices);
};
