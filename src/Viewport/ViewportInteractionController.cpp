#include "ViewportInteractionController.h"

#include <QMatrix4x4>

CornerAxisPosition ViewportInteractionController::normalizeCornerAxisPosition(
    CornerAxisPosition position)
{
    switch (position)
    {
    case CornerAxisPosition::TOP_LEFT:
    case CornerAxisPosition::TOP_RIGHT:
    case CornerAxisPosition::BOTTOM_LEFT:
    case CornerAxisPosition::BOTTOM_RIGHT:
        return position;
    default:
        return CornerAxisPosition::TOP_RIGHT;
    }
}

bool ViewportInteractionController::convertPixelToRay(
    const QPoint& pixel, const QRect& viewport, int widgetHeight,
    const QMatrix4x4& view, const QMatrix4x4& projection,
    QVector3D& orig, QVector3D& dir)
{
    if (viewport.width() <= 0 || viewport.height() <= 0)
    {
        orig = QVector3D(0, 0, 0);
        dir  = QVector3D(0, 0, 0);
        return false;
    }

    const int   yInverted = widgetHeight - pixel.y() - 1;
    const float ndcX = (2.0f * (pixel.x() - viewport.x())) / viewport.width()  - 1.0f;
    const float ndcY = (2.0f * (yInverted  - viewport.y())) / viewport.height() - 1.0f;

    const QVector4D nearNdc(ndcX, ndcY, -1.0f, 1.0f);
    const QVector4D farNdc (ndcX, ndcY,  1.0f, 1.0f);
    const QMatrix4x4 inv = (projection * view).inverted();

    QVector4D nearWorld = inv * nearNdc;
    QVector4D farWorld  = inv * farNdc;
    if (qFuzzyIsNull(nearWorld.w()) || qFuzzyIsNull(farWorld.w()))
    {
        orig = QVector3D(0, 0, 0);
        dir  = QVector3D(0, 0, 0);
        return false;
    }

    nearWorld /= nearWorld.w();
    farWorld  /= farWorld.w();
    orig = nearWorld.toVector3D();
    const QVector3D rawDir = farWorld.toVector3D() - orig;
    dir = rawDir.isNull() ? QVector3D(0, 0, 0) : rawDir.normalized();
    return !dir.isNull();
}

bool ViewportInteractionController::intersectRayPlane(
    const QVector3D& rayOrigin, const QVector3D& rayDir,
    const QVector3D& planePoint, const QVector3D& planeNormal,
    QVector3D& outPoint)
{
    const float denom = QVector3D::dotProduct(rayDir, planeNormal);
    if (std::abs(denom) <= 1.0e-6f)
        return false;

    const float t = QVector3D::dotProduct(planePoint - rayOrigin, planeNormal) / denom;
    if (t < 0.0f)
        return false;

    outPoint = rayOrigin + rayDir * t;
    return true;
}

QVector3D ViewportInteractionController::rotatePointAroundAxis(
    const QVector3D& point, const QVector3D& pivot,
    const QVector3D& axis, float angleDegrees)
{
    QMatrix4x4 rot;
    rot.setToIdentity();
    rot.translate(pivot);
    rot.rotate(angleDegrees, axis);
    rot.translate(-pivot);
    return rot.map(point);
}

void ViewportInteractionController::setShowViewCubeOverride(bool show)
{
    _showViewCubeOverride = show;
    if (!show)
        _viewCubeHoveredRegionId = -1;
}

void ViewportInteractionController::setCornerAxisPosition(CornerAxisPosition pos)
{
    _cornerAxisPosition = normalizeCornerAxisPosition(pos);
}

void ViewportInteractionController::setModelMatrix(const QMatrix4x4& matrix)
{
    _modelMatrix = matrix;
    recomputeModelViewMatrix();
}

void ViewportInteractionController::setViewMatrix(const QMatrix4x4& matrix)
{
    _viewMatrix = matrix;
    recomputeModelViewMatrix();
}

void ViewportInteractionController::resetSelectionBoundingSphere()
{
    _selectionBoundingSphere.setCenter(0, 0, 0);
    _selectionBoundingSphere.setRadius(0.0f);
}

float ViewportInteractionController::advanceSlerpStep()
{
    _slerpStep += _slerpFrac;
    return _slerpStep;
}

void ViewportInteractionController::setNavigationModes(bool rotating, bool panning, bool zooming)
{
    _viewRotating = rotating;
    _viewPanning  = panning;
    _viewZooming  = zooming;
}

void ViewportInteractionController::clearNavigationModes()
{
    setNavigationModes(false, false, false);
}

