#pragma once

#include <cstdint>
#include <memory>

#include "RtDenoiser.h"
#include "RtSceneSnapshot.h"

class RtOptixSceneTracer;

// ---------------------------------------------------------------------------
// InteractivePtRenderer
//
// A same-frame, GPU-resident, CONTINUOUSLY-ACCUMULATING path tracer for the
// viewport's interactive camera-motion path (mouse/keyboard driven). This is
// the "collapse interactive and settled into one continuous accumulator"
// architecture change - see the branch's design notes for why: the earlier
// (Phase A-D) model submitted a single moderately-good-quality launch per
// camera update and displayed it once complete, which meant the on-screen
// camera pose always lagged the live one by however long that launch took -
// a gap that grows with scene complexity (a bigger BVH means a slower
// launch), visible as lag at the START of a drag and as a "jerk" at the tail
// for anything but a trivial scene. This class instead does a small, FIXED
// amount of GPU work every tick (see _samplesPerLaunch), blended into a
// PERSISTENT GPU-resident accumulation buffer that keeps integrating samples
// for as long as the camera holds still and resets the moment it doesn't -
// same model NVIDIA's own reference renderer (vk_gltf_renderer) uses.
// Bounding per-launch cost this way, rather than adapting sample count to
// "fit" a frame budget, is what keeps the displayed camera pose in sync with
// the live one regardless of scene size; see _resolutionScale for what
// happens instead when even the fixed sample count doesn't fit.
//
// Owned directly by ViewportWidget and driven ENTIRELY from its render flow -
// ensureSceneResources()/updateCamera()/tick()/pollCompletedFrame() are all
// expected to be called from paintGL(), on the GL/UI thread. There is
// deliberately no background worker thread here (unlike
// RtOptixPathTracingSession::workerLoop()): tick() submits at most one async
// launch per call via RtOptixSceneTracer::submitSceneRenderToDevice() and
// returns immediately - it never waits for GPU completion, so paintGL() never
// stalls on a whole OptiX launch. Completion is discovered later via
// RtOptixSceneTracer::isEventComplete(), polled once per tick().
//
// Double-buffers (two "slots") EVERY resource a launch touches - not just the
// RGBA output, but also the pinned host params-staging buffer, the uploaded
// RtOptixSceneParams device copy, and the albedo/normal guide scratch buffers
// - so a slot is never reused/freed until its own completion event confirms
// the launch that wrote it has actually finished. Getting this wrong
// (freeing/reusing a buffer an in-flight kernel is still reading/writing) is
// a silent GPU memory-corruption bug, not a cosmetic one - see
// RtOptixSceneTracer::submitSceneRenderToDevice()'s doc comment. Unlike the
// old single-shot model, EACH slot is now itself a persistent accumulator
// (Slot::accumulatedSampleCount) - the two slots alternate turns
// contributing to the SAME conceptual image (see _accumulationCamera), not
// two independent "latest result" buffers.
//
// Denoising: see setDenoiserEnabled() - runs on EVERY published launch, into
// a separate device-resident copy (never gated on a minimum accumulated-
// sample count - the native OptiX denoiser is robust even at 1 spp, and
// there is no "the very end" to wait for anymore anyway, since the camera
// might never fully stop).
// ---------------------------------------------------------------------------
class InteractivePtRenderer
{
public:
	// tracer must outlive this object - not owned. Deliberately a SEPARATE
	// RtOptixSceneTracer instance from the settled GPU PT path's own
	// (_ptOptixSession's tracer) - each keeps its own independent GAS/IAS/
	// texture-cache state (see RtOptixSceneTracer::Impl::persistentTextureCache's
	// doc comment for why sharing one instance across both would be actively
	// wrong: the two sessions are torn down/rebuilt on different triggers and
	// neither should invalidate the other's resident scene). ViewportWidget
	// owns this tracer instance (_interactivePtTracer) and constructs it
	// before this renderer for that reason.
	explicit InteractivePtRenderer(RtOptixSceneTracer& tracer);
	~InteractivePtRenderer(); // drains any in-flight launch before freeing resources - see releaseResources()

	InteractivePtRenderer(const InteractivePtRenderer&) = delete;
	InteractivePtRenderer& operator=(const InteractivePtRenderer&) = delete;

