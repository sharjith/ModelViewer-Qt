#pragma once

#include <QHash>
#include <QPair>
#include <QSet>
#include <QUuid>
#include <QVector3D>

#include "AssemblyRelationGraph.h"

// ---------------------------------------------------------------------------
// ExplodedViewManager
// Computes per-mesh world-space translation offsets for exploded view rendering.
// ---------------------------------------------------------------------------
class ExplodedViewManager
{
public:
    enum class Mode { Auto, AxisX, AxisY, AxisZ, Vector };

    // Recompute offsets from current settings.
    //   worldCentroids : world-space centroid for each assembly mesh
    //   worldBoxes     : (boxMin, boxMax) world-space AABB for each assembly mesh;
    //                    used by axial modes to compute bbox-clearance spacing
    void recompute(const QSet<QUuid>&    assemblyUuids,
                   const QUuid&          anchorUuid,
                   Mode                  mode,
                   const QVector3D&      userVector,
                   float                 factor,
                   const QHash<QUuid, QVector3D>&              worldCentroids,
                   const QHash<QUuid, QPair<QVector3D,QVector3D>>& worldBoxes,
                   const AssemblyRelationGraph::AutoPlacementHints* autoHints = nullptr);

    void reset();

    QVector3D offsetForMesh(const QUuid& uuid) const;
    bool isActive() const { return !_scaledOffsets.isEmpty(); }

private:
    QHash<QUuid, QVector3D> _baseOffsets;
    QHash<QUuid, QVector3D> _scaledOffsets;
    float _factor = 1.0f;

    void applyFactor();
};
