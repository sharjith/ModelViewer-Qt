#include "ViewportWidget.h"
#include "ClippingPlanesEditor.h"
#include "ExplodedViewPanel.h"
#include "ModelViewer.h"
#include "TabbedViewportToolbar.h"
#include <QEvent>

ToolsToolbar* ViewportWidget::getToolsToolbar() const
{
    return _tabbedToolbar->toolsToolbar();
}

QVariantMap ViewportWidget::viewMenuState() const
{
    QVariantMap state;
    state["rotate"] = _viewCtrl.viewRotating();
    state["pan"] = _viewCtrl.viewPanning();
    state["zoom"] = _viewCtrl.viewZooming();
    state["orbit"] = cameraMode() == Camera::CameraMode::Orbit;
    state["fly"] = cameraMode() == Camera::CameraMode::Fly;
    state["firstPerson"] = cameraMode() == Camera::CameraMode::FirstPerson;
    state["zUp"] = isCameraUpAxisZUp();
    state["yUp"] = !isCameraUpAxisZUp();
    state["ortho"] = projection() == ViewProjection::ORTHOGRAPHIC;
    state["perspective"] = projection() == ViewProjection::PERSPECTIVE;
    state["multi"] = isMultiViewActive();
    state["shaded"] = getDisplayMode() == DisplayMode::SHADED;
    state["hollow"] = getDisplayMode() == DisplayMode::HOLLOW_MESH;
    state["meshEdges"] = getDisplayMode() == DisplayMode::MESH_EDGES;
    state["wireframe"] = getDisplayMode() == DisplayMode::WIREFRAME;
    state["shadedEdges"] = getDisplayMode() == DisplayMode::SHADED_WITH_EDGES;
    // Ray tracing is armed separately; getRenderingMode() reports its raster fallback.
    const bool rayTraced = isRayTracedRenderingModeArmed();
    state["ads"] = !rayTraced && getRenderingMode() == RenderingMode::ADS_BLINN_PHONG;
    state["pbr"] = !rayTraced && getRenderingMode() == RenderingMode::PHYSICALLY_BASED_RENDERING;
    state["rayTraced"] = rayTraced;
    state["smooth"] = shadingNormalMode() == ShadingNormalMode::SMOOTH;
    state["flat"] = shadingNormalMode() == ShadingNormalMode::FLAT;
    state["realistic"] = isRealismEnabled();
    state["clipping"] = _clippingPlanesEditor && !_clippingPlanesEditor->isHidden();
    state["exploded"] = _explodedViewPanel && !_explodedViewPanel->isHidden();
    state["turntable"] = turntableEnabled();
    state["axis"] = _viewCtrl.showAxis();
    state["lasso"] = lassoToolArmed();
    state["swap"] = _sceneRuntime.visibleSwapped();
    state["debugEnabled"] = isDebugOverlayEnabled();
    state["boundingBox"] = debugOverlayMode() == DebugOverlayMode::BoundingBox;
    state["vertexNormals"] = debugOverlayMode() == DebugOverlayMode::VertexNormals;
    state["faceNormals"] = debugOverlayMode() == DebugOverlayMode::FaceNormals;
    state["available.boundingBox"] = _renderCtrl.debugBoundingBoxAvailable();
    state["available.vertexNormals"] = _renderCtrl.debugVertexNormalsAvailable();
    state["available.faceNormals"] = _renderCtrl.debugFaceNormalsAvailable();
    state["available.debugEnabled"] = _renderCtrl.debugBoundingBoxAvailable() || _renderCtrl.debugVertexNormalsAvailable() || _renderCtrl.debugFaceNormalsAvailable();
    state["available.wireframe"] = _viewToolbar->featureEdgeModesVisible();
    state["available.shadedEdges"] = _viewToolbar->featureEdgeModesVisible();
    return state;
}

