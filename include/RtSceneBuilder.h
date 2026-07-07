#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <QString>
#include <QVector3D>

#include "BoundingBox.h"
#include "RenderEnums.h"
#include "RtSceneSnapshot.h"

class SceneRuntime;
class SceneMesh;
class RenderableMesh;
class Camera;
class Material;
struct GPULight;

// Everything RtSceneBuilder needs to add a floor instance, gathered in one
// place rather than as a long parameter list on build(). floorMesh is only
// consulted for its Material (see convertFloorMaterial()) - its actual
// geometry/extent is NOT reused: the raster floor's extent is an aesthetic
// "fade to background" area (_floorSize * floorSizeFactor, ~5x the scene
// bounding box by default - see ViewportWidget::updateFloorGeometry()), far
// larger than path tracing needs. A ray tracer gets no visual benefit from
// that oversized area (nothing beyond the model ever casts/receives a
// shadow there) and pays real BVH/intersection cost for it, so the
// path-traced floor is instead built here as a fresh, small quad sized just
// beyond sceneBoundingBox.
struct RtFloorParams
{
	const RenderableMesh* floorMesh = nullptr; // material source only
	GroundMode groundMode           = GroundMode::None;
	BoundingBox sceneBoundingBox;              // ViewportInteractionController::boundingBox()
	QVector3D center;                          // world-space X/Z (Y-up) or X/Y (Z-up) center - ViewportWidget::_floorCenter
	float planeLevel     = 0.0f;               // world-space height along the up axis - ViewportWidget::_floorPlaneZ
	bool cameraUpAxisZUp = false;

	// Raster's actual floor side length (CoordinateSystemHelper::
	// groundPlaneExtent()) and its texRepeatS/T (SceneRenderController::
	// floorTexRepeatS()/T()) - needed to scale UVs so individual tiles come
	// out the same physical world-space size on the path-traced floor's much
	// smaller quad, rather than either reusing raster's repeat count verbatim
	// (which would make tiles look larger/fewer here) or a flat 0..1 UV
	// (no tiling at all).
	float rasterFloorExtent = 0.0f;
	float texRepeatS        = 1.0f;
	float texRepeatT        = 1.0f;

	// Mirrors the Visualization panel's "Reflections" checkbox
	// (SceneRenderController::reflectionsEnabled()), which raster uses to
	// gate its own (fake, planar-mirror) floor reflection pass. The path-
	// traced floor's "reflection" is really just a deliberately-lowered
	// roughness override (see convertFloorMaterial()) - when this is false,
	// that override is skipped so the floor falls back to its actual
	// material roughness (no visible reflection), matching raster's toggle.
	bool reflectionsEnabled = true;
};

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
	// floor (see RtFloorParams) adds the app's ground plane as one extra ray-
	// traced instance when floor->groundMode == GroundMode::Floor - the
	// raster path relies on a shadow-map pass for floor shadows/AO, which
	// doesn't apply in path-traced mode; giving the floor real geometry
	// instead means it picks up correct ray-traced shadows/reflections for
	// free via the same NEE/bounce machinery every other mesh already uses.
	// floor may be nullptr (no floor instance added) or floor->floorMesh may
	// be nullptr (material unavailable, e.g. before the viewport's first
	// layout pass has created it) - both are silently skipped.
	static std::shared_ptr<RtSceneSnapshot> build(
		const SceneRuntime& runtime,
		const Camera& camera,
		float aspectRatio,
		const std::vector<GPULight>& lights,
		uint64_t revisionId,
		const RtEnvironment* environment = nullptr,
		const RtFloorParams* floor = nullptr);

private:
	static RtMeshGeometry convertGeometry(const SceneMesh* mesh);

	// Floor's RenderableMesh has no SceneMesh/Assimp material behind it (it's
	// a procedural Plane with a plain Material set directly via
	// ViewportWidget::applyFloorPlaneMaterialSettings()/
	// syncFloorPlaneAlbedoTexture()) - a separate, simpler conversion than
	// convertMaterial() below, which reuses extractTextureSample() with a
	// null mesh/meshTextureTypeKey (safe - see extractTextureSample()'s tier-1
	// guard) since the optional albedo texture it sets already carries
	// imageData directly, with no SceneMesh::textures() or texCache tier
	// needed.
	//
	// Roughness is overridden lower than the raster floor's actual Material
	// (0.45 by default) rather than reusing it verbatim: raster's floor
	// reflection is a wholly separate, non-physical planar-mirror render
	// pass (ViewportWidget's isReflectedPass, blended via the floorSpecular
	// Scale/floorFresnelDampen shader uniforms - see main_scene.frag) that
	// never consults Material::roughness/metalness at all, so a real BRDF
	// path tracer using that value as-is produces a much duller floor than
	// what raster's fake reflection shows. Lowering roughness is the only
	// physically-grounded lever available to make the *real* GGX specular
	// lobe visibly reflective without introducing a second, fake reflection
	// mechanism into the tracer.
	static RtMaterial convertFloorMaterial(const SceneRuntime& runtime, const Material& material, bool reflectionsEnabled);

	// Builds a fresh, minimal 4-vertex/2-triangle quad sized just beyond
	// floor.sceneBoundingBox (see RtFloorParams) rather than reusing
	// floor.floorMesh's actual (much larger, aesthetic-fade-out) geometry -
	// a flat floor gets no shading-quality benefit from being subdivided for
	// ray tracing (unlike rasterization, intersection math doesn't care how
	// many triangles a flat plane is cut into), so there's no reason to pay
	// for the raster extent's far larger surface area/BVH footprint here.
	static void addFloorInstance(RtSceneSnapshot& snapshot, const SceneRuntime& runtime, const RtFloorParams& floor);

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
