#include "SelectionManager.h"
#include "ViewportWidget.h"
#include "Camera.h"
#include "PickingHelper.h"
#include "RenderableMesh.h"
#include <QOpenGLFunctions_4_5_Core>
#include <QOpenGLContext>
#include <QOpenGLVersionFunctionsFactory>
#include <QApplication>
#include <QLineF>
#include <QRect>
#include <QMatrix4x4>
#include <QVariant>
#include <QVector2D>
#include <QVector4D>
#include <algorithm>
#include <iostream>
#include <limits>
#include <map>
#include <vector>

SelectionManager::SelectionManager(
    ViewportWidget* viewportWidget,
    Camera* primaryCamera,
    std::vector<SceneMeshRecord>& meshStore,
    const std::vector<int>& displayedObjectsIds,
    const std::vector<int>& hiddenObjectsIds,
    bool& visibleSwapped,
    QObject* parent)
    : QObject(parent),
      _viewportWidget(viewportWidget),
      _primaryCamera(primaryCamera),
      _meshStore(meshStore),
      _displayedObjectsIds(displayedObjectsIds),
      _hiddenObjectsIds(hiddenObjectsIds),
      _visibleSwapped(visibleSwapped)
{
    // Constructor body - initialization handled in member initializer list
}

SelectionManager::~SelectionManager()
{
    cleanupFBOResources();
}

// ============================================================================
// Public Methods - Selection Operations
// ============================================================================

int SelectionManager::clickSelect(const QPoint& pixel)
{
    int id = -1;
    _selectedMeshIds.clear();  // Click select clears and selects ONE mesh

    const auto& ids = _viewportWidget->currentVisibleObjectIds();
    if (ids.empty()) {
        return -1;
    }

    QVector3D rayPos, rayDir, intersectionPoint;
    const QRect viewport = PickingHelper::viewportRectForPoint(
        pixel, _viewportWidget->width(), _viewportWidget->height(), _viewportWidget->isMultiViewActive());

    QApplication::setOverrideCursor(Qt::WaitCursor);
    convertClickToRay(pixel, viewport, _viewportWidget->getCameraForPoint(pixel), rayPos, rayDir);
    if (rayDir.isNull()) {
        QApplication::restoreOverrideCursor();
        return -1;
    }
    rayDir.normalize();

    // === Ray-based intersection test ===
    QMap<int, float> selectedIdsDist;
    for (int i : ids) {
        SceneMesh* mesh = _meshStore.at(i).mesh;
        if (!mesh)
            continue;
        if (mesh->getBoundingSphere().intersectsWithRay(rayPos, rayDir)) {
            if (mesh->intersectsWithRay(rayPos, rayDir, intersectionPoint)) {
                selectedIdsDist[i] = intersectionPoint.distanceToPoint(rayPos);
            }
        }
    }

    if (!selectedIdsDist.isEmpty()) {
        auto it = std::min_element(
            selectedIdsDist.constBegin(), selectedIdsDist.constEnd(),
            [](auto a, auto b) { return a < b; });
        id = it.key();
    }

    // === GPU color-picking ===
    // This is the authoritative path for animated meshes because it uses the
    // current render-time transforms / skinning state rather than cached CPU
    // triangle data.
    const int colId = processSelection(pixel);

    QApplication::restoreOverrideCursor();

    int selectedId = -1;
    switch (_selectionMode) {
    case SelectionMode::RayOnly:
        selectedId = id;
        break;
    case SelectionMode::ColorOnly:
        selectedId = colId;
        break;
    case SelectionMode::Hybrid:
        selectedId = (colId != -1) ? colId : id;
        break;
    }

    if (selectedId >= 0)
        _selectedMeshIds.push_back(selectedId);

    // Always emit — an empty list notifies connected panels/views that
    // nothing is selected (e.g. the user clicked empty space in the viewport).
    emit selectionChanged(_selectedMeshIds);

    return selectedId;
}

