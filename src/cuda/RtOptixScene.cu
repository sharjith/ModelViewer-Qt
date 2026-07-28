// ---------------------------------------------------------------------------
// RtOptixScene.cu - Phase 3 kernel for the GPU (OptiX) path tracer backend.
//
// Renders the app's real scene geometry (via a real GAS-per-mesh/IAS-per-
// instance acceleration structure - see RtOptixSceneTracer.cpp) through the
// real RtCamera projection, shaded with the full metallic-roughness Cook-
// Torrance BRDF for direct lighting (GGX distribution, Smith-Schlick
// geometry, Fresnel - ported verbatim from CpuPathTracer's
// evaluateDirectBRDF()) plus core PBR textures (baseColor/metallic/
// roughness/normal/emissive) and COLOR_0 vertex color, and a plain boolean
// shadow ray per light (every direct-lighting sample is occlusion-tested
// before being added, using OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT - a
// boolean query is all a shadow test needs - rather than a second program-
// group/SBT pair; __closesthit__ch()/__miss__ms() both check
// optixGetRayFlags() up front and take a one-line early-out when it's a
// shadow ray, writing the occluded/unoccluded bit into payload 0; payloads
// 1/2 carry the source instance and self-shadow toggle for any-hit filtering.
//
// __raygen__rg() runs a REAL iterative path-tracing loop (see
// traceBouncePath()'s doc comment for the full payload layout this needs):
// for each of params.samplesPerPixel jittered AA samples, it traces a chain
// of up to params.maxBounces bounces, each one a single optixTrace() call
// (not recursive - closesthit computes this hit's own direct lighting AND
// stochastically samples ONE BSDF lobe for the next bounce direction,
// returning both via payload, so the LOOP lives in raygen instead of nested
// trace calls - this is what lets bounce count scale without the pipeline's
// required recursion depth growing with it, unlike an earlier version of
// this kernel that recursed a single deterministic mirror reflection
// directly from within closesthit). The lobe choice (cosine-weighted
// diffuse via cosineSampleHemisphere(), or GGX-VNDF-importance-sampled
// specular via sampleGGXVNDF()) is picked stochastically per hit, weighted
// by a Fresnel-based probability - see __closesthit__ch()'s own doc
// comment for the exact importance-sampling weights this applies. A simple
// max-throughput-component Russian roulette (after a couple of guaranteed
// bounces) bounds cost/variance on long paths, matching the spirit of
// CpuPathTracer's own Settings::russianRouletteStartDepth.
//
// This REPLACES the earlier single-deterministic-mirror-bounce design,
// which had no mechanism for diffuse indirect light at all (a face lit only
// by bounced/ambient light rendered flat black) and traced reflections as
// one exact mirror ray regardless of roughness (producing under-converged,
// speckled noise whenever that exact ray grazed small bright/colorful
// detail - e.g. a neighboring glossy object's own texture - since there was
// no way to importance-sample or blur it away). Escaping bounce rays still
// sample the roughness-appropriate GGX-prefiltered mip level for variance
// reduction (sampleEnvironmentSpecular(), same as before); a diffuse-lobe
// escape instead samples a real cosine-weighted irradiance convolution
// (sampleEnvironmentDiffuse()) - see RtOptixSceneParams.h's RtOptixEnvironment
// doc comment.
//
// Also has ambient occlusion (__closesthit__ch() darkens the diffuse lobe's
// indirect throughput weight by it - a deliberately simpler approximation
// of CpuPathTracer's own multi-site AO application, since this backend has
// no separate IBL step to apply it to) and glTF alphaMode Masked cutout
// (__anyhit__ah(), a new any-hit program - optixIgnoreIntersection() lets a
// sub-cutoff hit be transparent to every ray type uniformly, including
// shadow rays, without needing per-ray-type special-casing).
//
// KHR_materials_ior + KHR_materials_specular (including their textures)
// drive the F0/F90/directF0 computation, ported from CpuPathTracer::
// computeF0F90() - see its use site in __closesthit__ch() for the
// F0-vs-directF0 (indirect-vs-direct-lighting) split.
//
// KHR_materials_clearcoat (a second GGX lobe over its own normal/roughness -
// evaluateClearcoatDirect()/computeClearcoatFresnel(), blended into direct
// lighting via an analytic mix() and sampled as a third stochastic indirect
// lobe alongside diffuse/specular - see computeLobeProbabilities()) and
// KHR_materials_sheen (additive Charlie-NDF lobe - calculateSheen() for
// direct/punctual lighting, plus a small RNG-jittered-cone environment/IBL
// sample around the mirror-reflect direction - the visually DOMINANT
// contribution on scenes lit mainly by their environment, per CpuPathTracer's
// own history discovering this - and a base-layer energy-compensation
// dampening via sampleSheenAlbedoLUT()'s baked directional-albedo LUT),
// KHR_materials_anisotropy (distributionGGXAnisotropic()/
// visibilityGGXAnisotropic() for direct lighting, an anisotropic VNDF/Smith
// pair - sampleGGXVNDF()/smithG1GGXAniso()/smithG2HeightCorrelatedGGXAniso()
// - over a rotated tangent frame for indirect bounces), and
// KHR_materials_iridescence (evalIridescence()'s thin-film Fresnel tint,
// applied to both the direct-lighting dielectric/metal reconstruction and
// the indirect specular lobe's Fresnel term), KHR_materials_
// pbrSpecularGlossiness (legacy workflow - overrides baseColor/metalness/
// roughness/F0/F90/directF0 wholesale, and mixes rather than adds diffuse+
// specular for direct lighting), and KHR_materials_diffuse_transmission (a
// second back-hemisphere diffuse lobe, both a direct-lighting BTDF term and a
// stochastic front/back pick for indirect bounces - the raygen loop's next-
// bounce ray origin offset is now direction-aware for this, not always
// +worldNormal) are now implemented, all ported from CpuPathTracer's
// identically-named functions.
//
// glTF alphaMode Blend (a stochastic per-sample existence pick, same
// mechanism as Masked's deterministic threshold - see __anyhit__ah()) is
// also now implemented.
//
// KHR_materials_transmission/volume/dispersion are implemented too, using
// the same dedicated transmission branch and separate transmission-depth
// budget as the CPU tracer.
// Self-contained (no dependency on the OptiX SDK's bundled sutil).
// ---------------------------------------------------------------------------
#include <optix.h>

#include "RtOptixSceneParams.h"

extern "C" {
__constant__ RtOptixSceneParams params;
}

namespace
{
	constexpr float kPi = 3.14159265f;
	__forceinline__ __device__ float3 traceShadowRay(const float3& origin, const float3& direction, float maxDistance,
		unsigned int sourceInstanceId, unsigned int rngSeed, bool forceSelfExclude);

	// Debug-visualization toggle, mirroring CpuPathTracer.cpp's identical
	// kDebugVisualizeClearcoat (and its own file-top doc comment on the
	// pattern: flip manually for debugging, set back to false before
	// committing). Added specifically to compare PTC vs PTG per-hit clearcoat
	// values directly (red/green/blue channels below) after a reported PTC-
	// vs-PTG clearcoat reflection mismatch on automotive paint survived
	// ruling out TBN handedness, shadow-ray origin, environment prefilter
	// lookup, clearcoat GGX sampling, and texture-LOD formula comparisons -
	// see __closesthit__ch()'s use site for what each channel means.
	constexpr bool kDebugVisualizeClearcoat = false;

	// Debug-visualization toggle, mirroring CpuPathTracer.cpp's identical
	// kDebugVisualizeShadowTransmittance exactly - false-colors the primary
	// hit's raw front-hemisphere shadow-ray transmittance (summed across
	// every REAL scene light, i.e. params.lights[i].range >= 0.0f - the
	// app's own always-on default/fallback light is deliberately excluded,
	// same reason as CPU: it dominated the sum to near-white everywhere and
	// masked what a single small, close-range point light is actually
	// doing), BEFORE the default light's 0.15 floor clamp (moot anyway since
	// that light is excluded). KHR_materials_diffuse_transmission's back-
	// hemisphere NEE term contributes its own backShadowTransmittance the
	// same way. See __closesthit__ch()'s NEE loop for the accumulation
	// sites and the loop's closing brace for where it's substituted in.
	constexpr bool kDebugVisualizeShadowTransmittance = false;

	// Post-loop heat-ramp of KHR_materials_volume_scatter's scatterBounces
	// counter, mirroring CpuPathTracer.cpp's identical
	// kDebugVisualizeVolumeScatterBounces exactly - lives in __raygen__rg()
	// (not __closesthit__ch(), unlike kDebugVisualizeClearcoat above) since
	// scatterBounces is accumulated across the whole bounce loop, not known
	// within a single closest-hit invocation.
	constexpr bool kDebugVisualizeVolumeScatterBounces = false;
	constexpr float kVolumeScatterDebugRampCap = 32.0f;

	__forceinline__ __device__ float3 operator+(const float3& a, const float3& b)
	{
		return make_float3(a.x + b.x, a.y + b.y, a.z + b.z);
	}

	__forceinline__ __device__ float3 operator-(const float3& a, const float3& b)
	{
		return make_float3(a.x - b.x, a.y - b.y, a.z - b.z);
	}

	__forceinline__ __device__ float3 operator*(const float3& a, float s)
	{
		return make_float3(a.x * s, a.y * s, a.z * s);
	}

	__forceinline__ __device__ float3 operator*(const float3& a, const float3& b)
	{
		return make_float3(a.x * b.x, a.y * b.y, a.z * b.z);
	}

	__forceinline__ __device__ float dot3(const float3& a, const float3& b)
	{
		return a.x * b.x + a.y * b.y + a.z * b.z;
	}

	__forceinline__ __device__ float3 cross3(const float3& a, const float3& b)
	{
		return make_float3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
	}

	__forceinline__ __device__ float length3(const float3& v)
	{
		return sqrtf(dot3(v, v));
	}

	__forceinline__ __device__ float2 operator+(const float2& a, const float2& b)
	{
		return make_float2(a.x + b.x, a.y + b.y);
	}

	__forceinline__ __device__ float2 operator*(const float2& a, const float2& b)
	{
		return make_float2(a.x * b.x, a.y * b.y);
	}

	__forceinline__ __device__ float2 operator*(const float2& a, float s)
	{
		return make_float2(a.x * s, a.y * s);
	}

	__forceinline__ __device__ float3 normalizeF3(const float3& v)
	{
		const float invLen = rsqrtf(fmaxf(v.x * v.x + v.y * v.y + v.z * v.z, 1e-20f));
		return v * invLen;
	}

	__forceinline__ __device__ float3 lerp3(const float3& a, const float3& b, float t)
	{
		return a + (b - a) * t;
	}

	// Per-component mix, t itself varying per channel - glm::mix(a,b,vec3)'s
	// GPU counterpart (needed by KHR_materials_iridescence's direct-lighting
	// branch, which mixes by a per-channel Fresnel color, not a scalar).
	__forceinline__ __device__ float3 lerp3(const float3& a, const float3& b, const float3& t)
	{
		return make_float3(a.x + (b.x - a.x) * t.x, a.y + (b.y - a.y) * t.y, a.z + (b.z - a.z) * t.z);
	}

	__forceinline__ __device__ float3 reflectF3(const float3& incident, const float3& normal)
	{
		return incident - normal * (2.0f * dot3(normal, incident));
	}

	// GLSL/glm::refract() semantics exactly - returns the zero vector on
	// total internal reflection (the caller then knows to fall back to a
	// mirror bounce), matching CpuPathTracer's own use of glm::refract() in
	// its KHR_materials_transmission handling.
	__forceinline__ __device__ float3 refractF3(const float3& incident, const float3& normal, float eta)
	{
		const float NdotI = dot3(normal, incident);
		const float k = 1.0f - eta * eta * (1.0f - NdotI * NdotI);
		if (k < 0.0f)
			return make_float3(0.0f, 0.0f, 0.0f);
		return incident * eta - normal * (eta * NdotI + sqrtf(k));
	}

	// KHR_materials_volume - Beer-Lambert absorption over the real traced
	// distance a ray travelled through the medium since its previous hit,
	// ported from CpuPathTracer::calculateVolumeAttenuation() verbatim.
	// attenuationDistance<=0 means "no attenuation" (matches
	// RtMaterial::attenuationDistance's own default-infinity convention -
	// pow(color, distance/infinity) trivially equals 1 anyway, so only the
	// <=0 case needs an explicit guard to avoid a divide-by-zero).
	__forceinline__ __device__ float3 calculateVolumeAttenuation(const float3& attenuationColor, float attenuationDistance, float distance)
	{
		if (attenuationDistance <= 0.0f)
			return make_float3(1.0f, 1.0f, 1.0f);
		const float t = distance / attenuationDistance;
		return make_float3(powf(attenuationColor.x, t), powf(attenuationColor.y, t), powf(attenuationColor.z, t));
	}

	// Kulla-Conty single-scatter albedo recovery from a target multi-scatter
	// albedo (Kulla & Conty Estevez 2017), ported verbatim from NVIDIA's
	// vk_gltf_renderer reference implementation
	// (gltf_material_eval.h.slang:125-129's multiToSingleScatterAlbedo()) -
	// mirrors CpuPathTracer.cpp's identical function exactly.
	__forceinline__ __device__ float3 multiToSingleScatterAlbedo(const float3& rhoMs)
	{
		const float3 t = make_float3(
			4.09712f + 4.20863f * rhoMs.x - sqrtf(9.59217f + 41.6808f * rhoMs.x + 17.7126f * rhoMs.x * rhoMs.x),
			4.09712f + 4.20863f * rhoMs.y - sqrtf(9.59217f + 41.6808f * rhoMs.y + 17.7126f * rhoMs.y * rhoMs.y),
			4.09712f + 4.20863f * rhoMs.z - sqrtf(9.59217f + 41.6808f * rhoMs.z + 17.7126f * rhoMs.z * rhoMs.z));
		return make_float3(1.0f - t.x * t.x, 1.0f - t.y * t.y, 1.0f - t.z * t.z);
	}

	// KHR_materials_volume_scatter's per-channel extinction/scatter
	// coefficients for the free-flight random walk - mirrors
	// CpuPathTracer.cpp's computeVolumeScatterCoefficients() exactly
	// (same additive extinction=absorption+scattering composition, needed
	// to match NVIDIA's reference hue - see this feature's plan doc).
	__forceinline__ __device__ void computeVolumeScatterCoefficients(const float3& attenuationColor, float attenuationDistance,
		const float3& multiScatterColor, float3& outExtinction, float3& outScatterCoeff)
	{
		const float3 clampedAtten = make_float3(fmaxf(attenuationColor.x, 0.001f), fmaxf(attenuationColor.y, 0.001f), fmaxf(attenuationColor.z, 0.001f));
		const float safeDistance = fmaxf(attenuationDistance, 0.001f);
		const float3 absCoeff = make_float3(-logf(clampedAtten.x) / safeDistance, -logf(clampedAtten.y) / safeDistance, -logf(clampedAtten.z) / safeDistance);
		const float3 rawSingleAlbedo = multiToSingleScatterAlbedo(multiScatterColor);
		const float3 singleScatterAlbedo = make_float3(
			fminf(fmaxf(rawSingleAlbedo.x, 0.0f), 1.0f),
			fminf(fmaxf(rawSingleAlbedo.y, 0.0f), 1.0f),
			fminf(fmaxf(rawSingleAlbedo.z, 0.0f), 1.0f));
		outScatterCoeff = make_float3(absCoeff.x * singleScatterAlbedo.x, absCoeff.y * singleScatterAlbedo.y, absCoeff.z * singleScatterAlbedo.z);
		outExtinction = absCoeff + outScatterCoeff;
	}

	// Henyey-Greenstein phase-function sampling/pdf - see sampleHenyeyGreenstein()'s
	// definition further down (after buildOrthonormalBasis(), which it needs)
	// for the full doc comment.
	__forceinline__ __device__ float3 sampleHenyeyGreenstein(const float3& wi, float g, float u1, float u2);
	__forceinline__ __device__ float henyeyGreensteinPdf(float cosTheta, float g)
	{
		const float denom = fmaxf(1.0f + g * g - 2.0f * g * cosTheta, 1e-6f);
		return (1.0f - g * g) / (4.0f * kPi * denom * sqrtf(denom));
	}

	// No anisotropy factor exists yet - see sampleHenyeyGreenstein()'s doc
	// comment.
	constexpr float kVolumeScatterAnisotropy = 0.0f;
	constexpr float kVolumeMinScatter = 0.001f;
	constexpr float kVolumeRandFloor = 1.0e-10f;
	constexpr float kVolumeRrFloor = 0.001f;
	constexpr float kVolumeRrCap = 0.95f;
	// Distinguishes a hit reached via the volume-scatter free-flight walk's
	// scatter-event redirect (an explicit-origin continuation, hitFlag==4 -
	// see __closesthit__ch()'s hitBackface/hasVolumeScattering gate) from
	// every other escapeRoughness/hitFlag value. Only matters if THIS
	// redirected ray then escapes straight to the environment without
	// hitting real geometry first - see __miss__ms()'s use of it.
	constexpr float kVolumeScatterEscapeSentinel = -7.0f;

	// Scale-relative self-intersection epsilon, matching CpuPathTracer.cpp's
	// selfIntersectionEpsilon() exactly (a fixed world-space constant is
	// fragile across this app's full size range - too coarse for tiny
	// models, not enough to escape precision loss on large-coordinate ones).
	__forceinline__ __device__ float selfIntersectionEpsilon(const float3& position)
	{
		return fmaxf(1e-4f, sqrtf(dot3(position, position)) * 1e-5f);
	}

	// ---- Texture sampling, ported from CpuPathTracer.cpp's applyWrap()/
	// wrapTexelIndex()/sampleTexture()/applyChannelPacking()/sRGBToLinear()/
	// applyNormalMap() - see those functions' own doc comments for the
	// bilinear/wrap/KHR_texture_transform/channel-packing rationale this
	// reproduces step for step. ----
	constexpr unsigned int kGlClampToEdge    = 0x812Fu;
	constexpr unsigned int kGlMirroredRepeat = 0x8370u;

	__forceinline__ __device__ float applyWrap(float coord, unsigned int mode)
	{
		if (mode == kGlClampToEdge)
			return fminf(fmaxf(coord, 0.0f), 1.0f);
		if (mode == kGlMirroredRepeat)
		{
			const float t = fmodf(fabsf(coord), 2.0f);
			return (t > 1.0f) ? (2.0f - t) : t;
		}
		return coord - floorf(coord); // GL_REPEAT and anything unrecognized
	}

	__forceinline__ __device__ int wrapTexelIndex(int idx, int size, unsigned int mode)
	{
		if (mode == kGlClampToEdge)
			return min(max(idx, 0), size - 1);
		if (mode == kGlMirroredRepeat)
		{
			const int period = 2 * size;
			int m = idx % period;
			if (m < 0) m += period;
			return (m < size) ? m : (period - 1 - m);
		}
		int m = idx % size; // GL_REPEAT and anything unrecognized
		if (m < 0) m += size;
		return m;
	}

	__forceinline__ __device__ float sRGBToLinearChannel(float c)
	{
		return (c <= 0.04045f) ? (c / 12.92f) : powf((c + 0.055f) / 1.055f, 2.4f);
	}

	__forceinline__ __device__ float3 sRGBToLinear(const float3& c)
	{
		return make_float3(sRGBToLinearChannel(c.x), sRGBToLinearChannel(c.y), sRGBToLinearChannel(c.z));
	}

	__forceinline__ __device__ float4 lerp4(const float4& a, const float4& b, float t)
	{
		return make_float4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
	}

	__forceinline__ __device__ float4 fetchTexelWrapped(const uchar4* rgba8, int width, int height,
		unsigned int wrapS, unsigned int wrapT, int x, int y)
	{
		x = wrapTexelIndex(x, width, wrapS);
		y = wrapTexelIndex(y, height, wrapT);
		const uchar4 p = rgba8[static_cast<size_t>(y) * width + x];
		return make_float4(p.x / 255.0f, p.y / 255.0f, p.z / 255.0f, p.w / 255.0f);
	}

	// Bilinear, half-texel-centered, wrap-aware sample of ONE mip level -
	// factored out of sampleTexture2D() so trilinear filtering (below) can
	// call it twice (the two nearest levels) and blend, mirroring
	// CpuPathTracer::bilinearSampleLevel().
	__forceinline__ __device__ float4 bilinearSampleLevel2D(const uchar4* rgba8, int width, int height,
		unsigned int wrapS, unsigned int wrapT, const float2& st)
	{
		const float fx = st.x * static_cast<float>(width)  - 0.5f;
		const float fy = st.y * static_cast<float>(height) - 0.5f;
		const int x0 = static_cast<int>(floorf(fx));
		const int y0 = static_cast<int>(floorf(fy));
		const float tx = fx - static_cast<float>(x0);
		const float ty = fy - static_cast<float>(y0);

		const float4 c00 = fetchTexelWrapped(rgba8, width, height, wrapS, wrapT, x0,     y0);
		const float4 c10 = fetchTexelWrapped(rgba8, width, height, wrapS, wrapT, x0 + 1, y0);
		const float4 c01 = fetchTexelWrapped(rgba8, width, height, wrapS, wrapT, x0,     y0 + 1);
		const float4 c11 = fetchTexelWrapped(rgba8, width, height, wrapS, wrapT, x0 + 1, y0 + 1);

		const float4 top    = lerp4(c00, c10, tx);
		const float4 bottom = lerp4(c01, c11, tx);
		return lerp4(top, bottom, ty);
	}

	// Per-texture mip level for this specific texture's own resolution -
	// device counterpart of CpuPathTracer::computeTextureLod(), see that
	// function's doc comment for the derivation. footprintInUvArea<=0 means
	// "no LOD info for this hit" (bounce/indirect hits, where __closesthit__ch()
	// doesn't compute one) - sampleTexture2D()'s own lod<=0 fast path handles
	// that by sampling the base level only, matching pre-mipmap behavior.
	__forceinline__ __device__ float computeTextureLod(const RtOptixTexture& tex, float footprintInUvArea)
	{
		if (footprintInUvArea <= 0.0f || tex.mipCount <= 1 || tex.mips == nullptr)
			return 0.0f;
		const float footprintInTexelsSq = footprintInUvArea * static_cast<float>(tex.width) * static_cast<float>(tex.height);
		return 0.5f * log2f(fmaxf(footprintInTexelsSq, 1.0f));
	}

	// Bilinear, half-texel-centered, wrap-aware, KHR_texture_transform-aware,
	// trilinear-mip sample - see CpuPathTracer::sampleTexture()'s doc comment
	// for why each of those matters (a nearest-neighbor or wrap-oblivious
	// sample would silently regress the exact bugs that function's own
	// history fixed). Returns raw (not sRGB-decoded) 0-1 RGBA - callers
	// decide whether this texture's bytes are sRGB color data or linear
	// scalar/vector data. lod defaults to 0.0f (base level only, the exact
	// previous behavior) for call sites without per-hit triangle/camera
	// context cheaply available (shadow rays, alpha-cutout existence tests,
	// normal maps) - see CpuPathTracer::sampleTexture()'s identical default
	// and reasoning.
	__forceinline__ __device__ float4 sampleTexture2D(const RtOptixTexture& tex, const float2 uv[4], float lod = 0.0f)
	{
		if (tex.width <= 0 || tex.height <= 0)
			return make_float4(1.0f, 1.0f, 1.0f, 1.0f);

		const float2 rawUv = uv[tex.texCoordIndex];
		float2 st = make_float2(rawUv.x, 1.0f - rawUv.y); // shader's single explicit pre-transform UV.y flip
		st = st * tex.uvScale;
		if (tex.uvRotation != 0.0f)
		{
			const float c = cosf(tex.uvRotation);
			const float s = sinf(tex.uvRotation);
			st = make_float2(st.x * c + st.y * s, -st.x * s + st.y * c);
		}
		st = st + tex.uvOffset;

		st.x = applyWrap(st.x, tex.wrapS);
		st.y = applyWrap(st.y, tex.wrapT);

		if (tex.mipCount <= 0 || tex.mips == nullptr || lod <= 0.0f)
			return bilinearSampleLevel2D(tex.rgba8, tex.width, tex.height, tex.wrapS, tex.wrapT, st);

		const float clampedLod = fminf(fmaxf(lod, 0.0f), static_cast<float>(tex.mipCount - 1));
		const int level0 = static_cast<int>(floorf(clampedLod));
		const int level1 = min(level0 + 1, tex.mipCount - 1);
		const float levelBlend = clampedLod - static_cast<float>(level0);

		const RtOptixTextureMipLevel& mip0 = tex.mips[level0];
		const float4 sample0 = bilinearSampleLevel2D(mip0.rgba8, mip0.width, mip0.height, tex.wrapS, tex.wrapT, st);
		if (level1 == level0)
			return sample0;

		const RtOptixTextureMipLevel& mip1 = tex.mips[level1];
		const float4 sample1 = bilinearSampleLevel2D(mip1.rgba8, mip1.width, mip1.height, tex.wrapS, tex.wrapT, st);
		return lerp4(sample0, sample1, levelBlend);
	}

	__forceinline__ __device__ float applyChannelPacking(const float4& rgba, const RtOptixTexture& tex)
	{
		float v;
		switch (tex.packingChannel)
		{
			case 0:  v = rgba.x; break;
			case 1:  v = rgba.y; break;
			case 2:  v = rgba.z; break;
			case 3:  v = rgba.w; break;
			default: v = rgba.x; break;
		}
		if (tex.packingInvert) v = 1.0f - v;
		return v * tex.packingScale + tex.packingBias;
	}

	// All 4 UV channels, barycentrically interpolated - a texture's KHR-
	// declared texCoordIndex can reference any of them (see
	// sampleTexture2D()'s use site), matching CpuPathTracer::sampleTexture()'s
	// identical per-texture channel selection. Shared by __closesthit__ch()
	// and __anyhit__ah() (the latter only needs this to resolve opacity).
	__forceinline__ __device__ void interpolateUVs(const RtOptixSceneHitGroupData* data, const uint3& tri,
		float w, float u, float v, float2 outUv[4])
	{
		for (int ch = 0; ch < 4; ++ch)
		{
			const float2 t0 = data->texCoords[tri.x * 4 + ch];
			const float2 t1 = data->texCoords[tri.y * 4 + ch];
			const float2 t2 = data->texCoords[tri.z * 4 + ch];
			outUv[ch] = t0 * w + t1 * u + t2 * v;
		}
	}

	// Resolves this hit's opacity via CpuPathTracer::evaluateSurface()'s
	// exact fallback chain: a dedicated opacityTexture (channel-packed) if
	// present, else baseColorTexture's own alpha channel if IT is present,
	// else the flat opacity factor. Shared by __anyhit__ah() (Masked cutout)
	// and, later, the transmission phase's Blend handling.
	__forceinline__ __device__ float resolveOpacity(const RtOptixSceneHitGroupData* data, const float2 uv[4])
	{
		if (data->opacityTexture.width > 0)
			return applyChannelPacking(sampleTexture2D(data->opacityTexture, uv), data->opacityTexture);
		if (data->baseColorTexture.width > 0)
			return sampleTexture2D(data->baseColorTexture, uv).w;
		return data->opacity;
	}

	// Ported from CpuPathTracer::applyNormalMap() - N is the (already world-
	// space, faceforward) shading normal; rawTangentAndHandedness.xyz is the
	// OBJECT-space tangent already transformed to world space by the caller
	// (via optixTransformVectorFromObjectToWorldSpace(), the plain-model-
	// matrix direction transform - NOT normals' inverse-transpose one), .w
	// is the precomputed handedness sign (see RtOptixSceneHitGroupData::
	// tangents' doc comment). Returns N unchanged if there's no normal
	// texture or no tangent data, matching CPU's identical early-outs.
	__forceinline__ __device__ float3 applyNormalMap(const float3& N, const float4& rawTangentAndHandedness,
		const RtOptixTexture& normalTex, const float2 uv[4], float scale)
	{
		if (normalTex.width <= 0)
			return N;

		const float3 rawTangent = make_float3(rawTangentAndHandedness.x, rawTangentAndHandedness.y, rawTangentAndHandedness.z);
		if (sqrtf(dot3(rawTangent, rawTangent)) <= 0.01f)
			return N;

		const float3 T = normalizeF3(rawTangent - N * dot3(rawTangent, N));
		const float3 B = normalizeF3(cross3(N, T)) * rawTangentAndHandedness.w;

		const float4 sampled = sampleTexture2D(normalTex, uv);
		float3 tangentNormal = make_float3(sampled.x * 2.0f - 1.0f, sampled.y * 2.0f - 1.0f, sampled.z * 2.0f - 1.0f);
		tangentNormal.x *= scale;
		tangentNormal.y *= scale;
		tangentNormal = normalizeF3(tangentNormal);

		return normalizeF3(T * tangentNormal.x + B * tangentNormal.y + N * tangentNormal.z);
	}

	// ---- Cook-Torrance terms, ported verbatim from CpuPathTracer.cpp's
	// distributionGGX()/geometrySchlickGGX()/geometrySmith()/fresnelSchlick()
	// (themselves ported from main_scene.frag) so direct specular matches
	// both the CPU tracer and raster PBR exactly. ----
	__forceinline__ __device__ float distributionGGX(float NdotH, float roughness)
	{
		const float a = roughness * roughness;
		const float a2 = a * a;
		const float NdotH2 = NdotH * NdotH;
		float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
		denom = kPi * denom * denom;
		return a2 / fmaxf(denom, 0.001f);
	}

	__forceinline__ __device__ float geometrySchlickGGX(float NdotX, float roughness)
	{
		const float r = roughness + 1.0f;
		const float k = (r * r) / 8.0f;
		return NdotX / (NdotX * (1.0f - k) + k);
	}

	__forceinline__ __device__ float geometrySmith(float NdotV, float NdotL, float roughness)
	{
		return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
	}

	__forceinline__ __device__ float3 fresnelSchlick(float cosTheta, const float3& F0, const float3& F90)
	{
		const float t = powf(fminf(fmaxf(1.0f - cosTheta, 0.0f), 1.0f), 5.0f);
		return lerp3(F0, F90, t);
	}

