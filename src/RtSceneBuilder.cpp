#include "RtSceneBuilder.h"

#include "SceneRuntime.h"
#include "SceneMesh.h"
#include "RenderableMesh.h"
#include "Material.h"
#include "Camera.h"
#include "PunctualLights.h"

#include <glm/gtc/type_ptr.hpp>

#include <QImage>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
	// FNV-1a over raw bytes - not cross-platform/cross-run stable (nor does it
	// need to be: RtOptixSceneTracer::Impl::persistentGasCache that consumes
	// this only ever compares hashes computed within the SAME running
	// process), just fast and deterministic call to call so a mesh's content
	// hash changes if and only if its vertex/index data actually does - see
	// RtMeshGeometry::contentHash's doc comment (RtSceneSnapshot.h) for why
	// this, not sourceObjectId alone, is what detects a skinned/morphed
	// mesh's pose actually changing between two builds.
	inline uint64_t fnv1aHash(const void* data, size_t size, uint64_t seed = 1469598103934665603ULL)
	{
		const unsigned char* bytes = static_cast<const unsigned char*>(data);
		uint64_t hash = seed;
		for (size_t i = 0; i < size; ++i)
		{
			hash ^= bytes[i];
			hash *= 1099511628211ULL;
		}
		return hash;
	}

	inline glm::vec3 toGlm(const QVector3D& v) { return glm::vec3(v.x(), v.y(), v.z()); }

	inline glm::mat4 toGlm(const QMatrix4x4& m)
	{
		// QMatrix4x4::constData() is column-major (OpenGL convention), same
		// layout glm::mat4 expects - a direct element copy is correct.
		return glm::make_mat4(m.constData());
	}

	// Builds RtTextureSample::mips - see that field's own doc comment for
	// why this exists (path-traced texture minification aliasing, no
	// hardware mipmapping equivalent). Box-filter downsample (plain 2x2
	// average of the PREVIOUS level's sRGB-encoded bytes, not a linear-space
	// average) - a standard, if not perfectly radiometrically precise,
	// simplification real-time mipmap generators commonly make too; the
	// existing sampleTexture()/sRGBToLinear() call sites already decode
	// AFTER sampling, same as here. Halves each dimension every level (never
	// below 1), down to a 1x1 level - mips[0] duplicates the already-
	// populated base level so every level is uniformly indexable.
	void buildMipChain(RtTextureSample& sample)
	{
		sample.mips.clear();
		if (sample.width <= 0 || sample.height <= 0 || sample.rgba8.empty())
			return;

		RtTextureMipLevel base;
		base.width  = sample.width;
		base.height = sample.height;
		base.rgba8  = sample.rgba8;
		sample.mips.push_back(std::move(base));

		while (sample.mips.back().width > 1 || sample.mips.back().height > 1)
		{
			const RtTextureMipLevel& prev = sample.mips.back();
			const int nextW = (std::max)(1, prev.width  / 2);
			const int nextH = (std::max)(1, prev.height / 2);

			RtTextureMipLevel next;
			next.width  = nextW;
			next.height = nextH;
			next.rgba8.resize(static_cast<size_t>(nextW) * nextH * 4);

			for (int y = 0; y < nextH; ++y)
			{
				const int srcY0 = (std::min)(prev.height - 1, y * 2);
				const int srcY1 = (std::min)(prev.height - 1, y * 2 + 1);
				for (int x = 0; x < nextW; ++x)
				{
					const int srcX0 = (std::min)(prev.width - 1, x * 2);
					const int srcX1 = (std::min)(prev.width - 1, x * 2 + 1);

					unsigned int sum[4] = { 0, 0, 0, 0 };
					const int srcXs[2] = { srcX0, srcX1 };
					const int srcYs[2] = { srcY0, srcY1 };
					for (int sy : srcYs)
					{
						for (int sx : srcXs)
						{
							const size_t srcIdx = (static_cast<size_t>(sy) * prev.width + sx) * 4;
							for (int c = 0; c < 4; ++c)
								sum[c] += prev.rgba8[srcIdx + c];
						}
					}

					const size_t dstIdx = (static_cast<size_t>(y) * nextW + x) * 4;
					for (int c = 0; c < 4; ++c)
						next.rgba8[dstIdx + c] = static_cast<uint8_t>((sum[c] + 2) / 4);
				}
			}

			sample.mips.push_back(std::move(next));
		}
	}
}

