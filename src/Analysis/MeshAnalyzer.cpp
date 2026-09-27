#include "MeshAnalyzer.h"
#include <assimp/mesh.h>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <vector>

MeshAnalysis::AnalysisResult MeshAnalyzer::analyzeMesh(
    const aiMesh* mesh,
    const MeshAnalysis::SamplingConfig& config)
{

    MeshAnalysis::AnalysisResult result;

    if (!validateMesh(mesh))
    {
        return result; // Returns default MIXED type with low confidence
    }

    // Calculate basic geometry properties
    result.boundingBox = calculateBoundingBox(mesh);
    result.centroid = calculateCentroid(mesh);
    result.samplesUsed = calculateSampleCount(mesh->mNumVertices, config);

    // Determine surface type through specialized analysis
    result.surfaceType = determineSurfaceType(mesh, result.boundingBox, result.centroid, config, result);

    // Always check for uniform normals
    result.hasUniformNormals = checkUniformNormals(mesh, config.normalConsistencyThreshold);

    return result;
}

MeshAnalysis::BoundingBox MeshAnalyzer::calculateBoundingBox(const aiMesh* mesh)
{
    MeshAnalysis::BoundingBox bbox;
    bbox.min = glm::vec3(FLT_MAX);
    bbox.max = glm::vec3(-FLT_MAX);

    for (uint32_t i = 0; i < mesh->mNumVertices; ++i)
    {
        const auto& vertex = mesh->mVertices[i];
        const glm::vec3 pos(vertex.x, vertex.y, vertex.z);
        bbox.min = glm::min(bbox.min, pos);
        bbox.max = glm::max(bbox.max, pos);
    }

    return bbox;
}

glm::vec3 MeshAnalyzer::calculateCentroid(const aiMesh* mesh)
{
    glm::vec3 centroid(0.0f);

    for (uint32_t i = 0; i < mesh->mNumVertices; ++i)
    {
        const auto& vertex = mesh->mVertices[i];
        centroid += glm::vec3(vertex.x, vertex.y, vertex.z);
    }

    return centroid / static_cast<float>(mesh->mNumVertices);
}


MeshAnalysis::SurfaceType MeshAnalyzer::determineSurfaceType(
    const aiMesh* mesh,
    const MeshAnalysis::BoundingBox& bbox,
    const glm::vec3& centroid,
    const MeshAnalysis::SamplingConfig& config,
    MeshAnalysis::AnalysisResult& result)
{

    // Try planar analysis first for cube-like shapes
    if (analyzePlanarity(mesh, config, result))
    {
        return MeshAnalysis::SurfaceType::PLANAR;
    }

    // Try spherical analysis (with improved logic)
    if (analyzeSphericality(mesh, centroid, config, result))
    {
        return MeshAnalysis::SurfaceType::SPHERICAL;
    }

    // Try cylindrical analysis
    if (analyzeCylindricity(mesh, bbox, config, result))
    {
        return MeshAnalysis::SurfaceType::CYLINDRICAL;
    }

    // Default to mixed
    result.typeConfidence = 0.0f;
    return MeshAnalysis::SurfaceType::MIXED;
}

bool MeshAnalyzer::analyzeSphericality(
    const aiMesh* mesh,
    const glm::vec3& centroid,
    const MeshAnalysis::SamplingConfig& config,
    MeshAnalysis::AnalysisResult& result)
{

    // First check: aspect ratio should be very close to 1.0 for spheres
    const float minDim = result.boundingBox.getMinDimension();
    const float maxDim = result.boundingBox.getMaxDimension();

    if (maxDim == 0.0f || minDim / maxDim < 0.9f)
    {
        return false; // Very strict aspect ratio for spheres
    }

    if (!mesh->mNormals)
    {
        return false;
    }

    const uint32_t sampleCount = result.samplesUsed;
    const uint32_t step = calculateSampleStep(mesh->mNumVertices, sampleCount);

    uint32_t radialCount = 0;
    uint32_t cylindricalCount = 0; // Count normals that could be cylindrical
    float radiusSum = 0.0f;
    float radiusVariance = 0.0f;
    std::vector<float> radii;

    // Determine the dominant axis to check for cylindrical patterns
    const glm::vec3 dominantAxis = getDominantAxis(result.boundingBox);

    // First pass: collect radii and check radial normals
    for (uint32_t i = 0; i < mesh->mNumVertices; i += step)
    {
        const glm::vec3 pos(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z);
        const glm::vec3 normal(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);

        const float radius = glm::distance(pos, centroid);
        radii.push_back(radius);
        radiusSum += radius;

        const glm::vec3 radialDir = safeNormalize(pos - centroid);
        const float radialDot = glm::dot(glm::normalize(normal), radialDir);

        if (radialDot > config.radialNormalThreshold)
        {
            radialCount++;
        }

        // Check if this normal could be from a cylinder (perpendicular to dominant axis)
        const float axialDot = std::abs(glm::dot(glm::normalize(normal), dominantAxis));
        if (axialDot < 0.3f)
        { // Normal is mostly perpendicular to dominant axis
            cylindricalCount++;
        }
    }

    // Check radius uniformity (spheres should have consistent radius)
    const float avgRadius = radiusSum / radii.size();
    for (float radius : radii)
    {
        const float diff = radius - avgRadius;
        radiusVariance += diff * diff;
    }
    radiusVariance /= radii.size();

    const float radiusConsistency = 1.0f - (std::sqrt(radiusVariance) / avgRadius);
    const float radialRatio = static_cast<float>(radialCount) / sampleCount;
    const float cylindricalRatio = static_cast<float>(cylindricalCount) / sampleCount;

    // Reject if too many normals look cylindrical
    if (cylindricalRatio > 0.5f)
    {
        return false;
    }

    // Both radial normals and radius consistency should be very high for spheres
    if (radialRatio > 0.8f && radiusConsistency > 0.9f)
    {
        result.avgRadius = avgRadius;
        result.typeConfidence = std::min(radialRatio, radiusConsistency);
        return true;
    }

    return false;
}

