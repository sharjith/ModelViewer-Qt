#pragma once

#include <QUuid>
#include <QVector3D>

// ---------------------------------------------------------------------------
// MeshSurfaceAnchor
//
// A precise, mesh-relative 3D point on a mesh's surface, produced by
// SelectionManager::pickSurfaceAnchor(). Shared primitive for Measurement
// and Annotation anchors - both need the same thing (a stable point that
// isn't just a raw world-space XYZ), so it's built once here rather than
// per-feature.
//
// v1 scope: static (non-deforming) meshes only. worldPosition is resolved
// once at pick time from the mesh's current world-space triangle cache; it
// is not re-derived per frame. An anchor will go stale if the mesh is later
// animated (morph/skin) or otherwise deformed without a re-pick - animated-
// mesh anchor support (re-deriving worldPosition from triangleIndex +
// barycentric against the mesh's current deformed geometry each frame) is
// a deliberate follow-up, not solved here.
// ---------------------------------------------------------------------------
struct MeshSurfaceAnchor
{
    QUuid     meshUuid;

    // Index into the mesh's OWN index buffer (SceneMesh::indices()) - i.e.
    // triangle k covers indices()[3k], indices()[3k+1], indices()[3k+2].
    // -1 = invalid/empty anchor.
    int       triangleIndex = -1;

    // Barycentric weights (u,v,w; sum to 1) of the anchor within that
    // triangle, corresponding to indices()[3k+0], [3k+1], [3k+2] in order.
    QVector3D barycentric;

    // >= 0 when the pick snapped to the nearest of the triangle's 3
    // vertices (within pickSurfaceAnchor()'s snap radius): the ORIGINAL
    // mesh vertex index (into SceneMesh::vertices()), not 0/1/2 within the
    // triangle. -1 = not snapped; use triangleIndex/barycentric as picked.
    int       snappedVertexIndex = -1;

    // World-space position at pick time - see the staleness note above.
    QVector3D worldPosition;

    bool isValid() const { return triangleIndex >= 0; }
};