	// Lazily creates the dedicated CUDA stream (first call only), then
	// (re)builds the GPU acceleration structure via the owned tracer only if
	// snapshot->revisionId changed (mirrors RtOptixPathTracingSession::
	// start()'s identical revision-gated rebuild). Does NOT touch
	// resolution or the per-slot device/host buffers - RtSceneSnapshot
	// carries no width/height, so callers must ALSO call resize() (with
	// whatever resolution they're targeting) before tick() will actually
	// submit anything; tick() no-ops while either hasn't happened yet
	// (mirrors RtOptixPathTracingSession's own separate setResolution()+
	// start() calls, not one call doing both). Order between this and
	// resize() doesn't matter - call both before the first updateCamera()/
	// tick() of a new interactive burst. Returns false on hard failure
	// (OptiX unavailable, stream creation failed, or buildScene() failed).
	bool ensureSceneResources(const std::shared_ptr<const RtSceneSnapshot>& snapshot);

	// Cheap - just records the latest camera pose for the next tick() to
	// consider. Does NOT itself submit a launch. Unlike
	// RtOptixPathTracingSession::updateCamera(), no cross-thread generation
	// counter is needed - everything here runs on the caller's own thread.
	void updateCamera(const RtCamera& camera);

	// Call once per paintGL(). Does at most ONE of:
	//   (a) if a launch is in flight and its event has now completed, marks
	//       that slot "ready to present" (see pollCompletedFrame() - this
	//       does NOT itself copy/present anything) and frees the slot to
	//       accept a new submission - UNLESS a fresher pending camera is
	//       already queued and it's essentially identical to the one this
	//       completed launch was rendered with, in which case this result is
	//       silently dropped (not published) and (b) below fires in the same
	//       call instead. Avoids a "stop, then jerk by a tiny amount, then
	//       really stop" artifact right at the tail of a drag - see tick()'s
	//       own inline comment for the exact mechanism. A genuinely different
	//       pending camera (still-active motion) still publishes immediately;
	//       only a near-identical one is dropped. Also applies any pending
	//       resolution-scale change decided by the adaptive-overload check
	//       (see _resolutionScale) - safe here since nothing is in flight.
	//   (b) else, submits exactly one new async launch into the other slot
	//       and returns immediately - EVEN IF the camera hasn't changed since
	//       the last submission, as long as that slot's own accumulated
	//       sample count hasn't yet reached the configured cap (see
	//       setInteractiveBudget()). This is what makes image quality keep
	//       improving while the camera holds still, the same way NVIDIA's own
	//       reference renderer's per-frame accumulation does. A genuine pose
	//       change (see updateCamera()) resets both slots' accumulated
	//       sample count back to zero first.
	// Never blocks on GPU completion either way.
	void tick(const RtEnvironment& environment, bool shadowsEnabled, bool selfShadowsEnabled,
		bool enableEnvironmentImportanceSampling);

	// True if a submitted launch's completion event hasn't fired yet. Used
	// by resize()/callers to know when it's safe to tear down/reallocate
	// this renderer's resources - see resize()'s doc comment.
	bool isFrameInFlight() const;

	// Returns the CUDA device pointer (owned by this renderer, valid until
	// the NEXT tick() submits into this same slot - callers must finish
	// consuming it, e.g. via RtPresenter::uploadFromDevice(), before that
	// happens) to the most recently COMPLETED interactive frame, or nullptr
	// if none is ready yet. outGeneration increments every time a new
	// completed frame becomes available. outCamera receives the exact camera
	// pose THIS completed frame was rendered with - may lag the pose passed
	// to the most recent updateCamera() call by one in-flight launch (see
	// this class's own doc comment on why that gap is now bounded/small
	// regardless of scene size, rather than proportional to launch cost).
	// Callers (ViewportWidget's skybox/background coherence logic) MUST keep
	// drawing the background from THIS pose, not the live camera.
	void* pollCompletedFrame(int& outWidth, int& outHeight, RtCamera& outCamera, uint64_t& outGeneration) const;

	// Sets the interactive quality budget. samplesPerLaunch is FIXED (not
	// adapted per-frame, unlike the old sample-adaptive model this replaces)
	// - this is what bounds each launch's GPU cost independent of scene
	// complexity or desired final quality; keep it small (1-2). bounces is
	// also fixed - varying it frame-to-frame is more visually jarring (it
	// changes indirect-lighting brightness, not just noise level) than a
	// sample-count change. maxAccumulatedSamples caps how many samples a
	// slot accumulates before tick() stops submitting more for it (avoids
	// wasting GPU work once fully converged) - mirrors
	// RtOptixPathTracingSession::setMaxSamples()'s role. When
	// resolutionAdaptiveEnabled, tick() steps _resolutionScale down (and
	// back up) using a hysteresis band around targetFrameTimeMs (measured via
	// each launch's own RtOptixSceneTracer::elapsedEventTimeMs()) instead of
	// varying sample count - see _resolutionScale's own doc comment for why
	// this lever, not sample count, is what should flex under load.
	void setInteractiveBudget(uint32_t samplesPerLaunch, uint32_t maxAccumulatedSamples, uint32_t bounces,
		float targetFrameTimeMs, bool resolutionAdaptiveEnabled);

