#include "InteractivePtRenderer.h"

#include "RtOptixSceneTracer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

#include <QDebug>

namespace
{
// Tight equality check (not the tail-lag-skip fix's fuzzy "near identical"
// check elsewhere in this file - that one decides whether to publish a
// nearly-converged frame; this one decides whether the CURRENT accumulation
// streak is still valid) - _pendingCamera only ever takes on a genuinely new
// VALUE when a real mouse/keyboard event calls updateCamera() with a freshly
// computed RtCamera (see ViewportWidget::startInteractivePathTracedGpuSession()).
// A camera recomputed from unchanged state is expected to compare bit-equal
// (or within float noise) to the previous one, so a tiny epsilon here is
// only for floating-point safety, not a deliberate "close enough" tolerance.
bool cameraPosesMatch(const RtCamera& a, const RtCamera& b)
{
	// 1e-6, not 1e-5: RtSceneBuilder::buildCamera() is a pure function of the
	// live Camera's current state with no cross-call accumulation of its
	// own, so two calls with a genuinely unchanged camera reproduce
	// (effectively) bit-identical output - there is no real "floating point
	// noise" to tolerate here, only this computation's own rounding, which
	// sits far below 1e-6 relative. 1e-5 was too loose for the POSITION
	// check specifically: pan/dolly-zoom move the camera by small ABSOLUTE
	// world-space steps per mouse event, and a relative epsilon scaled by
	// the camera's own distance from the origin can exceed a real, visible
	// pan/zoom increment once the camera sits any reasonable distance out
	// (e.g. position magnitude 100 -> 1e-5 epsilon is 1mm, easily bigger
	// than one slow-drag mouse event's worth of translation). Rotation
	// wasn't affected because forward/right/up are unit vectors where even a
	// fraction of a degree moves a component by orders of magnitude more
	// than that. The visible symptom of the too-loose epsilon: pan/zoom
	// small increments got wrongly treated as "same pose, keep blending",
	// so the accumulator kept averaging together frames from genuinely
	// different camera positions - showing as ghosting/smearing that reads
	// as "sticky", not a real submission/latency problem.
	constexpr float kEps = 1e-6f;
	auto vecClose = [kEps](const glm::vec3& x, const glm::vec3& y) {
		return glm::length(x - y) < kEps * std::max(1.0f, glm::length(x));
	};
	return vecClose(a.position, b.position) &&
		vecClose(a.forward, b.forward) &&
		vecClose(a.right, b.right) &&
		vecClose(a.up, b.up) &&
		std::abs(a.aspectRatio - b.aspectRatio) < kEps &&
		a.orthographic == b.orthographic &&
		std::abs(a.tanHalfFovY - b.tanHalfFovY) < kEps &&
		std::abs(a.orthoHalfHeight - b.orthoHalfHeight) < kEps;
}

// Loose "about to settle" check for tick()'s tail-lag publish-skip heuristic
// - deliberately NOT cameraPosesMatch() above (that one's tolerance is tight
// enough to only match a truly-unchanged camera; reusing it here would make
// the skip almost never trigger for the decelerating-rotation tail case it
// was built for). This one only needs to ask "is the newer pending pose
// close enough to this one that publishing both would look redundant" -
// true right at the tail of a drag (consecutive poses converging toward a
// stop), never true during genuinely active motion (consecutive poses stay
// meaningfully apart every tick).
//
// Originally only checked the forward vector (a fine proxy for ROTATION,
// where even a fraction of a degree moves it far past any reasonable
// tolerance) - but pure pan/dolly-zoom motion never changes forward at all,
// so that check alone was permanently "true" throughout an entire pan/zoom
// drag, meaning publishThisFrame was false on nearly every completed launch
// for as long as the user kept dragging - nothing new ever reached the
// screen until the drag stopped. Checking position/FOV too (still with a
// generous, "nearly settled" tolerance, not cameraPosesMatch()'s strict one)
// fixes that: real per-event pan/zoom deltas stay well outside this
// tolerance while actively dragging, only converging inside it right at the
// tail, matching how rotation's own forward-dot check already behaved.
bool cameraNearlySettled(const RtCamera& a, const RtCamera& b)
{
	constexpr float kForwardDotThreshold = 0.999999f; // ~0.08 degrees - unchanged from the original rotation-only check
	constexpr float kPositionRelTolerance = 1e-4f;     // looser than cameraPosesMatch()'s 1e-6, still far tighter than one active-drag mouse-event's worth of pan/dolly
	constexpr float kFovTolerance = 1e-4f;

	const float forwardDot = glm::dot(glm::normalize(a.forward), glm::normalize(b.forward));
	if (forwardDot <= kForwardDotThreshold)
		return false;

	const float posDelta = glm::length(a.position - b.position);
	if (posDelta >= kPositionRelTolerance * std::max(1.0f, glm::length(a.position)))
		return false;

	if (a.orthographic != b.orthographic)
		return false;
	if (std::abs(a.tanHalfFovY - b.tanHalfFovY) >= kFovTolerance)
		return false;
	if (std::abs(a.orthoHalfHeight - b.orthoHalfHeight) >= kFovTolerance)
		return false;

	return true;
}
} // namespace

