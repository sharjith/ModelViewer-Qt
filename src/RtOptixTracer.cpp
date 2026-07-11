#include "RtOptixTracer.h"

#include <QDebug>

#ifdef MODELVIEWER_HAVE_OPTIX

#include <cuda_runtime.h>

#include <optix.h>
#include <optix_stack_size.h>
#include <optix_stubs.h>
// NOTE: optix_function_table_definition.h must be included in exactly one
// translation unit across the whole program - that's RtOptixContext.cpp,
// not here. This file only calls into the API table it already defined.

#include "RtOptixEmbeddedPtx.h"
#include "RtOptixTriangleParams.h"

#include <array>
#include <cstring>

namespace
{
	// Same return-false-and-log pattern as RtDenoiser/RtOptixContext rather
	// than exceptions, matching this codebase's own error-handling
	// convention (not the OptiX SDK samples' own OPTIX_CHECK/CUDA_CHECK
	// exception-throwing macros).
	bool cudaCheck(cudaError_t err, const char* what)
	{
		if (err == cudaSuccess)
			return true;
		qWarning() << "RtOptixTracer:" << what << "failed:" << cudaGetErrorString(err);
		return false;
	}

	bool optixCheck(OptixResult result, const char* what)
	{
		if (result == OPTIX_SUCCESS)
			return true;
		qWarning() << "RtOptixTracer:" << what << "failed (OptixResult" << static_cast<int>(result) << ")";
		return false;
	}

	void optixLogCallback(unsigned int level, const char* tag, const char* message, void* /*cbdata*/)
	{
		const QString text = QString("RtOptixTracer: OptiX [%1][%2]: %3").arg(level).arg(tag).arg(message);
		if (level <= 2)
			qWarning().noquote() << text;
		else
			qInfo().noquote() << text;
	}

	template <typename T>
	struct SbtRecord
	{
		__align__(OPTIX_SBT_RECORD_ALIGNMENT) char header[OPTIX_SBT_RECORD_HEADER_SIZE];
		T data;
	};

	struct EmptyData
	{
	};

	struct MissData
	{
		float3 bgColor;
	};

	using RayGenSbtRecord = SbtRecord<EmptyData>;
	using MissSbtRecord = SbtRecord<MissData>;
	using HitGroupSbtRecord = SbtRecord<EmptyData>;
}

struct RtOptixTracer::Impl
{
	bool valid = false;

	OptixDeviceContext context = nullptr;
	CUdeviceptr gasOutputBuffer = 0;
	OptixTraversableHandle gasHandle = 0;

	OptixModule module = nullptr;
	OptixPipeline pipeline = nullptr;
	OptixProgramGroup raygenGroup = nullptr;
	OptixProgramGroup missGroup = nullptr;
	OptixProgramGroup hitgroupGroup = nullptr;
	OptixShaderBindingTable sbt = {};

	~Impl()
	{
		if (sbt.raygenRecord) cudaFree(reinterpret_cast<void*>(sbt.raygenRecord));
		if (sbt.missRecordBase) cudaFree(reinterpret_cast<void*>(sbt.missRecordBase));
		if (sbt.hitgroupRecordBase) cudaFree(reinterpret_cast<void*>(sbt.hitgroupRecordBase));
		if (gasOutputBuffer) cudaFree(reinterpret_cast<void*>(gasOutputBuffer));

		if (pipeline) optixPipelineDestroy(pipeline);
		if (hitgroupGroup) optixProgramGroupDestroy(hitgroupGroup);
		if (missGroup) optixProgramGroupDestroy(missGroup);
		if (raygenGroup) optixProgramGroupDestroy(raygenGroup);
		if (module) optixModuleDestroy(module);
		if (context) optixDeviceContextDestroy(context);
	}
};

