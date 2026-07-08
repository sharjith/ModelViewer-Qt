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
	inline glm::vec3 toGlm(const QVector3D& v) { return glm::vec3(v.x(), v.y(), v.z()); }

	inline glm::mat4 toGlm(const QMatrix4x4& m)
	{
		// QMatrix4x4::constData() is column-major (OpenGL convention), same
		// layout glm::mat4 expects - a direct element copy is correct.
		return glm::make_mat4(m.constData());
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
		geom.vertices.push_back(rv);
	}

	geom.indices = mesh->indices();

	return geom;
}

std::shared_ptr<RtTextureSample> RtSceneBuilder::extractTextureSample(
	const SceneMesh* mesh, const SceneRuntime& runtime, const Material& material,
	int textureType, const char* meshTextureTypeKey, const QString& materialMapPath,
	const char* packingKey)
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

	sample->texCoordIndex = texCoordIndex;
	sample->uvScale       = uvScale;
	sample->uvOffset      = uvOffset;
	sample->uvRotation    = uvRotation;
	sample->wrapS         = wrapS;
	sample->wrapT         = wrapT;

	if (packingKey)
	{
		const Material::ChannelPacking packing = material.packingFor(QString::fromLatin1(packingKey));
		sample->packingChannel = packing.channel;
		sample->packingInvert  = packing.invert;
		sample->packingScale   = packing.scale;
		sample->packingBias    = packing.bias;
	}
	else
	{
		// Not a scalar-packed slot (albedo/normal/emissive use full RGB).
		sample->packingChannel = -1;
	}

	return sample;
}

