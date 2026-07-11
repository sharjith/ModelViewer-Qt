#include "RtOptixContext.h"

#include <QDebug>

#include <string>

#ifdef MODELVIEWER_HAVE_OPTIX

#include <cuda_runtime.h>

#include <optix.h>
#include <optix_function_table_definition.h> // exactly one TU in the whole program - this is it
#include <optix_stubs.h>

namespace
{
	// Routed to qWarning (not stderr/std::cerr) so OptiX's own validation
	// messages show up in this app's normal log file alongside everything
	// else - see RtOptixContext.h's doc comment on matching RtDenoiser's
	// logging conventions. level 1-2 are fatal/error-ish per OptiX's own
	// documented callback levels, 3 is warning, 4 is print/info.
	void optixLogCallback(unsigned int level, const char* tag, const char* message, void* /*cbdata*/)
	{
		const QString text = QString("RtOptixContext: OptiX [%1][%2]: %3").arg(level).arg(tag).arg(message);
		if (level <= 2)
			qWarning().noquote() << text;
		else
			qInfo().noquote() << text;
	}
}

struct RtOptixContext::Impl
{
	bool valid = false;
	std::string deviceName;
	OptixDeviceContext optixContext = nullptr;
};

RtOptixContext::RtOptixContext() : _impl(std::make_unique<Impl>())
{
	// Forces CUDA's primary context into existence on the default device
	// (device 0) - the standard "cudaFree(0)" idiom the OptiX SDK samples
	// themselves use for this, since there's no dedicated "just initialize"
	// runtime-API call. Also doubles as the first real CUDA call, so a
	// missing/incompatible driver fails here with a normal cudaError_t
	// rather than deeper inside OptiX's own init.
	cudaError_t cudaErr = cudaFree(0);
	if (cudaErr != cudaSuccess)
	{
		qWarning() << "RtOptixContext: CUDA initialization failed (cudaFree(0):"
			<< cudaGetErrorString(cudaErr) << ") - GPU path tracing unavailable.";
		return;
	}

	cudaDeviceProp props{};
	cudaErr = cudaGetDeviceProperties(&props, 0);
	if (cudaErr == cudaSuccess)
		_impl->deviceName = props.name;

	const OptixResult initResult = optixInit();
	if (initResult != OPTIX_SUCCESS)
	{
		qWarning() << "RtOptixContext: optixInit() failed (result" << static_cast<int>(initResult)
			<< ") - the NVIDIA driver on this system doesn't support OptiX 7.7. GPU path tracing unavailable.";
		return;
	}

	OptixDeviceContextOptions options{};
	options.logCallbackFunction = &optixLogCallback;
	options.logCallbackLevel = 4;
#ifndef NDEBUG
	options.validationMode = OPTIX_DEVICE_CONTEXT_VALIDATION_MODE_ALL;
#endif

	// CUcontext(0) means "use CUDA's current context" - already established
	// by the cudaFree(0) call above, so there's no need to touch the driver
	// API (cuCtxCreate etc.) directly here.
	const OptixResult contextResult = optixDeviceContextCreate(nullptr, &options, &_impl->optixContext);
	if (contextResult != OPTIX_SUCCESS)
	{
		qWarning() << "RtOptixContext: optixDeviceContextCreate() failed (result"
			<< static_cast<int>(contextResult) << ") - GPU path tracing unavailable.";
		_impl->optixContext = nullptr;
		return;
	}

	_impl->valid = true;
	qInfo() << "RtOptixContext: OptiX device context created successfully on"
		<< QString::fromStdString(_impl->deviceName);
}

RtOptixContext::~RtOptixContext()
{
	if (_impl->optixContext)
		optixDeviceContextDestroy(_impl->optixContext);
}

bool RtOptixContext::isAvailable() const
{
	return _impl->valid;
}

const char* RtOptixContext::deviceName() const
{
	return _impl->deviceName.c_str();
}

#else // !MODELVIEWER_HAVE_OPTIX

struct RtOptixContext::Impl
{
};

RtOptixContext::RtOptixContext() : _impl(std::make_unique<Impl>())
{
}

RtOptixContext::~RtOptixContext() = default;

bool RtOptixContext::isAvailable() const
{
	return false;
}

const char* RtOptixContext::deviceName() const
{
	return "";
}

#endif // MODELVIEWER_HAVE_OPTIX
