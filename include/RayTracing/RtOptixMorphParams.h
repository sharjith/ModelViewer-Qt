#pragma once

#include <vector_types.h> // float3, float4 - CUDA's own header, deliberately NOT optix_types.h (see RtOptixMorph.cu's doc comment for why this isn't an OptiX kernel)

// ---------------------------------------------------------------------------
// RtOptixMorphParams
//
// Shared between RtOptixSceneTracer.cpp (host, CUDA Driver-API launch) and
// src/cuda/RtOptixMorph.cu (device) - a plain per-vertex GPU morph-target
// blending compute kernel, NOT an OptiX pipeline kernel, mirroring
// RtOptixSkinningParams.h's own role for skinning exactly.
//
// One kernel launch blends ONE mesh's vertices (one CUDA thread per vertex) -
// mirrors SceneMesh::applyMorphWeights()'s CPU blend formula exactly: full
// weighted-sum-of-deltas + rest-pose base, no weight-sum clamping. base*/
// delta*/weights are device pointers the caller has already uploaded (base*
// and delta* once, persistent - see RtOptixSceneTracer::Impl's persistent
// morph-base cache doc comment; weights fresh whenever they change, small) -
// out* are the SAME device buffers the mesh's GAS refit consumes directly
// afterward, so there is no host round trip anywhere in this path.
// ---------------------------------------------------------------------------
struct RtOptixMorphParams
{
	const float3* basePositions; // rest pose, length vertexCount
	const float3* baseNormals;
	const float3* baseTangents;

	// Flattened target-major (target * vertexCount + i) - mirrors
	// RtMeshGeometry::morphTargetDeltaPositions/Normals/Tangents' layout
	// verbatim (RtSceneSnapshot.h). Length morphTargetCount * vertexCount.
	const float3* deltaPositions;
	const float3* deltaNormals;
	const float3* deltaTangents;

	// Per-target presence flags (length morphTargetCount) - a target's
	// delta array is either fully present or entirely absent for a given
	// attribute (never partial), mirroring SceneMesh::applyMorphWeights()'s
	// own per-attribute size checks. MUST be consulted rather than trusting
	// a zero delta at face value - see RtMeshGeometry::morphTargetHas*Deltas'
	// doc comment (RtSceneSnapshot.h) for why.
	const unsigned char* hasPositionDeltas;
	const unsigned char* hasNormalDeltas;
	const unsigned char* hasTangentDeltas;

	const float* weights; // length morphTargetCount
	unsigned int morphTargetCount;

	unsigned int vertexCount;

	float3* outPositions;
	float3* outNormals;
	// float4, NOT float3 - matches RtOptixSceneTracer::Impl::MeshGas::tangents'
	// actual buffer layout exactly (tangent direction in xyz, handedness
	// sign in w), same as RtOptixSkinningParams::outTangents. UNLIKE
	// skinning, this kernel does NOT unconditionally preserve the existing
	// w on every launch - see RtOptixMorph.cu's doc comment: it only
	// overwrites w (to a fixed +1) when this frame's blend actually changes
	// normal/tangent, matching SceneMesh::applyMorphWeights()'s own
	// normalChanged/tangentChanged-gated bitangent recompute exactly.
	float4* outTangents;
};