RtMaterial RtSceneBuilder::convertMaterial(const SceneMesh* mesh, const SceneRuntime& runtime)
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
	rt.opacityTexture   = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::Opacity), "opacityMap", material.opacityMapPath(), "opacity");
	rt.unlit             = material.isUnlit();
	rt.occlusionStrength = material.occlusionStrength();

	// KHR_materials_pbrSpecularGlossiness - see RtSceneSnapshot.h's comment
	// on why this completely overrides baseColor/metalness/roughness/F0
	// rather than being an additive modifier like the other extensions.
	rt.useSpecGloss = material.getUseSpecularGlossiness();
	rt.diffuseColor = toGlm(material.diffuseColor());
	rt.specGlossSpecularColor = toGlm(material.specularColor());
	rt.glossinessFactor = material.glossinessFactor();
	rt.diffuseTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::Diffuse), "diffuseMap", material.diffuseMapPath(), nullptr);
	// Packed RGB=specular color (sRGB, sampled directly as .rgb in
	// evaluateSurface() - NOT via applyChannelPacking, which is only for
	// single-channel factors) / A=glossiness (linear, packed channel below).
	rt.specularGlossinessTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::SpecularGlossiness), "specularGlossinessMap", material.specularGlossinessMap(), nullptr);
	if (rt.specularGlossinessTexture)
	{
		rt.specularGlossinessTexture->packingChannel = 3; // A
		rt.specularGlossinessTexture->packingInvert  = false;
		rt.specularGlossinessTexture->packingScale   = 1.0f;
		rt.specularGlossinessTexture->packingBias    = 0.0f;
	}
	rt.ior                  = material.ior();
	rt.specularFactor       = material.specularFactor();
	rt.specularColorFactor  = toGlm(material.specularColorFactor());

	rt.baseColorTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::Albedo), "albedoMap", material.albedoMapPath(), nullptr);
	rt.metallicTexture  = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::Metallic), "metallicMap", material.metallicMapPath(), "metallic");
	rt.roughnessTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::Roughness), "roughnessMap", material.roughnessMapPath(), "roughness");
	rt.normalTexture    = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::Normal), "normalMap", material.normalMapPath(), nullptr);
	rt.emissiveTexture  = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::Emissive), "emissiveMap", material.emissiveMapPath(), nullptr);
	rt.aoTexture        = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::AmbientOcclusion), "aoMap", material.aoMapPath(), "ao");

	// KHR_materials_specular's per-pixel maps - specularFactorMap's alpha
	// channel scales specularFactor (glTF-declared packing, not user-
	// configurable via Material::packingFor() like metallic/roughness/ao,
	// so the packing metadata is set directly below rather than via a
	// packingKey lookup), specularColorMap's RGB (sRGB) tints
	// specularColorFactor.
	rt.specularTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::SpecularFactor), "specularFactorMap", material.specularFactorMap(), nullptr);
	if (rt.specularTexture)
	{
		rt.specularTexture->packingChannel = 3; // A
		rt.specularTexture->packingInvert  = false;
		rt.specularTexture->packingScale   = 1.0f;
		rt.specularTexture->packingBias    = 0.0f;
	}
	rt.specularColorTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::SpecularColor), "specularColorMap", material.specularColorMap(), nullptr);

	// KHR_materials_clearcoat. Channel packing is glTF-fixed (not user-
	// configurable via Material::packingFor() like metallic/roughness/ao),
	// so it's set directly below rather than via a packingKey lookup -
	// mirrors main_scene.frag's hardcoded ".r"/".g" reads.
	rt.clearcoat          = material.clearcoat();
	rt.clearcoatRoughness = std::clamp(material.clearcoatRoughness(), 0.0001f, 1.0f);
	rt.clearcoatTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::ClearcoatColor), "clearcoatColorMap", material.clearcoatColorMapPath(), nullptr);
	if (rt.clearcoatTexture)
	{
		rt.clearcoatTexture->packingChannel = 0; // R
		rt.clearcoatTexture->packingInvert  = false;
		rt.clearcoatTexture->packingScale   = 1.0f;
		rt.clearcoatTexture->packingBias    = 0.0f;
	}
	rt.clearcoatRoughnessTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::ClearcoatRoughness), "clearcoatRoughnessMap", material.clearcoatRoughnessMapPath(), nullptr);
	if (rt.clearcoatRoughnessTexture)
	{
		rt.clearcoatRoughnessTexture->packingChannel = 1; // G
		rt.clearcoatRoughnessTexture->packingInvert  = false;
		rt.clearcoatRoughnessTexture->packingScale   = 1.0f;
		rt.clearcoatRoughnessTexture->packingBias    = 0.0f;
	}
	rt.clearcoatNormalTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::ClearcoatNormal), "clearcoatNormalMap", material.clearcoatNormalMapPath(), nullptr);

	// KHR_materials_sheen. Channel packing is glTF-fixed (sheenRoughnessMap's
	// alpha channel), so set directly rather than via a packingKey lookup.
	rt.sheenColor     = toGlm(material.sheenColor());
	rt.sheenRoughness = std::clamp(material.sheenRoughness(), 0.0001f, 1.0f);
	rt.sheenColorTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::SheenColor), "sheenColorMap", material.sheenColorMapPath(), nullptr);
	rt.sheenRoughnessTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::SheenRoughness), "sheenRoughnessMap", material.sheenRoughnessMapPath(), nullptr);
	if (rt.sheenRoughnessTexture)
	{
		rt.sheenRoughnessTexture->packingChannel = 3; // A
		rt.sheenRoughnessTexture->packingInvert  = false;
		rt.sheenRoughnessTexture->packingScale   = 1.0f;
		rt.sheenRoughnessTexture->packingBias    = 0.0f;
	}

	// KHR_materials_anisotropy
	rt.anisotropyStrength = material.anisotropyStrength();
	rt.anisotropyRotation = material.anisotropyRotation();
	rt.anisotropyTexture  = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::Anisotropy), "anisotropyMap", material.anisotropyMap(), nullptr);

	// KHR_materials_iridescence
	rt.iridescenceFactor       = material.iridescenceFactor();
	rt.iridescenceIor          = material.iridescenceIor();
	rt.iridescenceThicknessMin = material.iridescenceThicknessMin();
	rt.iridescenceThicknessMax = material.iridescenceThicknessMax();
	rt.iridescenceTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::Iridescence), "iridescenceMap", material.iridescenceMap(), nullptr);
	if (rt.iridescenceTexture)
	{
		rt.iridescenceTexture->packingChannel = 0; // R
		rt.iridescenceTexture->packingInvert  = false;
		rt.iridescenceTexture->packingScale   = 1.0f;
		rt.iridescenceTexture->packingBias    = 0.0f;
	}
	rt.iridescenceThicknessTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::IridescenceThickness), "iridescenceThicknessMap", material.iridescenceThicknessMap(), nullptr);
	if (rt.iridescenceThicknessTexture)
	{
		// mix(min, max, texG) == texG*(max-min) + min - exactly the linear
		// form applyChannelPacking() computes (packingScale*v + packingBias).
		rt.iridescenceThicknessTexture->packingChannel = 1; // G
		rt.iridescenceThicknessTexture->packingInvert  = false;
		rt.iridescenceThicknessTexture->packingScale   = rt.iridescenceThicknessMax - rt.iridescenceThicknessMin;
		rt.iridescenceThicknessTexture->packingBias    = rt.iridescenceThicknessMin;
	}

	// KHR_materials_transmission + KHR_materials_volume - see
	// RtSceneSnapshot.h's comment on why thicknessFactor's VALUE is
	// deliberately not read here (only its presence, as hasVolume).
	rt.transmission = material.transmission();
	rt.transmissionTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::Transmission), "transmissionMap", material.transmissionMapPath(), nullptr);
	if (rt.transmissionTexture)
	{
		rt.transmissionTexture->packingChannel = 0; // R
		rt.transmissionTexture->packingInvert  = false;
		rt.transmissionTexture->packingScale   = 1.0f;
		rt.transmissionTexture->packingBias    = 0.0f;
	}
	rt.hasVolume           = material.thicknessFactor() > 0.0f;
	rt.attenuationColor    = toGlm(material.attenuationColor());
	rt.attenuationDistance = material.attenuationDistance();
	rt.dispersion          = material.dispersion();
	rt.thicknessFactor     = material.thicknessFactor();

	// KHR_materials_diffuse_transmission - see RtSceneSnapshot.h's comment
	// on why this is handled as a front/back-hemisphere split of the
	// existing Lambertian lobe rather than a refraction path.
	rt.diffuseTransmissionFactor = material.diffuseTransmissionFactor();
	rt.diffuseTransmissionColor  = toGlm(material.diffuseTransmissionColorFactor());
	rt.diffuseTransmissionTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::DiffuseTransmission), "diffuseTransmissionMap", material.diffuseTransmissionMap(), nullptr);
	if (rt.diffuseTransmissionTexture)
	{
		rt.diffuseTransmissionTexture->packingChannel = 3; // A
		rt.diffuseTransmissionTexture->packingInvert  = false;
		rt.diffuseTransmissionTexture->packingScale   = 1.0f;
		rt.diffuseTransmissionTexture->packingBias    = 0.0f;
	}
	rt.diffuseTransmissionColorTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::DiffuseTransmissionColor), "diffuseTransmissionColorMap", material.diffuseTransmissionColorMap(), nullptr);

	return rt;
}