	// Progressive denoising - see this class's own doc comment. Mirrors
	// RtOptixPathTracingSession's identically-named setDenoiserEnabled()/
	// setDenoiserDevicePreference() exactly, just forwarded to this class's
	// OWN RtDenoiser instance rather than that session's (each GPU-facing
	// piece owns its own denoiser/device state - see RtDenoiser.h's Impl::
	// optixContext doc comment for the same pattern). When enabled, every
	// completed launch that's about to be published (see tick()) is also
	// denoised into a separate per-slot buffer (Slot::deviceDenoisedRGBA) via
	// RtDenoiser::denoiseDevice() - pollCompletedFrame() then hands back that
	// denoised buffer instead of the raw accumulation whenever the denoise
	// actually succeeded that launch, so the interactive path looks the same
	// (denoised) as a settled full-quality frame instead of showing raw
	// Monte Carlo noise for as long as the camera keeps moving.
	void setDenoiserEnabled(bool enabled) { _denoiserEnabled = enabled; }
	bool denoiserEnabled() const { return _denoiserEnabled; }
	void setDenoiserDevicePreference(DenoiserDevicePreference preference) { _denoiser.setDevicePreference(preference); }
	DenoiserDevicePreference denoiserDevicePreference() const { return _denoiser.devicePreference(); }
	const char* activeDenoiserName() const { return _denoiser.activeDeviceName(); }

	// Forwarded to RtOptixSceneTracer::submitSceneRenderToDevice() - mirror
	// RtOptixPathTracingSession's identically-named setters exactly (same
	// defaults too).
	void setMaxTransmissionBounces(uint32_t maxTransmissionBounces) { _maxTransmissionBounces = maxTransmissionBounces > 0 ? maxTransmissionBounces : 1; }
	void setFireflyClampThreshold(float threshold) { _fireflyClampThreshold = threshold > 0.0f ? threshold : 0.01f; }
	void setRussianRouletteStartDepth(uint32_t depth) { _russianRouletteStartDepth = depth > 0 ? depth : 1; }
	void setMaxVolumeScatterBounces(uint32_t maxVolumeScatterBounces) { _maxVolumeScatterBounces = maxVolumeScatterBounces > 0 ? maxVolumeScatterBounces : 1; }

	// Requested (viewport) resolution - what callers actually want displayed.
	// Destroys/reallocates both slots' resources at the corresponding INTERNAL
	// render resolution (round(width*_resolutionScale) x
	// round(height*_resolutionScale) - see _resolutionScale's doc comment;
	// may be smaller than what's requested here under sustained GPU load).
	// Must NOT be called while isFrameInFlight() is true (the implementation
	// defers rather than tearing down live memory out from under a still-
	// running kernel - a use-after-free, not a cosmetic stale-frame issue).
	// Callers should keep calling tick() without changing the camera until
	// isFrameInFlight() goes false before actually needing the new
	// resolution to take effect.
	void resize(int width, int height);

	// Requested (viewport) resolution - whatever the last resize() call set.
	// Callers (ViewportWidget's fast/slow-path resolution-match check) should
	// compare against this, not the possibly-smaller internal render
	// resolution actually being used under load.
	int width() const { return _requestedWidth; }
	int height() const { return _requestedHeight; }

	// Tears down both slots' GPU resources (stream, events, host staging,
	// scratch/output buffers). Unlike resize(), this DOES drain first (waits
	// for any in-flight event) since destruction has nowhere else to defer
	// to.
	void releaseResources();

private:
	struct Slot
	{
		void* deviceImageRGBA   = nullptr; // width*height float4 - this slot's persistent accumulation buffer
		void* hostParamsStaging = nullptr; // pinned host buffer, sizeof(RtOptixSceneParams) - see RtOptixSceneTracer::allocateParamsHostStagingBuffer()
		void* dParamsScratch    = nullptr; // sizeof(RtOptixSceneParams), device-side
		void* dAlbedoScratch    = nullptr; // width*height float3 - OIDN guide buffer, persistent (see progressive denoise)
		void* dNormalScratch    = nullptr; // width*height float3 - OIDN guide buffer, persistent
		void* startEvent        = nullptr; // CUDA event recorded right before this slot's launch - see elapsedEventTimeMs() use in tick()
		void* completionEvent   = nullptr; // CUDA event recorded right after this slot's launch
		RtCamera submittedCamera;

