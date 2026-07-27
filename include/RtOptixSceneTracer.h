#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "RtSceneSnapshot.h"

// ---------------------------------------------------------------------------
// RtOptixSceneTracer
//
// GPU (OptiX) path tracer backend - the RtEmbreeScene/CpuPathTracer
// counterpart, now at full material/lighting parity with the CPU engine
// (see RtOptixSceneParams.h's doc comment for the complete feature list:
// metallic-roughness Cook-Torrance + all supported KHR material extensions,
// shadow rays, environment-light NEE/MIS, mip-mapped textures, etc.). Builds
// a real two-level acceleration structure (GAS per RtMeshGeometry, IAS with
// one OptixInstance per RtInstance) mirroring RtEmbreeScene's BLAS/TLAS
// structure, plus uploads/caches every material texture - see buildScene()'s
// own doc comment for what does and doesn't persist across rebuilds.
//
// Deliberately self-contained - owns its own CUDA/OptiX device context
// rather than sharing one with any other GPU-facing piece in this codebase
// (RtDenoiser, InteractivePtRenderer's own tracer instance, etc. all do the
// same) - see RtDenoiser.cpp's Impl::optixContext doc comment for the same
// reasoning spelled out in more depth.
//
// Compiles to an inert stub when built without the CUDA/OptiX toolchain.
// ---------------------------------------------------------------------------
class RtOptixSceneTracer
{
public:
	RtOptixSceneTracer();
	~RtOptixSceneTracer();

	RtOptixSceneTracer(const RtOptixSceneTracer&) = delete;
	RtOptixSceneTracer& operator=(const RtOptixSceneTracer&) = delete;

	bool isAvailable() const;

	// Diagnostics tab accessors (PathTracingDialog) - all read already-
	// computed state with no per-call GPU work, so polling them at UI refresh
	// rate is free. hasHardwareRT()/deviceName() are fixed once isAvailable()
	// is true (set in the constructor); the rest reflect the most recent
	// successful buildScene() call and stay 0/empty until one has happened.
	// See RtOptixSceneTracer.cpp's Impl fields for what each measures.
	bool hasHardwareRT() const;
	const char* deviceName() const;
	double lastGasBuildMs() const;
	double lastIasBuildMs() const;
	uint64_t lastTriangleCount() const;

	// (Re)builds the GPU acceleration structure from this snapshot's
	// meshes/instances - mirrors RtEmbreeScene::build()'s contract. Callers
	// (InteractivePtRenderer::ensureSceneResources(), RtOptixPathTracingSession::
	// start()) already skip calling this at all when the snapshot's revisionId
	// hasn't changed. Every ACTUAL call still walks every mesh/instance, but
	// three pieces persist across calls independently rather than being
	// unconditionally rebuilt from scratch each time: material TEXTURES (see
	// Impl::persistentTextureCache's doc comment), mesh GAS (see
	// Impl::persistentGasCache's doc comment - a mesh whose content hash
	// hasn't changed since the last build reuses its already-built GAS with
	// no new device work; one whose vertex/index COUNT is unchanged but
	// whose content hash moved - a skinned/morphed pose update - REFITS its
	// existing GAS in place, OPTIX_BUILD_OPERATION_UPDATE, instead of a full
	// rebuild), and the IAS itself, which refits in place the same way
	// whenever its TOPOLOGY - instance count and each instance's GAS
	// assignment - hasn't changed (see the GAS/IAS sections' own doc
	// comments in the .cpp). A material-only edit or a single object's
	// transform move in an otherwise unchanged scene now pays close to none
	// of this function's cost, and actively playing a skinned/morphed
	// animation pays only a refit per animating mesh per frame rather than a
	// full GAS rebuild.
	// Returns false if OptiX isn't available or the build failed.
	bool buildScene(const RtSceneSnapshot& snapshot);

