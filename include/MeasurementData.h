#pragma once

#include <QUuid>
#include <QString>
#include <QVector>
#include <QVector3D>

// ---------------------------------------------------------------------------
// MeasurementData
//
// A saved Measurement is a small list of mesh-relative anchors, resolved on
// demand against the mesh's CURRENT geometry rather than a frozen world
// position - see MeshSurfaceAnchor.h for why (v1: static/non-deforming
// meshes only, but this still stays correct if a mesh's transform changes
// after the measurement was taken, e.g. via the Transform panel or exploded
// view).
//
// Document-level, not per-file (like SceneGraph::_gltfCameraDataByFile) -
// a measurement can span two different loaded files, so it isn't owned by
// any single one. Same reasoning as the exploded-view preset pattern.
//
// v1 scope: MVF-session-only (not exported to glTF/GLB - unlike a camera,
// glTF has no native concept of a measurement). Point/Distance/the two arc
// tools work on any mesh (built entirely on triangle-surface picking - see
// MeshSurfaceAnchor.h). Face/Edge/Edge-radius measurements are deliberately
// not included yet - they need real B-Rep topology identity, which only
// STEP/IGES/BREP-sourced meshes can have, and that plumbing doesn't exist
// yet (see BRepToAssimpConverter - it already walks TopoDS_Face/TopoDS_Edge
// for the edge-wireframe feature, but discards the topology once the
// wireframe geometry is built).
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
    Distance,
    ArcRadius3Point,        // 3 points on the arc; radius/center via circumcircle fit
    // Center point, then 2 points on the arc; radius = avg(center-to-point
    // distance). Boss-only for now: the center pick goes through the same
    // pickSurfaceAnchor() ray-triangle test as every other anchor, which
    // requires an actual triangle under the cursor - fine for a raised boss
    // (its flat cap face IS geometry at the center), but a through-hole's
    // center is empty space with nothing to hit. Fixing that for holes needs
    // an analytic center from real BRep topology (TopoDS_Edge circle
    // Location()), which is the same STEP/IGES/BREP-only topology-extraction
    // work already deferred for Face/Edge/Edge-radius measurements - see this
    // header's top comment. Revisit once that infra exists.
    ArcRadiusCenterPoint,
};

// Which measurement tool is currently armed in the viewport (None = normal
// selection behavior). Distinct from MeasurementType: a saved Measurement is
// always one of the concrete types, but the active tool can also be "off".
// 1:1 with MeasurementType otherwise - see measurementTypeForTool().
enum class MeasurementTool
{
    None,
    Point,
    Distance,
    ArcRadius3Point,
    ArcRadiusCenterPoint,
};

struct Measurement
{
    QUuid                          id;
    QString                        name;
    MeasurementType                type = MeasurementType::Point;

    // Anchor count is determined by type: 1 for Point, 2 for Distance, 3
    // for either arc type (see measurementToolRequiredAnchorCount()). For
    // ArcRadiusCenterPoint specifically, anchors[0] is the center and
    // anchors[1]/[2] are the two points on the arc.
    QVector<MeasurementAnchorRef>  anchors;

    // Show/hide toggle from the Measurement dialog's results list checkbox -
    // a plain display setting, not an undoable edit (same convention as
    // SceneTreeWidget's mesh-visibility checkboxes, which also bypass the
    // undo stack). Hidden measurements are skipped by both
    // ViewportWidget::drawMeasurementOverlay() and hitTestMeasurement() - a
    // hidden measurement can't be hovered/selected/deleted-by-click either,
    // only via the dialog's list (which stays visible).
    bool                           visible = true;
};

// How many anchor picks a tool needs before a Measurement is complete.
// Returns 0 for MeasurementTool::None.
inline int measurementToolRequiredAnchorCount(MeasurementTool tool)
{
    switch (tool)
    {
    case MeasurementTool::Point:                return 1;
    case MeasurementTool::Distance:             return 2;
    case MeasurementTool::ArcRadius3Point:      return 3;
    case MeasurementTool::ArcRadiusCenterPoint: return 3;
    case MeasurementTool::None:                 return 0;
    }
    return 0;
}

// The MeasurementType a completed pick sequence for `tool` should be saved
// as. Undefined (returns Point) for MeasurementTool::None - callers should
// never actually save a measurement for that case.
inline MeasurementType measurementTypeForTool(MeasurementTool tool)
{
    switch (tool)
    {
    case MeasurementTool::Point:                return MeasurementType::Point;
    case MeasurementTool::Distance:             return MeasurementType::Distance;
    case MeasurementTool::ArcRadius3Point:      return MeasurementType::ArcRadius3Point;
    case MeasurementTool::ArcRadiusCenterPoint: return MeasurementType::ArcRadiusCenterPoint;
    case MeasurementTool::None:                 return MeasurementType::Point;
    }
    return MeasurementType::Point;
}

// Human-readable name for the tool-selection combo box.
inline QString measurementToolDisplayName(MeasurementTool tool)
{
    switch (tool)
    {
    case MeasurementTool::Point:                return QStringLiteral("Point");
    case MeasurementTool::Distance:             return QStringLiteral("Distance");
    case MeasurementTool::ArcRadius3Point:      return QStringLiteral("3-Point Arc Radius");
    case MeasurementTool::ArcRadiusCenterPoint: return QStringLiteral("Center + 2-Point Arc Radius");
    case MeasurementTool::None:                 return QStringLiteral("None");
    }
    return QString();
}

// Short label for what to prompt the user to pick next, given how many
// anchors are already gathered for `tool` (0-based - picked.size() so far).
inline QString measurementToolPickPrompt(MeasurementTool tool, int alreadyPicked)
{
    switch (tool)
    {
    case MeasurementTool::Point:
        return QStringLiteral("Click a point");
    case MeasurementTool::Distance:
        return alreadyPicked == 0 ? QStringLiteral("Click the 1st point")
                                   : QStringLiteral("Click the 2nd point");
    case MeasurementTool::ArcRadius3Point:
        switch (alreadyPicked)
        {
        case 0:  return QStringLiteral("Click 1st point on the arc");
        case 1:  return QStringLiteral("Click 2nd point on the arc");
        default: return QStringLiteral("Click 3rd point on the arc");
        }
    case MeasurementTool::ArcRadiusCenterPoint:
        switch (alreadyPicked)
        {
        case 0:  return QStringLiteral("Click the arc's center");
        case 1:  return QStringLiteral("Click 1st point on the arc");
        default: return QStringLiteral("Click 2nd point on the arc");
        }
    case MeasurementTool::None:
        return QString();
    }
    return QString();
}