	// ---- Height-correlated Smith masking-shadowing (Heitz 2014), ported
	// verbatim from CpuPathTracer.cpp's smithLambdaGGX()/smithG1GGX()/
	// smithG2HeightCorrelatedGGX() - the pair the VNDF sampling weight
	// (F * G2/G1, see __closesthit__ch()'s specular-lobe branch) requires.
	// Kept deliberately separate from geometrySmith() above, which is the
	// raster-parity Schlick remapping meant for the direct-lighting BRDF
	// VALUE only; per CPU's own doc comment, mixing the two is inconsistent
	// (the sampling weight must match whichever G the VNDF pdf was derived
	// from for the algebra to cancel). An earlier version of this kernel did
	// exactly that mixing (geometrySmith with raw roughness in the VNDF
	// weight), which systematically, angle-dependently dimmed specular
	// bounce throughput - visible as a smudged, under-defined environment
	// reflection on a glossy dielectric vs the CPU tracer's own result. Note
	// these take ALPHA (roughness^2), not roughness. ----
	__forceinline__ __device__ float smithLambdaGGX(float NdotX, float alpha)
	{
		const float NdotX2 = NdotX * NdotX;
		const float tan2 = fmaxf(0.0f, 1.0f - NdotX2) / fmaxf(NdotX2, 1e-7f);
		return 0.5f * (-1.0f + sqrtf(1.0f + alpha * alpha * tan2));
	}

	__forceinline__ __device__ float smithG1GGX(float NdotX, float alpha)
	{
		return 1.0f / (1.0f + smithLambdaGGX(NdotX, alpha));
	}

	__forceinline__ __device__ float smithG2HeightCorrelatedGGX(float NdotV, float NdotL, float alpha)
	{
		return 1.0f / (1.0f + smithLambdaGGX(NdotV, alpha) + smithLambdaGGX(NdotL, alpha));
	}

	// ---- KHR_materials_anisotropy - ported verbatim from CpuPathTracer.cpp's
	// smithLambdaGGXAniso()/smithG1GGXAniso()/smithG2HeightCorrelatedGGXAniso()/
	// distributionGGXAnisotropic()/visibilityGGXAnisotropic() (themselves from
	// main_scene.frag's D_GGX_anisotropic()/V_GGX_anisotropic()).
	// smithLambdaGGXAniso() reduces to smithLambdaGGX(Xlocal.z, alpha) exactly
	// when alphaX==alphaY (isotropic case), consistent with how
	// sampleGGXVNDF() unifies both cases - Xlocal is X expressed in the
	// (anisotropicT, anisotropicB, N) local frame: (dot(X,T), dot(X,B),
	// dot(X,N)). ----
	__forceinline__ __device__ float smithLambdaGGXAniso(const float3& Xlocal, float alphaX, float alphaY)
	{
		const float NdotX2 = Xlocal.z * Xlocal.z;
		const float ax2 = alphaX * alphaX, ay2 = alphaY * alphaY;
		const float tan2Num = ax2 * Xlocal.x * Xlocal.x + ay2 * Xlocal.y * Xlocal.y;
		return 0.5f * (-1.0f + sqrtf(1.0f + tan2Num / fmaxf(NdotX2, 1e-7f)));
	}

	__forceinline__ __device__ float smithG1GGXAniso(const float3& Xlocal, float alphaX, float alphaY)
	{
		return 1.0f / (1.0f + smithLambdaGGXAniso(Xlocal, alphaX, alphaY));
	}

	__forceinline__ __device__ float smithG2HeightCorrelatedGGXAniso(const float3& Vlocal, const float3& Llocal, float alphaX, float alphaY)
	{
		return 1.0f / (1.0f + smithLambdaGGXAniso(Vlocal, alphaX, alphaY) + smithLambdaGGXAniso(Llocal, alphaX, alphaY));
	}

	// Direct-lighting (NEE) anisotropic Cook-Torrance D/V - V_GGX_anisotropic
	// already bakes in the 1/(4*NdotV*NdotL) visibility term (Khronos spec's
	// "V" function), unlike the isotropic distributionGGX()/geometrySmith()
	// pair which needs that division applied separately at the call site.
	__forceinline__ __device__ float distributionGGXAnisotropic(float NdotH, float TdotH, float BdotH, float at, float ab)
	{
		const float a2 = at * ab;
		const float3 f = make_float3(ab * TdotH, at * BdotH, a2 * NdotH);
		const float w2 = a2 / dot3(f, f);
		return a2 * w2 * w2 / kPi;
	}

	__forceinline__ __device__ float visibilityGGXAnisotropic(float NdotL, float NdotV, float BdotV, float TdotV, float TdotL, float BdotL, float at, float ab)
	{
		const float3 vV = make_float3(at * TdotV, ab * BdotV, NdotV);
		const float3 vL = make_float3(at * TdotL, ab * BdotL, NdotL);
		const float GGXV = NdotL * sqrtf(dot3(vV, vV));
		const float GGXL = NdotV * sqrtf(dot3(vL, vL));
		return fminf(fmaxf(0.5f / (GGXV + GGXL), 0.0f), 1.0f);
	}

	// Ported from decodeAnisotropyTexture() in main_scene.frag (via
	// CpuPathTracer's identically-named function). Without a texture this is
	// a no-op (returns the raw uniform factors unchanged) - only called when
	// a texture is actually present. With a texture, the RG channels
	// ([0,1] -> [-1,1]) give a base direction that the uniform rotation then
	// rotates further, reduced to a single final angle (outRotation) since
	// that's all the tangent-frame construction below needs. The texture's B
	// channel scales the uniform strength.
	__forceinline__ __device__ void decodeAnisotropyTexture(const float3& texelRGB, float uniformStrength, float uniformRotation,
		float& outStrength, float& outRotation)
	{
		float2 direction = make_float2(texelRGB.x * 2.0f - 1.0f, texelRGB.y * 2.0f - 1.0f);
		const float directionLength = sqrtf(direction.x * direction.x + direction.y * direction.y);
		direction = (directionLength < 0.0001f) ? make_float2(1.0f, 0.0f) : (direction * (1.0f / directionLength));

		outStrength = fminf(fmaxf(texelRGB.z * uniformStrength, 0.0f), 1.0f);

		const float c = cosf(uniformRotation);
		const float s = sinf(uniformRotation);
		const float2 rotated = make_float2(c * direction.x - s * direction.y, s * direction.x + c * direction.y);
		outRotation = atan2f(rotated.y, rotated.x);
	}

	// ---- KHR_materials_iridescence, ported verbatim from CpuPathTracer.cpp's
	// evalIridescence()/fresnel0ToIor()/iorToFresnel0()/fSchlickIridescence()/
	// evalSensitivity()/rgbMix()/applyIridescenceToFresnel() (themselves from
	// main_scene.frag). ----
	__forceinline__ __device__ float3 sqf3(const float3& a) { return a * a; }

	__forceinline__ __device__ float3 fresnel0ToIor(const float3& fresnel0)
	{
		const float3 sqrtF0 = make_float3(sqrtf(fresnel0.x), sqrtf(fresnel0.y), sqrtf(fresnel0.z));
		return make_float3((1.0f + sqrtF0.x) / (1.0f - sqrtF0.x), (1.0f + sqrtF0.y) / (1.0f - sqrtF0.y), (1.0f + sqrtF0.z) / (1.0f - sqrtF0.z));
	}

	__forceinline__ __device__ float3 iorToFresnel0_3(const float3& transmittedIor, float incidentIor)
	{
		const float3 d = make_float3(transmittedIor.x - incidentIor, transmittedIor.y - incidentIor, transmittedIor.z - incidentIor);
		const float3 s = make_float3(transmittedIor.x + incidentIor, transmittedIor.y + incidentIor, transmittedIor.z + incidentIor);
		return sqf3(make_float3(d.x / s.x, d.y / s.y, d.z / s.z));
	}

	__forceinline__ __device__ float iorToFresnel0_1(float transmittedIor, float incidentIor)
	{
		const float d = (transmittedIor - incidentIor) / (transmittedIor + incidentIor);
		return d * d;
	}

	__forceinline__ __device__ float fSchlickIridescence1(float f0, float cosTheta, float f90)
	{
		return f0 + (f90 - f0) * powf(fminf(fmaxf(1.0f - cosTheta, 0.0f), 1.0f), 5.0f);
	}

	__forceinline__ __device__ float3 fSchlickIridescence3(const float3& f0, float cosTheta, const float3& f90)
	{
		const float t = powf(fminf(fmaxf(1.0f - cosTheta, 0.0f), 1.0f), 5.0f);
		return f0 + (f90 - f0) * t;
	}

	// XYZ color-matching-function sensitivity curves -> linear sRGB, giving
	// thin-film interference its vibrant, angle-dependent hue shift.
	__forceinline__ __device__ float3 evalSensitivity(float OPD, const float3& shift)
	{
		const float phase = 2.0f * kPi * OPD * 1.0e-9f;
		const float3 val = make_float3(5.4856e-13f, 4.4201e-13f, 5.2481e-13f);
		const float3 pos = make_float3(1.6810e+06f, 1.7953e+06f, 2.2084e+06f);
		const float3 var = make_float3(4.3278e+09f, 9.3046e+09f, 6.6121e+09f);

		float3 xyz;
		xyz.x = val.x * sqrtf(2.0f * kPi * var.x) * cosf(pos.x * phase + shift.x) * expf(-(phase * phase) * var.x);
		xyz.y = val.y * sqrtf(2.0f * kPi * var.y) * cosf(pos.y * phase + shift.y) * expf(-(phase * phase) * var.y);
		xyz.z = val.z * sqrtf(2.0f * kPi * var.z) * cosf(pos.z * phase + shift.z) * expf(-(phase * phase) * var.z);
		xyz.x += 9.7470e-14f * sqrtf(2.0f * kPi * 4.5282e+09f) * cosf(2.2399e+06f * phase + shift.x) * expf(-4.5282e+09f * (phase * phase));
		xyz = xyz * (1.0f / 1.0685e-7f);

		// Matches main_scene.frag's XYZ_TO_REC709 mat3 literal - GLSL's
		// mat3(...) 9-scalar constructor is column-major, matching the row-major
		// listing below transposed the same way CpuPathTracer's glm::mat3
		// (also column-major) reproduces it with the identical 9 values.
		return make_float3(
			3.2404542f * xyz.x + -1.5371385f * xyz.y + -0.4985314f * xyz.z,
			-0.9692660f * xyz.x + 1.8760108f * xyz.y + 0.0415560f * xyz.z,
			0.0556434f * xyz.x + -0.2040259f * xyz.y + 1.0572252f * xyz.z);
	}

	// baseF90 defaults to (1,1,1) to match main_scene.frag's single-arg overload.
	__forceinline__ __device__ float3 evalIridescence(float outsideIOR, float eta2, float cosTheta1, float thinFilmThickness,
		const float3& baseF0, const float3& baseF90)
	{
		const float t = fminf(fmaxf(thinFilmThickness / 0.03f, 0.0f), 1.0f); // smoothstep(0,0.03,thickness), thickness>=0 here
		const float smoothT = t * t * (3.0f - 2.0f * t);
		const float iridescenceIor = outsideIOR + (eta2 - outsideIOR) * smoothT;
		const float sinTheta2Sq = (outsideIOR / iridescenceIor) * (outsideIOR / iridescenceIor) * (1.0f - cosTheta1 * cosTheta1);
		const float cosTheta2Sq = 1.0f - sinTheta2Sq;
		if (cosTheta2Sq < 0.0f)
			return make_float3(1.0f, 1.0f, 1.0f);
		const float cosTheta2 = sqrtf(cosTheta2Sq);

		const float R0 = iorToFresnel0_1(iridescenceIor, outsideIOR);
		const float R12 = fSchlickIridescence1(R0, cosTheta1, 1.0f);
		const float T121 = 1.0f - R12;
		float phi12 = 0.0f;
		if (iridescenceIor < outsideIOR) phi12 = kPi;
		const float phi21 = kPi - phi12;

		const float3 baseF0Clamped = make_float3(fminf(fmaxf(baseF0.x, 0.0f), 0.9999f), fminf(fmaxf(baseF0.y, 0.0f), 0.9999f), fminf(fmaxf(baseF0.z, 0.0f), 0.9999f));
		const float3 baseIOR = fresnel0ToIor(baseF0Clamped);
		const float3 R1 = iorToFresnel0_3(baseIOR, iridescenceIor);
		const float3 R23 = fSchlickIridescence3(R1, cosTheta2, baseF90);
		float3 phi23 = make_float3(0.0f, 0.0f, 0.0f);
		if (baseIOR.x < iridescenceIor) phi23.x = kPi;
		if (baseIOR.y < iridescenceIor) phi23.y = kPi;
		if (baseIOR.z < iridescenceIor) phi23.z = kPi;

		const float OPD = 2.0f * iridescenceIor * thinFilmThickness * cosTheta2;
		const float3 phi = make_float3(phi21 + phi23.x, phi21 + phi23.y, phi21 + phi23.z);

		float3 R123 = R23 * R12;
		R123 = make_float3(fminf(fmaxf(R123.x, 1e-5f), 0.9999f), fminf(fmaxf(R123.y, 1e-5f), 0.9999f), fminf(fmaxf(R123.z, 1e-5f), 0.9999f));
		const float3 r123 = make_float3(sqrtf(R123.x), sqrtf(R123.y), sqrtf(R123.z));
		const float3 T121sq = make_float3(T121 * T121, T121 * T121, T121 * T121);
		const float3 Rs = make_float3(T121sq.x * R23.x, T121sq.y * R23.y, T121sq.z * R23.z) * make_float3(1.0f / (1.0f - R123.x), 1.0f / (1.0f - R123.y), 1.0f / (1.0f - R123.z));

		float3 I = make_float3(R12 + Rs.x, R12 + Rs.y, R12 + Rs.z); // DC term

		float3 Cm = Rs - make_float3(T121, T121, T121);
		for (int m = 1; m <= 2; ++m)
		{
			Cm = Cm * r123;
			const float3 Sm = evalSensitivity(static_cast<float>(m) * OPD, make_float3(static_cast<float>(m) * phi.x, static_cast<float>(m) * phi.y, static_cast<float>(m) * phi.z)) * 2.0f;
			I = I + Cm * Sm;
		}
		return make_float3(fmaxf(I.x, 0.0f), fmaxf(I.y, 0.0f), fmaxf(I.z, 0.0f));
	}

	// Ported from rgb_mix() in main_scene.frag - an energy-conserving mix for
	// iridescent dielectric surfaces. A per-channel-varying Fresnel would let
	// a plain per-channel mix() leave low-Fresnel channels holding onto most
	// of "base", inflating overall brightness; this reduces base by the MAX
	// channel's Fresnel uniformly instead, so no channel keeps more base than
	// the most-reflective channel allows, while per-channel specular coloring
	// is preserved.
	__forceinline__ __device__ float3 rgbMix(const float3& base, const float3& layer, const float3& rgbAlpha)
	{
		const float rgbAlphaMax = fmaxf(fmaxf(rgbAlpha.x, rgbAlpha.y), rgbAlpha.z);
		return base * (1.0f - rgbAlphaMax) + layer * rgbAlpha;
	}

	// Indirect/bounce-sampling side of KHR_materials_iridescence - unlike
	// direct lighting (which replicates main_scene.frag's evaluateBaseDirect()
	// iridescence branch exactly), there's no analytic prefiltered-IBL lookup
	// here to replicate raster's evaluateBaseIBL() iridescence branch against,
	// so this instead blends the ordinary Fresnel term toward
	// evalIridescence()'s angle/thickness-dependent color by
	// iridescenceFactor - ported from CpuPathTracer::applyIridescenceToFresnel().
	__forceinline__ __device__ float3 applyIridescenceToFresnel(const float3& baseFresnel, float cosTheta, const float3& F0,
		float iridescenceFactor, float iridescenceIor, float iridescenceThickness)
	{
		if (iridescenceFactor <= 0.001f || iridescenceThickness <= 0.0f)
			return baseFresnel;
		const float3 F0Clamped = make_float3(fminf(fmaxf(F0.x, 0.0f), 0.9999f), fminf(fmaxf(F0.y, 0.0f), 0.9999f), fminf(fmaxf(F0.z, 0.0f), 0.9999f));
		const float3 iridescent = evalIridescence(1.0f, iridescenceIor, fminf(fmaxf(cosTheta, 0.0f), 1.0f), iridescenceThickness, F0Clamped, make_float3(1.0f, 1.0f, 1.0f));
		return baseFresnel * (1.0f - iridescenceFactor) + iridescent * iridescenceFactor;
	}

	// ---- KHR_materials_clearcoat, ported verbatim from CpuPathTracer.cpp's
	// evaluateClearcoatDirect()/computeClearcoatFresnel() (themselves from
	// main_scene.frag). Note evaluateClearcoatDirect() deliberately reuses
	// distributionGGX()/geometrySmith() (which re-square their "roughness"
	// argument internally) by passing clearcoatRoughness^2 (alpha) rather than
	// clearcoatRoughness directly, and has no NdotL factor at all (divides by
	// NdotV only) - both kept verbatim to match the shader's own clearcoat
	// call site exactly rather than "fixing" it into a more standard form. ----
	__forceinline__ __device__ float3 evaluateClearcoatDirect(const float3& Ncoat, const float3& V, const float3& L,
		float clearcoat, float clearcoatRoughness)
	{
		if (clearcoat <= 0.0f)
			return make_float3(0.0f, 0.0f, 0.0f);

		const float3 H = normalizeF3(V + L);
		const float NdotL = fmaxf(dot3(Ncoat, L), 0.0f);
		const float NdotV = fmaxf(dot3(Ncoat, V), 0.0f);
		const float NdotH = fmaxf(dot3(Ncoat, H), 0.0f);
		if (NdotL <= 0.0f || NdotV <= 0.0f)
			return make_float3(0.0f, 0.0f, 0.0f);

		const float alpha = clearcoatRoughness * clearcoatRoughness;
		const float D = distributionGGX(NdotH, alpha);
		const float G = geometrySmith(NdotV, NdotL, alpha);
		const float clearcoatBRDF = (D * G) / fmaxf(4.0f * NdotV, 0.001f);
		return make_float3(clearcoatBRDF * clearcoat, clearcoatBRDF * clearcoat, clearcoatBRDF * clearcoat);
	}

	// clamp(ior, 1, inf)-derived dielectric F0, the coat layer's fixed Fresnel
	// reflectance - unlike the base layer, the coat never tints via
	// specularColorFactor or mixes toward metalness.
	__forceinline__ __device__ float3 computeClearcoatFresnel(float ior, const float3& Ncoat, const float3& V)
	{
		const float clearcoatIor = fmaxf(ior, 1.0f);
		const float f0Scalar = powf((clearcoatIor - 1.0f) / (clearcoatIor + 1.0f), 2.0f);
		const float NdotV = fminf(fmaxf(dot3(Ncoat, V), 0.0f), 1.0f);
		return fresnelSchlick(NdotV, make_float3(f0Scalar, f0Scalar, f0Scalar), make_float3(1.0f, 1.0f, 1.0f));
	}

	// ---- KHR_materials_sheen, ported verbatim from CpuPathTracer.cpp's
	// distributionCharlie()/lambdaSheen()/visibilitySheen()/calculateSheen()
	// (themselves from main_scene.frag). ----
	__forceinline__ __device__ float distributionCharlie(float NdotH, float roughness)
	{
		const float alpha = fmaxf(roughness * roughness, 0.000001f);
		const float invAlpha = 1.0f / alpha;
		const float sin2h = fmaxf(1.0f - NdotH * NdotH, 0.0078125f); // 2^(-7)
		return (2.0f + invAlpha) * powf(sin2h, invAlpha * 0.5f) / (2.0f * kPi);
	}

	__forceinline__ __device__ float lambdaSheenNumericHelper(float x, float alphaG)
	{
		const float oneMinusAlphaSq = (1.0f - alphaG) * (1.0f - alphaG);
		const float a = 21.5473f + (25.3245f - 21.5473f) * oneMinusAlphaSq;
		const float b = 3.82987f + (3.32435f - 3.82987f) * oneMinusAlphaSq;
		const float c = 0.19823f + (0.16801f - 0.19823f) * oneMinusAlphaSq;
		const float d = -1.97760f + (-1.27393f - (-1.97760f)) * oneMinusAlphaSq;
		const float e = -4.32054f + (-4.85967f - (-4.32054f)) * oneMinusAlphaSq;
		return a / (1.0f + b * powf(x, c)) + d * x + e;
	}

	__forceinline__ __device__ float lambdaSheen(float cosTheta, float alphaG)
	{
		if (fabsf(cosTheta) < 0.5f)
			return expf(lambdaSheenNumericHelper(cosTheta, alphaG));
		return expf(2.0f * lambdaSheenNumericHelper(0.5f, alphaG) -
			lambdaSheenNumericHelper(1.0f - cosTheta, alphaG));
	}

	__forceinline__ __device__ float visibilitySheen(float NdotL, float NdotV, float sheenRoughness)
	{
		sheenRoughness = fmaxf(sheenRoughness, 0.000001f);
		const float alphaG = sheenRoughness * sheenRoughness;
		return fminf(fmaxf(1.0f / ((1.0f + lambdaSheen(NdotV, alphaG) + lambdaSheen(NdotL, alphaG)) * (4.0f * NdotV * NdotL)), 0.0f), 1.0f);
	}

	__forceinline__ __device__ float3 calculateSheen(const float3& N, const float3& V, const float3& L, const float3& sheenColor, float sheenRoughness)
	{
		const float3 H = normalizeF3(V + L);
		const float NdotL = fminf(fmaxf(dot3(N, L), 0.0f), 1.0f);
		const float NdotV = fminf(fmaxf(dot3(N, V), 0.0f), 1.0f);
		const float NdotH = fminf(fmaxf(dot3(N, H), 0.0f), 1.0f);
		if (NdotL <= 0.0f || NdotV <= 0.0f)
			return make_float3(0.0f, 0.0f, 0.0f);

		const float sheenRoughFinal = fminf(fmaxf(sheenRoughness, 0.000001f), 1.0f);
		const float D = distributionCharlie(NdotH, sheenRoughFinal);
		const float V_sheen = visibilitySheen(NdotL, NdotV, sheenRoughFinal);
		return sheenColor * (D * V_sheen * NdotL);
	}

	// Bilinear lookup into the same Khronos sheen LUT data raster samples with
	// GL_LINEAR filtering. Guards against a missing/failed upload by returning
	// 0 (no dampening/add-back).
	__forceinline__ __device__ float sampleSheenAlbedoLUT(const float* lut, int lutSize, float NdotV, float roughness)
	{
		if (!lut || lutSize <= 0)
			return 0.0f;
		const float x = fminf(fmaxf(NdotV, 0.0f), 1.0f) * static_cast<float>(lutSize - 1);
		const float y = fminf(fmaxf(roughness, 0.0f), 1.0f) * static_cast<float>(lutSize - 1);
		const int x0 = min(max(static_cast<int>(floorf(x)), 0), lutSize - 1);
		const int y0 = min(max(static_cast<int>(floorf(y)), 0), lutSize - 1);
		const int x1 = min(x0 + 1, lutSize - 1);
		const int y1 = min(y0 + 1, lutSize - 1);
		const float tx = x - static_cast<float>(x0);
		const float ty = y - static_cast<float>(y0);
		const float v00 = lut[static_cast<size_t>(y0) * lutSize + x0];
		const float v10 = lut[static_cast<size_t>(y0) * lutSize + x1];
		const float v01 = lut[static_cast<size_t>(y1) * lutSize + x0];
		const float v11 = lut[static_cast<size_t>(y1) * lutSize + x1];
		const float vx0 = v00 + (v10 - v00) * tx;
		const float vx1 = v01 + (v11 - v01) * tx;
		return vx0 + (vx1 - vx0) * ty;
	}

	__forceinline__ __device__ float sampleSheenIblEnergy(float NdotV, float roughness)
	{
		const float eSheen = sampleSheenAlbedoLUT(params.sheenAlbedoLUT, params.sheenAlbedoLUTSize, NdotV, roughness);
		if (!params.sheenCharlieLUT)
			return eSheen;
		const float eCharlie = sampleSheenAlbedoLUT(params.sheenCharlieLUT, params.sheenAlbedoLUTSize, NdotV, roughness);
		return fminf(eCharlie, eSheen);
	}

	// Ported from CpuPathTracer::computeLobeProbabilities(): base specular
	// probability plus a clearcoat probability, both boosted by their own
	// (1-roughness)^2 smoothness term for the same under-sampling reason the
	// base specular lobe's own smoothness boost exists (see
	// __closesthit__ch()'s specProb comment) - a smooth coat's narrow
	// reflection is otherwise starved to the probability floor regardless of
	// view angle. Also boosted by anisotropyStrength^2 - VNDF-sampling a
	// STRETCHED anisotropic lobe (anisoAlphaT widened toward 1) has higher
	// per-sample variance than an isotropic lobe of the same average
	// roughness, so resolving a "brushed metal" highlight cleanly needs more
	// of the sample budget directed at this lobe, not just correct
	// importance sampling once chosen - see CpuPathTracer::
	// computeLobeProbabilities()'s identical boost for the full rationale
	// (reported by the user comparing AnisotropyBarnLamp's raster - a smooth,
	// noise-free swept ring - against a patchier PT highlight at ordinary
	// sample counts).
	__forceinline__ __device__ void computeLobeProbabilities(const float3& F0, float metalness, float roughness,
		const float3& clearcoatBlend, float clearcoat, float clearcoatRoughness, float anisotropyStrength,
		float& outSpecProb, float& outCoatProb)
	{
		const float smoothness = 1.0f - roughness;
		const float anisotropyBoost = anisotropyStrength * anisotropyStrength;
		outSpecProb = fminf(fmaxf((F0.x + F0.y + F0.z) * (1.0f / 3.0f) + 0.5f * metalness + 0.5f * smoothness * smoothness
			+ 0.5f * anisotropyBoost, 0.05f), 0.95f);

		const float coatSmoothness = 1.0f - clearcoatRoughness;
		outCoatProb = clearcoat > 0.0f
			? fminf(fmaxf((clearcoatBlend.x + clearcoatBlend.y + clearcoatBlend.z) / 3.0f + 0.5f * clearcoat * coatSmoothness * coatSmoothness, 0.05f), 0.9f)
			: 0.0f;
	}

	// Heitz 2018 VNDF importance-sampling pdf (isotropic case) - the pdf the
	// general specular/coat lobes actually sample from, evaluated here for an
	// ARBITRARY given L rather than a freshly-sampled one (needed for
	// environment-NEE's MIS weighting below - see evaluateBsdfPdf()). Ported
	// from CpuPathTracer::ggxVndfPdfIsotropic() verbatim, including its
	// unclamped-D rationale (NOT distributionGGX(), which floors its
	// denominator for direct-light-BRDF safety against an exactly-aligned
	// punctual light - that floor is wrong for a pdf evaluation, since it
	// under-estimates D exactly where a smooth material's VNDF-sampled H
	// lands, which previously handed MIS most of its weight to environment-
	// NEE and visibly darkened mirror reflections).
	__forceinline__ __device__ float ggxVndfPdfIsotropic(const float3& N, const float3& V, const float3& L, float roughness)
	{
		const float NdotV = fmaxf(dot3(N, V), 0.0f);
		const float NdotL = fmaxf(dot3(N, L), 0.0f);
		if (NdotV <= 0.0f || NdotL <= 0.0f)
			return 0.0f;

		const float3 H = normalizeF3(V + L);
		const float NdotH = fmaxf(dot3(N, H), 0.0f);
		const float VdotH = fmaxf(dot3(V, H), 0.0f);
		if (NdotH <= 0.0f || VdotH <= 0.0f)
			return 0.0f;

		const float alpha = roughness * roughness;
		const float a2 = alpha * alpha;
		const float NdotH2 = NdotH * NdotH;
		const float denomTerm = NdotH2 * a2 + (1.0f - NdotH2);
		const float D = a2 / fmaxf(kPi * denomTerm * denomTerm, 1e-12f);

		const float G1v = smithG1GGX(NdotV, alpha);
		return (G1v * D * VdotH) / fmaxf(NdotV, 1e-6f) / fmaxf(4.0f * VdotH, 1e-6f);
	}

	// Combined sampling pdf of the general lobe-selection code (diffuse/
	// specular/coat) for an arbitrary given direction L - used only for
	// environment-NEE's MIS weighting (see __closesthit__ch()'s
	// environment-NEE block). Ported from CpuPathTracer::evaluateBsdfPdf()
	// verbatim - must match the lobe-selection code's actual three-way split
	// exactly (coat picked first with probability coatProb, only the
	// remainder further split between spec/diffuse).
	__forceinline__ __device__ float evaluateBsdfPdf(const float3& N, const float3& Ncoat, const float3& V, const float3& L,
		float roughness, float clearcoatRoughness, bool hasAniso, const float3& anisoT, const float3& anisoB,
		float alphaT, float alphaB, float specProb, float coatProb)
	{
		const float NdotL = dot3(N, L);
		if (NdotL <= 0.0f)
			return 0.0f;

		const float specProbScaled = specProb * (1.0f - coatProb);
		const float diffuseProb = fmaxf(1.0f - coatProb - specProbScaled, 0.0f);

		const float coatPdf = coatProb > 0.0f ? ggxVndfPdfIsotropic(Ncoat, V, L, clearcoatRoughness) : 0.0f;

		float specPdf;
		if (hasAniso)
		{
			const float NdotV = fmaxf(dot3(N, V), 0.0f);
			const float3 H = normalizeF3(V + L);
			const float NdotH = fmaxf(dot3(N, H), 0.0f);
			const float VdotH = fmaxf(dot3(V, H), 0.0f);
			if (NdotV <= 0.0f || NdotH <= 0.0f || VdotH <= 0.0f)
				specPdf = 0.0f;
			else
			{
				const float TdotH = dot3(anisoT, H), BdotH = dot3(anisoB, H);
				const float D = distributionGGXAnisotropic(NdotH, TdotH, BdotH, alphaT, alphaB);
				const float3 Vlocal = make_float3(dot3(V, anisoT), dot3(V, anisoB), NdotV);
				const float G1v = smithG1GGXAniso(Vlocal, alphaT, alphaB);
				specPdf = (G1v * D * VdotH) / fmaxf(NdotV, 1e-6f) / fmaxf(4.0f * VdotH, 1e-6f);
			}
		}
		else
		{
			specPdf = ggxVndfPdfIsotropic(N, V, L, roughness);
		}

		const float cosinePdf = NdotL / kPi;
		return coatProb * coatPdf + specProbScaled * specPdf + diffuseProb * cosinePdf;
	}