	// Renders the current scene (from the last successful buildScene() call)
	// through the given camera at the given resolution, averaging
	// samplesPerPixel jittered primary-ray path samples (box-filter AA
	// jitter, each path up to maxBounces long - see RtOptixSceneParams.h's
	// samplesPerPixel/maxBounces doc comments) within a single optixLaunch().
	// environment supplies the CHEAP per-launch scalars (showBackground/
	// exposure/rotation/fallback colors...) fresh from the caller's current
	// snapshot each call - only the heavy face/mip texel data comes from the
	// revision-gated buildScene() upload, so lightweight environment-setting
	// changes take effect without a scene-revision bump (its faces/
	// irradiance/prefilter pixel data members are ignored here). Output is
	// linear HDR radiance (un-tonemapped) - same contract as
	// CpuPathTracer's own frame output - so callers wanting progressive
	// multi-launch accumulation (see RtOptixPathTracingSession) can average
	// multiple calls' results in linear space before a single final tonemap.
	// outAlbedo/outNormal are OIDN guide (auxiliary feature) buffers -
	// primary-hit base color/world-space shading normal, chunk-averaged the
	// same way the beauty image is - mirroring RtFrameAccumulator::
	// resolveAlbedo()/resolveNormal()'s contract, see RtOptixSceneParams.h's
	// albedoImage/normalImage doc comment. outAlpha is the per-pixel
	// primary-hit fraction (RtPresenter's alpha-composited-background
	// channel - see RtOptixSceneParams::image's doc comment, .w component). On
	// success, resizes all four to width*height and returns true.
	// enableEnvironmentImportanceSampling mirrors CpuPathTracer::Settings::
	// enableEnvironmentImportanceSampling - additionally gated on the
	// uploaded environment-sampler distribution actually being valid (see
	// RtOptixSceneParams::RtOptixEnvironment::envTotalWeight's doc comment),
	// same as CPU's own envSampler.isValid() check.
	// maxTransmissionBounces/fireflyClampThreshold/russianRouletteStartDepth/
	// maxVolumeScatterBounces mirror CpuPathTracer::Settings' identically-
	// named fields exactly - see RtOptixSceneParams.h's doc comment on the
	// fields these populate.
	bool renderScene(const RtCamera& camera, const RtEnvironment& environment,
		int width, int height, unsigned int samplesPerPixel, unsigned int sampleOffset,
		unsigned int maxBounces, bool shadowsEnabled, bool selfShadowsEnabled, bool enableEnvironmentImportanceSampling,
		unsigned int maxTransmissionBounces, float fireflyClampThreshold, unsigned int russianRouletteStartDepth,
		unsigned int maxVolumeScatterBounces,
		std::vector<glm::vec3>& outImageLinearRgb, std::vector<glm::vec3>& outAlbedo, std::vector<glm::vec3>& outNormal,
		std::vector<float>& outAlpha);

	// GPU-resident variant for the interactive/CUDA-GL-interop path (see
	// RtOptixPathTracingSession's persistent device buffer) - same rendering
	// as renderScene() above (same camera/environment/quality-setting
	// semantics), but writes the combined linear-HDR RGBA result (see
	// RtOptixSceneParams::image's doc comment - .rgb beauty, .w alpha)
	// directly into deviceImageRGBA, a caller-owned CUDA device pointer
	// already sized width*height*sizeof(float4)/16 bytes-per-pixel, instead
	// of allocating/reading back its own buffer. This is what lets the
	// interactive path skip the device-to-host readback that dominates
	// renderScene()'s per-chunk latency. Albedo/normal OIDN guide buffers are
	// still computed internally (the kernel writes them unconditionally) but
	// discarded (scratch device memory, freed before returning, never read
	// back) - they're unused on the interactive path since its denoiser is
	// off. Returns false (deviceImageRGBA left untouched) if the scene isn't
	// built yet or the launch fails.
	bool renderSceneToDevice(const RtCamera& camera, const RtEnvironment& environment,
		int width, int height, unsigned int samplesPerPixel, unsigned int sampleOffset,
		unsigned int maxBounces, bool shadowsEnabled, bool selfShadowsEnabled, bool enableEnvironmentImportanceSampling,
		unsigned int maxTransmissionBounces, float fireflyClampThreshold, unsigned int russianRouletteStartDepth,
		unsigned int maxVolumeScatterBounces,
		void* deviceImageRGBA);

	// Allocates/frees a CUDA device buffer sized width*height*sizeof(float4)
	// (16 bytes/pixel), suitable for renderSceneToDevice()'s deviceImageRGBA
	// parameter. Exists so RtOptixPathTracingSession (its persistent
	// ping-pong interactive-frame buffers) never has to include a CUDA
	// header or link against the CUDA runtime itself - this class remains
	// the sole CUDA/OptiX-aware boundary, same role its stub/real split
	// already plays for every other member here. allocateDeviceRGBABuffer()
	// returns nullptr on allocation failure or when built without CUDA/
	// OptiX (stub); freeDeviceRGBABuffer() is a no-op on nullptr.
	void* allocateDeviceRGBABuffer(int width, int height) const;
	void freeDeviceRGBABuffer(void* devicePtr) const;