MeshSurfaceAnchor SelectionManager::pickSurfaceAnchor(const QPoint& pixel, int snapPixelRadius)
{
    MeshSurfaceAnchor anchor;

    const auto& ids = _viewportWidget->currentVisibleObjectIds();
    if (ids.empty())
        return anchor;

    const QRect viewport = PickingHelper::viewportRectForPoint(
        pixel, _viewportWidget->width(), _viewportWidget->height(), _viewportWidget->isMultiViewActive());

    Camera* camera = _viewportWidget->getCameraForPoint(pixel);

    QVector3D rayPos, rayDir;
    convertClickToRay(pixel, viewport, camera, rayPos, rayDir);
    if (rayDir.isNull())
        return anchor;
    rayDir.normalize();

    // === Closest ray-triangle hit across all visible meshes ===
    float closestDist = std::numeric_limits<float>::max();
    SceneMesh* bestMesh = nullptr;
    QVector3D bestPoint;
    int bestTriangleIndex = -1;
    QVector3D bestBary;

    for (int i : ids)
    {
        SceneMesh* mesh = _meshStore.at(i).mesh;
        if (!mesh)
            continue;
        if (!mesh->getBoundingSphere().intersectsWithRay(rayPos, rayDir))
            continue;

        QVector3D hitPoint, bary;
        int triIndex = -1;
        if (mesh->intersectsWithRayDetailed(rayPos, rayDir, hitPoint, triIndex, bary))
        {
            const float dist = hitPoint.distanceToPoint(rayPos);
            if (dist < closestDist)
            {
                closestDist       = dist;
                bestMesh          = mesh;
                bestPoint         = hitPoint;
                bestTriangleIndex = triIndex;
                bestBary          = bary;
            }
        }
    }

    if (!bestMesh || bestTriangleIndex < 0)
        return anchor;

    anchor.meshUuid      = bestMesh->uuid();
    anchor.triangleIndex = bestTriangleIndex;
    anchor.barycentric   = bestBary;
    anchor.worldPosition = bestPoint;

    // === Vertex snapping: project the hit triangle's 3 vertices to screen
    // space and snap to the nearest one if within snapPixelRadius pixels. ===
    if (camera && snapPixelRadius > 0)
    {
        const std::vector<unsigned int> meshIndices = bestMesh->indices();
        const size_t base = static_cast<size_t>(bestTriangleIndex) * 3;
        if (base + 2 < meshIndices.size())
        {
            const unsigned int triVertexIndices[3] = {
                meshIndices[base], meshIndices[base + 1], meshIndices[base + 2] };
            const std::vector<float>& trsfPoints = bestMesh->getTrsfPoints();

            const QMatrix4x4 viewMatrix = camera->getViewMatrix();
            const QMatrix4x4 projMatrix = camera->getProjectionMatrix();
            const float snapRadiusPx = static_cast<float>(snapPixelRadius);
            float bestVertDistSq = snapRadiusPx * snapRadiusPx;

            for (unsigned int vIdx : triVertexIndices)
            {
                const size_t p = static_cast<size_t>(vIdx) * 3;
                if (p + 2 >= trsfPoints.size())
                    continue;

                const QVector3D worldVert(trsfPoints[p], trsfPoints[p + 1], trsfPoints[p + 2]);
                const QVector3D screenVert = worldVert.project(viewMatrix, projMatrix, viewport);
                // QVector3D::project() returns y measured from the bottom of
                // the viewport (OpenGL convention); pixel is measured from
                // the top (Qt convention) - flip before comparing, same fix
                // already applied to mouse deltas in the transform-gizmo
                // drag code (see updateTransformGizmoTranslationDrag()).
                const float screenY = static_cast<float>(viewport.height()) - screenVert.y();
                const float dx = screenVert.x() - static_cast<float>(pixel.x());
                const float dy = screenY - static_cast<float>(pixel.y());
                const float distSq = dx * dx + dy * dy;

                if (distSq < bestVertDistSq)
                {
                    bestVertDistSq = distSq;
                    anchor.snappedVertexIndex = static_cast<int>(vIdx);
                    anchor.worldPosition = worldVert;
                }
            }
        }
    }

    return anchor;
}

