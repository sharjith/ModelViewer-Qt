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
// reduction (sampleEnvironmentSpecular(), same as before) - see
// RtOptixSceneParams.h's RtOptixEnvironment doc comment for why a diffuse-
// lobe escape uses the sentinel roughness=1.0 (most-blurred mip) in place
// of a real irradiance map, which this backend doesn't capture separately.
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
// the indirect specular lobe's Fresnel term) are now implemented, all ported
// from CpuPathTracer's identically-named functions.
//
// Still deferred: alphaMode Blend (true transparency compositing) and
// transmission - see RtMaterial's own doc comments for what those add.
// Self-contained, same style as RtOptixTriangle.cu (no dependency on the
// OptiX SDK's bundled sutil).
// ---------------------------------------------------------------------------
#include <optix.h>

#include "RtOptixSceneParams.h"

extern "C" {
__constant__ RtOptixSceneParams params;
}

namespace
{
	constexpr float kPi = 3.14159265f;

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

	__forceinline__ __device__ float4 fetchTexelWrapped(const RtOptixTexture& tex, int x, int y)
	{
		x = wrapTexelIndex(x, tex.width, tex.wrapS);
		y = wrapTexelIndex(y, tex.height, tex.wrapT);
		const uchar4 p = tex.rgba8[static_cast<size_t>(y) * tex.width + x];
		return make_float4(p.x / 255.0f, p.y / 255.0f, p.z / 255.0f, p.w / 255.0f);
	}

	// Bilinear, half-texel-centered, wrap-aware, KHR_texture_transform-aware
	// sample - see CpuPathTracer::sampleTexture()'s doc comment for why each
	// of those matters (a nearest-neighbor or wrap-oblivious sample would
	// silently regress the exact bugs that function's own history fixed).
	// Returns raw (not sRGB-decoded) 0-1 RGBA - callers decide whether this
	// texture's bytes are sRGB color data or linear scalar/vector data.
	__forceinline__ __device__ float4 sampleTexture2D(const RtOptixTexture& tex, const float2 uv[4])
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

		const float fx = st.x * static_cast<float>(tex.width)  - 0.5f;
		const float fy = st.y * static_cast<float>(tex.height) - 0.5f;
		const int x0 = static_cast<int>(floorf(fx));
		const int y0 = static_cast<int>(floorf(fy));
		const float tx = fx - static_cast<float>(x0);
		const float ty = fy - static_cast<float>(y0);

		const float4 c00 = fetchTexelWrapped(tex, x0,     y0);
		const float4 c10 = fetchTexelWrapped(tex, x0 + 1, y0);
		const float4 c01 = fetchTexelWrapped(tex, x0,     y0 + 1);
		const float4 c11 = fetchTexelWrapped(tex, x0 + 1, y0 + 1);

		const float4 top    = lerp4(c00, c10, tx);
		const float4 bottom = lerp4(c01, c11, tx);
		return lerp4(top, bottom, ty);
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