	// Ported from CpuPathTracer::evaluateDirectBRDF() - the direct-light BRDF
	// evaluated at an ARBITRARY given L, needed by the environment-NEE block
	// below (which needs the same BRDF the punctual-light loop's own inline
	// computation in __closesthit__ch() evaluates, just at the env-sampled
	// direction instead of a light's). Kept as a standalone function (not
	// shared with that inline loop) to avoid touching its already-working
	// code path.
	__forceinline__ __device__ float3 evaluateDirectBRDF(const float3& N, const float3& V, const float3& L,
		const float3& baseColor, float metalness, const float3& directF0, const float3& F90, float roughness,
		bool hasAniso, const float3& anisoT, const float3& anisoB, float alphaT, float alphaB,
		int useSpecGloss, float transmission, float diffuseTransmissionFactor,
		float iridescenceFactor, float iridescenceIor, float iridescenceThickness,
		const float3& dielectricF0, const float3& dielectricDirectF0, float texturedSpecularFactor)
	{
		const float NdotL = fmaxf(dot3(N, L), 0.0f);
		const float NdotV = fmaxf(dot3(N, V), 0.0f);
		if (NdotL <= 0.0f || NdotV <= 0.0f)
			return make_float3(0.0f, 0.0f, 0.0f);

		const float3 H = normalizeF3(V + L);
		const float NdotH = fmaxf(dot3(N, H), 0.0f);
		const float VdotH = fminf(fmaxf(dot3(H, V), 0.0f), 1.0f);

		const float3 F = fresnelSchlick(VdotH, directF0, F90);

		float3 specularNoF;
		if (hasAniso)
		{
			const float D_aniso = distributionGGXAnisotropic(NdotH, dot3(anisoT, H), dot3(anisoB, H), alphaT, alphaB);
			const float V_aniso = visibilityGGXAnisotropic(NdotL, NdotV,
				dot3(anisoB, V), dot3(anisoT, V), dot3(anisoT, L), dot3(anisoB, L), alphaT, alphaB);
			const float dv = D_aniso * V_aniso;
			specularNoF = make_float3(dv, dv, dv);
		}
		else
		{
			const float D = distributionGGX(NdotH, roughness);
			const float G = geometrySmith(NdotV, NdotL, roughness);
			const float dg = (D * G) / fmaxf(4.0f * NdotV * NdotL, 0.001f);
			specularNoF = make_float3(dg, dg, dg);
		}
		const float3 specular = specularNoF * F;

		if (useSpecGloss != 0)
		{
			const float3 l_diffuse = baseColor * (1.0f / kPi);
			return lerp3(l_diffuse, specular, F) * NdotL;
		}

		const float3 kD = (make_float3(1.0f, 1.0f, 1.0f) - F) * (1.0f - metalness);
		const float3 diffuse = kD * baseColor * (1.0f / kPi) * (1.0f - transmission) * (1.0f - diffuseTransmissionFactor);

		if (iridescenceFactor > 0.001f && iridescenceThickness > 0.0f)
		{
			const float3 l_diffuse = diffuse * NdotL;
			const float3 l_specular = specularNoF * NdotL * (1.0f - transmission);

			const float3 dielectricFresnel = fresnelSchlick(VdotH, dielectricDirectF0,
				make_float3(texturedSpecularFactor, texturedSpecularFactor, texturedSpecularFactor));
			const float3 metalFresnel = fresnelSchlick(VdotH, baseColor, make_float3(1.0f, 1.0f, 1.0f));
			float3 dielectricBrdf = lerp3(l_diffuse, l_specular, dielectricFresnel);
			float3 metalBrdf = metalFresnel * l_specular;

			const float3 iridescenceFresnelDielectric = evalIridescence(1.0f, iridescenceIor, NdotV, iridescenceThickness, dielectricF0, make_float3(1.0f, 1.0f, 1.0f));
			const float3 iridescenceFresnelMetallic = evalIridescence(1.0f, iridescenceIor, NdotV, iridescenceThickness, baseColor, make_float3(1.0f, 1.0f, 1.0f));
			metalBrdf = lerp3(metalBrdf, l_specular * iridescenceFresnelMetallic, iridescenceFactor);
			dielectricBrdf = lerp3(dielectricBrdf, rgbMix(l_diffuse, l_specular, iridescenceFresnelDielectric), iridescenceFactor);

			return lerp3(dielectricBrdf, metalBrdf, metalness);
		}

		return (diffuse + specular * (1.0f - transmission)) * NdotL;
	}

	// Small, fast, self-contained hash-based PRNG (PCG variant, same
	// technique as the OptiX SDK samples' rng.h - reimplemented here rather
	// than pulled in, matching this file's own no-sutil-dependency style).
	// Seeded per (pixel, sample) below, so different samples of the same
	// pixel get decorrelated jitter without needing any persistent
	// per-launch state.
	__forceinline__ __device__ unsigned int pcgHash(unsigned int seed)
	{
		unsigned int state = seed * 747796405u + 2891336453u;
		unsigned int word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
		return (word >> 22u) ^ word;
	}

	// Maps a hash to [0, 1).
	__forceinline__ __device__ float hashToUnitFloat(unsigned int h)
	{
		return static_cast<float>(h) * (1.0f / 4294967296.0f);
	}

	__forceinline__ __device__ void setPayload(float3 p)
	{
		optixSetPayload_0(__float_as_uint(p.x));
		optixSetPayload_1(__float_as_uint(p.y));
		optixSetPayload_2(__float_as_uint(p.z));
	}

	// Branchless orthonormal basis from a unit normal (Duff et al. 2017,
	// "Building an Orthonormal Basis, Revisited") - used to build a local
	// tangent frame for BSDF importance sampling below. Doesn't need to
	// align with the mesh's own authored tangent (that's only for normal
	// mapping) since roughness is isotropic here - anisotropy is deferred.
	__forceinline__ __device__ void buildOrthonormalBasis(const float3& N, float3& T, float3& B)
	{
		const float sign = N.z >= 0.0f ? 1.0f : -1.0f;
		const float a = -1.0f / (sign + N.z);
		const float b = N.x * N.y * a;
		T = make_float3(1.0f + sign * N.x * N.x * a, sign * b, -sign * N.x);
		B = make_float3(b, sign + N.y * N.y * a, -N.y);
	}

	// Henyey-Greenstein phase-function sampling - mirrors CpuPathTracer.cpp's
	// identical sampleHenyeyGreenstein() exactly (standard formula, e.g.
	// PBRT's HenyeyGreenstein - not proprietary). wi is the ray's incoming
	// travel direction (NOT negated); cosTheta is measured between wi and
	// the sampled outgoing direction, so g>0 biases toward continuing
	// forward (dot near 1). g=0 (isotropic) degenerates exactly to uniform-
	// sphere sampling - every call site passes g=0.0f for now (no
	// scatterAnisotropy field exists anywhere in this codebase's material
	// pipeline yet). Defined here (after buildOrthonormalBasis(), which it
	// needs) rather than alongside its henyeyGreensteinPdf()/
	// computeVolumeScatterCoefficients() siblings further up - see this
	// function's forward declaration there.
	__forceinline__ __device__ float3 sampleHenyeyGreenstein(const float3& wi, float g, float u1, float u2)
	{
		float cosTheta;
		if (fabsf(g) < 1e-3f)
		{
			cosTheta = 1.0f - 2.0f * u1;
		}
		else
		{
			const float sqrTerm = (1.0f - g * g) / (1.0f + g - 2.0f * g * u1);
			cosTheta = (1.0f + g * g - sqrTerm * sqrTerm) / (2.0f * g);
		}
		const float sinTheta = sqrtf(fmaxf(0.0f, 1.0f - cosTheta * cosTheta));
		const float phi = 2.0f * kPi * u2;
		float3 t, b;
		buildOrthonormalBasis(wi, t, b);
		return normalizeF3(t * (sinTheta * cosf(phi)) + b * (sinTheta * sinf(phi)) + wi * cosTheta);
	}

	// Cosine-weighted hemisphere sample in tangent space (Z-up) - the
	// standard importance sampler for a Lambertian diffuse lobe, whose
	// pdf(L)=NdotL/pi exactly cancels the BRDF's own NdotL/pi term, leaving
	// throughput *= albedo with no further division needed (see this file's
	// diffuse-lobe branch in __closesthit__ch()).
	__forceinline__ __device__ float3 cosineSampleHemisphere(float u1, float u2)
	{
		const float r = sqrtf(u1);
		const float phi = 2.0f * kPi * u2;
		return make_float3(r * cosf(phi), r * sinf(phi), sqrtf(fmaxf(0.0f, 1.0f - u1)));
	}

	// GGX Visible Normal Distribution Function sample (Heitz 2018, "Sampling
	// the GGX Distribution of Visible Normals"). Ve is the view direction in
	// TANGENT space (Z-up); returns the sampled half-vector H, also in
	// tangent space. Reflecting the (tangent-space or, as used here, world-
	// space-via-the-same-basis) view direction around this H gives a
	// specular-lobe-importance-sampled bounce direction, whose throughput
	// weight is F*G2/G1 (the well-known VNDF-sampling simplification - see
	// this file's specular-lobe branch in __closesthit__ch()). The algorithm
	// as published is already anisotropic-capable (separate alphaX/alphaY
	// roughness scaling) - ported from CpuPathTracer::sampleGGXVNDF(), whose
	// own doc comment notes alphaX/alphaY are only ever equal for isotropic
	// materials, letting KHR_materials_anisotropy's stretched lobe reuse this
	// same sampler (with its own rotated tangent frame) rather than needing
	// a separate one. Existing isotropic call sites just pass alpha for both.
	__forceinline__ __device__ float3 sampleGGXVNDF(const float3& Ve, float alphaX, float alphaY, float u1, float u2)
	{
		const float3 Vh = normalizeF3(make_float3(alphaX * Ve.x, alphaY * Ve.y, Ve.z));

		const float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
		const float3 T1 = lensq > 0.0f ? (make_float3(-Vh.y, Vh.x, 0.0f) * rsqrtf(lensq)) : make_float3(1.0f, 0.0f, 0.0f);
		const float3 T2 = cross3(Vh, T1);

		const float r = sqrtf(u1);
		const float phi = 2.0f * kPi * u2;
		float t1 = r * cosf(phi);
		float t2 = r * sinf(phi);
		const float s = 0.5f * (1.0f + Vh.z);
		t2 = (1.0f - s) * sqrtf(fmaxf(0.0f, 1.0f - t1 * t1)) + s * t2;

		const float3 Nh = T1 * t1 + T2 * t2 + Vh * sqrtf(fmaxf(0.0f, 1.0f - t1 * t1 - t2 * t2));

		return normalizeF3(make_float3(alphaX * Nh.x, alphaY * Nh.y, fmaxf(0.0f, Nh.z)));
	}

	// Ported verbatim from CpuPathTracer::evaluatePunctualLight() (itself
	// ported from main_scene.frag's evaluatePunctualLight()) - KHR_lights_
	// punctual's range/spot attenuation, so direct lighting matches both the
	// raster pass and the CPU tracer's own direct-lighting term exactly.
	__forceinline__ __device__ void evaluatePunctualLight(const RtOptixLight& light, const float3& surfacePos,
		float3& outDir, float3& outIntensity, float& outDistance)
	{
		outDir = make_float3(0.0f, 0.0f, 0.0f);
		outIntensity = make_float3(0.0f, 0.0f, 0.0f);
		outDistance = 1e30f;

		if (light.type == 0) // Directional
		{
			outDir = normalizeF3(light.direction) * -1.0f;
			outIntensity = light.color * light.intensity;
			return;
		}

		const float3 pointToLight = light.position - surfacePos;
		const float distance = sqrtf(dot3(pointToLight, pointToLight));
		if (distance <= 1e-6f)
			return;

		outDir = pointToLight * (1.0f / distance);
		outDistance = distance;

		// range < 0 marks the app/raster default light: positional direction,
		// constant intensity. KHR_lights_punctual point/spot lights keep their
		// spec-defined inverse-square attenuation when range >= 0.
		float rangeAttenuation = (light.range < 0.0f) ? 1.0f : 1.0f / (distance * distance);
		if (light.range > 0.0f)
		{
			const float ratio = distance / light.range;
			const float distAttenuation = 1.0f - ratio * ratio * ratio * ratio;
			rangeAttenuation = fminf(fmaxf(distAttenuation, 0.0f), 1.0f) / (distance * distance);
		}

		float spotAttenuation = 1.0f;
		if (light.type == 2) // Spot
		{
			const float3 lightDirWorld = normalizeF3(light.direction);
			const float actualCos = dot3(lightDirWorld, normalizeF3(pointToLight * -1.0f));
			if (actualCos > light.outerConeCos)
			{
				if (actualCos < light.innerConeCos)
				{
					const float angularAtten = (actualCos - light.outerConeCos) / (light.innerConeCos - light.outerConeCos);
					spotAttenuation = angularAtten * angularAtten;
				}
			}
			else
			{
				spotAttenuation = 0.0f;
			}
		}

		outIntensity = light.color * (light.intensity * rangeAttenuation * spotAttenuation);
	}

	// ---- Environment sampling, ported verbatim from CpuPathTracer.cpp's
	// selectCubemapFaceUV()/sampleCubemapFaces()/undoSkyboxRotation()/
	// flatGradientMiss()/sampleFallbackBackgroundGradient() so the GPU
	// backend samples the identical captured cubemap the same way. ----
	__forceinline__ __device__ void selectCubemapFaceUV(const float3& dir, int& face, float& u, float& v)
	{
		const float ax = fabsf(dir.x), ay = fabsf(dir.y), az = fabsf(dir.z);
		float sc, tc, ma;
		if (ax >= ay && ax >= az)
		{
			ma = ax;
			if (dir.x > 0.0f) { face = 0; sc = -dir.z; tc = -dir.y; } // +X
			else              { face = 1; sc =  dir.z; tc = -dir.y; } // -X
		}
		else if (ay >= ax && ay >= az)
		{
			ma = ay;
			if (dir.y > 0.0f) { face = 2; sc = dir.x; tc =  dir.z; } // +Y
			else              { face = 3; sc = dir.x; tc = -dir.z; } // -Y
		}
		else
		{
			ma = az;
			if (dir.z > 0.0f) { face = 4; sc =  dir.x; tc = -dir.y; } // +Z
			else              { face = 5; sc = -dir.x; tc = -dir.y; } // -Z
		}
		u = (sc / ma + 1.0f) * 0.5f;
		v = (tc / ma + 1.0f) * 0.5f;
	}

	// Inverse of selectCubemapFaceUV() - reconstructs an (unnormalized)
	// direction from a face index and u/v in [0,1]. Matches
	// RtEnvironmentSampler's own faceScToDirection() exactly, just
	// re-expressed in u/v (converted to that function's [-1,1] sc/tc here)
	// instead of taking sc/tc directly.
	__forceinline__ __device__ float3 faceUVToDirection(int face, float u, float v)
	{
		const float sc = u * 2.0f - 1.0f;
		const float tc = v * 2.0f - 1.0f;
		switch (face)
		{
			case 0:  return make_float3(1.0f, -tc, -sc);
			case 1:  return make_float3(-1.0f, -tc, sc);
			case 2:  return make_float3(sc, 1.0f, tc);
			case 3:  return make_float3(sc, -1.0f, -tc);
			case 4:  return make_float3(sc, -tc, 1.0f);
			default: return make_float3(-sc, -tc, -1.0f);
		}
	}

	// Device counterpart of RtEnvironmentSampler::sample()/pdf() - importance
	// samples (or evaluates the pdf of) the SAME luminance-weighted flat CDF
	// CPU builds and RtOptixSceneTracer::buildScene() uploads verbatim (see
	// RtOptixEnvironment::envFlatCdf's doc comment), so both engines converge
	// toward the identical environment-NEE distribution. u0 picks a texel
	// (binary search over the cumulative array, matching std::upper_bound);
	// u1/u2 jitter continuously within that texel.
	__forceinline__ __device__ void envSamplerSample(const RtOptixEnvironment& env, float u0, float u1, float u2, float3& outDir, float& outPdf)
	{
		if (env.envFlatCdf == nullptr || env.envTexelPdf == nullptr || env.envTotalWeight <= 0.0f || env.faceSize <= 0)
		{
			outDir = make_float3(0.0f, 1.0f, 0.0f);
			outPdf = 0.0f;
			return;
		}

		const size_t faceSize = static_cast<size_t>(env.faceSize);
		const size_t texelsPerFace = faceSize * faceSize;
		const size_t totalTexels = texelsPerFace * 6;

		const float target = fminf(fmaxf(u0, 0.0f), 1.0f) * env.envTotalWeight;
		size_t lo = 0, hi = totalTexels + 1; // upper_bound over envFlatCdf[0..totalTexels]
		while (lo < hi)
		{
			const size_t mid = lo + (hi - lo) / 2;
			if (env.envFlatCdf[mid] <= target)
				lo = mid + 1;
			else
				hi = mid;
		}
		size_t flatIndex = lo - 1; // lo>=1 always: envFlatCdf[0]==0.0f<=target (target>=0) guarantees at least one advance
		if (flatIndex > totalTexels - 1)
			flatIndex = totalTexels - 1;

		const int face = static_cast<int>(flatIndex / texelsPerFace);
		const size_t rem = flatIndex % texelsPerFace;
		const int y = static_cast<int>(rem / faceSize);
		const int x = static_cast<int>(rem % faceSize);

		const float invSize = 1.0f / static_cast<float>(env.faceSize);
		const float u = (static_cast<float>(x) + u1) * invSize;
		const float v = (static_cast<float>(y) + u2) * invSize;

		outDir = normalizeF3(faceUVToDirection(face, u, v));
		outPdf = env.envTexelPdf[flatIndex];
	}

	__forceinline__ __device__ float envSamplerPdf(const RtOptixEnvironment& env, const float3& direction)
	{
		if (env.envTexelPdf == nullptr || env.envTotalWeight <= 0.0f || env.faceSize <= 0)
			return 0.0f;

		int face;
		float u, v;
		selectCubemapFaceUV(normalizeF3(direction), face, u, v);

		const int faceSize = env.faceSize;
		const int x = min(max(static_cast<int>(u * static_cast<float>(faceSize)), 0), faceSize - 1);
		const int y = min(max(static_cast<int>(v * static_cast<float>(faceSize)), 0), faceSize - 1);

		const size_t flatIndex = static_cast<size_t>(face) * faceSize * faceSize + static_cast<size_t>(y) * faceSize + x;
		return env.envTexelPdf[flatIndex];
	}

	// Takes a raw faces[6]/size pair rather than a whole RtOptixEnvironment so
	// it can sample either the raw map (env.faces/env.faceSize) or any one
	// prefilter mip level (RtOptixPrefilterMip::faces/faceSize) - matches
	// CpuPathTracer's own sampleCubemapFaces(faces[6], size, direction) doing
	// double duty the same way.
	__forceinline__ __device__ float3 sampleCubemapFaces(float3* const* faces, int size, const float3& direction)
	{
		int face;
		float u, v;
		selectCubemapFaceUV(normalizeF3(direction), face, u, v);

		const float3* faceData = faces[face];

		const float fx = fminf(fmaxf(u * static_cast<float>(size) - 0.5f, 0.0f), static_cast<float>(size - 1));
		const float fy = fminf(fmaxf(v * static_cast<float>(size) - 0.5f, 0.0f), static_cast<float>(size - 1));

		const int x0 = static_cast<int>(fx);
		const int y0 = static_cast<int>(fy);
		const int x1 = min(x0 + 1, size - 1);
		const int y1 = min(y0 + 1, size - 1);
		const float tx = fx - static_cast<float>(x0);
		const float ty = fy - static_cast<float>(y0);

		const float3 c00 = faceData[static_cast<size_t>(y0) * size + x0];
		const float3 c10 = faceData[static_cast<size_t>(y0) * size + x1];
		const float3 c01 = faceData[static_cast<size_t>(y1) * size + x0];
		const float3 c11 = faceData[static_cast<size_t>(y1) * size + x1];

		const float3 top = lerp3(c00, c10, tx);
		const float3 bottom = lerp3(c01, c11, tx);
		return lerp3(top, bottom, ty);
	}

	// R1^-1 * R2^-1 * R3^-1 composed as explicit rotations (matching
	// CpuPathTracer's glm::rotate() sequence exactly) rather than building a
	// 4x4 matrix - simpler without glm on the device. Rotation about X by
	// angle t: (x, y*cos t - z*sin t, y*sin t + z*cos t). Rotation about Y
	// by angle t: (x*cos t + z*sin t, y, -x*sin t + z*cos t).
	__forceinline__ __device__ float3 undoSkyboxRotation(const float3& direction, bool cameraUpAxisZUp, float skyBoxZRotationDegrees)
	{
		float3 v = direction;
		if (!cameraUpAxisZUp)
		{
			// Rot(+90, X): cos=0, sin=1 -> (x, -z, y)
			v = make_float3(v.x, -v.z, v.y);
		}
		// Rot(-90, X): cos=0, sin=-1 -> (x, z, -y)
		v = make_float3(v.x, v.z, -v.y);
		// Rot(-skyBoxZRotationDegrees, Y)
		const float rad = -skyBoxZRotationDegrees * (kPi / 180.0f);
		const float c = cosf(rad), s = sinf(rad);
		v = make_float3(v.x * c + v.z * s, v.y, -v.x * s + v.z * c);
		return v;
	}

	// ViewportWidget::drawSkyBox() always renders the skybox cube through a
	// PERSPECTIVE projection (RtEnvironment::skyBoxFOV's doc comment), even
	// under an orthographic scene camera - a deliberate cheat so the
	// background still reads as a varied, panoramic backdrop instead of the
	// flat, perfectly uniform color a true orthographic camera's parallel
	// rays would otherwise sample (every such ray shares the exact same
	// direction, differing only in origin). Mirrors that same cheat for the
	// kernel's own directly-visible background sample - only ever called for
	// the primary-ray-miss case (escapeRoughness == -1.0f), matching
	// CpuPathTracer::tracePixel()'s identical backgroundDirection. su/sv are
	// the same fixed pixel-center screen UVs __miss__ms()/traceBouncePath()'s
	// shadow-catcher gate already compute for the fallback-gradient lookup,
	// reused here rather than recomputing an equivalent NDC pair.
	__forceinline__ __device__ float3 computeSkyboxBackgroundDirection(const float3& realDirection, float su, float sv)
	{
		if (!params.camOrthographic)
			return realDirection;
		const float ndcX = 2.0f * su - 1.0f;
		const float ndcY = 2.0f * sv - 1.0f;
		const float tanHalfFovY = tanf(params.environment.skyBoxFOV * (kPi / 180.0f) * 0.5f);
		return normalizeF3(params.camForward
			+ params.camRight * (ndcX * params.camAspectRatio * tanHalfFovY)
			+ params.camUp * (ndcY * tanHalfFovY));
	}

	__forceinline__ __device__ float3 flatGradientMiss(const float3& direction)
	{
		const float t = fminf(fmaxf(direction.y * 0.5f + 0.5f, 0.0f), 1.0f);
		const float3 horizonC = make_float3(0.35f, 0.38f, 0.42f);
		const float3 zenithC = make_float3(0.10f, 0.14f, 0.22f);
		return lerp3(horizonC, zenithC, t);
	}

	__forceinline__ __device__ float3 sampleFallbackBackgroundGradient(const RtOptixEnvironment& env, float su, float sv)
	{
		su = fminf(fmaxf(su, 0.0f), 1.0f);
		sv = fminf(fmaxf(sv, 0.0f), 1.0f);
		float factor = sv;

		if (env.fallbackGradientStyle == 1)
			return lerp3(env.fallbackTopColor, env.fallbackBottomColor, su);
		if (env.fallbackGradientStyle == 2)
			factor = (su + (1.0f - sv)) * 0.5f;
		else if (env.fallbackGradientStyle == 3)
			factor = ((1.0f - su) + (1.0f - sv)) * 0.5f;

		return lerp3(env.fallbackBottomColor, env.fallbackTopColor, factor);
	}

	// Directly-visible background (primary ray miss, depth==0) - matches
	// CpuPathTracer::sampleEnvironmentBackground(): no envMapExposure (that's
	// applied once, uniformly, at the final present/tonemap stage - a later
	// increment once this GPU path has one).
	__forceinline__ __device__ float3 sampleEnvironmentBackground(const RtOptixEnvironment& env, const float3& direction, float su, float sv)
	{
		if (!env.showBackground)
			return sampleFallbackBackgroundGradient(env, su, sv);

		const float3 sampleDir = undoSkyboxRotation(direction, env.cameraUpAxisZUp != 0, env.skyBoxZRotationDegrees);
		// faceSize<=0 means "skybox on but no HDRI actually loaded" - falls
		// back to the user's configured gradient, matching CpuPathTracer::
		// sampleEnvironmentBackground()'s identical fix (see its comment for
		// why flatGradientMiss()'s hardcoded placeholder sky here was
		// invisible under normal alpha blending but became visibly wrong
		// once RtPresenter::draw()'s forceOpaque path exposed it directly).
		return env.faceSize > 0 ? sampleCubemapFaces(env.faces, env.faceSize, sampleDir) : sampleFallbackBackgroundGradient(env, su, sv);
	}

	// Fixed swizzle needed ONLY on top of undoSkyboxRotation() when sampling
	// the PREFILTER MIP CHAIN specifically - ported from CpuPathTracer.cpp's
	// identically-named toPrefilterDirection(). Per that function's own doc
	// comment, this compensates for how the prefilter chain itself was
	// captured/oriented (a quirk of main_scene.frag's specular-IBL sampling
	// code) - NOT a general property of specular/reflection sampling, so it
	// must NOT be applied when sampling the raw map (see sampleEnvironmentRaw()
	// below) - an earlier version of this file applied it to the raw map on
	// the wrong theory that any mirror-direction sample needed it, which
	// produced a visible 90-degree rotation instead of fixing anything.
	__forceinline__ __device__ float3 toPrefilterDirection(const float3& v)
	{
		return make_float3(v.x, -v.z, v.y);
	}

	// Raw (mip-0) reflection/specular sample - matches CpuPathTracer::
	// sampleEnvironmentMiss(): plain undoSkyboxRotation(), no swizzle. Only
	// reached via sampleEnvironmentSpecular()'s own no-prefilter-chain
	// fallback below. envMapExposure DOES apply here (every surface-side
	// IBL sample gets it), unlike the directly-visible background above.
	__forceinline__ __device__ float3 sampleEnvironmentRaw(const RtOptixEnvironment& env, const float3& direction)
	{
		if (env.faceSize <= 0)
			return flatGradientMiss(direction) * env.envMapExposure;

		const float3 sampleDir = undoSkyboxRotation(direction, env.cameraUpAxisZUp != 0, env.skyBoxZRotationDegrees);
		return sampleCubemapFaces(env.faces, env.faceSize, sampleDir) * env.envMapExposure;
	}

	// Diffuse-lobe environment sample - ported from CpuPathTracer::
	// sampleEnvironmentDiffuse(): a real cosine-weighted irradiance
	// convolution (RtOptixEnvironment::irradianceFaces - see its own doc
	// comment), plain undoSkyboxRotation() with NO extra toPrefilterDirection
	// swizzle (that swizzle is specifically a quirk of how the SPECULAR
	// prefilter chain was captured - see toPrefilterDirection()'s own doc
	// comment - not a general property of blurred/convolved IBL sampling,
	// and the irradiance map is captured without it, matching CPU exactly).
	// Falls back to the raw map (sampleEnvironmentRaw(), NOT the specular
	// prefilter chain's roughest mip - that was the old, inexact stand-in
	// this replaces) if no irradiance map was uploaded, matching CPU's own
	// identical sampleEnvironmentDiffuse()->sampleEnvironmentMiss() fallback.
	__forceinline__ __device__ float3 sampleEnvironmentDiffuse(const RtOptixEnvironment& env, const float3& direction)
	{
		if (env.irradianceFaceSize <= 0)
			return sampleEnvironmentRaw(env, direction);

		const float3 sampleDir = undoSkyboxRotation(direction, env.cameraUpAxisZUp != 0, env.skyBoxZRotationDegrees);
		return sampleCubemapFaces(env.irradianceFaces, env.irradianceFaceSize, sampleDir) * env.envMapExposure;
	}

	// Roughness-aware specular/reflection environment sample - ported from
	// CpuPathTracer::sampleEnvironmentSpecular(): selects (and linearly
	// blends between) the two nearest GGX-prefiltered mip levels for the
	// given roughness, giving reflective surfaces a properly blurred
	// (rather than always mirror-sharp) environment reflection - falls back
	// to the raw map (sampleEnvironmentRaw(), no swizzle) if no prefilter
	// chain was uploaded (RtOptixEnvironment::prefilterMipCount == 0),
	// matching CPU's own identical fallback exactly.
	__forceinline__ __device__ float3 sampleEnvironmentSpecular(const RtOptixEnvironment& env, const float3& direction, float roughness)
	{
		if (env.prefilterMipCount <= 0)
			return sampleEnvironmentRaw(env, direction);

		const float3 sampleDir = toPrefilterDirection(
			undoSkyboxRotation(direction, env.cameraUpAxisZUp != 0, env.skyBoxZRotationDegrees));

		const float maxLod = static_cast<float>(env.prefilterMipCount - 1);
		const float lod = fminf(fmaxf(roughness, 0.0f), 1.0f) * maxLod;
		const int mipLow = min(max(static_cast<int>(floorf(lod)), 0), static_cast<int>(maxLod));
		const int mipHigh = min(mipLow + 1, static_cast<int>(maxLod));
		const float frac = lod - static_cast<float>(mipLow);

		const RtOptixPrefilterMip& lowMip = env.prefilterMips[mipLow];
		const float3 colorLow = sampleCubemapFaces(lowMip.faces, lowMip.faceSize, sampleDir);
		if (mipHigh == mipLow)
			return colorLow * env.envMapExposure;

		const RtOptixPrefilterMip& highMip = env.prefilterMips[mipHigh];
		const float3 colorHigh = sampleCubemapFaces(highMip.faces, highMip.faceSize, sampleDir);
		return lerp3(colorLow, colorHigh, frac) * env.envMapExposure;
	}

	__forceinline__ __device__ float3 sampleEnvironmentSheen(const RtOptixEnvironment& env, const float3& direction, float roughness)
	{
		if (env.sheenPrefilterMipCount <= 0)
			return sampleEnvironmentSpecular(env, direction, roughness);

		const float3 sampleDir = toPrefilterDirection(
			undoSkyboxRotation(direction, env.cameraUpAxisZUp != 0, env.skyBoxZRotationDegrees));

		const float maxLod = static_cast<float>(env.sheenPrefilterMipCount - 1);
		const float lod = fminf(fmaxf(roughness, 0.0f), 1.0f) * maxLod;
		const int mipLow = min(max(static_cast<int>(floorf(lod)), 0), static_cast<int>(maxLod));
		const int mipHigh = min(mipLow + 1, static_cast<int>(maxLod));
		const float frac = lod - static_cast<float>(mipLow);

		const RtOptixPrefilterMip& lowMip = env.sheenPrefilterMips[mipLow];
		const float3 colorLow = sampleCubemapFaces(lowMip.faces, lowMip.faceSize, sampleDir);
		if (mipHigh == mipLow)
			return colorLow * env.envMapExposure;

		const RtOptixPrefilterMip& highMip = env.sheenPrefilterMips[mipHigh];
		const float3 colorHigh = sampleCubemapFaces(highMip.faces, highMip.faceSize, sampleDir);
		return lerp3(colorLow, colorHigh, frac) * env.envMapExposure;
	}

