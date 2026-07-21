#pragma once

#include <optix_types.h> // OptixTraversableHandle

// ---------------------------------------------------------------------------
// RtOptixSceneParams / RtOptixSceneHitGroupData / RtOptixLight
//
// Shared between RtOptixSceneTracer.cpp (host) and src/cuda/RtOptixScene.cu
// (device) - GPU path tracer backend, real scene geometry via a real two-
// level acceleration structure (GAS per RtMeshGeometry, IAS with one
// OptixInstance per RtInstance - mirrors RtEmbreeScene's BLAS/TLAS structure
// exactly), using the real RtCamera, full metallic-roughness Cook-Torrance
// direct lighting, shadow rays, core PBR textures (baseColor/metallic/
// roughness/normal/emissive - see RtOptixTexture/RtOptixSceneHitGroupData
// below) and COLOR_0 vertex color, ported from CpuPathTracer's
// sampleTexture()/evaluateSurface(). __raygen__rg() now runs a real
// iterative path-tracing loop (up to maxBounces below) instead of a single
// deterministic mirror bounce: each hit stochastically samples ONE lobe
// (cosine-weighted diffuse or GGX-VNDF specular, chosen by a Fresnel-based
// probability) for its continuation direction, weighting throughput by the
// importance-sampling estimator - see RtOptixScene.cu's traceBouncePath()/
// sampleGGXVNDF()/cosineSampleHemisphere() doc comments. Also has ambient
// occlusion (applied to the diffuse-lobe's indirect throughput only, not
// direct lighting - see __closesthit__ch()'s doc comment for why that's a
// deliberate simplification of CpuPathTracer's own multi-site AO
// application) and glTF alphaMode Masked/Blend cutout (via __anyhit__ah()
// below - Masked deterministic, Blend a stochastic per-sample existence
// pick). KHR_materials_ior + KHR_materials_specular (including their
// textures) now drive the F0/F90/directF0 computation - see RtOptixScene.cu's
// computeF0F90 port. KHR_materials_clearcoat (a second GGX lobe over its own
// normal/roughness, blended per computeClearcoatFresnel()'s Fresnel weight -
// both a direct-light mix and a third stochastic indirect lobe, ported from
// CpuPathTracer::evaluateClearcoatDirect()/sampleBSDFBounce()'s coat branch)
// and KHR_materials_sheen (additive Charlie-NDF grazing-angle lobe, both
// direct/punctual lighting and a small stochastic environment/IBL sample,
// plus a base-layer energy-compensation dampening via a small baked
// directional-albedo LUT - see sheenAlbedoLUT below), KHR_materials_
// anisotropy (stretches the base specular lobe along a tangent-space
// direction, both direct and indirect), KHR_materials_iridescence (thin-film
// Fresnel tinting, both direct and indirect), KHR_materials_
// pbrSpecularGlossiness (legacy workflow, overrides baseColor/metalness/
// roughness/F0 wholesale), and KHR_materials_diffuse_transmission (a second
// back-hemisphere diffuse lobe, both direct and indirect) are now
// implemented. Material textures also carry a box-filtered mip pyramid
// (RtOptixTexture::mips) with a per-primary-hit, triangle/camera-footprint-
// derived trilinear LOD (see RtOptixScene.cu's computeTextureLod()/
// sampleTexture2D()) to avoid minification aliasing, mirroring
// CpuPathTracer's identical mip-mapping. KHR_materials_transmission +
// KHR_materials_volume + KHR_materials_dispersion are now implemented too -
// see RtOptixScene.cu's transmission branch in __closesthit__ch() (ported
// from CpuPathTracer::tracePixel()'s identical transmission handling,
// including real Beer-Lambert volume absorption over the traced entry-to-
// exit distance and hero-wavelength dispersion). maxTransmissionBounces/
// fireflyClampThreshold/russianRouletteStartDepth below all mirror
// CpuPathTracer::Settings' identically-named fields exactly, read from the
// same PathTracingDialog-backed ViewportWidget settings CPU uses - they used
// to be hardcoded constants in RtOptixScene.cu instead, silently diverging
// from whatever the dialog actually showed.
// ---------------------------------------------------------------------------
struct RtOptixLight
{
	int type; // matches RtLight::type: 0=Directional, 1=Point, 2=Spot
	float3 position;
	float3 direction;
	float3 color;
	float intensity;
	float range;
	float innerConeCos;
	float outerConeCos;
};