MeshEdgeCircleAnchor SelectionManager::pickEdgeCircleAnchor(const QPoint& pixel, int snapPixelRadius)
{
    MeshEdgeCircleAnchor result;

    const auto& ids = _viewportWidget->currentVisibleObjectIds();
    if (ids.empty())
        return result;

    Camera* camera = _viewportWidget->getCameraForPoint(pixel);
    if (!camera)
        return result;

    const QRect viewport = PickingHelper::viewportRectForPoint(
        pixel, _viewportWidget->width(), _viewportWidget->height(), _viewportWidget->isMultiViewActive());
    const QMatrix4x4 viewMatrix = camera->getViewMatrix();
    const QMatrix4x4 projMatrix = camera->getProjectionMatrix();

    auto toScreen = [&](const QVector3D& worldPos) -> QVector2D {
        const QVector3D projected = worldPos.project(viewMatrix, projMatrix, viewport);
        // Same y-flip as pickSurfaceAnchor()'s vertex-snap projection -
        // project() is OpenGL (bottom-up), pixel is Qt (top-down).
        return QVector2D(projected.x(), static_cast<float>(viewport.height()) - projected.y());
    };
    auto distPointToSegment = [](const QVector2D& p, const QVector2D& a, const QVector2D& b) -> float {
        const QVector2D ab = b - a;
        const float abLenSq = QVector2D::dotProduct(ab, ab);
        float t = abLenSq > 1.0e-6f ? QVector2D::dotProduct(p - a, ab) / abLenSq : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        return (p - (a + ab * t)).length();
    };

    const QVector2D clickPt(static_cast<float>(pixel.x()), static_cast<float>(pixel.y()));
    float bestDist = static_cast<float>(snapPixelRadius);

    for (int i : ids)
    {
        SceneMesh* mesh = _meshStore.at(i).mesh;
        if (!mesh)
            continue;

        const std::vector<OccEdgeCircleInfo>& circles = mesh->getOccEdgeCircles();
        if (circles.empty())
            continue;
        const std::vector<float>& segments = mesh->getOccEdgeSegments();
        const std::vector<int>& bounds = mesh->getOccEdgeBoundaries();
        if (segments.empty() || bounds.size() < 2)
            continue;

        const QMatrix4x4 combined = mesh->combinedRenderTransform();
        const int numEdges = std::min(static_cast<int>(bounds.size()) - 1, static_cast<int>(circles.size()));

        for (int e = 0; e < numEdges; ++e)
        {
            if (!circles[e].isCircle)
                continue;

            const int startVec3 = bounds[e];
            const int endVec3 = bounds[e + 1];
            // Segments are flat GL_LINES pairs (see OccEdgeData's doc
            // comment) - each pair of consecutive vec3 entries is one
            // independent segment, so step by 2, not 1.
            for (int v = startVec3; v + 1 < endVec3; v += 2)
            {
                const size_t p0 = static_cast<size_t>(v) * 3;
                const size_t p1 = static_cast<size_t>(v + 1) * 3;
                if (p1 + 2 >= segments.size())
                    break;

                const QVector3D worldA = combined.map(QVector3D(segments[p0], segments[p0 + 1], segments[p0 + 2]));
                const QVector3D worldB = combined.map(QVector3D(segments[p1], segments[p1 + 1], segments[p1 + 2]));
                const float d = distPointToSegment(clickPt, toScreen(worldA), toScreen(worldB));
                if (d < bestDist)
                {
                    bestDist = d;
                    result.meshUuid = mesh->uuid();
                    result.edgeIndex = e;
                }
            }
        }
    }

    return result;
}

