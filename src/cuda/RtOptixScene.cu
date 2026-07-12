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
// shadow ray, writing just the occluded/unoccluded bit into payload 0).
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
// Still deferred: AO, opacity/alpha, and every KHR extension texture
// (specular/clearcoat/sheen/anisotropy/iridescence/transmission) - see
// RtMaterial's own doc comments for what those add. Self-contained, same
// style as RtOptixTriangle.cu (no dependency on the OptiX SDK's bundled
// sutil).
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
	// the GGX Distribution of Visible Normals") - isotropic form (single
	// alpha, matching this backend's no-anisotropy scope). Ve is the view
	// direction in TANGENT space (Z-up); returns the sampled half-vector H,
	// also in tangent space. Reflecting the (tangent-space or, as used here,
	// world-space-via-the-same-basis) view direction around this H gives a
	// specular-lobe-importance-sampled bounce direction, whose throughput
	// weight is F*G2/G1 (the well-known VNDF-sampling simplification - see
	// this file's specular-lobe branch in __closesthit__ch()).
	__forceinline__ __device__ float3 sampleGGXVNDF(const float3& Ve, float alpha, float u1, float u2)
	{
		const float3 Vh = normalizeF3(make_float3(alpha * Ve.x, alpha * Ve.y, Ve.z));

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

		return normalizeF3(make_float3(alpha * Nh.x, alpha * Nh.y, fmaxf(0.0f, Nh.z)));
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
	// (or the diffuse-lobe sentinel 1.0) of whichever lobe the PREVIOUS
	// bounce's closest-hit sampled to produce this ray's direction (-1.0 for
	// the very first, primary/camera ray, meaning "show the plain
	// background, not an environment-lighting contribution"). On a real hit,
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
			1e-4f,  // tmin
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
	// one-line early-out instead of running the full shading path. Only
	// payload 0 is used (and meaningful) for this trace - the other
	// registers keep whatever stale values they held from the caller's
	// previous trace call, which is fine since neither program writes them
	// on this path.
	__forceinline__ __device__ bool traceShadowRay(const float3& origin, const float3& direction, float maxDistance)
	{
		const float eps = selfIntersectionEpsilon(origin);
		unsigned int occluded = 0u;
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
			occluded);
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
	for (unsigned int s = 0; s < spp; ++s)
	{
		const unsigned int seed = pcgHash(pixelIndex * 9781u + s * 6271u + 1u);
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
			}

			if (hitFlag == 0u)
				break; // escaped to the environment, or a dead-end sample with no valid continuation - already fully accounted for above

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
}

