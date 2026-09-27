
#pragma once
#include "MeshAnalyzer.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>
#include <cmath>
#include "SceneMesh.h"

class TangentGenerator
{
public:
    static void generateMikkTSpaceTangentsForMesh(
        std::vector<Vertex>& vertices, 
        const std::vector<unsigned int>& indices);

    static void generateTangentsForMesh(
        std::vector<Vertex>& vertices,
        const std::vector<uint32_t>& indices,
        const MeshAnalysis::AnalysisResult& analysis
    );

    static void generateTangentsForSurface(
        Vertex& vertex,
        const MeshAnalysis::AnalysisResult& analysis
    );

private:
    static void generateSphericalTangents(
        Vertex& vertex,
        const glm::vec3& center
    );

    static void generateCylindricalTangents(
        Vertex& vertex,
        const glm::vec3& axis
    );

    static void generatePlanarTangents(
        Vertex& vertex
    );

    // Traditional tangent calculation for mixed surfaces
    static void calculateTangentsFromTriangles(
        std::vector<Vertex>& vertices,
        const std::vector<uint32_t>& indices
    );

    // Utility functions
    static glm::vec3 orthogonalize(const glm::vec3& vec, const glm::vec3& normal);
    static void gramSchmidt(glm::vec3& tangent, glm::vec3& bitangent, const glm::vec3& normal);
};