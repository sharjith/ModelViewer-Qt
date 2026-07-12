#pragma once

#include <optix_types.h> // OptixTraversableHandle

// ---------------------------------------------------------------------------
// RtOptixSceneParams / RtOptixSceneHitGroupData / RtOptixLight
//
// Shared between RtOptixSceneTracer.cpp (host) and src/cuda/RtOptixScene.cu
// (device) - GPU path tracer backend, real scene geometry via a real two-
// level acceleration structure (GAS per RtMeshGeometry, IAS with one
// OptixInstance per RtInstance - mirrors RtEmbreeScene's BLAS/TLAS structure
// exactly), using the real RtCamera. Phase 2b adds real flat material colors
// (baseColor/metalness/roughness/emissive, no textures yet) and basic direct
// lighting (KHR_lights_punctual attenuation ported verbatim from
// CpuPathTracer::evaluatePunctualLight(), Lambertian diffuse only - no
// shadow rays/occlusion, no specular/roughness response, no bounces/GI yet -
// each deferred to a later increment so this stays independently
// verifiable against the CPU tracer's own direct-lighting-only result).
// ---------------------------------------------------------------------------
struct RtOptixLight
{
	int type; // matches RtLight::type: 0=Directional, 1=Point, 2=Spot
	float3 position;
	float3 direction;
	float3 color;
	float intensity;
	float range;
	float innerConeCos;
	float outerConeCos;
};

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

	const RtOptixLight* lights;
	unsigned int lightCount;

	OptixTraversableHandle handle;
};

// One per RtInstance in the SBT (see RtOptixSceneTracer.cpp's SBT build) -
// gives the closest-hit program that instance's own mesh geometry/material
// to shade with (OptiX's built-in triangle intersection supplies
// barycentrics and the primitive index, but NOT vertex attribute data - the
// same reason RtEmbreeScene::intersect() fetches vertices from its own
// mesh.vertices array by hand).
struct RtOptixSceneHitGroupData
{
	float3* normals; // object-space, per-vertex, indexed via `indices` below
	uint3* indices;  // one uint3 per triangle - matches RtMeshGeometry's flat uint32 triple layout

	float3 baseColor;
	float metalness;
	float roughness;
	float3 emissive;
	float emissiveStrength;
};
