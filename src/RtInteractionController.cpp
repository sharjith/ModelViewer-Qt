#include "RtInteractionController.h"

#include "RtInteractiveRenderer.h"

#include <QTimer>

namespace
{
	// Fixed small per-launch sample count, accumulated up to a cap; only
	// resolution scale (not sample count) flexes under sustained GPU load -
	// see RtInteractiveRenderer::_resolutionScale's own doc comment for why.
	// No separate, lower interactive-only cap constant exists anymore either -
	// the accumulator's sample cap is always the caller's actual dialog-
	// configured target (maxSamples()/maxBounces() callbacks), both while
	// interacting and once settled. An earlier version used a separate, lower,
	// hardcoded interactive-only cap (128) that got raised to the dialog target
	// only once settled - but that meant if the dialog target was ever
	// configured BELOW 128, the accumulator could already have overshot the
	// new (lower) cap by the time settle fired, leaving accumulatedSampleCount
	// >= maxAccumulatedSamples with no new launch left to complete and trigger
	// the one-shot settled denoise - a genuine dead state, stuck showing the
	// last raw, undenoised frame forever. Using the same value throughout
	// removes that class of bug entirely.
	constexpr uint32_t kInteractiveSamplesPerLaunch = 1;

	// ~30fps budget for the launch itself, both interacting and settled - NOT
	// 20ms/50fps as first tried: measurement ([PT-RESSCALE-DIAG], since
	// removed) showed a real scene's ordinary per-launch cost sitting at
	// ~26-29ms, comfortably usable but just over an aggressive 20ms target -
	// the resolution-scale fallback kept "fixing" that non-problem, and since
	// one step down cost ~20-26ms itself (right back at the boundary), it
	// oscillated between the two forever instead of ever settling. 33ms gives
	// normal moderately-heavy scenes actual room to live at full resolution,
	// reserving the fallback for scenes that are genuinely far outside it.
	constexpr float kInteractiveTargetFrameMs = 33.0f;
}

RtInteractionController::RtInteractionController(RtInteractiveRenderer& rtInteractiveRenderer,
	QTimer* idleTimer, QTimer* resumeTimer, Callbacks callbacks)
	: _rtInteractiveRenderer(rtInteractiveRenderer), _idleTimer(idleTimer), _resumeTimer(resumeTimer), _callbacks(std::move(callbacks))
{
}

void RtInteractionController::arm(bool startInteractiveSessionNow)
{
	if (_state != State::Disarmed)
		return;

	if (!startInteractiveSessionNow)
	{
		// Caller (requestPathTracedRenderNow()) is about to immediately start
		// the settled session itself instead - mirrors
		// armPathTracedRenderingMode()'s identical early-out. Still counts as
		// armed so disarm() correctly tears it back down later, but this
		// controller's own state machine has nothing to do until the next
		// notify*() call.
		_state = State::Recovering; // "not yet live" - the next real notify*() picks a real transition
		return;
	}

	// Start the continuous interactive accumulator immediately rather than
	// waiting for the first camera-move event - it serves both the "just
	// armed, camera hasn't moved yet" case and the actively-dragging case
	// identically. CPU/Embree has no interactive-quality tier at all, so it
	// goes straight through the same "invalidate, settle 450ms later" path
	// every other non-interactive event uses (enterRecovering() - NOT a
	// bare state/timer set: CPU still needs its current trace torn down via
	// the teardownSessions callback, exactly like any other CPU
	// invalidation, or a session from before this arm() could keep running
	// underneath the fresh one - see notifyCameraInteracting()'s identical
	// CPU branch and the Codex audit finding that caught this originally
	// being skipped here).
	if (_callbacks.isGpuEngine())
		enterInteracting();
	else
		enterRecovering();
}

void RtInteractionController::disarm()
{
	if (_state == State::Disarmed)
		return;
	_state = State::Disarmed;

	_idleTimer->stop();
	_resumeTimer->stop();
	_callbacks.stopAllWorkerSessions();
	_callbacks.teardownSessions();
}

