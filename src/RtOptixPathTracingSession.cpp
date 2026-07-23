#include "RtOptixPathTracingSession.h"

#include <algorithm>
#include <chrono>
#include <thread>

#include <QDebug>

RtOptixPathTracingSession::RtOptixPathTracingSession() = default;

RtOptixPathTracingSession::~RtOptixPathTracingSession()
{
	stop();
}

bool RtOptixPathTracingSession::isAvailable() const
{
	return _tracer.isAvailable();
}

void RtOptixPathTracingSession::setResolution(int width, int height)
{
	_width  = width;
	_height = height;
}

bool RtOptixPathTracingSession::start(std::shared_ptr<const RtSceneSnapshot> snapshot)
{
	stop(); // cancel/join any previously running worker before touching shared state

	if (!snapshot || _width <= 0 || _height <= 0 || !_tracer.isAvailable())
		return false;

	if (snapshot->revisionId != _builtRevision || _builtRevision == 0)
	{
		if (!_tracer.buildScene(*snapshot))
		{
			qWarning() << "RtOptixPathTracingSession::start: buildScene() failed.";
			return false;
		}
		_builtRevision = snapshot->revisionId;
	}

	{
		std::lock_guard<std::mutex> lock(_snapshotMutex);
		_snapshot = snapshot;
	}

	{
		std::lock_guard<std::mutex> lock(_publishMutex);
		_publishedFrame.clear();
		_publishedAlpha.clear();
		_publishedCamera = snapshot->camera;
		_publishedWidth  = 0;
		_publishedHeight = 0;
	}
	_publishedSampleCount.store(0, std::memory_order_release);
	_cameraGeneration.store(0, std::memory_order_release);

	const uint64_t myGeneration = ++_generation;
	_cancelRequested.store(false, std::memory_order_release);
	_running.store(true, std::memory_order_release);
	_worker = std::thread(&RtOptixPathTracingSession::workerLoop, this, myGeneration);
	return true;
}

bool RtOptixPathTracingSession::updateCamera(const RtCamera& camera)
{
	if (!_running.load(std::memory_order_acquire))
		return false;

	{
		std::lock_guard<std::mutex> lock(_cameraMutex);
		_pendingCamera = camera;
	}
	_cameraGeneration.fetch_add(1, std::memory_order_release);
	return true;
}

void RtOptixPathTracingSession::stop()
{
	_cancelRequested.store(true, std::memory_order_release);
	if (_worker.joinable())
		_worker.join();
	_running.store(false, std::memory_order_release);
}

