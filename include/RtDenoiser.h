#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

// User-facing denoiser device choice (Visualization/PT settings dropdown):
//   Auto  - try native OptiX first (generally the best quality/performance on
//           an RTX GPU, and independent of which render engine actually
//           produced the frame - see OptiX's own entry below), then OIDN
//           CUDA, then OIDN CPU, then finally the built-in bilateral filter
//           if none of those initialize. The permissive default.
//   CPU   - skip CUDA/OptiX entirely and use the OIDN CPU device (falls back
//           to bilateral if even that fails to initialize).
//   GPU   - only attempt OIDN CUDA; if it's unavailable, falls straight to
//           the bilateral filter WITHOUT trying CPU or OptiX. Deliberately
//           does not silently substitute another device here - a user who
//           explicitly asked for GPU denoising should get an unambiguous
//           signal (the existing "OIDN device unavailable" log, plus a
//           visibly different activeDeviceName()) that GPU denoising
//           specifically isn't working, rather than transparently getting
//           something else instead and never finding out.
//   OptiX - NVIDIA's own AI denoiser (optixDenoiserInvoke() - a genuinely
//           different model/API from Intel's OIDN above, not just another
//           OIDN device type). RtDenoiser owns its own standalone
//           OptixDeviceContext (see Impl::optixContext's doc comment in
//           RtDenoiser.cpp), so unlike GPU's OIDN-CUDA option this works
//           regardless of which render engine (CPU Embree or GPU OptiX)
//           actually produced the frame being denoised - it only needs an
//           NVIDIA GPU with OptiX support to be present. Falls back to the
//           bilateral filter (never silently to OIDN) if that's not the
//           case, or this build has no OptiX SDK at all, matching GPU's own
//           explicit-choice-deserves-an-unambiguous-signal philosophy above.
enum class DenoiserDevicePreference
{
	Auto,
	CPU,
	GPU,
	OptiX
};

// ---------------------------------------------------------------------------
// RtDenoiser
//
// Wraps an Intel Open Image Denoise "RT" filter. Device selection is driven
// by DenoiserDevicePreference (see above) - CUDA runs on an NVIDIA GPU with a
// working driver (see the vcpkg overlay port comment in overlay-ports/
// openimagedenoise for why this needs no CUDA Toolkit at all, only the
// driver); the built-in bilateral filter is the last-resort fallback if
// OIDN itself fails to initialize on whichever device(s) the preference
// allows (see denoise()'s doc comment).
//
// CUDA is a *denoising* speedup only - this app's actual ray tracing is
// still CPU Embree; OIDN's device choice is independent of that.
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
	explicit RtDenoiser(DenoiserDevicePreference preference = DenoiserDevicePreference::Auto);
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
	// already averaged into input) mildly tapers the fallback filter's
	// strength: more accumulated samples mean less residual Monte Carlo noise
	// to clean up, so a well-converged image needs a lighter touch than a
	// noisy one - but never zero, since RtPathTracingSession only calls this
	// once, on the final pass (see its maxSamples()), not every intermediate
	// pass, so this is the only denoise opportunity that pass gets. Only
	// affects the fallback path - ignored when OIDN itself is used.
	//
	// albedo/normal, if both non-null, are OIDN guide (auxiliary feature)
	// buffers - RtFrameAccumulator::resolveAlbedo()/resolveNormal(), which
	// come from CpuPathTracer's primary-hit base color/shading normal (see
	// its outPrimaryAlbedo/outPrimaryNormal doc comment). Per OIDN's own
	// documentation, running "beauty-only" (neither buffer supplied, this
	// class's previous default) "does not provide any benefits for
	// reflections and objects visible through transparent surfaces" - a
	// transmissive material's own base color/normal are deliberately zeroed
	// out at the source (CpuPathTracer) rather than fed in, matching OIDN's
	// documented guidance to treat such pixels as pass-through rather than
	// anchoring the denoiser to the glass's own flat tint. Ignored by the
	// bilateral fallback path (only OIDN itself uses guide buffers).
	bool denoise(const std::vector<glm::vec3>& input, int width, int height,
		std::vector<glm::vec3>& output, uint32_t sampleCount = 1,
		const std::vector<glm::vec3>* albedo = nullptr,
		const std::vector<glm::vec3>* normal = nullptr);

	// Human-readable name of whichever OIDN device actually initialized
	// ("CUDA", "CPU", or "none (bilateral fallback)") - for diagnostics/UI,
	// not used internally.
	const char* activeDeviceName() const;

	// Device-resident counterpart of denoise() for InteractivePtRenderer's
	// continuous accumulator - operates entirely on existing CUDA device
	// pointers (the SAME persistent buffers that renderer already accumulates
	// into), with no host round-trip at all, unlike denoise()'s std::vector
	// API. dColorRGBA/dOutputRGBA are float4 (RGBA, matching
	// RtOptixSceneParams::image's layout) - alpha (the primary-hit
	// compositing mask, not real geometric opacity) is copied through
	// unmodified rather than denoised. dAlbedo/dNormal are float3 guide
	// buffers, both required together or both null (matching denoise()'s own
	// albedo/normal contract). Writes the denoised result into dOutputRGBA, a
	// SEPARATE buffer from dColorRGBA - the caller's own raw accumulation
	// keeps refining underneath, exactly like RtPathTracingSession's "denoise
	// a copy" pattern, just invoked periodically instead of once at the end.
	//
	// Only implemented for the native OptiX denoiser - the interactive GPU-PT
	// accumulator only exists at all when OptiX is available, so there is no
	// OIDN-CUDA/CPU/bilateral fallback tier here (unlike denoise()). Returns
	// false (leaving dOutputRGBA untouched) if this instance isn't using the
	// native OptiX denoiser - callers should keep presenting the raw
	// accumulation in that case, not treat this as blocking presentation.
	bool denoiseDevice(void* dColorRGBA, void* dAlbedo, void* dNormal, int width, int height,
		void* dOutputRGBA, uint32_t sampleCount = 1);

	// Re-runs device selection under a new preference (see
	// DenoiserDevicePreference's doc comment) - releases whichever OIDN
	// device is currently held (if any) and tries again from scratch. Safe
	// to call at any time, including mid-session (e.g. the user changes the
	// setting while Path Tracing is already running) - denoise() always
	// reads the current device state fresh on its next call, there's no
	// torn-state risk.
	void setDevicePreference(DenoiserDevicePreference preference);
	DenoiserDevicePreference devicePreference() const;

private:
	void initializeDevice();

	// Only ever called (from denoise()) when initializeDevice() successfully
	// set up a native OptiX denoiser context - defined (as a real OptiX
	// create/setup/invoke sequence) only when this build has the OptiX SDK;
	// a private member function rather than a free function in RtDenoiser.cpp
	// so it can reach the private Impl type without exposing any OptiX types
	// in this header.
	bool denoiseWithOptix(const std::vector<glm::vec3>& input, int width, int height,
		std::vector<glm::vec3>& output,
		const std::vector<glm::vec3>* albedo, const std::vector<glm::vec3>* normal);

	// Device-resident counterpart of denoiseWithOptix() - see denoiseDevice()'s
	// doc comment. Same "only defined/called under MODELVIEWER_HAVE_OPTIX"
	// contract as denoiseWithOptix() above.
	bool denoiseDeviceWithOptix(void* dColorRGBA, void* dAlbedo, void* dNormal, int width, int height,
		void* dOutputRGBA);

	struct Impl;
	std::unique_ptr<Impl> _impl;
};