	// Bilinear lookup into the sheen directional-albedo LUT baked on the host
	// (RtOptixSceneTracer::Impl::ensureSheenAlbedoLut()) - device-side
	// counterpart of CpuPathTracer::sampleSheenAlbedoLUT(). Nearest-indexed
	// (matches the CPU function exactly - no bilinear there either), guards
	// against a missing/failed-upload LUT by returning 0 (no dampening).
	__forceinline__ __device__ float sampleSheenAlbedoLUT(const float* lut, int lutSize, float NdotV, float roughness)
	{
		if (!lut || lutSize <= 0)
			return 0.0f;
		const int vi = min(max(static_cast<int>(NdotV * lutSize), 0), lutSize - 1);
		const int ri = min(max(static_cast<int>(roughness * lutSize), 0), lutSize - 1);
		return lut[static_cast<size_t>(ri) * lutSize + vi];
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

		float rangeAttenuation = 1.0f / (distance * distance);
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
		return env.faceSize > 0 ? sampleCubemapFaces(env.faces, env.faceSize, sampleDir) : flatGradientMiss(direction);
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
		unsigned int rngSeed, float escapeRoughness,
		float3& outRadiance, unsigned int& outHitFlag, float3& outWorldNormal, float& outHitDistance,
		float3& outNextDirection, float3& outThroughputWeight, float3& outGuideAlbedo, float& outEscapeRoughness)
	{
		unsigned int p0 = 0u, p1 = 0u, p2 = 0u, p3 = 0u, p4 = 0u, p5 = 0u, p6 = 0u, p7 = 0u, p8 = 0u;
		unsigned int p9 = 0u, p10 = 0u, p11 = 0u, p12 = 0u, p13 = 0u, p14 = 0u, p15 = 0u, p16 = 0u;
		unsigned int p17 = rngSeed;
		unsigned int p18 = __float_as_uint(escapeRoughness);
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
			p0, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18);
		outRadiance = make_float3(__uint_as_float(p0), __uint_as_float(p1), __uint_as_float(p2));
		outHitFlag = p3;
		outWorldNormal = make_float3(__uint_as_float(p4), __uint_as_float(p5), __uint_as_float(p6));
		outHitDistance = __uint_as_float(p7);
		outNextDirection = make_float3(__uint_as_float(p8), __uint_as_float(p9), __uint_as_float(p10));
		outThroughputWeight = make_float3(__uint_as_float(p11), __uint_as_float(p12), __uint_as_float(p13));
		outGuideAlbedo = make_float3(__uint_as_float(p14), __uint_as_float(p15), __uint_as_float(p16));
		outEscapeRoughness = __uint_as_float(p17);
	}

	// Plain boolean occlusion query - reuses the same pipeline/SBT as the
	// other trace wrappers above (no dedicated occlusion program group/hit
	// records) by setting OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT and having
	// __closesthit__ch()/__miss__ms() check optixGetRayFlags() to take a
	// one-line early-out instead of running the full shading path. Payload
	// 0 is the occluded bit; payloads 1/2 let __anyhit__ah() ignore hits
	// against the source instance when self-shadows are disabled.
	__forceinline__ __device__ bool traceShadowRay(const float3& origin, const float3& direction, float maxDistance,
		unsigned int sourceInstanceId)
	{
		const float eps = selfIntersectionEpsilon(origin);
		unsigned int occluded = 0u;
		unsigned int selfInstanceId = sourceInstanceId;
		unsigned int selfShadowsEnabled = params.selfShadowsEnabled != 0 ? 1u : 0u;
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
			occluded, selfInstanceId, selfShadowsEnabled);
		return occluded != 0u;
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
	float accumulatedHits = 0.0f; // primary-hit count -> alpha/hit-fraction, see params.alphaImage's doc comment
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
		float3 curOrigin = rayOrigin;
		float3 curDirection = rayDirection;

		for (unsigned int bounce = 0; bounce < maxBounces; ++bounce)
		{
			rngState = pcgHash(rngState + bounce * 0x9e3779b9u);

			float3 hitRadiance, worldNormal, nextDirection, throughputWeight, guideAlbedo;
			float hitDistance, nextEscapeRoughness;
			unsigned int hitFlag;
			traceBouncePath(curOrigin, curDirection, rngState, escapeRoughness,
				hitRadiance, hitFlag, worldNormal, hitDistance, nextDirection, throughputWeight, guideAlbedo, nextEscapeRoughness);

			sampleRadiance = sampleRadiance + throughput * hitRadiance;
			if (bounce == 0)
			{
				sampleAlbedo = guideAlbedo;
				sampleNormal = worldNormal;
				if (hitFlag != 0u) // 1 (hit+continuation) or 2 (hit+dead-end) - either way the primary ray hit geometry
					accumulatedHits += 1.0f;
			}

			if (hitFlag != 1u)
				break; // 0: escaped to the environment; 2: hit but dead-end sample with no valid continuation - either way, fully accounted for above

			throughput = throughput * throughputWeight;

			// Russian roulette after a couple of guaranteed bounces, bounding
			// cost/variance on long paths - matches the spirit of
			// CpuPathTracer::Settings::russianRouletteStartDepth (a fixed
			// depth floor before RR kicks in, so the cheap, high-value first
			// few bounces are never cut short).
			if (bounce >= 2)
			{
				const float continueProb = fminf(fmaxf(fmaxf(throughput.x, fmaxf(throughput.y, throughput.z)), 0.05f), 0.95f);
				rngState = pcgHash(rngState ^ 0xA5A5A5A5u);
				if (hashToUnitFloat(rngState) > continueProb)
					break;
				throughput = throughput * (1.0f / continueProb);
			}

			const float3 hitPos = curOrigin + curDirection * hitDistance;
			curOrigin = hitPos + worldNormal * selfIntersectionEpsilon(hitPos);
			curDirection = nextDirection;
			escapeRoughness = nextEscapeRoughness;
		}

		accumulated = accumulated + sampleRadiance;
		accumulatedAlbedo = accumulatedAlbedo + sampleAlbedo;
		accumulatedNormal = accumulatedNormal + sampleNormal;
	}

	const float invSpp = 1.0f / static_cast<float>(spp);
	params.image[pixelIndex]       = accumulated * invSpp;
	params.albedoImage[pixelIndex] = accumulatedAlbedo * invSpp;
	params.normalImage[pixelIndex] = accumulatedNormal * invSpp;
	params.alphaImage[pixelIndex]  = accumulatedHits * invSpp;
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
	// and >=0 means a GGX specular-lobe escape with that material roughness.
	const float escapeRoughness = __uint_as_float(optixGetPayload_18());
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
		result = sampleEnvironmentBackground(params.environment, dir, su, sv);
	}
	else if (escapeRoughness < 0.0f)
	{
		// Diffuse-lobe escape. This backend does not upload the irradiance
		// cubemap yet, so keep the existing roughest-prefilter stand-in for
		// diffuse ambient/environment light.
		result = sampleEnvironmentSpecular(params.environment, dir, 1.0f);
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

	setPayload(result);
	optixSetPayload_3(0u); // hitFlag = miss, no continuation for the raygen loop

	// No primary-hit surface to derive a guide value from on a miss -
	// matches CpuPathTracer's own zero-initialized outPrimaryAlbedo/
	// outPrimaryNormal default when the primary ray never hits geometry.
	optixSetPayload_4(0u); optixSetPayload_5(0u); optixSetPayload_6(0u);
	optixSetPayload_14(0u); optixSetPayload_15(0u); optixSetPayload_16(0u);
}

