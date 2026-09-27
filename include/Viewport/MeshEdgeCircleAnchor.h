#pragma once

#include <QUuid>

// ---------------------------------------------------------------------------
// MeshEdgeCircleAnchor
//
// A pick result identifying one topological B-Rep circular edge, produced by
// SelectionManager::pickEdgeCircleAnchor() - the Edge Radius measurement
// tool's counterpart to MeshSurfaceAnchor/pickSurfaceAnchor(). Deliberately
// NOT reusing MeshSurfaceAnchor: a circular edge resolves to a whole
// analytic circle (center + axis + radius), not a single surface point, so
// it needs its own resolve path (ViewportWidget::resolveMeasurementEdgeCircle())
// rather than MeshSurfaceAnchor::worldPosition's single-point model.
//
// v1 scope, same as MeshSurfaceAnchor: static (non-deforming) meshes only,
// and CAD-only - edgeIndex is only ever meaningful for STEP/IGES/BREP-
// sourced meshes (see SceneMesh::getOccEdgeCircles()); glTF/OBJ meshes have
// no OCC edge data at all, so nothing is ever a valid pick target for them.
// ---------------------------------------------------------------------------
struct MeshEdgeCircleAnchor
{
    QUuid meshUuid;

    // Index into SceneMesh::getOccEdgeCircles()/getOccEdgeBoundaries() - the
    // topological-edge index used throughout the OCC edge pipeline (see
    // BRepToAssimpConverter::OccEdgeData's doc comment). -1 = invalid/empty.
    int   edgeIndex = -1;

    bool isValid() const { return edgeIndex >= 0; }
};
