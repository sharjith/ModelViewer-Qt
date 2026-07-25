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
// GPU/OptiX-only: this class is now used exclusively for the SETTLED/
// full-quality session - ViewportWidget's interactive (camera-actively-
// moving) GPU path lives entirely in InteractivePtRenderer instead (a
// same-frame, non-blocking-submission renderer with no background worker
// thread at all - see that class's own doc comment). That split existed
// once as two profiles of this SAME class (a reduced-quality "interactive"
// one via setStayAliveAtConvergence(true)/updateCamera(), and the full one
// via a plain start()); the interactive half was retired once
// InteractivePtRenderer was proven, since a worker-thread-publish model
// could never fully avoid the one-tick-behind lag a same-frame renderer
// doesn't have in the first place (see the branch's design notes).
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

	mutable std::mutex     _publishMutex;
	std::vector<glm::vec3> _publishedFrame;
	std::vector<float>     _publishedAlpha;
	RtCamera               _publishedCamera;
	int _publishedWidth  = 0;
	int _publishedHeight = 0;
};