InteractivePtRenderer::InteractivePtRenderer(RtOptixSceneTracer& tracer) : _tracer(tracer)
{
}

InteractivePtRenderer::~InteractivePtRenderer()
{
	releaseResources();
}

bool InteractivePtRenderer::applySceneSnapshot(const std::shared_ptr<const RtSceneSnapshot>& snapshot)
{
	if (!snapshot || !_tracer.isAvailable())
		return false;

	if (!_stream)
	{
		_stream = _tracer.createStream();
		if (!_stream)
			return false;
	}

	// Revision-gated rebuild, mirrors RtOptixPathTracingSession::start()'s
	// identical check - camera-only movement leaves snapshot->revisionId
	// unchanged, so this (comparatively expensive) GAS/IAS rebuild is only
	// paid when the scene itself actually changed.
	if (snapshot->revisionId != _builtRevision || _builtRevision == 0)
	{
		if (!_tracer.buildScene(*snapshot))
		{
			qWarning() << "InteractivePtRenderer::applySceneSnapshot: buildScene() failed.";
			return false;
		}
		_builtRevision = snapshot->revisionId;
		_skipNextTimingSample = true; // see this class's own doc comment - the next launch's timing won't be representative
		_smoothedFrameTimeMs = -1.0f; // stale average from before the rebuild is as unrepresentative as the skipped reading itself
		// A rebuilt scene invalidates whatever's already accumulated - same
		// reasoning as a camera pose change (see tick()). Deliberately does
		// NOT clear _readySlot: a just-completed frame from the PREVIOUS
		// revision may still be the newest displayable chunk this paint, and
		// paintGL() polls only AFTER tick() returns. Clearing _readySlot here
		// was making every animation-driven pending-scene apply erase the
		// frame that had just completed moments earlier, so pollCompletedFrame()
		// immediately returned nullptr and interactive PT appeared frozen/off
		// throughout continuous animation.
		_accumulationCameraValid = false;
		_accumulatedSampleCount = 0;
		_latestCompletedSlot = -1;
	}

	return true;
}

bool InteractivePtRenderer::ensureSceneResources(const std::shared_ptr<const RtSceneSnapshot>& snapshot)
{
	if (!snapshot || !_tracer.isAvailable())
		return false;

	// Animation-driven updates can arrive while the previous launch is still
	// rendering. Rebuilding the tracer right then would race the in-flight
	// work; queue only the newest snapshot and let tick() apply it once the
	// current launch completes.
	if (isFrameInFlight() && (snapshot->revisionId != _builtRevision || _builtRevision == 0))
	{
		_pendingSceneSnapshot = snapshot;
		return true;
	}

	return applySceneSnapshot(snapshot);
}

