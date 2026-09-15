#pragma once

#include "BoundingBox.h"
#include "BoundingSphere.h"
#include "Camera.h"
#include "RenderEnums.h"
#include "TransformCommand.h"

#include <QMap>
#include <QMatrix4x4>
#include <QPoint>
#include <QQuaternion>
#include <QRect>
#include <QSet>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

// ---------------------------------------------------------------------------
// ViewportInteractionController
//
// Groups all viewport navigation, camera, gizmo drag, and rubber-band
// interaction state that was previously scattered through ViewportWidget's private
// section. A small public API now owns externally visible viewport settings
// and matrix access, while the bulk interaction state remains an internal
// aggregate for ViewportWidget during the incremental de-aliasing refactor.
//
// Introduced in Phase 11 of the mesh/render/runtime separation refactor.
// De-aliased in the controller ownership cleanup pass.
//
// Qt-owned pointers (_primaryCamera, _orthoViewsCamera, navigation timers,
// _rubberBand, _selectRect, _inertiaTimer) are NOT stored here — they remain
// as direct ViewportWidget members because Qt object-tree ownership requires the
// parent to be a QObject.
// ---------------------------------------------------------------------------

class ViewportInteractionController
{
public:
    // ---- Public-facing config / state accessors ----------------------------
    bool cameraUpAxisZUp() const                         { return _cameraUpAxisZUp; }
    void setCameraUpAxisZUp(bool zUp)                    { _cameraUpAxisZUp = zUp; }

    bool multiViewActive() const                         { return _multiViewActive; }
    void setMultiViewActive(bool active)                 { _multiViewActive = active; }

    bool userShowAxisOverride() const                    { return _userShowAxisOverride; }
    void setUserShowAxisOverride(bool show)              { _userShowAxisOverride = show; }

    bool userShowCornerAxisOverride() const              { return _userShowCornerAxisOverride; }
    void setUserShowCornerAxisOverride(bool show)        { _userShowCornerAxisOverride = show; }

    bool showViewCubeOverride() const                    { return _showViewCubeOverride; }
    void setShowViewCubeOverride(bool show);

    bool showAxis() const                                { return _showAxis; }
    void setShowAxis(bool show)                          { _showAxis = show; }

    float FOV() const                                    { return _FOV; }
    void setFOV(float fov)                               { _FOV = fov; }

    float viewRange() const                              { return _viewRange; }
    void setViewRange(float range)                       { _viewRange = range; }

    float currentViewRange() const                       { return _currentViewRange; }
    void setCurrentViewRange(float range)                { _currentViewRange = range; }
    float scaleFrac() const                              { return _scaleFrac; }
    void setScaleFrac(float frac)                        { _scaleFrac = frac; }

    float viewBoundingSphereDia() const                  { return _viewBoundingSphereDia; }
    void setViewBoundingSphereDia(float diameter)        { _viewBoundingSphereDia = diameter; }

    bool autoFitViewOnUpdate() const                     { return _autoFitViewOnUpdate; }
    void setAutoFitViewOnUpdate(bool update)             { _autoFitViewOnUpdate = update; }

    float zoomInLimit() const                            { return _zoomInLimit; }
    void setZoomInLimit(float limit)                     { _zoomInLimit = limit; }

    ViewProjection projection() const                    { return _projection; }
    void setProjection(ViewProjection projection)        { _projection = projection; }

    Camera::ProjectionType previousProjection() const  { return _previousProjection; }
    void setPreviousProjection(Camera::ProjectionType projection)
    {
        _previousProjection = projection;
    }

    ViewMode viewMode() const                            { return _viewMode; }
    void setViewMode(ViewMode mode)                      { _viewMode = mode; }

    const QVector3D& currentTranslation() const          { return _currentTranslation; }
    void setCurrentTranslation(const QVector3D& translation)
    {
        _currentTranslation = translation;
    }

    const QQuaternion& currentRotation() const           { return _currentRotation; }
    void setCurrentRotation(const QQuaternion& rotation) { _currentRotation = rotation; }

    const QQuaternion& customViewTargetRotation() const  { return _customViewTargetRotation; }
    void setCustomViewTargetRotation(const QQuaternion& rotation)
    {
        _customViewTargetRotation = rotation;
    }

