
#pragma once
#include "SceneMesh.h"
#include "MeshAnalyzer.h"
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <utility>
#include <vector>


// UV Generation Configuration
struct UVConfig
{
    float sphericalScale = 1.0f;        // Scale factor for spherical UV
    float sphericalUVRotation = 0.0f;   // Radians; e.g. glm::radians(90.0f)
	bool duplicatePoleVertices = true; // Duplicate vertices at poles for seamless spherical UV
    float cylindricalScale = 1.0f;      // Scale factor for cylindrical UV
    glm::vec2 planarScale = glm::vec2(1.0f);  // Scale factor for planar UV
    bool flipV = false;                 // Flip V coordinate
    bool seamlessSpherical = true;      // Handle spherical UV seams
    bool seamlessCylindrical = true;    // Handle cylindrical UV seam (own field - see generateCylindrical())
    float cylindricalOffset = 0.0f;     // Offset for cylindrical wrapping
    float cylindricalSeamRotation = 0.0f; // radians

    // Cylinder axis: auto-detected via PCA by default (the actual mesh axis, which need not be
    // world-Y - see generateCylindrical()'s doc comment). Set cylindricalAutoDetectAxis to false
    // to override with an explicit axis instead, for the rare case PCA misjudges it (e.g. a
    // cylinder whose length is close to its diameter, or one with an attached feature - a handle,
    // a flange - that skews the covariance away from the true axis).
    bool cylindricalAutoDetectAxis = true;
    glm::vec3 cylindricalAxis = glm::vec3(0.0f, 1.0f, 0.0f); // only used when the above is false

    // Sphere polar axis: same idea as cylindricalAutoDetectAxis/cylindricalAxis above, for the
    // same reason - generateSpherical() used to hardcode world-Y as the pole-to-pole axis, wrong
    // for a sphere/spheroid modeled/imported at any other orientation. A true (non-elongated)
    // sphere has no geometrically "correct" axis at all - any choice is equally valid - so this
    // mainly matters for a spheroid whose poles are meant to sit along a specific direction, or to
    // pin the seam/pole location deterministically instead of leaving it to PCA noise.
    bool sphericalAutoDetectAxis = true;
    glm::vec3 sphericalAxis = glm::vec3(0.0f, 1.0f, 0.0f); // only used when the above is false

    // Torus axis of revolution: same auto-detect/override pattern as cylindricalAutoDetectAxis/
    // cylindricalAxis above - see generateTorus()'s doc comment. A torus's thin axial extent (tube
    // thickness) is the PCA "outlier eigenvalue" against the two large, roughly-equal in-plane
    // eigenvalues of the major-radius disk, the same detection generateCylindrical() already uses.
    bool torusAutoDetectAxis = true;
    glm::vec3 torusAxis = glm::vec3(0.0f, 1.0f, 0.0f); // only used when the above is false
    float torusScale = 1.0f;            // Scale factor for both torus UV axes
    float torusSeamRotation = 0.0f;     // radians; rotates the major (theta/U) seam
    bool seamlessTorus = true;          // Handle torus UV seams (both U and V are periodic)
    // Manual V-axis multiplier: a thin-tube torus (r << R, the common case - donuts, tires, pipe
    // elbows) naively maps both a ~2*pi*R and a ~2*pi*r sweep to the same [0,1] range, visibly
    // over-stretching the texture in one direction. Left as a plain manual multiplier (matching
    // cylindricalScale/sphericalScale) rather than auto-computed from the estimated r/R ratio.
    float torusMinorScale = 1.0f;

    // Angle-based unwrapping parameters
    float angleThreshold = 60.0f;       // Angle threshold for seam detection (degrees)
    float distortionWeight = 0.5f;      // Weight for distortion vs area preservation
    bool preserveAspectRatio = true;    // Preserve triangle aspect ratios
    float seamPadding = 0.02f;          // Padding around UV islands
    bool enableRelaxation = false;
    int relaxationIterations = 10; // Default number of smoothing passes
    bool enablePacking = true;

