#include "MeshRepair.h"

#include <CGAL/boost/graph/iterator.h>
#include <CGAL/Polygon_mesh_processing/manifoldness.h>
#include <CGAL/Polygon_mesh_processing/measure.h>
#include <CGAL/Polygon_mesh_processing/compute_normal.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/repair_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/repair_self_intersections.h>
#include <CGAL/Polygon_mesh_processing/self_intersections.h>
#include <CGAL/Polygon_mesh_processing/stitch_borders.h>

#include <cmath>
#include <unordered_map>

bool MeshRepair::repairSoupToMesh(
    std::vector<Point_3> points,
    std::vector<std::array<std::size_t, 3>> faces,
    Mesh& outMesh,
    MeshRepairReport* report)
{
    namespace PMP = CGAL::Polygon_mesh_processing;

    MeshRepairReport localReport;
    MeshRepairReport& r = report ? *report : localReport;
    r = MeshRepairReport{};

    const std::size_t pointsBefore = points.size();
    const std::size_t facesBefore = faces.size();

    // Cleans up duplicate points and degenerate/invalid/duplicate polygons in the raw soup -
    // points/faces are modified in place. Doesn't by itself fix non-manifold edges/winding
    // consistency/singular vertices (a vertex shared by two otherwise-disconnected triangle
    // fans, touching at a point but no shared edge) - orient_polygon_soup() below and
    // duplicate_non_manifold_vertices() further down handle those.
    PMP::repair_polygon_soup(points, faces);

    // orient_polygon_soup() returns false when it had to duplicate anything - CGAL's own doc
    // comment describes this as producing a "combinatorially manifold but self-intersecting"
    // result, not simply "harmless". The return value is intentionally ignored here: a
    // combinatorially valid (if locally self-intersecting near a duplicated seam) mesh is still
    // exactly what is_polygon_soup_a_polygon_mesh()/polygon_soup_to_polygon_mesh() need, and
    // does_self_intersect()/remove_self_intersections() below already handle geometric
    // self-intersection cleanup regardless of what introduced it.
    //
    // The size delta captured here is this function's OWN non-manifold-vertex fix, distinct from
    // duplicate_non_manifold_vertices() further down - orient_polygon_soup()'s internal
    // duplicate_singular_vertices() step (confirmed in CGAL 6.2's orient_polygon_soup.h) detects a
    // vertex shared by disconnected face fans (no edge between them - the common "pinched"/bowtie
    // case) and duplicates it right here, growing `points`. Without counting this, a mesh whose
    // only defect was exactly this - verified against RepairMeshTest.obj/
    // TwoTrianglesSharingVertex.obj/ThreeTrianglesSharingVertex.obj - reports 0 non-manifold
    // vertices fixed below even though the fix demonstrably happened.
    const std::size_t pointsBeforeOrient = points.size();
    PMP::orient_polygon_soup(points, faces);
    const std::size_t singularVerticesDuplicated =
        points.size() > pointsBeforeOrient ? points.size() - pointsBeforeOrient : 0;

    r.soupPointsRemoved = pointsBefore > points.size() ? pointsBefore - points.size() : 0;
    r.soupFacesRemoved = facesBefore > faces.size() ? facesBefore - faces.size() : 0;

    // polygon_soup_to_polygon_mesh() itself only asserts this precondition (a no-op in release
    // builds) rather than reporting failure - check it explicitly so a soup repair that couldn't
    // fully clean up fails this function cleanly instead of risking undefined behavior downstream.
    if (!PMP::is_polygon_soup_a_polygon_mesh(faces))
        return false;

    outMesh.clear();
    PMP::polygon_soup_to_polygon_mesh(points, faces, outMesh);
    PMP::stitch_borders(outMesh); // welds duplicate boundary halfedges left by the soup->mesh conversion

    // Returns the number of vertices CREATED to resolve non-manifold configurations that survived
    // into the built Mesh - added to singularVerticesDuplicated above (the same class of fix,
    // applied earlier at the soup level) rather than reported separately, since both represent
    // "a non-manifold vertex was split into multiple manifold ones". Never used anywhere in this
    // codebase before this shared helper (confirmed via search) - Mesh Union/Subdivide/Geodesic
    // Distance's own repair pipelines all skipped this step, so meshes with a non-manifold vertex
    // (common in real STEP/BREP imports) silently kept that defect through all three.
    r.nonManifoldVerticesFixed = singularVerticesDuplicated + PMP::duplicate_non_manifold_vertices(outMesh);

    r.hadSelfIntersections = PMP::does_self_intersect(outMesh);
    if (r.hadSelfIntersections)
    {
        // Best-effort - not guaranteed to fully succeed (up to max_steps rounds of smoothing then
        // local hole-refill internally; see CGAL's own docs). Still an improvement over leaving
        // self-intersections in place even when it can't fully resolve them, so its result isn't
        // itself treated as a failure condition here - only reported.
        PMP::experimental::remove_self_intersections(outMesh);
        r.selfIntersectionsResolved = !PMP::does_self_intersect(outMesh);

        // See MeshRepairReport::selfIntersectionLikelyFromNonManifoldFix's doc comment (MeshRepair.h)
        // for why this heuristic is grounded in orient_polygon_soup()'s own documented behavior
        // rather than a geometric proof.
        r.selfIntersectionLikelyFromNonManifoldFix =
            !r.selfIntersectionsResolved && singularVerticesDuplicated > 0;
    }

    r.wasAlreadyValid = r.soupPointsRemoved == 0 && r.soupFacesRemoved == 0
        && r.nonManifoldVerticesFixed == 0 && !r.hadSelfIntersections;
    r.succeeded = true;
    return true;
}