// One GGX-prefiltered mip level - RtEnvironment::PrefilterMip's GPU-side
// counterpart. faces[6]/faceSize follow the same convention as
// RtOptixEnvironment::faces/faceSize below, just at this mip's (smaller,
// blurrier) resolution.
struct RtOptixPrefilterMip
{
	float3* faces[6];
	int faceSize;
};

// Mirrors RtEnvironment's raw (mip-0) cubemap plus the rotation/exposure/
// fallback fields CpuPathTracer's sampleEnvironmentBackground()/
// sampleEnvironmentMiss() need - see undoSkyboxRotation()'s doc comment in
// CpuPathTracer.cpp for why the captured cubemap needs an un-rotate before
// sampling. Also carries the GGX-prefiltered mip chain (RtEnvironment::
// prefilterMips) so escaping bounce rays sample a roughness-appropriate mip
// as a variance-reduction aid (mirrors CpuPathTracer's own real-per-sample
// sampleEnvironmentSpecular(ray.direction, lastBounceEnvRoughness) call) -
// see RtOptixScene.cu's sampleEnvironmentSpecular() and CpuPathTracer's
// identically-named function/its toPrefilterDirection() doc comment for the
// extra swizzle the prefilter chain (but NOT the raw map) needs. A cosine-
// weighted diffuse-lobe escape samples irradianceFaces below - a real
// diffuse-convolved cubemap, same one CPU's sampleEnvironmentDiffuse() uses
// (RtEnvironment::irradianceFaces, captured via SceneRenderController::
// irradianceMap() and shared by both engines' snapshot already) - not a
// roughest-specular-mip stand-in.
struct RtOptixEnvironment
{
	float3* faces[6]; // nullptr (faceSize<=0) if no environment map loaded
	int faceSize;

	// Diffuse-convolved irradiance cubemap - see RtEnvironment::
	// irradianceFaces' doc comment for the full rationale (used for indirect
	// bounces whose most recent surface interaction was a diffuse lobe).
	// nullptr/irradianceFaceSize<=0 means "not captured" (no environment map
	// loaded), in which case sampleEnvironmentDiffuse() falls back to the
	// raw map, matching CPU's own identical fallback exactly.
	float3* irradianceFaces[6];
	int irradianceFaceSize;

	int showBackground; // bool as int
	float3 fallbackTopColor;
	float3 fallbackBottomColor;
	int fallbackGradientStyle;

	int cameraUpAxisZUp; // bool as int
	float skyBoxZRotationDegrees;
	float envMapExposure;

	// Device array of prefilterMipCount RtOptixPrefilterMip entries, ordered
	// sharp-to-blurry exactly like RtEnvironment::prefilterMips - nullptr/0
	// if no environment map (or its prefilter chain specifically) was
	// captured, in which case reflections fall back to the raw map.
	const RtOptixPrefilterMip* prefilterMips;
	int prefilterMipCount;
	const RtOptixPrefilterMip* sheenPrefilterMips;
	int sheenPrefilterMipCount;

	// Environment-light NEE + MIS - device counterpart of RtEnvironmentSampler's
	// internal distribution, uploaded VERBATIM (not rebuilt device-side) by
	// RtOptixSceneTracer::buildScene() from a host-built RtEnvironmentSampler
	// instance, so both engines importance-sample the identical luminance-
	// weighted distribution over the same cubemap. envFlatCdf is the
	// cumulative array (size 6*faceSize*faceSize+1, [0..envTotalWeight]);
	// envTexelPdf is the per-texel solid-angle pdf (size 6*faceSize*faceSize).
	// See RtOptixScene.cu's envSamplerSample()/envSamplerPdf() (device
	// counterparts of RtEnvironmentSampler::sample()/pdf()) for how these are
	// consumed. nullptr/envTotalWeight<=0 means "no importance-sampling data"
	// (matches RtEnvironmentSampler::isValid()'s identical gate) - environment-
	// NEE is then simply skipped, same as when the CPU sampler itself isn't
	// valid.
	const float* envFlatCdf;
	const float* envTexelPdf;
	float envTotalWeight;
};