void InteractivePtRenderer::updateCamera(const RtCamera& camera)
{
	const bool poseChanged = !_pendingCameraValid || !cameraPosesMatch(_pendingCamera, camera);
	_pendingCamera = camera;
	_pendingCameraValid = true;
	if (poseChanged)
		_pendingCameraDirty = true;
}

bool InteractivePtRenderer::allocateSlot(Slot& slot, int width, int height)
{
	slot.deviceImageRGBA    = _tracer.allocateDeviceRGBABuffer(width, height);
	slot.hostParamsStaging  = _tracer.allocateParamsHostStagingBuffer();
	slot.dParamsScratch     = _tracer.allocateParamsScratchBuffer();
	slot.dAlbedoScratch     = _tracer.allocateGuideScratchBuffer(width, height);
	slot.dNormalScratch     = _tracer.allocateGuideScratchBuffer(width, height);
	slot.startEvent         = _tracer.createEvent();
	slot.completionEvent    = _tracer.createEvent();
	slot.deviceDenoisedRGBA = _tracer.allocateDeviceRGBABuffer(width, height);

	if (!slot.deviceImageRGBA || !slot.hostParamsStaging || !slot.dParamsScratch ||
		!slot.dAlbedoScratch || !slot.dNormalScratch || !slot.startEvent || !slot.completionEvent ||
		!slot.deviceDenoisedRGBA)
	{
		freeSlot(slot);
		return false;
	}
	slot.denoisedValid = false;
	return true;
}

void InteractivePtRenderer::freeSlot(Slot& slot)
{
	_tracer.freeDeviceRGBABuffer(slot.deviceImageRGBA);
	slot.deviceImageRGBA = nullptr;
	_tracer.freeParamsHostStagingBuffer(slot.hostParamsStaging);
	slot.hostParamsStaging = nullptr;
	_tracer.freeScratchBuffer(slot.dParamsScratch);
	slot.dParamsScratch = nullptr;
	_tracer.freeScratchBuffer(slot.dAlbedoScratch);
	slot.dAlbedoScratch = nullptr;
	_tracer.freeScratchBuffer(slot.dNormalScratch);
	slot.dNormalScratch = nullptr;
	_tracer.destroyEvent(slot.startEvent);
	slot.startEvent = nullptr;
	_tracer.destroyEvent(slot.completionEvent);
	slot.completionEvent = nullptr;
	_tracer.freeDeviceRGBABuffer(slot.deviceDenoisedRGBA);
	slot.deviceDenoisedRGBA = nullptr;
	slot.denoisedValid = false;
}

void InteractivePtRenderer::drainSlot(Slot& slot)
{
	if (!slot.completionEvent)
		return;
	// Bounded busy-wait - only ever called from releaseResources() (i.e.
	// session stop/destruction), never from tick()'s per-paint path, so a
	// short blocking wait here does not reintroduce the UI-thread stall this
	// class otherwise avoids. The bound (2s) is generous relative to any
	// realistic interactive launch - only exists so a genuinely hung GPU
	// doesn't hang teardown forever; the buffers still leak in that case,
	// but that's an unrecoverable device-hang scenario regardless.
	constexpr int kMaxWaitMs = 2000;
	constexpr int kPollIntervalMs = 1;
	int waitedMs = 0;
	while (!_tracer.isEventComplete(slot.completionEvent) && waitedMs < kMaxWaitMs)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
		waitedMs += kPollIntervalMs;
	}
	if (waitedMs >= kMaxWaitMs)
		qWarning() << "InteractivePtRenderer::drainSlot: timed out waiting for in-flight launch to complete - "
			"freeing its resources anyway (possible GPU hang).";
}

