#include "RtDenoiser.h"

#include <OpenImageDenoise/oidn.hpp>

#ifdef MODELVIEWER_HAVE_OPTIX
#include <cuda_runtime.h>
#include <optix.h>
#include <optix_stubs.h>
// NOTE: optix_function_table_definition.h must be included in exactly one
// translation unit across the whole program - that's RtOptixContext.cpp, not
// this file.
#endif

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

	// Set only when preference == OptiX and a native OptiX denoiser context
	// was successfully created (see initializeDevice()'s OptiX branch) -
	// denoise() branches to denoiseWithOptix() instead of the OIDN filter
	// path above when this is true. This is a genuinely different API
	// (optixDenoiserInvoke()) from Intel's OIDN above, not just another OIDN
	// device type, so it's tracked independently of deviceValid/
	// isSharedMemoryDevice rather than folded into OIDN's device selection.
	bool useNativeOptixDenoiser = false;
#ifdef MODELVIEWER_HAVE_OPTIX
	// Created once in initializeDevice() and held for this Impl's lifetime -
	// only the per-call OptixDenoiser handle (sized to that call's
	// width/height) is created/destroyed fresh every denoise() call, mirroring
	// how the OIDN path above also creates its oidn::FilterRef fresh each
	// call rather than caching it. Deliberately an independent
	// OptixDeviceContext rather than sharing RtOptixSceneTracer's - matches
	// how RtOptixSceneTracer itself already creates its own context rather
	// than reusing RtOptixContext's; each GPU-facing piece in this codebase
	// owns its own CUDA/OptiX device state.
	OptixDeviceContext optixContext = nullptr;

	// Cached device-resident denoiser handle + its state/scratch buffers -
	// see denoiseDeviceWithOptix()'s doc comment for why this path (unlike
	// the host-vector denoiseWithOptix() above, called once per full render)
	// must NOT create/destroy a fresh OptixDenoiser on every call: it's
	// invoked on every interactive tick, and both the repeated
	// optixDenoiserCreate()/optixDenoiserSetup() GPU work AND OptiX's own
	// internal log callback (the DISKCACHE/DENOISER lines) fire every single
	// time otherwise, flooding the log during any drag. Rebuilt only when
	// resolution or guide-buffer availability actually changes (see
	// deviceDenoiserWidth/Height/HasGuides below).
	OptixDenoiser deviceDenoiser = nullptr;
	CUdeviceptr deviceDenoiserState = 0, deviceDenoiserScratch = 0, deviceDenoiserIntensity = 0;
	size_t deviceDenoiserStateSize = 0, deviceDenoiserScratchSize = 0;
	int deviceDenoiserWidth = 0, deviceDenoiserHeight = 0;
	bool deviceDenoiserHasGuides = false;

	void releaseDeviceDenoiser()
	{
		if (deviceDenoiser)
		{
			optixDenoiserDestroy(deviceDenoiser);
			deviceDenoiser = nullptr;
		}
		if (deviceDenoiserState)    { cudaFree(reinterpret_cast<void*>(deviceDenoiserState));    deviceDenoiserState = 0; }
		if (deviceDenoiserScratch)  { cudaFree(reinterpret_cast<void*>(deviceDenoiserScratch));  deviceDenoiserScratch = 0; }
		if (deviceDenoiserIntensity){ cudaFree(reinterpret_cast<void*>(deviceDenoiserIntensity)); deviceDenoiserIntensity = 0; }
		deviceDenoiserStateSize = 0;
		deviceDenoiserScratchSize = 0;
		deviceDenoiserWidth = 0;
		deviceDenoiserHeight = 0;
		deviceDenoiserHasGuides = false;
	}

	~Impl()
	{
		releaseDeviceDenoiser();
		if (optixContext)
			optixDeviceContextDestroy(optixContext);
	}
#endif
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

