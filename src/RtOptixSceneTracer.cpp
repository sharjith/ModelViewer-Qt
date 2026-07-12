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

#include <algorithm>
#include <cstring>
#include <unordered_map>

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
		CUdeviceptr normals = 0;
		CUdeviceptr texCoords = 0; // float2, 4 per vertex - see RtOptixSceneHitGroupData::texCoords
		CUdeviceptr tangents = 0;  // float4 (xyz=tangent, w=handedness) - see RtOptixSceneHitGroupData::tangents
		CUdeviceptr vertexColors = 0; // float3 - see RtOptixSceneHitGroupData::vertexColors
		CUdeviceptr gasOutputBuffer = 0;
		OptixTraversableHandle handle = 0;
	};
	std::vector<MeshGas> meshGasEntries;

	CUdeviceptr instancesBuffer = 0;
	CUdeviceptr iasOutputBuffer = 0;
	OptixTraversableHandle iasHandle = 0;
	CUdeviceptr lightsBuffer = 0;
	unsigned int lightCount = 0;

	// Every material texture buffer uploaded this buildScene() call (baseColor/
	// metallic/roughness/normal/emissive rgba8 arrays) - kept alive for the
	// scene's lifetime and freed in freeSceneBuffers(), same pattern as the
	// environment face buffers below. Deduplicated per-RtTextureSample (by
	// pointer identity) at upload time so multiple materials/instances sharing
	// the same texture object don't re-upload identical bytes - see
	// buildScene()'s uploadTexture() lambda.
	std::vector<CUdeviceptr> textureBuffers;

	// Environment cubemap (raw/mip-0) plus the scalar rotation/exposure/
	// fallback fields, copied from the snapshot's RtEnvironment at
	// buildScene() time.
	CUdeviceptr envFaceBuffers[6] = { 0, 0, 0, 0, 0, 0 };
	int envFaceSize = 0;
	bool envShowBackground = false;
	glm::vec3 envFallbackTopColor{ 0.5f };
	glm::vec3 envFallbackBottomColor{ 0.5f };
	int envFallbackGradientStyle = 0;
	bool envCameraUpAxisZUp = false;
	float envSkyBoxZRotationDegrees = 0.0f;
	float envMapExposure = 1.0f;

	// GGX-prefiltered mip chain (RtEnvironment::prefilterMips) - one entry
	// per mip level, each holding its own 6 face device buffers (kept alive
	// alongside prefilterMipsBuffer, the device array of RtOptixPrefilterMip
	// structs whose .faces[] members point at these same buffers - mirrors
	// the raw-map upload above, just per mip). Empty/0 if no environment (or
	// no prefilter chain specifically) was captured.
	struct PrefilterMipGpu
	{
		CUdeviceptr faceBuffers[6] = { 0, 0, 0, 0, 0, 0 };
	};
	std::vector<PrefilterMipGpu> prefilterMipEntries;
	CUdeviceptr prefilterMipsBuffer = 0;
	int prefilterMipCount = 0;

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
			if (gas.normals) cudaFree(reinterpret_cast<void*>(gas.normals));
			if (gas.texCoords) cudaFree(reinterpret_cast<void*>(gas.texCoords));
			if (gas.tangents) cudaFree(reinterpret_cast<void*>(gas.tangents));
			if (gas.vertexColors) cudaFree(reinterpret_cast<void*>(gas.vertexColors));
			if (gas.indices) cudaFree(reinterpret_cast<void*>(gas.indices));
			if (gas.gasOutputBuffer) cudaFree(reinterpret_cast<void*>(gas.gasOutputBuffer));
		}
		meshGasEntries.clear();

		for (CUdeviceptr& texBuf : textureBuffers)
			if (texBuf) cudaFree(reinterpret_cast<void*>(texBuf));
		textureBuffers.clear();

		if (instancesBuffer) cudaFree(reinterpret_cast<void*>(instancesBuffer));
		instancesBuffer = 0;
		if (iasOutputBuffer) cudaFree(reinterpret_cast<void*>(iasOutputBuffer));
		iasOutputBuffer = 0;
		iasHandle = 0;
		if (lightsBuffer) cudaFree(reinterpret_cast<void*>(lightsBuffer));
		lightsBuffer = 0;
		lightCount = 0;

		for (CUdeviceptr& face : envFaceBuffers)
		{
			if (face) cudaFree(reinterpret_cast<void*>(face));
			face = 0;
		}
		envFaceSize = 0;

		for (PrefilterMipGpu& mip : prefilterMipEntries)
			for (CUdeviceptr& face : mip.faceBuffers)
				if (face) cudaFree(reinterpret_cast<void*>(face));
		prefilterMipEntries.clear();
		if (prefilterMipsBuffer) cudaFree(reinterpret_cast<void*>(prefilterMipsBuffer));
		prefilterMipsBuffer = 0;
		prefilterMipCount = 0;

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
	// p0-p2 = radiance, p3 = hitFlag, p4-p6 = world-space shading normal
	// (doubles as the OIDN guide normal at bounce 0), p7 = hit distance,
	// p8-p10 = next bounce direction, p11-p13 = throughput weight for that
	// direction, p14-p16 = OIDN guide albedo (baseColor at the hit), p17 =
	// RNG seed in / this hit's own escape-roughness out, p18 = escape-
	// roughness in (for the miss shader, if THIS ray escapes) - see
	// RtOptixScene.cu's traceBouncePath()/__closesthit__ch() doc comments.
	// Shadow rays reuse just p0 as an occluded bool (see traceShadowRay()).
	pipelineCompileOptions.numPayloadValues = 19;
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

	// 2: the raygen loop's own per-bounce trace call, plus one shadow ray
	// nested inside that hit's direct-lighting NEE (see RtOptixScene.cu's
	// __closesthit__ch()) - bounces themselves are an ITERATIVE loop in
	// __raygen__rg() now, not recursive optixTrace() calls, so this doesn't
	// grow with maxBounces the way an earlier (single-recursive-reflection)
	// version of this kernel's depth requirement did.
	constexpr uint32_t kMaxTraceDepth = 2;
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
		std::vector<float3> normals(mesh.vertices.size());
		std::vector<float2> texCoords(mesh.vertices.size() * 4);
		std::vector<float4> tangents(mesh.vertices.size());
		std::vector<float3> vertexColors(mesh.vertices.size());
		for (size_t i = 0; i < mesh.vertices.size(); ++i)
		{
			const RtVertex& v = mesh.vertices[i];
			positions[i] = make_float3(v.position.x, v.position.y, v.position.z);
			normals[i] = make_float3(v.normal.x, v.normal.y, v.normal.z);
			vertexColors[i] = make_float3(v.color.x, v.color.y, v.color.z);
			for (int ch = 0; ch < 4; ++ch)
				texCoords[i * 4 + ch] = make_float2(v.texCoords[ch].x, v.texCoords[ch].y);

			// Precomputed handedness sign - see RtOptixSceneHitGroupData::
			// tangents' doc comment for why this matches CpuPathTracer::
			// applyNormalMap()'s "orthogonalize bitangent, take cross(N,T)
			// sign" derivation done once here in object space instead of
			// per-shading-sample in world space.
			float handedness = 1.0f;
			if (glm::length(v.tangent) > 0.01f && glm::length(v.bitangent) > 0.01f)
			{
				const glm::vec3 T = glm::normalize(v.tangent - glm::dot(v.tangent, v.normal) * v.normal);
				const glm::vec3 importedBitangent = glm::normalize(v.bitangent - glm::dot(v.bitangent, v.normal) * v.normal);
				const float sign = glm::sign(glm::dot(glm::cross(v.normal, T), importedBitangent));
				if (sign != 0.0f)
					handedness = sign;
			}
			tangents[i] = make_float4(v.tangent.x, v.tangent.y, v.tangent.z, handedness);
		}

		const size_t positionsBytes = positions.size() * sizeof(float3);
		const size_t normalsBytes = normals.size() * sizeof(float3);
		const size_t texCoordsBytes = texCoords.size() * sizeof(float2);
		const size_t tangentsBytes = tangents.size() * sizeof(float4);
		const size_t vertexColorsBytes = vertexColors.size() * sizeof(float3);
		const size_t indicesBytes = mesh.indices.size() * sizeof(uint32_t); // uint3[] and uint32_t[3*N] share the same binary layout

		bool ok = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&gas.positions), positionsBytes), "cudaMalloc(mesh positions)");
		if (ok) ok = cudaCheck(cudaMemcpy(reinterpret_cast<void*>(gas.positions), positions.data(), positionsBytes, cudaMemcpyHostToDevice), "cudaMemcpy(mesh positions)");
		if (ok) ok = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&gas.normals), normalsBytes), "cudaMalloc(mesh normals)");
		if (ok) ok = cudaCheck(cudaMemcpy(reinterpret_cast<void*>(gas.normals), normals.data(), normalsBytes, cudaMemcpyHostToDevice), "cudaMemcpy(mesh normals)");
		if (ok) ok = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&gas.texCoords), texCoordsBytes), "cudaMalloc(mesh texCoords)");
		if (ok) ok = cudaCheck(cudaMemcpy(reinterpret_cast<void*>(gas.texCoords), texCoords.data(), texCoordsBytes, cudaMemcpyHostToDevice), "cudaMemcpy(mesh texCoords)");
		if (ok) ok = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&gas.tangents), tangentsBytes), "cudaMalloc(mesh tangents)");
		if (ok) ok = cudaCheck(cudaMemcpy(reinterpret_cast<void*>(gas.tangents), tangents.data(), tangentsBytes, cudaMemcpyHostToDevice), "cudaMemcpy(mesh tangents)");
		if (ok) ok = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&gas.vertexColors), vertexColorsBytes), "cudaMalloc(mesh vertexColors)");
		if (ok) ok = cudaCheck(cudaMemcpy(reinterpret_cast<void*>(gas.vertexColors), vertexColors.data(), vertexColorsBytes, cudaMemcpyHostToDevice), "cudaMemcpy(mesh vertexColors)");
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

	// Uploads (and caches, by RtTextureSample pointer identity) one material
	// texture's rgba8 bytes to the device, populating an RtOptixTexture with
	// the resulting pointer plus its KHR_texture_transform/wrap/channel-
	// packing metadata copied verbatim - mirrors RtTextureSample's fields
	// exactly (see RtOptixSceneParams.h's RtOptixTexture doc comment).
	// Multiple materials/instances referencing the SAME RtTextureSample
	// object (a shared texture) upload its bytes only once.
	std::unordered_map<const RtTextureSample*, CUdeviceptr> textureCache;
	auto uploadMaterialTexture = [&](const std::shared_ptr<RtTextureSample>& tex, RtOptixTexture& out) -> void
	{
		out.rgba8 = nullptr;
		out.width = 0;
		out.height = 0;
		if (!tex || tex->width <= 0 || tex->height <= 0 ||
			tex->rgba8.size() != static_cast<size_t>(tex->width) * tex->height * 4)
			return;

		CUdeviceptr deviceRgba8 = 0;
		auto cached = textureCache.find(tex.get());
		if (cached != textureCache.end())
		{
			deviceRgba8 = cached->second;
		}
		else
		{
			const size_t bytes = tex->rgba8.size();
			if (!cudaCheck(cudaMalloc(reinterpret_cast<void**>(&deviceRgba8), bytes), "cudaMalloc(material texture)"))
				return;
			if (!cudaCheck(cudaMemcpy(reinterpret_cast<void*>(deviceRgba8), tex->rgba8.data(), bytes, cudaMemcpyHostToDevice), "cudaMemcpy(material texture)"))
			{
				cudaFree(reinterpret_cast<void*>(deviceRgba8));
				return;
			}
			_impl->textureBuffers.push_back(deviceRgba8);
			textureCache.emplace(tex.get(), deviceRgba8);
		}

		out.rgba8 = reinterpret_cast<const uchar4*>(deviceRgba8);
		out.width = tex->width;
		out.height = tex->height;
		out.texCoordIndex = std::clamp(tex->texCoordIndex, 0, 3);
		out.uvScale = make_float2(tex->uvScale.x, tex->uvScale.y);
		out.uvOffset = make_float2(tex->uvOffset.x, tex->uvOffset.y);
		out.uvRotation = tex->uvRotation;
		out.packingChannel = tex->packingChannel;
		out.packingInvert = tex->packingInvert ? 1 : 0;
		out.packingScale = tex->packingScale;
		out.packingBias = tex->packingBias;
		out.wrapS = tex->wrapS;
		out.wrapT = tex->wrapT;
	};

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
		hgSbt.data.normals = reinterpret_cast<float3*>(_impl->meshGasEntries[inst.meshIndex].normals);
		hgSbt.data.indices = reinterpret_cast<uint3*>(_impl->meshGasEntries[inst.meshIndex].indices);
		hgSbt.data.texCoords = reinterpret_cast<float2*>(_impl->meshGasEntries[inst.meshIndex].texCoords);
		hgSbt.data.tangents = reinterpret_cast<float4*>(_impl->meshGasEntries[inst.meshIndex].tangents);
		hgSbt.data.vertexColors = reinterpret_cast<float3*>(_impl->meshGasEntries[inst.meshIndex].vertexColors);

		if (inst.materialIndex < snapshot.materials.size())
		{
			const RtMaterial& mat = snapshot.materials[inst.materialIndex];
			hgSbt.data.baseColor = make_float3(mat.baseColor.x, mat.baseColor.y, mat.baseColor.z);
			hgSbt.data.metalness = mat.metalness;
			hgSbt.data.roughness = mat.roughness;
			hgSbt.data.emissive = make_float3(mat.emissive.x, mat.emissive.y, mat.emissive.z);
			hgSbt.data.emissiveStrength = mat.emissiveStrength;
			hgSbt.data.normalScale = mat.normalScale;

			uploadMaterialTexture(mat.baseColorTexture, hgSbt.data.baseColorTexture);
			uploadMaterialTexture(mat.metallicTexture, hgSbt.data.metallicTexture);
			uploadMaterialTexture(mat.roughnessTexture, hgSbt.data.roughnessTexture);
			uploadMaterialTexture(mat.normalTexture, hgSbt.data.normalTexture);
			uploadMaterialTexture(mat.emissiveTexture, hgSbt.data.emissiveTexture);
		}
		else
		{
			hgSbt.data.baseColor = make_float3(0.8f, 0.8f, 0.8f);
			hgSbt.data.metalness = 0.0f;
			hgSbt.data.roughness = 0.5f;
			hgSbt.data.emissive = make_float3(0.0f, 0.0f, 0.0f);
			hgSbt.data.emissiveStrength = 0.0f;
			hgSbt.data.normalScale = 1.0f;

			hgSbt.data.baseColorTexture.width = 0;
			hgSbt.data.metallicTexture.width = 0;
			hgSbt.data.roughnessTexture.width = 0;
			hgSbt.data.normalTexture.width = 0;
			hgSbt.data.emissiveTexture.width = 0;
		}

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

	// --- Lights (KHR_lights_punctual - see evaluatePunctualLight() in the
	// kernel) - flattened world-space list, same one the raster UBO and
	// CpuPathTracer both use (see RtLight's doc comment), so all three stay
	// in sync by construction. ---
	if (!snapshot.lights.empty())
	{
		std::vector<RtOptixLight> lightsHost(snapshot.lights.size());
		for (size_t i = 0; i < snapshot.lights.size(); ++i)
		{
			const RtLight& light = snapshot.lights[i];
			RtOptixLight& out = lightsHost[i];
			out.type = light.type;
			out.position = make_float3(light.position.x, light.position.y, light.position.z);
			out.direction = make_float3(light.direction.x, light.direction.y, light.direction.z);
			out.color = make_float3(light.color.x, light.color.y, light.color.z);
			out.intensity = light.intensity;
			out.range = light.range;
			out.innerConeCos = light.innerConeCos;
			out.outerConeCos = light.outerConeCos;
		}

		const size_t lightsBytes = lightsHost.size() * sizeof(RtOptixLight);
		if (!cudaCheck(cudaMalloc(reinterpret_cast<void**>(&_impl->lightsBuffer), lightsBytes), "cudaMalloc(lights)"))
			return false;
		if (!cudaCheck(cudaMemcpy(reinterpret_cast<void*>(_impl->lightsBuffer), lightsHost.data(), lightsBytes, cudaMemcpyHostToDevice), "cudaMemcpy(lights)"))
			return false;
		_impl->lightCount = static_cast<unsigned int>(lightsHost.size());
	}

	// --- Environment: raw (mip-0) cubemap, the same captured cubemap
	// CpuPathTracer's own raw-map sampling uses, plus the GGX-prefiltered
	// mip chain for roughness-blurred reflections (RtEnvironment::
	// prefilterMips - see RtOptixSceneParams.h's RtOptixEnvironment doc
	// comment). ---
	auto uploadCubemapFace = [](const std::vector<float>& faceData, int faceSize, CUdeviceptr& outBuffer) -> bool
	{
		if (faceData.size() != static_cast<size_t>(faceSize) * faceSize * 3)
			return false;

		std::vector<float3> faceFloat3(static_cast<size_t>(faceSize) * faceSize);
		for (size_t i = 0; i < faceFloat3.size(); ++i)
			faceFloat3[i] = make_float3(faceData[i * 3 + 0], faceData[i * 3 + 1], faceData[i * 3 + 2]);

		const size_t faceBytes = faceFloat3.size() * sizeof(float3);
		if (!cudaCheck(cudaMalloc(reinterpret_cast<void**>(&outBuffer), faceBytes), "cudaMalloc(env face)"))
			return false;
		if (!cudaCheck(cudaMemcpy(reinterpret_cast<void*>(outBuffer), faceFloat3.data(), faceBytes, cudaMemcpyHostToDevice), "cudaMemcpy(env face)"))
			return false;
		return true;
	};

	const RtEnvironment& env = snapshot.environment;
	_impl->envFaceSize = env.faceSize;
	_impl->envShowBackground = env.showBackground;
	_impl->envFallbackTopColor = env.fallbackTopColor;
	_impl->envFallbackBottomColor = env.fallbackBottomColor;
	_impl->envFallbackGradientStyle = env.fallbackGradientStyle;
	_impl->envCameraUpAxisZUp = env.cameraUpAxisZUp;
	_impl->envSkyBoxZRotationDegrees = env.skyBoxZRotationDegrees;
	_impl->envMapExposure = env.envMapExposure;

	if (env.faceSize > 0)
	{
		for (int face = 0; face < 6; ++face)
		{
			if (!uploadCubemapFace(env.faces[face], env.faceSize, _impl->envFaceBuffers[face]))
			{
				qWarning() << "RtOptixSceneTracer::buildScene(): environment face" << face << "has unexpected size - disabling environment.";
				_impl->envFaceSize = 0;
				break;
			}
		}
	}

	_impl->prefilterMipCount = 0;
	if (!env.prefilterMips.empty())
	{
		_impl->prefilterMipEntries.resize(env.prefilterMips.size());
		std::vector<RtOptixPrefilterMip> hostMips(env.prefilterMips.size());
		bool mipsOk = true;
		for (size_t m = 0; mipsOk && m < env.prefilterMips.size(); ++m)
		{
			const RtEnvironment::PrefilterMip& mip = env.prefilterMips[m];
			hostMips[m].faceSize = mip.faceSize;
			for (int face = 0; face < 6; ++face)
			{
				if (!uploadCubemapFace(mip.faces[face], mip.faceSize, _impl->prefilterMipEntries[m].faceBuffers[face]))
				{
					qWarning() << "RtOptixSceneTracer::buildScene(): prefilter mip" << static_cast<int>(m) << "face" << face
						<< "has unexpected size - disabling the prefilter chain (falling back to the raw map).";
					mipsOk = false;
					break;
				}
				hostMips[m].faces[face] = reinterpret_cast<float3*>(_impl->prefilterMipEntries[m].faceBuffers[face]);
			}
		}

		if (mipsOk)
		{
			const size_t mipsBytes = hostMips.size() * sizeof(RtOptixPrefilterMip);
			if (cudaCheck(cudaMalloc(reinterpret_cast<void**>(&_impl->prefilterMipsBuffer), mipsBytes), "cudaMalloc(prefilter mips)") &&
				cudaCheck(cudaMemcpy(reinterpret_cast<void*>(_impl->prefilterMipsBuffer), hostMips.data(), mipsBytes, cudaMemcpyHostToDevice), "cudaMemcpy(prefilter mips)"))
			{
				_impl->prefilterMipCount = static_cast<int>(hostMips.size());
			}
		}
	}

	qInfo() << "RtOptixSceneTracer: scene built (" << _impl->meshGasEntries.size() << "meshes,"
		<< instances.size() << "instances," << _impl->lightCount << "lights).";
	return true;
}

