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
// tools/Face-to-Face/Point-to-Face all work on any mesh (built entirely on
// triangle-surface picking - see MeshSurfaceAnchor.h; a "face" here is just
// one triangle's plane, not a grouped CAD face - see
// ViewportWidget::resolveMeasurementAnchorPlane()). Edge Radius is CAD-only
// (STEP/IGES/BREP) - it needs a real analytic circle from B-Rep topology,
// which BRepToAssimpConverter now retains alongside the tessellated
// wireframe segments it already extracted for the edge-wireframe feature -
// see BRepToAssimpConverter::OccEdgeData and SceneMesh::getOccEdgeCircles().
// Edge-to-Edge/Edge-to-Face/Edge-to-Vertex/Edge Length are still deferred,
// but NOT CAD-only either once built - SceneMesh already computes a
// discrete, CPU-retained straight-edge list for every mesh type (feature-
// edge detection for the wireframe overlay, _featureEdgeIndices), just not
// yet exposed for picking.
// ---------------------------------------------------------------------------

// A mesh-relative anchor reference for one measurement point.
struct MeasurementAnchorRef
{
    QUuid     meshUuid;

    // Index into the mesh's OWN index buffer (SceneMesh::indices()) - see
    // MeshSurfaceAnchor.h. -1 = no triangle recorded (snapped-vertex-only,
    // or this is an edge anchor - see edgeIndex below).
    int       triangleIndex = -1;

    // Barycentric weights (u,v,w; sum to 1) within that triangle.
    QVector3D barycentric;

    // >= 0 when the pick snapped to a mesh vertex (SceneMesh::vertices()
    // index) - takes precedence over triangleIndex/barycentric when resolving.
    int       snappedVertexIndex = -1;

    // >= 0 for an Edge Radius anchor (MeshEdgeCircleAnchor::edgeIndex) - a
    // wholly different kind of pick from triangleIndex/barycentric above: it
    // identifies a topological B-Rep edge, not a surface point, and resolves
    // to a whole circle (center/axis/radius) via
    // ViewportWidget::resolveMeasurementEdgeCircle(), not a single QVector3D
    // position. -1 = not an edge anchor; the triangle/vertex fields above
    // apply instead.
    int       edgeIndex = -1;

    bool isValid() const { return triangleIndex >= 0 || snappedVertexIndex >= 0 || edgeIndex >= 0; }
};

enum class MeasurementType
{
    Point,
    Distance,
    ArcRadius3Point,        // 3 points on the arc; radius/center via circumcircle fit
    // Center point, then 2 points on the arc; radius = avg(center-to-point
    // distance). The center pick prefers snapping to a nearby circular
    // B-Rep edge's exact analytic center (see
    // SelectionManager::pickCircularEdgeCenterAnchor() and
    // ViewportWidget::resolveMeasurementAnchor()'s edgeIndex handling) -
    // works correctly for a through-hole this way, not just a boss, on
    // STEP/IGES/BREP parts. Falls back to the ordinary pickSurfaceAnchor()
    // ray-triangle test (requiring an actual triangle under the cursor) if
    // no circular edge center is nearby - still boss-only on glTF/OBJ
    // meshes, which have no B-Rep topology to detect a center from at all.
    ArcRadiusCenterPoint,

    // A single pick directly on a circular B-Rep edge (see
    // MeshEdgeCircleAnchor/pickEdgeCircleAnchor()) - radius/center/axis come
    // straight from OCC's analytic circle for that edge, no fitting needed.
    // CAD-only (STEP/IGES/BREP): glTF/OBJ meshes have no OCC edge data, so
    // there is nothing to pick for them (see MeasurementDialog's tooltip on
    // this tool for the in-app version of this note). Unlike
    // ArcRadiusCenterPoint, this works correctly for through-holes too - the
    // pick targets the edge itself, not a point on a face, so there's no
    // "boss-only" limitation.
    EdgeRadius,

    // Two face-picks (see ViewportWidget::resolveMeasurementAnchorPlane()) -
    // if the two triangle planes are near-parallel, reports the
    // perpendicular distance between them; otherwise the acute angle (0-90)
    // between them. Works on any mesh - "face" here means one triangle's
    // plane, not a grouped CAD face, so this needs no B-Rep topology at all.
    FaceToFace,

    // A point-pick and a face-pick - perpendicular distance from the point
    // to the face's plane. Same anchor mechanism as FaceToFace, just one
    // anchor read as a position instead of a plane.
    PointToFace,

