#include "UVGenerator.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <queue>
#include <utility>
#include <unordered_set>

namespace
{
void buildIdentityVertexMap(size_t vertexCount, std::vector<unsigned int>* sourceVertexMap)
{
    if (!sourceVertexMap)
        return;

    sourceVertexMap->resize(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i)
        (*sourceVertexMap)[i] = static_cast<unsigned int>(i);
}

// Port of Blender's ortho_basis_v3v3_v3 (source/blender/blenlib/intern/math_vector.cc).
// Builds two vectors orthogonal to a unit normal, used as a projection's tangent/bitangent.
void orthoBasisFromNormal(const glm::vec3& n, glm::vec3& r_t, glm::vec3& r_b)
{
    const float eps = std::numeric_limits<float>::epsilon();
    const float f = n.x * n.x + n.y * n.y;

    if (f > eps)
    {
        const float d = 1.0f / std::sqrt(f);
        r_t = glm::vec3(n.y * d, -n.x * d, 0.0f);
        r_b = glm::vec3(-n.z * r_t.y, n.z * r_t.x, n.x * r_t.y - n.y * r_t.x);
    }
    else
    {
        r_t = glm::vec3((n.z < 0.0f) ? -1.0f : 1.0f, 0.0f, 0.0f);
        r_b = glm::vec3(0.0f, 1.0f, 0.0f);
    }
}
}

namespace std
{
    template<>
    struct hash<std::pair<uint32_t, uint32_t>>
    {
        size_t operator()(const std::pair<uint32_t, uint32_t>& p) const
        {
            // Combine the two integers into a 64-bit value
            return std::hash<uint64_t>()(
                (static_cast<uint64_t>(p.first) << 32) | p.second
                );
        }
    };
}


// Method 1: Angle-based unwrapping (most similar to Blender's Smart UV)
bool UVGenerator::generateAngleBased(
    std::vector<Vertex>& vertices,
    std::vector<unsigned int>& indices,
    const UVConfig& config,
    std::vector<unsigned int>* sourceVertexMap)
{
    if (vertices.empty() || indices.empty()) return false;

    // Build triangle list
    std::vector<MeshTriangle> triangles;
    buildTriangleList(vertices, indices, triangles);

    // Find seams based on angle threshold
    std::vector<std::pair<unsigned int, unsigned int>> seams;
    findSeams(vertices, triangles, seams, config.angleThreshold);

    // Create UV islands
    std::vector<UVIsland> islands;
    createUVIslands(triangles, seams, islands);

    // Unwrap each island
    std::vector<glm::vec2> uvs(vertices.size());
    for (const auto& island : islands)
    {
        unwrapIsland(vertices, triangles, island, uvs);
    }

    // Pack UV islands
    packUVIslands(const_cast<std::vector<UVIsland>&>(islands), uvs, config.seamPadding);

    // Apply transformations and update vertices
    for (size_t i = 0; i < vertices.size(); ++i)
    {
        glm::vec2 finalUV = uvs[i];
        applyUVTransforms(finalUV, config);
        vertices[i].TexCoords[0] = finalUV;
    }

    buildIdentityVertexMap(vertices.size(), sourceVertexMap);

    return true;
}


// Method 2: Cylindrical projection
bool UVGenerator::generateCylindrical(
    std::vector<Vertex>& vertices,
    std::vector<unsigned int>& indices,
    const UVConfig& config,
    std::vector<unsigned int>* sourceVertexMap)
{
    if (vertices.empty() || indices.empty()) return false;

    buildIdentityVertexMap(vertices.size(), sourceVertexMap);

    glm::vec3 centroid = calculateCentroid(vertices);
    glm::vec3 minBounds, maxBounds;
    calculateBounds(vertices, minBounds, maxBounds);
    float height = maxBounds.y - minBounds.y;
    if (height < 1e-6f) height = 1.0f; // Avoid division by zero

    // Step 1: Assign UVs based on cylindrical mapping
    for (auto& vertex : vertices)
    {
        glm::vec3 localPos = vertex.Position - centroid;

        // Calculate angle with proper handling of edge cases
        float angle = atan2(localPos.z, localPos.x);
        angle += config.cylindricalSeamRotation; // rotate seam if needed

        // Normalize angle to [0, 2pi] range first
        while (angle < 0.0f) angle += 2.0f * M_PI;
        while (angle >= 2.0f * M_PI) angle -= 2.0f * M_PI;

        float u = angle / (2.0f * M_PI); // map to [0,1]
        float v = (vertex.Position.y - minBounds.y) / height;

        // Apply user offset and scale
        u += config.cylindricalOffset;
        u = fmod(u + 1.0f, 1.0f); // ensure [0,1] wrap

        glm::vec2 uv(u * config.cylindricalScale, v);
        applyUVTransforms(uv, config);
        vertex.TexCoords[0] = uv;
    }

    // Step 2: Handle seam-crossing triangles by duplicating vertices
    if (config.seamlessSpherical) // Note: This should probably be renamed to seamlessCylindrical
    {
        const size_t triangleCount = indices.size() / 3;
        std::vector<unsigned int> newIndices;
        newIndices.reserve(indices.size());

        for (size_t t = 0; t < triangleCount; ++t)
        {
            unsigned int i0 = indices[3 * t + 0];
            unsigned int i1 = indices[3 * t + 1];
            unsigned int i2 = indices[3 * t + 2];

            // Get UV coordinates
            float u0 = vertices[i0].TexCoords[0].x;
            float u1 = vertices[i1].TexCoords[0].x;
            float u2 = vertices[i2].TexCoords[0].x;

            // Check if triangle crosses the seam (0/1 boundary)
            float maxU = std::max({ u0, u1, u2 });
            float minU = std::min({ u0, u1, u2 });

            // If UVs span the seam boundary (accounting for wrapping)
            if (maxU - minU > 0.5f)
            {
                // Create new indices for this triangle
                unsigned int newI0 = i0, newI1 = i1, newI2 = i2;

                // Helper lambda to duplicate vertex if it needs seam adjustment
                auto duplicateIfNeeded = [&](unsigned int originalIdx, float u) -> unsigned int {
                    if (u < 0.5f) // Vertex is on the "left" side of seam, needs to be moved right
                    {
                        Vertex dup = vertices[originalIdx];
                        dup.TexCoords[0].x += 1.0f; // Shift u to maintain continuity

                        // Don't apply transforms again - they were already applied
                        // The transformed coordinates should maintain the offset

                        vertices.push_back(dup);
                        if (sourceVertexMap)
                            sourceVertexMap->push_back(originalIdx);
                        return static_cast<unsigned int>(vertices.size() - 1);
                    }
                    return originalIdx;
                    };

                newI0 = duplicateIfNeeded(i0, u0);
                newI1 = duplicateIfNeeded(i1, u1);
                newI2 = duplicateIfNeeded(i2, u2);

                newIndices.push_back(newI0);
                newIndices.push_back(newI1);
                newIndices.push_back(newI2);
            }
            else
            {
                // Triangle doesn't cross seam, use original indices
                newIndices.push_back(i0);
                newIndices.push_back(i1);
                newIndices.push_back(i2);
            }
        }

        // Replace indices with the new ones
        indices = std::move(newIndices);
    }

    return true;
}