void InteractivePtRenderer::tick(const RtEnvironment& environment, bool shadowsEnabled, bool selfShadowsEnabled,
	bool enableEnvironmentImportanceSampling)
{
	// Consume a pending requestFullResolution() request immediately if
	// nothing is currently in flight - safe to reallocate right now, same
	// precondition applyInternalResolution() always requires. If something
	// IS in flight, this is instead consumed a few lines down, inside the
	// completion-handling branch, before it would otherwise recompute
	// _pendingResolutionScale from _resolutionScale and silently discard
	// this request.
	if (_forceFullResolutionRequested && _inFlightSlot < 0)
	{
		_forceFullResolutionRequested = false;
		if (_resolutionScale != 1.0f)
		{
			_resolutionScale = 1.0f;
			applyInternalResolution();
			// See _holdPublishUntilConverged's doc comment - keeps whatever
			// was last displayed on screen instead of popping to this fresh,
			// single-sample accumulation the instant it completes.
			_holdPublishUntilConverged = true;
		}
	}

	// (a) Check the in-flight submission (if any) for completion first. If
	// it just completed, fall through to (b) in this SAME call instead of
	// returning - submitting the next launch immediately (rather than
	// waiting for the next paintGL()) avoids a one-paint idle bubble between
	// "frame finished" and "next frame submitted". If it's still in flight,
	// there is nothing else to do this call.
	if (_inFlightSlot >= 0)
	{
		Slot& slot = _slots[_inFlightSlot];
		if (!_tracer.isEventComplete(slot.completionEvent))
			return; // still running - nothing else to do this call

		// Resolution-scale adaptive step - replaces the old sample-count
		// adaptive step (see _resolutionScale's own doc comment for why this
		// lever, not sample count, is what should flex under load). Requires
		// several CONSECUTIVE over/under-budget readings (see
		// kConsecutiveSamplesRequired's doc comment) before actually acting -
		// a single reading is not enough, since stepping straight between two
		// candidate resolutions can straddle a scene's true cost and cycle
		// forever otherwise. The change is only APPLIED a few lines down
		// (once we know nothing is in flight), not here directly.
		//
		// A pending requestFullResolution() request takes priority over this
		// whole computation - see _forceFullResolutionRequested's doc
		// comment for why this must be checked before (not folded into) the
		// normal "_pendingResolutionScale = _resolutionScale" reset below,
		// which would otherwise silently discard it.
		if (_forceFullResolutionRequested)
		{
			_pendingResolutionScale = 1.0f;
			_forceFullResolutionRequested = false;
			// See _holdPublishUntilConverged's doc comment - this reallocation
			// won't actually happen until the "safe to reallocate" point
			// below, so the hold itself is armed there, once we know the
			// reallocation is really about to happen.
			_pendingResolutionChangeIsForced = true;
		}
		else
		{
			_pendingResolutionScale = _resolutionScale;
			if (_resolutionAdaptiveEnabled)
			{
			const float ms = _tracer.elapsedEventTimeMs(slot.startEvent, slot.completionEvent);
			if (ms >= 0.0f)
			{
				if (_skipNextTimingSample)
				{
					// First launch after a resolution/scene change - not
					// representative (warm-up cost), see this class's own
					// doc comment on _skipNextTimingSample. Consume the skip
					// without acting on this one reading, and without folding
					// it into the smoothed average either (that average is
					// reset to uninitialized alongside this flag - see
					// applyInternalResolution()/ensureSceneResources() - so
					// there's nothing to fold it into yet regardless).
					_skipNextTimingSample = false;
				}
				else
				{
					// EMA smoothing of the timing signal - see
					// _smoothedFrameTimeMs's doc comment for why this, not
					// each launch's raw (noisy) ms, is what the threshold
					// comparisons and kConsecutiveSamplesRequired below
					// actually act on. First real reading after a reset seeds
					// the average directly (no slow ramp-up bias from
					// blending against an arbitrary starting value).
					_smoothedFrameTimeMs = (_smoothedFrameTimeMs < 0.0f)
						? ms
						: _smoothedFrameTimeMs + (ms - _smoothedFrameTimeMs) * kFrameTimeSmoothingAlpha;

					if (_smoothedFrameTimeMs > _targetFrameTimeMs * kResolutionScaleDownThreshold)
					{
						_consecutiveUnderBudget = 0;
						if (_resolutionScale > kMinResolutionScale && ++_consecutiveOverBudget >= kConsecutiveSamplesRequired)
						{
							_pendingResolutionScale = std::max(kMinResolutionScale, _resolutionScale * kResolutionScaleStep);
							_consecutiveOverBudget = 0;
						}
					}
					else if (_smoothedFrameTimeMs < _targetFrameTimeMs * kResolutionScaleUpThreshold)
					{
						_consecutiveOverBudget = 0;
						if (_resolutionScale < 1.0f && ++_consecutiveUnderBudget >= kConsecutiveSamplesRequired)
						{
							_pendingResolutionScale = std::min(1.0f, _resolutionScale / kResolutionScaleStep);
							_consecutiveUnderBudget = 0;
						}
					}
					else
					{
						// Neither clearly over nor clearly under - a genuinely
						// mixed/borderline signal breaks any streak in progress
						// rather than letting stale old counts from before this
						// reading eventually trigger a change they no longer
						// reflect.
						_consecutiveOverBudget = 0;
						_consecutiveUnderBudget = 0;
					}
				}
			}
		}
		}

		// If a fresher camera is already queued (dirty) AND it's essentially
		// the same pose this now-completing launch was rendered with, don't
		// publish this now-stale result at all - just fall through to (b)
		// below and resubmit with the newer pose immediately. Otherwise the
		// user sees this frame and THEN, one launch-latency later, a small
		// corrective jerk to the true final pose - exactly the "stops, then
		// jerks ahead a little, then stops for good" artifact reported at
		// the tail of a drag. Deliberately only skipped for a near-identical
		// pose (tight epsilon): a genuinely different pending camera means
		// motion is still actively ongoing, where publishing immediately for
		// progressive feedback matters more than avoiding an extra
		// intermediate frame.
		bool publishThisFrame = true;
		if (_pendingCameraDirty && cameraNearlySettled(slot.submittedCamera, _pendingCamera))
			publishThisFrame = false;

		// ACCUMULATION-HISTORY bookkeeping - unconditional, regardless of
		// publishThisFrame: this slot's buffers now hold the most recently
		// completed state of the shared accumulation, and the NEXT
		// submission's copy-forward must read from it even if this
		// particular result never gets displayed (see this class's own doc
		// comment on why _latestCompletedSlot must not be conflated with
		// _readySlot, a purely presentation-facing decision below).
		_latestCompletedSlot = _inFlightSlot;

		if (publishThisFrame)
		{
			// Denoise a COPY of this slot's raw accumulation before
			// publishing - see setDenoiserEnabled()'s doc comment for why a
			// separate buffer, not in-place. Runs synchronously (blocks this
			// call briefly - RtDenoiser::denoiseDeviceWithOptix() explicitly
			// syncs before returning) but only once per completed launch, on
			// the GUI thread same as the rest of tick() - acceptable next to
			// the launch's own (async, non-blocking) GPU cost. A failed/
			// disabled denoise just means this launch presents its raw
			// accumulation instead (see pollCompletedFrame()), same as any
			// other "denoiser unavailable" fallback elsewhere in this
			// codebase - never blocks presentation outright.
			//
			// Skipped entirely while the camera is still moving (see
			// setCameraSettled()'s doc comment) - denoising an under-converged,
			// rapidly-changing accumulation every tick smeared sharp/thin CAD
			// geometry into an ill-defined blur during motion. pollCompletedFrame()
			// falls back to the raw (sharp but noisy) accumulation whenever
			// denoisedValid is false, same as any other denoise-skipped case.
			//
			// Also skipped once settled but still ramping up: ViewportWidget
			// raises this renderer's own sample cap/resolution toward full
			// quality on settle (see requestFullResolution()/
			// setInteractiveBudget()), and re-denoising every single
			// completed launch throughout that whole climb (128 samples up
			// to the dialog's target) is wasted GPU work that also
			// backslides visually launch to launch as the denoiser keeps
			// re-guessing from a still-growing sample count - a genuinely
			// SHARPER final image comes from denoising exactly once, when
			// _accumulatedSampleCount has actually reached the cap: the raw
			// accumulation is shown (increasingly clean on its own as
			// samples pile up) for the whole climb, then a single denoise
			// pass on the fully-converged result. Since tick() stops
			// submitting new launches once the cap is reached (see its own
			// cap check), this condition can only become true once, on the
			// launch that crosses the cap - a natural one-shot, no separate
			// tracking flag needed.
			slot.denoisedValid = _denoiserEnabled && _cameraSettled && _accumulatedSampleCount >= _maxAccumulatedSamples &&
				_denoiser.denoiseDevice(slot.deviceImageRGBA, slot.dAlbedoScratch, slot.dNormalScratch,
					_width, _height, slot.deviceDenoisedRGBA, _accumulatedSampleCount);

			// See _holdPublishUntilConverged's doc comment - skips publishing
			// ONLY this one completed launch (the freshly-reallocated,
			// ~1-sample-accumulated result that would otherwise pop against
			// whatever was displayed before a forced full-resolution
			// reallocation), then resumes normal per-launch publishing
			// immediately after - so the climb from here still reads as a
			// smooth, progressive convergence (same as any other settle),
			// just without that single worst-case jarring first frame.
			if (_holdPublishUntilConverged)
			{
				_holdPublishUntilConverged = false;
			}
			else
			{
				_readySlot = _inFlightSlot;
				++_generation;
			}
		}
		_inFlightSlot = -1;

		// Safe to reallocate now - nothing in flight. Resets the
		// accumulation streak too (fresh buffers), same as any other
		// resolution change.
		if (_pendingResolutionScale != _resolutionScale)
		{
			_resolutionScale = _pendingResolutionScale;
			applyInternalResolution();
			if (_pendingResolutionChangeIsForced)
			{
				_pendingResolutionChangeIsForced = false;
				_holdPublishUntilConverged = true;
			}
		}
	}

	// A newer scene revision may have arrived while the last launch was in
	// flight (e.g. skeletal/morph animation advancing). Apply it now, at the
	// first safe point with no launch using the old tracer state anymore.
	if (_pendingSceneSnapshot && (_pendingSceneSnapshot->revisionId != _builtRevision || _builtRevision == 0))
	{
		if (!applySceneSnapshot(_pendingSceneSnapshot))
			return;
		_pendingSceneSnapshot.reset();
	}
	else if (_pendingSceneSnapshot)
	{
		_pendingSceneSnapshot.reset();
	}

	// (b) Nothing in flight (either it never was, or (a) just freed it
	// above) - consider submitting a new launch. Unlike the old single-shot
	// model, this does NOT require _pendingCameraDirty - continuous
	// accumulation keeps submitting more samples at the SAME pose for as
	// long as the current slot hasn't hit its cap, which is what lets image
	// quality keep improving while the camera holds still.
	if (_width <= 0 || _height <= 0)
		return;

	const int submitInto = (_readySlot == 0) ? 1 : 0;
	Slot& slot = _slots[submitInto];
	if (!slot.deviceImageRGBA)
		return; // resources not allocated yet - see resize()

	// A genuine pose (or scene) change invalidates the shared accumulation
	// history entirely - discard it so a later submission doesn't blend
	// fresh samples (new pose) against old-pose data.
	const bool poseChanged = !_accumulationCameraValid || !cameraPosesMatch(_accumulationCamera, _pendingCamera);
	if (poseChanged)
	{
		_accumulationCamera = _pendingCamera;
		_accumulationCameraValid = true;
		_accumulatedSampleCount = 0;
		_latestCompletedSlot = -1;
	}

	if (_accumulatedSampleCount >= _maxAccumulatedSamples)
	{
		// Fully converged at this pose - nothing more to do until the pose
		// changes again.
		_pendingCameraDirty = false;
		return;
	}

	// Copy the shared accumulation history FORWARD into this slot before
	// blending a new sample into it, unless this is the very first
	// submission ever for this streak (_latestCompletedSlot == -1, nothing
	// to copy yet - the kernel's own previousSampleCount == 0 branch
	// overwrites fresh instead of blending, matching that case exactly).
	// Without this, this slot would blend its next sample against whatever
	// STALE data it happened to hold from several submissions ago (its own
	// last turn), producing a second, independently-diverging accumulation
	// instead of continuing the ONE shared history - see this class's own
	// doc comment for the full symptom this caused.
	if (_latestCompletedSlot >= 0 && _latestCompletedSlot != submitInto)
	{
		const Slot& source = _slots[_latestCompletedSlot];
		const bool copiedThisTick =
			_tracer.copyDeviceRGBABufferAsync(slot.deviceImageRGBA, source.deviceImageRGBA, _width, _height, _stream) &&
			_tracer.copyGuideScratchBufferAsync(slot.dAlbedoScratch, source.dAlbedoScratch, _width, _height, _stream) &&
			_tracer.copyGuideScratchBufferAsync(slot.dNormalScratch, source.dNormalScratch, _width, _height, _stream);
		if (!copiedThisTick)
			return; // try again next tick() rather than launch against a half-copied/stale destination
	}

	const uint32_t previousSampleCount = _accumulatedSampleCount;
	const bool ok = _tracer.submitSceneRenderToDevice(_pendingCamera, environment, _width, _height,
		_samplesPerLaunch, previousSampleCount, _maxBounces, shadowsEnabled, selfShadowsEnabled, enableEnvironmentImportanceSampling,
		_maxTransmissionBounces, _fireflyClampThreshold, _russianRouletteStartDepth, _maxVolumeScatterBounces, previousSampleCount,
		slot.deviceImageRGBA, slot.hostParamsStaging, slot.dParamsScratch, slot.dAlbedoScratch, slot.dNormalScratch,
		_stream, slot.startEvent, slot.completionEvent);

	if (ok)
	{
		slot.submittedCamera = _pendingCamera;
		_accumulatedSampleCount = previousSampleCount + _samplesPerLaunch;
		_inFlightSlot = submitInto;
		_pendingCameraDirty = false;
	}
	// On failure, leave _pendingCameraDirty set - the next tick() will
	// simply try again with whatever the latest pending camera is by then.
}

