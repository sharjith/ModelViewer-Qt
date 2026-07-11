#include "RtOptixSceneTracer.h"

#include <QDebug>

#ifdef MODELVIEWER_HAVE_OPTIX

#include <cuda_runtime.h>

#include <optix.h>
#include <optix_stack_size.h>
#include <optix_stubs.h>
// NOTE: optix_function_table_definition.h must be included in exactly one
// translation unit across the whole program - that's RtOptixContext.cpp.

#include "RtOptixEmbeddedPtx.h"
#include "RtOptixSceneParams.h"

#include <glm/glm.hpp>

#include <cstring>

namespace
{
	bool cudaCheck(cudaError_t err, const char* what)
	{
		if (err == cudaSuccess)
			return true;
		qWarning() << "RtOptixSceneTracer:" << what << "failed:" << cudaGetErrorString(err);
		return false;
	}

	bool optixCheck(OptixResult result, const char* what)
	{
		if (result == OPTIX_SUCCESS)
			return true;
		qWarning() << "RtOptixSceneTracer:" << what << "failed (OptixResult" << static_cast<int>(result) << ")";
		return false;
	}

	void optixLogCallback(unsigned int level, const char* tag, const char* message, void* /*cbdata*/)
	{
		const QString text = QString("RtOptixSceneTracer: OptiX [%1][%2]: %3").arg(level).arg(tag).arg(message);
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

	using RayGenSbtRecord = SbtRecord<EmptyData>;
	using MissSbtRecord = SbtRecord<EmptyData>;
	using HitGroupSbtRecord = SbtRecord<RtOptixSceneHitGroupData>;

	// glm::mat4 is column-major (mat[col][row]); OptixInstance::transform is a
	// row-major 3x4 affine matrix (no perspective row) - see optix_types.h's
	// doc comment on OptixInstance::transform.
	void toOptixTransform(const glm::mat4& m, float outTransform[12])
	{
		for (int row = 0; row < 3; ++row)
			for (int col = 0; col < 4; ++col)
				outTransform[row * 4 + col] = m[col][row];
	}
}

struct RtOptixSceneTracer::Impl
{
	bool valid = false;

	OptixDeviceContext context = nullptr;

	// One entry per snapshot mesh (GAS) - kept alive for the lifetime of the
	// built scene since the closest-hit shader dereferences positions/indices
	// directly, and the IAS's OptixInstance array references each handle.
	struct MeshGas
	{
		CUdeviceptr positions = 0;
		CUdeviceptr indices = 0;
		CUdeviceptr gasOutputBuffer = 0;
		OptixTraversableHandle handle = 0;
	};
	std::vector<MeshGas> meshGasEntries;

	CUdeviceptr instancesBuffer = 0;
	CUdeviceptr iasOutputBuffer = 0;
	OptixTraversableHandle iasHandle = 0;

	OptixModule module = nullptr;
	OptixPipeline pipeline = nullptr;
	OptixProgramGroup raygenGroup = nullptr;
	OptixProgramGroup missGroup = nullptr;
	OptixProgramGroup hitgroupGroup = nullptr;

	CUdeviceptr raygenRecord = 0;
	CUdeviceptr missRecord = 0;
	CUdeviceptr hitgroupRecords = 0; // one HitGroupSbtRecord per instance
	OptixShaderBindingTable sbt = {};

	void freeSceneBuffers()
	{
		for (MeshGas& gas : meshGasEntries)
		{
			if (gas.positions) cudaFree(reinterpret_cast<void*>(gas.positions));
			if (gas.indices) cudaFree(reinterpret_cast<void*>(gas.indices));
			if (gas.gasOutputBuffer) cudaFree(reinterpret_cast<void*>(gas.gasOutputBuffer));
		}
		meshGasEntries.clear();

		if (instancesBuffer) cudaFree(reinterpret_cast<void*>(instancesBuffer));
		instancesBuffer = 0;
		if (iasOutputBuffer) cudaFree(reinterpret_cast<void*>(iasOutputBuffer));
		iasOutputBuffer = 0;
		iasHandle = 0;

		if (hitgroupRecords) cudaFree(reinterpret_cast<void*>(hitgroupRecords));
		hitgroupRecords = 0;
		sbt.hitgroupRecordBase = 0;
		sbt.hitgroupRecordCount = 0;
	}