// Method 3: Spherical projection
bool UVGenerator::generateSpherical(
    std::vector<Vertex>& vertices,
    std::vector<unsigned int>& indices,
    const UVConfig& config,
    std::vector<unsigned int>* sourceVertexMap)
{
    if (vertices.empty() || indices.empty())
        return false;

    glm::vec3 centroid = calculateCentroid(vertices);
    const float poleThreshold = 0.98f;
    float longitudeOffset = config.sphericalUVRotation;

    // Helper function to calculate spherical coordinates
    auto calculateSphericalUV = [&](const glm::vec3& localPos) -> glm::vec2 {
        float longitude = atan2(localPos.z, localPos.x);
        float latitude = asin(glm::clamp(localPos.y, -1.0f, 1.0f));
        longitude += longitudeOffset;

        // Normalize longitude to [0, 2pi)
        while (longitude < 0.0f) longitude += 2.0f * M_PI;
        while (longitude >= 2.0f * M_PI) longitude -= 2.0f * M_PI;

        float u = longitude / (2.0f * M_PI);
        float v = (latitude + M_PI_2) / M_PI;

        return glm::vec2(u, v);
        };

    // Analyze mesh to find optimal seam placement
    auto findOptimalSeam = [&]() -> float {
        std::vector<float> seamCandidates;

        // Sample longitude values from mesh
        for (const auto& vertex : vertices)
        {
            glm::vec3 localPos = glm::normalize(vertex.Position - centroid);
            float longitude = atan2(localPos.z, localPos.x);
            longitude += longitudeOffset;
            while (longitude < 0.0f) longitude += 2.0f * M_PI;
            while (longitude >= 2.0f * M_PI) longitude -= 2.0f * M_PI;
            seamCandidates.push_back(longitude);
        }

        // Find the longitude range with fewest vertices (best seam location)
        std::sort(seamCandidates.begin(), seamCandidates.end());

        float bestSeamLongitude = 0.0f;
        float maxGap = 0.0f;

        for (size_t i = 0; i < seamCandidates.size() - 1; ++i)
        {
            float gap = seamCandidates[i + 1] - seamCandidates[i];
            if (gap > maxGap)
            {
                maxGap = gap;
                bestSeamLongitude = (seamCandidates[i] + seamCandidates[i + 1]) * 0.5f;
            }
        }

        // Check wrap-around gap
        float wrapGap = (seamCandidates[0] + 2.0f * M_PI) - seamCandidates.back();
        if (wrapGap > maxGap)
        {
            bestSeamLongitude = seamCandidates.back() + wrapGap * 0.5f;
            if (bestSeamLongitude >= 2.0f * M_PI) bestSeamLongitude -= 2.0f * M_PI;
        }

        return bestSeamLongitude;
        };

    // Calculate optimal seam position
    float optimalSeamLongitude = config.seamlessSpherical ? findOptimalSeam() : 0.0f;

    // Helper function to detect if triangle crosses seam
    auto crossesSeam = [&](const std::array<glm::vec2, 3>& uvs, float seamU) -> bool {
        // Convert seam longitude to U coordinate
        float seamUCoord = seamU / (2.0f * M_PI);

        // Check if vertices are on opposite sides of the seam
        for (int i = 0; i < 3; ++i)
        {
            for (int j = i + 1; j < 3; ++j)
            {
                float uDiff = std::abs(uvs[i].x - uvs[j].x);
                if (uDiff > 0.5f)
                {
                    return true;
                }
            }
        }
        return false;
        };

    // Helper function to fix seam crossing with adaptive approach
    auto fixSeamCrossing = [&](std::array<glm::vec2, 3>& uvs,
        const std::array<glm::vec3, 3>& worldPos) {
            // Calculate triangle center in world space
            glm::vec3 triCenter = (worldPos[0] + worldPos[1] + worldPos[2]) / 3.0f;
            glm::vec3 localTriCenter = glm::normalize(triCenter - centroid);

            // Determine which side of seam the triangle center is on
            float centerLongitude = atan2(localTriCenter.z, localTriCenter.x);
            centerLongitude += longitudeOffset;
            while (centerLongitude < 0.0f) centerLongitude += 2.0f * M_PI;
            while (centerLongitude >= 2.0f * M_PI) centerLongitude -= 2.0f * M_PI;

            float centerU = centerLongitude / (2.0f * M_PI);

            // Adjust vertices to be on the same side as the triangle center
            for (int i = 0; i < 3; ++i)
            {
                float uDiff = uvs[i].x - centerU;

                if (uDiff > 0.5f)
                {
                    uvs[i].x -= 1.0f;
                }
                else if (uDiff < -0.5f)
                {
                    uvs[i].x += 1.0f;
                }
            }
        };

    // Helper function to handle pole triangles
    auto handlePoleTriangle = [&](std::array<glm::vec2, 3>& uvs,
        const std::array<glm::vec3, 3>& localPos) -> bool {
            int poleVertices = 0;
            int poleIndex = -1;

            for (int i = 0; i < 3; ++i)
            {
                if (std::abs(localPos[i].y) > poleThreshold)
                {
                    poleVertices++;
                    poleIndex = i;
                }
            }

            if (poleVertices == 1)
            {
                // Single pole vertex - interpolate U from other vertices
                float avgU = 0.0f;
                int nonPoleCount = 0;

                for (int i = 0; i < 3; ++i)
                {
                    if (i != poleIndex)
                    {
                        avgU += uvs[i].x;
                        nonPoleCount++;
                    }
                }

                if (nonPoleCount > 0)
                {
                    uvs[poleIndex].x = avgU / nonPoleCount;
                }

                // Adjust V coordinate slightly to avoid exact pole
                if (localPos[poleIndex].y > 0)
                {
                    uvs[poleIndex].y = 1.0f - 0.001f;
                }
                else
                {
                    uvs[poleIndex].y = 0.001f;
                }

                return true;
            }

            return false;
        };

    if (config.duplicatePoleVertices)
    {
        std::vector<Vertex> finalVertices;
        std::vector<unsigned int> finalIndices;
        std::vector<unsigned int> finalSourceVertexMap;

        // Process triangles to handle seams and poles
        for (size_t i = 0; i < indices.size(); i += 3)
        {
            std::array<unsigned int, 3> triIndices = { indices[i], indices[i + 1], indices[i + 2] };
            std::array<Vertex, 3> triVertices = { vertices[triIndices[0]], vertices[triIndices[1]], vertices[triIndices[2]] };
            std::array<glm::vec3, 3> localPos;
            std::array<glm::vec2, 3> uvs;

            // Calculate initial UVs and local positions
            for (int j = 0; j < 3; ++j)
            {
                localPos[j] = glm::normalize(triVertices[j].Position - centroid);
                uvs[j] = calculateSphericalUV(localPos[j]);
            }

            // Handle pole triangles first
            bool isPoleTriangle = handlePoleTriangle(uvs, localPos);

            // Handle seam crossing if not a pole triangle
            if (!isPoleTriangle && crossesSeam(uvs, optimalSeamLongitude))
            {
                fixSeamCrossing(uvs, { triVertices[0].Position, triVertices[1].Position, triVertices[2].Position });
            }

            // Create final vertices with corrected UVs
            std::array<unsigned int, 3> newTriIndices;
            for (int j = 0; j < 3; ++j)
            {
                Vertex newVertex = triVertices[j];
                glm::vec2 finalUV = uvs[j];

                // Wrap U coordinates back to [0,1] range
                while (finalUV.x < 0.0f) finalUV.x += 1.0f;
                while (finalUV.x > 1.0f) finalUV.x -= 1.0f;
                finalUV.y = glm::clamp(finalUV.y, 0.0f, 1.0f);

                newVertex.TexCoords[0] = glm::vec2(finalUV.x * config.sphericalScale,
                    finalUV.y * config.sphericalScale);
                applyUVTransforms(newVertex.TexCoords[0], config);

                finalVertices.push_back(newVertex);
                finalSourceVertexMap.push_back(triIndices[j]);
                newTriIndices[j] = static_cast<unsigned int>(finalVertices.size() - 1);
            }

            finalIndices.insert(finalIndices.end(), {
                newTriIndices[0], newTriIndices[1], newTriIndices[2]
                });
        }

        vertices = std::move(finalVertices);
        indices = std::move(finalIndices);
        if (sourceVertexMap)
            *sourceVertexMap = std::move(finalSourceVertexMap);
    }
    else
    {
        buildIdentityVertexMap(vertices.size(), sourceVertexMap);

        // Create a mapping from original to corrected UVs
        std::unordered_map<unsigned int, glm::vec2> vertexUVMap;

        // Process triangles to determine correct UVs
        for (size_t i = 0; i < indices.size(); i += 3)
        {
            std::array<unsigned int, 3> triIndices = { indices[i], indices[i + 1], indices[i + 2] };
            std::array<glm::vec3, 3> localPos;
            std::array<glm::vec2, 3> uvs;

            // Calculate initial UVs and local positions
            for (int j = 0; j < 3; ++j)
            {
                localPos[j] = glm::normalize(vertices[triIndices[j]].Position - centroid);
                uvs[j] = calculateSphericalUV(localPos[j]);
            }

            // Handle pole triangles
            bool isPoleTriangle = handlePoleTriangle(uvs, localPos);

            // Handle seam crossing if not a pole triangle
            if (!isPoleTriangle && crossesSeam(uvs, optimalSeamLongitude))
            {
                fixSeamCrossing(uvs, { vertices[triIndices[0]].Position,
                                    vertices[triIndices[1]].Position,
                                    vertices[triIndices[2]].Position });
            }

            // Store corrected UVs (may overwrite previous values, but that's expected)
            for (int j = 0; j < 3; ++j)
            {
                vertexUVMap[triIndices[j]] = uvs[j];
            }
        }

        // Apply final UVs to vertices
        for (size_t i = 0; i < vertices.size(); ++i)
        {
            glm::vec2 uv = vertexUVMap[i];

            // Wrap U coordinates back to [0,1] range
            while (uv.x < 0.0f) uv.x += 1.0f;
            while (uv.x > 1.0f) uv.x -= 1.0f;
            uv.y = glm::clamp(uv.y, 0.0f, 1.0f);

            vertices[i].TexCoords[0] = glm::vec2(uv.x * config.sphericalScale,
                uv.y * config.sphericalScale);
            applyUVTransforms(vertices[i].TexCoords[0], config);
        }
    }

    return true;
}