bool InteractivePtRenderer::isFrameInFlight() const
{
	return _inFlightSlot >= 0 && !_tracer.isEventComplete(_slots[_inFlightSlot].completionEvent);
}

uint32_t InteractivePtRenderer::currentSampleCount() const
{
	return _accumulatedSampleCount;
}

void* InteractivePtRenderer::pollCompletedFrame(int& outWidth, int& outHeight, RtCamera& outCamera, uint64_t& outGeneration) const
{
	if (_readySlot < 0)
	{
		outWidth = 0;
		outHeight = 0;
		outGeneration = _generation;
		return nullptr;
	}
	const Slot& slot = _slots[_readySlot];
	outWidth = _width;
	outHeight = _height;
	outCamera = slot.submittedCamera;
	outGeneration = _generation;
	// Prefer the denoised copy (see tick()'s doc comment) - falls back to the
	// raw accumulation whenever denoising was disabled/unavailable/failed for
	// this particular completed launch.
	return slot.denoisedValid ? slot.deviceDenoisedRGBA : slot.deviceImageRGBA;
}

void InteractivePtRenderer::setInteractiveBudget(uint32_t samplesPerLaunch, uint32_t maxAccumulatedSamples, uint32_t bounces,
	float targetFrameTimeMs, bool resolutionAdaptiveEnabled)
{
	_samplesPerLaunch = samplesPerLaunch > 0 ? samplesPerLaunch : 1;
	_maxAccumulatedSamples = maxAccumulatedSamples >= _samplesPerLaunch ? maxAccumulatedSamples : _samplesPerLaunch;
	_maxBounces = bounces > 0 ? bounces : 1;
	_targetFrameTimeMs = targetFrameTimeMs > 0.0f ? targetFrameTimeMs : 16.0f;
	_resolutionAdaptiveEnabled = resolutionAdaptiveEnabled;
}