	// The one trace wrapper the raygen bounce loop uses for EVERY bounce
	// (including the primary/camera ray) - see this file's top-of-file doc
	// comment and __closesthit__ch()'s own doc comment for the full payload
	// layout. rngSeed is an INPUT the closest-hit program uses to
	// stochastically sample this hit's own continuation direction;
	// escapeRoughness is a SEPARATE input, read by the MISS program only if
	// THIS particular ray escapes to the environment - it's the roughness
	// (or a negative sentinel) of whichever lobe the PREVIOUS
	// bounce's closest-hit sampled to produce this ray's direction (-1.0 for
	// the very first, primary/camera ray, -2.0 for a diffuse-lobe escape,
	// and >=0 for a specular-lobe escape). On a real hit,
	// outWorldNormal/outGuideAlbedo double as this hit's OIDN guide values
	// (raygen only actually uses them at bounce 0) and outEscapeRoughness is
	// the roughness-or-sentinel to feed back in as the NEXT call's
	// escapeRoughness input.
	__forceinline__ __device__ void traceBouncePath(const float3& origin, const float3& direction,
		unsigned int rngSeed, float escapeRoughness, float previousBsdfPdf,
		float3& outRadiance, unsigned int& outHitFlag, float3& outWorldNormal, float& outHitDistance,
		float3& outNextDirection, float3& outThroughputWeight, float3& outGuideAlbedo, float& outEscapeRoughness,
		float& outNextBsdfPdf, float3& outGuideNormal)
	{
		unsigned int p0 = 0u, p1 = 0u, p2 = 0u, p3 = 0u, p4 = 0u, p5 = 0u, p6 = 0u, p7 = 0u, p8 = 0u;
		unsigned int p9 = 0u, p10 = 0u, p11 = 0u, p12 = 0u, p13 = 0u, p14 = 0u, p15 = 0u, p16 = 0u;
		unsigned int p17 = rngSeed;
		unsigned int p18 = __float_as_uint(escapeRoughness);
		unsigned int p19 = __float_as_uint(previousBsdfPdf);
		optixTrace(
			params.handle,
			origin,
			direction,
			selfIntersectionEpsilon(origin), // tmin
			1e16f,  // tmax
			0.0f,   // rayTime
			OptixVisibilityMask(255),
			OPTIX_RAY_FLAG_NONE,
			0, // SBT offset
			1, // SBT stride
			0, // missSBTIndex
			p0, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19);
		outRadiance = make_float3(__uint_as_float(p0), __uint_as_float(p1), __uint_as_float(p2));
		outHitFlag = p3;
		outWorldNormal = make_float3(__uint_as_float(p4), __uint_as_float(p5), __uint_as_float(p6));
		outHitDistance = __uint_as_float(p7);
		outNextDirection = make_float3(__uint_as_float(p8), __uint_as_float(p9), __uint_as_float(p10));
		outThroughputWeight = make_float3(__uint_as_float(p11), __uint_as_float(p12), __uint_as_float(p13));
		outGuideAlbedo = make_float3(__uint_as_float(p14), __uint_as_float(p15), __uint_as_float(p16));
		outEscapeRoughness = __uint_as_float(p17);
		outNextBsdfPdf = __uint_as_float(p19);
		// outWorldNormal (p4-6) is repurposed to carry KHR_materials_
		// volume_scatter's free-flight-walk scatter POSITION when
		// outHitFlag==4 (see __closesthit__ch()'s hasExplicitContinuationOrigin) -
		// no octahedral-normal smuggling is needed for this case (unlike the
		// old BSSRDF redirect this replaces): a scatter vertex has no
		// surface normal at all, and OIDN's guide-normal buffer is only
		// ever captured on the primary hit (bounce==0/transmissionDepth==0
		// in __raygen__rg()), which a scatter continuation can only reach
		// in the degenerate case of the camera starting inside a volume-
		// scatter medium - an accepted v1 gap, matching this feature's plan
		// doc.
		outGuideNormal = outWorldNormal;

		// CPU parity for NVIDIA-style analytic infinite-plane shadow
		// catcher: when enabled, the plane competes with BVH geometry by
		// distance instead of existing as a finite proxy mesh. This is the
		// piece that removes the hard rectangular slab footprint.
		if (params.infinitePlaneEnabled != 0 && params.infinitePlaneIsShadowCatcher != 0)
		{
			const float3 planeNormal = params.infinitePlaneCameraUpAxisZUp != 0
				? make_float3(0.0f, 0.0f, 1.0f)
				: make_float3(0.0f, 1.0f, 0.0f);
			const float originUp = params.infinitePlaneCameraUpAxisZUp != 0 ? origin.z : origin.y;
			const float dirUp = params.infinitePlaneCameraUpAxisZUp != 0 ? direction.z : direction.y;
			const float eps = selfIntersectionEpsilon(origin);
			if (originUp > params.infinitePlaneHeight + eps && fabsf(dirUp) > 1e-6f)
			{
				const float tPlane = (params.infinitePlaneHeight - originUp) / dirUp;
				const bool planeBeatsScene = tPlane > eps && tPlane < 1e16f
					&& (outHitFlag == 0u || tPlane <= outHitDistance);
				if (planeBeatsScene)
				{
					const float3 planePos = origin + direction * tPlane;

					// Faithful port of NVIDIA's getDirectLightingTechniqueProbabilities()/
					// sampleLights() stochastic technique pick (pathtrace_functions.h.
					// slang:357-464) - see CpuPathTracer::tracePixel()'s identical
					// isShadowCatcher gate for the full write-up of why this (not a
					// deterministic sum over every light, this file's earlier
					// approach) is what actually localizes the darkening near the
					// model instead of spreading a hard directional-light shadow
					// across the whole ground: 50% chance a uniformly-random
					// punctual light, 50% chance an environment-importance-sampled
					// direction (proportional weights if only one technique is
					// available), then ONE shadow ray against whichever direction
					// was picked.
					unsigned int catcherRng = rngSeed;
					const bool haveLights = params.lightCount > 0;
					const bool haveEnv = params.environment.envFlatCdf != nullptr && params.environment.envTotalWeight > 0.0f;
					float lightTechWeight = haveLights ? 0.5f : 0.0f;
					float envTechWeight = haveEnv ? 0.5f : 0.0f;
					const float totalTechWeight = lightTechWeight + envTechWeight;

					float3 shadowFactor = make_float3(1.0f, 1.0f, 1.0f);
					if (totalTechWeight > 0.0f)
					{
						lightTechWeight /= totalTechWeight;
						envTechWeight /= totalTechWeight;

						catcherRng = pcgHash(catcherRng ^ 0x2545F491u);
						const bool sampleLightTech = hashToUnitFloat(catcherRng) < lightTechWeight;

						float3 sampleDir = make_float3(0.0f, 0.0f, 0.0f);
						float sampleDistance = 1e16f; // environment/unbounded default
						bool haveSampleDir = false;

						if (sampleLightTech)
						{
							catcherRng = pcgHash(catcherRng);
							const unsigned int lightIndex = min(
								static_cast<unsigned int>(hashToUnitFloat(catcherRng) * static_cast<float>(params.lightCount)),
								params.lightCount - 1);
							float3 lightDir, lightIntensity;
							float lightDistance;
							evaluatePunctualLight(params.lights[lightIndex], planePos, lightDir, lightIntensity, lightDistance);
							if (lightIntensity.x > 0.0f || lightIntensity.y > 0.0f || lightIntensity.z > 0.0f)
							{
								sampleDir = lightDir;
								sampleDistance = lightDistance;
								haveSampleDir = true;
							}
						}
						else
						{
							catcherRng = pcgHash(catcherRng);
							const float eu0 = hashToUnitFloat(catcherRng);
							catcherRng = pcgHash(catcherRng);
							const float eu1 = hashToUnitFloat(catcherRng);
							catcherRng = pcgHash(catcherRng);
							const float eu2 = hashToUnitFloat(catcherRng);
							float3 envDir;
							float envPdf;
							envSamplerSample(params.environment, eu0, eu1, eu2, envDir, envPdf);
							if (envPdf > 0.0f)
							{
								sampleDir = envDir;
								haveSampleDir = true;
							}
						}

						if (haveSampleDir && dot3(sampleDir, planeNormal) > 0.0f)
						{
							catcherRng = pcgHash(catcherRng);
							const float3 shadowOrigin = planePos + planeNormal * eps;
							const float shadowMaxDistance = fminf(sampleDistance, 1e16f);
							shadowFactor = params.shadowsEnabled != 0
								? traceShadowRay(shadowOrigin, sampleDir, shadowMaxDistance, 0xFFFFFFFFu, catcherRng, true)
								: make_float3(1.0f, 1.0f, 1.0f);
						}
					}

					float3 envColor;
					if (escapeRoughness == -1.0f)
					{
						const uint3 idx = optixGetLaunchIndex();
						const uint3 dimLaunch = optixGetLaunchDimensions();
						const float su = (static_cast<float>(idx.x) + 0.5f) / static_cast<float>(dimLaunch.x);
						const float sv = 1.0f - (static_cast<float>(idx.y) + 0.5f) / static_cast<float>(dimLaunch.y);
						envColor = sampleEnvironmentBackground(params.environment, computeSkyboxBackgroundDirection(direction, su, sv), su, sv);
					}
					else
					{
						const float envPdfAtRay = (previousBsdfPdf > 0.0f && params.enableEnvironmentImportanceSampling != 0)
							? envSamplerPdf(params.environment, direction) : 0.0f;
						const float misWeight = (previousBsdfPdf > 0.0f && envPdfAtRay > 0.0f)
							? (previousBsdfPdf / (previousBsdfPdf + envPdfAtRay)) : 1.0f;
						envColor = sampleEnvironmentRaw(params.environment, direction) * misWeight;
					}

					// Literal port of handleShadowCatcher()'s own branch split
					// (pathtrace_functions.h.slang:527-535) - see CPU's identical
					// isShadowCatcher gate for the full doc comment.
					const bool fullyLit = shadowFactor.x >= 1.0f && shadowFactor.y >= 1.0f && shadowFactor.z >= 1.0f;
					const float shadowStrength = fminf(fmaxf(params.infinitePlaneShadowCatcherDarkness, 0.0f), 1.0f);
					const float3 resultColor = fullyLit
						? envColor
						: envColor * shadowFactor - envColor * (make_float3(1.0f, 1.0f, 1.0f) - shadowFactor) * shadowStrength;

					outRadiance = resultColor;
					outWorldNormal = planeNormal;
					outHitDistance = tPlane;
					outGuideAlbedo = params.infinitePlaneBaseColor;
					outGuideNormal = planeNormal;

					if (fullyLit)
					{
						outHitFlag = 0u;
						outNextDirection = make_float3(0.0f, 0.0f, 0.0f);
						outThroughputWeight = make_float3(0.0f, 0.0f, 0.0f);
						outEscapeRoughness = 0.0f;
						outNextBsdfPdf = 0.0f;
					}
					else
					{
						// Continue the path as an ordinary BSDF bounce off the
						// shadow-catcher's own flat material (infinitePlaneBaseColor/
						// Metalness/Roughness) - mirrors NVIDIA's
						// `bsdfSampleSimple(sampleData, pbrMat); ... return true`
						// continuation exactly (pathtrace_functions.h.slang:537-553).
						// A simplified diffuse/GGX-specular mixture (this flat
						// material has no clearcoat/sheen/anisotropy to mix in),
						// since traceBouncePath() runs outside __closesthit__ch()
						// and can't reuse that shader's own inline BSDF-sampling
						// code directly.
						catcherRng = pcgHash(catcherRng ^ 0x5BD1E995u);
						const float u1 = hashToUnitFloat(catcherRng);
						catcherRng = pcgHash(catcherRng);
						const float u2 = hashToUnitFloat(catcherRng);
						catcherRng = pcgHash(catcherRng);
						const float lobeXi = hashToUnitFloat(catcherRng);

						const float metalness = fminf(fmaxf(params.infinitePlaneMetalness, 0.0f), 1.0f);
						const float roughness = fminf(fmaxf(params.infinitePlaneRoughness, 0.0001f), 1.0f);
						const float3 dielectricF0 = make_float3(0.04f, 0.04f, 0.04f);
						const float3 F0 = lerp3(dielectricF0, params.infinitePlaneBaseColor, metalness);
						const float specProb = fminf(fmaxf(fmaxf(F0.x, fmaxf(F0.y, F0.z)), 0.04f), 0.96f);

						const float3 V = direction * -1.0f;
						float3 T, B;
						buildOrthonormalBasis(planeNormal, T, B);

						float3 nextDir = make_float3(0.0f, 0.0f, 0.0f);
						float3 bounceThroughput = make_float3(0.0f, 0.0f, 0.0f);
						float bounceEscapeRoughness = -2.0f; // diffuse-lobe sentinel, matches __closesthit__ch()'s convention
						bool haveBounce = false;

						if (lobeXi < specProb)
						{
							const float alpha = roughness * roughness;
							if (roughness <= 0.01f)
							{
								const float3 L = reflectF3(V * -1.0f, planeNormal);
								if (dot3(planeNormal, L) > 0.0f)
								{
									const float NdotV = fmaxf(dot3(planeNormal, V), 1e-4f);
									const float3 F = fresnelSchlick(NdotV, F0, make_float3(1.0f, 1.0f, 1.0f));
									nextDir = L;
									bounceThroughput = F * (1.0f / specProb);
									bounceEscapeRoughness = 0.0f;
									haveBounce = true;
								}
							}
							else
							{
								const float NdotV0 = fmaxf(dot3(planeNormal, V), 1e-4f);
								const float3 Ve = make_float3(dot3(V, T), dot3(V, B), NdotV0);
								const float3 Hlocal = sampleGGXVNDF(Ve, alpha, alpha, u1, u2);
								const float3 Hworld = normalizeF3(T * Hlocal.x + B * Hlocal.y + planeNormal * Hlocal.z);
								const float3 L = reflectF3(V * -1.0f, Hworld);
								const float NdotL = dot3(planeNormal, L);
								if (NdotL > 0.0f)
								{
									const float NdotV = fmaxf(dot3(planeNormal, V), 1e-4f);
									const float VdotH = fmaxf(dot3(V, Hworld), 0.0f);
									const float G1v = smithG1GGX(NdotV, alpha);
									const float G2 = smithG2HeightCorrelatedGGX(NdotV, NdotL, alpha);
									const float3 F = fresnelSchlick(VdotH, F0, make_float3(1.0f, 1.0f, 1.0f));
									nextDir = L;
									bounceThroughput = F * (G2 / fmaxf(G1v, 1e-6f)) * (1.0f / specProb);
									bounceEscapeRoughness = roughness;
									haveBounce = true;
								}
							}
						}
						else
						{
							const float diffuseProb = fmaxf(1.0f - specProb, 1e-4f);
							const float3 localDir = cosineSampleHemisphere(u1, u2);
							nextDir = normalizeF3(T * localDir.x + B * localDir.y + planeNormal * localDir.z);
							const float NdotV = fmaxf(dot3(planeNormal, V), 1e-4f);
							const float3 Fview = fresnelSchlick(NdotV, F0, make_float3(1.0f, 1.0f, 1.0f));
							const float3 kD = (make_float3(1.0f, 1.0f, 1.0f) - Fview) * (1.0f - metalness);
							bounceThroughput = kD * params.infinitePlaneBaseColor * (1.0f / diffuseProb);
							bounceEscapeRoughness = -2.0f;
							haveBounce = true;
						}

						if (haveBounce)
						{
							outHitFlag = 1u;
							outNextDirection = nextDir;
							outThroughputWeight = bounceThroughput;
							outEscapeRoughness = bounceEscapeRoughness;
							outNextBsdfPdf = 0.0f; // no MIS - matches the alpha-pass-through/transmission convention
						}
						else
						{
							outHitFlag = 2u;
							outNextDirection = make_float3(0.0f, 0.0f, 0.0f);
							outThroughputWeight = make_float3(0.0f, 0.0f, 0.0f);
							outEscapeRoughness = 1.0f; // fully opaque alpha - a real (if bounce-less) shadowed hit
							outNextBsdfPdf = 0.0f;
						}
					}
				}
			}
		}
	}

	// RGB transmittance shadow query - reuses the same pipeline/SBT as the
	// other trace wrappers above (no dedicated occlusion program group/hit
	// records) by setting OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT and having
	// __closesthit__ch()/__miss__ms() check optixGetRayFlags() to take a
	// one-line early-out instead of running the full shading path. Payload
	// 0 is the occluded bit; payloads 1/2 let __anyhit__ah() ignore hits
	// against the source instance when self-shadows are disabled; payload 3
	// is an RNG seed __anyhit__ah() reads/advances; payloads 4-6 carry a
	// running RGB transmittance through transmissive any-hit pass-throughs.
	// forceSelfExclude (default false, matching every existing call site's
	// prior behavior exactly): when true, ALWAYS excludes sourceInstanceId
	// from this ray's occlusion test regardless of params.selfShadowsEnabled -
	// used by the shadow-catcher floor's own shadow-ray test, where the
	// source is always a single flat quad that can never legitimately
	// self-shadow (see __closesthit__ch()'s isShadowCatcher gate).
	__forceinline__ __device__ float3 traceShadowRay(const float3& origin, const float3& direction, float maxDistance,
		unsigned int sourceInstanceId, unsigned int rngSeed, bool forceSelfExclude = false)
	{
		const float eps = selfIntersectionEpsilon(origin);
		unsigned int occluded = 0u;
		unsigned int selfInstanceId = sourceInstanceId;
		unsigned int selfShadowsEnabled = (!forceSelfExclude && params.selfShadowsEnabled != 0) ? 1u : 0u;
		unsigned int alphaRngSeed = rngSeed;
		unsigned int trX = __float_as_uint(1.0f);
		unsigned int trY = __float_as_uint(1.0f);
		unsigned int trZ = __float_as_uint(1.0f);
		unsigned int p7 = 0u, p8 = 0u, p9 = 0u, p10 = 0u, p11 = 0u, p12 = 0u, p13 = 0u;
		unsigned int p14 = 0u, p15 = 0u, p16 = 0u, p17 = 0u, p18 = 0u, p19 = 0u;
		optixTrace(
			params.handle,
			origin,
			direction,
			eps,                                // tmin
			fmaxf(maxDistance - 2.0f * eps, eps), // tmax
			0.0f,                                // rayTime
			OptixVisibilityMask(255),
			OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT,
			0, // SBT offset
			1, // SBT stride
			0, // missSBTIndex
			occluded, selfInstanceId, selfShadowsEnabled, alphaRngSeed, trX, trY, trZ,
			p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19);
		if (occluded != 0u)
			return make_float3(0.0f, 0.0f, 0.0f);
		return make_float3(__uint_as_float(trX), __uint_as_float(trY), __uint_as_float(trZ));
	}

	// KHR_materials_volume_scatter's free-flight random walk NEE (see
	// __closesthit__ch()'s hitBackface/hasVolumeScattering gate) - next-
	// event estimation from a volume-INTERIOR scatter vertex, mirroring
	// CpuPathTracer.cpp's sampleVolumeScatterNEE() exactly: the Henyey-
	// Greenstein phase function replaces the BRDF and there is no NdotL/
	// surface-cosine term (no surface normal exists at a scatter point -
	// the phase function's own normalization already accounts for the full
	// sphere integral). wi is the ray's incoming travel direction at the
	// scatter point. Punctual lights use the same "delta-direction, no
	// extra pdf division" convention __closesthit__ch()'s ordinary surface
	// NEE loop already uses; the environment term MIS-combines the env-
	// sampling pdf against the phase pdf, mirroring that surface NEE
	// block's balance-heuristic weighting exactly. No self-shadow-instance
	// mask exclusion is needed (that toggle exists so a surface doesn't
	// shadow itself - a scatter point in open space isn't on any
	// instance's own geometry), so sourceInstanceId is passed through
	// mainly so traceShadowRay()'s signature is satisfied uniformly - it
	// only matters when params.selfShadowsEnabled==0, which can never
	// self-exclude a non-existent "own instance" for a scatter vertex
	// anyway.
	__forceinline__ __device__ float3 sampleVolumeScatterNEE(const float3& scatterPos, const float3& wi,
		const float3& throughput, unsigned int sourceInstanceId, unsigned int rngState)
	{
		float3 result = make_float3(0.0f, 0.0f, 0.0f);

		for (unsigned int i = 0; i < params.lightCount; ++i)
		{
			float3 lightDir, lightIntensity;
			float lightDistance;
			evaluatePunctualLight(params.lights[i], scatterPos, lightDir, lightIntensity, lightDistance);
			if (lightIntensity.x <= 0.0f && lightIntensity.y <= 0.0f && lightIntensity.z <= 0.0f)
				continue;

			rngState = pcgHash(rngState ^ (i * 0x9E3779B9u));
			const float shadowMaxDistance = fminf(lightDistance, 1e16f);
			float3 shadowTransmittance = params.shadowsEnabled != 0
				? traceShadowRay(scatterPos, lightDir, shadowMaxDistance, sourceInstanceId, rngState)
				: make_float3(1.0f, 1.0f, 1.0f);
			if (params.lights[i].range < 0.0f)
				shadowTransmittance = make_float3(fmaxf(shadowTransmittance.x, 0.15f), fmaxf(shadowTransmittance.y, 0.15f), fmaxf(shadowTransmittance.z, 0.15f));
			if (shadowTransmittance.x <= 0.0f && shadowTransmittance.y <= 0.0f && shadowTransmittance.z <= 0.0f)
				continue;

			const float phasePdf = henyeyGreensteinPdf(dot3(wi, lightDir), kVolumeScatterAnisotropy);
			result = result + throughput * phasePdf * lightIntensity * shadowTransmittance;
		}

		if (params.enableEnvironmentImportanceSampling != 0
			&& params.environment.envTotalWeight > 0.0f
			&& params.environment.envFlatCdf != nullptr
			&& params.environment.envTexelPdf != nullptr)
		{
			rngState = pcgHash(rngState ^ 0x6D2B79F5u);
			const float eu0 = hashToUnitFloat(rngState);
			rngState = pcgHash(rngState);
			const float eu1 = hashToUnitFloat(rngState);
			rngState = pcgHash(rngState);
			const float eu2 = hashToUnitFloat(rngState);

			float3 envDir;
			float envPdf;
			envSamplerSample(params.environment, eu0, eu1, eu2, envDir, envPdf);
			if (envPdf > 0.0f)
			{
				rngState = pcgHash(rngState);
				const float3 envTransmittance = params.shadowsEnabled != 0
					? traceShadowRay(scatterPos, envDir, 1e16f, sourceInstanceId, rngState)
					: make_float3(1.0f, 1.0f, 1.0f);
				if (envTransmittance.x > 0.0f || envTransmittance.y > 0.0f || envTransmittance.z > 0.0f)
				{
					const float phasePdf = henyeyGreensteinPdf(dot3(wi, envDir), kVolumeScatterAnisotropy);
					const float misWeight = phasePdf / (phasePdf + envPdf); // balance heuristic vs. the phase-sampled bounce's own MIS half - see __miss__ms()'s previousBsdfPdf weighting
					const float3 envRadiance = sampleEnvironmentRaw(params.environment, envDir) * envTransmittance;
					result = result + throughput * (misWeight / envPdf) * phasePdf * envRadiance;
				}
			}
		}

		return result;
	}
}

