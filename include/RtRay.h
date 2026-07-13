#pragma once

#include <cstdint>
#include <limits>

#include <glm/glm.hpp>

// ---------------------------------------------------------------------------
// RtRay / RtHit
//
// Plain, backend-agnostic ray/hit types used at the CpuPathTracer <-> Ray
// intersector boundary. Kept free of any Embree types so CpuPathTracer's BSDF
// bounce-loop code (task 4) doesn't need to touch the Embree API directly -
// only RtEmbreeScene.cpp does.
// ---------------------------------------------------------------------------

struct RtRay
{
	glm::vec3 origin{ 0.0f };
	glm::vec3 direction{ 0.0f, 0.0f, -1.0f };
	float     tNear = 1e-4f; // small epsilon, avoids self-intersection at the origin
	float     tFar  = std::numeric_limits<float>::max();

	// Embree ray mask (bitwise-ANDed against each instance's geometry mask -
	// see RtEmbreeScene::build()'s rtcSetGeometryMask(), one bit per
	// instance index mod 32) - all bits set (every instance intersectable)
	// by default. Used to implement the Visualization panel's "Self
	// Shadows" toggle: a shadow ray can clear the CURRENT shading
	// instance's own bit to test occlusion against every OTHER instance
	// while ignoring self-occlusion, without needing a real any-hit filter
	// callback - see CpuPathTracer.cpp's NEE shadow-ray construction.
	uint32_t  mask  = 0xFFFFFFFFu;
};

struct RtHit
{
	bool      hit      = false;
	float     distance = 0.0f;
	glm::vec3 position{ 0.0f };
	glm::vec3 normal{ 0.0f, 1.0f, 0.0f };          // smooth-shaded (vertex-interpolated), world-space
	glm::vec3 geometricNormal{ 0.0f, 1.0f, 0.0f }; // flat per-triangle normal, world-space - used for
	                                                // numerically robust ray-offsetting (see CpuPathTracer.cpp)
	glm::vec2 texCoords[4]{};                      // all 4 UV channels - a texture's declared
	                                                // texCoordIndex can reference any of them
	glm::vec4 vertexColor{ 1.0f };                 // interpolated COLOR_0 (or identity if absent)

	// Interpolated, world-space (NOT inverse-transposed - tangents run along
	// the surface, they transform with the plain model matrix, unlike
	// normals). Zero-length when the mesh has no tangent data - see
	// RtVertex::tangent.
	glm::vec3 tangent{ 0.0f };
	glm::vec3 bitangent{ 0.0f };

	uint32_t  instanceIndex = 0;
	uint32_t  materialIndex = 0;

	// Texel-density proxy for texture-minification LOD selection - the hit
	// triangle's world-space area divided into its UV(channel 0)-space area
	// (units: UV^2 per world-unit^2), computed once in RtEmbreeScene::
	// intersect() from the triangle's raw (pre-interpolation) vertex data.
	// Deliberately resolution-independent (no specific texture's width/
	// height baked in, since different textures on the same material can
	// have different native resolutions) - CpuPathTracer.cpp's
	// computeTextureLod() combines this with a specific texture's own
	// dimensions and the camera's per-pixel world-space footprint at this
	// hit to pick a mip level. 0 (the default) means "no LOD info available
	// / not computed for this hit" - sampleTexture() then falls back to its
	// existing base-level-only behavior, same as before this field existed.
	float     uvAreaPerWorldArea = 0.0f;
};