// Method 4: Planar projection with automatic orientation
bool UVGenerator::generatePlanar(
    std::vector<Vertex>& vertices,
    std::vector<unsigned int>& indices,
    const UVConfig& config,
    std::vector<unsigned int>* sourceVertexMap)
{
    if (vertices.empty()) return false;

    glm::vec3 minBounds, maxBounds;
    calculateBounds(vertices, minBounds, maxBounds);

    glm::vec3 size = maxBounds - minBounds;

    // Add epsilon to prevent division by zero
    const float epsilon = 1e-6f;
    size.x = std::max(size.x, epsilon);
    size.y = std::max(size.y, epsilon);
    size.z = std::max(size.z, epsilon);

    // For proper cube mapping, we need to consider face normals
    // This approach projects from the dominant direction while maintaining orientation

    // Calculate the overall bounding box dimensions
    glm::vec3 center = (minBounds + maxBounds) * 0.5f;
    float maxDimension = std::max({ size.x, size.y, size.z });

    // Pre-calculate inverse sizes for efficiency
    const glm::vec3 invSize = 1.0f / size;

    // Generate UV coordinates for each vertex
    for (auto& vertex : vertices)
    {
        glm::vec3 pos = vertex.Position;
        glm::vec3 normal = vertex.Normal;

        // Find the dominant axis of the normal
        glm::vec3 absNormal = glm::abs(normal);

        glm::vec2 uv;

        if (absNormal.x >= absNormal.y && absNormal.x >= absNormal.z)
        {
            // X-dominant face (left/right walls)
            if (normal.x > 0)
            {
                // Right face (+X): looking from outside, Y goes right, Z goes up
                uv.x = (pos.y - minBounds.y) * invSize.y;
                uv.y = (pos.z - minBounds.z) * invSize.z;
            }
            else
            {
                // Left face (-X): looking from outside, Y goes left, Z goes up
                uv.x = 1.0f - (pos.y - minBounds.y) * invSize.y;
                uv.y = (pos.z - minBounds.z) * invSize.z;
            }
        }
        else if (absNormal.y >= absNormal.x && absNormal.y >= absNormal.z)
        {
            // Y-dominant face (front/back walls)
            if (normal.y > 0)
            {
                // Front face (+Y): looking from outside, X goes right, Z goes up
                uv.x = (pos.x - minBounds.x) * invSize.x;
                uv.y = (pos.z - minBounds.z) * invSize.z;
            }
            else
            {
                // Back face (-Y): looking from outside, X goes left, Z goes up
                uv.x = 1.0f - (pos.x - minBounds.x) * invSize.x;
                uv.y = (pos.z - minBounds.z) * invSize.z;
            }
        }
        else
        {
            // Z-dominant face (top/bottom)
            if (normal.z > 0)
            {
                // Top face (+Z): looking from above, X goes right, Y goes away
                uv.x = (pos.x - minBounds.x) * invSize.x;
                uv.y = (pos.y - minBounds.y) * invSize.y;
            }
            else
            {
                // Bottom face (-Z): looking from below, X goes right, Y goes toward
                uv.x = (pos.x - minBounds.x) * invSize.x;
                uv.y = 1.0f - (pos.y - minBounds.y) * invSize.y;
            }
        }

        // Apply scaling
        uv *= config.planarScale;

        // Apply additional transforms
        applyUVTransforms(uv, config);

        // Assign to vertex
        vertex.TexCoords[0] = uv;
    }

    buildIdentityVertexMap(vertices.size(), sourceVertexMap);

    return true;
}


