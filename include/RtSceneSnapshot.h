#pragma once

#include <cstdint>
#include <limits>
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
// One level of RtTextureSample's mip pyramid - see RtTextureSample::mips'
// doc comment.
struct RtTextureMipLevel
{
	std::vector<uint8_t> rgba8; // width * height * 4, row-major, no padding
	int width  = 0;
	int height = 0;
};

struct RtTextureSample
{
	std::vector<uint8_t> rgba8; // width * height * 4, row-major, no padding
	int width  = 0;
	int height = 0;

	// Box-filtered mip pyramid, built ONCE at texture-load time
	// (RtSceneBuilder::extractTextureSample()) - mips[0] duplicates the base
	// level above (same width/height/rgba8), mips[1] is half-resolution,
	// mips[2] quarter, etc., down to a 1x1 level. Without this, a path
	// tracer's plain bilinear sample at the texture's FULL resolution
	// aliases badly whenever a textured surface is minified (viewed from a
	// distance/oblique angle) - unlike raster's hardware mipmapping, there
	// is no other mechanism here to average away sub-texel detail smaller
	// than one pixel's footprint. See CpuPathTracer.cpp's computeTextureLod()/
	// sampleTexture()'s lod parameter for how this is consumed (a ray-cone-
	// style estimate of the hit's texel footprint, trilinearly blended
	// between the two nearest levels here). Deliberately built as a
	// self-contained, uniformly-indexable chain (duplicating the base into
	// mips[0]) rather than tacking "levels 1+" onto the existing width/
	// height/rgba8 fields, for simpler, branch-free level-N sampling code.
	std::vector<RtTextureMipLevel> mips;

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

	// KHR_materials_unlit - see CpuPathTracer.cpp's tracePixel() for the
	// short-circuit this triggers (baseColor+emissive only, no lighting of
	// any kind evaluated, matching main_scene.frag's unlit branch).
	bool      unlit            = false;

	// Base glTF alphaMode (OPAQUE/MASK/BLEND) - previously unimplemented in
	// this tracer entirely (opacity/alpha was never referenced anywhere in
	// CpuPathTracer.cpp), a pre-existing gap rather than something removed
	// during the KHR extension work. blendMode mirrors Material::BlendMode's
	// ordinal values verbatim (0=Opaque, 1=Masked, 2=Alpha/glTF BLEND - the
	// remaining Additive/Multiply values aren't part of glTF's alphaMode and
	// aren't handled here). alphaThreshold is glTF's alphaCutoff (MASK only).
	// opacityTexture uses the same user-configurable channel-packing as
	// metallic/roughness/ao (Material::packingFor("opacity")), mirroring
	// main_scene.frag's sampleOpacityMap(); when absent, evaluateSurface()
	// falls back to the base color texture's own alpha channel (matching
	// main_scene.frag's sampleFallbackOpacity() PBR-mode branch), then to
	// the flat opacity factor above if there's no texture at all.
	int       blendMode      = 0;
	float     alphaThreshold = 0.5f;
	std::shared_ptr<RtTextureSample> opacityTexture;

	// glTF's material.doubleSided (Material::twoSided()) - previously
	// unreferenced by either path tracer entirely, so EVERY material rendered
	// as if double-sided regardless of what was authored. Per the glTF spec,
	// a single-sided (false) material's back face should be treated as if it
	// doesn't exist (matching real-time back-face culling) - see
	// CpuPathTracer.cpp's hitBackface-gated pass-through and
	// RtOptixScene.cu's __anyhit__ah() optixIsFrontFaceHit() check for where
	// this is now honored. Defaults to true (double-sided), matching
	// Material::_twoSided's own default.
	bool      twoSided = true;

	// KHR_materials_pbrSpecularGlossiness - legacy alternate workflow to
	// metallic-roughness (glTF's ORIGINAL v1 material model, kept as an
	// archived extension for backward-compat content). When active, this
	// completely REPLACES baseColor/metalness/roughness/F0's normal
	// meaning - see CpuPathTracer.cpp's evaluateSurface() for the override
	// logic and evaluateDirectBRDF() for the mix()-based (not additive)
	// direct-lighting formula main_scene.frag uses for this workflow.
	// specularGlossinessTexture is packed RGB=specular color (sRGB),
	// A=glossiness (linear) - matching main_scene.frag's
	// specularGlossinessMap sampling.
	bool      useSpecGloss     = false;
	glm::vec3 diffuseColor     = glm::vec3(1.0f); // spec-gloss's baseColor equivalent
	glm::vec3 specGlossSpecularColor = glm::vec3(0.0f); // spec-gloss's F0 equivalent - a direct RGB color, not IOR-derived
	float     glossinessFactor = 1.0f; // inverse of roughness: roughness = 1 - glossinessFactor*(texture.a if present)
	std::shared_ptr<RtTextureSample> diffuseTexture;
	std::shared_ptr<RtTextureSample> specularGlossinessTexture;

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