RtMeshGeometry RtSceneBuilder::convertGeometry(const SceneMesh* mesh)
{
	RtMeshGeometry geom;

	const std::vector<Vertex> verts = mesh->vertices();
	// Vertex::Color is unspecified/garbage data when the mesh has no COLOR_0
	// attribute (main_scene.frag gates on the same "hasVertexColors" uniform
	// rather than assuming absent color reads as white) - only copy it
	// through when the mesh actually has one; otherwise RtVertex's (1,1,1,1)
	// default is the correct identity value.
	const bool hasVertexColors = mesh->hasVertexColors();

	// KHR_mesh_skinning (bone/joint skinning) - mesh->vertices() only ever
	// carries the BIND-POSE position/normal/tangent/bitangent (morph targets
	// aside - SceneMesh::applyMorphWeights() bakes those into _vertices
	// in-place, so those already came through above; skinning does not).
	// Raster's actual posed geometry only exists as a per-frame joint-matrix
	// UNIFORM array applied by main_scene.vert's computeSkinMatrix() at draw
	// time - it never gets written back into any CPU-side buffer this
	// tracer can read. Without this, any skinned/rigged model path-traced
	// in its rest/bind pose regardless of the raster viewport's current
	// animation frame. Applied here (mirroring computeSkinMatrix()/main()
	// exactly: same per-vertex 4-bone blend, same no-renormalization-of-
	// weights assumption per glTF's WEIGHTS_0-sums-to-1 convention, same
	// mat3(skin) - no inverse-transpose - for normals/tangents/bitangents,
	// since a skeletal rig's joints are rotation+translation only, never
	// non-uniform scale) so both engines pick up whatever pose the mesh is
	// CURRENTLY sitting in (playing, paused, or scrubbed) the moment this
	// snapshot is built - not continuously re-synced during playback, same
	// as everything else this one-shot "flatten and freeze" builder captures.
	const QVector<QMatrix4x4>& jointPaletteQt = mesh->jointPalette();
	const bool applySkinning = mesh->hasSkinning() && !jointPaletteQt.isEmpty();
	std::vector<glm::mat4> jointPalette;
	if (applySkinning)
	{
		jointPalette.reserve(jointPaletteQt.size());
		for (const QMatrix4x4& m : jointPaletteQt)
			jointPalette.push_back(toGlm(m));
	}

	// GPU-skinning enrichment (see RtMeshGeometry::hasSkinningData's doc
	// comment, RtSceneSnapshot.h) - captured alongside the existing CPU bake
	// below, never instead of it. Only meaningful when the mesh actually has
	// skin data at all; left empty otherwise (zero extra cost for the vast
	// majority of static meshes).
	geom.hasSkinningData = mesh->hasSkinning();
	if (geom.hasSkinningData)
	{
		geom.baseSkinPositions.reserve(verts.size());
		geom.baseSkinNormals.reserve(verts.size());
		geom.baseSkinTangents.reserve(verts.size());
		geom.tangentHandedness.reserve(verts.size());
		geom.jointIndices.reserve(verts.size());
		geom.jointWeights.reserve(verts.size());
		geom.jointPalette = jointPalette;
	}

	// GPU-morph enrichment (see RtMeshGeometry::hasMorphData's doc comment,
	// RtSceneSnapshot.h) - captured alongside the existing CPU bake below,
	// never instead of it. Sourced from SceneMesh::baseVertices() (the TRUE
	// unmorphed rest pose), NOT `verts` above (which is already morphed).
	geom.hasMorphData = mesh->hasMorphTargets();
	if (geom.hasMorphData)
	{
		const std::vector<Vertex> baseVerts = mesh->baseVertices();
		const QVector<MorphTargetData>& morphTargets = mesh->getMorphTargets();
		geom.morphTargetCount = static_cast<uint32_t>(morphTargets.size());

		geom.baseMorphPositions.reserve(baseVerts.size());
		geom.baseMorphNormals.reserve(baseVerts.size());
		geom.baseMorphTangents.reserve(baseVerts.size());
		for (const Vertex& bv : baseVerts)
		{
			geom.baseMorphPositions.push_back(bv.Position);
			geom.baseMorphNormals.push_back(bv.Normal);
			geom.baseMorphTangents.push_back(bv.Tangent);
		}

		// Flattened target-major (target * vertexCount + i) - see
		// RtMeshGeometry's doc comment. A target's delta array is either
		// fully present (exact size match) or entirely absent - never
		// partial, mirroring SceneMesh::applyMorphWeights()'s own
		// per-attribute size checks exactly - tracked via the has*Deltas
		// flags below (NOT inferred from zero-padding, which would be
		// indistinguishable from a legitimately-authored zero delta).
		const size_t vertexCount = baseVerts.size();
		geom.morphTargetDeltaPositions.reserve(static_cast<size_t>(geom.morphTargetCount) * vertexCount);
		geom.morphTargetDeltaNormals.reserve(static_cast<size_t>(geom.morphTargetCount) * vertexCount);
		geom.morphTargetDeltaTangents.reserve(static_cast<size_t>(geom.morphTargetCount) * vertexCount);
		geom.morphTargetHasPositionDeltas.reserve(morphTargets.size());
		geom.morphTargetHasNormalDeltas.reserve(morphTargets.size());
		geom.morphTargetHasTangentDeltas.reserve(morphTargets.size());
		for (const MorphTargetData& target : morphTargets)
		{
			const bool hasPos = target.positionDeltas.size() == vertexCount;
			const bool hasNorm = target.normalDeltas.size() == vertexCount;
			const bool hasTan = target.tangentDeltas.size() == vertexCount;
			geom.morphTargetHasPositionDeltas.push_back(hasPos ? 1 : 0);
			geom.morphTargetHasNormalDeltas.push_back(hasNorm ? 1 : 0);
			geom.morphTargetHasTangentDeltas.push_back(hasTan ? 1 : 0);
			for (size_t i = 0; i < vertexCount; ++i)
				geom.morphTargetDeltaPositions.push_back(hasPos ? target.positionDeltas[i] : glm::vec3(0.0f));
			for (size_t i = 0; i < vertexCount; ++i)
				geom.morphTargetDeltaNormals.push_back(hasNorm ? target.normalDeltas[i] : glm::vec3(0.0f));
			for (size_t i = 0; i < vertexCount; ++i)
				geom.morphTargetDeltaTangents.push_back(hasTan ? target.tangentDeltas[i] : glm::vec3(0.0f));
		}

		// Current per-target weights (small, changes every animation tick -
		// mirrors jointPalette's role for skinning). Already resized/clamped
		// to morphTargets.size() by SceneMesh::applyMorphWeights().
		const QVector<float>& weightsQt = mesh->animationState().currentMorphWeights();
		geom.morphWeights.reserve(static_cast<size_t>(weightsQt.size()));
		for (float w : weightsQt)
			geom.morphWeights.push_back(w);
		geom.morphWeights.resize(geom.morphTargetCount, 0.0f);
	}

	geom.vertices.reserve(verts.size());
	for (const Vertex& v : verts)
	{
		RtVertex rv;
		rv.position  = v.Position;
		rv.normal    = v.Normal;
		rv.tangent   = v.Tangent;
		rv.bitangent = v.Bitangent;
		for (int uvSet = 0; uvSet < 4; ++uvSet)
			rv.texCoords[uvSet] = v.TexCoords[uvSet];
		if (hasVertexColors)
			rv.color = v.Color;

		if (geom.hasSkinningData)
		{
			geom.baseSkinPositions.push_back(rv.position);
			geom.baseSkinNormals.push_back(rv.normal);
			geom.baseSkinTangents.push_back(rv.tangent);
			geom.jointIndices.push_back(glm::vec4(v.JointIndices[0], v.JointIndices[1], v.JointIndices[2], v.JointIndices[3]));
			geom.jointWeights.push_back(glm::vec4(v.JointWeights[0], v.JointWeights[1], v.JointWeights[2], v.JointWeights[3]));

			// Precomputed ONCE, on the BIND-POSE tangent/normal/bitangent -
			// mirrors RtOptixSceneTracer's identical per-vertex handedness-
			// sign derivation exactly (see that class's GAS-loop doc
			// comment). Never needs recomputing per pose: it's a
			// topological property invariant under any pure rotation, and
			// joints here are rigid (rotation+translation only).
			float handedness = 1.0f;
			if (glm::length(rv.tangent) > 0.01f && glm::length(rv.bitangent) > 0.01f)
			{
				const glm::vec3 T = glm::normalize(rv.tangent - glm::dot(rv.tangent, rv.normal) * rv.normal);
				const glm::vec3 importedBitangent = glm::normalize(rv.bitangent - glm::dot(rv.bitangent, rv.normal) * rv.normal);
				const float sign = glm::sign(glm::dot(glm::cross(rv.normal, T), importedBitangent));
				if (sign != 0.0f)
					handedness = sign;
			}
			geom.tangentHandedness.push_back(handedness);
		}

		if (applySkinning)
		{
			glm::mat4 skin(0.0f);
			float totalWeight = 0.0f;
			for (int i = 0; i < 4; ++i)
			{
				const float weight = v.JointWeights[i];
				if (weight <= 0.0f)
					continue;
				const int jointIndex = static_cast<int>(v.JointIndices[i]);
				if (jointIndex < 0 || jointIndex >= static_cast<int>(jointPalette.size()))
					continue;
				skin += jointPalette[jointIndex] * weight;
				totalWeight += weight;
			}
			if (totalWeight > 0.0f)
			{
				rv.position = glm::vec3(skin * glm::vec4(rv.position, 1.0f));
				const glm::mat3 skin3(skin);
				rv.normal    = skin3 * rv.normal;
				rv.tangent   = skin3 * rv.tangent;
				rv.bitangent = skin3 * rv.bitangent;
			}
		}

		geom.vertices.push_back(rv);
	}

	geom.indices = mesh->indices();

	if (geom.hasSkinningData)
	{
		// See RtMeshGeometry::baseContentHash's doc comment - hashed over the
		// BIND-POSE arrays + joint indices/weights + indices ONLY, deliberately
		// excluding jointPalette (which changes every animation tick, unlike
		// everything else this identity needs to stay stable across).
		uint64_t hash = fnv1aHash(geom.indices.data(), geom.indices.size() * sizeof(uint32_t));
		hash = fnv1aHash(geom.baseSkinPositions.data(), geom.baseSkinPositions.size() * sizeof(glm::vec3), hash);
		hash = fnv1aHash(geom.baseSkinNormals.data(), geom.baseSkinNormals.size() * sizeof(glm::vec3), hash);
		hash = fnv1aHash(geom.baseSkinTangents.data(), geom.baseSkinTangents.size() * sizeof(glm::vec3), hash);
		hash = fnv1aHash(geom.jointIndices.data(), geom.jointIndices.size() * sizeof(glm::vec4), hash);
		hash = fnv1aHash(geom.jointWeights.data(), geom.jointWeights.size() * sizeof(glm::vec4), hash);
		geom.baseContentHash = hash;
	}

	if (geom.hasMorphData)
	{
		// See RtMeshGeometry::morphBaseContentHash's doc comment - hashed
		// over the rest-pose arrays + delta arrays + indices ONLY,
		// deliberately excluding morphWeights (which changes every
		// animation tick, unlike everything else this identity needs to
		// stay stable across).
		uint64_t hash = fnv1aHash(geom.indices.data(), geom.indices.size() * sizeof(uint32_t));
		hash = fnv1aHash(geom.baseMorphPositions.data(), geom.baseMorphPositions.size() * sizeof(glm::vec3), hash);
		hash = fnv1aHash(geom.baseMorphNormals.data(), geom.baseMorphNormals.size() * sizeof(glm::vec3), hash);
		hash = fnv1aHash(geom.baseMorphTangents.data(), geom.baseMorphTangents.size() * sizeof(glm::vec3), hash);
		hash = fnv1aHash(geom.morphTargetDeltaPositions.data(), geom.morphTargetDeltaPositions.size() * sizeof(glm::vec3), hash);
		hash = fnv1aHash(geom.morphTargetDeltaNormals.data(), geom.morphTargetDeltaNormals.size() * sizeof(glm::vec3), hash);
		hash = fnv1aHash(geom.morphTargetDeltaTangents.data(), geom.morphTargetDeltaTangents.size() * sizeof(glm::vec3), hash);
		geom.morphBaseContentHash = hash;
	}

	return geom;
}

