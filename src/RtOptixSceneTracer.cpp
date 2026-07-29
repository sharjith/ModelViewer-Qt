#include "RtOptixSceneTracer.h"

#include <QDebug>
#include <QElapsedTimer>

#ifdef MODELVIEWER_HAVE_OPTIX

#include <cuda_runtime.h>
#include <cuda.h> // Driver API (cuModuleLoadDataEx()/cuLaunchKernel()) - only used to load/launch RtOptixSkinning.cu's plain compute kernel, see ensureSkinningModuleLoaded()

#include <optix.h>
#include <optix_stack_size.h>
#include <optix_stubs.h>
// NOTE: optix_function_table_definition.h must be included in exactly one
// translation unit across the whole program - that's RtOptixContext.cpp.

#include "RtEnvironmentSampler.h"
#include "RtOptixEmbeddedPtx.h"
#include "RtOptixSceneParams.h"
#include "RtOptixSkinningParams.h"
#include "RtOptixMorphParams.h"
#include "PathUtils.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
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

	// CUDA Driver API's own error-check convention - only used for loading/
	// launching RtOptixSkinning.cu's plain compute kernel (cuModuleLoadDataEx()/
	// cuModuleGetFunction()/cuModuleGetGlobal()/cuLaunchKernel()); everything
	// else in this file is either the Runtime API (cudaCheck() above) or the
	// OptiX API (optixCheck() above).
	// FNV-1a over raw bytes - mirrors RtSceneBuilder.cpp's identical helper
	// (used there for RtMeshGeometry::contentHash/baseContentHash) - not
	// cross-run/cross-process stable, just fast and deterministic within
	// this one running process, which is all a same-session "did this
	// change since the last buildScene() call" comparison needs. Used below
	// to detect an unchanged environment (cubemap/irradiance/prefilter
	// chains) so its expensive re-upload + RtEnvironmentSampler CDF/PDF
	// rebuild can be skipped on a build triggered by something else
	// entirely (e.g. an animated mesh's pose changing) - see
	// Impl::lastEnvironmentHash's doc comment.
	// Processes 8 bytes/iteration (an unaligned uint64_t read - safe and fast
	// on x64, this project's only target) instead of 1, since this is now
	// used to hash tens of megabytes of environment cubemap/prefilter pixel
	// data every buildScene() call (see envHash's call site) - a plain
	// byte-at-a-time loop measured at ~53ms/call on real environment data,
	// which had become the new dominant cost right after fixing the
	// redundant-reupload problem this hash exists to detect in the first
	// place. Still a full-fidelity fingerprint (every byte folded in, just
	// 8 at a time) - not a sampled/partial hash, so no new risk of missing a
	// genuine content change.
	uint64_t fnv1aHash(const void* data, size_t size, uint64_t seed = 1469598103934665603ULL)
	{
		const unsigned char* bytes = static_cast<const unsigned char*>(data);
		uint64_t hash = seed;
		const size_t wordCount = size / sizeof(uint64_t);
		const uint64_t* words = reinterpret_cast<const uint64_t*>(bytes);
		for (size_t i = 0; i < wordCount; ++i)
		{
			hash ^= words[i];
			hash *= 1099511628211ULL;
		}
		for (size_t i = wordCount * sizeof(uint64_t); i < size; ++i)
		{
			hash ^= bytes[i];
			hash *= 1099511628211ULL;
		}
		return hash;
	}

	bool cuCheck(CUresult result, const char* what)
	{
		if (result == CUDA_SUCCESS)
			return true;
		const char* errName = nullptr;
		cuGetErrorName(result, &errName);
		qWarning() << "RtOptixSceneTracer:" << what << "failed (CUresult" << static_cast<int>(result) << (errName ? errName : "") << ")";
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

	// Diagnostics snapshot (Diagnostics tab) - populated once in the
	// constructor (rtCoreVersion/deviceName, fixed for this context's
	// lifetime) or once per buildScene() call (the rest, since the scene
	// doesn't change between renderScene() calls) - never touched from
	// renderScene() itself, so reading these has no per-frame cost.
	unsigned int rtCoreVersion = 0;
	std::string deviceName;
	double lastGasBuildMs = 0.0;  // total across all meshes' GAS builds, this buildScene() call
	double lastIasBuildMs = 0.0;
	uint64_t lastTriangleCount = 0;

	// One entry per snapshot mesh (GAS), THIS build's positional list only -
	// kept alive for the lifetime of the built scene since the closest-hit
	// shader dereferences positions/indices directly, and the IAS's
	// OptixInstance array references each handle. Does NOT own the device
	// buffers below (persistentGasCache does - see its own doc comment) -
	// just a per-build, positionally-indexed copy of whichever cache entry
	// (hit or miss) resolved to each snapshot mesh, exactly mirroring how
	// uploadMaterialTexture()'s RtOptixTexture outputs point at
	// persistentTextureCache-owned buffers without owning them either.
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

	// Persistent cache of built mesh GAS (bottom-level acceleration
	// structures), keyed on RtMeshGeometry::sourceObjectId alone (a stable
	// per-mesh identity - see that field's own doc comment, RtSceneSnapshot.h)
	// - mirrors persistentTextureCache/evictUnusedTextures() exactly (see
	// that struct's doc comment), just for mesh geometry instead of textures.
	// SURVIVES across buildScene() calls (see freeSceneBuffers()'s
	// includePersistentCaches parameter) so a mesh whose geometry hasn't
	// actually changed since the last build - the common case for a
	// material/light/single-object edit in an otherwise-static large
	// assembly - needs no new cudaMalloc/cudaMemcpy/optixAccelBuild() at all.
	//
	// contentHash/vertexCount/indexCount are carried IN the entry (rather
	// than baked into the map key, as an earlier version of this cache did)
	// specifically so a MISS (contentHash changed - e.g. a skinned/morphed
	// mesh's pose advanced) can still be resolved against its own PREVIOUS
	// entry by sourceObjectId: if vertexCount/indexCount are unchanged
	// (topology-preserving deformation - skinning/morph moves positions/
	// normals/tangents but never changes triangle connectivity), buildScene()
	// REFITS (OPTIX_BUILD_OPERATION_UPDATE) the existing device buffers/GAS
	// in place instead of freeing and rebuilding from scratch - much cheaper,
	// and the dominant cost during actual skinned-animation playback (a
	// content-hash miss is otherwise expected and correct there, since the
	// pose genuinely differs every frame - see this cache's own hit/miss
	// logic in buildScene() for the full three-way hit/refit/rebuild split).
	struct MeshGasEntry
	{
		uint64_t contentHash = 0;
		uint32_t vertexCount = 0;
		uint32_t indexCount = 0;
		CUdeviceptr positions = 0;
		CUdeviceptr indices = 0;
		CUdeviceptr normals = 0;
		CUdeviceptr texCoords = 0;
		CUdeviceptr tangents = 0;
		CUdeviceptr vertexColors = 0;
		CUdeviceptr gasOutputBuffer = 0;
		OptixTraversableHandle handle = 0;
		// Mark-and-sweep flag for evictUnusedGas() - same convention as
		// TextureUploadEntry::touchedThisBuild.
		bool touchedThisBuild = false;
	};
	std::unordered_map<uint32_t, MeshGasEntry> persistentGasCache;

	static void freeGasEntry(MeshGasEntry& entry)
	{
		if (entry.positions) cudaFree(reinterpret_cast<void*>(entry.positions));
		if (entry.normals) cudaFree(reinterpret_cast<void*>(entry.normals));
		if (entry.texCoords) cudaFree(reinterpret_cast<void*>(entry.texCoords));
		if (entry.tangents) cudaFree(reinterpret_cast<void*>(entry.tangents));
		if (entry.vertexColors) cudaFree(reinterpret_cast<void*>(entry.vertexColors));
		if (entry.indices) cudaFree(reinterpret_cast<void*>(entry.indices));
		if (entry.gasOutputBuffer) cudaFree(reinterpret_cast<void*>(entry.gasOutputBuffer));
	}

	// Mark-and-sweep eviction - called once, at the end of a SUCCESSFUL
	// buildScene(), after every mesh the new snapshot actually references has
	// already been resolved (and its cache entry marked touchedThisBuild by
	// the GAS loop below, whether a fresh build or a reuse). Anything left
	// unmarked wasn't referenced by this snapshot at all (removed mesh, or a
	// skinned/morphed mesh whose content hash moved on) and gets freed
	// instead of kept forever - mirrors evictUnusedTextures() exactly.
	void evictUnusedGas()
	{
		int evicted = 0;
		for (auto it = persistentGasCache.begin(); it != persistentGasCache.end(); )
		{
			if (it->second.touchedThisBuild)
			{
				it->second.touchedThisBuild = false; // reset for the next build's marking pass
				++it;
			}
			else
			{
				freeGasEntry(it->second);
				it = persistentGasCache.erase(it);
				++evicted;
			}
		}
		if (evicted > 0)
			qInfo() << "RtOptixSceneTracer::evictUnusedGas: freed" << evicted << "mesh GAS entry/entries no longer referenced.";
	}

	// instancesBuffer/iasOutputBuffer are NOT unconditionally freed at the
	// top of every buildScene() call the way most other buffers below are -
	// see buildScene()'s IAS section for why: when the IAS's TOPOLOGY hasn't
	// changed since the last build (same instance count, same per-instance
	// GAS handle - i.e. no mesh added/removed/swapped, only transforms
	// and/or materials differ), both buffers are reused in place via
	// OPTIX_BUILD_OPERATION_UPDATE (a refit) instead of a full rebuild - see
	// lastIasInstanceHandles below. Only freed when a full rebuild is
	// actually chosen (topology changed, so the buffers may need a different
	// size) or on final teardown (~Impl()).
	CUdeviceptr instancesBuffer = 0;
	CUdeviceptr iasOutputBuffer = 0;
	OptixTraversableHandle iasHandle = 0;

	// Ordered per-instance GAS handle sequence from the LAST successful IAS
	// build (index i == the OptixInstance built for snapshot.instances[i]) -
	// the topology fingerprint buildScene() diffs the NEW instance list
	// against to decide refit-vs-rebuild. Empty means "no valid previous
	// build to refit against" (first build ever, or the last one used a full
	// rebuild for some other reason) - always forces a full rebuild.
	std::vector<OptixTraversableHandle> lastIasInstanceHandles;

	CUdeviceptr lightsBuffer = 0;
	unsigned int lightCount = 0;
	bool infinitePlaneEnabled = false;
	bool infinitePlaneCameraUpAxisZUp = false;
	float infinitePlaneHeight = 0.0f;
	bool infinitePlaneIsShadowCatcher = false;
	float infinitePlaneShadowCatcherDarkness = 0.5f;
	glm::vec3 infinitePlaneBaseColor = glm::vec3(0.5f);
	float infinitePlaneMetalness = 0.0f;
	float infinitePlaneRoughness = 0.5f;

	// Persistent, content-keyed cache of uploaded material textures (base
	// rgba8 + mip pyramid) - unlike the GAS/IAS/lights/environment buffers
	// below, this SURVIVES across buildScene() calls (see freeSceneBuffers()'s
	// includeTextureCache parameter). Keyed on RtTextureSample::imageCacheKey
	// plus every baked-in uv/wrap/packing parameter (mirrors RtSceneBuilder's
	// own private TextureDedupKey) - see RtTextureSample::imageCacheKey's doc
	// comment for why raw RtTextureSample pointer/object identity can't be
	// used here instead: RtSceneBuilder::build() allocates a brand-new
	// RtTextureSample for every texture on every single snapshot build,
	// whether or not the underlying image actually changed, so pointer
	// identity only ever matched textures shared WITHIN one build - never
	// across two separate ones. A texture unchanged since the last
	// buildScene() call is now recognized by content key and reused with NO
	// new cudaMalloc/cudaMemcpy at all.
	struct TextureUploadEntry
	{
		CUdeviceptr baseRgba8 = 0;
		std::vector<CUdeviceptr> mipBuffers; // per-level buffers actually allocated (excludes any level reusing baseRgba8 - see buildScene()'s uploadMaterialTexture() lambda)
		CUdeviceptr mipArrayDevice = 0;
		int mipCount = 0;
		// Mark-and-sweep flag for evictUnusedTextures() - set whenever this
		// build's uploadMaterialTexture() resolves to this entry (whether a
		// fresh upload or a reuse), cleared again once a sweep confirms it's
		// still wanted; an entry left false at sweep time wasn't referenced
		// by this snapshot at all (removed/replaced material, or the source
		// image changed enough that its key moved on) and gets freed.
		bool touchedThisBuild = false;
	};
	struct TextureUploadKey
	{
		int64_t imageCacheKey = 0;
		float uvScaleX = 1.0f, uvScaleY = 1.0f;
		float uvOffsetX = 0.0f, uvOffsetY = 0.0f;
		float uvRotation = 0.0f;
		int texCoordIndex = 0;
		unsigned int wrapS = 0, wrapT = 0;
		int packingChannel = -1;
		bool packingInvert = false;
		float packingScale = 1.0f;
		float packingBias = 0.0f;

		bool operator==(const TextureUploadKey& o) const
		{
			return imageCacheKey == o.imageCacheKey &&
				uvScaleX == o.uvScaleX && uvScaleY == o.uvScaleY &&
				uvOffsetX == o.uvOffsetX && uvOffsetY == o.uvOffsetY &&
				uvRotation == o.uvRotation && texCoordIndex == o.texCoordIndex &&
				wrapS == o.wrapS && wrapT == o.wrapT &&
				packingChannel == o.packingChannel && packingInvert == o.packingInvert &&
				packingScale == o.packingScale && packingBias == o.packingBias;
		}
	};
	struct TextureUploadKeyHash
	{
		size_t operator()(const TextureUploadKey& k) const
		{
			size_t h = std::hash<int64_t>{}(k.imageCacheKey);
			auto mix = [&h](size_t v) { h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2); };
			mix(std::hash<float>{}(k.uvScaleX));
			mix(std::hash<float>{}(k.uvScaleY));
			mix(std::hash<float>{}(k.uvOffsetX));
			mix(std::hash<float>{}(k.uvOffsetY));
			mix(std::hash<float>{}(k.uvRotation));
			mix(std::hash<int>{}(k.texCoordIndex));
			mix(std::hash<unsigned int>{}(k.wrapS));
			mix(std::hash<unsigned int>{}(k.wrapT));
			mix(std::hash<int>{}(k.packingChannel));
			mix(std::hash<bool>{}(k.packingInvert));
			mix(std::hash<float>{}(k.packingScale));
			mix(std::hash<float>{}(k.packingBias));
			return h;
		}
	};
	std::unordered_map<TextureUploadKey, TextureUploadEntry, TextureUploadKeyHash> persistentTextureCache;

	static void freeTextureEntry(TextureUploadEntry& entry)
	{
		if (entry.baseRgba8) cudaFree(reinterpret_cast<void*>(entry.baseRgba8));
		for (CUdeviceptr mipBuf : entry.mipBuffers)
			if (mipBuf) cudaFree(reinterpret_cast<void*>(mipBuf));
		if (entry.mipArrayDevice) cudaFree(reinterpret_cast<void*>(entry.mipArrayDevice));
	}

	// Mark-and-sweep eviction - called once, at the end of a SUCCESSFUL
	// buildScene(), after every texture the new snapshot actually references
	// has already been resolved (and its cache entry marked touchedThisBuild
	// by uploadMaterialTexture()). Anything left unmarked is freed instead of
	// kept forever, so an edited/reloaded/removed texture's old VRAM buffer
	// doesn't leak for the rest of the session.
	void evictUnusedTextures()
	{
		int evicted = 0;
		for (auto it = persistentTextureCache.begin(); it != persistentTextureCache.end(); )
		{
			if (it->second.touchedThisBuild)
			{
				it->second.touchedThisBuild = false; // reset for the next build's marking pass
				++it;
			}
			else
			{
				freeTextureEntry(it->second);
				it = persistentTextureCache.erase(it);
				++evicted;
			}
		}
		if (evicted > 0)
			qInfo() << "RtOptixSceneTracer::evictUnusedTextures: freed" << evicted << "texture(s) no longer referenced.";
	}

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

	// Content identity for everything uploaded above - SURVIVES across
	// buildScene() calls (see freeSceneBuffers()'s includePersistentCaches
	// parameter) exactly like persistentGasCache/persistentTextureCache, so
	// an unchanged environment (the common case during animation playback -
	// nothing about the skybox/HDRI changes just because a mesh's pose did)
	// costs nothing on a build where the scene revision bumped for an
	// unrelated reason. Confirmed via real session timing instrumentation
	// that this environment re-upload (cubemap faces + irradiance + full
	// GGX-prefiltered mip chain, TWICE for the sheen variant, plus rebuilding
	// a whole RtEnvironmentSampler CDF/PDF distribution from scratch) was
	// the dominant cost in buildScene() - ~115-155ms out of a 120-180ms
	// total, versus the GAS loop's 7-26ms - every single animation frame.
	// environmentUploaded distinguishes "never uploaded yet" from "hash
	// coincidentally 0" (an empty/no-environment scene hashes to a fixed
	// seed value, which is a legitimate hash, not a sentinel).
	uint64_t lastEnvironmentHash = 0;
	bool environmentUploaded = false;

	// Frees everything the environment-upload block below owns - called
	// either when replacing a changed environment (buildScene(), before the
	// fresh upload) or unconditionally on final teardown
	// (freeSceneBuffers(includePersistentCaches=true)). NOT called at the
	// top of every buildScene() the way it used to be - that's the whole
	// point of this being persistent now.
	void freeEnvironmentBuffers()
	{
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

		lastEnvironmentHash = 0;
		environmentUploaded = false;
	}

	OptixModule module = nullptr;
	OptixPipeline pipeline = nullptr;
	OptixProgramGroup raygenGroup = nullptr;
	OptixProgramGroup missGroup = nullptr;
	OptixProgramGroup hitgroupGroup = nullptr;

	CUdeviceptr raygenRecord = 0;
	CUdeviceptr missRecord = 0;
	CUdeviceptr hitgroupRecords = 0; // one HitGroupSbtRecord per instance
	OptixShaderBindingTable sbt = {};

	// GPU-skinning compute kernel module/function (see RtOptixSkinning.cu's
	// doc comment) - a plain CUDA Driver-API module, entirely separate from
	// the OptiX module/pipeline above. Lazily loaded on first use
	// (ensureSkinningModuleLoaded()) rather than in the constructor
	// alongside the OptiX pipeline, since most scenes never have a skinned
	// mesh at all - no reason to pay the (cheap but nonzero) module-load
	// cost unconditionally for every document.
	CUmodule skinningModule = nullptr;
	CUfunction skinningFunction = nullptr;
	bool skinningModuleLoadAttempted = false; // true after the first attempt (success OR failure) - never retried, matching this class's other "probe once" conventions
	bool ensureSkinningModuleLoaded();

	// Persistent cache of GPU-skinning BIND-POSE base data - mirrors
	// persistentGasCache exactly in shape/lifecycle (content-keyed, mark-
	// and-sweep via evictUnusedSkinBase(), SURVIVES across buildScene()
	// calls - see freeSceneBuffers()'s includePersistentCaches parameter),
	// but keyed on RtMeshGeometry::baseContentHash (the BIND-POSE identity)
	// instead of the POSED contentHash the GAS cache uses: bind-pose data
	// changes rarely (mesh reload/edit) while the posed contentHash changes
	// every single animation frame, so keying this cache on the posed hash
	// would defeat the whole point (every frame would look like a genuinely
	// new mesh). Holds the device-resident bind-pose position/normal/
	// tangent + joint index/weight buffers, uploaded ONCE per bind-pose
	// identity and reused every frame that identity is still current -
	// RtOptixSkinning.cu's kernel reads these plus a small per-build joint-
	// palette upload and writes the blended result directly into the SAME
	// device position/normal/tangent buffers the mesh's GAS (refit) already
	// owns - see buildScene()'s GAS loop for the exact integration point
	// (inside the existing refitEligible branch, replacing the CPU-bake-
	// and-cudaMemcpy path specifically for meshes with hasSkinningData).
	struct SkinBaseEntry
	{
		uint64_t contentHash = 0; // this entry's own baseContentHash - bookkeeping symmetry with persistentGasCache, not load-bearing (the map key already encodes it)
		CUdeviceptr basePositions = 0;
		CUdeviceptr baseNormals = 0;
		CUdeviceptr baseTangents = 0;
		CUdeviceptr jointIndices = 0;
		CUdeviceptr jointWeights = 0;
		bool touchedThisBuild = false;
	};
	std::unordered_map<uint32_t, SkinBaseEntry> persistentSkinBaseCache;

	static void freeSkinBaseEntry(SkinBaseEntry& entry)
	{
		if (entry.basePositions) cudaFree(reinterpret_cast<void*>(entry.basePositions));
		if (entry.baseNormals) cudaFree(reinterpret_cast<void*>(entry.baseNormals));
		if (entry.baseTangents) cudaFree(reinterpret_cast<void*>(entry.baseTangents));
		if (entry.jointIndices) cudaFree(reinterpret_cast<void*>(entry.jointIndices));
		if (entry.jointWeights) cudaFree(reinterpret_cast<void*>(entry.jointWeights));
	}

	// Mark-and-sweep eviction, same convention as evictUnusedGas() - called
	// once, at the end of a successful buildScene(), after every skinned
	// mesh the new snapshot actually references has already been resolved.
	void evictUnusedSkinBase()
	{
		int evicted = 0;
		for (auto it = persistentSkinBaseCache.begin(); it != persistentSkinBaseCache.end(); )
		{
			if (it->second.touchedThisBuild)
			{
				it->second.touchedThisBuild = false;
				++it;
			}
			else
			{
				freeSkinBaseEntry(it->second);
				it = persistentSkinBaseCache.erase(it);
				++evicted;
			}
		}
		if (evicted > 0)
			qInfo() << "RtOptixSceneTracer::evictUnusedSkinBase: freed" << evicted << "skin-base entr(y/ies) no longer referenced.";
	}

	// Ensures persistentSkinBaseCache has an up-to-date entry for this mesh
	// (uploading bind-pose position/normal/tangent + joint index/weight
	// buffers fresh only if missing or the bind pose itself changed - see
	// RtMeshGeometry::baseContentHash's doc comment), uploads this build's
	// (small) joint palette, then launches RtOptixSkinning.cu's kernel
	// writing the blended result directly into outPositions/outNormals/
	// outTangents (the mesh's own GAS position/normal/tangent buffers -
	// caller passes the SAME pointers gas.positions/gas.normals/gas.tangents
	// already resolved to, whether those came from a GAS cache hit or a
	// fresh allocation). Returns false on any failure (caller falls back to
	// treating this exactly like a failed CPU-bake refit - see call site).
	bool updateSkinBase(const RtMeshGeometry& mesh, CUdeviceptr outPositions, CUdeviceptr outNormals, CUdeviceptr outTangents);

	// GPU morph-blending compute kernel module/function (see RtOptixMorph.cu's
	// doc comment) - same lazy-load convention as skinningModule above, kept
	// entirely separate since most scenes have no morph-target mesh at all.
	CUmodule morphModule = nullptr;
	CUfunction morphFunction = nullptr;
	bool morphModuleLoadAttempted = false;
	bool ensureMorphModuleLoaded();

	// Persistent cache of GPU-morph REST-POSE base data - mirrors
	// persistentSkinBaseCache exactly in shape/lifecycle, keyed on
	// RtMeshGeometry::morphBaseContentHash (the rest-pose + delta identity,
	// changes rarely) instead of the per-frame morphWeights. Holds the
	// device-resident rest-pose position/normal/tangent + flattened
	// per-target delta buffers (+ presence flags), uploaded ONCE per
	// rest-pose identity and reused every frame that identity is still
	// current - RtOptixMorph.cu's kernel reads these plus a small per-build
	// weights upload and writes the blended result directly into the SAME
	// device position/normal/tangent buffers the mesh's GAS (refit) already
	// owns.
	struct MorphBaseEntry
	{
		uint64_t contentHash = 0; // this entry's own morphBaseContentHash - bookkeeping symmetry with persistentSkinBaseCache, not load-bearing (the map key already encodes it)
		CUdeviceptr basePositions = 0;
		CUdeviceptr baseNormals = 0;
		CUdeviceptr baseTangents = 0;
		CUdeviceptr deltaPositions = 0;
		CUdeviceptr deltaNormals = 0;
		CUdeviceptr deltaTangents = 0;
		CUdeviceptr hasPositionDeltas = 0;
		CUdeviceptr hasNormalDeltas = 0;
		CUdeviceptr hasTangentDeltas = 0;
		bool touchedThisBuild = false;
	};
	std::unordered_map<uint32_t, MorphBaseEntry> persistentMorphBaseCache;

	// Small-but-hot per-frame animation inputs (joint palettes, morph
	// weights) and the temporary OptiX accel scratch used by the repeated
	// refit/build calls below were still being cudaMalloc/cudaFree'd on the
	// animation hot path every tick. Reuse them across buildScene() calls so
	// animated PT pays the upload/compute cost, not allocator churn.
	struct ReusableDeviceBuffer
	{
		CUdeviceptr ptr = 0;
		size_t      bytes = 0;
	};
	ReusableDeviceBuffer skinJointPaletteBuffer;
	ReusableDeviceBuffer morphWeightsBuffer;
	ReusableDeviceBuffer accelScratchBuffer;

	bool ensureReusableDeviceBuffer(ReusableDeviceBuffer& buffer, size_t requiredBytes, const char* label)
	{
		if (requiredBytes == 0)
			return true;
		if (buffer.ptr != 0 && buffer.bytes >= requiredBytes)
			return true;

		if (buffer.ptr != 0)
		{
			cudaFree(reinterpret_cast<void*>(buffer.ptr));
			buffer.ptr = 0;
			buffer.bytes = 0;
		}

		if (!cudaCheck(cudaMalloc(reinterpret_cast<void**>(&buffer.ptr), requiredBytes), label))
			return false;
		buffer.bytes = requiredBytes;
		return true;
	}

	static void freeReusableDeviceBuffer(ReusableDeviceBuffer& buffer)
	{
		if (buffer.ptr)
			cudaFree(reinterpret_cast<void*>(buffer.ptr));
		buffer.ptr = 0;
		buffer.bytes = 0;
	}

	static void freeMorphBaseEntry(MorphBaseEntry& entry)
	{
		if (entry.basePositions) cudaFree(reinterpret_cast<void*>(entry.basePositions));
		if (entry.baseNormals) cudaFree(reinterpret_cast<void*>(entry.baseNormals));
		if (entry.baseTangents) cudaFree(reinterpret_cast<void*>(entry.baseTangents));
		if (entry.deltaPositions) cudaFree(reinterpret_cast<void*>(entry.deltaPositions));
		if (entry.deltaNormals) cudaFree(reinterpret_cast<void*>(entry.deltaNormals));
		if (entry.deltaTangents) cudaFree(reinterpret_cast<void*>(entry.deltaTangents));
		if (entry.hasPositionDeltas) cudaFree(reinterpret_cast<void*>(entry.hasPositionDeltas));
		if (entry.hasNormalDeltas) cudaFree(reinterpret_cast<void*>(entry.hasNormalDeltas));
		if (entry.hasTangentDeltas) cudaFree(reinterpret_cast<void*>(entry.hasTangentDeltas));
	}

	// Mark-and-sweep eviction, same convention as evictUnusedSkinBase() -
	// called once, at the end of a successful buildScene().
	void evictUnusedMorphBase()
	{
		int evicted = 0;
		for (auto it = persistentMorphBaseCache.begin(); it != persistentMorphBaseCache.end(); )
		{
			if (it->second.touchedThisBuild)
			{
				it->second.touchedThisBuild = false;
				++it;
			}
			else
			{
				freeMorphBaseEntry(it->second);
				it = persistentMorphBaseCache.erase(it);
				++evicted;
			}
		}
		if (evicted > 0)
			qInfo() << "RtOptixSceneTracer::evictUnusedMorphBase: freed" << evicted << "morph-base entr(y/ies) no longer referenced.";
	}

	// Ensures persistentMorphBaseCache has an up-to-date entry for this mesh
	// (uploading rest-pose position/normal/tangent + flattened delta/presence
	// buffers fresh only if missing or the rest pose itself changed - see
	// RtMeshGeometry::morphBaseContentHash's doc comment), uploads this
	// build's (small) weights array, then launches RtOptixMorph.cu's kernel
	// writing the blended result directly into outPositions/outNormals/
	// outTangents (the mesh's own GAS position/normal/tangent buffers).
	// Returns false on any failure (caller falls back to the CPU-bake refit
	// path - see call site).
	bool updateMorphBase(const RtMeshGeometry& mesh, CUdeviceptr outPositions, CUdeviceptr outNormals, CUdeviceptr outTangents);

	// KHR_materials_sheen's directional-albedo LUT (RtOptixSceneParams::
	// sheenAlbedoLUT) - a process-constant table (depends on nothing but the
	// Charlie BRDF), so it's baked and uploaded ONCE (lazily, on the first
	// buildScene() call) rather than per-scene-revision like the rest of
	// freeSceneBuffers()'s buffers - see ensureSheenAlbedoLut().
	static constexpr int kSheenAlbedoLutSize = 128;
	CUdeviceptr sheenAlbedoLutBuffer = 0;
	CUdeviceptr sheenCharlieLutBuffer = 0;

	void ensureSheenAlbedoLut();

	// includePersistentCaches: false (default, used at the top of every
	// buildScene() call) leaves persistentTextureCache/persistentGasCache
	// untouched - that's the whole point of them being persistent (see their
	// own doc comments); a fresh build's uploadMaterialTexture()/GAS-loop
	// lookups reuse whatever's still resident, and evictUnusedTextures()/
	// evictUnusedGas() sweep anything genuinely stale AFTER the new
	// snapshot's meshes/textures have all been resolved, not here. true
	// (only ~Impl(), i.e. the whole tracer/document being torn down) also
	// frees every cached texture/GAS and clears both caches, since there's no
	// future buildScene() call left to reuse them.
	void freeSceneBuffers(bool includePersistentCaches = false)
	{
		// meshGasEntries itself is just THIS build's transient, positionally-
		// indexed list of pointers into persistentGasCache entries (see its
		// own doc comment) - it doesn't own the device buffers, so clearing
		// it never frees anything; only evictUnusedGas()/the
		// includePersistentCaches branch below actually do.
		meshGasEntries.clear();

		if (includePersistentCaches)
		{
			if (!persistentGasCache.empty())
				qInfo() << "RtOptixSceneTracer::freeSceneBuffers: releasing" << persistentGasCache.size() << "cached mesh GAS entr(y/ies) (final teardown).";
			for (auto& [key, entry] : persistentGasCache)
				freeGasEntry(entry);
			persistentGasCache.clear();

			if (!persistentTextureCache.empty())
				qInfo() << "RtOptixSceneTracer::freeSceneBuffers: releasing" << persistentTextureCache.size() << "cached texture(s) (final teardown).";
			for (auto& [key, entry] : persistentTextureCache)
				freeTextureEntry(entry);
			persistentTextureCache.clear();

			if (!persistentSkinBaseCache.empty())
				qInfo() << "RtOptixSceneTracer::freeSceneBuffers: releasing" << persistentSkinBaseCache.size() << "cached skin-base entr(y/ies) (final teardown).";
			for (auto& [key, entry] : persistentSkinBaseCache)
				freeSkinBaseEntry(entry);
			persistentSkinBaseCache.clear();

			if (!persistentMorphBaseCache.empty())
				qInfo() << "RtOptixSceneTracer::freeSceneBuffers: releasing" << persistentMorphBaseCache.size() << "cached morph-base entr(y/ies) (final teardown).";
			for (auto& [key, entry] : persistentMorphBaseCache)
				freeMorphBaseEntry(entry);
			persistentMorphBaseCache.clear();

			// Final teardown only - see instancesBuffer/iasOutputBuffer's own
			// doc comment for why these are NOT unconditionally freed here
			// the way they used to be (a mid-session buildScene() call frees
			// them explicitly itself, only when actually choosing a full
			// rebuild - see that section).
			if (instancesBuffer) cudaFree(reinterpret_cast<void*>(instancesBuffer));
			instancesBuffer = 0;
			if (iasOutputBuffer) cudaFree(reinterpret_cast<void*>(iasOutputBuffer));
			iasOutputBuffer = 0;
			iasHandle = 0;
			lastIasInstanceHandles.clear();

			freeReusableDeviceBuffer(skinJointPaletteBuffer);
			freeReusableDeviceBuffer(morphWeightsBuffer);
			freeReusableDeviceBuffer(accelScratchBuffer);

			// Same reasoning, for the environment upload - see
			// freeEnvironmentBuffers()'s own doc comment.
			freeEnvironmentBuffers();
		}

		if (lightsBuffer) cudaFree(reinterpret_cast<void*>(lightsBuffer));
		lightsBuffer = 0;
		lightCount = 0;

		if (hitgroupRecords) cudaFree(reinterpret_cast<void*>(hitgroupRecords));
		hitgroupRecords = 0;
		sbt.hitgroupRecordBase = 0;
		sbt.hitgroupRecordCount = 0;
	}

	~Impl()
	{
		// Whole-tracer teardown (document closed, or GPU engine torn down) -
		// distinct from freeSceneBuffers()'s own per-rebuild log line above:
		// seeing THIS line in the log confirms the pipeline/module/device
		// context themselves (not just the scene's mesh/texture buffers) were
		// actually released, not just their owning RtOptixSceneTracer/
		// RtOptixPathTracingSession going out of scope in C++ terms.
		qInfo() << "RtOptixSceneTracer: tracer destroyed - releasing pipeline/module/device context.";
		freeSceneBuffers(/*includePersistentCaches=*/true);
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

		if (skinningModule) cuModuleUnload(skinningModule);
		if (morphModule) cuModuleUnload(morphModule);
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

// updateSkinBase() below uploads glm::vec3/glm::vec4/glm::mat4 host arrays
// verbatim (a plain cudaMemcpy of their raw bytes) and has the device kernel
// read them back as float3/float4 - only valid if GLM's default (no forced
// alignment/padding) layout is in effect, matching plain CUDA vector_types.h
// byte-for-byte. This project never defines GLM_FORCE_ALIGNED_GENTYPES/
// GLM_FORCE_XYZW_ONLY-style macros that would break that assumption, but
// enforce it at compile time anyway rather than risk silently garbled
// positions if that ever changes.
static_assert(sizeof(glm::vec3) == sizeof(float3), "glm::vec3 must match CUDA float3's layout for RtOptixSkinning.cu's raw device-buffer uploads");
static_assert(sizeof(glm::vec4) == sizeof(float4), "glm::vec4 must match CUDA float4's layout for RtOptixSkinning.cu's raw device-buffer uploads");
static_assert(sizeof(glm::mat4) == sizeof(float4) * 4, "glm::mat4 must be 4 contiguous float4 columns for RtOptixSkinning.cu's raw joint-palette upload");

bool RtOptixSceneTracer::Impl::ensureSkinningModuleLoaded()
{
	if (skinningModule && skinningFunction)
		return true;
	if (skinningModuleLoadAttempted)
		return false; // already tried and failed - never retried
	skinningModuleLoadAttempted = true;

	if (!cuCheck(cuModuleLoadDataEx(&skinningModule, g_rtOptixSkinningPtx, 0, nullptr, nullptr), "cuModuleLoadDataEx(RtOptixSkinning)"))
	{
		skinningModule = nullptr;
		return false;
	}
	if (!cuCheck(cuModuleGetFunction(&skinningFunction, skinningModule, "skinVertices"), "cuModuleGetFunction(skinVertices)"))
	{
		cuModuleUnload(skinningModule);
		skinningModule = nullptr;
		skinningFunction = nullptr;
		return false;
	}
	return true;
}

bool RtOptixSceneTracer::Impl::updateSkinBase(const RtMeshGeometry& mesh, CUdeviceptr outPositions, CUdeviceptr outNormals, CUdeviceptr outTangents)
{
	if (!ensureSkinningModuleLoaded())
		return false;

	const uint32_t vertexCount = static_cast<uint32_t>(mesh.baseSkinPositions.size());
	if (vertexCount == 0 || mesh.jointPalette.empty())
		return false; // nothing meaningful to blend this build (e.g. palette not currently available)

	auto cachedIt = persistentSkinBaseCache.find(mesh.sourceObjectId);
	if (cachedIt == persistentSkinBaseCache.end() || cachedIt->second.contentHash != mesh.baseContentHash)
	{
		// Missing, or the bind pose itself changed (mesh reload/edit) - rare,
		// most builds hit the "already resident" branch below instead.
		SkinBaseEntry entry;
		entry.contentHash = mesh.baseContentHash;

		const size_t posBytes = mesh.baseSkinPositions.size() * sizeof(glm::vec3);
		const size_t jointBytes = mesh.jointIndices.size() * sizeof(glm::vec4);

		bool ok = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&entry.basePositions), posBytes), "cudaMalloc(skin base positions)");
		if (ok) ok = cudaCheck(cudaMemcpy(reinterpret_cast<void*>(entry.basePositions), mesh.baseSkinPositions.data(), posBytes, cudaMemcpyHostToDevice), "cudaMemcpy(skin base positions)");
		if (ok) ok = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&entry.baseNormals), posBytes), "cudaMalloc(skin base normals)");
		if (ok) ok = cudaCheck(cudaMemcpy(reinterpret_cast<void*>(entry.baseNormals), mesh.baseSkinNormals.data(), posBytes, cudaMemcpyHostToDevice), "cudaMemcpy(skin base normals)");
		if (ok) ok = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&entry.baseTangents), posBytes), "cudaMalloc(skin base tangents)");
		if (ok) ok = cudaCheck(cudaMemcpy(reinterpret_cast<void*>(entry.baseTangents), mesh.baseSkinTangents.data(), posBytes, cudaMemcpyHostToDevice), "cudaMemcpy(skin base tangents)");
		if (ok) ok = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&entry.jointIndices), jointBytes), "cudaMalloc(skin joint indices)");
		if (ok) ok = cudaCheck(cudaMemcpy(reinterpret_cast<void*>(entry.jointIndices), mesh.jointIndices.data(), jointBytes, cudaMemcpyHostToDevice), "cudaMemcpy(skin joint indices)");
		if (ok) ok = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&entry.jointWeights), jointBytes), "cudaMalloc(skin joint weights)");
		if (ok) ok = cudaCheck(cudaMemcpy(reinterpret_cast<void*>(entry.jointWeights), mesh.jointWeights.data(), jointBytes, cudaMemcpyHostToDevice), "cudaMemcpy(skin joint weights)");

		if (!ok)
		{
			freeSkinBaseEntry(entry);
			return false;
		}

		if (cachedIt != persistentSkinBaseCache.end())
			freeSkinBaseEntry(cachedIt->second); // replacing a stale bind pose - free the old buffers first
		persistentSkinBaseCache[mesh.sourceObjectId] = entry;
		cachedIt = persistentSkinBaseCache.find(mesh.sourceObjectId);
	}
	cachedIt->second.touchedThisBuild = true;

	// This build's joint palette - small, changes every animation tick
	// (unlike everything else here). Keep one reusable device buffer sized
	// to the largest palette seen so far instead of reallocating every mesh
	// on every animation frame.
	const size_t paletteBytes = mesh.jointPalette.size() * sizeof(glm::mat4);
	if (!ensureReusableDeviceBuffer(skinJointPaletteBuffer, paletteBytes, "cudaMalloc(reusable joint palette buffer)"))
		return false;
	if (!cudaCheck(cudaMemcpy(reinterpret_cast<void*>(skinJointPaletteBuffer.ptr), mesh.jointPalette.data(), paletteBytes, cudaMemcpyHostToDevice), "cudaMemcpy(joint palette)"))
		return false;

	RtOptixSkinningParams hostParams{};
	hostParams.basePositions = reinterpret_cast<const float3*>(cachedIt->second.basePositions);
	hostParams.baseNormals = reinterpret_cast<const float3*>(cachedIt->second.baseNormals);
	hostParams.baseTangents = reinterpret_cast<const float3*>(cachedIt->second.baseTangents);
	hostParams.jointIndices = reinterpret_cast<const float4*>(cachedIt->second.jointIndices);
	hostParams.jointWeights = reinterpret_cast<const float4*>(cachedIt->second.jointWeights);
	hostParams.jointPalette = reinterpret_cast<const float4*>(skinJointPaletteBuffer.ptr);
	hostParams.jointCount = static_cast<unsigned int>(mesh.jointPalette.size());
	hostParams.vertexCount = vertexCount;
	hostParams.outPositions = reinterpret_cast<float3*>(outPositions);
	hostParams.outNormals = reinterpret_cast<float3*>(outNormals);
	hostParams.outTangents = reinterpret_cast<float4*>(outTangents);

	CUdeviceptr paramsDevicePtr = 0;
	size_t paramsSize = 0;
	bool ok = cuCheck(cuModuleGetGlobal(&paramsDevicePtr, &paramsSize, skinningModule, "params"), "cuModuleGetGlobal(params)");
	if (ok) ok = cuCheck(cuMemcpyHtoD(paramsDevicePtr, &hostParams, sizeof(hostParams)), "cuMemcpyHtoD(skinning params)");
	if (ok)
	{
		constexpr unsigned int kBlockSize = 256;
		const unsigned int gridSize = (vertexCount + kBlockSize - 1) / kBlockSize;
		ok = cuCheck(cuLaunchKernel(skinningFunction, gridSize, 1, 1, kBlockSize, 1, 1, 0, nullptr, nullptr, nullptr), "cuLaunchKernel(skinVertices)");
	}
	return ok;
}

