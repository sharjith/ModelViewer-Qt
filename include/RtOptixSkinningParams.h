#pragma once

#include <vector_types.h> // float3, float4 - CUDA's own header, deliberately NOT optix_types.h (see RtOptixSkinning.cu's doc comment for why this isn't an OptiX kernel)

// ---------------------------------------------------------------------------
// RtOptixSkinningParams
//
// Shared between RtOptixSceneTracer.cpp (host, CUDA Driver-API launch) and
// src/cuda/RtOptixSkinning.cu (device) - a plain per-vertex GPU-skinning
// compute kernel, NOT an OptiX pipeline kernel (see that .cu file's own doc
// comment for why it's launched differently from RtOptixScene.cu despite
// reusing the same PTX-embed CMake machinery).
//
// One kernel launch skins ONE mesh's vertices (one CUDA thread per vertex) -
// mirrors RtSceneBuilder::convertGeometry()'s CPU skin-blend formula exactly
// (4-bone blend, no weight renormalization per glTF's WEIGHTS_0-sums-to-1
// convention, no inverse-transpose for normals/tangents - joints are rigid,
// rotation+translation only, same reasoning as the CPU path). base*/joint*
// are device pointers the caller has already uploaded once and reuses across
// frames (see RtOptixSceneTracer::Impl's persistent skin-base cache doc
// comment) - out* are the SAME device buffers the mesh's GAS refit consumes
// directly afterward, so there is no host round trip anywhere in this path.
// ---------------------------------------------------------------------------
struct RtOptixSkinningParams
{
	const float3* basePositions; // bind-pose, length vertexCount
	const float3* baseNormals;
	const float3* baseTangents;
	const float4* jointIndices;  // length vertexCount
	const float4* jointWeights;

	// Row-major-by-column layout: 4 consecutive float4 entries per joint
	// (matching glm::mat4's own column-major memory layout byte-for-byte),
	// so the host side can upload a std::vector<glm::mat4> directly with no
	// repacking. jointPalette[joint*4 + col] is that joint's matrix's column
	// `col` (xyz = rotation/translation basis, w unused for col0-2, =1 for
	// col3's translation row in an affine 4x4).
	const float4* jointPalette;
	unsigned int  jointCount;

	unsigned int vertexCount;

	float3* outPositions;
	float3* outNormals;
	// float4, NOT float3 - matches RtOptixSceneTracer::Impl::MeshGas::tangents'
	// actual buffer layout exactly (tangent direction in xyz, PRECOMPUTED
	// handedness sign in w - see RtMeshGeometry::tangentHandedness's doc
	// comment for why the kernel must preserve, never recompute, that w).
	float4* outTangents;
};