    bool customViewAnimationActive() const               { return _customViewAnimationActive; }
    void setCustomViewAnimationActive(bool active)       { _customViewAnimationActive = active; }

    CornerAxisPosition cornerAxisPosition() const        { return _cornerAxisPosition; }
    void setCornerAxisPosition(CornerAxisPosition pos);

    const QMatrix4x4& modelMatrix() const                { return _modelMatrix; }
    void setModelMatrix(const QMatrix4x4& matrix);

    const QMatrix4x4& viewMatrix() const                 { return _viewMatrix; }
    void setViewMatrix(const QMatrix4x4& matrix);

    const QMatrix4x4& projectionMatrix() const           { return _projectionMatrix; }
    void setProjectionMatrix(const QMatrix4x4& matrix)   { _projectionMatrix = matrix; }

    const QMatrix4x4& modelViewMatrix() const            { return _modelViewMatrix; }
    const QMatrix4x4& viewportMatrix() const             { return _viewportMatrix; }

    const BoundingSphere& boundingSphere() const         { return _boundingSphere; }
    void setBoundingSphereCenter(const QVector3D& center){ _boundingSphere.setCenter(center); }
    void setBoundingSphereCenter(float x, float y, float z)
    {
        _boundingSphere.setCenter(x, y, z);
    }
    void setBoundingSphereRadius(float radius)           { _boundingSphere.setRadius(radius); }

    const BoundingSphere& selectionBoundingSphere() const { return _selectionBoundingSphere; }
    void resetSelectionBoundingSphere();
    void setSelectionBoundingSphere(const BoundingSphere& sphere)
    {
        _selectionBoundingSphere = sphere;
    }
    void addSelectionBoundingSphere(const BoundingSphere& sphere)
    {
        _selectionBoundingSphere.addSphere(sphere);
    }

    const BoundingBox& boundingBox() const               { return _boundingBox; }
    void setBoundingBox(const BoundingBox& box)          { _boundingBox = box; }
    void expandBoundingBox(const BoundingBox& box)       { _boundingBox.addBox(box); }
    void setBoundingBoxLimits(double xmin, double xmax, double ymin,
                              double ymax, double zmin, double zmax)
    {
        _boundingBox.setLimits(xmin, xmax, ymin, ymax, zmin, zmax);
    }

    float visibleHighestZ() const                        { return _visibleHighestZ; }
    void setVisibleHighestZ(float z)                     { _visibleHighestZ = z; }
    float visibleLowestZ() const                         { return _visibleLowestZ; }
    void setVisibleLowestZ(float z)                      { _visibleLowestZ = z; }

    float slerpStep() const                              { return _slerpStep; }
    void setSlerpStep(float step)                        { _slerpStep = step; }
    void resetSlerpStep()                                { _slerpStep = 0.0f; }
    float slerpFrac() const                              { return _slerpFrac; }
    void setSlerpFrac(float frac)                        { _slerpFrac = frac; }
    float advanceSlerpStep();

    bool windowZoomActive() const                        { return _windowZoomActive; }
    void setWindowZoomActive(bool active)                { _windowZoomActive = active; }
    bool viewZooming() const                             { return _viewZooming; }
    void setViewZooming(bool active)                     { _viewZooming = active; }
    bool viewPanning() const                             { return _viewPanning; }
    void setViewPanning(bool active)                     { _viewPanning = active; }
    bool viewRotating() const                            { return _viewRotating; }
    void setViewRotating(bool active)                    { _viewRotating = active; }
    void setNavigationModes(bool rotating, bool panning, bool zooming);
    void clearNavigationModes();

    const QVector3D& rubberBandPan() const               { return _rubberBandPan; }
    void setRubberBandPan(const QVector3D& pan)          { _rubberBandPan = pan; }
    float rubberBandZoomRatio() const                    { return _rubberBandZoomRatio; }
    void setRubberBandZoomRatio(float ratio)             { _rubberBandZoomRatio = ratio; }