	// glTF normalTexture.scale - dampens the tangent-space X/Y perturbation
	// before renormalizing (main_scene.frag's sampleMappedNormal(): "
	// tangentNormal.xy *= normalScale" - the Z/"how flat" component is never
	// scaled, only spec). Defaults to 1.0 (full-strength, glTF's own
	// default) when the material doesn't author one. CpuPathTracer's
	// applyNormalMap() previously never applied this at all - a real,
	// previously-unnoticed gap: a material authoring a deliberately gentle
	// bump (e.g. a fine, densely-tiled texture meant to read as a subtle
	// surface finish, scale well under 1.0) instead got the texture's full,
	// undamped tilt, perturbing the shading normal far more aggressively
	// than intended - which cascades into every angle-dependent
	// calculation that reads it (Fresnel, reflection direction, anything
	// KHR_materials_iridescence-driven), and reads as excess per-pixel
	// noise/graininess that no amount of sampling or environment-side
	// fixes can resolve, since the underlying normal itself is the thing
	// varying far more than authored.
	float normalScale = 1.0f;

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
	float clearcoatNormalScale = 1.0f; // same role as RtMaterial::normalScale above, for the coat's own normal map

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

	// KHR_materials_transmission + KHR_materials_volume - unlike raster
	// (which can only approximate a refracted view via a screen-space
	// "transmission framebuffer" sample plus an authored thicknessFactor
	// standing in for the object's true internal depth, since it has no
	// real scene geometry to trace through), this path tracer actually
	// refracts a continuing ray through the real mesh and measures the true
	// entry-to-exit distance for Beer-Lambert absorption - see
	// CpuPathTracer.cpp's transmission handling in tracePixel(). That means
	// thicknessFactor's VALUE (KHR_materials_volume's approximation input)
	// is deliberately not used for absorption depth - the real traced
	// distance replaces what it exists to estimate. Its mere presence
	// (thicknessFactor > 0, i.e. hasVolume below) IS still read, though: per
	// spec, KHR_materials_transmission without KHR_materials_volume means
	// the surface is implicitly "thin-walled" (thicknessFactor's own default
	// is 0) - light should pass straight through undeviated, with no
	// refraction bend and no interior to absorb through, not behave like a
	// real solid dielectric.
	float transmission = 0.0f;
	std::shared_ptr<RtTextureSample> transmissionTexture; // R channel scales transmission
	bool      hasVolume           = false; // KHR_materials_volume present (thicknessFactor > 0) - see above
	glm::vec3 attenuationColor    = glm::vec3(1.0f);
	float     attenuationDistance = std::numeric_limits<float>::infinity();

	// The actual authored thicknessFactor VALUE - unlike the specular
	// transmission path above (which deliberately ignores it in favor of
	// real traced entry-to-exit distance), KHR_materials_diffuse_
	// transmission's NEE contribution has no traced path to measure (it's
	// a single-point analytic term, not a continuing ray) - see
	// tracePixel()'s diffuse-transmission NEE handling, which uses this as
	// the Beer-Lambert distance the same way main_scene.frag's
	// computeVolumeThickness()/diffuseTransmissionThickness does. No
	// texture support (thicknessTexture) for this narrow use - main
	// transmission's real-traced-distance approach makes that unnecessary
	// there, and this is only a fallback for the one term that has no
	// alternative.
	float thicknessFactor = 0.0f;

	// KHR_materials_dispersion - per-channel IOR spread on transmission
	// (chromatic aberration/prism effect). No per-material texture in the
	// glTF spec, just a flat factor. See CpuPathTracer.cpp's transmission
	// handling for the hero-channel stochastic-selection approach used to
	// avoid tracing 3x rays per dispersive sample.
	float dispersion = 0.0f;

	// KHR_materials_diffuse_transmission - a translucent DIFFUSE material
	// (leaves, paper, thin curtains), distinct from KHR_materials_
	// transmission's specular/refractive glass model: light landing on the
	// front diffusely scatters through to the back (and vice versa) rather
	// than bending through a dielectric interface, so no ior/Fresnel/TIR
	// machinery is involved - see CpuPathTracer.cpp's NEE and diffuse-lobe
	// handling for how this splits the existing Lambertian response between
	// the front and back hemispheres instead of adding a new refraction
	// path. diffuseTransmissionFactor's texture uses the ALPHA channel
	// (glTF spec), diffuseTransmissionColorTexture uses RGB (sRGB) -
	// matching main_scene.frag's diffuseTransmissionMap/
	// diffuseTransmissionColorMap sampling.
	float     diffuseTransmissionFactor = 0.0f;
	glm::vec3 diffuseTransmissionColor  = glm::vec3(1.0f);
	std::shared_ptr<RtTextureSample> diffuseTransmissionTexture;      // A channel scales diffuseTransmissionFactor
	std::shared_ptr<RtTextureSample> diffuseTransmissionColorTexture; // RGB (sRGB) tints diffuseTransmissionColor
};

// One placed copy of a mesh in the scene. meshIndex/materialIndex index into
// RtSceneSnapshot::meshes/materials.
struct RtInstance
{
	uint32_t  meshIndex     = 0;
	uint32_t  materialIndex = 0;
	glm::mat4 localToWorld{ 1.0f };