bool RtOptixSceneTracer::Impl::ensureMorphModuleLoaded()
{
	if (morphModule && morphFunction)
		return true;
	if (morphModuleLoadAttempted)
		return false; // already tried and failed - never retried
	morphModuleLoadAttempted = true;

	if (!cuCheck(cuModuleLoadDataEx(&morphModule, g_rtOptixMorphPtx, 0, nullptr, nullptr), "cuModuleLoadDataEx(RtOptixMorph)"))
	{
		morphModule = nullptr;
		return false;
	}
	if (!cuCheck(cuModuleGetFunction(&morphFunction, morphModule, "morphVertices"), "cuModuleGetFunction(morphVertices)"))
	{
		cuModuleUnload(morphModule);
		morphModule = nullptr;
		morphFunction = nullptr;
		return false;
	}
	return true;
}

bool RtOptixSceneTracer::Impl::updateMorphBase(const RtMeshGeometry& mesh, CUdeviceptr outPositions, CUdeviceptr outNormals, CUdeviceptr outTangents)
{
	if (!ensureMorphModuleLoaded())
		return false;

	const uint32_t vertexCount = static_cast<uint32_t>(mesh.baseMorphPositions.size());
	if (vertexCount == 0 || mesh.morphTargetCount == 0 || mesh.morphWeights.empty())
		return false; // nothing meaningful to blend this build

	auto cachedIt = persistentMorphBaseCache.find(mesh.sourceObjectId);
	if (cachedIt == persistentMorphBaseCache.end() || cachedIt->second.contentHash != mesh.morphBaseContentHash)
	{
		// Missing, or the rest pose/deltas themselves changed (mesh
		// reload/edit) - rare, most builds hit the "already resident"
		// branch below instead.
		MorphBaseEntry entry;
		entry.contentHash = mesh.morphBaseContentHash;

		const size_t posBytes = mesh.baseMorphPositions.size() * sizeof(glm::vec3);
		const size_t deltaBytes = mesh.morphTargetDeltaPositions.size() * sizeof(glm::vec3);
		const size_t flagBytes = static_cast<size_t>(mesh.morphTargetCount) * sizeof(uint8_t);

		bool ok = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&entry.basePositions), posBytes), "cudaMalloc(morph base positions)");
		if (ok) ok = cudaCheck(cudaMemcpy(reinterpret_cast<void*>(entry.basePositions), mesh.baseMorphPositions.data(), posBytes, cudaMemcpyHostToDevice), "cudaMemcpy(morph base positions)");
		if (ok) ok = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&entry.baseNormals), posBytes), "cudaMalloc(morph base normals)");
		if (ok) ok = cudaCheck(cudaMemcpy(reinterpret_cast<void*>(entry.baseNormals), mesh.baseMorphNormals.data(), posBytes, cudaMemcpyHostToDevice), "cudaMemcpy(morph base normals)");
		if (ok) ok = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&entry.baseTangents), posBytes), "cudaMalloc(morph base tangents)");
		if (ok) ok = cudaCheck(cudaMemcpy(reinterpret_cast<void*>(entry.baseTangents), mesh.baseMorphTangents.data(), posBytes, cudaMemcpyHostToDevice), "cudaMemcpy(morph base tangents)");
		if (ok) ok = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&entry.deltaPositions), deltaBytes), "cudaMalloc(morph delta positions)");
		if (ok) ok = cudaCheck(cudaMemcpy(reinterpret_cast<void*>(entry.deltaPositions), mesh.morphTargetDeltaPositions.data(), deltaBytes, cudaMemcpyHostToDevice), "cudaMemcpy(morph delta positions)");
		if (ok) ok = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&entry.deltaNormals), deltaBytes), "cudaMalloc(morph delta normals)");
		if (ok) ok = cudaCheck(cudaMemcpy(reinterpret_cast<void*>(entry.deltaNormals), mesh.morphTargetDeltaNormals.data(), deltaBytes, cudaMemcpyHostToDevice), "cudaMemcpy(morph delta normals)");
		if (ok) ok = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&entry.deltaTangents), deltaBytes), "cudaMalloc(morph delta tangents)");
		if (ok) ok = cudaCheck(cudaMemcpy(reinterpret_cast<void*>(entry.deltaTangents), mesh.morphTargetDeltaTangents.data(), deltaBytes, cudaMemcpyHostToDevice), "cudaMemcpy(morph delta tangents)");
		if (ok) ok = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&entry.hasPositionDeltas), flagBytes), "cudaMalloc(morph has-position-deltas)");
		if (ok) ok = cudaCheck(cudaMemcpy(reinterpret_cast<void*>(entry.hasPositionDeltas), mesh.morphTargetHasPositionDeltas.data(), flagBytes, cudaMemcpyHostToDevice), "cudaMemcpy(morph has-position-deltas)");
		if (ok) ok = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&entry.hasNormalDeltas), flagBytes), "cudaMalloc(morph has-normal-deltas)");
		if (ok) ok = cudaCheck(cudaMemcpy(reinterpret_cast<void*>(entry.hasNormalDeltas), mesh.morphTargetHasNormalDeltas.data(), flagBytes, cudaMemcpyHostToDevice), "cudaMemcpy(morph has-normal-deltas)");
		if (ok) ok = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&entry.hasTangentDeltas), flagBytes), "cudaMalloc(morph has-tangent-deltas)");
		if (ok) ok = cudaCheck(cudaMemcpy(reinterpret_cast<void*>(entry.hasTangentDeltas), mesh.morphTargetHasTangentDeltas.data(), flagBytes, cudaMemcpyHostToDevice), "cudaMemcpy(morph has-tangent-deltas)");

		if (!ok)
		{
			freeMorphBaseEntry(entry);
			return false;
		}

		if (cachedIt != persistentMorphBaseCache.end())
			freeMorphBaseEntry(cachedIt->second); // replacing a stale rest pose - free the old buffers first
		persistentMorphBaseCache[mesh.sourceObjectId] = entry;
		cachedIt = persistentMorphBaseCache.find(mesh.sourceObjectId);
	}
	cachedIt->second.touchedThisBuild = true;

	// This build's weights array - small, changes every animation tick
	// (unlike everything else here). Reuse one device buffer across frames
	// rather than reallocating for every morph mesh.
	const size_t weightsBytes = mesh.morphWeights.size() * sizeof(float);
	if (!ensureReusableDeviceBuffer(morphWeightsBuffer, weightsBytes, "cudaMalloc(reusable morph weights buffer)"))
		return false;
	if (!cudaCheck(cudaMemcpy(reinterpret_cast<void*>(morphWeightsBuffer.ptr), mesh.morphWeights.data(), weightsBytes, cudaMemcpyHostToDevice), "cudaMemcpy(morph weights)"))
		return false;

	RtOptixMorphParams hostParams{};
	hostParams.basePositions = reinterpret_cast<const float3*>(cachedIt->second.basePositions);
	hostParams.baseNormals = reinterpret_cast<const float3*>(cachedIt->second.baseNormals);
	hostParams.baseTangents = reinterpret_cast<const float3*>(cachedIt->second.baseTangents);
	hostParams.deltaPositions = reinterpret_cast<const float3*>(cachedIt->second.deltaPositions);
	hostParams.deltaNormals = reinterpret_cast<const float3*>(cachedIt->second.deltaNormals);
	hostParams.deltaTangents = reinterpret_cast<const float3*>(cachedIt->second.deltaTangents);
	hostParams.hasPositionDeltas = reinterpret_cast<const unsigned char*>(cachedIt->second.hasPositionDeltas);
	hostParams.hasNormalDeltas = reinterpret_cast<const unsigned char*>(cachedIt->second.hasNormalDeltas);
	hostParams.hasTangentDeltas = reinterpret_cast<const unsigned char*>(cachedIt->second.hasTangentDeltas);
	hostParams.weights = reinterpret_cast<const float*>(morphWeightsBuffer.ptr);
	hostParams.morphTargetCount = mesh.morphTargetCount;
	hostParams.vertexCount = vertexCount;
	hostParams.outPositions = reinterpret_cast<float3*>(outPositions);
	hostParams.outNormals = reinterpret_cast<float3*>(outNormals);
	hostParams.outTangents = reinterpret_cast<float4*>(outTangents);

	CUdeviceptr paramsDevicePtr = 0;
	size_t paramsSize = 0;
	bool ok = cuCheck(cuModuleGetGlobal(&paramsDevicePtr, &paramsSize, morphModule, "params"), "cuModuleGetGlobal(morph params)");
	if (ok) ok = cuCheck(cuMemcpyHtoD(paramsDevicePtr, &hostParams, sizeof(hostParams)), "cuMemcpyHtoD(morph params)");
	if (ok)
	{
		constexpr unsigned int kBlockSize = 256;
		const unsigned int gridSize = (vertexCount + kBlockSize - 1) / kBlockSize;
		ok = cuCheck(cuLaunchKernel(morphFunction, gridSize, 1, 1, kBlockSize, 1, 1, 0, nullptr, nullptr, nullptr), "cuLaunchKernel(morphVertices)");
	}
	return ok;
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
	// comment for why) ---
	if (!cudaCheck(cudaFree(0), "cudaFree(0)"))
		return;
	if (!optixCheck(optixInit(), "optixInit()"))
		return;

	OptixDeviceContextOptions contextOptions{};
	contextOptions.logCallbackFunction = &optixLogCallback;
	contextOptions.logCallbackLevel = 4;
	if (!optixCheck(optixDeviceContextCreate(nullptr, &contextOptions, &_impl->context), "optixDeviceContextCreate()"))
		return;

	cudaDeviceProp props{};
	if (cudaGetDeviceProperties(&props, 0) == cudaSuccess)
		_impl->deviceName = props.name;

	// Logs whether this GPU actually has RT cores (hardware BVH traversal/
	// ray-triangle intersection) or OptiX is falling back to a software
	// megakernel implementation - OPTIX_DEVICE_PROPERTY_RTCORE_VERSION is
	// the canonical way to ask this (0 = no RT cores present, nonzero = a
	// hardware generation number, e.g. 10/Turing, 20/Ampere, 30/Ada). More
	// authoritative than inferring it from raw CUDA compute capability,
	// since it's the exact thing OptiX itself checked before deciding which
	// traversal path to compile/dispatch for this device. Stored on Impl (not
	// just logged) so the Diagnostics tab can show "Hardware RT"/"Software
	// (megakernel)" without re-querying it every poll tick.
	if (optixCheck(optixDeviceContextGetProperty(_impl->context, OPTIX_DEVICE_PROPERTY_RTCORE_VERSION,
		&_impl->rtCoreVersion, sizeof(_impl->rtCoreVersion)), "optixDeviceContextGetProperty(RTCORE_VERSION)"))
	{
		if (_impl->rtCoreVersion > 0)
			qInfo() << "RtOptixSceneTracer: RT core version" << _impl->rtCoreVersion << "- using hardware-accelerated ray tracing.";
		else
			qInfo() << "RtOptixSceneTracer: no RT cores on this device - OptiX is using its software (megakernel) BVH traversal fallback.";
	}

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