	~Impl()
	{
		freeSceneBuffers();
		if (raygenRecord) cudaFree(reinterpret_cast<void*>(raygenRecord));
		if (missRecord) cudaFree(reinterpret_cast<void*>(missRecord));

		if (pipeline) optixPipelineDestroy(pipeline);
		if (hitgroupGroup) optixProgramGroupDestroy(hitgroupGroup);
		if (missGroup) optixProgramGroupDestroy(missGroup);
		if (raygenGroup) optixProgramGroupDestroy(raygenGroup);
		if (module) optixModuleDestroy(module);
		if (context) optixDeviceContextDestroy(context);
	}
};

RtOptixSceneTracer::RtOptixSceneTracer() : _impl(std::make_unique<Impl>())
{
	// --- CUDA + OptiX device context (self-contained - see this class's doc
	// comment for why, same as RtOptixTracer/Phase 1b) ---
	if (!cudaCheck(cudaFree(0), "cudaFree(0)"))
		return;
	if (!optixCheck(optixInit(), "optixInit()"))
		return;

	OptixDeviceContextOptions contextOptions{};
	contextOptions.logCallbackFunction = &optixLogCallback;
	contextOptions.logCallbackLevel = 4;
	if (!optixCheck(optixDeviceContextCreate(nullptr, &contextOptions, &_impl->context), "optixDeviceContextCreate()"))
		return;

	// --- Module/pipeline/program groups (fixed - independent of any
	// particular scene, unlike the GAS/IAS/SBT built per buildScene() call) ---
	OptixModuleCompileOptions moduleCompileOptions{};
#ifndef NDEBUG
	moduleCompileOptions.optLevel = OPTIX_COMPILE_OPTIMIZATION_LEVEL_0;
	moduleCompileOptions.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_FULL;
#endif

	OptixPipelineCompileOptions pipelineCompileOptions{};
	pipelineCompileOptions.usesMotionBlur = false;
	pipelineCompileOptions.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_ANY;
	pipelineCompileOptions.numPayloadValues = 3;
	pipelineCompileOptions.numAttributeValues = 3;
	pipelineCompileOptions.exceptionFlags = OPTIX_EXCEPTION_FLAG_NONE;
	pipelineCompileOptions.pipelineLaunchParamsVariableName = "params";
	pipelineCompileOptions.usesPrimitiveTypeFlags = OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE;

	char log[2048];
	size_t logSize = sizeof(log);
	OptixResult moduleResult = optixModuleCreate(
		_impl->context, &moduleCompileOptions, &pipelineCompileOptions,
		g_rtOptixScenePtx, std::strlen(g_rtOptixScenePtx),
		log, &logSize, &_impl->module);
	if (logSize > 1)
		qInfo().noquote() << "RtOptixSceneTracer: optixModuleCreate() log:" << log;
	if (!optixCheck(moduleResult, "optixModuleCreate()"))
		return;

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
	// maxTraversableDepth = 2 (IAS -> GAS), unlike Phase 1b's single-GAS
	// ALLOW_SINGLE_GAS scene which only needed depth 1.
	if (!optixCheck(optixPipelineSetStackSize(_impl->pipeline, dcStackFromTraversal, dcStackFromState, continuationStack, 2), "optixPipelineSetStackSize()"))
		return;

	// --- Raygen/miss SBT records (fixed - no per-scene data) ---
	RayGenSbtRecord rgSbt{};
	if (!cudaCheck(cudaMalloc(reinterpret_cast<void**>(&_impl->raygenRecord), sizeof(RayGenSbtRecord)), "cudaMalloc(raygen SBT)"))
		return;
	if (!optixCheck(optixSbtRecordPackHeader(_impl->raygenGroup, &rgSbt), "optixSbtRecordPackHeader(raygen)"))
		return;
	if (!cudaCheck(cudaMemcpy(reinterpret_cast<void*>(_impl->raygenRecord), &rgSbt, sizeof(RayGenSbtRecord), cudaMemcpyHostToDevice), "cudaMemcpy(raygen SBT)"))
		return;

	MissSbtRecord msSbt{};
	if (!cudaCheck(cudaMalloc(reinterpret_cast<void**>(&_impl->missRecord), sizeof(MissSbtRecord)), "cudaMalloc(miss SBT)"))
		return;
	if (!optixCheck(optixSbtRecordPackHeader(_impl->missGroup, &msSbt), "optixSbtRecordPackHeader(miss)"))
		return;
	if (!cudaCheck(cudaMemcpy(reinterpret_cast<void*>(_impl->missRecord), &msSbt, sizeof(MissSbtRecord), cudaMemcpyHostToDevice), "cudaMemcpy(miss SBT)"))
		return;

	_impl->sbt.raygenRecord = _impl->raygenRecord;
	_impl->sbt.missRecordBase = _impl->missRecord;
	_impl->sbt.missRecordStrideInBytes = sizeof(MissSbtRecord);
	_impl->sbt.missRecordCount = 1;

	_impl->valid = true;
	qInfo() << "RtOptixSceneTracer: pipeline ready.";
}

RtOptixSceneTracer::~RtOptixSceneTracer() = default;

bool RtOptixSceneTracer::isAvailable() const
{
	return _impl->valid;
}

bool RtOptixSceneTracer::buildScene(const RtSceneSnapshot& snapshot)
{
	if (!_impl->valid)
		return false;

	_impl->freeSceneBuffers();

	// --- One GAS per unique mesh - mirrors RtEmbreeScene::build()'s BLAS
	// loop exactly, just building an OptiX GAS instead of an Embree scene. ---
	_impl->meshGasEntries.reserve(snapshot.meshes.size());
	for (const RtMeshGeometry& mesh : snapshot.meshes)
	{
		Impl::MeshGas gas;

		if (mesh.vertices.empty() || mesh.indices.size() < 3)
		{
			_impl->meshGasEntries.push_back(gas); // empty mesh - handle stays 0, referenced by no instance normally
			continue;
		}

		std::vector<float3> positions(mesh.vertices.size());
		for (size_t i = 0; i < mesh.vertices.size(); ++i)
			positions[i] = make_float3(mesh.vertices[i].position.x, mesh.vertices[i].position.y, mesh.vertices[i].position.z);

		const size_t positionsBytes = positions.size() * sizeof(float3);
		const size_t indicesBytes = mesh.indices.size() * sizeof(uint32_t); // uint3[] and uint32_t[3*N] share the same binary layout

		bool ok = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&gas.positions), positionsBytes), "cudaMalloc(mesh positions)");
		if (ok) ok = cudaCheck(cudaMemcpy(reinterpret_cast<void*>(gas.positions), positions.data(), positionsBytes, cudaMemcpyHostToDevice), "cudaMemcpy(mesh positions)");
		if (ok) ok = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&gas.indices), indicesBytes), "cudaMalloc(mesh indices)");
		if (ok) ok = cudaCheck(cudaMemcpy(reinterpret_cast<void*>(gas.indices), mesh.indices.data(), indicesBytes, cudaMemcpyHostToDevice), "cudaMemcpy(mesh indices)");
		if (!ok)
		{
			_impl->meshGasEntries.push_back(gas);
			continue;
		}

		OptixAccelBuildOptions accelOptions{};
		accelOptions.buildFlags = OPTIX_BUILD_FLAG_NONE;
		accelOptions.operation = OPTIX_BUILD_OPERATION_BUILD;

		const uint32_t triangleInputFlags[1] = { OPTIX_GEOMETRY_FLAG_NONE };
		OptixBuildInput triangleInput{};
		triangleInput.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
		triangleInput.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
		triangleInput.triangleArray.numVertices = static_cast<uint32_t>(positions.size());
		triangleInput.triangleArray.vertexBuffers = &gas.positions;
		triangleInput.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
		triangleInput.triangleArray.numIndexTriplets = static_cast<uint32_t>(mesh.indices.size() / 3);
		triangleInput.triangleArray.indexBuffer = gas.indices;
		triangleInput.triangleArray.flags = triangleInputFlags;
		triangleInput.triangleArray.numSbtRecords = 1;

		OptixAccelBufferSizes gasBufferSizes{};
		if (!optixCheck(optixAccelComputeMemoryUsage(_impl->context, &accelOptions, &triangleInput, 1, &gasBufferSizes), "optixAccelComputeMemoryUsage()"))
		{
			_impl->meshGasEntries.push_back(gas);
			continue;
		}

		CUdeviceptr tempBuffer = 0;
		ok = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&tempBuffer), gasBufferSizes.tempSizeInBytes), "cudaMalloc(gas temp)");
		if (ok) ok = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&gas.gasOutputBuffer), gasBufferSizes.outputSizeInBytes), "cudaMalloc(gas output)");
		if (ok)
		{
			ok = optixCheck(optixAccelBuild(_impl->context, 0, &accelOptions, &triangleInput, 1,
				tempBuffer, gasBufferSizes.tempSizeInBytes,
				gas.gasOutputBuffer, gasBufferSizes.outputSizeInBytes,
				&gas.handle, nullptr, 0), "optixAccelBuild(GAS)");
		}
		if (tempBuffer) cudaFree(reinterpret_cast<void*>(tempBuffer));

		_impl->meshGasEntries.push_back(gas);
	}

	// --- IAS: one OptixInstance per RtInstance, applying its world transform
	// - mirrors RtEmbreeScene::build()'s TLAS loop. ---
	std::vector<OptixInstance> instances;
	instances.reserve(snapshot.instances.size());
	std::vector<HitGroupSbtRecord> hitgroupRecordsHost;
	hitgroupRecordsHost.reserve(snapshot.instances.size());

	for (size_t i = 0; i < snapshot.instances.size(); ++i)
	{
		const RtInstance& inst = snapshot.instances[i];
		if (inst.meshIndex >= _impl->meshGasEntries.size() || _impl->meshGasEntries[inst.meshIndex].handle == 0)
			continue; // empty/failed mesh - nothing to instantiate

		OptixInstance optixInst{};
		toOptixTransform(inst.localToWorld, optixInst.transform);
		optixInst.instanceId = static_cast<unsigned int>(i);
		optixInst.sbtOffset = static_cast<unsigned int>(hitgroupRecordsHost.size()); // 1 ray type -> sbtOffset == record index
		optixInst.visibilityMask = 255;
		optixInst.flags = OPTIX_INSTANCE_FLAG_NONE;
		optixInst.traversableHandle = _impl->meshGasEntries[inst.meshIndex].handle;
		instances.push_back(optixInst);

		HitGroupSbtRecord hgSbt{};
		if (!optixCheck(optixSbtRecordPackHeader(_impl->hitgroupGroup, &hgSbt), "optixSbtRecordPackHeader(hitgroup)"))
			return false;
		hgSbt.data.positions = reinterpret_cast<float3*>(_impl->meshGasEntries[inst.meshIndex].positions);
		hgSbt.data.indices = reinterpret_cast<uint3*>(_impl->meshGasEntries[inst.meshIndex].indices);
		hitgroupRecordsHost.push_back(hgSbt);
	}

	if (instances.empty())
	{
		qWarning() << "RtOptixSceneTracer::buildScene(): no valid instances - nothing to render.";
		return false;
	}

	const size_t instancesBytes = instances.size() * sizeof(OptixInstance);
	if (!cudaCheck(cudaMalloc(reinterpret_cast<void**>(&_impl->instancesBuffer), instancesBytes), "cudaMalloc(instances)"))
		return false;
	if (!cudaCheck(cudaMemcpy(reinterpret_cast<void*>(_impl->instancesBuffer), instances.data(), instancesBytes, cudaMemcpyHostToDevice), "cudaMemcpy(instances)"))
		return false;

	OptixBuildInput instanceInput{};
	instanceInput.type = OPTIX_BUILD_INPUT_TYPE_INSTANCES;
	instanceInput.instanceArray.instances = _impl->instancesBuffer;
	instanceInput.instanceArray.numInstances = static_cast<unsigned int>(instances.size());

	OptixAccelBuildOptions iasAccelOptions{};
	iasAccelOptions.buildFlags = OPTIX_BUILD_FLAG_NONE;
	iasAccelOptions.operation = OPTIX_BUILD_OPERATION_BUILD;

	OptixAccelBufferSizes iasBufferSizes{};
	if (!optixCheck(optixAccelComputeMemoryUsage(_impl->context, &iasAccelOptions, &instanceInput, 1, &iasBufferSizes), "optixAccelComputeMemoryUsage(IAS)"))
		return false;

	CUdeviceptr iasTempBuffer = 0;
	if (!cudaCheck(cudaMalloc(reinterpret_cast<void**>(&iasTempBuffer), iasBufferSizes.tempSizeInBytes), "cudaMalloc(ias temp)"))
		return false;
	if (!cudaCheck(cudaMalloc(reinterpret_cast<void**>(&_impl->iasOutputBuffer), iasBufferSizes.outputSizeInBytes), "cudaMalloc(ias output)"))
	{
		cudaFree(reinterpret_cast<void*>(iasTempBuffer));
		return false;
	}

	const bool iasBuilt = optixCheck(optixAccelBuild(_impl->context, 0, &iasAccelOptions, &instanceInput, 1,
		iasTempBuffer, iasBufferSizes.tempSizeInBytes,
		_impl->iasOutputBuffer, iasBufferSizes.outputSizeInBytes,
		&_impl->iasHandle, nullptr, 0), "optixAccelBuild(IAS)");
	cudaFree(reinterpret_cast<void*>(iasTempBuffer));
	if (!iasBuilt)
		return false;

	// --- Hitgroup SBT records (one per instance) ---
	const size_t hitgroupBytes = hitgroupRecordsHost.size() * sizeof(HitGroupSbtRecord);
	if (!cudaCheck(cudaMalloc(reinterpret_cast<void**>(&_impl->hitgroupRecords), hitgroupBytes), "cudaMalloc(hitgroup SBT)"))
		return false;
	if (!cudaCheck(cudaMemcpy(reinterpret_cast<void*>(_impl->hitgroupRecords), hitgroupRecordsHost.data(), hitgroupBytes, cudaMemcpyHostToDevice), "cudaMemcpy(hitgroup SBT)"))
		return false;

	_impl->sbt.hitgroupRecordBase = _impl->hitgroupRecords;
	_impl->sbt.hitgroupRecordStrideInBytes = sizeof(HitGroupSbtRecord);
	_impl->sbt.hitgroupRecordCount = static_cast<unsigned int>(hitgroupRecordsHost.size());

	qInfo() << "RtOptixSceneTracer: scene built (" << _impl->meshGasEntries.size() << "meshes,"
		<< instances.size() << "instances).";
	return true;
}