	// NOTE: a negative-determinant localToWorld (e.g. glTF's
	// NegativeScaleTest.gltf) needs NO special handling here, unlike
	// raster's glFrontFace(GL_CW/GL_CCW) flip (RenderableMesh::render()).
	// Both path tracers intersect by transforming the RAY into object space
	// and testing winding there against the triangle's own local normal -
	// algebraically identical to the correct world-space test for ANY
	// invertible transform, reflections included (see RtEmbreeScene::
	// intersect()'s and RtOptixSceneTracer.cpp's IAS-build comments for the
	// derivation). An earlier version of this struct carried a
	// hasNegativeScale flag and both tracers applied an extra sign/flip
	// correction driven by it - confirmed WRONG by testing against
	// NegativeScaleTest.gltf (fixed the never-flipped positive-scale row,
	// broke the negative-scale row that got the extra flip) - removed.
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

	// SceneRenderController::envMapExposure() / Visualization panel's
	// "Env Map Exposure" control - main_scene.frag applies this at every
	// surface-side IBL sampling site (specular prefilter, diffuse
	// irradiance, transmission/clearcoat/sheen IBL - see envMapExposure's
	// uses there), separately from the whole-frame iblExposure the skybox/
	// present shaders apply. Only affects environment light REACHING a
	// surface, not the directly-visible background - see
	// CpuPathTracer's sampleEnvironmentMiss() (bounce > 0, i.e. every
	// surface-side environment contribution) vs sampleEnvironmentBackground()
	// (bounce == 0, the visible backdrop, which mirrors skybox.frag and does
	// NOT use this).
	float envMapExposure = 1.0f;

	// Fully diffuse-convolved cubemap (SceneRenderController::irradianceMap()
	// - the same one raster's own diffuse IBL term samples directly, no LOD),
	// captured alongside the raw map. Used for indirect bounces whose most
	// recent surface interaction was a diffuse (cosine-weighted) lobe -
	// stochastically sampling many random directions into the SHARP raw
	// cubemap would eventually average to the same value (that's literally
	// what irradiance convolution precomputes), but converges far slower,
	// since a full-detail HDRI has much more high-frequency content to
	// average out than a simple noise source - see CpuPathTracer.cpp's
	// sampleEnvironmentDiffuse()/sampleEnvironmentSpecular() for where this
	// actually gets consumed, and their doc comments for the fuller
	// rationale (diagnosed against a moderately rough, swirl-iridescent car
	// paint material whose reflections looked persistently grainy/washed
	// out even at very high sample counts - a genuine but very slowly
	// converging Monte Carlo noise source, not a bug in the BSDF/texture
	// math itself, all of which checked out correct in isolation).
	// faceSize == 0 means not captured (no environment map loaded).
	std::vector<float> irradianceFaces[6];
	int irradianceFaceSize = 0;

	// GGX-prefiltered mip chain (SceneRenderController::prefilterMap()/
	// prefilterMipLevels()) - each level is a progressively blurred version
	// of the raw cubemap, roughness-matched exactly the way raster's own
	// specular IBL term selects a mip via textureLod(prefilterMap, R,
	// roughness * (prefilterMipLevels-1)) (see main_scene.frag). Used for
	// indirect bounces whose most recent surface interaction was the
	// specular/coat lobe, selecting (and linearly blending between) the two
	// nearest mip levels for a given roughness - the same variance-reduction
	// raster gets "for free" from a single prefiltered texture lookup,
	// applied to PT's stochastic bounce sampling instead of always hitting
	// the raw, unblurred map regardless of how rough the reflecting surface
	// is. Empty means not captured.
	struct PrefilterMip
	{
		std::vector<float> faces[6];
		int faceSize = 0;
	};
	std::vector<PrefilterMip> prefilterMips;
};

struct RtSceneSnapshot
{
	std::vector<RtMeshGeometry> meshes;
	std::vector<RtInstance>     instances;
	std::vector<RtMaterial>     materials;
	std::vector<RtLight>        lights;
	RtCamera                    camera;
	RtEnvironment                environment;

	// Visualization panel's "Shadows"/"Self Shadows" checkboxes - mirrors
	// SceneRenderController::shadowsEnabled()/selfShadowsEnabled(), which
	// raster's shadow-map pass already respects (main_scene.frag:
	// "shadowsEnabled && (selfShadowsEnabled || floorRendering || ...)").
	// shadowsEnabled=false skips NEE shadow-ray occlusion testing entirely
	// (every light always visible, matching raster's shadow map being
	// fully disabled). selfShadowsEnabled=false still tests occlusion
	// against every OTHER instance but excludes the CURRENT shading
	// instance's own geometry from that specific shadow ray (via RtRay::
	// mask) - matching raster's distinction between "an object shadows
	// itself" vs. "an object casts/receives shadows from other objects".
	bool shadowsEnabled     = true;
	bool selfShadowsEnabled = true;

	// Bumped every time a new snapshot is built. Lets the path tracer /
	// accumulator detect that geometry/material/visibility actually changed
	// (as opposed to just the camera moving, which resets accumulation
	// without needing a new snapshot or acceleration-structure rebuild).
	uint64_t revisionId = 0;
};
