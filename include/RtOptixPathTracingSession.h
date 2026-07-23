#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <glm/glm.hpp>

#include "RtDenoiser.h"
#include "RtOptixSceneTracer.h"
#include "RtSceneSnapshot.h"

// ---------------------------------------------------------------------------
// RtOptixPathTracingSession
//
// GPU-backend counterpart to RtPathTracingSession (see that class's doc
// comment for the pattern this mirrors) - owns a background worker thread
// that repeatedly renders a small chunk of samples per pixel via
// RtOptixSceneTracer::renderScene(), accumulates the results (as a running
// mean, in linear HDR space - see RtOptixSceneParams.h's image field doc
// comment for why linear-space accumulation matters here) and publishes the
// latest averaged frame to a mutex-protected slot the GL/UI thread can poll
// via latestFrame() without blocking the worker. This is what lets
// PathTracingDialog's progress bar/elapsed-time display move for the GPU
// engine the same way it already does for the CPU one, instead of the
// caller blocking on one single all-samples-at-once optixLaunch().
//
// Unlike RtPathTracingSession, there is no separate notifyCameraChanged() -
// ViewportWidget's own CPU-path usage never actually calls that either (see
// its own doc comment), always calling start() unconditionally instead, so
// there was no existing behavior to preserve here. start() always resets
// accumulation from scratch but only rebuilds the GPU acceleration
// structure (RtOptixSceneTracer::buildScene()) when the snapshot's
// revisionId actually changed - preserving the revision-gated rebuild
// optimization ViewportWidget used to do at its own call site before this
// class existed (building a real GAS/IAS is heavier than Embree's own BVH
// build, worth skipping on pure camera movement).
//
// ViewportWidget now calls start() with one of TWO different setting
// profiles depending on whether the camera is actively moving or settled -
// a reduced-quality "interactive" profile (low maxSamples/maxBounces,
// denoiser off, setStayAliveAtConvergence(true)) while dragging, and the
// full user-configured profile once idle (see ViewportWidget::
// startInteractivePathTracedGpuSession()/startOptixTestPathTracedSession()).
// This class itself remains completely unaware of that distinction - it
// just receives different set*() values before each start() call, same as
// any other setting change.
//
// Only the FIRST interactive tick of a drag (or a mid-drag resolution
// change) actually calls start() - every subsequent tick instead calls the
// much cheaper updateCamera(), which hands the still-running worker thread
// a new camera pose in place (see workerLoop()'s _cameraGeneration check)
// without a thread respawn, a GAS/IAS touch, or (on the ViewportWidget side)
// rebuilding the whole scene snapshot. That snapshot rebuild - not the
// thread respawn itself, and not the actual 1-2 SPP trace - is the real cost
// a naive "just call start() again on every tick" approach pays for
// (ViewportWidget::buildPathTracedSnapshot() does a synchronous GPU
// environment-cubemap readback on every call), which is what updateCamera()
// exists to let interactive dragging skip entirely. setStayAliveAtConvergence()
// is what keeps the worker thread available to receive those updates instead
// of exiting once it converges at the (deliberately tiny) interactive sample
// count - see its own doc comment.
//
// Also owns an RtDenoiser, run once the final chunk reaches maxSamples() -
// same "denoise only on the last pass" contract as RtPathTracingSession's
// own publishLatest(finalDenoise) (see RtDenoiser::denoise()'s doc comment
// for why: OIDN's guide buffers make more of a difference the fewer real
// samples/pixel are behind the beauty image, so the final pass is the only
// one worth paying for). RtOptixSceneTracer::renderScene() supplies the
// albedo/normal guide buffers this needs - see its own doc comment.
// ---------------------------------------------------------------------------
class RtOptixPathTracingSession
{
public:
	RtOptixPathTracingSession();
	~RtOptixPathTracingSession();

	RtOptixPathTracingSession(const RtOptixPathTracingSession&) = delete;
	RtOptixPathTracingSession& operator=(const RtOptixPathTracingSession&) = delete;

	bool isAvailable() const;

	// Takes effect on the next start() call - does not affect an already-running session by itself.
	void setResolution(int width, int height);

	void setMaxSamples(uint32_t maxSamples) { _maxSamples = maxSamples > 0 ? maxSamples : 1; }
	uint32_t maxSamples() const { return _maxSamples; }

	// Forwarded to RtOptixSceneTracer::renderScene() - mirrors CpuPathTracer::
	// Settings::maxBounces (RtPathTracingSession's identical setting).
	void setMaxBounces(uint32_t maxBounces) { _maxBounces = maxBounces > 0 ? maxBounces : 1; }
	uint32_t maxBounces() const { return _maxBounces; }