std::shared_ptr<RtTextureSample> RtSceneBuilder::extractTextureSample(
	const SceneMesh* mesh, const SceneRuntime& runtime, const Material& material,
	int textureType, const char* meshTextureTypeKey, const QString& materialMapPath,
	const char* packingKey, TextureDedupCache& dedupCache,
	const RtPackingOverride* packingOverride)
{
	// Tier 1: mesh-level texture list (SceneMesh::textures(), keyed by a type
	// string like "albedoMap") - imported (glTF) materials don't use this, but
	// check it first since it's authoritative when present.
	Material::Texture meshLevelCopy;
	const Material::Texture* texPtr = nullptr;
	if (meshTextureTypeKey)
	{
		for (const Material::Texture& candidate : mesh->textures())
		{
			if (candidate.type == meshTextureTypeKey && !candidate.imageData.isNull())
			{
				meshLevelCopy = candidate;
				texPtr = &meshLevelCopy;
				break;
			}
		}
	}

	// Tier 2: Material's own internal per-TextureType array - the glTF/
	// import path's storage.
	const Material::Texture& materialLevelTex = material.texture(static_cast<Material::TextureType>(textureType));
	if (!texPtr && !materialLevelTex.imageData.isNull())
		texPtr = &materialLevelTex;

	QImage img;
	glm::vec2 uvScale(1.0f), uvOffset(0.0f);
	float uvRotation = 0.0f;
	int texCoordIndex = 0;
	unsigned int wrapS = 0x2901, wrapT = 0x2901; // GL_REPEAT

	if (texPtr)
	{
		img            = texPtr->imageData;
		uvScale        = texPtr->scale;
		uvOffset       = texPtr->offset;
		uvRotation     = texPtr->rotation;
		texCoordIndex  = texPtr->texCoordIndex;
		wrapS          = static_cast<unsigned int>(texPtr->wrapS);
		wrapT          = static_cast<unsigned int>(texPtr->wrapT);
	}
	else if (!materialMapPath.isEmpty())
	{
		// Tier 3: SceneRuntime's path-keyed decoded-image cache - the
		// predefined-material/Properties-panel resolution path
		// (ViewportWidget::resolveMaterialTextures()/getOrLoadTextureCached())
		// only ever writes a GL texture id onto the Material/Texture objects
		// themselves; the actual decoded pixels only exist here. Sampler/
		// transform metadata still comes from materialLevelTex (or its
		// defaults above), since the cache entry itself carries none.
		const auto it = runtime.texCache().find(materialMapPath);
		if (it != runtime.texCache().end() && !it->second.image.isNull())
		{
			img           = it->second.image;
			uvScale       = materialLevelTex.scale;
			uvOffset      = materialLevelTex.offset;
			uvRotation    = materialLevelTex.rotation;
			texCoordIndex = materialLevelTex.texCoordIndex;
			wrapS         = static_cast<unsigned int>(materialLevelTex.wrapS);
			wrapT         = static_cast<unsigned int>(materialLevelTex.wrapT);
		}
	}

	if (img.isNull())
		return nullptr;

	// Packing resolved before the cache lookup below, not after - see
	// extractTextureSample()'s own doc comment (RtSceneBuilder.h) on why a
	// cached/shared RtTextureSample can never be safely mutated post-hoc.
	int packingChannel = -1;
	bool packingInvert = false;
	float packingScale = 1.0f, packingBias = 0.0f;
	if (packingOverride)
	{
		packingChannel = packingOverride->channel;
		packingInvert  = packingOverride->invert;
		packingScale   = packingOverride->scale;
		packingBias    = packingOverride->bias;
	}
	else if (packingKey)
	{
		const Material::ChannelPacking packing = material.packingFor(QString::fromLatin1(packingKey));
		packingChannel = packing.channel;
		packingInvert  = packing.invert;
		packingScale   = packing.scale;
		packingBias    = packing.bias;
	}
	// else: not a scalar-packed slot (albedo/normal/emissive use full RGB) - packingChannel stays -1.

	// Dedup cache lookup keyed on the source QImage's identity (before any
	// format conversion below, since that's the buffer actually shared across
	// call sites that reference the same underlying texture) plus every
	// baked-in parameter resolved so far - see TextureDedupKey's doc comment.
	// Checked before the (potentially expensive) format conversion/scanline
	// copy/mip-chain build below so a cache hit skips all of that work, not
	// just the shared_ptr allocation.
	TextureDedupKey key;
	key.imageCacheKey  = img.cacheKey();
	key.uvScaleX       = uvScale.x;
	key.uvScaleY       = uvScale.y;
	key.uvOffsetX      = uvOffset.x;
	key.uvOffsetY      = uvOffset.y;
	key.uvRotation     = uvRotation;
	key.texCoordIndex  = texCoordIndex;
	key.wrapS          = wrapS;
	key.wrapT          = wrapT;
	key.packingChannel = packingChannel;
	key.packingInvert  = packingInvert;
	key.packingScale   = packingScale;
	key.packingBias    = packingBias;

	if (const auto it = dedupCache.find(key); it != dedupCache.end())
		return it->second;

	if (img.format() != QImage::Format_RGBA8888)
		img = img.convertToFormat(QImage::Format_RGBA8888);
	if (img.isNull() || img.width() <= 0 || img.height() <= 0)
		return nullptr;

	auto sample = std::make_shared<RtTextureSample>();
	sample->width  = img.width();
	sample->height = img.height();
	sample->rgba8.resize(static_cast<size_t>(img.width()) * static_cast<size_t>(img.height()) * 4);

	// Copy scanline-by-scanline: QImage rows may be padded (bytesPerLine()
	// can exceed width*4), so a single flat memcpy would be wrong.
	for (int y = 0; y < img.height(); ++y)
	{
		const uchar* line = img.constScanLine(y);
		std::memcpy(
			sample->rgba8.data() + static_cast<size_t>(y) * img.width() * 4,
			line,
			static_cast<size_t>(img.width()) * 4);
	}

	sample->texCoordIndex  = texCoordIndex;
	sample->uvScale        = uvScale;
	sample->uvOffset       = uvOffset;
	sample->uvRotation     = uvRotation;
	sample->wrapS          = wrapS;
	sample->wrapT          = wrapT;
	sample->packingChannel = packingChannel;
	sample->packingInvert  = packingInvert;
	sample->packingScale   = packingScale;
	sample->packingBias    = packingBias;
	// Same value as `key.imageCacheKey` above (computed from the PRE-format-
	// conversion source img) - see RtTextureSample::imageCacheKey's own doc
	// comment for why this needs to survive past this function, unlike the
	// rest of TextureDedupKey which only ever mattered within this one
	// build() call.
	sample->imageCacheKey = key.imageCacheKey;

	buildMipChain(*sample);
	dedupCache.emplace(key, sample);
	return sample;
}