    // A single pick on any edge (see MeshEdgeCircleAnchor/
    // pickStraightEdgeAnchor() - CAD edges of any curve type, or the
    // heuristic feature-edge list on non-CAD meshes). Length is the sum of
    // the edge's tessellated segment lengths within its range - this works
    // identically for a straight feature edge (one segment) or a curved
    // OCC edge (line/circle/spline alike) with zero curve-type-awareness
    // needed, unlike Edge Radius which specifically needs the analytic
    // circle. Works on any mesh - the first of the general edge-measurement
    // tools that isn't CAD-only (see this header's top comment).
    EdgeLength,

    // An edge-pick and a point-pick - perpendicular distance from the point
    // to the edge's INFINITE line (not clamped to the finite chord), same
    // "infinite, not clamped" convention PointToFace already uses for a
    // face's plane. anchors[0] is the edge, anchors[1] is the point -
    // reading order matches the tool's name.
    EdgeToVertex,

    // Two edge-picks - if the two edges' directions are near-parallel,
    // reports the perpendicular distance between their (infinite) lines;
    // otherwise the acute angle (0-90) between them - same distance/angle
    // split as FaceToFace, applied to lines instead of planes. The angle
    // case (including skew, non-intersecting edges - the common case in
    // 3D) reports only the angle, same as FaceToFace's non-parallel case
    // not also attempting a distance figure. anchors[0]/[1] are the two
    // edges, in pick order.
    EdgeToEdge,

    // An edge-pick and a face-pick - if the edge's direction is near-
    // perpendicular to the face's normal (i.e. the edge lies within a
    // plane parallel to the face), reports the perpendicular distance from
    // the edge to the face's plane; otherwise the angle (0-90) between the
    // edge and the face. Note the "parallel" test here is the OPPOSITE
    // alignment check from FaceToFace/EdgeToEdge (which test near-zero
    // angle between two normals/directions of the same kind) - see
    // MeasurementGeometry::compareEdgeToFace()'s doc comment. anchors[0]
    // is the edge, anchors[1] is the face.
    EdgeToFace,
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
    EdgeRadius,
    FaceToFace,
    PointToFace,
    EdgeLength,
    EdgeToVertex,
    EdgeToEdge,
    EdgeToFace,
};

struct Measurement
{
    QUuid                          id;
    QString                        name;
    MeasurementType                type = MeasurementType::Point;

    // Anchor count is determined by type: 1 for Point or EdgeRadius, 2 for
    // Distance/FaceToFace/PointToFace, 3 for either arc type (see
    // measurementToolRequiredAnchorCount()). For ArcRadiusCenterPoint
    // specifically, anchors[0] is the center and anchors[1]/[2] are the two
    // points on the arc. For EdgeRadius, anchors[0] is an edge anchor
    // (MeasurementAnchorRef::edgeIndex >= 0), not a triangle/vertex anchor.
    // For PointToFace, anchors[0] is the point and anchors[1] is the face.
    QVector<MeasurementAnchorRef>  anchors;

    // Show/hide toggle from the Measurement dialog's results list checkbox -
    // a plain display setting, not an undoable edit (same convention as
    // SceneTreeWidget's mesh-visibility checkboxes, which also bypass the
    // undo stack). Hidden measurements are skipped by both
    // ViewportWidget::drawMeasurementOverlay() and hitTestMeasurement() - a
    // hidden measurement can't be hovered/selected/deleted-by-click either,
    // only via the dialog's list (which stays visible).
    bool                           visible = true;

    // World-space direction used to pick which way a linear dimension's
    // offset/extension lines lean (see ViewportWidget's addOffsetDimension()
    // in drawMeasurementOverlay()) - captured ONCE from the active camera's
    // view direction at the moment this measurement was created, then
    // reused as-is on every subsequent frame. Deliberately NOT re-derived
    // from the live camera each frame: that was the first cut, and it made
    // the dimension line visibly rotate/swim as the user orbited, which
    // reads as broken rather than "always readable" - real CAD tools place
    // a dimension once and leave it fixed in space. Zero-length (default)
    // means "not set" (e.g. a measurement predating this field) - callers
    // fall back to a live-camera-derived direction in that case, same as
    // the original behavior, since a stale/unset direction beats none.
    QVector3D                      offsetReferenceDir;