    // Whether the caller should bother resolving/passing user-marked seam edges (see
    // findSeams()'s userSeamEdges parameter) at all - a per-method opt-out so a session's marks
    // can be compared against without clearing the mark list. UVGenerator itself stays fully
    // static/stateless/QUuid-unaware; this flag is consumed by the CALLER (AssImpModelLoader::
    // regenerateUVs()/ViewportWidget::generateUVsForMeshes()), not by UVGenerator.
    bool useMarkedSeams = true;

    // Smart Project (ported from Blender's "Smart UV Project"): clusters triangles by
    // face-normal similarity, independent of mesh connectivity/seams.
    float smartProjectAngleLimit = 66.0f;  // Degrees; lower = more projection groups, higher = less distortion
    float smartProjectAreaWeight = 0.0f;   // 0..1; weights cluster-normal averaging by face area

    // ARAP (CGAL as-rigid-as-possible parameterization): reuses angleThreshold (seam detection,
    // same as Angle-Based) and seamPadding/enablePacking (packing, same as every island-based
    // method) - this is ARAP's own regularization weight, balancing the as-rigid-as-possible energy
    // term against the free-boundary solve. CGAL's own default is on this order of magnitude;
    // higher values bias toward preserving the boundary shape, lower toward local rigidity.
    float arapLambda = 1000.0f;
};

struct MeshTriangle
{
    unsigned int indices[3];

    // Position-welded counterpart of indices[3], used ONLY for edge-adjacency/topology purposes
    // (findSeams()/createUVIslands()'s edge maps) - see buildTriangleList()'s doc comment for why
    // this exists separately from indices[3] rather than just using it directly.
    unsigned int topoIndices[3];

    glm::vec3 normal;
    float area;
    bool visited;
};

struct UVIsland
{
    std::vector<unsigned int> triangles;
    glm::vec2 minUV, maxUV;
    float totalArea;
};

class UVGenerator
{
public:
    // Method 1: Angle-based unwrapping (most reliable for automation). userSeamEdges - see
    // findSeams()'s doc comment - is an optional set of local-space position pairs (from
    // SeamMarkingController, resolved by the caller) each forced into the seam set regardless of
    // angleThreshold, unioned with the automatic detection.
    static bool generateAngleBased(
        std::vector<Vertex>& vertices,
        std::vector<unsigned int>& indices,
        const UVConfig& config = UVConfig{},
        std::vector<unsigned int>* sourceVertexMap = nullptr,
        const std::vector<std::pair<glm::vec3, glm::vec3>>* userSeamEdges = nullptr);

    // Method 2: Cylindrical projection (good for organic shapes)
    static bool generateCylindrical(
        std::vector<Vertex>& vertices,
        std::vector<unsigned int>& indices,
        const UVConfig& config = UVConfig{},
        std::vector<unsigned int>* sourceVertexMap = nullptr);

    // Method 3: Spherical projection (good for rounded objects)
    static bool generateSpherical(
        std::vector<Vertex>& vertices,
        std::vector<unsigned int>& indices,
        const UVConfig& config = UVConfig{},
        std::vector<unsigned int>* sourceVertexMap = nullptr);

    // Method 4: Planar projection with automatic orientation
    static bool generatePlanar(
        std::vector<Vertex>& vertices,
        std::vector<unsigned int>& indices,
        const UVConfig& config = UVConfig{},
        std::vector<unsigned int>* sourceVertexMap = nullptr);

    // Method 5: Hybrid approach (combines multiple methods)
    static bool generateHybrid(
        std::vector<Vertex>& vertices,
        std::vector<unsigned int>& indices,
        const UVConfig& config = UVConfig{},
        std::vector<unsigned int>* sourceVertexMap = nullptr);

