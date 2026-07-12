// ---------------------------------------------------------------------------
// RtOptixScene.cu - Phase 2c kernel for the GPU (OptiX) path tracer backend.
//
// Renders the app's real scene geometry (via a real GAS-per-mesh/IAS-per-
// instance acceleration structure - see RtOptixSceneTracer.cpp) through the
// real RtCamera projection, shaded with real flat material colors
// (baseColor/metalness/roughness/emissive - no textures yet) and basic
// direct lighting: KHR_lights_punctual attenuation plus the full metallic-
// roughness Cook-Torrance BRDF (GGX distribution, Smith-Schlick geometry,
// Fresnel), both ported verbatim from CpuPathTracer's evaluatePunctualLight()/
// evaluateDirectBRDF()/distributionGGX()/geometrySmith()/fresnelSchlick() so
// this matches the CPU tracer's own direct-lighting term exactly (same F0
// simplification as the CPU tracer's non-KHR_materials_ior/specular default:
// F0 = mix(0.04, baseColor, metalness), F90 = 1.0). Still no shadow rays/
// occlusion, no reflection/refraction bounces, no textures, no anisotropy/
// clearcoat/sheen/transmission/iridescence - see RtOptixSceneParams.h's doc
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
		denom = 3.14159265f * denom * denom;
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

	unsigned int p0, p1, p2;
	optixTrace(
		params.handle,
		rayOrigin,
		rayDirection,
		1e-4f,  // tmin
		1e16f,  // tmax
		0.0f,   // rayTime
		OptixVisibilityMask(255),
		OPTIX_RAY_FLAG_NONE,
		0, // SBT offset
		1, // SBT stride
		0, // missSBTIndex
		p0, p1, p2);

	const float3 result = make_float3(__uint_as_float(p0), __uint_as_float(p1), __uint_as_float(p2));
	params.image[idx.y * params.imageWidth + idx.x] = toColor(result);
}

extern "C" __global__ void __miss__ms()
{
	setPayload(make_float3(0.08f, 0.08f, 0.1f)); // flat dark background
}

extern "C" __global__ void __closesthit__ch()
{
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

			const float3 H = normalizeF3(V + lightDir);
			const float NdotH = fmaxf(dot3(worldNormal, H), 0.0f);
			const float VdotH = fminf(fmaxf(dot3(H, V), 0.0f), 1.0f);

			const float3 F = fresnelSchlick(VdotH, F0, F90);
			const float D = distributionGGX(NdotH, roughness);
			const float G = geometrySmith(NdotV, NdotL, roughness);
			const float3 specular = F * (D * G / fmaxf(4.0f * NdotV * NdotL, 0.001f));

			const float3 kD = (make_float3(1.0f, 1.0f, 1.0f) - F) * (1.0f - data->metalness);
			const float3 diffuse = kD * data->baseColor * (1.0f / 3.14159265f);

			radiance = radiance + (diffuse + specular) * lightIntensity * NdotL;
		}
	}

	setPayload(radiance);
}
