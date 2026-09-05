#include "UVGenerator.h"
#include <QDebug>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <limits>
#include <queue>
#include <utility>
#include <unordered_set>

// Temporary diagnostic for the ARAP addition - flip to true, rebuild, run
// Generate UVs with ARAP selected, and check the log for how many islands
// actually got a real ARAP unfold vs. fell back to unwrapIslandPCA (and
// why). Remove once ARAP's real behavior is confirmed.
constexpr bool kARAPVerbose = false;

// Temporary diagnostic for the spherical UV pole/seam artifact - flip to true, rebuild, run
// Generate UVs with Spherical selected, and check the log for per-triangle pole/seam data.
// Remove once the pole/seam distortion is confirmed fixed.
constexpr bool kSphericalVerbose = false;

// Temporary diagnostic for the Hybrid method-selection regression ("always falls back to
// Planar") - flip to true, rebuild, run Generate UVs with Hybrid selected, and check the log for
// the actual eigenvalues/elongation/variance it computed and which branch it took.
constexpr bool kHybridVerbose = true;

// Temporary diagnostic for the Torus addition - flip to true, rebuild, run Generate UVs with
// Torus selected, and check the log for the estimated major/minor radius and whether R<=r (a
// degenerate spindle/horn torus). Remove once Torus's real behavior is confirmed.
constexpr bool kTorusVerbose = false;

// Temporary diagnostic for a reported hang ("UV never converges", no crash) in Angle-Based Smart
// UV on a specific model - flip to true, rebuild, reproduce, and check the log for whatever gets
// printed right before the hang. packWithXAtlas() has no loop of its own; the only place that
// isn't bounded by our own iteration counts is xatlas::Generate() itself (third-party), so this
// scans the input handed to it for NaN/Inf (which could make xatlas's own internal algorithms
// genuinely never converge) and reports basic degenerate-triangle stats. Remove once the real
// cause is confirmed.
constexpr bool kXAtlasVerbose = false;