struct RtOptixSceneParams
{
	// Linear HDR radiance, un-tonemapped/un-gamma-encoded (RtOptixSceneTracer
	// reads this back as-is into glm::vec3s) - RtOptixPathTracingSession
	// accumulates multiple single/few-sample launches' worth of these in
	// linear space and only tonemaps once at present time (RtPresenter::
	// upload()), same contract as CpuPathTracer's own output. An earlier
	// version of this kernel instead wrote display-ready 0-255 bytes
	// (via a toColor() clamp) straight from a single all-samples-at-once
	// launch - fine there since there was nothing to average across launches
	// yet, but averaging several already-gamma-encoded launches' bytes
	// together would have been mathematically wrong (gamma encoding doesn't
	// commute with averaging) once progressive multi-launch accumulation
	// was added.
	float3* image;
	// OIDN guide (auxiliary feature) buffers - primary-hit base color/
	// world-space shading normal, chunk-averaged the same way `image` is,
	// zeroed wherever the primary ray never hits geometry. Mirrors
	// RtFrameAccumulator::resolveAlbedo()/resolveNormal()'s contract so
	// RtOptixPathTracingSession can feed them straight to RtDenoiser - see
	// RtDenoiser::denoise()'s albedo/normal doc comment for why both
	// buffers help it specifically with reflections (this kernel's own
	// mirror-bounce term).
	float3* albedoImage;
	float3* normalImage;
	// Per-pixel primary-hit fraction (hits/spp for this launch's samples) -
	// mirrors RtFrameAccumulator::hitCounts()/RtPathTracingSession's
	// published alpha exactly. RtPresenter alpha-blends the path-traced
	// frame over the already-rendered raster, so 0 here (pure background)
	// lets raster's own sharp skybox/gradient show through instead of this
	// kernel's traced background - the SAME "alpha-composited background"
	// design the CPU session uses (see RtPathTracingSession::publishLatest());
	// without it the GPU frame rendered fully opaque, covering the raster
	// skybox, which is why enabling the Sky Box checkbox showed no skybox
	// in GPU mode while CPU mode (alpha-composited) showed it fine.
	float* alphaImage;
	unsigned int imageWidth;
	unsigned int imageHeight;

	float3 camPosition;
	float3 camForward;
	float3 camRight;
	float3 camUp;
	float camAspectRatio;
	int camOrthographic; // bool as int - POD-safe across the host/device boundary
	float camTanHalfFovY;     // perspective only
	float camOrthoHalfHeight; // orthographic only

	const RtOptixLight* lights;
	unsigned int lightCount;
	int shadowsEnabled;
	int selfShadowsEnabled;

	// Mirrors CpuPathTracer::Settings::enableEnvironmentImportanceSampling -
	// see RtOptixEnvironment::envFlatCdf's doc comment for the additional
	// envTotalWeight>0 gate this is combined with in __closesthit__ch().
	int enableEnvironmentImportanceSampling;

	RtOptixEnvironment environment;

	// KHR_materials_sheen directional-albedo LUTs. sheenAlbedoLUT mirrors
	// raster's sheenELUT.r/new roughness² convention for direct-light base
	// dampening; sheenCharlieLUT mirrors raster's charlieLUT.b/legacy roughness
	// convention, used only as the IBL energy bound via min(charlie, sheenE).
	// Both are process-constant row-major [roughness][NdotV] tables.
	const float* sheenAlbedoLUT;
	const float* sheenCharlieLUT;
	int sheenAlbedoLUTSize;

	// Number of jittered primary-ray samples averaged per pixel within THIS
	// launch (box-filter AA jitter, via a per-(pixel,sample) hash seed - see
	// RtOptixScene.cu's pcgHash()). RtOptixPathTracingSession additionally
	// accumulates the results of several launches (each a small chunk of
	// this many samples) across time for real progressive refinement/
	// progress reporting - see that class's doc comment - so this is a
	// per-launch chunk size, not necessarily the full target sample count.
	unsigned int samplesPerPixel;

	// Global sample index of sample 0 in this launch. Progressive rendering
	// invokes this kernel repeatedly in chunks; without this offset, chunk 0
	// sample 0 and chunk N sample 0 reuse the exact same RNG seed and the
	// accumulation never actually refines.
	unsigned int sampleOffset;

