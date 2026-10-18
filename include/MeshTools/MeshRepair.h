#pragma once

// Unlike this codebase's other CGAL-using headers (e.g. UVGenerator.h), this one DOES expose
// CGAL types in its public signature - deliberately. The repair pipeline below is shared by
// SceneMesh.cpp (booleanUnionMeshes()/subdivideMesh()/reconstructSurfaceFromPoints()) and
// MeasurementController.cpp (resolveMeasurementGeodesicDistance()) - genuinely different
// translation units, so it can't stay an anonymous-namespace/internal-linkage helper the way
// each of those used to duplicate it individually. Every caller already includes CGAL
// Surface_mesh headers directly for its own purposes, so this doesn't newly couple anything
// that wasn't already CGAL-dependent.
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Surface_mesh.h>
#include "MeshVertex.h"
#include <array>
#include <cstddef>
#include <vector>

// What repairTriangleSoup() actually found/fixed - backs a result dialog's status label.
// Deliberately limited to what CGAL's own repair functions cleanly report (soup-level point/face
// count deltas from repair_polygon_soup(), duplicate_non_manifold_vertices()'s own returned
// count, and does_self_intersect() before/after) rather than finer-grained categories CGAL
// doesn't actually distinguish (e.g. "duplicate" vs "degenerate" within repair_polygon_soup()'s
// single combined pass).
struct MeshRepairReport
{
    bool succeeded = false;        // false only if the soup could not become a valid mesh at all
    bool wasAlreadyValid = false;  // soup size unchanged, no non-manifold fix, no self-intersections found
    std::size_t soupPointsRemoved = 0;
    std::size_t soupFacesRemoved = 0;
    // Combines two distinct fixes that both resolve the same class of defect (a vertex shared by
    // disconnected face fans, no edge between them): points orient_polygon_soup() itself had to
    // duplicate to make the soup combinatorially manifold (its own duplicate_singular_vertices()
    // step - confirmed via CGAL 6.2's orient_polygon_soup.h source this is what actually fixes the
    // common "pinched"/bowtie case, before polygon_soup_to_polygon_mesh() ever runs), plus
    // duplicate_non_manifold_vertices()'s own returned count for whatever non-manifoldness survives
    // into the built Mesh. Verified empirically against RepairMeshTest.obj/
    // TwoTrianglesSharingVertex.obj/ThreeTrianglesSharingVertex.obj: without the first term this
    // field read 0 for all three even though the fix demonstrably happened (vertex count grew by
    // exactly the expected N-1 per an N-way fan).
    std::size_t nonManifoldVerticesFixed = 0;
    bool hadSelfIntersections = false;
    bool selfIntersectionsResolved = false; // only meaningful when hadSelfIntersections is true
    // True when a residual (unresolved) self-intersection is very likely just the harmless
    // coincident-point byproduct of the orient_polygon_soup() duplication above, rather than a
    // genuine overlapping-geometry defect in the source mesh: two triangles that used to share a
    // non-manifold vertex now touch at a single coincident point (same position, different vertex
    // index) instead - not an overlapping area remove_self_intersections() could meaningfully fix,
    // which is exactly why it fails to resolve it. Heuristic (points duplicated for
    // non-manifoldness doesn't strictly prove every residual intersection traces back to one), but
    // grounded in orient_polygon_soup()'s own documented behavior ("producing a combinatorially
    // manifold but self-intersecting polyhedron"). Only meaningful when hadSelfIntersections is
    // true and selfIntersectionsResolved is false.
    bool selfIntersectionLikelyFromNonManifoldFix = false;
};

// Shared defect-cleanup pipeline for a world-space triangle soup: repair_polygon_soup ->
// orient_polygon_soup -> is_polygon_soup_a_polygon_mesh gate -> polygon_soup_to_polygon_mesh ->
// stitch_borders -> duplicate_non_manifold_vertices -> (if does_self_intersect)
// remove_self_intersections (best-effort). Deliberately does NOT check is_closed/
// does_bound_a_volume/orient_to_bound_a_volume - an open mesh (e.g. a single unclosed panel)
// stays open; callers that specifically need a closed, volume-bounding result (booleanUnionMeshes())
// layer that check on top of this function's output themselves, same as before this was factored
// out - see SceneMesh.cpp's tryBuildRepairedVolumeMesh().
class MeshRepair
{
public:
    using Kernel  = CGAL::Exact_predicates_inexact_constructions_kernel;
    using Point_3 = Kernel::Point_3;
    using Mesh    = CGAL::Surface_mesh<Point_3>;

    // maxSelfIntersectionSteps/trySmoothingForSelfIntersections are passed straight through to
    // remove_self_intersections()'s own number_of_iterations/use_smoothing named parameters -
    // defaulted to CGAL's own current defaults (7 / false, verified against the vendored CGAL
    // 6.2 repair_self_intersections.h) so every existing caller that doesn't pass these two
    // explicitly sees zero behavior change. use_smoothing=false means the smoothing-based repair
    // strategy never even runs by default - only hole-filling-based repair does; a caller that
    // wants the more thorough (slower) alternative strategy needs to opt in explicitly.
    static bool repairSoupToMesh(
        std::vector<Point_3> points,
        std::vector<std::array<std::size_t, 3>> faces,
        Mesh& outMesh,
        MeshRepairReport* report = nullptr,
        int maxSelfIntersectionSteps = 7,
        bool trySmoothingForSelfIntersections = false);

    // Converts a CGAL mesh into this app's own Vertex/index-buffer form with CREASE-AWARE normal
    // splitting, instead of a single averaged (smooth) normal per vertex position: a vertex's
    // incident faces are grouped into shading runs wherever consecutive face normals (in
    // halfedge-cyclic order) diverge past creaseAngleDegrees, and one duplicated vertex is
    // emitted per run rather than blending across the whole fan. Needed because a plain
    // compute_vertex_normals()-style average makes a genuinely sharp edge in the mesh's geometry
    // look incorrectly rounded/faceted-but-smoothed under lighting - confirmed as a real,
    // previously-debugged issue for booleanUnionMeshes()'s identical crease-splitting need (see
    // its doc comment in SceneMesh.cpp for the investigation that arrived at 15 degrees, the same
    // default AssImpModelLoader uses on import). Also skips near-zero-area degenerate faces (both
    // when accumulating normals and when writing the index buffer), same defense
    // booleanUnionMeshes()/subdivideMesh() already use.
    //
    // Unlike booleanUnionMeshes()'s own inline copy of this same pattern (which assumes a closed,
    // watertight mesh), this is safe for a mesh with open borders: a border halfedge has no
    // incident face, so it simply can never join a shading run - which is the geometrically
    // correct behavior at a real boundary anyway, not a special case that needs handling.
    static void buildCreaseAwareVertexBuffers(
        const Mesh& mesh,
        std::vector<Vertex>& outVertices,
        std::vector<unsigned int>& outIndices,
        float creaseAngleDegrees = 15.0f);
};
