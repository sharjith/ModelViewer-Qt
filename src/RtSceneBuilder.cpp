#include "RtSceneBuilder.h"

#include "SceneRuntime.h"
#include "SceneMesh.h"
#include "Material.h"
#include "Camera.h"
#include "PunctualLights.h"

#include <glm/gtc/type_ptr.hpp>

#include <QImage>

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
	rt.occlusionStrength = material.occlusionStrength();

	rt.baseColorTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::Albedo), "albedoMap", material.albedoMapPath(), nullptr);
	rt.metallicTexture  = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::Metallic), "metallicMap", material.metallicMapPath(), "metallic");
	rt.roughnessTexture = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::Roughness), "roughnessMap", material.roughnessMapPath(), "roughness");
	rt.normalTexture    = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::Normal), "normalMap", material.normalMapPath(), nullptr);
	rt.emissiveTexture  = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::Emissive), "emissiveMap", material.emissiveMapPath(), nullptr);
	rt.aoTexture        = extractTextureSample(mesh, runtime, material, static_cast<int>(Material::TextureType::AmbientOcclusion), "aoMap", material.aoMapPath(), "ao");

	return rt;
}

std::shared_ptr<RtSceneSnapshot> RtSceneBuilder::build(
	const SceneRuntime& runtime,
	const Camera& camera,
	float aspectRatio,
	const std::vector<GPULight>& lights,
	uint64_t revisionId,
	const RtEnvironment* environment)
{
	auto snapshot = std::make_shared<RtSceneSnapshot>();
	snapshot->revisionId = revisionId;

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