    const QPoint& leftButtonPoint() const                { return _leftButtonPoint; }
    void setLeftButtonPoint(const QPoint& point)         { _leftButtonPoint = point; }
    const QPoint& rightButtonPoint() const               { return _rightButtonPoint; }
    void setRightButtonPoint(const QPoint& point)        { _rightButtonPoint = point; }
    const QPoint& middleButtonPoint() const              { return _middleButtonPoint; }
    void setMiddleButtonPoint(const QPoint& point)       { _middleButtonPoint = point; }

    bool navigationViewportLocked() const                { return _navigationViewportLocked; }
    const QRect& navigationLockedViewport() const        { return _navigationLockedViewport; }
    const QRect& navigationLockedClientRect() const      { return _navigationLockedClientRect; }
    void setNavigationLock(const QRect& viewport, const QRect& clientRect);
    void clearNavigationLock();

    const QPoint& lastPanPoint() const                   { return _lastPanPoint; }
    void setLastPanPoint(const QPoint& point)            { _lastPanPoint = point; }
    int lastZoomDirection() const                        { return _lastZoomDirection; }
    void setLastZoomDirection(int direction)             { _lastZoomDirection = direction; }

    bool mouseMovedSincePress() const                    { return _mouseMovedSincePress; }
    void setMouseMovedSincePress(bool moved)             { _mouseMovedSincePress = moved; }
    qint64 lastMouseMoveTime() const                     { return _lastMouseMoveTime; }
    void setLastMouseMoveTime(qint64 time)               { _lastMouseMoveTime = time; }
    const QPoint& lastMousePos() const                   { return _lastMousePos; }
    void setLastMousePos(const QPoint& point)            { _lastMousePos = point; }
    qint64 lastMouseTime() const                         { return _lastMouseTime; }
    void setLastMouseTime(qint64 time)                   { _lastMouseTime = time; }

    bool shiftDragActive() const                         { return _shiftDragActive; }
    void setShiftDragActive(bool active)                 { _shiftDragActive = active; }

    // Same press-time latch idiom as shiftDragActive() above, for the
    // Alt+Shift Subtract-mode combo (see ViewportWidget::mouseReleaseEvent's
    // sweep/lasso branches) - covers the case where the user releases Alt or
    // Shift before releasing the mouse button.
    bool altShiftDragActive() const                      { return _altShiftDragActive; }
    void setAltShiftDragActive(bool active)               { _altShiftDragActive = active; }

    const QPoint& sweepStartPoint() const                { return _sweepStartPoint; }
    void setSweepStartPoint(const QPoint& point)         { _sweepStartPoint = point; }

    const QVector3D& inertiaZoomPanVelocity() const      { return _inertiaZoomPanVelocity; }
    void setInertiaZoomPanVelocity(const QVector3D& velocity)
    {
        _inertiaZoomPanVelocity = velocity;
    }
    void addInertiaZoomPanVelocity(const QVector3D& velocity)
    {
        _inertiaZoomPanVelocity += velocity;
    }
    void scaleInertiaZoomPanVelocity(float scale)        { _inertiaZoomPanVelocity *= scale; }

    const QVector2D& inertiaPanVelocity() const          { return _inertiaPanVelocity; }
    void setInertiaPanVelocity(const QVector2D& velocity){ _inertiaPanVelocity = velocity; }
    void scaleInertiaPanVelocity(float scale)            { _inertiaPanVelocity *= scale; }

    float inertiaZoomVelocity() const                    { return _inertiaZoomVelocity; }
    void setInertiaZoomVelocity(float velocity)          { _inertiaZoomVelocity = velocity; }
    void scaleInertiaZoomVelocity(float scale)           { _inertiaZoomVelocity *= scale; }

    const QVector2D& inertiaRotateVelocity() const       { return _inertiaRotateVelocity; }
    void setInertiaRotateVelocity(const QVector2D& velocity)
    {
        _inertiaRotateVelocity = velocity;
    }
    void scaleInertiaRotateVelocity(float scale)         { _inertiaRotateVelocity *= scale; }

    float inertiaDamping() const                         { return _inertiaDamping; }
    void clearInertiaState();

    const QVector3D& lastZoomPanVector() const           { return _lastZoomPanVector; }
    void setLastZoomPanVector(const QVector3D& vector)   { _lastZoomPanVector = vector; }