void ViewportInteractionController::setNavigationLock(const QRect& viewport, const QRect& clientRect)
{
    _navigationViewportLocked     = true;
    _navigationLockedViewport     = viewport;
    _navigationLockedClientRect   = clientRect;
}

void ViewportInteractionController::clearNavigationLock()
{
    _navigationViewportLocked     = false;
    _navigationLockedViewport     = QRect();
    _navigationLockedClientRect   = QRect();
}

void ViewportInteractionController::clearInertiaState()
{
    _inertiaPanVelocity      = QVector2D();
    _inertiaZoomVelocity     = 0.0f;
    _inertiaRotateVelocity   = QVector2D();
    _inertiaZoomPanVelocity  = QVector3D();
}

void ViewportInteractionController::setTransformGizmoMode(
    bool translating, bool scaling, bool uniformScaling, bool rotating)
{
    _transformGizmoTranslating    = translating;
    _transformGizmoScaling        = scaling;
    _transformGizmoUniformScaling = uniformScaling;
    _transformGizmoRotating       = rotating;
}

void ViewportInteractionController::resetTransformGizmoDragSession()
{
    _transformGizmoStartStates.clear();
    _transformGizmoStartCenters.clear();
    _transformGizmoStartMatrices.clear();
    _transformGizmoCurrentTranslationDelta = QVector3D(0.0f, 0.0f, 0.0f);
    _transformGizmoCurrentScaleDelta       = QVector3D(1.0f, 1.0f, 1.0f);
    _transformGizmoCurrentRotationDelta    = QVector3D(0.0f, 0.0f, 0.0f);
    _transformGizmoLoggedTranslationUpdate = false;
}

void ViewportInteractionController::setViewportMatrix(float width, float height)
{
    _viewportMatrix = QMatrix4x4(width  / 2.0f, 0.0f,          0.0f, 0.0f,
                                 0.0f,          height / 2.0f, 0.0f, 0.0f,
                                 0.0f,          0.0f,          1.0f, 0.0f,
                                 width  / 2.0f, height / 2.0f, 0.0f, 1.0f);
}

void ViewportInteractionController::syncMatricesFromCamera(const Camera& camera)
{
    _viewMatrix       = camera.getViewMatrix();
    _projectionMatrix = camera.getProjectionMatrix();
    recomputeModelViewMatrix();
}

void ViewportInteractionController::syncPoseFromCamera(const Camera& camera)
{
    _currentRotation    = QQuaternion::fromRotationMatrix(
        camera.getViewMatrix().toGenericMatrix<3, 3>());
    _currentTranslation = camera.getPosition();
}

void ViewportInteractionController::syncRotationFromCamera(const Camera& camera)
{
    _currentRotation = QQuaternion::fromRotationMatrix(
        camera.getViewMatrix().toGenericMatrix<3, 3>());
}

void ViewportInteractionController::syncPoseAndRangeFromCamera(const Camera& camera)
{
    syncPoseFromCamera(camera);
    syncCurrentViewRange();
}

void ViewportInteractionController::updateFrustumPlanes()
{
    const QMatrix4x4 vp = _projectionMatrix * _viewMatrix;
    const QVector4D  r0 = vp.row(0);
    const QVector4D  r1 = vp.row(1);
    const QVector4D  r2 = vp.row(2);
    const QVector4D  r3 = vp.row(3);

    _frustumPlanes[0] = r3 + r0;
    _frustumPlanes[1] = r3 - r0;
    _frustumPlanes[2] = r3 + r1;
    _frustumPlanes[3] = r3 - r1;
    _frustumPlanes[4] = r3 + r2;
    _frustumPlanes[5] = r3 - r2;

    for (int i = 0; i < 6; ++i)
    {
        const float len = QVector3D(_frustumPlanes[i].x(),
                                    _frustumPlanes[i].y(),
                                    _frustumPlanes[i].z()).length();
        if (len > 1e-6f)
            _frustumPlanes[i] /= len;
    }
}

void ViewportInteractionController::saveSystemCameraState(const Camera& camera)
{
    _savedCameraPos       = camera.getPosition();
    _savedCameraDir       = camera.getViewDir();
    _savedCameraUp        = camera.getUpVector();
    _savedCameraRight     = camera.getRightVector();
    _savedProjectionType  = camera.getProjectionType();
    _savedCameraFOV       = camera.getFOV();
    _savedCameraViewRange = camera.getViewRange();
    _systemCameraStateSaved = true;
}

void ViewportInteractionController::restoreSystemCameraState(Camera& camera)
{
    camera.setView(_savedCameraPos, _savedCameraDir, _savedCameraUp, _savedCameraRight);
    camera.setProjectionType(_savedProjectionType);
    camera.setFOV(_savedCameraFOV);
    camera.setViewRange(_savedCameraViewRange);
}