// Method 5: Hybrid approach
bool UVGenerator::generateHybrid(
    std::vector<Vertex>& vertices,
    std::vector<unsigned int>& indices,
    const UVConfig& config,
    std::vector<unsigned int>* sourceVertexMap)
{
    if (vertices.empty()) return false;

    // Compute bounding box and size
    glm::vec3 minBounds, maxBounds;
    calculateBounds(vertices, minBounds, maxBounds);
    glm::vec3 size = maxBounds - minBounds;

    // Principal Component Analysis (for elongation and dominant axis)
    glm::vec3 mean(0.0f);
    for (const auto& v : vertices)
        mean += v.Position;
    mean /= static_cast<float>(vertices.size());

    glm::mat3 covariance(0.0f);
    for (const auto& v : vertices)
    {
        glm::vec3 p = v.Position - mean;
        covariance[0] += p.x * p;
        covariance[1] += p.y * p;
        covariance[2] += p.z * p;
    }

    covariance /= static_cast<float>(vertices.size());

    // Eigen decomposition to get principal axes
    glm::vec3 eigenValues;
    glm::mat3 eigenVectors;
    computeEigenDecomposition(covariance, eigenValues, eigenVectors);

    // Sort eigenvalues (largest = most elongated axis)
    float e0 = eigenValues[0], e1 = eigenValues[1], e2 = eigenValues[2];
    float elongation = e0 / e2; // e0 >= e1 >= e2 assumed after sort

    // Use elongation + variance to determine mapping
    if (elongation > 4.0f)
    {
        return generateCylindrical(vertices, indices, config, sourceVertexMap);
    }
    else if (elongation < 1.5f)
    {
        float avg = (e0 + e1 + e2) / 3.0f;
        float var = (pow(e0 - avg, 2) + pow(e1 - avg, 2) + pow(e2 - avg, 2)) / 3.0f;

        if (var < avg * 0.05f)
            return generateSpherical(vertices, indices, config, sourceVertexMap);
        else
            return generateAngleBased(vertices, indices, config, sourceVertexMap);
    }
    else
    {
        return generatePlanar(vertices, indices, config, sourceVertexMap);
    }
}


// Method 6: Angle-based Smart UV (similar to Blender's Smart UV)
bool UVGenerator::generateAngleBasedSmartUV(    
    std::vector<Vertex>& vertices,
    std::vector<unsigned int>& indices,
    const UVConfig& config,
    std::vector<unsigned int>* sourceVertexMap)
{
    if (vertices.empty() || indices.empty())
        return false;

    // 1. Build triangle list and detect seams
    std::vector<MeshTriangle> triangles;
    buildTriangleList(vertices, indices, triangles);

    std::vector<std::pair<uint32_t, uint32_t>> seams;
    findSeams(vertices, triangles, seams, config.angleThreshold);

    std::vector<UVIsland> islands;
    createUVIslands(triangles, seams, islands);

    // 2. Unwrap per island using PCA (per-triangle UVs)
    std::unordered_map<unsigned int, std::array<glm::vec2, 3>> triangleUVs;

    for (int i = 0; i < static_cast<int>(islands.size()); ++i)
    {
        unwrapIslandPCA(vertices, triangles, islands[i], triangleUVs, true);
    }

    // 3. Flatten: expand vertices and indices to support seams
    std::vector<Vertex> newVertices;
    std::vector<unsigned int> newIndices;
    std::vector<unsigned int> newSourceVertexMap;

    for (size_t triIdx = 0; triIdx < triangles.size(); ++triIdx)
    {
        const MeshTriangle& tri = triangles[triIdx];
        auto it = triangleUVs.find(static_cast<unsigned int>(triIdx));
        if (it == triangleUVs.end()) continue;

        const auto& uvSet = it->second;

        for (int i = 0; i < 3; ++i)
        {
            Vertex v = vertices[tri.indices[i]];
            v.TexCoords[0] = uvSet[i];
            newIndices.push_back(static_cast<unsigned int>(newVertices.size()));
            newVertices.push_back(v);
            newSourceVertexMap.push_back(tri.indices[i]);
        }
    }

    // 4. Optional: pack with xatlas
    if (config.enablePacking)
    {
        std::vector<glm::vec2> packedUVs(newVertices.size());
        std::vector<glm::vec3> positions(newVertices.size());

        for (size_t i = 0; i < newVertices.size(); ++i)
            positions[i] = newVertices[i].Position;

        for (size_t i = 0; i < newVertices.size(); ++i)
            packedUVs[i] = newVertices[i].TexCoords[0];

        packWithXAtlas(packedUVs, newIndices, positions);

        // 5. Apply UV transforms
        for (size_t i = 0; i < newVertices.size(); ++i)
        {
            applyUVTransforms(packedUVs[i], config);
            newVertices[i].TexCoords[0] = packedUVs[i];
        }
    }
    else
    {
        // Apply transforms without packing
        for (auto& v : newVertices)
        {
            applyUVTransforms(v.TexCoords[0], config);
        }
    }

    // 6. Replace original vertex/index buffers
    vertices = std::move(newVertices);
    indices = std::move(newIndices);
    if (sourceVertexMap)
        *sourceVertexMap = std::move(newSourceVertexMap);

    return true;
}