// glTF alphaMode Masked cutout - any-hit runs for EVERY ray type (primary/
// bounce trace calls and shadow rays alike), so a sub-cutoff hit is
// invisible to all of them uniformly, matching the spec's "treat as if this
// geometry doesn't exist here" semantics (CpuPathTracer's own MASK handling
// in RtEmbreeScene::intersect()). optixIgnoreIntersection() tells BVH
// traversal to discard this candidate hit and keep looking - it does NOT
// terminate traversal, so a shadow ray with OPTIX_RAY_FLAG_TERMINATE_ON_
// FIRST_HIT correctly continues past a masked-out hit to find (or not find)
// a REAL occluder behind it. Opaque (blendMode==0) and Blend (blendMode==2,
// not yet implemented - see RtOptixSceneHitGroupData::blendMode's doc
// comment) materials always accept the hit here (a no-op any-hit program is
// the same as not having one at all).
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
	if (data->blendMode != 1)
		return;

	const unsigned int primIdx = optixGetPrimitiveIndex();
	const uint3 tri = data->indices[primIdx];
	const float2 bary = optixGetTriangleBarycentrics();
	const float u = bary.x, v = bary.y, w = 1.0f - u - v;

	float2 uv[4];
	interpolateUVs(data, tri, w, u, v, uv);

	if (resolveOpacity(data, uv) < data->alphaThreshold)
		optixIgnoreIntersection();
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

	// Faceforward against the ray, matching CpuPathTracer's own "shade the
	// side the ray actually hit" handling for thin/backfacing geometry.
	if (dot3(worldNormal, rayDir) > 0.0f)
		worldNormal = worldNormal * -1.0f;

	float2 uv[4];
	interpolateUVs(data, tri, w, u, v, uv);

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
	// layer's - falls back to the (already normal-mapped) base shading
	// normal when absent, matching CpuPathTracer's ": N" fallback exactly.
	const float3 Ncoat = (data->clearcoatNormalTexture.width > 0)
		? applyNormalMap(geometricNormal, worldTangentAndHandedness, data->clearcoatNormalTexture, uv, data->clearcoatNormalScale)
		: worldNormal;

	// Core PBR textures, matching CpuPathTracer::evaluateSurface()'s exact
	// factor*texture multiply order and sRGB/linear decode split (baseColor/
	// emissive are sRGB-encoded color data; metallic/roughness are linear
	// scalar data, channel-packed via applyChannelPacking()).
	float3 baseColor = data->baseColor;
	if (data->baseColorTexture.width > 0)
	{
		const float4 sampled = sampleTexture2D(data->baseColorTexture, uv);
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
		metalness *= applyChannelPacking(sampleTexture2D(data->metallicTexture, uv), data->metallicTexture);

	float roughnessFactor = data->roughness;
	if (data->roughnessTexture.width > 0)
		roughnessFactor *= applyChannelPacking(sampleTexture2D(data->roughnessTexture, uv), data->roughnessTexture);

	float3 emissive = data->emissive * data->emissiveStrength;
	if (data->emissiveTexture.width > 0)
	{
		const float4 sampled = sampleTexture2D(data->emissiveTexture, uv);
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
		const float texAo = applyChannelPacking(sampleTexture2D(data->aoTexture, uv), data->aoTexture);
		const float mixed = 1.0f + (texAo - 1.0f) * data->occlusionStrength; // mix(1.0, texAo, occlusionStrength)
		ao = fminf(fmaxf(mixed, 0.0001f), 1.0f);
	}

	// KHR_materials_specular's per-pixel maps - specularTexture's alpha
	// channel scales specularFactor (channel-packed), specularColorTexture's
	// RGB (sRGB-encoded) tints specularColorFactor - matching CpuPathTracer::
	// evaluateSurface()'s identical modulation before computeF0F90().
	float texturedSpecularFactor = data->specularFactor;
	if (data->specularTexture.width > 0)
		texturedSpecularFactor *= applyChannelPacking(sampleTexture2D(data->specularTexture, uv), data->specularTexture);

	float3 texturedSpecularColorFactor = data->specularColorFactor;
	if (data->specularColorTexture.width > 0)
	{
		const float4 sampled = sampleTexture2D(data->specularColorTexture, uv);
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
	const float3 V = isPrimaryOrthoHit ? normalizeF3(params.camPosition - worldPos) : normalizeF3(rayDir * -1.0f);
	const float NdotV = fmaxf(dot3(worldNormal, V), 0.0f);
	const float3 F0 = lerp3(dielectricF0, baseColor, metalness);
	const float3 F90 = lerp3(make_float3(texturedSpecularFactor, texturedSpecularFactor, texturedSpecularFactor), make_float3(1.0f, 1.0f, 1.0f), metalness);
	const float3 directF0 = lerp3(dielectricF0 * texturedSpecularFactor, baseColor, metalness);
	const float roughness = fmaxf(roughnessFactor, 0.0001f); // matches main_scene.frag/CpuPathTracer roughness floor

	// KHR_materials_clearcoat - factors/textures ported from CpuPathTracer::
	// evaluateSurface()'s identical R/G-channel-packed sampling.
	float clearcoat = data->clearcoat;
	if (data->clearcoatTexture.width > 0)
		clearcoat *= applyChannelPacking(sampleTexture2D(data->clearcoatTexture, uv), data->clearcoatTexture);
	clearcoat = fminf(fmaxf(clearcoat, 0.0f), 1.0f);

	float clearcoatRoughness = data->clearcoatRoughness;
	if (data->clearcoatRoughnessTexture.width > 0)
		clearcoatRoughness *= applyChannelPacking(sampleTexture2D(data->clearcoatRoughnessTexture, uv), data->clearcoatRoughnessTexture);
	clearcoatRoughness = fminf(fmaxf(clearcoatRoughness, 0.0001f), 1.0f);

	// KHR_materials_sheen - sheenColorTexture is sRGB RGB, sheenRoughnessTexture's
	// alpha channel scales sheenRoughness (channel-packed by RtSceneBuilder),
	// matching CpuPathTracer::evaluateSurface() exactly.
	float3 sheenColor = data->sheenColorFactor;
	if (data->sheenColorTexture.width > 0)
	{
		const float4 sampled = sampleTexture2D(data->sheenColorTexture, uv);
		sheenColor = sheenColor * sRGBToLinear(make_float3(sampled.x, sampled.y, sampled.z));
	}
	sheenColor = make_float3(fminf(fmaxf(sheenColor.x, 0.0f), 1.0f), fminf(fmaxf(sheenColor.y, 0.0f), 1.0f), fminf(fmaxf(sheenColor.z, 0.0f), 1.0f));

	float sheenRoughness = data->sheenRoughness;
	if (data->sheenRoughnessTexture.width > 0)
		sheenRoughness *= applyChannelPacking(sampleTexture2D(data->sheenRoughnessTexture, uv), data->sheenRoughnessTexture);
	sheenRoughness = fminf(fmaxf(sheenRoughness, 0.0001f), 1.0f);

	// Fresnel-weighted blend factor between the base layer and the coat layer
	// - ported from CpuPathTracer::tracePixel()'s "clearcoat * computeClearcoatFresnel(...)",
	// consumed both by the direct-lighting mix() below and by
	// computeLobeProbabilities()'s coat-lobe sampling weight.
	const float3 clearcoatBlend = computeClearcoatFresnel(data->ior, Ncoat, V) * clearcoat;

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
		const float4 sampled = sampleTexture2D(data->anisotropyTexture, uv);
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
		iridescenceFactor *= applyChannelPacking(sampleTexture2D(data->iridescenceTexture, uv), data->iridescenceTexture);
	iridescenceFactor = fminf(fmaxf(iridescenceFactor, 0.0f), 1.0f);

	const float iridescenceIor = data->iridescenceIor;
	float iridescenceThickness = data->iridescenceThickness;
	if (data->iridescenceThicknessTexture.width > 0)
		iridescenceThickness = applyChannelPacking(sampleTexture2D(data->iridescenceThicknessTexture, uv), data->iridescenceThicknessTexture);

	float3 radiance = emissive;
	if (NdotV > 0.0f)
	{
		for (unsigned int i = 0; i < params.lightCount; ++i)
		{
			float3 lightDir, lightIntensity;
			float lightDistance;
			evaluatePunctualLight(params.lights[i], worldPos, lightDir, lightIntensity, lightDistance);

			const float NdotL = fmaxf(dot3(worldNormal, lightDir), 0.0f);
			if (NdotL <= 0.0f)
				continue;

			// Offset along the (faceforward) shading normal, same as
			// CpuPathTracer's NEE shadow rays, to dodge self-intersection
			// with the surface this ray originates from.
			const float3 shadowOrigin = worldPos + worldNormal * selfIntersectionEpsilon(worldPos);
			const float shadowMaxDistance = fminf(lightDistance, 1e16f);
			if (params.shadowsEnabled != 0 && traceShadowRay(shadowOrigin, lightDir, shadowMaxDistance, instanceId))
				continue;

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
			const float3 diffuse = kD * baseColor * (1.0f / kPi);

			float3 baseDirect;
			// KHR_materials_iridescence - ported from CpuPathTracer::
			// evaluateDirectBRDF()'s iridescence branch, which entirely
			// replaces the diffuse+specular combination above with its own
			// dielectric/metal reconstruction rather than adding a term on
			// top.
			if (iridescenceFactor > 0.001f && iridescenceThickness > 0.0f)
			{
				const float3 l_diffuse = diffuse * NdotL;
				const float3 l_specular = specularNoF * NdotL;

				const float3 dielectricFresnel = fresnelSchlick(VdotH, dielectricDirectF0,
					make_float3(texturedSpecularFactor, texturedSpecularFactor, texturedSpecularFactor));
				const float3 metalFresnel = fresnelSchlick(VdotH, baseColor, make_float3(1.0f, 1.0f, 1.0f));
				float3 dielectricBrdf = lerp3(l_diffuse, l_specular, dielectricFresnel);
				float3 metalBrdf = metalFresnel * l_specular;

				const float3 iridescenceFresnelDielectric = evalIridescence(1.0f, iridescenceIor, NdotV, iridescenceThickness, dielectricF0, make_float3(1.0f, 1.0f, 1.0f));
				const float3 iridescenceFresnelMetallic = evalIridescence(1.0f, iridescenceIor, NdotV, iridescenceThickness, baseColor, make_float3(1.0f, 1.0f, 1.0f));
				metalBrdf = lerp3(metalBrdf, l_specular * iridescenceFresnelMetallic, iridescenceFactor);
				dielectricBrdf = lerp3(dielectricBrdf, rgbMix(l_diffuse, l_specular, iridescenceFresnelDielectric), iridescenceFactor);

				baseDirect = lerp3(dielectricBrdf, metalBrdf, metalness) * lightIntensity;
			}
			else
			{
				baseDirect = (diffuse + specular) * lightIntensity * NdotL;
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
				const float3 coatDirect = evaluateClearcoatDirect(Ncoat, V, lightDir, clearcoat, clearcoatRoughness) * lightIntensity;
				radiance = radiance + lerp3(baseDirect, coatDirect, fminf(fmaxf((clearcoatBlend.x + clearcoatBlend.y + clearcoatBlend.z) / 3.0f, 0.0f), 1.0f));
			}
			else
			{
				radiance = radiance + baseDirect;
			}

			// KHR_materials_sheen - additive, not blended.
			if (sheenColor.x > 0.0f || sheenColor.y > 0.0f || sheenColor.z > 0.0f)
				radiance = radiance + calculateSheen(worldNormal, V, lightDir, sheenColor, sheenRoughness) * lightIntensity;
		}
	}

	// KHR_materials_sheen's environment/IBL contribution - ported from
	// CpuPathTracer::tracePixel()'s identical block. This is NOT an optional
	// extra: per that function's own doc comment, direct (punctual-light)
	// sheen alone reads as "missing" on any scene lit mainly by its
	// environment (the common case for glTF sheen test/showcase assets,
	// which are typically lit by a neutral studio HDRI with weak or no
	// punctual lights) - env/IBL sheen turned out to be the visually
	// DOMINANT contribution there. This kernel originally skipped it as a
	// documented simplification (no separate IBL step, unlike AO's
	// throughput-only treatment) - that gap is exactly what made
	// sheenColorFactor variation invisible in GPU renders of SheenTestGrid
	// while raster and CPU PT (which already had this block) both show it
	// clearly. A small fixed number of RNG-jittered samples in a
	// roughness-sized cone around the mirror-reflect direction, taken on
	// every hit (not gated behind a rare lobe-selection probability) -
	// cheap since sampleEnvironmentRaw() is a plain cubemap fetch, not a
	// traced ray.
	float sheenIndirectDampening = 1.0f;
	if (NdotV > 0.0f && (sheenColor.x > 0.0f || sheenColor.y > 0.0f || sheenColor.z > 0.0f))
	{
		const float sheenStrengthForIBL = fmaxf(fmaxf(sheenColor.x, sheenColor.y), sheenColor.z);
		const float3 R = reflectF3(V * -1.0f, worldNormal);
		float3 Tc, Bc;
		buildOrthonormalBasis(R, Tc, Bc);

		const float sheenRoughFinal = fminf(fmaxf(sheenRoughness, 0.0001f), 1.0f);
		const float coneAngle = sheenRoughFinal * (kPi * 0.5f); // up to a full hemisphere spread at roughness 1

		// Derived from, but distinct from, the RNG stream the lobe-selection
		// code below draws from optixGetPayload_17() - reading a payload
		// doesn't consume/mutate it, so both blocks would otherwise see the
		// identical raw seed; XOR-ing a distinguishing constant first keeps
		// their jitter sequences decorrelated.
		unsigned int sheenRng = optixGetPayload_17() ^ 0xC0FFEE17u;

		constexpr int kSheenEnvSamples = 8;
		float3 envSum = make_float3(0.0f, 0.0f, 0.0f);
		for (int s = 0; s < kSheenEnvSamples; ++s)
		{
			sheenRng = pcgHash(sheenRng);
			const float u1 = hashToUnitFloat(sheenRng);
			sheenRng = pcgHash(sheenRng);
			const float u2 = hashToUnitFloat(sheenRng);

			const float phi = 2.0f * kPi * u1;
			const float cosTheta = 1.0f - u2 * (1.0f - cosf(coneAngle)); // uniform within the cone
			const float sinTheta = sqrtf(fmaxf(0.0f, 1.0f - cosTheta * cosTheta));
			const float3 localDir = make_float3(sinTheta * cosf(phi), sinTheta * sinf(phi), cosTheta);
			const float3 dir = normalizeF3(Tc * localDir.x + Bc * localDir.y + R * localDir.z);
			envSum = envSum + sampleEnvironmentRaw(params.environment, dir);
		}
		envSum = envSum * (1.0f / static_cast<float>(kSheenEnvSamples));

		const float NdotVSheenIbl = fminf(fmaxf(dot3(worldNormal, V), 0.0f), 1.0f);
		const float E_sheen = sampleSheenAlbedoLUT(params.sheenAlbedoLUT, params.sheenAlbedoLUTSize, NdotVSheenIbl, sheenRoughFinal);
		radiance = radiance + sheenColor * envSum * (ao * E_sheen);

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
	bool hasContinuation = false;
	if (NdotV > 0.0f)
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
				throughputWeight = F * (G2 / fmaxf(G1v, 1e-6f)) * (1.0f / coatProb);
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
			const float3 localDir = cosineSampleHemisphere(u1, u2);
			const float3 L = normalizeF3(T * localDir.x + B * localDir.y + worldNormal * localDir.z);
			// Cosine-weighted sampling's pdf (NdotL/pi) exactly cancels the
			// Lambertian BRDF's own NdotL/pi term, leaving just the albedo -
			// divided by this lobe's own selection probability, per the
			// standard multi-lobe stochastic-BSDF estimator.
			const float diffuseProb = fmaxf(1.0f - coatProb - specProb * (1.0f - coatProb), 1e-4f);
			const float3 kD = (make_float3(1.0f, 1.0f, 1.0f) - Fview) * (1.0f - metalness);
			nextDirection = L;
			throughputWeight = kD * baseColor * (ao / diffuseProb);
			outEscapeRoughness = -2.0f;
			hasContinuation = true;
		}
	}

	// KHR_materials_sheen's base+sheen energy-conservation dampening,
	// applied uniformly regardless of which lobe was picked above - see
	// sheenIndirectDampening's own computation/doc comment.
	throughputWeight = throughputWeight * sheenIndirectDampening;

	setPayload(radiance);
	// 1 = hit with a valid continuation direction, 2 = hit but dead-end (no
	// continuation - e.g. the rare below-surface VNDF sample), 0 = miss
	// (written by __miss__ms() only). The raygen loop continues only on 1,
	// but counts BOTH 1 and 2 as "primary ray hit geometry" for the
	// alpha/hit-fraction channel - a dead-end hit is still a hit.
	optixSetPayload_3(hasContinuation ? 1u : 2u);
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
	optixSetPayload_14(__float_as_uint(baseColor.x));
	optixSetPayload_15(__float_as_uint(baseColor.y));
	optixSetPayload_16(__float_as_uint(baseColor.z));
	optixSetPayload_17(__float_as_uint(outEscapeRoughness));
}
