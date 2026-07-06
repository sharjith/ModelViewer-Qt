#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "RtSceneSnapshot.h"

class RtEmbreeScene;

// ---------------------------------------------------------------------------
// CpuPathTracer
//
// Renders one sample-per-pixel pass of the scene: unidirectional path tracing
// with next-event-estimation direct lighting (against snapshot.lights, using
// the exact glTF KHR_lights_punctual range/spot attenuation formula ported
// from evaluatePunctualLight() in main_scene.frag) plus BSDF-sampled indirect
// bounces, terminated by Russian roulette. Diffuse + metallic-roughness GGX
// only for v1 (see RtMaterial) - the Cook-Torrance D/G/F terms are ported
// from the same shader's distributionGGX()/geometrySmith()/fresnelSchlick()
// so path-traced and raster PBR shading agree on a material at rest.
//
// Repeated calls to renderPass() with different sampleSeed values produce
// statistically independent noisy estimates of the same image - averaging
// many passes into a stable image is RtFrameAccumulator's job (task 5), not
// this class's.
//
// Environment/IBL miss shading samples the scene's actual environment
// cubemap, read back face-by-face from the GPU texture the raster skybox/
// reflections themselves sample (SceneRenderController::
// captureEnvironmentCubemapCPU() - see RtSceneSnapshot.h's RtEnvironment),
// via the standard OpenGL cubemap direction->face+uv convention. Falls back
// to a flat two-tone gradient when no environment map is loaded. Importance
// sampling the environment for faster diffuse-under-IBL convergence (rather
// than uniform BSDF-driven sampling alone) is flagged follow-up work, not
// done here.
// ---------------------------------------------------------------------------
class CpuPathTracer
{
public:
	struct Settings
	{
		int maxBounces                = 6;
		int russianRouletteStartDepth = 3;

		// Diagnostic: when true, the first hit's UV0 is output directly as
		// color (R=U, G=V, B=0), bypassing all texture sampling/shading/
		// lighting entirely. Makes any flip/rotation/axis-swap in the UV
		// pipeline immediately and unambiguously visible - a smooth diagonal
		// gradient (red increasing right, green increasing... whichever
		// direction is correct for the mesh) confirms UV extraction/
		// interpolation itself is right, independent of texture sampling,
		// wrap modes, sRGB decode, or lighting. Not wired to any UI - flip
		// manually for debugging, then set back to false.
		bool debugVisualizeUV = false;
	};

	void setSettings(const Settings& s) { _settings = s; }
	const Settings& settings() const { return _settings; }

	// Renders width*height radiance samples (linear HDR, un-tonemapped) into
	// outRadiance, parallelized across worker threads sized off
	// hardware_concurrency() (mirrors the worker-pool pattern already used by
	// xatlas.cpp for UV unwrapping). sampleSeed should differ between calls so
	// successive passes are statistically independent.
	//
	// cancelFlag, if non-null, is checked once per scanline by every worker
	// thread so a pass can be aborted quickly (within about one row's worth of
	// tracing time) rather than only between whole passes - this is what lets
	// RtPathTracingSession::stop() return promptly when interaction resumes
	// and the mode needs to fall back to raster immediately. On cancellation
	// outRadiance is only partially filled and must be discarded by the
	// caller, not accumulated.
	// outPrimaryHitMask, if non-null, is resized to width*height and filled
	// with 1 where the primary (camera) ray hit scene geometry and 0 where it
	// escaped straight to the environment/background - see
	// RtFrameAccumulator's hit-count tracking for why this matters (OIDN's
	// beauty-only denoise pass has no guide buffers to tell it a smooth
	// environment texture isn't noise, and over-blurs it - background pixels
	// are composited from the raw, undenoised accumulation instead).
	void renderPass(
		const RtEmbreeScene& scene,
		const RtSceneSnapshot& snapshot,
		int width, int height,
		uint32_t sampleSeed,
		std::vector<glm::vec3>& outRadiance,
		const std::atomic<bool>* cancelFlag = nullptr,
		std::vector<uint8_t>* outPrimaryHitMask = nullptr) const;

private:
	Settings _settings;
};