bool MeshAnalyzer::analyzeCylindricity(
    const aiMesh* mesh,
    const MeshAnalysis::BoundingBox& bbox,
    const MeshAnalysis::SamplingConfig& config,
    MeshAnalysis::AnalysisResult& result)
{

    const float minDim = bbox.getMinDimension();
    const float maxDim = bbox.getMaxDimension();

    // Check aspect ratio for cylindrical shape - even 1:1 ratio should be considered
    if (maxDim == 0.0f || maxDim / minDim < 1.0f)
    {
        return false;
    }

    if (!mesh->mNormals)
    {
        return false;
    }

    const glm::vec3 dominantAxis = getDominantAxis(bbox);
    const uint32_t sampleCount = result.samplesUsed;
    const uint32_t step = calculateSampleStep(mesh->mNumVertices, sampleCount);

    uint32_t perpendicularCount = 0;
    uint32_t radialCount = 0;
    uint32_t axialCount = 0; // Count normals parallel to axis (end caps)

    for (uint32_t i = 0; i < mesh->mNumVertices; i += step)
    {
        const glm::vec3 pos(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z);
        const glm::vec3 normal(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);
        const float axialDot = std::abs(glm::dot(glm::normalize(normal), dominantAxis));

        // Check if normal is parallel to cylinder axis (end caps)
        if (axialDot > 0.8f)
        {
            axialCount++;
        }
        // Check if normal is perpendicular to cylinder axis (side surface)
        else if (axialDot < config.perpendicularNormalThreshold)
        {
            perpendicularCount++;

            // For true cylinders, check if perpendicular normals are radial from axis
            const glm::vec3 axisPoint = result.centroid + glm::dot(pos - result.centroid, dominantAxis) * dominantAxis;
            const glm::vec3 radialDir = safeNormalize(pos - axisPoint);

            if (glm::length(pos - axisPoint) > 0.01f)
            {
                const float radialDot = glm::dot(glm::normalize(normal), radialDir);
                if (radialDot > 0.7f)
                {
                    radialCount++;
                }
            }
        }
    }

    const float perpendicularRatio = static_cast<float>(perpendicularCount) / sampleCount;
    const float radialRatio = perpendicularCount > 0 ? static_cast<float>(radialCount) / perpendicularCount : 0.0f;
    const float axialRatio = static_cast<float>(axialCount) / sampleCount;

    // Cylinders should have significant perpendicular normals AND some axial normals (end caps)
    // OR very high radial consistency even without end caps
    if ((perpendicularRatio > 0.4f && radialRatio > 0.6f && axialRatio > 0.1f) ||
        (perpendicularRatio > 0.6f && radialRatio > 0.8f))
    {
        result.dominantAxis = dominantAxis;
        result.typeConfidence = std::min(perpendicularRatio, radialRatio);
        return true;
    }

    return false;
}