    float rubberBandRadius() const                       { return _rubberBandRadius; }
    void setRubberBandRadius(float radius)               { _rubberBandRadius = radius; }

    bool transformGizmoRequested() const                 { return _transformGizmoRequested; }
    void setTransformGizmoRequested(bool requested)      { _transformGizmoRequested = requested; }
    bool transformGizmoTranslating() const               { return _transformGizmoTranslating; }
    void setTransformGizmoTranslating(bool active)       { _transformGizmoTranslating = active; }
    bool transformGizmoScaling() const                   { return _transformGizmoScaling; }
    void setTransformGizmoScaling(bool active)           { _transformGizmoScaling = active; }
    bool transformGizmoUniformScaling() const            { return _transformGizmoUniformScaling; }
    void setTransformGizmoUniformScaling(bool active)    { _transformGizmoUniformScaling = active; }
    bool transformGizmoRotating() const                  { return _transformGizmoRotating; }
    void setTransformGizmoRotating(bool active)          { _transformGizmoRotating = active; }
    void setTransformGizmoMode(bool translating, bool scaling, bool uniformScaling, bool rotating);

    const QPoint& transformGizmoDragStartPixel() const   { return _transformGizmoDragStartPixel; }
    void setTransformGizmoDragStartPixel(const QPoint& pixel)
    {
        _transformGizmoDragStartPixel = pixel;
    }
    const QVector3D& transformGizmoDragAxis() const      { return _transformGizmoDragAxis; }
    void setTransformGizmoDragAxis(const QVector3D& axis){ _transformGizmoDragAxis = axis; }
    const QVector3D& transformGizmoStartPivot() const    { return _transformGizmoStartPivot; }
    void setTransformGizmoStartPivot(const QVector3D& pivot)
    {
        _transformGizmoStartPivot = pivot;
    }
    float transformGizmoDragScale() const                { return _transformGizmoDragScale; }
    void setTransformGizmoDragScale(float scale)         { _transformGizmoDragScale = scale; }

    QMap<int, TransformState>& transformGizmoStartStates() { return _transformGizmoStartStates; }
    const QMap<int, TransformState>& transformGizmoStartStates() const
    {
        return _transformGizmoStartStates;
    }
    QMap<int, QVector3D>& transformGizmoStartCenters()   { return _transformGizmoStartCenters; }
    const QMap<int, QVector3D>& transformGizmoStartCenters() const
    {
        return _transformGizmoStartCenters;
    }
    QMap<int, QMatrix4x4>& transformGizmoStartMatrices() { return _transformGizmoStartMatrices; }
    const QMap<int, QMatrix4x4>& transformGizmoStartMatrices() const
    {
        return _transformGizmoStartMatrices;
    }

    const QVector3D& transformGizmoCurrentTranslationDelta() const
    {
        return _transformGizmoCurrentTranslationDelta;
    }
    void setTransformGizmoCurrentTranslationDelta(const QVector3D& delta)
    {
        _transformGizmoCurrentTranslationDelta = delta;
    }
    const QVector3D& transformGizmoCurrentScaleDelta() const
    {
        return _transformGizmoCurrentScaleDelta;
    }
    void setTransformGizmoCurrentScaleDelta(const QVector3D& delta)
    {
        _transformGizmoCurrentScaleDelta = delta;
    }
    const QVector3D& transformGizmoRotationPlaneNormal() const
    {
        return _transformGizmoRotationPlaneNormal;
    }
    void setTransformGizmoRotationPlaneNormal(const QVector3D& normal)
    {
        _transformGizmoRotationPlaneNormal = normal;
    }
    const QVector3D& transformGizmoRotationStartVector() const
    {
        return _transformGizmoRotationStartVector;
    }
    void setTransformGizmoRotationStartVector(const QVector3D& vector)
    {
        _transformGizmoRotationStartVector = vector;
    }
    const QVector3D& transformGizmoCurrentRotationDelta() const
    {
        return _transformGizmoCurrentRotationDelta;
    }
    void setTransformGizmoCurrentRotationDelta(const QVector3D& delta)
    {
        _transformGizmoCurrentRotationDelta = delta;
    }

