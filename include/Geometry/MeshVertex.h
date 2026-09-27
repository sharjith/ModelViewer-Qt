#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vector>

// Interleaved per-vertex data used by SceneMesh for meshopt optimisation,
// morph-target application, and GPU upload.  Stored as _vertices and
// _baseVertices in SceneMesh; split into separate float arrays in
// initBuffers() for the GPU buffers.
struct Vertex
{
	// Vertex Color
	glm::vec4 Color;
	// Position
	glm::vec3 Position;
	// Normal
	glm::vec3 Normal;
	// tangent
	glm::vec3 Tangent;
	// bitangent
	glm::vec3 Bitangent;
	// TexCoords
	glm::vec2 TexCoords[4];
	// Skinning
	glm::vec4 JointIndices = glm::vec4(0.0f);
	glm::vec4 JointWeights = glm::vec4(0.0f);
};

inline glm::vec2 getTexCoord(const Vertex& v, int index = 0)
{
	return (index >= 0 && index < 4) ? v.TexCoords[index] : glm::vec2(0.0f);
}

static_assert(sizeof(Vertex) == sizeof(float) * (4 + 3 + 3 + 3 + 3 + 8 + 4 + 4),
	"Vertex struct has unexpected padding - meshopt stride will be incorrect");

// Per-morph-target position/normal/tangent deltas (CPU-side only).
// Stored in RenderableMesh::_morphTargets and consumed by
// SceneMesh::applyMorphWeights() to blend _baseVertices into _vertices.
struct MorphTargetData
{
	std::vector<glm::vec3> positionDeltas;
	std::vector<glm::vec3> normalDeltas;
	std::vector<glm::vec3> tangentDeltas;
};
