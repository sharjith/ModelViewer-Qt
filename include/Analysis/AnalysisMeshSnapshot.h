#pragma once

#include <vector>
#include <QtGlobal>

#include "BoundingBox.h"
#include "SurfaceAnalysisOverlay.h"

class SceneMesh;

// Immutable, Qt/GL-free flattened copy of ONE mesh's world-space geometry,
// built synchronously on the main thread right before dispatching a
// background AnalysisComputeWorker - same shape/intent as RtSceneSnapshot
// (see its own doc comment), just at per-mesh granularity instead of
// whole-scene: a background thread must never read a live, mutably-changing
// SceneMesh concurrently with the main thread, so this copies out exactly
// what the CGAL analyzers need up front instead.
struct AnalysisMeshSnapshot
{
	// Opaque correlation token ONLY - identifies which live mesh a result
	// belongs to once the worker hands it back, never dereferenced off the
	// main thread. Same no-dereference convention as RtSceneSnapshot's own
	// mesh handles.
	SceneMesh* meshHandle = nullptr;

	std::vector<float> points;          // world-space, mirrors getTrsfPoints()
	std::vector<unsigned int> indices;  // mirrors getIndices()
	std::vector<float> normals;         // mirrors getTrsfNormals() - only CurvatureAnalyzer needs this
	std::vector<quint64> sourceMeshIds; // mirrors getSourceMeshIds() - only CurvatureAnalyzer needs this
	bool topologyRepaired = false;      // mirrors SceneMesh::topologyRepaired() - Mass Properties/Wall Thickness only
	BoundingBox boundingBox;

	// Stamped via SurfaceAnalysisOverlay::computeCurrentKey() at capture
	// time - the key the worker's result is being computed against. The
	// caller re-derives a fresh key once the result comes back and compares
	// via CacheKey::operator== to decide whether to apply it (see
	// AnalysisComputeSession's own doc comment).
	SurfaceAnalysisOverlay::CacheKey key;
};

// Captures `mesh`'s current geometry + cache key. `parameters` is opaque to
// this function - passed straight through to computeCurrentKey() (ball
// radius, pull direction, analysis mode name, etc., whatever the caller's
// analysis mode needs to distinguish a parameter change from a no-op
// re-Apply). `referenceMesh`, when given (Deviation only), also stamps the
// key's reference fields from that mesh's own current state - it is NOT
// itself captured into a snapshot by this function; a caller comparing two
// meshes captures each one separately.
AnalysisMeshSnapshot captureAnalysisMeshSnapshot(SceneMesh* mesh, const QVariantMap& parameters,
	SceneMesh* referenceMesh = nullptr);