    bool transformGizmoLoggedTranslationUpdate() const
    {
        return _transformGizmoLoggedTranslationUpdate;
    }
    void setTransformGizmoLoggedTranslationUpdate(bool logged)
    {
        _transformGizmoLoggedTranslationUpdate = logged;
    }

    void resetTransformGizmoDragSession();

    void setViewportMatrix(float width, float height);
    void recomputeModelViewMatrix()           { _modelViewMatrix = _viewMatrix * _modelMatrix; }

    void syncMatricesFromCamera(const Camera& camera);
    void syncPoseFromCamera(const Camera& camera);
    void syncRotationFromCamera(const Camera& camera);
    void syncTranslationFromCamera(const Camera& camera) { _currentTranslation = camera.getPosition(); }
    void syncCurrentViewRange()               { _currentViewRange = _viewRange; }
    void syncPoseAndRangeFromCamera(const Camera& camera);

    void updateFrustumPlanes();

    const QVector4D& frustumPlane(int index) const       { return _frustumPlanes[index]; }
    const QVector4D* frustumPlanes() const               { return _frustumPlanes; }

    int viewCubeHoveredRegionId() const                  { return _viewCubeHoveredRegionId; }
    void setViewCubeHoveredRegionId(int regionId)        { _viewCubeHoveredRegionId = regionId; }

    bool systemCameraStateSaved() const                  { return _systemCameraStateSaved; }
    void saveSystemCameraState(const Camera& camera);
    void restoreSystemCameraState(Camera& camera);
    Camera::ProjectionType savedProjectionType() const { return _savedProjectionType; }
    float savedCameraViewRange() const                   { return _savedCameraViewRange; }
    void clearSystemCameraState()                        { _systemCameraStateSaved = false; }

private:
    // ---- Saved system-camera state -----------------------------------------
    // Captured when a glTF camera is first activated; restored on deactivation.
    bool                     _systemCameraStateSaved  = false;
    QVector3D                _savedCameraPos;
    QVector3D                _savedCameraDir;
    QVector3D                _savedCameraUp;
    QVector3D                _savedCameraRight;
    Camera::ProjectionType _savedProjectionType     = Camera::ProjectionType::PERSPECTIVE;
    float                    _savedCameraFOV          = 45.0f;
    float                    _savedCameraViewRange    = 200.0f;

    // ---- View / navigation state -------------------------------------------
    QVector3D   _currentTranslation;
    QQuaternion _currentRotation;
    QQuaternion _customViewTargetRotation;
    float       _slerpStep                   = 0.0f;
    float       _slerpFrac                   = 0.0f;
    float       _currentViewRange            = 0.0f;
    float       _scaleFrac                   = 0.0f;
    float       _viewRange                   = 0.0f;
    float       _viewBoundingSphereDia       = 0.0f;
    float       _FOV                         = 45.0f;
    bool        _autoFitViewOnUpdate         = false;
    float       _zoomInLimit                 = 1.0f;

    // ---- View mode ---------------------------------------------------------
    ViewMode     _viewMode                   = ViewMode::NONE;
    ViewProjection _projection               = ViewProjection::PERSPECTIVE;
    Camera::ProjectionType _previousProjection = Camera::ProjectionType::PERSPECTIVE;
    bool         _multiViewActive            = false;
    int          _viewCubeHoveredRegionId    = -1;
    bool         _customViewAnimationActive  = false;
    bool         _cameraUpAxisZUp            = true;
    bool         _showViewCubeOverride       = true;
    CornerAxisPosition _cornerAxisPosition   = CornerAxisPosition::TOP_RIGHT;
    bool         _showAxis                   = true;
    bool         _userShowAxisOverride       = false;
    bool         _userShowCornerAxisOverride = false;

    // ---- Matrices ----------------------------------------------------------
    QMatrix4x4 _projectionMatrix;
    QMatrix4x4 _viewMatrix;
    QMatrix4x4 _modelMatrix;
    QMatrix4x4 _modelViewMatrix;
    QMatrix4x4 _viewportMatrix;
    QVector4D  _frustumPlanes[6];