void RtOptixPathTracingSession::workerLoop(uint64_t myGeneration)
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

	const int width  = _width;
	const int height = _height;
	const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);

	// Running means in double precision, accumulated across chunks - avoids
	// the fp32 precision loss a long-running naive running-sum would incur
	// once sampleCount climbs into the hundreds, same concern
	// RtFrameAccumulator's own accumulator addresses on the CPU side. Albedo/
	// normal are OIDN guide buffers, averaged the same way the beauty image
	// is - see RtOptixSceneTracer::renderScene()'s doc comment.
	std::vector<glm::dvec3> runningMean(pixelCount, glm::dvec3(0.0));
	std::vector<glm::dvec3> runningMeanAlbedo(pixelCount, glm::dvec3(0.0));
	std::vector<glm::dvec3> runningMeanNormal(pixelCount, glm::dvec3(0.0));
	std::vector<double>     runningMeanAlpha(pixelCount, 0.0);
	uint32_t sampleCount = 0;

	// activeCamera starts as the snapshot's own camera and can be swapped in
	// place by updateCamera() (see lastSeenCameraGeneration check below) -
	// snapshot->camera itself is never touched again after this point, since
	// the snapshot is shared (const) and may be read by other things.
	RtCamera activeCamera = snapshot->camera;
	uint64_t lastSeenCameraGeneration = _cameraGeneration.load(std::memory_order_acquire);

	while (!_cancelRequested.load(std::memory_order_acquire) &&
	       _generation.load(std::memory_order_acquire) == myGeneration)
	{
		const uint64_t currentCameraGeneration = _cameraGeneration.load(std::memory_order_acquire);
		if (currentCameraGeneration != lastSeenCameraGeneration)
		{
			{
				std::lock_guard<std::mutex> lock(_cameraMutex);
				activeCamera = _pendingCamera;
			}
			lastSeenCameraGeneration = currentCameraGeneration;

			// A new camera pose invalidates every sample gathered so far for
			// the old pose - reset accumulation in place rather than
			// respawning the thread (that respawn, and the caller-side
			// snapshot rebuild it usually comes bundled with, is exactly the
			// per-drag-tick overhead this mechanism exists to avoid).
			std::fill(runningMean.begin(), runningMean.end(), glm::dvec3(0.0));
			std::fill(runningMeanAlbedo.begin(), runningMeanAlbedo.end(), glm::dvec3(0.0));
			std::fill(runningMeanNormal.begin(), runningMeanNormal.end(), glm::dvec3(0.0));
			std::fill(runningMeanAlpha.begin(), runningMeanAlpha.end(), 0.0);
			sampleCount = 0;
			_publishedSampleCount.store(0, std::memory_order_release);
		}

		if (sampleCount >= _maxSamples)
		{
			// Fully converged for the active camera pose. A plain (non-
			// stayAliveAtConvergence) session just exits here, as before -
			// the interactive session instead idles, ready to react the
			// instant updateCamera() bumps _cameraGeneration again.
			if (!_stayAliveAtConvergence)
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
			continue;
		}

		const uint32_t chunkSpp = std::min(_samplesPerChunk, _maxSamples - sampleCount);

		std::vector<glm::vec3> chunkFrame, chunkAlbedo, chunkNormal;
		std::vector<float> chunkAlpha;
		if (!_tracer.renderScene(activeCamera, snapshot->environment, width, height, chunkSpp, sampleCount, _maxBounces,
			snapshot->shadowsEnabled, snapshot->selfShadowsEnabled, _enableEnvironmentImportanceSampling,
			_maxTransmissionBounces, _fireflyClampThreshold, _russianRouletteStartDepth, _maxVolumeScatterBounces,
			chunkFrame, chunkAlbedo, chunkNormal, chunkAlpha))
			break;

		if (_cancelRequested.load(std::memory_order_acquire) ||
		    _generation.load(std::memory_order_acquire) != myGeneration ||
		    chunkFrame.size() != pixelCount)
			break;

		// Incremental running-mean update: newMean = oldMean + (chunkValue -
		// oldMean) * chunkWeight / newTotalCount - equivalent to a plain
		// weighted running sum but numerically steadier over many chunks.
		const uint32_t newSampleCount = sampleCount + chunkSpp;
		const double chunkWeight = static_cast<double>(chunkSpp) / static_cast<double>(newSampleCount);
		for (size_t i = 0; i < pixelCount; ++i)
		{
			runningMean[i]       += (glm::dvec3(chunkFrame[i])  - runningMean[i])       * chunkWeight;
			runningMeanAlbedo[i] += (glm::dvec3(chunkAlbedo[i]) - runningMeanAlbedo[i]) * chunkWeight;
			runningMeanNormal[i] += (glm::dvec3(chunkNormal[i]) - runningMeanNormal[i]) * chunkWeight;
			runningMeanAlpha[i]  += (static_cast<double>(chunkAlpha[i]) - runningMeanAlpha[i]) * chunkWeight;
		}
		sampleCount = newSampleCount;

		std::vector<glm::vec3> resolved(pixelCount);
		std::vector<float> alpha(pixelCount);
		for (size_t i = 0; i < pixelCount; ++i)
		{
			resolved[i] = glm::vec3(runningMean[i]);
			alpha[i] = static_cast<float>(runningMeanAlpha[i]);
		}

		std::vector<glm::vec3> presented = resolved;

		// Only worth denoising once accumulation is done - see this class's
		// own doc comment (mirrors RtPathTracingSession::publishLatest()'s
		// finalDenoise contract exactly).
		if (_denoiserEnabled && sampleCount >= _maxSamples)
		{
			std::vector<glm::vec3> resolvedAlbedo(pixelCount), resolvedNormal(pixelCount);
			for (size_t i = 0; i < pixelCount; ++i)
			{
				resolvedAlbedo[i] = glm::vec3(runningMeanAlbedo[i]);
				resolvedNormal[i] = glm::vec3(runningMeanNormal[i]);
			}
			_denoiser.denoise(resolved, width, height, presented, sampleCount, &resolvedAlbedo, &resolvedNormal);

			// Pure-background pixels (no primary ray ever hit geometry) keep
			// the raw accumulated value - OIDN over-smooths a sharp traced
			// background it has no guide values for; same restoration
			// RtPathTracingSession::publishLatest() does. Mostly invisible
			// here since these pixels are alpha=0 anyway (raster shows
			// through), but keeps the published frame itself sane.
			for (size_t i = 0; i < pixelCount; ++i)
				if (alpha[i] <= 0.0f)
					presented[i] = resolved[i];
		}

		{
			std::lock_guard<std::mutex> lock(_publishMutex);
			_publishedFrame  = std::move(presented);
			_publishedAlpha  = std::move(alpha);
			_publishedCamera = activeCamera;
			_publishedWidth  = width;
			_publishedHeight = height;
		}
		_publishedSampleCount.store(sampleCount, std::memory_order_release);
	}

	_running.store(false, std::memory_order_release);
}

std::vector<glm::vec3> RtOptixPathTracingSession::latestFrame(int& outWidth, int& outHeight, uint32_t& outSampleCount,
	std::vector<float>* outAlpha, RtCamera* outCamera) const
{
	std::lock_guard<std::mutex> lock(_publishMutex);
	outWidth       = _publishedWidth;
	outHeight      = _publishedHeight;
	outSampleCount = _publishedSampleCount.load(std::memory_order_acquire);
	if (outAlpha)
		*outAlpha = _publishedAlpha;
	if (outCamera)
		*outCamera = _publishedCamera;
	return _publishedFrame;
}