void RtInteractionController::notifyCameraInteracting()
{
	if (_state == State::Disarmed)
		return;

	// GPU/OptiX only: a genuine camera-move event stays on the continuous
	// interactive accumulator rather than falling back to raster. CPU/Embree
	// has no hardware RT acceleration to make a per-frame interactive trace
	// realistic, so it always falls through to the shared non-interactive
	// path below regardless (enterRecovering() - Codex's audit caught this
	// CPU branch previously skipping the teardownSessions callback entirely,
	// leaving a CPU trace running WHILE the camera was actively moving - the
	// exact "stop tracing immediately on interaction" behavior this whole
	// controller exists to preserve).
	if (_callbacks.isGpuEngine())
		enterInteracting();
	else
		enterRecovering();
}

void RtInteractionController::notifyCameraAnimationTick()
{
	// Mechanically identical to every other non-interactive notify below
	// today - kept as its own entry point specifically because it's called
	// repeatedly, every ~5ms, for a scripted animation's whole duration. That
	// repetition is what keeps the resume debounce from firing mid-animation
	// (see the Recovering state's own doc comment) - any single call behaves
	// exactly like notifySceneContentMutated() would.
	notifySceneContentMutated();
}

void RtInteractionController::notifyCameraJumpNonInteractive()
{
	// Also mechanically identical today - see this method's header doc
	// comment for why it's still a distinct entry point.
	notifySceneContentMutated();
}

void RtInteractionController::notifySceneContentMutated()
{
	if (_state == State::Disarmed)
		return;
	enterRecovering();
}

void RtInteractionController::notifyContentAnimationTick()
{
	if (_state == State::Disarmed)
		return;

	if (_callbacks.isGpuEngine())
	{
		// Unlike a genuine scene mutation, animation playback keeps the
		// interactive accumulator ALIVE and refreshes its scene snapshot in
		// place rather than tearing it down - dropping to raster for the
		// whole duration of an animated clip would be a much worse
		// regression than a live PT preview with a slightly stale cadence.
		// Leaves _state alone when it was already Interacting or Settled -
		// but a successful revival OUT OF Recovering must normalize to
		// Interacting (2nd Codex audit catch): leaving _state stuck at
		// Recovering here left the controller with no path back to Settled
		// at all - onResumeTimerFired() only re-enters Interacting when
		// isInteractiveSessionLive() is FALSE (see its own guard), so once
		// this callback revived a live session out of Recovering, that
		// timer's eventual fire became a permanent no-op: the idle timer was
		// never started, so the accumulator could never promote to Settled
		// again even once the camera/content genuinely went still. The idle
		// timer is (re)armed on both the Interacting and freshly-normalized-
		// from-Recovering cases: if a content animation starts ticking while
		// still in Interacting (e.g. right after a drag stopped, before the
		// 450ms settle countdown elapsed), that countdown must not be
		// allowed to fire mid-playback and promote to Settled while the
		// scene is still changing every frame - each tick here defers it
		// exactly like a live camera-interacting event would. A no-op
		// restart while already Settled (single-shot timer, not running).
		_callbacks.startInteractiveSessionWithSceneRefresh();
		if (_callbacks.isInteractiveSessionLive())
		{
			if (_state == State::Recovering)
			{
				_resumeTimer->stop();
				_state = State::Interacting;
			}
			if (_state == State::Interacting)
				_idleTimer->start();
			return;
		}
	}

	// CPU/Embree, OptiX unavailable, or a throttled/failed interactive GPU
	// refresh all fall back to the same safe invalidation every other
	// non-interactive event uses.
	enterRecovering();
}

void RtInteractionController::notifyResize()
{
	if (_state == State::Disarmed)
		return;
	enterRecovering();
}