RtMaterial RtSceneBuilder::convertMaterial(const SceneMesh* mesh, const SceneRuntime& runtime, TextureDedupCache& dedupCache)
{
	const Material material = mesh->getMaterial();

	RtMaterial rt;
	rt.baseColor        = toGlm(material.albedoColor());
	rt.metalness        = material.metalness();
	rt.roughness        = material.roughness();
	rt.emissive         = toGlm(material.emissive());
	rt.emissiveStrength = material.emissiveStrength();
	rt.opacity          = material.opacity();
	rt.blendMode        = static_cast<int>(material.blendMode());
	rt.alphaThreshold   = material.alphaThreshold();
	rt.opacityTexture   = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::Opacity), "opacityMap", material.opacityMapPath(), "opacity", dedupCache);
	rt.twoSided          = material.twoSided();
	rt.unlit             = material.isUnlit();
	rt.occlusionStrength = material.occlusionStrength();

	// KHR_materials_pbrSpecularGlossiness - see RtSceneSnapshot.h's comment
	// on why this completely overrides baseColor/metalness/roughness/F0
	// rather than being an additive modifier like the other extensions.
	rt.useSpecGloss = material.getUseSpecularGlossiness();
	rt.diffuseColor = toGlm(material.diffuseColor());
	rt.specGlossSpecularColor = toGlm(material.specularColor());
	rt.glossinessFactor = material.glossinessFactor();
	rt.diffuseTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::Diffuse), "diffuseMap", material.diffuseMapPath(), nullptr, dedupCache);
	// Packed RGB=specular color (sRGB, sampled directly as .rgb in
	// evaluateSurface() - NOT via applyChannelPacking, which is only for
	// single-channel factors) / A=glossiness (linear, packed channel below).
	{
		const RtPackingOverride packAlpha{ 3, false, 1.0f, 0.0f }; // A
		rt.specularGlossinessTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::SpecularGlossiness), "specularGlossinessMap", material.specularGlossinessMap(), nullptr, dedupCache, &packAlpha);
	}
	rt.ior                  = material.ior();
	rt.specularFactor       = material.specularFactor();
	rt.specularColorFactor  = toGlm(material.specularColorFactor());

	rt.baseColorTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::Albedo), "albedoMap", material.albedoMapPath(), nullptr, dedupCache);
	rt.metallicTexture  = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::Metallic), "metallicMap", material.metallicMapPath(), "metallic", dedupCache);
	rt.roughnessTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::Roughness), "roughnessMap", material.roughnessMapPath(), "roughness", dedupCache);
	rt.normalTexture    = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::Normal), "normalMap", material.normalMapPath(), nullptr, dedupCache);
	rt.normalScale      = material.normalScale();
	rt.emissiveTexture  = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::Emissive), "emissiveMap", material.emissiveMapPath(), nullptr, dedupCache);
	rt.aoTexture        = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::AmbientOcclusion), "aoMap", material.aoMapPath(), "ao", dedupCache);

	// KHR_materials_specular's per-pixel maps - specularFactorMap's alpha
	// channel scales specularFactor (glTF-declared packing, not user-
	// configurable via Material::packingFor() like metallic/roughness/ao,
	// so the packing metadata is passed as an override below rather than via
	// a packingKey lookup), specularColorMap's RGB (sRGB) tints
	// specularColorFactor.
	{
		const RtPackingOverride packAlpha{ 3, false, 1.0f, 0.0f }; // A
		rt.specularTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::SpecularFactor), "specularFactorMap", material.specularFactorMap(), nullptr, dedupCache, &packAlpha);
	}
	rt.specularColorTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::SpecularColor), "specularColorMap", material.specularColorMap(), nullptr, dedupCache);

	// KHR_materials_clearcoat. Channel packing is glTF-fixed (not user-
	// configurable via Material::packingFor() like metallic/roughness/ao),
	// so it's passed as an override below rather than via a packingKey
	// lookup - mirrors main_scene.frag's hardcoded ".r"/".g" reads.
	rt.clearcoat          = material.clearcoat();
	rt.clearcoatRoughness = std::clamp(material.clearcoatRoughness(), 0.0001f, 1.0f);
	{
		const RtPackingOverride packR{ 0, false, 1.0f, 0.0f }; // R
		rt.clearcoatTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::ClearcoatColor), "clearcoatColorMap", material.clearcoatColorMapPath(), nullptr, dedupCache, &packR);
	}
	{
		const RtPackingOverride packG{ 1, false, 1.0f, 0.0f }; // G
		rt.clearcoatRoughnessTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::ClearcoatRoughness), "clearcoatRoughnessMap", material.clearcoatRoughnessMapPath(), nullptr, dedupCache, &packG);
	}
	rt.clearcoatNormalTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::ClearcoatNormal), "clearcoatNormalMap", material.clearcoatNormalMapPath(), nullptr, dedupCache);
	rt.clearcoatNormalScale   = material.clearcoatNormalScale();

	// KHR_materials_sheen. Channel packing is glTF-fixed (sheenRoughnessMap's
	// alpha channel), so passed as an override rather than via a packingKey
	// lookup.
	rt.sheenColor     = toGlm(material.sheenColor());
	rt.sheenRoughness = std::clamp(material.sheenRoughness(), 0.0001f, 1.0f);
	rt.sheenColorTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::SheenColor), "sheenColorMap", material.sheenColorMapPath(), nullptr, dedupCache);
	{
		const RtPackingOverride packAlpha{ 3, false, 1.0f, 0.0f }; // A
		rt.sheenRoughnessTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::SheenRoughness), "sheenRoughnessMap", material.sheenRoughnessMapPath(), nullptr, dedupCache, &packAlpha);
	}

	// KHR_materials_anisotropy
	rt.anisotropyStrength = material.anisotropyStrength();
	rt.anisotropyRotation = material.anisotropyRotation();
	rt.anisotropyTexture  = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::Anisotropy), "anisotropyMap", material.anisotropyMap(), nullptr, dedupCache);

	// KHR_materials_iridescence
	rt.iridescenceFactor       = material.iridescenceFactor();
	rt.iridescenceIor          = material.iridescenceIor();
	rt.iridescenceThicknessMin = material.iridescenceThicknessMin();
	rt.iridescenceThicknessMax = material.iridescenceThicknessMax();
	{
		const RtPackingOverride packR{ 0, false, 1.0f, 0.0f }; // R
		rt.iridescenceTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::Iridescence), "iridescenceMap", material.iridescenceMap(), nullptr, dedupCache, &packR);
	}
	{
		// mix(min, max, texG) == texG*(max-min) + min - exactly the linear
		// form applyChannelPacking() computes (packingScale*v + packingBias).
		const RtPackingOverride packG{ 1, false, rt.iridescenceThicknessMax - rt.iridescenceThicknessMin, rt.iridescenceThicknessMin }; // G
		rt.iridescenceThicknessTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::IridescenceThickness), "iridescenceThicknessMap", material.iridescenceThicknessMap(), nullptr, dedupCache, &packG);
	}

	// KHR_materials_transmission + KHR_materials_volume - see
	// RtSceneSnapshot.h's comment on why thicknessFactor's VALUE is
	// deliberately not read here (only its presence, as hasVolume).
	rt.transmission = material.transmission();
	{
		const RtPackingOverride packR{ 0, false, 1.0f, 0.0f }; // R
		rt.transmissionTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::Transmission), "transmissionMap", material.transmissionMapPath(), nullptr, dedupCache, &packR);
	}
	rt.hasVolume           = material.thicknessFactor() > 0.0f;
	rt.attenuationColor    = toGlm(material.attenuationColor());
	rt.attenuationDistance = material.attenuationDistance();
	rt.dispersion          = material.dispersion();
	rt.thicknessFactor     = material.thicknessFactor();
	// multiscatterColorFactor is consumed directly as Burley normalized
	// diffusion's target albedo `A` - no conversion needed (see
	// sampleBSSRDFEntryPoint()'s doc comment in CpuPathTracer.cpp).
	rt.multiScatterColor = toGlm(material.multiScatterColor());
	rt.hasVolumeScattering = material.hasVolumeScattering();

	// KHR_materials_diffuse_transmission - see RtSceneSnapshot.h's comment
	// on why this is handled as a front/back-hemisphere split of the
	// existing Lambertian lobe rather than a refraction path.
	rt.diffuseTransmissionFactor = material.diffuseTransmissionFactor();
	rt.diffuseTransmissionColor  = toGlm(material.diffuseTransmissionColorFactor());
	{
		const RtPackingOverride packAlpha{ 3, false, 1.0f, 0.0f }; // A
		rt.diffuseTransmissionTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::DiffuseTransmission), "diffuseTransmissionMap", material.diffuseTransmissionMap(), nullptr, dedupCache, &packAlpha);
	}
	rt.diffuseTransmissionColorTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::DiffuseTransmissionColor), "diffuseTransmissionColorMap", material.diffuseTransmissionColorMap(), nullptr, dedupCache);

	return rt;
}

