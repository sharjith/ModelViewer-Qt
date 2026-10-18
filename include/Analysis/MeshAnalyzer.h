#pragma once
#include <glm/glm.hpp>
#include <optional>
#include <iostream>
#include <algorithm>
#undef max
#undef min

// Forward declaration
struct aiMesh;

namespace MeshAnalysis
{
    enum class SurfaceType : uint8_t
    {
        PLANAR,
        CYLINDRICAL,
        SPHERICAL,
        MIXED
    };

    struct BoundingBox
    {
        glm::vec3 min;
        glm::vec3 max;

        glm::vec3 getCenter() const { return (min + max) * 0.5f; }
        glm::vec3 getSize() const { return max - min; }
        float getMaxDimension() const
        {
            const auto size = getSize();
            return std::max({ size.x, size.y, size.z });
        }
        float getMinDimension() const
        {
            const auto size = getSize();
            return std::min({ size.x, size.y, size.z });
        }
    };

    struct AnalysisResult
    {
        SurfaceType surfaceType = SurfaceType::MIXED;
        BoundingBox boundingBox;
        glm::vec3 centroid{ 0.0f };
        std::optional<glm::vec3> dominantAxis;  // For cylindrical surfaces
        std::optional<float> avgRadius;         // For spherical surfaces
        bool hasUniformNormals = false;

        // Confidence metrics
        float typeConfidence = 0.0f;  // 0.0 to 1.0
        uint32_t samplesUsed = 0;
    };

    struct SamplingConfig
    {
        uint32_t maxSamples = 100;
        float minSampleRatio = 0.1f;  // Minimum 10% of vertices
        float maxSampleRatio = 1.0f;  // Maximum 100% of vertices

        // Thresholds for surface type detection
        float sphericalAspectRatio = 0.8f;
        float cylindricalAspectRatio = 2.0f;
        float normalConsistencyThreshold = 0.8f;
        float radialNormalThreshold = 0.7f;
        float perpendicularNormalThreshold = 0.3f;
    };
}

class MeshAnalyzer
{
public:
    static MeshAnalysis::AnalysisResult analyzeMesh(
        const aiMesh* mesh,
        const MeshAnalysis::SamplingConfig& config = {}
    );

private:
    // Core analysis methods
    static MeshAnalysis::BoundingBox calculateBoundingBox(const aiMesh* mesh);
    static glm::vec3 calculateCentroid(const aiMesh* mesh);

    // Surface type detection
    static MeshAnalysis::SurfaceType determineSurfaceType(
        const aiMesh* mesh,
        const MeshAnalysis::BoundingBox& bbox,
        const glm::vec3& centroid,
        const MeshAnalysis::SamplingConfig& config,
        MeshAnalysis::AnalysisResult& result
    );

    // Specialized surface analysis
    static bool analyzeSphericality(
        const aiMesh* mesh,
        const glm::vec3& centroid,
        const MeshAnalysis::SamplingConfig& config,
        MeshAnalysis::AnalysisResult& result
    );

    static bool analyzeCylindricity(
        const aiMesh* mesh,
        const MeshAnalysis::BoundingBox& bbox,
        const MeshAnalysis::SamplingConfig& config,
        MeshAnalysis::AnalysisResult& result
    );

    static bool analyzePlanarity(
        const aiMesh* mesh,
        const MeshAnalysis::SamplingConfig& config,
        MeshAnalysis::AnalysisResult& result
    );

    // Utility methods
    static glm::vec3 getDominantAxis(const MeshAnalysis::BoundingBox& bbox);
    static float calculateAverageRadius(const aiMesh* mesh, const glm::vec3& center);
    static bool checkUniformNormals(const aiMesh* mesh, float threshold = 0.8f);

    // Sampling utilities
    static uint32_t calculateSampleCount(uint32_t vertexCount, const MeshAnalysis::SamplingConfig& config);
    static uint32_t calculateSampleStep(uint32_t vertexCount, uint32_t sampleCount);

    // Validation
    static bool validateMesh(const aiMesh* mesh);
    static glm::vec3 safeNormalize(const glm::vec3& vec, const glm::vec3& fallback = glm::vec3(0.0f, 1.0f, 0.0f));
};
