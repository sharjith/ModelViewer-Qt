#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <glm/glm.hpp>

#include "RtSceneSnapshot.h"
#include "RtEmbreeScene.h"
#include "CpuPathTracer.h"
#include "RtFrameAccumulator.h"
#include "RtDenoiser.h"
#include "RtEnvironmentSampler.h"

// ---------------------------------------------------------------------------
// RtPathTracingSession
//
// Owns the background progressive-rendering loop: repeatedly calls
// CpuPathTracer::renderPass() and accumulates into RtFrameAccumulator,
// publishing the latest resolved (averaged) frame to a double-buffered slot
// the GL/UI thread can read via latestFrame() without blocking the worker.
//
// The outer worker thread here does not itself do CPU-heavy path-tracing
// math - each call to CpuPathTracer::renderPass() already parallelizes one
// full-frame pass across its own worker threads (see CpuPathTracer.cpp).
// This thread just orchestrates: run a pass, accumulate, publish, repeat.
//
// Revision/cancellation model (mirrors SceneRuntime's existing
// _cancelRequested/_loadCancelled pattern - see SceneRuntime.h):
//   - start(snapshot): rebuilds the Embree BVH and restarts accumulation
//     from scratch. Call whenever geometry/material/visibility changes.
//   - notifyCameraChanged(snapshot): resets accumulation but does NOT
//     rebuild the Embree BVH - cheaper path for camera-only changes.
//   - stop(): cancels any in-flight pass and joins the worker thread. Call
//     when interaction resumes (falls back to raster) or the mode is exited.
// ---------------------------------------------------------------------------
class RtPathTracingSession
{
public:
	RtPathTracingSession();
	~RtPathTracingSession();

	RtPathTracingSession(const RtPathTracingSession&)            = delete;
	RtPathTracingSession& operator=(const RtPathTracingSession&) = delete;

	// Records the target render resolution. Takes effect on the next
	// start()/notifyCameraChanged() call - does not affect an already-running
	// session by itself.
	void setResolution(int width, int height);

	// Hard-coded v1 convergence cap. Later this can be surfaced in Settings.
	void setMaxSamples(uint32_t maxSamples) { _maxSamples = maxSamples > 0 ? maxSamples : 1; }
	uint32_t maxSamples() const { return _maxSamples; }

	// Forwarded to the owned CpuPathTracer - takes effect on the next
	// start()/notifyCameraChanged() call, same as setMaxSamples() above (the
	// worker thread reads _tracer's settings once per renderPass() call, not
	// mid-pass, so there's no torn-state risk in setting this while a
	// previous session's worker is still winding down).
	void setTracerSettings(const CpuPathTracer::Settings& settings) { _tracer.setSettings(settings); }
	const CpuPathTracer::Settings& tracerSettings() const { return _tracer.settings(); }

	// Current raw (pre-final-denoise) accumulated sample count - cheap
	// progress-polling accessor that doesn't copy the frame buffer, unlike
	// latestFrame(). RtFrameAccumulator::_sampleCount is a plain (non-
	// atomic) uint32_t the worker thread increments once per completed
	// pass - reading it from the UI thread here is a benign data race for
	// progress-bar purposes (worst case one stale/torn-adjacent read gets
	// silently corrected on the next poll), not worth a real atomic for.
	uint32_t currentSampleCount() const { return _accumulator.sampleCount(); }

	// When false, publishLatest() never runs the final OIDN pass even once
	// maxSamples() is reached - the raw progressive accumulation itself is
	// published as-is instead. Lets a user compare the noisy-but-unfiltered
	// result against the denoised one, or skip denoising entirely for a
	// faster export at the cost of visible noise.
	void setDenoiserEnabled(bool enabled) { _denoiserEnabled = enabled; }
	bool denoiserEnabled() const { return _denoiserEnabled; }

	// Rebuilds the Embree scene from snapshot and (re)starts progressive
	// accumulation from scratch. Cancels/joins any previously running pass
	// first, so this is also the right call to "restart with a fresh scene".
	void start(std::shared_ptr<const RtSceneSnapshot> snapshot);

	// Updates the live snapshot (new camera, same geometry/instances) and
	// resets accumulation without rebuilding the Embree BVH. Cancels/joins
	// any previously running pass first.
	void notifyCameraChanged(std::shared_ptr<const RtSceneSnapshot> snapshotWithNewCamera);

	// Cancels any in-flight pass and joins the worker thread. Safe to call
	// even if not currently running.
	void stop();

	bool isRunning() const { return _running.load(std::memory_order_acquire); }

	// Latest frame available for display: raw resolved accumulation while
	// samples are still progressing, then one final OIDN-denoised frame when
	// maxSamples() is reached. Also returns how many raw samples/pixel it
	// represents. Safe to call from any thread (e.g. the GL paintGL() thread)
	// while the worker keeps running; returns an empty vector if no pass has
	// completed yet.
	std::vector<glm::vec3> latestFrame(int& outWidth, int& outHeight, uint32_t& outSampleCount,
		std::vector<float>* outAlpha = nullptr) const;

private:
	void workerLoop(uint64_t myRevision);
	void publishLatest(bool finalDenoise);
	void resetForNewPass(std::shared_ptr<const RtSceneSnapshot> snapshot, bool rebuildEmbreeScene);

	RtEmbreeScene         _embreeScene;
	CpuPathTracer         _tracer;
	RtFrameAccumulator    _accumulator;
	RtDenoiser            _denoiser;
	RtEnvironmentSampler  _envSampler; // rebuilt alongside _embreeScene - see resetForNewPass()

	int _width  = 0;
	int _height = 0;
	uint32_t _maxSamples = 128;
	bool _denoiserEnabled = true;

	mutable std::mutex _snapshotMutex;
	std::shared_ptr<const RtSceneSnapshot> _snapshot;

	std::thread _worker;
	std::atomic<uint64_t> _activeRevision{ 0 };
	std::atomic<bool>     _cancelRequested{ false };
	std::atomic<bool>     _running{ false };

	mutable std::mutex     _publishMutex;
	std::vector<glm::vec3> _publishedFrame;
	std::vector<float>     _publishedAlpha;
	int      _publishedWidth       = 0;
	int      _publishedHeight      = 0;
	uint32_t _publishedSampleCount = 0;
};
