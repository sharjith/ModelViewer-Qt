#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
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

// Everything RtSceneBuilder needs to populate the path tracer's analytic
// infinite plane, gathered in one place rather than as a long parameter list
// on build(). floorMesh is only consulted for its Material (see
// convertFloorMaterial()) - unlike the raster floor there is no PT mesh or
// extent to build here, just a procedurally intersected plane with a
// material/shadow-catcher description.
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
	// floorTexRepeatS()/T()) - still needed by the temporary GPU mesh-backed
	// fallback so it preserves existing floor tiling until OptiX moves to the
	// shared analytic infinite plane too.
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

	// shadowCatcherEnabled mirrors GroundMode::InfinitePlane being selected
	// (its own radio button in the Visualization panel's Ground section,
	// mutually exclusive with None/Floor/Grid) - ViewportWidget derives this
	// bool from the selected GroundMode at the PT-snapshot-build call site;
	// RtSceneBuilder itself only ever sees "Floor mode + this bool", never
	// the InfinitePlane enum value. Darkness/BaseColor/Metalness/Roughness
	// mirror SceneRenderController::shadowCatcherDarkness()/BaseColor()/
	// Metalness()/Roughness() (the panel's Shadow Catcher Settings group) -
	// see RtMaterial::isShadowCatcher's doc comment for what these drive.
	// Only meaningful for the path tracer; raster has no equivalent.
	// BaseColor/Metalness/Roughness default to NVIDIA's own vk_gltf_renderer
	// defaults (mid-grey, non-metal, medium roughness) for the same reason
	// theirs do - a plausible, unremarkable default GI contributor while the
	// shadow-catcher continuation bounce is active.
	bool shadowCatcherEnabled    = false;
	float shadowCatcherDarkness = 0.5f;
	QVector3D shadowCatcherBaseColor = QVector3D(0.5f, 0.5f, 0.5f);
	float shadowCatcherMetalness = 0.0f;
	float shadowCatcherRoughness = 0.5f;
};

// Fixed (glTF-spec-mandated, not user-configurable) channel packing for a
// texture slot whose packing can't be looked up via Material::packingFor() -
// e.g. sheenRoughnessMap's alpha channel, clearcoatMap's red channel. Passed
// into extractTextureSample() so the packing is baked in before the
// RtTextureSample is created/cached, rather than mutated on the returned
// object afterwards - a mutate-after-return pattern is incompatible with
// sharing one cached instance across multiple call sites (see
// RtSceneBuilder::extractTextureSample()'s dedup-cache doc comment).
struct RtPackingOverride
{
	int channel   = -1;
	bool invert   = false;
	float scale   = 1.0f;
	float bias    = 0.0f;
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
	// floor (see RtFloorParams) populates the path tracer's analytic infinite
	// plane when floor->groundMode == GroundMode::Floor. The raster path still
	// owns the visible finite floor/grid meshes; path tracing consumes only
	// the plane height/material/shadow-catcher description and intersects it
	// procedurally at render time. floor may be nullptr (no infinite plane) or
	// floor->floorMesh may be nullptr (material unavailable, e.g. before the
	// viewport's first layout pass has created it) - both are silently
	// skipped.
	// shadowsEnabled/selfShadowsEnabled mirror the Visualization panel's
	// "Shadows"/"Self Shadows" checkboxes (SceneRenderController::
	// shadowsEnabled()/selfShadowsEnabled()) - see RtSceneSnapshot.h's
	// fields of the same name for what they control.
	// Extracted from build()'s own camera-conversion code so a camera-only
	// update (see RtOptixPathTracingSession::updateCamera()) can build a fresh
	// RtCamera without paying for a full build() call (geometry/material/
	// environment-cubemap capture etc, all irrelevant to a pure camera move).
	static RtCamera buildCamera(const Camera& camera, float aspectRatio);

	static std::shared_ptr<RtSceneSnapshot> build(
		const SceneRuntime& runtime,
		const Camera& camera,
		float aspectRatio,
		const std::vector<GPULight>& lights,
		uint64_t revisionId,
		const RtEnvironment* environment = nullptr,
		const RtFloorParams* floor = nullptr,
		bool shadowsEnabled = true,
		bool selfShadowsEnabled = true);

private:
	// Identifies a fully-resolved RtTextureSample (source pixels + every
	// baked-in per-usage parameter) well enough that two extractTextureSample()
	// calls producing an identical key are guaranteed to want identical output -
	// see extractTextureSample()'s own doc comment for why this exists and what
	// it deliberately does NOT catch (independently-decoded copies of the same
	// source image).
	struct TextureDedupKey
	{
		qint64 imageCacheKey = 0;
		float uvScaleX = 1.0f, uvScaleY = 1.0f;
		float uvOffsetX = 0.0f, uvOffsetY = 0.0f;
		float uvRotation = 0.0f;
		int texCoordIndex = 0;
		unsigned int wrapS = 0, wrapT = 0;
		int packingChannel = -1;
		bool packingInvert = false;
		float packingScale = 1.0f;
		float packingBias = 0.0f;