	// One-off CPU readback of a buffer previously filled by
	// renderSceneToDevice()/allocateDeviceRGBABuffer() - NOT part of the
	// normal interactive/GPU-resident presentation path (see
	// RtPresenter::uploadFromDevice(), which is the fast path meant to run
	// every frame), only its fallback: if CUDA-GL interop registration ever
	// fails there (unusual, but possible on some driver states), this lets
	// ViewportWidget still present that same already-rendered device frame
	// via the ordinary host-vector RtPresenter::upload() rather than showing
	// nothing. Unpacks the combined RGBA buffer into separate RGB/alpha
	// vectors exactly like renderScene()'s own readback does. Returns false
	// (vectors left untouched) on any CUDA failure or when built without
	// CUDA/OptiX (stub).
	bool readbackDeviceRGBABuffer(void* devicePtr, int width, int height,
		std::vector<glm::vec3>& outImageLinearRgb, std::vector<float>& outAlpha) const;

	// -----------------------------------------------------------------------
	// Non-blocking interactive submission API (InteractivePtRenderer's Phase A
	// groundwork - see that class's own doc comment for the full design this
	// serves). Everything below exists so InteractivePtRenderer never has to
	// include a CUDA header itself (same "sole CUDA/OptiX-aware boundary"
	// role this class already plays above) and never has to block the
	// calling thread waiting for GPU completion - unlike renderScene()/
	// renderSceneToDevice(), which both end in cudaDeviceSynchronize() and
	// are fine for that only because they run exclusively on
	// RtOptixPathTracingSession's background worker thread, never the UI
	// thread. submitSceneRenderToDevice() is the UI-thread-safe counterpart.
	// -----------------------------------------------------------------------

	// Creates a dedicated CUDA stream for a caller to submit interactive
	// launches on, deliberately separate from the default/null stream every
	// other call in this class implicitly uses today (renderScene()/
	// renderSceneToDevice() both pass nullptr to optixLaunch()) - keeps this
	// stream's own completion events unambiguous instead of entangled with
	// unrelated CUDA work ordered on the default stream. Returns nullptr on
	// failure or when built without CUDA/OptiX (stub).
	void* createStream() const;
	// Destroys a stream returned by createStream(). No-op on nullptr. Caller
	// must ensure no submission on this stream is still in flight (see
	// isEventComplete()) before calling this.
	void destroyStream(void* stream) const;

	// Creates/destroys a CUDA event for tracking one in-flight launch's
	// completion (see submitSceneRenderToDevice()). Returns nullptr on
	// failure/stub. destroyEvent() is a no-op on nullptr.
	void* createEvent() const;
	void destroyEvent(void* event) const;

	// Non-blocking: true once the launch that recorded this event has
	// actually finished on the device; false if still in flight (or on
	// error/stub). This is the ONLY way callers should check completion -
	// do NOT call any CUDA-GL interop function (e.g. RtPresenter::
	// uploadFromDevice()'s cudaGraphicsMapResources()) speculatively to
	// "find out" instead: interop mapping performs its own implicit stream
	// sync and will stall exactly like cudaDeviceSynchronize() did if the
	// launch isn't actually done, silently reintroducing the UI-thread
	// block this whole API exists to avoid. Only attempt the interop copy
	// once this returns true for the event that submission recorded.
	bool isEventComplete(void* event) const;

	// Elapsed GPU time in milliseconds between two events previously recorded
	// on the SAME stream, both already complete (see isEventComplete()) -
	// wraps cudaEventElapsedTime(). Exists for InteractivePtRenderer's Phase D
	// adaptive-budget logic: measuring actual GPU compute time per launch
	// (via a start event recorded just before optixLaunch() and the existing
	// completion event recorded just after - see submitSceneRenderToDevice()'s
	// optional startEvent parameter) rather than CPU-side wall-clock time,
	// which would also include paintGL()'s own polling cadence and isn't a
	// clean measure of the launch itself. Returns a negative value on
	// failure/stub or if either event hasn't completed yet - callers should
	// treat any negative result as "no measurement available this round"
	// rather than a real duration.
	float elapsedEventTimeMs(void* startEvent, void* endEvent) const;

