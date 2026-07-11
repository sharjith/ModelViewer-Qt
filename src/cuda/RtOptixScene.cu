// ---------------------------------------------------------------------------
// RtOptixScene.cu - Phase 2a kernel for the GPU (OptiX) path tracer backend.
//
// Renders the app's real scene geometry (via a real GAS-per-mesh/IAS-per-
// instance acceleration structure - see RtOptixSceneTracer.cpp) through the
// real RtCamera projection, shading each hit as its flat object-space
// triangle normal transformed to world space and visualized as color. No
// materials, lights, textures, or bounces yet - see RtOptixSceneParams.h's
// doc comment for why this scope is deliberate. Self-contained, same style
// as RtOptixTriangle.cu (no dependency on the OptiX SDK's bundled sutil).
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

	__forceinline__ __device__ float3 cross3(const float3& a, const float3& b)
	{
		return make_float3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
	}

	__forceinline__ __device__ float3 normalizeF3(const float3& v)
	{
		const float invLen = rsqrtf(fmaxf(v.x * v.x + v.y * v.y + v.z * v.z, 1e-20f));
		return v * invLen;
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
	const float3 v0 = data->positions[tri.x];
	const float3 v1 = data->positions[tri.y];
	const float3 v2 = data->positions[tri.z];

	// Flat per-triangle object-space normal (mirrors RtEmbreeScene::
	// intersect()'s localGeometricNormal) - transformed to world space using
	// the CURRENT instance's transform, which OptiX already knows from IAS
	// traversal (no need to duplicate the matrix in the SBT record).
	const float3 objectNormal = cross3(v1 - v0, v2 - v0);
	const float3 worldNormal = normalizeF3(optixTransformNormalFromObjectToWorldSpace(objectNormal));

	// Faceforward against the ray so back-facing triangles don't just look
	// like a flipped, differently-colored front face - matches this being a
	// "does the geometry/instancing/camera pipeline work" visualization, not
	// real shading.
	const float3 rayDir = optixGetWorldRayDirection();
	const float3 shadingNormal = (worldNormal.x * rayDir.x + worldNormal.y * rayDir.y + worldNormal.z * rayDir.z > 0.0f)
		? worldNormal * -1.0f
		: worldNormal;

	setPayload(shadingNormal * 0.5f + make_float3(0.5f, 0.5f, 0.5f));
}
