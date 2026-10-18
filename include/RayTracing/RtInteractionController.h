#pragma once

#include <functional>

class QTimer;
class RtInteractiveRenderer;

// ---------------------------------------------------------------------------
// RtInteractionController
//
// Owns the GPU interactive-PT camera-settle/resume STATE MACHINE that used to
// be scattered across ViewportWidget as boolean flags (_rayTracedArmed,
// _rayTracedInteractiveActive, plus RtInteractiveRenderer's own
// _cameraSettled) and several functions that each had to independently
// remember the correct call ORDER (the now-removed resetRayTracedIdleTimer()
// and armRayTracedResumeWarmUp(), plus onRayTracedIdleTimeout() and
// onRayTracedResumeWarmUpTimeout(), which still exist as thin forwarding
// wrappers into this class). That ordering was never enforced by the
// code, only documented - Codex's audit found the SAME "call
// setCameraSettled(false) before starting the session, not after" bug in two
// separate call sites, which is exactly the failure mode a shared, implicit
// convention produces. This class exists so that mistake becomes structurally
// impossible: only THIS class calls RtInteractiveRenderer::setCameraSettled()/
// setInteractiveBudget()/requestFullResolution(), and only THIS class starts
// or stops the settle/resume timers.
//
// States:
//   Disarmed   - ray tracing isn't active at all.
//   Interacting - the interactive accumulator is live; camera may be
//                 currently moving (denoise off, resolution-adaptive on) or
//                 may have just resumed from Recovering (same unsettled
//                 configuration) - the 450ms settle timer is counting down to
//                 Settled the whole time this state is held.
//   Recovering - a scene mutation/scripted-animation/resize/engine-switch
//                event tore the interactive session down; the 400ms debounce
//                is counting down before resuming into Interacting. Debounced
//                specifically because scripted camera animations
//                (notifyCameraAnimationTick()) call in here on every ~5ms
//                frame - the repeated calls keep re-arming this debounce
//                faster than it can fire, so a whole animation only pays the
//                rebuild cost once, right at the end, not on every tick.
//   Settled    - the accumulator's own sample cap has been raised to the
//                dialog-configured target, resolution forced back to full,
//                and the one-shot final denoise armed - see
//                RtInteractiveRenderer's own class doc comment for why this
//                is done IN PLACE now rather than handing off to a separate
//                settled/offline session (that handoff caused a visible
//                "flash").
//
// Deliberately does NOT own: CPU/Embree engine selection, the offline/export
// PT path, RtSceneSnapshot construction, or GAS/texture rebuild mechanics -
// those stay in ViewportWidget and are reached here only through the small
// set of injected callbacks (see Callbacks below), since they remain far too
// entangled with rendering/scene state to safely relocate in a single,
// behavior-preserving pass. This is a faithful extraction of ORCHESTRATION
// POLICY, not a renderer redesign.
// ---------------------------------------------------------------------------
class RtInteractionController
{
public:
	enum class State
	{
		Disarmed,
		Interacting, // accumulator live, camera moving OR idle-counting-down toward Settled
		Recovering,  // torn down, debounced resume counting down
		Settled
	};

	// Callbacks the controller invokes to perform the actual session
	// start/stop mechanics - see this class's own doc comment for why these
	// stay as injected functions rather than being moved into this class.
	struct Callbacks
	{
		// True if the currently effective PT engine is GPU/OptiX (vs CPU/
		// Embree) - CPU has no interactive-quality tier at all, so this
		// controller collapses straight to the "invalidate, then settle
		// 450ms later" behavior regardless of which notify*() method was
		// called (see isGpuEngine's use in the .cpp).
		std::function<bool()> isGpuEngine;

		// Starts (or hands the live camera pose to, if already running at
		// this resolution) the interactive GPU accumulator. Mirrors
		// ViewportWidget::startInteractiveRayTracedGpuSession(false).
		std::function<void()> startInteractiveSession;

		// Same, but forces a scene-snapshot refresh against the SAME live
		// accumulator instead of tearing it down - used only for
		// notifyContentAnimationTick() (skeletal/morph/pointer-animation
		// playback). Mirrors startInteractiveRayTracedGpuSession(true).
		std::function<void()> startInteractiveSessionWithSceneRefresh;

		// Tears down whichever GPU PT session(s) are active. Mirrors
		// ViewportWidget::teardownActiveRayTracedSessions().
		std::function<void()> teardownSessions;

		// Starts the SETTLED/offline session immediately, bypassing any
		// debounce - used for CPU/Embree's only quality tier, and for the
		// engine-switch case, which deliberately doesn't wait (a single
		// explicit menu choice, not rapid camera interaction). Mirrors
		// ViewportWidget::startRayTracedSession().
		std::function<void()> startSettledSessionImmediate;

		// Full stop of any CPU or GPU worker session, without touching this
		// controller's own state - used by disarm(). Mirrors the
		// _rtSession.stop()/_ptOptixSession.stop() pair already scattered
		// across several call sites.
		std::function<void()> stopAllWorkerSessions;

