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
	// through the given camera at the given resolution, single sample per
	// pixel, no accumulation. On success, resizes outImageRgba8 to
	// width*height*4 bytes (row-major, no padding) and returns true.
	bool renderScene(const RtCamera& camera, int width, int height, std::vector<uint8_t>& outImageRgba8);

private:
	struct Impl;
	std::unique_ptr<Impl> _impl;
};
