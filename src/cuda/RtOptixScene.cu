// ---------------------------------------------------------------------------
// RtOptixScene.cu - Phase 2e kernel for the GPU (OptiX) path tracer backend.
//
// Renders the app's real scene geometry (via a real GAS-per-mesh/IAS-per-
// instance acceleration structure - see RtOptixSceneTracer.cpp) through the
// real RtCamera projection, shaded with real flat material colors
// (baseColor/metalness/roughness/emissive - no textures yet), the full
// metallic-roughness Cook-Torrance BRDF for direct lighting (GGX
// distribution, Smith-Schlick geometry, Fresnel - ported verbatim from
// CpuPathTracer's evaluateDirectBRDF()), ONE mirror reflection bounce per
// primary hit (reflected ray either hits other scene geometry, shaded with
// its own direct lighting only, or escapes to the environment cubemap), and
// now a plain boolean shadow ray per light: every direct-lighting sample is
// occlusion-tested before being added, using OPTIX_RAY_FLAG_TERMINATE_ON_
// FIRST_HIT (stop at ANY hit, not necessarily the closest - a boolean query
// is all a shadow test needs) rather than a second program-group/SBT pair -
// __closesthit__ch()/__miss__ms() both check optixGetRayFlags() up front and
// take a one-line early-out path when it's a shadow ray, writing just the
// occluded/unoccluded bit into payload 0. This mirrors RtEmbreeScene::
// occluded()'s plain any-hit test, CpuPathTracer's own ORIGINAL (pre-alpha/
// transmission-aware) shadow-ray behavior - full alpha/MASK/transmission-
// aware shadow attenuation (CpuPathTracer::traceShadowRay()'s closest-hit
// walk) is a later increment, once this GPU backend has textures/alpha and
// transmissive materials to begin with. Still no textures, no anisotropy/
// clearcoat/sheen/transmission/iridescence, no roughness-based reflection
// blur, no multi-sample accumulation - see RtOptixSceneParams.h's doc
// comment for why this scope is deliberate. Self-contained, same style as
// RtOptixTriangle.cu (no dependency on the OptiX SDK's bundled sutil).
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

	__forceinline__ __device__ uchar4 toColor(const float3& c)
	{
		auto toByte = [](float v) -> unsigned char
		{
			v = fminf(fmaxf(v, 0.0f), 1.0f);
			return static_cast<unsigned char>(v * 255.0f + 0.5f);
		};
		return make_uchar4(toByte(c.x), toByte(c.y), toByte(c.z), 255);
	}

	__forceinline__ __device__ void setPayload(float3 p)
	{
		optixSetPayload_0(__float_as_uint(p.x));
		optixSetPayload_1(__float_as_uint(p.y));
		optixSetPayload_2(__float_as_uint(p.z));
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

	__forceinline__ __device__ float3 sampleCubemapFaces(const RtOptixEnvironment& env, const float3& direction)
	{
		int face;
		float u, v;
		selectCubemapFaceUV(normalizeF3(direction), face, u, v);

		const float3* faceData = env.faces[face];
		const int size = env.faceSize;

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
		return env.faceSize > 0 ? sampleCubemapFaces(env, sampleDir) : flatGradientMiss(direction);
	}

	// Reflection-bounce escape (depth>0) - matches CpuPathTracer::
	// sampleEnvironmentMiss(): envMapExposure DOES apply here (every
	// surface-side IBL sample gets it), unlike the background above.
	__forceinline__ __device__ float3 sampleEnvironmentReflection(const RtOptixEnvironment& env, const float3& direction)
	{
		if (env.faceSize <= 0)
			return flatGradientMiss(direction) * env.envMapExposure;

		const float3 sampleDir = undoSkyboxRotation(direction, env.cameraUpAxisZUp != 0, env.skyBoxZRotationDegrees);
		return sampleCubemapFaces(env, sampleDir) * env.envMapExposure;
	}

	// Wraps optixTrace() with the extra depth payload (p3) - depth is only
	// ever 0 (primary) or 1 (one reflection bounce) in this increment; the
	// closest-hit program itself refuses to spawn a second reflection at
	// depth==1 (see its use site), so this never recurses further no matter
	// what depth is passed in.
	__forceinline__ __device__ float3 traceRadiance(const float3& origin, const float3& direction, unsigned int depth)
	{
		unsigned int p0, p1, p2, p3 = depth;
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
			p0, p1, p2, p3);
		return make_float3(__uint_as_float(p0), __uint_as_float(p1), __uint_as_float(p2));
	}

	// Plain boolean occlusion query - reuses the same pipeline/SBT as
	// traceRadiance() above (no dedicated occlusion program group/hit
	// records) by setting OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT and having
	// __closesthit__ch()/__miss__ms() check optixGetRayFlags() to take a
	// one-line early-out instead of running the full shading path. Only
	// payload 0 is used (and meaningful) for this trace - the other
	// registers keep whatever stale values they held from the caller's
	// last traceRadiance() call, which is fine since neither program writes
	// them on this path.
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

	// Matches CpuPathTracer::tracePixel()'s primary-ray formula exactly (pixel
	// center, no AA jitter yet - single sample per pixel for this checkpoint).
	const float ndcX = 2.0f * (static_cast<float>(idx.x) + 0.5f) / static_cast<float>(dim.x) - 1.0f;
	const float ndcY = 1.0f - 2.0f * (static_cast<float>(idx.y) + 0.5f) / static_cast<float>(dim.y);

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

	const float3 result = traceRadiance(rayOrigin, rayDirection, 0);
	params.image[idx.y * params.imageWidth + idx.x] = toColor(result);
}