		// True if the interactive GPU accumulator actually ended up live
		// after the most recent startInteractiveSession*() call - mirrors
		// ViewportWidget::_rayTracedInteractiveActive. Only consulted by
		// notifyContentAnimationTick(), to mirror
		// notifyRayTracedAnimationMutated()'s original control flow exactly:
		// attempt the in-place refresh first, and only fall back to a normal
		// teardown+Recovering transition if that attempt didn't actually
		// leave a live session behind.
		std::function<bool()> isInteractiveSessionLive;

		// Current dialog-configured sample cap / bounce count (already
		// clamped to >= 1 by the caller) - passed to
		// RtInteractiveRenderer::setInteractiveBudget() by this controller.
		// Mirrors the std::max<uint32_t>(_ptMaxSamples, 1) /
		// std::max(_ptMaxBounces, 1) expressions previously duplicated at
		// every setInteractiveBudget() call site.
		std::function<unsigned int()> maxSamples;
		std::function<unsigned int()> maxBounces;
	};

	// idleTimer: 450ms single-shot, fires when the camera has held still long
	// enough to promote Interacting -> Settled (or, for CPU, to trigger the
	// only settled session it has).
	// resumeTimer: 400ms single-shot, fires when Recovering has gone quiet
	// long enough to resume into Interacting.
	// Both are NON-OWNING - ViewportWidget continues to construct/parent
	// them (Qt object-tree ownership requires a QObject parent, and this
	// class deliberately isn't one - see ViewportInteractionController's
	// identical convention), and connects their timeout() signals to two
	// thin forwarding calls into onIdleTimerFired()/onResumeTimerFired()
	// below. This controller is the ONLY code that calls start()/stop() on
	// them, though.
	RtInteractionController(RtInteractiveRenderer& rtInteractiveRenderer, QTimer* idleTimer, QTimer* resumeTimer, Callbacks callbacks);

	State state() const { return _state; }
	bool armed() const { return _state != State::Disarmed; }

	// Arms ray tracing. startInteractiveSessionNow=false is used only by the
	// dialog's manual Render button path (requestRayTracedRenderNow()),
	// which starts the settled session directly instead - mirrors
	// armRayTracedRenderingMode()'s identical parameter.
	void arm(bool startInteractiveSessionNow = true);
	void disarm();

	// A genuine, live camera-move event: mouse drag, wheel zoom, keyboard
	// nav, inertia coast. Transitions to Interacting from any state and
	// (re)arms the 450ms settle timer.
	void notifyCameraInteracting();

	// One frame of a SCRIPTED camera animation (Home/standard-view, Fit All,
	// window-zoom, the shared zoom/pan slerp helper) - called repeatedly,
	// every ~5ms, for the animation's whole duration. Transitions to
	// Recovering like any other non-interactive event, but relies on being
	// called repeatedly to keep the resume debounce from firing mid-
	// animation - see the Recovering state's own doc comment.
	void notifyCameraAnimationTick();

	// A discrete, ONE-SHOT camera reposition with no follow-up ticks at all
	// (restoreCameraPose(), resetToSystemCamera(),
	// applyGltfCameraEntryTransform(), fitAll()'s Fly/FirstPerson branch).
	// Mechanically identical to notifyCameraAnimationTick() today (same
	// Recovering transition), kept as a separate entry point so it isn't
	// conflated with genuine per-frame ticking if the two ever need to
	// diverge later.
	void notifyCameraJumpNonInteractive();

	// Scene content actually changed in a way that invalidates shading
	// correctness (material/light/geometry edit, skybox/background toggle) -
	// always tears down and waits for the debounced resume, since a
	// stale-but-still-displayed frame would now be wrong, not just imprecise.
	void notifySceneContentMutated();

	// One frame of skeletal/morph/pointer-animation PLAYBACK. Distinct from
	// notifySceneContentMutated(): this keeps the interactive GPU accumulator
	// ALIVE and refreshes its scene snapshot in place (see
	// startInteractiveSessionWithSceneRefresh), rather than tearing it down -
	// see ViewportWidget::notifyRayTracedAnimationMutated()'s original doc
	// comment for why (dropping to raster for the whole duration of any
	// animated clip would be a much worse regression than a live but
	// slightly-stale-cadence PT preview).
	void notifyContentAnimationTick();

	// A real widget resize.
	void notifyResize();

	// The user explicitly changed the CPU/GPU/Auto engine preference (a
	// single menu choice, not rapid interaction) - unlike every other
	// non-interactive notify above, this does NOT wait for the debounce; it
	// tears down and immediately starts the new engine's settled session.
	void notifyEngineSwitch();

private:
	void enterInteracting();
	void enterRecovering();
	void enterSettled();
	void onIdleTimerFired();
	void onResumeTimerFired();

	RtInteractiveRenderer& _rtInteractiveRenderer;
	QTimer* _idleTimer;
	QTimer* _resumeTimer;
	Callbacks _callbacks;
	State _state = State::Disarmed;

	friend class ViewportWidget; // connects timer signals to onIdleTimerFired()/onResumeTimerFired()
};