bool RtOptixSceneTracer::renderScene(const RtCamera& camera, int width, int height, unsigned int samplesPerPixel, unsigned int maxBounces,
	std::vector<glm::vec3>& outImageLinearRgb, std::vector<glm::vec3>& outAlbedo, std::vector<glm::vec3>& outNormal)
{
	if (!_impl->valid || _impl->iasHandle == 0 || width <= 0 || height <= 0)
		return false;

	const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
	CUdeviceptr dImage = 0, dAlbedo = 0, dNormal = 0;
	bool allocOk = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&dImage), pixelCount * sizeof(float3)), "cudaMalloc(output image)");
	if (allocOk)
		allocOk = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&dAlbedo), pixelCount * sizeof(float3)), "cudaMalloc(albedo guide image)");
	if (allocOk)
		allocOk = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&dNormal), pixelCount * sizeof(float3)), "cudaMalloc(normal guide image)");
	if (!allocOk)
	{
		if (dNormal) cudaFree(reinterpret_cast<void*>(dNormal));
		if (dAlbedo) cudaFree(reinterpret_cast<void*>(dAlbedo));
		if (dImage) cudaFree(reinterpret_cast<void*>(dImage));
		return false;
	}

	RtOptixSceneParams params{};
	params.image = reinterpret_cast<float3*>(dImage);
	params.albedoImage = reinterpret_cast<float3*>(dAlbedo);
	params.normalImage = reinterpret_cast<float3*>(dNormal);
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
	params.lights = reinterpret_cast<const RtOptixLight*>(_impl->lightsBuffer);
	params.lightCount = _impl->lightCount;

	for (int face = 0; face < 6; ++face)
		params.environment.faces[face] = reinterpret_cast<float3*>(_impl->envFaceBuffers[face]);
	params.environment.faceSize = _impl->envFaceSize;
	params.environment.showBackground = _impl->envShowBackground ? 1 : 0;
	params.environment.fallbackTopColor = make_float3(_impl->envFallbackTopColor.x, _impl->envFallbackTopColor.y, _impl->envFallbackTopColor.z);
	params.environment.fallbackBottomColor = make_float3(_impl->envFallbackBottomColor.x, _impl->envFallbackBottomColor.y, _impl->envFallbackBottomColor.z);
	params.environment.fallbackGradientStyle = _impl->envFallbackGradientStyle;
	params.environment.cameraUpAxisZUp = _impl->envCameraUpAxisZUp ? 1 : 0;
	params.environment.skyBoxZRotationDegrees = _impl->envSkyBoxZRotationDegrees;
	params.environment.envMapExposure = _impl->envMapExposure;
	params.environment.prefilterMips = reinterpret_cast<const RtOptixPrefilterMip*>(_impl->prefilterMipsBuffer);
	params.environment.prefilterMipCount = _impl->prefilterMipCount;
	params.samplesPerPixel = samplesPerPixel > 0 ? samplesPerPixel : 1;
	params.maxBounces = maxBounces > 0 ? maxBounces : 1;

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
		// float3 and glm::vec3 are both three tight-packed floats (no
		// padding/alignment mismatch - same layout assumption already made
		// throughout this file, e.g. camPosition/camForward above), so the
		// device buffers can be read straight into the glm::vec3 vectors.
		outImageLinearRgb.resize(pixelCount);
		outAlbedo.resize(pixelCount);
		outNormal.resize(pixelCount);
		ok = cudaCheck(cudaMemcpy(outImageLinearRgb.data(), reinterpret_cast<void*>(dImage), pixelCount * sizeof(float3), cudaMemcpyDeviceToHost), "cudaMemcpy(readback image)");
		if (ok)
			ok = cudaCheck(cudaMemcpy(outAlbedo.data(), reinterpret_cast<void*>(dAlbedo), pixelCount * sizeof(float3), cudaMemcpyDeviceToHost), "cudaMemcpy(readback albedo)");
		if (ok)
			ok = cudaCheck(cudaMemcpy(outNormal.data(), reinterpret_cast<void*>(dNormal), pixelCount * sizeof(float3), cudaMemcpyDeviceToHost), "cudaMemcpy(readback normal)");
	}

	if (dParams) cudaFree(reinterpret_cast<void*>(dParams));
	cudaFree(reinterpret_cast<void*>(dImage));
	cudaFree(reinterpret_cast<void*>(dAlbedo));
	cudaFree(reinterpret_cast<void*>(dNormal));

	if (!ok)
	{
		outImageLinearRgb.clear();
		outAlbedo.clear();
		outNormal.clear();
	}
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

bool RtOptixSceneTracer::renderScene(const RtCamera&, int, int, unsigned int, unsigned int, std::vector<glm::vec3>&, std::vector<glm::vec3>&, std::vector<glm::vec3>&)
{
	return false;
}

#endif // MODELVIEWER_HAVE_OPTIX