extern "C" __global__ void __raygen__rg()
{
	const uint3 idx = optixGetLaunchIndex();
	const uint3 dim = optixGetLaunchDimensions();
	const unsigned int pixelIndex = idx.y * dim.x + idx.x;
	const unsigned int spp = max(params.samplesPerPixel, 1u);

	// Averages spp jittered primary rays per pixel within this single
	// launch (box-filter AA - each sample's sub-pixel offset comes from a
	// hash of (pixelIndex, sampleIndex), so samples are decorrelated without
	// any persistent per-launch RNG state). This is the same "more samples
	// = less noise, no free lunch" tradeoff CpuPathTracer's own multi-sample
	// accumulation makes, just gathered in one launch rather than one
	// progressive frame per sample.
	float3 accumulated = make_float3(0.0f, 0.0f, 0.0f);
	float3 accumulatedAlbedo = make_float3(0.0f, 0.0f, 0.0f);
	float3 accumulatedNormal = make_float3(0.0f, 0.0f, 0.0f);
	float accumulatedHits = 0.0f; // primary-hit count -> alpha/hit-fraction, see params.image's doc comment (.w component)
	for (unsigned int s = 0; s < spp; ++s)
	{
		const unsigned int globalSampleIndex = params.sampleOffset + s;
		const unsigned int seed = pcgHash(pixelIndex * 9781u + globalSampleIndex * 6271u + 1u);
		const float jitterX = hashToUnitFloat(pcgHash(seed));
		const float jitterY = hashToUnitFloat(pcgHash(seed ^ 0x9e3779b9u));

		// Matches CpuPathTracer::tracePixel()'s primary-ray formula, with the
		// pixel-center 0.5 offset replaced by a jittered offset in [0, 1).
		const float ndcX = 2.0f * (static_cast<float>(idx.x) + jitterX) / static_cast<float>(dim.x) - 1.0f;
		const float ndcY = 1.0f - 2.0f * (static_cast<float>(idx.y) + jitterY) / static_cast<float>(dim.y);

		float3 rayOrigin, rayDirection;
		if (params.camOrthographic)
		{
			rayOrigin = params.camPosition
				+ params.camRight * (ndcX * params.camAspectRatio * params.camOrthoHalfHeight)
				+ params.camUp * (ndcY * params.camOrthoHalfHeight);
			rayDirection = params.camForward;
		}
		else
		{
			rayOrigin = params.camPosition;
			rayDirection = normalizeF3(params.camForward
				+ params.camRight * (ndcX * params.camAspectRatio * params.camTanHalfFovY)
				+ params.camUp * (ndcY * params.camTanHalfFovY));
		}

		// Iterative bounce loop for this one AA sample - see this file's own
		// top-of-file doc comment and traceBouncePath()'s for the full
		// design. escapeRoughness starts at the "primary/camera ray" sentinel
		// (-1) so a miss on the very first bounce shows the plain background
		// rather than an environment-lighting contribution.
		const unsigned int maxBounces = max(params.maxBounces, 1u);
		float3 throughput = make_float3(1.0f, 1.0f, 1.0f);
		float3 sampleRadiance = make_float3(0.0f, 0.0f, 0.0f);
		float3 sampleAlbedo = make_float3(0.0f, 0.0f, 0.0f);
		float3 sampleNormal = make_float3(0.0f, 0.0f, 0.0f);
		unsigned int rngState = pcgHash(seed ^ 0x68bc21ebu); // decorrelated from the AA-jitter stream above
		float escapeRoughness = -1.0f;
		float previousBsdfPdf = 0.0f;
		float3 curOrigin = rayOrigin;
		float3 curDirection = rayDirection;

		// KHR_materials_transmission tracks its own, separate bounce-depth
		// budget (transmissionDepth), just like CpuPathTracer::tracePixel()'s
		// identical bounce/transmissionDepth split - a transmission
		// continuation (hitFlag==3, see __closesthit__ch()'s transmission
		// branch) increments transmissionDepth instead of bounce, so a long
		// TIR chain inside a dielectric doesn't eat into the ordinary
		// maxBounces budget at all. params.maxTransmissionBounces mirrors
		// CpuPathTracer::Settings::maxTransmissionBounces - actually read
		// from the dialog now, not a hardcoded constant (see
		// RtOptixSceneParams.h's doc comment on that field).
		unsigned int bounce = 0;
		unsigned int transmissionDepth = 0;
		// KHR_materials_volume_scatter's free-flight random walk counts its
		// scatter events separately from both bounce and transmissionDepth -
		// mirrors CpuPathTracer::tracePixel()'s identical scatterBounces
		// counter exactly (see this feature's plan doc). Free (no Russian
		// roulette) until params.maxVolumeScatterBounces, then RR - see this
		// loop's hitFlag==4 branch below.
		unsigned int scatterBounces = 0;
		// Inclusive (<=), matching CpuPathTracer::tracePixel()'s documented
		// "for (bounce=0; bounce<=maxBounces;...)" termination point exactly -
		// this loop previously used < maxBounces, running one fewer real
		// bounce than CPU for the identical maxBounces setting (e.g. 6 vs 7
		// total iterations at the default), losing longer light paths a
		// highly reflective clearcoat/mirror surface depends on.
		while (bounce <= maxBounces)
		{
			rngState = pcgHash(rngState + (bounce + transmissionDepth) * 0x9e3779b9u);

			float3 hitRadiance, worldNormal, nextDirection, throughputWeight, guideAlbedo, guideNormal;
			float hitDistance, nextEscapeRoughness, nextBsdfPdf;
			unsigned int hitFlag;
			traceBouncePath(curOrigin, curDirection, rngState, escapeRoughness, previousBsdfPdf,
				hitRadiance, hitFlag, worldNormal, hitDistance, nextDirection, throughputWeight, guideAlbedo, nextEscapeRoughness, nextBsdfPdf, guideNormal);

			sampleRadiance = sampleRadiance + throughput * hitRadiance;
			if (bounce == 0 && transmissionDepth == 0)
			{
				sampleAlbedo = guideAlbedo;
				// guideNormal, NOT worldNormal directly - see
				// traceBouncePath()'s doc comment: worldNormal is repurposed
				// to carry the volume-scatter BSSRDF-sampled entry POSITION when
				// hitFlag==4, which would otherwise corrupt this guide-
				// normal buffer for every pixel where a KHR_materials_
				// volume_scatter material (e.g. ScatteringSkull.gltf) is
				// visible directly on the primary ray - exactly the common
				// case for that asset.
				sampleNormal = guideNormal;
				if (hitFlag == 2u)
					accumulatedHits += fminf(fmaxf(nextEscapeRoughness, 0.0f), 1.0f);
				else if (hitFlag != 0u) // 1/3/4 (hit, any kind) - the primary ray hit geometry opaquely
					accumulatedHits += 1.0f;
			}

			if (hitFlag == 0u || hitFlag == 2u)
				break; // 0: escaped to the environment; 2: hit but dead-end sample with no valid continuation - either way, fully accounted for above

			throughput = throughput * throughputWeight;

			// Russian roulette after a configurable number of guaranteed
			// bounces, bounding cost/variance on long paths - mirrors
			// CpuPathTracer::Settings::russianRouletteStartDepth exactly
			// (params.russianRouletteStartDepth, actually read from the
			// dialog now instead of a hardcoded literal). Combined bounce+
			// transmissionDepth, matching CPU's identical combined gate - a
			// long TIR chain with genuinely low throughput (e.g. from
			// attenuationColor absorption) still gets a chance to terminate
			// early rather than only ever stopping via maxTransmissionBounces.
			// Skipped for a volume-scatter continuation (hitFlag==4) - that
			// case has its own, SEPARATE free-budget/RR policy below
			// (params.maxVolumeScatterBounces scatter events run free before RR
			// starts), matching CpuPathTracer::tracePixel()'s identical
			// scatter-continue-bypasses-the-ordinary-RR-check structure
			// exactly. Applying the ordinary combined bounce+transmissionDepth
			// RR check here too would re-introduce this feature's own
			// previously-fixed bug: bounce/transmissionDepth stay flat during
			// a scatter streak, so once they cross russianRouletteStartDepth
			// (typically already true by the time a ray has bounced its way
			// into the medium), EVERY subsequent scatter event would also be
			// probabilistically killed here - exactly the "RR every step"
			// failure mode the free-flight walk was redesigned to avoid.
			if (hitFlag != 4u && bounce + transmissionDepth >= params.russianRouletteStartDepth)
			{
				const float continueProb = fminf(fmaxf(fmaxf(throughput.x, fmaxf(throughput.y, throughput.z)), 0.05f), 1.0f);
				rngState = pcgHash(rngState ^ 0xA5A5A5A5u);
				if (hashToUnitFloat(rngState) > continueProb)
					break;
				throughput = throughput * (1.0f / continueProb);
			}

			const float3 hitPos = curOrigin + curDirection * hitDistance;
			// Offset toward whichever side nextDirection actually continues
			// on - ordinarily always the front (+worldNormal), but
			// KHR_materials_diffuse_transmission's back-hemisphere lobe (see
			// __closesthit__ch()'s diffuse-lobe branch) and KHR_materials_
			// transmission's refracted/thin-walled-through directions (see
			// its transmission branch) can return a direction on the far
			// side of the surface, which needs the opposite offset to avoid
			// immediately self-intersecting the same triangle - matches
			// CpuPathTracer::tracePixel()'s identical direction-aware offset
			// exactly.
			if (hitFlag == 4u)
			{
				curOrigin = worldNormal;
			}
			else
			{
				const float offsetSign = dot3(nextDirection, worldNormal) >= 0.0f ? 1.0f : -1.0f;
				curOrigin = hitPos + worldNormal * (selfIntersectionEpsilon(hitPos) * offsetSign);
			}
			curDirection = nextDirection;
			escapeRoughness = nextEscapeRoughness;
			previousBsdfPdf = nextBsdfPdf;

			if (hitFlag == 3u)
			{
				++transmissionDepth;
				if (transmissionDepth >= params.maxTransmissionBounces)
					break; // exhausted the transmission budget - a rare edge case (an extremely narrow TIR escape cone), matching CPU's identical exhaustion handling
			}
			else if (hitFlag == 4u)
			{
				// KHR_materials_volume_scatter's free-flight walk - see this
				// counter's declaration above and CpuPathTracer::tracePixel()'s
				// identical policy: free (no RR at all) for the first
				// params.maxVolumeScatterBounces scatter events, then RR with
				// the same floor/cap NVIDIA's vk_gltf_renderer reference uses.
				++scatterBounces;
				if (scatterBounces >= max(params.maxVolumeScatterBounces, 1u))
				{
					const float rrPcont = fminf(fmaxf(fmaxf(throughput.x, fmaxf(throughput.y, throughput.z)) + kVolumeRrFloor, 0.0f), kVolumeRrCap);
					rngState = pcgHash(rngState ^ 0xC2B2AE3Du);
					if (hashToUnitFloat(rngState) >= rrPcont)
						break;
					throughput = throughput * (1.0f / rrPcont);
				}
			}
			else
			{
				++bounce;
			}
		}

		// kDebugVisualizeVolumeScatterBounces - same heat-ramp pattern as
		// CpuPathTracer.cpp's identical debug view, over scatterBounces'
		// final value for this sample, normalized against
		// kVolumeScatterDebugRampCap (a separate, smaller cap than
		// params.maxVolumeScatterBounces - chosen purely for a visually useful
		// range).
		if (kDebugVisualizeVolumeScatterBounces && scatterBounces > 0)
		{
			const float t = fminf(fmaxf(static_cast<float>(scatterBounces) / kVolumeScatterDebugRampCap, 0.0f), 1.0f);
			if (t < 0.25f)
				sampleRadiance = lerp3(make_float3(0.0f, 0.0f, 0.0f), make_float3(0.0f, 0.0f, 1.0f), t / 0.25f);
			else if (t < 0.5f)
				sampleRadiance = lerp3(make_float3(0.0f, 0.0f, 1.0f), make_float3(0.0f, 1.0f, 0.0f), (t - 0.25f) / 0.25f);
			else if (t < 0.75f)
				sampleRadiance = lerp3(make_float3(0.0f, 1.0f, 0.0f), make_float3(1.0f, 1.0f, 0.0f), (t - 0.5f) / 0.25f);
			else
				sampleRadiance = lerp3(make_float3(1.0f, 1.0f, 0.0f), make_float3(1.0f, 1.0f, 1.0f), (t - 0.75f) / 0.25f);
		}

		// A NaN/Inf channel must be caught BEFORE the firefly clamp below,
		// not by it - see CpuPathTracer.cpp's identical guard (the two must
		// stay in exact parity) for the full reasoning: any comparison
		// against NaN is false, so "sampleMaxChannel > threshold" silently
		// lets a NaN sample straight through unclamped, and dividing by it
		// would still yield NaN regardless. That NaN then persists forever
		// in the running-mean accumulation buffer and corrupts the
		// denoiser's guide/color input - almost certainly the cause of the
		// reported "denoiser sometimes hangs" symptom, since neither OIDN
		// nor the native OptiX denoiser were designed to receive non-finite
		// pixels. Treat a non-finite sample as contributing nothing.
		if (!isfinite(sampleRadiance.x) || !isfinite(sampleRadiance.y) || !isfinite(sampleRadiance.z))
		{
			sampleRadiance = make_float3(0.0f, 0.0f, 0.0f);
		}
		else
		{
			// Firefly/outlier suppression - params.fireflyClampThreshold mirrors
			// CpuPathTracer::Settings::fireflyClampThreshold exactly, actually
			// read from the dialog now instead of a hardcoded constant (used to
			// silently ignore the user's real setting, which went unnoticed
			// until KHR_materials_volume_scatter's random walk - a per-step
			// MULTIPLICATIVE MIS weight compounding over up to kMaxVolume
			// ScatterSteps iterations - made a genuinely heavy-tailed estimator,
			// and a handful of extreme per-sample outliers were enough to wash
			// out the whole accumulated image). Scales the whole sample down
			// proportionally (not per-channel) so an extreme-value sample is
			// dimmed without shifting its hue - matches CpuPathTracer.cpp's
			// identical formula exactly.
			const float sampleMaxChannel = fmaxf(sampleRadiance.x, fmaxf(sampleRadiance.y, sampleRadiance.z));
			if (sampleMaxChannel > params.fireflyClampThreshold && sampleMaxChannel > 0.0f)
				sampleRadiance = sampleRadiance * (params.fireflyClampThreshold / sampleMaxChannel);
		}

		accumulated = accumulated + sampleRadiance;
		accumulatedAlbedo = accumulatedAlbedo + sampleAlbedo;
		accumulatedNormal = accumulatedNormal + sampleNormal;
	}

	const float invSpp = 1.0f / static_cast<float>(spp);
	const float3 thisLaunchColor  = accumulated * invSpp;
	const float  thisLaunchHits   = accumulatedHits * invSpp;
	const float3 thisLaunchAlbedo = accumulatedAlbedo * invSpp;
	const float3 thisLaunchNormal = accumulatedNormal * invSpp;

	if (params.previousSampleCount > 0)
	{
		// Blend this launch's own average into whatever's ALREADY in the
		// output buffers via an incremental running mean, instead of
		// overwriting - see RtOptixSceneParams::previousSampleCount's doc
		// comment for why this lives here rather than being combined on the
		// host after a readback.
		const float4 prevImage  = params.image[pixelIndex];
		const float3 prevColor  = make_float3(prevImage.x, prevImage.y, prevImage.z);
		const float  prevHits   = prevImage.w;
		const float3 prevAlbedo = params.albedoImage[pixelIndex];
		const float3 prevNormal = params.normalImage[pixelIndex];

		const float newCount    = static_cast<float>(params.previousSampleCount) + static_cast<float>(spp);
		const float chunkWeight = static_cast<float>(spp) / newCount;

		const float3 blendedColor  = prevColor  + (thisLaunchColor  - prevColor)  * chunkWeight;
		const float  blendedHits   = prevHits   + (thisLaunchHits   - prevHits)   * chunkWeight;
		const float3 blendedAlbedo = prevAlbedo + (thisLaunchAlbedo - prevAlbedo) * chunkWeight;
		const float3 blendedNormal = prevNormal + (thisLaunchNormal - prevNormal) * chunkWeight;

		params.image[pixelIndex]       = make_float4(blendedColor.x, blendedColor.y, blendedColor.z, blendedHits);
		params.albedoImage[pixelIndex] = blendedAlbedo;
		params.normalImage[pixelIndex] = blendedNormal;
	}
	else
	{
		params.image[pixelIndex]       = make_float4(thisLaunchColor.x, thisLaunchColor.y, thisLaunchColor.z, thisLaunchHits);
		params.albedoImage[pixelIndex] = thisLaunchAlbedo;
		params.normalImage[pixelIndex] = thisLaunchNormal;
	}
}

extern "C" __global__ void __miss__ms()
{
	if (optixGetRayFlags() & OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT)
	{
		// Shadow ray reached the light with nothing in the way - unoccluded.
		optixSetPayload_0(0u);
		return;
	}

	// p18 carries the escape mode for THIS ray (see traceBouncePath()'s doc
	// comment): -1 means primary/camera ray, -2 means a diffuse-lobe escape,
	// -3 means a KHR_materials_transmission-branch escape (see
	// __closesthit__ch()'s transmission branch), kVolumeScatterEscapeSentinel
	// (-7) marks a KHR_materials_volume_scatter free-flight-walk redirect
	// that escaped straight to the environment without hitting real
	// geometry first (see that sentinel's own doc comment), and >=0 means a
	// GGX specular-lobe escape with that material roughness.
	const float escapeRoughness = __uint_as_float(optixGetPayload_18());
	const float previousBsdfPdf = __uint_as_float(optixGetPayload_19());
	const float3 dir = optixGetWorldRayDirection();

	float3 result;
	if (escapeRoughness == -1.0f)
	{
		// Matches CpuPathTracer::tracePixel()'s screenUv convention exactly
		// (top of image = 1, bottom = 0) - only used by the fallback
		// gradient when no skybox is shown.
		const uint3 idx = optixGetLaunchIndex();
		const uint3 dimLaunch = optixGetLaunchDimensions();
		const float su = (static_cast<float>(idx.x) + 0.5f) / static_cast<float>(dimLaunch.x);
		const float sv = 1.0f - (static_cast<float>(idx.y) + 0.5f) / static_cast<float>(dimLaunch.y);
		result = sampleEnvironmentBackground(params.environment, computeSkyboxBackgroundDirection(dir, su, sv), su, sv);
	}
	else if (escapeRoughness == -3.0f || escapeRoughness == kVolumeScatterEscapeSentinel)
	{
		// KHR_materials_transmission-branch escape (reflect, thin-walled
		// pass-through, or refract/TIR) - a deterministic Fresnel pick, not a
		// diffuse/specular-lobe MIXTURE sample, so it gets the same sharp/
		// unblurred map the specular-lobe escape below uses, matching
		// CpuPathTracer::tracePixel()'s identical lastBsdfSamplePdf<=0 ->
		// sampleEnvironmentMiss() (this backend's sampleEnvironmentRaw())
		// treatment for this same case exactly. A volume-scatter free-
		// flight redirect's escape direction is HG-phase-sampled (roughly
		// uniform over the sphere for the isotropic v1 default), not a
		// cosine-weighted lobe around a surface normal, so the raw/
		// unfiltered map is the more honest choice here too - the diffuse-
		// lobe bucket below's cosine-weighted irradiance convolution
		// assumes a surface normal that doesn't exist at a scatter vertex.
		result = sampleEnvironmentRaw(params.environment, dir);
	}
	else if (escapeRoughness < 0.0f)
	{
		// Diffuse-lobe escape - real cosine-weighted irradiance convolution,
		// matching CpuPathTracer::tracePixel()'s identical lastBounceEnvRoughness
		// < 0.0f -> sampleEnvironmentDiffuse() treatment exactly (previously
		// used the roughest specular prefilter mip as an inexact stand-in -
		// see sampleEnvironmentDiffuse()'s own doc comment for why that
		// mattered).
		result = sampleEnvironmentDiffuse(params.environment, dir);
	}
	else
	{
		// Specular-lobe escape. The direction was already sampled from the
		// rough GGX lobe; sampling a roughness-prefiltered cubemap here would
		// apply that blur a second time and wash out mirror-like metallic
		// reflections. Use the sharp environment and let the Monte-Carlo lobe
		// sampling produce the rough reflection over many samples.
		result = sampleEnvironmentRaw(params.environment, dir);
	}

	if (previousBsdfPdf > 0.0f && params.enableEnvironmentImportanceSampling != 0)
	{
		const float envPdfAtRay = envSamplerPdf(params.environment, dir);
		if (envPdfAtRay > 0.0f)
			result = result * (previousBsdfPdf / (previousBsdfPdf + envPdfAtRay));
	}

	setPayload(result);
	optixSetPayload_3(0u); // hitFlag = miss, no continuation for the raygen loop

	// No primary-hit surface to derive a guide value from on a miss -
	// matches CpuPathTracer's own zero-initialized outPrimaryAlbedo/
	// outPrimaryNormal default when the primary ray never hits geometry.
	optixSetPayload_4(0u); optixSetPayload_5(0u); optixSetPayload_6(0u);
	optixSetPayload_14(0u); optixSetPayload_15(0u); optixSetPayload_16(0u);
	optixSetPayload_19(0u);
}

// glTF alphaMode Masked/Blend cutout - any-hit runs for EVERY ray type
// (primary/bounce trace calls and shadow rays alike), so a rejected hit is
// invisible to all of them uniformly, matching the spec's "treat as if this
// geometry doesn't exist here" semantics (CpuPathTracer's own MASK/BLEND
// handling in tracePixel()'s identical alphaTest block). optixIgnoreIntersection()
// tells BVH traversal to discard this candidate hit and keep looking - it
// does NOT terminate traversal, so a shadow ray with
// OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT correctly continues past a rejected
// hit to find (or not find) a REAL occluder behind it. Opaque (blendMode==0)
// materials always accept the hit here (a no-op any-hit program is the same
// as not having one at all). Masked (1) uses a deterministic threshold test
// (matching main_scene.frag's hard discard); Blend (2, glTF alphaMode BLEND)
// stochastically picks per sample instead - ported from CpuPathTracer's
// identical "rng.next01() >= alphaTest" - since a binary "was the surface
// here or not" existence pick needs no weight adjustment either way (unlike
// a Fresnel-weighted energy split), the whichever-branch-taken throughput is
// already correct as-is. Needs actual randomness (unlike Masked's fixed
// threshold), which bounce-trace rays get from payload 17 (the same RNG
// seed traceBouncePath() threads in) and shadow rays get from payload 3
// (threaded in by traceShadowRay() specifically for this - see its own doc
// comment) - both payloads are ADVANCED (hashed and written back) here so a
// ray passing through several Blend surfaces in a row draws a fresh value
// at each one instead of reusing the same draw. Previously entirely
// unimplemented in this kernel (every Blend material rendered fully
// opaque) - visible as a deterministic (non-noise, doesn't improve with more
// samples) ghost of the wrong/hidden layer bleeding through an overlapping
// Blend-textured surface, reported against glTF's own NegativeScaleTest.gltf
// sample (its answer-key texture cards use alphaMode BLEND).
//
// Also handles KHR_materials_transmission's shadow-ray pass-through (SHADOW
// RAYS ONLY - see the dedicated check inside this function for the full
// rationale): without it, any transmissive (glass) material would block
// shadow rays as if fully opaque, since __closesthit__ch()'s own
// transmission handling only runs for ordinary camera/bounce rays.
extern "C" __global__ void __anyhit__ah()
{
	if (optixGetRayFlags() & OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT)
	{
		const unsigned int selfInstanceId = optixGetPayload_1();
		const unsigned int selfShadowsEnabled = optixGetPayload_2();
		if (selfShadowsEnabled == 0u && optixGetInstanceId() == selfInstanceId)
		{
			optixIgnoreIntersection();
			return;
		}
	}

	const RtOptixSceneHitGroupData* data = reinterpret_cast<const RtOptixSceneHitGroupData*>(optixGetSbtDataPointer());

	const bool isShadowRayAH = (optixGetRayFlags() & OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT) != 0;

	// glTF material.doubleSided==false back-face culling - a single-sided
	// material's back face generally doesn't exist for any ray type, same
	// "invisible uniformly" semantics as the Masked cutout below. One
	// important exception: non-shadow rays crossing out of a solid
	// KHR_materials_volume surface must reach closest-hit so Beer-Lambert
	// attenuation can be applied at the exit surface. CPU does exactly that:
	// it applies volume attenuation before its later single-sided backface
	// pass-through. If any-hit discards that exit first, PTG never sees the
	// attenuation color.
	// optixIsTriangleFrontFaceHit() needs NO negative-determinant-instance
	// correction (no OPTIX_INSTANCE_FLAG_FLIP_TRIANGLE_FACING at IAS-build
	// time, deliberately - see RtOptixSceneTracer.cpp's IAS-build comment for
	// the derivation showing OptiX's native object-space winding test is
	// already correct for any instance transform, reflections included).
	// Conceptually matches raster's gl_FrontFacing-based discard (see
	// main_scene.frag) - previously entirely unimplemented in this kernel, so
	// every material rendered as if double-sided. Reported against glTF's
	// own NegativeScaleTest.gltf sample.
	//
	// hasVolumeScattering is ALSO included here (not just transmission>
	// 0.001f) - KHR_materials_volume_scatter's free-flight random walk
	// (see __closesthit__ch()'s hitBackface/hasVolumeScattering gate) enters
	// its medium via KHR_materials_diffuse_transmission's back-hemisphere
	// lobe, not specular KHR_materials_transmission, so a volume-scatter
	// material with NO specular transmission (e.g. ScatteringSkull.gltf,
	// which is single-sided and diffuse_transmission-only) would otherwise
	// have every single one of its interior/backface hits silently
	// discarded here before ever reaching __closesthit__ch() - meaning
	// hitBackface could NEVER become true and the walk could never run at
	// all. This was found by adding a debug view that colors any hit where
	// hasVolume/hasVolumeScattering read true, decoupled from hitBackface -
	// it showed those flags correct elsewhere but never on this material's
	// own backface hits, isolating the bug to this any-hit cull rather than
	// the material data itself (CPU, which has no equivalent any-hit
	// culling stage, already proved the material data correct).
	const bool solidVolumeExitCandidate = !isShadowRayAH && data->hasVolume != 0 && (data->transmission > 0.001f || data->hasVolumeScattering != 0);
	if (data->twoSided == 0 && !optixIsTriangleFrontFaceHit() && !solidVolumeExitCandidate)
	{
		optixIgnoreIntersection();
		return;
	}

	const bool needsAlphaTest = data->blendMode != 0;
	// KHR_materials_transmission - SHADOW RAYS ONLY (ordinary camera/bounce
	// rays instead handle transmission via __closesthit__ch()'s dedicated
	// reflect/refract branch, which needs this hit's REAL shading, not a
	// pass-through skip) - see the check further down for the full
	// rationale.
	const bool needsTransmissionTest = isShadowRayAH && data->transmission > 0.001f;
	// KHR_materials_diffuse_transmission - SHADOW RAYS ONLY, same reasoning
	// as needsTransmissionTest above. Without this, a shadow ray hitting a
	// diffuse-transmissive surface (e.g. another leaf in a dense plant model)
	// registered as fully opaque, since only mat.transmission (glass/
	// dielectric) had a pass-through branch - the diffuse-transmission back-
	// side NEE ray (this kernel's rawNdotL<=0 branch above) toward a light
	// almost always clips at least one OTHER leaf instance before reaching
	// it, so every leaf's transmitted glow read as fully shadowed the
	// instant Shadows was enabled, independent of the Self Shadows toggle
	// (which only excludes the ORIGINATING instance, not other leaves
	// genuinely in the ray's path). See CpuPathTracer::traceShadowRay()'s
	// identical addition.
	const bool needsDiffuseTransmissionTest = isShadowRayAH && data->diffuseTransmissionFactor > 0.001f;
	if (!needsAlphaTest && !needsTransmissionTest && !needsDiffuseTransmissionTest)
		return; // Opaque and non-transmissive (or a non-shadow ray) - no test needed at all

	const unsigned int primIdx = optixGetPrimitiveIndex();
	const uint3 tri = data->indices[primIdx];
	const float2 bary = optixGetTriangleBarycentrics();
	const float u = bary.x, v = bary.y, w = 1.0f - u - v;

	float2 uv[4];
	interpolateUVs(data, tri, w, u, v, uv);

	bool passThrough = false;
	if (needsAlphaTest)
	{
		const float opacity = resolveOpacity(data, uv);
		if (data->blendMode == 1) // Masked
		{
			passThrough = opacity < data->alphaThreshold;
		}
		else // Blend (glTF alphaMode BLEND) - stochastic, see this function's doc comment
		{
			const unsigned int seed = pcgHash(isShadowRayAH ? optixGetPayload_3() : optixGetPayload_17());
			if (isShadowRayAH)
				optixSetPayload_3(seed);
			else
				optixSetPayload_17(seed);
			passThrough = hashToUnitFloat(seed) >= opacity;
		}
	}

	// KHR_materials_transmission's shadow-ray handling - ported from
	// CpuPathTracer::traceShadowRay()'s identical Fresnel-weighted
	// stochastic pass-through. CPU carries full RGB transmittance through a
	// closest-hit-walking shadow loop; this OptiX any-hit path mirrors that
	// by multiplying payload 4-6 by baseColor * volumeTint *
	// transmissionFactor whenever the ray passes through. Uses the FLAT
	// (per-triangle) normal, not the smooth shading one, matching CPU's own
	// hit.geometricNormal-based Fresnel term exactly - fetched via
	// optixGetTriangleVertexData() (needs OPTIX_BUILD_FLAG_ALLOW_RANDOM_VERTEX_ACCESS
	// on this GAS, already enabled for the texture-footprint/LOD computation
	// - see RtOptixSceneTracer.cpp's GAS build).
	float3 transmissionTint = make_float3(1.0f, 1.0f, 1.0f);
	if (!passThrough && needsTransmissionTest)
	{
		float transmissionFactor = data->transmission;
		if (data->transmissionTexture.width > 0)
			transmissionFactor *= applyChannelPacking(sampleTexture2D(data->transmissionTexture, uv), data->transmissionTexture);
		transmissionFactor = fminf(fmaxf(transmissionFactor, 0.0f), 1.0f);

		float3 objectTriAH[3];
		optixGetTriangleVertexData(optixGetGASTraversableHandle(), primIdx, optixGetSbtGASIndex(), 0.0f, objectTriAH);
		const float3 objectFlatNormal = cross3(objectTriAH[1] - objectTriAH[0], objectTriAH[2] - objectTriAH[0]);
		const float3 worldFlatNormal = normalizeF3(optixTransformNormalFromObjectToWorldSpace(objectFlatNormal));
		const float3 rayDirAH = optixGetWorldRayDirection();
		const float NdotV = fminf(fmaxf(dot3(worldFlatNormal, rayDirAH * -1.0f), 0.0f), 1.0f);

		const float f0FromIor = powf((data->ior - 1.0f) / (data->ior + 1.0f), 2.0f);
		const float3 dielectricF0Shadow = make_float3(
			fminf(fmaxf(f0FromIor * data->specularColorFactor.x * data->specularFactor, 0.0f), 1.0f),
			fminf(fmaxf(f0FromIor * data->specularColorFactor.y * data->specularFactor, 0.0f), 1.0f),
			fminf(fmaxf(f0FromIor * data->specularColorFactor.z * data->specularFactor, 0.0f), 1.0f));
		const float3 fresnel = fresnelSchlick(NdotV, dielectricF0Shadow, make_float3(1.0f, 1.0f, 1.0f));
		const float reflectProb = fminf(fmaxf((fresnel.x + fresnel.y + fresnel.z) / 3.0f, 0.05f), 0.95f);

		float3 tint = data->baseColor;
		if (data->baseColorTexture.width > 0)
		{
			const float4 sampledBase = sampleTexture2D(data->baseColorTexture, uv);
			tint = tint * sRGBToLinear(make_float3(sampledBase.x, sampledBase.y, sampledBase.z));
		}
		if (data->hasVolume != 0)
			tint = tint * calculateVolumeAttenuation(data->attenuationColor, data->attenuationDistance, data->thicknessFactor);
		transmissionTint = tint * transmissionFactor;

		const unsigned int seed = pcgHash(optixGetPayload_3());
		optixSetPayload_3(seed);
		passThrough = hashToUnitFloat(seed) >= reflectProb;
	}

	// KHR_materials_diffuse_transmission's shadow-ray handling - mirrors
	// CpuPathTracer::traceShadowRay()'s identical addition. Stochastic
	// pass-through by diffuseTransmissionFactor (Russian-roulette, unbiased
	// over many samples), no Fresnel/IOR term needed since this isn't a
	// refractive BTDF like KHR_materials_transmission above.
	float3 diffuseTransmissionTint = make_float3(1.0f, 1.0f, 1.0f);
	if (!passThrough && needsDiffuseTransmissionTest)
	{
		float diffuseTransFactor = data->diffuseTransmissionFactor;
		if (data->diffuseTransmissionTexture.width > 0)
			diffuseTransFactor *= applyChannelPacking(sampleTexture2D(data->diffuseTransmissionTexture, uv), data->diffuseTransmissionTexture);
		diffuseTransFactor = fminf(fmaxf(diffuseTransFactor, 0.0f), 1.0f);

		// needsDiffuseTransmissionTest implies isShadowRayAH (see its
		// declaration above) - payload 3 is this any-hit shader's shadow-ray
		// RNG stream carrier (same one the alpha-test/transmission blocks
		// above chain through), no non-shadow-ray branch needed here.
		const unsigned int seed = pcgHash(optixGetPayload_3());
		optixSetPayload_3(seed);
		passThrough = hashToUnitFloat(seed) < diffuseTransFactor;
		if (passThrough)
		{
			float3 tint = data->diffuseTransmissionColor;
			if (data->diffuseTransmissionColorTexture.width > 0)
			{
				const float4 sampledColor = sampleTexture2D(data->diffuseTransmissionColorTexture, uv);
				tint = tint * sRGBToLinear(make_float3(sampledColor.x, sampledColor.y, sampledColor.z));
			}
			if (data->hasVolume != 0)
				tint = tint * calculateVolumeAttenuation(data->attenuationColor, data->attenuationDistance, data->thicknessFactor);
			diffuseTransmissionTint = tint;
		}
	}

	if (passThrough)
	{
		if (isShadowRayAH && (needsTransmissionTest || needsDiffuseTransmissionTest))
		{
			const float3 combinedTint = transmissionTint * diffuseTransmissionTint;
			const float3 shadowTr = make_float3(
				__uint_as_float(optixGetPayload_4()),
				__uint_as_float(optixGetPayload_5()),
				__uint_as_float(optixGetPayload_6())) * combinedTint;
			optixSetPayload_4(__float_as_uint(shadowTr.x));
			optixSetPayload_5(__float_as_uint(shadowTr.y));
			optixSetPayload_6(__float_as_uint(shadowTr.z));
		}
		optixIgnoreIntersection();
	}
}