RtOptixTracer::RtOptixTracer() : _impl(std::make_unique<Impl>())
{
	// --- CUDA + OptiX device context (see RtOptixContext's doc comment on
	// why this is duplicated here rather than shared for this checkpoint) ---
	if (!cudaCheck(cudaFree(0), "cudaFree(0)"))
		return;
	if (!optixCheck(optixInit(), "optixInit()"))
		return;

	OptixDeviceContextOptions contextOptions{};
	contextOptions.logCallbackFunction = &optixLogCallback;
	contextOptions.logCallbackLevel = 4;
	if (!optixCheck(optixDeviceContextCreate(nullptr, &contextOptions, &_impl->context), "optixDeviceContextCreate()"))
		return;

	// --- Geometry acceleration structure: one hardcoded triangle ---
	const std::array<float3, 3> vertices = { {
		make_float3(-0.5f, -0.5f, 0.0f),
		make_float3(0.5f, -0.5f, 0.0f),
		make_float3(0.0f, 0.5f, 0.0f)
	} };
	const size_t verticesSize = sizeof(float3) * vertices.size();

	CUdeviceptr dVertices = 0;
	if (!cudaCheck(cudaMalloc(reinterpret_cast<void**>(&dVertices), verticesSize), "cudaMalloc(vertices)"))
		return;
	if (!cudaCheck(cudaMemcpy(reinterpret_cast<void*>(dVertices), vertices.data(), verticesSize, cudaMemcpyHostToDevice), "cudaMemcpy(vertices)"))
	{
		cudaFree(reinterpret_cast<void*>(dVertices));
		return;
	}

	OptixAccelBuildOptions accelOptions{};
	accelOptions.buildFlags = OPTIX_BUILD_FLAG_NONE;
	accelOptions.operation = OPTIX_BUILD_OPERATION_BUILD;

	const uint32_t triangleInputFlags[1] = { OPTIX_GEOMETRY_FLAG_NONE };
	OptixBuildInput triangleInput{};
	triangleInput.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
	triangleInput.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
	triangleInput.triangleArray.numVertices = static_cast<uint32_t>(vertices.size());
	triangleInput.triangleArray.vertexBuffers = &dVertices;
	triangleInput.triangleArray.flags = triangleInputFlags;
	triangleInput.triangleArray.numSbtRecords = 1;

	OptixAccelBufferSizes gasBufferSizes{};
	if (!optixCheck(optixAccelComputeMemoryUsage(_impl->context, &accelOptions, &triangleInput, 1, &gasBufferSizes), "optixAccelComputeMemoryUsage()"))
	{
		cudaFree(reinterpret_cast<void*>(dVertices));
		return;
	}

	CUdeviceptr dTempBuffer = 0;
	if (!cudaCheck(cudaMalloc(reinterpret_cast<void**>(&dTempBuffer), gasBufferSizes.tempSizeInBytes), "cudaMalloc(gas temp)") ||
	    !cudaCheck(cudaMalloc(reinterpret_cast<void**>(&_impl->gasOutputBuffer), gasBufferSizes.outputSizeInBytes), "cudaMalloc(gas output)"))
	{
		cudaFree(reinterpret_cast<void*>(dVertices));
		if (dTempBuffer) cudaFree(reinterpret_cast<void*>(dTempBuffer));
		return;
	}

	const bool accelBuilt = optixCheck(optixAccelBuild(
		_impl->context, 0, &accelOptions, &triangleInput, 1,
		dTempBuffer, gasBufferSizes.tempSizeInBytes,
		_impl->gasOutputBuffer, gasBufferSizes.outputSizeInBytes,
		&_impl->gasHandle, nullptr, 0), "optixAccelBuild()");

	cudaFree(reinterpret_cast<void*>(dTempBuffer));
	cudaFree(reinterpret_cast<void*>(dVertices));
	if (!accelBuilt)
		return;

	// --- Module (compiled from the embedded PTX built by CMake's
	// add_optix_kernel()/EmbedPTX.cmake - see RtOptixTriangle.cu) ---
	OptixModuleCompileOptions moduleCompileOptions{};
#ifndef NDEBUG
	moduleCompileOptions.optLevel = OPTIX_COMPILE_OPTIMIZATION_LEVEL_0;
	moduleCompileOptions.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_FULL;
#endif

	OptixPipelineCompileOptions pipelineCompileOptions{};
	pipelineCompileOptions.usesMotionBlur = false;
	pipelineCompileOptions.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_GAS;
	pipelineCompileOptions.numPayloadValues = 3;
	pipelineCompileOptions.numAttributeValues = 3;
	pipelineCompileOptions.exceptionFlags = OPTIX_EXCEPTION_FLAG_NONE;
	pipelineCompileOptions.pipelineLaunchParamsVariableName = "params";
	pipelineCompileOptions.usesPrimitiveTypeFlags = OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE;

	char log[2048];
	size_t logSize = sizeof(log);
	OptixResult moduleResult = optixModuleCreate(
		_impl->context, &moduleCompileOptions, &pipelineCompileOptions,
		g_rtOptixTrianglePtx, std::strlen(g_rtOptixTrianglePtx),
		log, &logSize, &_impl->module);
	if (logSize > 1)
		qInfo().noquote() << "RtOptixTracer: optixModuleCreate() log:" << log;
	if (!optixCheck(moduleResult, "optixModuleCreate()"))
		return;

	// --- Program groups ---
	OptixProgramGroupOptions programGroupOptions{};

	OptixProgramGroupDesc raygenDesc{};
	raygenDesc.kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
	raygenDesc.raygen.module = _impl->module;
	raygenDesc.raygen.entryFunctionName = "__raygen__rg";
	logSize = sizeof(log);
	if (!optixCheck(optixProgramGroupCreate(_impl->context, &raygenDesc, 1, &programGroupOptions, log, &logSize, &_impl->raygenGroup), "optixProgramGroupCreate(raygen)"))
		return;

	OptixProgramGroupDesc missDesc{};
	missDesc.kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
	missDesc.miss.module = _impl->module;
	missDesc.miss.entryFunctionName = "__miss__ms";
	logSize = sizeof(log);
	if (!optixCheck(optixProgramGroupCreate(_impl->context, &missDesc, 1, &programGroupOptions, log, &logSize, &_impl->missGroup), "optixProgramGroupCreate(miss)"))
		return;

	OptixProgramGroupDesc hitgroupDesc{};
	hitgroupDesc.kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
	hitgroupDesc.hitgroup.moduleCH = _impl->module;
	hitgroupDesc.hitgroup.entryFunctionNameCH = "__closesthit__ch";
	logSize = sizeof(log);
	if (!optixCheck(optixProgramGroupCreate(_impl->context, &hitgroupDesc, 1, &programGroupOptions, log, &logSize, &_impl->hitgroupGroup), "optixProgramGroupCreate(hitgroup)"))
		return;

	// --- Pipeline ---
	constexpr uint32_t kMaxTraceDepth = 1;
	OptixProgramGroup programGroups[] = { _impl->raygenGroup, _impl->missGroup, _impl->hitgroupGroup };

	OptixPipelineLinkOptions pipelineLinkOptions{};
	pipelineLinkOptions.maxTraceDepth = kMaxTraceDepth;
	logSize = sizeof(log);
	if (!optixCheck(optixPipelineCreate(_impl->context, &pipelineCompileOptions, &pipelineLinkOptions,
		programGroups, static_cast<unsigned int>(std::size(programGroups)), log, &logSize, &_impl->pipeline), "optixPipelineCreate()"))
		return;

	OptixStackSizes stackSizes{};
	for (OptixProgramGroup group : programGroups)
	{
		if (!optixCheck(optixUtilAccumulateStackSizes(group, &stackSizes, _impl->pipeline), "optixUtilAccumulateStackSizes()"))
			return;
	}
	uint32_t dcStackFromTraversal = 0, dcStackFromState = 0, continuationStack = 0;
	if (!optixCheck(optixUtilComputeStackSizes(&stackSizes, kMaxTraceDepth, 0, 0,
		&dcStackFromTraversal, &dcStackFromState, &continuationStack), "optixUtilComputeStackSizes()"))
		return;
	if (!optixCheck(optixPipelineSetStackSize(_impl->pipeline, dcStackFromTraversal, dcStackFromState, continuationStack, 1), "optixPipelineSetStackSize()"))
		return;

	// --- Shader binding table ---
	CUdeviceptr raygenRecord = 0;
	RayGenSbtRecord rgSbt{};
	if (!cudaCheck(cudaMalloc(reinterpret_cast<void**>(&raygenRecord), sizeof(RayGenSbtRecord)), "cudaMalloc(raygen SBT)"))
		return;
	if (!optixCheck(optixSbtRecordPackHeader(_impl->raygenGroup, &rgSbt), "optixSbtRecordPackHeader(raygen)"))
		return;
	if (!cudaCheck(cudaMemcpy(reinterpret_cast<void*>(raygenRecord), &rgSbt, sizeof(RayGenSbtRecord), cudaMemcpyHostToDevice), "cudaMemcpy(raygen SBT)"))
		return;

	CUdeviceptr missRecord = 0;
	MissSbtRecord msSbt{};
	msSbt.data.bgColor = make_float3(0.1f, 0.1f, 0.15f);
	if (!cudaCheck(cudaMalloc(reinterpret_cast<void**>(&missRecord), sizeof(MissSbtRecord)), "cudaMalloc(miss SBT)"))
		return;
	if (!optixCheck(optixSbtRecordPackHeader(_impl->missGroup, &msSbt), "optixSbtRecordPackHeader(miss)"))
		return;
	if (!cudaCheck(cudaMemcpy(reinterpret_cast<void*>(missRecord), &msSbt, sizeof(MissSbtRecord), cudaMemcpyHostToDevice), "cudaMemcpy(miss SBT)"))
		return;

	CUdeviceptr hitgroupRecord = 0;
	HitGroupSbtRecord hgSbt{};
	if (!cudaCheck(cudaMalloc(reinterpret_cast<void**>(&hitgroupRecord), sizeof(HitGroupSbtRecord)), "cudaMalloc(hitgroup SBT)"))
		return;
	if (!optixCheck(optixSbtRecordPackHeader(_impl->hitgroupGroup, &hgSbt), "optixSbtRecordPackHeader(hitgroup)"))
		return;
	if (!cudaCheck(cudaMemcpy(reinterpret_cast<void*>(hitgroupRecord), &hgSbt, sizeof(HitGroupSbtRecord), cudaMemcpyHostToDevice), "cudaMemcpy(hitgroup SBT)"))
		return;

	_impl->sbt.raygenRecord = raygenRecord;
	_impl->sbt.missRecordBase = missRecord;
	_impl->sbt.missRecordStrideInBytes = sizeof(MissSbtRecord);
	_impl->sbt.missRecordCount = 1;
	_impl->sbt.hitgroupRecordBase = hitgroupRecord;
	_impl->sbt.hitgroupRecordStrideInBytes = sizeof(HitGroupSbtRecord);
	_impl->sbt.hitgroupRecordCount = 1;

	_impl->valid = true;
	qInfo() << "RtOptixTracer: pipeline ready (test triangle).";
}

