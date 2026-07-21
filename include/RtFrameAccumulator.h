#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

// ---------------------------------------------------------------------------
// RtFrameAccumulator
//
// Running-average HDR accumulation buffer. Pure math, no threading - each
// call to accumulate() adds one CpuPathTracer::renderPass() result (one
// sample per pixel) into a running sum; resolve() returns the current
// average (sum / sampleCount). Owned and driven by RtPathTracingSession's
// background loop, which decides when to reset() (geometry/material/
// visibility/camera changes - see RtPathTracingSession.h).
// ---------------------------------------------------------------------------
class RtFrameAccumulator
{
public:
	void resize(int width, int height);

	// Clears the accumulated sum and sample count; keeps the buffer allocated
	// at its current resize()'d dimensions.
	void reset();

	// Adds one renderPass() result (width*height radiance samples) into the
	// running sum. sampleRadiance must match the current width()/height().
	// primaryHitMask, if provided (see CpuPathTracer::renderPass), must also
	// match size - per-pixel primary coverage is tracked alongside the
	// radiance sum so callers can tell "this pixel's primary ray has never hit
	// geometry" (pure background) apart from "this pixel is on a silhouette
	// edge or shadow catcher and only partially covers the raster backdrop
	// across AA-jittered samples). primaryAlbedo/
	// primaryNormal, if provided, are accumulated the same way (running sum,
	// divided by sampleCount at resolve time) - see
	// CpuPathTracer::renderPass()'s outPrimaryAlbedo/outPrimaryNormal for
	// what these represent (OIDN denoiser guide buffers, RtDenoiser::
	// denoise()).
	void accumulate(const std::vector<glm::vec3>& sampleRadiance,
		const std::vector<float>* primaryHitMask = nullptr,
		const std::vector<glm::vec3>* primaryAlbedo = nullptr,
		const std::vector<glm::vec3>* primaryNormal = nullptr);

	// Current running average (sum / sampleCount), or an all-zero buffer if
	// no samples have been accumulated yet.
	std::vector<glm::vec3> resolve() const;

	// Running average of the accumulated primary-hit albedo/normal guide
	// buffers (see accumulate()'s primaryAlbedo/primaryNormal), or an
	// all-zero buffer if accumulate() was never called with them.
	std::vector<glm::vec3> resolveAlbedo() const;
	std::vector<glm::vec3> resolveNormal() const;

	// Per-pixel accumulated primary coverage/alpha (see accumulate()'s
	// primaryHitMask). Empty if accumulate() was never called with a mask.
	const std::vector<float>& hitCounts() const { return _hitCounts; }

	int width() const { return _width; }
	int height() const { return _height; }
	uint32_t sampleCount() const { return _sampleCount; }

private:
	int _width  = 0;
	int _height = 0;
	std::vector<glm::vec3> _sum;
	std::vector<glm::vec3> _albedoSum;
	std::vector<glm::vec3> _normalSum;
	std::vector<float> _hitCounts;
	uint32_t _sampleCount = 0;
};
