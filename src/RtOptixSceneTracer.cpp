#include "RtOptixSceneTracer.h"

#include <QDebug>

#ifdef MODELVIEWER_HAVE_OPTIX

#include <cuda_runtime.h>

#include <optix.h>
#include <optix_stack_size.h>
#include <optix_stubs.h>
// NOTE: optix_function_table_definition.h must be included in exactly one
// translation unit across the whole program - that's RtOptixContext.cpp.

#include "RtEnvironmentSampler.h"
#include "RtOptixEmbeddedPtx.h"
#include "RtOptixSceneParams.h"
#include "PathUtils.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace
{
	QString resolveKhronosLUTPath(const QString& fileName)
	{
		const QString dataCandidate = QDir(PathUtils::getDataDirectory()).absoluteFilePath(
			"textures/khronos/" + fileName);
		if (QFileInfo::exists(dataCandidate))
			return dataCandidate;
		const QString sourceCandidate = QDir(QDir::currentPath()).absoluteFilePath(
			"textures/khronos/" + fileName);
		if (QFileInfo::exists(sourceCandidate))
			return sourceCandidate;
		return dataCandidate;
	}

	std::vector<float> loadKhronosScalarLUT(const QString& fileName, int channel, int lutSize)
	{
		const QString path = resolveKhronosLUTPath(fileName);
		QImage image(path);
		if (image.isNull())
			return {};

		QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
		std::vector<float> table(static_cast<size_t>(lutSize) * lutSize);
		for (int ri = 0; ri < lutSize; ++ri)
		{
			const float roughness = (ri + 0.5f) / static_cast<float>(lutSize);
			const int y = std::clamp(static_cast<int>(roughness * static_cast<float>(rgba.height() - 1)), 0, rgba.height() - 1);
			for (int vi = 0; vi < lutSize; ++vi)
			{
				const float ndotv = (vi + 0.5f) / static_cast<float>(lutSize);
				const int x = std::clamp(static_cast<int>(ndotv * static_cast<float>(rgba.width() - 1)), 0, rgba.width() - 1);
				const QRgb pixel = rgba.pixel(x, y);
				const int component = channel == 0 ? qRed(pixel) : (channel == 1 ? qGreen(pixel) : (channel == 2 ? qBlue(pixel) : qAlpha(pixel)));
				table[static_cast<size_t>(ri) * lutSize + vi] = static_cast<float>(component) / 255.0f;
			}
		}
		return table;
	}

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

	// Environment cubemap (raw/mip-0) face buffers. ONLY the heavy texel
	// data lives here (uploaded at revision-gated buildScene() time) - the
	// cheap environment SCALARS (showBackground/exposure/rotation/fallback
	// colors...) are deliberately NOT captured into this struct: they flow
	// per-launch through renderScene()'s environment parameter instead, so
	// a lightweight setting change (e.g. the user tweaking Env Map Exposure
	// or the background gradient colors) takes effect on the very next
	// camera-settle restart without needing a scene-revision bump/full
	// GAS-and-texture rebuild. An earlier version captured the scalars here
	// too, which - once buildScene() became genuinely revision-gated - left
	// them stale on the GPU while the CPU tracer (which reads its snapshot
	// fresh every render) picked the same changes up.
	CUdeviceptr envFaceBuffers[6] = { 0, 0, 0, 0, 0, 0 };
	int envFaceSize = 0;

	// Diffuse-convolved irradiance cubemap (RtEnvironment::irradianceFaces) -
	// same upload pattern as envFaceBuffers/envFaceSize above, just a
	// separate single-level cubemap. Used for indirect bounces whose most
	// recent surface interaction was a diffuse (cosine-weighted) lobe - see
	// RtOptixScene.cu's sampleEnvironmentDiffuse() and CpuPathTracer.cpp's
	// identically-named function/RtEnvironment::irradianceFaces' doc comment
	// for the full rationale. This backend used to fall back to the
	// roughest GGX-prefiltered specular mip as a stand-in here - NOT
	// equivalent to a true Lambertian convolution (GGX importance sampling
	// at roughness=1 still weights differently and retains more directional
	// structure), which was inflating diffuse-lobe environment escapes
	// relative to CPU - most visible on scenes with no punctual lights where
	// environment light is the only illumination and diffuse escapes
	// dominate (e.g. KHR_materials_volume_scatter's ScatteringSkull.gltf).
	CUdeviceptr irradianceFaceBuffers[6] = { 0, 0, 0, 0, 0, 0 };
	int irradianceFaceSize = 0;

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
	std::vector<PrefilterMipGpu> sheenPrefilterMipEntries;
	CUdeviceptr sheenPrefilterMipsBuffer = 0;
	int sheenPrefilterMipCount = 0;

	// Environment-light NEE + MIS - device upload of a host-built
	// RtEnvironmentSampler's raw distribution (see RtOptixSceneParams.h's
	// RtOptixEnvironment::envFlatCdf doc comment). Rebuilt/reuploaded
	// alongside the environment cubemap above (same revision-gated
	// buildScene() call), not per-launch.
	CUdeviceptr envFlatCdfBuffer = 0;
	CUdeviceptr envTexelPdfBuffer = 0;
	float envTotalWeight = 0.0f;

	OptixModule module = nullptr;
	OptixPipeline pipeline = nullptr;
	OptixProgramGroup raygenGroup = nullptr;
	OptixProgramGroup missGroup = nullptr;
	OptixProgramGroup hitgroupGroup = nullptr;

	CUdeviceptr raygenRecord = 0;
	CUdeviceptr missRecord = 0;
	CUdeviceptr hitgroupRecords = 0; // one HitGroupSbtRecord per instance
	OptixShaderBindingTable sbt = {};

	// KHR_materials_sheen's directional-albedo LUT (RtOptixSceneParams::
	// sheenAlbedoLUT) - a process-constant table (depends on nothing but the
	// Charlie BRDF), so it's baked and uploaded ONCE (lazily, on the first
	// buildScene() call) rather than per-scene-revision like the rest of
	// freeSceneBuffers()'s buffers - see ensureSheenAlbedoLut().
	static constexpr int kSheenAlbedoLutSize = 128;
	CUdeviceptr sheenAlbedoLutBuffer = 0;
	CUdeviceptr sheenCharlieLutBuffer = 0;

	void ensureSheenAlbedoLut();

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

		for (CUdeviceptr& face : irradianceFaceBuffers)
		{
			if (face) cudaFree(reinterpret_cast<void*>(face));
			face = 0;
		}
		irradianceFaceSize = 0;

		for (PrefilterMipGpu& mip : prefilterMipEntries)
			for (CUdeviceptr& face : mip.faceBuffers)
				if (face) cudaFree(reinterpret_cast<void*>(face));
		prefilterMipEntries.clear();
		if (prefilterMipsBuffer) cudaFree(reinterpret_cast<void*>(prefilterMipsBuffer));
		prefilterMipsBuffer = 0;
		prefilterMipCount = 0;

		for (PrefilterMipGpu& mip : sheenPrefilterMipEntries)
			for (CUdeviceptr& face : mip.faceBuffers)
				if (face) cudaFree(reinterpret_cast<void*>(face));
		sheenPrefilterMipEntries.clear();
		if (sheenPrefilterMipsBuffer) cudaFree(reinterpret_cast<void*>(sheenPrefilterMipsBuffer));
		sheenPrefilterMipsBuffer = 0;
		sheenPrefilterMipCount = 0;

		if (envFlatCdfBuffer) cudaFree(reinterpret_cast<void*>(envFlatCdfBuffer));
		envFlatCdfBuffer = 0;
		if (envTexelPdfBuffer) cudaFree(reinterpret_cast<void*>(envTexelPdfBuffer));
		envTexelPdfBuffer = 0;
		envTotalWeight = 0.0f;

		if (hitgroupRecords) cudaFree(reinterpret_cast<void*>(hitgroupRecords));
		hitgroupRecords = 0;
		sbt.hitgroupRecordBase = 0;
		sbt.hitgroupRecordCount = 0;
	}

	~Impl()
	{
		freeSceneBuffers();
		if (sheenAlbedoLutBuffer) cudaFree(reinterpret_cast<void*>(sheenAlbedoLutBuffer));
		if (sheenCharlieLutBuffer) cudaFree(reinterpret_cast<void*>(sheenCharlieLutBuffer));
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

namespace
{
	// Host-side bake of KHR_materials_sheen's directional-albedo LUT - mirrors
	// CpuPathTracer::sheenAlbedoLUT()'s algorithm/fixed seed exactly (see that
	// function's doc comment for why a small Monte-Carlo-baked table stands in
	// for main_scene.frag's baked sheenELUT texture), just using a local
	// deterministic RNG instead of CpuPathTracer.cpp's private Rng type.
	// Row-major [roughness][NdotV], matching RtOptixSceneParams::
	// sheenAlbedoLUT's doc comment and RtOptixScene.cu's sampleSheenAlbedoLUT()
	// device-side lookup.
	float distributionCharlieHost(float NdotH, float roughness)
	{
		const float alpha = (std::max)(roughness * roughness, 0.000001f);
		const float invAlpha = 1.0f / alpha;
		const float sin2h = (std::max)(1.0f - NdotH * NdotH, 0.0078125f); // 2^(-7)
		return (2.0f + invAlpha) * std::pow(sin2h, invAlpha * 0.5f) / (2.0f * glm::pi<float>());
	}

	float distributionCharlieLegacyHost(float NdotH, float roughness)
	{
		const float invAlpha = 1.0f / (std::max)(roughness, 0.001f);
		const float sin2h = (std::max)(1.0f - NdotH * NdotH, 0.0078125f); // 2^(-7)
		return (2.0f + invAlpha) * std::pow(sin2h, invAlpha * 0.5f) / (2.0f * glm::pi<float>());
	}

	float lambdaSheenNumericHelperHost(float x, float alphaG)
	{
		const float oneMinusAlphaSq = (1.0f - alphaG) * (1.0f - alphaG);
		const float a = glm::mix(21.5473f, 25.3245f, oneMinusAlphaSq);
		const float b = glm::mix(3.82987f, 3.32435f, oneMinusAlphaSq);
		const float c = glm::mix(0.19823f, 0.16801f, oneMinusAlphaSq);
		const float d = glm::mix(-1.97760f, -1.27393f, oneMinusAlphaSq);
		const float e = glm::mix(-4.32054f, -4.85967f, oneMinusAlphaSq);
		return a / (1.0f + b * std::pow(x, c)) + d * x + e;
	}

	float lambdaSheenHost(float cosTheta, float alphaG)
	{
		if (std::abs(cosTheta) < 0.5f)
			return std::exp(lambdaSheenNumericHelperHost(cosTheta, alphaG));
		return std::exp(2.0f * lambdaSheenNumericHelperHost(0.5f, alphaG) -
			lambdaSheenNumericHelperHost(1.0f - cosTheta, alphaG));
	}

	float visibilitySheenHost(float NdotL, float NdotV, float sheenRoughness)
	{
		sheenRoughness = (std::max)(sheenRoughness, 0.000001f);
		const float alphaG = sheenRoughness * sheenRoughness;
		return std::clamp(1.0f / ((1.0f + lambdaSheenHost(NdotV, alphaG) + lambdaSheenHost(NdotL, alphaG)) * (4.0f * NdotV * NdotL)), 0.0f, 1.0f);
	}

	// Small deterministic PCG-style hash RNG - fixed seed, only ever used for
	// this one-time bake, so it doesn't need to match the kernel's own
	// per-pixel RNG stream.
	struct LutBakeRng
	{
		unsigned int state;
		float next01()
		{
			state = state * 747796405u + 2891336453u;
			unsigned int word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
			word = (word >> 22u) ^ word;
			return static_cast<float>(word) / 4294967296.0f;
		}
	};

	std::vector<float> bakeSheenAlbedoLutHost(int lutSize)
	{
		constexpr int kBakeSamples = 256;
		std::vector<float> table(static_cast<size_t>(lutSize) * lutSize);
		LutBakeRng rng{ 0x5EEE17u };
		for (int ri = 0; ri < lutSize; ++ri)
		{
			const float roughness = (ri + 0.5f) / lutSize;
			for (int vi = 0; vi < lutSize; ++vi)
			{
				const float NdotV = (std::max)((vi + 0.5f) / lutSize, 1e-4f);
				const glm::vec3 V(std::sqrt((std::max)(0.0f, 1.0f - NdotV * NdotV)), 0.0f, NdotV);

				float sum = 0.0f;
				for (int s = 0; s < kBakeSamples; ++s)
				{
					// Cosine-weighted hemisphere sample in the local frame
					// (N=+Z) - matches CpuPathTracer::cosineSampleHemisphere()
					// exactly, inlined here to avoid depending on that TU.
					const float u1 = rng.next01();
					const float u2 = rng.next01();
					const float r = std::sqrt(u1);
					const float phi = 2.0f * glm::pi<float>() * u2;
					const glm::vec3 L(r * std::cos(phi), r * std::sin(phi), std::sqrt((std::max)(0.0f, 1.0f - u1)));

					const float NdotL = (std::max)(L.z, 1e-4f);
					const glm::vec3 H = glm::normalize(V + L);
					const float NdotH = std::clamp(H.z, 0.0f, 1.0f);
					sum += distributionCharlieHost(NdotH, roughness) * visibilitySheenHost(NdotL, NdotV, roughness) * glm::pi<float>();
				}
				table[static_cast<size_t>(ri) * lutSize + vi] = sum / kBakeSamples;
			}
		}
		return table;
	}

	float radicalInverseVdCHost(uint32_t bits)
	{
		bits = (bits << 16u) | (bits >> 16u);
		bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
		bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
		bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
		bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
		return static_cast<float>(bits) * 2.3283064365386963e-10f;
	}

	glm::vec3 importanceSampleCharlieHost(const glm::vec2& xi, float sheenRoughness)
	{
		const float alpha = (std::max)(sheenRoughness, 0.001f);
		const float phi = 2.0f * glm::pi<float>() * xi.x;
		const float sinTheta = std::pow(xi.y, alpha / (2.0f * alpha + 1.0f));
		const float cosTheta = std::sqrt((std::max)(1.0f - sinTheta * sinTheta, 0.0f));
		return glm::normalize(glm::vec3(std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta));
	}

	std::vector<float> bakeSheenCharlieLutHost(int lutSize)
	{
		constexpr int kBakeSamples = 1024;
		std::vector<float> table(static_cast<size_t>(lutSize) * lutSize);
		for (int ri = 0; ri < lutSize; ++ri)
		{
			const float roughness = (ri + 0.5f) / lutSize;
			for (int vi = 0; vi < lutSize; ++vi)
			{
				const float NdotV = (std::max)((vi + 0.5f) / lutSize, 1e-4f);
				const glm::vec3 V(std::sqrt((std::max)(0.0f, 1.0f - NdotV * NdotV)), 0.0f, NdotV);

				float sum = 0.0f;
				for (int s = 0; s < kBakeSamples; ++s)
				{
					const glm::vec2 xi((s + 0.5f) / kBakeSamples, radicalInverseVdCHost(static_cast<uint32_t>(s)));
					const glm::vec3 H = importanceSampleCharlieHost(xi, roughness);
					const glm::vec3 L = glm::normalize(2.0f * glm::dot(V, H) * H - V);
					const float NdotL = (std::max)(L.z, 0.0f);
					const float NdotH = (std::max)(H.z, 0.0f);
					if (NdotL > 0.0f && NdotH > 0.0f)
						sum += distributionCharlieLegacyHost(NdotH, roughness) * visibilitySheenHost(NdotL, NdotV, roughness) * NdotL;
				}
				table[static_cast<size_t>(ri) * lutSize + vi] = sum / kBakeSamples;
			}
		}
		return table;
	}
}

void RtOptixSceneTracer::Impl::ensureSheenAlbedoLut()
{
	if (sheenAlbedoLutBuffer && sheenCharlieLutBuffer)
		return; // already baked/uploaded - process-constant, never needs a rebuild

	if (!sheenAlbedoLutBuffer)
	{
		std::vector<float> lut = loadKhronosScalarLUT(QStringLiteral("lut_sheen_E.png"), 0, kSheenAlbedoLutSize);
		if (lut.empty())
			lut = bakeSheenAlbedoLutHost(kSheenAlbedoLutSize);
		const size_t bytes = lut.size() * sizeof(float);
		if (!cudaCheck(cudaMalloc(reinterpret_cast<void**>(&sheenAlbedoLutBuffer), bytes), "cudaMalloc(sheen albedo LUT)"))
		{
			sheenAlbedoLutBuffer = 0;
			return;
		}
		if (!cudaCheck(cudaMemcpy(reinterpret_cast<void*>(sheenAlbedoLutBuffer), lut.data(), bytes, cudaMemcpyHostToDevice), "cudaMemcpy(sheen albedo LUT)"))
		{
			cudaFree(reinterpret_cast<void*>(sheenAlbedoLutBuffer));
			sheenAlbedoLutBuffer = 0;
		}
	}

	if (!sheenCharlieLutBuffer)
	{
		std::vector<float> lut = loadKhronosScalarLUT(QStringLiteral("lut_charlie.png"), 2, kSheenAlbedoLutSize);
		if (lut.empty())
			lut = bakeSheenCharlieLutHost(kSheenAlbedoLutSize);
		const size_t bytes = lut.size() * sizeof(float);
		if (!cudaCheck(cudaMalloc(reinterpret_cast<void**>(&sheenCharlieLutBuffer), bytes), "cudaMalloc(sheen Charlie LUT)"))
		{
			sheenCharlieLutBuffer = 0;
			return;
		}
		if (!cudaCheck(cudaMemcpy(reinterpret_cast<void*>(sheenCharlieLutBuffer), lut.data(), bytes, cudaMemcpyHostToDevice), "cudaMemcpy(sheen Charlie LUT)"))
		{
			cudaFree(reinterpret_cast<void*>(sheenCharlieLutBuffer));
			sheenCharlieLutBuffer = 0;
		}
	}
}

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
	// roughness in (for the miss shader, if THIS ray escapes), p19 =
	// previous-BSDF pdf in / next-BSDF pdf out for env-MIS - see
	// RtOptixScene.cu's traceBouncePath()/__closesthit__ch() doc comments.
	// Shadow rays reuse p0 as an occluded bool and p1/p2 for self-shadow
	// filtering (see traceShadowRay()/__anyhit__ah()).
	pipelineCompileOptions.numPayloadValues = 20;
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
	// Any-hit program - only actually rejects a hit for glTF alphaMode
	// Masked materials below their alphaCutoff (see RtOptixScene.cu's
	// __anyhit__ah() doc comment); every other case accepts the hit
	// immediately (the default when an any-hit program does nothing).
	hitgroupDesc.hitgroup.moduleAH = _impl->module;
	hitgroupDesc.hitgroup.entryFunctionNameAH = "__anyhit__ah";
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

	_impl->ensureSheenAlbedoLut(); // process-constant, baked once - see its own doc comment
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
		// ALLOW_RANDOM_VERTEX_ACCESS is required for __closesthit__ch()'s
		// optixGetTriangleVertexData() call (texture-footprint/LOD
		// computation - see its call site's doc comment) to legally read a
		// hit triangle's object-space vertex positions back out of this GAS.
		accelOptions.buildFlags = OPTIX_BUILD_FLAG_ALLOW_RANDOM_VERTEX_ACCESS;
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

	// Uploaded mip pyramid for one RtTextureSample (device array of
	// RtOptixTextureMipLevel entries, each pointing at its own uploaded
	// rgba8 buffer) - deduplicated by RtTextureSample pointer identity like
	// textureCache above. mipArrayDevice==0 means "no mips uploaded" (either
	// the source RtTextureSample::mips was empty, or an upload failed) - out.
	// mips/mipCount then stay nullptr/0, and RtOptixScene.cu's
	// sampleTexture2D() falls back to base-level-only sampling.
	struct MipCacheEntry { CUdeviceptr mipArrayDevice; int mipCount; };
	std::unordered_map<const RtTextureSample*, MipCacheEntry> textureMipCache;

	auto uploadMaterialTexture = [&](const std::shared_ptr<RtTextureSample>& tex, RtOptixTexture& out) -> void
	{
		out.rgba8 = nullptr;
		out.width = 0;
		out.height = 0;
		out.mips = nullptr;
		out.mipCount = 0;
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

		// Box-filter mip pyramid (RtTextureSample::mips, built once on the
		// host by RtSceneBuilder::buildMipChain()) - each level's rgba8
		// uploaded individually, then a device array of RtOptixTextureMipLevel
		// {devicePtr,width,height} entries uploaded once, mirroring
		// RtOptixEnvironment::prefilterMips' own per-level-array upload
		// pattern. See RtOptixTexture::mips' doc comment for the consumer.
		auto mipCached = textureMipCache.find(tex.get());
		if (mipCached != textureMipCache.end())
		{
			out.mips = reinterpret_cast<const RtOptixTextureMipLevel*>(mipCached->second.mipArrayDevice);
			out.mipCount = mipCached->second.mipCount;
			return;
		}

		CUdeviceptr mipArrayDevice = 0;
		int mipCount = 0;
		if (!tex->mips.empty())
		{
			std::vector<RtOptixTextureMipLevel> mipsHost;
			mipsHost.reserve(tex->mips.size());
			bool ok = true;
			bool first = true;
			for (const RtTextureMipLevel& mip : tex->mips)
			{
				if (mip.width <= 0 || mip.height <= 0 ||
					mip.rgba8.size() != static_cast<size_t>(mip.width) * mip.height * 4)
				{
					ok = false;
					break;
				}

				// mips[0] is guaranteed byte-identical to the base level just
				// uploaded above as deviceRgba8 (RtSceneBuilder::buildMipChain()
				// constructs it as a straight copy of sample.rgba8, kept only so
				// the CPU tracer can index mips[0..N] uniformly rather than
				// special-casing level 0 against width/height/rgba8 directly -
				// see RtTextureSample::mips' doc comment). Re-uploading that same
				// data to a second VRAM buffer here would double the resident
				// cost of every texture's base level for no benefit - reuse the
				// buffer already uploaded instead of allocating a duplicate.
				if (first && mip.width == tex->width && mip.height == tex->height)
				{
					RtOptixTextureMipLevel entry{};
					entry.rgba8 = reinterpret_cast<const uchar4*>(deviceRgba8);
					entry.width = mip.width;
					entry.height = mip.height;
					mipsHost.push_back(entry);
					first = false;
					continue;
				}
				first = false;

				CUdeviceptr deviceMipRgba8 = 0;
				const size_t mipBytes = mip.rgba8.size();
				if (!cudaCheck(cudaMalloc(reinterpret_cast<void**>(&deviceMipRgba8), mipBytes), "cudaMalloc(material texture mip)"))
				{
					ok = false;
					break;
				}
				if (!cudaCheck(cudaMemcpy(reinterpret_cast<void*>(deviceMipRgba8), mip.rgba8.data(), mipBytes, cudaMemcpyHostToDevice), "cudaMemcpy(material texture mip)"))
				{
					cudaFree(reinterpret_cast<void*>(deviceMipRgba8));
					ok = false;
					break;
				}
				_impl->textureBuffers.push_back(deviceMipRgba8);

				RtOptixTextureMipLevel entry{};
				entry.rgba8 = reinterpret_cast<const uchar4*>(deviceMipRgba8);
				entry.width = mip.width;
				entry.height = mip.height;
				mipsHost.push_back(entry);
			}

			if (ok && !mipsHost.empty())
			{
				const size_t arrayBytes = mipsHost.size() * sizeof(RtOptixTextureMipLevel);
				const bool mallocOk = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&mipArrayDevice), arrayBytes), "cudaMalloc(material texture mip array)");
				if (mallocOk && cudaCheck(cudaMemcpy(reinterpret_cast<void*>(mipArrayDevice), mipsHost.data(), arrayBytes, cudaMemcpyHostToDevice), "cudaMemcpy(material texture mip array)"))
				{
					_impl->textureBuffers.push_back(mipArrayDevice);
					mipCount = static_cast<int>(mipsHost.size());
				}
				else
				{
					// mallocOk but the memcpy failed: mipArrayDevice is a real,
					// still-live allocation that was never pushed onto
					// textureBuffers (only a successful upload gets tracked
					// there for freeSceneBuffers() to free later) - free it
					// here directly or it's orphaned with no remaining pointer
					// to reach it. If mallocOk is false, mipArrayDevice is
					// already 0 and cudaFree(nullptr) is a harmless no-op.
					if (mallocOk)
						cudaFree(reinterpret_cast<void*>(mipArrayDevice));
					mipArrayDevice = 0;
				}
			}
		}

		textureMipCache.emplace(tex.get(), MipCacheEntry{ mipArrayDevice, mipCount });
		out.mips = reinterpret_cast<const RtOptixTextureMipLevel*>(mipArrayDevice);
		out.mipCount = mipCount;
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
		// Deliberately NOT flipped for negative-determinant (mirrored)
		// instances, despite an earlier version of this code doing so via
		// OPTIX_INSTANCE_FLAG_FLIP_TRIANGLE_FACING. OptiX transforms the RAY
		// into object space and tests winding there against the triangle's
		// own local normal - algebraically identical to the mathematically
		// correct world-space test (dot(rayDir_world, inverseTranspose(M)*
		// n_local)) for ANY invertible M, reflections included:
		//   sign(dot(rayDir_world, (M^-1)^T * n_local))
		//     = sign(dot(M^-1 * rayDir_world, n_local))   [transpose identity]
		//     = sign(dot(rayDir_objectSpace, n_local))     <- what OptiX computes natively
		// So optixIsTriangleFrontFaceHit() is ALREADY correct without this
		// flag, for both positive- and negative-determinant instances -
		// applying the flag only to negative-determinant ones double-flips
		// an already-correct answer. Confirmed by testing against glTF's
		// NegativeScaleTest.gltf: adding the flag fixed the positive-scale
		// row (which was never flipped) while breaking the negative-scale
		// row (which was), exactly the tell.
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
			hgSbt.data.ior = mat.ior;
			hgSbt.data.specularFactor = mat.specularFactor;
			hgSbt.data.specularColorFactor = make_float3(mat.specularColorFactor.x, mat.specularColorFactor.y, mat.specularColorFactor.z);
			hgSbt.data.occlusionStrength = mat.occlusionStrength;
			hgSbt.data.blendMode = mat.blendMode;
			hgSbt.data.alphaThreshold = mat.alphaThreshold;
			hgSbt.data.twoSided = mat.twoSided ? 1 : 0;
			hgSbt.data.opacity = mat.opacity;
			hgSbt.data.normalScale = mat.normalScale;
			hgSbt.data.clearcoat = mat.clearcoat;
			hgSbt.data.clearcoatRoughness = mat.clearcoatRoughness;
			hgSbt.data.clearcoatNormalScale = mat.clearcoatNormalScale;
			hgSbt.data.sheenColorFactor = make_float3(mat.sheenColor.x, mat.sheenColor.y, mat.sheenColor.z);
			hgSbt.data.sheenRoughness = mat.sheenRoughness;
			hgSbt.data.anisotropyStrength = mat.anisotropyStrength;
			hgSbt.data.anisotropyRotation = mat.anisotropyRotation;
			hgSbt.data.iridescenceFactor = mat.iridescenceFactor;
			hgSbt.data.iridescenceIor = mat.iridescenceIor;
			hgSbt.data.iridescenceThickness = mat.iridescenceThicknessMax;
			hgSbt.data.useSpecGloss = mat.useSpecGloss ? 1 : 0;
			hgSbt.data.diffuseColor = make_float3(mat.diffuseColor.x, mat.diffuseColor.y, mat.diffuseColor.z);
			hgSbt.data.specGlossSpecularColor = make_float3(mat.specGlossSpecularColor.x, mat.specGlossSpecularColor.y, mat.specGlossSpecularColor.z);
			hgSbt.data.glossinessFactor = mat.glossinessFactor;
			hgSbt.data.diffuseTransmissionFactor = mat.diffuseTransmissionFactor;
			hgSbt.data.diffuseTransmissionColor = make_float3(mat.diffuseTransmissionColor.x, mat.diffuseTransmissionColor.y, mat.diffuseTransmissionColor.z);
			hgSbt.data.transmission = mat.transmission;
			hgSbt.data.hasVolume = mat.hasVolume ? 1 : 0;
			hgSbt.data.attenuationColor = make_float3(mat.attenuationColor.x, mat.attenuationColor.y, mat.attenuationColor.z);
			hgSbt.data.attenuationDistance = mat.attenuationDistance;
			hgSbt.data.thicknessFactor = mat.thicknessFactor;
			hgSbt.data.dispersion = mat.dispersion;
			hgSbt.data.multiScatterColor = make_float3(mat.multiScatterColor.x, mat.multiScatterColor.y, mat.multiScatterColor.z);
			hgSbt.data.hasVolumeScattering = mat.hasVolumeScattering ? 1 : 0;

			uploadMaterialTexture(mat.baseColorTexture, hgSbt.data.baseColorTexture);
			uploadMaterialTexture(mat.metallicTexture, hgSbt.data.metallicTexture);
			uploadMaterialTexture(mat.roughnessTexture, hgSbt.data.roughnessTexture);
			uploadMaterialTexture(mat.normalTexture, hgSbt.data.normalTexture);
			uploadMaterialTexture(mat.emissiveTexture, hgSbt.data.emissiveTexture);
			uploadMaterialTexture(mat.aoTexture, hgSbt.data.aoTexture);
			uploadMaterialTexture(mat.opacityTexture, hgSbt.data.opacityTexture);
			uploadMaterialTexture(mat.specularTexture, hgSbt.data.specularTexture);
			uploadMaterialTexture(mat.specularColorTexture, hgSbt.data.specularColorTexture);
			uploadMaterialTexture(mat.clearcoatTexture, hgSbt.data.clearcoatTexture);
			uploadMaterialTexture(mat.clearcoatRoughnessTexture, hgSbt.data.clearcoatRoughnessTexture);
			uploadMaterialTexture(mat.clearcoatNormalTexture, hgSbt.data.clearcoatNormalTexture);
			uploadMaterialTexture(mat.sheenColorTexture, hgSbt.data.sheenColorTexture);
			uploadMaterialTexture(mat.sheenRoughnessTexture, hgSbt.data.sheenRoughnessTexture);
			uploadMaterialTexture(mat.anisotropyTexture, hgSbt.data.anisotropyTexture);
			uploadMaterialTexture(mat.iridescenceTexture, hgSbt.data.iridescenceTexture);
			uploadMaterialTexture(mat.iridescenceThicknessTexture, hgSbt.data.iridescenceThicknessTexture);
			uploadMaterialTexture(mat.diffuseTexture, hgSbt.data.diffuseTexture);
			uploadMaterialTexture(mat.specularGlossinessTexture, hgSbt.data.specularGlossinessTexture);
			uploadMaterialTexture(mat.diffuseTransmissionTexture, hgSbt.data.diffuseTransmissionTexture);
			uploadMaterialTexture(mat.diffuseTransmissionColorTexture, hgSbt.data.diffuseTransmissionColorTexture);
			uploadMaterialTexture(mat.transmissionTexture, hgSbt.data.transmissionTexture);
		}
		else
		{
			hgSbt.data.baseColor = make_float3(0.8f, 0.8f, 0.8f);
			hgSbt.data.metalness = 0.0f;
			hgSbt.data.roughness = 0.5f;
			hgSbt.data.emissive = make_float3(0.0f, 0.0f, 0.0f);
			hgSbt.data.emissiveStrength = 0.0f;
			hgSbt.data.ior = 1.5f;
			hgSbt.data.specularFactor = 1.0f;
			hgSbt.data.specularColorFactor = make_float3(1.0f, 1.0f, 1.0f);
			hgSbt.data.occlusionStrength = 1.0f;
			hgSbt.data.blendMode = 0;
			hgSbt.data.alphaThreshold = 0.5f;
			hgSbt.data.twoSided = 1;
			hgSbt.data.opacity = 1.0f;
			hgSbt.data.normalScale = 1.0f;
			hgSbt.data.clearcoat = 0.0f;
			hgSbt.data.clearcoatRoughness = 0.0001f;
			hgSbt.data.clearcoatNormalScale = 1.0f;
			hgSbt.data.sheenColorFactor = make_float3(0.0f, 0.0f, 0.0f);
			hgSbt.data.sheenRoughness = 0.0001f;
			hgSbt.data.anisotropyStrength = 0.0f;
			hgSbt.data.anisotropyRotation = 0.0f;
			hgSbt.data.iridescenceFactor = 0.0f;
			hgSbt.data.iridescenceIor = 1.3f;
			hgSbt.data.iridescenceThickness = 400.0f;
			hgSbt.data.useSpecGloss = 0;
			hgSbt.data.diffuseColor = make_float3(1.0f, 1.0f, 1.0f);
			hgSbt.data.specGlossSpecularColor = make_float3(0.0f, 0.0f, 0.0f);
			hgSbt.data.glossinessFactor = 1.0f;
			hgSbt.data.diffuseTransmissionFactor = 0.0f;
			hgSbt.data.diffuseTransmissionColor = make_float3(1.0f, 1.0f, 1.0f);
			hgSbt.data.transmission = 0.0f;
			hgSbt.data.hasVolume = 0;
			hgSbt.data.attenuationColor = make_float3(1.0f, 1.0f, 1.0f);
			hgSbt.data.attenuationDistance = std::numeric_limits<float>::infinity();
			hgSbt.data.thicknessFactor = 0.0f;
			hgSbt.data.dispersion = 0.0f;
			hgSbt.data.multiScatterColor = make_float3(1.0f, 1.0f, 1.0f);
			hgSbt.data.hasVolumeScattering = 0;

			hgSbt.data.baseColorTexture.width = 0;
			hgSbt.data.metallicTexture.width = 0;
			hgSbt.data.roughnessTexture.width = 0;
			hgSbt.data.normalTexture.width = 0;
			hgSbt.data.emissiveTexture.width = 0;
			hgSbt.data.aoTexture.width = 0;
			hgSbt.data.opacityTexture.width = 0;
			hgSbt.data.specularTexture.width = 0;
			hgSbt.data.specularColorTexture.width = 0;
			hgSbt.data.clearcoatTexture.width = 0;
			hgSbt.data.clearcoatRoughnessTexture.width = 0;
			hgSbt.data.clearcoatNormalTexture.width = 0;
			hgSbt.data.sheenColorTexture.width = 0;
			hgSbt.data.sheenRoughnessTexture.width = 0;
			hgSbt.data.anisotropyTexture.width = 0;
			hgSbt.data.iridescenceTexture.width = 0;
			hgSbt.data.iridescenceThicknessTexture.width = 0;
			hgSbt.data.diffuseTexture.width = 0;
			hgSbt.data.specularGlossinessTexture.width = 0;
			hgSbt.data.diffuseTransmissionTexture.width = 0;
			hgSbt.data.diffuseTransmissionColorTexture.width = 0;
			hgSbt.data.transmissionTexture.width = 0;
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

	// Only the heavy face/mip texel data is uploaded here - the environment
	// SCALARS deliberately flow per-launch through renderScene() instead,
	// see Impl::envFaceBuffers' doc comment for why.
	const RtEnvironment& env = snapshot.environment;
	_impl->envFaceSize = env.faceSize;

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

	_impl->irradianceFaceSize = env.irradianceFaceSize;
	if (env.irradianceFaceSize > 0)
	{
		for (int face = 0; face < 6; ++face)
		{
			if (!uploadCubemapFace(env.irradianceFaces[face], env.irradianceFaceSize, _impl->irradianceFaceBuffers[face]))
			{
				qWarning() << "RtOptixSceneTracer::buildScene(): irradiance face" << face << "has unexpected size - disabling GPU diffuse IBL (falls back to the raw map).";
				_impl->irradianceFaceSize = 0;
				break;
			}
		}
	}

	auto uploadPrefilterChain = [&](const std::vector<RtEnvironment::PrefilterMip>& sourceMips,
		std::vector<Impl::PrefilterMipGpu>& gpuMips,
		CUdeviceptr& mipsBuffer,
		int& mipCount,
		const char* label) -> void
	{
		mipCount = 0;
		if (sourceMips.empty())
			return;

		gpuMips.resize(sourceMips.size());
		std::vector<RtOptixPrefilterMip> hostMips(sourceMips.size());
		bool mipsOk = true;
		for (size_t m = 0; mipsOk && m < sourceMips.size(); ++m)
		{
			const RtEnvironment::PrefilterMip& mip = sourceMips[m];
			hostMips[m].faceSize = mip.faceSize;
			for (int face = 0; face < 6; ++face)
			{
				if (!uploadCubemapFace(mip.faces[face], mip.faceSize, gpuMips[m].faceBuffers[face]))
				{
					qWarning() << "RtOptixSceneTracer::buildScene():" << label << "mip" << static_cast<int>(m) << "face" << face
						<< "has unexpected size - disabling the prefilter chain (falling back to the raw map).";
					mipsOk = false;
					break;
				}
				hostMips[m].faces[face] = reinterpret_cast<float3*>(gpuMips[m].faceBuffers[face]);
			}
		}

		if (mipsOk)
		{
			const size_t mipsBytes = hostMips.size() * sizeof(RtOptixPrefilterMip);
			if (cudaCheck(cudaMalloc(reinterpret_cast<void**>(&mipsBuffer), mipsBytes), "cudaMalloc(prefilter mips)") &&
				cudaCheck(cudaMemcpy(reinterpret_cast<void*>(mipsBuffer), hostMips.data(), mipsBytes, cudaMemcpyHostToDevice), "cudaMemcpy(prefilter mips)"))
			{
				mipCount = static_cast<int>(hostMips.size());
			}
		}
	};

	uploadPrefilterChain(env.prefilterMips, _impl->prefilterMipEntries, _impl->prefilterMipsBuffer, _impl->prefilterMipCount, "prefilter");
	uploadPrefilterChain(env.sheenPrefilterMips, _impl->sheenPrefilterMipEntries, _impl->sheenPrefilterMipsBuffer, _impl->sheenPrefilterMipCount, "sheen prefilter");

	// Environment-light NEE + MIS - builds a host-side RtEnvironmentSampler
	// from the SAME environment cubemap just uploaded above (so both engines
	// importance-sample the identical distribution), then uploads its raw
	// flat CDF/texel-pdf arrays verbatim - see RtOptixSceneParams.h's
	// RtOptixEnvironment::envFlatCdf doc comment. A local, buildScene()-
	// scoped instance is sufficient (unlike CPU's RtPathTracingSession-owned
	// one, which stays alive to serve every render call) since this backend
	// only needs the arrays uploaded once, not kept around host-side.
	_impl->envTotalWeight = 0.0f;
	{
		RtEnvironmentSampler envSampler;
		envSampler.build(env);
		if (envSampler.isValid())
		{
			const std::vector<float>& flatCdf = envSampler.flatCdf();
			const std::vector<float>& texelPdf = envSampler.texelPdf();
			const size_t flatCdfBytes = flatCdf.size() * sizeof(float);
			const size_t texelPdfBytes = texelPdf.size() * sizeof(float);
			if (cudaCheck(cudaMalloc(reinterpret_cast<void**>(&_impl->envFlatCdfBuffer), flatCdfBytes), "cudaMalloc(env flat CDF)") &&
				cudaCheck(cudaMemcpy(reinterpret_cast<void*>(_impl->envFlatCdfBuffer), flatCdf.data(), flatCdfBytes, cudaMemcpyHostToDevice), "cudaMemcpy(env flat CDF)") &&
				cudaCheck(cudaMalloc(reinterpret_cast<void**>(&_impl->envTexelPdfBuffer), texelPdfBytes), "cudaMalloc(env texel pdf)") &&
				cudaCheck(cudaMemcpy(reinterpret_cast<void*>(_impl->envTexelPdfBuffer), texelPdf.data(), texelPdfBytes, cudaMemcpyHostToDevice), "cudaMemcpy(env texel pdf)"))
			{
				_impl->envTotalWeight = envSampler.totalWeight();
			}
			else
			{
				if (_impl->envFlatCdfBuffer) cudaFree(reinterpret_cast<void*>(_impl->envFlatCdfBuffer));
				_impl->envFlatCdfBuffer = 0;
				if (_impl->envTexelPdfBuffer) cudaFree(reinterpret_cast<void*>(_impl->envTexelPdfBuffer));
				_impl->envTexelPdfBuffer = 0;
			}
		}
	}

	qInfo() << "RtOptixSceneTracer: scene built (" << _impl->meshGasEntries.size() << "meshes,"
		<< instances.size() << "instances," << _impl->lightCount << "lights).";
	return true;
}

