#include "RtDenoiser.h"

#include <OpenImageDenoise/oidn.hpp>

#include <QDebug>
#include <QElapsedTimer>
#include <QString>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

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
	DenoiserDevicePreference preference = DenoiserDevicePreference::Auto;

	// CPU device memory is directly host-accessible, so denoise() can wrap
	// the caller's std::vector storage in-place via OIDN's "shared buffer"
	// API (oidnNewSharedBuffer, zero extra copies). Any other device type
	// (currently just CUDA) can NOT safely do that - a plain std::vector's
	// heap pointer is ordinary pageable host memory, not something a CUDA
	// kernel can dereference - so those need OIDN's own device-allocated
	// buffers plus explicit write()/read() transfers instead. See denoise().
	bool isSharedMemoryDevice = true;
};

namespace
{
	// Tries newDevice(type) + commit(); returns true (and leaves the device
	// in *outDevice) only if it actually initialized without error. Always
	// leaves *outDevice in a well-defined state (either the committed device
	// or a fresh empty DeviceRef) so callers can just try the next candidate.
	// outFailureReason, if non-null, is filled in on failure - copied to a
	// std::string rather than returning OIDN's const char* directly, since
	// that pointer's lifetime is tied to the device object, which is about
	// to be destroyed (never outlives this function) on the failure path.
	bool tryInitDevice(oidn::DeviceType type, oidn::DeviceRef& outDevice, std::string* outFailureReason = nullptr)
	{
		oidn::DeviceRef device = oidn::newDevice(type);
		if (!device)
		{
			if (outFailureReason)
				*outFailureReason = "oidnNewDevice returned a null handle (device type unsupported/unavailable in this OIDN build or on this system)";
			return false;
		}
		device.commit();
		const char* errorMessage = nullptr;
		const oidn::Error err = device.getError(errorMessage);
		if (err != oidn::Error::None)
		{
			if (outFailureReason)
				*outFailureReason = "error " + std::to_string(static_cast<int>(err)) + " - " +
					(errorMessage ? errorMessage : "no message");
			return false;
		}
		outDevice = std::move(device);
		return true;
	}
}

RtDenoiser::RtDenoiser(DenoiserDevicePreference preference) : _impl(std::make_unique<Impl>())
{
	_impl->preference = preference;
	initializeDevice();
}

RtDenoiser::~RtDenoiser() = default;

void RtDenoiser::initializeDevice()
{
	// Release whatever device this instance might already hold (a no-op the
	// first time, from the constructor) - lets setDevicePreference() just
	// call this again to switch devices mid-session.
	_impl->device = oidn::DeviceRef();
	_impl->deviceValid = false;

	// CPU-only: skip CUDA entirely, matching DenoiserDevicePreference::CPU's
	// contract.
	if (_impl->preference == DenoiserDevicePreference::CPU)
	{
		if (tryInitDevice(oidn::DeviceType::CPU, _impl->device))
		{
			_impl->deviceValid = true;
			_impl->isSharedMemoryDevice = true;
		}
	}
	// GPU-only: only attempt CUDA - deliberately no CPU fallback here, see
	// DenoiserDevicePreference::GPU's doc comment.
	else if (_impl->preference == DenoiserDevicePreference::GPU)
	{
		std::string cudaFailureReason;
		if (tryInitDevice(oidn::DeviceType::CUDA, _impl->device, &cudaFailureReason))
		{
			_impl->deviceValid = true;
			_impl->isSharedMemoryDevice = false;
		}
		else
		{
			qWarning().noquote() << "RtDenoiser: GPU denoiser device requested but OIDN CUDA device unavailable ("
				<< QString::fromStdString(cudaFailureReason) << ") - not falling back to CPU (GPU was explicitly"
				<< " requested); path-traced frames will use the built-in bilateral fallback denoiser instead.";
		}
	}
	// Auto (default): try CUDA first - a plain denoising speedup on an
	// NVIDIA GPU, entirely independent of this app's CPU Embree ray tracing.
	// Requires only a working NVIDIA driver at runtime, not the CUDA Toolkit
	// (see the vcpkg overlay port comment) - falls through to CPU (logging
	// why) on any machine without a suitable NVIDIA GPU/driver, exactly like
	// OIDN's own documented device-selection guidance.
	else
	{
		std::string cudaFailureReason;
		if (tryInitDevice(oidn::DeviceType::CUDA, _impl->device, &cudaFailureReason))
		{
			_impl->deviceValid = true;
			_impl->isSharedMemoryDevice = false;
		}
		else
		{
			qInfo().noquote() << "RtDenoiser: OIDN CUDA device unavailable (" << QString::fromStdString(cudaFailureReason)
				<< ") - trying the CPU device instead.";
			if (tryInitDevice(oidn::DeviceType::CPU, _impl->device))
			{
				_impl->deviceValid = true;
				_impl->isSharedMemoryDevice = true;
			}
		}
	}

	// Falls back to bilateralFallbackDenoise() (see above) whenever neither
	// device is valid, rather than a raw copy - every path-traced frame
	// would otherwise look permanently noisy/grainy on a machine where OIDN
	// fails to initialize (missing runtime dependency, unsupported CPU ISA,
	// driver issue, ...), with no visible indication why beyond this log.
	if (!_impl->deviceValid)
	{
		if (_impl->preference != DenoiserDevicePreference::GPU) // GPU's own branch above already logged why
			qWarning() << "RtDenoiser: OIDN failed to initialize on the requested device(s)"
				<< "- path-traced frames will use a lower-quality built-in bilateral fallback denoiser instead.";
	}
	else
		qInfo() << "RtDenoiser: using OIDN" << activeDeviceName() << "device for path-traced frame denoising.";
}