MeshEdgeCircleAnchor SelectionManager::pickStraightEdgeAnchor(const QPoint& pixel, int snapPixelRadius)
{
    MeshEdgeCircleAnchor result;

    const auto& ids = _viewportWidget->currentVisibleObjectIds();
    if (ids.empty())
        return result;

    Camera* camera = _viewportWidget->getCameraForPoint(pixel);
    if (!camera)
        return result;

    const QRect viewport = PickingHelper::viewportRectForPoint(
        pixel, _viewportWidget->width(), _viewportWidget->height(), _viewportWidget->isMultiViewActive());
    const QMatrix4x4 viewMatrix = camera->getViewMatrix();
    const QMatrix4x4 projMatrix = camera->getProjectionMatrix();

    auto toScreen = [&](const QVector3D& worldPos) -> QVector2D {
        const QVector3D projected = worldPos.project(viewMatrix, projMatrix, viewport);
        return QVector2D(projected.x(), static_cast<float>(viewport.height()) - projected.y());
    };
    auto distPointToSegment = [](const QVector2D& p, const QVector2D& a, const QVector2D& b) -> float {
        const QVector2D ab = b - a;
        const float abLenSq = QVector2D::dotProduct(ab, ab);
        float t = abLenSq > 1.0e-6f ? QVector2D::dotProduct(p - a, ab) / abLenSq : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        return (p - (a + ab * t)).length();
    };

    const QVector2D clickPt(static_cast<float>(pixel.x()), static_cast<float>(pixel.y()));
    float bestDist = static_cast<float>(snapPixelRadius);

    for (int i : ids)
    {
        SceneMesh* mesh = _meshStore.at(i).mesh;
        if (!mesh)
            continue;

        const std::vector<int>& occBounds = mesh->getOccEdgeBoundaries();
        if (!occBounds.empty())
        {
            // CAD mesh - every OCC edge is a candidate (not just circles,
            // unlike pickEdgeCircleAnchor()).
            const std::vector<float>& segments = mesh->getOccEdgeSegments();
            if (segments.empty() || occBounds.size() < 2)
                continue;

            const QMatrix4x4 combined = mesh->combinedRenderTransform();
            const int numEdges = static_cast<int>(occBounds.size()) - 1;
            for (int e = 0; e < numEdges; ++e)
            {
                const int startVec3 = occBounds[e];
                const int endVec3 = occBounds[e + 1];
                // Segments are flat GL_LINES pairs - step by 2, not 1 (see
                // OccEdgeData's doc comment).
                for (int v = startVec3; v + 1 < endVec3; v += 2)
                {
                    const size_t p0 = static_cast<size_t>(v) * 3;
                    const size_t p1 = static_cast<size_t>(v + 1) * 3;
                    if (p1 + 2 >= segments.size())
                        break;

                    const QVector3D worldA = combined.map(QVector3D(segments[p0], segments[p0 + 1], segments[p0 + 2]));
                    const QVector3D worldB = combined.map(QVector3D(segments[p1], segments[p1 + 1], segments[p1 + 2]));
                    const float d = distPointToSegment(clickPt, toScreen(worldA), toScreen(worldB));
                    if (d < bestDist)
                    {
                        bestDist = d;
                        result.meshUuid = mesh->uuid();
                        result.edgeIndex = e;
                    }
                }
            }
        }
        else
        {
            // Non-CAD mesh - fall back to the heuristic feature-edge list.
            // Each pair of indices IS one discrete straight edge already
            // (real mesh vertices, not tessellated floats), so this is
            // actually simpler than the OCC path above - no boundary table,
            // no local-to-world transform needed (getTrsfPoints() is
            // already world-space).
            const std::vector<uint32_t>& featureEdges = mesh->getFeatureEdgeIndices();
            if (featureEdges.empty())
                continue;
            const std::vector<float>& trsfPoints = mesh->getTrsfPoints();

            const int numEdges = static_cast<int>(featureEdges.size()) / 2;
            for (int e = 0; e < numEdges; ++e)
            {
                const size_t p0 = static_cast<size_t>(featureEdges[e * 2]) * 3;
                const size_t p1 = static_cast<size_t>(featureEdges[e * 2 + 1]) * 3;
                if (p0 + 2 >= trsfPoints.size() || p1 + 2 >= trsfPoints.size())
                    continue;

                const QVector3D worldA(trsfPoints[p0], trsfPoints[p0 + 1], trsfPoints[p0 + 2]);
                const QVector3D worldB(trsfPoints[p1], trsfPoints[p1 + 1], trsfPoints[p1 + 2]);
                const float d = distPointToSegment(clickPt, toScreen(worldA), toScreen(worldB));
                if (d < bestDist)
                {
                    bestDist = d;
                    result.meshUuid = mesh->uuid();
                    result.edgeIndex = e;
                }
            }
        }
    }

    return result;
}

MeshEdgeCircleAnchor SelectionManager::pickCircularEdgeCenterAnchor(const QPoint& pixel, int snapPixelRadius)
{
    MeshEdgeCircleAnchor result;

    const auto& ids = _viewportWidget->currentVisibleObjectIds();
    if (ids.empty())
        return result;

    Camera* camera = _viewportWidget->getCameraForPoint(pixel);
    if (!camera)
        return result;

    const QRect viewport = PickingHelper::viewportRectForPoint(
        pixel, _viewportWidget->width(), _viewportWidget->height(), _viewportWidget->isMultiViewActive());
    const QMatrix4x4 viewMatrix = camera->getViewMatrix();
    const QMatrix4x4 projMatrix = camera->getProjectionMatrix();

    auto toScreen = [&](const QVector3D& worldPos) -> QVector2D {
        const QVector3D projected = worldPos.project(viewMatrix, projMatrix, viewport);
        return QVector2D(projected.x(), static_cast<float>(viewport.height()) - projected.y());
    };

    const QVector2D clickPt(static_cast<float>(pixel.x()), static_cast<float>(pixel.y()));
    float bestDist = static_cast<float>(snapPixelRadius);

    for (int i : ids)
    {
        SceneMesh* mesh = _meshStore.at(i).mesh;
        if (!mesh)
            continue;

        const std::vector<OccEdgeCircleInfo>& circles = mesh->getOccEdgeCircles();
        if (circles.empty())
            continue;

        const QMatrix4x4 combined = mesh->combinedRenderTransform();
        for (int e = 0; e < static_cast<int>(circles.size()); ++e)
        {
            if (!circles[e].isCircle)
                continue;

            const QVector3D centerLocal(static_cast<float>(circles[e].centerX),
                                         static_cast<float>(circles[e].centerY),
                                         static_cast<float>(circles[e].centerZ));
            const QVector3D centerWorld = combined.map(centerLocal);
            const float d = (clickPt - toScreen(centerWorld)).length();
            if (d < bestDist)
            {
                bestDist = d;
                result.meshUuid = mesh->uuid();
                result.edgeIndex = e;
            }
        }
    }

    return result;
}