bool RtOptixSceneTracer::hasHardwareRT() const
{
	return _impl->rtCoreVersion > 0;
}

const char* RtOptixSceneTracer::deviceName() const
{
	return _impl->deviceName.c_str();
}

double RtOptixSceneTracer::lastGasBuildMs() const
{
	return _impl->lastGasBuildMs;
}

double RtOptixSceneTracer::lastIasBuildMs() const
{
	return _impl->lastIasBuildMs;
}

uint64_t RtOptixSceneTracer::lastTriangleCount() const
{
	return _impl->lastTriangleCount;
}

bool RtOptixSceneTracer::buildScene(const RtSceneSnapshot& snapshot)
{
	if (!_impl->valid)
		return false;

	// Releases whatever THIS build attempt allocated if the function returns
	// before reaching the success point at the bottom (there are ~15 early
	// `return false;` sites below, for bad geometry/CUDA allocation failures/
	// etc. - not worth annotating each one individually). Without this, a
	// build that fails partway through leaves its already-uploaded
	// environment/lights/instance buffers allocated until the NEXT
	// buildScene() call (whose own freeSceneBuffers() at the top would
	// eventually reach them) or the tracer's destruction - exactly backwards
	// for a failure that may itself be OOM-triggered, which should shed
	// memory pressure immediately instead of compounding it. GAS/texture
	// buffers are handled separately now (see persistentGasCache/
	// persistentTextureCache's own doc comments) - a mesh/texture that
	// already succeeded before a LATER failure elsewhere in this same
	// attempt stays cached rather than being torn down by this guard, since
	// there's no reason to redo work that already succeeded.
	bool succeeded = false;
	struct BuildFailureGuard
	{
		Impl* impl;
		const bool& succeeded;
		~BuildFailureGuard() { if (!succeeded) impl->freeSceneBuffers(); }
	} buildFailureGuard{ _impl.get(), succeeded };

	_impl->ensureSheenAlbedoLut(); // process-constant, baked once - see its own doc comment
	_impl->freeSceneBuffers();
	_impl->infinitePlaneEnabled = snapshot.infinitePlane.enabled;
	_impl->infinitePlaneCameraUpAxisZUp = snapshot.infinitePlane.cameraUpAxisZUp;
	_impl->infinitePlaneHeight = snapshot.infinitePlane.height;
	_impl->infinitePlaneIsShadowCatcher = snapshot.infinitePlane.material.isShadowCatcher;
	_impl->infinitePlaneShadowCatcherDarkness = snapshot.infinitePlane.material.shadowCatcherDarkness;
	// shadowCatcherBaseColor/Metalness/Roughness - the independent flat
	// material NVIDIA's own infinitePlane* fields represent (never the
	// floor's real baseColor/metalness) - see RtMaterial::isShadowCatcher's
	// doc comment.
	_impl->infinitePlaneBaseColor = snapshot.infinitePlane.material.shadowCatcherBaseColor;
	_impl->infinitePlaneMetalness = snapshot.infinitePlane.material.shadowCatcherMetalness;
	_impl->infinitePlaneRoughness = snapshot.infinitePlane.material.shadowCatcherRoughness;

	// --- One GAS per unique mesh - mirrors RtEmbreeScene::build()'s BLAS
	// loop exactly, just building an OptiX GAS instead of an Embree scene.
	// Content-keyed against Impl::persistentGasCache first (see that
	// member's doc comment) - a mesh unchanged since the last buildScene()
	// call reuses its already-uploaded/built GAS with zero new device work. ---
	_impl->meshGasEntries.reserve(snapshot.meshes.size());
	_impl->lastGasBuildMs = 0.0;
	_impl->lastTriangleCount = 0;
	int gasCacheHits = 0;
	int gasRefits = 0;
	int gasGpuSkinUpdates = 0; // subset of gasRefits actually handled by RtOptixSkinning.cu's device blend instead of a CPU-bake-and-memcpy
	int gasGpuMorphUpdates = 0; // subset of gasRefits actually handled by RtOptixMorph.cu's device blend instead of a CPU-bake-and-memcpy
	int gasCacheMisses = 0;
	for (const RtMeshGeometry& mesh : snapshot.meshes)
	{
		Impl::MeshGas gas;

		if (mesh.vertices.empty() || mesh.indices.size() < 3)
		{
			_impl->meshGasEntries.push_back(gas); // empty mesh - handle stays 0, referenced by no instance normally
			continue;
		}

		auto cachedIt = _impl->persistentGasCache.find(mesh.sourceObjectId);
		bool haveCachedEntry = cachedIt != _impl->persistentGasCache.end();

		if (haveCachedEntry && cachedIt->second.contentHash == mesh.contentHash)
		{
			// Same source object + same content hash as a GAS already built -
			// either this is the first duplicate reference within THIS build
			// (identical geometry reused by another instance - rare in this
			// codebase's 1:1 mesh model, see RtSceneSnapshot.h's own comment
			// on deferred instance-sharing) or, far more commonly, carried
			// over unchanged from the PREVIOUS build. Either way, no new
			// device work at all.
			cachedIt->second.touchedThisBuild = true;
			++gasCacheHits;
			_impl->lastTriangleCount += mesh.indices.size() / 3;
			gas.positions = cachedIt->second.positions;
			gas.indices = cachedIt->second.indices;
			gas.normals = cachedIt->second.normals;
			gas.texCoords = cachedIt->second.texCoords;
			gas.tangents = cachedIt->second.tangents;
			gas.vertexColors = cachedIt->second.vertexColors;
			gas.gasOutputBuffer = cachedIt->second.gasOutputBuffer;
			gas.handle = cachedIt->second.handle;
			_impl->meshGasEntries.push_back(gas);
			continue;
		}

		// MISS - content hash differs (or this is a genuinely new object).
		// REFIT-eligible if there's a previous entry for this same object
		// whose vertex/index COUNT is unchanged - topology-preserving
		// deformation (skinning/morph moves positions/normals/tangents, never
		// triangle connectivity) - so its existing device buffers can be
		// overwritten and the GAS updated in place (OPTIX_BUILD_OPERATION_
		// UPDATE) instead of freed and rebuilt from scratch. This is the
		// dominant cost during actual skinned-animation playback (confirmed
		// via a real session log: BrainStem.glb's ~59 skinned meshes each
		// missed and paid a FULL rebuild on every single animation tick
		// before this refit path existed).
		const bool refitEligible = haveCachedEntry &&
			cachedIt->second.vertexCount == static_cast<uint32_t>(mesh.vertices.size()) &&
			cachedIt->second.indexCount == static_cast<uint32_t>(mesh.indices.size());

		bool refitSucceeded = false;

		// GPU-skin fast path - attempted BEFORE building any host-side vertex
		// arrays below, not after: this mesh's positions/normals/tangents
		// never need to touch the CPU at all when this succeeds, so building
		// them first (only to discard almost all of it once the GPU kernel
		// writes its own result) would be pure waste. Confirmed via a real
		// session log that this waste was real and measurable: BrainStem's
		// per-frame cadence didn't improve at all when the GPU-skin kernel
		// first landed, because this exact host-side loop was still running
		// unconditionally for all 59 skinned meshes every frame regardless.
		if (refitEligible && mesh.hasSkinningData)
		{
			gas.positions = cachedIt->second.positions;
			gas.normals = cachedIt->second.normals;
			gas.texCoords = cachedIt->second.texCoords;
			gas.tangents = cachedIt->second.tangents;
			gas.vertexColors = cachedIt->second.vertexColors;
			gas.indices = cachedIt->second.indices;
			gas.gasOutputBuffer = cachedIt->second.gasOutputBuffer;

			if (_impl->updateSkinBase(mesh, gas.positions, gas.normals, gas.tangents))
			{
				OptixAccelBuildOptions accelOptions{};
				accelOptions.buildFlags = OPTIX_BUILD_FLAG_ALLOW_RANDOM_VERTEX_ACCESS | OPTIX_BUILD_FLAG_ALLOW_UPDATE;
				accelOptions.operation = OPTIX_BUILD_OPERATION_UPDATE;

				const uint32_t triangleInputFlags[1] = { OPTIX_GEOMETRY_FLAG_NONE };
				OptixBuildInput triangleInput{};
				triangleInput.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
				triangleInput.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
				triangleInput.triangleArray.numVertices = static_cast<uint32_t>(mesh.vertices.size());
				triangleInput.triangleArray.vertexBuffers = &gas.positions;
				triangleInput.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
				triangleInput.triangleArray.numIndexTriplets = static_cast<uint32_t>(mesh.indices.size() / 3);
				triangleInput.triangleArray.indexBuffer = gas.indices;
				triangleInput.triangleArray.flags = triangleInputFlags;
				triangleInput.triangleArray.numSbtRecords = 1;

				OptixAccelBufferSizes gasBufferSizes{};
				bool ok = optixCheck(optixAccelComputeMemoryUsage(_impl->context, &accelOptions, &triangleInput, 1, &gasBufferSizes), "optixAccelComputeMemoryUsage(GAS GPU-skin refit)");
				if (ok)
				{
					ok = _impl->ensureReusableDeviceBuffer(_impl->accelScratchBuffer, gasBufferSizes.tempUpdateSizeInBytes, "cudaMalloc(reusable GAS refit scratch)");
					if (ok)
					{
						const CUdeviceptr tempBuffer = gasBufferSizes.tempUpdateSizeInBytes > 0 ? _impl->accelScratchBuffer.ptr : 0;
						QElapsedTimer gasTimer;
						gasTimer.start();
						ok = optixCheck(optixAccelBuild(_impl->context, 0, &accelOptions, &triangleInput, 1,
							tempBuffer, gasBufferSizes.tempUpdateSizeInBytes,
							gas.gasOutputBuffer, gasBufferSizes.outputSizeInBytes,
							&gas.handle, nullptr, 0), "optixAccelBuild(GAS GPU-skin refit)");
						_impl->lastGasBuildMs += static_cast<double>(gasTimer.nsecsElapsed()) / 1.0e6;
						if (ok)
							_impl->lastTriangleCount += mesh.indices.size() / 3;
					}
				}

				if (ok)
				{
					++gasGpuSkinUpdates;
					refitSucceeded = true;
					++gasRefits;
					cachedIt->second.contentHash = mesh.contentHash;
					cachedIt->second.handle = gas.handle;
					cachedIt->second.touchedThisBuild = true;
					_impl->meshGasEntries.push_back(gas);
					continue;
				}
				// GPU-skin update succeeded but the accel build itself
				// failed - fall through to the slow path below (host-array
				// build + CPU-bake refit), same as if updateSkinBase() had
				// failed in the first place. gas.handle may be stale here,
				// but the slow path rebuilds it from scratch via the same
				// buffers regardless.
			}
			// updateSkinBase() unavailable/failed - fall through to the slow
			// (CPU-bake) path below, same as any other refit-eligible mesh.
		}

		// GPU-morph fast path - same rationale as the GPU-skin path above,
		// scoped to MORPH-ONLY meshes (hasSkinningData == false): a mesh
		// animated by BOTH skinning and morphing at once would need the
		// morph kernel's output chained as the skin kernel's input (the
		// "rest pose" the skin kernel consumes would then change every
		// frame morphing does, breaking the skin-base cache's "upload once"
		// assumption) - a distinct chaining problem, not attempted here.
		// Such meshes keep falling through to the CPU-bake path below
		// exactly as they did before GPU-morph blending existed at all -
		// no regression, just no speedup for that combination.
		if (refitEligible && !refitSucceeded && mesh.hasMorphData && !mesh.hasSkinningData)
		{
			gas.positions = cachedIt->second.positions;
			gas.normals = cachedIt->second.normals;
			gas.texCoords = cachedIt->second.texCoords;
			gas.tangents = cachedIt->second.tangents;
			gas.vertexColors = cachedIt->second.vertexColors;
			gas.indices = cachedIt->second.indices;
			gas.gasOutputBuffer = cachedIt->second.gasOutputBuffer;

			if (_impl->updateMorphBase(mesh, gas.positions, gas.normals, gas.tangents))
			{
				OptixAccelBuildOptions accelOptions{};
				accelOptions.buildFlags = OPTIX_BUILD_FLAG_ALLOW_RANDOM_VERTEX_ACCESS | OPTIX_BUILD_FLAG_ALLOW_UPDATE;
				accelOptions.operation = OPTIX_BUILD_OPERATION_UPDATE;

				const uint32_t triangleInputFlags[1] = { OPTIX_GEOMETRY_FLAG_NONE };
				OptixBuildInput triangleInput{};
				triangleInput.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
				triangleInput.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
				triangleInput.triangleArray.numVertices = static_cast<uint32_t>(mesh.vertices.size());
				triangleInput.triangleArray.vertexBuffers = &gas.positions;
				triangleInput.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
				triangleInput.triangleArray.numIndexTriplets = static_cast<uint32_t>(mesh.indices.size() / 3);
				triangleInput.triangleArray.indexBuffer = gas.indices;
				triangleInput.triangleArray.flags = triangleInputFlags;
				triangleInput.triangleArray.numSbtRecords = 1;

				OptixAccelBufferSizes gasBufferSizes{};
				bool ok = optixCheck(optixAccelComputeMemoryUsage(_impl->context, &accelOptions, &triangleInput, 1, &gasBufferSizes), "optixAccelComputeMemoryUsage(GAS GPU-morph refit)");
				if (ok)
				{
					ok = _impl->ensureReusableDeviceBuffer(_impl->accelScratchBuffer, gasBufferSizes.tempUpdateSizeInBytes, "cudaMalloc(reusable GAS refit scratch)");
					if (ok)
					{
						const CUdeviceptr tempBuffer = gasBufferSizes.tempUpdateSizeInBytes > 0 ? _impl->accelScratchBuffer.ptr : 0;
						QElapsedTimer gasTimer;
						gasTimer.start();
						ok = optixCheck(optixAccelBuild(_impl->context, 0, &accelOptions, &triangleInput, 1,
							tempBuffer, gasBufferSizes.tempUpdateSizeInBytes,
							gas.gasOutputBuffer, gasBufferSizes.outputSizeInBytes,
							&gas.handle, nullptr, 0), "optixAccelBuild(GAS GPU-morph refit)");
						_impl->lastGasBuildMs += static_cast<double>(gasTimer.nsecsElapsed()) / 1.0e6;
						if (ok)
							_impl->lastTriangleCount += mesh.indices.size() / 3;
					}
				}

				if (ok)
				{
					++gasGpuMorphUpdates;
					refitSucceeded = true;
					++gasRefits;
					cachedIt->second.contentHash = mesh.contentHash;
					cachedIt->second.handle = gas.handle;
					cachedIt->second.touchedThisBuild = true;
					_impl->meshGasEntries.push_back(gas);
					continue;
				}
				// GPU-morph update succeeded but the accel build itself
				// failed - fall through to the slow path below, same as the
				// GPU-skin path's identical case above.
			}
			// updateMorphBase() unavailable/failed - fall through to the
			// slow (CPU-bake) path below.
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

		if (refitEligible && !refitSucceeded)
		{
			// Reuse the existing device buffers/handle in place - vertex/
			// index COUNT is unchanged (checked above), so their byte sizes
			// match exactly; only their CONTENTS need updating. Indices,
			// texCoords, and vertexColors are NOT re-uploaded: topology-
			// preserving deformation (skinning/morph) only ever moves
			// positions/normals/tangents - triangle connectivity, UVs, and
			// vertex colors are baked once at import and never touched by
			// animation, so re-uploading byte-identical data for them every
			// single frame (as an earlier version of this refit path did)
			// was pure waste.
			//
			// Reached only when the GPU-skin fast path above wasn't taken
			// (not a skinned mesh) or fell through after failing - CPU-bake
			// (already computed into positions/normals/tangents above,
			// unavoidably for THIS case since there's no GPU alternative) +
			// cudaMemcpy is the only option here.
			gas.positions = cachedIt->second.positions;
			gas.normals = cachedIt->second.normals;
			gas.texCoords = cachedIt->second.texCoords;
			gas.tangents = cachedIt->second.tangents;
			gas.vertexColors = cachedIt->second.vertexColors;
			gas.indices = cachedIt->second.indices;
			gas.gasOutputBuffer = cachedIt->second.gasOutputBuffer;

			bool ok = cudaCheck(cudaMemcpy(reinterpret_cast<void*>(gas.positions), positions.data(), positionsBytes, cudaMemcpyHostToDevice), "cudaMemcpy(mesh positions, GAS refit)");
			if (ok) ok = cudaCheck(cudaMemcpy(reinterpret_cast<void*>(gas.normals), normals.data(), normalsBytes, cudaMemcpyHostToDevice), "cudaMemcpy(mesh normals, GAS refit)");
			if (ok) ok = cudaCheck(cudaMemcpy(reinterpret_cast<void*>(gas.tangents), tangents.data(), tangentsBytes, cudaMemcpyHostToDevice), "cudaMemcpy(mesh tangents, GAS refit)");

			if (ok)
			{
				OptixAccelBuildOptions accelOptions{};
				accelOptions.buildFlags = OPTIX_BUILD_FLAG_ALLOW_RANDOM_VERTEX_ACCESS | OPTIX_BUILD_FLAG_ALLOW_UPDATE;
				accelOptions.operation = OPTIX_BUILD_OPERATION_UPDATE;

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
				ok = optixCheck(optixAccelComputeMemoryUsage(_impl->context, &accelOptions, &triangleInput, 1, &gasBufferSizes), "optixAccelComputeMemoryUsage(GAS refit)");
				if (ok)
				{
					CUdeviceptr tempBuffer = 0;
					ok = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&tempBuffer), gasBufferSizes.tempUpdateSizeInBytes), "cudaMalloc(gas refit temp)");
					if (ok)
					{
						QElapsedTimer gasTimer;
						gasTimer.start();
						ok = optixCheck(optixAccelBuild(_impl->context, 0, &accelOptions, &triangleInput, 1,
							tempBuffer, gasBufferSizes.tempUpdateSizeInBytes,
							gas.gasOutputBuffer, gasBufferSizes.outputSizeInBytes,
							&gas.handle, nullptr, 0), "optixAccelBuild(GAS refit)");
						_impl->lastGasBuildMs += static_cast<double>(gasTimer.nsecsElapsed()) / 1.0e6;
						if (ok)
							_impl->lastTriangleCount += mesh.indices.size() / 3;
					}
					if (tempBuffer) cudaFree(reinterpret_cast<void*>(tempBuffer));
				}
			}

			if (ok)
			{
				refitSucceeded = true;
				++gasRefits;
				cachedIt->second.contentHash = mesh.contentHash;
				cachedIt->second.handle = gas.handle;
				cachedIt->second.touchedThisBuild = true;
				_impl->meshGasEntries.push_back(gas);
				continue;
			}

			// Refit failed partway through (bad memcpy/
			// optixAccelComputeMemoryUsage/optixAccelBuild) - the buffers may
			// now hold a mix of old/new content and gas.handle is
			// unreliable. They're still owned by persistentGasCache (not
			// leaked), but this entry can no longer be trusted - erase it
			// (freeing its buffers) and fall through to a genuine full
			// rebuild below, exactly as if refit had never been attempted.
			Impl::freeGasEntry(cachedIt->second);
			_impl->persistentGasCache.erase(cachedIt);
			haveCachedEntry = false;
			gas = Impl::MeshGas{};
		}
		else if (haveCachedEntry)
		{
			// Same object, but vertex/index COUNT changed (topology itself
			// changed, not just a deformation) - the old entry's buffers are
			// the wrong size to refit into; free them before the full
			// rebuild below allocates fresh ones sized for the NEW geometry.
			Impl::freeGasEntry(cachedIt->second);
			_impl->persistentGasCache.erase(cachedIt);
			haveCachedEntry = false;
		}

		if (!refitSucceeded)
		{
			++gasCacheMisses;

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

			if (ok)
			{
				OptixAccelBuildOptions accelOptions{};
				// ALLOW_RANDOM_VERTEX_ACCESS is required for __closesthit__ch()'s
				// optixGetTriangleVertexData() call (texture-footprint/LOD
				// computation - see its call site's doc comment) to legally
				// read a hit triangle's object-space vertex positions back
				// out of this GAS. ALLOW_UPDATE unconditionally too (a small,
				// standard OptiX cost) - required up front for ANY future
				// buildScene() call to legally refit against THIS GAS (e.g.
				// the next skinned/morphed pose change), even though THIS
				// particular call is itself a full build.
				accelOptions.buildFlags = OPTIX_BUILD_FLAG_ALLOW_RANDOM_VERTEX_ACCESS | OPTIX_BUILD_FLAG_ALLOW_UPDATE;
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
				ok = optixCheck(optixAccelComputeMemoryUsage(_impl->context, &accelOptions, &triangleInput, 1, &gasBufferSizes), "optixAccelComputeMemoryUsage()");
				if (ok)
				{
					ok = _impl->ensureReusableDeviceBuffer(_impl->accelScratchBuffer, gasBufferSizes.tempSizeInBytes, "cudaMalloc(reusable GAS build scratch)");
					if (ok) ok = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&gas.gasOutputBuffer), gasBufferSizes.outputSizeInBytes), "cudaMalloc(gas output)");
					if (ok)
					{
						const CUdeviceptr tempBuffer = gasBufferSizes.tempSizeInBytes > 0 ? _impl->accelScratchBuffer.ptr : 0;
						// Diagnostics tab timing only - this still measures the
						// host-side enqueue cost of the build, not a fence-backed
						// GPU duration. Keeping the behavior unchanged here is
						// deliberate: this pass is about removing allocator churn,
						// not redefining the diagnostics metric.
						QElapsedTimer gasTimer;
						gasTimer.start();
						ok = optixCheck(optixAccelBuild(_impl->context, 0, &accelOptions, &triangleInput, 1,
							tempBuffer, gasBufferSizes.tempSizeInBytes,
							gas.gasOutputBuffer, gasBufferSizes.outputSizeInBytes,
							&gas.handle, nullptr, 0), "optixAccelBuild(GAS)");
						_impl->lastGasBuildMs += static_cast<double>(gasTimer.nsecsElapsed()) / 1.0e6;
						if (ok)
							_impl->lastTriangleCount += mesh.indices.size() / 3;
					}
				}
			}

			if (gas.handle != 0)
			{
				// Genuinely new (or topology-changed) GAS - hand ownership of
				// every device buffer to persistentGasCache so it survives
				// past this buildScene() call (see that member's doc
				// comment); THIS build's meshGasEntries entry below is just a
				// non-owning copy of the same pointers/handle.
				Impl::MeshGasEntry entry;
				entry.contentHash = mesh.contentHash;
				entry.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
				entry.indexCount = static_cast<uint32_t>(mesh.indices.size());
				entry.positions = gas.positions;
				entry.indices = gas.indices;
				entry.normals = gas.normals;
				entry.texCoords = gas.texCoords;
				entry.tangents = gas.tangents;
				entry.vertexColors = gas.vertexColors;
				entry.gasOutputBuffer = gas.gasOutputBuffer;
				entry.handle = gas.handle;
				entry.touchedThisBuild = true;
				_impl->persistentGasCache[mesh.sourceObjectId] = entry;
			}
			else
			{
				// Failed partway through (bad malloc/optixAccelComputeMemoryUsage/
				// optixAccelBuild) - whatever WAS allocated above never made it
				// into persistentGasCache, so nothing else will ever free it;
				// free it here instead of leaking it. meshGasEntries.clear() no
				// longer frees anything now that ownership lives in the
				// persistent cache (see freeSceneBuffers()), so this failure
				// path can't rely on that safety net the way it implicitly used
				// to.
				if (gas.positions) cudaFree(reinterpret_cast<void*>(gas.positions));
				if (gas.normals) cudaFree(reinterpret_cast<void*>(gas.normals));
				if (gas.texCoords) cudaFree(reinterpret_cast<void*>(gas.texCoords));
				if (gas.tangents) cudaFree(reinterpret_cast<void*>(gas.tangents));
				if (gas.vertexColors) cudaFree(reinterpret_cast<void*>(gas.vertexColors));
				if (gas.indices) cudaFree(reinterpret_cast<void*>(gas.indices));
				if (gas.gasOutputBuffer) cudaFree(reinterpret_cast<void*>(gas.gasOutputBuffer));
				gas = Impl::MeshGas{}; // all-zero - meshGasEntries must not hold pointers to memory just freed above
			}
		}

		_impl->meshGasEntries.push_back(gas);
	}

	// --- IAS: one OptixInstance per RtInstance, applying its world transform
	// - mirrors RtEmbreeScene::build()'s TLAS loop. ---
	std::vector<OptixInstance> instances;
	instances.reserve(snapshot.instances.size());
	std::vector<HitGroupSbtRecord> hitgroupRecordsHost;
	hitgroupRecordsHost.reserve(snapshot.instances.size());

	// Diagnostics only (not currently logged - inspect via debugger/future
	// diagnostics UI) - lets a report like "GPU performance degrades as more
	// documents get loaded" be read directly instead of re-derived from
	// scratch each time: dedupedTextureRefs high
	// relative to uniqueTextures confirms the dedup (now spanning both THIS
	// build's own shared textures AND anything reused from the PREVIOUS
	// build - see Impl::persistentTextureCache's doc comment) is actually
	// doing its job on a given scene, and textureBytes/mipBytes give a
	// concrete "how much NEW VRAM did this rebuild actually need" figure to
	// compare across documents or over a session.
	struct TextureUploadStats
	{
		int uniqueTextures = 0;      // genuinely new uploads this call (cache miss - new or changed texture)
		int dedupedTextureRefs = 0;  // requests reused from the cache, whether shared within this build or carried over from the last one
		size_t textureBytes = 0;     // total base-level rgba8 bytes uploaded to VRAM (new uploads only)
		int mipLevelsUploaded = 0;   // mip levels uploaded as new buffers (excludes reused level 0)
		int mipLevelsReused = 0;     // level-0 mips that reused the base buffer instead of a new upload
		size_t mipBytes = 0;         // total mip-level bytes uploaded (excludes reused level 0)
	} textureStats;

	// Populates every RtOptixTexture field EXCEPT rgba8/mips/mipCount, which
	// differ between the cache-hit and cache-miss paths below - factored out
	// so those two paths don't have to duplicate this list.
	auto populateTextureTransformFields = [](const RtTextureSample& tex, RtOptixTexture& out)
	{
		out.width = tex.width;
		out.height = tex.height;
		out.texCoordIndex = std::clamp(tex.texCoordIndex, 0, 3);
		out.uvScale = make_float2(tex.uvScale.x, tex.uvScale.y);
		out.uvOffset = make_float2(tex.uvOffset.x, tex.uvOffset.y);
		out.uvRotation = tex.uvRotation;
		out.packingChannel = tex.packingChannel;
		out.packingInvert = tex.packingInvert ? 1 : 0;
		out.packingScale = tex.packingScale;
		out.packingBias = tex.packingBias;
		out.wrapS = tex.wrapS;
		out.wrapT = tex.wrapT;
	};

	// Uploads (or reuses, from Impl::persistentTextureCache - see its own doc
	// comment for why content identity, not RtTextureSample object identity,
	// is what makes cross-buildScene()-call reuse possible) one material
	// texture's rgba8 bytes to the device, populating an RtOptixTexture with
	// the resulting pointer plus its KHR_texture_transform/wrap/channel-
	// packing metadata copied verbatim - mirrors RtTextureSample's fields
	// exactly (see RtOptixSceneParams.h's RtOptixTexture doc comment).
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

		Impl::TextureUploadKey key;
		key.imageCacheKey  = tex->imageCacheKey;
		key.uvScaleX       = tex->uvScale.x;
		key.uvScaleY       = tex->uvScale.y;
		key.uvOffsetX      = tex->uvOffset.x;
		key.uvOffsetY      = tex->uvOffset.y;
		key.uvRotation     = tex->uvRotation;
		key.texCoordIndex  = tex->texCoordIndex;
		key.wrapS          = tex->wrapS;
		key.wrapT          = tex->wrapT;
		key.packingChannel = tex->packingChannel;
		key.packingInvert  = tex->packingInvert;
		key.packingScale   = tex->packingScale;
		key.packingBias    = tex->packingBias;

		if (const auto cached = _impl->persistentTextureCache.find(key); cached != _impl->persistentTextureCache.end())
		{
			// Same source image + same baked-in uv/wrap/packing params as a
			// texture already resident - either shared by another material
			// within THIS build, or carried over unchanged from the PREVIOUS
			// one. Either way, no new device work at all.
			cached->second.touchedThisBuild = true;
			++textureStats.dedupedTextureRefs;

			populateTextureTransformFields(*tex, out);
			out.rgba8 = reinterpret_cast<const uchar4*>(cached->second.baseRgba8);
			out.mips = reinterpret_cast<const RtOptixTextureMipLevel*>(cached->second.mipArrayDevice);
			out.mipCount = cached->second.mipCount;
			return;
		}

		// Cache miss - genuinely new (or changed since last build) texture.
		CUdeviceptr deviceRgba8 = 0;
		const size_t bytes = tex->rgba8.size();
		if (!cudaCheck(cudaMalloc(reinterpret_cast<void**>(&deviceRgba8), bytes), "cudaMalloc(material texture)"))
			return;
		if (!cudaCheck(cudaMemcpy(reinterpret_cast<void*>(deviceRgba8), tex->rgba8.data(), bytes, cudaMemcpyHostToDevice), "cudaMemcpy(material texture)"))
		{
			cudaFree(reinterpret_cast<void*>(deviceRgba8));
			return;
		}
		++textureStats.uniqueTextures;
		textureStats.textureBytes += bytes;

		// Box-filter mip pyramid (RtTextureSample::mips, built once on the
		// host by RtSceneBuilder::buildMipChain()) - each level's rgba8
		// uploaded individually, then a device array of RtOptixTextureMipLevel
		// {devicePtr,width,height} entries uploaded once, mirroring
		// RtOptixEnvironment::prefilterMips' own per-level-array upload
		// pattern. See RtOptixTexture::mips' doc comment for the consumer.
		std::vector<CUdeviceptr> localMipBuffers;
		CUdeviceptr mipArrayDevice = 0;
		int mipCount = 0;
		if (!tex->mips.empty())
		{
			std::vector<RtOptixTextureMipLevel> mipsHost;
			mipsHost.reserve(tex->mips.size());
			bool ok = true;
			bool first = true;
			// Accumulated locally and only folded into textureStats once this
			// texture's mip array is confirmed fully built below - crediting
			// textureStats eagerly during the loop would over-report bytes/
			// levels as "uploaded" for any texture whose build later fails
			// (per-level or at the final array-copy step), since those
			// buffers get freed again rather than ending up resident.
			int localMipLevelsUploaded = 0;
			int localMipLevelsReused = 0;
			size_t localMipBytes = 0;
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
					++localMipLevelsReused;
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
				++localMipLevelsUploaded;
				localMipBytes += mipBytes;
				localMipBuffers.push_back(deviceMipRgba8);

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
					mipCount = static_cast<int>(mipsHost.size());
				}
				else
				{
					// mallocOk but the memcpy failed: mipArrayDevice is a real,
					// still-live allocation - free it here directly or it's
					// orphaned with no remaining pointer to reach it. If
					// mallocOk is false, mipArrayDevice is already 0 and
					// cudaFree(nullptr) is a harmless no-op.
					if (mallocOk)
						cudaFree(reinterpret_cast<void*>(mipArrayDevice));
					mipArrayDevice = 0;
				}
			}

			if (mipCount > 0)
			{
				// Only now, with the array genuinely finalized and resident,
				// do the per-level uploads this texture made actually count
				// as "uploaded" - see localMipLevelsUploaded's own doc
				// comment above for why this isn't credited eagerly.
				textureStats.mipLevelsUploaded += localMipLevelsUploaded;
				textureStats.mipLevelsReused += localMipLevelsReused;
				textureStats.mipBytes += localMipBytes;
			}
			else
			{
				// Either the per-level loop failed partway (ok==false) or it
				// finished but the final array malloc/memcpy above failed -
				// out.mips/mipCount stay null/0 (base-only sampling fallback)
				// and every per-level buffer this texture's loop uploaded is
				// now unreferenced - free them immediately rather than
				// stashing them in the persistent cache entry below.
				for (CUdeviceptr mipBuf : localMipBuffers)
					if (mipBuf) cudaFree(reinterpret_cast<void*>(mipBuf));
				localMipBuffers.clear();
			}
		}

		Impl::TextureUploadEntry entry;
		entry.baseRgba8 = deviceRgba8;
		entry.mipBuffers = std::move(localMipBuffers);
		entry.mipArrayDevice = mipArrayDevice;
		entry.mipCount = mipCount;
		entry.touchedThisBuild = true;
		_impl->persistentTextureCache.emplace(key, std::move(entry));

		populateTextureTransformFields(*tex, out);
		out.rgba8 = reinterpret_cast<const uchar4*>(deviceRgba8);
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
			hgSbt.data.isShadowCatcher = mat.isShadowCatcher ? 1 : 0;
			hgSbt.data.shadowCatcherDarkness = mat.shadowCatcherDarkness;

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
			hgSbt.data.isShadowCatcher = 0;
			hgSbt.data.shadowCatcherDarkness = 0.5f;

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

	// Refit (OPTIX_BUILD_OPERATION_UPDATE) instead of a full rebuild when the
	// TOPOLOGY - instance count and each instance's underlying GAS handle -
	// is unchanged since the last successful build (see
	// Impl::lastIasInstanceHandles' doc comment): a material-only edit or a
	// transform-only move never changes which mesh each instance points at,
	// only the transform values (and material data, decoupled from the IAS
	// itself - see this block's own doc comment above the instance loop) -
	// refitting against unchanged handles (and possibly unchanged transforms
	// too, e.g. a material-only edit) is a valid, cheap no-op update, not a
	// special case to detect separately.
	bool iasRefit = !_impl->lastIasInstanceHandles.empty() &&
		_impl->lastIasInstanceHandles.size() == instances.size() &&
		_impl->instancesBuffer != 0 && _impl->iasOutputBuffer != 0;
	if (iasRefit)
	{
		for (size_t i = 0; i < instances.size(); ++i)
		{
			if (instances[i].traversableHandle != _impl->lastIasInstanceHandles[i])
			{
				iasRefit = false;
				break;
			}
		}
	}

	const size_t instancesBytes = instances.size() * sizeof(OptixInstance);
	if (!iasRefit)
	{
		// Full rebuild - topology changed (or no valid previous build exists
		// to refit against) - the old buffer may be the wrong size for this
		// instance count, so free and reallocate rather than risk reusing a
		// stale size.
		if (_impl->instancesBuffer) cudaFree(reinterpret_cast<void*>(_impl->instancesBuffer));
		_impl->instancesBuffer = 0;
		if (!cudaCheck(cudaMalloc(reinterpret_cast<void**>(&_impl->instancesBuffer), instancesBytes), "cudaMalloc(instances)"))
			return false;
	}
	if (!cudaCheck(cudaMemcpy(reinterpret_cast<void*>(_impl->instancesBuffer), instances.data(), instancesBytes, cudaMemcpyHostToDevice), "cudaMemcpy(instances)"))
		return false;

	OptixBuildInput instanceInput{};
	instanceInput.type = OPTIX_BUILD_INPUT_TYPE_INSTANCES;
	instanceInput.instanceArray.instances = _impl->instancesBuffer;
	instanceInput.instanceArray.numInstances = static_cast<unsigned int>(instances.size());

	OptixAccelBuildOptions iasAccelOptions{};
	// ALLOW_UPDATE unconditionally now (a small, standard OptiX memory/build-
	// time cost) - required up front for ANY future buildScene() call to
	// legally refit against THIS build, even when THIS particular call is
	// itself a full rebuild.
	iasAccelOptions.buildFlags = OPTIX_BUILD_FLAG_ALLOW_UPDATE;
	iasAccelOptions.operation = iasRefit ? OPTIX_BUILD_OPERATION_UPDATE : OPTIX_BUILD_OPERATION_BUILD;

	OptixAccelBufferSizes iasBufferSizes{};
	if (!optixCheck(optixAccelComputeMemoryUsage(_impl->context, &iasAccelOptions, &instanceInput, 1, &iasBufferSizes), "optixAccelComputeMemoryUsage(IAS)"))
		return false;

	if (!iasRefit)
	{
		// Output buffer size depends only on instance count/flags (stable
		// across an update), so a refit reuses the existing one untouched -
		// only a full rebuild (topology changed) needs a fresh allocation.
		if (_impl->iasOutputBuffer) cudaFree(reinterpret_cast<void*>(_impl->iasOutputBuffer));
		_impl->iasOutputBuffer = 0;
		if (!cudaCheck(cudaMalloc(reinterpret_cast<void**>(&_impl->iasOutputBuffer), iasBufferSizes.outputSizeInBytes), "cudaMalloc(ias output)"))
			return false;
	}

	// tempUpdateSizeInBytes (valid whenever ALLOW_UPDATE is set, regardless
	// of which operation is actually chosen) is typically much smaller than
	// a full build's tempSizeInBytes - using the matching one for whichever
	// operation this call actually performs.
	const size_t iasTempBytes = iasRefit ? iasBufferSizes.tempUpdateSizeInBytes : iasBufferSizes.tempSizeInBytes;
	if (!_impl->ensureReusableDeviceBuffer(_impl->accelScratchBuffer, iasTempBytes, "cudaMalloc(reusable IAS scratch)"))
		return false;
	const CUdeviceptr iasTempBuffer = iasTempBytes > 0 ? _impl->accelScratchBuffer.ptr : 0;

	// Diagnostics tab timing only - same enqueue-cost metric as the GAS path
	// above. This optimization pass keeps that metric unchanged.
	QElapsedTimer iasTimer;
	iasTimer.start();
	const bool iasBuilt = optixCheck(optixAccelBuild(_impl->context, 0, &iasAccelOptions, &instanceInput, 1,
		iasTempBuffer, iasTempBytes,
		_impl->iasOutputBuffer, iasBufferSizes.outputSizeInBytes,
		&_impl->iasHandle, nullptr, 0), iasRefit ? "optixAccelBuild(IAS refit)" : "optixAccelBuild(IAS)");
	_impl->lastIasBuildMs = static_cast<double>(iasTimer.nsecsElapsed()) / 1.0e6;
	if (!iasBuilt)
		return false;

	// Record this build's topology so the NEXT buildScene() call can decide
	// refit-vs-rebuild against it.
	_impl->lastIasInstanceHandles.resize(instances.size());
	for (size_t i = 0; i < instances.size(); ++i)
		_impl->lastIasInstanceHandles[i] = instances[i].traversableHandle;

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
		{
			// cudaMalloc above succeeded - outBuffer is a real, live
			// allocation. Every current call site binds outBuffer to a
			// persistent Impl member that freeSceneBuffers() will eventually
			// reach even without this (so this wasn't a live leak today),
			// but freeing it here directly means a caller doesn't have to
			// know that to be correct, and the allocation isn't held onto
			// pointlessly until the next rebuild.
			cudaFree(reinterpret_cast<void*>(outBuffer));
			outBuffer = 0;
			return false;
		}
		return true;
	};

	// Frees every non-null entry of a fixed 6-face cubemap array and zeroes
	// them - used below whenever a face upload fails partway through and the
	// whole cubemap (raw env / irradiance) gets disabled, so the faces that
	// DID succeed before the failure don't sit resident-but-unreferenced
	// until the next buildScene()/destructor - same immediate-release
	// discipline as BuildFailureGuard/the per-texture mip cleanup above.
	auto freeCubemapFaces = [](CUdeviceptr* faces) -> void
	{
		for (int i = 0; i < 6; ++i)
		{
			if (faces[i]) cudaFree(reinterpret_cast<void*>(faces[i]));
			faces[i] = 0;
		}
	};

	// Only the heavy face/mip texel data is uploaded here - the environment
	// SCALARS deliberately flow per-launch through renderScene() instead,
	// see Impl::envFaceBuffers' doc comment for why.
	const RtEnvironment& env = snapshot.environment;

	// Content identity for everything below - see Impl::lastEnvironmentHash's
	// doc comment for why this exists at all (the environment almost never
	// actually changes between two buildScene() calls, e.g. every call
	// triggered by an animated mesh's pose - re-uploading and rebuilding the
	// whole importance-sampling distribution anyway was the actual dominant
	// cost in this function, confirmed via real session timing
	// instrumentation). Hashed over the SOURCE cubemap/irradiance/prefilter
	// pixel data only (not jointPalette-style per-frame data, since none
	// exists here) - unchanged source data means the RtEnvironmentSampler
	// distribution DERIVED from it is unchanged too, safe to skip rebuilding
	// without re-deriving it to check.
	uint64_t envHash = fnv1aHash(&env.faceSize, sizeof(env.faceSize));
	for (int face = 0; face < 6; ++face)
		envHash = fnv1aHash(env.faces[face].data(), env.faces[face].size() * sizeof(float), envHash);
	envHash = fnv1aHash(&env.irradianceFaceSize, sizeof(env.irradianceFaceSize), envHash);
	for (int face = 0; face < 6; ++face)
		envHash = fnv1aHash(env.irradianceFaces[face].data(), env.irradianceFaces[face].size() * sizeof(float), envHash);
	auto hashPrefilterChain = [&envHash](const std::vector<RtEnvironment::PrefilterMip>& mips)
	{
		const size_t count = mips.size();
		envHash = fnv1aHash(&count, sizeof(count), envHash);
		for (const RtEnvironment::PrefilterMip& mip : mips)
		{
			envHash = fnv1aHash(&mip.faceSize, sizeof(mip.faceSize), envHash);
			for (int face = 0; face < 6; ++face)
				envHash = fnv1aHash(mip.faces[face].data(), mip.faces[face].size() * sizeof(float), envHash);
		}
	};
	hashPrefilterChain(env.prefilterMips);
	hashPrefilterChain(env.sheenPrefilterMips);

	if (!(_impl->environmentUploaded && envHash == _impl->lastEnvironmentHash))
	{
		// Replacing (or uploading for the first time) - free whatever was
		// there before starting the fresh upload below, mirroring the GAS
		// cache's "topology changed, free the old entry first" handling.
		_impl->freeEnvironmentBuffers();

		_impl->envFaceSize = env.faceSize;

		if (env.faceSize > 0)
		{
			for (int face = 0; face < 6; ++face)
			{
				if (!uploadCubemapFace(env.faces[face], env.faceSize, _impl->envFaceBuffers[face]))
				{
					qWarning() << "RtOptixSceneTracer::buildScene(): environment face" << face << "has unexpected size - disabling environment.";
					_impl->envFaceSize = 0;
					freeCubemapFaces(_impl->envFaceBuffers);
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
					freeCubemapFaces(_impl->irradianceFaceBuffers);
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

			bool finalized = false;
			if (mipsOk)
			{
				const size_t mipsBytes = hostMips.size() * sizeof(RtOptixPrefilterMip);
				const bool mallocOk = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&mipsBuffer), mipsBytes), "cudaMalloc(prefilter mips)");
				if (mallocOk && cudaCheck(cudaMemcpy(reinterpret_cast<void*>(mipsBuffer), hostMips.data(), mipsBytes, cudaMemcpyHostToDevice), "cudaMemcpy(prefilter mips)"))
				{
					mipCount = static_cast<int>(hostMips.size());
					finalized = true;
				}
				else if (mallocOk)
				{
					// malloc succeeded but the memcpy failed - same pattern as the
					// per-texture mip array above: free the live-but-unfinalized
					// allocation directly rather than leaving it in mipsBuffer
					// (still reachable via the persistent Impl member the caller
					// passed in, but pointlessly resident with mipCount left at 0).
					cudaFree(reinterpret_cast<void*>(mipsBuffer));
					mipsBuffer = 0;
				}
			}

			if (!finalized)
			{
				// Either a per-face upload failed partway through the chain
				// (mipsOk==false) or the chain finished but the final device-
				// array malloc/memcpy above failed - either way mipCount stays 0
				// (chain disabled, falls back to the raw map) and every face
				// buffer already uploaded for this chain is now unreferenced.
				// Free them immediately rather than leaving them resident until
				// the next buildScene()/destructor.
				for (Impl::PrefilterMipGpu& gpuMip : gpuMips)
					for (CUdeviceptr& face : gpuMip.faceBuffers)
					{
						if (face) cudaFree(reinterpret_cast<void*>(face));
						face = 0;
					}
				gpuMips.clear();
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

		_impl->lastEnvironmentHash = envHash;
		_impl->environmentUploaded = true;
	}


	// Every texture this snapshot actually references has now been resolved
	// (and its persistentTextureCache entry marked touched, whether reused or
	// freshly uploaded above) - safe to sweep anything left unmarked (a
	// texture from the previous build that this one no longer references at
	// all) now, before declaring the build itself successful.
	_impl->evictUnusedTextures();
	// Same reasoning, for persistentGasCache - every mesh's GAS entry was
	// already marked touched (hit or miss) by the GAS loop above.
	_impl->evictUnusedGas();
	// Same reasoning, for persistentSkinBaseCache - every skinned mesh's
	// bind-pose entry was already marked touched by updateSkinBase() above
	// (called from the GAS loop's refit branch for hasSkinningData meshes).
	_impl->evictUnusedSkinBase();

	// Same reasoning again, for persistentMorphBaseCache - every morph-only
	// mesh's rest-pose entry was already marked touched by updateMorphBase()
	// above (called from the GAS loop's refit branch for hasMorphData
	// meshes with hasSkinningData == false).
	_impl->evictUnusedMorphBase();

	succeeded = true;
	return true;
}

