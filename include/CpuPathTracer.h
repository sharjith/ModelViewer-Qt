#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "RtSceneSnapshot.h"
#include "RtEnvironmentSampler.h"

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
// to a flat two-tone gradient when no environment map is loaded.
//
// Environment next-event-estimation: RtEnvironmentSampler importance-samples
// the environment cubemap directly (luminance-weighted), MIS-combined via
// the balance heuristic with the existing BSDF-sampled bounces that escape
// to the environment - see tracePixel()'s environment-NEE block and the
// symmetric weighting applied at the escape-to-environment miss site. Lets
// small/bright HDRI features (a sun disk, a window) converge in far fewer
// samples than relying on BSDF bounces alone to stumble into them.
// ---------------------------------------------------------------------------
class CpuPathTracer
{
public:
	struct Settings
	{
		int maxBounces                = 6;
		int russianRouletteStartDepth = 3;

		// A separate, much larger budget for consecutive KHR_materials_
		// transmission reflect/refract/TIR events specifically (tracked
		// independently of maxBounces - see tracePixel()'s transmissionDepth
		// counter). High-IOR ("diamond-like", ior significantly above ~2)
		// dielectrics have a narrow total-internal-reflection escape cone
		// (critical angle = asin(1/ior), e.g. only ~24 degrees at ior=2.42
		// vs ~42 degrees for ordinary ior=1.5 glass), so a light path can
		// need many internal bounces before it finds an exit angle inside
		// that cone - a low shared maxBounces budget (6 by default, sized
		// for ordinary opaque diffuse/specular scenes) was causing these
		// paths to terminate before ever escaping, showing as implausibly
		// dim/undistorted transmission instead of the sphere's true
		// (strongly bent) refracted view. Each transmission bounce is cheap
		// (a Fresnel check + refract, no NEE/BRDF evaluation), so a
		// generous cap here doesn't meaningfully slow down scenes that
		// don't have high-IOR transmissive materials at all.
		int maxTransmissionBounces = 32;

		// Firefly/outlier suppression - caps a single sample's maximum
		// radiance channel before it's returned for accumulation, scaling
		// the whole RGB down proportionally (not clamping per-channel) so
		// hue isn't shifted. Standard technique for suppressing rare
		// extreme-value samples (compounding low-probability Fresnel/TIR
		// events can produce them) that would otherwise show as isolated
		// bright/colorful noise blotches - DS's own UI exposes an
		// equivalent "Clamp Threshold" (seen set to 3.0 in their default
		// setup), which this value matches for a direct comparison.
		float fireflyClampThreshold = 3.0f;

		// Environment-map next-event-estimation/MIS (RtEnvironmentSampler,
		// see CpuPathTracer.h's class comment) - a comparison/debug toggle
		// more than a quality knob most users will tune: turning it off
		// falls back to pre-MIS behavior (environment light only found via
		// BSDF-sampled bounces stumbling into it), useful for verifying how
		// much the feature is actually helping on a given scene.
		bool enableEnvironmentImportanceSampling = true;

		// Caps how many consecutive transmissive/masked surfaces a single
		// shadow ray walks through (traceShadowRay()'s bounded loop) before
		// giving up and treating the light as occluded - matters for
		// physically-plausible tinted shadows through several stacked panes
		// of glass. GPU (OptiX) has no equivalent cap - its any-hit shader
		// walks transmissive hits within a single optixTrace() call rather
		// than a bounded host-side loop, and adding a matching limit there
		// would mean spending one of the pipeline's fully-allocated 20
		// payload registers, so this is CPU-only for now (see
		// PathTracingDialog's Advanced tab, disabled while the GPU engine is
		// active).
		int maxShadowRayHits = 8;

		// KHR_materials_volume_scatter's free-flight random walk (see
		// tracePixel()'s hitBackface/hasVolumeScattering gate) tracks its own,
		// separate scatter-event budget - like maxTransmissionBounces above,
		// this never eats into the ordinary maxBounces budget. Runs entirely
		// free (no Russian roulette) up to this count, then RR kicks in -
		// matches NVIDIA's vk_gltf_renderer reference (VOLUME_FREE_BUDGET).
		// Lower this to trade SSS quality for speed on volume-scatter-heavy
		// scenes (e.g. ScatteringSkull.gltf) without touching maxBounces.
		int maxVolumeScatterBounces = 64;

		// Debug-visualization toggles used to live here as Settings fields,
		// but flipping a bool in a widely-included header like this one
		// forced a full rebuild of every translation unit that includes
		// CpuPathTracer.h (~34 files) just to change a diagnostic-only
		// value nothing outside CpuPathTracer.cpp ever reads or sets. They
		// are now file-local constants at the top of CpuPathTracer.cpp
		// instead (kDebugVisualizeUV/kDebugVisualizeTransmission/
		// kDebugVisualizeTransmissionBounceCount) - flip those, only
		// CpuPathTracer.cpp rebuilds.
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
	// with the primary ray's effective coverage/alpha for this sample
	// (normally 1 for a real opaque geometry hit, 0 for a pure miss, and a
	// fractional value for path-traced shadow-catcher compositing) - see
	// RtFrameAccumulator's hit-count tracking for why this matters (OIDN's
	// beauty-only denoise pass has no guide buffers to tell it a smooth
	// environment texture isn't noise, and over-blurs it - background pixels
	// are composited from the raw, undenoised accumulation instead).
	//
	// outPrimaryAlbedo/outPrimaryNormal, if non-null, are resized to
	// width*height and filled with the primary hit's base color / shading
	// normal - OIDN denoiser guide buffers (see RtDenoiser::denoise()). A
	// transmissive primary hit (e.g. glass) can't use its own material
	// properties as a guide - a window's own flat tint/normal is unrelated to
	// what's actually visible through it - so instead it peeks through the
	// transmissive surface (an undeviated straight-through ray, the same
	// thin-wall approximation already used elsewhere in this tracer) to find
	// the real surface behind it and uses THAT surface's guide values -
	// see tracePixel()'s findGuideSurfaceThroughTransmission() use site.
	void renderPass(
		const RtEmbreeScene& scene,
		const RtSceneSnapshot& snapshot,
		const RtEnvironmentSampler& envSampler,
		int width, int height,
		uint32_t sampleSeed,
		std::vector<glm::vec3>& outRadiance,
		const std::atomic<bool>* cancelFlag = nullptr,
		std::vector<float>* outPrimaryHitMask = nullptr,
		std::vector<glm::vec3>* outPrimaryAlbedo = nullptr,
		std::vector<glm::vec3>* outPrimaryNormal = nullptr) const;

private:
	Settings _settings;
};