	// Method 6: Angle-based Smart UV (similar to Blender's Smart UV)
    /*static bool generateAngleBasedSmartUV(
        aiMesh* mesh,
        std::vector<Vertex>& vertices,
        std::vector<unsigned int>& indices,
        const UVConfig& config);*/

    static bool generateAngleBasedSmartUV(
        std::vector<Vertex>& vertices,
        std::vector<unsigned int>& indices,
        const UVConfig& config,
        std::vector<unsigned int>* sourceVertexMap = nullptr,
        const std::vector<std::pair<glm::vec3, glm::vec3>>* userSeamEdges = nullptr);

    // Method 7: Smart Project - ported from Blender's UV_OT_smart_project. Clusters
    // triangles by face-normal similarity (not mesh connectivity) and linearly projects
    // each cluster using an orthonormal basis built from its averaged normal.
    static bool generateSmartProject(
        std::vector<Vertex>& vertices,
        std::vector<unsigned int>& indices,
        const UVConfig& config = UVConfig{},
        std::vector<unsigned int>* sourceVertexMap = nullptr);

    // Method 8: ARAP (As-Rigid-As-Possible parameterization, CGAL
    // Surface_mesh_parameterization). Reuses the same seam/island detection as
    // generateAngleBased() (buildTriangleList/findSeams/createUVIslands), but replaces
    // unwrapIsland()'s flat orthogonal projection with a real distortion-minimizing unfold per
    // island. Falls back to unwrapIslandPCA() for any island CGAL can't parameterize (no border -
    // not a topological disk - or a numerical failure, both reported as a graceful CGAL Error_code
    // rather than a crash) - see the .cpp for why that fallback is safe and how it's detected.
    static bool generateARAP(
        std::vector<Vertex>& vertices,
        std::vector<unsigned int>& indices,
        const UVConfig& config = UVConfig{},
        std::vector<unsigned int>* sourceVertexMap = nullptr,
        const std::vector<std::pair<glm::vec3, glm::vec3>>* userSeamEdges = nullptr);

    // Method 10: LSCM (Least Squares Conformal Maps, CGAL Surface_mesh_parameterization).
    // Identical shape to generateARAP() - same seam/island detection, same
    // unwrapIslandPCA() fallback for any island CGAL can't parameterize - but a single
    // global linear solve instead of ARAP's iterative local/global passes, with no
    // tunable parameter equivalent to ARAP's lambda. Angle-preserving rather than
    // area-preserving.
    static bool generateLSCM(
        std::vector<Vertex>& vertices,
        std::vector<unsigned int>& indices,
        const UVConfig& config = UVConfig{},
        std::vector<unsigned int>* sourceVertexMap = nullptr,
        const std::vector<std::pair<glm::vec3, glm::vec3>>* userSeamEdges = nullptr);

    // Method 9: Torus projection (donut-style major/minor angle mapping). Axis auto-detected via
    // the same PCA "outlier eigenvalue" test as generateCylindrical() (or overridden via
    // config.torusAxis). Unlike Spherical/Cylindrical, a torus is DOUBLY periodic - both U (major
    // angle around the axis) and V (minor/tube angle around the tube's own circular cross-section)
    // wrap - so seam-crossing correction runs independently on each axis using the same circular-
    // mean primitives generateSpherical() already established (they're generic, not U-specific).
    // A proper ring torus (major radius R > minor radius r, both auto-estimated from the mesh) has
    // no per-vertex singularity anywhere, unlike a sphere's poles - R<=r (a spindle/horn torus,
    // where the tube passes through or near the axis) is the real degenerate case, guarded via
    // kTorusVerbose logging rather than rejected, matching every other method's "produce a
    // distorted-but-valid result for atypical input" posture. Always explodes vertices (one set
    // per triangle-corner, no shared-vertex fallback) - a torus's seam is a grid (both a U=0 ring
    // and a V=0 ring), so the "last writer wins" ambiguity a shared-vertex path would carry is
    // proportionally far worse here than generateSpherical()'s single seam line.
    static bool generateTorus(
        std::vector<Vertex>& vertices,
        std::vector<unsigned int>& indices,
        const UVConfig& config = UVConfig{},
        std::vector<unsigned int>* sourceVertexMap = nullptr);

private:
    // Helper methods for angle-based unwrapping
    static void buildTriangleList(const std::vector<Vertex>& vertices,
        const std::vector<unsigned int>& indices,
        std::vector<MeshTriangle>& triangles);