	// Forwarded to RtOptixSceneTracer::renderScene() - mirrors CpuPathTracer::
	// Settings::enableEnvironmentImportanceSampling / ViewportWidget's
	// "Environment Importance Sampling" toggle. Environment-NEE is still
	// additionally gated on the uploaded distribution actually being valid
	// (see RtOptixSceneParams::RtOptixEnvironment::envTotalWeight's doc
	// comment) - this flag only controls whether it's ATTEMPTED at all.
	void setEnvironmentImportanceSamplingEnabled(bool enabled) { _enableEnvironmentImportanceSampling = enabled; }
	bool environmentImportanceSamplingEnabled() const { return _enableEnvironmentImportanceSampling; }

	// Forwarded to RtOptixSceneTracer::renderScene() - mirror CpuPathTracer::
	// Settings::maxTransmissionBounces/fireflyClampThreshold/
	// russianRouletteStartDepth exactly (same defaults too) - previously
	// hardcoded constants in RtOptixScene.cu, silently ignoring whatever
	// ViewportWidget's PathTracingDialog-backed settings actually held even
	// when CPU and GPU renders were started from identical-looking dialog
	// state.
	void setMaxTransmissionBounces(uint32_t maxTransmissionBounces) { _maxTransmissionBounces = maxTransmissionBounces > 0 ? maxTransmissionBounces : 1; }
	uint32_t maxTransmissionBounces() const { return _maxTransmissionBounces; }

	void setFireflyClampThreshold(float threshold) { _fireflyClampThreshold = threshold > 0.0f ? threshold : 0.01f; }
	float fireflyClampThreshold() const { return _fireflyClampThreshold; }

	void setRussianRouletteStartDepth(uint32_t depth) { _russianRouletteStartDepth = depth > 0 ? depth : 1; }
	uint32_t russianRouletteStartDepth() const { return _russianRouletteStartDepth; }

	// Mirrors CpuPathTracer::Settings::maxVolumeScatterBounces exactly (same
	// default) - KHR_materials_volume_scatter's free-flight random walk's own,
	// separate scatter-event budget.
	void setMaxVolumeScatterBounces(uint32_t maxVolumeScatterBounces) { _maxVolumeScatterBounces = maxVolumeScatterBounces > 0 ? maxVolumeScatterBounces : 1; }
	uint32_t maxVolumeScatterBounces() const { return _maxVolumeScatterBounces; }

	// Chunk size (samples per pixel gathered per background-thread pass,
	// i.e. per RtOptixSceneTracer::renderScene() call) - smaller values give
	// more frequent progress-bar/preview updates at the cost of more
	// per-launch overhead (device alloc/readback happens once per chunk).
	// Clamped to maxSamples() internally so a single chunk never overshoots
	// the target.
	void setSamplesPerChunk(uint32_t samplesPerChunk) { _samplesPerChunk = samplesPerChunk > 0 ? samplesPerChunk : 1; }

	// When true, workerLoop() never exits on its own once sampleCount reaches
	// maxSamples() - it idles (checking for cancellation/a new updateCamera()
	// call) instead of letting _running go false. Takes effect on the next
	// start() call. ViewportWidget sets this before starting the interactive
	// session (see startInteractivePathTracedGpuSession()) so the worker
	// thread survives being fully converged at its (deliberately tiny)
	// interactive sample count, ready to immediately service the next
	// updateCamera() call instead of needing a full stop()/start() respawn -
	// see updateCamera()'s own doc comment for why that respawn is the thing
	// this whole mechanism exists to avoid. Left false (default) for the
	// normal settled/full-quality session, which should still exit its
	// worker thread once done, as before.
	void setStayAliveAtConvergence(bool enabled) { _stayAliveAtConvergence = enabled; }

	// When false, the published frame never gets the final OIDN pass even
	// once maxSamples() is reached - the raw progressive accumulation is
	// published as-is instead. Mirrors RtPathTracingSession::
	// setDenoiserEnabled()'s identical contract.
	void setDenoiserEnabled(bool enabled) { _denoiserEnabled = enabled; }
	bool denoiserEnabled() const { return _denoiserEnabled; }

	// Forwarded to the owned RtDenoiser - safe to call at any time, including
	// while a session is actively running (see RtDenoiser::setDevicePreference()).
	void setDenoiserDevicePreference(DenoiserDevicePreference preference) { _denoiser.setDevicePreference(preference); }
	DenoiserDevicePreference denoiserDevicePreference() const { return _denoiser.devicePreference(); }

	// (Re)builds the GPU acceleration structure only if snapshot->revisionId
	// changed since the last successful build, then (re)starts progressive
	// accumulation from scratch. Cancels/joins any previously running pass
	// first, so this is also the right call to "restart with a fresh
	// camera/scene". Returns false only for hard failures (OptiX
	// unavailable, buildScene() failed) - accumulation simply won't start
	// on failure.
	bool start(std::shared_ptr<const RtSceneSnapshot> snapshot);