	// Allocates the scratch buffers submitSceneRenderToDevice() needs beyond
	// the RGBA output (already covered by allocateDeviceRGBABuffer()) -
	// exists so callers don't need to know RtOptixSceneParams' size or the
	// guide buffers' element type, same reasoning as
	// allocateDeviceRGBABuffer(). Returns nullptr on failure/stub.
	void* allocateParamsScratchBuffer() const;                      // sizeof(RtOptixSceneParams)
	void* allocateGuideScratchBuffer(int width, int height) const;  // width*height float3 - used for both albedo and normal
	// Generic device-memory free, shared by every allocate*() method above
	// (and usable for allocateDeviceRGBABuffer()'s pointer too, though that
	// one also has its own freeDeviceRGBABuffer() name for callers that
	// don't otherwise touch this section). No-op on nullptr.
	void freeScratchBuffer(void* devicePtr) const;

	// Device-to-device "copy forward" primitives for InteractivePtRenderer's
	// two-slot ping-pong accumulator: before submitting a new launch into
	// whichever slot ISN'T currently displayed, the caller copies the OTHER
	// slot's already-accumulated raw buffers into it first, so both slots
	// always represent progressive snapshots of ONE shared, growing
	// accumulation history rather than two independent ones (see
	// InteractivePtRenderer::tick()'s doc comment for why this matters - two
	// slots blending samples independently, alternately displayed, was
	// visually indistinguishable from persistent noise even once each
	// individually reached a high sample count). Same size formulas as
	// allocateDeviceRGBABuffer()/allocateGuideScratchBuffer() respectively.
	// Issued on `stream` (the caller's dedicated interactive stream) - CUDA's
	// stream-ordering guarantees this copy completes before a
	// submitSceneRenderToDevice() launch enqueued afterward on the SAME
	// stream reads/writes the destination, no extra synchronization needed
	// (same reasoning submitSceneRenderToDevice()'s own async params upload
	// already relies on). Returns false on failure/stub or bad arguments -
	// callers should treat that as "copy didn't happen, don't submit this
	// round" rather than silently launching against stale/uninitialized
	// destination contents.
	bool copyDeviceRGBABufferAsync(void* dst, const void* src, int width, int height, void* stream) const;
	bool copyGuideScratchBufferAsync(void* dst, const void* src, int width, int height, void* stream) const;

	// Allocates/frees a PINNED (page-locked) HOST buffer sized
	// sizeof(RtOptixSceneParams) - the host-side staging buffer
	// submitSceneRenderToDevice() writes the launch params into before an
	// ASYNC (stream-ordered) upload. Must be pinned, not a plain
	// new/malloc'd or stack buffer: cudaMemcpyAsync() only actually behaves
	// asynchronously (and only guarantees it won't read the source before
	// the copy is scheduled, while still allowing it to read the source
	// after this call returns) when the host side is page-locked - with
	// ordinary pageable host memory, the driver may fall back to
	// synchronizing internally, silently reintroducing the exact stall this
	// whole API exists to avoid. Same lifetime rule as the device scratch
	// buffers: callers must not reuse/overwrite/free this buffer until
	// isEventComplete() confirms the submission that used it has finished.
	// Returns nullptr on failure/stub; freeParamsHostStagingBuffer() (NOT
	// freeScratchBuffer() - pinned host memory needs its own CUDA free call)
	// is a no-op on nullptr.
	void* allocateParamsHostStagingBuffer() const;
	void freeParamsHostStagingBuffer(void* hostPtr) const;