RtMaterial RtSceneBuilder::convertFloorMaterial(const SceneRuntime& runtime, const Material& material, bool reflectionsEnabled)
{
	RtMaterial rt;
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
	rt.baseColorTexture = extractTextureSample(nullptr, runtime, material, static_cast<int>(Material::TextureType::Albedo), nullptr, material.albedoMapPath(), nullptr);

	return rt;
}

void RtSceneBuilder::addFloorInstance(RtSceneSnapshot& snapshot, const SceneRuntime& runtime, const RtFloorParams& floor)
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
	// U is always the X extent (both up-axis conventions keep X as the first
	// floor axis - see Plane.cpp's XZ_YNormal vs XY_ZNormal); V is whichever
	// axis is the *other* floor axis for the current up-axis convention.
	const float extentU = static_cast<float>(bbox.getXSize());
	const float extentV = floor.cameraUpAxisZUp ? static_cast<float>(bbox.getYSize()) : static_cast<float>(bbox.getZSize());
	const float halfExtent = (std::max)(extentU, extentV) * 0.5f * kMarginFactor;
	// Degenerate/zero-size bounding box (e.g. nothing loaded yet) - fall back
	// to a small nominal floor rather than a zero-area (invisible) quad.
	const float safeHalfU = halfExtent > 1e-4f ? halfExtent : 1.0f;
	const float safeHalfV = safeHalfU;

	const float cx = floor.center.x();
	const float cu2 = floor.cameraUpAxisZUp ? floor.center.y() : floor.center.z();

	// Scale repeat counts so each tile comes out the same physical
	// world-space size as raster's, rather than reusing raster's repeat
	// count verbatim (which would stretch each tile to cover this quad's
	// smaller side length, making them look larger/fewer) or using a flat
	// 0..1 UV (no tiling at all).
	const float ptSideLength = 2.0f * safeHalfU; // square floor - same for U and V
	const float sizeRatio = floor.rasterFloorExtent > 1e-4f ? (ptSideLength / floor.rasterFloorExtent) : 1.0f;
	const float effectiveRepeatS = floor.texRepeatS * sizeRatio;
	const float effectiveRepeatT = floor.texRepeatT * sizeRatio;

	RtMeshGeometry geom;
	geom.vertices.resize(4);
	// Plane's own convention (see Plane.cpp buildMesh()): XZ_YNormal (Y-up)
	// uses (x, zlevel, z); XY_ZNormal (Z-up) uses (x, y, zlevel). Normal sign
	// is irrelevant either way - tracePixel() already flips the shading
	// normal to face whichever side a ray actually hits.
	for (int corner = 0; corner < 4; ++corner)
	{
		// Corner order: (-U,-V), (+U,-V), (+U,+V), (-U,+V) - a simple
		// quad fan, winding doesn't matter since Embree/our BVH don't cull.
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

	const uint32_t index = static_cast<uint32_t>(snapshot.meshes.size());
	snapshot.meshes.push_back(std::move(geom));
	snapshot.materials.push_back(convertFloorMaterial(runtime, floor.floorMesh->getMaterial(), floor.reflectionsEnabled));

	RtInstance instance;
	instance.meshIndex     = index;
	instance.materialIndex = index;
	instance.localToWorld  = glm::mat4(1.0f); // vertices already built in absolute world space above
	snapshot.instances.push_back(instance);
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

		snapshot->meshes.push_back(convertGeometry(mesh));
		snapshot->materials.push_back(convertMaterial(mesh, runtime));

		RtInstance instance;
		instance.meshIndex     = index;
		instance.materialIndex = index; // 1:1 for v1 - see RtSceneSnapshot.h comment on deferred instance-sharing
		instance.localToWorld  = toGlm(mesh->combinedRenderTransform());
		snapshot->instances.push_back(instance);
	}

	if (floor && floor->groundMode == GroundMode::Floor)
		addFloorInstance(*snapshot, runtime, *floor);

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

	// Mirrors the raster camera convention (view vector = normalize(cameraPos
	// - fragPos), see main_scene.frag) so primary rays align with what's on
	// screen - see Camera.h/getRenderPosition() for why that getter specifically.
	snapshot->camera.position    = toGlm(camera.getRenderPosition());
	snapshot->camera.forward     = toGlm(camera.getViewDir());
	snapshot->camera.right       = toGlm(camera.getRightVector());
	snapshot->camera.up          = toGlm(camera.getUpVector());
	snapshot->camera.aspectRatio = aspectRatio;

	// Read the vertical scale factor straight out of the projection matrix
	// (element [1][1] of both a standard OpenGL perspective matrix - where it
	// equals 1/tan(fovY/2) - and a symmetric ortho matrix - where it equals
	// 2/(top-bottom), i.e. 1/halfHeight) rather than recomputing it from
	// Camera::getFOV()/getAspectRatio() - see RtCamera's comment for why.
	snapshot->camera.orthographic = (camera.getProjectionType() == Camera::ProjectionType::ORTHOGRAPHIC);
	const QMatrix4x4 proj = camera.getProjectionMatrix();
	const float p11 = proj(1, 1);
	const float verticalScale = (std::abs(p11) > 1e-8f) ? (1.0f / p11) : 1.0f;
	if (snapshot->camera.orthographic)
		snapshot->camera.orthoHalfHeight = verticalScale;
	else
		snapshot->camera.tanHalfFovY = verticalScale;

	if (environment)
		snapshot->environment = *environment;

	return snapshot;
}
