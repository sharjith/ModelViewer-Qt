// ---------------------------------------------------------------------------
// RtOptixMorph.cu
//
// Plain per-vertex GPU morph-target blending compute kernel - deliberately
// NOT an OptiX pipeline kernel (no optix.h, no raygen/closesthit/miss, no
// optixLaunch()), same as RtOptixSkinning.cu. Compiled to PTX via the exact
// same CMakeLists.txt add_optix_kernel() machinery, LOADED and LAUNCHED via
// the CUDA Driver API (cuModuleLoadDataEx()/cuModuleGetFunction()/
// cuLaunchKernel(), <cuda.h>) from RtOptixSceneTracer.cpp - see
// RtOptixSkinning.cu's own doc comment for the full rationale, identical
// here.
//
// Mirrors SceneMesh::applyMorphWeights()'s CPU blend formula exactly: full
// weighted-sum-of-deltas + rest-pose base (no weight-sum clamping), per
// target skipped when |weight| <= 0.0001. Normal/tangent are renormalized
// only if actually changed by some target's delta, exactly like the CPU
// reference's normalChanged/tangentChanged bookkeeping - see this file's
// tangent-handedness handling below for why that bookkeeping is preserved
// bit-for-bit rather than simplified away.
// ---------------------------------------------------------------------------
#include "RtOptixMorphParams.h"

extern "C" {
__constant__ RtOptixMorphParams params;
}

namespace
{
	__forceinline__ __device__ float3 add(const float3& a, const float3& b) { return make_float3(a.x + b.x, a.y + b.y, a.z + b.z); }
	__forceinline__ __device__ float3 scale(const float3& a, float s) { return make_float3(a.x * s, a.y * s, a.z * s); }
	__forceinline__ __device__ float lengthOf(const float3& a) { return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }
	__forceinline__ __device__ float3 normalizeOrZero(const float3& a)
	{
		const float len = lengthOf(a);
		return len > 0.0001f ? scale(a, 1.0f / len) : a;
	}
}

extern "C" __global__ void morphVertices()
{
	const unsigned int i = blockIdx.x * blockDim.x + threadIdx.x;
	if (i >= params.vertexCount)
		return;

	float3 position = params.basePositions[i];
	float3 normal = params.baseNormals[i];
	float3 tangent = params.baseTangents[i];
	bool normalChanged = false;
	bool tangentChanged = false;

	for (unsigned int t = 0; t < params.morphTargetCount; ++t)
	{
		const float weight = params.weights[t];
		if (weight <= 0.0001f && weight >= -0.0001f)
			continue;

		const size_t idx = static_cast<size_t>(t) * params.vertexCount + i;
		if (params.hasPositionDeltas[t])
			position = add(position, scale(params.deltaPositions[idx], weight));
		if (params.hasNormalDeltas[t])
		{
			normal = add(normal, scale(params.deltaNormals[idx], weight));
			normalChanged = true;
		}
		if (params.hasTangentDeltas[t])
		{
			tangent = add(tangent, scale(params.deltaTangents[idx], weight));
			tangentChanged = true;
		}
	}

	if (normalChanged)
		normal = normalizeOrZero(normal);
	if (tangentChanged)
		tangent = normalizeOrZero(tangent);

	params.outPositions[i] = position;
	params.outNormals[i] = normal;

	// Tangent handedness: only overwrite (to the CPU reference's implicit
	// +1 convention - see SceneMesh::applyMorphWeights(), which recomputes
	// Bitangent as cross(normal, tangent) whenever normal or tangent
	// actually changed) when this frame's blend fired for this vertex.
	// Otherwise leave whatever handedness is already resident in
	// outTangents[i].w untouched - it was seeded correctly by whichever
	// full/CPU-bake build wrote this mesh's tangent buffer before the GPU
	// path first engaged (same device-buffer-read-back trick
	// RtOptixSkinning.cu uses), and must survive frames where morphing
	// doesn't touch this vertex's normal/tangent at all (e.g. all weights
	// ~0, or a position-only target). Precomputing a single handedness
	// value once and reusing it forever - the way RtOptixSkinning.cu
	// safely does for rigid joints - is NOT valid here: morph deltas are
	// an arbitrary per-vertex blend,
	// not a rigid rotation, so a stale precomputed sign could be wrong the
	// instant normal/tangent are actually perturbed.
	if (normalChanged || tangentChanged)
		params.outTangents[i] = make_float4(tangent.x, tangent.y, tangent.z, 1.0f);
	else
		params.outTangents[i] = make_float4(tangent.x, tangent.y, tangent.z, params.outTangents[i].w);
}