    // ---- Mouse interaction state -------------------------------------------
    bool     _windowZoomActive           = false;
    bool     _viewZooming                = false;
    bool     _viewPanning                = false;
    bool     _viewRotating               = false;
    QPoint   _leftButtonPoint;
    QPoint   _rightButtonPoint;
    QPoint   _middleButtonPoint;
    bool     _navigationViewportLocked   = false;
    QRect    _navigationLockedViewport;
    QRect    _navigationLockedClientRect;
    QPoint   _lastPanPoint;
    int      _lastZoomDirection          = 0;
    float    _lastZoomStep               = 1.05f;
    QVector3D _lastZoomPanVector;
    QVector3D _inertiaZoomPanVelocity;
    QVector2D _inertiaPanVelocity;
    float    _inertiaZoomVelocity        = 0.0f;
    QVector2D _inertiaRotateVelocity;
    float    _inertiaDamping             = 0.8f;
    bool     _mouseMovedSincePress       = false;
    qint64   _lastMouseMoveTime          = 0;
    QPoint   _lastMousePos;
    qint64   _lastMouseTime              = 0;

    // ---- Rubber-band selection state ---------------------------------------
    QVector3D _rubberBandPan;
    float     _rubberBandZoomRatio       = 0.0f;
    float     _rubberBandRadius          = 0.0f;
    QVector3D _rubberBandCenter;
    bool      _shiftDragActive           = false;
    bool      _altShiftDragActive        = false;
    QPoint    _sweepStartPoint;

    // ---- Transform-gizmo drag state ----------------------------------------
    bool      _transformGizmoRequested            = false;
    bool      _transformGizmoTranslating          = false;
    bool      _transformGizmoScaling              = false;
    bool      _transformGizmoUniformScaling       = false;
    bool      _transformGizmoRotating             = false;
    QPoint    _transformGizmoDragStartPixel;
    QVector3D _transformGizmoDragAxis;
    QVector3D _transformGizmoStartPivot;
    float     _transformGizmoDragScale            = 1.0f;
    QMap<int, TransformState>  _transformGizmoStartStates;
    QMap<int, QVector3D>       _transformGizmoStartCenters;
    QMap<int, QMatrix4x4>      _transformGizmoStartMatrices;
    QVector3D _transformGizmoCurrentTranslationDelta;
    QVector3D _transformGizmoCurrentScaleDelta    = QVector3D(1.0f, 1.0f, 1.0f);
    QVector3D _transformGizmoRotationPlaneNormal  = QVector3D(0.0f, 0.0f, 1.0f);
    QVector3D _transformGizmoRotationStartVector  = QVector3D(1.0f, 0.0f, 0.0f);
    QVector3D _transformGizmoCurrentRotationDelta;
    bool      _transformGizmoLoggedTranslationUpdate = false;

    // ---- Scene bounding sphere / box --------------------------------------
    BoundingSphere _boundingSphere;
    BoundingSphere _selectionBoundingSphere;
    BoundingBox    _boundingBox;
    float          _visibleHighestZ = 0.0f;
    float          _visibleLowestZ  = 0.0f;

public:
    // ---- Static picking / viewport helpers ---------------------------------
    // Returns the CornerAxisPosition clamped to one of the four valid corners.
    static CornerAxisPosition normalizeCornerAxisPosition(CornerAxisPosition position);


    // Converts a pixel coordinate to a world-space ray (orig, dir).
    // Returns false if the viewport is degenerate or the ray is zero.
    static bool convertPixelToRay(const QPoint& pixel, const QRect& viewport,
                                   int widgetHeight,
                                   const QMatrix4x4& view,
                                   const QMatrix4x4& projection,
                                   QVector3D& orig, QVector3D& dir);

    // Intersects a ray with a plane; populates outPoint and returns true on hit.
    static bool intersectRayPlane(const QVector3D& rayOrigin, const QVector3D& rayDir,
                                   const QVector3D& planePoint,
                                   const QVector3D& planeNormal,
                                   QVector3D& outPoint);

    // Rotates a point around an arbitrary axis through pivot by angleDegrees.
    static QVector3D rotatePointAroundAxis(const QVector3D& point, const QVector3D& pivot,
                                            const QVector3D& axis, float angleDegrees);
};