bool MeshAnalyzer::analyzePlanarity(
    const aiMesh* mesh,
    const MeshAnalysis::SamplingConfig& config,
    MeshAnalysis::AnalysisResult& result)
{

    if (!mesh->mNormals)
    {
        return false;
    }

    const uint32_t sampleCount = result.samplesUsed;
    const uint32_t step = calculateSampleStep(mesh->mNumVertices, sampleCount);

    // For cube-like shapes, group normals by similarity
    std::vector<glm::vec3> normalGroups;
    std::vector<uint32_t> groupCounts;

    for (uint32_t i = 0; i < mesh->mNumVertices; i += step)
    {
        const glm::vec3 normal = glm::normalize(glm::vec3(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z));

        bool foundGroup = false;
        for (size_t j = 0; j < normalGroups.size(); ++j)
        {
            if (glm::dot(normal, normalGroups[j]) > 0.9f)
            {
                groupCounts[j]++;
                foundGroup = true;
                break;
            }
        }

        if (!foundGroup)
        {
            normalGroups.push_back(normal);
            groupCounts.push_back(1);
        }
    }

    // Check if we have distinct planar faces (like a cube)
    if (normalGroups.size() <= 6)
    { // Max 6 faces for cube
        uint32_t totalGrouped = 0;
        for (uint32_t count : groupCounts)
        {
            totalGrouped += count;
        }

        const float groupedRatio = static_cast<float>(totalGrouped) / sampleCount;
        if (groupedRatio > 0.8f)
        {
            result.typeConfidence = groupedRatio;
            return true;
        }
    }

    // Traditional planar test for single planes
    glm::vec3 avgNormal(0.0f);
    for (uint32_t i = 0; i < mesh->mNumVertices; i += step)
    {
        const glm::vec3 normal(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);
        avgNormal += normal;
    }

    avgNormal = safeNormalize(avgNormal);

    if (glm::length(avgNormal) < 0.1f)
    {
        return false; // Average normal is too small (opposing normals cancel out)
    }

    uint32_t consistentCount = 0;
    for (uint32_t i = 0; i < mesh->mNumVertices; i += step)
    {
        const glm::vec3 normal(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);
        const float consistency = glm::dot(glm::normalize(normal), avgNormal);

        if (consistency > config.normalConsistencyThreshold)
        {
            consistentCount++;
        }
    }

    const float consistencyRatio = static_cast<float>(consistentCount) / sampleCount;
    if (consistencyRatio > 0.8f)
    {
        result.typeConfidence = consistencyRatio;
        return true;
    }

    return false;
}


glm::vec3 MeshAnalyzer::getDominantAxis(const MeshAnalysis::BoundingBox& bbox)
{
    const glm::vec3 size = bbox.getSize();

    if (size.y > size.x && size.y > size.z)
    {
        return glm::vec3(0.0f, 1.0f, 0.0f);
    }
    else if (size.x > size.y && size.x > size.z)
    {
        return glm::vec3(1.0f, 0.0f, 0.0f);
    }
    else
    {
        return glm::vec3(0.0f, 0.0f, 1.0f);
    }
}

float MeshAnalyzer::calculateAverageRadius(const aiMesh* mesh, const glm::vec3& center)
{
    float radiusSum = 0.0f;

    for (uint32_t i = 0; i < mesh->mNumVertices; ++i)
    {
        const glm::vec3 pos(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z);
        radiusSum += glm::distance(pos, center);
    }

    return radiusSum / static_cast<float>(mesh->mNumVertices);
}

bool MeshAnalyzer::checkUniformNormals(const aiMesh* mesh, float threshold)
{
    if (!mesh->mNormals || mesh->mNumVertices < 2)
    {
        return false;
    }

    // Calculate average normal
    glm::vec3 avgNormal(0.0f);
    for (uint32_t i = 0; i < mesh->mNumVertices; ++i)
    {
        const glm::vec3 normal(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);
        avgNormal += normal;
    }

    avgNormal = safeNormalize(avgNormal);

    // Check how many normals are consistent with average
    uint32_t consistentCount = 0;
    for (uint32_t i = 0; i < mesh->mNumVertices; ++i)
    {
        const glm::vec3 normal(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);
        const float consistency = glm::dot(glm::normalize(normal), avgNormal);

        if (consistency > threshold)
        {
            consistentCount++;
        }
    }

    const float consistencyRatio = static_cast<float>(consistentCount) / mesh->mNumVertices;
    return consistencyRatio > 0.8f;
}

uint32_t MeshAnalyzer::calculateSampleCount(uint32_t vertexCount, const MeshAnalysis::SamplingConfig& config)
{
    const uint32_t minSamples = static_cast<uint32_t>(vertexCount * config.minSampleRatio);
    const uint32_t maxSamples = static_cast<uint32_t>(vertexCount * config.maxSampleRatio);

    return std::max(1u, std::min(config.maxSamples, std::min(maxSamples, std::max(minSamples, vertexCount / 10))));
}

uint32_t MeshAnalyzer::calculateSampleStep(uint32_t vertexCount, uint32_t sampleCount)
{
    return std::max(1u, vertexCount / sampleCount);
}

bool MeshAnalyzer::validateMesh(const aiMesh* mesh)
{
    return mesh != nullptr &&
        mesh->mVertices != nullptr &&
        mesh->mNumVertices > 0;
}

glm::vec3 MeshAnalyzer::safeNormalize(const glm::vec3& vec, const glm::vec3& fallback)
{
    const float length = glm::length(vec);
    return (length > 1e-6f) ? vec / length : fallback;
}
