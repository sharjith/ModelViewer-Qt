#pragma once

#include <QUuid>
#include <QString>
#include <QVector>
#include <QVector3D>

// ---------------------------------------------------------------------------
// MeasurementData
//
// A saved Measurement (Point or Distance, "Measure" tool in the viewport)
// is a small list of mesh-relative anchors, resolved on demand against the
// mesh's CURRENT geometry rather than a frozen world position - see
// MeshSurfaceAnchor.h for why (v1: static/non-deforming meshes only, but
// this still stays correct if a mesh's transform changes after the
// measurement was taken, e.g. via the Transform panel or exploded view).
//
// Document-level, not per-file (like SceneGraph::_gltfCameraDataByFile) -
// a measurement can span two different loaded files, so it isn't owned by
// any single one. Same reasoning as the exploded-view preset pattern.
//
// v1 scope: MVF-session-only (not exported to glTF/GLB - unlike a camera,
// glTF has no native concept of a measurement).
// ---------------------------------------------------------------------------

// A mesh-relative anchor reference for one measurement point.
struct MeasurementAnchorRef
{
    QUuid     meshUuid;

    // Index into the mesh's OWN index buffer (SceneMesh::indices()) - see
    // MeshSurfaceAnchor.h. -1 = no triangle recorded (snapped-vertex-only).
    int       triangleIndex = -1;

    // Barycentric weights (u,v,w; sum to 1) within that triangle.
    QVector3D barycentric;

    // >= 0 when the pick snapped to a mesh vertex (SceneMesh::vertices()
    // index) - takes precedence over triangleIndex/barycentric when resolving.
    int       snappedVertexIndex = -1;

    bool isValid() const { return triangleIndex >= 0 || snappedVertexIndex >= 0; }
};

enum class MeasurementType
{
    Point,
    Distance
};

// Which measurement tool is currently armed in the viewport (None = normal
// selection behavior). Distinct from MeasurementType: a saved Measurement is
// always Point or Distance, but the active tool can also be "off".
enum class MeasurementTool
{
    None,
    Point,
    Distance
};

struct Measurement
{
    QUuid                          id;
    QString                        name;
    MeasurementType                type = MeasurementType::Point;

    // 1 anchor for Point, 2 for Distance.
    QVector<MeasurementAnchorRef>  anchors;
};
