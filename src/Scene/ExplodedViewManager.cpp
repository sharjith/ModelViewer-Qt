#include "ExplodedViewManager.h"

#include <QtGlobal>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <limits>
#include <vector>

void ExplodedViewManager::reset()
{
    _baseOffsets.clear();
    _scaledOffsets.clear();
    _factor = 1.0f;
}

QVector3D ExplodedViewManager::offsetForMesh(const QUuid& uuid) const
{
    return _scaledOffsets.value(uuid, QVector3D());
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Returns the (min, max) projection of an AABB onto a unit axis direction.
// Uses the AABB support function: for each component, pick min or max based on
// the sign of axisDir.  Works for any axis direction, not just canonical axes.
static std::pair<float, float>
aabbAxisExtent(const QPair<QVector3D, QVector3D>& box, const QVector3D& axisDir)
{
    const QVector3D& bMin = box.first;
    const QVector3D& bMax = box.second;

    float pMin = 0.0f, pMax = 0.0f;
    const float dx = axisDir.x(), dy = axisDir.y(), dz = axisDir.z();
    pMin = std::min(dx * bMin.x(), dx * bMax.x())
         + std::min(dy * bMin.y(), dy * bMax.y())
         + std::min(dz * bMin.z(), dz * bMax.z());
    pMax = std::max(dx * bMin.x(), dx * bMax.x())
         + std::max(dy * bMin.y(), dy * bMax.y())
         + std::max(dz * bMin.z(), dz * bMax.z());
    return {pMin, pMax};
}

static QVector3D sectorDirection(int sector)
{
    switch (sector)
    {
    case 0: return QVector3D(1, 0, 0);
    case 1: return QVector3D(-1, 0, 0);
    case 2: return QVector3D(0, 1, 0);
    case 3: return QVector3D(0, -1, 0);
    case 4: return QVector3D(0, 0, 1);
    default: return QVector3D(0, 0, -1);
    }
}

static float axisComponent(const QVector3D& vector, int axis)
{
    switch (axis)
    {
    case 0: return vector.x();
    case 1: return vector.y();
    default: return vector.z();
    }
}

static std::array<int, 3> sortedAxisIndicesByExtent(const QVector3D& size)
{
    std::array<int, 3> indices{0, 1, 2};
    std::sort(indices.begin(), indices.end(), [&](int a, int b) {
        return axisComponent(size, a) < axisComponent(size, b);
    });
    return indices;
}

struct ShapeAxisInfo
{
    int axis = -1;
    bool elongated = false;
    bool discLike = false;
};

static ShapeAxisInfo inferShapeAxis(const QPair<QVector3D, QVector3D>& box)
{
    const QVector3D size = box.second - box.first;
    const auto order = sortedAxisIndicesByExtent(size);
    const float minExtent = std::max(1.0e-5f, axisComponent(size, order[0]));
    const float midExtent = std::max(1.0e-5f, axisComponent(size, order[1]));
    const float maxExtent = std::max(1.0e-5f, axisComponent(size, order[2]));

    ShapeAxisInfo info;
    const float largeToMid = maxExtent / midExtent;
    const float midToSmall = midExtent / minExtent;

    if (largeToMid >= 1.35f && midToSmall <= 1.2f)
    {
        info.axis = order[2];
        info.elongated = true;
        return info;
    }

    if (midToSmall >= 1.35f && largeToMid <= 1.2f)
    {
        info.axis = order[0];
        info.discLike = true;
        return info;
    }

    if (maxExtent / minExtent >= 1.75f)
    {
        if ((maxExtent - midExtent) >= (midExtent - minExtent))
        {
            info.axis = order[2];
            info.elongated = true;
        }
        else
        {
            info.axis = order[0];
            info.discLike = true;
        }
    }

    return info;
}

static int dominantAxisSector(const QUuid& uuid,
                              const QVector3D& origin,
                              const QVector3D& center,
                              const QHash<QUuid, QPair<QVector3D, QVector3D>>& worldBoxes)
{
    const QVector3D delta = center - origin;
    const float absX = std::abs(delta.x());
    const float absY = std::abs(delta.y());
    const float absZ = std::abs(delta.z());

    if (absX > 1e-4f || absY > 1e-4f || absZ > 1e-4f)
    {
        if (absX >= absY && absX >= absZ)
            return delta.x() >= 0.0f ? 0 : 1;
        if (absY >= absX && absY >= absZ)
            return delta.y() >= 0.0f ? 2 : 3;
        return delta.z() >= 0.0f ? 4 : 5;
    }

    if (worldBoxes.contains(uuid))
    {
        const auto& box = worldBoxes.value(uuid);
        const QVector3D& bMin = box.first;
        const QVector3D& bMax = box.second;

        const float scores[6] = {
            bMax.x() - origin.x(),
            origin.x() - bMin.x(),
            bMax.y() - origin.y(),
            origin.y() - bMin.y(),
            bMax.z() - origin.z(),
            origin.z() - bMin.z()
        };

        int bestIdx = 0;
        float bestScore = scores[0];
        for (int i = 1; i < 6; ++i)
        {
            if (scores[i] > bestScore)
            {
                bestScore = scores[i];
                bestIdx = i;
            }
        }
        if (bestScore > 1e-4f)
            return bestIdx;
    }

    return static_cast<int>(qHash(uuid) % 6u);
}

// ---------------------------------------------------------------------------
// recompute
// ---------------------------------------------------------------------------
void ExplodedViewManager::recompute(
    const QSet<QUuid>&                          assemblyUuids,
    const QUuid&                                anchorUuid,
    Mode                                        mode,
    const QVector3D&                            userVector,
    float                                       factor,
    const QHash<QUuid, QVector3D>&              worldCentroids,
    const QHash<QUuid, QPair<QVector3D,QVector3D>>& worldBoxes,
    const AssemblyRelationGraph::AutoPlacementHints* autoHints)
{
    reset();

    if (assemblyUuids.isEmpty())
        return;

    _factor = factor;
    layoutFlat(assemblyUuids, anchorUuid, mode, userVector, worldCentroids, worldBoxes, autoHints, _baseOffsets);
    applyFactor();
}

void ExplodedViewManager::layoutFlat(
    const QSet<QUuid>&                          assemblyUuids,
    const QUuid&                                anchorUuid,
    Mode                                        mode,
    const QVector3D&                            userVector,
    const QHash<QUuid, QVector3D>&              worldCentroids,
    const QHash<QUuid, QPair<QVector3D,QVector3D>>& worldBoxes,
    const AssemblyRelationGraph::AutoPlacementHints* autoHints,
    QHash<QUuid, QVector3D>&                    out) const
{
    if (assemblyUuids.isEmpty())
        return;

    // --- Assembly bounding box centre (explosion origin for no-anchor modes) ---
    // Use the geometric centre of the combined AABB rather than the arithmetic
    // mean of part centroids. The mean is biased by part count (e.g. many small
    // fasteners clustered near one truck shifts the origin toward that end),
    // which causes parts near the true centre to get the wrong sector assignment.
    QVector3D origin;
    {
        QVector3D bboxMin( std::numeric_limits<float>::max(),
                            std::numeric_limits<float>::max(),
                            std::numeric_limits<float>::max());
        QVector3D bboxMax(-std::numeric_limits<float>::max(),
                          -std::numeric_limits<float>::max(),
                          -std::numeric_limits<float>::max());
        bool anyBox = false;
        for (const QUuid& uuid : assemblyUuids)
        {
            if (!worldBoxes.contains(uuid)) continue;
            const auto& box = worldBoxes.value(uuid);
            bboxMin = QVector3D(std::min(bboxMin.x(), box.first.x()),
                                std::min(bboxMin.y(), box.first.y()),
                                std::min(bboxMin.z(), box.first.z()));
            bboxMax = QVector3D(std::max(bboxMax.x(), box.second.x()),
                                std::max(bboxMax.y(), box.second.y()),
                                std::max(bboxMax.z(), box.second.z()));
            anyBox = true;
        }
        if (anyBox)
            origin = (bboxMin + bboxMax) * 0.5f;
    }
    if (!anchorUuid.isNull() && worldCentroids.contains(anchorUuid))
        origin = worldCentroids.value(anchorUuid);

    // --- Assembly bounding radius (used for gap sizing in all modes) ---
    float assemblyRadius = 0.0f;
    for (const QUuid& uuid : assemblyUuids)
    {
        if (!worldCentroids.contains(uuid)) continue;
        assemblyRadius = std::max(assemblyRadius,
            (worldCentroids.value(uuid) - origin).length());
    }
    if (assemblyRadius < 1e-6f) assemblyRadius = 1.0f;

    // --- Axis direction ---
    QVector3D axisDir;
    bool fixedAxis = false;
    switch (mode)
    {
    case Mode::AxisX:  axisDir = {1,0,0}; fixedAxis = true; break;
    case Mode::AxisY:  axisDir = {0,1,0}; fixedAxis = true; break;
    case Mode::AxisZ:  axisDir = {0,0,1}; fixedAxis = true; break;
    case Mode::Vector:
        if (userVector.lengthSquared() > 1e-9f)
        { axisDir = userVector.normalized(); fixedAxis = true; }
        break;
    default: break;
    }

    // ======================================================================
    // AXIAL MODE  —  bbox-clearance-based placement
    // ======================================================================
    if (fixedAxis)
    {
        // Collect per-mesh data: centroid projection + bbox min/max along axis.
        struct MeshData
        {
            QUuid  uuid;
            float  centerProj; // centroid projected onto axisDir
            float  projMin;    // bbox min projected onto axisDir
            float  projMax;    // bbox max projected onto axisDir
            float  size() const { return projMax - projMin; }
        };

        std::vector<MeshData> parts;
        parts.reserve(static_cast<size_t>(assemblyUuids.size()));

        for (const QUuid& uuid : assemblyUuids)
        {
            if (!worldCentroids.contains(uuid)) continue;

            const float cp = QVector3D::dotProduct(worldCentroids.value(uuid), axisDir);
            float pMin = cp, pMax = cp; // fallback: point extent

            if (worldBoxes.contains(uuid))
            {
                auto [mn, mx] = aabbAxisExtent(worldBoxes.value(uuid), axisDir);
                pMin = mn; pMax = mx;
            }
            parts.push_back({uuid, cp, pMin, pMax});
        }

        if (parts.empty()) return;

        std::sort(parts.begin(), parts.end(),
                  [](const MeshData& a, const MeshData& b)
                  { return a.centerProj < b.centerProj; });

        const int n = static_cast<int>(parts.size());

        // Gap between adjacent bounding boxes at factor = 1.
        // Use the average part size along the axis so that the gap is
        // proportional to part geometry, not the overall assembly extent.
        float totalSize = 0.0f;
        for (const auto& p : parts) totalSize += p.size();
        const float avgSize  = totalSize / n;
        // The radius-based floor is capped so that n gaps add up to at most about one radius; otherwise the line-up grows with the part count.
        const float baseGap  = std::max(avgSize * 0.5f,
                                        std::min(assemblyRadius * 0.05f, assemblyRadius / static_cast<float>(n)));

        // ----------------------------------------------------------------
        // WITH ANCHOR  —  anchor pinned, parts placed by bbox-clearance
        // ----------------------------------------------------------------
        if (!anchorUuid.isNull() && worldCentroids.contains(anchorUuid))
        {
            // Find anchor's rank in sorted list.
            int refIdx = 0;
            for (int i = 0; i < n; ++i)
                if (parts[i].uuid == anchorUuid) { refIdx = i; break; }

            const float anchorProjMin = parts[refIdx].projMin;
            const float anchorProjMax = parts[refIdx].projMax;

            // Parts above anchor (higher projection → move in +axisDir).
            float prevMax = anchorProjMax;
            for (int i = refIdx + 1; i < n; ++i)
            {
                const float targetMin = prevMax + baseGap;
                const float disp      = targetMin - parts[i].projMin;
                out.insert(parts[i].uuid, axisDir * disp);
                prevMax = targetMin + parts[i].size();
            }

            // Parts below anchor (lower projection → move in -axisDir).
            float prevMin = anchorProjMin;
            for (int i = refIdx - 1; i >= 0; --i)
            {
                const float targetMax = prevMin - baseGap;
                const float disp      = targetMax - parts[i].projMax;
                out.insert(parts[i].uuid, axisDir * disp);
                prevMin = targetMax - parts[i].size();
            }
        }
        // ----------------------------------------------------------------
        // NO ANCHOR  —  layout centred at assembly centroid projection
        // ----------------------------------------------------------------
        else
        {
            // Total layout width = sum of all part sizes + (n-1) gaps.
            const float layoutWidth = totalSize + (n - 1) * baseGap;
            const float centroidProj = QVector3D::dotProduct(origin, axisDir);

            // Start placing from the left edge of the centred layout.
            float cursor = centroidProj - layoutWidth * 0.5f;
            for (int i = 0; i < n; ++i)
            {
                const float targetMin = cursor;
                const float disp      = targetMin - parts[i].projMin;
                if (std::abs(disp) > 1e-9f)
                    out.insert(parts[i].uuid, axisDir * disp);
                cursor += parts[i].size() + baseGap;
            }
        }

        return;
    }

    // ======================================================================
    // AUTO MODE  —  assign each part to a dominant canonical sector, then
    //               pack outward using bbox-clearance placement per sector.
    // ======================================================================
    struct MeshData
    {
        QUuid  uuid;
        float  centerProj;
        float  projMin;
        float  projMax;
        float  diagonal3D = 0.0f; // 3D bbox diagonal; tiebreaker for coaxial parts
        float  size() const { return projMax - projMin; }
    };

    struct BackboneData
    {
        QUuid uuid;
        int axis = -1;
        QVector3D center;
        QPair<QVector3D, QVector3D> box;
    };

    struct ChainPart
    {
        QUuid uuid;
        float absSignedDistance = 0.0f;
        float diagonal = 0.0f;
        float projMin = 0.0f;
        float projMax = 0.0f;
        float size() const { return projMax - projMin; }
    };

    std::vector<MeshData> sectors[6];
    QHash<QUuid, ShapeAxisInfo> shapeInfoByUuid;
    std::vector<BackboneData> backbones;

    for (const QUuid& uuid : assemblyUuids)
    {
        if (!worldBoxes.contains(uuid) || !worldCentroids.contains(uuid))
            continue;

        const ShapeAxisInfo shapeInfo = inferShapeAxis(worldBoxes.value(uuid));
        shapeInfoByUuid.insert(uuid, shapeInfo);
        if (shapeInfo.elongated && shapeInfo.axis >= 0)
            backbones.push_back({uuid, shapeInfo.axis, worldCentroids.value(uuid), worldBoxes.value(uuid)});
    }

    if (!backbones.empty())
    {
        const bool anchorHasCentroid = !anchorUuid.isNull() && worldCentroids.contains(anchorUuid);
        const QVector3D anchorCenter = anchorHasCentroid ? worldCentroids.value(anchorUuid) : origin;

        std::sort(backbones.begin(), backbones.end(), [&](const BackboneData& a, const BackboneData& b) {
            const bool aIsAnchor = !anchorUuid.isNull() && a.uuid == anchorUuid;
            const bool bIsAnchor = !anchorUuid.isNull() && b.uuid == anchorUuid;
            if (aIsAnchor != bIsAnchor)
                return aIsAnchor;

            const float distA = (a.center - anchorCenter).lengthSquared();
            const float distB = (b.center - anchorCenter).lengthSquared();
            constexpr float kDistTolerance = 1.0e-4f;
            if (distA + kDistTolerance < distB)
                return true;
            if (distB + kDistTolerance < distA)
                return false;

            return a.uuid.toString(QUuid::WithoutBraces) < b.uuid.toString(QUuid::WithoutBraces);
        });
    }

    for (const QUuid& uuid : assemblyUuids)
    {
        if (uuid == anchorUuid || !worldCentroids.contains(uuid))
            continue;

        const int sector = dominantAxisSector(uuid, origin, worldCentroids.value(uuid), worldBoxes);
        const QVector3D dir = sectorDirection(sector);

        const float cp = QVector3D::dotProduct(worldCentroids.value(uuid), dir);
        float pMin = cp;
        float pMax = cp;
        float diag3D = 0.0f;
        if (worldBoxes.contains(uuid))
        {
            auto [mn, mx] = aabbAxisExtent(worldBoxes.value(uuid), dir);
            pMin = mn;
            pMax = mx;
            diag3D = (worldBoxes.value(uuid).second - worldBoxes.value(uuid).first).length();
        }
        sectors[sector].push_back({uuid, cp, pMin, pMax, diag3D});
    }

    const auto pairIsStrong = [autoHints](const QUuid& a, const QUuid& b) {
        return autoHints
            && autoHints->strongPairKeys.contains(AssemblyRelationGraph::makeMeshPairKey(a, b));
    };

    QSet<QUuid> handledByBackbone;

    for (const BackboneData& backbone : backbones)
    {
        const QVector3D positiveDir =
            backbone.axis == 0 ? QVector3D(1, 0, 0)
          : backbone.axis == 1 ? QVector3D(0, 1, 0)
                               : QVector3D(0, 0, 1);

        std::vector<ChainPart> positiveChain;
        std::vector<ChainPart> negativeChain;

        for (const QUuid& uuid : assemblyUuids)
        {
            if (uuid == anchorUuid || uuid == backbone.uuid || handledByBackbone.contains(uuid))
                continue;
            if (!worldBoxes.contains(uuid) || !worldCentroids.contains(uuid))
                continue;

            const bool stronglyRelatedToBackbone = pairIsStrong(backbone.uuid, uuid);
            const bool stronglyRelatedToAnchor = !anchorUuid.isNull() && pairIsStrong(anchorUuid, uuid);
            if (!stronglyRelatedToBackbone && !(backbone.uuid == anchorUuid && stronglyRelatedToAnchor))
                continue;

            const ShapeAxisInfo shapeInfo = shapeInfoByUuid.value(uuid);
            if (shapeInfo.axis != backbone.axis)
                continue;

            const QVector3D partCenter = worldCentroids.value(uuid);
            const QVector3D delta = partCenter - backbone.center;
            const int axisA = (backbone.axis + 1) % 3;
            const int axisB = (backbone.axis + 2) % 3;
            const float orthDist = std::sqrt(
                std::pow(axisComponent(delta, axisA), 2.0f) +
                std::pow(axisComponent(delta, axisB), 2.0f));

            const QVector3D backboneSize = backbone.box.second - backbone.box.first;
            const QVector3D partSize = worldBoxes.value(uuid).second - worldBoxes.value(uuid).first;
            const float orthThreshold = std::max(
                1.0f,
                0.35f * (axisComponent(backboneSize, axisA) + axisComponent(backboneSize, axisB)
                       + axisComponent(partSize, axisA) + axisComponent(partSize, axisB)));
            if (orthDist > orthThreshold)
                continue;

            const float signedDistance = QVector3D::dotProduct(delta, positiveDir);
            if (std::abs(signedDistance) <= 1.0e-4f)
                continue;

            auto [posMin, posMax] = aabbAxisExtent(worldBoxes.value(uuid), positiveDir);
            const QVector3D diagVec = worldBoxes.value(uuid).second - worldBoxes.value(uuid).first;
            const float diagonal = diagVec.length();
            ChainPart chainPart{uuid, std::abs(signedDistance), diagonal, posMin, posMax};
            if (signedDistance > 0.0f)
                positiveChain.push_back(chainPart);
            else
                negativeChain.push_back(chainPart);
        }

        auto sortChain = [](std::vector<ChainPart>& chain) {
            std::sort(chain.begin(), chain.end(), [](const ChainPart& a, const ChainPart& b) {
                constexpr float kDistTolerance = 1.0e-4f;
                constexpr float kDiagTolerance = 1.0e-4f;
                if (a.absSignedDistance + kDistTolerance < b.absSignedDistance)
                    return true;
                if (b.absSignedDistance + kDistTolerance < a.absSignedDistance)
                    return false;
                if (a.diagonal + kDiagTolerance < b.diagonal)
                    return true;
                if (b.diagonal + kDiagTolerance < a.diagonal)
                    return false;
                return a.uuid.toString(QUuid::WithoutBraces) < b.uuid.toString(QUuid::WithoutBraces);
            });
        };

        sortChain(positiveChain);
        sortChain(negativeChain);

        // Gap sized to average chain-part extent, not raw assembly radius alone.
        // This prevents enormous gaps when small fasteners hang off a large backbone.
        auto applyChain = [&](const std::vector<ChainPart>& chain, const QVector3D& dir) {
            if (chain.empty())
                return;

            float totalChainSize = 0.0f;
            for (const ChainPart& p : chain)
                totalChainSize += p.size();
            const float avgChainPartSize = totalChainSize / static_cast<float>(chain.size());
            const float chainGap = std::max({avgChainPartSize * 0.35f,
                                             assemblyRadius * 0.05f,
                                             1.0f});

            auto [backboneMin, backboneMax] = aabbAxisExtent(backbone.box, dir);
            Q_UNUSED(backboneMin);
            const float dirOriginProj = QVector3D::dotProduct(backbone.center, dir);
            float cursor = (backboneMax - dirOriginProj) + chainGap;

            for (const ChainPart& part : chain)
            {
                const auto [partMin, partMax] = aabbAxisExtent(worldBoxes.value(part.uuid), dir);
                const float relMin = partMin - dirOriginProj;
                const float targetMin = std::max(cursor, relMin);
                const float disp = targetMin - relMin;
                if (disp > 1.0e-6f)
                    out.insert(part.uuid, dir * disp);
                cursor = targetMin + (partMax - partMin) + chainGap;
                handledByBackbone.insert(part.uuid);
            }
        };

        applyChain(positiveChain, positiveDir);
        applyChain(negativeChain, -positiveDir);
    }

    for (int sector = 0; sector < 6; ++sector)
    {
        auto& parts = sectors[sector];
        if (parts.empty())
            continue;

        const QVector3D dir = sectorDirection(sector);
        const float originProj = QVector3D::dotProduct(origin, dir);
        parts.erase(std::remove_if(parts.begin(), parts.end(), [&](const MeshData& part) {
            return handledByBackbone.contains(part.uuid);
        }), parts.end());
        if (parts.empty())
            continue;

        std::sort(parts.begin(), parts.end(),
                  [&](const MeshData& a, const MeshData& b)
                  {
                      constexpr float kCenterTieTolerance = 1.0e-4f;
                      constexpr float kExtentTieTolerance = 1.0e-4f;

                      if (a.centerProj + kCenterTieTolerance < b.centerProj)
                          return true;
                      if (b.centerProj + kCenterTieTolerance < a.centerProj)
                          return false;

                      const bool strongPair = pairIsStrong(a.uuid, b.uuid);

                      if (strongPair)
                      {
                          if (a.projMin + kExtentTieTolerance < b.projMin)
                              return true;
                          if (b.projMin + kExtentTieTolerance < a.projMin)
                              return false;
                          if (a.size() > b.size() + kExtentTieTolerance)
                              return true;
                          if (b.size() > a.size() + kExtentTieTolerance)
                              return false;
                      }

                      if (a.projMin + kExtentTieTolerance < b.projMin)
                          return true;
                      if (b.projMin + kExtentTieTolerance < a.projMin)
                          return false;
                      if (a.projMax + kExtentTieTolerance < b.projMax)
                          return true;
                      if (b.projMax + kExtentTieTolerance < a.projMax)
                          return false;
                      // Coaxial shells (e.g. tire over hub) have identical 1D projections.
                      // Use 3D bounding diagonal as tiebreaker: smaller diagonal = inner
                      // shell = placed first (closer to origin); larger = outer shell =
                      // placed farther. This is deterministic and physically correct.
                      if (a.diagonal3D + kExtentTieTolerance < b.diagonal3D)
                          return true;
                      if (b.diagonal3D + kExtentTieTolerance < a.diagonal3D)
                          return false;
                      return a.uuid.toString(QUuid::WithoutBraces)
                          < b.uuid.toString(QUuid::WithoutBraces);
                  });

        // The spread must depend on the assembly's size, not on how many parts it has. The previous scheme stacked every part of a
        // sector one after another along its axis, so the explosion grew in proportion to the part count (a gap of 5 % of the
        // radius per part: hundreds of radii for a few thousand parts). Here each part moves out in proportion to its own distance
        // from the origin (the whole assembly scales up about the origin), and is pushed further only past parts that really sit in
        // its way: those whose footprint overlaps its own across the axis. A column of N parts still gets N gaps, which is correct;
        // unrelated parts side by side no longer queue behind each other.
        constexpr float kSpread = 0.6f;                      // extra distance at factor 1, as a fraction of the distance from the origin
        const float minGap = std::max(assemblyRadius * 0.004f, 1.0e-4f);
        const float maxPush = assemblyRadius * 0.4f;         // most a part is pushed past others for clearance
        const float overlapTol =std::max(assemblyRadius * 1.0e-4f, 1.0e-6f);

        float floorMin = std::max(assemblyRadius * 0.01f, minGap);
        if (!anchorUuid.isNull() && worldBoxes.contains(anchorUuid))
        {
            auto [anchorMin, anchorMax] = aabbAxisExtent(worldBoxes.value(anchorUuid), dir);
            Q_UNUSED(anchorMin);
            floorMin = std::max(floorMin, (anchorMax - originProj) + minGap);
        }

        const int axisIdx = sector / 2;
        const int axisA = (axisIdx + 1) % 3;
        const int axisB = (axisIdx + 2) % 3;

        struct Placed
        {
            float aMin, aMax, bMin, bMax; // footprint across the axis
            float placedMax;              // far face along the axis, relative to the origin, after placement
        };
        std::vector<Placed> placed;
        placed.reserve(parts.size());

        for (const auto& part : parts)
        {
            const float relMin = part.projMin - originProj;
            const auto box = worldBoxes.value(part.uuid);
            const float aMin = axisComponent(box.first, axisA), aMax = axisComponent(box.second, axisA);
            const float bMin = axisComponent(box.first, axisB), bMax = axisComponent(box.second, axisB);
            const float gap = std::max(part.size() * 0.2f, minGap);

            float targetMin = relMin > 0.0f ? relMin * (1.0f + kSpread) : relMin;
            targetMin = std::max(targetMin, relMin < floorMin ? floorMin : relMin);
            const float baseTarget = targetMin;
            for (const Placed& other : placed)
            {
                if (other.aMin >= aMax - overlapTol || other.aMax <= aMin + overlapTol) continue;
                if (other.bMin >= bMax - overlapTol || other.bMax <= bMin + overlapTol) continue;
                targetMin = std::max(targetMin, other.placedMax + gap);
            }
            // Clearance may push a part past those in front of it, but only so far: a deep stack (or a part with a large footprint
            // that many others overlap) used to build long rails along the axes. Beyond the cap parts may still overlap in depth.
            targetMin = std::min(targetMin, baseTarget + maxPush);

            const float disp = targetMin - relMin;
            if (disp > 1e-6f)
                out.insert(part.uuid, dir * disp);
            placed.push_back({aMin, aMax, bMin, bMax, targetMin + part.size()});
        }
    }
}

void ExplodedViewManager::applyFactor()
{
    _scaledOffsets.clear();
    for (auto it = _baseOffsets.cbegin(); it != _baseOffsets.cend(); ++it)
        _scaledOffsets.insert(it.key(), it.value() * _factor);
}

// ---------------------------------------------------------------------------
// recomputeHierarchical
// ---------------------------------------------------------------------------
namespace
{
constexpr float kLevelDamping = 0.85f;       // per hierarchy level, applied to the offsets inside that level
constexpr float kGroupGrowthShare = 0.4f;    // share of a sub-group's own explosion growth that its siblings make room for
constexpr float kScaleGain = 1.0f;           // radial scale gain at factor 1: item positions about the group centre grow by (1 + gain)

// Proportional ("radial scale") layout of one group: every item (part or sub-group block) moves away from the centre of the group
// in proportion to its distance from it. The overall shape, symmetry and relative weights are kept: items near the centre barely
// move, outer items move most, and concentric parts stay concentric. The anchor item, when given, stays where it is.
void layoutRadialScaled(const QSet<QUuid>& items, const QUuid& anchorItem,
                        const QHash<QUuid, QVector3D>& centres,
                        const QHash<QUuid, QPair<QVector3D, QVector3D>>& boxes,
                        float gain, QHash<QUuid, QVector3D>& out,
                        const QHash<QUuid, QVector3D>* fixedOffsets = nullptr)   // items placed by another rule (mating layout)
{
    out.clear();
    if (items.size() < 2 || gain <= 0.0f)
        return;
    const float inf = std::numeric_limits<float>::max();
    QVector3D lo(inf, inf, inf), hi(-inf, -inf, -inf);
    for (const QUuid& id : items)
    {
        const auto it = boxes.constFind(id);
        if (it == boxes.constEnd())
            continue;
        lo = QVector3D(std::min(lo.x(), it->first.x()), std::min(lo.y(), it->first.y()), std::min(lo.z(), it->first.z()));
        hi = QVector3D(std::max(hi.x(), it->second.x()), std::max(hi.y(), it->second.y()), std::max(hi.z(), it->second.z()));
    }
    const QVector3D centre = (lo + hi) * 0.5f;

    QHash<QUuid, QVector3D> offsets;
    const auto isFixed = [&](const QUuid& id) { return fixedOffsets && fixedOffsets->contains(id); };
    for (const QUuid& id : items)
        offsets.insert(id, isFixed(id) ? fixedOffsets->value(id) : (centres.value(id) - centre) * gain);

    // Concentric items (wheel and tire, shaft and bush) share a centre, so scaling about it moves none of them and the level would
    // show no change. When two or more items sit at the group centre, spread them along the group's thinnest axis (the usual
    // axis of such parts), centred on the group so the layout stays symmetrical.
    const QVector3D extent = hi - lo;
    const float diag = extent.length();
    QVector<QUuid> central;
    for (const QUuid& id : items)
        if (!isFixed(id) && (centres.value(id) - centre).length() < 0.2f * diag)
            central.push_back(id);
    // Only when the whole group is concentric: in a big group, items near the middle are not coincident, and spreading them by the
    // group's diagonal threw parts far away.
    int movable = 0;
    for (const QUuid& id : items)
        if (!isFixed(id))
            ++movable;
    if (central.size() >= 2 && central.size() == movable && central.size() <= 4 && diag > 0.0f)
    {
        const auto volume = [&](const QUuid& id) {
            const auto b = boxes.value(id);
            const QVector3D e = b.second - b.first;
            return double(e.x()) * e.y() * e.z();
        };
        std::sort(central.begin(), central.end(), [&](const QUuid& a, const QUuid& b) {
            const double va = volume(a), vb = volume(b);
            return va != vb ? va < vb : a.toString() < b.toString();
        });
        int axis = 0;
        if (extent.y() < extent[axis]) axis = 1;
        if (extent.z() < extent[axis]) axis = 2;
        QVector3D dir;
        dir[axis] = 1.0f;
        const float step = 0.5f * diag * gain;
        const int m = central.size();
        for (int i = 0; i < m; ++i)
            offsets[central[i]] += dir * ((float(i) - 0.5f * float(m - 1)) * step);
    }

    const QVector3D hold = anchorItem.isNull() ? QVector3D() : offsets.value(anchorItem);
    for (auto it = offsets.cbegin(); it != offsets.cend(); ++it)
    {
        const QVector3D offset = it.value() - hold;
        if (offset.lengthSquared() > 1.0e-12f)
            out.insert(it.key(), offset);
    }
}

// "Mating" layout of one group: every item leaves the item it touches along the direction in which it comes off it, instead of
// moving away from the group centre. The direction is the axis of least penetration between the two boxes (a truck under a board
// drops straight down); a rod lying inside its partner (a screw in a block) leaves along its own long axis. An item moves with the
// item it is attached to plus a clearance, so fasteners clear the part they go through. Items with no clear contact partner keep
// the proportional radial placement.
void layoutMating(const QSet<QUuid>& items, const QUuid& anchorItem,
                  const QHash<QUuid, QVector3D>& centres,
                  const QHash<QUuid, QPair<QVector3D, QVector3D>>& boxes,
                  float gain, const QVector3D& refCentre, QHash<QUuid, QVector3D>& out)
{
    out.clear();
    if (items.size() < 2 || gain <= 0.0f)
        return;
    // A part uses the contact rule only when it is trustworthy (checked per part); otherwise it keeps the radial placement:
    //  - its best contact partner must clearly beat the runner-up (kPartnerDominance times the overlap),
    //  - the contact must be a face contact (small penetration), an embedded rod, or a concentric fit,
    //  - the partner must itself have been placed by the rule (parts placed radially are never used as partners).
    constexpr double kPartnerDominance = 2.0;
    // MODELVIEWER_EXPLODED_DEBUG=1 logs, for every item of every group, how the mating layout treated it.
    static const bool debugLog = qEnvironmentVariableIntValue("MODELVIEWER_EXPLODED_DEBUG") != 0;
    const auto tag = [&](const QUuid& id) {
        const auto b = boxes.value(id);
        const QVector3D e = b.second - b.first, c = (b.first + b.second) * 0.5f;
        return QStringLiteral("%1 ext(%2,%3,%4) ctr(%5,%6,%7)").arg(id.toString(QUuid::WithoutBraces).left(8))
            .arg(e.x(), 0, 'g', 4).arg(e.y(), 0, 'g', 4).arg(e.z(), 0, 'g', 4)
            .arg(c.x(), 0, 'g', 4).arg(c.y(), 0, 'g', 4).arg(c.z(), 0, 'g', 4);
    };
    const auto note = [&](const QUuid& id, const QString& what) {
        if (debugLog)
            qInfo().noquote() << "[ExplodedMating]" << tag(id) << "->" << what;
    };
    if (debugLog)
        qInfo().noquote() << "[ExplodedMating] group of" << items.size() << "items, gain" << gain;

    const float inf = std::numeric_limits<float>::max();
    QVector3D lo(inf, inf, inf), hi(-inf, -inf, -inf);
    for (const QUuid& id : items)
    {
        const auto it = boxes.constFind(id);
        if (it == boxes.constEnd())
            continue;
        lo = QVector3D(std::min(lo.x(), it->first.x()), std::min(lo.y(), it->first.y()), std::min(lo.z(), it->first.z()));
        hi = QVector3D(std::max(hi.x(), it->second.x()), std::max(hi.y(), it->second.y()), std::max(hi.z(), it->second.z()));
    }
    const QVector3D groupCentre = (lo + hi) * 0.5f;
    const float tol = 0.05f * (hi - lo).length();

    const auto extentOf = [&](const QUuid& id) { const auto b = boxes.value(id); return b.second - b.first; };
    const auto volumeOf = [&](const QUuid& id) {
        const QVector3D e = extentOf(id);
        return double(e.x()) * e.y() * e.z();
    };

    // Largest first: the biggest item is the base the others come off, and a part is placed after the part it sits in or on.
    QVector<QUuid> order(items.cbegin(), items.cend());
    std::sort(order.begin(), order.end(), [&](const QUuid& a, const QUuid& b) {
        const double va = volumeOf(a), vb = volumeOf(b);
        return va != vb ? va > vb : a.toString() < b.toString();
    });

    QHash<QUuid, QVector3D> placed;

    // How an item would leave a given partner: axis, direction and kind of contact, or why that is not clear.
    struct Exit
    {
        bool ok = false;
        int axis = 0;
        float dir = 1.0f;
        bool rod = false, embedded = false, concentric = false;
        QString why;
    };
    const auto exitFrom = [&](const QUuid& id, const QUuid& partner, const float sep[3]) {
        Exit e;
        const auto bi = boxes.value(id);
        const QVector3D ei = extentOf(id), ci = centres.value(id);
        const QVector3D ej = extentOf(partner), cj = centres.value(partner);
        // Axis of least penetration (or of the separating gap).
        int axis = 0;
        for (int a = 1; a < 3; ++a)
            if (sep[a] > sep[axis])
                axis = a;
        // A rod embedded in its partner leaves along its own length instead.
        e.embedded = -sep[axis] >= 0.5f * ei[axis];
        if (e.embedded)
        {
            int longest = 0;
            for (int a = 1; a < 3; ++a)
                if (ei[a] > ei[longest]) longest = a;
            float second = 0.0f;
            for (int a = 0; a < 3; ++a)
                if (a != longest) second = std::max(second, ei[a]);
            if (ei[longest] >= 2.0f * second)
            {
                axis = longest;
                e.rod = true;
            }
        }
        e.axis = axis;
        // Concentric = same centre in all three axes and a partner of similar size (a wheel in its tire). A part inside a much
        // larger partner (a wheel set inside the frame's box) is not concentric with it.
        const float ejMax = std::max(ej.x(), std::max(ej.y(), ej.z()));
        const float eiMax = std::max(ei.x(), std::max(ei.y(), ei.z()));
        e.concentric = (ci - cj).length() < 0.1f * ejMax && ejMax <= 1.5f * eiMax && eiMax <= 1.5f * ejMax;
        if (e.concentric)
        {
            int thin = 0;
            for (int a = 1; a < 3; ++a)
                if (ei[a] < ei[thin]) thin = a;
            e.axis = thin;
            const float outward = groupCentre[thin] - refCentre[thin];
            e.dir = outward < 0.0f ? -1.0f : 1.0f;
            e.ok = true;
            return e;
        }
        if (e.embedded && !e.rod)
        {
            e.why = QStringLiteral("buried in partner on axis %1").arg(axis);
            return e;
        }
        // Direction: away from the partner along the axis; when the centres coincide on it, away from the group centre.
        float side = ci[axis] - cj[axis];
        if (std::fabs(side) < 0.05f * std::max(ej[axis], 1.0e-6f))
            side = ci[axis] - groupCentre[axis];
        if (std::fabs(side) < 1.0e-6f)
        {
            e.why = QStringLiteral("no direction along axis %1").arg(axis);
            return e;
        }
        e.dir = side > 0.0f ? 1.0f : -1.0f;
        e.ok = true;
        return e;
    };

    struct Candidate { QUuid id; double score; float sep[3]; };
    for (const QUuid& id : order)
    {
        if (placed.isEmpty())
        {
            placed.insert(id, QVector3D());
            note(id, QStringLiteral("base (largest item), stays"));
            continue;
        }
        const auto bi = boxes.value(id);
        const QVector3D ei = extentOf(id);

        // Contact candidates among the items placed so far.
        QVector<Candidate> candidates;
        for (auto pit = placed.cbegin(); pit != placed.cend(); ++pit)
        {
            const auto bj = boxes.value(pit.key());
            Candidate c;
            c.id = pit.key();
            float maxSep = -inf;
            for (int a = 0; a < 3; ++a)
            {
                c.sep[a] = std::max(bi.first[a] - bj.second[a], bj.first[a] - bi.second[a]);   // < 0: boxes overlap on this axis
                maxSep = std::max(maxSep, c.sep[a]);
            }
            if (maxSep > tol)
                continue;
            c.score = 1.0;
            for (int a = 0; a < 3; ++a)
                c.score *= double(std::max(0.0f, -c.sep[a]) + 1.0e-6f);
            candidates.push_back(c);
        }
        if (candidates.isEmpty())
        {
            note(id, QStringLiteral("radial: touches no placed part"));
            continue;
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
        const Candidate& best = candidates.first();
        const Exit ex = exitFrom(id, best.id, best.sep);
        if (!ex.ok)
        {
            note(id, QStringLiteral("radial: %1 (partner %2)").arg(ex.why).arg(tag(best.id)));
            continue;
        }
        // Several partners touched about equally (a bolt through a truck and a board) are fine when they all leave along the same
        // axis. The direction is then that of the biggest of them, and the part moves out beyond the one that has itself moved
        // furthest that way, so it clears all of them.
        QVector<int> tied;
        tied.push_back(0);
        for (int k = 1; k < candidates.size(); ++k)
            if (best.score < kPartnerDominance * candidates[k].score)
                tied.push_back(k);
        int baseIdx = 0;
        float dir = ex.dir;
        if (tied.size() > 1)
        {
            bool sameAxis = true;
            int biggest = 0;
            for (int k : tied)
            {
                const Exit other = exitFrom(id, candidates[k].id, candidates[k].sep);
                if (!other.ok || other.axis != ex.axis)
                {
                    sameAxis = false;
                    break;
                }
                if (volumeOf(candidates[k].id) > volumeOf(candidates[biggest].id))
                    biggest = k;
            }
            if (!sameAxis)
            {
                note(id, QStringLiteral("radial: ambiguous partners leave along different axes (best %1)").arg(best.score));
                continue;
            }
            dir = exitFrom(id, candidates[biggest].id, candidates[biggest].sep).dir;
            float furthest = -inf;
            for (int k : tied)
            {
                const float along = dir * placed.value(candidates[k].id)[ex.axis];
                if (along > furthest)
                {
                    furthest = along;
                    baseIdx = k;
                }
            }
        }

        const Candidate& base = candidates[baseIdx];
        const QVector3D ej = extentOf(base.id);
        QVector3D offset = placed.value(base.id);
        if (ex.concentric && tied.size() == 1)
        {
            offset[ex.axis] += ex.dir * gain * 0.5f * (ei[ex.axis] + ej[ex.axis]);
            note(id, QStringLiteral("mating: concentric with %1, axis %2, dir %3").arg(tag(base.id)).arg(ex.axis).arg(ex.dir));
        }
        else
        {
            // Distance to leave the partner's box on that side, plus half the part's own size as a gap.
            const auto bj = boxes.value(base.id);
            const float exitDistance = dir > 0.0f ? std::max(0.0f, bj.second[ex.axis] - bi.first[ex.axis])
                                                  : std::max(0.0f, bi.second[ex.axis] - bj.first[ex.axis]);
            offset[ex.axis] += dir * gain * (exitDistance + 0.5f * ei[ex.axis]);
            note(id, QStringLiteral("mating: partner %1 (%2 tied), axis %3, dir %4, %5").arg(tag(base.id)).arg(tied.size())
                         .arg(ex.axis).arg(dir)
                         .arg(ex.rod ? QStringLiteral("rod") : (ex.embedded ? QStringLiteral("embedded") : QStringLiteral("face contact"))));
        }
        placed.insert(id, offset);
    }

    layoutRadialScaled(items, anchorItem, centres, boxes, gain, out, &placed);
}
}

void ExplodedViewManager::recomputeHierarchical(
    const ExplodedGroupTree::Tree&              tree,
    const QUuid&                                anchorUuid,
    float                                       factor,
    const QHash<QUuid, QVector3D>&              /*worldCentroids*/,
    const QHash<QUuid, QPair<QVector3D,QVector3D>>& worldBoxes,
    const AssemblyRelationGraph::AutoPlacementHints* autoHints,
    int                                         maxDepth,
    Mode                                        mode)
{
    using Box = QPair<QVector3D, QVector3D>;

    reset();
    if (tree.groups.isEmpty())
        return;
    _factor = factor;

    const int n = tree.groups.size();

    // Middle of the whole assembly: the mating layout uses it to tell outward from inward for concentric parts.
    QVector3D assemblyCentre;
    {
        const float inf = std::numeric_limits<float>::max();
        QVector3D lo(inf, inf, inf), hi(-inf, -inf, -inf);
        for (const ExplodedGroupTree::Group& g : tree.groups)
            for (const QUuid& part : g.parts)
            {
                const auto it = worldBoxes.constFind(part);
                if (it == worldBoxes.constEnd())
                    continue;
                lo = QVector3D(std::min(lo.x(), it->first.x()), std::min(lo.y(), it->first.y()), std::min(lo.z(), it->first.z()));
                hi = QVector3D(std::max(hi.x(), it->second.x()), std::max(hi.y(), it->second.y()), std::max(hi.z(), it->second.z()));
            }
        if (lo.x() <= hi.x())
            assemblyCentre = (lo + hi) * 0.5f;
    }

    // Depth of every group, and the groups whose subtree holds the anchor part (the groups are in pre-order: parents first).
    QVector<int> depth(n, 0);
    QVector<char> holdsAnchor(n, 0);
    for (int gi = 0; gi < n; ++gi)
    {
        const ExplodedGroupTree::Group& g = tree.groups[gi];
        depth[gi] = g.parent < 0 ? 0 : depth[g.parent] + 1;
        if (!anchorUuid.isNull() && g.parts.contains(anchorUuid))
            for (int up = gi; up >= 0; up = tree.groups[up].parent)
                holdsAnchor[up] = 1;
    }

    QVector<QHash<QUuid, QVector3D>> itemOffsets(n);   // offset of each part / sub-group block inside its group, factor 1
    QVector<Box> groupBox(n);                          // the group's box with its own explosion applied
    QVector<char> hasBox(n, 0);

    // Children come after their parents, so walking backwards lays out every sub-group before the group that contains it.
    for (int gi = n - 1; gi >= 0; --gi)
    {
        const ExplodedGroupTree::Group& g = tree.groups[gi];

        QSet<QUuid> items;
        QHash<QUuid, QVector3D> centres;
        QHash<QUuid, Box> boxes;
        const auto addItem = [&](const QUuid& id, const Box& box) {
            items.insert(id);
            boxes.insert(id, box);
            centres.insert(id, (box.first + box.second) * 0.5f);
        };
        for (const QUuid& part : g.parts)
        {
            const auto it = worldBoxes.constFind(part);
            if (it != worldBoxes.constEnd())
                addItem(part, it.value());
        }
        QUuid anchorItem;
        if (!anchorUuid.isNull() && items.contains(anchorUuid))
            anchorItem = anchorUuid;
        for (int child : g.children)
        {
            if (!hasBox[child])
                continue;
            const QUuid& childId = tree.groups[child].id;
            addItem(childId, groupBox[child]);
            if (holdsAnchor[child])
                anchorItem = childId;
        }
        if (items.isEmpty())
            continue;

        if (items.size() >= 2 && (maxDepth <= 0 || depth[gi] < maxDepth))
        {
            // Deeper levels explode a little less, so the top-level structure stays readable.
            const float levelScale = std::pow(kLevelDamping, float(depth[gi]));
            // MODELVIEWER_EXPLODED_PACK=1 selects the earlier block-packing layout instead of the proportional one.
            static const bool packLayout = qEnvironmentVariableIntValue("MODELVIEWER_EXPLODED_PACK") != 0;
            if (packLayout)
            {
                layoutFlat(items, anchorItem, Mode::Auto, QVector3D(), centres, boxes, autoHints, itemOffsets[gi]);
                if (levelScale < 0.999f)
                    for (auto it = itemOffsets[gi].begin(); it != itemOffsets[gi].end(); ++it)
                        it.value() *= levelScale;
            }
            else if (mode == Mode::AutoMating)
                layoutMating(items, anchorItem, centres, boxes, kScaleGain * levelScale, assemblyCentre, itemOffsets[gi]);
            else
                layoutRadialScaled(items, anchorItem, centres, boxes, kScaleGain * levelScale, itemOffsets[gi]);
        }

        const float inf = std::numeric_limits<float>::max();
        QVector3D bMin(inf, inf, inf), bMax(-inf, -inf, -inf);   // the group with its own explosion applied
        QVector3D cMin(inf, inf, inf), cMax(-inf, -inf, -inf);   // the group as it sits unexploded
        for (auto it = boxes.cbegin(); it != boxes.cend(); ++it)
        {
            const QVector3D shift = itemOffsets[gi].value(it.key());
            const QVector3D lo = it.value().first + shift, hi = it.value().second + shift;
            bMin = QVector3D(std::min(bMin.x(), lo.x()), std::min(bMin.y(), lo.y()), std::min(bMin.z(), lo.z()));
            bMax = QVector3D(std::max(bMax.x(), hi.x()), std::max(bMax.y(), hi.y()), std::max(bMax.z(), hi.z()));
            const QVector3D& clo = it.value().first;
            const QVector3D& chi = it.value().second;
            cMin = QVector3D(std::min(cMin.x(), clo.x()), std::min(cMin.y(), clo.y()), std::min(cMin.z(), clo.z()));
            cMax = QVector3D(std::max(cMax.x(), chi.x()), std::max(cMax.y(), chi.y()), std::max(cMax.z(), chi.z()));
        }
        // The parent spaces this group by its compact size plus only a share of the growth from its own explosion,
        // so one heavily exploded sub-assembly does not push every sibling far away.
        groupBox[gi] = {cMin + (bMin - cMin) * kGroupGrowthShare, cMax + (bMax - cMax) * kGroupGrowthShare};
        hasBox[gi] = 1;
    }

    // Top-down: a part moves with every block above it.
    QVector<QVector3D> accumulated(n);
    for (int gi = 0; gi < n; ++gi)
    {
        const ExplodedGroupTree::Group& g = tree.groups[gi];
        if (g.parent >= 0)
            accumulated[gi] = accumulated[g.parent] + itemOffsets[g.parent].value(g.id);
        for (const QUuid& part : g.parts)
        {
            const QVector3D offset = accumulated[gi] + itemOffsets[gi].value(part);
            if (offset.lengthSquared() > 1.0e-12f)
                _baseOffsets.insert(part, offset);
        }
    }

    applyFactor();
}
