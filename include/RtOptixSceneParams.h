#pragma once

#include <optix_types.h> // OptixTraversableHandle

// ---------------------------------------------------------------------------
// RtOptixSceneParams / RtOptixSceneHitGroupData
//
// Shared between RtOptixSceneTracer.cpp (host) and src/cuda/RtOptixScene.cu
// (device) - Phase 2a of the GPU path tracer backend. Unlike Phase 1b's
// RtOptixTriangleParams (one hardcoded triangle), this renders the app's
// REAL scene geometry through a real two-level acceleration structure (GAS
// per RtMeshGeometry, IAS with one OptixInstance per RtInstance - mirrors
// RtEmbreeScene's BLAS/TLAS structure exactly), using the real RtCamera.
// Shading is deliberately still just a flat per-triangle object-space normal
// (transformed to world space via optixTransformNormalFromObjectToWorldSpace()
// in the closest-hit program) visualized as color - no materials, lights, or
// bounces yet. The point of this checkpoint is proving real multi-mesh/
// multi-instance geometry upload, instance transforms, and the real camera
// projection all work correctly before any shading/material complexity is
// layered on top.
// ---------------------------------------------------------------------------
struct RtOptixSceneParams
{
	uchar4* image;
	unsigned int imageWidth;
	unsigned int imageHeight;

	float3 camPosition;
	float3 camForward;
	float3 camRight;
	float3 camUp;
	float camAspectRatio;
	int camOrthographic; // bool as int - POD-safe across the host/device boundary
	float camTanHalfFovY;     // perspective only
	float camOrthoHalfHeight; // orthographic only

	OptixTraversableHandle handle;
};

// One per RtInstance in the SBT (see RtOptixSceneTracer.cpp's SBT build) -
// gives the closest-hit program that instance's own mesh geometry to fetch
// vertex positions from (OptiX's built-in triangle intersection supplies
// barycentrics and the primitive index, but NOT vertex attribute data - the
// same reason RtEmbreeScene::intersect() fetches vertices from its own
// mesh.vertices array by hand).
struct RtOptixSceneHitGroupData
{
	float3* positions; // object-space, indexed via `indices` below
	uint3* indices;    // one uint3 per triangle - matches RtMeshGeometry's flat uint32 triple layout
};
