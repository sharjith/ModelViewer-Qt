#include "AssemblyRelationGraph.h"

#include "ViewportWidget.h"
#include "SceneGraph.h"
#include "SceneNode.h"
#include "RenderableMesh.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cmath>

namespace
{

struct Box3
{
    QVector3D min;
    QVector3D max;

    QVector3D size() const { return max - min; }
    QVector3D center() const { return (min + max) * 0.5f; }
    float diagonal() const { return size().length(); }
    float volume() const
    {
        const QVector3D s = size();
        return std::max(0.0f, s.x()) * std::max(0.0f, s.y()) * std::max(0.0f, s.z());
    }
};

struct PartInfo
{
    QUuid meshUuid;
    QString label;
    QString sourceFile;
    QString sourceNodeName;
    Box3 box;
    QVector3D center;
    QVector3D size;
    float diagonal = 0.0f;
    const SceneNode* ownerNode = nullptr;
    const SceneNode* parentNode = nullptr;
    int coaxialAxis = -1;
    QString coaxialAxisKind;
    QString normalizedNameStem;
};

struct PairRelationFlags
{
    bool isNear = false;
    bool isNested = false;
    bool isSameParent = false;
    bool isCoaxial = false;
    bool isRepeated = false;
};

QJsonArray vector3ToJson(const QVector3D& v)
{
    return QJsonArray{v.x(), v.y(), v.z()};
}

QString baseNameOnly(const QString& path)
{
    const int slash = std::max(path.lastIndexOf('/'), path.lastIndexOf('\\'));
    return slash >= 0 ? path.mid(slash + 1) : path;
}

QString normalizeNameStem(const QString& raw)
{
    QString stem = raw.trimmed().toLower();
    stem.replace(QRegularExpression(QStringLiteral("[_\\-]+")), QStringLiteral(" "));
    stem.remove(QRegularExpression(QStringLiteral("\\b(copy|instance)\\b")));
    stem.remove(QRegularExpression(QStringLiteral("\\d+")));
    stem = stem.simplified();
    return stem;
}

float gap1D(float aMin, float aMax, float bMin, float bMax)
{
    if (aMax < bMin)
        return bMin - aMax;
    if (bMax < aMin)
        return aMin - bMax;
    return 0.0f;
}

float overlap1D(float aMin, float aMax, float bMin, float bMax)
{
    return std::max(0.0f, std::min(aMax, bMax) - std::max(aMin, bMin));
}

float boxDistance(const Box3& a, const Box3& b)
{
    const float dx = gap1D(a.min.x(), a.max.x(), b.min.x(), b.max.x());
    const float dy = gap1D(a.min.y(), a.max.y(), b.min.y(), b.max.y());
    const float dz = gap1D(a.min.z(), a.max.z(), b.min.z(), b.max.z());
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool pointInExpandedBox(const QVector3D& point, const Box3& box, float tolerance)
{
    return point.x() >= box.min.x() - tolerance && point.x() <= box.max.x() + tolerance
        && point.y() >= box.min.y() - tolerance && point.y() <= box.max.y() + tolerance
        && point.z() >= box.min.z() - tolerance && point.z() <= box.max.z() + tolerance;
}

float containmentFraction(const Box3& outer, const Box3& inner)
{
    const float sx = std::max(1.0e-5f, inner.size().x());
    const float sy = std::max(1.0e-5f, inner.size().y());
    const float sz = std::max(1.0e-5f, inner.size().z());

    const float fx = qBound(0.0f, overlap1D(outer.min.x(), outer.max.x(), inner.min.x(), inner.max.x()) / sx, 1.0f);
    const float fy = qBound(0.0f, overlap1D(outer.min.y(), outer.max.y(), inner.min.y(), inner.max.y()) / sy, 1.0f);
    const float fz = qBound(0.0f, overlap1D(outer.min.z(), outer.max.z(), inner.min.z(), inner.max.z()) / sz, 1.0f);

    return (fx + fy + fz) / 3.0f;
}

float axisComponent(const QVector3D& vector, int axis)
{
    switch (axis)
    {
    case 0: return vector.x();
    case 1: return vector.y();
    default: return vector.z();
    }
}

std::array<int, 3> sortedAxisIndicesByExtent(const QVector3D& size)
{
    std::array<int, 3> indices{0, 1, 2};
    std::sort(indices.begin(), indices.end(), [&](int a, int b) {
        return axisComponent(size, a) < axisComponent(size, b);
    });
    return indices;
}

int inferCoaxialAxis(const QVector3D& size, QString& outKind)
{
    const auto order = sortedAxisIndicesByExtent(size);
    const float minExtent = std::max(1.0e-5f, axisComponent(size, order[0]));
    const float midExtent = std::max(1.0e-5f, axisComponent(size, order[1]));
    const float maxExtent = std::max(1.0e-5f, axisComponent(size, order[2]));

    const float largeToMid = maxExtent / midExtent;
    const float midToSmall = midExtent / minExtent;

    if (largeToMid >= 1.35f && midToSmall <= 1.2f)
    {
        outKind = QStringLiteral("elongated");
        return order[2];
    }

    if (midToSmall >= 1.35f && largeToMid <= 1.2f)
    {
        outKind = QStringLiteral("disc");
        return order[0];
    }

    if (maxExtent / minExtent >= 1.75f)
    {
        if ((maxExtent - midExtent) >= (midExtent - minExtent))
        {
            outKind = QStringLiteral("elongated");
            return order[2];
        }

        outKind = QStringLiteral("disc");
        return order[0];
    }

    outKind = QStringLiteral("ambiguous");
    return -1;
}

QJsonObject buildNodeJson(const PartInfo& part)
{
    QJsonObject node;
    node.insert(QStringLiteral("meshUuid"), part.meshUuid.toString(QUuid::WithoutBraces));
    node.insert(QStringLiteral("label"), part.label);
    node.insert(QStringLiteral("sourceFile"), part.sourceFile);
    node.insert(QStringLiteral("sourceFileName"), baseNameOnly(part.sourceFile));
    node.insert(QStringLiteral("sourceNodeName"), part.sourceNodeName);
    node.insert(QStringLiteral("ownerNodeUuid"),
                part.ownerNode ? part.ownerNode->nodeUuid.toString(QUuid::WithoutBraces) : QString());
    node.insert(QStringLiteral("ownerNodeName"), part.ownerNode ? part.ownerNode->name : QString());
    node.insert(QStringLiteral("parentNodeUuid"),
                part.parentNode ? part.parentNode->nodeUuid.toString(QUuid::WithoutBraces) : QString());
    node.insert(QStringLiteral("parentNodeName"), part.parentNode ? part.parentNode->name : QString());
    node.insert(QStringLiteral("center"), vector3ToJson(part.center));
    node.insert(QStringLiteral("bboxMin"), vector3ToJson(part.box.min));
    node.insert(QStringLiteral("bboxMax"), vector3ToJson(part.box.max));
    node.insert(QStringLiteral("size"), vector3ToJson(part.size));
    node.insert(QStringLiteral("diagonal"), part.diagonal);
    node.insert(QStringLiteral("coaxialAxis"), part.coaxialAxis);
    node.insert(QStringLiteral("coaxialAxisKind"), part.coaxialAxisKind);
    node.insert(QStringLiteral("nameStem"), part.normalizedNameStem);
    return node;
}

QJsonObject buildEdgeJson(const QString& relation,
                          const PartInfo& a,
                          const PartInfo& b,
                          double score,
                          QJsonObject metrics)
{
    QJsonObject edge;
    edge.insert(QStringLiteral("relation"), relation);
    edge.insert(QStringLiteral("meshA"), a.meshUuid.toString(QUuid::WithoutBraces));
    edge.insert(QStringLiteral("meshB"), b.meshUuid.toString(QUuid::WithoutBraces));
    edge.insert(QStringLiteral("labelA"), a.label);
    edge.insert(QStringLiteral("labelB"), b.label);
    edge.insert(QStringLiteral("score"), score);
    edge.insert(QStringLiteral("metrics"), metrics);
    return edge;
}

QVector<PartInfo> collectPartInfos(const QSet<QUuid>& assemblyUuids,
                                   const ViewportWidget* viewportWidget,
                                   const SceneGraph* sceneGraph)
{
    QVector<PartInfo> parts;
    if (!viewportWidget)
        return parts;

    parts.reserve(assemblyUuids.size());
    for (const QUuid& uuid : assemblyUuids)
    {
        SceneMesh* mesh = viewportWidget->getMeshByUuid(uuid);
        if (!mesh)
            continue;

        const BoundingBox bb = mesh->getBoundingBox();
        PartInfo part;
        part.meshUuid = uuid;
        part.label = mesh->getName();
        part.sourceFile = mesh->getSourceFile();
        part.sourceNodeName = mesh->getSourceNodeName();
        part.box.min = QVector3D(static_cast<float>(bb.xMin()),
                                 static_cast<float>(bb.yMin()),
                                 static_cast<float>(bb.zMin()));
        part.box.max = QVector3D(static_cast<float>(bb.xMax()),
                                 static_cast<float>(bb.yMax()),
                                 static_cast<float>(bb.zMax()));
        part.center = part.box.center();
        part.size = part.box.size();
        part.diagonal = part.box.diagonal();
        part.normalizedNameStem = normalizeNameStem(
            !part.sourceNodeName.isEmpty() ? part.sourceNodeName : part.label);
        part.coaxialAxis = inferCoaxialAxis(part.size, part.coaxialAxisKind);

        if (sceneGraph)
        {
            part.ownerNode = sceneGraph->findNodeForMesh(uuid);
            part.parentNode = part.ownerNode ? part.ownerNode->parent : nullptr;
        }

        parts.append(part);
    }

    std::sort(parts.begin(), parts.end(), [](const PartInfo& a, const PartInfo& b) {
        if (a.sourceFile != b.sourceFile)
            return a.sourceFile < b.sourceFile;
        if (a.label != b.label)
            return a.label < b.label;
        return a.meshUuid.toString(QUuid::WithoutBraces) < b.meshUuid.toString(QUuid::WithoutBraces);
    });

    return parts;
}

PairRelationFlags classifyPair(const PartInfo& a, const PartInfo& b)
{
    PairRelationFlags flags;

    const float avgDiag = std::max(1.0e-4f, (a.diagonal + b.diagonal) * 0.5f);
    const float nearDistance = boxDistance(a.box, b.box);
    const float nearThreshold = std::max(0.5f, avgDiag * 0.2f);
    flags.isNear = nearDistance <= nearThreshold;

    const auto nestedMatch = [](const PartInfo& outer, const PartInfo& inner) {
        const float tol = std::max(0.25f, outer.diagonal * 0.03f);
        if (!pointInExpandedBox(inner.center, outer.box, tol))
            return false;
        const float containFrac = containmentFraction(outer.box, inner.box);
        const float volumeRatio = outer.box.volume() / std::max(1.0e-5f, inner.box.volume());
        return containFrac >= 0.75f && volumeRatio >= 1.15f;
    };
    flags.isNested = nestedMatch(a, b) || nestedMatch(b, a);

    flags.isSameParent = a.parentNode && a.parentNode == b.parentNode;

    if (a.coaxialAxis >= 0 && a.coaxialAxis == b.coaxialAxis)
    {
        const int axis = a.coaxialAxis;
        const int other0 = (axis + 1) % 3;
        const int other1 = (axis + 2) % 3;
        const float orthDist = std::sqrt(
            std::pow(axisComponent(a.center, other0) - axisComponent(b.center, other0), 2.0f) +
            std::pow(axisComponent(a.center, other1) - axisComponent(b.center, other1), 2.0f));
        const float avgCrossSection =
            0.25f * (axisComponent(a.size, other0) + axisComponent(a.size, other1)
                   + axisComponent(b.size, other0) + axisComponent(b.size, other1));
        const float orthThreshold = std::max(0.35f, avgCrossSection * 0.45f);
        const float axisGap = gap1D(axisComponent(a.box.min, axis), axisComponent(a.box.max, axis),
                                    axisComponent(b.box.min, axis), axisComponent(b.box.max, axis));
        const float axisThreshold = std::max(
            0.5f,
            0.75f * (axisComponent(a.size, axis) + axisComponent(b.size, axis)));
        flags.isCoaxial = orthDist <= orthThreshold && axisGap <= axisThreshold;
    }

    std::array<float, 3> dimsA{a.size.x(), a.size.y(), a.size.z()};
    std::array<float, 3> dimsB{b.size.x(), b.size.y(), b.size.z()};
    std::sort(dimsA.begin(), dimsA.end());
    std::sort(dimsB.begin(), dimsB.end());
    double maxRelDiff = 0.0;
    for (int axis = 0; axis < 3; ++axis)
    {
        const double denom = std::max(1.0e-5, static_cast<double>(std::max(dimsA[axis], dimsB[axis])));
        maxRelDiff = std::max(maxRelDiff, std::abs(dimsA[axis] - dimsB[axis]) / denom);
    }

    const bool nameStemMatch = !a.normalizedNameStem.isEmpty()
        && a.normalizedNameStem == b.normalizedNameStem;
    flags.isRepeated = maxRelDiff <= 0.12
        && (nameStemMatch || flags.isSameParent);

    return flags;
}

} // namespace

namespace AssemblyRelationGraph
{

QString makeMeshPairKey(const QUuid& a, const QUuid& b)
{
    const QString sa = a.toString(QUuid::WithoutBraces);
    const QString sb = b.toString(QUuid::WithoutBraces);
    return sa < sb ? (sa + QLatin1Char('|') + sb)
                   : (sb + QLatin1Char('|') + sa);
}

AutoPlacementHints buildAutoPlacementHints(const QSet<QUuid>& assemblyUuids,
                                           const ViewportWidget* viewportWidget,
                                           const SceneGraph* sceneGraph)
{
    AutoPlacementHints hints;
    const QVector<PartInfo> parts = collectPartInfos(assemblyUuids, viewportWidget, sceneGraph);

    // Populate sub-assembly groups from scene-graph parent nodes.
    for (const PartInfo& part : parts)
    {
        if (part.parentNode)
            hints.partGroupUuid.insert(part.meshUuid, part.parentNode->nodeUuid);
    }

    // Returns pointer to the outer (containing) PartInfo, or nullptr if neither contains the other.
    const auto outerOfNested = [](const PartInfo& a, const PartInfo& b) -> const PartInfo* {
        const auto isOuter = [](const PartInfo& outer, const PartInfo& inner) {
            const float tol = std::max(0.25f, outer.diagonal * 0.03f);
            if (!pointInExpandedBox(inner.center, outer.box, tol))
                return false;
            return containmentFraction(outer.box, inner.box) >= 0.75f
                && outer.box.volume() / std::max(1.0e-5f, inner.box.volume()) >= 1.15f;
        };
        if (isOuter(a, b)) return &a;
        if (isOuter(b, a)) return &b;
        return nullptr;
    };

    for (int i = 0; i < parts.size(); ++i)
    {
        for (int j = i + 1; j < parts.size(); ++j)
        {
            const PairRelationFlags flags = classifyPair(parts[i], parts[j]);
            const QString key = makeMeshPairKey(parts[i].meshUuid, parts[j].meshUuid);

            if (flags.isSameParent || flags.isCoaxial || flags.isRepeated)
                hints.strongPairKeys.insert(key);

            if (flags.isNear)
                hints.nearPairKeys.insert(key);

            if (flags.isNested)
            {
                if (const PartInfo* outer = outerOfNested(parts[i], parts[j]))
                    hints.nestedOuterUuid.insert(key, outer->meshUuid);
            }
        }
    }

    return hints;
}

QJsonObject buildInspectionJson(const QSet<QUuid>& assemblyUuids,
                                const ViewportWidget* viewportWidget,
                                const SceneGraph* sceneGraph)
{
    QJsonObject root;
    QJsonArray nodesJson;
    QJsonArray edgesJson;

    if (!viewportWidget || assemblyUuids.isEmpty())
    {
        root.insert(QStringLiteral("nodes"), nodesJson);
        root.insert(QStringLiteral("edges"), edgesJson);
        root.insert(QStringLiteral("meshCount"), 0);
        return root;
    }

    const QVector<PartInfo> parts = collectPartInfos(assemblyUuids, viewportWidget, sceneGraph);

    for (const PartInfo& part : parts)
        nodesJson.append(buildNodeJson(part));

    QJsonObject summaryCounts;
    int nearCount = 0;
    int nestedCount = 0;
    int coaxialCount = 0;
    int sameParentCount = 0;
    int repeatedCount = 0;

    for (int i = 0; i < parts.size(); ++i)
    {
        for (int j = i + 1; j < parts.size(); ++j)
        {
            const PartInfo& a = parts[i];
            const PartInfo& b = parts[j];

            const PairRelationFlags flags = classifyPair(a, b);
            const float avgDiag = std::max(1.0e-4f, (a.diagonal + b.diagonal) * 0.5f);
            const float nearDistance = boxDistance(a.box, b.box);
            const float nearThreshold = std::max(0.5f, avgDiag * 0.2f);
            if (flags.isNear)
            {
                QJsonObject metrics;
                metrics.insert(QStringLiteral("distance"), nearDistance);
                metrics.insert(QStringLiteral("threshold"), nearThreshold);
                metrics.insert(QStringLiteral("normalizedDistance"), nearDistance / nearThreshold);
                const double score = 1.0 - std::min(1.0, static_cast<double>(nearDistance / nearThreshold));
                edgesJson.append(buildEdgeJson(QStringLiteral("nearParts"), a, b, score, metrics));
                ++nearCount;
            }

            const auto appendNestedEdge = [&](const PartInfo& outer, const PartInfo& inner) {
                const float tol = std::max(0.25f, outer.diagonal * 0.03f);
                if (!pointInExpandedBox(inner.center, outer.box, tol))
                    return;

                const float containFrac = containmentFraction(outer.box, inner.box);
                const float volumeRatio = outer.box.volume() / std::max(1.0e-5f, inner.box.volume());
                if (containFrac < 0.75f || volumeRatio < 1.15f)
                    return;

                QJsonObject metrics;
                metrics.insert(QStringLiteral("outerMesh"), outer.meshUuid.toString(QUuid::WithoutBraces));
                metrics.insert(QStringLiteral("innerMesh"), inner.meshUuid.toString(QUuid::WithoutBraces));
                metrics.insert(QStringLiteral("containmentFraction"), containFrac);
                metrics.insert(QStringLiteral("volumeRatio"), volumeRatio);
                metrics.insert(QStringLiteral("tolerance"), tol);
                const double score = std::min(1.0, 0.65 * containFrac + 0.35 * std::min(volumeRatio / 4.0f, 1.0f));
                edgesJson.append(buildEdgeJson(QStringLiteral("nestedParts"), outer, inner, score, metrics));
                ++nestedCount;
            };

            appendNestedEdge(a, b);
            appendNestedEdge(b, a);

            if (flags.isCoaxial)
            {
                const int axis = a.coaxialAxis;
                const int other0 = (axis + 1) % 3;
                const int other1 = (axis + 2) % 3;
                const float orthDist = std::sqrt(
                    std::pow(axisComponent(a.center, other0) - axisComponent(b.center, other0), 2.0f) +
                    std::pow(axisComponent(a.center, other1) - axisComponent(b.center, other1), 2.0f));
                const float avgCrossSection =
                    0.25f * (axisComponent(a.size, other0) + axisComponent(a.size, other1)
                           + axisComponent(b.size, other0) + axisComponent(b.size, other1));
                const float orthThreshold = std::max(0.35f, avgCrossSection * 0.45f);
                const float axisGap = gap1D(axisComponent(a.box.min, axis), axisComponent(a.box.max, axis),
                                            axisComponent(b.box.min, axis), axisComponent(b.box.max, axis));
                const float axisThreshold = std::max(
                    0.5f,
                    0.75f * (axisComponent(a.size, axis) + axisComponent(b.size, axis)));

                if (orthDist <= orthThreshold && axisGap <= axisThreshold)
                {
                    const double orthScore = 1.0 - std::min(1.0, static_cast<double>(orthDist / orthThreshold));
                    const double gapScore = 1.0 - std::min(1.0, static_cast<double>(axisGap / axisThreshold));
                    const double kindBoost = (a.coaxialAxisKind == b.coaxialAxisKind) ? 1.0 : 0.85;

                    QJsonObject metrics;
                    metrics.insert(QStringLiteral("axis"), axis);
                    metrics.insert(QStringLiteral("axisKindA"), a.coaxialAxisKind);
                    metrics.insert(QStringLiteral("axisKindB"), b.coaxialAxisKind);
                    metrics.insert(QStringLiteral("orthogonalDistance"), orthDist);
                    metrics.insert(QStringLiteral("orthogonalThreshold"), orthThreshold);
                    metrics.insert(QStringLiteral("axisGap"), axisGap);
                    metrics.insert(QStringLiteral("axisGapThreshold"), axisThreshold);

                    edgesJson.append(buildEdgeJson(
                        QStringLiteral("approximateCoaxialCandidates"),
                        a,
                        b,
                        std::min(1.0, (0.55 * orthScore + 0.45 * gapScore) * kindBoost),
                        metrics));
                    ++coaxialCount;
                }
            }

            if (flags.isSameParent)
            {
                QJsonObject metrics;
                metrics.insert(QStringLiteral("parentNodeUuid"),
                               a.parentNode->nodeUuid.toString(QUuid::WithoutBraces));
                metrics.insert(QStringLiteral("parentNodeName"), a.parentNode->name);
                edgesJson.append(buildEdgeJson(QStringLiteral("sameParentCandidates"), a, b, 1.0, metrics));
                ++sameParentCount;
            }

            std::array<float, 3> dimsA{a.size.x(), a.size.y(), a.size.z()};
            std::array<float, 3> dimsB{b.size.x(), b.size.y(), b.size.z()};
            std::sort(dimsA.begin(), dimsA.end());
            std::sort(dimsB.begin(), dimsB.end());
            double maxRelDiff = 0.0;
            for (int axis = 0; axis < 3; ++axis)
            {
                const double denom = std::max(1.0e-5, static_cast<double>(std::max(dimsA[axis], dimsB[axis])));
                maxRelDiff = std::max(maxRelDiff, std::abs(dimsA[axis] - dimsB[axis]) / denom);
            }

            const bool nameStemMatch = !a.normalizedNameStem.isEmpty()
                && a.normalizedNameStem == b.normalizedNameStem;

            if (flags.isRepeated)
            {
                QJsonObject metrics;
                metrics.insert(QStringLiteral("maxRelativeExtentDifference"), maxRelDiff);
                metrics.insert(QStringLiteral("nameStemMatch"), nameStemMatch);
                metrics.insert(QStringLiteral("nameStem"), nameStemMatch ? a.normalizedNameStem : QString());
                const double score = std::min(1.0, 0.7 * (1.0 - maxRelDiff) + 0.3 * (nameStemMatch ? 1.0 : 0.6));
                edgesJson.append(buildEdgeJson(QStringLiteral("repeatedSpatialPatterns"), a, b, score, metrics));
                ++repeatedCount;
            }
        }
    }

    summaryCounts.insert(QStringLiteral("nearParts"), nearCount);
    summaryCounts.insert(QStringLiteral("nestedParts"), nestedCount);
    summaryCounts.insert(QStringLiteral("approximateCoaxialCandidates"), coaxialCount);
    summaryCounts.insert(QStringLiteral("sameParentCandidates"), sameParentCount);
    summaryCounts.insert(QStringLiteral("repeatedSpatialPatterns"), repeatedCount);

    QJsonObject heuristics;
    heuristics.insert(QStringLiteral("note"),
                      QStringLiteral("Diagnostic heuristics only. These relations are not yet used by auto explode."));
    heuristics.insert(QStringLiteral("nearParts"),
                      QStringLiteral("AABB distance threshold based on average part diagonal."));
    heuristics.insert(QStringLiteral("nestedParts"),
                      QStringLiteral("Inner box center inside outer box plus strong box overlap and volume ratio."));
    heuristics.insert(QStringLiteral("approximateCoaxialCandidates"),
                      QStringLiteral("Axis inferred from AABB proportions, then checked for orthogonal centerline proximity."));
    heuristics.insert(QStringLiteral("sameParentCandidates"),
                      QStringLiteral("SceneGraph owner nodes share the same parent node."));
    heuristics.insert(QStringLiteral("repeatedSpatialPatterns"),
                      QStringLiteral("Similar sorted extents with matching normalized names or same-parent sibling repetition."));

    root.insert(QStringLiteral("meshCount"), parts.size());
    root.insert(QStringLiteral("nodes"), nodesJson);
    root.insert(QStringLiteral("edges"), edgesJson);
    root.insert(QStringLiteral("summaryCounts"), summaryCounts);
    root.insert(QStringLiteral("heuristics"), heuristics);
    return root;
}

} // namespace AssemblyRelationGraph