	// Cheap alternative to a full stop()+start() respawn for a camera-only
	// move while a (stayAliveAtConvergence) session is already running: hands
	// the live worker thread a new camera pose without touching the thread
	// itself, the GAS/IAS, or the snapshot - it just resets the worker's own
	// in-progress accumulation the next time it notices the pose changed (see
	// workerLoop()'s _cameraGeneration check). This is what actually removes
	// the per-drag-tick overhead a respawn pays for - most of which is not
	// this class's own thread-join/relaunch cost but the CALLER's snapshot-
	// rebuild cost (ViewportWidget::buildPathTracedSnapshot() does a
	// synchronous GPU environment-cubemap readback on every call) that a
	// camera-only update has no reason to redo. Returns false (no-op) if no
	// session is currently running - callers should fall back to a real
	// start() in that case (first tick of a new interactive burst).
	bool updateCamera(const RtCamera& camera);

	// Cancels any in-flight pass and joins the worker thread. Safe to call even if not currently running.
	void stop();

	bool isRunning() const { return _running.load(std::memory_order_acquire); }

	// Cheap progress-polling accessor - see RtPathTracingSession::currentSampleCount()'s identical reasoning.
	uint32_t currentSampleCount() const { return _publishedSampleCount.load(std::memory_order_acquire); }

	// Latest averaged linear-HDR frame available for display - safe to call
	// from any thread while the worker keeps running; returns an empty
	// vector if no chunk has completed yet. outAlpha (if non-null) receives
	// the per-pixel primary-hit fraction (0 = pure background), which
	// RtPresenter alpha-blends so raster's own sharp skybox/gradient shows
	// through behind the path-traced geometry - the same alpha-composited-
	// background contract as RtPathTracingSession::latestFrame(). outCamera
	// (if non-null) receives the exact camera pose this published frame was
	// rendered with; ViewportWidget uses that during interactive PT so the
	// raster skybox can be drawn from the same pose as the last PT chunk
	// instead of from the newer live mouse camera.
	std::vector<glm::vec3> latestFrame(int& outWidth, int& outHeight, uint32_t& outSampleCount,
		std::vector<float>* outAlpha = nullptr, RtCamera* outCamera = nullptr) const;

	// Diagnostics tab accessor (PathTracingDialog) - read-only access to the
	// owned tracer's own diagnostics accessors (hasHardwareRT()/deviceName()/
	// lastGasBuildMs()/lastIasBuildMs()/lastTriangleCount() - see
	// RtOptixSceneTracer.h). No per-call GPU work, safe to poll at UI refresh rate.
	const RtOptixSceneTracer& tracer() const { return _tracer; }

	// Human-readable active denoiser name - mirrors RtDenoiser::activeDeviceName().
	const char* activeDenoiserName() const { return _denoiser.activeDeviceName(); }

	// Current render resolution (Diagnostics tab) - whatever the last
	// setResolution() call set, regardless of whether a session is running.
	int width() const { return _width; }
	int height() const { return _height; }

private:
	void workerLoop(uint64_t myGeneration);

	RtOptixSceneTracer _tracer;
	RtDenoiser _denoiser;
	bool _denoiserEnabled = true;

	int _width  = 0;
	int _height = 0;
	uint32_t _maxSamples = 16;
	uint32_t _maxBounces = 6;
	bool _enableEnvironmentImportanceSampling = true;
	uint32_t _maxTransmissionBounces = 32;
	float _fireflyClampThreshold = 3.0f;
	uint32_t _russianRouletteStartDepth = 3;
	uint32_t _maxVolumeScatterBounces = 64;
	uint32_t _samplesPerChunk = 1;
	uint64_t _builtRevision = 0; // last snapshot->revisionId successfully passed to _tracer.buildScene()
	bool _stayAliveAtConvergence = false;

	mutable std::mutex _snapshotMutex;
	std::shared_ptr<const RtSceneSnapshot> _snapshot;

	std::thread _worker;
	std::atomic<uint64_t> _generation{ 0 }; // bumped every start() call - lets a still-running worker notice it's stale
	std::atomic<bool>     _cancelRequested{ false };
	std::atomic<bool>     _running{ false };
	std::atomic<uint32_t> _publishedSampleCount{ 0 };

	// Camera-only live-update channel - see updateCamera()'s doc comment.
	// Distinct from _generation (which gates a hard start()/stop() respawn):
	// bumping this alone never touches the thread, only tells workerLoop() to
	// swap in a new camera pose and reset its local accumulation in place.
	mutable std::mutex    _cameraMutex;
	RtCamera              _pendingCamera;
	std::atomic<uint64_t> _cameraGeneration{ 0 };

	mutable std::mutex     _publishMutex;
	std::vector<glm::vec3> _publishedFrame;
	std::vector<float>     _publishedAlpha;
	RtCamera               _publishedCamera;
	int _publishedWidth  = 0;
	int _publishedHeight = 0;
};