// Method 7: Smart Project - ported from Blender's UV_OT_smart_project
// (source/blender/editors/uvedit/uvedit_unwrap_ops.cc: smart_uv_project_calculate_project_normals
// and smart_project_exec). Unlike the seam/connectivity-based methods above, this clusters
// triangles purely by face-normal similarity: seed a cluster from the largest-area triangle,
// grow it by (optionally area-weighted) average normal within half the angle limit, then repeatedly
// spawn a new cluster from whichever remaining triangle is least similar to every existing
// cluster, until none exceeds the angle limit. Each cluster is then linearly projected with an
// orthonormal basis built from its averaged normal.
bool UVGenerator::generateSmartProject(
    std::vector<Vertex>& vertices,
    std::vector<unsigned int>& indices,
    const UVConfig& config,
    std::vector<unsigned int>* sourceVertexMap)
{
    if (vertices.empty() || indices.empty())
        return false;

    std::vector<MeshTriangle> triangles;
    buildTriangleList(vertices, indices, triangles);
    if (triangles.empty())
        return false;

    const float areaIgnoreThreshold = 1e-12f; // Matches Blender's smart_uv_project_area_ignore
    const float angleLimitRad = glm::radians(config.smartProjectAngleLimit);
    const float angleLimitCos = std::cos(angleLimitRad);
    const float angleLimitHalfCos = std::cos(angleLimitRad * 0.5f);
    const float areaWeight = glm::clamp(config.smartProjectAreaWeight, 0.0f, 1.0f);

    // Sort triangles by descending area and drop near-zero-area ones from clustering
    // (they're still emitted below, with UV (0,0), same as Blender does for degenerate faces).
    std::vector<uint32_t> order(triangles.size());
    for (uint32_t i = 0; i < triangles.size(); ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        return triangles[a].area > triangles[b].area;
        });

    size_t thickCount = order.size();
    while (thickCount > 0 && !(triangles[order[thickCount - 1]].area > areaIgnoreThreshold))
        --thickCount;

    // --- Cluster triangles by normal similarity (greedy furthest-normal seeding) ---
    std::vector<glm::vec3> projectNormals;
    if (thickCount > 0)
    {
        std::vector<bool> tagged(thickCount, false);
        glm::vec3 projectNormal = triangles[order[0]].normal;
        std::vector<size_t> clusterMembers;

        while (true)
        {
            for (size_t i = thickCount; i-- > 0;)
            {
                if (tagged[i]) continue;
                if (glm::dot(triangles[order[i]].normal, projectNormal) > angleLimitHalfCos)
                {
                    clusterMembers.push_back(i);
                    tagged[i] = true;
                }
            }

            glm::vec3 avgNormal(0.0f);
            for (size_t idx : clusterMembers)
            {
                const MeshTriangle& tri = triangles[order[idx]];
                if (areaWeight <= 0.0f)
                    avgNormal += tri.normal;
                else if (areaWeight >= 1.0f)
                    avgNormal += tri.normal * tri.area;
                else
                    avgNormal += tri.normal * (tri.area * areaWeight + (1.0f - areaWeight));
            }
            if (glm::length(avgNormal) > 1e-12f)
                projectNormals.push_back(glm::normalize(avgNormal));

            clusterMembers.clear();

            // Find the remaining triangle whose normal is most different ("most unique")
            // from every existing cluster normal.
            float angleBest = 1.0f;
            size_t angleBestIndex = 0;
            for (size_t i = thickCount; i-- > 0;)
            {
                if (tagged[i]) continue;
                float angleTest = -1.0f;
                for (const glm::vec3& p : projectNormals)
                    angleTest = std::max(angleTest, glm::dot(p, triangles[order[i]].normal));
                if (angleTest < angleBest)
                {
                    angleBest = angleTest;
                    angleBestIndex = i;
                }
            }

            if (angleBest < angleLimitCos)
            {
                projectNormal = triangles[order[angleBestIndex]].normal;
                clusterMembers.push_back(angleBestIndex);
                tagged[angleBestIndex] = true;
            }
            else if (!projectNormals.empty())
            {
                break;
            }
        }
    }

    // --- Assign every non-degenerate triangle to its closest projection normal ---
    std::vector<int> triangleGroup(triangles.size(), -1);
    for (size_t i = 0; i < thickCount; ++i)
    {
        uint32_t triIdx = order[i];
        if (projectNormals.empty())
            continue;

        float best = glm::dot(triangles[triIdx].normal, projectNormals[0]);
        int bestGroup = 0;
        for (int g = 1; g < static_cast<int>(projectNormals.size()); ++g)
        {
            float d = glm::dot(triangles[triIdx].normal, projectNormals[g]);
            if (d > best)
            {
                best = d;
                bestGroup = g;
            }
        }
        triangleGroup[triIdx] = bestGroup;
    }

    // --- Precompute an orthonormal projection basis per cluster ---
    std::vector<glm::vec3> groupTangent(projectNormals.size());
    std::vector<glm::vec3> groupBitangent(projectNormals.size());
    for (size_t g = 0; g < projectNormals.size(); ++g)
        orthoBasisFromNormal(projectNormals[g], groupTangent[g], groupBitangent[g]);

    // --- Project and flatten. Vertices are duplicated per-triangle so that adjoining
    // triangles assigned to different clusters don't fight over one shared UV. ---
    std::vector<Vertex> newVertices;
    std::vector<unsigned int> newIndices;
    std::vector<unsigned int> newSourceVertexMap;
    newVertices.reserve(triangles.size() * 3);
    newIndices.reserve(triangles.size() * 3);

    for (size_t t = 0; t < triangles.size(); ++t)
    {
        const MeshTriangle& tri = triangles[t];
        int group = triangleGroup[t];

        for (int i = 0; i < 3; ++i)
        {
            Vertex v = vertices[tri.indices[i]];
            v.TexCoords[0] = (group >= 0)
                ? glm::vec2(glm::dot(v.Position, groupTangent[group]),
                            glm::dot(v.Position, groupBitangent[group]))
                : glm::vec2(0.0f);

            newIndices.push_back(static_cast<unsigned int>(newVertices.size()));
            newVertices.push_back(v);
            newSourceVertexMap.push_back(tri.indices[i]);
        }
    }

    // --- Pack islands (plays the role of Blender's uvedit_pack_islands_multi) and
    // apply user transforms ---
    if (config.enablePacking)
    {
        std::vector<glm::vec2> packedUVs(newVertices.size());
        std::vector<glm::vec3> positions(newVertices.size());
        for (size_t i = 0; i < newVertices.size(); ++i)
        {
            positions[i] = newVertices[i].Position;
            packedUVs[i] = newVertices[i].TexCoords[0];
        }

        packWithXAtlas(packedUVs, newIndices, positions);

        for (size_t i = 0; i < newVertices.size(); ++i)
        {
            applyUVTransforms(packedUVs[i], config);
            newVertices[i].TexCoords[0] = packedUVs[i];
        }
    }
    else
    {
        for (auto& v : newVertices)
            applyUVTransforms(v.TexCoords[0], config);
    }

    vertices = std::move(newVertices);
    indices = std::move(newIndices);
    if (sourceVertexMap)
        *sourceVertexMap = std::move(newSourceVertexMap);

    return true;
}


