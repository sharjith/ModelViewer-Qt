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
	// match size - per-pixel hit counts are tracked alongside the radiance sum
	// so callers can tell "this pixel's primary ray has never hit geometry"
	// (pure background) apart from "this pixel is on a silhouette edge and
	// flips between hit/miss across AA-jittered samples). primaryAlbedo/
	// primaryNormal, if provided, are accumulated the same way (running sum,
	// divided by sampleCount at resolve time) - see
	// CpuPathTracer::renderPass()'s outPrimaryAlbedo/outPrimaryNormal for
	// what these represent (OIDN denoiser guide buffers, RtDenoiser::
	// denoise()).
	// clearTransmissionMask, if provided, is counted the same way as
	// primaryHitMask - see CpuPathTracer::renderPass()'s
	// outClearTransmissionMask and clearTransmissionCounts() below.
	void accumulate(const std::vector<glm::vec3>& sampleRadiance,
		const std::vector<uint8_t>* primaryHitMask = nullptr,
		const std::vector<glm::vec3>* primaryAlbedo = nullptr,
		const std::vector<glm::vec3>* primaryNormal = nullptr,
		const std::vector<uint8_t>* clearTransmissionMask = nullptr);

	// Current running average (sum / sampleCount), or an all-zero buffer if
	// no samples have been accumulated yet.
	std::vector<glm::vec3> resolve() const;

	// Running average of the accumulated primary-hit albedo/normal guide
	// buffers (see accumulate()'s primaryAlbedo/primaryNormal), or an
	// all-zero buffer if accumulate() was never called with them.
	std::vector<glm::vec3> resolveAlbedo() const;
	std::vector<glm::vec3> resolveNormal() const;

	// Per-pixel count of accumulated samples whose primary ray hit geometry
	// (see accumulate()'s primaryHitMask). Empty if accumulate() was never
	// called with a mask.
	const std::vector<uint32_t>& hitCounts() const { return _hitCounts; }

	// Per-pixel count of accumulated samples whose primary hit was a "clear
	// transmission" surface (see CpuPathTracer::renderPass()'s
	// outClearTransmissionMask doc comment). Empty if accumulate() was never
	// called with a mask.
	const std::vector<uint32_t>& clearTransmissionCounts() const { return _clearTransmissionCounts; }

	int width() const { return _width; }
	int height() const { return _height; }
	uint32_t sampleCount() const { return _sampleCount; }

private:
	int _width  = 0;
	int _height = 0;
	std::vector<glm::vec3> _sum;
	std::vector<glm::vec3> _albedoSum;
	std::vector<glm::vec3> _normalSum;
	std::vector<uint32_t> _hitCounts;
	std::vector<uint32_t> _clearTransmissionCounts;
	uint32_t _sampleCount = 0;
};
