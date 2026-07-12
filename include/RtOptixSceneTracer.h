#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "RtSceneSnapshot.h"

// ---------------------------------------------------------------------------
// RtOptixSceneTracer
//
// Phase 2a of the GPU (OptiX) path tracer backend - see RtOptixSceneParams.h's
// doc comment for exactly what this does and doesn't render yet (real
// geometry/instancing/camera, flat-normal-as-color shading, no materials/
// lights/bounces). Builds a real two-level acceleration structure (GAS per
// RtMeshGeometry, IAS with one OptixInstance per RtInstance) mirroring
// RtEmbreeScene's BLAS/TLAS structure.
//
// Deliberately self-contained (owns its own CUDA/OptiX device context) same
// as RtOptixTracer (Phase 1b) - see that class's doc comment for why. Will
// be consolidated once real material/lighting rendering replaces both test
// paths.
//
// Compiles to an inert stub when built without the CUDA/OptiX toolchain,
// same pattern as RtOptixContext/RtOptixTracer.
// ---------------------------------------------------------------------------
class RtOptixSceneTracer
{
public:
	RtOptixSceneTracer();
	~RtOptixSceneTracer();

	RtOptixSceneTracer(const RtOptixSceneTracer&) = delete;
	RtOptixSceneTracer& operator=(const RtOptixSceneTracer&) = delete;

	bool isAvailable() const;

	// (Re)builds the GPU acceleration structure from this snapshot's
	// meshes/instances - mirrors RtEmbreeScene::build()'s contract. Always
	// rebuilds unconditionally (no revision-check fast path yet - see
	// RtEmbreeScene's own revisionId()/RtFrameAccumulator's handling for the
	// pattern a later phase should adopt here). Returns false if OptiX isn't
	// available or the build failed.
	bool buildScene(const RtSceneSnapshot& snapshot);

	// Renders the current scene (from the last successful buildScene() call)
	// through the given camera at the given resolution, averaging
	// samplesPerPixel jittered primary-ray path samples (box-filter AA
	// jitter, each path up to maxBounces long - see RtOptixSceneParams.h's
	// samplesPerPixel/maxBounces doc comments) within a single optixLaunch().
	// Output is linear HDR radiance (un-tonemapped) - same contract as
	// CpuPathTracer's own frame output - so callers wanting progressive
	// multi-launch accumulation (see RtOptixPathTracingSession) can average
	// multiple calls' results in linear space before a single final tonemap.
	// outAlbedo/outNormal are OIDN guide (auxiliary feature) buffers -
	// primary-hit base color/world-space shading normal, chunk-averaged the
	// same way the beauty image is - mirroring RtFrameAccumulator::
	// resolveAlbedo()/resolveNormal()'s contract, see RtOptixSceneParams.h's
	// albedoImage/normalImage doc comment. On success, resizes all three to
	// width*height and returns true.
	bool renderScene(const RtCamera& camera, int width, int height, unsigned int samplesPerPixel, unsigned int maxBounces,
		std::vector<glm::vec3>& outImageLinearRgb, std::vector<glm::vec3>& outAlbedo, std::vector<glm::vec3>& outNormal);

private:
	struct Impl;
	std::unique_ptr<Impl> _impl;
};
