#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

// ---------------------------------------------------------------------------
// RtSceneSnapshot
//
// Immutable, Qt/GL-free flattened copy of the scene, consumed by the CPU path
// tracer's worker threads. Built once per revision by RtSceneBuilder, which is
// the *only* bridge between the live, mutable SceneRuntime and the tracer -
// worker threads never touch SceneRuntime/SceneMesh/Material directly, so
// there is no locking and no risk of reading half-updated scene state while a
// trace is in flight.
//
// A new snapshot replaces the old one wholesale when geometry/material/
// visibility changes; camera movement alone does not rebuild a snapshot, it
// only resets accumulation (see RtFrameAccumulator).
// ---------------------------------------------------------------------------

// Local-space vertex (pre-transform; RtInstance::localToWorld places it in
// world space). Mirrors the subset of Vertex (MeshVertex.h) the tracer needs -
// position/normal/all 4 UV channels (a texture's KHR-declared texCoordIndex
// can reference any of them; hardcoding channel 0 for every texture silently
// samples the wrong UV set whenever a material uses a non-zero channel).
struct RtVertex
{
	glm::vec3 position{ 0.0f };
	glm::vec3 normal{ 0.0f, 1.0f, 0.0f };
	glm::vec2 texCoords[4]{};

	// COLOR_0-style per-vertex color (RGBA, linear per glTF spec - no sRGB
	// conversion needed, unlike textures). Always (1,1,1,1) - true identity,
	// safe to unconditionally multiply - when the source mesh has no vertex
	// color attribute; RtSceneBuilder is responsible for that gating since
	// Vertex::Color itself is otherwise unspecified data, not implicitly white.
	glm::vec4 color{ 1.0f };

	// For normal mapping - mirrors Vertex::Tangent/Bitangent (MeshVertex.h).
	// Zero-length (the default) signals "no tangent data", matching the
	// raster shader's own hasTangents = length(v_tangent) > 0.01 check in
	// calcBumpedNormal() (main_scene.frag).
	glm::vec3 tangent{ 0.0f };
	glm::vec3 bitangent{ 0.0f };
};

// One unique mesh's local-space triangle geometry. v1 does not deduplicate
// identical geometry referenced by multiple instances (this codebase's
// SceneMesh model has no such sharing today - each SceneMesh already owns its
// own full vertex copy) - every visible SceneMesh contributes exactly one
// RtMeshGeometry + one RtInstance. True instance-sharing (hashing identical
// geometry to reuse one Embree BLAS across repeated parts) is a perf
// optimization deferred to the large-assembly tuning pass, not a v1
// correctness requirement.
struct RtMeshGeometry
{
	std::vector<RtVertex> vertices;
	std::vector<uint32_t> indices; // triangle list, 3 indices per triangle
};

// CPU-resident RGBA8 pixel buffer copied out of a Material::Texture's QImage
// at snapshot-build time, plus its KHR_texture_transform and channel-packing
// metadata verbatim. Decoding which channel(s) to read (e.g. metallic in B,
// roughness in G) is deferred to the BSDF evaluation in CpuPathTracer - this
// struct only carries the raw data forward.
struct RtTextureSample
{
	std::vector<uint8_t> rgba8; // width * height * 4, row-major, no padding
	int width  = 0;
	int height = 0;

	int       texCoordIndex = 0;
	glm::vec2 uvScale{ 1.0f };
	glm::vec2 uvOffset{ 0.0f };
	float     uvRotation = 0.0f;

	// Mirrors Material::ChannelPacking without depending on Material.h/Qt.
	int   packingChannel = 0;      // 0=R, 1=G, 2=B, 3=A, -1=none
	bool  packingInvert  = false;
	float packingScale   = 1.0f;
	float packingBias    = 0.0f;

	// Mirrors the raw GLenum values of Material::Texture::wrapS/wrapT (GL_REPEAT
	// 0x2901, GL_CLAMP_TO_EDGE 0x812F, GL_MIRRORED_REPEAT 0x8370) without
	// depending on a GL header here - RtSceneBuilder.cpp copies them verbatim,
	// CpuPathTracer.cpp compares against the same numeric constants. A
	// texture authored as a single centered decal (e.g. CLAMP_TO_EDGE) tiles
	// repeatedly across the whole surface if wrap mode is ignored and REPEAT
	// is assumed unconditionally - this was a real, previously-unhandled gap.
	unsigned int wrapS = 0x2901; // GL_REPEAT
	unsigned int wrapT = 0x2901; // GL_REPEAT
};