// Shared by renderScene() (settled/CPU-readback path) and
// renderSceneToDevice() (interactive/GPU-resident path, see
// RtOptixPathTracingSession's persistent device buffer) - builds the launch
// params from the tracer's built scene + the caller's camera/environment/
// quality settings, uploads them, and runs one optixLaunch(). Callers own
// dImageRGBA/dAlbedo/dNormal (already sized width*height, of the types the
// RtOptixSceneParams fields expect - float4/float3/float3 respectively) and
// are responsible for freeing them; this function only reads/writes through
// the pointers it's given. Declared taking void* in the header (which must
// also compile in the no-CUDA/OptiX stub build), cast to the real CUDA
// vector types here.
void RtOptixSceneTracer::buildRenderParamsInto(void* paramsOut, const RtCamera& camera, const RtEnvironment& environment,
	int width, int height, unsigned int samplesPerPixel, unsigned int sampleOffset,
	unsigned int maxBounces, bool shadowsEnabled, bool selfShadowsEnabled, bool enableEnvironmentImportanceSampling,
	unsigned int maxTransmissionBounces, float fireflyClampThreshold, unsigned int russianRouletteStartDepth,
	unsigned int maxVolumeScatterBounces, unsigned int previousSampleCount,
	void* dImageRGBA, void* dAlbedo_, void* dNormal_) const
{
	Impl* impl = _impl.get();
	float3* dAlbedo = static_cast<float3*>(dAlbedo_);
	float3* dNormal = static_cast<float3*>(dNormal_);

	RtOptixSceneParams& params = *reinterpret_cast<RtOptixSceneParams*>(paramsOut);
	params = RtOptixSceneParams{};
	params.image = static_cast<float4*>(dImageRGBA);
	params.albedoImage = dAlbedo;
	params.normalImage = dNormal;
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
	params.lights = reinterpret_cast<const RtOptixLight*>(impl->lightsBuffer);
	params.lightCount = impl->lightCount;
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
		params.environment.faces[face] = reinterpret_cast<float3*>(impl->envFaceBuffers[face]);
	params.environment.faceSize = impl->envFaceSize;
	for (int face = 0; face < 6; ++face)
		params.environment.irradianceFaces[face] = reinterpret_cast<float3*>(impl->irradianceFaceBuffers[face]);
	params.environment.irradianceFaceSize = impl->irradianceFaceSize;
	params.environment.showBackground = environment.showBackground ? 1 : 0;
	params.environment.fallbackTopColor = make_float3(environment.fallbackTopColor.x, environment.fallbackTopColor.y, environment.fallbackTopColor.z);
	params.environment.fallbackBottomColor = make_float3(environment.fallbackBottomColor.x, environment.fallbackBottomColor.y, environment.fallbackBottomColor.z);
	params.environment.fallbackGradientStyle = environment.fallbackGradientStyle;
	params.environment.cameraUpAxisZUp = environment.cameraUpAxisZUp ? 1 : 0;
	params.environment.skyBoxZRotationDegrees = environment.skyBoxZRotationDegrees;
	params.environment.envMapExposure = environment.envMapExposure;
	params.environment.skyBoxFOV = environment.skyBoxFOV;
	params.environment.prefilterMips = reinterpret_cast<const RtOptixPrefilterMip*>(impl->prefilterMipsBuffer);
	params.environment.prefilterMipCount = impl->prefilterMipCount;
	params.environment.sheenPrefilterMips = reinterpret_cast<const RtOptixPrefilterMip*>(impl->sheenPrefilterMipsBuffer);
	params.environment.sheenPrefilterMipCount = impl->sheenPrefilterMipCount;
	params.environment.envFlatCdf = reinterpret_cast<const float*>(impl->envFlatCdfBuffer);
	params.environment.envTexelPdf = reinterpret_cast<const float*>(impl->envTexelPdfBuffer);
	params.environment.envTotalWeight = impl->envTotalWeight;
	params.infinitePlaneEnabled = impl->infinitePlaneEnabled ? 1 : 0;
	params.infinitePlaneCameraUpAxisZUp = impl->infinitePlaneCameraUpAxisZUp ? 1 : 0;
	params.infinitePlaneHeight = impl->infinitePlaneHeight;
	params.infinitePlaneIsShadowCatcher = impl->infinitePlaneIsShadowCatcher ? 1 : 0;
	params.infinitePlaneShadowCatcherDarkness = impl->infinitePlaneShadowCatcherDarkness;
	params.infinitePlaneBaseColor = make_float3(
		impl->infinitePlaneBaseColor.r,
		impl->infinitePlaneBaseColor.g,
		impl->infinitePlaneBaseColor.b);
	params.infinitePlaneMetalness = impl->infinitePlaneMetalness;
	params.infinitePlaneRoughness = impl->infinitePlaneRoughness;
	params.sheenAlbedoLUT = reinterpret_cast<const float*>(impl->sheenAlbedoLutBuffer);
	params.sheenCharlieLUT = reinterpret_cast<const float*>(impl->sheenCharlieLutBuffer);
	params.sheenAlbedoLUTSize = impl->sheenAlbedoLutBuffer ? RtOptixSceneTracer::Impl::kSheenAlbedoLutSize : 0;
	params.samplesPerPixel = samplesPerPixel > 0 ? samplesPerPixel : 1;
	params.sampleOffset = sampleOffset;
	params.previousSampleCount = previousSampleCount;
	params.maxBounces = maxBounces > 0 ? maxBounces : 1;
	params.maxTransmissionBounces = maxTransmissionBounces > 0 ? maxTransmissionBounces : 1;
	params.fireflyClampThreshold = fireflyClampThreshold > 0.0f ? fireflyClampThreshold : 0.01f;
	params.russianRouletteStartDepth = russianRouletteStartDepth > 0 ? russianRouletteStartDepth : 1;
	params.maxVolumeScatterBounces = maxVolumeScatterBounces > 0 ? maxVolumeScatterBounces : 1;

	params.handle = impl->iasHandle;
}