void ViewportWidget::executeViewCommand(const QString& command, bool checked)
{
    // Menu commands target this viewport. Keep the same side effects as the toolbar.
    if (command == "fit") fitAll();
    else if (command == "windowZoom") beginWindowZoom();
    else if (command == "rotate") { if (checked) setRotationActive(true); else clearViewNavigation(); }
    else if (command == "pan") { if (checked) setPanningActive(true); else clearViewNavigation(); }
    else if (command == "zoom") { if (checked) setZoomingActive(true); else clearViewNavigation(); }
    else if (command == "top") { setViewMode(ViewMode::TOP); _viewToolbar->setDefaultStandardViewAction(StandardViewActions::TOP); }
    else if (command == "bottom") { setViewMode(ViewMode::BOTTOM); _viewToolbar->setDefaultStandardViewAction(StandardViewActions::BOTTOM); }
    else if (command == "front") { setViewMode(ViewMode::FRONT); _viewToolbar->setDefaultStandardViewAction(StandardViewActions::FRONT); }
    else if (command == "rear") { setViewMode(ViewMode::BACK); _viewToolbar->setDefaultStandardViewAction(StandardViewActions::REAR); }
    else if (command == "left") { setViewMode(ViewMode::LEFT); _viewToolbar->setDefaultStandardViewAction(StandardViewActions::LEFT); }
    else if (command == "right") { setViewMode(ViewMode::RIGHT); _viewToolbar->setDefaultStandardViewAction(StandardViewActions::RIGHT); }
    else if (command == "iso" || command == "dimetric" || command == "trimetric") {
        setViewMode(command == "iso" ? ViewMode::ISOMETRIC : command == "dimetric" ? ViewMode::DIMETRIC : ViewMode::TRIMETRIC);
        _viewToolbar->setDefaultViewModeAction(command == "iso" ? ViewModeActions::ISOMETRIC : command == "dimetric" ? ViewModeActions::DIMETRIC : ViewModeActions::TRIMETRIC);
        _viewToolbar->setDefaultStandardViewAction(StandardViewActions::TOP);
    }
    else if (command == "orbit") setCameraMode(Camera::CameraMode::Orbit);
    else if (command == "fly") setCameraMode(Camera::CameraMode::Fly);
    else if (command == "firstPerson") setCameraMode(Camera::CameraMode::FirstPerson);
    else if (command == "zUp" || command == "yUp") setCameraUpAxisZUp(command == "zUp");
    else if (command == "ortho" || command == "perspective") {
        setProjection(command == "ortho" ? ViewProjection::ORTHOGRAPHIC : ViewProjection::PERSPECTIVE);
        fitAll(); update();
    }
    else if (command == "multi") { setMultiView(checked); if (checked) setViewMode(ViewMode::ISOMETRIC); fitAll(); update(); }
    else if (command == "shaded") setDisplayMode(DisplayMode::SHADED);
    else if (command == "hollow") setDisplayMode(DisplayMode::HOLLOW_MESH);
    else if (command == "meshEdges") setDisplayMode(DisplayMode::MESH_EDGES);
    else if (command == "wireframe") setDisplayMode(DisplayMode::WIREFRAME);
    else if (command == "shadedEdges") setDisplayMode(DisplayMode::SHADED_WITH_EDGES);
    else if (command == "ads") _viewer->onRenderingModeSelected(QStringLiteral("ADS"));
    else if (command == "pbr") _viewer->onRenderingModeSelected(QStringLiteral("PBR"));
    else if (command == "rayTraced") _viewer->onRenderingModeSelected(QStringLiteral("RayTraced"));
    else if (command == "smooth" || command == "flat") setShadingNormalMode(command == "flat" ? ShadingNormalMode::FLAT : ShadingNormalMode::SMOOTH);
    else if (command == "realistic") setRealismEnabled(checked);
    else if (command == "clipping") showClippingPlaneEditor(checked);
    else if (command == "exploded") showExplodedViewPanel(checked);
    else if (command == "turntable") setTurntableEnabled(checked);
    else if (command == "axis") showAxis(checked);
    else if (command == "lasso") setLassoToolArmed(checked);
    else if (command == "swap") swapVisible(checked);
    else if (command == "debugEnabled") setDebugOverlayEnabled(checked);
    else if (command == "boundingBox") setDebugOverlayMode(DebugOverlayMode::BoundingBox);
    else if (command == "vertexNormals") setDebugOverlayMode(DebugOverlayMode::VertexNormals);
    else if (command == "faceNormals") setDebugOverlayMode(DebugOverlayMode::FaceNormals);
    emit viewStateChanged();
}

void ViewportWidget::clearViewNavigation()
{
    _viewCtrl.clearNavigationModes();
    _viewToolbar->deactivateAllNavigationModes();
    restoreArmedToolCursor();
    emit viewStateChanged();
}

bool ViewportWidget::eventFilter(QObject* object, QEvent* event)
{
    if ((object == _clippingPlanesEditor || object == _explodedViewPanel) &&
        (event->type() == QEvent::Show || event->type() == QEvent::Hide))
        emit viewStateChanged();
    return QOpenGLWidget::eventFilter(object, event);
}