// Helper method implementations
void UVGenerator::buildTriangleList(const std::vector<Vertex>& vertices,
    const std::vector<unsigned int>& indices,
    std::vector<MeshTriangle>& triangles)
{
    triangles.clear();

    // Guard: Ensure indices size is a multiple of 3
    if (indices.size() % 3 != 0)
    {        
        std::cerr << "Warning: Indices size is not a multiple of 3. Ignoring incomplete triangle." << std::endl;
    }

    triangles.reserve(indices.size() / 3);

    for (size_t i = 0; i + 2 < indices.size(); i += 3) // Safe loop condition
    {
        MeshTriangle tri;
        tri.indices[0] = indices[i];
        tri.indices[1] = indices[i + 1];
        tri.indices[2] = indices[i + 2];
        tri.visited = false;

        // Ensure index values are within bounds of vertices
        if (tri.indices[0] >= vertices.size() ||
            tri.indices[1] >= vertices.size() ||
            tri.indices[2] >= vertices.size())
        {
            std::cerr << "Warning: Triangle index out of bounds. Skipping triangle." << std::endl;
            continue;
        }

        const glm::vec3& v0 = vertices[tri.indices[0]].Position;
        const glm::vec3& v1 = vertices[tri.indices[1]].Position;
        const glm::vec3& v2 = vertices[tri.indices[2]].Position;

        tri.normal = calculateTriangleNormal(v0, v1, v2);
        tri.area = calculateTriangleArea(v0, v1, v2);

        triangles.push_back(tri);
    }
}


void UVGenerator::findSeams(const std::vector<Vertex>& vertices,
    const std::vector<MeshTriangle>& triangles,
    std::vector<std::pair<uint32_t, uint32_t>>& seams,
    float angleThreshold)
{
    seams.clear();
        
    std::unordered_map<Edge, std::vector<uint32_t>> edgeToTriangles;

    // 1. Build edge -> triangle adjacency
    for (uint32_t i = 0; i < triangles.size(); ++i)
    {
        const MeshTriangle& tri = triangles[i];
        for (int j = 0; j < 3; ++j)
        {
            uint32_t a = tri.indices[j];
            uint32_t b = tri.indices[(j + 1) % 3];
            edgeToTriangles[Edge(a, b)].push_back(i);
        }
    }

    const float cosThreshold = std::cos(glm::radians(angleThreshold));

    // 2. Check each edge's adjacent triangle pair(s)
    for (const auto& entry : edgeToTriangles)
    {
        const auto& adjTris = entry.second;
        if (adjTris.size() != 2)
            continue; // boundary edge

        uint32_t t0 = adjTris[0];
        uint32_t t1 = adjTris[1];

        const glm::vec3& n0 = triangles[t0].normal;
        const glm::vec3& n1 = triangles[t1].normal;

        float dot = glm::dot(n0, n1);
        if (dot < cosThreshold)
        {
            seams.emplace_back(t0, t1);
        }
    }
}


void UVGenerator::createUVIslands(const std::vector<MeshTriangle>& triangles,
    const std::vector<std::pair<uint32_t, uint32_t>>& seams,
    std::vector<UVIsland>& islands)
{
    islands.clear();
    const size_t triangleCount = triangles.size();

    // Build fast edge -> triangle adjacency       

    std::unordered_map<Edge, std::vector<uint32_t>> edgeMap;

    for (uint32_t i = 0; i < triangleCount; ++i)
    {
        const auto& tri = triangles[i];
        edgeMap[Edge(tri.indices[0], tri.indices[1])].push_back(i);
        edgeMap[Edge(tri.indices[1], tri.indices[2])].push_back(i);
        edgeMap[Edge(tri.indices[2], tri.indices[0])].push_back(i);
    }

    // Build seam edge set for fast lookup
    std::unordered_set<Edge> seamEdges;
    for (const auto& s : seams)
    {
        const auto& t0 = triangles[s.first];
        const auto& t1 = triangles[s.second];
        for (int i = 0; i < 3; ++i)
        {
            uint32_t a = t0.indices[i];
            uint32_t b = t0.indices[(i + 1) % 3];
            Edge e = Edge(a, b);

            // Check if edge exists in both triangles
            for (int j = 0; j < 3; ++j)
            {
                uint32_t a1 = t1.indices[j];
                uint32_t b1 = t1.indices[(j + 1) % 3];
                if (Edge(a1, b1) == e)
                {
                    seamEdges.emplace(e);
                }
            }
        }
    }

    // Flood fill to build islands
    std::vector<bool> visited(triangleCount, false);
    for (uint32_t i = 0; i < triangleCount; ++i)
    {
        if (visited[i])
            continue;

        UVIsland island;
        std::queue<uint32_t> q;
        q.push(i);
        visited[i] = true;

        while (!q.empty())
        {
            uint32_t tidx = q.front();
            q.pop();
            island.triangles.push_back(tidx);
            island.totalArea += triangles[tidx].area;

            const auto& tri = triangles[tidx];
            for (int ei = 0; ei < 3; ++ei)
            {
                Edge e = Edge(tri.indices[ei], tri.indices[(ei + 1) % 3]);
                if (seamEdges.count(e)) continue;

                // Neighbors sharing this edge
                const auto& adjTris = edgeMap[e];
                for (uint32_t nidx : adjTris)
                {
                    if (!visited[nidx])
                    {
                        visited[nidx] = true;
                        q.push(nidx);
                    }
                }
            }
        }

        islands.push_back(std::move(island));
    }
}