		bool operator==(const TextureDedupKey& o) const
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
	struct TextureDedupKeyHash
	{
		size_t operator()(const TextureDedupKey& k) const
		{
			size_t h = std::hash<qint64>{}(k.imageCacheKey);
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
	// Keyed and populated per-build() call (see build()'s local variable) -
	// never persisted across calls, so a texture edited/reloaded between two
	// builds is never at risk of serving a stale cached copy.
	using TextureDedupCache = std::unordered_map<TextureDedupKey, std::shared_ptr<RtTextureSample>, TextureDedupKeyHash>;

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
	// applyRadialFade/fadeCenter/fadeUpAxisZUp/floorRadius drive
	// RtMaterial::hasRadialAlphaFade (see its own doc comment) - only
	// meaningful (and only ever passed true) when shadowCatcherEnabled is
	// false, since the shadow-catcher gate already substitutes background
	// radiance directly and has no use for a separate fade.
	static RtMaterial convertFloorMaterial(const SceneRuntime& runtime, const Material& material, bool reflectionsEnabled,
		bool shadowCatcherEnabled, float shadowCatcherDarkness, const QVector3D& shadowCatcherBaseColor,
		float shadowCatcherMetalness, float shadowCatcherRoughness, TextureDedupCache& dedupCache,
		bool applyRadialFade, const QVector3D& fadeCenter, bool fadeUpAxisZUp, float floorRadius);

	// Fills the PT-only analytic infinite plane description from the app's
	// current floor controls/material. CPU uses this directly; GPU keeps the
	// existing mesh-backed fallback until its own analytic-plane port lands.
	static void fillInfinitePlane(RtSceneSnapshot& snapshot, const SceneRuntime& runtime, const RtFloorParams& floor, TextureDedupCache& dedupCache);

	// Existing mesh-backed PT floor path, retained temporarily so the OptiX
	// backend keeps its current behavior until it is ported to the analytic
	// infinite plane as well.
	static void addFloorInstance(RtSceneSnapshot& snapshot, const SceneRuntime& runtime, const RtFloorParams& floor, TextureDedupCache& dedupCache);

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
	static RtMaterial convertMaterial(const SceneMesh* mesh, const SceneRuntime& runtime, TextureDedupCache& dedupCache);
	// dedupCache: many meshes/materials in a scene commonly reference the very
	// same underlying texture (a shared glTF material used by dozens of
	// instances is the common case, not the exception) - without this cache,
	// every one of those call sites independently re-decoded the QImage to
	// RGBA8888, copied its pixels into a brand-new RtTextureSample, and
	// rebuilt its full mip chain, and RtOptixSceneTracer's own upload cache
	// (keyed on RtTextureSample*, see its textureCache/textureMipCache) then
	// saw N distinct pointers and uploaded N redundant copies to VRAM too.
	// Keyed on QImage::cacheKey() (identifies the same underlying decoded
	// pixel buffer without hashing pixels) plus every parameter baked into
	// the resulting RtTextureSample (UV transform, wrap mode, channel
	// packing) - see TextureDedupKey. Deliberately does NOT catch two
	// independently-decoded QImages that merely contain identical pixels
	// (e.g. two SceneMesh::textures() copies loaded from the same file at
	// import time but never sharing a QImage buffer) - only the common
	// same-Material/same-cache-entry sharing case, which is what actually
	// drives the duplication in real scenes.
	// packingOverride: when non-null, used verbatim instead of
	// material.packingFor(packingKey) - required for the ~10 texture slots
	// whose channel packing is glTF-spec-fixed rather than user-configurable
	// (sheenRoughnessMap alpha, clearcoatMap red, etc.). These used to be
	// applied by mutating the returned RtTextureSample after the call
	// returned, which is exactly the kind of use-after-cache mutation the
	// dedup cache above cannot tolerate (a second, unrelated call site
	// sharing that same cached instance would see the mutation too).
	static std::shared_ptr<RtTextureSample> extractTextureSample(
		const SceneMesh* mesh, const SceneRuntime& runtime, const Material& material,
		int textureType, const char* meshTextureTypeKey, const QString& materialMapPath,
		const char* packingKey, TextureDedupCache& dedupCache,
		const RtPackingOverride* packingOverride = nullptr);
};