RtMaterial RtSceneBuilder::convertFloorMaterial(const SceneRuntime& runtime, const Material& material, bool reflectionsEnabled,
	bool shadowCatcherEnabled, float shadowCatcherDarkness, const QVector3D& shadowCatcherBaseColor,
	float shadowCatcherMetalness, float shadowCatcherRoughness, TextureDedupCache& dedupCache)
{
	RtMaterial rt;
	// Shadow-catcher floor mode (path tracer only) - see RtMaterial::
	// isShadowCatcher's doc comment. Set here, unconditionally of the rest
	// of this function's normal material conversion below (which still runs
	// so rt.baseColor/roughness/etc. stay populated for the non-shadow-
	// catcher fallback both tracers use if this flag is off - the shadow-
	// catcher gate itself never reads them, since it always substitutes
	// background radiance instead, and its continuation bounce reads the
	// independent shadowCatcherBaseColor/Metalness/Roughness below instead).
	rt.isShadowCatcher       = shadowCatcherEnabled;
	rt.shadowCatcherDarkness = shadowCatcherDarkness;
	rt.shadowCatcherBaseColor = glm::vec3(shadowCatcherBaseColor.x(), shadowCatcherBaseColor.y(), shadowCatcherBaseColor.z());
	rt.shadowCatcherMetalness = shadowCatcherMetalness;
	rt.shadowCatcherRoughness = shadowCatcherRoughness;
	rt.baseColor         = toGlm(material.albedoColor());
	rt.metalness         = material.metalness();
	if (reflectionsEnabled)
	{
		// See the declaration comment in RtSceneBuilder.h: raster's floor
		// reflection never reads Material::roughness at all (it's a separate
		// fake planar-mirror pass), so reusing the raster value verbatim (0.45
		// by default - fairly diffuse-dominant) leaves a real BRDF path tracer's
		// floor looking duller than what raster shows. This is the one
		// physically-grounded lever available to make it actually reflective.
		// Clamped lower than the first attempt (0.12) - once the fallback
		// light's calibrated intensity made the floor's direct-lit diffuse
		// response strong enough for a visible shadow, that same brightness
		// compressed the previous, still-fairly-soft reflection into near-
		// invisibility through the ACES tonemap. A sharper (lower-roughness)
		// GGX lobe concentrates the same reflected energy into a smaller,
		// higher-contrast highlight instead of spreading it thin, so it reads
		// as a real reflection rather than a faint smear even next to a bright
		// diffuse floor.
		rt.roughness = (std::min)(material.roughness(), 0.04f);
	}
	else
	{
		// "Reflections" toggled off (Visualization panel, mirrors raster's
		// own fake-reflection-pass gate) - use the floor's actual material
		// roughness unmodified, same as any other surface, so no visible
		// specular reflection shows.
		rt.roughness = material.roughness();
	}
	rt.emissive          = toGlm(material.emissive());
	rt.emissiveStrength  = material.emissiveStrength();
	rt.opacity           = material.opacity();
	rt.occlusionStrength = material.occlusionStrength();

	// mesh=nullptr, meshTextureTypeKey=nullptr: skips extractTextureSample()'s
	// tier-1 (SceneMesh::textures()) lookup entirely without ever
	// dereferencing mesh - the floor has no SceneMesh behind it. Tier 2
	// (material.texture(Albedo)) already carries imageData directly (see
	// ViewportWidget::syncFloorPlaneAlbedoTexture()'s "generated://floor-
	// albedo" texture), so no texCache/tier-3 lookup is needed either.
	rt.baseColorTexture = extractTextureSample(nullptr, runtime, material, static_cast<int>(Material::TextureType::Albedo), nullptr, material.albedoMapPath(), nullptr, dedupCache);

	return rt;
}