    // userSeamEdges: optional local-space position pairs (each an edge's two endpoints) to force
    // into seams regardless of angleThreshold - resolved/owned by the caller (ViewportWidget::
    // generateUVsForMeshes(), from SeamMarkingController's marks), welded here to topoIndices via
    // the same exact-position-equality convention buildTriangleList() already uses (safe for the
    // same reason: both sides trace back to the SAME unmodified vertices array at this point,
    // before any exploding). A pair that doesn't resolve to an existing 2-triangle-adjacent edge
    // (stale edgeIndex, or the mesh doesn't have that edge) is silently skipped here - the caller
    // is responsible for reporting "N of M could not be resolved" if it cares.
    static void findSeams(const std::vector<Vertex>& vertices,
        const std::vector<MeshTriangle>& triangles,
        std::vector<std::pair<unsigned int, unsigned int>>& seams,
        float angleThreshold,
        const std::vector<std::pair<glm::vec3, glm::vec3>>* userSeamEdges = nullptr);

    static void createUVIslands(const std::vector<MeshTriangle>& triangles,
        const std::vector<std::pair<unsigned int, unsigned int>>& seams,
        std::vector<UVIsland>& islands);

    // Writes into TWO outputs from the same per-corner computation: `uvs` (shared, indexed by
    // original vertex index - kept only so relaxUVs()'s neighbor-averaging, which needs shared
    // vertex adjacency, still has something to read/write) and, when non-null, `triangleUVs`
    // (indexed by triangle index, one UV per corner - see unwrapIslandPCA()'s doc comment for the
    // same map shape). `uvs` is unsafe wherever a vertex sits on a seam between two islands: the
    // island processed later in generateAngleBased()'s loop silently overwrites the earlier
    // island's value for that shared index, corrupting whichever island's packUVIslands() bounding
    // box happens to include it (confirmed real bug: a marked seam splitting a mesh into two
    // panels - the panel processed first came back with wildly non-square, skewed UVs once
    // packing was disabled, because one of its boundary vertices held the OTHER panel's UV by the
    // time packUVIslands() read it). `triangleUVs` never has this problem - each triangle is
    // written to exactly once, regardless of how many islands touch its vertices - which is why
    // generateAngleBased() uses it (via the same explode-to-one-vertex-per-corner step every other
    // UV method already takes) as its actual output, falling back to re-syncing from the (still
    // seam-unsafe) `uvs` array only when relaxation is enabled.
    static void unwrapIsland(const std::vector<Vertex>& vertices,
        const std::vector<MeshTriangle>& triangles,
        const UVIsland& island,
        std::vector<glm::vec2>& uvs,
        std::unordered_map<unsigned int, std::array<glm::vec2, 3>>* triangleUVs = nullptr);

    /*static void unwrapIslandPCA(const std::vector<Vertex>& vertices,
        const std::vector<Triangle>& triangles,
        const UVIsland& island,
        std::vector<glm::vec2>& uvs);*/

    static void unwrapIslandPCA(const std::vector<Vertex>& vertices,
        const std::vector<MeshTriangle>& triangles,
        const UVIsland& island,
        std::unordered_map<unsigned int, std::array<glm::vec2, 3>>& triangleUVs,
        bool normalizeUVs = true);

