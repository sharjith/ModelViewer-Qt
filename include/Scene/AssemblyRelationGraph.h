#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QSet>
#include <QUuid>

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

QJsonObject buildInspectionJson(const QSet<QUuid>& assemblyUuids,
                                const ViewportWidget* viewportWidget,
                                const SceneGraph* sceneGraph);

}
