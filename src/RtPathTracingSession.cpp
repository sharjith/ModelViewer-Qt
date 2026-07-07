#include "RtPathTracingSession.h"

#include <algorithm>

RtPathTracingSession::RtPathTracingSession() = default;

RtPathTracingSession::~RtPathTracingSession()
{
	stop();
}

void RtPathTracingSession::setResolution(int width, int height)
{
	_width  = width;
	_height = height;
}

void RtPathTracingSession::resetForNewPass(std::shared_ptr<const RtSceneSnapshot> snapshot, bool rebuildEmbreeScene)
{
	stop(); // cancel/join any previously running worker before touching shared state

	{
		std::lock_guard<std::mutex> lock(_snapshotMutex);
		_snapshot = snapshot;
	}

	if (!snapshot || _width <= 0 || _height <= 0)
		return;

	if (rebuildEmbreeScene)
		_embreeScene.build(snapshot);

	_accumulator.resize(_width, _height);
	_accumulator.reset();

	{
		std::lock_guard<std::mutex> lock(_publishMutex);
		_publishedFrame.clear();
		_publishedAlpha.clear();
		_publishedWidth       = 0;
		_publishedHeight      = 0;
		_publishedSampleCount = 0;
	}

	const uint64_t myRevision = ++_activeRevision;
	_cancelRequested.store(false, std::memory_order_release);
	_running.store(true, std::memory_order_release);
	_worker = std::thread(&RtPathTracingSession::workerLoop, this, myRevision);
}

void RtPathTracingSession::start(std::shared_ptr<const RtSceneSnapshot> snapshot)
{
	resetForNewPass(std::move(snapshot), /*rebuildEmbreeScene=*/true);
}

void RtPathTracingSession::notifyCameraChanged(std::shared_ptr<const RtSceneSnapshot> snapshotWithNewCamera)
{
	resetForNewPass(std::move(snapshotWithNewCamera), /*rebuildEmbreeScene=*/false);
}

void RtPathTracingSession::stop()
{
	_cancelRequested.store(true, std::memory_order_release);
	if (_worker.joinable())
		_worker.join();
	_running.store(false, std::memory_order_release);
}

void RtPathTracingSession::workerLoop(uint64_t myRevision)
{
	std::shared_ptr<const RtSceneSnapshot> snapshot;
	{
		std::lock_guard<std::mutex> lock(_snapshotMutex);
		snapshot = _snapshot;
	}
	if (!snapshot)
	{
		_running.store(false, std::memory_order_release);
		return;
	}

	uint32_t sampleSeed = 0;
	while (!_cancelRequested.load(std::memory_order_acquire) &&
	       _activeRevision.load(std::memory_order_acquire) == myRevision &&
	       _accumulator.sampleCount() < _maxSamples)
	{
		std::vector<glm::vec3> passResult;
		std::vector<uint8_t> hitMask;
		_tracer.renderPass(_embreeScene, *snapshot, _width, _height, sampleSeed++, passResult, &_cancelRequested, &hitMask);

		// Don't accumulate/publish a pass that was cancelled or superseded
		// while it was running - the result may not even match the current
		// resolution/scene anymore.
		if (_cancelRequested.load(std::memory_order_acquire) ||
		    _activeRevision.load(std::memory_order_acquire) != myRevision)
			break;

		_accumulator.accumulate(passResult, &hitMask);
		publishLatest(_accumulator.sampleCount() >= _maxSamples);
	}

	_running.store(false, std::memory_order_release);
}

void RtPathTracingSession::publishLatest(bool finalDenoise)
{
	std::vector<glm::vec3> resolved = _accumulator.resolve();
	const int width  = _accumulator.width();
	const int height = _accumulator.height();
	const uint32_t sampleCount = _accumulator.sampleCount();

	std::vector<glm::vec3> presented = resolved;
	if (finalDenoise)
		_denoiser.denoise(resolved, width, height, presented, sampleCount);

	// OIDN's beauty-only filter (no albedo/normal guide buffers - see
	// RtDenoiser) has no way to tell a sharp environment-map background apart
	// from noise, and over-smooths it into something that looks like a
	// blurred irradiance/prefilter map. Background pixels don't need
	// denoising anyway - sampleEnvironmentMiss() is a deterministic texture
	// lookup that only varies (very slightly, from AA jitter) across passes,
	// so the raw accumulated average is already clean. Restore it wherever a
	// pixel's primary ray has never once hit geometry.
	const std::vector<uint32_t>& hitCounts = _accumulator.hitCounts();
	std::vector<float> alpha(presented.size(), 1.0f);
	if (hitCounts.size() == presented.size())
	{
		for (size_t i = 0; i < presented.size(); ++i)
		{
			if (hitCounts[i] == 0)
				presented[i] = resolved[i];
			alpha[i] = sampleCount > 0
				? std::clamp(static_cast<float>(hitCounts[i]) / static_cast<float>(sampleCount), 0.0f, 1.0f)
				: 0.0f;
		}
	}

	std::lock_guard<std::mutex> lock(_publishMutex);
	_publishedFrame       = std::move(presented);
	_publishedAlpha       = std::move(alpha);
	_publishedWidth       = width;
	_publishedHeight      = height;
	_publishedSampleCount = sampleCount;
}

std::vector<glm::vec3> RtPathTracingSession::latestFrame(int& outWidth, int& outHeight, uint32_t& outSampleCount,
	std::vector<float>* outAlpha) const
{
	std::lock_guard<std::mutex> lock(_publishMutex);
	outWidth       = _publishedWidth;
	outHeight      = _publishedHeight;
	outSampleCount = _publishedSampleCount;
	if (outAlpha)
		*outAlpha = _publishedAlpha;
	return _publishedFrame;
}