void RtInteractionController::notifyEngineSwitch()
{
	if (_state == State::Disarmed)
		return;

	// A single explicit menu choice, not rapid camera interaction - the
	// debounce exists for the latter, so this deliberately bypasses it and
	// starts the new engine's settled session immediately instead of waiting
	// through Recovering.
	_idleTimer->stop();
	_resumeTimer->stop();
	_callbacks.teardownSessions();
	_callbacks.startSettledSessionImmediate();
	_state = State::Settled;
}

void RtInteractionController::enterInteracting()
{
	_resumeTimer->stop();

	// MUST run before starting/resuming the session below, not after: a
	// resumed (previously torn-down) session pays its warm-up cost
	// synchronously, and that warm-up's own first denoise decision reads
	// _cameraSettled immediately - calling this after would let those very
	// first ticks observe whatever _cameraSettled was stale-left-at by the
	// previous session (RtInteractiveRenderer::releaseResources() does not
	// reset it). This exact ordering bug was found twice by Codex's audit in
	// the code this controller replaces - enforcing it in one place instead
	// of at every call site is the entire reason this class exists.
	_rtInteractiveRenderer.setCameraSettled(false);
	_rtInteractiveRenderer.setInteractiveBudget(kInteractiveSamplesPerLaunch, _callbacks.maxSamples(), _callbacks.maxBounces(),
		kInteractiveTargetFrameMs, /*resolutionAdaptiveEnabled=*/true);
	_callbacks.startInteractiveSession();

	_state = State::Interacting;
	_idleTimer->start();
}

void RtInteractionController::enterRecovering()
{
	_callbacks.teardownSessions();
	_state = State::Recovering;

	// MUST stop _idleTimer here (Codex audit catch): it may still be
	// counting down from a PRIOR Interacting spell (e.g. a drag settled,
	// then a scene mutation arrived before the 450ms countdown finished).
	// Left running, that stale timer can still fire mid-Recovering and call
	// onIdleTimerFired() -> enterSettled(), which stomps _state to Settled
	// on a session that was JUST torn down above. That corrupted state then
	// makes onResumeTimerFired()'s own `_state != State::Recovering` guard
	// bail out permanently once the resume debounce actually fires -
	// silently losing the resume entirely, not just mis-ordering it.
	_idleTimer->stop();

	if (_callbacks.isGpuEngine())
	{
		// GPU: no separate settled tier to wait for - just debounce the
		// resume. Deliberately does NOT restart _idleTimer here (unlike the
		// CPU branch below) - onResumeTimerFired() re-enters enterInteracting()
		// once the debounce clears, which starts it itself.
		_resumeTimer->start();
	}
	else
	{
		// CPU/Embree has no interactive tier at all - every event, camera or
		// otherwise, just restarts the same 450ms idle-then-settle countdown.
		_idleTimer->start();
	}
}

void RtInteractionController::enterSettled()
{
	_rtInteractiveRenderer.setCameraSettled(true);
	_rtInteractiveRenderer.setInteractiveBudget(kInteractiveSamplesPerLaunch, _callbacks.maxSamples(), _callbacks.maxBounces(),
		kInteractiveTargetFrameMs, /*resolutionAdaptiveEnabled=*/false);
	_rtInteractiveRenderer.requestFullResolution();
	_state = State::Settled;
}

void RtInteractionController::onIdleTimerFired()
{
	if (_state == State::Disarmed)
		return;

	if (_callbacks.isGpuEngine())
	{
		enterSettled();
		return;
	}

	_callbacks.startSettledSessionImmediate();
}

void RtInteractionController::onResumeTimerFired()
{
	if (_state != State::Recovering || !_callbacks.isGpuEngine())
		return;

	// The user already resumed interaction (or a settled dialog render is
	// running) before this timer fired - re-entering here too would be
	// redundant at best, a wasted duplicate GAS rebuild at worst.
	if (_callbacks.isInteractiveSessionLive())
		return;

	enterInteracting();
}