extern "C" __global__ void __miss__ms()
{
	if (optixGetRayFlags() & OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT)
	{
		// Shadow ray reached the light with nothing in the way - unoccluded.
		optixSetPayload_0(0u);
		return;
	}

	const unsigned int depth = optixGetPayload_3();
	const float3 dir = optixGetWorldRayDirection();

	if (depth == 0)
	{
		// Matches CpuPathTracer::tracePixel()'s screenUv convention exactly
		// (top of image = 1, bottom = 0) - only used by the fallback
		// gradient when no skybox is shown.
		const uint3 idx = optixGetLaunchIndex();
		const uint3 dimLaunch = optixGetLaunchDimensions();
		const float su = (static_cast<float>(idx.x) + 0.5f) / static_cast<float>(dimLaunch.x);
		const float sv = 1.0f - (static_cast<float>(idx.y) + 0.5f) / static_cast<float>(dimLaunch.y);
		setPayload(sampleEnvironmentBackground(params.environment, dir, su, sv));
	}
	else
	{
		setPayload(sampleEnvironmentReflection(params.environment, dir));
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
	const unsigned int depth = optixGetPayload_3();

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

	// View direction and the simplified (no KHR_materials_ior/specular) F0 -
	// mirrors CpuPathTracer's own default: dielectrics get the standard
	// 0.04 reflectance, metals use their own baseColor as F0, F90 = 1.0.
	const float3 V = normalizeF3(rayDir * -1.0f);
	const float NdotV = fmaxf(dot3(worldNormal, V), 0.0f);
	const float3 F0 = lerp3(make_float3(0.04f, 0.04f, 0.04f), data->baseColor, data->metalness);
	const float3 F90 = make_float3(1.0f, 1.0f, 1.0f);
	const float roughness = fmaxf(data->roughness, 0.03f); // avoid a singular perfect mirror (alpha=0), matching CpuPathTracer's own floor

	float3 radiance = data->emissive * data->emissiveStrength;
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

			const float3 kD = (make_float3(1.0f, 1.0f, 1.0f) - F) * (1.0f - data->metalness);
			const float3 diffuse = kD * data->baseColor * (1.0f / kPi);

			radiance = radiance + (diffuse + specular) * lightIntensity * NdotL;
		}
	}

	// Single mirror reflection bounce - only from a primary hit (depth==0),
	// so a reflected ray hitting ANOTHER reflective surface just shows that
	// surface's direct lighting only, never a third-level reflection (keeps
	// recursion bounded at exactly one extra optixTrace call, matching
	// pipelineLinkOptions.maxTraceDepth == 2 in RtOptixSceneTracer.cpp).
	// Weighted by the same Fresnel term (at the primary viewing angle) that
	// already governs how much of this surface's response is specular vs
	// diffuse - a crude single-bounce specular-IBL approximation, not the
	// roughness-blurred prefilter sampling the CPU tracer eventually uses.
	if (depth == 0 && NdotV > 0.0f)
	{
		const float3 reflectDir = normalizeF3(reflectF3(rayDir, worldNormal));
		const float3 reflection = traceRadiance(worldPos, reflectDir, 1);
		const float3 Fview = fresnelSchlick(NdotV, F0, F90);
		radiance = radiance + reflection * Fview;
	}

	setPayload(radiance);
}