void InteractivePtRenderer::applyInternalResolution()
{
	const int newWidth  = std::max(1, static_cast<int>(std::lround(_requestedWidth  * _resolutionScale)));
	const int newHeight = std::max(1, static_cast<int>(std::lround(_requestedHeight * _resolutionScale)));
	if (newWidth == _width && newHeight == _height)
		return;

	freeSlot(_slots[0]);
	freeSlot(_slots[1]);
	_readySlot = -1;
	_inFlightSlot = -1;
	_latestCompletedSlot = -1;
	_accumulatedSampleCount = 0;
	_pendingCameraDirty = false;
	_pendingCameraValid = false;
	_accumulationCameraValid = false;

	_width = newWidth;
	_height = newHeight;
	_skipNextTimingSample = true; // see this class's own doc comment - a fresh resolution's first launch isn't representative
	_smoothedFrameTimeMs = -1.0f; // stale average from before the resolution change is as unrepresentative as the skipped reading itself
	_consecutiveOverBudget = 0;
	_consecutiveUnderBudget = 0;

	if (!allocateSlot(_slots[0], _width, _height) || !allocateSlot(_slots[1], _width, _height))
	{
		qWarning() << "InteractivePtRenderer::applyInternalResolution: failed to allocate interactive slot resources at"
			<< _width << "x" << _height << "- interactive GPU-resident presentation unavailable this session.";
		freeSlot(_slots[0]);
		freeSlot(_slots[1]);
		_width = 0;
		_height = 0;
	}
}