// v1 shading vocabulary: diffuse + metallic-roughness + emissive + basic
// dielectric Fresnel, matching the raster PBR mode's core parameters.
// Transmission/clearcoat/sheen (which Material already tracks) are deferred
// to a fast-follow rather than chasing full material parity in v1.
// KHR_materials_ior/KHR_materials_specular (ior/specularFactor/
// specularColorFactor) were added once the floor's reflection work made it
// clear the hardcoded 0.04 dielectric F0 was itself a v1 simplification
// worth removing - see CpuPathTracer.cpp's evaluateSurface()/computeF0F90().
struct RtMaterial
{
	glm::vec3 baseColor        = glm::vec3(0.8f);
	float     metalness        = 0.0f;
	float     roughness        = 0.5f;
	glm::vec3 emissive         = glm::vec3(0.0f);
	float     emissiveStrength = 0.0f;
	float     opacity          = 1.0f;

	// glTF occlusionTexture.strength - lerps between "no AO" (1.0) and the
	// sampled AO texture value; see evaluateSurface()'s ao computation, which
	// mirrors main_scene.frag's "clamp(mix(1.0, texAO, occlusionStrength),
	// 0.0001, 1.0)" exactly.
	float occlusionStrength = 1.0f;

	// KHR_materials_ior - replaces the fixed 1.5/0.04 dielectric assumption.
	float ior = 1.5f;

	// KHR_materials_specular - scales dielectric reflectance strength/tint;
	// has no effect on metals (see computeF0F90() in CpuPathTracer.cpp,
	// ported from computeDielectricF0()/computeF90() in main_scene.frag).
	float     specularFactor      = 1.0f;
	glm::vec3 specularColorFactor = glm::vec3(1.0f);

	// Null when the material has no map for that slot.
	std::shared_ptr<RtTextureSample> baseColorTexture;
	std::shared_ptr<RtTextureSample> metallicTexture;
	std::shared_ptr<RtTextureSample> roughnessTexture;
	std::shared_ptr<RtTextureSample> normalTexture;
	std::shared_ptr<RtTextureSample> emissiveTexture;
	std::shared_ptr<RtTextureSample> aoTexture;

	// KHR_materials_specular's per-pixel maps - specularTexture's alpha
	// channel scales specularFactor, specularColorTexture's RGB (sRGB-
	// encoded) tints specularColorFactor (main_scene.frag: "params.
	// specularFactor *= texture(specularFactorMap,...).a" / "params.
	// specularColor *= sRGBToLinear(texture(specularColorMap,...).rgb)").
	// Without these, materials that author their specular strength/tint
	// mostly through a texture (flat factors left at their 1.0 defaults)
	// rendered as a uniformly mirror-bright surface instead of the mostly-
	// matte-with-a-patterned-highlight look raster shows.
	std::shared_ptr<RtTextureSample> specularTexture;
	std::shared_ptr<RtTextureSample> specularColorTexture;

	// KHR_materials_clearcoat - a second, independent GGX lobe layered on
	// top of the base material (its own normal, its own roughness, always
	// dielectric F0 derived from ior - see main_scene.frag's
	// evaluateClearcoatDirect()/evaluateClearcoatIBL() and the final
	// baseColor/clearcoat mix in composeLighting()/similar).
	float clearcoat          = 0.0f;
	float clearcoatRoughness = 0.0001f; // matches main_scene.frag's clamp(..., 0.0001, 1.0) floor
	std::shared_ptr<RtTextureSample> clearcoatTexture;          // R channel scales clearcoat
	std::shared_ptr<RtTextureSample> clearcoatRoughnessTexture; // G channel scales clearcoatRoughness
	std::shared_ptr<RtTextureSample> clearcoatNormalTexture;