#ifdef MODELVIEWER_HAVE_OPTIX
	bool cudaOk(cudaError_t err, const char* what)
	{
		if (err != cudaSuccess)
		{
			qWarning() << "RtDenoiser: CUDA call failed (" << what << "):" << cudaGetErrorString(err);
			return false;
		}
		return true;
	}

	bool optixOk(OptixResult result, const char* what)
	{
		if (result != OPTIX_SUCCESS)
		{
			qWarning() << "RtDenoiser: OptiX call failed (" << what << "), result" << static_cast<int>(result);
			return false;
		}
		return true;
	}

	void optixDenoiserLogCallback(unsigned int level, const char* tag, const char* message, void* /*cbdata*/)
	{
		const QString text = QString("RtDenoiser: OptiX [%1][%2]: %3").arg(level).arg(tag).arg(message);
		if (level <= 2)
			qWarning().noquote() << text;
		else
			qInfo().noquote() << text;
	}

	// Mirrors RtOptixContext.cpp's constructor exactly (cudaFree(0) to force
	// CUDA's primary context into existence, optixInit(), then
	// optixDeviceContextCreate() with CUcontext(0) meaning "use CUDA's
	// current context") - see RtDenoiser::Impl::optixContext's doc comment
	// for why this doesn't just reuse RtOptixContext/RtOptixSceneTracer's own
	// context instead.
	bool initializeOptixDenoiserContext(OptixDeviceContext& outContext)
	{
		if (!cudaOk(cudaFree(0), "cudaFree(0)"))
			return false;

		if (!optixOk(optixInit(), "optixInit()"))
			return false;

		OptixDeviceContextOptions options{};
		options.logCallbackFunction = &optixDenoiserLogCallback;
		options.logCallbackLevel = 4;
#ifndef NDEBUG
		options.validationMode = OPTIX_DEVICE_CONTEXT_VALIDATION_MODE_ALL;
#endif
		return optixOk(optixDeviceContextCreate(nullptr, &options, &outContext), "optixDeviceContextCreate()");
	}

	OptixImage2D makeOptixImage(CUdeviceptr data, int width, int height)
	{
		OptixImage2D image{};
		image.data = data;
		image.width = static_cast<unsigned int>(width);
		image.height = static_cast<unsigned int>(height);
		image.rowStrideInBytes = static_cast<unsigned int>(width) * sizeof(float3);
		image.pixelStrideInBytes = sizeof(float3);
		image.format = OPTIX_PIXEL_FORMAT_FLOAT3;
		return image;
	}

#endif // MODELVIEWER_HAVE_OPTIX
}

