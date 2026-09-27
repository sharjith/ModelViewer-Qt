#pragma once

#include <memory>

// ---------------------------------------------------------------------------
// RtOptixContext
//
// Phase 1 foundation for the GPU (OptiX 7.7 + CUDA) path tracer backend - see
// the "OptiX GPU path tracer" project notes. This class does nothing more
// than initialize CUDA and create an OptixDeviceContext, then report whether
// that succeeded - no scene data, no pipeline, no rendering yet. The point is
// to validate the toolchain (CMake's CUDA/OptiX discovery, the NVIDIA driver's
// OptiX runtime support on the actual target GPU) as its own isolated,
// independently-testable checkpoint before building anything on top of it.
//
// Mirrors RtDenoiser's device-init logging/fallback conventions: construction
// never throws or crashes the app even if CUDA/OptiX aren't available (no
// NVIDIA GPU, driver too old, etc.) - isAvailable() reports the outcome, and
// every other member function is a safe no-op when it's false.
//
// Compiles to an inert stub (isAvailable() always false) when the project was
// configured without the CUDA/OptiX toolchain (MODELVIEWER_HAVE_OPTIX not
// defined - see CMakeLists.txt's CheckLanguage(CUDA) probe) so this header
// can be included and this class instantiated unconditionally from anywhere
// in the app, the same way RtDenoiser's CPU/CUDA device split already works.
// ---------------------------------------------------------------------------
class RtOptixContext
{
public:
	RtOptixContext();
	~RtOptixContext();

	RtOptixContext(const RtOptixContext&) = delete;
	RtOptixContext& operator=(const RtOptixContext&) = delete;

	// True once CUDA + OptiX initialized successfully and an OptixDeviceContext
	// is ready to use. False (always, unconditionally) when this build has no
	// CUDA/OptiX toolchain, or when initialization failed at runtime (no
	// compatible NVIDIA GPU, driver lacks OptiX support, etc. - see the log
	// for the specific reason).
	bool isAvailable() const;

	// Human-readable device name (e.g. "NVIDIA GeForce GTX 1660 Ti") once
	// available, empty otherwise - for UI/log display alongside RtDenoiser's
	// own activeDeviceName().
	const char* deviceName() const;

private:
	struct Impl;
	std::unique_ptr<Impl> _impl;
};