void MeshRepair::buildCreaseAwareVertexBuffers(
    const Mesh& mesh,
    std::vector<Vertex>& outVertices,
    std::vector<unsigned int>& outIndices,
    float creaseAngleDegrees)
{
    namespace PMP = CGAL::Polygon_mesh_processing;

    outVertices.clear();
    outIndices.clear();

    const float cosCreaseThresh = std::cos(creaseAngleDegrees * 3.14159265358979f / 180.0f);

    // Same degenerate-face defense booleanUnionMeshes()/subdivideMesh() already use - a handful
    // of near-zero-area sliver triangles can survive repair_polygon_soup()/stitch_borders() even
    // on an otherwise clean input, and their near-arbitrary normals would otherwise poison
    // compute_face_normal()-based averaging at every vertex they touch. A plain local map rather
    // than a mesh-attached property map (mesh.add_property_map()) - that requires non-const
    // access, and this function takes `mesh` by const reference deliberately (it only reads).
    std::unordered_map<Mesh::Face_index, double> faceArea;
    faceArea.reserve(mesh.number_of_faces());
    double areaSum = 0.0;
    for (Mesh::Face_index f : mesh.faces())
    {
        const double area = CGAL::to_double(PMP::face_area(f, mesh));
        faceArea[f] = area;
        areaSum += area;
    }
    const double avgFaceArea = mesh.number_of_faces() > 0
        ? areaSum / static_cast<double>(mesh.number_of_faces()) : 0.0;
    constexpr double kDegenerateAreaRatio = 1.0e-6;
    const double degenerateAreaThreshold = avgFaceArea * kDegenerateAreaRatio;

    struct FanEntry
    {
        Mesh::Halfedge_index halfedge{};
        glm::vec3 normal{0.0f};
        double area = 0.0;
        bool valid = false;
    };

    outVertices.reserve(mesh.number_of_vertices());
    std::unordered_map<Mesh::Halfedge_index, unsigned int> halfedgeVertexIndex;
    halfedgeVertexIndex.reserve(mesh.number_of_halfedges());

    for (Mesh::Vertex_index v : mesh.vertices())
    {
        std::vector<FanEntry> fan;
        for (Mesh::Halfedge_index h : CGAL::halfedges_around_target(v, mesh))
        {
            FanEntry entry;
            entry.halfedge = h;
            // A border halfedge has no incident face - entry.valid stays false, which naturally
            // forces a shading-group break on either side of it. Correct behavior at an open
            // mesh's boundary, not a special case that needs handling.
            if (!mesh.is_border(h))
            {
                const Mesh::Face_index f = mesh.face(h);
                entry.area = faceArea[f];
                entry.valid = entry.area >= degenerateAreaThreshold;
                if (entry.valid)
                {
                    const Kernel::Vector_3 fn = PMP::compute_face_normal(f, mesh);
                    entry.normal = glm::vec3(static_cast<float>(CGAL::to_double(fn.x())),
                                             static_cast<float>(CGAL::to_double(fn.y())),
                                             static_cast<float>(CGAL::to_double(fn.z())));
                }
            }
            fan.push_back(entry);
        }

        const int fanSize = static_cast<int>(fan.size());
        if (fanSize == 0)
            continue;

        // same[i] says whether fan[i] and its cyclic successor belong in the same shading group
        // (both valid and within the crease angle).
        std::vector<bool> sameGroup(fanSize, false);
        for (int i = 0; i < fanSize; ++i)
        {
            const int next = (i + 1) % fanSize;
            sameGroup[i] = fan[i].valid && fan[next].valid
                && glm::dot(fan[i].normal, fan[next].normal) >= cosCreaseThresh;
        }
        int breakIndex = -1;
        for (int i = 0; i < fanSize; ++i)
            if (!sameGroup[i]) { breakIndex = i; break; }

        // Linearize the cyclic run: start right after a break (if any) so a group never needs to
        // wrap across it.
        std::vector<int> groupId(fanSize, 0);
        const int start = (breakIndex < 0) ? 0 : (breakIndex + 1) % fanSize;
        int groupCount = 1;
        int current = start;
        for (int step = 1; step < fanSize; ++step)
        {
            const int previous = current;
            current = (current + 1) % fanSize;
            if (!sameGroup[previous])
                ++groupCount;
            groupId[current] = groupCount - 1;
        }

        const Point_3& p = mesh.point(v);
        const glm::vec3 position(static_cast<float>(CGAL::to_double(p.x())),
                                 static_cast<float>(CGAL::to_double(p.y())),
                                 static_cast<float>(CGAL::to_double(p.z())));

        for (int group = 0; group < groupCount; ++group)
        {
            glm::vec3 sum(0.0f);
            for (int i = 0; i < fanSize; ++i)
                if (groupId[i] == group && fan[i].valid)
                    sum += static_cast<float>(fan[i].area) * fan[i].normal;

            glm::vec3 normal(0.0f);
            if (glm::dot(sum, sum) > 1.0e-12f)
            {
                normal = glm::normalize(sum);
            }
            else
            {
                // No valid (non-degenerate, non-border) face in this run - fall back to any
                // incident face's own normal so the shader never sees a NaN from normalizing a
                // zero vector.
                for (const FanEntry& entry : fan)
                {
                    if (!mesh.is_border(entry.halfedge))
                    {
                        const Kernel::Vector_3 fn = PMP::compute_face_normal(mesh.face(entry.halfedge), mesh);
                        normal = glm::vec3(static_cast<float>(CGAL::to_double(fn.x())),
                                           static_cast<float>(CGAL::to_double(fn.y())),
                                           static_cast<float>(CGAL::to_double(fn.z())));
                        break;
                    }
                }
            }

            Vertex vert{};
            vert.Color = glm::vec4(1.0f);
            vert.Tangent = glm::vec3(0.0f);
            vert.Bitangent = glm::vec3(0.0f);
            for (glm::vec2& uv : vert.TexCoords)
                uv = glm::vec2(0.0f);
            vert.Position = position;
            vert.Normal = normal;

            const unsigned int newIndex = static_cast<unsigned int>(outVertices.size());
            outVertices.push_back(vert);
            for (int i = 0; i < fanSize; ++i)
                if (groupId[i] == group && fan[i].valid)
                    halfedgeVertexIndex.emplace(fan[i].halfedge, newIndex);
        }
    }

    // Skip degenerate (near-zero-area) faces here too, same threshold as above - a sliver
    // contributes no meaningfully visible area, so omitting it from the rendered index buffer
    // entirely is imperceptible.
    outIndices.reserve(mesh.number_of_faces() * 3);
    for (Mesh::Face_index f : mesh.faces())
    {
        if (faceArea[f] < degenerateAreaThreshold)
            continue;
        for (Mesh::Halfedge_index h : CGAL::halfedges_around_face(mesh.halfedge(f), mesh))
        {
            const auto it = halfedgeVertexIndex.find(h);
            if (it != halfedgeVertexIndex.end())
                outIndices.push_back(it->second);
        }
    }
}