		// Denoised copy of deviceImageRGBA - see setDenoiserEnabled()'s doc
		// comment. A SEPARATE buffer (not written in place) so the raw
		// accumulation this slot keeps blending into is never itself
		// denoised - denoising the accumulation buffer in place would bake
		// denoiser artifacts into every subsequent blend, compounding over
		// time instead of each denoise starting fresh from the real
		// (still-refining) accumulated data. denoisedValid is false whenever
		// denoising was skipped/disabled/failed for this slot's last
		// completed launch - pollCompletedFrame() then falls back to the raw
		// deviceImageRGBA rather than presenting a stale or garbage buffer.
		void* deviceDenoisedRGBA = nullptr;
		bool  denoisedValid = false;

		// How many samples are already blended into deviceImageRGBA/
		// dAlbedoScratch/dNormalScratch for the CURRENT accumulation streak
		// (see _accumulationCamera) - passed as submitSceneRenderToDevice()'s
		// previousSampleCount on this slot's next submission, and as the RNG
		// sampleOffset (same value serves both purposes - see
		// RtOptixSceneParams::sampleOffset's doc comment). Reset to 0
		// whenever the camera genuinely changes.
		uint32_t accumulatedSampleCount = 0;
	};

	bool allocateSlot(Slot& slot, int width, int height);
	void freeSlot(Slot& slot);
	void drainSlot(Slot& slot); // bounded busy-wait for an in-flight slot's event - used only by releaseResources()
	void applyInternalResolution(); // recomputes _width/_height from _requestedWidth/_requestedHeight * _resolutionScale, reallocates both slots, resets the accumulation streak

	RtOptixSceneTracer& _tracer; // not owned
	void* _stream = nullptr;     // dedicated, non-default CUDA stream
	Slot  _slots[2];
	int   _readySlot    = -1; // index holding the newest COMPLETED, not-yet-superseded result; -1 = none yet
	int   _inFlightSlot = -1; // index of the slot with a submission still awaiting completion; -1 = none in flight

	// Edge-triggered, not equality-compared: every updateCamera() call marks
	// the pending pose dirty regardless of whether it actually differs from
	// the last one - cheaper and more robust than floating-point camera-
	// struct comparison, and a redundant resubmit for an unchanged pose is
	// harmless (same cost as any other submission). Used only by tick()'s
	// tail-frame publish-skip check now - see tick()'s own doc comment - NOT
	// as a gate on whether to submit at all (continuous accumulation keeps
	// submitting regardless, see tick()).
	RtCamera _pendingCamera;
	bool     _pendingCameraDirty = false;

	// The camera pose the CURRENT accumulation streak (across both slots) is
	// building up around - compared against each new _pendingCamera (see
	// tick()) to decide "keep blending" vs "genuinely moved, start over". A
	// pose change resets BOTH slots' accumulatedSampleCount to 0 at once,
	// since they alternate turns contributing to what is conceptually a
	// single accumulating image.
	RtCamera _accumulationCamera;
	bool     _accumulationCameraValid = false;

	int _requestedWidth = 0, _requestedHeight = 0; // what the caller last asked for via resize()
	int _width = 0, _height = 0;                   // actual allocated/rendered resolution = requested * _resolutionScale
	uint64_t _generation = 0;

	uint64_t _builtRevision = 0; // last snapshot->revisionId successfully passed to _tracer.buildScene()

	uint32_t _samplesPerLaunch = 1; // fixed per-launch sample count - see class/setInteractiveBudget() doc comments for why this replaced sample-count adaptation
	uint32_t _maxBounces = 2;
	uint32_t _maxAccumulatedSamples = 1;
	float _targetFrameTimeMs = 16.0f;
	bool  _resolutionAdaptiveEnabled = false;

	// Internal render resolution scale in (0,1] - 1.0 means "render at the
	// full requested resolution" (the common case). Stepped down (hysteresis
	// band around _targetFrameTimeMs, measured via elapsedEventTimeMs()) when
	// even this class's fixed, small samplesPerLaunch doesn't fit inside the
	// frame budget for a heavy enough scene, and stepped back up once
	// comfortably under budget. This - not sample count - is the lever that
	// flexes under load: a smaller launch resolution means fewer primary
	// rays (a real, direct cut to GPU cost), whereas adapting sample count
	// per-frame was the old model's actual bug (a heavier scene just got a
	// smaller-but-still-proportionally-slower launch, so the display-lag gap
	// this whole redesign exists to close never actually closed). The
	// presenter already upscales a smaller texture via GL_LINEAR, so no
	// shader change is needed to display at less than full resolution.
	float _resolutionScale = 1.0f;
	static constexpr float kMinResolutionScale = 0.25f;
	static constexpr float kResolutionScaleStep = 0.85f; // multiplicative step down; 1/step used to step back up
	float _pendingResolutionScale = 1.0f; // decided during (a), applied at the safe point right after (see tick())

