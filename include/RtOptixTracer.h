#pragma once

#include <cstdint>
#include <memory>
#include <vector>

// User-facing path-tracing render-engine choice (PT settings dropdown) -
// mirrors DenoiserDevicePreference's placement/style in RtDenoiser.h,
// including the Auto option: GPU is now a full, feature-complete backend
// (this enum originally had no Auto specifically because GPU only rendered
// a Phase 1b test-triangle placeholder at the time - no longer true, so the
// reasoning against silent fallback no longer applies either). Auto resolves
// to a concrete CPU/GPU choice via ViewportWidget::
// effectivePathTracingEnginePreference() - see that function's doc comment
// for how (a cheap check, not a new probe). Every render-path branch reads
// the EFFECTIVE preference, never this raw one directly, so Auto never needs
// handling at individual call sites - CPU and GPU remain the only two real
// backends as far as rendering code is concerned.
enum class RtPathTracingEnginePreference
{
	Auto,
	CPU,
	GPU
};

// ---------------------------------------------------------------------------
// RtOptixTracer
//
// Phase 1b of the GPU (OptiX) path tracer backend - see RtOptixContext's doc
// comment for Phase 1's context-creation-only checkpoint. This adds the rest
// of the minimal OptiX pipeline (GAS, module/pipeline/SBT, optixLaunch) but
// only for ONE hardcoded test triangle (see src/cuda/RtOptixTriangle.cu) -
// still no real scene data, materials, or bounces. The point of this
// checkpoint is proving the full round trip (build acceleration structure ->
// compile/link pipeline from embedded PTX -> launch -> read pixels back to
// the host) works before any of it is wired to the app's actual scene
// snapshot / material system.
//
// Deliberately self-contained (creates and owns its own CUDA/OptiX device
// context rather than sharing RtOptixContext's) to keep this Phase 1b
// checkpoint isolated and simple to reason about - see its constructor's
// comment for why. A later phase will consolidate onto one shared context
// once real scene rendering replaces this test path entirely.
//
// Compiles to an inert stub (isAvailable() always false, renderTestTriangle()
// always false) when built without the CUDA/OptiX toolchain, same pattern as
// RtOptixContext.
// ---------------------------------------------------------------------------
class RtOptixTracer
{
public:
	RtOptixTracer();
	~RtOptixTracer();

	RtOptixTracer(const RtOptixTracer&) = delete;
	RtOptixTracer& operator=(const RtOptixTracer&) = delete;

	bool isAvailable() const;

	// Renders the hardcoded test triangle at the given resolution. On
	// success, resizes outImageRgba8 to width*height*4 bytes (row-major,
	// no padding, R,G,B,A per pixel) and returns true. Returns false (and
	// leaves outImageRgba8 untouched) if OptiX isn't available or a launch
	// failed - see the log for the specific reason.
	bool renderTestTriangle(int width, int height, std::vector<uint8_t>& outImageRgba8);

private:
	struct Impl;
	std::unique_ptr<Impl> _impl;
};