void RtDenoiser::setDevicePreference(DenoiserDevicePreference preference)
{
	if (_impl->preference == preference)
		return;
	_impl->preference = preference;
	initializeDevice();
}

DenoiserDevicePreference RtDenoiser::devicePreference() const
{
	return _impl->preference;
}

const char* RtDenoiser::activeDeviceName() const
{
	if (!_impl->deviceValid)
		return "none (bilateral fallback)";
	return _impl->isSharedMemoryDevice ? "CPU" : "CUDA";
}

bool RtDenoiser::denoise(const std::vector<glm::vec3>& input, int width, int height,
	std::vector<glm::vec3>& output, uint32_t sampleCount,
	const std::vector<glm::vec3>* albedo, const std::vector<glm::vec3>* normal)
{
	if (width <= 0 || height <= 0 ||
	    input.size() != static_cast<size_t>(width) * static_cast<size_t>(height))
	{
		output = input;
		return false;
	}

	if (!_impl->deviceValid)
	{
		QElapsedTimer timer;
		timer.start();
		bilateralFallbackDenoise(input, width, height, output, sampleCount);
		qInfo() << "RtDenoiser: bilateral fallback denoise of" << width << "x" << height
			<< "took" << timer.elapsed() << "ms (no OIDN device available)";
		return false;
	}

	QElapsedTimer timer;
	timer.start();

	output.resize(input.size());

	const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
	const size_t byteSize = pixelCount * sizeof(glm::vec3);
	const bool haveGuides = albedo && normal &&
		albedo->size() == pixelCount && normal->size() == pixelCount;

	oidn::FilterRef filter = _impl->device.newFilter("RT");

	// Only used on the CUDA (non-shared-memory) path below; empty/unused
	// BufferRefs are harmless and just fall out of scope after execute().
	oidn::BufferRef colorBuffer, albedoBuffer, normalBuffer, outputBuffer;

	if (_impl->isSharedMemoryDevice)
	{
		// CPU device: host memory IS the device's own memory, so wrap the
		// caller's existing storage in place - zero extra copies. const_cast
		// is safe here: OIDN's "shared image" overload only reads from the
		// "color"/"albedo"/"normal" inputs during execute(), it never writes
		// back into them.
		filter.setImage("color", const_cast<glm::vec3*>(input.data()), oidn::Format::Float3,
			static_cast<size_t>(width), static_cast<size_t>(height));
		if (haveGuides)
		{
			filter.setImage("albedo", const_cast<glm::vec3*>(albedo->data()), oidn::Format::Float3,
				static_cast<size_t>(width), static_cast<size_t>(height));
			filter.setImage("normal", const_cast<glm::vec3*>(normal->data()), oidn::Format::Float3,
				static_cast<size_t>(width), static_cast<size_t>(height));
		}
		filter.setImage("output", output.data(), oidn::Format::Float3,
			static_cast<size_t>(width), static_cast<size_t>(height));
	}
	else
	{
		// CUDA (or any other non-shared-memory device): a std::vector's
		// pointer is ordinary pageable host memory, which a CUDA kernel
		// cannot dereference directly - wrapping it via the shared-image
		// path above would be wrong on this device. Instead allocate OIDN's
		// own host-and-device-accessible buffers (newBuffer(byteSize) - see
		// its doc comment in oidn.hpp) and explicitly transfer data with
		// write()/read(), OIDN's documented portable mechanism for this
		// (a plain memcpy on a CPU-class device, a real host<->VRAM copy on
		// CUDA).
		colorBuffer = _impl->device.newBuffer(byteSize);
		colorBuffer.write(0, byteSize, input.data());
		filter.setImage("color", colorBuffer, oidn::Format::Float3,
			static_cast<size_t>(width), static_cast<size_t>(height));

		if (haveGuides)
		{
			albedoBuffer = _impl->device.newBuffer(byteSize);
			albedoBuffer.write(0, byteSize, albedo->data());
			filter.setImage("albedo", albedoBuffer, oidn::Format::Float3,
				static_cast<size_t>(width), static_cast<size_t>(height));

			normalBuffer = _impl->device.newBuffer(byteSize);
			normalBuffer.write(0, byteSize, normal->data());
			filter.setImage("normal", normalBuffer, oidn::Format::Float3,
				static_cast<size_t>(width), static_cast<size_t>(height));
		}

		outputBuffer = _impl->device.newBuffer(byteSize);
		filter.setImage("output", outputBuffer, oidn::Format::Float3,
			static_cast<size_t>(width), static_cast<size_t>(height));
	}

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
			qWarning() << "RtDenoiser: OIDN filter execution failed on the" << activeDeviceName() << "device (error"
				<< static_cast<int>(err) << "-" << (errorMessage ? errorMessage : "no message")
				<< ") - falling back to the built-in bilateral denoiser (logged once).";
			loggedOnce = true;
		}
		bilateralFallbackDenoise(input, width, height, output, sampleCount);
		return false;
	}

	if (!_impl->isSharedMemoryDevice)
		outputBuffer.read(0, byteSize, output.data());

	qInfo() << "RtDenoiser: OIDN" << activeDeviceName() << "denoise of" << width << "x" << height
		<< "(sampleCount" << sampleCount << ", guides" << (haveGuides ? "yes" : "no") << ") took"
		<< timer.elapsed() << "ms";

	return true;
}