	// Maximum path length (primary hit + subsequent bounces) the raygen loop
	// in RtOptixScene.cu will trace per sample before giving up - mirrors
	// CpuPathTracer::Settings::maxBounces (RtPathTracingSession's identical
	// setting), previously read but never actually honored by this backend.
	unsigned int maxBounces;

	// Mirrors CpuPathTracer::Settings::maxTransmissionBounces/
	// fireflyClampThreshold/russianRouletteStartDepth exactly - all three
	// used to be hardcoded constants in RtOptixScene.cu (kMaxTransmission
	// Bounces=32, kFireflyClampThreshold=3.0f, an inline `>= 2` literal)
	// instead of reading the user's actual dialog settings, silently
	// diverging from whatever CPU was configured with even when the UI
	// showed identical values - see PathTracingDialog's matching CPU/GPU
	// hygiene pass.
	unsigned int maxTransmissionBounces;
	float fireflyClampThreshold;
	unsigned int russianRouletteStartDepth;

	// Mirrors CpuPathTracer::Settings::maxVolumeScatterBounces - see
	// RtOptixScene.cu's __raygen__rg() hitFlag==4 branch (KHR_materials_
	// volume_scatter's free-flight random walk's own, separate scatter-
	// event budget - free/no-RR up to this count, then RR).
	unsigned int maxVolumeScatterBounces;

	OptixTraversableHandle handle;
};

// Device-side counterpart of RtTextureSample - see CpuPathTracer.cpp's
// sampleTexture()/applyChannelPacking() for the exact bilinear + KHR_texture_
// transform + wrap + channel-packing pipeline RtOptixScene.cu's own
// sampleTexture2D()/applyChannelPacking() port verbatim from. width<=0 means
// "absent" (same convention as RtOptixEnvironment::faceSize<=0) - baseColor/
// normal/emissive textures ignore packingChannel/packingInvert/packingScale/
// packingBias (RGB/RGBA reads), those fields only matter for single-channel
// (metallic/roughness) textures.
// Device-side counterpart of RtTextureMipLevel - one box-filtered mip level
// of a material texture's rgba8 pyramid. See RtOptixTexture::mips' doc
// comment for how these are consumed.
struct RtOptixTextureMipLevel
{
	const uchar4* rgba8;
	int width;
	int height;
};

struct RtOptixTexture
{
	const uchar4* rgba8; // width*height texels, row-major RGBA8, no padding - matches RtTextureSample::rgba8's byte layout exactly
	int width;
	int height;

	int texCoordIndex; // which of the mesh's 4 UV channels (RtOptixSceneHitGroupData::texCoords) this texture samples
	float2 uvScale;
	float2 uvOffset;
	float uvRotation;

	int packingChannel; // 0=R, 1=G, 2=B, 3=A
	int packingInvert;  // bool as int
	float packingScale;
	float packingBias;

	unsigned int wrapS; // raw GL wrap enum (GL_REPEAT/GL_CLAMP_TO_EDGE/GL_MIRRORED_REPEAT) - see RtTextureSample::wrapS's doc comment
	unsigned int wrapT;

	// Box-filtered mip pyramid - device counterpart of RtTextureSample::mips,
	// same ordering (mips[0] duplicates the base rgba8/width/height above,
	// for uniform indexing). nullptr/0 if the upload failed or the source
	// RtTextureSample had no mips (RtOptixScene.cu's sampleTexture2D() then
	// falls back to base-level-only sampling, same as before this field
	// existed). See RtOptixScene.cu's computeTextureLod()/sampleTexture2D()
	// for how a per-hit footprint combines with this texture's own width/
	// height to pick a level.
	const RtOptixTextureMipLevel* mips;
	int mipCount;
};

// One per RtInstance in the SBT (see RtOptixSceneTracer.cpp's SBT build) -
// gives the closest-hit program that instance's own mesh geometry/material
// to shade with (OptiX's built-in triangle intersection supplies
// barycentrics and the primitive index, but NOT vertex attribute data - the
// same reason RtEmbreeScene::intersect() fetches vertices from its own
// mesh.vertices array by hand).
struct RtOptixSceneHitGroupData
{
	float3* normals; // object-space, per-vertex, indexed via `indices` below
	uint3* indices;  // one uint3 per triangle - matches RtMeshGeometry's flat uint32 triple layout