int SelectionManager::hoverSelect(const QPoint& pixel)
{
    int hoveredId = -1;

    const auto& ids = _viewportWidget->currentVisibleObjectIds();
    if (ids.empty())
        return -1;

    const bool animatedPoseActive = _viewportWidget
        && !_viewportWidget->activeAnimationFile().isEmpty()
        && _viewportWidget->activeAnimationClip() >= 0;

    if (_hoverHighlightMode == HoverHighlightMode::Accurate || animatedPoseActive)
    {
        hoveredId = processSelection(pixel);
    }
    else
    {
        QVector3D rayPos, rayDir, intersectionPoint;
        const QRect viewport = PickingHelper::viewportRectForPoint(
            pixel, _viewportWidget->width(), _viewportWidget->height(), _viewportWidget->isMultiViewActive());

        convertClickToRay(pixel, viewport, _viewportWidget->getCameraForPoint(pixel), rayPos, rayDir);
        if (rayDir.isNull())
            return -1;
        rayDir.normalize();

        // === Ray-based intersection test (performance-optimized) ===
        QMap<int, float> hitDistances;
        for (int i : ids) {
            SceneMesh* mesh = _meshStore.at(i).mesh;
            if (!mesh)
                continue;
            if (mesh->getBoundingSphere().intersectsWithRay(rayPos, rayDir)) {
                if (mesh->intersectsWithRay(rayPos, rayDir, intersectionPoint)) {
                    hitDistances[i] = intersectionPoint.distanceToPoint(rayPos);
                }
            }
        }

        // Return the closest hit
        if (!hitDistances.isEmpty()) {
            auto it = std::min_element(
                hitDistances.constBegin(), hitDistances.constEnd(),
                [](auto a, auto b) { return a < b; });
            hoveredId = it.key();
        }
    }

    // Update hover state and emit signal if changed
    if (hoveredId != _hoveredMeshId) {
        _hoveredMeshId = hoveredId;
        emit hoverChanged(hoveredId);
    }

    return hoveredId;
}

QList<int> SelectionManager::sweepSelect(const QPoint& p1, const QPoint& p2, bool addToSelection)
{
    const auto& ids = _viewportWidget->currentVisibleObjectIds();
    if (ids.empty())
        return _selectedMeshIds;

    const QRect rubberRect = QRect(p1, p2).normalized();
    if (rubberRect.isNull())
        return _selectedMeshIds;

    QList<int> selectedIds = addToSelection ? _selectedMeshIds : QList<int>{};

    const QRect viewport(0, 0, _viewportWidget->width(), _viewportWidget->height());
    const QMatrix4x4 projMatrix = _viewportWidget->getProjectionMatrix();
    const QMatrix4x4 viewMatrix = _viewportWidget->getModelViewMatrix();
    constexpr float SELECTION_THRESHOLD = 0.5f;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    selectedIds.reserve(selectedIds.size() + static_cast<qsizetype>(ids.size()));

    for (int i : ids)
    {
        SceneMesh* mesh = _meshStore.at(i).mesh;
        if (!mesh)
            continue;

        const BoundingSphere sphere = mesh->getBoundingSphere();
        const QVector3D center = sphere.getCenter();
        const float radius = sphere.getRadius();

        const QVector4D projectedCenter = projMatrix * viewMatrix * QVector4D(center, 1.0f);
        if (projectedCenter.w() <= 0.0f)
            continue;

        const QVector3D ndcCenter = projectedCenter.toVector3DAffine();
        const QPointF screenCenter(
            (ndcCenter.x() * 0.5f + 0.5f) * viewport.width(),
            (1.0f - (ndcCenter.y() * 0.5f + 0.5f)) * viewport.height());

        const QVector4D edge4 = projMatrix * viewMatrix * QVector4D(center + QVector3D(radius, 0, 0), 1.0f);
        if (edge4.w() <= 0.0f)
            continue;

        const QVector3D ndcEdge = edge4.toVector3DAffine();
        const QPointF screenEdge(
            (ndcEdge.x() * 0.5f + 0.5f) * viewport.width(),
            (1.0f - (ndcEdge.y() * 0.5f + 0.5f)) * viewport.height());

        const float radiusPixels = QLineF(screenCenter, screenEdge).length();
        const QRectF projectedRect(
            screenCenter.x() - radiusPixels,
            screenCenter.y() - radiusPixels,
            2 * radiusPixels,
            2 * radiusPixels);

        if (rubberRect.contains(projectedRect.toRect()))
        {
            if (!selectedIds.contains(i))
                selectedIds.push_back(i);
        }
        else if (rubberRect.intersects(projectedRect.toRect()))
        {
            const QRectF intersected = rubberRect.intersected(projectedRect.toRect());
            const float intersectArea = intersected.width() * intersected.height();
            const float projectedArea = projectedRect.width() * projectedRect.height();

            if (projectedArea > 0 && (intersectArea / projectedArea) >= SELECTION_THRESHOLD)
            {
                if (!selectedIds.contains(i))
                    selectedIds.push_back(i);
            }
        }
    }

    QApplication::restoreOverrideCursor();

    _selectedMeshIds = selectedIds;
    return _selectedMeshIds;
}

