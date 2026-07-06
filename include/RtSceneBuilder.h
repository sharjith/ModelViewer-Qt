#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <QString>

#include "RtSceneSnapshot.h"

class SceneRuntime;
class SceneMesh;
class Camera;
class Material;
struct GPULight;

// ---------------------------------------------------------------------------
// RtSceneBuilder
//
// Builds an immutable RtSceneSnapshot by walking the live SceneRuntime once
// and flattening its currently-visible meshes/materials, plus the current
// camera and the enabled light list, into plain data. This is the one-shot
// "flatten and freeze" step that decouples the path tracer's worker threads
// from the live, mutable scene graph - see RtSceneSnapshot.h for why that
// separation matters.
//
// Deliberately reads geometry/material data directly from SceneMesh/Material
// (both keep their CPU-side arrays resident for the object's lifetime - see
// project notes on RenderableMesh/Material texture retention) rather than
// through the glTF/MVF exporters, which are entangled with Assimp's own
// scene-graph construction and are not a reusable flattener.
// ---------------------------------------------------------------------------
class RtSceneBuilder
{
public:
	// lights must already be world-space and enabled-filtered, i.e. the same
	// flat list SceneGraph::buildEnabledLightList() produces for the raster
	// UBO - reusing it keeps raster and path-traced lighting in sync by
	// construction instead of re-deriving light transforms independently.
	//
	// environment is copied verbatim into the snapshot (may be nullptr, or
	// point to an RtEnvironment with width/height == 0, meaning "no
	// environment map loaded") - passed in rather than read from
	// SceneRenderController directly to keep this builder decoupled from
	// that (large, GL-heavy) header.
	static std::shared_ptr<RtSceneSnapshot> build(
		const SceneRuntime& runtime,
		const Camera& camera,
		float aspectRatio,
		const std::vector<GPULight>& lights,
		uint64_t revisionId,
		const RtEnvironment* environment = nullptr);

private:
	static RtMeshGeometry convertGeometry(const SceneMesh* mesh);

	// Takes the owning SceneMesh and the SceneRuntime, not just the Material,
	// because a texture's actual decoded pixel data can live in any of THREE
	// independent stores depending on how it was applied, and this app keeps
	// them out of sync with each other by design (each is optimized for its
	// own call path, not for being read back generically):
	//   1) Material's own internal per-TextureType array (Material::texture())
	//      - populated by the glTF/import path.
	//   2) SceneMesh::textures(), a *separate* per-mesh vector keyed by a type
	//      string ("albedoMap" etc.) - populated by user-applied textures via
	//      the Material Properties panel (SceneMesh::setTextureMaps()/
	//      syncTexturesFromMaterialIfNeeded()), but only with a GL texture id,
	//      never imageData.
	//   3) SceneRuntime::texCache(), a path-keyed cache of decoded QImages
	//      (ViewportWidget::getOrLoadTextureCached()) - populated by the
	//      predefined-material resolution path (resolveMaterialTextures()),
	//      which only ever writes a GL texture id onto the Material/Texture
	//      objects themselves, never imageData; the actual pixel data only
	//      exists in this cache, keyed by the source file path.
	// extractTextureSample() checks all three, in that order, and uses
	// whichever first has non-null imageData/image data for the requested slot.
	static RtMaterial convertMaterial(const SceneMesh* mesh, const SceneRuntime& runtime);
	static std::shared_ptr<RtTextureSample> extractTextureSample(
		const SceneMesh* mesh, const SceneRuntime& runtime, const Material& material,
		int textureType, const char* meshTextureTypeKey, const QString& materialMapPath,
		const char* packingKey);
};