#ifdef MODELVIEWER_HAVE_OPTIX
// Runs one full create -> computeMemoryResources -> setup -> (optional
// computeIntensity) -> invoke sequence and reads the result back to
// 'output', then tears every per-call OptiX/CUDA resource back down before
// returning - mirrors this file's OIDN path, which also creates/destroys its
// oidn::FilterRef fresh every call rather than caching it. Only
// Impl::optixContext itself is persisted across calls (see its doc comment).
// Returns false (leaving 'output' untouched) on any failure - denoise() falls
// back to the bilateral filter in that case, same as an OIDN filter-execution
// failure. A private member function (rather than a free function) so it can
// reach _impl->optixContext without exposing the private Impl type outside
// this class - see RtDenoiser.h's doc comment on this method.
bool RtDenoiser::denoiseWithOptix(const std::vector<glm::vec3>& input, int width, int height,
	std::vector<glm::vec3>& output, const std::vector<glm::vec3>* albedo, const std::vector<glm::vec3>* normal)
{
	// CUDA's "current context" is per-OS-thread state, but this function can
	// be called from any of several different threads depending on which
	// engine is active: the main/GUI thread (offline render path), the GPU
	// (OptiX) path tracer's own worker thread, or the CPU (Embree) path
	// tracer's worker thread. _impl->optixContext was created once, on
	// whichever thread happened to call setDevicePreference()/initializeDevice()
	// (typically the main thread) - a later call from a DIFFERENT thread that
	// has never made any CUDA call before has no context attached to it at
	// all yet, and optixDenoiserCreate() below fails immediately with
	// CUDA_ERROR_INVALID_CONTEXT (201) in that case. The GPU engine's worker
	// thread happens to dodge this by accident (RtOptixSceneTracer::renderScene()
	// already made plenty of its own CUDA calls on that same thread earlier in
	// the same iteration, which attaches a context as a side effect) - the CPU
	// engine's worker thread never touches CUDA otherwise, so this was the
	// first CUDA call on that thread and always failed. cudaFree(0) is this
	// codebase's established idiom (see RtOptixContext.cpp/RtOptixSceneTracer.cpp)
	// for forcing the primary context to exist AND be current on the CALLING
	// thread - cheap/idempotent if already attached, so safe to call
	// unconditionally on every invocation regardless of which thread this is.
	if (!cudaOk(cudaFree(0), "cudaFree(0)"))
		return false;

	const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
	const size_t byteSize = pixelCount * sizeof(float3);
	const bool haveGuides = albedo && normal && albedo->size() == pixelCount && normal->size() == pixelCount;

	OptixDenoiserOptions denoiserOptions{};
	denoiserOptions.guideAlbedo = haveGuides ? 1u : 0u;
	denoiserOptions.guideNormal = haveGuides ? 1u : 0u;
#if OPTIX_VERSION >= 80000
	// denoiseAlpha moved from OptixDenoiserParams (SDK 7.x, set on 'params'
	// below instead) to OptixDenoiserOptions somewhere around SDK 8.0 - we
	// don't feed alpha data through this denoiser at all (float3 RGB only),
	// so COPY (the default/no-op mode, value 0 in both SDK generations) is
	// correct either way; this just satisfies whichever struct actually
	// declares the field for the SDK this build was configured against.
	denoiserOptions.denoiseAlpha = OPTIX_DENOISER_ALPHA_MODE_COPY;
#endif

	OptixDenoiser denoiser = nullptr;
	bool ok = optixOk(optixDenoiserCreate(_impl->optixContext, OPTIX_DENOISER_MODEL_KIND_HDR, &denoiserOptions, &denoiser),
		"optixDenoiserCreate()");

	OptixDenoiserSizes sizes{};
	if (ok)
		ok = optixOk(optixDenoiserComputeMemoryResources(denoiser, static_cast<unsigned int>(width),
			static_cast<unsigned int>(height), &sizes), "optixDenoiserComputeMemoryResources()");

	CUdeviceptr state = 0, scratch = 0, colorDevice = 0, albedoDevice = 0, normalDevice = 0;
	CUdeviceptr outputDevice = 0, intensityDevice = 0;

	if (ok) ok = cudaOk(cudaMalloc(reinterpret_cast<void**>(&state), sizes.stateSizeInBytes), "cudaMalloc(optix denoiser state)");
	if (ok) ok = cudaOk(cudaMalloc(reinterpret_cast<void**>(&scratch), sizes.withoutOverlapScratchSizeInBytes), "cudaMalloc(optix denoiser scratch)");
	if (ok) ok = cudaOk(cudaMalloc(reinterpret_cast<void**>(&colorDevice), byteSize), "cudaMalloc(optix denoiser color)");
	if (ok) ok = cudaOk(cudaMemcpy(reinterpret_cast<void*>(colorDevice), input.data(), byteSize, cudaMemcpyHostToDevice), "cudaMemcpy(optix denoiser color)");
	if (ok) ok = cudaOk(cudaMalloc(reinterpret_cast<void**>(&outputDevice), byteSize), "cudaMalloc(optix denoiser output)");
	if (ok) ok = cudaOk(cudaMalloc(reinterpret_cast<void**>(&intensityDevice), sizeof(float)), "cudaMalloc(optix denoiser intensity)");
	if (ok && haveGuides)
	{
		ok = cudaOk(cudaMalloc(reinterpret_cast<void**>(&albedoDevice), byteSize), "cudaMalloc(optix denoiser albedo)");
		if (ok) ok = cudaOk(cudaMemcpy(reinterpret_cast<void*>(albedoDevice), albedo->data(), byteSize, cudaMemcpyHostToDevice), "cudaMemcpy(optix denoiser albedo)");
		if (ok) ok = cudaOk(cudaMalloc(reinterpret_cast<void**>(&normalDevice), byteSize), "cudaMalloc(optix denoiser normal)");
		if (ok) ok = cudaOk(cudaMemcpy(reinterpret_cast<void*>(normalDevice), normal->data(), byteSize, cudaMemcpyHostToDevice), "cudaMemcpy(optix denoiser normal)");
	}

	const OptixImage2D colorImage = makeOptixImage(colorDevice, width, height);
	const OptixImage2D outputImage = makeOptixImage(outputDevice, width, height);

	if (ok)
		ok = optixOk(optixDenoiserSetup(denoiser, nullptr, static_cast<unsigned int>(width), static_cast<unsigned int>(height),
			state, sizes.stateSizeInBytes, scratch, sizes.withoutOverlapScratchSizeInBytes), "optixDenoiserSetup()");

	if (ok)
		ok = optixOk(optixDenoiserComputeIntensity(denoiser, nullptr, &colorImage, intensityDevice, scratch,
			sizes.withoutOverlapScratchSizeInBytes), "optixDenoiserComputeIntensity()");

	OptixDenoiserGuideLayer guideLayer{};
	if (haveGuides)
	{
		guideLayer.albedo = makeOptixImage(albedoDevice, width, height);
		guideLayer.normal = makeOptixImage(normalDevice, width, height);
	}

	OptixDenoiserLayer layer{};
	layer.input = colorImage;
	layer.output = outputImage;

	OptixDenoiserParams params{};
#if OPTIX_VERSION < 80000
	params.denoiseAlpha = OPTIX_DENOISER_ALPHA_MODE_COPY; // see denoiserOptions.denoiseAlpha's doc comment above
#endif
	params.hdrIntensity = intensityDevice;
	params.blendFactor = 0.0f;

	if (ok)
		ok = optixOk(optixDenoiserInvoke(denoiser, nullptr, &params, state, sizes.stateSizeInBytes, &guideLayer,
			&layer, 1, 0, 0, scratch, sizes.withoutOverlapScratchSizeInBytes), "optixDenoiserInvoke()");

	if (ok)
	{
		output.resize(pixelCount);
		ok = cudaOk(cudaMemcpy(output.data(), reinterpret_cast<void*>(outputDevice), byteSize, cudaMemcpyDeviceToHost),
			"cudaMemcpy(optix denoiser readback)");
	}

	if (denoiser)
		optixDenoiserDestroy(denoiser);
	if (state) cudaFree(reinterpret_cast<void*>(state));
	if (scratch) cudaFree(reinterpret_cast<void*>(scratch));
	if (colorDevice) cudaFree(reinterpret_cast<void*>(colorDevice));
	if (outputDevice) cudaFree(reinterpret_cast<void*>(outputDevice));
	if (intensityDevice) cudaFree(reinterpret_cast<void*>(intensityDevice));
	if (albedoDevice) cudaFree(reinterpret_cast<void*>(albedoDevice));
	if (normalDevice) cudaFree(reinterpret_cast<void*>(normalDevice));

	return ok;
}