void SelectionManager::select(int id)
{
    try
    {
        if (id < 0 || id >= static_cast<int>(_meshStore.size()))
            return;

        SceneMesh* mesh = _meshStore.at(id).mesh;
        if (!mesh)
            return;

        mesh->select();
        if (!_selectedMeshIds.contains(id))
            _selectedMeshIds.append(id);
    }
    catch (const std::exception& ex)
    {
        std::cout << "Exception raised in SelectionManager::select\n" << ex.what() << std::endl;
    }
}

void SelectionManager::deselect(int id)
{
    try
    {
        if (id < 0 || id >= static_cast<int>(_meshStore.size()))
            return;

        SceneMesh* mesh = _meshStore.at(id).mesh;
        if (!mesh)
            return;

        mesh->deselect();
        _selectedMeshIds.removeAll(id);
    }
    catch (const std::exception& ex)
    {
        std::cout << "Exception raised in SelectionManager::deselect\n" << ex.what() << std::endl;
    }
}

void SelectionManager::syncMeshSelectionVisualState()
{
    for (const SceneMeshRecord& meshRecord : _meshStore)
    {
        if (meshRecord.mesh)
            meshRecord.mesh->deselect();
    }

    for (int id : _selectedMeshIds)
    {
        if (id < 0 || id >= static_cast<int>(_meshStore.size()))
            continue;

        SceneMesh* mesh = _meshStore.at(id).mesh;
        if (mesh)
            mesh->select();
    }
}

// ============================================================================
// Settings Slots
// ============================================================================

void SelectionManager::setHoverHighlightMode(HoverHighlightMode mode)
{
    if (_hoverHighlightMode != mode)
    {
        _hoverHighlightMode = mode;
        if (mode == HoverHighlightMode::Disabled && _hoveredMeshId != -1)
        {
            _hoveredMeshId = -1;
            emit hoverChanged(-1);
        }
        emit hoverModeChanged(mode);
    }
}

void SelectionManager::setSelectionMode(SelectionMode mode)
{
    if (_selectionMode != mode)
    {
        _selectionMode = mode;
        emit selectionModeChanged(mode);
    }
}

// ============================================================================
// FBO Management
// ============================================================================

void SelectionManager::initializeFBOResources()
{
    // FBO resources are created on-demand in processSelection
    // This is called during initialization if needed in future
}

void SelectionManager::cleanupFBOResources()
{
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (context)
    {
        if (auto* f = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_5_Core>(context))
        {
            if (_selectionFBO != 0)
                f->glDeleteFramebuffers(1, &_selectionFBO);
            if (_selectionRBO != 0)
                f->glDeleteRenderbuffers(1, &_selectionRBO);
            if (_selectionDBO != 0)
                f->glDeleteRenderbuffers(1, &_selectionDBO);
        }
    }

    _selectionFBO = 0;
    _selectionRBO = 0;
    _selectionDBO = 0;
}

void SelectionManager::resizeFBOResources(int width, int height)
{
    cleanupFBOResources();
    _fboWidth = width;
    _fboHeight = height;
}

// ============================================================================
// Helper Methods - Ray Conversion
// ============================================================================

void SelectionManager::getRayFromPixelCoords(const QPoint& pixel, QVector3D& rayPos, QVector3D& rayDir)
{
    // This is a placeholder - actual implementation uses convertClickToRay
    // Kept for API consistency
}