void UVGenerator::unwrapIsland(const std::vector<Vertex>& vertices,
    const std::vector<MeshTriangle>& triangles,
    const UVIsland& island,
    std::vector<glm::vec2>& uvs)
{
    if (island.triangles.empty())
        return;

    // A single area-weighted average normal (and the basis derived from it) is used
    // for every triangle in the island. Computing the tangent/bitangent basis
    // per-triangle (as before) meant a vertex shared by two triangles in the same
    // island got overwritten with UVs from two different bases depending on
    // iteration order, tearing the island apart at internal edges.
    glm::vec3 avgNormal(0.0f);
    for (unsigned int triIdx : island.triangles)
    {
        const MeshTriangle& tri = triangles[triIdx];
        avgNormal += tri.normal * tri.area;
    }

    glm::vec3 normal = (glm::length(avgNormal) > 1e-8f)
        ? glm::normalize(avgNormal)
        : triangles[island.triangles[0]].normal;

    glm::vec3 tangent = glm::normalize(glm::cross(normal, glm::vec3(0, 1, 0)));
    if (glm::length(tangent) < 0.1f)
    {
        tangent = glm::normalize(glm::cross(normal, glm::vec3(1, 0, 0)));
    }
    glm::vec3 bitangent = glm::cross(normal, tangent);

    for (unsigned int triIdx : island.triangles)
    {
        const MeshTriangle& tri = triangles[triIdx];
        for (int i = 0; i < 3; ++i)
        {
            glm::vec3 pos = vertices[tri.indices[i]].Position;
            uvs[tri.indices[i]] = glm::vec2(
                glm::dot(pos, tangent),
                glm::dot(pos, bitangent)
            );
        }
    }
}


void UVGenerator::unwrapIslandPCA(const std::vector<Vertex>& vertices,
    const std::vector<MeshTriangle>& triangles,
    const UVIsland& island,
    std::unordered_map<unsigned int, std::array<glm::vec2, 3>>& triangleUVs,
    bool normalizeUVs /* = true */)
{
    // 1. Gather all island points
    std::vector<glm::vec3> points;
    for (unsigned int triIdx : island.triangles)
    {
        const MeshTriangle& tri = triangles[triIdx];
        for (int i = 0; i < 3; ++i)
        {
            points.push_back(vertices[tri.indices[i]].Position);
        }
    }

    if (points.empty())
        return;

    // 2. Compute centroid
    glm::vec3 centroid(0.0f);
    for (const auto& p : points)
        centroid += p;
    centroid /= static_cast<float>(points.size());

    // 3. Compute covariance matrix
    glm::mat3 cov(0.0f);
    for (const auto& p : points)
    {
        glm::vec3 d = p - centroid;
        cov += glm::outerProduct(d, d);
    }

    // 4. PCA: compute eigenvectors from covariance
    glm::vec3 eigenValues;
    glm::mat3 eigenVectors;
    computeEigenDecomposition(cov, eigenValues, eigenVectors);

    glm::vec3 axis1 = glm::normalize(glm::vec3(eigenVectors[0][0], eigenVectors[1][0], eigenVectors[2][0]));
    glm::vec3 axis2 = glm::normalize(glm::vec3(eigenVectors[0][1], eigenVectors[1][1], eigenVectors[2][1]));

    // 5. Project and collect per-triangle UVs
    glm::vec2 minUV(FLT_MAX), maxUV(-FLT_MAX);

    for (unsigned int triIdx : island.triangles)
    {
        const MeshTriangle& tri = triangles[triIdx];
        std::array<glm::vec2, 3> projected;

        for (int i = 0; i < 3; ++i)
        {
            glm::vec3 pos = vertices[tri.indices[i]].Position - centroid;
            glm::vec2 uv(glm::dot(pos, axis1), glm::dot(pos, axis2));
            projected[i] = uv;

            if (normalizeUVs)
            {
                minUV = glm::min(minUV, uv);
                maxUV = glm::max(maxUV, uv);
            }
        }

        triangleUVs[triIdx] = projected;
    }

    // 6. Normalize to [0,1] UV box (optional)
    if (normalizeUVs)
    {
        glm::vec2 size = maxUV - minUV;
        if (size.x > 0 && size.y > 0)
        {
            for (auto& [triIdx, uvSet] : triangleUVs)
            {
                for (glm::vec2& uv : uvSet)
                {
                    uv = (uv - minUV) / size;
                }
            }
        }
    }
}


void UVGenerator::relaxUVs(
    const std::vector<MeshTriangle>& triangles,
    std::vector<glm::vec2>& uvs,
    const std::vector<UVIsland>& islands,
    const UVConfig& config,
    int iterations)
{
    // Build adjacency map: vertex index -> unique neighbors
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> adjacency;

    for (const auto& island : islands)
    {
        for (uint32_t triIdx : island.triangles)
        {
            const MeshTriangle& tri = triangles[triIdx];
            for (int i = 0; i < 3; ++i)
            {
                uint32_t vi = tri.indices[i];
                for (int j = 0; j < 3; ++j)
                {
                    uint32_t vj = tri.indices[j];
                    if (vi != vj)
                        adjacency[vi].insert(vj); // insert deduplicates
                }
            }
        }
    }

    std::vector<glm::vec2> newUVs = uvs;

    for (int iter = 0; iter < iterations; ++iter)
    {
        for (size_t i = 0; i < uvs.size(); ++i)
        {
            auto it = adjacency.find(static_cast<uint32_t>(i));
            if (it == adjacency.end() || it->second.empty())
                continue;

            glm::vec2 avg(0.0f);
            for (uint32_t neighbor : it->second)
                avg += uvs[neighbor];

            avg /= static_cast<float>(it->second.size());
            newUVs[i] = avg;
        }

        std::swap(uvs, newUVs); // Apply new UVs for next iteration
    }
}