	// KHR_materials_sheen - an additive fabric/velvet-like grazing-angle
	// retroreflection lobe (Charlie NDF), on top of the base layer. sheenColor
	// (0,0,0) means "no sheen" per spec - see evaluateSurface()'s
	// length(sheenColor)<=0 early-out, ported from main_scene.frag's
	// calculateSheen()/evaluateSheenDirect(). v1 only covers direct (punctual)
	// lighting - unlike clearcoat, sheen's IBL/energy-conservation terms in
	// main_scene.frag depend on baked LUT textures (sheenELUT/charlieLUT/
	// sheenPrefilterMap) that have no CPU-side equivalent in this tracer, so
	// indirect/environment sheen and the base-layer energy-compensation
	// dampening are both deliberately skipped rather than approximated.
	glm::vec3 sheenColor     = glm::vec3(0.0f);
	float     sheenRoughness = 0.0001f;
	std::shared_ptr<RtTextureSample> sheenColorTexture;
	std::shared_ptr<RtTextureSample> sheenRoughnessTexture; // A channel scales sheenRoughness

	// KHR_materials_anisotropy - stretches the specular lobe along a tangent-
	// space direction (brushed metal, hair, records...). anisotropyTexture's
	// RG channels encode a [-1,1] direction (decoded in CpuPathTracer's
	// decodeAnisotropyTexture(), ported from main_scene.frag), B channel
	// scales anisotropyStrength - not a simple single-channel pack like
	// metallic/roughness/ao, so no packingChannel is set on it (consumed as
	// raw RGB, like a normal map).
	float anisotropyStrength = 0.0f;
	float anisotropyRotation = 0.0f; // radians
	std::shared_ptr<RtTextureSample> anisotropyTexture;

	// KHR_materials_iridescence - thin-film interference (evalIridescence()
	// in CpuPathTracer.cpp, ported from main_scene.frag). iridescenceTexture
	// is a simple R-channel factor scale (packingChannel=0, like clearcoat's
	// factor map); iridescenceThicknessTexture's G channel is remapped
	// min..max via packingScale=(max-min)/packingBias=min - see
	// RtSceneBuilder::convertMaterial() - since that's exactly the linear
	// form applyChannelPacking() already computes, reproducing main_scene.
	// frag's "mix(thicknessMin, thicknessMax, texG)" without needing a
	// separate non-channel-packing code path.
	float iridescenceFactor       = 0.0f;
	float iridescenceIor          = 1.3f;
	float iridescenceThicknessMin = 100.0f;
	float iridescenceThicknessMax = 400.0f;
	std::shared_ptr<RtTextureSample> iridescenceTexture;
	std::shared_ptr<RtTextureSample> iridescenceThicknessTexture;
};

// One placed copy of a mesh in the scene. meshIndex/materialIndex index into
// RtSceneSnapshot::meshes/materials.
struct RtInstance
{
	uint32_t  meshIndex     = 0;
	uint32_t  materialIndex = 0;
	glm::mat4 localToWorld{ 1.0f };
};

// Flattened form of GPULight (PunctualLights.h) - position/direction are
// already world-space, copied verbatim from SceneGraph::buildEnabledLightList()
// (the same list the raster UBO uses), so raster and path-traced lighting
// stay in sync by construction.
struct RtLight
{
	int       type = 1; // matches LightType: 0=Directional, 1=Point, 2=Spot
	glm::vec3 position{ 0.0f };
	glm::vec3 direction{ 0.0f, -1.0f, 0.0f };
	glm::vec3 color{ 1.0f };
	float     intensity    = 1.0f;
	float     range        = 0.0f;
	float     innerConeCos = 1.0f;
	float     outerConeCos = 0.7f;
};

