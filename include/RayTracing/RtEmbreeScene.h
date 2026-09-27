#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "RtSceneSnapshot.h"
#include "RtRay.h"

// Opaque Embree handles, forward-declared to avoid leaking embree3/rtcore.h
// (and its C API) into every translation unit that includes this header -
// only RtEmbreeScene.cpp needs the real Embree types.
struct RTCDeviceTy;
struct RTCSceneTy;
using RTCDevice = RTCDeviceTy*;
using RTCScene  = RTCSceneTy*;

// ---------------------------------------------------------------------------
// RtEmbreeScene
//
// Wraps an Embree device + two-level scene (one BLAS scene per unique mesh,
// one TLAS scene of RTC_GEOMETRY_TYPE_INSTANCE geometries applying each
// RtInstance's world transform) built from an RtSceneSnapshot.
//
// v1 does not deduplicate identical geometry across instances (see
// RtSceneSnapshot.h) - every instance still gets its own BLAS scene today,
// but the two-level instancing structure is already in place so adding real
// geometry sharing later (the large-assembly perf-tuning pass) is a
// self-contained change here, not an architecture change.
//
// Rebuilt only when the snapshot's geometry/instances actually change -
// camera-only changes should not call build() again (see RtFrameAccumulator's
// revision handling).
// ---------------------------------------------------------------------------
class RtEmbreeScene
{
public:
	RtEmbreeScene();
	~RtEmbreeScene();

	RtEmbreeScene(const RtEmbreeScene&)            = delete;
	RtEmbreeScene& operator=(const RtEmbreeScene&) = delete;

	// Builds the BLAS/TLAS structure from the snapshot. The snapshot is kept
	// alive (shared_ptr) for the lifetime of this scene, since Embree's
	// vertex/index buffers are shared (zero-copy) directly against the
	// snapshot's own geometry vectors rather than duplicated.
	bool build(std::shared_ptr<const RtSceneSnapshot> snapshot);

	// Finds the closest hit along the ray. Returns a default-constructed
	// (hit=false) RtHit on a miss.
	RtHit intersect(const RtRay& ray) const;

	// Cheaper any-hit query for shadow/occlusion rays - does not compute
	// shading data, just whether anything blocks the ray.
	bool occluded(const RtRay& ray) const;

	uint64_t revisionId() const { return _snapshot ? _snapshot->revisionId : 0; }

private:
	void releaseScene();

	RTCDevice _device = nullptr;
	RTCScene  _topScene = nullptr;         // TLAS: one INSTANCE geometry per RtInstance
	std::vector<RTCScene> _blasScenes;     // one per RtMeshGeometry, index == meshIndex
	std::vector<uint32_t> _geomIdToInstanceIndex; // TLAS geomID -> index into snapshot->instances

	std::shared_ptr<const RtSceneSnapshot> _snapshot;
};