	// Async-submitting counterpart to renderSceneToDevice(): enqueues the
	// launch on `stream` and returns immediately - no cudaDeviceSynchronize()
	// AND no blocking cudaMemcpy() either (see below) - recording
	// `completionEvent` right after the launch so
	// isEventComplete(completionEvent) can be polled later to learn when
	// it's actually safe to read/copy deviceImageRGBA. hostParamsStaging
	// (from allocateParamsHostStagingBuffer()), dParamsScratch,
	// dAlbedoScratch, dNormalScratch, and deviceImageRGBA are ALL
	// caller-owned, persistent buffers - NONE are allocated or freed here
	// (unlike renderSceneToDevice(), which owns dAlbedo/dNormal as its own
	// per-call scratch). The caller MUST NOT reuse, overwrite, or free ANY
	// of them (including hostParamsStaging) until
	// isEventComplete(completionEvent) confirms THIS submission has
	// finished - the expected pattern is double-buffering all five per
	// in-flight "slot" (extends RtOptixPathTracingSession's existing
	// RGBA-only ping-pong to cover every buffer a launch touches, since ALL
	// of them are live for the duration of an async launch now, not just
	// the output). The params struct is written into hostParamsStaging here,
	// then copied to dParamsScratch via cudaMemcpyAsync() on `stream` -
	// deliberately NOT a plain synchronous cudaMemcpy(): that call would use
	// legacy default-stream semantics (implicitly synchronizing against
	// every other stream in the context) even though no explicit
	// cudaDeviceSynchronize() follows it, silently serializing this
	// "non-blocking" submission against unrelated GPU work. `stream` should
	// be a value from createStream() - passing nullptr (default stream) is
	// still legal but reintroduces that same cross-stream-ordering ambiguity.
	// Returns false only for an immediately-known submission failure (scene
	// not built yet, bad arguments) - a launch that starts fine but later
	// crashes on-device is effectively unrecoverable regardless of how it's
	// detected, same as today's synchronous calls. startEvent (optional,
	// nullable - pass nullptr to skip) is recorded on `stream` immediately
	// before optixLaunch(), so callers wanting Phase D's per-launch GPU
	// timing can later call elapsedEventTimeMs(startEvent, completionEvent)
	// once both have completed. previousSampleCount (0 = fresh start) lets a
	// caller doing persistent GPU-resident accumulation across many small
	// launches (see InteractivePtRenderer) have the kernel blend this
	// launch's own samples into whatever's ALREADY in deviceImageRGBA/
	// dAlbedoScratch/dNormalScratch instead of overwriting them - see
	// RtOptixSceneParams::previousSampleCount's own doc comment.
	bool submitSceneRenderToDevice(const RtCamera& camera, const RtEnvironment& environment,
		int width, int height, unsigned int samplesPerPixel, unsigned int sampleOffset,
		unsigned int maxBounces, bool shadowsEnabled, bool selfShadowsEnabled, bool enableEnvironmentImportanceSampling,
		unsigned int maxTransmissionBounces, float fireflyClampThreshold, unsigned int russianRouletteStartDepth,
		unsigned int maxVolumeScatterBounces, unsigned int previousSampleCount,
		void* deviceImageRGBA, void* hostParamsStaging, void* dParamsScratch, void* dAlbedoScratch, void* dNormalScratch,
		void* stream, void* startEvent, void* completionEvent);

private:
	struct Impl;
	std::unique_ptr<Impl> _impl;

	// Shared by renderScene()/renderSceneToDevice() - builds the launch
	// params from the built scene + the caller's camera/environment/quality
	// settings, uploads them, and runs one optixLaunch(). dImageRGBA/
	// dAlbedo/dNormal are caller-owned CUDA device pointers (void* here so
	// this declaration doesn't need CUDA vector types in a header that must
	// also compile in the no-CUDA/OptiX stub build; the .cpp casts them to
	// float4*/float3* internally) already sized width*height for their
	// respective element types; this function only reads/writes through the
	// pointers it's given and does not free them.
	bool launchOptixSceneRender(const RtCamera& camera, const RtEnvironment& environment,
		int width, int height, unsigned int samplesPerPixel, unsigned int sampleOffset,
		unsigned int maxBounces, bool shadowsEnabled, bool selfShadowsEnabled, bool enableEnvironmentImportanceSampling,
		unsigned int maxTransmissionBounces, float fireflyClampThreshold, unsigned int russianRouletteStartDepth,
		unsigned int maxVolumeScatterBounces,
		void* dImageRGBA, void* dAlbedo, void* dNormal);

	// Fills *paramsOut (a caller-owned HOST-memory RtOptixSceneParams struct,
	// or a buffer at least sizeof(RtOptixSceneParams) large - see
	// allocateParamsScratchBuffer()'s doc comment for why a SEPARATE
	// caller-owned DEVICE copy exists too) from the built scene + this
	// call's camera/environment/quality settings - the ~70 lines of
	// per-field wiring shared by launchOptixSceneRender() (synchronous) and
	// submitSceneRenderToDevice() (async), factored out so that wiring
	// exists in exactly one place. void* here for the usual stub-build
	// reason (this header must compile without RtOptixSceneParams.h, which
	// isn't stub-safe).
	void buildRenderParamsInto(void* paramsOut, const RtCamera& camera, const RtEnvironment& environment,
		int width, int height, unsigned int samplesPerPixel, unsigned int sampleOffset,
		unsigned int maxBounces, bool shadowsEnabled, bool selfShadowsEnabled, bool enableEnvironmentImportanceSampling,
		unsigned int maxTransmissionBounces, float fireflyClampThreshold, unsigned int russianRouletteStartDepth,
		unsigned int maxVolumeScatterBounces, unsigned int previousSampleCount,
		void* dImageRGBA, void* dAlbedo, void* dNormal) const;
};
