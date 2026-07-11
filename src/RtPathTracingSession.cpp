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
	{
		_embreeScene.build(snapshot);
		// Environment content only ever changes alongside a full scene
		// rebuild (skybox/HDRI load, not camera movement) - tied to the same
		// condition as the Embree BVH rebuild above rather than rebuilt
		// every pass, since building the importance-sampling CDF walks
		// every texel of every cubemap face.
		_envSampler.build(snapshot->environment);
	}

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
		std::vector<glm::vec3> albedoResult, normalResult;
		std::vector<uint8_t> clearTransmissionMask;
		_tracer.renderPass(_embreeScene, *snapshot, _envSampler, _width, _height, sampleSeed++, passResult,
			&_cancelRequested, &hitMask, &albedoResult, &normalResult, &clearTransmissionMask);

		// Don't accumulate/publish a pass that was cancelled or superseded
		// while it was running - the result may not even match the current
		// resolution/scene anymore.
		if (_cancelRequested.load(std::memory_order_acquire) ||
		    _activeRevision.load(std::memory_order_acquire) != myRevision)
			break;

		_accumulator.accumulate(passResult, &hitMask, &albedoResult, &normalResult, &clearTransmissionMask);
		publishLatest(_denoiserEnabled && _accumulator.sampleCount() >= _maxSamples);
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
	{
		// See RtDenoiser::denoise()'s doc comment and CpuPathTracer's
		// outPrimaryAlbedo/outPrimaryNormal - feeding OIDN these guide
		// buffers (rather than running beauty-only) is what lets it
		// distinguish real detail seen through/behind a transmissive
		// surface from noise, instead of over-smoothing it toward the
		// glass's own flat tint.
		const std::vector<glm::vec3> albedo = _accumulator.resolveAlbedo();
		const std::vector<glm::vec3> normal = _accumulator.resolveNormal();
		_denoiser.denoise(resolved, width, height, presented, sampleCount, &albedo, &normal);
	}

	// OIDN's guide buffers (albedo/normal - see above) still can't tell a
	// sharp environment-map background apart from noise when the primary ray
	// never hit any geometry at all (there's no material to derive a guide
	// value from), and over-smooths it into something that looks like a
	// blurred irradiance/prefilter map in that case. Background pixels don't
	// need denoising anyway - sampleEnvironmentMiss() is a deterministic
	// texture lookup that only varies (very slightly, from AA jitter) across
	// passes, so the raw accumulated average is already clean. Restore it
	// wherever a pixel's primary ray has never once hit geometry.
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

	// Same rationale as above, for a narrower category: a primary hit on a
	// "clear transmission" surface (see CpuPathTracer's kClearTransmission*
	// thresholds - strongly transmissive, low roughness, no volume/
	// clearcoat/sheen/iridescence, e.g. plain window glass) also zeroes
	// OIDN's guide buffers (there's no meaningful albedo/normal to give it
	// for what's actually visible - the scene behind the glass), so it's
	// just as prone to over-smoothing the sharp detail seen through it.
	// Unlike a pure background miss, these pixels aren't perfectly noise-free
	// (the Fresnel-weighted reflect/transmit pick still varies per sample),
	// so this only restores raw when clear-transmission samples clearly
	// dominate the pixel's history - not merely nonzero - to avoid flickery
	// behavior on silhouette/AA-edge pixels that mix hit categories.
	constexpr float kClearTransmissionDominanceRatio = 0.5f;
	const std::vector<uint32_t>& clearTransmissionCounts = _accumulator.clearTransmissionCounts();
	if (clearTransmissionCounts.size() == presented.size() && sampleCount > 0)
	{
		for (size_t i = 0; i < presented.size(); ++i)
		{
			if (static_cast<float>(clearTransmissionCounts[i]) > static_cast<float>(sampleCount) * kClearTransmissionDominanceRatio)
				presented[i] = resolved[i];
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