void UVGenerator::packUVIslands(std::vector<UVIsland>& islands,
    std::vector<glm::vec2>& uvs,
    float padding)
{
    // Simple UV packing - normalize all UVs to [0,1] range
    if (uvs.empty()) return;

    glm::vec2 minUV = uvs[0];
    glm::vec2 maxUV = uvs[0];

    for (const auto& uv : uvs)
    {
        minUV = glm::min(minUV, uv);
        maxUV = glm::max(maxUV, uv);
    }

    glm::vec2 size = maxUV - minUV;
    if (size.x > 0 && size.y > 0)
    {
        for (auto& uv : uvs)
        {
            uv = (uv - minUV) / size;
        }
    }
}


#include <xatlas.h>
void UVGenerator::packWithXAtlas(
    std::vector<glm::vec2>& uvs,
    const std::vector<unsigned int>& indices,
    const std::vector<glm::vec3>& positions)
{
    assert(!uvs.empty());
    assert(!indices.empty());
    assert(positions.size() == uvs.size());

    xatlas::Atlas* atlas = xatlas::Create();

    xatlas::MeshDecl meshDecl{};
    meshDecl.vertexCount = static_cast<uint32_t>(positions.size());
    meshDecl.vertexPositionData = positions.data();
    meshDecl.vertexPositionStride = sizeof(glm::vec3);
    meshDecl.indexCount = static_cast<uint32_t>(indices.size());
    meshDecl.indexData = indices.data();
    meshDecl.indexFormat = xatlas::IndexFormat::UInt32;

    // Add mesh to xatlas
    xatlas::AddMeshError error = xatlas::AddMesh(atlas, meshDecl);
    if (error != xatlas::AddMeshError::Success)
    {
        printf("xatlas AddMesh failed: %s\n", xatlas::StringForEnum(error));
        xatlas::Destroy(atlas);
        return;
    }

    xatlas::ChartOptions chartOptions{};
    xatlas::PackOptions packOptions{};
    packOptions.padding = 2;
    packOptions.texelsPerUnit = 1.0f;

    xatlas::Generate(atlas, chartOptions, packOptions);

    const xatlas::Mesh& outMesh = atlas->meshes[0];
    uvs.resize(outMesh.vertexCount);

    for (uint32_t i = 0; i < outMesh.vertexCount; ++i)
    {
        const xatlas::Vertex& v = outMesh.vertexArray[i];
        uvs[v.xref] = glm::vec2(
            v.uv[0] / float(atlas->width),
            v.uv[1] / float(atlas->height));
    }

    xatlas::Destroy(atlas);
}


void UVGenerator::applyUVTransforms(glm::vec2& uv, const UVConfig& config)
{
    uv *= config.planarScale;

    if (config.flipV)
    {
        uv.y = 1.0f - uv.y;
    }

    // Ensure UVs are in [0,1] range
    uv = glm::clamp(uv, 0.0f, 1.0f);
}

// Utility methods
glm::vec3 UVGenerator::calculateCentroid(const std::vector<Vertex>& vertices)
{
    glm::vec3 centroid(0.0f);
    for (const auto& vertex : vertices)
    {
        centroid += vertex.Position;
    }
    return centroid / static_cast<float>(vertices.size());
}

glm::vec3 UVGenerator::calculateBounds(const std::vector<Vertex>& vertices,
    glm::vec3& minBounds, glm::vec3& maxBounds)
{
    if (vertices.empty()) return glm::vec3(0);

    minBounds = maxBounds = vertices[0].Position;
    for (const auto& vertex : vertices)
    {
        minBounds = glm::min(minBounds, vertex.Position);
        maxBounds = glm::max(maxBounds, vertex.Position);
    }
    return maxBounds - minBounds;
}

float UVGenerator::calculateTriangleArea(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2)
{
    return 0.5f * glm::length(glm::cross(v1 - v0, v2 - v0));
}

glm::vec3 UVGenerator::calculateTriangleNormal(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2)
{
    return glm::normalize(glm::cross(v1 - v0, v2 - v0));
}

// Utility: compute eigenvalues and eigenvectors of symmetric 3x3 matrix
// Only works correctly for symmetric matrices (like covariance)
void UVGenerator::computeEigenDecomposition(
    const glm::mat3& m,
    glm::vec3& eigenValues,
    glm::mat3& eigenVectors)
{
    const int maxIterations = 50;
    const float epsilon = 1e-10f;

    glm::mat3 A = m;
    eigenVectors = glm::mat3(1.0f); // Identity

    for (int iter = 0; iter < maxIterations; ++iter)
    {
        // Find largest off-diagonal element in A
        int p = 0, q = 1;
        float maxVal = std::abs(A[0][1]);
        for (int i = 0; i < 3; ++i)
        {
            for (int j = i + 1; j < 3; ++j)
            {
                float val = std::abs(A[i][j]);
                if (val > maxVal)
                {
                    maxVal = val;
                    p = i;
                    q = j;
                }
            }
        }

        if (maxVal < epsilon)
            break; // Converged

        float app = A[p][p];
        float aqq = A[q][q];
        float apq = A[p][q];

        float phi = 0.5f * atan2(2.0f * apq, aqq - app);
        float c = cos(phi);
        float s = sin(phi);

        // Build rotation matrix
        glm::mat3 R(1.0f);
        R[p][p] = c;
        R[q][q] = c;
        R[p][q] = s;
        R[q][p] = -s;

        // A = R^T * A * R
        A = glm::transpose(R) * A * R;
        eigenVectors = eigenVectors * R;
    }

    eigenValues = glm::vec3(A[0][0], A[1][1], A[2][2]);

    // Sort by eigenvalue magnitude (descending)
    std::array<std::pair<float, glm::vec3>, 3> sorted = {
        std::make_pair(eigenValues.x, glm::vec3(eigenVectors[0][0], eigenVectors[1][0], eigenVectors[2][0])),
        std::make_pair(eigenValues.y, glm::vec3(eigenVectors[0][1], eigenVectors[1][1], eigenVectors[2][1])),
        std::make_pair(eigenValues.z, glm::vec3(eigenVectors[0][2], eigenVectors[1][2], eigenVectors[2][2]))
    };

    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
        });

    for (int i = 0; i < 3; ++i)
    {
        eigenValues[i] = sorted[i].first;
        eigenVectors[0][i] = sorted[i].second.x;
        eigenVectors[1][i] = sorted[i].second.y;
        eigenVectors[2][i] = sorted[i].second.z;
    }
}
