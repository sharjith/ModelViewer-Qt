#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

// ---------------------------------------------------------------------------
// RtDenoiser
//
// Wraps an Intel Open Image Denoise "RT" filter on the CPU device (this app
// has no GPU compute path, so CPU is the only device type that makes sense
// here - see the vcpkg overlay port comment in overlay-ports/openimagedenoise
// for why OIDN is repackaged as a custom port in the first place).
//
// Denoises a linear HDR RGB buffer produced by RtFrameAccumulator::resolve()
// into a separate, visually clean buffer for display, while the raw
// accumulation keeps refining underneath (see RtPathTracingSession) - this is
// what lets a handful of samples already look presentable instead of showing
// raw Monte Carlo noise while converging.
//
// Uses a pimpl so oidn.hpp/the OIDN C++ API doesn't leak into every
// translation unit that includes this header, matching the same pattern
// RtEmbreeScene.h uses for Embree's opaque handles.
// ---------------------------------------------------------------------------
class RtDenoiser
{
public:
	RtDenoiser();
	~RtDenoiser();

	RtDenoiser(const RtDenoiser&)            = delete;
	RtDenoiser& operator=(const RtDenoiser&) = delete;

	// Denoises input (width*height linear HDR RGB, un-tonemapped) into output.
	// Returns false if the OIDN device failed to initialize or reported an
	// error - callers should treat that as "OIDN unavailable this pass", not
	// a hard failure: output is still populated, via a lower-quality built-in
	// bilateral fallback filter rather than a raw copy (see RtDenoiser.cpp),
	// so path-traced frames aren't left permanently noisy on machines where
	// OIDN's CPU device fails to initialize.
	//
	// sampleCount (RtFrameAccumulator::sampleCount() - how many passes are
	// already averaged into input) tapers the fallback filter's strength: the
	// raw accumulated average is noisiest right after a reset and gets
	// progressively cleaner on its own as more samples land, so unconditional
	// full-strength smoothing on every pass regressed to a permanently smudgy
	// image once convergence had already done most of the work itself, and
	// wasted CPU time smoothing an image that barely needed it anymore. Only
	// affects the fallback path - ignored when OIDN itself is used.
	bool denoise(const std::vector<glm::vec3>& input, int width, int height,
		std::vector<glm::vec3>& output, uint32_t sampleCount = 1);

private:
	struct Impl;
	std::unique_ptr<Impl> _impl;
};