// Device-resident sibling of denoiseWithOptix() above - conceptually the same
// create -> computeMemoryResources -> setup -> computeIntensity -> invoke
// sequence, but color/albedo/normal/output all point at CALLER-owned device
// memory directly instead of this function's own cudaMalloc'd + cudaMemcpy'd
// staging buffers, so there is no host round-trip and no color/albedo/normal
// copy at all (colorImage/outputImage alias the caller's own persistent
// accumulation/"presented" buffers). Color uses FLOAT4 (RGBA) here, unlike
// denoiseWithOptix()'s FLOAT3 - InteractivePtRenderer's accumulation buffer
// carries the hit-fraction alpha in .w (see RtOptixSceneParams::image's doc
// comment), and OPTIX_DENOISER_ALPHA_MODE_COPY passes that channel through
// unmodified rather than having the denoiser treat it as real opacity.
//
// UNLIKE denoiseWithOptix() (called once per full render), this is called on
// every interactive tick - so the denoiser handle plus its state/scratch/
// intensity buffers are cached in _impl (Impl::deviceDenoiser and friends)
// and only rebuilt when resolution or guide-buffer availability actually
// changes, rather than create/destroy every call. Doing the create/destroy
// every call (the first version of this method, mirroring denoiseWithOptix()'s
// pattern) both repeated real GPU work every tick AND made OptiX's own
// internal log callback re-log its "using cuda device"/"layers created"
// diagnostics every tick too, flooding the console during any drag.
bool RtDenoiser::denoiseDeviceWithOptix(void* dColorRGBA, void* dAlbedo, void* dNormal, int width, int height,
	void* dOutputRGBA)
{
	if (!cudaOk(cudaFree(0), "cudaFree(0)")) // see denoiseWithOptix()'s identical call for why this is needed every time
		return false;

	const bool haveGuides = dAlbedo && dNormal;

	if (!_impl->deviceDenoiser || _impl->deviceDenoiserWidth != width || _impl->deviceDenoiserHeight != height ||
		_impl->deviceDenoiserHasGuides != haveGuides)
	{
		_impl->releaseDeviceDenoiser();

		OptixDenoiserOptions denoiserOptions{};
		denoiserOptions.guideAlbedo = haveGuides ? 1u : 0u;
		denoiserOptions.guideNormal = haveGuides ? 1u : 0u;
#if OPTIX_VERSION >= 80000
		denoiserOptions.denoiseAlpha = OPTIX_DENOISER_ALPHA_MODE_COPY; // see this method's doc comment
#endif

		bool ok = optixOk(optixDenoiserCreate(_impl->optixContext, OPTIX_DENOISER_MODEL_KIND_HDR, &denoiserOptions,
			&_impl->deviceDenoiser), "optixDenoiserCreate() (device-resident)");

		OptixDenoiserSizes sizes{};
		if (ok)
			ok = optixOk(optixDenoiserComputeMemoryResources(_impl->deviceDenoiser, static_cast<unsigned int>(width),
				static_cast<unsigned int>(height), &sizes), "optixDenoiserComputeMemoryResources() (device-resident)");

		if (ok) ok = cudaOk(cudaMalloc(reinterpret_cast<void**>(&_impl->deviceDenoiserState), sizes.stateSizeInBytes), "cudaMalloc(device-resident denoiser state)");
		if (ok) ok = cudaOk(cudaMalloc(reinterpret_cast<void**>(&_impl->deviceDenoiserScratch), sizes.withoutOverlapScratchSizeInBytes), "cudaMalloc(device-resident denoiser scratch)");
		if (ok) ok = cudaOk(cudaMalloc(reinterpret_cast<void**>(&_impl->deviceDenoiserIntensity), sizeof(float)), "cudaMalloc(device-resident denoiser intensity)");

		if (ok)
			ok = optixOk(optixDenoiserSetup(_impl->deviceDenoiser, nullptr, static_cast<unsigned int>(width), static_cast<unsigned int>(height),
				_impl->deviceDenoiserState, sizes.stateSizeInBytes, _impl->deviceDenoiserScratch, sizes.withoutOverlapScratchSizeInBytes),
				"optixDenoiserSetup() (device-resident)");

		if (!ok)
		{
			_impl->releaseDeviceDenoiser();
			return false;
		}

		_impl->deviceDenoiserStateSize = sizes.stateSizeInBytes;
		_impl->deviceDenoiserScratchSize = sizes.withoutOverlapScratchSizeInBytes;
		_impl->deviceDenoiserWidth = width;
		_impl->deviceDenoiserHeight = height;
		_impl->deviceDenoiserHasGuides = haveGuides;
	}

	OptixImage2D colorImage{};
	colorImage.data = reinterpret_cast<CUdeviceptr>(dColorRGBA);
	colorImage.width = static_cast<unsigned int>(width);
	colorImage.height = static_cast<unsigned int>(height);
	colorImage.rowStrideInBytes = static_cast<unsigned int>(width) * sizeof(float4);
	colorImage.pixelStrideInBytes = sizeof(float4);
	colorImage.format = OPTIX_PIXEL_FORMAT_FLOAT4;

	OptixImage2D outputImage = colorImage;
	outputImage.data = reinterpret_cast<CUdeviceptr>(dOutputRGBA);

	OptixDenoiserGuideLayer guideLayer{};
	if (haveGuides)
	{
		guideLayer.albedo = makeOptixImage(reinterpret_cast<CUdeviceptr>(dAlbedo), width, height);
		guideLayer.normal = makeOptixImage(reinterpret_cast<CUdeviceptr>(dNormal), width, height);
	}

	OptixDenoiserLayer layer{};
	layer.input = colorImage;
	layer.output = outputImage;

	bool ok = optixOk(optixDenoiserComputeIntensity(_impl->deviceDenoiser, nullptr, &colorImage, _impl->deviceDenoiserIntensity,
		_impl->deviceDenoiserScratch, _impl->deviceDenoiserScratchSize), "optixDenoiserComputeIntensity() (device-resident)");

	OptixDenoiserParams params{};
#if OPTIX_VERSION < 80000
	params.denoiseAlpha = OPTIX_DENOISER_ALPHA_MODE_COPY; // see this method's doc comment
#endif
	params.hdrIntensity = _impl->deviceDenoiserIntensity;
	params.blendFactor = 0.0f;

	if (ok)
		ok = optixOk(optixDenoiserInvoke(_impl->deviceDenoiser, nullptr, &params, _impl->deviceDenoiserState, _impl->deviceDenoiserStateSize,
			&guideLayer, &layer, 1, 0, 0, _impl->deviceDenoiserScratch, _impl->deviceDenoiserScratchSize), "optixDenoiserInvoke() (device-resident)");

	// This call (and computeIntensity/setup above) runs on the default stream
	// (nullptr), asynchronously - unlike denoiseWithOptix()'s std::vector API,
	// there is no cudaMemcpy(...DeviceToHost) afterward whose own implicit
	// synchronization would guarantee dOutputRGBA is actually finished before
	// this function returns. The caller (InteractivePtRenderer::tick())
	// treats dOutputRGBA as immediately presentable once this call returns
	// true, so an explicit sync here is required for correctness, not just
	// tidiness.
	if (ok)
		ok = cudaOk(cudaStreamSynchronize(nullptr), "cudaStreamSynchronize() (device-resident)");

	return ok;
}
#endif // MODELVIEWER_HAVE_OPTIX