void RtSceneBuilder::fillInfinitePlane(RtSceneSnapshot& snapshot, const SceneRuntime& runtime, const RtFloorParams& floor, TextureDedupCache& dedupCache)
{
	if (!floor.floorMesh)
		return; // floor material not created yet (e.g. before the viewport's first layout pass)

	snapshot.infinitePlane.enabled         = true;
	snapshot.infinitePlane.cameraUpAxisZUp = floor.cameraUpAxisZUp;
	snapshot.infinitePlane.height          = floor.planeLevel;
	snapshot.infinitePlane.material = convertFloorMaterial(
		runtime, floor.floorMesh->getMaterial(), floor.reflectionsEnabled,
		floor.shadowCatcherEnabled, floor.shadowCatcherDarkness, floor.shadowCatcherBaseColor,
		floor.shadowCatcherMetalness, floor.shadowCatcherRoughness, dedupCache);
}

void RtSceneBuilder::addFloorInstance(RtSceneSnapshot& snapshot, const SceneRuntime& runtime, const RtFloorParams& floor, TextureDedupCache& dedupCache)
{
	if (!floor.floorMesh)
		return; // floor not created yet (e.g. before the viewport's first layout pass)

	// Half-extent from the live scene bounding box, not the raster floor's
	// own (much larger, aesthetic fade-out) geometry - see the declaration
	// comments in RtSceneBuilder.h/RtFloorParams for why. A flat 30% margin
	// keeps the floor extending a bit past the model on every side (so
	// contact shadows/reflections aren't clipped right at the silhouette)
	// without paying for the raster extent's far larger area. Square, not
	// matching the bounding box's actual aspect ratio: using the larger of
	// the two footprint axes for both keeps the floor from looking cut off
	// along whichever axis happens to be shorter (e.g. a long, narrow model).
	constexpr float kMarginFactor = 1.3f;
	const BoundingBox& bbox = floor.sceneBoundingBox;
	const float extentU = static_cast<float>(bbox.getXSize());
	const float extentV = floor.cameraUpAxisZUp ? static_cast<float>(bbox.getYSize()) : static_cast<float>(bbox.getZSize());
	const float halfExtent = (std::max)(extentU, extentV) * 0.5f * kMarginFactor;
	const float safeHalfU = halfExtent > 1e-4f ? halfExtent : 1.0f;
	const float safeHalfV = safeHalfU;

	const float cx = floor.center.x();
	const float cu2 = floor.cameraUpAxisZUp ? floor.center.y() : floor.center.z();

	const float ptSideLength = 2.0f * safeHalfU;
	const float sizeRatio = floor.rasterFloorExtent > 1e-4f ? (ptSideLength / floor.rasterFloorExtent) : 1.0f;
	const float effectiveRepeatS = floor.texRepeatS * sizeRatio;
	const float effectiveRepeatT = floor.texRepeatT * sizeRatio;

	RtMeshGeometry geom;
	geom.vertices.resize(4);
	for (int corner = 0; corner < 4; ++corner)
	{
		const float su = (corner == 1 || corner == 2) ? 1.0f : -1.0f;
		const float sv = (corner == 2 || corner == 3) ? 1.0f : -1.0f;
		const float u = cx + su * safeHalfU;
		const float v = cu2 + sv * safeHalfV;

		RtVertex& vert = geom.vertices[static_cast<size_t>(corner)];
		if (floor.cameraUpAxisZUp)
		{
			vert.position = glm::vec3(u, v, floor.planeLevel);
			vert.normal   = glm::vec3(0.0f, 0.0f, -1.0f);
		}
		else
		{
			vert.position = glm::vec3(u, floor.planeLevel, v);
			vert.normal   = glm::vec3(0.0f, -1.0f, 0.0f);
		}
		const glm::vec2 uv((su + 1.0f) * 0.5f * effectiveRepeatS, (sv + 1.0f) * 0.5f * effectiveRepeatT);
		for (int uvSet = 0; uvSet < 4; ++uvSet)
			vert.texCoords[uvSet] = uv;
	}
	geom.indices = { 0, 1, 2, 0, 2, 3 };

	// Reserved id, never a real SceneRuntime object id (those come from
	// currentVisibleObjectIds(), small non-negative indices) - this quad is
	// synthetic, regenerated fresh from floor params on every build() call,
	// so it needs its own stable identity distinct from mesh index 0 to
	// avoid RtOptixSceneTracer::Impl::persistentGasCache colliding a real
	// mesh's cache entry with the floor's (or vice versa). contentHash is
	// still computed for real below - the floor's vertex data changes
	// whenever the scene bounding box/repeat/center do, and that must be
	// caught the same way any other mesh's content change is.
	geom.sourceObjectId = 0xFFFFFFFFu;
	geom.contentHash = fnv1aHash(geom.indices.data(), geom.indices.size() * sizeof(uint32_t));
	geom.contentHash = fnv1aHash(geom.vertices.data(), geom.vertices.size() * sizeof(RtVertex), geom.contentHash);

	const uint32_t index = static_cast<uint32_t>(snapshot.meshes.size());
	snapshot.meshes.push_back(std::move(geom));
	snapshot.materials.push_back(snapshot.infinitePlane.material);

	RtInstance instance;
	instance.meshIndex     = index;
	instance.materialIndex = index;
	instance.localToWorld  = glm::mat4(1.0f);
	snapshot.infinitePlane.instanceIndex = static_cast<uint32_t>(snapshot.instances.size());
	snapshot.instances.push_back(instance);
}