	// 4 UV channels per vertex, flattened as texCoords[vertexIndex*4 + channel]
	// - mirrors RtVertex::texCoords[4] (a texture's KHR-declared texCoordIndex
	// can reference any of them, see RtOptixTexture::texCoordIndex).
	float2* texCoords;

	// xyz = object-space tangent (RtVertex::tangent, untransformed - see
	// RtOptixScene.cu's normal-mapping code for why it's transformed via
	// optixTransformVectorFromObjectToWorldSpace(), the plain-model-matrix
	// direction transform, NOT the inverse-transpose one normals use).
	// w = handedness sign, PRECOMPUTED per-vertex at buildScene() time from
	// RtVertex::tangent/normal/bitangent (matches CpuPathTracer::
	// applyNormalMap()'s "orthogonalize bitangent, take cross(N,T) sign"
	// derivation exactly, just done once on the host in object space rather
	// than per-shading-sample in world space - equivalent as long as the
	// instance transform doesn't mirror/flip handedness itself, which
	// CpuPathTracer doesn't account for either). Zero-length xyz signals "no
	// tangent data" (matches RtVertex::tangent's own zero-length convention).
	float4* tangents;

	// COLOR_0-style per-vertex RGB (RtVertex::color.rgb - already linear, no
	// sRGB decode needed, unlike textures - see RtVertex::color's doc
	// comment). Always (1,1,1) when the source mesh has no vertex color
	// attribute, so it's always safe to unconditionally multiply into
	// baseColor, matching CpuPathTracer::evaluateSurface()'s "apply vertex
	// color last (in linear)". Alpha is intentionally not carried - nothing
	// in this backend consumes opacity/alpha yet.
	float3* vertexColors;

	float3 baseColor;
	float metalness;
	float roughness;
	float3 emissive;
	float emissiveStrength;

	// KHR_materials_ior - replaces the fixed 1.5/0.04 dielectric F0
	// assumption; see RtOptixScene.cu's computeF0F90() port (and
	// CpuPathTracer's identically-named original) for how this and the
	// KHR_materials_specular factors below combine into F0/F90/directF0.
	float ior;

	// KHR_materials_specular - scales/tints dielectric reflectance (no
	// effect on metals). specularTexture's ALPHA channel scales
	// specularFactor (channel-packed, packingChannel=3 set by
	// RtSceneBuilder), specularColorTexture's RGB (sRGB-encoded) tints
	// specularColorFactor - same slots/conventions as RtMaterial's.
	float specularFactor;
	float3 specularColorFactor;

	// glTF occlusionTexture.strength - see RtMaterial::occlusionStrength's
	// doc comment ("clamp(mix(1.0, texAO, occlusionStrength), 0.0001, 1.0)").
	// Only meaningful when aoTexture.width > 0.
	float occlusionStrength;

	// Base glTF alphaMode - see RtMaterial::blendMode's doc comment.
	// 0=Opaque, 1=Masked (deterministic cutout), 2=Blend (stochastic
	// per-sample existence pick - both handled uniformly by __anyhit__ah()
	// below, ported from CpuPathTracer's identical MASK/BLEND alphaTest
	// block). alphaThreshold is glTF's alphaCutoff (Masked only).
	int blendMode;
	float alphaThreshold;

	// glTF material.doubleSided (int as bool) - previously unreferenced by
	// this kernel entirely, so every material rendered as if double-sided.
	// A single-sided material's back face should be treated as if it doesn't
	// exist, matching real-time back-face culling - see __anyhit__ah()'s
	// optixIsTriangleFrontFaceHit() check, which is already correct for
	// mirrored/negative-scale instances too with no extra instance flag
	// needed - see RtOptixSceneTracer.cpp's optixInst.flags assignment for
	// the proof (OptiX's native object-space winding test is algebraically
	// identical to the world-space test for any invertible transform).
	int twoSided;

	// Flat opacity factor - used when opacityTexture is absent AND
	// baseColorTexture has no alpha channel to fall back to (matching
	// CpuPathTracer::evaluateSurface()'s fallback chain: opacityTexture ->
	// baseColorTexture's alpha -> this flat factor).
	float opacity;