    // Attempts a real CGAL ARAP unfold of one island, writing per-triangle-corner UVs into
    // triangleUVs on success (same map/shape unwrapIslandPCA() writes into, so generateARAP()'s
    // caller-side flatten step doesn't need to know which one actually produced a given island's
    // result). Returns false - leaving triangleUVs untouched for this island's triangles - if the
    // island has no border (not a topological disk) or CGAL's parameterize() reports any other
    // Error_code; the caller falls back to unwrapIslandPCA() in that case.
    static bool tryUnwrapIslandARAP(const std::vector<Vertex>& vertices,
        const std::vector<MeshTriangle>& triangles,
        const UVIsland& island,
        const UVConfig& config,
        std::unordered_map<unsigned int, std::array<glm::vec2, 3>>& triangleUVs);

    // Same idea as tryUnwrapIslandARAP() (see its doc comment), a real CGAL LSCM unfold instead -
    // no UVConfig parameter needed since LSCM has no tunable parameter. Both share their
    // soup-building/border-detection/UV-mapback implementation via a private template helper in
    // UVGenerator.cpp (tryUnwrapIslandCGAL()) - only the CGAL Parameterizer_3 instance differs.
    static bool tryUnwrapIslandLSCM(const std::vector<Vertex>& vertices,
        const std::vector<MeshTriangle>& triangles,
        const UVIsland& island,
        std::unordered_map<unsigned int, std::array<glm::vec2, 3>>& triangleUVs);

    static void relaxUVs(
        const std::vector<MeshTriangle>& triangles,
        std::vector<glm::vec2>& uvs,
        const std::vector<UVIsland>& islands,
        const UVConfig& config,
        int iterations);

    // generateAngleBased()'s own packing-disabled fallback (mirrors unwrapIslandPCA()'s
    // normalizeUVs=true convention used by the other methods) - normalizes each island's UV range
    // INDEPENDENTLY into its own full [0,1] box, NOT a single combined bounding box across every
    // island together (islands with unrelated per-island bases can have wildly different absolute
    // UV magnitudes, so a single shared bbox would let one island dominate while another
    // collapses to a sliver). `uvs` is generateAngleBased()'s EXPLODED per-corner array (one entry
    // per triangle corner, in triangle order - triangle i's three corners live at uvs[3*i],
    // uvs[3*i+1], uvs[3*i+2]), not the shared per-vertex array unwrapIsland() also writes -
    // operating on the exploded form is what lets two islands sharing a seam vertex each get
    // normalized against their own true bounding box instead of one that's been corrupted by the
    // other island's UV value at that shared index (see unwrapIsland()'s doc comment).
    static void packUVIslands(const std::vector<UVIsland>& islands,
        std::vector<glm::vec2>& uvs);

    static void packWithXAtlas(
        std::vector<glm::vec2>& uvs,
        const std::vector<unsigned int>& indices,
        const std::vector<glm::vec3>& positions);

    // Utility methods
    static void applyUVTransforms(glm::vec2& uv, const UVConfig& config);

    // Each DISTINCT vertex position, first-occurrence order. Use this instead of the raw vertex
    // array before any UNWEIGHTED positional statistic (a centroid/mean, a PCA covariance) that's
    // meant to reflect the mesh's true shape - a vertex repeated at the same 3D position (from a
    // UV method's own seam-vertex duplication, from a prior exploding UV pass, or from ordinary
    // hard-edge/duplicate-on-import mesh authoring) carries no new positional information, so
    // counting it more than once silently over-weights whatever region it clusters in. See
    // generateCylindrical()'s doc comment for the confirmed regression this was introduced to fix
    // (auto-detect axis skewed after a manual-axis run duplicated seam vertices).
    static std::vector<glm::vec3> computeUniquePositions(const std::vector<Vertex>& vertices);
    static glm::vec3 calculateBounds(const std::vector<Vertex>& vertices,
        glm::vec3& minBounds, glm::vec3& maxBounds);
    static float calculateTriangleArea(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2);
    static glm::vec3 calculateTriangleNormal(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2);   
    static void computeEigenDecomposition(const glm::mat3& m, glm::vec3& eigenValues, glm::mat3& eigenVectors);
};