void SelectionManager::convertClickToRay(const QPoint& pixel, const QRect& viewport,
                                        Camera* camera, QVector3D& orig, QVector3D& dir)
{
    if (viewport.width() <= 0 || viewport.height() <= 0) {
        orig = QVector3D(0, 0, 0);
        dir  = QVector3D(0, 0, 0);
        return;
    }

    int yInverted = _viewportWidget->height() - pixel.y() - 1;

    QMatrix4x4 view = camera->getViewMatrix();
    QMatrix4x4 projection = camera->getProjectionMatrix();

    // Convert to Normalized Device Coordinates [-1, 1]
    float ndcX = (2.0f * (pixel.x() - viewport.x())) / viewport.width() - 1.0f;
    float ndcY = (2.0f * (yInverted - viewport.y())) / viewport.height() - 1.0f;

    QVector4D nearNDC(ndcX, ndcY, -1.0f, 1.0f); // Near plane
    QVector4D farNDC(ndcX, ndcY, 1.0f, 1.0f);   // Far plane

    QMatrix4x4 inv = (projection * view).inverted();

    QVector4D nearWorld = inv * nearNDC;
    QVector4D farWorld = inv * farNDC;

    // Homogeneous divide
    nearWorld /= nearWorld.w();
    farWorld /= farWorld.w();

    orig = nearWorld.toVector3D();
    QVector3D rawDir = farWorld.toVector3D() - orig;
    dir = rawDir.isNull() ? QVector3D(0, 0, 0) : rawDir.normalized();
}

// ============================================================================
// Helper Methods - Color Picking
// ============================================================================

int SelectionManager::processSelection(const QPoint& pixel)
{
    if (!_viewportWidget)
        return -1;

    const auto& visibleIds = _viewportWidget->currentVisibleObjectIds();
    if (visibleIds.empty())
        return -1;

    _viewportWidget->makeCurrent();

    const int widgetWidth = _viewportWidget->width();
    const int widgetHeight = _viewportWidget->height();
    if (widgetWidth <= 0 || widgetHeight <= 0)
        return -1;

    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context)
        return -1;

    auto* f = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_5_Core>(context);
    if (!f)
        return -1;

    int id = -1;

    if (_selectionFBO == 0)
        f->glGenFramebuffers(1, &_selectionFBO);
    f->glBindFramebuffer(GL_FRAMEBUFFER, _selectionFBO);
#ifdef GL_FRAMEBUFFER_DEFAULT_SAMPLES
    f->glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_SAMPLES, 0);
#else
    f->glFramebufferParameteri(GL_FRAMEBUFFER, 0, 0);