bool RtOptixSceneTracer::launchOptixSceneRender(const RtCamera& camera, const RtEnvironment& environment,
	int width, int height, unsigned int samplesPerPixel, unsigned int sampleOffset,
	unsigned int maxBounces, bool shadowsEnabled, bool selfShadowsEnabled, bool enableEnvironmentImportanceSampling,
	unsigned int maxTransmissionBounces, float fireflyClampThreshold, unsigned int russianRouletteStartDepth,
	unsigned int maxVolumeScatterBounces,
	void* dImageRGBA, void* dAlbedo_, void* dNormal_)
{
	Impl* impl = _impl.get();

	RtOptixSceneParams params{};
	// 0 = fresh start, no accumulation to blend against - renderScene()/
	// renderSceneToDevice() (the settled/offline synchronous path) never do
	// persistent GPU-resident accumulation across launches, unlike
	// submitSceneRenderToDevice()'s callers.
	buildRenderParamsInto(&params, camera, environment, width, height, samplesPerPixel, sampleOffset,
		maxBounces, shadowsEnabled, selfShadowsEnabled, enableEnvironmentImportanceSampling,
		maxTransmissionBounces, fireflyClampThreshold, russianRouletteStartDepth, maxVolumeScatterBounces, 0,
		dImageRGBA, dAlbedo_, dNormal_);

	CUdeviceptr dParams = 0;
	bool ok = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&dParams), sizeof(RtOptixSceneParams)), "cudaMalloc(params)");
	if (ok)
		ok = cudaCheck(cudaMemcpy(reinterpret_cast<void*>(dParams), &params, sizeof(RtOptixSceneParams), cudaMemcpyHostToDevice), "cudaMemcpy(params)");

	if (ok)
		ok = optixCheck(optixLaunch(impl->pipeline, nullptr, dParams, sizeof(RtOptixSceneParams), &impl->sbt,
			static_cast<unsigned int>(width), static_cast<unsigned int>(height), 1), "optixLaunch()");
	if (ok)
		ok = cudaCheck(cudaDeviceSynchronize(), "cudaDeviceSynchronize()");

	if (dParams) cudaFree(reinterpret_cast<void*>(dParams));
	return ok;
}