void InteractivePtRenderer::resize(int width, int height)
{
	if (width == _requestedWidth && height == _requestedHeight)
		return;
	if (isFrameInFlight())
	{
		// Can't safely tear down a slot a still-running kernel is writing
		// into - see this method's header doc comment. Deferring silently
		// is safe: the caller is expected to keep calling tick()/resize() on
		// subsequent paints until this succeeds, same pattern
		// RtPresenter::upload()'s own _texWidth/_texHeight mismatch check
		// uses for its lazy reallocation.
		return;
	}

	_requestedWidth = width;
	_requestedHeight = height;
	if (width <= 0 || height <= 0)
	{
		freeSlot(_slots[0]);
		freeSlot(_slots[1]);
		_readySlot = -1;
		_inFlightSlot = -1;
		_latestCompletedSlot = -1;
		_accumulatedSampleCount = 0;
		_pendingCameraDirty = false;
		_pendingCameraValid = false;
		_accumulationCameraValid = false;
		_width = 0;
		_height = 0;
		return; // e.g. minimized - leave both slots unallocated until a real size arrives
	}

	applyInternalResolution();
}

void InteractivePtRenderer::releaseResources()
{
	if (_inFlightSlot >= 0)
	{
		drainSlot(_slots[_inFlightSlot]);
		_inFlightSlot = -1;
	}
	freeSlot(_slots[0]);
	freeSlot(_slots[1]);
	if (_stream)
	{
		_tracer.destroyStream(_stream);
		_stream = nullptr;
	}
	_readySlot = -1;
	_latestCompletedSlot = -1;
	_accumulatedSampleCount = 0;
	_pendingCameraDirty = false;
	_pendingCameraValid = false;
	_accumulationCameraValid = false;
	_requestedWidth = 0;
	_requestedHeight = 0;
	_width = 0;
	_height = 0;
	_builtRevision = 0;
	_pendingSceneSnapshot.reset();
	_resolutionScale = 1.0f; // fresh start next time - don't carry over a scale reduced under a previous session's load
	_pendingResolutionScale = 1.0f;
	_skipNextTimingSample = false;
	_smoothedFrameTimeMs = -1.0f;
	_consecutiveOverBudget = 0;
	_consecutiveUnderBudget = 0;
	_forceFullResolutionRequested = false;
	_pendingResolutionChangeIsForced = false;
	_holdPublishUntilConverged = false;
}
