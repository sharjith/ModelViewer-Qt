#pragma once

#include <QHash>
#include <QPair>
#include <QSet>
#include <QUuid>
#include <QVector3D>

#include "AssemblyRelationGraph.h"
#include "ExplodedGroupTree.h"

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

    // Hierarchical Auto layout. Every group of the tree is laid out bottom-up: its parts and sub-groups are placed as rigid blocks
    // around the group's own centre with the same sector placement recompute() uses for a flat set, and a sub-group enters its
    // parent as a block whose box already includes its own explosion. A part's offset is the sum of the offsets of the blocks it
    // sits in. The anchor part stays put; at every level the block that holds it is the anchor.
    //   maxDepth : number of levels from the root that explode (0 = all); deeper groups move as rigid blocks.
    // A tree with a single group gives the same result as recompute() in Auto mode.
    void recomputeHierarchical(const ExplodedGroupTree::Tree& tree,
                               const QUuid&          anchorUuid,
                               float                 factor,
                               const QHash<QUuid, QVector3D>&              worldCentroids,
                               const QHash<QUuid, QPair<QVector3D,QVector3D>>& worldBoxes,
                               const AssemblyRelationGraph::AutoPlacementHints* autoHints = nullptr,
                               int                   maxDepth = 0);

    void reset();

    // Rescales the offsets of the last recompute() to a new factor. The layout does not depend on the factor (the offsets are
    // linear in it), so a slider tick or a preview step needs no sorting and no sector assignment.
    void setFactor(float factor) { _factor = factor; applyFactor(); }
    bool hasLayout() const { return !_baseOffsets.isEmpty(); }

    QVector3D offsetForMesh(const QUuid& uuid) const;
    bool isActive() const { return !_scaledOffsets.isEmpty(); }

private:
    QHash<QUuid, QVector3D> _baseOffsets;
    QHash<QUuid, QVector3D> _scaledOffsets;
    float _factor = 1.0f;

    void applyFactor();

    // The placement shared by both entry points: lays out the given set around its own centre and writes base offsets (factor 1)
    // for the members that move into out.
    void layoutFlat(const QSet<QUuid>&          assemblyUuids,
                    const QUuid&                anchorUuid,
                    Mode                        mode,
                    const QVector3D&            userVector,
                    const QHash<QUuid, QVector3D>&              worldCentroids,
                    const QHash<QUuid, QPair<QVector3D,QVector3D>>& worldBoxes,
                    const AssemblyRelationGraph::AutoPlacementHints* autoHints,
                    QHash<QUuid, QVector3D>&    out) const;
};