	// Deliberately asymmetric hysteresis, wider than a naive symmetric band:
	// step down only on a CLEARLY over-budget launch (>1.3x target), step up
	// on merely AT-or-under budget (<1.0x target, not a conservative 0.8x).
	// A narrower/symmetric band left a "dead zone" a launch's timing could
	// sit in indefinitely once scaled down (neither over the down-threshold
	// nor under the up-threshold), so the scale never recovered even once
	// GPU headroom was clearly available again.
	static constexpr float kResolutionScaleDownThreshold = 1.3f;
	static constexpr float kResolutionScaleUpThreshold    = 1.0f;

	// A single over/under-threshold READING is not enough to act on by
	// itself: stepping straight from N down-steps to N-1 (or all the way
	// back to 1.0 from one step down) can land exactly on a scene whose true
	// cost sits BETWEEN the two candidate resolutions - e.g. full resolution
	// costs 28ms (over the 26ms down-threshold) but one step down costs
	// 20ms (right at the up-threshold), so it steps down, immediately looks
	// "comfortably" under budget, steps back up, is over budget again, and
	// repeats forever. Requiring several CONSECUTIVE readings on the same
	// side before acting (and resetting the count whenever a reading falls
	// in neither direction, or a change actually happens) turns single noisy
	// samples into a genuine trend before committing to a change - this is
	// what actually stops the limit-cycle, not the width of the threshold
	// band alone (already tried, and still cycled).
	static constexpr int kConsecutiveSamplesRequired = 5;
	int _consecutiveOverBudget  = 0;
	int _consecutiveUnderBudget = 0;

	// Exponential moving average of elapsedEventTimeMs() readings - the
	// signal the threshold comparisons above and kConsecutiveSamplesRequired
	// actually act on, instead of each launch's raw (noisy) timing directly.
	// A single launch's GPU time already jitters meaningfully call to call
	// (driver scheduling, other GPU work, thermal/clock variance) even when
	// the scene's true steady-state cost hasn't changed - smoothing that out
	// first means the consecutive-samples counter above is counting genuine
	// trend readings, not just however many noisy spikes happened to land on
	// the same side of a threshold in a row. Cheap (one lerp per completed
	// launch) and strictly additive to the existing hysteresis, not a
	// replacement for it. -1.0f means "uninitialized" (see tick()'s use
	// site) - reset alongside _skipNextTimingSample wherever that's set,
	// since a stale average from before a resolution/scene change is exactly
	// as unrepresentative as the raw skipped reading itself.
	static constexpr float kFrameTimeSmoothingAlpha = 0.25f; // weight given to each NEW reading; higher = faster to react, lower = smoother
	float _smoothedFrameTimeMs = -1.0f;

	// Set whenever a resolution/scene change just happened (see
	// applyInternalResolution()/ensureSceneResources()) - the FIRST launch
	// afterward often costs meaningfully more than steady-state (OptiX
	// pipeline/SBT warm-up, driver-side lazy allocation, cache-cold BVH
	// traversal) and is not representative of this scene's real per-launch
	// cost at the new resolution. Without skipping it, that one outlier
	// timing could trigger an immediate, spurious step down that then never
	// recovers (see the hysteresis comment above) even though the scene
	// would have rendered comfortably fast from the second launch onward.
	bool _skipNextTimingSample = false;

	unsigned int _maxTransmissionBounces = 32;
	float _fireflyClampThreshold = 3.0f;
	unsigned int _russianRouletteStartDepth = 3;
	unsigned int _maxVolumeScatterBounces = 64;

	// This class's OWN RtDenoiser instance - see setDenoiserEnabled()'s doc
	// comment for why this isn't shared with RtOptixPathTracingSession's.
	// Defaults to enabled/Auto, matching that session's own defaults; both
	// are overwritten by ViewportWidget's actual PT-dialog-backed settings
	// immediately after construction (see startInteractivePathTracedGpuSession()).
	RtDenoiser _denoiser;
	bool _denoiserEnabled = true;
};
