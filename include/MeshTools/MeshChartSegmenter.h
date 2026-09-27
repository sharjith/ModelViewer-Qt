#pragma once
#include <assimp/mesh.h>
#include <vector>
#include <glm/glm.hpp>

struct MeshChart
{
    std::vector<glm::vec3> positions;
    std::vector<unsigned int> indices;
};

std::vector<MeshChart> splitByNormalDeviation(const aiMesh* mesh, float maxAngleDegrees);