extern "C" __global__ void __miss__ms()
{
	if (optixGetRayFlags() & OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT)
	{
		// Shadow ray reached the light with nothing in the way - unoccluded.
		optixSetPayload_0(0u);
		return;
	}

	// p18 carries the escape-roughness for THIS ray (see traceBouncePath()'s
	// doc comment) - the sentinel -1 means "this is the primary/camera ray,
	// show the plain background," any other value means "this ray was
	// sampled from a bounce's lobe with this roughness (1.0 for the diffuse
	// lobe's sentinel), sample the correspondingly-blurred environment mip."
	const float escapeRoughness = __uint_as_float(optixGetPayload_18());
	const float3 dir = optixGetWorldRayDirection();

	float3 result;
	if (escapeRoughness < 0.0f)
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
	else
	{
		result = sampleEnvironmentSpecular(params.environment, dir, escapeRoughness);
	}

	setPayload(result);
	optixSetPayload_3(0u); // hitFlag = miss, no continuation for the raygen loop

	// No primary-hit surface to derive a guide value from on a miss -
	// matches CpuPathTracer's own zero-initialized outPrimaryAlbedo/
	// outPrimaryNormal default when the primary ray never hits geometry.
	optixSetPayload_4(0u); optixSetPayload_5(0u); optixSetPayload_6(0u);
	optixSetPayload_14(0u); optixSetPayload_15(0u); optixSetPayload_16(0u);
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

	// All 4 UV channels, barycentrically interpolated - a texture's KHR-
	// declared texCoordIndex can reference any of them (see
	// sampleTexture2D()'s use site), matching CpuPathTracer::sampleTexture()'s
	// identical per-texture channel selection.
	float2 uv[4];
	for (int ch = 0; ch < 4; ++ch)
	{
		const float2 t0 = data->texCoords[tri.x * 4 + ch];
		const float2 t1 = data->texCoords[tri.y * 4 + ch];
		const float2 t2 = data->texCoords[tri.z * 4 + ch];
		uv[ch] = t0 * w + t1 * u + t2 * v;
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

	worldNormal = applyNormalMap(worldNormal, worldTangentAndHandedness, data->normalTexture, uv, data->normalScale);

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

	// View direction and the simplified (no KHR_materials_ior/specular) F0 -
	// mirrors CpuPathTracer's own default: dielectrics get the standard
	// 0.04 reflectance, metals use their own baseColor as F0, F90 = 1.0.
	const float3 V = normalizeF3(rayDir * -1.0f);
	const float NdotV = fmaxf(dot3(worldNormal, V), 0.0f);
	const float3 F0 = lerp3(make_float3(0.04f, 0.04f, 0.04f), baseColor, metalness);
	const float3 F90 = make_float3(1.0f, 1.0f, 1.0f);
	const float roughness = fmaxf(roughnessFactor, 0.03f); // avoid a singular perfect mirror (alpha=0), matching CpuPathTracer's own floor

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
			if (traceShadowRay(shadowOrigin, lightDir, shadowMaxDistance))
				continue;

			const float3 H = normalizeF3(V + lightDir);
			const float NdotH = fmaxf(dot3(worldNormal, H), 0.0f);
			const float VdotH = fminf(fmaxf(dot3(H, V), 0.0f), 1.0f);

			const float3 F = fresnelSchlick(VdotH, F0, F90);
			const float D = distributionGGX(NdotH, roughness);
			const float G = geometrySmith(NdotV, NdotL, roughness);
			const float3 specular = F * (D * G / fmaxf(4.0f * NdotV * NdotL, 0.001f));

			const float3 kD = (make_float3(1.0f, 1.0f, 1.0f) - F) * (1.0f - metalness);
			const float3 diffuse = kD * baseColor * (1.0f / kPi);

			radiance = radiance + (diffuse + specular) * lightIntensity * NdotL;
		}
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
	float outEscapeRoughness = 1.0f; // diffuse-lobe sentinel by default
	bool hasContinuation = false;
	if (NdotV > 0.0f)
	{
		unsigned int rngState = optixGetPayload_17();

		float3 T, B;
		buildOrthonormalBasis(worldNormal, T, B);
		const float3 Vlocal = make_float3(dot3(V, T), dot3(V, B), dot3(V, worldNormal));

		// Average Fresnel reflectance at the viewing angle, clamped well
		// away from 0/1 so neither lobe is ever starved of samples entirely
		// (a dielectric with F0~0.04 would otherwise almost never sample
		// specular, missing its grazing-angle Fresnel-bright rim; a near-
		// mirror surface would almost never sample diffuse).
		const float3 Fview = fresnelSchlick(NdotV, F0, F90);
		const float specProb = fminf(fmaxf((Fview.x + Fview.y + Fview.z) * (1.0f / 3.0f), 0.1f), 0.9f);

		rngState = pcgHash(rngState);
		const float lobeXi = hashToUnitFloat(rngState);

		if (lobeXi < specProb)
		{
			rngState = pcgHash(rngState);
			const float u1 = hashToUnitFloat(rngState);
			rngState = pcgHash(rngState);
			const float u2 = hashToUnitFloat(rngState);

			const float alpha = roughness * roughness;
			const float3 Hlocal = sampleGGXVNDF(Vlocal, alpha, u1, u2);
			const float3 Hworld = normalizeF3(T * Hlocal.x + B * Hlocal.y + worldNormal * Hlocal.z);
			const float3 L = reflectF3(rayDir, Hworld);
			const float NdotL = dot3(worldNormal, L);
			if (NdotL > 0.0f)
			{
				// F*G2/G1 - the well-known VNDF-sampling weight simplification
				// (the D and one G1 factor already cancel against the VNDF
				// sampling pdf, leaving just the masking-shadowing ratio).
				const float VdotH = fmaxf(dot3(V, Hworld), 0.0f);
				const float G1v = geometrySchlickGGX(NdotV, roughness);
				const float G2 = geometrySmith(NdotV, NdotL, roughness);
				const float3 F = fresnelSchlick(VdotH, F0, F90);
				nextDirection = L;
				throughputWeight = F * (G2 / fmaxf(G1v, 1e-4f)) * (1.0f / specProb);
				outEscapeRoughness = roughness;
				hasContinuation = true;
			}
			// NdotL<=0: a VNDF sample that reflects below the macro surface -
			// a known, rare edge case at high roughness/grazing angles.
			// Treated as a dead-end path (no continuation), same as
			// CpuPathTracer's own identical handling.
		}
		else
		{
			rngState = pcgHash(rngState);
			const float u1 = hashToUnitFloat(rngState);
			rngState = pcgHash(rngState);
			const float u2 = hashToUnitFloat(rngState);

			const float3 localDir = cosineSampleHemisphere(u1, u2);
			const float3 L = normalizeF3(T * localDir.x + B * localDir.y + worldNormal * localDir.z);
			// Cosine-weighted sampling's pdf (NdotL/pi) exactly cancels the
			// Lambertian BRDF's own NdotL/pi term, leaving just the albedo -
			// divided by this lobe's own selection probability, per the
			// standard two-lobe stochastic-BSDF estimator.
			const float3 kD = (make_float3(1.0f, 1.0f, 1.0f) - Fview) * (1.0f - metalness);
			nextDirection = L;
			throughputWeight = kD * baseColor * (1.0f / fmaxf(1.0f - specProb, 1e-4f));
			outEscapeRoughness = 1.0f;
			hasContinuation = true;
		}
	}

	setPayload(radiance);
	optixSetPayload_3(hasContinuation ? 1u : 0u);
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