// Camera snapshot for primary-ray generation. Deliberately mirrors the
// raster camera convention (view vector = normalize(cameraPos - fragPos),
// see main_scene.frag) so primary rays align with what's on screen.
//
// tanHalfFovY/orthoHalfHeight are derived directly from the *actual*
// projection matrix (element [1][1]) rather than recomputed from Camera::
// getFOV()/getAspectRatio() - Camera::updateProjectionMatrix() applies a
// "Hor+" adjustment that widens the vertical FOV for portrait-aspect
// viewports (keeps horizontal FOV constant instead), and re-deriving that
// logic independently here would silently drift out of sync with it (this
// mismatch was the cause of an early "model renders too small" bug - fixed
// by reading the matrix directly instead of reimplementing its FOV math).
struct RtCamera
{
	glm::vec3 position{ 0.0f };
	glm::vec3 forward{ 0.0f, 0.0f, -1.0f };
	glm::vec3 right{ 1.0f, 0.0f, 0.0f };
	glm::vec3 up{ 0.0f, 1.0f, 0.0f };
	float     aspectRatio = 1.0f;

	bool  orthographic    = false;
	float tanHalfFovY     = 0.4142f; // perspective only (~45 deg)
	float orthoHalfHeight = 1.0f;    // orthographic only, world units
};

// CPU-resident copy of the scene's environment cubemap, read back face-by-face
// from the actual GPU texture the raster skybox/reflections sample
// (SceneRenderController::captureEnvironmentCubemapCPU()). Using the real
// cubemap - rather than caching whatever HDR file was last loaded - means
// this works regardless of how the environment was authored (single
// equirectangular HDR, 6 separate face images, a strip format, ...): it's
// always exactly what's currently shown as the background/used for
// reflections, since GPU readback has no notion of "which loader populated
// it". faces[i] is faceSize*faceSize*3 RGB float triplets, row-major, in
// GL_TEXTURE_CUBE_MAP_POSITIVE_X+i order - same texel layout the GPU sampler
// itself reads, so CpuPathTracer's direction->face+uv lookup must use the
// standard OpenGL cubemap face-selection convention to stay consistent.
// faceSize == 0 means no environment map is loaded, in which case the tracer
// falls back to a flat ambient miss color.
struct RtEnvironment
{
	// Sharp (mip 0), used for indirect/reflection bounces (bounce > 0) -
	// mirror-like surfaces should reflect full environment detail regardless
	// of whether the background sphere itself is toggled on.
	std::vector<float> faces[6];
	int faceSize = 0;

	// Whether the background sphere is actually toggled on (Visualization
	// panel's "Sky Box" checkbox - SceneRenderController::skyBoxEnabled()).
	// Mirrors raster: turning this off hides the visible background but does
	// NOT disable the environment as a reflection/lighting source (that's a
	// separate "Reflections" toggle raster already has, left alone here).
	bool showBackground = false;

	// Raster's plain background gradient, shown for bounce == 0 misses
	// instead of the environment when showBackground is false - so path-
	// traced and raster agree on what "no skybox" looks like.
	glm::vec3 fallbackTopColor{ 0.5f };
	glm::vec3 fallbackBottomColor{ 0.5f };
	int       fallbackGradientStyle = 0;

	// ViewportWidget::drawSkyBox() rotates the skybox cube's *geometry* by
	// upAxisConventionRotation * Rot(90,X) [raw/non-prefiltered env map only]
	// * Rot(skyBoxZRotation,Y) before drawing it, while skybox.frag samples
	// the cubemap using the cube's *unrotated local* vertex position
	// (texCoords = vertexPosition, see skybox.vert/frag) - so what a given
	// world-space camera ray direction actually samples in the cubemap is
	// inverse(that rotation) * direction, not the raw direction. Without
	// undoing it, our own direct world-space cubemap lookup samples a
	// different, rotated region of the same texture than raster's background
	// shows - CpuPathTracer's sampleEnvironmentBackground() applies the
	// inverse rotation using these two values before the lookup.
	bool  cameraUpAxisZUp          = false;
	float skyBoxZRotationDegrees   = 0.0f;
};

struct RtSceneSnapshot
{
	std::vector<RtMeshGeometry> meshes;
	std::vector<RtInstance>     instances;
	std::vector<RtMaterial>     materials;
	std::vector<RtLight>        lights;
	RtCamera                    camera;
	RtEnvironment                environment;

	// Bumped every time a new snapshot is built. Lets the path tracer /
	// accumulator detect that geometry/material/visibility actually changed
	// (as opposed to just the camera moving, which resets accumulation
	// without needing a new snapshot or acceleration-structure rebuild).
	uint64_t revisionId = 0;
};