RtOptixTracer::~RtOptixTracer() = default;

bool RtOptixTracer::isAvailable() const
{
	return _impl->valid;
}

bool RtOptixTracer::renderTestTriangle(int width, int height, std::vector<uint8_t>& outImageRgba8)
{
	if (!_impl->valid || width <= 0 || height <= 0)
		return false;

	const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
	CUdeviceptr dImage = 0;
	if (!cudaCheck(cudaMalloc(reinterpret_cast<void**>(&dImage), pixelCount * sizeof(uchar4)), "cudaMalloc(output image)"))
		return false;

	RtOptixTriangleParams params{};
	params.image = reinterpret_cast<uchar4*>(dImage);
	params.imageWidth = static_cast<unsigned int>(width);
	params.imageHeight = static_cast<unsigned int>(height);
	params.camEye = make_float3(0.0f, 0.0f, 2.0f);
	params.camU = make_float3(1.0f, 0.0f, 0.0f);
	params.camV = make_float3(0.0f, static_cast<float>(height) / static_cast<float>(width), 0.0f);
	params.camW = make_float3(0.0f, 0.0f, -1.5f);
	params.handle = _impl->gasHandle;

	CUdeviceptr dParams = 0;
	bool ok = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&dParams), sizeof(RtOptixTriangleParams)), "cudaMalloc(params)");
	if (ok)
		ok = cudaCheck(cudaMemcpy(reinterpret_cast<void*>(dParams), &params, sizeof(RtOptixTriangleParams), cudaMemcpyHostToDevice), "cudaMemcpy(params)");
	if (ok)
		ok = optixCheck(optixLaunch(_impl->pipeline, nullptr, dParams, sizeof(RtOptixTriangleParams), &_impl->sbt,
			static_cast<unsigned int>(width), static_cast<unsigned int>(height), 1), "optixLaunch()");
	if (ok)
		ok = cudaCheck(cudaDeviceSynchronize(), "cudaDeviceSynchronize()");

	if (ok)
	{
		outImageRgba8.resize(pixelCount * 4);
		ok = cudaCheck(cudaMemcpy(outImageRgba8.data(), reinterpret_cast<void*>(dImage), pixelCount * sizeof(uchar4), cudaMemcpyDeviceToHost), "cudaMemcpy(readback)");
	}

	if (dParams) cudaFree(reinterpret_cast<void*>(dParams));
	cudaFree(reinterpret_cast<void*>(dImage));

	if (!ok)
		outImageRgba8.clear();
	return ok;
}

#else // !MODELVIEWER_HAVE_OPTIX

struct RtOptixTracer::Impl
{
};

RtOptixTracer::RtOptixTracer() : _impl(std::make_unique<Impl>())
{
}

RtOptixTracer::~RtOptixTracer() = default;

bool RtOptixTracer::isAvailable() const
{
	return false;
}

bool RtOptixTracer::renderTestTriangle(int, int, std::vector<uint8_t>&)
{
	return false;
}

#endif // MODELVIEWER_HAVE_OPTIX