RtCamera RtSceneBuilder::buildCamera(const Camera& camera, float aspectRatio)
{
	RtCamera rtCamera;

	// Mirrors the raster camera convention (view vector = normalize(cameraPos
	// - fragPos), see main_scene.frag) so primary rays align with what's on
	// screen - see Camera.h/getRenderPosition() for why that getter specifically.
	rtCamera.position    = toGlm(camera.getRenderPosition());
	rtCamera.forward     = toGlm(camera.getViewDir());
	rtCamera.right       = toGlm(camera.getRightVector());
	rtCamera.up          = toGlm(camera.getUpVector());
	rtCamera.aspectRatio = aspectRatio;

	// Read the vertical scale factor straight out of the projection matrix
	// (element [1][1] of both a standard OpenGL perspective matrix - where it
	// equals 1/tan(fovY/2) - and a symmetric ortho matrix - where it equals
	// 2/(top-bottom), i.e. 1/halfHeight) rather than recomputing it from
	// Camera::getFOV()/getAspectRatio() - see RtCamera's comment for why.
	rtCamera.orthographic = (camera.getProjectionType() == Camera::ProjectionType::ORTHOGRAPHIC);
	const QMatrix4x4 proj = camera.getProjectionMatrix();
	const float p11 = proj(1, 1);
	const float verticalScale = (std::abs(p11) > 1e-8f) ? (1.0f / p11) : 1.0f;
	if (rtCamera.orthographic)
		rtCamera.orthoHalfHeight = verticalScale;
	else
		rtCamera.tanHalfFovY = verticalScale;

	return rtCamera;
}