bool RtOptixSceneTracer::renderScene(const RtCamera& camera, const RtEnvironment& environment,
	int width, int height, unsigned int samplesPerPixel, unsigned int sampleOffset,
	unsigned int maxBounces, bool shadowsEnabled, bool selfShadowsEnabled, bool enableEnvironmentImportanceSampling,
	unsigned int maxTransmissionBounces, float fireflyClampThreshold, unsigned int russianRouletteStartDepth,
	unsigned int maxVolumeScatterBounces,
	std::vector<glm::vec3>& outImageLinearRgb, std::vector<glm::vec3>& outAlbedo, std::vector<glm::vec3>& outNormal,
	std::vector<float>& outAlpha)
{
	static_assert(sizeof(glm::vec3) == sizeof(float) * 3, "RtOptixSceneTracer assumes glm::vec3 is three tightly packed floats.");

	if (!_impl->valid || _impl->iasHandle == 0 || width <= 0 || height <= 0)
		return false;

	const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
	CUdeviceptr dImage = 0, dAlbedo = 0, dNormal = 0;
	bool allocOk = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&dImage), pixelCount * sizeof(float4)), "cudaMalloc(output image)");
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

	bool ok = launchOptixSceneRender(camera, environment, width, height, samplesPerPixel, sampleOffset,
		maxBounces, shadowsEnabled, selfShadowsEnabled, enableEnvironmentImportanceSampling,
		maxTransmissionBounces, fireflyClampThreshold, russianRouletteStartDepth, maxVolumeScatterBounces,
		reinterpret_cast<void*>(dImage), reinterpret_cast<void*>(dAlbedo), reinterpret_cast<void*>(dNormal));

	if (ok)
	{
		// float3 and glm::vec3 are both three tight-packed floats (no
		// padding/alignment mismatch - same layout assumption already made
		// throughout this file, e.g. camPosition/camForward above), so the
		// device buffers can be read straight into the glm::vec3 vectors.
		// The combined RGBA image buffer (see RtOptixSceneParams::image's doc
		// comment) is read back into a temporary float4 vector and unpacked
		// into the existing separate outImageLinearRgb/outAlpha vectors so
		// this function's public contract is unchanged for callers.
		std::vector<float4> rgbaImage(pixelCount);
		outAlbedo.resize(pixelCount);
		outNormal.resize(pixelCount);
		ok = cudaCheck(cudaMemcpy(rgbaImage.data(), reinterpret_cast<void*>(dImage), pixelCount * sizeof(float4), cudaMemcpyDeviceToHost), "cudaMemcpy(readback image)");
		if (ok)
			ok = cudaCheck(cudaMemcpy(outAlbedo.data(), reinterpret_cast<void*>(dAlbedo), pixelCount * sizeof(float3), cudaMemcpyDeviceToHost), "cudaMemcpy(readback albedo)");
		if (ok)
			ok = cudaCheck(cudaMemcpy(outNormal.data(), reinterpret_cast<void*>(dNormal), pixelCount * sizeof(float3), cudaMemcpyDeviceToHost), "cudaMemcpy(readback normal)");
		if (ok)
		{
			outImageLinearRgb.resize(pixelCount);
			outAlpha.resize(pixelCount);
			for (size_t i = 0; i < pixelCount; ++i)
			{
				const float4& px = rgbaImage[i];
				outImageLinearRgb[i] = glm::vec3(px.x, px.y, px.z);
				outAlpha[i] = px.w;
			}
		}
	}

	cudaFree(reinterpret_cast<void*>(dImage));
	cudaFree(reinterpret_cast<void*>(dAlbedo));
	cudaFree(reinterpret_cast<void*>(dNormal));

	if (!ok)
	{
		outImageLinearRgb.clear();
		outAlbedo.clear();
		outNormal.clear();
		outAlpha.clear();
	}

	return ok;
}

