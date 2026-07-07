#include "RtDenoiser.h"

#include <OpenImageDenoise/oidn.hpp>

#include <QDebug>

#include <algorithm>
#include <cmath>

namespace
{
	// Lightweight edge-aware fallback used whenever OIDN's CPU device isn't
	// available (see RtDenoiser::RtDenoiser()'s warning - this has been
	// observed to fail to initialize on some machines despite the hardware/
	// packaging being fine). A bilateral filter: each output pixel averages
	// its spatial neighborhood weighted both by distance and by how similar
	// the neighbor's color is, so flat noisy regions get smoothed while real
	// edges (geometry silhouettes, material boundaries) are mostly preserved
	// - unlike a plain box/Gaussian blur, which would blur those edges just
	// as much as the noise.
	void bilateralPass(const std::vector<glm::vec3>& input, int width, int height,
		int radius, float spatialSigma, float rangeSigma, std::vector<glm::vec3>& output)
	{
		output.resize(input.size());

		auto luminance = [](const glm::vec3& c) { return glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f)); };
		// Reinhard-style compression before comparing so bright HDR outliers
		// (light sources, sharp specular highlights) don't dominate the range
		// term and wash out the edge-preserving behavior.
		auto compressedLuminance = [&](const glm::vec3& c) { const float l = luminance(c); return l / (1.0f + l); };

		std::vector<float> spatialWeights(static_cast<size_t>(2 * radius + 1) * (2 * radius + 1));
		const int kernelSize = 2 * radius + 1;
		for (int dy = -radius; dy <= radius; ++dy)
			for (int dx = -radius; dx <= radius; ++dx)
				spatialWeights[static_cast<size_t>(dy + radius) * kernelSize + (dx + radius)] =
					std::exp(-static_cast<float>(dx * dx + dy * dy) / (2.0f * spatialSigma * spatialSigma));

		for (int y = 0; y < height; ++y)
		{
			for (int x = 0; x < width; ++x)
			{
				const glm::vec3& center = input[static_cast<size_t>(y) * width + x];
				const float centerLum = compressedLuminance(center);

				glm::vec3 sum(0.0f);
				float weightSum = 0.0f;

				for (int dy = -radius; dy <= radius; ++dy)
				{
					const int sy = std::clamp(y + dy, 0, height - 1);
					for (int dx = -radius; dx <= radius; ++dx)
					{
						const int sx = std::clamp(x + dx, 0, width - 1);
						const glm::vec3& sample = input[static_cast<size_t>(sy) * width + sx];
						const float sampleLum = compressedLuminance(sample);

						const float rangeDiff = centerLum - sampleLum;
						const float rangeWeight = std::exp(-(rangeDiff * rangeDiff) / (2.0f * rangeSigma * rangeSigma));
						const float weight = spatialWeights[static_cast<size_t>(dy + radius) * kernelSize + (dx + radius)] * rangeWeight;

						sum += sample * weight;
						weightSum += weight;
					}
				}

				output[static_cast<size_t>(y) * width + x] = weightSum > 1e-6f ? sum / weightSum : center;
			}
		}
	}

	// RtPathTracingSession::workerLoop() only calls denoise() once now - on
	// the final pass, once RtPathTracingSession::maxSamples() is reached, not
	// on every intermediate pass (see RtPathTracingSession.cpp's finalDenoise
	// flag) - so this always needs to do *some* real smoothing work; unlike
	// an earlier per-pass design, it's never valid to taper down to "skip
	// entirely" here, since this is the only denoise opportunity the final
	// displayed image gets. Still scales mildly with sampleCount, since more
	// accumulated samples genuinely do mean less residual Monte Carlo noise
	// to clean up (real path-tracing noise falls off roughly as 1/sqrt(N)) -
	// but the floor is a light pass, not zero.
	void bilateralFallbackDenoise(const std::vector<glm::vec3>& input, int width, int height,
		std::vector<glm::vec3>& output, uint32_t sampleCount)
	{
		int radius;
		float spatialSigma, rangeSigma;
		if (sampleCount <= 8)       { radius = 2; spatialSigma = 1.5f; rangeSigma = 0.2f; }
		else if (sampleCount <= 32) { radius = 1; spatialSigma = 1.0f; rangeSigma = 0.15f; }
		else                        { radius = 1; spatialSigma = 0.8f; rangeSigma = 0.1f; }

		bilateralPass(input, width, height, radius, spatialSigma, rangeSigma, output);
	}
}

struct RtDenoiser::Impl
{
	oidn::DeviceRef device;
	bool deviceValid = false;
};

RtDenoiser::RtDenoiser() : _impl(std::make_unique<Impl>())
{
	_impl->device = oidn::newDevice(oidn::DeviceType::CPU);
	_impl->device.commit();

	const char* errorMessage = nullptr;
	const oidn::Error err = _impl->device.getError(errorMessage);
	_impl->deviceValid = (err == oidn::Error::None);

	// Falls back to bilateralFallbackDenoise() (see above) whenever the
	// device is invalid, rather than a raw copy - every path-traced frame
	// would otherwise look permanently noisy/grainy on a machine where OIDN
	// fails to initialize (missing runtime dependency, unsupported CPU ISA,
	// driver issue, ...), with no visible indication why beyond this log.
	if (!_impl->deviceValid)
		qWarning() << "RtDenoiser: OIDN CPU device failed to initialize (error"
			<< static_cast<int>(err) << "-" << (errorMessage ? errorMessage : "no message")
			<< ") - path-traced frames will use a lower-quality built-in bilateral fallback denoiser instead.";
}

RtDenoiser::~RtDenoiser() = default;

bool RtDenoiser::denoise(const std::vector<glm::vec3>& input, int width, int height,
	std::vector<glm::vec3>& output, uint32_t sampleCount)
{
	if (width <= 0 || height <= 0 ||
	    input.size() != static_cast<size_t>(width) * static_cast<size_t>(height))
	{
		output = input;
		return false;
	}

	if (!_impl->deviceValid)
	{
		bilateralFallbackDenoise(input, width, height, output, sampleCount);
		return false;
	}

	output.resize(input.size());

	// const_cast is safe here: OIDN's "shared image" overload only reads from
	// the "color" input during execute(), it never writes back into it.
	oidn::FilterRef filter = _impl->device.newFilter("RT");
	filter.setImage("color", const_cast<glm::vec3*>(input.data()), oidn::Format::Float3,
		static_cast<size_t>(width), static_cast<size_t>(height));
	filter.setImage("output", output.data(), oidn::Format::Float3,
		static_cast<size_t>(width), static_cast<size_t>(height));
	filter.set("hdr", true); // renderPass() output is linear HDR, un-tonemapped
	filter.commit();
	filter.execute();

	const char* errorMessage = nullptr;
	const oidn::Error err = _impl->device.getError(errorMessage);
	if (err != oidn::Error::None)
	{
		static bool loggedOnce = false;
		if (!loggedOnce)
		{
			qWarning() << "RtDenoiser: OIDN filter execution failed (error"
				<< static_cast<int>(err) << "-" << (errorMessage ? errorMessage : "no message")
				<< ") - falling back to the built-in bilateral denoiser (logged once).";
			loggedOnce = true;
		}
		bilateralFallbackDenoise(input, width, height, output, sampleCount);
		return false;
	}

	return true;
}