	// glTF normalTexture.scale - see RtMaterial::normalScale's doc comment.
	// Only meaningful when normalTexture.width > 0.
	float normalScale;

	// KHR_materials_clearcoat - a second, independent GGX lobe with its own
	// normal/roughness, layered over the base lobes. Field names/semantics
	// mirror RtMaterial's own clearcoat/clearcoatRoughness/
	// clearcoatNormalScale exactly (see RtSceneSnapshot.h's doc comment).
	// clearcoatTexture's R channel scales clearcoat, clearcoatRoughnessTexture's
	// G channel scales clearcoatRoughness (packing set by RtSceneBuilder, read
	// the same way metallicTexture/roughnessTexture are above).
	float clearcoat;
	float clearcoatRoughness;
	float clearcoatNormalScale;

	// KHR_materials_sheen - additive fabric/velvet-like grazing-angle
	// retroreflection lobe (Charlie NDF). sheenColorFactor==(0,0,0) means "no
	// sheen" (mirrors RtMaterial::sheenColor's same convention).
	// sheenColorTexture is sRGB-encoded RGB; sheenRoughnessTexture's A channel
	// scales sheenRoughness (packing set by RtSceneBuilder).
	float3 sheenColorFactor;
	float sheenRoughness;

	// KHR_materials_anisotropy - stretches the base specular lobe along a
	// tangent-space direction. anisotropyTexture (when present) carries a
	// per-pixel direction in its RG channels ([0,1] -> [-1,1], further
	// rotated by anisotropyRotation) and a strength SCALE in its B channel -
	// decoded together, not simple factor*texture channel packing, so it's
	// read as a raw RGB sample (see decodeAnisotropyTexture() in
	// RtOptixScene.cu, ported from CpuPathTracer's identically-named
	// function) rather than through RtOptixTexture's packingChannel
	// machinery. anisotropyRotation is in radians (RtMaterial's own
	// convention, already converted from the glTF factor's raw units).
	float anisotropyStrength;
	float anisotropyRotation;

	// KHR_materials_iridescence - thin-film interference tinting applied to
	// the Fresnel term (see RtOptixScene.cu's applyIridescenceToFresnel()/
	// evalIridescence(), ported from CpuPathTracer's identically-named
	// functions). iridescenceFactor<=0 means "no iridescence" (matches
	// RtMaterial::iridescenceFactor's default). iridescenceThickness is
	// REPLACED (not multiplied) by iridescenceThicknessTexture when present -
	// matches CpuPathTracer::evaluateSurface()'s "s.iridescenceThickness =
	// mat.iridescenceThicknessMax; if (texture) s.iridescenceThickness =
	// applyChannelPacking(...)" exactly (the max/min-thickness texture-lerp
	// glTF technically defines is deferred - this backend, like CPU, treats
	// the texture as supplying the final thickness directly).
	float iridescenceFactor;
	float iridescenceIor;
	float iridescenceThickness;

	// KHR_materials_pbrSpecularGlossiness - legacy alternate workflow to
	// metallic-roughness. When useSpecGloss is set, this COMPLETELY REPLACES
	// baseColor/metalness/roughness/F0/F90/directF0's normal meaning (see
	// RtOptixScene.cu's override block in __closesthit__ch(), ported from
	// CpuPathTracer::evaluateSurface()'s identical override) - diffuseColor
	// becomes the baseColor equivalent, specGlossSpecularColor is a direct
	// RGB F0 (not IOR-derived), roughness = 1-glossiness. specularGlossinessTexture
	// is packed RGB=specular color (sRGB), A=glossiness (linear) - matching
	// CpuPathTracer's identical packed-texture sampling.
	int useSpecGloss; // bool as int
	float3 diffuseColor;
	float3 specGlossSpecularColor;
	float glossinessFactor;