bool RtOptixSceneTracer::renderSceneToDevice(const RtCamera& camera, const RtEnvironment& environment,
	int width, int height, unsigned int samplesPerPixel, unsigned int sampleOffset,
	unsigned int maxBounces, bool shadowsEnabled, bool selfShadowsEnabled, bool enableEnvironmentImportanceSampling,
	unsigned int maxTransmissionBounces, float fireflyClampThreshold, unsigned int russianRouletteStartDepth,
	unsigned int maxVolumeScatterBounces,
	void* deviceImageRGBA)
{
	if (!_impl->valid || _impl->iasHandle == 0 || width <= 0 || height <= 0 || !deviceImageRGBA)
		return false;

	// Interactive/GPU-resident path (see RtOptixPathTracingSession's
	// persistent ping-pong device buffers): deviceImageRGBA is a
	// caller-owned, already-allocated device pointer sized
	// width*height*sizeof(float4) - this call writes straight into it, with
	// no per-chunk cudaMalloc/cudaMemcpy(DeviceToHost)/cudaFree for the beauty
	// image at all, eliminating the readback that dominates renderScene()'s
	// per-chunk latency. Albedo/normal guide buffers are still computed (the kernel writes them
	// unconditionally) but into scratch device memory that's freed
	// immediately after the launch and never read back - they're denoiser
	// guide buffers, unused on the interactive path (denoiser is off there),
	// so paying their launch-time cost but skipping their readback is a
	// deliberate, low-risk simplification, not a correctness compromise.

	const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
	CUdeviceptr dAlbedo = 0, dNormal = 0;
	bool allocOk = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&dAlbedo), pixelCount * sizeof(float3)), "cudaMalloc(albedo guide image)");
	if (allocOk)
		allocOk = cudaCheck(cudaMalloc(reinterpret_cast<void**>(&dNormal), pixelCount * sizeof(float3)), "cudaMalloc(normal guide image)");
	if (!allocOk)
	{
		if (dNormal) cudaFree(reinterpret_cast<void*>(dNormal));
		if (dAlbedo) cudaFree(reinterpret_cast<void*>(dAlbedo));
		return false;
	}

	const bool ok = launchOptixSceneRender(camera, environment, width, height, samplesPerPixel, sampleOffset,
		maxBounces, shadowsEnabled, selfShadowsEnabled, enableEnvironmentImportanceSampling,
		maxTransmissionBounces, fireflyClampThreshold, russianRouletteStartDepth, maxVolumeScatterBounces,
		deviceImageRGBA, reinterpret_cast<void*>(dAlbedo), reinterpret_cast<void*>(dNormal));

	cudaFree(reinterpret_cast<void*>(dAlbedo));
	cudaFree(reinterpret_cast<void*>(dNormal));

	return ok;
}

