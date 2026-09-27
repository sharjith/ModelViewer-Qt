// ---------------------------------------------------------------------------
// RtOptixSkinning.cu
//
// Plain per-vertex GPU-skinning compute kernel - deliberately NOT an OptiX
// pipeline kernel (no optix.h, no raygen/closesthit/miss, no optixLaunch()).
// Compiled to PTX via the exact same CMakeLists.txt add_optix_kernel()
// machinery RtOptixScene.cu uses (that function is generic nvcc --ptx +
// bin2c embedding, nothing OptiX-specific about it - only a .cu file's own
// contents determine whether it's an OptiX kernel), but LOADED and LAUNCHED
// via the CUDA Driver API (cuModuleLoadDataEx()/cuModuleGetFunction()/
// cuLaunchKernel(), <cuda.h>) from RtOptixSceneTracer.cpp, not the OptiX API -
// see that class's persistent skin-base cache doc comment for why (this
// kernel replaces a CPU joint-palette blend + host-to-device vertex-buffer
// copy that ran every single animation frame; see RtOptixSkinningParams.h's
// own doc comment for the full data-flow picture).
//
// Mirrors RtSceneBuilder::convertGeometry()'s CPU skin-blend formula exactly:
// 4-bone blend, no weight renormalization (glTF's WEIGHTS_0-sums-to-1
// convention), no inverse-transpose for normals/tangents (joints are rigid -
// rotation + translation only, never non-uniform scale).
// ---------------------------------------------------------------------------
#include "RtOptixSkinningParams.h"

extern "C" {
__constant__ RtOptixSkinningParams params;
}

namespace
{
	// Applies a weighted-accumulated affine 4x4 (4 float4 columns, column-
	// major, matching glm::mat4's own memory layout - see
	// RtOptixSkinningParams::jointPalette's doc comment) to a POINT (adds the
	// translation column).
	__forceinline__ __device__ float3 transformPoint(const float4& c0, const float4& c1, const float4& c2, const float4& c3, const float3& p)
	{
		return make_float3(
			c0.x * p.x + c1.x * p.y + c2.x * p.z + c3.x,
			c0.y * p.x + c1.y * p.y + c2.y * p.z + c3.y,
			c0.z * p.x + c1.z * p.y + c2.z * p.z + c3.z);
	}

	// Same, for a DIRECTION (normal/tangent) - rotation-only, no translation
	// column, no inverse-transpose (see this file's own doc comment for why
	// that's correct here: joints are rigid).
	__forceinline__ __device__ float3 transformDirection(const float4& c0, const float4& c1, const float4& c2, const float3& v)
	{
		return make_float3(
			c0.x * v.x + c1.x * v.y + c2.x * v.z,
			c0.y * v.x + c1.y * v.y + c2.y * v.z,
			c0.z * v.x + c1.z * v.y + c2.z * v.z);
	}
}

extern "C" __global__ void skinVertices()
{
	const unsigned int i = blockIdx.x * blockDim.x + threadIdx.x;
	if (i >= params.vertexCount)
		return;

	const float4 jw = params.jointWeights[i];
	const float4 ji = params.jointIndices[i];
	const float jwArr[4] = { jw.x, jw.y, jw.z, jw.w };
	const float jiArr[4] = { ji.x, ji.y, ji.z, ji.w };

	// Weighted-sum the 4 joint matrices first (mirrors CpuPathTracer/
	// RtSceneBuilder's `skin += jointPalette[jointIndex] * weight` exactly),
	// THEN apply the single accumulated matrix once - algebraically
	// identical to blending transformed positions directly, and matches the
	// CPU reference bit-for-bit in intent.
	float4 c0 = make_float4(0.0f, 0.0f, 0.0f, 0.0f);
	float4 c1 = make_float4(0.0f, 0.0f, 0.0f, 0.0f);
	float4 c2 = make_float4(0.0f, 0.0f, 0.0f, 0.0f);
	float4 c3 = make_float4(0.0f, 0.0f, 0.0f, 0.0f);
	float totalWeight = 0.0f;

	for (int b = 0; b < 4; ++b)
	{
		const float weight = jwArr[b];
		if (weight <= 0.0f)
			continue;
		const int jointIndex = static_cast<int>(jiArr[b]);
		if (jointIndex < 0 || jointIndex >= static_cast<int>(params.jointCount))
			continue;

		const float4* joint = params.jointPalette + static_cast<size_t>(jointIndex) * 4;
		c0.x += joint[0].x * weight; c0.y += joint[0].y * weight; c0.z += joint[0].z * weight; c0.w += joint[0].w * weight;
		c1.x += joint[1].x * weight; c1.y += joint[1].y * weight; c1.z += joint[1].z * weight; c1.w += joint[1].w * weight;
		c2.x += joint[2].x * weight; c2.y += joint[2].y * weight; c2.z += joint[2].z * weight; c2.w += joint[2].w * weight;
		c3.x += joint[3].x * weight; c3.y += joint[3].y * weight; c3.z += joint[3].z * weight; c3.w += joint[3].w * weight;
		totalWeight += weight;
	}

	const float3 basePos = params.basePositions[i];
	const float3 baseNormal = params.baseNormals[i];
	const float3 baseTangent = params.baseTangents[i];
	// outTangents' .w (handedness sign) was already correctly set once, on
	// this mesh's FIRST build (see RtMeshGeometry::tangentHandedness's doc
	// comment for why it never needs recomputing) - preserve it, only xyz
	// changes here.
	const float handedness = params.outTangents[i].w;

	if (totalWeight > 0.0f)
	{
		params.outPositions[i] = transformPoint(c0, c1, c2, c3, basePos);
		params.outNormals[i] = transformDirection(c0, c1, c2, baseNormal);
		const float3 tangent = transformDirection(c0, c1, c2, baseTangent);
		params.outTangents[i] = make_float4(tangent.x, tangent.y, tangent.z, handedness);
	}
	else
	{
		// No valid joint influence at all (matches CPU's identical
		// totalWeight<=0 fallback) - bind pose unchanged.
		params.outPositions[i] = basePos;
		params.outNormals[i] = baseNormal;
		params.outTangents[i] = make_float4(baseTangent.x, baseTangent.y, baseTangent.z, handedness);
	}
}