bool RtDenoiser::denoiseDevice(void* dColorRGBA, void* dAlbedo, void* dNormal, int width, int height,
	void* dOutputRGBA, uint32_t sampleCount)
{
	(void)sampleCount; // no fallback tier here (see this method's header doc comment) - nothing tapers on sample count
	if (width <= 0 || height <= 0 || !dColorRGBA || !dOutputRGBA)
		return false;

#ifdef MODELVIEWER_HAVE_OPTIX
	if (_impl->useNativeOptixDenoiser)
		return denoiseDeviceWithOptix(dColorRGBA, dAlbedo, dNormal, width, height, dOutputRGBA);
#endif
	return false;
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
	_impl->useNativeOptixDenoiser = false;
#ifdef MODELVIEWER_HAVE_OPTIX
	// The cached device-resident denoiser (if any) was created against the
	// OLD optixContext about to be destroyed below - releasing it first
	// avoids leaving a dangling handle tied to a context that no longer
	// exists (see Impl::deviceDenoiser's own doc comment).
	_impl->releaseDeviceDenoiser();
	if (_impl->optixContext)
	{
		optixDeviceContextDestroy(_impl->optixContext);
		_impl->optixContext = nullptr;
	}
#endif

	// OptiX: NVIDIA's own AI denoiser, a completely different API from OIDN
	// above - see DenoiserDevicePreference::OptiX's doc comment. Deliberately
	// no fallback to OIDN here, matching GPU's own explicit-choice philosophy.
	if (_impl->preference == DenoiserDevicePreference::OptiX)
	{
#ifdef MODELVIEWER_HAVE_OPTIX
		if (initializeOptixDenoiserContext(_impl->optixContext))
		{
			_impl->deviceValid = true;
			_impl->useNativeOptixDenoiser = true;
		}
		else
		{
			_impl->optixContext = nullptr;
			qWarning() << "RtDenoiser: OptiX denoiser requested but its CUDA/OptiX context could not be initialized"
				<< "- not falling back to OIDN (OptiX was explicitly requested); path-traced frames will use the"
				<< "built-in bilateral fallback denoiser instead.";
		}
#else
		qWarning() << "RtDenoiser: OptiX denoiser requested but this build has no OptiX SDK"
			<< "(MODELVIEWER_HAVE_OPTIX not defined) - not falling back to OIDN; path-traced frames will use the"
			<< "built-in bilateral fallback denoiser instead.";
#endif
		return;
	}

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
	// Auto (default): try native OptiX first - generally the best quality/
	// performance on an RTX GPU, and (unlike the OIDN devices below) entirely
	// independent of which render engine actually produced the frame, since
	// RtDenoiser owns its own standalone OptixDeviceContext (see Impl::
	// optixContext's doc comment) rather than reusing the path tracer's.
	// Falls through to OIDN CUDA, then OIDN CPU, exactly like OIDN's own
	// documented device-selection guidance, on any machine without OptiX/a
	// suitable NVIDIA GPU-driver.
#ifdef MODELVIEWER_HAVE_OPTIX
	else if (initializeOptixDenoiserContext(_impl->optixContext))
	{
		_impl->deviceValid = true;
		_impl->useNativeOptixDenoiser = true;
	}
#endif
	else
	{
#ifdef MODELVIEWER_HAVE_OPTIX
		_impl->optixContext = nullptr;
#endif
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
	else if (_impl->useNativeOptixDenoiser)
		qInfo() << "RtDenoiser: using native OptiX denoiser for path-traced frame denoising.";
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
	if (_impl->useNativeOptixDenoiser)
		return "OptiX";
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

	if (_impl->useNativeOptixDenoiser)
	{
#ifdef MODELVIEWER_HAVE_OPTIX
		QElapsedTimer optixTimer;
		optixTimer.start();
		if (denoiseWithOptix(input, width, height, output, albedo, normal))
		{
			qInfo() << "RtDenoiser: OptiX native denoise of" << width << "x" << height
				<< "took" << optixTimer.elapsed() << "ms";
			return true;
		}
		static bool loggedOptixFailureOnce = false;
		if (!loggedOptixFailureOnce)
		{
			qWarning() << "RtDenoiser: OptiX native denoiser failed - falling back to the built-in bilateral"
				<< "denoiser (logged once).";
			loggedOptixFailureOnce = true;
		}
#endif
		bilateralFallbackDenoise(input, width, height, output, sampleCount);
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
