#pragma once

#include <QUuid>

// ---------------------------------------------------------------------------
// SeamEdgeMark
//
// A user-forced UV seam edge, identified the same way MeshEdgeCircleAnchor
// identifies a picked edge (meshUuid + edgeIndex) - produced by
// SelectionManager::pickStraightEdgeAnchor() via SeamMarkingController.
// edgeIndex is CAD/non-CAD context-dependent exactly like
// MeshEdgeCircleAnchor's own: SceneMesh::getOccEdgeBoundaries() for a CAD
// mesh, SceneMesh::getFeatureEdgeIndices() pairs for a non-CAD one.
//
// Owned by SeamMarkingController for the lifetime of one Generate UVs dialog
// session only - not persisted, not undoable, not saved to .mvf. See
// SeamMarkingController's own doc comment for why.
// ---------------------------------------------------------------------------
struct SeamEdgeMark
{
    QUuid meshUuid;
    int   edgeIndex = -1;

    bool isValid() const { return edgeIndex >= 0; }

    bool operator==(const SeamEdgeMark& other) const
    {
        return meshUuid == other.meshUuid && edgeIndex == other.edgeIndex;
    }
};