std::shared_ptr<RtSceneSnapshot> RtSceneBuilder::build(
	const SceneRuntime& runtime,
	const Camera& camera,
	float aspectRatio,
	const std::vector<GPULight>& lights,
	uint64_t revisionId,
	const RtEnvironment* environment,
	const RtFloorParams* floor,
	bool shadowsEnabled,
	bool selfShadowsEnabled)
{
	auto snapshot = std::make_shared<RtSceneSnapshot>();
	snapshot->revisionId = revisionId;
	snapshot->shadowsEnabled = shadowsEnabled;
	snapshot->selfShadowsEnabled = selfShadowsEnabled;

	// Only currently-shown meshes (matches _sceneRuntime.currentVisibleObjectIds()
	// used by the raster render loop) - deliberately NOT the per-frame frustum/
	// clip-plane visibility used by ViewportWidget::isMeshVisible(), since that
	// is a raster-only view-dependent optimization: a path-traced ray can hit
	// geometry currently outside the raster frustum after a bounce.
	const std::vector<int>& visibleIds = runtime.currentVisibleObjectIds();
	snapshot->meshes.reserve(visibleIds.size());
	snapshot->instances.reserve(visibleIds.size());
	snapshot->materials.reserve(visibleIds.size());

	// Scoped to this single build() call - see TextureDedupCache's own doc
	// comment (RtSceneBuilder.h) for why a texture edited/reloaded between
	// two builds is never at risk of serving a stale cached copy from this.
	TextureDedupCache dedupCache;

	for (int id : visibleIds)
	{
		if (id < 0) continue;
		const SceneMesh* mesh = runtime.meshAt(static_cast<size_t>(id));
		if (!mesh) continue;

		// Path tracing needs triangle data - a non-triangle mesh's index
		// buffer (points/lines/strips - e.g. hinges, wires, or other
		// non-surface elements some models include) is not a flat triangle
		// list, and reading it as one produces garbage/degenerate triangles
		// with essentially random connectivity. Skip anything that isn't
		// GL_TRIANGLES rather than silently misinterpreting its indices.
		if (mesh->getPrimitiveMode() != GL_TRIANGLES)
			continue;

		const uint32_t index = static_cast<uint32_t>(snapshot->meshes.size());

		RtMeshGeometry geometry = convertGeometry(mesh);
		// mesh->uuid() (Drawable::uuid(), same stable QUuid SceneMeshRecord
		// carries alongside this same pointer in _meshStore) - NOT `id`
		// itself. `id` is a positional index into _meshStore
		// (SceneRuntime::meshAt()), and SceneRuntime::removeMeshAt()/
		// restoreDetachedMesh() erase()/insert() that vector directly,
		// shifting every LATER mesh's index by one - so a single delete or
		// undo-restore elsewhere in the scene would otherwise change most
		// OTHER meshes' "identity" too, even though nothing about their own
		// geometry changed (confirmed via a real log: undoing a 3-object
		// delete in a ~150-mesh scene spuriously cache-missed ~100 unrelated
		// meshes in RtOptixSceneTracer::Impl::persistentGasCache). The UUID
		// is genuinely stable across the mesh's whole lifetime regardless of
		// what else in the scene is added/removed/reordered.
		geometry.sourceObjectId = static_cast<uint32_t>(qHash(mesh->uuid()));
		// Hashed over the FINAL (post-skinning/morph) buffers convertGeometry()
		// just produced, not the SceneMesh's own bind-pose data - see
		// RtMeshGeometry::contentHash's doc comment for why that distinction
		// is correctness-critical, not just a convenience.
		geometry.contentHash = fnv1aHash(geometry.indices.data(), geometry.indices.size() * sizeof(uint32_t));
		geometry.contentHash = fnv1aHash(geometry.vertices.data(), geometry.vertices.size() * sizeof(RtVertex), geometry.contentHash);
		snapshot->meshes.push_back(std::move(geometry));
		snapshot->materials.push_back(convertMaterial(mesh, runtime, dedupCache));

		RtInstance instance;
		instance.meshIndex     = index;
		instance.materialIndex = index; // 1:1 for v1 - see RtSceneSnapshot.h comment on deferred instance-sharing
		instance.localToWorld  = toGlm(mesh->combinedRenderTransform());
		snapshot->instances.push_back(instance);
	}

	if (floor && floor->groundMode == GroundMode::Floor)
	{
		fillInfinitePlane(*snapshot, runtime, *floor, dedupCache);
		// Shadow-catcher PT mode should behave like NVIDIA's analytic
		// infinite plane, not like a finite fallback quad. Keeping the mesh
		// proxy here is what creates the hard rectangular footprint visible
		// in the user's screenshot, because primary rays can still hit that
		// geometry instead of an unbounded plane/miss-equivalent.
		if (!floor->shadowCatcherEnabled)
			addFloorInstance(*snapshot, runtime, *floor, dedupCache);
	}

	snapshot->lights.reserve(lights.size());
	for (const GPULight& light : lights)
	{
		RtLight rl;
		rl.type         = light.type;
		rl.position     = light.position;
		rl.direction    = light.direction;
		rl.color        = light.color;
		rl.intensity    = light.intensity;
		rl.range        = light.range;
		rl.innerConeCos = light.innerConeCos;
		rl.outerConeCos = light.outerConeCos;
		snapshot->lights.push_back(rl);
	}

	snapshot->camera = buildCamera(camera, aspectRatio);

	if (environment)
		snapshot->environment = *environment;

	return snapshot;
}