void* RtOptixSceneTracer::allocateDeviceRGBABuffer(int width, int height) const
{
	if (width <= 0 || height <= 0)
		return nullptr;
	const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * sizeof(float4);
	void* devicePtr = nullptr;
	if (!cudaCheck(cudaMalloc(&devicePtr, bytes), "cudaMalloc(interactive device RGBA buffer)"))
		return nullptr;
	return devicePtr;
}

void RtOptixSceneTracer::freeDeviceRGBABuffer(void* devicePtr) const
{
	if (devicePtr)
		cudaFree(devicePtr);
}

bool RtOptixSceneTracer::readbackDeviceRGBABuffer(void* devicePtr, int width, int height,
	std::vector<glm::vec3>& outImageLinearRgb, std::vector<float>& outAlpha) const
{
	if (!devicePtr || width <= 0 || height <= 0)
		return false;

	const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
	std::vector<float4> rgbaImage(pixelCount);
	if (!cudaCheck(cudaMemcpy(rgbaImage.data(), devicePtr, pixelCount * sizeof(float4), cudaMemcpyDeviceToHost),
			"cudaMemcpy(fallback readback of interactive device RGBA buffer)"))
		return false;

	outImageLinearRgb.resize(pixelCount);
	outAlpha.resize(pixelCount);
	for (size_t i = 0; i < pixelCount; ++i)
	{
		const float4& px = rgbaImage[i];
		outImageLinearRgb[i] = glm::vec3(px.x, px.y, px.z);
		outAlpha[i] = px.w;
	}
	return true;
}

void* RtOptixSceneTracer::createStream() const
{
	cudaStream_t stream = nullptr;
	if (!cudaCheck(cudaStreamCreate(&stream), "cudaStreamCreate(interactive PT stream)"))
		return nullptr;
	return stream;
}

void RtOptixSceneTracer::destroyStream(void* stream) const
{
	if (stream)
		cudaStreamDestroy(static_cast<cudaStream_t>(stream));
}

void* RtOptixSceneTracer::createEvent() const
{
	// cudaEventDisableTiming would be cheaper, but RtInteractiveRenderer's
	// adaptive-budget design (Phase D) wants each event's measured GPU time
	// via cudaEventElapsedTime() - see setInteractiveBudget()'s doc comment
	// in RtInteractiveRenderer.h - so timing stays enabled here.
	cudaEvent_t event = nullptr;
	if (!cudaCheck(cudaEventCreate(&event), "cudaEventCreate(interactive PT completion event)"))
		return nullptr;
	return event;
}

void RtOptixSceneTracer::destroyEvent(void* event) const
{
	if (event)
		cudaEventDestroy(static_cast<cudaEvent_t>(event));
}

bool RtOptixSceneTracer::isEventComplete(void* event) const
{
	if (!event)
		return false;
	// cudaEventQuery() never blocks - returns cudaSuccess (event fired),
	// cudaErrorNotReady (still in flight), or a real error. Deliberately NOT
	// cudaEventSynchronize()/cudaStreamSynchronize() - either of those would
	// block the calling thread until completion, exactly what this API
	// exists to let callers avoid (see this method's header doc comment).
	const cudaError_t status = cudaEventQuery(static_cast<cudaEvent_t>(event));
	return status == cudaSuccess;
}

float RtOptixSceneTracer::elapsedEventTimeMs(void* startEvent, void* endEvent) const
{
	if (!startEvent || !endEvent)
		return -1.0f;
	float ms = 0.0f;
	// cudaEventElapsedTime() itself requires both events to have already
	// completed (it returns cudaErrorNotReady otherwise) - callers are
	// expected to have already confirmed that via isEventComplete(), same
	// precondition as the rest of this non-blocking API.
	if (!cudaCheck(cudaEventElapsedTime(&ms, static_cast<cudaEvent_t>(startEvent), static_cast<cudaEvent_t>(endEvent)),
			"cudaEventElapsedTime(interactive PT launch timing)"))
		return -1.0f;
	return ms;
}

void* RtOptixSceneTracer::allocateParamsScratchBuffer() const
{
	void* devicePtr = nullptr;
	if (!cudaCheck(cudaMalloc(&devicePtr, sizeof(RtOptixSceneParams)), "cudaMalloc(interactive params scratch buffer)"))
		return nullptr;
	return devicePtr;
}

void* RtOptixSceneTracer::allocateGuideScratchBuffer(int width, int height) const
{
	if (width <= 0 || height <= 0)
		return nullptr;
	const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * sizeof(float3);
	void* devicePtr = nullptr;
	if (!cudaCheck(cudaMalloc(&devicePtr, bytes), "cudaMalloc(interactive guide scratch buffer)"))
		return nullptr;
	return devicePtr;
}

void RtOptixSceneTracer::freeScratchBuffer(void* devicePtr) const
{
	if (devicePtr)
		cudaFree(devicePtr);
}

bool RtOptixSceneTracer::copyDeviceRGBABufferAsync(void* dst, const void* src, int width, int height, void* stream) const
{
	if (!dst || !src || width <= 0 || height <= 0)
		return false;
	const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * sizeof(float4);
	return cudaCheck(cudaMemcpyAsync(dst, src, bytes, cudaMemcpyDeviceToDevice, static_cast<cudaStream_t>(stream)),
		"cudaMemcpyAsync(interactive accumulation copy-forward, RGBA)");
}

bool RtOptixSceneTracer::copyGuideScratchBufferAsync(void* dst, const void* src, int width, int height, void* stream) const
{
	if (!dst || !src || width <= 0 || height <= 0)
		return false;
	const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * sizeof(float3);
	return cudaCheck(cudaMemcpyAsync(dst, src, bytes, cudaMemcpyDeviceToDevice, static_cast<cudaStream_t>(stream)),
		"cudaMemcpyAsync(interactive accumulation copy-forward, guide)");
}

void* RtOptixSceneTracer::allocateParamsHostStagingBuffer() const
{
	// cudaHostAlloc (default flags) gives page-locked host memory -
	// required for cudaMemcpyAsync() to actually behave asynchronously
	// (stream-ordered, no implicit blocking/legacy-default-stream fallback)
	// - see submitSceneRenderToDevice()'s doc comment for why an ordinary
	// pageable buffer (stack-local or new'd) doesn't satisfy that.
	void* hostPtr = nullptr;
	if (!cudaCheck(cudaHostAlloc(&hostPtr, sizeof(RtOptixSceneParams), cudaHostAllocDefault), "cudaHostAlloc(params host staging)"))
		return nullptr;
	return hostPtr;
}

void RtOptixSceneTracer::freeParamsHostStagingBuffer(void* hostPtr) const
{
	if (hostPtr)
		cudaFreeHost(hostPtr);
}

bool RtOptixSceneTracer::submitSceneRenderToDevice(const RtCamera& camera, const RtEnvironment& environment,
	int width, int height, unsigned int samplesPerPixel, unsigned int sampleOffset,
	unsigned int maxBounces, bool shadowsEnabled, bool selfShadowsEnabled, bool enableEnvironmentImportanceSampling,
	unsigned int maxTransmissionBounces, float fireflyClampThreshold, unsigned int russianRouletteStartDepth,
	unsigned int maxVolumeScatterBounces, unsigned int previousSampleCount,
	void* deviceImageRGBA, void* hostParamsStaging, void* dParamsScratch, void* dAlbedoScratch, void* dNormalScratch,
	void* stream, void* startEvent, void* completionEvent)
{
	if (!_impl->valid || _impl->iasHandle == 0 || width <= 0 || height <= 0 ||
		!deviceImageRGBA || !hostParamsStaging || !dParamsScratch || !dAlbedoScratch || !dNormalScratch || !completionEvent)
		return false;

	// The launch params struct is built directly into hostParamsStaging (a
	// caller-owned PINNED host buffer, NOT a stack-local - see
	// allocateParamsHostStagingBuffer()'s doc comment) and then uploaded via
	// cudaMemcpyAsync() on `stream`, stream-ordered like the launch itself -
	// deliberately NOT a plain synchronous cudaMemcpy(): that call uses
	// legacy default-stream semantics (implicitly synchronizing against
	// every other stream in the context) even with no explicit
	// cudaDeviceSynchronize() following it, which would silently serialize
	// this "non-blocking" submission against unrelated GPU work - exactly
	// the stall this whole API exists to avoid, just moved one call earlier.
	buildRenderParamsInto(hostParamsStaging, camera, environment, width, height, samplesPerPixel, sampleOffset,
		maxBounces, shadowsEnabled, selfShadowsEnabled, enableEnvironmentImportanceSampling,
		maxTransmissionBounces, fireflyClampThreshold, russianRouletteStartDepth, maxVolumeScatterBounces, previousSampleCount,
		deviceImageRGBA, dAlbedoScratch, dNormalScratch);

	cudaStream_t cudaStream = static_cast<cudaStream_t>(stream);
	if (!cudaCheck(cudaMemcpyAsync(dParamsScratch, hostParamsStaging, sizeof(RtOptixSceneParams), cudaMemcpyHostToDevice, cudaStream),
			"cudaMemcpyAsync(params)"))
		return false;

	// Recorded immediately before the launch (not before the params upload
	// above) so elapsedEventTimeMs(startEvent, completionEvent) measures the
	// launch itself, not the (already-cheap, ~0.05-0.09ms) params copy.
	if (startEvent && !cudaCheck(cudaEventRecord(static_cast<cudaEvent_t>(startEvent), cudaStream), "cudaEventRecord(async start)"))
		return false;

	if (!optixCheck(optixLaunch(_impl->pipeline, cudaStream, reinterpret_cast<CUdeviceptr>(dParamsScratch),
			sizeof(RtOptixSceneParams), &_impl->sbt,
			static_cast<unsigned int>(width), static_cast<unsigned int>(height), 1), "optixLaunch() (async)"))
		return false;

	// Records completionEvent on the SAME stream right after the launch -
	// isEventComplete(completionEvent) becomes true only once every prior
	// operation enqueued on this stream (i.e. this launch) has actually
	// finished on the device. No cudaDeviceSynchronize()/cudaStreamSynchronize()
	// call anywhere in this function - this call returns as soon as the
	// launch and event record are ENQUEUED, not when they complete.
	if (!cudaCheck(cudaEventRecord(static_cast<cudaEvent_t>(completionEvent), cudaStream), "cudaEventRecord(async completion)"))
		return false;

	return true;
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

bool RtOptixSceneTracer::hasHardwareRT() const
{
	return false;
}

const char* RtOptixSceneTracer::deviceName() const
{
	return "";
}

double RtOptixSceneTracer::lastGasBuildMs() const
{
	return 0.0;
}

double RtOptixSceneTracer::lastIasBuildMs() const
{
	return 0.0;
}

uint64_t RtOptixSceneTracer::lastTriangleCount() const
{
	return 0;
}

bool RtOptixSceneTracer::buildScene(const RtSceneSnapshot&)
{
	return false;
}

bool RtOptixSceneTracer::renderScene(const RtCamera&, const RtEnvironment&, int, int, unsigned int, unsigned int, unsigned int, bool, bool, bool, unsigned int, float, unsigned int, unsigned int, std::vector<glm::vec3>&, std::vector<glm::vec3>&, std::vector<glm::vec3>&, std::vector<float>&)
{
	return false;
}

bool RtOptixSceneTracer::renderSceneToDevice(const RtCamera&, const RtEnvironment&, int, int, unsigned int, unsigned int, unsigned int, bool, bool, bool, unsigned int, float, unsigned int, unsigned int, void*)
{
	return false;
}

bool RtOptixSceneTracer::launchOptixSceneRender(const RtCamera&, const RtEnvironment&, int, int, unsigned int, unsigned int, unsigned int, bool, bool, bool, unsigned int, float, unsigned int, unsigned int, void*, void*, void*)
{
	return false;
}

void* RtOptixSceneTracer::allocateDeviceRGBABuffer(int, int) const
{
	return nullptr;
}

void RtOptixSceneTracer::freeDeviceRGBABuffer(void*) const
{
}

bool RtOptixSceneTracer::readbackDeviceRGBABuffer(void*, int, int, std::vector<glm::vec3>&, std::vector<float>&) const
{
	return false;
}

void RtOptixSceneTracer::buildRenderParamsInto(void*, const RtCamera&, const RtEnvironment&, int, int, unsigned int, unsigned int,
	unsigned int, bool, bool, bool, unsigned int, float, unsigned int, unsigned int, unsigned int, void*, void*, void*) const
{
}

void* RtOptixSceneTracer::createStream() const
{
	return nullptr;
}

void RtOptixSceneTracer::destroyStream(void*) const
{
}

void* RtOptixSceneTracer::createEvent() const
{
	return nullptr;
}

void RtOptixSceneTracer::destroyEvent(void*) const
{
}

bool RtOptixSceneTracer::isEventComplete(void*) const
{
	return false;
}

float RtOptixSceneTracer::elapsedEventTimeMs(void*, void*) const
{
	return -1.0f;
}

void* RtOptixSceneTracer::allocateParamsScratchBuffer() const
{
	return nullptr;
}

void* RtOptixSceneTracer::allocateGuideScratchBuffer(int, int) const
{
	return nullptr;
}

void RtOptixSceneTracer::freeScratchBuffer(void*) const
{
}

bool RtOptixSceneTracer::copyDeviceRGBABufferAsync(void*, const void*, int, int, void*) const
{
	return false;
}

bool RtOptixSceneTracer::copyGuideScratchBufferAsync(void*, const void*, int, int, void*) const
{
	return false;
}

void* RtOptixSceneTracer::allocateParamsHostStagingBuffer() const
{
	return nullptr;
}

void RtOptixSceneTracer::freeParamsHostStagingBuffer(void*) const
{
}

bool RtOptixSceneTracer::submitSceneRenderToDevice(const RtCamera&, const RtEnvironment&, int, int, unsigned int, unsigned int,
	unsigned int, bool, bool, bool, unsigned int, float, unsigned int, unsigned int, unsigned int,
	void*, void*, void*, void*, void*, void*, void*, void*)
{
	return false;
}

#endif // MODELVIEWER_HAVE_OPTIX
