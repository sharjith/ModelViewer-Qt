#include "ExplodedViewManager.h"

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
        const float baseGap  = std::max(avgSize * 0.5f, assemblyRadius * 0.05f);

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
                _baseOffsets.insert(parts[i].uuid, axisDir * disp);
                prevMax = targetMin + parts[i].size();
            }

            // Parts below anchor (lower projection → move in -axisDir).
            float prevMin = anchorProjMin;
            for (int i = refIdx - 1; i >= 0; --i)
            {
                const float targetMax = prevMin - baseGap;
                const float disp      = targetMax - parts[i].projMax;
                _baseOffsets.insert(parts[i].uuid, axisDir * disp);
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
                    _baseOffsets.insert(parts[i].uuid, axisDir * disp);
                cursor += parts[i].size() + baseGap;
            }
        }

        applyFactor();
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
                    _baseOffsets.insert(part.uuid, dir * disp);
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

        float totalSize = 0.0f;
        for (const auto& p : parts)
            totalSize += p.size();
        const float avgSize = totalSize / static_cast<float>(parts.size());

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

        const float baseGap = std::max(avgSize * 0.35f, assemblyRadius * 0.05f);

        float cursor = baseGap;
        if (!anchorUuid.isNull() && worldBoxes.contains(anchorUuid))
        {
            auto [anchorMin, anchorMax] = aabbAxisExtent(worldBoxes.value(anchorUuid), dir);
            Q_UNUSED(anchorMin);
            cursor = std::max(cursor, (anchorMax - originProj) + baseGap);
        }

        for (const auto& part : parts)
        {
            const float relMin = part.projMin - originProj;
            const float targetMin = std::max(cursor, relMin);
            const float disp = targetMin - relMin;

            if (disp > 1e-6f)
                _baseOffsets.insert(part.uuid, dir * disp);

            cursor = targetMin + part.size() + baseGap;
        }
    }

    applyFactor();
}

void ExplodedViewManager::applyFactor()
{
    _scaledOffsets.clear();
    for (auto it = _baseOffsets.cbegin(); it != _baseOffsets.cend(); ++it)
        _scaledOffsets.insert(it.key(), it.value() * _factor);
}
