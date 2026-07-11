// ---------------------------------------------------------------------------
// RtOptixTriangle.cu - Phase 1b test kernel for the GPU (OptiX) path tracer
// backend.
//
// Deliberately self-contained (no dependency on the OptiX SDK's bundled
// sutil/cuda/helpers.h, which this project doesn't vendor) - just enough
// float3 arithmetic to build a camera ray and shade a hit. Renders one
// hardcoded triangle: closest-hit shades its barycentric coordinates as a
// color, miss returns a flat background color. This exists purely to prove
// optixTrace()/closest-hit/miss dispatch and the host<->device readback path
// work end-to-end - see RtOptixTracer.cpp for the host side and
// RtOptixContext for the device/context setup this builds on.
// ---------------------------------------------------------------------------
#include <optix.h>

#include "RtOptixTriangleParams.h"

extern "C" {
__constant__ RtOptixTriangleParams params;
}

namespace
{
	__forceinline__ __device__ float3 operator+(const float3& a, const float3& b)
	{
		return make_float3(a.x + b.x, a.y + b.y, a.z + b.z);
	}

	__forceinline__ __device__ float3 operator*(const float3& a, float s)
	{
		return make_float3(a.x * s, a.y * s, a.z * s);
	}

	__forceinline__ __device__ float3 normalizeF3(const float3& v)
	{
		const float invLen = rsqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
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

	const float2 d = make_float2(
		2.0f * static_cast<float>(idx.x) / static_cast<float>(dim.x) - 1.0f,
		2.0f * static_cast<float>(idx.y) / static_cast<float>(dim.y) - 1.0f);

	const float3 rayOrigin = params.camEye;
	const float3 rayDirection = normalizeF3(params.camU * d.x + params.camV * d.y + params.camW);

	unsigned int p0, p1, p2;
	optixTrace(
		params.handle,
		rayOrigin,
		rayDirection,
		0.0f,   // tmin
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
	setPayload(make_float3(0.1f, 0.1f, 0.15f)); // flat dark-blue background, matches nothing in particular
}

extern "C" __global__ void __closesthit__ch()
{
	// Built-in triangle intersection supplies barycentric coordinates
	// directly - shading them as RGB is the standard "did this actually
	// hit and interpolate correctly" smoke test.
	const float2 barycentrics = optixGetTriangleBarycentrics();
	setPayload(make_float3(barycentrics.x, barycentrics.y, 1.0f - barycentrics.x - barycentrics.y));
}
