#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QSet>
#include <QUuid>

#include "ExplodedGroupTree.h"

class ViewportWidget;
class SceneGraph;

namespace AssemblyRelationGraph
{

struct AutoPlacementHints
{
    // Pairs that share a parent node, are coaxial, or are repeated instances.
    // Used to guide chain membership and sector-sort stability.
    QSet<QString> strongPairKeys;

    // Pairs whose AABBs are geometrically near (distance <= 20% of avg diagonal).
    // Used as an additional gate on backbone chain membership to prevent false
    // chaining of identical parts that happen to share an axis but are far apart.
    QSet<QString> nearPairKeys;

    // Maps pairKey -> UUID of the outer (containing) part for nested pairs.
    // The complementary UUID in the pair is the inner (contained) part, which
    // should be exploded outward along the containment axis rather than falling
    // into a generic sector.
    QHash<QString, QUuid> nestedOuterUuid;

    // Maps mesh UUID -> scene-graph parent node UUID for parts that share a parent.
    // Reserved for future sub-assembly grouping; not used in placement yet.
    QHash<QUuid, QUuid> partGroupUuid;
};

QString makeMeshPairKey(const QUuid& a, const QUuid& b);

AutoPlacementHints buildAutoPlacementHints(const QSet<QUuid>& assemblyUuids,
                                           const ViewportWidget* viewportWidget,
                                           const SceneGraph* sceneGraph);

// Assembly-aware hierarchy: the scene tree restricted to the given parts, wrapper nodes collapsed (see ExplodedGroupTree::fromDraft).
// Parts the scene graph does not know sit in the root. A null scene graph gives a flat tree.
ExplodedGroupTree::Tree buildSceneGroupTree(const QSet<QUuid>& assemblyUuids, const SceneGraph* sceneGraph);

// The placement hints of buildAutoPlacementHints, computed only among the parts of one group at a time, so the cost is the sum of
// the squares of the group sizes instead of the square of the part count. Groups above kMaxHintGroupParts get none.
constexpr int kMaxHintGroupParts = 300;
AutoPlacementHints buildSiblingPlacementHints(const ExplodedGroupTree::Tree& tree,
                                              const ViewportWidget* viewportWidget,
                                              const SceneGraph* sceneGraph);

QJsonObject buildInspectionJson(const QSet<QUuid>& assemblyUuids,
                                const ViewportWidget* viewportWidget,
                                const SceneGraph* sceneGraph);

}