bool RtOptixSceneTracer::renderScene(const RtCamera& camera, const RtEnvironment& environment,
	int width, int height, unsigned int samplesPerPixel, unsigned int sampleOffset,
	unsigned int maxBounces, bool shadowsEnabled, bool selfShadowsEnabled, bool enableEnvironmentImportanceSampling,
	unsigned int maxTransmissionBounces, float fireflyClampThreshold, unsigned int russianRouletteStartDepth,
	std::vector<glm::vec3>& outImageLinearRgb, std::vector<glm::vec3>& outAlbedo, std::vector<glm::vec3>& outNormal,
	std::vector<float>& outAlpha)
{
	static_assert(sizeof(glm::vec3) == sizeof(float) * 3, "RtOptixSceneTracer assumes glm::vec3 is three tightly packed floats.");

	if (!_impl->valid || _impl->iasHandle == 0 || width <= 0 || height <= 0)
		return false;

	const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
	CUdeviceptr dImage = 0, dAlbedo = 0, dNormal = 0, dAlpha = 0;
	bool allocOk = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&dImage), pixelCount * sizeof(float3)), "cudaMalloc(output image)");
	if (allocOk)
		allocOk = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&dAlbedo), pixelCount * sizeof(float3)), "cudaMalloc(albedo guide image)");
	if (allocOk)
		allocOk = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&dNormal), pixelCount * sizeof(float3)), "cudaMalloc(normal guide image)");
	if (allocOk)
		allocOk = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&dAlpha), pixelCount * sizeof(float)), "cudaMalloc(alpha image)");
	if (!allocOk)
	{
		if (dAlpha) cudaFree(reinterpret_cast<void*>(dAlpha));
		if (dNormal) cudaFree(reinterpret_cast<void*>(dNormal));
		if (dAlbedo) cudaFree(reinterpret_cast<void*>(dAlbedo));
		if (dImage) cudaFree(reinterpret_cast<void*>(dImage));
		return false;
	}

	RtOptixSceneParams params{};
	params.image = reinterpret_cast<float3*>(dImage);
	params.albedoImage = reinterpret_cast<float3*>(dAlbedo);
	params.normalImage = reinterpret_cast<float3*>(dNormal);
	params.alphaImage = reinterpret_cast<float*>(dAlpha);
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
	params.shadowsEnabled = shadowsEnabled ? 1 : 0;
	params.selfShadowsEnabled = selfShadowsEnabled ? 1 : 0;
	params.enableEnvironmentImportanceSampling = enableEnvironmentImportanceSampling ? 1 : 0;

	// Heavy texel data (face/mip device pointers) comes from the revision-
	// gated buildScene() upload; the cheap scalars come fresh from THIS
	// call's snapshot environment, so lightweight setting changes (exposure,
	// fallback gradient colors, skybox visibility/rotation...) take effect
	// on the next restart without a scene-revision bump - see
	// Impl::envFaceBuffers' doc comment.
	for (int face = 0; face < 6; ++face)
		params.environment.faces[face] = reinterpret_cast<float3*>(_impl->envFaceBuffers[face]);
	params.environment.faceSize = _impl->envFaceSize;
	for (int face = 0; face < 6; ++face)
		params.environment.irradianceFaces[face] = reinterpret_cast<float3*>(_impl->irradianceFaceBuffers[face]);
	params.environment.irradianceFaceSize = _impl->irradianceFaceSize;
	params.environment.showBackground = environment.showBackground ? 1 : 0;
	params.environment.fallbackTopColor = make_float3(environment.fallbackTopColor.x, environment.fallbackTopColor.y, environment.fallbackTopColor.z);
	params.environment.fallbackBottomColor = make_float3(environment.fallbackBottomColor.x, environment.fallbackBottomColor.y, environment.fallbackBottomColor.z);
	params.environment.fallbackGradientStyle = environment.fallbackGradientStyle;
	params.environment.cameraUpAxisZUp = environment.cameraUpAxisZUp ? 1 : 0;
	params.environment.skyBoxZRotationDegrees = environment.skyBoxZRotationDegrees;
	params.environment.envMapExposure = environment.envMapExposure;
	params.environment.prefilterMips = reinterpret_cast<const RtOptixPrefilterMip*>(_impl->prefilterMipsBuffer);
	params.environment.prefilterMipCount = _impl->prefilterMipCount;
	params.environment.sheenPrefilterMips = reinterpret_cast<const RtOptixPrefilterMip*>(_impl->sheenPrefilterMipsBuffer);
	params.environment.sheenPrefilterMipCount = _impl->sheenPrefilterMipCount;
	params.environment.envFlatCdf = reinterpret_cast<const float*>(_impl->envFlatCdfBuffer);
	params.environment.envTexelPdf = reinterpret_cast<const float*>(_impl->envTexelPdfBuffer);
	params.environment.envTotalWeight = _impl->envTotalWeight;
	params.sheenAlbedoLUT = reinterpret_cast<const float*>(_impl->sheenAlbedoLutBuffer);
	params.sheenCharlieLUT = reinterpret_cast<const float*>(_impl->sheenCharlieLutBuffer);
	params.sheenAlbedoLUTSize = _impl->sheenAlbedoLutBuffer ? Impl::kSheenAlbedoLutSize : 0;
	params.samplesPerPixel = samplesPerPixel > 0 ? samplesPerPixel : 1;
	params.sampleOffset = sampleOffset;
	params.maxBounces = maxBounces > 0 ? maxBounces : 1;
	params.maxTransmissionBounces = maxTransmissionBounces > 0 ? maxTransmissionBounces : 1;
	params.fireflyClampThreshold = fireflyClampThreshold > 0.0f ? fireflyClampThreshold : 0.01f;
	params.russianRouletteStartDepth = russianRouletteStartDepth > 0 ? russianRouletteStartDepth : 1;

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
		outAlpha.resize(pixelCount);
		ok = cudaCheck(cudaMemcpy(outImageLinearRgb.data(), reinterpret_cast<void*>(dImage), pixelCount * sizeof(float3), cudaMemcpyDeviceToHost), "cudaMemcpy(readback image)");
		if (ok)
			ok = cudaCheck(cudaMemcpy(outAlbedo.data(), reinterpret_cast<void*>(dAlbedo), pixelCount * sizeof(float3), cudaMemcpyDeviceToHost), "cudaMemcpy(readback albedo)");
		if (ok)
			ok = cudaCheck(cudaMemcpy(outNormal.data(), reinterpret_cast<void*>(dNormal), pixelCount * sizeof(float3), cudaMemcpyDeviceToHost), "cudaMemcpy(readback normal)");
		if (ok)
			ok = cudaCheck(cudaMemcpy(outAlpha.data(), reinterpret_cast<void*>(dAlpha), pixelCount * sizeof(float), cudaMemcpyDeviceToHost), "cudaMemcpy(readback alpha)");
	}

	if (dParams) cudaFree(reinterpret_cast<void*>(dParams));
	cudaFree(reinterpret_cast<void*>(dImage));
	cudaFree(reinterpret_cast<void*>(dAlbedo));
	cudaFree(reinterpret_cast<void*>(dNormal));
	cudaFree(reinterpret_cast<void*>(dAlpha));

	if (!ok)
	{
		outImageLinearRgb.clear();
		outAlbedo.clear();
		outNormal.clear();
		outAlpha.clear();
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

bool RtOptixSceneTracer::renderScene(const RtCamera&, const RtEnvironment&, int, int, unsigned int, unsigned int, unsigned int, bool, bool, bool, unsigned int, float, unsigned int, std::vector<glm::vec3>&, std::vector<glm::vec3>&, std::vector<glm::vec3>&, std::vector<float>&)
{
	return false;
}

#endif // MODELVIEWER_HAVE_OPTIX