bool RtOptixSceneTracer::renderScene(const RtCamera& camera, int width, int height, std::vector<uint8_t>& outImageRgba8)
{
	if (!_impl->valid || _impl->iasHandle == 0 || width <= 0 || height <= 0)
		return false;

	const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
	CUdeviceptr dImage = 0;
	if (!cudaCheck(cudaMalloc(reinterpret_cast<void**>(&dImage), pixelCount * sizeof(uchar4)), "cudaMalloc(output image)"))
		return false;

	RtOptixSceneParams params{};
	params.image = reinterpret_cast<uchar4*>(dImage);
	params.imageWidth = static_cast<unsigned int>(width);
	params.imageHeight = static_cast<unsigned int>(height);
	params.camPosition = make_float3(camera.position.x, camera.position.y, camera.position.z);
	params.camForward = make_float3(camera.forward.x, camera.forward.y, camera.forward.z);
	params.camRight = make_float3(camera.right.x, camera.right.y, camera.right.z);
	params.camUp = make_float3(camera.up.x, camera.up.y, camera.up.z);
	params.camAspectRatio = camera.aspectRatio;
	params.camOrthographic = camera.orthographic ? 1 : 0;
	params.camTanHalfFovY = camera.tanHalfFovY;
	params.camOrthoHalfHeight = camera.orthoHalfHeight;
	params.handle = _impl->iasHandle;

	CUdeviceptr dParams = 0;
	bool ok = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&dParams), sizeof(RtOptixSceneParams)), "cudaMalloc(params)");
	if (ok)
		ok = cudaCheck(cudaMemcpy(reinterpret_cast<void*>(dParams), &params, sizeof(RtOptixSceneParams), cudaMemcpyHostToDevice), "cudaMemcpy(params)");
	if (ok)
		ok = optixCheck(optixLaunch(_impl->pipeline, nullptr, dParams, sizeof(RtOptixSceneParams), &_impl->sbt,
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

struct RtOptixSceneTracer::Impl
{
};

RtOptixSceneTracer::RtOptixSceneTracer() : _impl(std::make_unique<Impl>())
{
}

RtOptixSceneTracer::~RtOptixSceneTracer() = default;

bool RtOptixSceneTracer::isAvailable() const
{
	return false;
}

bool RtOptixSceneTracer::buildScene(const RtSceneSnapshot&)
{
	return false;
}

bool RtOptixSceneTracer::renderScene(const RtCamera&, int, int, std::vector<uint8_t>&)
{
	return false;
}

#endif // MODELVIEWER_HAVE_OPTIX