extern "C" __global__ void __closesthit__ch()
{
	if (optixGetRayFlags() & OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT)
	{
		// Shadow ray hit something before reaching the light - occluded.
		// Any hit will do (that's the whole point of TERMINATE_ON_FIRST_HIT),
		// so skip straight past all the shading work below.
		optixSetPayload_0(1u);
		return;
	}

	const RtOptixSceneHitGroupData* data = reinterpret_cast<const RtOptixSceneHitGroupData*>(optixGetSbtDataPointer());
	const unsigned int instanceId = optixGetInstanceId();

	const unsigned int primIdx = optixGetPrimitiveIndex();
	const uint3 tri = data->indices[primIdx];

	// Smooth (vertex-interpolated) object-space normal - mirrors
	// RtEmbreeScene::intersect()'s localNormal - transformed to world space
	// using the CURRENT instance's transform, which OptiX already knows
	// from IAS traversal (no need to duplicate the matrix in the SBT
	// record).
	const float2 bary = optixGetTriangleBarycentrics();
	const float u = bary.x, v = bary.y, w = 1.0f - u - v;
	const float3 n0 = data->normals[tri.x];
	const float3 n1 = data->normals[tri.y];
	const float3 n2 = data->normals[tri.z];
	const float3 objectNormal = n0 * w + n1 * u + n2 * v;
	float3 worldNormal = normalizeF3(optixTransformNormalFromObjectToWorldSpace(objectNormal));

	// World-space hit position - ray tracing itself is in world space
	// regardless of instancing, so no transform needed here (unlike the
	// normal above, which was fetched in object space).
	const float3 rayOrigin = optixGetWorldRayOrigin();
	const float3 rayDir = optixGetWorldRayDirection();
	const float3 worldPos = rayOrigin + rayDir * optixGetRayTmax();

	// Flat per-triangle geometric normal, distinct from the smooth
	// vertex-interpolated shading normal above. CPU derives hitBackface
	// from dot(ray.direction, hit.geometricNormal)>0; do the same explicit
	// test here instead of relying on OptiX's front-face flag so volume
	// entry/exit classification is exactly shared between PTC and PTG.
	float3 objectTri[3];
	optixGetTriangleVertexData(optixGetGASTraversableHandle(), primIdx, optixGetSbtGASIndex(), 0.0f, objectTri);
	const float3 objectFlatNormal = cross3(objectTri[1] - objectTri[0], objectTri[2] - objectTri[0]);
	const float3 worldFlatNormal = normalizeF3(optixTransformNormalFromObjectToWorldSpace(objectFlatNormal));

	// Faceforward against the ray, matching CpuPathTracer's own "shade the
	// side the ray actually hit" handling for thin/backfacing geometry.
	if (dot3(worldNormal, rayDir) > 0.0f)
		worldNormal = worldNormal * -1.0f;

	// Stateless "am I currently inside this medium" test - ported from
	// CpuPathTracer::tracePixel()'s identical per-hit (never carried-forward)
	// derivation: true when the ray struck this triangle's BACK face,
	// meaning it's exiting a volume rather than entering one.
	// Uses the same flat geometric normal as CPU, not the smooth shading
	// normal (which may be normal-mapped/faceforwarded for shading).
	const bool hitBackface = dot3(rayDir, worldFlatNormal) > 0.0f;

	float2 uv[4];
	interpolateUVs(data, tri, w, u, v, uv);

	// Camera pixel footprint at this hit, converted to this hit's own UV
	// space via the triangle's own UV(channel 0)/world-area ratio - device
	// counterpart of CpuPathTracer::tracePixel()'s identical computation and
	// RtHit::uvAreaPerWorldArea's doc comment. Restricted to primary rays
	// only (escapeRoughness==-1.0f - see traceBouncePath()'s doc comment):
	// this backend keys "primary ray" off the same escape sentinel used by
	// the miss shader. Non-primary and transmission-continuation hits pass
	// footprintInUvArea=0.0f, which computeTextureLod() treats as "no LOD
	// info" (base mip only) - same pragmatic simplification CPU makes for
	// its own indirect hits.
	float footprintInUvArea = 0.0f;
	if (__uint_as_float(optixGetPayload_18()) == -1.0f)
	{
		const float3 worldP0 = optixTransformPointFromObjectToWorldSpace(objectTri[0]);
		const float3 worldP1 = optixTransformPointFromObjectToWorldSpace(objectTri[1]);
		const float3 worldP2 = optixTransformPointFromObjectToWorldSpace(objectTri[2]);
		const float worldArea = 0.5f * length3(cross3(worldP1 - worldP0, worldP2 - worldP0));

		const float2 uv0 = data->texCoords[tri.x * 4 + 0];
		const float2 uv1 = data->texCoords[tri.y * 4 + 0];
		const float2 uv2 = data->texCoords[tri.z * 4 + 0];
		const float uvArea = 0.5f * fabsf((uv1.x - uv0.x) * (uv2.y - uv0.y) - (uv2.x - uv0.x) * (uv1.y - uv0.y));
		const float uvAreaPerWorldArea = worldArea > 1e-12f ? (uvArea / worldArea) : 0.0f;

		const float pixelWorldSize = (params.camOrthographic != 0)
			? (2.0f * params.camOrthoHalfHeight / static_cast<float>(params.imageHeight))
			: (optixGetRayTmax() * 2.0f * params.camTanHalfFovY / static_cast<float>(params.imageHeight));
		footprintInUvArea = uvAreaPerWorldArea * pixelWorldSize * pixelWorldSize;
	}

	// Object-space tangent + handedness, barycentrically interpolated then
	// transformed to world space via the plain-model-matrix direction
	// transform (NOT normals' inverse-transpose one) - see
	// RtOptixSceneHitGroupData::tangents' doc comment. Interpolating the
	// precomputed +-1 handedness values and re-signing the result degrades
	// gracefully at a genuine UV-seam-straddling triangle (rare) exactly the
	// same way CpuPathTracer's own interpolate-then-derive approach does.
	const float4 tan0 = data->tangents[tri.x];
	const float4 tan1 = data->tangents[tri.y];
	const float4 tan2 = data->tangents[tri.z];
	const float3 objectTangent = make_float3(
		tan0.x * w + tan1.x * u + tan2.x * v,
		tan0.y * w + tan1.y * u + tan2.y * v,
		tan0.z * w + tan1.z * u + tan2.z * v);
	const float interpolatedHandedness = tan0.w * w + tan1.w * u + tan2.w * v;
	const float3 worldTangent = optixTransformVectorFromObjectToWorldSpace(objectTangent);
	const float4 worldTangentAndHandedness = make_float4(worldTangent.x, worldTangent.y, worldTangent.z,
		(interpolatedHandedness >= 0.0f) ? 1.0f : -1.0f);

	// Geometric (faceforward, pre-normal-map) shading normal - the base for
	// KHR_materials_clearcoat's OWN normal map below, matching
	// CpuPathTracer::tracePixel()'s Ncoat derivation (applyNormalMap(hit.
	// normal, ..., clearcoatNormalTexture, ...) - i.e. built from the same
	// raw geometric normal the base layer starts from, NOT from the
	// already-base-normal-mapped worldNormal).
	const float3 geometricNormal = worldNormal;

	worldNormal = applyNormalMap(worldNormal, worldTangentAndHandedness, data->normalTexture, uv, data->normalScale);

	// KHR_materials_clearcoat's own normal map, independent of the base
	// layer's - falls back to geometricNormal (the pre-base-normal-map
	// smooth normal) when absent, matching CpuPathTracer::tracePixel()'s
	// main bounce-sampling loop exactly: its Ncoat is
	// applyNormalMap(Nsmooth, ..., mat.clearcoatNormalTexture.get(), ...)
	// at CpuPathTracer.cpp's own use site, and applyNormalMap() returns its
	// input unchanged when there's no texture - so CPU's effective fallback
	// is Nsmooth (pre-base-normal-map), not the already-perturbed N. A
	// previous version of this fallback used worldNormal (POST-base-normal-
	// map, since that reassignment happens on the line above) - wrong, and
	// consequential: a car-paint base color/metallic-flake normal map is
	// exactly the kind of noisy per-pixel detail whose leaking into the
	// clearcoat's OWN reflection direction broadens/scrambles what should be
	// a much smoother coat reflection, visible from the very first sample
	// (this is a deterministic per-hit normal, not a shadow-ray/sampling-
	// noise effect) - this was the actual cause of a reported PTC-vs-PTG
	// clearcoat reflection mismatch on automotive paint, found after ruling
	// out TBN handedness, shadow-ray origin, environment prefilter lookup,
	// clearcoat GGX sampling, and texture-LOD formula differences.
	const float3 Ncoat = (data->clearcoatNormalTexture.width > 0)
		? applyNormalMap(geometricNormal, worldTangentAndHandedness, data->clearcoatNormalTexture, uv, data->clearcoatNormalScale)
		: geometricNormal;

	// Core PBR textures, matching CpuPathTracer::evaluateSurface()'s exact
	// factor*texture multiply order and sRGB/linear decode split (baseColor/
	// emissive are sRGB-encoded color data; metallic/roughness are linear
	// scalar data, channel-packed via applyChannelPacking()).
	float3 baseColor = data->baseColor;
	if (data->baseColorTexture.width > 0)
	{
		const float4 sampled = sampleTexture2D(data->baseColorTexture, uv, computeTextureLod(data->baseColorTexture, footprintInUvArea));
		baseColor = baseColor * sRGBToLinear(make_float3(sampled.x, sampled.y, sampled.z));
	}

	// COLOR_0 vertex color, applied last (in linear) - matches
	// CpuPathTracer::evaluateSurface()'s "apply vertex color last" comment.
	// Always (1,1,1) when the mesh has no vertex color attribute (see
	// RtOptixSceneHitGroupData::vertexColors' doc comment), so this is
	// always safe to apply unconditionally.
	{
		const float3 vc0 = data->vertexColors[tri.x];
		const float3 vc1 = data->vertexColors[tri.y];
		const float3 vc2 = data->vertexColors[tri.z];
		baseColor = baseColor * (vc0 * w + vc1 * u + vc2 * v);
	}

	float metalness = data->metalness;
	if (data->metallicTexture.width > 0)
		metalness *= applyChannelPacking(sampleTexture2D(data->metallicTexture, uv, computeTextureLod(data->metallicTexture, footprintInUvArea)), data->metallicTexture);

	float roughnessFactor = data->roughness;
	if (data->roughnessTexture.width > 0)
		roughnessFactor *= applyChannelPacking(sampleTexture2D(data->roughnessTexture, uv, computeTextureLod(data->roughnessTexture, footprintInUvArea)), data->roughnessTexture);

	float3 emissive = data->emissive * data->emissiveStrength;
	if (data->emissiveTexture.width > 0)
	{
		const float4 sampled = sampleTexture2D(data->emissiveTexture, uv, computeTextureLod(data->emissiveTexture, footprintInUvArea));
		emissive = emissive * sRGBToLinear(make_float3(sampled.x, sampled.y, sampled.z));
	}

	// Ambient occlusion - ported from CpuPathTracer::evaluateSurface()'s
	// "clamp(mix(1.0, texAO, occlusionStrength), 0.0001, 1.0)". CPU applies
	// this to diffuse/specular IBL and environment-escape terms specifically,
	// never to direct (punctual) lighting - see RtMaterial::occlusionStrength's
	// doc comment. This backend has no separate IBL step (indirect light is
	// real bounced continuation instead), so as a deliberately simpler
	// approximation of that same intent, AO here darkens only the diffuse
	// lobe's own indirect throughput weight below (matching how baked AO
	// maps are conventionally applied in real-time engines - to ambient/
	// indirect diffuse specifically, not specular or direct light).
	float ao = 1.0f;
	if (data->aoTexture.width > 0)
	{
		const float texAo = applyChannelPacking(sampleTexture2D(data->aoTexture, uv, computeTextureLod(data->aoTexture, footprintInUvArea)), data->aoTexture);
		const float mixed = 1.0f + (texAo - 1.0f) * data->occlusionStrength; // mix(1.0, texAo, occlusionStrength)
		ao = fminf(fmaxf(mixed, 0.0001f), 1.0f);
	}

	// KHR_materials_specular's per-pixel maps - specularTexture's alpha
	// channel scales specularFactor (channel-packed), specularColorTexture's
	// RGB (sRGB-encoded) tints specularColorFactor - matching CpuPathTracer::
	// evaluateSurface()'s identical modulation before computeF0F90().
	float texturedSpecularFactor = data->specularFactor;
	if (data->specularTexture.width > 0)
		texturedSpecularFactor *= applyChannelPacking(sampleTexture2D(data->specularTexture, uv, computeTextureLod(data->specularTexture, footprintInUvArea)), data->specularTexture);

	float3 texturedSpecularColorFactor = data->specularColorFactor;
	if (data->specularColorTexture.width > 0)
	{
		const float4 sampled = sampleTexture2D(data->specularColorTexture, uv, computeTextureLod(data->specularColorTexture, footprintInUvArea));
		texturedSpecularColorFactor = texturedSpecularColorFactor * sRGBToLinear(make_float3(sampled.x, sampled.y, sampled.z));
	}

	// Ported from CpuPathTracer::computeF0F90() (itself from main_scene.frag's
	// computeDielectricF0()/computeF90()): KHR_materials_ior replaces the
	// fixed 0.04 dielectric F0 with an IOR-derived value; KHR_materials_
	// specular scales/tints the dielectric term (no effect on metals). F0 is
	// the general/indirect-bounce reflectance (BSDF lobe sampling below);
	// directF0 additionally scales the dielectric term by specularFactor a
	// second time and is used ONLY by the direct-lighting loop - the same
	// F0-vs-directF0 split CpuPathTracer's evaluateDirectBRDF() (directF0)
	// vs sampleBSDFBounce()/computeLobeProbabilities() (F0) applies. The
	// pre-metal-mix dielectric terms CPU also computes (dielectricF0/
	// dielectricDirectF0) are iridescence-only consumers, deferred here.
	const float f0FromIor = powf((data->ior - 1.0f) / (data->ior + 1.0f), 2.0f);
	float3 dielectricF0 = make_float3(f0FromIor, f0FromIor, f0FromIor);
	if (texturedSpecularFactor > 0.0f)
		dielectricF0 = dielectricF0 * texturedSpecularColorFactor;
	dielectricF0 = make_float3(
		fminf(fmaxf(dielectricF0.x, 0.0f), 1.0f),
		fminf(fmaxf(dielectricF0.y, 0.0f), 1.0f),
		fminf(fmaxf(dielectricF0.z, 0.0f), 1.0f));

	// Shading view vector - see CpuPathTracer::tracePixel()'s identical fix
	// for the full rationale. Perspective: -rayDir already equals normalize
	// (camPosition - worldPos) exactly, no divergence. Orthographic PRIMARY
	// rays (escapeRoughness==-1.0f, the sentinel traceBouncePath()'s doc
	// comment gives the very first/camera ray) are traced PARALLEL (constant
	// rayDirection=camForward - see __raygen__rg()'s ray setup), so -rayDir
	// is constant across the whole object, whereas main_scene.frag's
	// frame.V = normalize(cameraPos - fragPos) is NOT (no ortho branch there
	// at all) - varying per-fragment even in ortho. Using the constant
	// vector fed both the Fresnel terms and the specular lobe's own sampling
	// frame, producing a systematically differently scaled/framed reflected-
	// environment direction than raster - the reported "overstretched"/
	// mismatched ortho reflection (perspective was unaffected, hence why it
	// only showed up in ortho mode). Every OTHER hit (secondary bounce) is a
	// REAL traced ray with its own genuine incoming direction and must stay
	// -rayDir - there is no single "camera position" a bounce ray's shading
	// point is at a fixed relative offset from.
	const float primaryRaySentinel = __uint_as_float(optixGetPayload_18());
	const bool isPrimaryOrthoHit = (params.camOrthographic != 0) && (primaryRaySentinel == -1.0f);
	// KHR_materials_volume_scatter's free-flight walk needs no special-case
	// here (unlike the old BSSRDF redirect this replaces): a scatter
	// continuation's ray genuinely travels from the scatter vertex in the
	// HG-sampled direction and lands on whatever real surface it lands on,
	// so V = -rayDir is already correct without any override - there's no
	// "cached hit reused as the next vertex" self-consistency concern the
	// way BSSRDF's redirect had.
	const float3 V = isPrimaryOrthoHit ? normalizeF3(params.camPosition - worldPos) : normalizeF3(rayDir * -1.0f);
	const float NdotV = fmaxf(dot3(worldNormal, V), 0.0f);
	float3 F0 = lerp3(dielectricF0, baseColor, metalness);
	float3 F90 = lerp3(make_float3(texturedSpecularFactor, texturedSpecularFactor, texturedSpecularFactor), make_float3(1.0f, 1.0f, 1.0f), metalness);
	float3 directF0 = lerp3(dielectricF0 * texturedSpecularFactor, baseColor, metalness);
	float roughness = fmaxf(roughnessFactor, 0.0001f); // matches main_scene.frag/CpuPathTracer roughness floor

	// KHR_materials_pbrSpecularGlossiness - legacy alternate workflow; COMPLETELY
	// REPLACES the metallic-roughness values just computed above, matching
	// CpuPathTracer::evaluateSurface()'s identical override block exactly.
	// baseColor becomes diffuseColor, F0 becomes the specular color directly
	// (an authored RGB reflectance, not IOR-derived), F90 is forced to 1.0,
	// metalness is forced to 0 (spec-gloss has no metalness concept), and
	// roughness is derived from glossiness's inverse. dielectricF0 is also
	// overridden so KHR_materials_iridescence (if combined with spec-gloss,
	// via dielectricDirectF0 = dielectricF0*texturedSpecularFactor below)
	// still gets a sensible base reflectance to work from.
	if (data->useSpecGloss != 0)
	{
		baseColor = data->diffuseColor;
		if (data->diffuseTexture.width > 0)
		{
			const float4 sampled = sampleTexture2D(data->diffuseTexture, uv, computeTextureLod(data->diffuseTexture, footprintInUvArea));
			baseColor = baseColor * sRGBToLinear(make_float3(sampled.x, sampled.y, sampled.z));
		}
		{
			const float3 vc0 = data->vertexColors[tri.x];
			const float3 vc1 = data->vertexColors[tri.y];
			const float3 vc2 = data->vertexColors[tri.z];
			baseColor = baseColor * (vc0 * w + vc1 * u + vc2 * v);
		}

		float3 specGlossColor = data->specGlossSpecularColor;
		float glossiness = data->glossinessFactor;
		if (data->specularGlossinessTexture.width > 0)
		{
			const float4 packed = sampleTexture2D(data->specularGlossinessTexture, uv, computeTextureLod(data->specularGlossinessTexture, footprintInUvArea));
			specGlossColor = specGlossColor * sRGBToLinear(make_float3(packed.x, packed.y, packed.z));
			glossiness *= packed.w;
		}
		specGlossColor = make_float3(fminf(fmaxf(specGlossColor.x, 0.0f), 1.0f), fminf(fmaxf(specGlossColor.y, 0.0f), 1.0f), fminf(fmaxf(specGlossColor.z, 0.0f), 1.0f));

		roughness = fminf(fmaxf(1.0f - glossiness, 0.0001f), 1.0f);
		metalness = 0.0f;
		F0 = specGlossColor;
		F90 = make_float3(1.0f, 1.0f, 1.0f);
		directF0 = specGlossColor;
		dielectricF0 = specGlossColor;
		texturedSpecularFactor = 1.0f;
	}

	// KHR_materials_clearcoat - factors/textures ported from CpuPathTracer::
	// evaluateSurface()'s identical R/G-channel-packed sampling.
	float clearcoat = data->clearcoat;
	if (data->clearcoatTexture.width > 0)
		clearcoat *= applyChannelPacking(sampleTexture2D(data->clearcoatTexture, uv, computeTextureLod(data->clearcoatTexture, footprintInUvArea)), data->clearcoatTexture);
	clearcoat = fminf(fmaxf(clearcoat, 0.0f), 1.0f);

	float clearcoatRoughness = data->clearcoatRoughness;
	if (data->clearcoatRoughnessTexture.width > 0)
		clearcoatRoughness *= applyChannelPacking(sampleTexture2D(data->clearcoatRoughnessTexture, uv, computeTextureLod(data->clearcoatRoughnessTexture, footprintInUvArea)), data->clearcoatRoughnessTexture);
	clearcoatRoughness = fminf(fmaxf(clearcoatRoughness, 0.0001f), 1.0f);

	// KHR_materials_sheen - sheenColorTexture is sRGB RGB, sheenRoughnessTexture's
	// alpha channel scales sheenRoughness (channel-packed by RtSceneBuilder),
	// matching CpuPathTracer::evaluateSurface() exactly.
	float3 sheenColor = data->sheenColorFactor;
	if (data->sheenColorTexture.width > 0)
	{
		const float4 sampled = sampleTexture2D(data->sheenColorTexture, uv, computeTextureLod(data->sheenColorTexture, footprintInUvArea));
		sheenColor = sheenColor * sRGBToLinear(make_float3(sampled.x, sampled.y, sampled.z));
	}
	sheenColor = make_float3(fminf(fmaxf(sheenColor.x, 0.0f), 1.0f), fminf(fmaxf(sheenColor.y, 0.0f), 1.0f), fminf(fmaxf(sheenColor.z, 0.0f), 1.0f));

	float sheenRoughness = data->sheenRoughness;
	if (data->sheenRoughnessTexture.width > 0)
		sheenRoughness *= applyChannelPacking(sampleTexture2D(data->sheenRoughnessTexture, uv, computeTextureLod(data->sheenRoughnessTexture, footprintInUvArea)), data->sheenRoughnessTexture);
	sheenRoughness = fminf(fmaxf(sheenRoughness, 0.0001f), 1.0f);

	// Fresnel-weighted blend factor between the base layer and the coat layer
	// - ported from CpuPathTracer::tracePixel()'s "clearcoat * computeClearcoatFresnel(...)",
	// consumed both by the direct-lighting mix() below and by
	// computeLobeProbabilities()'s coat-lobe sampling weight.
	const float3 clearcoatBlend = computeClearcoatFresnel(data->ior, Ncoat, V) * clearcoat;

	// kDebugVisualizeClearcoat - false-colors the primary hit only, mirroring
	// CpuPathTracer::tracePixel()'s identical capture exactly: red =
	// clearcoat (raw, texture-sampled mask), green = clearcoatBlend.x (the
	// angle-dependent Fresnel weight actually used to composite the coat over
	// the base layer), blue = the mip LOD actually used to sample
	// data->clearcoatTexture, normalized against its own mip-chain length (0
	// = base level/sharpest, 1 = smallest/blurriest mip). Written directly
	// into the radiance payload (0-2) and hitFlag forced to 0 (the same value
	// __miss__ms() uses for "escaped, no continuation") so the raygen loop's
	// `if (hitFlag == 0u) break;` stops this sample immediately after -
	// matching CpuPathTracer's own early substitution-for-the-whole-function-
	// return exactly (no later bounce ever dilutes this pixel's debug color).
	if (kDebugVisualizeClearcoat && primaryRaySentinel == -1.0f)
	{
		float lodNorm = 0.0f;
		if (data->clearcoatTexture.width > 0 && data->clearcoatTexture.mipCount > 1)
		{
			const float lod = computeTextureLod(data->clearcoatTexture, footprintInUvArea);
			lodNorm = fminf(fmaxf(lod / static_cast<float>(data->clearcoatTexture.mipCount - 1), 0.0f), 1.0f);
		}
		optixSetPayload_0(__float_as_uint(clearcoat));
		optixSetPayload_1(__float_as_uint(clearcoatBlend.x));
		optixSetPayload_2(__float_as_uint(lodNorm));
		optixSetPayload_3(0u); // hitFlag = 0 ("escaped", matching __miss__ms()'s value) - terminates the raygen bounce loop right after this sample
		return;
	}

	// KHR_materials_iridescence's direct-lighting branch needs the PRE-metal-
	// mix dielectric direct-F0 standalone (see CpuPathTracer::computeF0F90()'s
	// outDielectricDirectF0 doc comment) - dielectricF0*texturedSpecularFactor
	// is already computed inline as part of directF0 above; naming it here
	// just reuses that same expression.
	const float3 dielectricDirectF0 = dielectricF0 * texturedSpecularFactor;

	// KHR_materials_anisotropy - stretches the base specular lobe along a
	// tangent-space direction. Ported from CpuPathTracer::tracePixel()'s
	// per-hit computation (buildAnisotropyBasis() in main_scene.frag) using
	// geometricNormal as the analog of frame.Ng/Nsmooth (the smoothly-
	// interpolated, pre-normal-map normal - NOT the flat per-triangle one).
	// Requires real tangent data to build a meaningful basis - untextured/
	// tangentless meshes simply render isotropically (hasAniso false), same
	// scoping already accepted for normal mapping in this file.
	float anisotropyStrength = data->anisotropyStrength;
	float anisotropyRotation = data->anisotropyRotation;
	if (data->anisotropyTexture.width > 0)
	{
		const float4 sampled = sampleTexture2D(data->anisotropyTexture, uv, computeTextureLod(data->anisotropyTexture, footprintInUvArea));
		decodeAnisotropyTexture(make_float3(sampled.x, sampled.y, sampled.z), data->anisotropyStrength, data->anisotropyRotation,
			anisotropyStrength, anisotropyRotation);
	}
	const float3 worldTangentDir = make_float3(worldTangentAndHandedness.x, worldTangentAndHandedness.y, worldTangentAndHandedness.z);
	const bool hasAniso = anisotropyStrength > 0.0f && dot3(worldTangentDir, worldTangentDir) > 0.0001f;
	float3 anisoT = make_float3(1.0f, 0.0f, 0.0f);
	float3 anisoB = make_float3(0.0f, 1.0f, 0.0f);
	float anisoAlphaT = roughness * roughness;
	float anisoAlphaB = anisoAlphaT;
	if (hasAniso)
	{
		// Tb/Bb: same orthogonalized-tangent-frame derivation applyNormalMap()
		// uses (T orthogonalized against N, B = normalize(cross(N,T))*
		// handedness) - this backend only stores one tangent+handedness sign
		// per vertex (not independent tangent/bitangent attributes like
		// CpuPathTracer's hit.tangent/hit.bitangent), so Bb is ALREADY
		// guaranteed orthogonal/consistently-oriented by construction; no
		// extra "flip if cross(Tb,Bb)*N < 0" correction is needed (or even
		// meaningful) here the way CPU's independently-stored attributes need.
		const float3 Tb = normalizeF3(worldTangentDir - geometricNormal * dot3(worldTangentDir, geometricNormal));
		const float3 Bb = normalizeF3(cross3(geometricNormal, Tb)) * worldTangentAndHandedness.w;

		const float2 dir = make_float2(cosf(anisotropyRotation), sinf(anisotropyRotation));
		anisoT = normalizeF3(Tb * dir.x + Bb * dir.y);
		anisoB = normalizeF3(cross3(geometricNormal, anisoT));
		if (dot3(anisoB, anisoB) < 0.0001f * 0.0001f)
			anisoB = normalizeF3(cross3(geometricNormal, Tb));
		if (dot3(cross3(anisoT, anisoB), geometricNormal) < 0.0f)
			anisoB = anisoB * -1.0f;

		const float alphaRoughness = fmaxf(roughness * roughness, 0.001f);
		anisoAlphaT = alphaRoughness + (1.0f - alphaRoughness) * (anisotropyStrength * anisotropyStrength);
		anisoAlphaB = fminf(fmaxf(alphaRoughness, 0.001f), 1.0f);
	}

	// KHR_materials_iridescence - iridescenceTexture is channel-packed
	// (scales iridescenceFactor); iridescenceThicknessTexture REPLACES
	// iridescenceThickness rather than scaling it (matches CpuPathTracer::
	// evaluateSurface() exactly).
	float iridescenceFactor = data->iridescenceFactor;
	if (data->iridescenceTexture.width > 0)
		iridescenceFactor *= applyChannelPacking(sampleTexture2D(data->iridescenceTexture, uv, computeTextureLod(data->iridescenceTexture, footprintInUvArea)), data->iridescenceTexture);
	iridescenceFactor = fminf(fmaxf(iridescenceFactor, 0.0f), 1.0f);

	const float iridescenceIor = data->iridescenceIor;
	float iridescenceThickness = data->iridescenceThickness;
	if (data->iridescenceThicknessTexture.width > 0)
		iridescenceThickness = applyChannelPacking(sampleTexture2D(data->iridescenceThicknessTexture, uv, computeTextureLod(data->iridescenceThicknessTexture, footprintInUvArea)), data->iridescenceThicknessTexture);

	// KHR_materials_diffuse_transmission - a translucent DIFFUSE material
	// (leaves, paper, curtains): light landing on the front diffusely
	// scatters through to the back (and vice versa), no ior/Fresnel/
	// refraction involved. diffuseTransmissionTexture's ALPHA channel scales
	// diffuseTransmissionFactor (glTF spec convention); diffuseTransmissionColorTexture
	// is sRGB RGB - matching CpuPathTracer::evaluateSurface() exactly.
	float diffuseTransmissionFactor = data->diffuseTransmissionFactor;
	if (data->diffuseTransmissionTexture.width > 0)
		diffuseTransmissionFactor *= applyChannelPacking(sampleTexture2D(data->diffuseTransmissionTexture, uv, computeTextureLod(data->diffuseTransmissionTexture, footprintInUvArea)), data->diffuseTransmissionTexture);

	float3 diffuseTransmissionColor = data->diffuseTransmissionColor;
	if (data->diffuseTransmissionColorTexture.width > 0)
	{
		const float4 sampled = sampleTexture2D(data->diffuseTransmissionColorTexture, uv, computeTextureLod(data->diffuseTransmissionColorTexture, footprintInUvArea));
		diffuseTransmissionColor = diffuseTransmissionColor * sRGBToLinear(make_float3(sampled.x, sampled.y, sampled.z));
	}

	// KHR_materials_transmission + KHR_materials_volume + KHR_materials_
	// dispersion - transmissionTexture's R channel scales transmission
	// (channel-packed); hasVolume/attenuationColor/attenuationDistance/
	// thicknessFactor/dispersion have no texture inputs (matches
	// CpuPathTracer::evaluateSurface() exactly).
	float transmission = data->transmission;
	if (data->transmissionTexture.width > 0)
		transmission *= applyChannelPacking(sampleTexture2D(data->transmissionTexture, uv, computeTextureLod(data->transmissionTexture, footprintInUvArea)), data->transmissionTexture);
	const int hasVolume = data->hasVolume;
	const float3 attenuationColor = data->attenuationColor;
	const float attenuationDistance = data->attenuationDistance;
	const float thicknessFactor = data->thicknessFactor;
	const float dispersion = data->dispersion;
	const int hasVolumeScattering = data->hasVolumeScattering;
	const float3 multiScatterColor = data->multiScatterColor;

	// KHR shadow-catcher floor mode (path tracer only) - the background
	// here is a real environment map/skybox (confirmed), so NVIDIA's
	// original env-radiance-substitution approach is the right model - the
	// floor's own material is never meant to be seen. History: an earlier
	// version of this block ALSO folded in a stochastic ambient/IBL-
	// occlusion ray (reasoning that a punctual-light-only shadowFactor
	// misses IBL contact darkening) - but the user confirmed the punctual-
	// light-only shadow shape below was ALREADY correct/well-shaped; the
	// actual (now-fixed, see below) bug was that unshadowed regions weren't
	// blending to true invisibility. Adding AO on top instead made the
	// ENTIRE quad read as occluded (a single hemisphere sample bounded to a
	// radius large enough to cover the whole floor's footprint has real
	// odds of still grazing the model's own body from almost anywhere on
	// that footprint), turning the whole patch into a uniform shadow
	// instead of a localized one - so removed again; shadowFactor is
	// punctual-lights only, matching what was already confirmed to look
	// right. Separately: the "shadowed" branch used to ALSO spend a whole
	// extra cosine-weighted GI bounce off a flat override material, which
	// could pick up bounce light (e.g. off the model's own bright/white
	// paint) that a genuine background pixel never would, visibly
	// brightening/color-shifting the quad relative to its true
	// surroundings - removed entirely; a shadow-catcher hit now always
	// terminates with a single substituted/darkened radiance value, exactly
	// mirroring a real miss with zero extra bounces.
	if (data->isShadowCatcher != 0)
	{
		// shadowFactor: same lit/fullyLit-ratio generalization CPU uses,
		// reusing the existing per-light evaluatePunctualLight()/
		// traceShadowRay() pattern (see the NEE loop further below).
		float3 litSum = make_float3(0.0f, 0.0f, 0.0f);
		float3 unshadowedSum = make_float3(0.0f, 0.0f, 0.0f);
		unsigned int catcherRng = optixGetPayload_17();
		for (unsigned int i = 0; i < params.lightCount; ++i)
		{
			float3 lightDir, lightIntensity;
			float lightDistance;
			evaluatePunctualLight(params.lights[i], worldPos, lightDir, lightIntensity, lightDistance);
			if (lightIntensity.x <= 0.0f && lightIntensity.y <= 0.0f && lightIntensity.z <= 0.0f)
				continue;
			const float NdotL = dot3(worldNormal, lightDir);
			if (NdotL <= 0.0f)
				continue;
			const float3 weighted = lightIntensity * NdotL;
			unshadowedSum = unshadowedSum + weighted;

			catcherRng = pcgHash(catcherRng ^ (i * 0x9E3779B9u));
			const float3 shadowOrigin = worldPos + worldNormal * selfIntersectionEpsilon(worldPos);
			const float shadowMaxDistance = fminf(lightDistance, 1e16f);
			// forceSelfExclude=true - see traceShadowRay()'s doc comment:
			// a single flat quad can never legitimately self-shadow, so any
			// self-hit here can only be numerical/grazing-angle noise, which
			// would otherwise stop shadowFactor from ever reading EXACTLY
			// (1,1,1) in genuinely-unoccluded areas, permanently preventing
			// them from qualifying as "essentially invisible" below.
			const float3 shadowTransmittance = params.shadowsEnabled != 0
				? traceShadowRay(shadowOrigin, lightDir, shadowMaxDistance, instanceId, catcherRng, true)
				: make_float3(1.0f, 1.0f, 1.0f);
			litSum = litSum + weighted * shadowTransmittance;
		}
		const float3 shadowFactor = make_float3(
			unshadowedSum.x > 1e-6f ? fminf(fmaxf(litSum.x / unshadowedSum.x, 0.0f), 1.0f) : 1.0f,
			unshadowedSum.y > 1e-6f ? fminf(fmaxf(litSum.y / unshadowedSum.y, 0.0f), 1.0f) : 1.0f,
			unshadowedSum.z > 1e-6f ? fminf(fmaxf(litSum.z / unshadowedSum.z, 0.0f), 1.0f) : 1.0f);

		// Background radiance in the ray's CURRENT direction - mirrors
		// __miss__ms()'s own dual convention exactly (pixel-perfect skybox
		// lookup at the primary hit, MIS-weighted lookup for later bounces)
		// so an unshadowed floor hit is indistinguishable from a genuine
		// miss at this same bounce depth.
		float3 envColor;
		if (primaryRaySentinel == -1.0f)
		{
			const uint3 idx = optixGetLaunchIndex();
			const uint3 dimLaunch = optixGetLaunchDimensions();
			const float su = (static_cast<float>(idx.x) + 0.5f) / static_cast<float>(dimLaunch.x);
			const float sv = 1.0f - (static_cast<float>(idx.y) + 0.5f) / static_cast<float>(dimLaunch.y);
			envColor = sampleEnvironmentBackground(params.environment, rayDir, su, sv);
		}
		else
		{
			const float previousBsdfPdfFloor = __uint_as_float(optixGetPayload_19());
			const float envPdfAtRayFloor = (previousBsdfPdfFloor > 0.0f && params.enableEnvironmentImportanceSampling != 0)
				? envSamplerPdf(params.environment, rayDir) : 0.0f;
			const float misWeightFloor = (previousBsdfPdfFloor > 0.0f && envPdfAtRayFloor > 0.0f)
				? (previousBsdfPdfFloor / (previousBsdfPdfFloor + envPdfAtRayFloor)) : 1.0f;
			envColor = sampleEnvironmentRaw(params.environment, rayDir) * misWeightFloor;
		}

		// Blend toward the shadowed solution instead of subtracting past it:
		// with this tracer's harder shadowFactor estimator, the old
		// subtractive formula produced an opaque black slab. Here the user
		// darkness slider acts as shadow strength: 0 = invisible catcher,
		// 1 = full shadowFactor attenuation.
		const float shadowStrength = fminf(fmaxf(data->shadowCatcherDarkness, 0.0f), 1.0f);
		const float3 shadowMix = make_float3(
			1.0f + (shadowFactor.x - 1.0f) * shadowStrength,
			1.0f + (shadowFactor.y - 1.0f) * shadowStrength,
			1.0f + (shadowFactor.z - 1.0f) * shadowStrength);
		const float3 shadowedRadiance = envColor * shadowMix;
		const float shadowVisibility = 1.0f - fminf(fmaxf(
			0.2126f * shadowFactor.x + 0.7152f * shadowFactor.y + 0.0722f * shadowFactor.z,
			0.0f), 1.0f);
		const float shadowOpacity = fminf(fmaxf(shadowVisibility * shadowStrength, 0.0f), 1.0f);

		setPayload(shadowedRadiance);
		const bool essentiallyInvisible = shadowOpacity <= 1e-3f;
		if (essentiallyInvisible)
		{
			// hitFlag=0: identical to a genuine miss - __raygen__rg()'s
			// accumulatedHits (this pixel's alpha) does NOT increment, so
			// raster's own background shows through untouched here.
			optixSetPayload_3(0u);
			optixSetPayload_4(0u); optixSetPayload_5(0u); optixSetPayload_6(0u);
			optixSetPayload_14(0u); optixSetPayload_15(0u); optixSetPayload_16(0u);
		}
		else
		{
			// hitFlag=2: a terminal shadow-catcher hit. Unlike an ordinary
			// opaque primary hit, raygen uses payload 17 as a FRACTIONAL
			// coverage/alpha for this special case, so weak contact-darkening
			// stays subtly blended with the raster background instead of
			// turning the whole catcher quad into an opaque slab.
			optixSetPayload_3(2u);
			optixSetPayload_4(__float_as_uint(worldNormal.x));
			optixSetPayload_5(__float_as_uint(worldNormal.y));
			optixSetPayload_6(__float_as_uint(worldNormal.z));
			optixSetPayload_7(__float_as_uint(optixGetRayTmax()));
			optixSetPayload_14(__float_as_uint(baseColor.x));
			optixSetPayload_15(__float_as_uint(baseColor.y));
			optixSetPayload_16(__float_as_uint(baseColor.z));
			optixSetPayload_17(__float_as_uint(shadowOpacity));
		}
		optixSetPayload_19(0u);
		return;
	}

	// Beer-Lambert absorption over the real distance traveled since the
	// previous hit - only on a back-face hit of a material that actually
	// has a volume (a thin-walled surface has no interior to absorb
	// through). optixGetRayTmax() is exactly that distance: curOrigin in
	// __raygen__rg() is always the previous hit's own position (offset by a
	// negligible epsilon), so the length of THIS ray is the distance
	// travelled through the medium since entry - ported from
	// CpuPathTracer::tracePixel()'s identical glm::length(hit.position -
	// prevHitPos) computation. Applied to this hit's own radiance/
	// throughputWeight contribution at the very end of this function (see
	// their assignment there), matching CPU's single throughput*=atten
	// multiply affecting everything computed from this point onward in the
	// same tracePixel() iteration.
	//
	// KHR_materials_volume_scatter genuine free-flight random walk (mirrors
	// NVIDIA's vk_gltf_renderer exactly - see this feature's plan doc): when
	// the medium ALSO carries volume-scatter data, replace the deterministic
	// full-segment multiply below with a stochastic per-segment scatter-or-
	// absorb test, using this same optixGetRayTmax() segment distance as the
	// free-flight bound. A scatter event never reaches this hit's surface at
	// all - it writes its OWN complete payload set (reusing the same
	// explicit-origin continuation mechanism the old BSSRDF redirect used,
	// hitFlag==4) and returns immediately, skipping every bit of this
	// function's surface shading below (NEE loop, BSDF lobes, emissive).
	float3 volumeAttenuation = make_float3(1.0f, 1.0f, 1.0f);
	if (hitBackface && hasVolume != 0)
	{
		if (hasVolumeScattering != 0)
		{
			float3 extinction, scatterCoeff;
			computeVolumeScatterCoefficients(attenuationColor, attenuationDistance, multiScatterColor, extinction, scatterCoeff);
			const float maxScatter = fmaxf(scatterCoeff.x, fmaxf(scatterCoeff.y, scatterCoeff.z));
			if (maxScatter > kVolumeMinScatter)
			{
				const float maxExtinction = fmaxf(extinction.x, fmaxf(extinction.y, extinction.z));
				const float segmentDistance = optixGetRayTmax();
				unsigned int rngState = optixGetPayload_17();
				rngState = pcgHash(rngState);
				const float scatterDist = -logf(fmaxf(hashToUnitFloat(rngState), kVolumeRandFloor)) / maxExtinction;
				if (scatterDist < segmentDistance)
				{
					const float3 scatterThroughput = make_float3(1.0f, 1.0f, 1.0f) - (extinction - scatterCoeff) * (1.0f / maxExtinction);
					const float3 scatterPos = rayOrigin + rayDir * scatterDist;
					const float3 wi = rayDir;

					rngState = pcgHash(rngState);
					const float u1 = hashToUnitFloat(rngState);
					rngState = pcgHash(rngState);
					const float u2 = hashToUnitFloat(rngState);
					const float3 newDir = sampleHenyeyGreenstein(wi, kVolumeScatterAnisotropy, u1, u2);
					const float phasePdf = henyeyGreensteinPdf(dot3(wi, newDir), kVolumeScatterAnisotropy);

					rngState = pcgHash(rngState);
					const float3 neeRadiance = sampleVolumeScatterNEE(scatterPos, wi, scatterThroughput, instanceId, rngState);

					setPayload(neeRadiance);
					optixSetPayload_3(4u); // explicit-origin continuation - see traceBouncePath()'s and __raygen__rg()'s hitFlag==4 handling
					optixSetPayload_4(__float_as_uint(scatterPos.x));
					optixSetPayload_5(__float_as_uint(scatterPos.y));
					optixSetPayload_6(__float_as_uint(scatterPos.z));
					optixSetPayload_7(__float_as_uint(segmentDistance)); // unread for hitFlag==4 (see traceBouncePath()'s doc comment), kept for consistency
					optixSetPayload_8(__float_as_uint(newDir.x));
					optixSetPayload_9(__float_as_uint(newDir.y));
					optixSetPayload_10(__float_as_uint(newDir.z));
					optixSetPayload_11(__float_as_uint(scatterThroughput.x));
					optixSetPayload_12(__float_as_uint(scatterThroughput.y));
					optixSetPayload_13(__float_as_uint(scatterThroughput.z));
					// Guide albedo/normal for OIDN are only ever consumed at
					// the primary hit (bounce==0/transmissionDepth==0 in
					// __raygen__rg()) - a scatter continuation can only reach
					// that in the degenerate case of the camera starting
					// inside a volume-scatter medium, an accepted v1 gap (see
					// this feature's plan doc). baseColor is a reasonable
					// placeholder for the ordinary case.
					optixSetPayload_14(__float_as_uint(baseColor.x));
					optixSetPayload_15(__float_as_uint(baseColor.y));
					optixSetPayload_16(__float_as_uint(baseColor.z));
					optixSetPayload_17(__float_as_uint(kVolumeScatterEscapeSentinel));
					optixSetPayload_19(__float_as_uint(phasePdf));
					return;
				}
				else
				{
					volumeAttenuation = make_float3(
						expf(segmentDistance * (maxExtinction - extinction.x)),
						expf(segmentDistance * (maxExtinction - extinction.y)),
						expf(segmentDistance * (maxExtinction - extinction.z)));
				}
			}
			else
			{
				volumeAttenuation = calculateVolumeAttenuation(attenuationColor, attenuationDistance, optixGetRayTmax());
			}
		}
		else
		{
			volumeAttenuation = calculateVolumeAttenuation(attenuationColor, attenuationDistance, optixGetRayTmax());
		}
	}

	// kDebugVisualizeShadowTransmittance - x < 0.0f means "nothing captured
	// yet" (no real light contributed), same sentinel convention as
	// debugClearcoatColor's GPU counterpart above.
	float3 debugShadowTransmittanceColor = make_float3(-1.0f, -1.0f, -1.0f);

	float3 radiance = emissive;
	if (NdotV > 0.0f)
	{
		for (unsigned int i = 0; i < params.lightCount; ++i)
		{
			float3 lightDir, lightIntensity;
			float lightDistance;
			evaluatePunctualLight(params.lights[i], worldPos, lightDir, lightIntensity, lightDistance);

			const float rawNdotL = dot3(worldNormal, lightDir);
			if (rawNdotL <= 0.0f)
			{
				// KHR_materials_diffuse_transmission - a light on the BACK
				// side of the surface (relative to worldNormal) still
				// contributes if the material lets light diffusely scatter
				// through from behind (e.g. a leaf/curtain lit from the far
				// side) - a plain Lambertian term using |NdotL| and tinted by
				// diffuseTransmissionColor, evaluated separately from the
				// front-hemisphere response since this is a distinct light-
				// transport path, not a variant of the same BRDF lobe. The
				// shadow ray originates from the back side (-worldNormal)
				// since that's the side actually facing this light. Ported
				// from CpuPathTracer::tracePixel()'s identical NEE handling,
				// including KHR_materials_volume's thicknessFactor-based tint
				// (this path has no traced entry-to-exit distance of its own
				// to measure, unlike KHR_materials_transmission's real
				// Beer-Lambert absorption above - thicknessFactor is the
				// authored approximation instead, same as CPU).
				if (diffuseTransmissionFactor > 0.0f)
				{
					const float3 backShadowOrigin = worldPos - worldNormal * selfIntersectionEpsilon(worldPos);
					const float backShadowMaxDistance = fminf(lightDistance, 1e16f);
					const unsigned int backShadowRngSeed = pcgHash(optixGetPayload_17() ^ (i * 0x9E3779B9u) ^ 0xB5297A4Du);
					const float3 backShadowTransmittance = params.shadowsEnabled != 0
						? traceShadowRay(backShadowOrigin, lightDir, backShadowMaxDistance, instanceId, backShadowRngSeed)
						: make_float3(1.0f, 1.0f, 1.0f);
					if (kDebugVisualizeShadowTransmittance && primaryRaySentinel == -1.0f && params.lights[i].range >= 0.0f)
					{
						if (debugShadowTransmittanceColor.x < 0.0f)
							debugShadowTransmittanceColor = make_float3(0.0f, 0.0f, 0.0f);
						debugShadowTransmittanceColor = debugShadowTransmittanceColor + backShadowTransmittance;
					}
					if (backShadowTransmittance.x > 0.0f || backShadowTransmittance.y > 0.0f || backShadowTransmittance.z > 0.0f)
					{
						float3 diffuseBTDF = diffuseTransmissionColor * (1.0f / kPi) * fabsf(rawNdotL) * diffuseTransmissionFactor;
						if (hasVolume != 0)
							diffuseBTDF = diffuseBTDF * calculateVolumeAttenuation(attenuationColor, attenuationDistance, thicknessFactor);
						radiance = radiance + diffuseBTDF * (lightIntensity * backShadowTransmittance);
					}
				}
				continue;
			}
			const float NdotL = rawNdotL;

			// Offset along the (faceforward) shading normal, same as
			// CpuPathTracer's NEE shadow rays, to dodge self-intersection
			// with the surface this ray originates from.
			const float3 shadowOrigin = worldPos + worldNormal * selfIntersectionEpsilon(worldPos);
			const float shadowMaxDistance = fminf(lightDistance, 1e16f);
			const unsigned int shadowRngSeed = pcgHash(optixGetPayload_17() ^ (i * 0x9E3779B9u));
			float3 shadowTransmittance = params.shadowsEnabled != 0
				? traceShadowRay(shadowOrigin, lightDir, shadowMaxDistance, instanceId, shadowRngSeed)
				: make_float3(1.0f, 1.0f, 1.0f);
			if (kDebugVisualizeShadowTransmittance && primaryRaySentinel == -1.0f && params.lights[i].range >= 0.0f)
			{
				if (debugShadowTransmittanceColor.x < 0.0f)
					debugShadowTransmittanceColor = make_float3(0.0f, 0.0f, 0.0f);
				debugShadowTransmittanceColor = debugShadowTransmittanceColor + shadowTransmittance;
			}
			if (params.lights[i].range < 0.0f)
			{
				// Raster clamps the app/default light's shadow factor to 0.85,
				// leaving 15% direct light in fully shadowed regions. Keep real
				// glTF punctual lights on the physical traceShadowRay result.
				shadowTransmittance = make_float3(
					fmaxf(shadowTransmittance.x, 0.15f),
					fmaxf(shadowTransmittance.y, 0.15f),
					fmaxf(shadowTransmittance.z, 0.15f));
			}
			if (shadowTransmittance.x <= 0.0f && shadowTransmittance.y <= 0.0f && shadowTransmittance.z <= 0.0f)
				continue;
			const float3 shadowedLightIntensity = lightIntensity * shadowTransmittance;

			const float3 H = normalizeF3(V + lightDir);
			const float NdotH = fmaxf(dot3(worldNormal, H), 0.0f);
			const float VdotH = fminf(fmaxf(dot3(H, V), 0.0f), 1.0f);

			// directF0, not F0 - the direct-lighting-specific extra
			// specularFactor scale, matching CpuPathTracer::
			// evaluateDirectBRDF()'s own fresnelSchlick(VdotH, surf.directF0,
			// surf.F90) exactly.
			const float3 F = fresnelSchlick(VdotH, directF0, F90);

			// KHR_materials_anisotropy - ported from CpuPathTracer::
			// evaluateDirectBRDF()'s hasAniso branch. specularNoF is kept
			// Fresnel-FREE (matching CPU) since KHR_materials_iridescence's
			// direct-lighting branch below needs the bare D*V/G term to
			// rebuild its own dielectric/metal Fresnel split from scratch.
			float3 specularNoF;
			if (hasAniso)
			{
				const float D_aniso = distributionGGXAnisotropic(NdotH, dot3(anisoT, H), dot3(anisoB, H), anisoAlphaT, anisoAlphaB);
				const float V_aniso = visibilityGGXAnisotropic(NdotL, NdotV,
					dot3(anisoB, V), dot3(anisoT, V), dot3(anisoT, lightDir), dot3(anisoB, lightDir), anisoAlphaT, anisoAlphaB);
				const float dv = D_aniso * V_aniso;
				specularNoF = make_float3(dv, dv, dv);
			}
			else
			{
				const float D = distributionGGX(NdotH, roughness);
				const float G = geometrySmith(NdotV, NdotL, roughness);
				const float dg = (D * G) / fmaxf(4.0f * NdotV * NdotL, 0.001f);
				specularNoF = make_float3(dg, dg, dg);
			}
			const float3 specular = specularNoF * F;

			const float3 kD = (make_float3(1.0f, 1.0f, 1.0f) - F) * (1.0f - metalness);
			// KHR_materials_transmission - at transmission=1 the diffuse
			// response vanishes entirely (replaced by the refracted
			// continuation ray in the lobe-selection section below), a plain
			// deterministic scale-down (no variance added). KHR_materials_
			// diffuse_transmission - part of the front-facing diffuse albedo
			// is redirected to transmit through to the back instead (see the
			// back-hemisphere NEE term above and the diffuse-lobe front/back
			// stochastic split below) - both ported from CpuPathTracer::
			// evaluateDirectBRDF() exactly.
			const float3 diffuse = kD * baseColor * (1.0f / kPi) * (1.0f - transmission) * (1.0f - diffuseTransmissionFactor);

			float3 baseDirect;
			// KHR_materials_pbrSpecularGlossiness's direct-lighting formula -
			// MIXES (not adds) diffuse/specular by the dielectric Fresnel
			// term, matching CpuPathTracer::evaluateDirectBRDF()'s useSpecGloss
			// early return exactly (l_diffuse there is baseColor/pi WITHOUT
			// the kD=(1-F) weighting `diffuse` above already has - mix()
			// itself supplies that weighting, so re-applying it would double
			// it). Checked BEFORE iridescence below and returns early,
			// matching CPU's own early-return ordering - spec-gloss +
			// iridescence combined has no iridescence effect on direct
			// lighting in either engine, a pre-existing CPU limitation
			// preserved here for parity, not a new gap introduced by this port.
			if (data->useSpecGloss != 0)
			{
				const float3 l_diffuse = baseColor * (1.0f / kPi);
				baseDirect = lerp3(l_diffuse, specular, F) * (shadowedLightIntensity * NdotL);
			}
			// KHR_materials_iridescence - ported from CpuPathTracer::
			// evaluateDirectBRDF()'s iridescence branch, which entirely
			// replaces the diffuse+specular combination above with its own
			// dielectric/metal reconstruction rather than adding a term on
			// top.
			else if (iridescenceFactor > 0.001f && iridescenceThickness > 0.0f)
			{
				const float3 l_diffuse = diffuse * NdotL;
				// KHR_materials_transmission - scaled down here too, not just
				// diffuse above, matching CpuPathTracer::evaluateDirectBRDF()'s
				// identical iridescence-branch treatment.
				const float3 l_specular = specularNoF * NdotL * (1.0f - transmission);

				const float3 dielectricFresnel = fresnelSchlick(VdotH, dielectricDirectF0,
					make_float3(texturedSpecularFactor, texturedSpecularFactor, texturedSpecularFactor));
				const float3 metalFresnel = fresnelSchlick(VdotH, baseColor, make_float3(1.0f, 1.0f, 1.0f));
				float3 dielectricBrdf = lerp3(l_diffuse, l_specular, dielectricFresnel);
				float3 metalBrdf = metalFresnel * l_specular;

				const float3 iridescenceFresnelDielectric = evalIridescence(1.0f, iridescenceIor, NdotV, iridescenceThickness, dielectricF0, make_float3(1.0f, 1.0f, 1.0f));
				const float3 iridescenceFresnelMetallic = evalIridescence(1.0f, iridescenceIor, NdotV, iridescenceThickness, baseColor, make_float3(1.0f, 1.0f, 1.0f));
				metalBrdf = lerp3(metalBrdf, l_specular * iridescenceFresnelMetallic, iridescenceFactor);
				dielectricBrdf = lerp3(dielectricBrdf, rgbMix(l_diffuse, l_specular, iridescenceFresnelDielectric), iridescenceFactor);

				baseDirect = lerp3(dielectricBrdf, metalBrdf, metalness) * shadowedLightIntensity;
			}
			else
			{
				// KHR_materials_transmission - both DS's and RayTrophi's
				// reference implementations skip NEE specular against smooth/
				// near-delta transmissive materials entirely; this backend
				// (like CPU) doesn't yet support rough/glossy transmission's
				// own NEE term, so the specular response is scaled down the
				// same way diffuse already is, matching CpuPathTracer::
				// evaluateDirectBRDF()'s identical final-return formula.
				baseDirect = (diffuse + specular * (1.0f - transmission)) * shadowedLightIntensity * NdotL;
			}

			// KHR_materials_sheen's base-layer energy-compensation dampening -
			// ported from CpuPathTracer::tracePixel()'s "albedoSheenScaling",
			// itself from main_scene.frag's direct-light sheen handling. Not
			// optional polish - per the Dassault Enterprise PBR spec this is
			// the actual mechanism keeping base+sheen combined energy-
			// conserving (otherwise the sheen fuzz reads as pure extra
			// brightness rather than partially replacing the base response).
			if (sheenColor.x > 0.0f || sheenColor.y > 0.0f || sheenColor.z > 0.0f)
			{
				const float sheenStrength = fmaxf(fmaxf(sheenColor.x, sheenColor.y), sheenColor.z);
				const float NdotVSheen = fminf(fmaxf(dot3(worldNormal, V), 0.0f), 1.0f);
				const float albedoSheenScaling = fminf(
					1.0f - sheenStrength * sampleSheenAlbedoLUT(params.sheenAlbedoLUT, params.sheenAlbedoLUTSize, NdotVSheen, sheenRoughness),
					1.0f - sheenStrength * sampleSheenAlbedoLUT(params.sheenAlbedoLUT, params.sheenAlbedoLUTSize, fminf(fmaxf(NdotL, 0.0f), 1.0f), sheenRoughness));
				baseDirect = baseDirect * albedoSheenScaling;
			}

			// KHR_materials_clearcoat - blend base vs. coat exactly like
			// composeLayeredPBR()'s mix(baseLayer, clearcoatLayer, clearcoat*
			// clearcoatFresnel); clearcoatBlend is 0 for non-clearcoat
			// materials, reducing to baseDirect unchanged.
			if (clearcoat > 0.0f)
			{
				const float3 coatDirect = evaluateClearcoatDirect(Ncoat, V, lightDir, clearcoat, clearcoatRoughness) * shadowedLightIntensity;
				// Component-wise vec3 mix, matching CpuPathTracer::tracePixel()'s
				// glm::mix(baseDirect, coatDirect, clearcoatBlend) exactly - a
				// scalar (x+y+z)/3 average here would silently diverge if
				// clearcoatBlend ever becomes chromatic (it's currently always
				// achromatic dielectric Fresnel, so this was visually near-
				// identical, but not exact parity).
				radiance = radiance + lerp3(baseDirect, coatDirect, clearcoatBlend);
			}
			else
			{
				radiance = radiance + baseDirect;
			}

			// KHR_materials_sheen - additive, not blended.
			if (sheenColor.x > 0.0f || sheenColor.y > 0.0f || sheenColor.z > 0.0f)
				radiance = radiance + calculateSheen(worldNormal, V, lightDir, sheenColor, sheenRoughness) * shadowedLightIntensity;
		}
	}

	// kDebugVisualizeShadowTransmittance - false-colors the primary hit only,
	// mirroring CpuPathTracer::tracePixel()'s identical substitution-for-the-
	// whole-function-return exactly (see that function's final return
	// block). Written directly into the radiance payload and hitFlag forced
	// to 0 (the same value __miss__ms() uses for "escaped, no continuation")
	// so the raygen loop's `if (hitFlag == 0u) break;` stops this sample
	// immediately after - no later bounce ever dilutes this pixel's debug
	// color, same guarantee kDebugVisualizeClearcoat's early return gives.
	if (kDebugVisualizeShadowTransmittance && primaryRaySentinel == -1.0f && debugShadowTransmittanceColor.x >= 0.0f)
	{
		optixSetPayload_0(__float_as_uint(debugShadowTransmittanceColor.x));
		optixSetPayload_1(__float_as_uint(debugShadowTransmittanceColor.y));
		optixSetPayload_2(__float_as_uint(debugShadowTransmittanceColor.z));
		optixSetPayload_3(0u);
		return;
	}

	// Environment NEE - direct importance sampling of the HDRI with balance-
	// heuristic MIS against the BSDF-sampled environment escapes handled in
	// __miss__ms(). This completes the host-side CDF upload/device sampler
	// Claude added: without this closest-hit term the uploaded tables were
	// dead data, and enabling the setting could only affect future code.
	if (params.enableEnvironmentImportanceSampling != 0
		&& params.environment.envTotalWeight > 0.0f
		&& params.environment.envFlatCdf != nullptr
		&& params.environment.envTexelPdf != nullptr
		&& transmission <= 0.001f
		&& NdotV > 0.0f)
	{
		unsigned int envRng = optixGetPayload_17() ^ 0x6D2B79F5u;
		envRng = pcgHash(envRng);
		const float eu0 = hashToUnitFloat(envRng);
		envRng = pcgHash(envRng);
		const float eu1 = hashToUnitFloat(envRng);
		envRng = pcgHash(envRng);
		const float eu2 = hashToUnitFloat(envRng);

		float3 envDir;
		float envPdf;
		envSamplerSample(params.environment, eu0, eu1, eu2, envDir, envPdf);

		const float NdotLEnv = dot3(worldNormal, envDir);
		if (envPdf > 0.0f && NdotLEnv > 0.0f)
		{
			const float3 envShadowOrigin = worldPos + worldNormal * selfIntersectionEpsilon(worldPos);
			const unsigned int envShadowRngSeed = pcgHash(optixGetPayload_17() ^ 0xBB67AE85u);
			const float3 envShadowTransmittance = params.shadowsEnabled != 0
				? traceShadowRay(envShadowOrigin, envDir, 1e16f, instanceId, envShadowRngSeed)
				: make_float3(1.0f, 1.0f, 1.0f);
			if (envShadowTransmittance.x > 0.0f || envShadowTransmittance.y > 0.0f || envShadowTransmittance.z > 0.0f)
			{
				float specProbEnv, coatProbEnv;
				computeLobeProbabilities(F0, metalness, roughness, clearcoatBlend, clearcoat, clearcoatRoughness, anisotropyStrength, specProbEnv, coatProbEnv);
				const float bsdfPdf = evaluateBsdfPdf(worldNormal, Ncoat, V, envDir,
					roughness, clearcoatRoughness, hasAniso, anisoT, anisoB, anisoAlphaT, anisoAlphaB, specProbEnv, coatProbEnv);
				const float misWeight = envPdf / (envPdf + bsdfPdf);

				const float3 envRadiance = sampleEnvironmentRaw(params.environment, envDir) * envShadowTransmittance;
				float3 envDirect = evaluateDirectBRDF(worldNormal, V, envDir,
					baseColor, metalness, directF0, F90, roughness,
					hasAniso, anisoT, anisoB, anisoAlphaT, anisoAlphaB,
					data->useSpecGloss, transmission, diffuseTransmissionFactor,
					iridescenceFactor, iridescenceIor, iridescenceThickness,
					dielectricF0, dielectricDirectF0, texturedSpecularFactor) * envRadiance;

				if (sheenColor.x > 0.0f || sheenColor.y > 0.0f || sheenColor.z > 0.0f)
				{
					const float sheenStrength = fmaxf(fmaxf(sheenColor.x, sheenColor.y), sheenColor.z);
					const float NdotVSheen = fminf(fmaxf(dot3(worldNormal, V), 0.0f), 1.0f);
					const float albedoSheenScaling =
						1.0f - sheenStrength * sampleSheenIblEnergy(NdotVSheen, sheenRoughness);
					envDirect = envDirect * albedoSheenScaling;
				}

				const float3 weightedEnv = make_float3(ao * misWeight / envPdf, ao * misWeight / envPdf, ao * misWeight / envPdf);
				if (clearcoat > 0.0f)
				{
					const float3 coatDirect = evaluateClearcoatDirect(Ncoat, V, envDir, clearcoat, clearcoatRoughness) * envRadiance;
					// Component-wise vec3 mix, matching CpuPathTracer::tracePixel()'s
					// glm::mix(envDirect, coatDirect, clearcoatBlend) exactly - see
					// the direct-light NEE site above for why the scalar average
					// this replaces wasn't exact parity.
					radiance = radiance + weightedEnv * lerp3(envDirect, coatDirect, clearcoatBlend);
				}
				else
				{
					radiance = radiance + weightedEnv * envDirect;
				}
			}
		}
	}

	// KHR_materials_sheen environment/IBL contribution. Raster evaluates this
	// as a split-sum lookup into sheenPrefilterMap multiplied by
	// min(charlieLUT.b, sheenELUT.r). Reuse the existing prefiltered
	// environment chain here rather than a raw stochastic cone: it is not a
	// perfect Charlie prefilter, but it tracks raster's stable prefiltered IBL
	// much more closely than a bright raw-environment overlay.
	float sheenIndirectDampening = 1.0f;
	if (NdotV > 0.0f && (sheenColor.x > 0.0f || sheenColor.y > 0.0f || sheenColor.z > 0.0f))
	{
		const float sheenStrengthForIBL = fmaxf(fmaxf(sheenColor.x, sheenColor.y), sheenColor.z);
		const float3 R = reflectF3(V * -1.0f, worldNormal);

		const float sheenRoughFinal = fminf(fmaxf(sheenRoughness, 0.0001f), 1.0f);
		const float3 envSheen = sampleEnvironmentSheen(params.environment, R, sheenRoughFinal);

		const float NdotVSheenIbl = fminf(fmaxf(dot3(worldNormal, V), 0.0f), 1.0f);
		const float E_sheen = sampleSheenIblEnergy(NdotVSheenIbl, sheenRoughFinal);
		radiance = radiance + sheenColor * envSheen * (ao * E_sheen);

		// Dampens the base layer's OWN indirect (bounce) throughput, applied
		// to whichever lobe the stochastic BSDF sample below picks -
		// mirrors CpuPathTracer's throughput *= sheenIndirectDampening,
		// the same base+sheen energy-conservation mechanism the direct-light
		// albedoSheenScaling term above provides for punctual lights.
		sheenIndirectDampening = 1.0f - sheenStrengthForIBL * E_sheen;
	}

	// Stochastic BSDF sample for the raygen loop's NEXT bounce - replaces
	// the old single deterministic mirror reflection. Picks ONE lobe per
	// hit (cosine-weighted diffuse, or GGX-VNDF-importance-sampled
	// specular), chosen by a Fresnel-based probability so neither lobe
	// dominates the RNG draw disproportionately to its actual visual
	// contribution. See this file's top-of-file doc comment for why this
	// (an unbiased Monte Carlo estimator converging over many samples) is
	// the actual fix for both the "unlit-by-direct-light face renders
	// black" gap (no diffuse lobe ever continued before) and the "reflection
	// speckle noise" symptom (an exact, unblurred, unimportance-sampled
	// mirror ray for ANY roughness, previously).
	float3 nextDirection = make_float3(0.0f, 0.0f, 0.0f);
	float3 throughputWeight = make_float3(0.0f, 0.0f, 0.0f);
	float outEscapeRoughness = -2.0f; // diffuse-lobe sentinel by default
	float outBsdfPdf = 0.0f;
	bool hasContinuation = false;
	bool hasPrecomputedBsdfPdf = false;

	// KHR_materials_transmission - bypasses the general multi-lobe
	// stochastic BSDF sampling below ENTIRELY for transmissive materials,
	// mirroring CpuPathTracer::tracePixel()'s identical bypass: a single
	// Fresnel-weighted reflect-or-refract choice, computed exactly once,
	// rather than stacking a second independent rare-branch probability on
	// top of the lobe-selection's own internal specProb weighting (which
	// would multiply the two together into extreme-variance fireflies -
	// see CPU's own doc comment for the full rationale). Rough/glossy
	// transmission and interaction with clearcoat/sheen/anisotropy are out
	// of scope here too, matching CPU - transmissive materials are treated
	// as smooth dielectrics (with GGX-VNDF roughness support per Walter et
	// al. 2007, same as CPU).
	bool isTransmissionBounce = false;
	if (transmission > 0.001f)
	{
		isTransmissionBounce = true;
		unsigned int rngState = optixGetPayload_17();

		const float NdotVTransmission = fminf(fmaxf(NdotV, 0.0f), 1.0f);
		float3 Ht, Bt;
		buildOrthonormalBasis(worldNormal, Ht, Bt);
		const float transmissionAlpha = roughness * roughness;
		const float NdotV0 = fmaxf(NdotVTransmission, 1e-4f);
		const float3 Ve = make_float3(dot3(V, Ht), dot3(V, Bt), NdotV0);

		rngState = pcgHash(rngState);
		const float u1t = hashToUnitFloat(rngState);
		rngState = pcgHash(rngState);
		const float u2t = hashToUnitFloat(rngState);
		const float3 hLocal = sampleGGXVNDF(Ve, transmissionAlpha, transmissionAlpha, u1t, u2t);
		const float3 Hm = normalizeF3(Ht * hLocal.x + Bt * hLocal.y + worldNormal * hLocal.z);
		const float VdotHm = fminf(fmaxf(dot3(V, Hm), 0.0f), 1.0f);

		// KHR_materials_iridescence applies to the Fresnel reflectance at
		// ANY dielectric interface, not just an opaque one - a transmissive
		// material (soap bubble, iridescent glass) still shows the same
		// thin-film color shift on its reflected portion. Evaluated at the
		// microfacet normal (VdotHm), matching Walter et al.'s treatment.
		const float3 transmissionFresnel = applyIridescenceToFresnel(
			fresnelSchlick(VdotHm, dielectricF0, make_float3(1.0f, 1.0f, 1.0f)), VdotHm, dielectricF0,
			iridescenceFactor, iridescenceIor, iridescenceThickness);
		const float reflectProb = fminf(fmaxf((transmissionFresnel.x + transmissionFresnel.y + transmissionFresnel.z) / 3.0f, 0.05f), 0.95f);

		rngState = pcgHash(rngState);
		const float reflectXi = hashToUnitFloat(rngState);

		float3 bounceDir = make_float3(0.0f, 0.0f, 0.0f);
		float3 bounceThroughput = make_float3(0.0f, 0.0f, 0.0f);
		bool valid = false;

		if (reflectXi < reflectProb)
		{
			// glm::reflect(ray.direction, Hm) on CPU - the true incoming ray
			// direction, NOT -V (which, for an orthographic PRIMARY hit, is
			// the raster-matching "fake camera" vector, not the true parallel
			// ray direction) - matches CpuPathTracer::tracePixel()'s
			// transmission-branch reflect exactly.
			bounceDir = reflectF3(rayDir, Hm);
			const float NdotL = dot3(worldNormal, bounceDir);
			if (NdotL > 0.0f)
			{
				const float G1v = smithG1GGX(NdotV0, transmissionAlpha);
				const float G2 = smithG2HeightCorrelatedGGX(NdotV0, NdotL, transmissionAlpha);
				bounceThroughput = transmissionFresnel * (G2 / fmaxf(G1v, 1e-6f)) * (1.0f / reflectProb);
				valid = true;
			}
			// NdotL<=0: a VNDF sample that reflects below the macro surface -
			// dead-end path, same as CPU's identical handling.
		}
		else if (hasVolume == 0)
		{
			// KHR_materials_transmission WITHOUT KHR_materials_volume means
			// the surface is implicitly thin-walled (glTF's "hole"/idealized
			// infinitely-thin-film intent) - the transmitted ray passes
			// straight through completely undeviated, tinted by baseColor
			// (matches CpuPathTracer's identical thin-walled branch exactly,
			// including its baseColor tint rationale).
			bounceDir = rayDir;
			bounceThroughput = baseColor * (make_float3(1.0f, 1.0f, 1.0f) - transmissionFresnel) * (1.0f / (1.0f - reflectProb));
			valid = true;
		}
		else
		{
			// KHR_materials_dispersion - per-channel IOR spread on refraction,
			// via the same hero-wavelength stochastic-single-channel trick
			// CpuPathTracer uses (rather than tracing 3 separate rays per
			// sample): pick ONE channel with equal 1/3 probability, refract
			// using only that channel's IOR, mask+triple the resulting
			// throughput to that channel. dispersion==0 (the common case)
			// makes this a no-op (dispersedIor==data->ior, channelMask==(1,1,1)).
			float dispersedIor = data->ior;
			float3 channelMask = make_float3(1.0f, 1.0f, 1.0f);
			if (dispersion > 0.0f)
			{
				const float halfSpread = (data->ior - 1.0f) * 0.025f * dispersion;
				rngState = pcgHash(rngState);
				const float channelXi = hashToUnitFloat(rngState);
				if (channelXi < 1.0f / 3.0f)
				{
					dispersedIor = data->ior - halfSpread;
					channelMask = make_float3(3.0f, 0.0f, 0.0f);
				}
				else if (channelXi < 2.0f / 3.0f)
				{
					channelMask = make_float3(0.0f, 3.0f, 0.0f);
				}
				else
				{
					dispersedIor = data->ior + halfSpread;
					channelMask = make_float3(0.0f, 0.0f, 3.0f);
				}
			}

			const float eta = hitBackface ? dispersedIor : (1.0f / dispersedIor);
			float3 refractDir = refractF3(rayDir, Hm, eta);
			if (refractDir.x == 0.0f && refractDir.y == 0.0f && refractDir.z == 0.0f)
			{
				// Total internal reflection - a genuine mirror bounce (100%
				// reflectance, geometrically forced by Snell's law), NOT a
				// partial/tinted transmission event - deliberately NOT tinted
				// by baseColor and NOT channel-masked (TIR is dispersion-
				// neutral), matching CpuPathTracer's identical TIR handling.
				refractDir = reflectF3(rayDir, Hm);
				const float NdotL = dot3(worldNormal, refractDir);
				if (NdotL > 0.0f)
				{
					const float G1v = smithG1GGX(NdotV0, transmissionAlpha);
					const float G2 = smithG2HeightCorrelatedGGX(NdotV0, NdotL, transmissionAlpha);
					bounceDir = refractDir;
					bounceThroughput = make_float3(G2 / fmaxf(G1v, 1e-6f), G2 / fmaxf(G1v, 1e-6f), G2 / fmaxf(G1v, 1e-6f));
					valid = true;
				}
			}
			else
			{
				bounceDir = refractDir;

				// Pragmatic multi-scatter approximation for high-roughness
				// "frosted diffuser" materials - blends the transmitted
				// direction toward a true Lambertian cosine-weighted
				// hemisphere (on the far side, -worldNormal) with probability
				// sqrt(roughness), matching CpuPathTracer's identical stand-in
				// for real subsurface scattering exactly (same sqrt(roughness)
				// mapping rationale).
				const float diffuseBlendProb = sqrtf(fminf(fmaxf(roughness, 0.0f), 1.0f));
				rngState = pcgHash(rngState);
				if (diffuseBlendProb > 0.0f && hashToUnitFloat(rngState) < diffuseBlendProb)
				{
					float3 Td, Bd;
					buildOrthonormalBasis(worldNormal * -1.0f, Td, Bd);
					rngState = pcgHash(rngState);
					const float du1 = hashToUnitFloat(rngState);
					rngState = pcgHash(rngState);
					const float du2 = hashToUnitFloat(rngState);
					const float3 diffuseLocal = cosineSampleHemisphere(du1, du2);
					bounceDir = normalizeF3(Td * diffuseLocal.x + Bd * diffuseLocal.y + (worldNormal * -1.0f) * diffuseLocal.z);
				}

				// Smith masking-shadowing ratio for the transmitted direction -
				// NdotL is naturally negative here (L is on the opposite side
				// of N from V), so the visibility term uses |NdotL|, matching
				// CpuPathTracer's identical treatment.
				const float NdotL = fabsf(dot3(worldNormal, bounceDir));
				const float G1v = smithG1GGX(NdotV0, transmissionAlpha);
				const float G2 = smithG2HeightCorrelatedGGX(NdotV0, NdotL, transmissionAlpha);
				bounceThroughput = channelMask * baseColor * (make_float3(1.0f, 1.0f, 1.0f) - transmissionFresnel) * (G2 / fmaxf(G1v, 1e-6f)) * (1.0f / (1.0f - reflectProb));
				valid = true;
			}
		}

		if (valid)
		{
			nextDirection = bounceDir;
			throughputWeight = bounceThroughput;
			// Transmission-branch sentinel - raw/sharp env map on a miss,
			// matching CpuPathTracer's lastBsdfSamplePdf=0 -> sampleEnvironmentMiss()
			// treatment for this same "deterministic Fresnel pick, not a
			// BSDF-lobe-mixture sample" case (see __miss__ms()).
			outEscapeRoughness = -3.0f;
			outBsdfPdf = 0.0f;
			hasContinuation = true;
		}
	}
	else if (NdotV > 0.0f)
	{
		unsigned int rngState = optixGetPayload_17();

		float3 T, B;
		buildOrthonormalBasis(worldNormal, T, B);

		// Ported from CpuPathTracer::computeLobeProbabilities(): average F0
		// plus a metalness boost plus a (1-roughness)^2 smoothness boost,
		// clamped away from 0/1 so neither lobe is ever starved entirely.
		// The smoothness term is load-bearing, not a tweak - F0/metalness
		// alone chronically under-samples SMOOTH DIELECTRICS (F0 ~ 0.04,
		// metalness 0), clamping them to the probability floor no matter how
		// narrow (and therefore impossible to resolve via the diffuse lobe)
		// their specular reflection is; an earlier version of this kernel
		// used average-Fresnel-at-view alone and a glossy red dielectric
		// sphere showed essentially no environment reflection at all - the
		// few specular samples that did land were then smeared away by the
		// denoiser. Since the estimator divides by whichever probability was
		// used, this only redistributes samples (variance), never changes
		// the converged mean.
		const float3 Fview = fresnelSchlick(NdotV, F0, F90);
		float specProb, coatProb;
		computeLobeProbabilities(F0, metalness, roughness, clearcoatBlend, clearcoat, clearcoatRoughness, anisotropyStrength, specProb, coatProb);

		rngState = pcgHash(rngState);
		const float lobeXi = hashToUnitFloat(rngState);
		rngState = pcgHash(rngState);
		const float u1 = hashToUnitFloat(rngState);
		rngState = pcgHash(rngState);
		const float u2 = hashToUnitFloat(rngState);

		if (lobeXi < coatProb)
		{
			// KHR_materials_clearcoat, indirect side: a third stochastically-
			// selected GGX lobe over the coat's own normal/roughness/fixed
			// dielectric F0 - ported from CpuPathTracer::sampleBSDFBounce()'s
			// coat branch. There's no analytic prefiltered-IBL lookup
			// available here for the coat (unlike the direct-light path,
			// which replicates the analytic mix() exactly), so this is the
			// standard way a layered BSDF is handled in a Monte-Carlo path
			// tracer instead.
			float3 Tc, Bc;
			buildOrthonormalBasis(Ncoat, Tc, Bc);

			const float alpha = clearcoatRoughness * clearcoatRoughness;
			const float NdotV0 = fmaxf(dot3(Ncoat, V), 1e-4f);
			const float3 Ve = make_float3(dot3(V, Tc), dot3(V, Bc), NdotV0);

			const float3 Hlocal = sampleGGXVNDF(Ve, alpha, alpha, u1, u2); // isotropic coat lobe - alphaX==alphaY
			const float3 H = normalizeF3(Tc * Hlocal.x + Bc * Hlocal.y + Ncoat * Hlocal.z);
			// reflect(-V, H), NOT reflect(rayDir, H) - matches CpuPathTracer::
			// sampleBSDFBounce()'s identical glm::reflect(-V, H) exactly. The
			// two are equal in perspective (-V == rayDir always there), but
			// diverge for an orthographic PRIMARY hit, where V has been
			// re-derived above as the raster-matching per-fragment "fake
			// camera" vector while rayDir stays the true parallel ray - using
			// rayDir here would silently discard that fix for the actual
			// reflected DIRECTION (only Fresnel/NdotV would have picked it up),
			// which is what caused the reported reflection stretch/mismatch
			// in the first place, not just a brightness difference.
			const float3 L = reflectF3(V * -1.0f, H);

			const float NdotL = dot3(Ncoat, L);
			const float NdotVc = dot3(Ncoat, V);
			if (NdotL > 0.0f && NdotVc > 0.0f)
			{
				const float VdotH = fminf(fmaxf(dot3(H, V), 0.0f), 1.0f);
				const float3 coatF0 = computeClearcoatFresnel(data->ior, Ncoat, V); // undoes clearcoatBlend's own *clearcoat factor
				const float3 F = fresnelSchlick(VdotH, coatF0, make_float3(1.0f, 1.0f, 1.0f));

				const float G1v = smithG1GGX(NdotVc, alpha);
				const float G2 = smithG2HeightCorrelatedGGX(NdotVc, NdotL, alpha);
				nextDirection = L;
				// clearcoat multiply matches CpuPathTracer::sampleBSDFBounce()'s
				// identical fix - see its doc comment. Without it, this lobe's
				// throughput represented the coat's full-strength Fresnel
				// reflectance regardless of how much of this point is actually
				// coated; a no-op at clearcoat==1 (uniform coats), but at
				// partial (0<clearcoat<1) values - KHR_materials_clearcoat's
				// clearcoatTexture case - it made the indirect/environment
				// reflection far too strong relative to the true coat amount.
				throughputWeight = F * (clearcoat * G2 / fmaxf(G1v, 1e-6f)) * (1.0f / coatProb);
				outEscapeRoughness = clearcoatRoughness;
				hasContinuation = true;
			}
			// NdotL<=0: a VNDF sample that reflects below the coat's macro
			// surface - dead-end path, same handling as the base specular
			// lobe's own identical edge case below.
		}
		else if (lobeXi < coatProb + specProb * (1.0f - coatProb))
		{
			const float specProbScaled = specProb * (1.0f - coatProb);
			const float alpha = roughness * roughness;
			// KHR_materials_anisotropy: reuse the rotated (anisoT, anisoB)
			// frame and its separate alphaT/alphaB in place of the isotropic
			// (T, B, alpha) basis when active - matches CpuPathTracer::
			// sampleBSDFBounce()'s hasAniso?anisoT:T/hasAniso?alphaT:alpha
			// selection exactly. The mirror fast-path is skipped entirely
			// when hasAniso (also matching CPU) - a perfectly smooth
			// anisotropic material still has a meaningfully STRETCHED
			// (non-mirror) lobe via anisoAlphaT, so short-circuiting to an
			// isotropic mirror reflection would silently discard the
			// stretch the user actually authored.
			const float3& Tb = hasAniso ? anisoT : T;
			const float3& Bb = hasAniso ? anisoB : B;
			const float aT = hasAniso ? anisoAlphaT : alpha;
			const float aB = hasAniso ? anisoAlphaB : alpha;
			const bool polishedMetalMirrorApprox = metalness >= 0.9f && roughness <= 0.12f;
			if (!hasAniso && (roughness <= 0.01f || polishedMetalMirrorApprox))
			{
				// reflect(-V, N), not reflect(rayDir, N) - see the coat lobe's
				// identical comment above for why (matches CpuPathTracer's
				// own glm::reflect(-V, basisN) here).
				const float3 L = reflectF3(V * -1.0f, worldNormal);
				const float NdotL = dot3(worldNormal, L);
				if (NdotL > 0.0f)
				{
					const float3 F = applyIridescenceToFresnel(fresnelSchlick(NdotV, F0, F90), NdotV, F0, iridescenceFactor, iridescenceIor, iridescenceThickness);
					nextDirection = L;
					throughputWeight = F * (1.0f / specProbScaled);
					outEscapeRoughness = 0.0f;
					hasContinuation = true;
				}
			}
			else
			{
			const float NdotV0 = fmaxf(dot3(worldNormal, V), 1e-4f);
			const float3 Ve = make_float3(dot3(V, Tb), dot3(V, Bb), NdotV0);
			const float3 Hlocal = sampleGGXVNDF(Ve, aT, aB, u1, u2);
			const float3 Hworld = normalizeF3(Tb * Hlocal.x + Bb * Hlocal.y + worldNormal * Hlocal.z);
			// reflect(-V, H), not reflect(rayDir, H) - see the coat lobe's
			// identical comment above for why (matches CpuPathTracer's own
			// glm::reflect(-V, H) here).
			const float3 L = reflectF3(V * -1.0f, Hworld);
			const float NdotL = dot3(worldNormal, L);
			if (NdotL > 0.0f)
			{
				// F*G2/G1 - the well-known VNDF-sampling weight simplification
				// (the D and one G1 factor already cancel against the VNDF
				// sampling pdf, leaving just the masking-shadowing ratio).
				// MUST use the height-correlated Smith pair with alpha - see
				// smithG1GGX()/smithG2HeightCorrelatedGGX()'s doc comment for
				// the smudged-reflection bug the raster-parity geometrySmith()
				// caused here before. Anisotropic materials use the anisotropic
				// Smith pair instead (over Tb/Bb), matching CpuPathTracer's
				// sampleBSDFBounce() exactly.
				const float VdotH = fmaxf(dot3(V, Hworld), 0.0f);
				float G1v, G2;
				if (hasAniso)
				{
					const float3 Vlocal2 = make_float3(dot3(V, Tb), dot3(V, Bb), NdotV);
					const float3 Llocal2 = make_float3(dot3(L, Tb), dot3(L, Bb), NdotL);
					G1v = smithG1GGXAniso(Vlocal2, aT, aB);
					G2 = smithG2HeightCorrelatedGGXAniso(Vlocal2, Llocal2, aT, aB);
				}
				else
				{
					G1v = smithG1GGX(NdotV, alpha);
					G2 = smithG2HeightCorrelatedGGX(NdotV, NdotL, alpha);
				}
				const float3 F = applyIridescenceToFresnel(fresnelSchlick(VdotH, F0, F90), VdotH, F0, iridescenceFactor, iridescenceIor, iridescenceThickness);
				nextDirection = L;
				throughputWeight = F * (G2 / fmaxf(G1v, 1e-6f)) * (1.0f / specProbScaled);
				outEscapeRoughness = roughness;
				hasContinuation = true;
			}
			// NdotL<=0: a VNDF sample that reflects below the macro surface -
			// a known, rare edge case at high roughness/grazing angles.
			// Treated as a dead-end path (no continuation), same as
			// CpuPathTracer's own identical handling.
			}
		}
		else
		{
			// KHR_materials_diffuse_transmission - stochastically pick
			// between this front-hemisphere reflection lobe (around
			// worldNormal) and a back-hemisphere transmission lobe (around
			// -worldNormal), weighted by diffuseTransmissionFactor (0 reduces
			// to the original front-only behavior exactly). Sampling
			// -worldNormal with the SAME (T,B) basis is valid since it shares
			// the same tangent plane, just flipped. The stochastic pick
			// weight cancels out of the final throughput algebraically (both
			// branches divide by the same diffuseProb, not diffuseProb
			// scaled by the pick probability) - see CpuPathTracer::
			// sampleBSDFBounce()'s identical diffuse-transmission branch.
			rngState = pcgHash(rngState);
			const bool transmitDiffuse = diffuseTransmissionFactor > 0.0f && hashToUnitFloat(rngState) < diffuseTransmissionFactor;
			const float3 lobeNormal = transmitDiffuse ? (worldNormal * -1.0f) : worldNormal;
			const float3 localDir = cosineSampleHemisphere(u1, u2);
			const float3 L = normalizeF3(T * localDir.x + B * localDir.y + lobeNormal * localDir.z);
			// Cosine-weighted sampling's pdf (NdotL/pi) exactly cancels the
			// Lambertian BRDF's own NdotL/pi term, leaving just the albedo -
			// divided by this lobe's own selection probability, per the
			// standard multi-lobe stochastic-BSDF estimator.
			const float diffuseProb = fmaxf(1.0f - coatProb - specProb * (1.0f - coatProb), 1e-4f);
			nextDirection = L;
			if (transmitDiffuse)
			{
				throughputWeight = diffuseTransmissionColor * (1.0f / diffuseProb);
			}
			else
			{
				const float3 kD = (make_float3(1.0f, 1.0f, 1.0f) - Fview) * (1.0f - metalness);
				throughputWeight = kD * baseColor * (ao / diffuseProb);
			}
			outEscapeRoughness = -2.0f;
			hasContinuation = true;
			// KHR_materials_volume_scatter: entry into the medium is just
			// the ordinary diffuse_transmission back-hemisphere lobe
			// computed above (throughputWeight/nextDirection, unmodified) -
			// no special-casing needed here. The genuine free-flight random
			// walk happens on SUBSEQUENT hits, at the hitBackface/
			// hasVolumeScattering gate further up this function, which
			// decides per-segment whether the ray scatters before reaching
			// the next surface. See this feature's plan doc for why the
			// BSSRDF-substitution approach that used to live here was
			// replaced.
		}

		// outEscapeRoughness==0.0f is an EXACT literal only ever written by
		// the polished-metal-mirror shortcut above - every other lobe here
		// reports a floored, always-nonzero roughness (roughness/
		// clearcoatRoughness are both clamped to a 0.0001 minimum
		// device-side, and the diffuse lobe uses the -2.0f sentinel
		// instead), so this exactly identifies a deterministic near-delta
		// reflection rather than a real finite-width GGX sample. Evaluating
		// a finite MIS pdf for a delta BSDF is meaningless: NEE can
		// (almost) never importance-sample the exact mirror direction, so
		// it contributes ~0 regardless, while computing a pdf for the
		// bsdf-escape channel here would get it discounted by
		// envPdfAtRay's mere presence at that direction in __miss__ms() -
		// not noise, a systematic bias, since the mirror direction is fixed
		// per hit. Skip MIS entirely (outBsdfPdf stays 0.0f, matching the
		// transmission branch's identical convention above) - ported from
		// CpuPathTracer::tracePixel()'s identical fix, which was the actual
		// cause of polished, highly-metallic spheres (e.g. glTF's
		// MetalRoughSpheresNoTextures) rendering with a dark body and only
		// sparse bright specks instead of a bright mirror-like environment
		// reflection.
		if (hasContinuation && !hasPrecomputedBsdfPdf && outEscapeRoughness != 0.0f)
			outBsdfPdf = evaluateBsdfPdf(worldNormal, Ncoat, V, nextDirection,
				roughness, clearcoatRoughness, hasAniso, anisoT, anisoB, anisoAlphaT, anisoAlphaB, specProb, coatProb);
	}

	// KHR_materials_sheen's base+sheen energy-conservation dampening,
	// applied uniformly regardless of which lobe was picked above - see
	// sheenIndirectDampening's own computation/doc comment. NOT applied to
	// the transmission branch above (matching CpuPathTracer::tracePixel(),
	// which only multiplies its own identical dampening term into the
	// general lobe-selection path's throughput, never the transmission
	// branch's - sheen+transmission combined materials are a rare, largely
	// untested combination in either engine).
	if (!isTransmissionBounce)
		throughputWeight = throughputWeight * sheenIndirectDampening;

	// KHR_materials_volume's Beer-Lambert absorption applies to EVERYTHING
	// this hit contributes from this point onward - both its own direct/
	// emissive radiance and the throughput weight future bounces will be
	// scaled by - matching CpuPathTracer::tracePixel()'s single early
	// throughput*=volumeAttenuation multiply, which (by running before that
	// function's own NEE/emissive accumulation) affects both in exactly the
	// same way. A no-op (1,1,1) multiply whenever volumeAttenuation isn't
	// applicable (see its own computation above).
	radiance = radiance * volumeAttenuation;
	throughputWeight = throughputWeight * volumeAttenuation;

	setPayload(radiance);
	// 1 = hit with a valid continuation direction that counts against the
	// raygen loop's ordinary bounce budget, 3 = hit with a valid
	// KHR_materials_transmission continuation that instead counts against
	// its own, separate (and much larger) transmission-bounce budget - see
	// __raygen__rg()'s transmissionDepth handling, 4 = explicit-origin
	// continuation, written directly by KHR_materials_volume_scatter's
	// free-flight walk's scatter-event branch above (which returns before
	// reaching this point) - never produced from here. 2 = hit but dead-end
	// (no continuation - e.g. the rare below-surface VNDF sample), 0 = miss
	// (written by __miss__ms() only). The raygen loop continues on 1, 3, or
	// 4, and counts any non-zero hit flag as "primary ray hit geometry" for
	// the alpha/hit-fraction channel - a dead-end hit is still a hit.
	optixSetPayload_3(hasContinuation ? (isTransmissionBounce ? 3u : 1u) : 2u);
	optixSetPayload_4(__float_as_uint(worldNormal.x));
	optixSetPayload_5(__float_as_uint(worldNormal.y));
	optixSetPayload_6(__float_as_uint(worldNormal.z));
	optixSetPayload_7(__float_as_uint(optixGetRayTmax()));
	optixSetPayload_8(__float_as_uint(nextDirection.x));
	optixSetPayload_9(__float_as_uint(nextDirection.y));
	optixSetPayload_10(__float_as_uint(nextDirection.z));
	optixSetPayload_11(__float_as_uint(throughputWeight.x));
	optixSetPayload_12(__float_as_uint(throughputWeight.y));
	optixSetPayload_13(__float_as_uint(throughputWeight.z));
	// OIDN guide albedo - ported from CpuPathTracer::tracePixel()'s identical
	// clearcoatGuideStrength computation (this backend previously used raw
	// baseColor here unconditionally, with NO clearcoat contribution at
	// all - a real, worse-than-CPU gap, not merely a missing refinement).
	// Blends toward neutral white by the max of two signals: clearcoatBlend
	// (Fresnel-weighted, angle-dependent - needed for a uniformly-coated
	// curved surface, where the coat's own colorless reflectance dominates
	// increasingly toward grazing silhouette edges) and clearcoat directly
	// (a direct, view-INDEPENDENT floor driven by KHR_materials_clearcoat's
	// own clearcoatTexture mask). Fresnel varies smoothly with view angle
	// and carries no signal at all for a texture-driven coat/no-coat
	// boundary (e.g. glTF's ClearCoatTest.gltf "Partial Coating" bands,
	// roughly constant view angle across the boundary) - relying on
	// clearcoatBlend alone let OIDN read that texture's own genuine pattern
	// as noise and blur the bands away entirely, even though they converged
	// correctly pre-denoise (visible at any sample count, ruling out
	// under-sampling as the cause).
	const float clearcoatGuideStrength = fmaxf(clearcoat,
		fminf(fmaxf((clearcoatBlend.x + clearcoatBlend.y + clearcoatBlend.z) / 3.0f, 0.0f), 1.0f));
	const float3 guideAlbedo = lerp3(baseColor, make_float3(1.0f, 1.0f, 1.0f), clearcoatGuideStrength);
	optixSetPayload_14(__float_as_uint(guideAlbedo.x));
	optixSetPayload_15(__float_as_uint(guideAlbedo.y));
	optixSetPayload_16(__float_as_uint(guideAlbedo.z));
	optixSetPayload_17(__float_as_uint(outEscapeRoughness));
	optixSetPayload_19(__float_as_uint(outBsdfPdf));
}