    // Angle dimension's (FaceToFace non-parallel case) arc radius override,
    // a.k.a. "leg length" - world-space distance the legs/arc sit away from
    // the vertex. -1 (default/sentinel) means "not set yet", falls back to
    // the original heuristic (half the anchor-to-anchor distance). Set once
    // the user drags the arc (see ViewportWidget's dimension-line-drag
    // interaction) - extension only for the angle case, no pivoting (an
    // angle's plane is already fully determined by the two face normals,
    // there's no free direction to pivot into the way a linear dimension's
    // offset has). Undoable (MeasurementOffsetCommand), unlike `visible`
    // above - repositioning a dimension is a deliberate spatial edit, not a
    // passive display toggle.
    float                          offsetDistance = -1.0f;

    // Linear dimension's (Distance/PointToFace/FaceToFace-parallel) full
    // world-space offset from the measured segment to where the dimension
    // line actually sits - see ViewportWidget::resolveDimensionOffsetVector().
    // Zero-length (default) means "not dragged yet": render/hit-test fall
    // back to offsetReferenceDir's perpendicular at a markerSize-based
    // default magnitude. Once the user drags, this holds the exact offset
    // vector (both direction AND magnitude - a linear dimension can be
    // "pivoted" to any angle around the measured segment, not just pushed
    // further along one fixed perpendicular, per the user's explicit ask),
    // so offsetReferenceDir stops being consulted at all for that
    // measurement from then on. Undoable, same reasoning as offsetDistance
    // above.
    QVector3D                      offsetVector;
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
    case MeasurementTool::EdgeRadius:           return 1;
    case MeasurementTool::FaceToFace:           return 2;
    case MeasurementTool::PointToFace:          return 2;
    case MeasurementTool::EdgeLength:           return 1;
    case MeasurementTool::EdgeToVertex:         return 2;
    case MeasurementTool::EdgeToEdge:           return 2;
    case MeasurementTool::EdgeToFace:           return 2;
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
    case MeasurementTool::EdgeRadius:           return MeasurementType::EdgeRadius;
    case MeasurementTool::FaceToFace:           return MeasurementType::FaceToFace;
    case MeasurementTool::PointToFace:          return MeasurementType::PointToFace;
    case MeasurementTool::EdgeLength:           return MeasurementType::EdgeLength;
    case MeasurementTool::EdgeToVertex:         return MeasurementType::EdgeToVertex;
    case MeasurementTool::EdgeToEdge:           return MeasurementType::EdgeToEdge;
    case MeasurementTool::EdgeToFace:           return MeasurementType::EdgeToFace;
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
    case MeasurementTool::EdgeRadius:           return QStringLiteral("Edge Radius (CAD only)");
    case MeasurementTool::FaceToFace:           return QStringLiteral("Face to Face");
    case MeasurementTool::PointToFace:          return QStringLiteral("Point to Face");
    case MeasurementTool::EdgeLength:           return QStringLiteral("Edge Length");
    case MeasurementTool::EdgeToVertex:         return QStringLiteral("Edge to Vertex");
    case MeasurementTool::EdgeToEdge:           return QStringLiteral("Edge to Edge");
    case MeasurementTool::EdgeToFace:           return QStringLiteral("Edge to Face");
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
    case MeasurementTool::EdgeRadius:
        return QStringLiteral("Click a circular edge");
    case MeasurementTool::FaceToFace:
        return alreadyPicked == 0 ? QStringLiteral("Click the 1st face")
                                   : QStringLiteral("Click the 2nd face");
    case MeasurementTool::PointToFace:
        return alreadyPicked == 0 ? QStringLiteral("Click a point")
                                   : QStringLiteral("Click a face");
    case MeasurementTool::EdgeLength:
        return QStringLiteral("Click an edge");
    case MeasurementTool::EdgeToVertex:
        return alreadyPicked == 0 ? QStringLiteral("Click an edge")
                                   : QStringLiteral("Click a vertex or point");
    case MeasurementTool::EdgeToEdge:
        return alreadyPicked == 0 ? QStringLiteral("Click the 1st edge")
                                   : QStringLiteral("Click the 2nd edge");
    case MeasurementTool::EdgeToFace:
        return alreadyPicked == 0 ? QStringLiteral("Click an edge")
                                   : QStringLiteral("Click a face");
    case MeasurementTool::None:
        return QString();
    }
    return QString();
}