#endif

    if (_selectionRBO == 0)
        f->glGenRenderbuffers(1, &_selectionRBO);
    f->glBindRenderbuffer(GL_RENDERBUFFER, _selectionRBO);
    f->glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA, widgetWidth, widgetHeight);
    f->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _selectionRBO);
    GLenum drawBuffers[1] = { GL_COLOR_ATTACHMENT0 };
    f->glDrawBuffers(1, drawBuffers);

    if (_selectionDBO == 0)
        f->glGenRenderbuffers(1, &_selectionDBO);
    f->glBindRenderbuffer(GL_RENDERBUFFER, _selectionDBO);
    f->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, widgetWidth, widgetHeight);
    f->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, _selectionDBO);

    const GLenum status = f->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        std::cout << "Failed to create selection framebuffer: " << status << std::endl;
        f->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _viewportWidget->defaultFramebufferObject());
        return -1;
    }

    GLint viewport[4];
    f->glGetIntegerv(GL_VIEWPORT, viewport);

    Camera* selCamera = _viewportWidget->getCameraForPoint(pixel);
    int selVpX = 0, selVpY = 0, selVpW = widgetWidth, selVpH = widgetHeight;
    if (_viewportWidget->isMultiViewActive())
    {
        const int hw = widgetWidth / 2;
        const int hh = widgetHeight / 2;
        if (pixel.x() < widgetWidth / 2 && pixel.y() > widgetHeight / 2)
            { selVpX = 0;  selVpY = 0;  selVpW = hw; selVpH = hh; }
        else if (pixel.x() < widgetWidth / 2 && pixel.y() <= widgetHeight / 2)
            { selVpX = 0;  selVpY = hh; selVpW = hw; selVpH = hh; }
        else if (pixel.x() >= widgetWidth / 2 && pixel.y() < widgetHeight / 2)
            { selVpX = hw; selVpY = hh; selVpW = hw; selVpH = hh; }
        else
            { selVpX = hw; selVpY = 0;  selVpW = hw; selVpH = hh; }
    }

    f->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    f->glViewport(0, 0, widgetWidth, widgetHeight);
    f->glBindFramebuffer(GL_FRAMEBUFFER, _selectionFBO);
    f->glDrawBuffer(GL_COLOR_ATTACHMENT0);
    f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    f->glViewport(selVpX, selVpY, selVpW, selVpH);
    f->glEnable(GL_DEPTH_TEST);
    f->glDisable(GL_BLEND);

    ShaderProgram* selectionShader = _viewportWidget->getSelectionShader();
    selectionShader->bind();
    selectionShader->setUniformValue("projectionMatrix", selCamera->getProjectionMatrix());
    selectionShader->setProperty("globalModelMatrix", QVariant::fromValue(_viewportWidget->getModelMatrix()));
    selectionShader->setUniformValue("viewMatrix", selCamera->getViewMatrix());

    for (int i : visibleIds)
    {
        try
        {
            SceneMesh* mesh = _meshStore.at(i).mesh;
            if (mesh && _viewportWidget->isMeshAnimationVisibleForSelection(mesh))
            {
                const QColor pickColor = PickingHelper::indexToColor(i + 1);
                selectionShader->bind();
                selectionShader->setUniformValue("pickingColor", QVector4D(
                    pickColor.redF(), pickColor.greenF(), pickColor.blueF(), pickColor.alphaF()));
                selectionShader->setUniformValue("modelMatrix", mesh->combinedRenderTransform());
                selectionShader->setUniformValue("hasSkinning", mesh->hasSkinning());
                selectionShader->setUniformValue("jointCount", static_cast<int>(mesh->jointPalette().size()));
                if (mesh->hasSkinning() && !mesh->jointPalette().isEmpty())
                {
                    const int maxJoints = std::min(static_cast<int>(mesh->jointPalette().size()), 128);
                    for (int jointIndex = 0; jointIndex < maxJoints; ++jointIndex)
                    {
                        const QString uniformName = QStringLiteral("jointMatrices[%1]").arg(jointIndex);
                        selectionShader->setUniformValue(uniformName.toUtf8().constData(), mesh->jointPalette()[jointIndex]);
                    }
                }
                mesh->setProg(selectionShader);
                mesh->getVAO().bind();
                if (mesh->getIndices().empty())
                    f->glDrawArrays(mesh->getPrimitiveMode(), 0, static_cast<int>(mesh->getPoints().size() / 3));
                else
                    f->glDrawElements(mesh->getPrimitiveMode(), static_cast<int>(mesh->getIndices().size()), GL_UNSIGNED_INT, nullptr);
                mesh->getVAO().release();
                f->glFlush();
                f->glFinish();
            }
        }
        catch (const std::exception& ex)
        {
            std::cout << "Exception raised in SelectionManager::processSelection\n" << ex.what() << std::endl;
        }
    }

    f->glReadBuffer(GL_COLOR_ATTACHMENT0);
    f->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const int pixelWinSize = 2;
    int readX = pixel.x() - pixelWinSize / 2;
    int readY = widgetHeight - pixel.y() - 1 + pixelWinSize / 2;
    if (readX < 0) readX = 0;
    if (readY < 0) readY = 0;
    if (readX + pixelWinSize > widgetWidth)  readX = widgetWidth - pixelWinSize;
    if (readY + pixelWinSize > widgetHeight) readY = widgetHeight - pixelWinSize;

    int readWidth = pixelWinSize;
    int readHeight = pixelWinSize;
    if (readX + readWidth > widgetWidth)   readWidth  = widgetWidth  - readX;
    if (readY + readHeight > widgetHeight) readHeight = widgetHeight - readY;

    if (readWidth <= 0 || readHeight <= 0)
    {
        f->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _viewportWidget->defaultFramebufferObject());
        f->glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        return -1;
    }

    std::vector<float> res(static_cast<size_t>(readWidth) * static_cast<size_t>(readHeight) * 4u);
    f->glReadPixels(readX, readY, readWidth, readHeight, GL_RGBA, GL_FLOAT, res.data());
    std::map<int, int> voteCount;
    for (size_t i = 0; i < res.size(); i += 4)
    {
        const QColor col = QColor::fromRgbF(res[i + 0], res[i + 1], res[i + 2], res[i + 3]);
        const unsigned int colId = PickingHelper::colorToIndex(col);
        if (colId != 0)
            voteCount[static_cast<int>(colId - 1)]++;
    }
    if (!voteCount.empty())
        id = std::max_element(voteCount.begin(), voteCount.end(), voteCount.value_comp())->first;

    f->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _viewportWidget->defaultFramebufferObject());
    f->glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    return id;
}
