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
// application) and glTF alphaMode Masked cutout (via __anyhit__ah() below -
// alphaMode Blend/true transparency compositing is deferred to the
// transmission phase, which needs similar stochastic-alpha machinery
// anyway). Still deferred: every KHR extension texture (specular/clearcoat/
// sheen/anisotropy/iridescence/transmission) - see RtMaterial's own doc
// comments for what those add.
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
// weighted diffuse-lobe escape passes roughness=1.0 (the most-blurred mip)
// as a rough stand-in for a real irradiance map, which this backend doesn't
// capture separately yet.
struct RtOptixEnvironment
{
	float3* faces[6]; // nullptr (faceSize<=0) if no environment map loaded
	int faceSize;

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

	RtOptixEnvironment environment;

	// Number of jittered primary-ray samples averaged per pixel within THIS
	// launch (box-filter AA jitter, via a per-(pixel,sample) hash seed - see
	// RtOptixScene.cu's pcgHash()). RtOptixPathTracingSession additionally
	// accumulates the results of several launches (each a small chunk of
	// this many samples) across time for real progressive refinement/
	// progress reporting - see that class's doc comment - so this is a
	// per-launch chunk size, not necessarily the full target sample count.
	unsigned int samplesPerPixel;

	// Maximum path length (primary hit + subsequent bounces) the raygen loop
	// in RtOptixScene.cu will trace per sample before giving up - mirrors
	// CpuPathTracer::Settings::maxBounces (RtPathTracingSession's identical
	// setting), previously read but never actually honored by this backend.
	unsigned int maxBounces;

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

	// glTF occlusionTexture.strength - see RtMaterial::occlusionStrength's
	// doc comment ("clamp(mix(1.0, texAO, occlusionStrength), 0.0001, 1.0)").
	// Only meaningful when aoTexture.width > 0.
	float occlusionStrength;

	// Base glTF alphaMode - see RtMaterial::blendMode's doc comment.
	// 0=Opaque, 1=Masked (cutout, handled by __anyhit__ah() below),
	// 2=Blend (true transparency compositing - not yet implemented, treated
	// as Opaque for now; deferred to the transmission phase, which needs
	// similar stochastic-alpha machinery anyway). alphaThreshold is glTF's
	// alphaCutoff (Masked only).
	int blendMode;
	float alphaThreshold;

	// Flat opacity factor - used when opacityTexture is absent AND
	// baseColorTexture has no alpha channel to fall back to (matching
	// CpuPathTracer::evaluateSurface()'s fallback chain: opacityTexture ->
	// baseColorTexture's alpha -> this flat factor).
	float opacity;

	// glTF normalTexture.scale - see RtMaterial::normalScale's doc comment.
	// Only meaningful when normalTexture.width > 0.
	float normalScale;

	RtOptixTexture baseColorTexture;
	RtOptixTexture metallicTexture;
	RtOptixTexture roughnessTexture;
	RtOptixTexture normalTexture;
	RtOptixTexture emissiveTexture;
	RtOptixTexture aoTexture;

	// width<=0 (absent) falls back to baseColorTexture's own alpha channel
	// (if that's present), then to the flat `opacity` factor above - see
	// __anyhit__ah()'s doc comment for the exact fallback chain.
	RtOptixTexture opacityTexture;
};