	// KHR_materials_diffuse_transmission - a translucent DIFFUSE material
	// (leaves, paper, curtains): light landing on the front diffusely
	// scatters through to the back (and vice versa), no ior/Fresnel/
	// refraction involved - see RtOptixScene.cu's direct-lighting back-
	// hemisphere NEE term and the indirect diffuse lobe's front/back
	// stochastic pick, ported from CpuPathTracer::tracePixel()/
	// sampleBSDFBounce() exactly. diffuseTransmissionFactor's texture uses
	// the ALPHA channel (glTF spec); diffuseTransmissionColorTexture uses
	// RGB (sRGB).
	float diffuseTransmissionFactor;
	float3 diffuseTransmissionColor;

	// KHR_materials_transmission + KHR_materials_volume + KHR_materials_
	// dispersion - see RtOptixScene.cu's transmission branch in
	// __closesthit__ch() (bypasses the general stochastic lobe-selection
	// entirely for transmissive materials, mirroring CpuPathTracer::
	// tracePixel()'s identical bypass) for the full reflect/refract/TIR/
	// dispersion mechanics, ported from CpuPathTracer's own transmission
	// handling verbatim. hasVolume mirrors RtMaterial::hasVolume (thin-
	// walled vs solid-dielectric gate); attenuationColor/attenuationDistance
	// feed Beer-Lambert absorption over the REAL traced entry-to-exit
	// distance (not the authored thicknessFactor approximation raster
	// uses) - see calculateVolumeAttenuation(). thicknessFactor is only
	// used by KHR_materials_diffuse_transmission's own NEE volume-tint
	// scaling (matches CpuPathTracer::tracePixel()'s identical use), since
	// that path has no traced distance of its own to measure. transmissionTexture's
	// R channel scales transmission (channel-packed, matching RtMaterial's
	// convention). dispersion has no texture input (glTF spec: a flat
	// factor only).
	float transmission;
	int hasVolume; // bool as int
	float3 attenuationColor;
	float attenuationDistance;
	float thicknessFactor;
	float dispersion;

	// KHR_materials_volume_scatter - see RtOptixScene.cu's
	// sampleBSSRDFEntryPoint() (BSSRDF diffusion-profile importance
	// sampling, called from __closesthit__ch()'s diffuse-transmission
	// branch). multiScatterColor is the authored multiscatterColorFactor,
	// passed straight through as Burley normalized diffusion's target
	// albedo `A` - no per-event conversion (see RtSceneBuilder.cpp).
	float3 multiScatterColor;
	int hasVolumeScattering; // bool as int

	RtOptixTexture baseColorTexture;
	RtOptixTexture metallicTexture;
	RtOptixTexture roughnessTexture;
	RtOptixTexture normalTexture;
	RtOptixTexture emissiveTexture;
	RtOptixTexture aoTexture;
	RtOptixTexture specularTexture;      // A channel scales specularFactor - see above
	RtOptixTexture specularColorTexture; // RGB (sRGB) tints specularColorFactor - see above

	// width<=0 (absent) falls back to baseColorTexture's own alpha channel
	// (if that's present), then to the flat `opacity` factor above - see
	// __anyhit__ah()'s doc comment for the exact fallback chain.
	RtOptixTexture opacityTexture;

	RtOptixTexture clearcoatTexture;          // R channel scales clearcoat
	RtOptixTexture clearcoatRoughnessTexture; // G channel scales clearcoatRoughness
	RtOptixTexture clearcoatNormalTexture;

	RtOptixTexture sheenColorTexture;     // sRGB RGB, tints sheenColorFactor
	RtOptixTexture sheenRoughnessTexture; // A channel scales sheenRoughness

	RtOptixTexture anisotropyTexture; // raw RGB sample, decoded via decodeAnisotropyTexture() - see anisotropyStrength's doc comment
	RtOptixTexture iridescenceTexture;          // channel-packed, scales iridescenceFactor
	RtOptixTexture iridescenceThicknessTexture; // channel-packed, REPLACES iridescenceThickness

	RtOptixTexture diffuseTexture;              // sRGB RGB, spec-gloss's baseColorTexture equivalent
	RtOptixTexture specularGlossinessTexture;   // packed RGB=specular color (sRGB), A=glossiness (linear)

	RtOptixTexture diffuseTransmissionTexture;      // A channel scales diffuseTransmissionFactor
	RtOptixTexture diffuseTransmissionColorTexture; // sRGB RGB, tints diffuseTransmissionColor

	RtOptixTexture transmissionTexture; // R channel scales transmission
};