// See generateARAP()'s doc comment (UVGenerator.h) for why these - CGAL's
// Surface_mesh_parameterization::ARAP_parameterizer_3 provides a real
// distortion-minimizing unfold per island, unlike this file's own
// unwrapIsland()/unwrapIslandPCA() (flat orthogonal projections onto one
// averaged/PCA'd basis - never a true unfolding). Same Kernel
// (Exact_predicates_inexact_constructions_kernel) as every other CGAL tool
// in this codebase - parameterization doesn't need exact predicates.
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/boost/graph/helpers.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Surface_mesh_parameterization/ARAP_parameterizer_3.h>
#include <CGAL/Surface_mesh_parameterization/parameterize.h>

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
    std::vector<unsigned int>* sourceVertexMap,
    const std::vector<std::pair<glm::vec3, glm::vec3>>* userSeamEdges)
{
    if (vertices.empty() || indices.empty()) return false;

    // Build triangle list
    std::vector<MeshTriangle> triangles;
    buildTriangleList(vertices, indices, triangles);

    // Find seams based on angle threshold
    std::vector<std::pair<unsigned int, unsigned int>> seams;
    findSeams(vertices, triangles, seams, config.angleThreshold, userSeamEdges);

    // Create UV islands
    std::vector<UVIsland> islands;
    createUVIslands(triangles, seams, islands);

    // Unwrap each island
    std::vector<glm::vec2> uvs(vertices.size());
    for (const auto& island : islands)
    {
        unwrapIsland(vertices, triangles, island, uvs);
    }

    // Smooth interior distortion by averaging each vertex's UV with its neighbors - BEFORE
    // packing, which rescales/repositions every island into a shared atlas; relaxing beforehand
    // means only an island's own local geometry influences it, not another island's post-pack
    // placement. config.enableRelaxation/relaxationIterations have been threaded all the way from
    // the dialog's "Enable Relaxation" checkbox since that control was added, but relaxUVs() was
    // never actually called anywhere - toggling the checkbox had no effect on the result.
    if (config.enableRelaxation && config.relaxationIterations > 0)
    {
        relaxUVs(triangles, uvs, islands, config, config.relaxationIterations);
    }

    // Pack UV islands. config.enablePacking has always been wired to this dialog's "Enable
    // Packing" checkbox but, unlike generateAngleBasedSmartUV()/generateSmartProject()/
    // generateARAP() (which all check it and call the real packWithXAtlas() below), this method
    // unconditionally used packUVIslands() - a naive GLOBAL min/max normalize across every vertex
    // combined, not real per-island packing. That's harmless when islands happen to end up on a
    // similar coordinate scale, but confirmed broken for 2 islands with sufficiently different
    // unwrapIsland() basis orientations (e.g. a marked seam splitting a mesh into two differently-
    // angled flat panels): one island's absolute UV range can be tiny relative to the other's, so
    // the shared global normalize collapses it into a sliver near a single point - every vertex
    // sampling effectively the same texel (seen as a solid, untextured-looking panel).
    if (config.enablePacking)
    {
        std::vector<glm::vec3> positions(vertices.size());
        for (size_t i = 0; i < vertices.size(); ++i)
            positions[i] = vertices[i].Position;
        packWithXAtlas(uvs, indices, positions);
    }
    else
    {
        packUVIslands(triangles, const_cast<std::vector<UVIsland>&>(islands), uvs, config.seamPadding);
    }

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

    // Weld by exact position before computing the centroid/covariance below - NOT a stylistic
    // choice, a correctness one. This function's OWN seam-crossing step (Step 2 below) duplicates
    // vertices along whatever seam the PREVIOUS UV generation used, and since generation mutates
    // the SAME mesh in place (confirmed real bug: manual-axis generation, then regenerating with
    // auto-detect on the SAME mesh, produced a skewed axis), those leftover duplicate-position
    // vertices - along with any from a prior exploding UV method (Smart Project/ARAP/etc, see
    // buildTriangleList()'s doc comment for why those duplicate too) - would otherwise silently
    // over-weight whatever curve/region they cluster along in an unweighted positional mean/PCA,
    // pulling the detected axis away from the mesh's true geometric one. A vertex actually
    // repeated at the same 3D position never carries new positional information, so counting it
    // more than once here is always wrong, regardless of why the duplicate exists.
    std::vector<glm::vec3> uniquePositions = computeUniquePositions(vertices);

    glm::vec3 centroid(0.0f);
    for (const auto& p : uniquePositions)
        centroid += p;
    centroid /= static_cast<float>(uniquePositions.size());

    // Determine the cylinder's axis, either via PCA or from config.cylindricalAxis - either way,
    // NOT hardcoded to world-Y. This used to hardcode height from vertex.Position.y and
    // circumferential angle from atan2(z,x) - correct only for a cylinder whose axis happens to
    // already be world-Y. A cylinder modeled/imported in any other orientation (confirmed real
    // case: OpenCylinder.obj's axis is world-Z, with X/Y forming its circular cross-section - X and
    // Y each range -20..20 while Z is only ever exactly +-20) then had "height" computed from a
    // circular, non-monotonic coordinate and "angle" computed from an axis pair that isn't the
    // circumference at all, producing a sheared/torn checker pattern with no relation to the mesh's
    // real geometry.
    glm::vec3 axis, planeX, planeY;

    if (config.cylindricalAutoDetectAxis)
    {
        glm::mat3 covariance(0.0f);
        for (const auto& pos : uniquePositions)
        {
            glm::vec3 p = pos - centroid;
            covariance[0] += p.x * p;
            covariance[1] += p.y * p;
            covariance[2] += p.z * p;
        }
        covariance /= static_cast<float>(uniquePositions.size());

        glm::vec3 eigenValues;
        glm::mat3 eigenVectors;
        computeEigenDecomposition(covariance, eigenValues, eigenVectors);
        // eigenValues/eigenVectors come back sorted descending (e0 >= e1 >= e2).

        // The axis is whichever eigenvalue is the OUTLIER relative to its neighbor - NOT simply
        // "the largest" (generateHybrid()'s "elongation" convention, which only holds for a
        // cylinder longer than roughly its own diameter). A true cylinder's two CROSS-SECTIONAL
        // eigenvalues are close to each other (circular symmetry) regardless of aspect ratio, so
        // the odd-one-out eigenvalue - whether it's the largest (elongated cylinder) or the
        // smallest (short, fat cylinder, where the circular cross-section has MORE variance than
        // the short axial extent) - is the real axis. PCA can still misjudge this for a near-
        // square-aspect cylinder or one with an attached feature skewing the covariance - that's
        // what config.cylindricalAutoDetectAxis=false / cylindricalAxis is for.
        int axisIdx = (eigenValues[0] - eigenValues[1] > eigenValues[1] - eigenValues[2]) ? 0 : 2;
        int uIdx = (axisIdx == 0) ? 1 : 0;
        int vIdx = (axisIdx == 2) ? 1 : 2;
        axis = glm::normalize(glm::vec3(
            eigenVectors[0][axisIdx], eigenVectors[1][axisIdx], eigenVectors[2][axisIdx]));
        // The other two eigenvectors are mutually orthogonal, and orthogonal to axis, by
        // construction (eigenvectors of a symmetric covariance matrix) - a ready-made basis for
        // the circular cross-section, no separate Gram-Schmidt step needed.
        planeX = glm::normalize(glm::vec3(
            eigenVectors[0][uIdx], eigenVectors[1][uIdx], eigenVectors[2][uIdx]));
        planeY = glm::normalize(glm::vec3(
            eigenVectors[0][vIdx], eigenVectors[1][vIdx], eigenVectors[2][vIdx]));
    }
    else
    {
        // Manual axis: fall back to world-Y if the user left it at zero-length (e.g. all 3 fields
        // cleared to 0) rather than normalizing a zero vector into garbage.
        axis = (glm::length(config.cylindricalAxis) > 1e-6f)
            ? glm::normalize(config.cylindricalAxis)
            : glm::vec3(0.0f, 1.0f, 0.0f);

        // Unlike the PCA case, there's no second/third eigenvector to reuse here - build an
        // arbitrary orthonormal basis perpendicular to axis. Cross axis with world-Y unless axis
        // is nearly parallel to Y itself (cross product degenerates near-zero there), in which
        // case cross with world-X instead.
        glm::vec3 fallback = (std::abs(axis.y) < 0.99f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
        planeX = glm::normalize(glm::cross(fallback, axis));
        planeY = glm::cross(axis, planeX);
    }

    // Axial extent, measured along the TRUE axis rather than assuming world-Y's bounding-box range.
    float minH = FLT_MAX, maxH = -FLT_MAX;
    for (const auto& v : vertices)
    {
        float h = glm::dot(v.Position - centroid, axis);
        minH = std::min(minH, h);
        maxH = std::max(maxH, h);
    }
    float height = maxH - minH;
    if (height < 1e-6f) height = 1.0f; // Avoid division by zero

    // Step 1: Assign UVs based on cylindrical mapping
    for (auto& vertex : vertices)
    {
        glm::vec3 rel = vertex.Position - centroid;
        float px = glm::dot(rel, planeX);
        float py = glm::dot(rel, planeY);
        float h = glm::dot(rel, axis);

        // Calculate angle with proper handling of edge cases
        float angle = atan2(py, px);
        angle += config.cylindricalSeamRotation; // rotate seam if needed

        // Normalize angle to [0, 2pi] range first
        while (angle < 0.0f) angle += 2.0f * M_PI;
        while (angle >= 2.0f * M_PI) angle -= 2.0f * M_PI;

        float u = angle / (2.0f * M_PI); // map to [0,1]
        float v = (h - minH) / height;

        // Apply user offset and scale
        u += config.cylindricalOffset;
        u = fmod(u + 1.0f, 1.0f); // ensure [0,1] wrap

        glm::vec2 uv(u * config.cylindricalScale, v);
        applyUVTransforms(uv, config);
        vertex.TexCoords[0] = uv;
    }

    // Step 2: Handle seam-crossing triangles by duplicating vertices
    if (config.seamlessCylindrical)
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

    // Weld to unique positions before computing centroid/covariance - same reasoning as
    // generateCylindrical()'s identical weld (see its doc comment): a mesh that already carries
    // duplicate-position vertices (from Spherical's own pole-vertex duplication, from Cylindrical's
    // seam-vertex duplication, or from a prior exploding UV method) would otherwise let those
    // duplicates over-weight whatever region they cluster in, biasing both the centroid and the
    // PCA-detected axis below.
    std::vector<glm::vec3> uniquePositions = computeUniquePositions(vertices);
    glm::vec3 centroid(0.0f);
    for (const auto& p : uniquePositions)
        centroid += p;
    centroid /= static_cast<float>(uniquePositions.size());

    // Determine the sphere's polar axis, either via PCA or from config.sphericalAxis - either way,
    // NOT hardcoded to world-Y. This used to hardcode latitude from localPos.y and longitude from
    // atan2(z,x) - correct only for a sphere/spheroid whose pole-to-pole axis happens to already be
    // world-Y, for the identical reason generateCylindrical() was broken before its own
    // axis-detection fix (see its doc comment). A true, non-elongated sphere has no geometrically
    // "correct" axis at all - every direction is equally valid - so PCA here mainly matters for a
    // spheroid whose poles are genuinely meant to sit along a specific (non-Y) direction; for a
    // perfect sphere it just picks some arbitrary-but-consistent axis, no worse than the old
    // hardcoded-Y default.
    glm::vec3 polarAxis, equatorX, equatorY;

    if (config.sphericalAutoDetectAxis)
    {
        glm::mat3 covariance(0.0f);
        for (const auto& pos : uniquePositions)
        {
            glm::vec3 p = pos - centroid;
            covariance[0] += p.x * p;
            covariance[1] += p.y * p;
            covariance[2] += p.z * p;
        }
        covariance /= static_cast<float>(uniquePositions.size());

        glm::vec3 eigenValues;
        glm::mat3 eigenVectors;
        computeEigenDecomposition(covariance, eigenValues, eigenVectors);

        // Same "odd one out" eigenvalue logic as generateCylindrical() - a spheroid flattened or
        // elongated along its true polar axis shows up as the outlier eigenvalue, whether largest
        // or smallest; a perfect sphere has all 3 nearly equal, so this just lands on whichever
        // axis numerical noise favors, an arbitrary-but-valid choice either way.
        int axisIdx = (eigenValues[0] - eigenValues[1] > eigenValues[1] - eigenValues[2]) ? 0 : 2;
        int uIdx = (axisIdx == 0) ? 1 : 0;
        int vIdx = (axisIdx == 2) ? 1 : 2;
        polarAxis = glm::normalize(glm::vec3(
            eigenVectors[0][axisIdx], eigenVectors[1][axisIdx], eigenVectors[2][axisIdx]));
        equatorX = glm::normalize(glm::vec3(
            eigenVectors[0][uIdx], eigenVectors[1][uIdx], eigenVectors[2][uIdx]));
        equatorY = glm::normalize(glm::vec3(
            eigenVectors[0][vIdx], eigenVectors[1][vIdx], eigenVectors[2][vIdx]));
    }
    else
    {
        polarAxis = (glm::length(config.sphericalAxis) > 1e-6f)
            ? glm::normalize(config.sphericalAxis)
            : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 fallback = (std::abs(polarAxis.y) < 0.99f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
        equatorX = glm::normalize(glm::cross(fallback, polarAxis));
        equatorY = glm::cross(polarAxis, equatorX);
    }

    // Everywhere below expresses a normalized direction as localPos = (equatorX-component,
    // polarAxis-component, equatorY-component) - i.e. re-using .x/.y/.z exactly as the rest of this
    // function already did (localPos.y for latitude, atan2(localPos.z, localPos.x) for longitude),
    // just in the DETECTED basis instead of raw world axes. Rotating into this basis preserves unit
    // length, so every existing downstream computation (asin/atan2/pole-threshold comparisons)
    // stays correct unchanged.
    auto toLocalBasis = [&](const glm::vec3& worldDir) -> glm::vec3 {
        return glm::vec3(glm::dot(worldDir, equatorX), glm::dot(worldDir, polarAxis), glm::dot(worldDir, equatorY));
        };

    if (kSphericalVerbose)
    {
        float minDist = FLT_MAX, maxDist = -FLT_MAX;
        int nearZero = 0;
        for (const auto& v : vertices)
        {
            float d = glm::length(v.Position - centroid);
            minDist = std::min(minDist, d);
            maxDist = std::max(maxDist, d);
            if (d < 1e-4f) ++nearZero;
        }
        qDebug() << "[Spherical] centroid=(" << centroid.x << centroid.y << centroid.z
                  << ") vertexCount=" << vertices.size()
                  << " distFromCentroid min=" << minDist << "max=" << maxDist
                  << " nearZeroDist(<1e-4)=" << nearZero;
    }

    // Only the numerically-degenerate apex itself (where x/z underflow to ~0 and atan2's result
    // is effectively noise) needs special handling - NOT a whole latitude band. 0.98 (~11 degrees
    // from the pole) was catching an entire cap of ordinary, well-defined-longitude vertices and
    // snapping each one to whatever its neighboring triangle happened to average to, tearing the
    // checker pattern into wedges near the pole instead of just resolving the one true singularity.
    const float poleThreshold = 0.9999f;
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
            glm::vec3 localPos = toLocalBasis(glm::normalize(vertex.Position - centroid));
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

    // Circular mean of a set of U coordinates (points on the circle) - correctly handles
    // wraparound via vector averaging, for ANY spread of input values, unlike comparing each one
    // against a single fixed reference with a hardcoded +-1 nudge (see fixSeamCrossing's old
    // implementation, replaced below): that left any vertex within 0.5 of the reference untouched
    // even when it was still far from its OWN triangle-mates - e.g. corners at U={0.1,0.4,0.9}
    // only ever got 0.1 and 0.9 pulled together, stranding 0.4. That's exactly the shape of a
    // wide-longitude-span triangle common right next to a pole, where a physically compact
    // triangle can span a large fraction of the full circle.
    auto circularMeanU = [](std::initializer_list<float> us) -> float {
        float sx = 0.0f, sy = 0.0f;
        for (float u : us)
        {
            float ang = u * 2.0f * static_cast<float>(M_PI);
            sx += std::cos(ang);
            sy += std::sin(ang);
        }
        float meanAngle = std::atan2(sy, sx);
        return meanAngle / (2.0f * static_cast<float>(M_PI));
        };

    // Re-expresses u as meanU plus whichever integer-shifted copy of u (..., u-1, u, u+1, ...)
    // sits closest to meanU, rather than only ever nudging by a hardcoded +-1.
    auto unwrapTowardU = [](float u, float meanU) -> float {
        float delta = u - meanU;
        while (delta > 0.5f) delta -= 1.0f;
        while (delta <= -0.5f) delta += 1.0f;
        return meanU + delta;
        };

    // Helper function to fix seam crossing: unwrap all 3 corners toward their own circular mean.
    auto fixSeamCrossing = [&](std::array<glm::vec2, 3>& uvs) {
            const float meanU = circularMeanU({ uvs[0].x, uvs[1].x, uvs[2].x });
            for (int i = 0; i < 3; ++i)
                uvs[i].x = unwrapTowardU(uvs[i].x, meanU);
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
                // Single pole vertex - interpolate U from the other two vertices' circular mean
                // (same primitive fixSeamCrossing uses). This function used to skip seam-crossing
                // correction entirely for any pole triangle, so a pole triangle whose two real
                // vertices straddled U=0/U=1 (very common right next to the seam, since every
                // longitude converges at the pole) got averaged straight across the seam - e.g.
                // ~0.02 and ~0.98 blending to ~0.5 - tearing the pole into a fan of wedges.
                int a = (poleIndex + 1) % 3;
                int b = (poleIndex + 2) % 3;
                const float meanU = circularMeanU({ uvs[a].x, uvs[b].x });
                uvs[a].x = unwrapTowardU(uvs[a].x, meanU);
                uvs[b].x = unwrapTowardU(uvs[b].x, meanU);
                uvs[poleIndex].x = meanU;

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
                localPos[j] = toLocalBasis(glm::normalize(triVertices[j].Position - centroid));
                uvs[j] = calculateSphericalUV(localPos[j]);
            }

            std::array<glm::vec2, 3> uvsRaw = uvs;

            // Handle pole triangles first
            bool isPoleTriangle = handlePoleTriangle(uvs, localPos);

            // Handle seam crossing if not a pole triangle. Gated on seamlessSpherical so
            // unchecking it actually disables the repair (matching generateCylindrical()/
            // generateTorus()'s "Seamless" checkbox) instead of only relocating an always-on
            // fix - previously this ran unconditionally and the checkbox only changed
            // optimalSeamLongitude above, so toggling it had no visible effect on a symmetric
            // test mesh. Pole handling stays unconditional - that's duplicatePoleVertices's job.
            bool didSeamFix = false;
            if (config.seamlessSpherical && !isPoleTriangle && crossesSeam(uvs, optimalSeamLongitude))
            {
                fixSeamCrossing(uvs);
                didSeamFix = true;
            }

            if (kSphericalVerbose && (isPoleTriangle || didSeamFix))
            {
                float minEdge = std::min({
                    glm::length(triVertices[0].Position - triVertices[1].Position),
                    glm::length(triVertices[1].Position - triVertices[2].Position),
                    glm::length(triVertices[2].Position - triVertices[0].Position) });
                qDebug() << "[Spherical] tri" << (i / 3)
                         << (isPoleTriangle ? "POLE" : "SEAM")
                         << "minEdgeLen=" << minEdge
                         << "rawU=(" << uvsRaw[0].x << uvsRaw[1].x << uvsRaw[2].x << ")"
                         << "rawV=(" << uvsRaw[0].y << uvsRaw[1].y << uvsRaw[2].y << ")"
                         << "fixedU=(" << uvs[0].x << uvs[1].x << uvs[2].x << ")"
                         << "fixedV=(" << uvs[0].y << uvs[1].y << uvs[2].y << ")"
                         << "localPosY=(" << localPos[0].y << localPos[1].y << localPos[2].y << ")";
            }

            // Apply a SINGLE per-triangle U shift, not three independent per-vertex wraps.
            // handlePoleTriangle()/fixSeamCrossing() above align this triangle's 3 UV values with
            // each other by design pushing one or two of them outside [0,1] (that's the whole
            // mechanism they use to resolve a seam crossing - e.g. a pole vertex landing at U=1.00
            // while its two triangle-mates sit at 1.02 and 0.98, all mutually consistent). Wrapping
            // each vertex back into [0,1] independently (the old per-vertex while-loops) silently
            // undid that alignment for whichever vertex happened to land outside the range - 1.02
            // reverts to 0.02 - while leaving its triangle-mates at 1.00/0.98 untouched,
            // reintroducing the very seam-split within one triangle that was just fixed (rendered as
            // UVs spanning almost the full texture width - a fan of thin stripes).
            //
            // The shift must be derived from the trio's AVERAGE, not from a single arbitrary corner
            // (uvs[0], "whichever vertex this triangle happens to list first" - unrelated to
            // neighboring triangles' vertex order): two physically-adjacent triangles straddling the
            // seam can both have their circular-mean-aligned trios centered near the SAME true
            // longitude (e.g. both near U=-0.02) yet, purely by chance of index order, have
            // DIFFERENT signs in their own "corner 0" (-0.05 for one, +0.02 for the other) - a
            // single-corner floor() then sends one triangle to the ~1.0 range and leaves the other at
            // ~0.0, a full UV unit apart, even though both are internally consistent and represent
            // the same physical seam location. That produced a regular zigzag of alternating
            // "individually fine but mutually misplaced" triangles right along the seam (confirmed
            // via a fresh finely-tessellated test sphere: pole distortion was gone, but this
            // misplacement pattern persisted unchanged). The average is symmetric under reordering,
            // so both triangles land on the same integer shift regardless of which corner is which.
            const float triAvgU = (uvs[0].x + uvs[1].x + uvs[2].x) / 3.0f;
            const float triShift = -std::floor(triAvgU);

            // Create final vertices with corrected UVs
            std::array<unsigned int, 3> newTriIndices;
            for (int j = 0; j < 3; ++j)
            {
                Vertex newVertex = triVertices[j];
                glm::vec2 finalUV = uvs[j];
                finalUV.x += triShift;
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
                localPos[j] = toLocalBasis(glm::normalize(vertices[triIndices[j]].Position - centroid));
                uvs[j] = calculateSphericalUV(localPos[j]);
            }

            // Handle pole triangles
            bool isPoleTriangle = handlePoleTriangle(uvs, localPos);

            // Handle seam crossing if not a pole triangle (see the identical comment on the
            // duplicatePoleVertices==true branch above for why this is gated on seamlessSpherical)
            if (config.seamlessSpherical && !isPoleTriangle && crossesSeam(uvs, optimalSeamLongitude))
            {
                fixSeamCrossing(uvs);
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

    // Weld to unique positions first - same reasoning as generateCylindrical()'s identical weld
    // (see its doc comment): regenerating with Hybrid on a mesh that already carries duplicate-
    // position vertices (from a prior exploding UV method, or from Cylindrical's own seam-vertex
    // duplication if Hybrid just dispatched there) would otherwise let those duplicates over-
    // weight whatever region they cluster in, skewing the statistics this function uses to CHOOSE
    // which method to dispatch to.
    std::vector<glm::vec3> uniquePositions = computeUniquePositions(vertices);

    glm::vec3 mean(0.0f);
    for (const auto& p : uniquePositions)
        mean += p;
    mean /= static_cast<float>(uniquePositions.size());

    // PCA still needed for the principal AXIS DIRECTIONS (used below to build an oriented bounding
    // box), but no longer for the classification decision itself - see below.
    glm::mat3 covariance(0.0f);
    for (const auto& pos : uniquePositions)
    {
        glm::vec3 p = pos - mean;
        covariance[0] += p.x * p;
        covariance[1] += p.y * p;
        covariance[2] += p.z * p;
    }
    covariance /= static_cast<float>(uniquePositions.size());

    glm::vec3 eigenValues;
    glm::mat3 eigenVectors;
    computeEigenDecomposition(covariance, eigenValues, eigenVectors);

    // Extent along each principal axis (an oriented bounding box), NOT raw eigenvalues (variance).
    // Confirmed via real test meshes: a true sphere (radius exactly constant everywhere) can still
    // show one eigenvalue nearly double the other two purely because its tessellation happens to
    // cluster more vertices along one axis than another - variance is a vertex-DENSITY statistic,
    // not a shape statistic. Extent (max-min projected onto an axis) doesn't care how many
    // vertices sit where along that axis, only how far apart the extremes are - a stable,
    // tessellation-independent measure of actual geometric elongation.
    glm::vec3 axis0(eigenVectors[0][0], eigenVectors[1][0], eigenVectors[2][0]);
    glm::vec3 axis1(eigenVectors[0][1], eigenVectors[1][1], eigenVectors[2][1]);
    glm::vec3 axis2(eigenVectors[0][2], eigenVectors[1][2], eigenVectors[2][2]);

    float min0 = FLT_MAX, max0 = -FLT_MAX;
    float min1 = FLT_MAX, max1 = -FLT_MAX;
    float min2 = FLT_MAX, max2 = -FLT_MAX;
    for (const auto& p : uniquePositions)
    {
        glm::vec3 rel = p - mean;
        float c0 = glm::dot(rel, axis0);
        float c1 = glm::dot(rel, axis1);
        float c2 = glm::dot(rel, axis2);
        min0 = std::min(min0, c0); max0 = std::max(max0, c0);
        min1 = std::min(min1, c1); max1 = std::max(max1, c1);
        min2 = std::min(min2, c2); max2 = std::max(max2, c2);
    }

    // eigenValues/eigenVectors are sorted by descending VARIANCE, which doesn't always match
    // descending EXTENT (that's the whole point of using extent instead) - re-sort explicitly.
    std::array<float, 3> extents = { max0 - min0, max1 - min1, max2 - min2 };
    std::sort(extents.begin(), extents.end(), std::greater<float>());
    const float d0 = extents[0], d1 = std::max(extents[1], 1e-6f), d2 = std::max(extents[2], 1e-6f);
    const float extentRatio = d0 / d2;

    // "One extent very different from the other two, which are close to each other" is ALSO the
    // signature of a thin flat plate - not just a cylinder - and extentRatio alone can't tell them
    // apart (confirmed on the Bezier saddle-patch test asset: extents ~(7.17, 7.16, 0.73), a high
    // extentRatio of ~9.8 that looks "elongated" but is really just thin, not axis-symmetric).
    // The two cases differ in WHICH extent is the odd one out: a cylinder's two SMALLEST extents
    // are close to each other (d1~=d2, a round cross-section) while its LARGEST stands alone; a
    // flat plate's two LARGEST are close to each other (d0~=d1, the flat face) while its SMALLEST
    // stands alone. crossSectionRatio being close to 1 is what actually distinguishes "genuinely
    // axis-elongated" from "just thin".
    const float crossSectionRatio = d1 / d2;

    // How much distance-from-centroid varies across the mesh - the most direct, tessellation-
    // independent "is this actually round" signal available: a true sphere has EXACTLY constant
    // radius everywhere, no matter how unevenly it's tessellated, whereas a cylinder (even a short
    // one whose bounding box looks roughly cubic) has rim vertices measurably farther from center
    // than cap-center vertices. Checked ahead of the elongation test so a genuine sphere is caught
    // reliably regardless of its bounding-box aspect ratio.
    float radiusMean = 0.0f;
    for (const auto& p : uniquePositions)
        radiusMean += glm::length(p - mean);
    radiusMean /= static_cast<float>(uniquePositions.size());

    float radiusVar = 0.0f;
    for (const auto& p : uniquePositions)
    {
        float d = glm::length(p - mean) - radiusMean;
        radiusVar += d * d;
    }
    radiusVar /= static_cast<float>(uniquePositions.size());

    const float radiusVarRatio = (radiusMean > 1e-6f) ? radiusVar / (radiusMean * radiusMean) : 0.0f;
    // 0.01 was too loose: a cone (real, structured radius variation - the apex is much closer to
    // centroid than the rim, radiusVarRatio confirmed 0.00787 on a real test cone) slipped under
    // it and got dispatched to Spherical, which visually looked worse than Angle-Based on that
    // shape. The three genuinely-round test meshes (Sphere.obj, and two coarse cylinders whose
    // minimal 2-ring/no-cap-center topology makes them geometrically indistinguishable from a
    // sphere by vertex position alone) all measured radiusVarRatio ~1e-13 - essentially exact
    // floating-point-noise zero. 0.001 sits comfortably below the cone's real value while staying
    // far above that noise floor.
    const bool isRoughlySpherical = radiusVarRatio < 0.001f;

    if (kHybridVerbose)
    {
        qDebug() << "[Hybrid] vertexCount=" << vertices.size()
                 << "uniquePositions=" << uniquePositions.size()
                 << "extents(d0,d1,d2)=(" << d0 << d1 << d2 << ")"
                 << "extentRatio(d0/d2)=" << extentRatio
                 << "crossSectionRatio(d1/d2)=" << crossSectionRatio
                 << "radiusMean=" << radiusMean << "radiusVarRatio=" << radiusVarRatio
                 << "isRoughlySpherical=" << isRoughlySpherical;
    }

    if (isRoughlySpherical)
    {
        if (kHybridVerbose)
            qDebug() << "[Hybrid] -> Spherical (radiusVarRatio < 0.001)";
        return generateSpherical(vertices, indices, config, sourceVertexMap);
    }
    else if (extentRatio > 2.0f && crossSectionRatio < 1.5f)
    {
        if (kHybridVerbose)
            qDebug() << "[Hybrid] -> Cylindrical (extentRatio > 2.0, round cross-section)";
        return generateCylindrical(vertices, indices, config, sourceVertexMap);
    }
    else if (extentRatio > 2.0f)
    {
        if (kHybridVerbose)
            qDebug() << "[Hybrid] -> Planar (elongated but not round cross-section - a thin plate, not a cylinder)";
        return generatePlanar(vertices, indices, config, sourceVertexMap);
    }
    else if (extentRatio < 1.5f)
    {
        if (kHybridVerbose)
            qDebug() << "[Hybrid] -> AngleBased (extentRatio < 1.5, not spherical by radius)";
        return generateAngleBased(vertices, indices, config, sourceVertexMap);
    }
    else
    {
        if (kHybridVerbose)
            qDebug() << "[Hybrid] -> Planar (1.5 <= extentRatio <= 2.0)";
        return generatePlanar(vertices, indices, config, sourceVertexMap);
    }
}


// Method 6: Angle-based Smart UV (similar to Blender's Smart UV)
bool UVGenerator::generateAngleBasedSmartUV(
    std::vector<Vertex>& vertices,
    std::vector<unsigned int>& indices,
    const UVConfig& config,
    std::vector<unsigned int>* sourceVertexMap,
    const std::vector<std::pair<glm::vec3, glm::vec3>>* userSeamEdges)
{
    if (vertices.empty() || indices.empty())
        return false;

    // 1. Build triangle list and detect seams
    std::vector<MeshTriangle> triangles;
    buildTriangleList(vertices, indices, triangles);

    std::vector<std::pair<uint32_t, uint32_t>> seams;
    findSeams(vertices, triangles, seams, config.angleThreshold, userSeamEdges);

    std::vector<UVIsland> islands;
    createUVIslands(triangles, seams, islands);

    // 2. Unwrap per island using PCA (per-triangle UVs)
    std::unordered_map<unsigned int, std::array<glm::vec2, 3>> triangleUVs;

    for (int i = 0; i < static_cast<int>(islands.size()); ++i)
    {
        // Do NOT pre-normalize each island independently into its own full [0,1] UV box when
        // packing will run afterward (config.enablePacking) - packWithXAtlas() below already
        // places/scales every island proportionately in the shared atlas. Pre-squishing a large,
        // many-triangle island into [0,1] on its own destroys relative size information and, for
        // a large enough island, can compress individual triangles' UV footprint below float32's
        // usable precision - confirmed the dominant cause of a reported hang: xatlas::Generate()
        // was fed ~82% zero-UV-area triangles on a real test model and never returned. Most of
        // that came from here, not from the (separate, smaller) axis-selection bug fixed inside
        // unwrapIslandPCA() itself just above.
        unwrapIslandPCA(vertices, triangles, islands[i], triangleUVs, !config.enablePacking);
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


// Method 8: ARAP - same seam/island detection as generateAngleBased(), but a real
// distortion-minimizing per-island unfold (CGAL ARAP_parameterizer_3) in place of
// unwrapIsland()'s flat orthogonal projection, with a graceful per-island fallback to
// unwrapIslandPCA() (see tryUnwrapIslandARAP()'s doc comment for why that fallback is safe).
bool UVGenerator::generateARAP(
    std::vector<Vertex>& vertices,
    std::vector<unsigned int>& indices,
    const UVConfig& config,
    std::vector<unsigned int>* sourceVertexMap,
    const std::vector<std::pair<glm::vec3, glm::vec3>>* userSeamEdges)
{
    if (vertices.empty() || indices.empty())
        return false;

    std::vector<MeshTriangle> triangles;
    buildTriangleList(vertices, indices, triangles);
    if (triangles.empty())
        return false;

    std::vector<std::pair<unsigned int, unsigned int>> seams;
    findSeams(vertices, triangles, seams, config.angleThreshold, userSeamEdges);

    std::vector<UVIsland> islands;
    createUVIslands(triangles, seams, islands);

    if (kARAPVerbose)
    {
        size_t singleTriIslands = 0;
        for (const UVIsland& isl : islands)
            if (isl.triangles.size() == 1)
                ++singleTriIslands;
        qDebug() << "[ARAP] " << triangles.size() << "triangles," << seams.size() << "seams ->"
                 << islands.size() << "islands (" << singleTriIslands << "are single-triangle)";
    }

    // Per-triangle-corner UVs, same convention as generateAngleBasedSmartUV()'s PCA path - lets
    // the flatten step below stay identical regardless of which per-island method (ARAP or the PCA
    // fallback) actually produced a given island's result.
    std::unordered_map<unsigned int, std::array<glm::vec2, 3>> triangleUVs;

    int arapSucceeded = 0, arapFellBack = 0;
    for (const UVIsland& island : islands)
    {
        if (tryUnwrapIslandARAP(vertices, triangles, island, config, triangleUVs))
            ++arapSucceeded;
        else
        {
            ++arapFellBack;
            // Same reasoning as generateAngleBasedSmartUV()'s identical call - see its comment.
            unwrapIslandPCA(vertices, triangles, island, triangleUVs, !config.enablePacking);
        }
    }
    if (kARAPVerbose)
        qDebug() << "[ARAP] " << islands.size() << "islands total -" << arapSucceeded
                 << "used real ARAP," << arapFellBack << "fell back to PCA";

    // Flatten: expand vertices/indices so every island's UVs stay seam-continuous within
    // themselves without colliding with a neighboring island's UVs at a shared 3D vertex (unlike
    // generateAngleBased()'s shared-uvs-by-original-index approach - see this method's doc comment
    // in UVGenerator.h for why that would undermine ARAP's whole point). Mirrors
    // generateAngleBasedSmartUV()'s identical flatten step exactly.
    std::vector<Vertex> newVertices;
    std::vector<unsigned int> newIndices;
    std::vector<unsigned int> newSourceVertexMap;

    for (size_t triIdx = 0; triIdx < triangles.size(); ++triIdx)
    {
        auto it = triangleUVs.find(static_cast<unsigned int>(triIdx));
        if (it == triangleUVs.end())
            continue;

        const MeshTriangle& tri = triangles[triIdx];
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

    if (newVertices.empty())
        return false;

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


// Method 9: Torus projection (donut-style major/minor angle mapping)
bool UVGenerator::generateTorus(
    std::vector<Vertex>& vertices,
    std::vector<unsigned int>& indices,
    const UVConfig& config,
    std::vector<unsigned int>* sourceVertexMap)
{
    if (vertices.empty() || indices.empty())
        return false;

    // Weld by exact position before computing centroid/covariance - same reasoning as
    // generateCylindrical()'s identical weld (see its doc comment).
    std::vector<glm::vec3> uniquePositions = computeUniquePositions(vertices);

    glm::vec3 centroid(0.0f);
    for (const auto& p : uniquePositions)
        centroid += p;
    centroid /= static_cast<float>(uniquePositions.size());

    // Determine the torus's axis of revolution, either via PCA or from config.torusAxis - same
    // detection as generateCylindrical() (see its doc comment for the full reasoning): a torus's
    // thin axial extent (tube thickness) is the PCA "outlier eigenvalue" against the two large,
    // roughly-equal in-plane eigenvalues of the major-radius disk.
    glm::vec3 axis, planeX, planeY;

    if (config.torusAutoDetectAxis)
    {
        glm::mat3 covariance(0.0f);
        for (const auto& pos : uniquePositions)
        {
            glm::vec3 p = pos - centroid;
            covariance[0] += p.x * p;
            covariance[1] += p.y * p;
            covariance[2] += p.z * p;
        }
        covariance /= static_cast<float>(uniquePositions.size());

        glm::vec3 eigenValues;
        glm::mat3 eigenVectors;
        computeEigenDecomposition(covariance, eigenValues, eigenVectors);
        // eigenValues/eigenVectors come back sorted descending (e0 >= e1 >= e2).

        int axisIdx = (eigenValues[0] - eigenValues[1] > eigenValues[1] - eigenValues[2]) ? 0 : 2;
        int uIdx = (axisIdx == 0) ? 1 : 0;
        int vIdx = (axisIdx == 2) ? 1 : 2;
        axis = glm::normalize(glm::vec3(
            eigenVectors[0][axisIdx], eigenVectors[1][axisIdx], eigenVectors[2][axisIdx]));
        planeX = glm::normalize(glm::vec3(
            eigenVectors[0][uIdx], eigenVectors[1][uIdx], eigenVectors[2][uIdx]));
        planeY = glm::normalize(glm::vec3(
            eigenVectors[0][vIdx], eigenVectors[1][vIdx], eigenVectors[2][vIdx]));

        if (kTorusVerbose)
        {
            qDebug() << "[Torus] axis-detect: e0=" << eigenValues[0] << "e1=" << eigenValues[1]
                     << "e2=" << eigenValues[2] << "axisIdx=" << axisIdx
                     << "axis=(" << axis.x << axis.y << axis.z << ")"
                     << "centroid=(" << centroid.x << centroid.y << centroid.z << ")";
        }
    }
    else
    {
        // Manual axis: fall back to world-Y if the user left it at zero-length rather than
        // normalizing a zero vector into garbage - same guard as generateCylindrical().
        axis = (glm::length(config.torusAxis) > 1e-6f)
            ? glm::normalize(config.torusAxis)
            : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 fallback = (std::abs(axis.y) < 0.99f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
        planeX = glm::normalize(glm::cross(fallback, axis));
        planeY = glm::cross(axis, planeX);
    }

    // Estimate major radius R (mean distance from the axis line) - exact, not just unbiased, for
    // any uniformly-angle-sampled tube cross-section, by roots-of-unity symmetry, regardless of
    // tessellation coarseness. Degrades only for a genuinely partial/non-uniform minor sweep (a
    // half-pipe profile) - an accepted limitation, same class as generateSpherical()'s shared-
    // vertex "last writer wins" limitation, not something engineered around here.
    float R = 0.0f;
    for (const auto& pos : uniquePositions)
    {
        glm::vec3 rel = pos - centroid;
        R += glm::length(glm::vec2(glm::dot(rel, planeX), glm::dot(rel, planeY)));
    }
    R /= static_cast<float>(uniquePositions.size());

    // Minor radius r: plain mean distance from the R-radius center circle, matching this file's
    // existing preference for simple arithmetic means over RMS/L2 statistics (nothing else in this
    // file uses RMS). Used both for the degeneracy check below and the torusMinorScale aspect
    // correction - the UV formula itself only needs R.
    float r = 0.0f;
    for (const auto& pos : uniquePositions)
    {
        glm::vec3 rel = pos - centroid;
        float h = glm::dot(rel, axis);
        float radial = glm::length(glm::vec2(glm::dot(rel, planeX), glm::dot(rel, planeY)));
        r += glm::length(glm::vec2(radial - R, h));
    }
    r /= static_cast<float>(uniquePositions.size());

    // A proper ring torus (R > r) has no per-vertex singularity anywhere - unlike a sphere, no
    // pole-style special case is needed below. R <= r (a spindle/horn torus, where the tube
    // passes through or near the axis) IS a real degeneracy - every vertex on the tube's inner
    // ring has radial ~= 0, making theta below as numerically unstable as atan2(z,x) at a sphere's
    // pole, but for a whole RING of vertices rather than two isolated points. Guarded via logging,
    // not rejected - matches every other method's "produce a distorted-but-valid result for
    // atypical input" posture rather than failing outright.
    if (kTorusVerbose)
    {
        qDebug() << "[Torus] uniquePositions=" << uniquePositions.size()
                 << "R=" << R << "r=" << r
                 << (R <= r ? "DEGENERATE (R<=r, spindle/horn torus)" : "OK");
    }

    const float seamRotation = config.torusSeamRotation;

    // Raw (unwrapped, un-seam-corrected) torus UV for a position already expressed relative to
    // centroid. theta (major angle around axis) -> U; phi (minor/tube angle around the tube's own
    // circular cross-section) -> V.
    auto calculateTorusUV = [&](const glm::vec3& rel) -> glm::vec2 {
        float px = glm::dot(rel, planeX);
        float py = glm::dot(rel, planeY);
        float h = glm::dot(rel, axis);
        float radial = glm::length(glm::vec2(px, py));

        float theta = atan2(py, px) + seamRotation;
        while (theta < 0.0f) theta += 2.0f * M_PI;
        while (theta >= 2.0f * M_PI) theta -= 2.0f * M_PI;

        float phi = atan2(h, radial - R);
        while (phi < 0.0f) phi += 2.0f * M_PI;
        while (phi >= 2.0f * M_PI) phi -= 2.0f * M_PI;

        return glm::vec2(theta / (2.0f * M_PI), phi / (2.0f * M_PI));
        };

    // Same generic circular-mean/unwrap primitives generateSpherical() established (capture-less,
    // operate on any periodic [0,1) value - nothing U-specific about them) reused verbatim for
    // BOTH U and V independently: a torus is doubly periodic (S^1 x S^1), and theta/phi above are
    // computed from disjoint inputs (theta only reads px/py, phi only reads h/radial), so a fix
    // applied to one axis never needs to know about the other.
    auto circularMean = [](std::initializer_list<float> us) -> float {
        float sx = 0.0f, sy = 0.0f;
        for (float u : us)
        {
            float ang = u * 2.0f * static_cast<float>(M_PI);
            sx += std::cos(ang);
            sy += std::sin(ang);
        }
        float meanAngle = std::atan2(sy, sx);
        return meanAngle / (2.0f * static_cast<float>(M_PI));
        };
    auto unwrapToward = [](float u, float meanU) -> float {
        float delta = u - meanU;
        while (delta > 0.5f) delta -= 1.0f;
        while (delta <= -0.5f) delta += 1.0f;
        return meanU + delta;
        };
    auto crossesSeamOnAxis = [](const std::array<glm::vec2, 3>& uvs, int comp) -> bool {
        for (int i = 0; i < 3; ++i)
            for (int j = i + 1; j < 3; ++j)
                if (std::abs(uvs[i][comp] - uvs[j][comp]) > 0.5f)
                    return true;
        return false;
        };

    // Always explodes vertices (one set per triangle-corner) - no shared-vertex fallback. A
    // torus's seam is a GRID (both a U=0 ring and a V=0 ring), so the "last writer wins"
    // ambiguity a shared-vertex path would carry (as generateSpherical()'s non-explode branch
    // does for its single seam line) is proportionally far worse here.
    std::vector<Vertex> finalVertices;
    std::vector<unsigned int> finalIndices;
    std::vector<unsigned int> finalSourceVertexMap;
    finalVertices.reserve(vertices.size());
    finalIndices.reserve(indices.size());

    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        std::array<unsigned int, 3> triIndices = { indices[i], indices[i + 1], indices[i + 2] };
        std::array<Vertex, 3> triVertices = {
            vertices[triIndices[0]], vertices[triIndices[1]], vertices[triIndices[2]] };
        std::array<glm::vec2, 3> uvs;

        for (int j = 0; j < 3; ++j)
            uvs[j] = calculateTorusUV(triVertices[j].Position - centroid);

        if (config.seamlessTorus)
        {
            if (crossesSeamOnAxis(uvs, 0))
            {
                const float meanU = circularMean({ uvs[0].x, uvs[1].x, uvs[2].x });
                for (int j = 0; j < 3; ++j)
                    uvs[j].x = unwrapToward(uvs[j].x, meanU);
            }
            if (crossesSeamOnAxis(uvs, 1))
            {
                const float meanV = circularMean({ uvs[0].y, uvs[1].y, uvs[2].y });
                for (int j = 0; j < 3; ++j)
                    uvs[j].y = unwrapToward(uvs[j].y, meanV);
            }
        }

        // Single per-triangle shift per axis, derived from the trio's average - NOT an arbitrary
        // single corner and NOT three independent per-vertex wraps, for the exact reason
        // generateSpherical()'s "Apply a SINGLE per-triangle U shift" comment documents: wrapping
        // each vertex back into [0,1) independently would silently undo the alignment above for
        // whichever corner happened to land outside that range, reintroducing the very seam-split
        // just fixed. Applied independently per axis since U and V are unrelated circles here.
        const float triAvgU = (uvs[0].x + uvs[1].x + uvs[2].x) / 3.0f;
        const float triAvgV = (uvs[0].y + uvs[1].y + uvs[2].y) / 3.0f;
        const float triShiftU = -std::floor(triAvgU);
        const float triShiftV = -std::floor(triAvgV);

        std::array<unsigned int, 3> newTriIndices;
        for (int j = 0; j < 3; ++j)
        {
            Vertex newVertex = triVertices[j];
            glm::vec2 finalUV(uvs[j].x + triShiftU, uvs[j].y + triShiftV);
            // torusMinorScale/torusScale are cosmetic post-corrections applied AFTER the seam fix
            // and shift above, which both rely on U/V wrapping at exact integer boundaries -
            // scaling first would break that periodicity.
            finalUV.y *= config.torusMinorScale;
            finalUV *= config.torusScale;
            applyUVTransforms(finalUV, config);
            newVertex.TexCoords[0] = finalUV;

            finalVertices.push_back(newVertex);
            finalSourceVertexMap.push_back(triIndices[j]);
            newTriIndices[j] = static_cast<unsigned int>(finalVertices.size() - 1);
        }

        finalIndices.insert(finalIndices.end(), { newTriIndices[0], newTriIndices[1], newTriIndices[2] });
    }

    vertices = std::move(finalVertices);
    indices = std::move(finalIndices);
    if (sourceVertexMap)
        *sourceVertexMap = std::move(finalSourceVertexMap);

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

    // Position-welded index per vertex, used only for topoIndices[3] (edge-adjacency/topology in
    // findSeams()/createUVIslands()) - NOT for indices[3], which still addresses the real vertex
    // array for position/normal/UV lookups. Needed because a mesh that already went through an
    // exploding UV method (Smart Project/Angle-Based Smart UV/ARAP itself, all of which duplicate a
    // vertex per triangle-corner with no shared indices at all) has zero shared vertex INDICES
    // between adjacent triangles even though they still share the same 3D position - without this,
    // seam/island detection sees every triangle as topologically isolated (confirmed real bug:
    // running ARAP after Smart Project produced one degenerate 1-triangle "island" per triangle,
    // instead of the real connected islands). Exact position equality (no epsilon) is sufficient and
    // safe here specifically because vertex explosion only ever copies a Vertex struct verbatim -
    // the duplicated positions are bit-for-bit identical to the original, never perturbed.
    std::vector<unsigned int> weldedIndex(vertices.size());
    {
        struct Vec3Hash
        {
            std::size_t operator()(const glm::vec3& p) const
            {
                std::size_t h = std::hash<float>()(p.x);
                h ^= std::hash<float>()(p.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
                h ^= std::hash<float>()(p.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
                return h;
            }
        };
        struct Vec3Equal
        {
            bool operator()(const glm::vec3& a, const glm::vec3& b) const
            {
                return a.x == b.x && a.y == b.y && a.z == b.z;
            }
        };
        std::unordered_map<glm::vec3, unsigned int, Vec3Hash, Vec3Equal> firstIndexAtPosition;
        firstIndexAtPosition.reserve(vertices.size());
        for (size_t i = 0; i < vertices.size(); ++i)
        {
            const auto [it, inserted] = firstIndexAtPosition.try_emplace(
                vertices[i].Position, static_cast<unsigned int>(i));
            weldedIndex[i] = it->second;
        }
        if (kARAPVerbose)
            qDebug() << "[ARAP] buildTriangleList: welded" << vertices.size() << "vertices down to"
                     << firstIndexAtPosition.size() << "unique positions";
    }

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

        tri.topoIndices[0] = weldedIndex[tri.indices[0]];
        tri.topoIndices[1] = weldedIndex[tri.indices[1]];
        tri.topoIndices[2] = weldedIndex[tri.indices[2]];

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
    float angleThreshold,
    const std::vector<std::pair<glm::vec3, glm::vec3>>* userSeamEdges)
{
    seams.clear();

    std::unordered_map<Edge, std::vector<uint32_t>> edgeToTriangles;

    // 1. Build edge -> triangle adjacency. Keyed by topoIndices (position-welded), not indices -
    // see MeshTriangle::topoIndices' doc comment for why: raw indices alone would see zero
    // adjacency at all on a mesh a prior exploding UV method already ran on.
    for (uint32_t i = 0; i < triangles.size(); ++i)
    {
        const MeshTriangle& tri = triangles[i];
        for (int j = 0; j < 3; ++j)
        {
            uint32_t a = tri.topoIndices[j];
            uint32_t b = tri.topoIndices[(j + 1) % 3];
            edgeToTriangles[Edge(a, b)].push_back(i);
        }
    }

    const float cosThreshold = std::cos(glm::radians(angleThreshold));

    // Tracks which edges already produced a seam via the angle-threshold pass below, so the
    // user-marked pass further down doesn't emit the same (t0,t1) pair twice.
    std::unordered_set<Edge> emittedEdges;

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
            emittedEdges.insert(entry.first);
        }
    }

    // 3. Force in any user-marked seam edges, unconditionally (skipping the angle comparison
    // entirely - that's the whole point, a marked seam holds even on a smooth region). Positions
    // are welded to topoIndices via the SAME exact-equality convention buildTriangleList() uses -
    // safe here because both sides trace back to the SAME unmodified vertices array at this
    // point in every caller (buildTriangleList() -> findSeams(), before any exploding).
    if (userSeamEdges && !userSeamEdges->empty())
    {
        struct Vec3Hash
        {
            std::size_t operator()(const glm::vec3& p) const
            {
                std::size_t h = std::hash<float>()(p.x);
                h ^= std::hash<float>()(p.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
                h ^= std::hash<float>()(p.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
                return h;
            }
        };
        struct Vec3Equal
        {
            bool operator()(const glm::vec3& a, const glm::vec3& b) const
            {
                return a.x == b.x && a.y == b.y && a.z == b.z;
            }
        };

        std::unordered_map<glm::vec3, uint32_t, Vec3Hash, Vec3Equal> positionToTopoIndex;
        positionToTopoIndex.reserve(vertices.size());
        for (const MeshTriangle& tri : triangles)
        {
            for (int j = 0; j < 3; ++j)
                positionToTopoIndex.try_emplace(vertices[tri.topoIndices[j]].Position, tri.topoIndices[j]);
        }

        for (const auto& [posA, posB] : *userSeamEdges)
        {
            const auto itA = positionToTopoIndex.find(posA);
            const auto itB = positionToTopoIndex.find(posB);
            if (itA == positionToTopoIndex.end() || itB == positionToTopoIndex.end())
                continue; // stale/unresolved mark - caller reports this, not us

            const Edge edge(itA->second, itB->second);

            // Linear scan rather than edgeToTriangles.find(edge)/emittedEdges.count(edge) -
            // confirmed via diagnostic logging that .find() spuriously reported "not found" for
            // a key that demonstrably existed (same hash, same bucket, operator== true via
            // manual scan) - an unexplained unordered_map lookup anomaly for this Edge/hash
            // combination in this build. The linear scan is the mechanism that was actually
            // verified to behave correctly; edgeToTriangles is small (bounded by this mesh's own
            // edge count) so the cost is negligible for a one-off, user-triggered Generate click.
            bool alreadyEmitted = false;
            for (const Edge& e : emittedEdges)
            {
                if (e == edge) { alreadyEmitted = true; break; }
            }
            if (alreadyEmitted)
                continue; // already a seam via the angle-threshold pass above

            const std::vector<uint32_t>* adjTris = nullptr;
            for (const auto& entry : edgeToTriangles)
            {
                if (entry.first == edge) { adjTris = &entry.second; break; }
            }
            if (!adjTris || adjTris->size() != 2)
                continue; // not a real interior edge on this mesh (boundary edge or no match)

            seams.emplace_back((*adjTris)[0], (*adjTris)[1]);
            emittedEdges.insert(edge);
        }
    }
}


void UVGenerator::createUVIslands(const std::vector<MeshTriangle>& triangles,
    const std::vector<std::pair<uint32_t, uint32_t>>& seams,
    std::vector<UVIsland>& islands)
{
    islands.clear();
    const size_t triangleCount = triangles.size();

    // Build fast edge -> triangle adjacency. Keyed by topoIndices (position-welded), not indices -
    // see MeshTriangle::topoIndices' doc comment for why.

    std::unordered_map<Edge, std::vector<uint32_t>> edgeMap;

    for (uint32_t i = 0; i < triangleCount; ++i)
    {
        const auto& tri = triangles[i];
        edgeMap[Edge(tri.topoIndices[0], tri.topoIndices[1])].push_back(i);
        edgeMap[Edge(tri.topoIndices[1], tri.topoIndices[2])].push_back(i);
        edgeMap[Edge(tri.topoIndices[2], tri.topoIndices[0])].push_back(i);
    }

    // Build seam edge set for fast lookup
    std::unordered_set<Edge> seamEdges;
    for (const auto& s : seams)
    {
        const auto& t0 = triangles[s.first];
        const auto& t1 = triangles[s.second];
        for (int i = 0; i < 3; ++i)
        {
            uint32_t a = t0.topoIndices[i];
            uint32_t b = t0.topoIndices[(i + 1) % 3];
            Edge e = Edge(a, b);

            // Check if edge exists in both triangles
            for (int j = 0; j < 3; ++j)
            {
                uint32_t a1 = t1.topoIndices[j];
                uint32_t b1 = t1.topoIndices[(j + 1) % 3];
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
                Edge e = Edge(tri.topoIndices[ei], tri.topoIndices[(ei + 1) % 3]);
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

    // Checked on the RAW (pre-normalize) cross product, not the normalized result - normal
    // exactly parallel to world-up (a perfectly horizontal island, e.g. a flat panel authored
    // with an up-facing normal) makes this cross product a true zero vector, and
    // glm::normalize() of a zero vector is NaN, not a small/zero length - so testing
    // length(tangent) AFTER normalizing never actually detects the degenerate case (NaN < 0.1f
    // is always false), silently propagating NaN into every UV this island produces. Confirmed
    // real bug: a flat, up-facing island rendered with a solid, untextured-looking appearance
    // (every vertex sampling the same texel) after a marked seam split it off as its own island.
    const glm::vec3 tangentRaw = glm::cross(normal, glm::vec3(0, 1, 0));
    glm::vec3 tangent = (glm::length(tangentRaw) < 0.1f)
        ? glm::normalize(glm::cross(normal, glm::vec3(1, 0, 0)))
        : glm::normalize(tangentRaw);
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

    // Which of the 3 eigenvectors is the island's "out of plane" (normal) direction is NOT safe
    // to assume from eigenvalue rank alone - confirmed real bug via a reported hang: for a mesh
    // with many single-triangle islands whose two TRUE in-plane eigenvalues happen to be close to
    // each other (near-equilateral/near-isosceles triangles, common in a uniformly-tessellated
    // mesh), Jacobi's numerical eigenvalue ordering can occasionally rank the true near-zero
    // (normal) eigenvalue ABOVE one of the genuinely in-plane ones, so blindly taking "the top 2
    // by eigenvalue" as axis1/axis2 silently substitutes the island's OWN normal direction for one
    // of the true in-plane axes - projecting onto (an in-plane axis, the near-normal axis)
    // collapses every triangle in the island to near-zero UV area. Confirmed via diagnostic
    // logging: xatlas::Generate() was fed ~87% zero-UV-area triangles on the reported model and
    // never returned - not a NaN/Inf issue (both were 0), a genuine degenerate-input hang.
    // The island's own area-weighted average face normal (same computation unwrapIsland() already
    // uses) is a robust, purely geometric signal for "which direction is out-of-plane" -
    // independent of any eigenvalue tie - so use IT to pick which eigenvector to exclude, rather
    // than trusting the eigenvalue sort order.
    glm::vec3 avgNormal(0.0f);
    for (unsigned int triIdx : island.triangles)
    {
        const MeshTriangle& tri = triangles[triIdx];
        avgNormal += tri.normal * tri.area;
    }
    avgNormal = (glm::length(avgNormal) > 1e-8f)
        ? glm::normalize(avgNormal)
        : triangles[island.triangles[0]].normal;

    int normalIdx = 0;
    float bestAlignment = -1.0f;
    for (int i = 0; i < 3; ++i)
    {
        const glm::vec3 candidate(eigenVectors[0][i], eigenVectors[1][i], eigenVectors[2][i]);
        const float alignment = std::abs(glm::dot(candidate, avgNormal));
        if (alignment > bestAlignment)
        {
            bestAlignment = alignment;
            normalIdx = i;
        }
    }
    int uIdx = -1, vIdx = -1;
    for (int i = 0; i < 3; ++i)
    {
        if (i == normalIdx) continue;
        if (uIdx < 0) uIdx = i;
        else vIdx = i;
    }

    glm::vec3 axis1 = glm::normalize(glm::vec3(eigenVectors[0][uIdx], eigenVectors[1][uIdx], eigenVectors[2][uIdx]));
    glm::vec3 axis2 = glm::normalize(glm::vec3(eigenVectors[0][vIdx], eigenVectors[1][vIdx], eigenVectors[2][vIdx]));

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


bool UVGenerator::tryUnwrapIslandARAP(const std::vector<Vertex>& vertices,
    const std::vector<MeshTriangle>& triangles,
    const UVIsland& island,
    const UVConfig& config,
    std::unordered_map<unsigned int, std::array<glm::vec2, 3>>& triangleUVs)
{
    using Kernel  = CGAL::Exact_predicates_inexact_constructions_kernel;
    using Point_3 = Kernel::Point_3;
    using Mesh    = CGAL::Surface_mesh<Point_3>;
    namespace PMP = CGAL::Polygon_mesh_processing;
    namespace SMP = CGAL::Surface_mesh_parameterization;

    if (island.triangles.empty())
        return false;

    // Local soup built directly from this island's own triangles, deduplicating by POSITION-WELDED
    // index (tri.topoIndices, not tri.indices) - not repaired/reoriented (unlike every other CGAL
    // soup-to-mesh conversion in this codebase). That's deliberate: repair_polygon_soup()/
    // orient_polygon_soup() can duplicate/reorder points, which would break the direct "local point i
    // == Mesh::Vertex_index(i)" correspondence this function relies on to map ARAP's per-vertex UV
    // output back onto the right original vertex (confirmed by reading polygon_soup_to_polygon_mesh.h
    // directly: it calls add_vertex() once per input point, in input order, with no dependency on
    // repair/orient ever having run).
    //
    // Keying by tri.indices[i] (the RAW per-corner index) instead of topoIndices would silently
    // build a disconnected soup - not just a wrong-but-plausible one - whenever the input mesh was
    // already vertex-exploded by a prior UV pass (Smart Project/Angle-Based Smart UV/a previous ARAP
    // run all duplicate 3 unique vertices per triangle-corner, see buildTriangleList()'s doc comment):
    // adjacent triangles in the SAME island never repeat a raw index even though they share a
    // position, so every triangle would contribute 3 brand-new points and the local Surface_mesh
    // would end up as N disconnected 1-triangle components instead of one connected topological
    // disk - is_polygon_soup_a_polygon_mesh() below still accepts that (disjoint triangles are a
    // valid, just disconnected, polygon soup), and parameterize() would then run over a mesh that
    // isn't actually the disk it looks like, producing garbage rather than a clean failure. Welding
    // by topoIndices (already computed in buildTriangleList() for exactly this reason) keeps the
    // local mesh's connectivity faithful to the island's real 3D topology regardless of how the
    // input vertices happen to be indexed.
    //
    // is_polygon_soup_a_polygon_mesh() below is used purely as a REJECT gate - an island failing it
    // (e.g. a non-manifold junction the existing dihedral-angle seam detection doesn't gate on, see
    // findSeams()'s doc comment) just isn't attempted with ARAP, it falls back to unwrapIslandPCA()
    // same as a topology failure below.
    std::vector<Point_3> points;
    std::vector<std::array<std::size_t, 3>> faces;
    std::unordered_map<unsigned int, std::size_t> weldedToLocal;
    points.reserve(island.triangles.size());
    faces.reserve(island.triangles.size());

    auto localIndex = [&](unsigned int origIdx, unsigned int weldedIdx) -> std::size_t {
        auto it = weldedToLocal.find(weldedIdx);
        if (it != weldedToLocal.end())
            return it->second;
        const std::size_t newIdx = points.size();
        const glm::vec3& p = vertices[origIdx].Position;
        points.emplace_back(p.x, p.y, p.z);
        weldedToLocal.emplace(weldedIdx, newIdx);
        return newIdx;
    };

    for (unsigned int triIdx : island.triangles)
    {
        const MeshTriangle& tri = triangles[triIdx];
        faces.push_back({
            localIndex(tri.indices[0], tri.topoIndices[0]),
            localIndex(tri.indices[1], tri.topoIndices[1]),
            localIndex(tri.indices[2], tri.topoIndices[2])
        });
    }

    if (kARAPVerbose)
        qDebug() << "[ARAP] island:" << island.triangles.size() << "triangles," << points.size() << "points";

    if (points.size() < 3 || faces.empty() || !PMP::is_polygon_soup_a_polygon_mesh(faces))
    {
        if (kARAPVerbose)
            qDebug() << "[ARAP]   -> reject: not a valid polygon soup (points" << points.size()
                     << "faces" << faces.size() << ")";
        return false;
    }

    Mesh mesh;
    PMP::polygon_soup_to_polygon_mesh(points, faces, mesh);
    if (mesh.number_of_vertices() == 0 || mesh.number_of_faces() == 0)
    {
        if (kARAPVerbose)
            qDebug() << "[ARAP]   -> reject: empty mesh after polygon_soup_to_polygon_mesh";
        return false;
    }

    // A closed island (no border at all - e.g. most of a sphere flood-filled as one island under a
    // permissive angleThreshold) can never be a topological disk - skip straight to the PCA
    // fallback rather than attempting parameterize() at all.
    // Free-function form (found via ADL, works for any FaceGraph model) - matches exactly what
    // CGAL's own parameterize() uses for its is_border() precondition check (confirmed by reading
    // parameterize.h directly), rather than assuming a same-named Surface_mesh member exists.
    Mesh::Halfedge_index borderHalfedge;
    bool foundBorder = false;
    for (Mesh::Halfedge_index hd : mesh.halfedges())
    {
        if (CGAL::is_border(hd, mesh))
        {
            borderHalfedge = hd;
            foundBorder = true;
            break;
        }
    }
    if (!foundBorder)
    {
        if (kARAPVerbose)
            qDebug() << "[ARAP]   -> reject: no border halfedge (closed island, not a topological disk)";
        return false;
    }

    using ARAP = SMP::ARAP_parameterizer_3<Mesh>;
    auto uvmap = mesh.add_property_map<Mesh::Vertex_index, Kernel::Point_2>(
        "h:uv", Kernel::Point_2(0, 0)).first;

    // ARAP::NT (its lambda constructor's parameter type) is a PRIVATE member typedef in the real,
    // non-Doxygen-only branch of this class (confirmed by reading the header directly - it's only
    // public in the doxygen-documentation-generation branch, never in actually-compiled code), so
    // it can't be named here. It resolves to Kernel::FT, which is plain double for
    // Exact_predicates_inexact_constructions_kernel - pass a double directly instead of trying to
    // spell the (inaccessible) type out.
    //
    // parameterize() reports failure (non-disk topology, a non-convex/degenerate border, an
    // unsolvable linear system, ...) via a graceful Error_code rather than crashing or asserting -
    // confirmed by reading Error_code.h directly - so every failure mode here is just "return
    // false", letting the caller fall back to unwrapIslandPCA() for this island.
    const SMP::Error_code err = SMP::parameterize(
        mesh, ARAP(static_cast<double>(config.arapLambda)), borderHalfedge, uvmap);
    if (err != SMP::OK)
    {
        if (kARAPVerbose)
            qDebug() << "[ARAP]   -> reject: parameterize() failed:" << SMP::get_error_message(err);
        return false;
    }
    if (kARAPVerbose)
        qDebug() << "[ARAP]   -> OK: real ARAP unfold succeeded";

    for (unsigned int triIdx : island.triangles)
    {
        const MeshTriangle& tri = triangles[triIdx];
        std::array<glm::vec2, 3> uvSet;
        for (int i = 0; i < 3; ++i)
        {
            const Mesh::Vertex_index v(weldedToLocal[tri.topoIndices[i]]);
            const Kernel::Point_2& uv = uvmap[v];
            uvSet[i] = glm::vec2(static_cast<float>(CGAL::to_double(uv.x())),
                                 static_cast<float>(CGAL::to_double(uv.y())));
        }
        triangleUVs[triIdx] = uvSet;
    }

    return true;
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


void UVGenerator::packUVIslands(const std::vector<MeshTriangle>& triangles,
    std::vector<UVIsland>& islands,
    std::vector<glm::vec2>& uvs,
    float padding)
{
    // Per-island normalize (see this method's header doc comment for why NOT a single combined
    // bounding box across every island).
    if (uvs.empty()) return;

    for (const UVIsland& island : islands)
    {
        if (island.triangles.empty())
            continue;

        // Deduplicate vertex indices first - island.triangles are TRIANGLE indices, and a normal
        // (non-exploded) mesh has vertices shared between several triangles within the same
        // island, so walking triangles-then-corners directly would revisit a shared vertex once
        // per triangle that references it. Confirmed real bug: applying "(uv - minUV) / size" a
        // SECOND time to an already-normalized value corrupts it (operates on the wrong range),
        // and since different vertices are shared by different numbers of triangles, different
        // vertices got corrupted by different amounts - producing an inconsistent, banded
        // distortion instead of a clean uniform rescale.
        std::unordered_set<unsigned int> islandVertexIndices;
        for (unsigned int triIdx : island.triangles)
        {
            const MeshTriangle& tri = triangles[triIdx];
            for (int j = 0; j < 3; ++j)
                islandVertexIndices.insert(tri.indices[j]);
        }

        glm::vec2 minUV(std::numeric_limits<float>::max());
        glm::vec2 maxUV(std::numeric_limits<float>::lowest());
        for (unsigned int vIdx : islandVertexIndices)
        {
            minUV = glm::min(minUV, uvs[vIdx]);
            maxUV = glm::max(maxUV, uvs[vIdx]);
        }

        const glm::vec2 size = maxUV - minUV;
        if (size.x <= 0.0f || size.y <= 0.0f)
            continue;

        for (unsigned int vIdx : islandVertexIndices)
            uvs[vIdx] = (uvs[vIdx] - minUV) / size;
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
    // Feed the UVs the caller already computed (Angle-Based/PCA/Smart Project/ARAP/...) into
    // xatlas as a chart-generation hint - without this, xatlas::Generate() below had no UV input
    // at all and silently computed its own charts completely from scratch, discarding whatever
    // unwrap algorithm actually ran. Confirmed by reading xatlas.h directly: vertexUvData is
    // "optional...provided as a hint to the chart generator", and separately gated by
    // ChartOptions::useInputMeshUvs (defaults to false) below - both are required together, setting
    // only one silently doesn't do anything.
    meshDecl.vertexUvData = uvs.data();
    meshDecl.vertexUvStride = sizeof(glm::vec2);
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
    chartOptions.useInputMeshUvs = true; // see the vertexUvData comment above - both are required
    xatlas::PackOptions packOptions{};
    packOptions.padding = 2;
    packOptions.texelsPerUnit = 1.0f;

    if (kXAtlasVerbose)
    {
        int nanInfPositions = 0, nanInfUVs = 0, zeroAreaUVTris = 0, zeroAreaPosTris = 0;
        for (const auto& p : positions)
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
                ++nanInfPositions;
        for (const auto& uv : uvs)
            if (!std::isfinite(uv.x) || !std::isfinite(uv.y))
                ++nanInfUVs;
        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const glm::vec2& u0 = uvs[indices[i]];
            const glm::vec2& u1 = uvs[indices[i + 1]];
            const glm::vec2& u2 = uvs[indices[i + 2]];
            const float uvArea = std::abs((u1.x - u0.x) * (u2.y - u0.y) - (u2.x - u0.x) * (u1.y - u0.y));
            if (uvArea < 1e-12f)
                ++zeroAreaUVTris;

            const glm::vec3& p0 = positions[indices[i]];
            const glm::vec3& p1 = positions[indices[i + 1]];
            const glm::vec3& p2 = positions[indices[i + 2]];
            const float posArea = glm::length(glm::cross(p1 - p0, p2 - p0));
            if (posArea < 1e-9f)
                ++zeroAreaPosTris;
        }
        qDebug() << "[xatlas] vertexCount=" << positions.size() << "triangleCount=" << (indices.size() / 3)
                 << "nanInfPositions=" << nanInfPositions << "nanInfUVs=" << nanInfUVs
                 << "zeroAreaUVTris=" << zeroAreaUVTris << "zeroAreaPosTris=" << zeroAreaPosTris
                 << "-- about to call xatlas::Generate()";
    }

    xatlas::Generate(atlas, chartOptions, packOptions);

    if (kXAtlasVerbose)
        qDebug() << "[xatlas] xatlas::Generate() returned, meshCount=" << atlas->meshCount
                 << "chartCount=" << atlas->chartCount;

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

    // Deliberately NOT clamped to [0,1]: a per-method Scale > 1 is meant to
    // tile the texture (more repeats across the surface), and Scale < 1 is
    // meant to tile more densely - both rely on values landing outside
    // [0,1] and wrapping via the texture's GL_REPEAT sampler (the default
    // wrap mode - see Material.cpp), the same way the spherical/torus
    // seam-continuity fix already relies on small out-of-range excursions
    // wrapping correctly. Clamping here silently flattened Scale>1 into a
    // clipped/smeared patch instead of repeating it. A texture explicitly
    // set to clamp-to-edge wrap mode clamps identically at the GPU sampler
    // either way, so leaving this unclamped costs nothing in that case.
}

// Utility methods
std::vector<glm::vec3> UVGenerator::computeUniquePositions(const std::vector<Vertex>& vertices)
{
    struct Vec3Hash
    {
        std::size_t operator()(const glm::vec3& p) const
        {
            std::size_t h = std::hash<float>()(p.x);
            h ^= std::hash<float>()(p.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<float>()(p.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
    struct Vec3Equal
    {
        bool operator()(const glm::vec3& a, const glm::vec3& b) const
        {
            return a.x == b.x && a.y == b.y && a.z == b.z;
        }
    };

    std::unordered_set<glm::vec3, Vec3Hash, Vec3Equal> seen;
    seen.reserve(vertices.size());
    std::vector<glm::vec3> uniquePositions;
    uniquePositions.reserve(vertices.size());
    for (const auto& v : vertices)
    {
        if (seen.insert(v.Position).second)
            uniquePositions.push_back(v.Position);
    }
    return uniquePositions;
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

    // Convergence threshold scaled to the matrix's own magnitude, NOT a fixed absolute value -
    // confirmed real bug found via generateTorus(): a covariance matrix built from real mesh
    // positions has values in the hundreds (or more), and floating-point summation noise on its
    // off-diagonals is routinely far larger than a tiny fixed epsilon like 1e-10, so the old
    // absolute threshold effectively NEVER triggered early-exit - every caller always ran the
    // full maxIterations regardless of scale. That was silently harmless for a matrix with
    // clearly-distinct eigenvalues (Jacobi still converges correctly well before the cap), but a
    // matrix with a genuinely (near-)degenerate eigenvalue pair - confirmed real case: a
    // Y-axis-symmetric torus, whose two in-plane eigenvalues are mathematically identical -
    // starts hitting a numerically unstable rotation angle (atan2 of two near-zero, noise-
    // dominated values, since both the off-diagonal AND the diagonal difference are tiny for
    // that pivot) once it's already effectively converged. Running 50 more such iterations
    // anyway injected a spurious rotation that measurably leaked variance from the true axis
    // into an adjacent one - a torus built exactly Y-axis-symmetric came back with its detected
    // axis tilted ~20 degrees off Y. Stopping as soon as the matrix is converged RELATIVE to its
    // own scale avoids ever reaching that noise-dominated regime.
    float scale = std::max({ std::abs(m[0][0]), std::abs(m[1][1]), std::abs(m[2][2]) });
    if (scale < 1e-12f) scale = 1.0f; // degenerate/zero input - fall back to a small absolute floor
    const float epsilon = 1e-6f * scale;

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
