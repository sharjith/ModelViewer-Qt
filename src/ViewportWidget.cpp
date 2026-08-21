
#include "AssImpMeshBuilder.h"
#include "ClippingPlanesEditor.h"
#include "ExplodedViewPanel.h"
#include "AssemblyRelationGraph.h"
#include "ExplodedViewManager.h"
#include "SceneGraph.h"
#include "ConeRenderable.h"
#include "CubeRenderable.h"
#include "CoordinateSystemHelper.h"
#include "FloorPlane.h"
#include "GltfCameraData.h"
#include "MeasurementGeometry.h"
#include "MeasurementOffsetCommand.h"
#include "MeasurementOffsetVectorCommand.h"
#include "ViewportWidget.h"
#include "PickingHelper.h"
#include "RtSceneBuilder.h"
#include <QtMath>
#include "SelectionManager.h"
#include "TransformGizmo.h"
#include "LanguageManager.h"
#include "MainWindow.h"
#include "MaterialVariantsPanel.h"
#include "ModelViewer.h"
#include "SceneTreeWidget.h"
#include "AnimationsPanel.h"
#include "ModelViewerApplication.h"
#include "PathUtils.h"
#include "PlaneRenderable.h"
#include "Point.h"
#include "SphereRenderable.h"
#include "ViewCubeMesh.h"
#include "stb_image.h"
#include "HdrImageLoader.h"
#include "TangentGenerator.h"
#include "TextRenderer.h"
#include "Utils.h"
#include <algorithm>
#include <iostream>
#include <QCryptographicHash>
#include <QOpenGLContext>
#include <QDateTime>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QPainter>
#include <QSettings>
#include <QStyleFactory>
#include <QThread>
#include <QTreeView>
#include <QDebug>
#include "AnimationUtils.h"
#include "MeshMathUtils.h"
#include "SceneNode.h"


constexpr auto  MAX_MODEL_SIZE_BYTES       = 52428800; // bytes
constexpr float kDefaultFloorOffsetPercent = 0.0f;

// Degenerate-guard floor for TransformGizmo::computeWorldScale(), not a
// mesh-relative size - the gizmo's on-screen size is meant to stay constant
// (screen-space-proportional via camera distance / ortho view range) fully
// independent of the selected mesh's own size, matching reference DCC/engine
// gizmos (e.g. NVIDIA's vk_gltf_renderer, which sizes purely off view depth).
// Tying this to selectionRadius previously let a close-zoom on a small part
// pin the gizmo to that part's world size, which then ballooned on screen as
// the camera moved closer still.
constexpr float kTransformGizmoMinWorldScale = 0.01f;

// Minimum wall-clock gap between interactive-GPU-session SLOW-path restarts
// (startInteractiveRayTracedGpuSession()'s real start() call, not its
// updateCamera() fast path) - a burst of camera-move events (mouseMoveEvent,
// and especially onInertiaTimer() at ~60Hz) can fire far faster than a GPU
// worker thread can usefully spawn/join/relaunch, which only happens on the
// first tick of a drag or a mid-drag resolution change.
constexpr qint64 kInteractiveGpuRestartMinIntervalMs = 50;

// Debounce interval for _rayTracedResumeWarmUpTimer (see
// ViewportWidget::onRayTracedResumeWarmUpTimeout()'s doc comment) - long
// enough to coalesce a rapid burst of teardown-triggering events (a slider
// being dragged in VisualizationEnvironmentPanel, say) into a single
// rebuild+warm-up once the burst actually settles, short enough that the
// user isn't likely to have already started a new drag by the time it fires
// (in which case the timeout handler is a no-op anyway - see its own doc
// comment).
constexpr int kRayTracedResumeWarmUpDebounceMs = 400;

// NOTE: a faster _rayTracedRefreshTimer interval during interactive
// dragging (polling closer to a real display refresh rate, on the theory
// that updateCamera() had made the worker fast enough for it to help) was
// tried and reverted - it made responsiveness WORSE, not better. Each poll
// that finds a new frame triggers a full update()/paintGL(), which
// re-renders the raster scene (HUD, gizmos, etc.) on the SAME GPU the OptiX
// worker thread is using; polling at ~60Hz added real GPU/CPU contention
// against the path tracer's own kernels, which read as sluggish/"sticky"
// input rather than smoother motion. The actual bottleneck was never the
// poll rate - it's how long the worker takes to render one interactive
// chunk - so a faster poll can only ever notice a new frame sooner, never
// make one exist sooner. _rayTracedRefreshTimer stays at its original
// 100ms interval (see its setInterval(100) call near this widget's
// constructor) for both the interactive and settled session.

static SceneNode* findSceneNodeByAiChildPath(SceneNode* root, const QVector<int>& aiChildPath)
{
    if (!root)
        return nullptr;
    SceneNode* current = root;
    for (int idx : aiChildPath)
    {
        if (idx < 0 || idx >= current->children.size())
            return nullptr;
        current = current->children.at(idx);
    }
    return current;
}

// Maps the Settings dialog's "Default View" / "Axonometric Mode" combo selections to
// the initial ViewMode + Camera::ViewProjection pair used to seed a brand-new viewport.
// "Default View" chooses the starting orientation; when it resolves to Isometric,
// "Axonometric Mode" additionally picks the isometric/dimetric/trimetric flavor.
namespace
{
struct InitialView { ViewMode mode; Camera::ViewProjection cameraView; };

InitialView resolveInitialView(int defaultViewIndex, int axonometricModeIndex)
{
    switch (defaultViewIndex)
    {
        case 1: return { ViewMode::TOP,    Camera::ViewProjection::TOP_VIEW };
        case 2: return { ViewMode::FRONT,  Camera::ViewProjection::FRONT_VIEW };
        case 3: return { ViewMode::LEFT,   Camera::ViewProjection::LEFT_VIEW };
        case 4: return { ViewMode::BOTTOM, Camera::ViewProjection::BOTTOM_VIEW };
        case 5: return { ViewMode::BACK,   Camera::ViewProjection::REAR_VIEW };
        case 6: return { ViewMode::RIGHT,  Camera::ViewProjection::RIGHT_VIEW };
        case 0: // Isometric — refine using the Axonometric Mode default
        default:
            switch (axonometricModeIndex)
            {
                case 1: return { ViewMode::DIMETRIC, Camera::ViewProjection::DIMETRIC_VIEW };
                case 2: return { ViewMode::TRIMETRIC, Camera::ViewProjection::TRIMETRIC_VIEW };
                default: return { ViewMode::ISOMETRIC, Camera::ViewProjection::SE_ISOMETRIC_VIEW };
            }
    }
}
}

ViewportWidget::ViewportWidget(QWidget* parent, const char* /*name*/) : QOpenGLWidget(parent),
_textRenderer(nullptr),
_axisTextRenderer(nullptr),
_clippingPlanesEditor(nullptr),
_explodedViewPanel(nullptr),
// _explodedViewManager allocated in constructor body (pointer now lives in _explodedViewCtrl)
_clippingPlaneXY(nullptr),
_clippingPlaneYZ(nullptr),
_clippingPlaneZX(nullptr),
_floorPlane(nullptr),
	_skyBox(nullptr),
	_axisCone(nullptr),
	_transformGizmo(nullptr),
	_lightCube(nullptr),
	_lightSphere(nullptr),
	_assimpModelLoader(nullptr)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);  // Enable mouseMoveEvent for hover highlighting

	// Alt-Tabbing to another application doesn't hide/show this widget at all
	// (that's a Qt-internal visibility concept - an external app stealing
	// foreground focus never touches it), so hideEvent()/showEvent() alone
	// don't catch that case. QGuiApplication::applicationStateChanged() does:
	// it fires ApplicationActive when this app regains OS focus, regardless
	// of which top-level/MDI-child window the event originated from.
	//
	// Deliberately does NOT also stop anything on ApplicationInactive: the
	// background tracer thread and (invisible) presenter don't cost anything
	// while genuinely not being painted, and having two independent triggers
	// both calling stop()/invalidate() (this one and hideEvent()'s, which can
	// fire in either order relative to this signal around a focus change) was
	// producing exactly the "never re-engages" bug this is meant to fix -
	// only ever forcing a *restart* here removes that race entirely.
	//
	// Calls startRayTracedSession() directly rather than going through
	// RtInteractionController (which would only re-arm the idle
	// QTimer's countdown): Qt's QTimer can be significantly throttled/coalesced by
	// Windows while the app isn't the foreground window, so a timer that was
	// already counting down when focus was lost may still report
	// isActive()==true (just delayed, not actually about to fire) once focus
	// returns - waiting on it produces exactly the "sometimes comes back,
	// sometimes doesn't" flakiness this was meant to fix. Starting a session
	// immediately sidesteps any timer-throttling question entirely.
	connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
		if (state == Qt::ApplicationActive && _rtInteractionCtrl->armed() && isVisible())
		{
			_keys.clear();
			QTimer::singleShot(0, this, [this]() {
				if (_rtInteractionCtrl->armed() && isVisible() &&
				    !_rayTracedIdleTimer->isActive() &&
				    !rayTracedSessionRunning() &&
				    !_rtPresenter.hasFrame())
				{
					startRayTracedSession();
				}
			});
		}
	});

    _viewer = static_cast<ModelViewer*>(parent);
	_explodedViewCtrl.setExplodedViewManager(new ExplodedViewManager());
	_transformGizmo = new TransformGizmo(this);
	// Registered once here, for the widget's whole lifetime - see
	// IGpuContextResource.h/GpuResourceRegistry.h. _renderCtrl is
	// registered here too (Controller phase, restored first) even though
	// it's constructed implicitly as a plain member, not via `new` -
	// there's no other single "runs exactly once" point to do it from.
	_gpuResourceRegistry.add(_transformGizmo, GpuResourcePhase::Decorations);
	_gpuResourceRegistry.add(&_renderCtrl, GpuResourcePhase::Controller);


	// Setup the view toolbar
	_viewToolbar = new ViewToolbar(this);
	_viewToolbar->reposition(width(), height());

	connect(_viewToolbar, &ViewToolbar::zoomViewRequested, this, [this]() {
		setZoomingActive(true);
		});

	connect(_viewToolbar, &ViewToolbar::panViewRequested, this, [this]() {
		setPanningActive(true);
		});

	connect(_viewToolbar, &ViewToolbar::rotateViewRequested, this, [this]() {
		setRotationActive(true);
		});

	 connect(_viewToolbar, &ViewToolbar::cameraModeSelected, this, [this](const QString& type) {
		if (type == "Orbit") setCameraMode(Camera::CameraMode::Orbit);
		else if (type == "Fly") setCameraMode(Camera::CameraMode::Fly);
		else if (type == "First Person") setCameraMode(Camera::CameraMode::FirstPerson);
		});

	connect(_viewToolbar, &ViewToolbar::cameraUpAxisToggled, this, [this](bool zUp) {
		setCameraUpAxisZUp(zUp, false);
	});

	 connect(_viewToolbar, &ViewToolbar::viewSelected, this, [this](const QString& view) {
        if (view == "Top") setViewMode(ViewMode::TOP);
        else if (view == "Front") setViewMode(ViewMode::FRONT);
        else if (view == "Left") setViewMode(ViewMode::LEFT);
        else if (view == "Bottom") setViewMode(ViewMode::BOTTOM);
        else if (view == "Rear") setViewMode(ViewMode::BACK);
        else if (view == "Right") setViewMode(ViewMode::RIGHT);
    });

	 connect(_viewToolbar, &ViewToolbar::axonometricSelected, this, [this](const QString& type) {
		 if (type == "Isometric") setViewMode(ViewMode::ISOMETRIC);
		 else if (type == "Dimetric") setViewMode(ViewMode::DIMETRIC);
		 else if (type == "Trimetric") setViewMode(ViewMode::TRIMETRIC);
		 });

	 connect(_viewToolbar, &ViewToolbar::displayModeSelected, this, [this](const QString& type) {
		 if (type == "Realistic") setRealismEnabled(!_realismEnabled);
		 else if (type == "Shaded") setDisplayMode(DisplayMode::SHADED);
		 else if (type == "HollowMesh") setDisplayMode(DisplayMode::HOLLOW_MESH);
		 else if (type == "MeshEdges") setDisplayMode(DisplayMode::MESH_EDGES);
		 else if (type == "Wireframe") setDisplayMode(DisplayMode::WIREFRAME);
		 else if (type == "ShadedWithEdges") setDisplayMode(DisplayMode::SHADED_WITH_EDGES);
		 });
	 connect(_viewToolbar, &ViewToolbar::shadingNormalModeSelected, this, [this](const QString& mode) {
		 if (mode == "Flat")   setShadingNormalMode(ShadingNormalMode::FLAT);
		 else                  setShadingNormalMode(ShadingNormalMode::SMOOTH);
		 });
	 connect(this, &ViewportWidget::displayModeChanged, _viewer, &ModelViewer::onDisplayModeChanged);

	 connect(_viewToolbar, &ViewToolbar::fitToViewRequested, this, &ViewportWidget::fitAll);
	 
	 connect(_viewToolbar, &ViewToolbar::windowZoomRequested, this, &ViewportWidget::beginWindowZoom);

	 connect(_viewToolbar, &ViewToolbar::projectionToggled, this, [this](bool ortho) {
		 setProjection(ortho ? ViewProjection::ORTHOGRAPHIC : ViewProjection::PERSPECTIVE);
		 fitAll();
		 update();
		 });

	 connect(_viewToolbar, &ViewToolbar::multiViewToggled, this, [this](bool enabled) {
		 setMultiView(enabled);
		 if (enabled)
			 setViewMode(ViewMode::ISOMETRIC);
		 fitAll();
		 update();
		 });

	 connect(_viewToolbar, &ViewToolbar::sectionViewToggled, this, [this](bool enabled) {
		 showClippingPlaneEditor(enabled);
		 });

	 connect(_viewToolbar, &ViewToolbar::explodedViewToggled, this, [this](bool enabled) {
		 showExplodedViewPanel(enabled);
		 });

	 connect(_viewToolbar, &ViewToolbar::swapVisibleToggled, this, [this](bool enabled) {
		 swapVisible(enabled);
		 });

	 connect(_viewToolbar, &ViewToolbar::axisDisplayToggled, this, [this](bool enabled) {
		 showAxis(enabled);
		 });

     connect(_viewToolbar, &ViewToolbar::debugOverlaySelected, this, [this](const QString& overlayType) {
         if (overlayType == "BoundingBox")
             setDebugOverlayMode(DebugOverlayMode::BoundingBox);
         else if (overlayType == "VertexNormals")
             setDebugOverlayMode(DebugOverlayMode::VertexNormals);
         else if (overlayType == "FaceNormals")
             setDebugOverlayMode(DebugOverlayMode::FaceNormals);
     });

     connect(_viewToolbar, &ViewToolbar::debugOverlayToggled, this, [this](bool enabled) {
         setDebugOverlayEnabled(enabled);
     });

	connect(this, &ViewportWidget::visibleSwapped, _viewToolbar, &ViewToolbar::setSwapVisibleChecked);

    const QSettings displaySettings(QCoreApplication::organizationName(),
                                    QCoreApplication::applicationName());
	const bool wireframeFeaturesEnabled =
		displaySettings.value("showWireframeCheckBox", true).toBool();
    const bool boundingBoxOverlayEnabled =
        displaySettings.value("showBoundingBoxCheckBox", true).toBool();
    const bool vertexNormalsOverlayEnabled =
        displaySettings.value("showVertexNormalsCheckBox", true).toBool();
    const bool faceNormalsOverlayEnabled =
        displaySettings.value("showFaceNormalsCheckBox", true).toBool();
	_viewToolbar->setFeatureEdgeModesVisible(wireframeFeaturesEnabled);
    _viewToolbar->setDebugOverlayModesAvailable(
        boundingBoxOverlayEnabled,
        vertexNormalsOverlayEnabled,
        faceNormalsOverlayEnabled);
    setDebugOverlayAvailability(
        boundingBoxOverlayEnabled,
        vertexNormalsOverlayEnabled,
        faceNormalsOverlayEnabled);

	loadBgColorSettings();


	QSettings initialViewSettings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
	const InitialView initialView = resolveInitialView(
		initialViewSettings.value("comboDefaultView", 0).toInt(),
		initialViewSettings.value("comboDefaultProjection", 0).toInt());
	const ViewProjection initialProjection = initialViewSettings.value("comboProjectionMode", 0).toInt() == 1
		? ViewProjection::PERSPECTIVE : ViewProjection::ORTHOGRAPHIC;
	const Camera::ProjectionType initialCameraProjection = (initialProjection == ViewProjection::PERSPECTIVE)
		? Camera::ProjectionType::PERSPECTIVE : Camera::ProjectionType::ORTHOGRAPHIC;

	_viewCtrl.setViewBoundingSphereDia(200.0f);
	_viewCtrl.setViewRange(_viewCtrl.viewBoundingSphereDia());
	_viewCtrl.setFOV(45.0f);
	_viewCtrl.setCurrentViewRange(1.0f);
	_viewCtrl.setViewMode(initialView.mode);
	_viewCtrl.setProjection(initialProjection);
	_viewCtrl.setPreviousProjection(initialCameraProjection);

	_viewCtrl.setAutoFitViewOnUpdate(true);
	_selectionHighlighting = true;

	_primaryCamera = new Camera(width(), height(), _viewCtrl.viewRange(), _viewCtrl.FOV());
	_primaryCamera->setView(initialView.cameraView);

	_orthoViewsCamera = new Camera(width(), height(), _viewCtrl.viewRange(), _viewCtrl.FOV());
	_orthoViewsCamera->setView(initialView.cameraView);

	loadNavigationSettings();
	{
		QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
		setCameraUpAxisZUp(settings.value("comboCameraUpAxis", 0).toInt() == 0);
	}

	_viewCtrl.syncPoseFromCamera(*_primaryCamera);
	_viewCtrl.setCurrentViewRange(_viewCtrl.viewRange());

	// Create SelectionManager (dependency injection of camera reference)
	_selectionManager = new SelectionManager(
		this,
		_primaryCamera,
		_sceneRuntime.meshStore(),
		_sceneRuntime.displayedObjectsIds(),
		_sceneRuntime.hiddenObjectsIds(),
		_sceneRuntime.visibleSwapped(),
		this);  // Parent for Qt memory management

	// Connect SelectionManager signals to ViewportWidget update
	connect(_selectionManager, &SelectionManager::hoverChanged,
			this, [this](int) { update(); });
	connect(_selectionManager, &SelectionManager::selectionChanged,
			this, [this](const QList<int>& selectedIds) {
				if (selectedIds.isEmpty()) {
					// Viewport empty-space click: nothing was hit.
					// Let setListRow(-1) handle deselecting the tree widget and
					// broadcasting the authoritative empty state to panels.
					emit singleSelectionDone(-1);
					update();
					return;
				}
				// Forward to external panels (e.g. TextureDebugPanel) BEFORE
				// singleSelectionDone so the panel sees the "raw" click state
				// first.  singleSelectionDone triggers setListRow, which may
				// toggle-deselect the mesh and call broadcastSelectionChanged({})
				// — that final broadcast is the authoritative state the panel
				// should end up in.  If we emitted selectionChanged AFTER
				// singleSelectionDone, the toggle-deselect clear would be
				// immediately overwritten by this "raw" [meshId] emission.
				emit selectionChanged(selectedIds);
				// Emit singleSelectionDone only for actual single clicks.
				// This triggers setListRow, which handles toggle-deselect and
				// multi-select bookkeeping, and ultimately calls
				// broadcastSelectionChanged with the authoritative final list.
				if (selectedIds.count() == 1) {
					emit singleSelectionDone(selectedIds.first());
				}
				update();
			});

	// Load hover highlight mode from saved settings
	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
	int modeIndex = settings.value("comboBoxHoverHighlightMode", static_cast<int>(HoverHighlightMode::Disabled)).toInt();
	if (modeIndex >= 0 && modeIndex <= 2) {
		_selectionManager->setHoverHighlightMode(static_cast<HoverHighlightMode>(modeIndex));
	}

	_viewCtrl.resetSlerpStep();
	_viewCtrl.setSlerpFrac(0.05f);

	_modelNum = 6;

	_ambientLight = { 0.12f, 0.12f, 0.12f, 1.0f };
	_diffuseLight = _renderCtrl.defaultLightColor();
	_specularLight = _renderCtrl.defaultLightColor();

	_lightPosition = { 25.0f, 25.0f, 50.0f };

	_displayMode = DisplayMode::SHADED;
	_viewCtrl.setMultiViewActive(false);

	_viewCtrl.setShowAxis(true);

	_viewCtrl.setWindowZoomActive(false);

    _rubberBand = new QRubberBand(QRubberBand::Rectangle, this);
    _rubberBand->setStyle(QStyleFactory::create("Fusion"));


	_viewCtrl.clearNavigationModes();

	_modelName = "Model";

	_clippingPlaneXY = nullptr;
	_clippingPlaneYZ = nullptr;
	_clippingPlaneZX = nullptr;

	_renderCtrl.setCappingEnabled(false);
	_renderCtrl.setCappingTexture(0);

	_renderCtrl.setShowVertexNormals(false);
	_renderCtrl.setShowFaceNormals(false);

	_renderCtrl.setEnvMapEnabled(false);
	_renderCtrl.setShadowsEnabled(false);
	_renderCtrl.setSelfShadowsEnabled(false);
	_renderCtrl.setReflectionsEnabled(false);
	_floorSize = 10.0f;
	_floorSizeFactor = 5.0f;
	_floorPlaneZ = -0.5f;
	_renderCtrl.setGroundMode(GroundMode::None);
	_renderCtrl.setFloorTextureDisplayed(true);
	_renderCtrl.setFloorTexRepeatS(1.0f);
	_renderCtrl.setFloorTexRepeatT(1.0f);
	_renderCtrl.setFloorOffsetPercent(kDefaultFloorOffsetPercent / 100.0f);

	// Floor texture
	if (!_texBuffer.load(PathUtils::getDataDirectory() + "/" + "textures/envmap/floor/Grey-White-Checkered-Squares1800x1800.jpg"))
	{ // Load first image from file
		qWarning("ViewportWidget::loadFloor - Could not read image file, using single-color instead.");
		QImage dummy(128, 128, QImage::Format_ARGB32);
		dummy.fill(Qt::white);
		_floorTexImage = dummy;
	}
	else
	{
		_floorTexImage = convertToGLFormat(_texBuffer);
	}

	_renderCtrl.setSkyBoxEnabled(false);
	_renderCtrl.setSkyBoxBlurPercent(0);
	_renderCtrl.setSkyBoxFOV(45.0f);
	_renderCtrl.setSkyBoxZRotation(0.0f);
	_renderCtrl.setSkyBoxTextureHDRI(false);
	_renderCtrl.setGammaCorrection(false);
	_renderCtrl.setScreenGamma(2.2f);
	_renderCtrl.setHdrToneMapping(false);
	_renderCtrl.setEnvMapExposure(1.0f);
	_renderCtrl.setIblExposure(1.0f);
	_renderCtrl.setToneMappingMode(HDRToneMapMode::KhronosPbrNeutral);

	_renderCtrl.setLowResEnabled(false);	
	_renderCtrl.setShowLights(false);
	_renderCtrl.setUseDefaultLights(true);
	_renderCtrl.setUsePunctualLights(true);
	_renderCtrl.setUseIBL(true);

	_renderCtrl.setShadowWidth(1024 * 4);
	_renderCtrl.setShadowHeight(1024 * 4);

	_renderCtrl.setEnvironmentMap(0);
	_renderCtrl.setIrradianceMap(0);
	_renderCtrl.setPrefilterMap(0);
	_renderCtrl.setBrdfLUTTexture(0);
	_renderCtrl.setCharlieLUTTexture(0);
	_renderCtrl.setSheenELUTTexture(0);

	_viewCtrl.setRubberBandRadius(1.0f);
	_viewCtrl.setRubberBandZoomRatio(0.5f);

	_viewCtrl.setScaleFrac(1.0f);

	_displayedObjectsMemSize = 0;
	_sceneRuntime.setVisibleSwapped(false);

	_keyboardNavTimer = new QTimer(this);
	connect(_keyboardNavTimer, &QTimer::timeout, this, &ViewportWidget::performKeyboardNav);
	_keyboardNavTimer->start(15);

	_animateViewTimer = new QTimer(this);
	_animateViewTimer->setTimerType(Qt::PreciseTimer);
	connect(_animateViewTimer, &QTimer::timeout, this, &ViewportWidget::animateViewChange);
	connect(this, &ViewportWidget::rotationsSet, this, &ViewportWidget::stopAnimations);

	_animateFitAllTimer = new QTimer(this);
	_animateFitAllTimer->setTimerType(Qt::PreciseTimer);
	connect(_animateFitAllTimer, &QTimer::timeout, this, &ViewportWidget::animateFitAll);
	connect(this, &ViewportWidget::zoomAndPanSet, this, &ViewportWidget::stopAnimations);

	_animateWindowZoomTimer = new QTimer(this);
	_animateWindowZoomTimer->setTimerType(Qt::PreciseTimer);
	connect(_animateWindowZoomTimer, &QTimer::timeout, this, &ViewportWidget::animateWindowZoom);
	connect(this, &ViewportWidget::zoomAndPanSet, this, &ViewportWidget::stopAnimations);

	_animateCenterScreenTimer = new QTimer(this);
	_animateCenterScreenTimer->setTimerType(Qt::PreciseTimer);
	connect(_animateCenterScreenTimer, &QTimer::timeout, this, &ViewportWidget::animateCenterScreen);
	connect(this, &ViewportWidget::zoomAndPanSet, this, &ViewportWidget::stopAnimations);

	_inertiaTimer = new QTimer(this);
	_inertiaTimer->setInterval(16); // ~60 FPS
	connect(_inertiaTimer, &QTimer::timeout, this, &ViewportWidget::onInertiaTimer);

	// Ray-traced mode: idle timer is single-shot, reset on every camera-
	// affecting event (owned by RtInteractionController - see its
	// class doc comment); firing means the camera has been still for the
	// timeout, so start a fresh trace.
	_rayTracedIdleTimer = new QTimer(this);
	_rayTracedIdleTimer->setSingleShot(true);
	_rayTracedIdleTimer->setInterval(450);
	connect(_rayTracedIdleTimer, &QTimer::timeout, this, &ViewportWidget::onRayTracedIdleTimeout);

	// Repaints the viewport periodically while a trace is running so newly
	// published progressive-refinement frames actually get shown - paintGL()
	// isn't otherwise re-triggered on its own while nothing else changes.
	_rayTracedRefreshTimer = new QTimer(this);
	_rayTracedRefreshTimer->setInterval(100);
	connect(_rayTracedRefreshTimer, &QTimer::timeout, this, &ViewportWidget::onRayTracedRefreshTimer);

	// See onRayTracedResumeWarmUpTimeout()'s doc comment - debounces the
	// interactive accumulator's post-teardown rebuild+warm-up (scene
	// mutations, scripted view animations, resizes, MDI hide/show, ...),
	// armed by RtInteractionController on entering its Recovering
	// state.
	_rayTracedResumeWarmUpTimer = new QTimer(this);
	_rayTracedResumeWarmUpTimer->setSingleShot(true);
	// MUST set the interval here: RtInteractionController::
	// enterRecovering() starts this timer with the no-arg start() (relying on
	// whatever interval is already configured), unlike the old
	// armRayTracedResumeWarmUp() this replaced, which always passed
	// kRayTracedResumeWarmUpDebounceMs explicitly to start() itself. Without
	// this line the timer defaults to a 0ms interval, so every single
	// Recovering entry (i.e. every ~5ms tick of a scripted view animation
	// while PT is armed) fires it again almost immediately - each fire re-
	// enters Interacting via a full GAS/IAS rebuild + synchronous warm-up
	// (see onResumeTimerFired()/enterInteracting()), which the very next
	// animation tick immediately tears back down again. That per-frame
	// rebuild cycle is exactly the "view animation stutters badly while PT
	// is armed" regression this line fixes.
	_rayTracedResumeWarmUpTimer->setInterval(kRayTracedResumeWarmUpDebounceMs);
	connect(_rayTracedResumeWarmUpTimer, &QTimer::timeout, this, &ViewportWidget::onRayTracedResumeWarmUpTimeout);

	{
		RtInteractionController::Callbacks ptCallbacks;
		ptCallbacks.isGpuEngine = [this] { return effectiveRayTracingEnginePreference() == RtRayTracingEnginePreference::GPU; };
		ptCallbacks.startInteractiveSession = [this] { startInteractiveRayTracedGpuSession(); };
		ptCallbacks.startInteractiveSessionWithSceneRefresh = [this] { startInteractiveRayTracedGpuSession(/*forceSceneRefresh=*/true); };
		ptCallbacks.teardownSessions = [this] { teardownActiveRayTracedSessions(); };
		ptCallbacks.startSettledSessionImmediate = [this] { startRayTracedSession(); };
		ptCallbacks.stopAllWorkerSessions = [this] { _rtSession.stop(); _ptOptixSession.stop(); };
		ptCallbacks.isInteractiveSessionLive = [this] { return _rayTracedInteractiveActive; };
		ptCallbacks.maxSamples = [this] { return std::max<uint32_t>(_ptMaxSamples, 1); };
		ptCallbacks.maxBounces = [this] { return static_cast<unsigned int>(std::max(_ptMaxBounces, 1)); };
		_rtInteractionCtrl = new RtInteractionController(_rtInteractiveRenderer, _rayTracedIdleTimer,
			_rayTracedResumeWarmUpTimer, std::move(ptCallbacks));
	}

	// Load the user's persisted PT quality settings now, unconditionally -
	// previously this only happened inside RtRenderDialog::loadSettings(),
	// which only runs if/when that dialog is actually opened. Since Path
	// Tracing itself can be triggered by the idle timer without the dialog
	// ever having been opened in the session, that left every PT setting
	// silently pinned to its hardcoded default (e.g. _ptMaxSamples's 128)
	// until the user happened to open the dialog once - see
	// loadRayTracingSettingsFromDisk()'s doc comment.
	loadRayTracingSettingsFromDisk();

	_animCtrl.setAnimationTimer(new QTimer(this));
	_animCtrl.animationTimer()->setInterval(16);
	connect(_animCtrl.animationTimer(), &QTimer::timeout, this, &ViewportWidget::onAnimationTick);

	_editorLayout = new QVBoxLayout(this);
	_upperLayout = new QFormLayout();
	_upperLayout->setFormAlignment(Qt::AlignTop | Qt::AlignLeft);
	_upperLayout->setRowWrapPolicy(QFormLayout::DontWrapRows);
	_upperLayout->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
	_editorLayout->addItem(_upperLayout);

	_editorLayout->addStretch(height());

	_lowerLayout = new QFormLayout();
	_editorLayout->addItem(_lowerLayout);
	_lowerLayout->setFormAlignment(Qt::AlignBottom | Qt::AlignRight);
	_lowerLayout->setRowWrapPolicy(QFormLayout::DontWrapRows);
	_lowerLayout->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);

	int toolbarHeight = _viewToolbar->height();
	_lowerLayout->setContentsMargins(0, 0, 0, toolbarHeight);

	_clippingPlanesEditor = new ClippingPlanesEditor(this);
	_lowerLayout->addWidget(_clippingPlanesEditor);
	connect(this, &ViewportWidget::backgroundColorChanged,
	        _clippingPlanesEditor, &ClippingPlanesEditor::applyBackgroundTheme);
	_clippingPlanesEditor->hide();

	_explodedViewPanel = new ExplodedViewPanel(this);
	_lowerLayout->addWidget(_explodedViewPanel);
	connect(this, &ViewportWidget::backgroundColorChanged,
	        _explodedViewPanel, &ExplodedViewPanel::applyBackgroundTheme);
	_explodedViewPanel->hide();
	connect(_explodedViewPanel, &ExplodedViewPanel::explosionParametersChanged,
	        this, &ViewportWidget::updateExplosion);
	updateOverlayEditorTheme();

	//_sceneRuntime.displayedObjectsIds().push_back(0);

	setContextMenuPolicy(Qt::CustomContextMenu);
	connect(this, &ViewportWidget::customContextMenuRequested, this, &ViewportWidget::showContextMenu);

	_selectRect = new QRubberBand(QRubberBand::Rectangle, this);

	retranslateUI();

	connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {		
		retranslateUI();  // if needed
		});
}

ViewportWidget::~ViewportWidget()
{
	// Must be first: disconnects the QOpenGLContext::aboutToBeDestroyed()
	// handler wired up in initializeGL() before the explicit cleanup below
	// runs. Without this, the base QOpenGLWidget destructor's later context
	// teardown (after this destructor's body finishes) fires that handler
	// again, calling releaseGLSceneResources() a second time on GL objects
	// this destructor already freed - see the connection member's own doc
	// comment in the header for the full story.
	disconnect(_glContextAboutToBeDestroyedConnection);

	// Cancel/join the ray-tracing worker before anything else is torn down -
	// it holds a shared_ptr to its own RtSceneSnapshot, not to any of this
	// widget's scene state, so no ordering dependency on the teardown below.
	_rtSession.stop();
	_ptOptixSession.stop(); // no-op if the GPU engine was never used this session
	stopRtInteractiveRenderer(); // no-op if the GPU interactive path was never used this session
	delete _rtInteractionCtrl;
	_rtInteractionCtrl = nullptr;

	if (_animCtrl.animationTimer())
	{
		_animCtrl.animationTimer()->stop();
		disconnect(_animCtrl.animationTimer(), nullptr, this, nullptr);
	}
	_animCtrl.resetPlayback();
	_animCtrl.clearAllAnimationFiles();

	_viewToolbar = nullptr;

	// _textRenderer/_axisTextRenderer deletion moved into
	// releaseGLSceneResources() below (called from the context() guard
	// right underneath) - it now nulls them after deleting, which this
	// site never did; leaving both here as well would double-delete once
	// releaseGLSceneResources() runs, since it'd see the same still-non-null
	// dangling pointer this block just freed.

	// ===== CRITICAL: Ensure context is current before GL calls =====
	// context()->isValid() only means Qt successfully created the underlying
	// QOpenGLContext object - it says nothing about whether THIS widget's
	// initializeGL() ever actually ran. If the widget is destroyed before its
	// first paint (e.g. it never received an expose/paint event - see
	// initializeGL()'s and paintGL()'s own isOpenGLInitialized() guards),
	// _renderCtrl/PunctualLights never resolved their GL function pointers,
	// so any GL call below (even via a "valid" context) segfaults.
	if (context() && context()->isValid() && _renderCtrl.isOpenGLInitialized())
	{
		makeCurrent();
		releaseGLSceneResources();
		// Must run before doneCurrent(): unlike releaseGLSceneResources()
		// (which only releases GPU handles, keeping the C++ objects alive
		// for a future restore), this actually deletes them - their
		// destructors make real GL delete calls (RenderableMesh::
		// ~RenderableMesh() etc.), so a current context is required. Not
		// called in the else branch below: that path only means this
		// widget never got a valid context at all, so none of these
		// objects were ever constructed in the first place (all still
		// null) - nothing to delete, and no current context to safely
		// delete through if there somehow were.
		deleteGpuOwnedObjects();
		doneCurrent();  // Release context

		qInfo() << "ViewportWidget::~ViewportWidget - OpenGL resources cleaned up successfully.";
	}
	else
	{
		qWarning() << "ViewportWidget::~ViewportWidget - No valid OpenGL context for cleanup.";
	}

	for (auto& a : _sceneRuntime.meshStore())
	{
		delete a.mesh;
	}
	if (_primaryCamera)
		delete _primaryCamera;
	if (_orthoViewsCamera)
		delete _orthoViewsCamera;

	if (_sceneRuntime.globalScene())
	{
		SceneUtils::deleteScene(_sceneRuntime.globalScene());
		_sceneRuntime.setGlobalScene(nullptr);
	}

	// _assimpModelLoader deletion also moved into releaseGLSceneResources()
	// above, same reasoning as _textRenderer/_axisTextRenderer - kept out of
	// here to avoid the same class of double-delete risk, even though this
	// particular ordering happened to leave it already-nulled by the time
	// execution reaches here.
}

void ViewportWidget::releaseGLSceneResources()
{
	releaseLoadedMeshGpuResources();

	// Deferred, not migrated into the GPU resource registry (see
	// IGpuContextResource.h) - these three are fully deleted and
	// reconstructed every context recreation, same as they always have
	// been (a recreation-cost concern, not a resource-lifecycle-
	// correctness one - they're already leak-free as-is). Kept OUTSIDE
	// _gpuResourceRegistry.releaseAll() below deliberately.
	if (_assimpModelLoader) { delete _assimpModelLoader; _assimpModelLoader = nullptr; }
	if (_textRenderer) { delete _textRenderer; _textRenderer = nullptr; }
	if (_axisTextRenderer) { delete _axisTextRenderer; _axisTextRenderer = nullptr; }

	_rtPresenter.cleanup();
	if (_selectionManager)
		_selectionManager->cleanupFBOResources();

	// Releases every migrated resource (SceneRenderController, the 7
	// RenderableMesh-derived decoration objects, TransformGizmo,
	// transmission/SSS buffers) in reverse phase/registration order - see
	// GpuResourceRegistry::releaseAll(). Unlike the old per-object delete
	// list this replaces, none of these C++ objects are destroyed here -
	// only their GPU handles are released; the objects themselves survive
	// for the next initializeGL()'s restorePhase() calls to reuse.
	_gpuResourceRegistry.releaseAll();
}

void ViewportWidget::deleteGpuOwnedObjects()
{
	// Must run first: deletes every adapter wrapper (RenderableMeshGpuResourceAdapter
	// for the 7 decoration objects, LambdaGpuResource for the transmission/
	// SSS buffer pairs) - these hold non-owning pointers/callbacks into the
	// objects deleted below, so they must go first to avoid a dangling
	// reference during their own destruction. Does NOT delete the wrapped
	// RenderableMesh objects themselves (RenderableMeshGpuResourceAdapter
	// doesn't own them).
	_gpuResourceAdapters.clear();

	_gpuResourceRegistry.remove(_transformGizmo);
	delete _transformGizmo;
	_transformGizmo = nullptr;

	// &_renderCtrl is never delete'd here - it's a plain value member of
	// this widget, destroyed automatically as part of ~ViewportWidget()'s
	// own member destruction, same as before this refactor.
	_gpuResourceRegistry.remove(&_renderCtrl);

	if (_clippingPlaneXY) { delete _clippingPlaneXY; _clippingPlaneXY = nullptr; }
	if (_clippingPlaneYZ) { delete _clippingPlaneYZ; _clippingPlaneYZ = nullptr; }
	if (_clippingPlaneZX) { delete _clippingPlaneZX; _clippingPlaneZX = nullptr; }
	if (_floorPlane) { delete _floorPlane; _floorPlane = nullptr; }
	if (_axisCone) { delete _axisCone; _axisCone = nullptr; }
	if (_viewCube) { delete _viewCube; _viewCube = nullptr; }
	if (_skyBox) { delete _skyBox; _skyBox = nullptr; }
	if (_lightCube) { delete _lightCube; _lightCube = nullptr; }
	if (_lightSphere) { delete _lightSphere; _lightSphere = nullptr; }
}

void ViewportWidget::invalidateTextureCacheGpuResources()
{
	if (QCoreApplication::testAttribute(Qt::AA_ShareOpenGLContexts))
		return;

	for (auto& [path, entry] : _sceneRuntime.texCache())
	{
		entry.lastGPUTexture = 0;
		entry.refCount = 0;
	}
	_sceneRuntime.texRefCount().clear();
}

void ViewportWidget::releaseLoadedMeshGpuResources()
{
	invalidateTextureCacheGpuResources();
	for (const SceneMeshRecord& meshRecord : _sceneRuntime.meshStore())
	{
		if (meshRecord.mesh)
			meshRecord.mesh->releaseContextBoundGpuResources();
	}
}

void ViewportWidget::restoreLoadedMeshGpuResources()
{
	for (const SceneMeshRecord& meshRecord : _sceneRuntime.meshStore())
	{
		SceneMesh* mesh = meshRecord.mesh;
		if (!mesh)
			continue;

		mesh->restoreContextBoundGpuResources(_renderCtrl.fgShader());
		// Even with shared contexts, meshes keep only raw GL ids. Re-resolving the
		// material refreshes those ids from the cache on the current context and
		// repopulates them entirely on the non-shared fallback path.
		const Material resolvedMaterial = resolveMaterialTextures(this, mesh->getMaterial());
		mesh->setTextureMaps(resolvedMaterial);
	}
}

void ViewportWidget::registerDecorationGpuResource(RenderableMesh* mesh, std::function<QOpenGLShaderProgram*()> shaderResolver)
{
	_gpuResourceAdapters.push_back(std::make_unique<RenderableMeshGpuResourceAdapter>(mesh, std::move(shaderResolver)));
	_gpuResourceRegistry.add(_gpuResourceAdapters.back().get(), GpuResourcePhase::Decorations);
}

void ViewportWidget::retranslateUI()
{
	// Axis labels
	_labelAxisX = tr("X");
	_labelAxisY = tr("Y");
	_labelAxisZ = tr("Z");

	// View labels
	_labelTop = tr("Top");
	_labelFront = tr("Front");
	_labelLeft = tr("Left");
	_labelIsometric = tr("Isometric");
	_labelDimetric = tr("Dimetric");
	_labelTrimetric = tr("Trimetric");
}

void ViewportWidget::moveToRecycleBin(const QUuid& uuid, int originalIndex)
{
	if (!_sceneRuntime.moveToRecycleBin(uuid, originalIndex))
	{
		qWarning() << "ViewportWidget::moveToRecycleBin - Mesh not found:" << uuid;
		return;
	}
	qDebug() << "Moved mesh to recycle bin, uuid:" << uuid;
}

bool ViewportWidget::restoreFromRecycleBin(const QUuid& uuid)
{
	if (!_sceneRuntime.restoreFromRecycleBin(uuid))
	{
		qWarning() << "ViewportWidget::restoreFromRecycleBin - Mesh not in bin:" << uuid;
		return false;
	}
	qDebug() << "Restored mesh from recycle bin, uuid:" << uuid;
	return true;
}

void ViewportWidget::permanentlyDeleteFromBin(const QUuid& uuid)
{
	if (!_sceneRuntime.permanentlyDeleteFromRecycleBin(uuid))
		return;
	qDebug() << "Permanently deleted mesh from recycle bin, uuid:" << uuid;
}

bool ViewportWidget::isInRecycleBin(const QUuid& uuid) const
{
	return _sceneRuntime.isInRecycleBin(uuid);
}

QVector<QUuid> ViewportWidget::getRecycleBinUuids() const
{
	return _sceneRuntime.recycleBinUuids();
}

QList<QUuid> ViewportWidget::getPendingSceneUuids() const
{
	return _sceneRuntime.pendingSceneUuids();
}

SceneMesh* ViewportWidget::getMeshByUuid(const QUuid& uuid) const
{
	return _sceneRuntime.getMeshByUuid(uuid);
}

SceneMesh* ViewportWidget::getMeshByIndex(int index) const
{
	return _sceneRuntime.getMeshByIndex(index);
}

int ViewportWidget::getIndexByUuid(const QUuid& uuid) const
{
	return _sceneRuntime.getIndexByUuid(uuid);
}

QUuid ViewportWidget::getUuidByIndex(int index) const
{
	return _sceneRuntime.getUuidByIndex(index);
}

// Map C++ DisplayMode enum to the integer values expected by main_scene.frag.
// Shader: 0=shaded, 1=hollow/wireframe, 2=wireshaded.
// Flat shading and realism are orthogonal uniforms (renderingMode==2 / realismEnabled).
static int displayModeShaderInt(DisplayMode mode)
{
	switch (mode)
	{
		case DisplayMode::HOLLOW_MESH:       return 1;
		case DisplayMode::MESH_EDGES:        return 2;
		default:                             return 0; // SHADED, WIREFRAME, SHADED_WITH_EDGES
	}
}

void ViewportWidget::initializeGL()
{
	_renderCtrl.setOpenGLInitialized(false);

	if (!QOpenGLContext::currentContext())
	{
		qCritical() << "ViewportWidget::initializeGL: no current OpenGL context — skipping initialisation";
		return;
	}

	if (!initializeOpenGLFunctions())
	{
		qCritical() << "ViewportWidget::initializeGL: failed to resolve OpenGL 4.5 Core functions — skipping initialisation";
		return;
	}

	if (!_renderCtrl.initialize())
	{
		qCritical() << "ViewportWidget::initializeGL: SceneRenderController failed to resolve OpenGL functions — skipping initialisation";
		return;
	}

	int maxSamples = 0;
	glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);	
	ModelViewerApplication::setSupportedMSAASamples(maxSamples);

	GLfloat maxAniso = 0.0f;
	glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
	ModelViewerApplication::setSupportedAnisotropicFilteringLevel(maxAniso);

	// Sheen is part of the guaranteed 0..31 budget, so its LUTs live on fixed
	// units 8/9 instead of using the older overflow/fallback layout.
	
	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
	// Set Anisotropic Filtering Level
	int anIsoVals[] = { 1, 2, 4, 8, 16, 32 };
	_renderCtrl.setAnisotropicFilteringLevel(anIsoVals[settings.value("anisotropyComboBox", 4).toInt()]);

	_viewCtrl.setUserShowAxisOverride(settings.value("showCenterTrihedronCheckBox", true).toBool());
	_viewCtrl.setUserShowCornerAxisOverride(settings.value("showCornerTrihedronCheckBox", true).toBool());
	_viewCtrl.setCornerAxisPosition(static_cast<CornerAxisPosition>(settings.value("comboBoxCornerTrihedronPosition", 1).toInt()));
	_viewCtrl.setShowViewCubeOverride(settings.value("showViewCubeCheckBox", true).toBool() && (_viewCtrl.cornerAxisPosition() != CornerAxisPosition::BOTTOM_RIGHT));
		
	makeCurrent();

	// Qt destroys and recreates this widget's QOpenGLContext (triggering a
	// fresh initializeGL() call, this one) whenever its effective top-level
	// window changes - see QOpenGLWidget's own docs. Qt-ADS's tab/dock
	// widget mechanics do this on Windows during ordinary tab switches with
	// multiple documents open (not observed on Linux); it's expected Qt
	// behavior, not a bug in the docking library. Per QOpenGLWidget's own
	// documented cleanup pattern, connect to the (about to die) context's
	// aboutToBeDestroyed() and release GPU objects while it's still current
	// - without this, the create-once/reuse-if-non-null objects below
	// (loadFloor()'s _floorPlane etc.) survive as dangling pointers into a
	// destroyed context, and the next reuse crashes reading through their
	// now-invalid resolved GL function pointers (glBindVertexArray et al).
	// Reconnected every initializeGL() call since context() is a new
	// QOpenGLContext instance each time. Saved so ~ViewportWidget() can
	// disconnect it before doing its own explicit cleanup - see the member's
	// own doc comment for why that ordering matters.
	_glContextAboutToBeDestroyedConnection =
		connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this]() {
			makeCurrent();
			releaseGLSceneResources();
		});

	createShaderPrograms();
	createFullscreenTriangle();
	// GpuResourcePhase::Controller first (SceneRenderController - shader/
	// primitive-owning, e.g. whiteTexture/debug placeholder textures) -
	// see IGpuContextResource.h/GpuResourceRegistry.h. On the very first
	// call this is a near-no-op (only &_renderCtrl/_transformGizmo are
	// registered yet, from the constructor); on every subsequent context
	// recreation this restores everything registered so far.
	_gpuResourceRegistry.restorePhase(GpuResourcePhase::Controller);
	restoreLoadedMeshGpuResources();
	// Decorations next (floor/skybox/axis cone/viewcube/light helpers/
	// clipping planes/transform gizmo) - must run before any of their
	// guarded construction sites further down in this function reach their
	// reuse branch (e.g. loadFloor()'s _floorPlane->setPlane() call),
	// which requires the VAO already restored. FramebufferAuxiliaries
	// (transmission/SSS buffers) last, since they're the least depended-on.
	_gpuResourceRegistry.restorePhase(GpuResourcePhase::Decorations);
	_gpuResourceRegistry.restorePhase(GpuResourcePhase::FramebufferAuxiliaries);

	qRegisterMetaType<AssImpMeshDataBatch>("AssImpMeshDataBatch");
	qRegisterMetaType<SceneUpAxis>("SceneUpAxis");
	qRegisterMetaType<const aiScene*>("const aiScene*");
	qRegisterMetaType<std::vector<GPULight>>("std::vector<GPULight>");

	if (!_ktx2Loader.initializeOpenGL())
	{
		qWarning() << "ViewportWidget::initializeGL - Failed to initialize KTX2 loader";
	}
	_gpuCapabilities = KTX2Loader::detectGPUCapabilities();

	_assimpModelLoader = new AssImpModelLoader();
	_assimpModelLoader->setImageTextureUploader(
		[this](Material::Texture& texture, const QImage& image) -> unsigned int {
			TextureSamplerSettings samplers{ texture.wrapS, texture.wrapT, texture.minFilter, texture.magFilter };
			if (QThread::currentThread() != this->thread())
			{
				unsigned int result = 0;
				const QImage imageCopy = image;
				QMetaObject::invokeMethod(this, [this, &result, imageCopy, samplers]() {
					result = uploadDecodedTextureImage(imageCopy, samplers);
					}, Qt::BlockingQueuedConnection);
				return result;
			}
			return uploadDecodedTextureImage(image, samplers);
		});
	_assimpModelLoader->setKtx2TextureUploader(
		[this](const QString& path, const std::string& mapType, Material::Texture& texture) -> unsigned int {
			TextureSamplerSettings samplers{ texture.wrapS, texture.wrapT, texture.minFilter, texture.magFilter };
			if (QThread::currentThread() != this->thread())
			{
				unsigned int result = 0;
				const QString pathCopy = path;
				const std::string mapTypeCopy = mapType;
				QMetaObject::invokeMethod(this, [this, &result, pathCopy, mapTypeCopy, samplers]() {
					result = uploadKtx2TextureImage(pathCopy, mapTypeCopy, samplers);
					}, Qt::BlockingQueuedConnection);
				return result;
			}
			return uploadKtx2TextureImage(path, mapType, samplers);
		});
	_assimpModelLoader->setUVDecisionCallback(
		[this](int totalTriangles, UVMethod currentMethod) -> UVMethod {
			if (QThread::currentThread() != this->thread())
			{
				UVMethod result = currentMethod;
				QMetaObject::invokeMethod(this, [this, totalTriangles, currentMethod, &result]() {
					result = promptLargeModelUVDecision(totalTriangles, currentMethod);
					}, Qt::BlockingQueuedConnection);
				return result;
			}
			return promptLargeModelUVDecision(totalTriangles, currentMethod);
		});
	connect(_assimpModelLoader, &AssImpModelLoader::fileReadProcessed, this, &ViewportWidget::showFileReadingProgress);
	connect(_assimpModelLoader, &AssImpModelLoader::verticesProcessed, this, &ViewportWidget::showMeshLoadingProgress);
	connect(_assimpModelLoader, &AssImpModelLoader::nodeMeshProgressUpdated, this, &ViewportWidget::showNodeMeshLoadingProgress);
	connect(this, &ViewportWidget::loadingAssImpModelCancelled, _assimpModelLoader, &AssImpModelLoader::cancelLoading);

	_renderCtrl.initLights();
	// Connect lights loading
	connect(_assimpModelLoader, &AssImpModelLoader::lightsLoaded,
		this, [this](const GltfLightData& lights) {
			setParsedLights(lights);
		});

	const std::string path = PathUtils::getDataDirectory().toStdString() + "/";
	// Text rendering
	_renderCtrl.textShader()->bind();
	_textRenderer = new TextRenderer(_renderCtrl.textShader(), width(), height());
	_textRenderer->Load(path + "fonts/arial.ttf", 20);
	_axisTextRenderer = new TextRenderer(_renderCtrl.textShader(), width(), height());
	_axisTextRenderer->Load(path + "fonts/arialbd.ttf", 16);
	_renderCtrl.textShader()->release();

	createCappingPlanes();

	createLights();

	// Environment Mapping - allowCacheReuse=true: this is initializeGL()'s
	// context-recreation path (see loadEnvMap()'s own doc comment).
	loadEnvMap(true);
	// IBL Map - allowCacheReuse=true: this is initializeGL()'s context-
	// recreation path, the one place where "the IBL maps already survived
	// under a shared context" is actually a valid reason to skip
	// regenerating them (see loadIrradianceMap()'s own doc comment).
	loadIrradianceMap(true);

	// Load preset environment maps (Studio, Outdoor, Office)
	const QString dataDir = PathUtils::getDataDirectory();
	const bool reuseSharedControllerTextures = IGpuContextResource::contextsAreShared();

	// Load preset environment maps (Studio, Outdoor, Office)
	// Each preset loads an HDR file, converts to cubemap, and generates IBL maps (irradiance + prefilter)
	{
		if (!reuseSharedControllerTextures || _renderCtrl.studioEnvironmentMap() == 0 ||
			_renderCtrl.studioIrradianceMap() == 0 || _renderCtrl.studioPrefilterMap() == 0 ||
			_renderCtrl.studioSheenPrefilterMap() == 0)
		{
			QString studioHDRPath = dataDir + "/textures/envmap/skyboxes/HDRI/studio.hdr";
			_renderCtrl.setStudioEnvironmentMap(loadPresetEnvironmentMap(studioHDRPath));
			if (_renderCtrl.studioEnvironmentMap())
			{
				GLuint irr = 0, pf = 0, spf = 0;
				generatePresetIBLMaps(_renderCtrl.studioEnvironmentMap(), irr, pf, spf);
				_renderCtrl.setStudioIrradianceMap(irr);
				_renderCtrl.setStudioPrefilterMap(pf);
				_renderCtrl.setStudioSheenPrefilterMap(spf);
			}
		}
	}

	{
		if (!reuseSharedControllerTextures || _renderCtrl.outdoorEnvironmentMap() == 0 ||
			_renderCtrl.outdoorIrradianceMap() == 0 || _renderCtrl.outdoorPrefilterMap() == 0 ||
			_renderCtrl.outdoorSheenPrefilterMap() == 0)
		{
			QString outdoorHDRPath = dataDir + "/textures/envmap/skyboxes/HDRI/outdoor.hdr";
			_renderCtrl.setOutdoorEnvironmentMap(loadPresetEnvironmentMap(outdoorHDRPath));
			if (_renderCtrl.outdoorEnvironmentMap())
			{
				GLuint irr = 0, pf = 0, spf = 0;
				generatePresetIBLMaps(_renderCtrl.outdoorEnvironmentMap(), irr, pf, spf);
				_renderCtrl.setOutdoorIrradianceMap(irr);
				_renderCtrl.setOutdoorPrefilterMap(pf);
				_renderCtrl.setOutdoorSheenPrefilterMap(spf);
			}
		}
	}

	{
		if (!reuseSharedControllerTextures || _renderCtrl.officeEnvironmentMap() == 0 ||
			_renderCtrl.officeIrradianceMap() == 0 || _renderCtrl.officePrefilterMap() == 0 ||
			_renderCtrl.officeSheenPrefilterMap() == 0)
		{
			QString officeHDRPath = dataDir + "/textures/envmap/skyboxes/HDRI/office.hdr";
			_renderCtrl.setOfficeEnvironmentMap(loadPresetEnvironmentMap(officeHDRPath));
			if (_renderCtrl.officeEnvironmentMap())
			{
				GLuint irr = 0, pf = 0, spf = 0;
				generatePresetIBLMaps(_renderCtrl.officeEnvironmentMap(), irr, pf, spf);
				_renderCtrl.setOfficeIrradianceMap(irr);
				_renderCtrl.setOfficePrefilterMap(pf);
				_renderCtrl.setOfficeSheenPrefilterMap(spf);
			}
		}
	}

	// Shadow mapping
	loadFloor();

	// _renderCtrl's whiteTexture is (re)created by
	// _gpuResourceRegistry.restorePhase(GpuResourcePhase::Controller) above
	// (see SceneRenderController::restoreGpuResources()) - a separate
	// unconditional call here used to run a second time on every pass,
	// leaking the first handle every time (glGenTextures with no guard).

	// First time only: _gpuResourceRegistry.restorePhase(FramebufferAuxiliaries)
	// (called earlier in this function - see the phase-restore block above)
	// already did this work via the registered lambdas below on every
	// SUBSEQUENT pass, using width()/height() evaluated fresh at call time -
	// calling these two again here unconditionally would be a redundant
	// double-call with no cleanup in between.
	if (!_bufferGpuResourcesRegistered)
	{
		initTransmissionBuffer();
		initSSSBuffer();
		_gpuResourceAdapters.push_back(std::make_unique<LambdaGpuResource>(
			[this] { _renderCtrl.cleanupTransmissionBuffer(); },
			[this] { initTransmissionBuffer(); }));
		_gpuResourceRegistry.add(_gpuResourceAdapters.back().get(), GpuResourcePhase::FramebufferAuxiliaries);
		_gpuResourceAdapters.push_back(std::make_unique<LambdaGpuResource>(
			[this] { _renderCtrl.cleanupSSSBuffer(); },
			[this] { initSSSBuffer(); }));
		_gpuResourceRegistry.add(_gpuResourceAdapters.back().get(), GpuResourcePhase::FramebufferAuxiliaries);
		_bufferGpuResourcesRegistered = true;
	}

	float size = 15;
	if (_axisCone == nullptr)
	{
		// Initial size only - kept in sync afterward by setParameters()
		// call sites elsewhere, so a stale size from construct-once is safe.
		_axisCone = new ConeRenderable(_renderCtrl.axisShader(), _viewCtrl.viewRange() / size / 15, _viewCtrl.viewRange() / size / 5, 8u, 1u);
		registerDecorationGpuResource(_axisCone, [this] { return _renderCtrl.axisShader(); });
	}
	if (_viewCube == nullptr)
	{
		_viewCube = new ViewCubeMesh(_renderCtrl.viewCubeShader(), 1.0f);
		registerDecorationGpuResource(_viewCube, [this] { return _renderCtrl.viewCubeShader(); });
	}
	initializeViewCubeLabels();

	// Set lighting information
	_renderCtrl.fgShader()->bind();
	syncDefaultLightColorUniforms();
	_renderCtrl.fgShader()->setUniformValue("lightSource.position", _lightPosition + _renderCtrl.lightOffset());
	_renderCtrl.fgShader()->setUniformValue("lightModel.ambient", QVector3D(0.2f, 0.2f, 0.2f));
	_renderCtrl.fgShader()->setUniformValue("Line.Width", 0.75f);
	_renderCtrl.fgShader()->setUniformValue("Line.Color", QVector4D(0.05f, 0.0f, 0.05f, 1.0f));
	_renderCtrl.fgShader()->setUniformValue("envMap", 1);
	_renderCtrl.fgShader()->setUniformValue("shadowMap", 2);
	_renderCtrl.fgShader()->setUniformValue("irradianceMap", 3);
	_renderCtrl.fgShader()->setUniformValue("prefilterMap", 4);
	_renderCtrl.fgShader()->setUniformValue("brdfLUT", 5);
	_renderCtrl.fgShader()->setUniformValue("sheenPrefilterMap", 7);
	_renderCtrl.fgShader()->setUniformValue("charlieLUT", 8);
	_renderCtrl.fgShader()->setUniformValue("sheenELUT",  9);
	_renderCtrl.fgShader()->setUniformValue("sheenPrefilterMipLevels", (int)_renderCtrl.sheenPrefilterMipLevels());
	_renderCtrl.fgShader()->setUniformValue("prefilterMipLevels", (int)_renderCtrl.prefilterMipLevels());
	_renderCtrl.fgShader()->setUniformValue("transmissionSceneTexture", 32);
	_renderCtrl.fgShader()->setUniformValue("transmissionDepthTexture", 33);
	_renderCtrl.fgShader()->setUniformValue("sssDiffuseTexture", 37);
	_renderCtrl.fgShader()->setUniformValue("sssDepthTexture", 38);
	_renderCtrl.fgShader()->setUniformValue("displayMode", displayModeShaderInt(_displayMode));
	_renderCtrl.fgShader()->setUniformValue("renderingMode", static_cast<int>(_renderCtrl.renderingMode()));
	_renderCtrl.fgShader()->setUniformValue("realismEnabled", _realismEnabled);
	_renderCtrl.fgShader()->setUniformValue("shadingNormalMode", static_cast<int>(_shadingNormalMode));
	_renderCtrl.fgShader()->setUniformValue("selectionHighlighting", _selectionHighlighting);

	updateEnvMapRotationMatrix();

	_renderCtrl.debugShader()->bind();
	_renderCtrl.debugShader()->setUniformValue("depthMap", 0);

	QMatrix4x4 identityViewMatrix;
	identityViewMatrix.setToIdentity();
	_viewCtrl.setViewMatrix(identityViewMatrix);
	glEnable(GL_DEPTH_TEST);

	glClearColor(0.0f, 0.0f, 0.0f, 1.f);

	// Debug placeholder textures for TextureDebugPanel (debugNeutralTex()/
	// debugNormalTex()/debugBlackTex()) are created by
	// _gpuResourceRegistry's Controller-phase restore above (see
	// SceneRenderController::restoreGpuResources() ->
	// initDebugPlaceholderTextures()) - moved there so they're properly
	// released/recreated across context recreation instead of leaking.

	_renderCtrl.setOpenGLInitialized(true);
	// Deferred, not called synchronously here: loadRenderSettings()
	// reentrantly triggers setDisplayMode()/onRenderingModeSelected(),
	// whose signals reach VisualizationEnvironmentPanel::
	// onDisplayModeChanged() and touch GL-dependent render state (floor/
	// cube geometry, etc.) that isn't actually fully constructed yet at
	// this exact point - despite isOpenGLInitialized() already reporting
	// true (set on the line above). Confirmed via a real crash
	// (CubeRenderable::setSize() corrupting a std::vector inside
	// updateFloorGeometry(), reached through this exact chain) that
	// guarding each individual downstream call isn't sustainable: an
	// EARLIER guard was already added to onDisplayModeChanged() for this
	// same reentrancy and still wasn't enough once a DIFFERENT GL-dependent
	// object was the one actually touched. Running this on the next event
	// loop iteration instead means it only starts once initializeGL() has
	// genuinely finished constructing everything - none of the code below
	// this point depends on loadRenderSettings() having already run.
	QTimer::singleShot(0, this, [this]() { loadRenderSettings(); });

	// Seed the default navigation/camera mode. setCameraMode() calls setProjection()
	// internally for Fly/FirstPerson, which touches shader state — must run after GL init.
	{
		QSettings navModeSettings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
		const int navModeIdx = navModeSettings.value("navigationModeComboBox", 0).toInt();
		if (navModeIdx == 1)
			setCameraMode(Camera::CameraMode::Fly);
		else if (navModeIdx == 2)
			setCameraMode(Camera::CameraMode::FirstPerson);
		// index 0 (Orbit) needs no call — it is already the Camera's default mode.
	}

	// Keep SceneRuntime's parsed-light baseline in sync with SceneGraph whenever
	// a file's light data is added or removed (multi-model scene support).
	if (_viewer && _viewer->sceneGraph())
	{
		connect(_viewer->sceneGraph(), &SceneGraph::lightDataChanged,
		        this, &ViewportWidget::onSceneLightDataChanged,
		        Qt::UniqueConnection);
		connect(_viewer->sceneGraph(), &SceneGraph::structureChanged,
		        this, &ViewportWidget::onSceneStructureChanged,
		        Qt::UniqueConnection);
	}

	// Retry the fallback light now that a GL context is actually current -
	// see refreshFallbackLight()'s own doc comment. On an empty/just-launched
	// scene, ModelViewer::showEvent() reaches setLightOffset()/
	// refreshFallbackLight() before this initializeGL() has ever run, so that
	// first attempt bails out with nothing created.
	refreshFallbackLight();

	// Same story for the startup skybox preset - see setSkyBoxTextureFolder()'s
	// own doc comment. Its early attempt (queued during VisualizationEnvironmentPanel's
	// construction, pumped by main()'s splash-screen processEvents() calls)
	// only recorded the folder; actually load it now that GL is ready.
	if (!_renderCtrl.currentSkyboxFolder().isEmpty())
		setSkyBoxTextureFolder(_renderCtrl.currentSkyboxFolder());
}

void ViewportWidget::resizeGL(int width, int height)
{
	if (!_renderCtrl.isOpenGLInitialized())
		return;

	float w = (float)width;
	float h = (float)height;
	const qreal dpr = devicePixelRatioF();
	const int fbWidth = std::max(1, static_cast<int>(width * dpr));
	const int fbHeight = std::max(1, static_cast<int>(height * dpr));
	const bool framebufferSizeChanged =
		fbWidth != _rayTracedFramebufferWidth ||
		fbHeight != _rayTracedFramebufferHeight;
	_rayTracedFramebufferWidth = fbWidth;
	_rayTracedFramebufferHeight = fbHeight;

	if (_selectionManager)
		_selectionManager->resizeFBOResources(width, height);

	glViewport(0, 0, w, h);
	_viewCtrl.setViewportMatrix(w, h);

	_primaryCamera->setScreenSize(w, h);
	_primaryCamera->setViewRange(_viewCtrl.viewRange());
	// Keep the scene radius in the camera up-to-date so that the perspective
	// far plane always covers the full scene regardless of zoom depth.
	_primaryCamera->setSceneRadius(_viewCtrl.boundingSphere().getRadius());
	if (_viewCtrl.projection() == ViewProjection::ORTHOGRAPHIC)
	{
		_primaryCamera->setProjectionType(Camera::ProjectionType::ORTHOGRAPHIC);		
	}
	else
	{
		_primaryCamera->setProjectionType(Camera::ProjectionType::PERSPECTIVE);		
	}
	_viewCtrl.syncMatricesFromCamera(*_primaryCamera);

	// Resize the text frame
	_textRenderer->setWidth(width);
	_textRenderer->setHeight(height);
	QMatrix4x4 projection;
	projection.ortho(QRect(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)));
	_renderCtrl.textShader()->bind();
	_renderCtrl.textShader()->setUniformValue("projection", projection);
	_renderCtrl.textShader()->release();

	resizeTransmissionBuffer(width, height);
	resizeSSSBuffer(width, height);

	_rtSession.setResolution(fbWidth, fbHeight);
	// Only a REAL framebuffer-size change should invalidate PT
	// accumulation. Many camera-motion code paths call resizeGL(width(),
	// height()) manually just to refresh matrices/projection state; treating
	// those synthetic calls as true resizes was immediately tearing down the
	// interactive PT session and forcing raster/PBR back on screen.
	if (_rtInteractionCtrl->armed() && framebufferSizeChanged)
		_rtInteractionCtrl->notifyResize(); // old accumulation no longer matches the new resolution

	update();
}

void ViewportWidget::paintGL()
{
	if (!_renderCtrl.isOpenGLInitialized())
		return;

	// Pull the latest interactive PT frame BEFORE the raster pass so the
	// same published camera pose can drive both the presenter overlay and
	// the raster skybox drawn underneath it in this paint.
	if (_rtInteractionCtrl->armed() && _rayTracedInteractiveActive &&
		effectiveRayTracingEnginePreference() == RtRayTracingEnginePreference::GPU)
	{
		// RtInteractiveRenderer::tick() does at most one of "check
		// completion"/"submit next launch" per call and never blocks (see
		// its own doc comment); pollCompletedFrame() then hands back
		// whatever's currently displayable, generation-gated so a paint
		// that hasn't seen a new completed chunk since the last one is free.
		if (_rtInteractiveRendererSnapshot)
			_rtInteractiveRenderer.tick(_rtInteractiveRendererSnapshot->environment,
				_rtInteractiveRendererSnapshot->shadowsEnabled, _rtInteractiveRendererSnapshot->selfShadowsEnabled,
				_ptEnvImportanceSamplingEnabled);

		int frameWidth = 0;
		int frameHeight = 0;
		RtCamera frameCamera;
		uint64_t deviceGeneration = 0;
		void* deviceFrame = _rtInteractiveRenderer.pollCompletedFrame(frameWidth, frameHeight, frameCamera, deviceGeneration);
		if (deviceFrame && deviceGeneration != _lastConsumedRtInteractiveRendererGeneration)
		{
			bool presented = _rtPresenter.uploadFromDevice(deviceFrame, frameWidth, frameHeight);
			if (!presented)
			{
				// CUDA-GL interop registration/copy failed (unusual - see
				// uploadFromDevice()'s doc comment) - fall back to reading
				// this same already-rendered device frame back to the host
				// once and presenting it the ordinary way, rather than
				// leaving the interactive preview blank.
				std::vector<glm::vec3> hostFrame;
				std::vector<float> hostAlpha;
				if (_rtInteractiveTracer.readbackDeviceRGBABuffer(deviceFrame, frameWidth, frameHeight, hostFrame, hostAlpha))
				{
					_rtPresenter.upload(hostFrame, frameWidth, frameHeight, &hostAlpha);
					presented = true;
				}
			}
			if (presented)
			{
				_lastConsumedRtInteractiveRendererGeneration = deviceGeneration;
				_rtInteractivePreviewCamera = frameCamera;
				_rtInteractivePreviewCameraValid = true;
			}
		}
	}

	const QColor& rc_top = _renderCtrl.bgTopColor();
	const QColor& rc_bot = _renderCtrl.bgBotColor();
	QColor topColor = !_sceneRuntime.visibleSwapped() ? rc_top : QColor::fromRgbF(1.0f - rc_top.redF(),
		1.0f - rc_top.greenF(), 1.0f - rc_top.blueF(),
		rc_top.alphaF());
	QColor botColor = !_sceneRuntime.visibleSwapped() ? rc_bot : QColor::fromRgbF(1.0f - rc_bot.redF(),
		1.0f - rc_bot.greenF(), 1.0f - rc_bot.blueF(),
		rc_bot.alphaF());
	try
	{
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

		if (_renderCtrl.bgStyleIndex() == 1) // Solid
			gradientBackground(topColor.redF(), topColor.greenF(), topColor.blueF(), topColor.alphaF(),
				topColor.redF(), topColor.greenF(), topColor.blueF(), topColor.alphaF(), 0);
		else // Gradient
			gradientBackground(topColor.redF(), topColor.greenF(), topColor.blueF(), topColor.alphaF(),
				botColor.redF(), botColor.greenF(), botColor.blueF(), botColor.alphaF(), _renderCtrl.gradientStyle());

		_renderCtrl.fgShader()->bind();
		_renderCtrl.punctualLights()->bind(_renderCtrl.fgShader()->programId());
		_renderCtrl.fgShader()->setUniformValue("lightCount", _renderCtrl.punctualLights()->getLightCount());

		QMatrix4x4 identityModelMatrix;
		identityModelMatrix.setToIdentity();
		_viewCtrl.setModelMatrix(identityModelMatrix);
		if (_viewCtrl.multiViewActive())
		{
			renderMultiView(topColor, botColor);
		}
		else
		{
			renderSingleView(topColor, botColor);
		}

		// Self-healing watchdog: armed, but neither counting down to a settle
		// nor actually tracing nor showing a result - something (a visibility
		// transition whose exact platform event didn't fire/arrive as
		// expected - alt-tab, window-manager quirks, etc.) left the mode
		// stuck idle. paintGL() keeps running regardless of *why* that
		// happened, so checking here catches it unconditionally instead of
		// depending on correctly predicting every platform-specific
		// visibility/focus event that should have restarted it.
		//
		// MUST also respect _rayTracedResumeWarmUpTimer, not just
		// _rayTracedIdleTimer: every GPU-backend teardown (a scene mutation,
		// not a live drag - see RtInteractionController::
		// notifySceneContentMutated()) deliberately leaves _rayTracedIdleTimer
		// untouched for GPU
		// and instead arms this debounced resume timer to bring the
		// interactive accumulator back via onRayTracedResumeWarmUpTimeout().
		// Without this check, this watchdog saw idleTimerActive==false
		// immediately (it was never armed for GPU) and fired on the very next
		// paintGL(), before the 400ms debounce had any chance to fire -
		// starting the settled _ptOptixSession, which itself calls
		// _rtPresenter.invalidate() and restarts from scratch every time it's
		// entered. Since this watchdog re-fires every frame (nothing ever got
		// a chance to publish a first frame before being invalidated again),
		// this was a genuine livelock: dozens of full OptiX session
		// start/stop cycles per second, indefinitely, until an unrelated
		// event (e.g. a real camera drag) happened to break it.
		if (_rtInteractionCtrl->armed() && !_rayTracedIdleTimer->isActive() &&
		    !(_rayTracedResumeWarmUpTimer && _rayTracedResumeWarmUpTimer->isActive()) &&
		    !rayTracedSessionRunning() && !_rtPresenter.hasFrame())
		{
			startRayTracedSession(); // bypass the idle countdown entirely - see applicationStateChanged handler for why
		}

		// Ray-traced overlay: drawn once the camera has settled (idle timer
		// no longer counting down) AND a
		// converged/converging frame has been published - OR, GPU/OptiX
		// only, while _rayTracedInteractiveActive is true (a reduced-
		// quality trace is running because the camera is actively moving -
		// see startInteractiveRayTracedGpuSession()). CPU/Embree never sets
		// _rayTracedInteractiveActive, so this reduces to exactly the
		// original settled-only condition for that backend. Drawn over the
		// just-rendered raster frame, before the viewcube/text overlay so
		// those still read on top of the ray-traced image too.
		if (_rtInteractionCtrl->armed() && (_rayTracedInteractiveActive || !_rayTracedIdleTimer->isActive()) && _rtPresenter.hasFrame())
		{
			// Interactive PT should present as a self-contained frame, not as
			// an alpha-blended model laid over a separately-rendered raster
			// background. With rough metallic materials in early passes, that
			// blend reads as the raster skybox/background "showing through" the
			// noisy PT sphere far more than the settled/dialog path does.
			//
			// Force opaque presentation for the entire interactive frame even
			// when a skybox is enabled: the PT frame already carries its own
			// background RGB, and presenting it opaquely avoids mixing two
			// independently-produced backgrounds in the same image.
			const bool forceOpaqueInteractive = _rayTracedInteractiveActive;
			_rtPresenter.draw(_renderCtrl.hdrToneMapping(), _renderCtrl.gammaCorrection(),
				_renderCtrl.screenGamma(), _renderCtrl.iblExposure(), static_cast<int>(_renderCtrl.toneMappingMode()),
				/*forceOpaque=*/forceOpaqueInteractive);
		}

		if (!_capturingCleanFrame)
		{
			// While an interactive PT frame is being shown, draw the ViewCube
			// AND both axis trihedrons against the SAME camera pose that
			// frame was actually rendered with (_rtInteractivePreviewCamera -
			// see its own doc comment), not the always-current _viewCtrl
			// matrices - same reasoning as render()'s identical
			// interactivePtSkyboxView construction for the raster skybox.
			// Interactive PT's own accumulation/publish lag means the
			// displayed model can be one or more ticks behind the live
			// camera during an active drag; drawing these from the live
			// camera instead made them visibly rotate ahead of the
			// still-catching-up model (confirmed for the ViewCube - it only
			// ever reflects rotation, never zoom/pan, so a rotation-only lag
			// like this is exactly what was visible).
			QMatrix4x4 interactivePtAxisView;
			const QMatrix4x4* axisViewOverride = nullptr;
			if (_rayTracedInteractiveActive && _rtInteractivePreviewCameraValid)
			{
				const glm::vec3& f = _rtInteractivePreviewCamera.forward;
				const glm::vec3& r = _rtInteractivePreviewCamera.right;
				const glm::vec3& u = _rtInteractivePreviewCamera.up;
				const glm::vec3& p = _rtInteractivePreviewCamera.position;
				interactivePtAxisView.setToIdentity();
				interactivePtAxisView(0, 0) = r.x; interactivePtAxisView(0, 1) = r.y; interactivePtAxisView(0, 2) = r.z;
				interactivePtAxisView(0, 3) = -(r.x * p.x + r.y * p.y + r.z * p.z);
				interactivePtAxisView(1, 0) = u.x; interactivePtAxisView(1, 1) = u.y; interactivePtAxisView(1, 2) = u.z;
				interactivePtAxisView(1, 3) = -(u.x * p.x + u.y * p.y + u.z * p.z);
				interactivePtAxisView(2, 0) = -f.x; interactivePtAxisView(2, 1) = -f.y; interactivePtAxisView(2, 2) = -f.z;
				interactivePtAxisView(2, 3) = (f.x * p.x + f.y * p.y + f.z * p.z);
				axisViewOverride = &interactivePtAxisView;
			}

			drawViewCube(axisViewOverride);

			// Axis trihedrons (center + corner): drawn here, after the path-
			// traced overlay above, for the same reason the ViewCube is -
			// interactive PT's force-opaque composite completely overwrites
			// the raster frame underneath it, so anything only drawn during
			// the earlier raster pass (render()/renderSingleView()) is
			// invisible for as long as PT is displaying. Multi-view mode
			// still draws its own per-viewport center-axis indicator inside
			// render() instead (no single global camera to draw this one
			// against, and no PT overlay to be wiped out by there).
			if (!_viewCtrl.multiViewActive())
			{
				if (_viewCtrl.showAxis() && _viewCtrl.userShowAxisOverride())
					drawAxis(_primaryCamera, axisViewOverride);
				if (_viewCtrl.userShowCornerAxisOverride())
					drawCornerAxis(_viewCtrl.cornerAxisPosition(), axisViewOverride);
			}

		}
	}
	catch (const std::exception& ex)
	{
		std::cout << "Exception raised in ViewportWidget::paintGL\n" << ex.what() << std::endl;
	}

	// Stomp alpha to fully opaque across the whole frame, right before Qt
	// composites this widget's FBO into the top-level window surface.
	// PBR/RT blending (floor-plane transparency, glass/transmission, and
	// RtPresenter's GL_SRC_ALPHA compositing) leaves per-pixel destination
	// alpha < 1 in the framebuffer. Requesting QSurfaceFormat::setAlphaBufferSize(0)
	// wasn't enough - Wayland EGL only offers RGBA configs on this driver, so the
	// surface still carries a live alpha channel that the Wayland compositor then
	// alpha-blends against the desktop, showing whatever's behind the window
	// through those pixels. The color mask restricts this to the alpha channel
	// only, so it can't affect the RGB result already rendered this frame.
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	// For testing rendered shadow map
	/*_renderCtrl.debugShader()->bind();
	_renderCtrl.debugShader()->setUniformValue("near_plane", 1.0f);
	_renderCtrl.debugShader()->setUniformValue("far_plane", _viewCtrl.viewRange());
	_renderCtrl.debugShader()->setUniformValue("screenSize", QVector2D(width(), height()));
	_renderCtrl.debugShader()->setUniformValue("transmissionColorTexture", 32);
	_renderCtrl.debugShader()->setUniformValue("transmissionDepthTexture", 33);	
	renderQuad();*/

	//_renderCtrl.brdfShader()->bind();
	//renderQuad();
}

void ViewportWidget::updateView()
{
	update();
}

void ViewportWidget::setSkyBoxTextureFolder(QString folder)
{
	QApplication::setOverrideCursor(Qt::WaitCursor);

	// Store the folder path for later regeneration in detached contexts
	_renderCtrl.setCurrentSkyboxFolder(folder);

	// Same early-startup timing issue as refreshFallbackLight() (see its doc
	// comment): VisualizationEnvironmentPanel's preset combo box population
	// can reach this via a queued onLoadSkyBoxPresetMaps() call that gets
	// pumped by main()'s splash-screen processEvents() calls, before this
	// widget's initializeGL() has ever run. The folder is already recorded
	// above, so initializeGL() re-issues this call once a GL context exists.
	if (!_renderCtrl.isOpenGLInitialized())
	{
		QApplication::restoreOverrideCursor();
		return;
	}

	// File pattern map: match flexible identifiers to cube map indices
	QMap<QString, int> faceMap = {
		{"right", 0}, {"posx", 0}, {"px", 0}, {"rt", 0},
		{"left", 1},  {"negx", 1}, {"nx", 1}, {"lt", 1},
		{"top", 2},   {"posy", 2}, {"py", 2}, {"up", 2},
		{"bottom", 3},{"negy", 3}, {"ny", 3}, {"dn", 3}, {"down", 3},
		{"front", 4}, {"posz", 4}, {"pz", 4}, {"ft", 4},
		{"back", 5},  {"negz", 5}, {"nz", 5}, {"bk", 5}
	};

	QStringList faceNames = { "right", "left", "top", "bottom", "front", "back" };
	QStringList supportedFormats = { "jpeg", "jpg", "png", "bmp", "psd", "tga", "gif", "hdr", "exr", "pic", "pnm" };

	QStringList files = QDir(folder).entryList(QDir::Files | QDir::Readable, QDir::Name);

	if (files.isEmpty())
	{
		QMessageBox::critical(this, tr("Error"), tr("No files found in selected folder."));
		QApplication::restoreOverrideCursor();
		return;
	}

	makeCurrent();
	glBindTexture(GL_TEXTURE_CUBE_MAP, _renderCtrl.environmentMap());

	// Temp holders
	QString skyboxImages[6];
	bool loadedFaces[6] = { false };

	// Try to match each file name with a face using the map
	for (const QString& file : files)
	{
		QString name = QFileInfo(file).baseName().toLower();
		for (auto it = faceMap.constBegin(); it != faceMap.constEnd(); ++it)
		{
			if (name.contains(it.key()) && !loadedFaces[it.value()])
			{
				skyboxImages[it.value()] = folder + "/" + file;
				loadedFaces[it.value()] = true;
				break;
			}
		}
	}

	// Check if all faces were loaded
	bool allFacesLoaded = std::all_of(std::begin(loadedFaces), std::end(loadedFaces),
		[](bool b) { return b; });

	if (!allFacesLoaded)
	{
		// Fallback: try single HDR/EXR cubemap image
		QStringList hdrFiles = QDir(folder).entryList(QStringList() << "*.hdr" << "*.exr", QDir::Files);
		if (!hdrFiles.isEmpty())
		{
			QString fallbackHDR = folder + "/" + hdrFiles.first();
			if (loadCubemapFromSingleHDR(fallbackHDR))
			{				
				loadIrradianceMap();
				update();
				QApplication::restoreOverrideCursor();
				notifyRayTracedSceneMutated();
				return;
			}
			else
			{
				QMessageBox::critical(this, tr("Error"),
					tr("Failed to load fallback HDR cubemap from:\n") + fallbackHDR);
			}
		}
		else
		{
			QMessageBox::critical(this, tr("Error"),
				tr("No valid 6-face skybox images or fallback HDR file found in folder."));
		}

		QApplication::restoreOverrideCursor();
		return;
	}

	// Ensure all 6 faces are found
	for (int i = 0; i < 6; ++i)
	{
		if (!loadedFaces[i])
		{
			QString missingFace = faceNames[i];
			QString title = tr("Error");
			QString message = tr("Missing skybox face: %1\nExpected files should include identifiers like posx/negx or right/left, etc.")
				.arg(missingFace);
			QMessageBox::critical(this, title, message);

			QApplication::restoreOverrideCursor();
			return;
		}
	}

	// Load and upload each face
	for (int i = 0; i < 6; ++i)
	{
		int width, height, nrComponents;
		void* data = nullptr;
		std::string fileName = skyboxImages[i].toStdString();

		// An .exr face can only ever be read as float data (stb_image's
		// stbi_load() has no EXR support at all) - checked in ADDITION to
		// the HDRI toggle rather than instead of it, so an .hdr face still
		// goes through the exact same branch it always has.
		if (_renderCtrl.skyBoxTextureHDRI() || HdrImageLoader::isExr(fileName))
		{
			data = static_cast<float*>(HdrImageLoader::load(fileName, width, height, nrComponents, false));
			if (!data) goto failure;
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB32F,
				width, height, 0, GL_RGB, GL_FLOAT, data);
		}
		else
		{
			// Reset explicitly rather than assuming it's already false -
			// HdrImageLoader::load() (used above, and by other call sites
			// elsewhere) sets this global stb_image flag as needed for its
			// OWN read and never restores it afterward, and the EXR path
			// doesn't touch it at all (handles flipping itself), so it
			// can't be trusted to already be in the state this LDR call
			// expects.
			stbi_set_flip_vertically_on_load(false);
			data = static_cast<unsigned char*>(stbi_load(fileName.c_str(), &width, &height, &nrComponents, 0));
			if (!data) goto failure;
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
				width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
		}
				
		stbi_image_free(data);
		continue;

	failure:
		{
			QMessageBox::critical(this, tr("Error"), tr("Failed to load skybox face:\n") + QString::fromStdString(fileName));
			QApplication::restoreOverrideCursor();
			return;
		}
	}

	// Generate mipmaps ONCE after all 6 faces are loaded
	glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

	// Setup sampler parameters
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

	loadIrradianceMap();
	update();
	QApplication::restoreOverrideCursor();
	notifyRayTracedSceneMutated();
}

bool ViewportWidget::loadCubemapFromSingleHDR(const QString& filePath)
{
	int imgWidth, imgHeight, channels;
	float* data = HdrImageLoader::load(filePath.toStdString(), imgWidth, imgHeight, channels, false);
	if (!data)
	{
		qWarning() << "Failed to load HDR file:" << filePath;
		return false;
	}

	// Check for equirectangular first (2:1 aspect ratio)
	if (imgWidth == 2 * imgHeight)
	{		
		stbi_image_free(data); // Free, we'll reload in conversion function
		return convertEquirectangularToCubemap(filePath);
	}

	glBindTexture(GL_TEXTURE_CUBE_MAP, _renderCtrl.environmentMap());

	int faceSize = 0;
	QPoint faceOffsets[6];
	bool validLayout = false;

	// --- Detect 6x1 strip ---
	if (imgWidth % 6 == 0 && imgHeight == imgWidth / 6)
	{
		faceSize = imgHeight;
		for (int i = 0; i < 6; ++i)
			faceOffsets[i] = QPoint(i * faceSize, 0);
		validLayout = true;
	}

	// --- Detect 1x6 vertical strip ---
	else if (imgHeight % 6 == 0 && imgWidth == imgHeight / 6)
	{
		faceSize = imgWidth;
		for (int i = 0; i < 6; ++i)
			faceOffsets[i] = QPoint(0, i * faceSize);
		validLayout = true;
	}

	// --- Detect 3x2 grid ---
	else if (imgWidth % 3 == 0 && imgHeight % 2 == 0 && imgWidth / 3 == imgHeight / 2)
	{
		faceSize = imgWidth / 3;
		QPoint gridOffsets[6] = {
			{0, 0}, // +X
			{1, 0}, // -X
			{2, 0}, // +Y
			{0, 1}, // -Y
			{1, 1}, // +Z
			{2, 1}  // -Z
		};
		for (int i = 0; i < 6; ++i)
			faceOffsets[i] = QPoint(gridOffsets[i].x() * faceSize, gridOffsets[i].y() * faceSize);
		validLayout = true;
	}

	// --- Detect 4x3 or 3x4 cross layout ---
	else if ((imgWidth % 4 == 0 && imgHeight % 3 == 0 && imgWidth / 4 == imgHeight / 3) ||
		(imgWidth % 3 == 0 && imgHeight % 4 == 0 && imgWidth / 3 == imgHeight / 4))
	{
		// Handle 4x3 cross layout
		if (imgWidth / 4 == imgHeight / 3)
		{
			faceSize = imgWidth / 4;
			QPoint crossOffsets[6] = {
				{2, 1}, // +X
				{0, 1}, // -X
				{1, 0}, // +Y
				{1, 2}, // -Y
				{1, 1}, // +Z
				{3, 1}  // -Z
			};
			for (int i = 0; i < 6; ++i)
				faceOffsets[i] = QPoint(crossOffsets[i].x() * faceSize, crossOffsets[i].y() * faceSize);
			validLayout = true;
		}
		// Handle 3x4 cross layout (rotated cross)
		else if (imgWidth / 3 == imgHeight / 4)
		{
			faceSize = imgWidth / 3;
			QPoint crossOffsets[6] = {
				{2, 1}, // +X
				{0, 1}, // -X
				{1, 0}, // +Y
				{1, 2}, // -Y
				{1, 1}, // +Z
				{1, 3}  // -Z
			};
			for (int i = 0; i < 6; ++i)
				faceOffsets[i] = QPoint(crossOffsets[i].x() * faceSize, crossOffsets[i].y() * faceSize);
			validLayout = true;
		}
	}

	// --- Fallback: 1 face only (not a cubemap) ---
	if (!validLayout)
	{
		qWarning() << "Unsupported cubemap layout. Cannot determine layout from image dimensions.";
		stbi_image_free(data);
		return false;
	}

	// --- Upload faces to OpenGL ---
	for (int i = 0; i < 6; ++i)
	{
		const QPoint& offset = faceOffsets[i];
		float* facePixels = new float[faceSize * faceSize * channels];

		for (int y = 0; y < faceSize; ++y)
		{
			const float* src = data + ((offset.y() + y) * imgWidth + offset.x()) * channels;
			float* dst = facePixels + y * faceSize * channels;
			memcpy(dst, src, sizeof(float) * faceSize * channels);
		}

		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB32F,
			faceSize, faceSize, 0, GL_RGB, GL_FLOAT, facePixels);
		delete[] facePixels;
	}

	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

	stbi_image_free(data);
	return true;
}

bool ViewportWidget::convertEquirectangularToCubemap(const QString& filePath)
{
	return _renderCtrl.convertEquirectToCubemap(filePath, defaultFramebufferObject());
}


bool ViewportWidget::convertEquirectangularToCubemapQuad(const QString& filePath)
{
	return _renderCtrl.convertEquirectToCubemapQuad(filePath, defaultFramebufferObject());
}
void ViewportWidget::renderConversionCube()
{
	_renderCtrl.renderConversionCube();
}

QVector3D ViewportWidget::getLightPosition() const
{
	return _lightPosition;
}

void ViewportWidget::syncDefaultLightColorUniforms()
{
	// Keep a small internal ambient term for ADS while exposing a single editable light color.
	static constexpr float kDefaultLightAmbientFactor = 0.12f;

	const QVector4D& dlc = _renderCtrl.defaultLightColor();
	_ambientLight = QVector4D(
		dlc.x() * kDefaultLightAmbientFactor,
		dlc.y() * kDefaultLightAmbientFactor,
		dlc.z() * kDefaultLightAmbientFactor,
		dlc.w());
	_diffuseLight = dlc;
	_specularLight = dlc;

	_renderCtrl.fgShader()->setUniformValue("lightSource.ambient", _ambientLight.toVector3D());
	_renderCtrl.fgShader()->setUniformValue("lightSource.diffuse", _diffuseLight.toVector3D());
	_renderCtrl.fgShader()->setUniformValue("lightSource.specular", _specularLight.toVector3D());
}

void ViewportWidget::setLightOffset(const QVector3D& offset)
{
	_renderCtrl.setLightOffset(offset);
	_renderCtrl.setShadowMapNeedsInitialization(true);

	// refreshFallbackLight() re-positions the PERSISTENT PunctualLights
	// fallback light (see its own doc comment in ViewportWidget.h) - without
	// this it stays frozen at whatever position was last set by
	// updateFloorPlane() (scene load/resize/etc.), so buildRayTracedSnapshot()
	// would pick up its stale position via punctualLights()->getLights() AND
	// append a second, freshly-positioned keyLight on top - two lights
	// casting two different shadow directions in CPU/GPU ray tracing that
	// don't match raster's single, always-live shadow.
	refreshFallbackLight();

	// effectiveWorldLightPosition() (fed from this offset) becomes that
	// keyLight's position in buildRayTracedSnapshot() - baked into the
	// lights buffer GPU's revision-gated buildScene() only re-uploads on a
	// scene mutation, same bug class as useDefaultLights()/usePunctualLights()
	// (see their doc comment in ViewportWidget.h).
	notifyRayTracedSceneMutated();
}

QVector4D ViewportWidget::getDefaultLightColor() const
{
	return _renderCtrl.defaultLightColor();
}

void ViewportWidget::setDefaultLightColor(const QVector4D& defaultLightColor)
{
	_renderCtrl.setDefaultLightColor(defaultLightColor);
	_renderCtrl.fgShader()->bind();
	syncDefaultLightColorUniforms();
	_renderCtrl.fgShader()->release();

	// _diffuseLight (fed from this color) becomes the fallback key light's
	// color in buildRayTracedSnapshot() - same reasoning as setLightOffset()
	// right above.
	notifyRayTracedSceneMutated();
}

void ViewportWidget::syncCameraWorldUp()
{
	const QVector3D worldUp = CoordinateSystemHelper::currentWorldUpVector(_viewCtrl.cameraUpAxisZUp());

	if (_primaryCamera)
		_primaryCamera->setWorldUpVector(worldUp);
	if (_orthoViewsCamera)
		_orthoViewsCamera->setWorldUpVector(worldUp);
}

void ViewportWidget::rotateCurrentCameraAroundWorldX(float degrees)
{
	if (!_primaryCamera || std::abs(degrees) <= 0.0001f)
		return;

	const QQuaternion rotation = QQuaternion::fromAxisAndAngle(QVector3D(1.0f, 0.0f, 0.0f), degrees);
	const QVector3D rotatedViewDir = rotation.rotatedVector(_primaryCamera->getViewDir()).normalized();

	// Re-derive right and up from the rotated view direction and the current world-up
	// so the orbit camera carries no spurious roll into the new convention.
	const QVector3D worldUp = CoordinateSystemHelper::currentWorldUpVector(_viewCtrl.cameraUpAxisZUp());
	QVector3D correctedRight = QVector3D::crossProduct(rotatedViewDir, worldUp).normalized();
	if (correctedRight.lengthSquared() < 1e-6f)
	{
		// viewDir nearly collinear with worldUp — pick any orthogonal fallback
		const QVector3D fallback = (std::abs(worldUp.z()) < 0.9f)
			? QVector3D(0.0f, 0.0f, 1.0f) : QVector3D(1.0f, 0.0f, 0.0f);
		correctedRight = QVector3D::crossProduct(rotatedViewDir, fallback).normalized();
	}
	const QVector3D correctedUp = QVector3D::crossProduct(correctedRight, rotatedViewDir).normalized();

	setView(_primaryCamera->getPosition(), rotatedViewDir, correctedUp, correctedRight);

	if (_primaryCamera->getMode() == Camera::CameraMode::Fly ||
		_primaryCamera->getMode() == Camera::CameraMode::FirstPerson)
	{
		_primaryCamera->setYawPitchFromViewDir();
	}

	_viewCtrl.syncPoseFromCamera(*_primaryCamera);
	_viewCtrl.setCustomViewTargetRotation(_viewCtrl.currentRotation());
	_viewCtrl.setCustomViewAnimationActive(false);
	update();
}

QString ViewportWidget::sceneUpAxisLabel(SceneUpAxis sceneUpAxis) const
{
	return CoordinateSystemHelper::sceneUpAxisIsZUp(sceneUpAxis) ? tr("Z-Up") : tr("Y-Up");
}

void ViewportWidget::applyAutoOrientCameraConvention(SceneUpAxis sceneUpAxis)
{
	setCameraUpAxisZUp(CoordinateSystemHelper::sceneUpAxisIsZUp(sceneUpAxis));
}

void ViewportWidget::warnOnConflictingImportedSceneUpAxis(const QString& fileName, SceneUpAxis sceneUpAxis)
{
	const QString importedAxis = sceneUpAxisLabel(sceneUpAxis);
		const QString activeAxis = _viewCtrl.cameraUpAxisZUp() ? tr("Z-Up") : tr("Y-Up");
	const QString importedFileName = QFileInfo(fileName).fileName();

	QMessageBox::warning(
		this,
		tr("Camera Up-Axis Mismatch"),
		tr("The imported model \"%1\" uses %2, but this view is currently %3.\n\n"
		   "The active camera convention was left unchanged because this view already contains content.")
			.arg(importedFileName, importedAxis, activeAxis));
}

void ViewportWidget::setCameraUpAxisZUp(bool zUp, bool syncToolbar)
{
	if (_viewCtrl.cameraUpAxisZUp() == zUp)
	{
		if (syncToolbar && _viewToolbar)
			_viewToolbar->setCameraUpAxisZUp(zUp);
		syncCameraWorldUp();
		return;
	}

	_viewCtrl.setCameraUpAxisZUp(zUp);
	syncCameraWorldUp();
	rotateCurrentCameraAroundWorldX(zUp ? 90.0f : -90.0f);
	updateEnvMapRotationMatrix();
	updateFloorPlane();
	recalculateVisibleSceneStats(false);
	_renderCtrl.setShadowMapNeedsInitialization(true);
	initializeViewCubeLabels();

	// PT's own analytic floor plane (RtFloorParams::cameraUpAxisZUp, baked
	// into the GAS at buildScene() time - see RtSceneBuilder::
	// fillInfinitePlane()) only gets rebuilt when the scene REVISION bumps;
	// updateFloorPlane() above only refreshes the RASTER floor mesh. Without
	// this, PT keeps showing the floor at its old orientation/position until
	// some unrelated scene edit happens to bump the revision - this is also
	// a real camera rotation (rotateCurrentCameraAroundWorldX()) that PT
	// needs to know about regardless of the floor.
	notifyRayTracedSceneMutated();

	if (syncToolbar && _viewToolbar)
		_viewToolbar->setCameraUpAxisZUp(zUp);

	emit cameraUpAxisChanged(zUp);
}

void ViewportWidget::setViewMode(ViewMode mode)
{
	// Home / standard-view / axonometric changes are camera motion, but
	// deliberately DON'T take the interactive-GPU-PT path
	// (cameraInteracting=false instead of true) - unlike a live mouse drag
	// or inertia coast, this animation's per-tick delta doesn't decay toward
	// zero (see animateToRotation()/the slerp step), so the interactive
	// renderer's inherent one-tick-behind lag (see RtInteractiveRenderer's
	// design notes) shows up as a full-sized, objectionable jerk right at
	// the end of the transition instead of the imperceptibly small one
	// inertia's exponential decay masks. Plain raster/PBR for the whole
	// animation, settling into full-quality PT only once it's actually done,
	// reads as smoother than a low-spp interactive trace that stutters at
	// the finish - same behavior GPU already falls back to for every other
	// non-drag camera-affecting event (see this method's own doc comment).
	_rtInteractionCtrl->notifyCameraAnimationTick(); // kicks off the scripted animation that animateViewChange() ticks per-frame below
	update();

	if (!_animateViewTimer->isActive())
	{
		_keyboardNavTimer->stop();

		const QQuaternion q = CoordinateSystemHelper::standardViewRotation(_viewCtrl.cameraUpAxisZUp(), mode);
		const QMatrix4x4  m(q.toRotationMatrix());

		// Compute fit + projected visual centre from the *target* orientation so
		// that the orbit target is correct as soon as the rotation animation begins.
		// Only compute fit view range if there are visible meshes.
		// On an empty scene, keep the cached fit diameter at the current
		// view range so the zoom animation is a no-op while the
		// rotation animation still proceeds normally.
		const std::vector<int>& visibleIds = _sceneRuntime.currentVisibleObjectIds();
		if (!_sceneRuntime.meshStore().empty() && !visibleIds.empty())
		{
			QVector3D projCenter;
			_viewCtrl.setViewBoundingSphereDia(computeFitViewRange(
				m.row(0).toVector3D().normalized(),
				m.row(1).toVector3D().normalized(),
				-m.row(2).toVector3D().normalized(),
				&projCenter));
			_viewCtrl.setBoundingSphereCenter(projCenter);
		}
		else
		{
			_viewCtrl.setViewBoundingSphereDia(_viewCtrl.currentViewRange());
		}

		_viewCtrl.setCustomViewTargetRotation(q);
		_viewCtrl.setCustomViewAnimationActive(true);
		_animateViewTimer->start(5);
		_viewCtrl.setViewMode(mode);
		_viewCtrl.resetSlerpStep();
	}
}

void ViewportWidget::fitAll()
{
	// Fit-to-view is a camera move, not a scene mutation, but (like
	// setViewMode() above) deliberately does NOT take the interactive-GPU-PT
	// path - see that method's doc comment for why: this animation's
	// per-tick delta doesn't decay toward zero the way a live drag's inertia
	// coast does, so the interactive renderer's one-tick-behind lag ends the
	// transition with a visible jerk instead of a smooth settle.
	//
	// This one call covers BOTH branches below (the Fly/FirstPerson branch's
	// synchronous one-shot camera set, and the orbit branch's scripted
	// _animateFitAllTimer/animateFitAll() sequence) - mechanically identical
	// to notifyCameraAnimationTick() today (see RtInteractionController.h's
	// doc comment), so using the "jump" name here for the synchronous branch
	// doesn't change behavior for the animated one.
	_rtInteractionCtrl->notifyCameraJumpNonInteractive();
	update();

	// Guard: do nothing if the scene has no visible meshes.
	// Without this, computeFitViewRange() operates on degenerate bounds,
	// driving the view range to near-zero and hiding the trihedron.
	const std::vector<int>& visibleIds = _sceneRuntime.currentVisibleObjectIds();
	if (_sceneRuntime.meshStore().empty() || visibleIds.empty())
		return;

	if (_primaryCamera->getMode() == Camera::CameraMode::Fly ||
		_primaryCamera->getMode() == Camera::CameraMode::FirstPerson)
	{
		checkAndStopTimers();
		_keyboardNavTimer->stop();
		const QVector3D viewDir = _primaryCamera->getViewDir().normalized();
		const QVector3D upDir = _primaryCamera->getUpVector().normalized();
		const QVector3D rightDir = _primaryCamera->getRightVector().normalized();
		const std::vector<QVector3D> corners = collectVisibleCorners();
		if (corners.empty())
			return;

		const float aspect = std::max(static_cast<float>(width()) / std::max(1.0f, static_cast<float>(height())), 0.001f);
		const float halfFovY = qDegreesToRadians(_viewCtrl.FOV()) * 0.5f;
		const float tanHalfY = std::max(std::tan(halfFovY), 0.001f);
		const float tanHalfX = std::max((aspect >= 1.0f ? tanHalfY * aspect : tanHalfY), 0.001f);
		const float margin = 1.05f;

		float xMin_v = std::numeric_limits<float>::max();
		float xMax_v = -std::numeric_limits<float>::max();
		float yMin_v = std::numeric_limits<float>::max();
		float yMax_v = -std::numeric_limits<float>::max();
		float zMin_v = std::numeric_limits<float>::max();
		float zMax_v = -std::numeric_limits<float>::max();

		for (const QVector3D& c : corners)
		{
			const float xc = QVector3D::dotProduct(c, rightDir);
			const float yc = QVector3D::dotProduct(c, upDir);
			const float zc = QVector3D::dotProduct(c, viewDir);
			xMin_v = std::min(xMin_v, xc);  xMax_v = std::max(xMax_v, xc);
			yMin_v = std::min(yMin_v, yc);  yMax_v = std::max(yMax_v, yc);
			zMin_v = std::min(zMin_v, zc);  zMax_v = std::max(zMax_v, zc);
		}

		const float cx = (xMin_v + xMax_v) * 0.5f;
		const float cy = (yMin_v + yMax_v) * 0.5f;
		const float cz = (zMin_v + zMax_v) * 0.5f;
		const QVector3D projCenter = rightDir * cx + upDir * cy + viewDir * cz;

		float desiredDist = 0.0f;
		for (const QVector3D& c : corners)
		{
			const float xc_rel = QVector3D::dotProduct(c, rightDir) - cx;
			const float yc_rel = QVector3D::dotProduct(c, upDir) - cy;
			const float dc = QVector3D::dotProduct(c, viewDir) - cz;

			float req;
			if (aspect >= 1.0f)
				req = std::max(std::abs(xc_rel) / aspect, std::abs(yc_rel)) / tanHalfY - dc;
			else
				req = std::max(std::abs(xc_rel), std::abs(yc_rel) * aspect) / tanHalfY - dc;

			desiredDist = std::max(desiredDist, req);
		}
		desiredDist = std::max(desiredDist * margin, 0.001f);

		const float shiftFactor = std::min(1.05f / std::sin(halfFovY), 1.25f);
		_viewCtrl.setViewBoundingSphereDia(std::max(desiredDist / std::max(shiftFactor, 0.001f), 0.0001f));
		_viewCtrl.setViewRange(_viewCtrl.viewBoundingSphereDia());
		_viewCtrl.setBoundingSphereCenter(projCenter);
		_primaryCamera->setViewRange(_viewCtrl.viewRange());
		_primaryCamera->setView(projCenter - viewDir * desiredDist, viewDir, upDir, rightDir);

		_viewCtrl.syncPoseAndRangeFromCamera(*_primaryCamera);

		resizeGL(width(), height());
		update();
		emit zoomAndPanSet();
		return;
	}

	// Compute the viewRange and the projected visual centre simultaneously.
	// The projected centre is the midpoint of the geometry's view-space extents
	// for the current orientation — setting it as the orbit target ensures the
	// scene appears centred on screen with equal margins on every side.
	QVector3D projCenter;
	_viewCtrl.setViewBoundingSphereDia(computeFitViewRange(&projCenter));
	_viewCtrl.setBoundingSphereCenter(projCenter);

	if (!_animateFitAllTimer->isActive())
	{
		_keyboardNavTimer->stop();
		_animateFitAllTimer->start(5);
		_viewCtrl.resetSlerpStep();
	}
}

void ViewportWidget::fitAllImmediate()
{
	const std::vector<int>& visibleIds = _sceneRuntime.currentVisibleObjectIds();
	if (_sceneRuntime.meshStore().empty() || visibleIds.empty())
		return;

	checkAndStopTimers();
	_keyboardNavTimer->stop();
	if (_animateFitAllTimer->isActive())
		_animateFitAllTimer->stop();
	_viewCtrl.resetSlerpStep();

	if (_primaryCamera->getMode() == Camera::CameraMode::Fly ||
		_primaryCamera->getMode() == Camera::CameraMode::FirstPerson)
	{
		// Delegates to the ANIMATED fit (fitAll()) for these modes - that
		// path deliberately does NOT use cameraInteracting=true (see
		// animateViewChange()'s doc comment on why an animation whose step
		// doesn't decay toward zero would otherwise show the interactive
		// renderer's one-tick-behind lag as a jerk at the end), so this
		// function must not call notifyCameraInteracting() here either.
		fitAll();
		return;
	}

	QVector3D projCenter;
	_viewCtrl.setViewBoundingSphereDia(computeFitViewRange(&projCenter));
	_viewCtrl.setBoundingSphereCenter(projCenter);
	_viewCtrl.setViewRange(_viewCtrl.viewBoundingSphereDia());
	_primaryCamera->setViewRange(_viewCtrl.viewRange());
	_primaryCamera->setPosition(projCenter);
	_viewCtrl.syncPoseAndRangeFromCamera(*_primaryCamera);

	// Genuinely immediate (non-animated) camera change - notify AFTER it's
	// fully applied, not before, so the interactive PT renderer is fed the
	// actual new pose instead of whatever the camera was before this call.
	// That includes resizeGL() specifically: it's what actually rebuilds
	// the projection matrix for the new viewRange set above (RtSceneBuilder::
	// buildCamera() reads that matrix for tanHalfFovY/orthoHalfHeight) - see
	// the mouse-wheel zoom handler's identical fix for why notifying before
	// this call captures a stale zoom scale against the new position.
	resizeGL(width(), height());
	_rtInteractionCtrl->notifyCameraInteracting();
	update();
	emit zoomAndPanSet();
}


void ViewportWidget::setSelectionHighlighting(bool highlight)
{
	_selectionHighlighting = highlight;
	_renderCtrl.fgShader()->setUniformValue("selectionHighlighting", _selectionHighlighting);
	update();
}

void ViewportWidget::beginWindowZoom()
{
	// Arming window zoom is the beginning of a camera-only interaction path;
	// keep parity with mouse drag by switching GPU PT into its interactive
	// preview profile up front.
	_rtInteractionCtrl->notifyCameraInteracting();

	_viewCtrl.setWindowZoomActive(true);
	setCursor(QCursor(QPixmap(":/icons/res/window-zoom-cursor.png"), 12, 12));
}

void ViewportWidget::performWindowZoom()
{
	_viewCtrl.setWindowZoomActive(false);

	QRect zoomRect = _rubberBand->geometry();
	if (zoomRect.width() == 0 || zoomRect.height() == 0)
	{
		emit windowZoomEnded();
		return;
	}

	QPoint zoomWinCen = zoomRect.center();
	QRect viewport = PickingHelper::viewportRectForPoint(zoomWinCen, width(), height(), _viewCtrl.multiViewActive());
	QMatrix4x4 mvMatrix = _viewCtrl.viewMatrix() * _viewCtrl.modelMatrix();

	// Sample the depth buffer at the rubber-band centre to get the actual scene depth.
	// When the centre pixel is background, scan a 9x9 neighbourhood and take the minimum
	// non-background depth (nearest geometry). This is critical for small rubber-bands on
	// model edges/silhouettes where the centre pixel often lands on background — the error
	// in z_v is then amplified by the zoom ratio, causing visible offset.
	float depthZ;
	{
		makeCurrent();
		float rawDepth = 1.0f;
		int cx = zoomWinCen.x();
		int cy_gl = height() - zoomWinCen.y() - 1;  // flip to OpenGL bottom-up Y
		glReadPixels(cx, cy_gl, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &rawDepth);

		if (rawDepth >= 1.0f)
		{
			// Centre is background — scan a 9x9 neighbourhood for nearest geometry.
			const int halfGrid = 4;
			int x0 = std::max(0,            cx      - halfGrid);
			int y0 = std::max(0,            cy_gl   - halfGrid);
			int x1 = std::min(width()  - 1, cx      + halfGrid);
			int y1 = std::min(height() - 1, cy_gl   + halfGrid);
			int sw = x1 - x0 + 1, sh = y1 - y0 + 1;
			std::vector<float> depthBuf(sw * sh, 1.0f);
			glReadPixels(x0, y0, sw, sh, GL_DEPTH_COMPONENT, GL_FLOAT, depthBuf.data());
			float minDepth = 1.0f;
			for (float d : depthBuf)
				if (d < minDepth) minDepth = d;
			if (minDepth < 1.0f)
				rawDepth = minDepth;
		}

		if (rawDepth >= 1.0f)
		{
			// No geometry found near centre — fall back to bounding sphere centre depth.
			QVector3D Z = (_primaryCamera->getMode() == Camera::CameraMode::Orbit)
				? _primaryCamera->getPosition()
				: _viewCtrl.boundingSphere().getCenter();
			Z = Z.project(mvMatrix, _viewCtrl.projectionMatrix(), viewport);
			depthZ = Z.z();
		}
		else
		{
			depthZ = rawDepth;
		}
	}

	// Unproject viewport centre (O) and rubber-band centre (P) at the scene depth.
	// The pan vector P - O brings the rubber-band centre to screen centre for any choice of depth.
	QRect clientRect = PickingHelper::clientRectForPoint(zoomWinCen, width(), height(), _viewCtrl.multiViewActive());
	QPoint clientWinCen = clientRect.center();
	QVector3D o(clientWinCen.x(), height() - clientWinCen.y(), depthZ);
	QVector3D O = o.unproject(mvMatrix, _viewCtrl.projectionMatrix(), viewport);

	QVector3D p(zoomWinCen.x(), height() - zoomWinCen.y(), depthZ);
	QVector3D P = p.unproject(mvMatrix, _viewCtrl.projectionMatrix(), viewport);

	// Pixel-space zoom ratio (fixed: was integer division before).
	double widthRatio  = static_cast<double>(clientRect.width())  / zoomRect.width();
	double heightRatio = static_cast<double>(clientRect.height()) / zoomRect.height();
	_viewCtrl.setRubberBandZoomRatio(static_cast<GLfloat>((heightRatio < widthRatio) ? heightRatio : widthRatio));

	// Perspective correction: the visible extent at signed view-space depth z_v is
	// proportional to the eye-to-anchor distance. Correct the zoom ratio accordingly.
	if (_viewCtrl.projection() == ViewProjection::PERSPECTIVE)
	{
		float distanceOld = _primaryCamera->getOrbitDistance();
		if (distanceOld > 0.0f && _viewCtrl.currentViewRange() > 0.0f)
		{
			const QVector3D target = _primaryCamera->getPosition();
			const QVector3D viewDir = _primaryCamera->getViewDir().normalized();
			const float dc = QVector3D::dotProduct(P - target, viewDir);
			// eye-to-P depth along viewDir = distanceOld + dc.
			// After zoom, we want the new eye-to-P = (distanceOld + dc) / ratio.
			// With D_new + dc = (distanceOld + dc) / ratio  →  D_new = anchor/ratio - dc.
			const float anchorDistanceOld = distanceOld + dc;
			if (anchorDistanceOld > 0.0f)
			{
				const float newDistance = anchorDistanceOld / _viewCtrl.rubberBandZoomRatio() - dc;
				if (newDistance > 0.0f)
				{
					const float distanceFactor = distanceOld / _viewCtrl.currentViewRange();
					const float newViewRange = newDistance / distanceFactor;
					_viewCtrl.setRubberBandZoomRatio(_viewCtrl.currentViewRange() / newViewRange);
				}
			}
		}
	}

	// Very small rectangles can feel too aggressive in perspective because even a
	// mathematically correct ratio is visually abrupt near the object. Compress the
	// high end of the zoom ratio to keep the target in frame more reliably.
	if (_viewCtrl.projection() == ViewProjection::PERSPECTIVE)
	{
		if (_viewCtrl.rubberBandZoomRatio() > 4.0f)
			_viewCtrl.setRubberBandZoomRatio(4.0f + (_viewCtrl.rubberBandZoomRatio() - 4.0f) * 0.6f);
		if (_viewCtrl.rubberBandZoomRatio() > 8.0f)
			_viewCtrl.setRubberBandZoomRatio(8.0f + (_viewCtrl.rubberBandZoomRatio() - 8.0f) * 0.4f);
	}

	// Compute the pan that brings the rubber-band centre to screen centre.
	// Both P and O are unprojected at the same depthZ, so (P - O) is already
	// perpendicular to viewDir — no depth stripping is needed.
	_viewCtrl.setRubberBandPan(P - O);

	if (!_animateWindowZoomTimer->isActive())
	{
		_keyboardNavTimer->stop();
		_animateWindowZoomTimer->start(5);
		_viewCtrl.resetSlerpStep();
	}
	emit windowZoomEnded();
}

void ViewportWidget::setProjection(ViewProjection proj)
{
	_viewCtrl.setProjection(proj);
	if (!_primaryCamera || _primaryCamera->getMode() == Camera::CameraMode::Orbit)
	{
		_viewCtrl.setPreviousProjection((proj == ViewProjection::PERSPECTIVE)
			? Camera::ProjectionType::PERSPECTIVE
			: Camera::ProjectionType::ORTHOGRAPHIC);
	}
	resizeGL(width(), height());
	// Notify AFTER the projection actually changed (resizeGL() recomputes
	// the projection matrix from it), not before - see the mouse-drag
	// handlers' identical fix for why.
	_rtInteractionCtrl->notifyCameraInteracting();
}

Camera::CameraMode ViewportWidget::cameraMode() const
{
	return _primaryCamera ? _primaryCamera->getMode() : Camera::CameraMode::Orbit;
}

bool ViewportWidget::positionGameplayCameraForScene(Camera::CameraMode mode)
{
	if (!_primaryCamera ||
		(mode != Camera::CameraMode::Fly && mode != Camera::CameraMode::FirstPerson))
	{
		return false;
	}

	const std::vector<int>& visibleIds = _sceneRuntime.currentVisibleObjectIds();
	if (_sceneRuntime.meshStore().empty() || visibleIds.empty())
		return false;

	const QVector3D worldUp = CoordinateSystemHelper::currentWorldUpVector(_viewCtrl.cameraUpAxisZUp());
	const QVector3D center = _viewCtrl.boundingSphere().getCenter();
	const float radius = std::max(_viewCtrl.boundingSphere().getRadius(), 0.001f);

	// Derive scene extent along the current world-up axis directly from the bounding box
	// so this is always correct regardless of convention and when recalculate was last called.
	const float lowestUp  = _viewCtrl.cameraUpAxisZUp() ? static_cast<float>(_viewCtrl.boundingBox().zMin())
	                                          : static_cast<float>(_viewCtrl.boundingBox().yMin());
	const float highestUp = _viewCtrl.cameraUpAxisZUp() ? static_cast<float>(_viewCtrl.boundingBox().zMax())
	                                          : static_cast<float>(_viewCtrl.boundingBox().yMax());
	const float modelHeight = std::max(highestUp - lowestUp, radius * 2.0f);

	// Strip the world-up component to get a purely horizontal direction.
	QVector3D horizontalViewDir = _primaryCamera->getViewDir();
	horizontalViewDir -= QVector3D::dotProduct(horizontalViewDir, worldUp) * worldUp;
	if (horizontalViewDir.lengthSquared() <= 1.0e-8f)
	{
		// Camera looking nearly straight up/down — pick a non-up fallback axis.
		const QVector3D alt = (std::abs(worldUp.x()) < 0.9f) ? QVector3D(1, 0, 0) : QVector3D(0, 1, 0);
		horizontalViewDir = alt - QVector3D::dotProduct(alt, worldUp) * worldUp;
	}
	horizontalViewDir.normalize();

	QVector3D eye;
	QVector3D target = center;
	if (mode == Camera::CameraMode::Fly)
	{
	const float flyDistance = std::max(radius * 2.25f, _viewCtrl.viewRange() * 0.9f);
		const float flyLift = std::clamp(modelHeight * 0.35f, radius * 0.25f, radius * 1.25f);
		const float targetLift = std::clamp(modelHeight * 0.10f, 0.0f, radius * 0.35f);

		eye    = center - horizontalViewDir * flyDistance + worldUp * flyLift;
		target = center + worldUp * targetLift;
	}
	else
	{
		const float eyeHeight    = std::clamp(modelHeight * 0.18f, radius * 0.12f, radius * 0.45f);
		const float walkDistance = std::max(radius * 2.6f, modelHeight * 0.75f);
		const float targetHeight = std::clamp(lowestUp + modelHeight * 0.33f,
		                                      lowestUp + eyeHeight * 0.8f, highestUp);

		eye = center - horizontalViewDir * walkDistance;
		// Replace the up-axis component of eye and target with the desired heights.
		eye    += (lowestUp  + eyeHeight - QVector3D::dotProduct(eye,    worldUp)) * worldUp;
		target += (targetHeight          - QVector3D::dotProduct(target, worldUp)) * worldUp;
	}

	QVector3D viewDir = target - eye;
	if (viewDir.lengthSquared() <= 1.0e-8f)
		viewDir = horizontalViewDir;
	viewDir.normalize();

	QVector3D rightDir = QVector3D::crossProduct(viewDir, worldUp);
	if (rightDir.lengthSquared() <= 1.0e-8f)
		rightDir = QVector3D(1.0f, 0.0f, 0.0f);
	rightDir.normalize();
	QVector3D upDir = QVector3D::crossProduct(rightDir, viewDir).normalized();

	_primaryCamera->setMode(mode);
	_primaryCamera->setZoom(1.0f);
	_primaryCamera->setView(eye, viewDir, upDir, rightDir);
	_primaryCamera->setYawPitchFromViewDir();
	_primaryCamera->updateFlyView();

	_viewCtrl.syncPoseFromCamera(*_primaryCamera);
	return true;
}

void ViewportWidget::setCameraMode(Camera::CameraMode mode)
{
	const std::vector<int>& visibleIds = _sceneRuntime.currentVisibleObjectIds();
	const bool hasVisibleScene = !_sceneRuntime.meshStore().empty() && !visibleIds.empty();

	if (mode == Camera::CameraMode::Fly || mode == Camera::CameraMode::FirstPerson)
	{
		const bool comingFromOrbit = _primaryCamera->getMode() == Camera::CameraMode::Orbit;
		QVector3D orbitEye = _primaryCamera->getPosition();
		const Camera::ProjectionType orbitProjection = comingFromOrbit
			? _primaryCamera->getProjectionType()
			: _viewCtrl.previousProjection();

		if (comingFromOrbit)
		{
			_viewCtrl.setPreviousProjection(orbitProjection);
			const QVector3D viewDir = _primaryCamera->getViewDir().normalized();
			const QVector3D center = _viewCtrl.boundingSphere().getCenter();
			const float desiredDist = std::max(_primaryCamera->getOrbitDistance(),
				std::max(_viewCtrl.viewRange(), _viewCtrl.boundingSphere().getRadius() * 1.75f));
			orbitEye = center - viewDir * desiredDist;
		}

		if (_primaryCamera->getProjectionType() != Camera::ProjectionType::PERSPECTIVE)
		{
			setProjection(ViewProjection::PERSPECTIVE);
			_viewCtrl.setPreviousProjection(orbitProjection);
		}

		// setMode syncs yaw/pitch from the current viewDir, resets up/right vectors
		_primaryCamera->setMode(mode);

		// Drop any zoom scale accumulated in Orbit mode; Fly uses real position instead
		_primaryCamera->setZoom(1.0f);

		// Spawn gameplay cameras around the loaded model instead of inheriting the orbit pivot.
		if (comingFromOrbit && positionGameplayCameraForScene(mode))
		{
			_viewCtrl.setCurrentTranslation(_primaryCamera->getPosition());
		}
		else
		{
			// Continue from the actual orbit eye position to avoid a visible jump.
			_primaryCamera->setPosition(orbitEye);
			_viewCtrl.setCurrentTranslation(_primaryCamera->getPosition());
		}
	}
	else if (mode == Camera::CameraMode::Orbit)
	{
		if (_primaryCamera->getMode() == Camera::CameraMode::Fly ||
			_primaryCamera->getMode() == Camera::CameraMode::FirstPerson)
		{
			const QVector3D eye = _primaryCamera->getPosition();
			const QVector3D target = eye + _primaryCamera->getViewDir() * _primaryCamera->getOrbitDistance();
			_primaryCamera->setPosition(target);
		}

		_primaryCamera->setMode(mode);
		setProjection(_viewCtrl.previousProjection() == Camera::ProjectionType::PERSPECTIVE ? ViewProjection::PERSPECTIVE : ViewProjection::ORTHOGRAPHIC);
		_viewCtrl.syncPoseFromCamera(*_primaryCamera);
		_viewCtrl.setCurrentViewRange(_viewCtrl.viewRange());
	}

	if (hasVisibleScene)
	{
		fitAll();
	}
	else
	{
		resizeGL(width(), height());
		update();
	}
}

void ViewportWidget::setRotationActive(bool active)
{
	_viewCtrl.setNavigationModes(active, false, false);
	setCursor(QCursor(QPixmap(":/icons/res/rotatecursor.png")));
	MainWindow::showStatusMessage(tr("Press Esc to deactivate rotation mode"));
}

void ViewportWidget::setPanningActive(bool active)
{
	_viewCtrl.setNavigationModes(false, active, false);
	setCursor(QCursor(QPixmap(":/icons/res/pancursor.png")));
	MainWindow::showStatusMessage(tr("Press Esc to deactivate panning mode"));
}

void ViewportWidget::setZoomingActive(bool active)
{
	_viewCtrl.setNavigationModes(false, false, active);
	setCursor(QCursor(QPixmap(":/icons/res/zoomcursor.png")));
	MainWindow::showStatusMessage(tr("Press Esc to deactivate zooming mode"));
}

void ViewportWidget::setDisplayList(const std::vector<int>& ids)
{
	if (_sceneRuntime.setDisplayList(ids))
		emit visibleSwapped(_sceneRuntime.visibleSwapped());

	_viewCtrl.syncTranslationFromCamera(*_primaryCamera);
	_viewCtrl.setBoundingSphereCenter(0, 0, 0);

	// Recompute all visible-scene aggregates in one pass.
	recalculateVisibleSceneStats(true);
	// Reset smoothed zoom floor to scene radius so a freshly loaded model
	// starts with the standard global clamp, not a stale small value.
	_viewCtrl.setZoomInLimit(_viewCtrl.boundingSphere().getRadius());

	// Reposition lights from per-file user model transforms (derived from the
	// current mesh TRS state — no bounding-sphere anchoring involved).
	updatePunctualLights();

	triggerShadowRecomputation();
	updateFloorPlane();

	if (_primaryCamera->getMode() == Camera::CameraMode::Fly ||
		_primaryCamera->getMode() == Camera::CameraMode::FirstPerson)
	{
		if (_primaryCamera->getProjectionType() != Camera::ProjectionType::PERSPECTIVE)
		{
			setProjection(ViewProjection::PERSPECTIVE);
		}

		positionGameplayCameraForScene(_primaryCamera->getMode());
	}
	else if (_viewCtrl.autoFitViewOnUpdate())
	{
		if (!isGltfCameraActive())
		{
			fitAll();
		}
	}

	update();

	emit displayListSet();
}

void ViewportWidget::recalculateVisibleSceneStats(bool updateMemorySize)
{
	_viewCtrl.syncTranslationFromCamera(*_primaryCamera);
	_viewCtrl.setBoundingSphereCenter(0, 0, 0);
	_viewCtrl.setBoundingSphereRadius(0.0f);
	_viewCtrl.setBoundingBoxLimits(-0.001, -0.001, -0.001, 0.001, 0.001, 0.001);
	_viewCtrl.setVisibleLowestZ(-1.0f);
	_viewCtrl.setVisibleHighestZ(1.0f);

	const std::vector<int>& visibleIds = _sceneRuntime.currentVisibleObjectIds();
	if (updateMemorySize)
	{
		_displayedObjectsMemSize = 0;
	}

	if (visibleIds.empty())
	{
		_primaryCamera->setPosition(0, 0, 0);
		_viewCtrl.syncTranslationFromCamera(*_primaryCamera);
		_viewCtrl.setBoundingSphereRadius(1.0f);
		return;
	}

	bool firstBox = true;
	float lowestZ = std::numeric_limits<float>::max();
	float highestZ = std::numeric_limits<float>::lowest();
	unsigned long long memSize = 0;

	for (int i : visibleIds)
	{
		try
		{
			SceneMesh* mesh = _sceneRuntime.meshAt(i);
			if (!isMeshAnimationVisible(mesh))
				continue;
			if (updateMemorySize)
			{
				memSize += mesh->memorySize();
			}

			const BoundingBox meshBox = mesh->getBoundingBox();
			if (firstBox)
			{
				_viewCtrl.setBoundingBox(meshBox);
				firstBox = false;
			}
			else
			{
				_viewCtrl.expandBoundingBox(meshBox);
			}

			const float meshLow  = _viewCtrl.cameraUpAxisZUp() ? static_cast<float>(meshBox.zMin()) : static_cast<float>(meshBox.yMin());
			const float meshHigh = _viewCtrl.cameraUpAxisZUp() ? static_cast<float>(meshBox.zMax()) : static_cast<float>(meshBox.yMax());
			lowestZ  = std::min(lowestZ,  meshLow);
			highestZ = std::max(highestZ, meshHigh);
		}
		catch (const std::out_of_range& ex)
		{
			std::cout << ex.what() << std::endl;
		}
	}

	if (updateMemorySize)
	{
		_displayedObjectsMemSize = memSize;
	}

	if (!firstBox)
	{
		_viewCtrl.setVisibleLowestZ(lowestZ);
		_viewCtrl.setVisibleHighestZ(highestZ);

		// Derive the scene bounding sphere center from the axis-aligned bounding box
		// midpoint — order-independent and immune to floating-point perturbations from
		// the world-transform round-trip during export/import.
		//
		// The radius is computed as max over all visible meshes of:
		//   distance(boxCenter, mesh.sphereCenter) + mesh.sphereRadius
		// This is still O(M) and order-independent (it's a simple max), but much tighter
		// than the box half-diagonal for round geometry (e.g. sphere meshes), where the
		// half-diagonal would be sqrt(3)x the actual radius.
		const QVector3D boxCenter(
			static_cast<float>((_viewCtrl.boundingBox().xMin() + _viewCtrl.boundingBox().xMax()) * 0.5),
			static_cast<float>((_viewCtrl.boundingBox().yMin() + _viewCtrl.boundingBox().yMax()) * 0.5),
			static_cast<float>((_viewCtrl.boundingBox().zMin() + _viewCtrl.boundingBox().zMax()) * 0.5)
		);
		float bsRadius = 0.0f;
		for (int i : visibleIds)
		{
			try
			{
				SceneMesh* mesh = _sceneRuntime.meshAt(i);
				if (!isMeshAnimationVisible(mesh))
					continue;
				BoundingSphere ms = mesh->getBoundingSphere();
				const float d = (ms.getCenter() - boxCenter).length() + ms.getRadius();
				if (d > bsRadius) bsRadius = d;
			}
			catch (const std::out_of_range&) {}
		}
		_viewCtrl.setBoundingSphereCenter(boxCenter);
		_viewCtrl.setBoundingSphereRadius(bsRadius > 0.0f ? bsRadius : 1.0f);
	}
}

void ViewportWidget::triggerShadowRecomputation()
{
	if (!_renderCtrl.fgShader())
	{
		_renderCtrl.setShadowMapNeedsInitialization(true);
		return;
	}

	float boundingRadius = _viewCtrl.boundingSphere().getRadius();
	_viewCtrl.setViewBoundingSphereDia(boundingRadius * 2);

	float lightDistance = calculateLightDistance();
	const float coverageHint = (_renderCtrl.shadowFrustumExtentW() > 0.0f)
		? (std::max)(_renderCtrl.shadowFrustumExtentW(), _renderCtrl.shadowFrustumExtentH())
		: -1.0f;
	float shadowFactor = shadowMapper.calculateShadowFactor(boundingRadius, lightDistance, coverageHint);
	shadowFactor = std::clamp(shadowFactor, 1.0f, 8.0f);

	_renderCtrl.setShadowWidth(static_cast<int>(1024 * shadowFactor));
	_renderCtrl.setShadowHeight(static_cast<int>(1024 * shadowFactor));

	// Get SIZE-AWARE shadow parameters
	auto shadowParams = shadowMapper.getShadowQualityParamsSmooth(boundingRadius);
	float shadowSoftness = shadowMapper.calculateShadowSoftness(_viewCtrl.viewBoundingSphereDia());
	float sizeScale = shadowMapper.calculateSizeQualityScale(boundingRadius);

	_renderCtrl.fgShader()->bind();

	_renderCtrl.fgShader()->setUniformValue("shadowSoftness", shadowSoftness);

	// Size-aware uniforms
	_renderCtrl.fgShader()->setUniformValue("shadowMaxKernelSize", shadowParams.maxKernelSize);
	_renderCtrl.fgShader()->setUniformValue("shadowSoftnessScale", shadowParams.softnessScale);
	_renderCtrl.fgShader()->setUniformValue("shadowMaxSoftnessClamp", shadowParams.maxSoftnessClamp);
	_renderCtrl.fgShader()->setUniformValue("shadowBiasMin", shadowParams.biasMin);
	_renderCtrl.fgShader()->setUniformValue("shadowBiasMax", shadowParams.biasMax);
	_renderCtrl.fgShader()->setUniformValue("shadowTransitionRange", shadowParams.transitionRange);
	_renderCtrl.fgShader()->setUniformValue("shadowGammaCorrection", shadowParams.gammaCorrection);
	_renderCtrl.fgShader()->setUniformValue("shadowSizeScale", sizeScale);

	_renderCtrl.fgShader()->release();

	_renderCtrl.setShadowMapNeedsInitialization(true);
	makeCurrent();
	loadFloor();	
}

void ViewportWidget::setShadowQuality(AdaptiveShadowMapper::QualityLevel quality)
{
	shadowMapper.setQuality(quality);
	triggerShadowRecomputation();
	updateFloorPlane();
}

float ViewportWidget::calculateLightDistance()
{
	QVector3D lightPos = effectiveWorldLightPosition();
	QVector3D center = _viewCtrl.boundingSphere().getCenter();
	return (lightPos - center).length();
}

QVector<QUuid> ViewportWidget::duplicateObjects(const std::vector<int>& ids)
{
	QVector<QUuid> duplicatedUuids;

	makeCurrent();

	for (int id : ids)
	{
		SceneMesh* originalMesh = _sceneRuntime.meshAt(id);
		if (originalMesh)
		{
			// Clone the mesh
			SceneMesh* newMesh = originalMesh->clone();
			if (newMesh)
			{
				// Generate unique name with suffix
				QString uniqueName = generateUniqueMeshName(originalMesh->getName());
				newMesh->setName(uniqueName);

				// Add to display
				addToDisplay(newMesh);

				// Store the UUID of the duplicated mesh
				duplicatedUuids.append(newMesh->uuid());

				qDebug() << "Duplicated mesh:" << originalMesh->getName()
					<< "->" << uniqueName
					<< "uuid:" << newMesh->uuid();
			}
		}
	}

	doneCurrent();

	return duplicatedUuids;
}

void ViewportWidget::updateBoundingSphere()
{
	recalculateVisibleSceneStats(false);
}

void ViewportWidget::updateBoundingBox()
{	
	recalculateVisibleSceneStats(false);
}

void ViewportWidget::updateFloorPlane()
{
	// loadRenderSettings() (called from initializeGL(), before _lightCube -
	// touched by updateFloorGeometry() below - is constructed) emits
	// displayModeChanged partway through initializeGL(), and
	// VisualizationEnvironmentPanel::onDisplayModeChanged() reacts to it by
	// calling setGroundMode() -> here. Harmless for every normal (post-init)
	// call, which is the vast majority - only blocks this one reentrant
	// during-init path.
	if (!_renderCtrl.isOpenGLInitialized())
		return;

	if (!_floorPlane || !_renderCtrl.fgShader())
		return;

	// Use helper to update floor geometry
	float halfObjectSize = updateFloorGeometry();

	// Use helper to set main light position (now consistent with loadFloor)
	updateMainLightPosition(halfObjectSize);

	_floorPlaneZ = CoordinateSystemHelper::groundPlaneZ(
		_viewCtrl.boundingBox(),
		_viewCtrl.cameraUpAxisZUp(),
		_floorSize,
		_renderCtrl.floorOffsetPercent(),
		SceneRenderController::computeFloorDepthBias(
			static_cast<float>(std::max({
				_viewCtrl.boundingBox().getXSize(),
				_viewCtrl.boundingBox().getYSize(),
				_viewCtrl.boundingBox().getZSize()
			})),
			_floorSize));
	const float groundExtent = CoordinateSystemHelper::groundPlaneExtent(
		_floorSize,
		_floorSizeFactor,
		_renderCtrl.groundMode());
	if (_floorPlane && _renderCtrl.fgShader())
	{
		_floorPlane->setPlane(
			_renderCtrl.fgShader(), _floorCenter, groundExtent, groundExtent, 1, 1,
			_floorPlaneZ, _renderCtrl.floorTexRepeatS(), _renderCtrl.floorTexRepeatT(),
			CoordinateSystemHelper::floorPlaneOrientation(_viewCtrl.cameraUpAxisZUp()));
		applyFloorPlaneMaterialSettings();
	}

	refreshFallbackLight();

	updateClippingPlane();
}

void ViewportWidget::refreshFallbackLight()
{
	// PunctualLights::createFallbackLight() below issues raw GL calls
	// (glGenBuffers/glBindBuffer). setLightOffset() can reach this from
	// ModelViewer::showEvent()'s first-time updateDisplayList() - i.e. before
	// this widget's own initializeGL() has ever run - so on platforms where
	// the GL context isn't made current until the widget actually paints
	// (X11/XCB queues the expose event rather than delivering it synchronously
	// like Windows' WM_PAINT can), there is no current context yet and those
	// calls crash. Bail out here; once initializeGL() completes it calls this
	// again with a valid context.
	if (!_renderCtrl.isOpenGLInitialized())
		return;

	// Create fallback light if no punctual lights are available. Also
	// gated on useDefaultLights() (previously wasn't) - this persistent
	// PunctualLights fallback entry is what the "Default Lights" checkbox is
	// actually understood to mean by the user, same as the separately-
	// recomputed keyLight in buildRayTracedSnapshot(); without this check,
	// disabling Default Lights still left this real, inverse-square-
	// attenuated light in place, so both raster's multi-light shading path
	// and CPU/GPU ray tracing kept casting its shadow regardless of the
	// toggle.
	if (_animCtrl.originalParsedLights().empty())
	{
		if (_renderCtrl.useDefaultLights() && shouldUseFallbackLightForVisibleScene())
		{
			const QVector3D fallbackLightPos = effectiveWorldLightPosition();

			// This light is deliberately placed far from the scene (see
			// updateMainLightPosition()) for a nice raking shadow direction
			// and a clean shadow-map frustum fit - but a flat, distance-
			// independent intensity leaves it contributing almost nothing
			// wherever real inverse-square falloff is actually applied (the
			// path tracer's NEE, and raster's own regular-mesh direct
			// lighting - only the floor's separate legacy shading term
			// happens to ignore this light's true attenuated magnitude,
			// which is what let this go unnoticed). Scaling intensity by
			// distance^2 from the scene center calibrates it to still
			// contribute a reasonable, physically-consistent amount at that
			// distance, regardless of scene scale or where this light
			// happens to be placed. Empirically tuned, not derived from a
			// physical unit system - 0.5 left the shadow too weak to notice;
			// reflection visibility is handled separately via the floor
			// material's roughness (see RtSceneBuilder::convertFloorMaterial())
			// rather than by fighting this value down.
			constexpr float kTargetSurfaceIntensity = 2.0f;
			const float lightDistance = static_cast<float>((fallbackLightPos - _floorCenter).length());
			const float calibratedIntensity = kTargetSurfaceIntensity * std::max(lightDistance * lightDistance, 1.0f);

			_renderCtrl.punctualLights()->createFallbackLight(glm::vec3(
				static_cast<float>(fallbackLightPos.x()),
				static_cast<float>(fallbackLightPos.y()),
				static_cast<float>(fallbackLightPos.z())
			), calibratedIntensity);
			syncPunctualLightUniforms(1, true);
		}
		else
		{
			_renderCtrl.punctualLights()->setLights({});
			syncPunctualLightUniforms(0, false);
		}
	}
}

void ViewportWidget::syncPunctualLightUniforms(int lightCount, bool hasPunctualLights)
{
	if (!_renderCtrl.fgShader())
		return;

	_renderCtrl.fgShader()->bind();
	_renderCtrl.fgShader()->setUniformValue("lightCount", lightCount);
	_renderCtrl.fgShader()->setUniformValue("hasPunctualLights", hasPunctualLights);
}

bool ViewportWidget::shouldUseFallbackLightForVisibleScene() const
{
	const std::vector<int>& visibleIds = _sceneRuntime.currentVisibleObjectIds();
	bool sawVisibleMesh = false;
	bool sawGltfDerivedMesh = false;

	for (int meshId : visibleIds)
	{
		if (meshId < 0 || meshId >= static_cast<int>(_sceneRuntime.meshStore().size()))
			continue;

		const SceneMesh* mesh = _sceneRuntime.meshAt(meshId);
		if (!mesh)
			continue;

		sawVisibleMesh = true;
		const QString sourceFile = mesh->getSourceFile().trimmed();
		if (sourceFile.isEmpty())
			continue;

		if (sourceFile.endsWith(".gltf", Qt::CaseInsensitive) ||
			sourceFile.endsWith(".glb", Qt::CaseInsensitive))
		{
			sawGltfDerivedMesh = true;
			continue;
		}

		return true;
	}

	if (!sawVisibleMesh)
		return true;

	return !sawGltfDerivedMesh;
}

void ViewportWidget::updateClippingPlane()
{
	float xside = _renderCtrl.clippingXFlipped() || _renderCtrl.clippingXCoeff() > 0 ? -1.0f : 1.0f;
	float yside = _renderCtrl.clippingYFlipped() || _renderCtrl.clippingYCoeff() > 0 ? 1.0f : -1.0f;
	float zside = _renderCtrl.clippingZFlipped() || _renderCtrl.clippingZCoeff() > 0 ? -1.0f : 1.0f;
	_clippingPlaneXY->setPlane(_renderCtrl.clippingPlaneShader(), _floorCenter, _floorSize * 100.0f, _floorSize * 100.0f, 1, 1, -_renderCtrl.clippingZCoeff() * zside, _floorSize, _floorSize);
	_clippingPlaneYZ->setPlane(_renderCtrl.clippingPlaneShader(), _floorCenter, _floorSize * 100.0f, _floorSize * 100.0f, 1, 1, -_renderCtrl.clippingXCoeff() * xside, _floorSize, _floorSize);
	_clippingPlaneZX->setPlane(_renderCtrl.clippingPlaneShader(), _floorCenter, _floorSize * 100.0f, _floorSize * 100.0f, 1, 1, -_renderCtrl.clippingYCoeff() * yside, _floorSize, _floorSize);
	_clippingPlanesEditor->setCoefficientLimits(-_viewCtrl.boundingBox().getXSize()/2, _viewCtrl.boundingBox().getXSize()/2,
		-_viewCtrl.boundingBox().getYSize() / 2, _viewCtrl.boundingBox().getYSize() / 2,
		-_viewCtrl.boundingBox().getZSize() / 2, _viewCtrl.boundingBox().getZSize() / 2);
}

void ViewportWidget::showClippingPlaneEditor(bool show)
{
	if (show)
	{
		if (_explodedViewPanel && _explodedViewPanel->isVisible())
			showExplodedViewPanel(false);
		if (_viewToolbar)
			_viewToolbar->setExplodedViewChecked(false);
		_clippingPlanesEditor->show();
	}
	else
	{
		_clippingPlanesEditor->hide();
	}
}

void ViewportWidget::showExplodedViewPanel(bool show)
{
	if (show) {
		if (_clippingPlanesEditor && _clippingPlanesEditor->isVisible())
			showClippingPlaneEditor(false);
		if (_viewToolbar)
			_viewToolbar->setSectionViewChecked(false);
		_explodedViewPanel->captureCurrentSelection();
		_explodedViewPanel->show();
		updateExplosion();

		if (_explodedViewCtrl.isManualPlacementSuppressed() && !_explodedViewCtrl.manualHiddenStates().isEmpty())
		{
			QHash<QUuid, QVector3D> savedExplosionOffsets;
			savedExplosionOffsets.reserve(static_cast<int>(_sceneRuntime.meshStore().size()));
			for (const SceneMeshRecord& meshRecord : _sceneRuntime.meshStore())
			{
				SceneMesh* mesh = meshRecord.mesh;
				if (!mesh)
					continue;

				savedExplosionOffsets.insert(mesh->uuid(), mesh->explosionOffset());
				mesh->setExplosionOffset(QVector3D());
			}

			for (auto it = _explodedViewCtrl.manualHiddenStates().cbegin();
			     it != _explodedViewCtrl.manualHiddenStates().cend(); ++it)
			{
				SceneMesh* mesh = getMeshByUuid(it.key());
				if (!mesh)
					continue;

				const TransformState& state = it.value();
				ExplodedViewRuntimeController::applyExplodedViewTransformState(mesh, state, false);
			}

			for (const SceneMeshRecord& meshRecord : _sceneRuntime.meshStore())
			{
				SceneMesh* mesh = meshRecord.mesh;
				if (!mesh)
					continue;

				mesh->setExplosionOffset(savedExplosionOffsets.value(mesh->uuid()));
				mesh->setSceneRenderTransformFast(mesh->getSceneRenderTransform());
			}

			_explodedViewCtrl.setManualPlacementSuppressed(false);
		}
	} else {
		_explodedViewPanel->deactivateInteractiveState();
		if (isExplodedViewManualPlacementActive())
			finishExplodedViewManualPlacement();
		else
			showTransformGizmoForSelection(false);

		if (!_explodedViewCtrl.manualOriginalStates().isEmpty())
		{
			_explodedViewCtrl.manualHiddenStates().clear();
			for (auto it = _explodedViewCtrl.manualOriginalStates().cbegin();
			     it != _explodedViewCtrl.manualOriginalStates().cend(); ++it)
			{
				SceneMesh* mesh = getMeshByUuid(it.key());
				if (!mesh)
					continue;

				_explodedViewCtrl.manualHiddenStates().insert(it.key(), TransformState(
					mesh->getExplodedViewTranslation(),
					mesh->getExplodedViewRotation(),
					mesh->getExplodedViewScaling(),
					mesh->getExplodedViewRotationQuaternion()));
			}
		}

		_explodedViewPanel->hide();
		_explodedViewCtrl.explodedViewManager()->reset();
		_explodedViewCtrl.invalidateHintsCache();

		// Hide the entire exploded authoring state while the panel is closed.
		// This means clearing auto offsets and temporarily restoring the original
		// mesh TRS for any staged manual placement, without discarding the staged
		// manual pose that will be restored when the panel is shown again.
		for (size_t i = 0; i < _sceneRuntime.meshStore().size(); ++i)
		{
			if (_sceneRuntime.meshAt(i))
				_sceneRuntime.meshAt(i)->setExplosionOffset(QVector3D());
		}

		for (auto it = _explodedViewCtrl.manualOriginalStates().cbegin();
		     it != _explodedViewCtrl.manualOriginalStates().cend(); ++it)
		{
			SceneMesh* mesh = getMeshByUuid(it.key());
			if (!mesh)
				continue;

			const TransformState& state = it.value();
			ExplodedViewRuntimeController::applyExplodedViewTransformState(mesh, state, false);
		}

		_explodedViewCtrl.setManualPlacementSuppressed(!_explodedViewCtrl.manualHiddenStates().isEmpty());
		for (size_t i = 0; i < _sceneRuntime.meshStore().size(); ++i)
		{
			if (_sceneRuntime.meshAt(i))
				_sceneRuntime.meshAt(i)->setSceneRenderTransformFast(_sceneRuntime.meshAt(i)->getSceneRenderTransform());
		}
		update();
	}
}

// ---------------------------------------------------------------------------
// Explosion: recompute offsets from current panel state and trigger repaint.
// ---------------------------------------------------------------------------
void ViewportWidget::updateExplosion()
{
    if (!_explodedViewPanel || !_explodedViewCtrl.explodedViewManager())
        return;

    const QSet<QUuid>& assemblyUuids = _explodedViewPanel->assemblyUuids();
    if (assemblyUuids.isEmpty()) {
        _explodedViewCtrl.explodedViewManager()->reset();
        for (size_t i = 0; i < _sceneRuntime.meshStore().size(); ++i)
        {
            if (_sceneRuntime.meshAt(i))
            {
                _sceneRuntime.meshAt(i)->setExplosionOffset(QVector3D());
                _sceneRuntime.meshAt(i)->setSceneRenderTransformFast(_sceneRuntime.meshAt(i)->getSceneRenderTransform());
            }
        }
        _renderCtrl.setShadowMapNeedsInitialization(true);
        update();
        return;
    }

    // Clear any previous offsets BEFORE building centroids so that getBoundingSphere()
    // (used in the no-scene-graph fallback) returns the original non-exploded centers.
    for (size_t i = 0; i < _sceneRuntime.meshStore().size(); ++i)
    {
        if (_sceneRuntime.meshAt(i))
        {
            _sceneRuntime.meshAt(i)->setExplosionOffset(QVector3D());
            _sceneRuntime.meshAt(i)->setSceneRenderTransformFast(_sceneRuntime.meshAt(i)->getSceneRenderTransform());
        }
    }

    // Build world-space centroids and AABBs for each assembly mesh.
    // Offsets were cleared above, so getBoundingBox() returns the unmodified
    // world-space AABB at the mesh's original (non-exploded) position.
    QHash<QUuid, QVector3D>                          worldCentroids;
    QHash<QUuid, QPair<QVector3D, QVector3D>>        worldBoxes;

    SceneGraph* sg = (_viewer && _viewer->sceneGraph()) ? _viewer->sceneGraph() : nullptr;
    if (sg)
    {
        const auto wt = sg->evaluateWorldTransforms();
        for (const QUuid& uuid : assemblyUuids)
        {
            SceneMesh* mesh = getMeshByUuid(uuid);
            if (!mesh) continue;

            const BoundingBox bb = mesh->getBoundingBox();
            const QVector3D bbMin(static_cast<float>(bb.xMin()),
                                  static_cast<float>(bb.yMin()),
                                  static_cast<float>(bb.zMin()));
            const QVector3D bbMax(static_cast<float>(bb.xMax()),
                                  static_cast<float>(bb.yMax()),
                                  static_cast<float>(bb.zMax()));

            // Centroid = midpoint of world-space AABB.
            worldCentroids.insert(uuid, (bbMin + bbMax) * 0.5f);
            worldBoxes.insert(uuid, {bbMin, bbMax});
        }
    }
    else
    {
        // No scene graph — fall back to bounding sphere centres.
        for (const QUuid& uuid : assemblyUuids)
        {
            SceneMesh* mesh = getMeshByUuid(uuid);
            if (!mesh) continue;

            const BoundingBox bb = mesh->getBoundingBox();
            const QVector3D bbMin(static_cast<float>(bb.xMin()),
                                  static_cast<float>(bb.yMin()),
                                  static_cast<float>(bb.zMin()));
            const QVector3D bbMax(static_cast<float>(bb.xMax()),
                                  static_cast<float>(bb.yMax()),
                                  static_cast<float>(bb.zMax()));

            worldCentroids.insert(uuid, (bbMin + bbMax) * 0.5f);
            worldBoxes.insert(uuid, {bbMin, bbMax});
        }
    }

    const bool useAssemblyAwareAutoHints =
        _explodedViewPanel->autoStrategy() == ExplodedViewPanel::AutoStrategy::AssemblyAware;

    // Rebuild O(n²) placement hints only when the assembly or anchor changes,
    // not on every slider tick.
    if (useAssemblyAwareAutoHints)
    {
        const QUuid anchorUuid = _explodedViewPanel->anchorUuid();
        if (!_explodedViewCtrl.cachedHintsValid()
            || _explodedViewCtrl.cachedHintsAssemblyUuids() != assemblyUuids
            || _explodedViewCtrl.cachedHintsAnchorUuid()    != anchorUuid)
        {
            _explodedViewCtrl.setHintsCache(
                assemblyUuids, anchorUuid,
                AssemblyRelationGraph::buildAutoPlacementHints(assemblyUuids, this, sg));
        }
    }
    else
    {
        _explodedViewCtrl.invalidateHintsCache();
    }

    _explodedViewCtrl.explodedViewManager()->recompute(
        assemblyUuids,
        _explodedViewPanel->anchorUuid(),
        _explodedViewPanel->mode(),
        _explodedViewPanel->userVector(),
        _explodedViewPanel->factor(),
        worldCentroids,
        worldBoxes,
        useAssemblyAwareAutoHints ? &_explodedViewCtrl.cachedAutoHints() : nullptr);

    // Push computed offsets onto the meshes so combinedRenderTransform() returns
    // the exploded position for every render path (main, selection, shadow, etc.).
    for (const QUuid& uuid : assemblyUuids)
    {
        SceneMesh* mesh = getMeshByUuid(uuid);
        if (mesh)
        {
            mesh->setExplosionOffset(_explodedViewCtrl.explodedViewManager()->offsetForMesh(uuid));
            mesh->setSceneRenderTransformFast(mesh->getSceneRenderTransform());
        }
    }

    _renderCtrl.setShadowMapNeedsInitialization(true);
    update();
}

// ---------------------------------------------------------------------------
// Render wrapper: explosion offsets are baked directly into each mesh's
// combinedRenderTransform() via RenderableMesh::_explosionOffset, so both the
// main render and the selection pass see the correct exploded positions
// automatically without any per-call shader state manipulation.
// ---------------------------------------------------------------------------
void ViewportWidget::renderMeshExploded(SceneMesh* mesh, DisplayMode mode)
{
    renderMeshWithDisplayMode(mesh, mode);
}

QWidget* ViewportWidget::attachOverlayPanel(QWidget* contentWidget, const QRect& geometry,
                                      Qt::Alignment, const QString& objectName)
{
	if (!contentWidget)
		return nullptr;

	auto* wrapper = new QWidget(this);
	const QString wrapperName = objectName.isEmpty()
		? QStringLiteral("glOverlayPanel")
		: objectName;
	wrapper->setObjectName(wrapperName);
	wrapper->setAttribute(Qt::WA_StyledBackground, true);
	wrapper->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
	applyOverlayPanelStyle(wrapper, wrapperName);

	QVBoxLayout* layout = new QVBoxLayout(wrapper);
	layout->setContentsMargins(6, 6, 6, 6);
	layout->addWidget(contentWidget);

	wrapper->setGeometry(geometry);
	wrapper->show();
	contentWidget->show();
	wrapper->raise();
	if (wrapperName == QLatin1String("navigationOverlayPanel"))
		_navigationOverlayPanel = wrapper;
	return wrapper;
}

QWidget* ViewportWidget::takeOverlayPanel(QWidget* contentWidget)
{
	if (!contentWidget)
		return nullptr;

	QWidget* wrapper = contentWidget->parentWidget();
	if (!wrapper || wrapper == this)
		return nullptr;

	if (QLayout* layout = wrapper->layout())
		layout->removeWidget(contentWidget);
	contentWidget->setParent(nullptr);

	if (wrapper == _navigationOverlayPanel)
		_navigationOverlayPanel = nullptr;

	wrapper->deleteLater();
	return wrapper;
}

void ViewportWidget::refreshDetachedNavigationOverlayTheme()
{
	refreshNavigationOverlayStyle();
}

void ViewportWidget::applyOverlayPanelStyle(QWidget* wrapper, const QString& objectName)
{
	if (!wrapper)
		return;

	const QColor averageBackgroundColor(
		(_renderCtrl.bgTopColor().red() + _renderCtrl.bgBotColor().red()) / 2,
		(_renderCtrl.bgTopColor().green() + _renderCtrl.bgBotColor().green()) / 2,
		(_renderCtrl.bgTopColor().blue() + _renderCtrl.bgBotColor().blue()) / 2,
		(_renderCtrl.bgTopColor().alpha() + _renderCtrl.bgBotColor().alpha()) / 2);
	const QColor contrastColor = (averageBackgroundColor.lightnessF() < 0.5)
		? QColor(255, 255, 255)
		: QColor(0, 0, 0);
	const QColor viewerTextColor = contrastColor;
	const bool darkBackground = averageBackgroundColor.lightnessF() < 0.5;
	const QColor panelFieldColor = darkBackground
		? QColor(24, 24, 24, 210)
		: QColor(255, 255, 255, 215);
	const QColor panelTextColor = (panelFieldColor.lightnessF() < 0.5)
		? QColor(255, 255, 255)
		: QColor(0, 0, 0);
	const QColor panelFieldBorderColor = darkBackground
		? QColor(255, 255, 255, 85)
		: QColor(0, 0, 0, 65);
	QColor treeBaseColor = darkBackground
		? QColor(255, 255, 255, 190)
		: QColor(32, 32, 32, 165);
	// Zeroed after computing the RGB (still used below for treeTextColor's
	// lightness check, which ignores alpha) - fully transparent so the dead
	// space below the last row, and every even-indexed row, matches the
	// fully-transparent wrapper instead of showing a solid tinted block.
	treeBaseColor.setAlpha(0);
	const QColor treeAlternateColor = darkBackground
		? QColor(245, 245, 245, 190)
		: QColor(52, 52, 52, 165);
	const QColor treeTextColor = (treeBaseColor.lightnessF() < 0.5)
		? QColor(255, 255, 255)
		: QColor(0, 0, 0);

	wrapper->setStyleSheet(QString(
		"QWidget#%1 {"
		"  background-color: rgba(255, 255, 255, 0%);"
		"  border: none;"
		"}"
		"QWidget#%1 QLineEdit {"
		"  background-color: rgba(%5, %6, %7, %8);"
		"  color: rgb(%21, %22, %23);"
		"  border: 1px solid rgba(%9, %10, %11, %12);"
		"  border-radius: 4px;"
		"  padding: 2px 6px;"
		"}"
		"QWidget#%1 QTreeWidget {"
		"  background-color: rgba(%13, %14, %15, %16);"
		"  alternate-background-color: rgba(%17, %18, %19, %20);"
		"  color: rgb(%24, %25, %26);"
		"  border: none;"
		"}"
		/* Tab bar: transparent background, tinted tabs that adapt to dark/light bg */
		"QWidget#%1 QTabBar {"
		"  background-color: transparent;"
		"}"
		"QWidget#%1 QTabWidget,"
		"QWidget#%1 QStackedWidget,"
		"QWidget#%1 QTabWidget::pane {"
		"  background: transparent;"
		"  border: none;"
		"}"
		"QWidget#%1 QWidget[transparentOverlaySurface=\"true\"] {"
		"  background: transparent;"
		"  border: none;"
		"}"
		"QWidget#%1 QTabBar::tab {"
		"  background-color: rgba(%2, %3, %4, 40);"
		"  color: rgb(%21, %22, %23);"
		"  border-radius: 4px;"
		"  padding: 3px 10px;"
		"  margin-right: 2px;"
		"}"
		"QWidget#%1 QTabBar::tab:selected {"
		"  background-color: rgba(%2, %3, %4, 110);"
		"}"
		"QWidget#%1 QTabBar::tab:hover:!selected {"
		"  background-color: rgba(%2, %3, %4, 65);"
		"}"
		"QWidget#%1 QLabel,"
		"QWidget#%1 QCheckBox,"
		"QWidget#%1 QRadioButton,"
		"QWidget#%1 QGroupBox,"
		"QWidget#%1 QPushButton,"
		"QWidget#%1 QToolButton,"
		"QWidget#%1 QLineEdit,"
		"QWidget#%1 QSpinBox,"
		"QWidget#%1 QDoubleSpinBox,"
		"QWidget#%1 QComboBox {"
		"  color: rgb(%21, %22, %23);"
		"}")
		.arg(objectName)
		.arg(contrastColor.red())
		.arg(contrastColor.green())
		.arg(contrastColor.blue())
		.arg(panelFieldColor.red())
		.arg(panelFieldColor.green())
		.arg(panelFieldColor.blue())
		.arg(panelFieldColor.alpha())
		.arg(panelFieldBorderColor.red())
		.arg(panelFieldBorderColor.green())
		.arg(panelFieldBorderColor.blue())
		.arg(panelFieldBorderColor.alpha())
		.arg(treeBaseColor.red())
		.arg(treeBaseColor.green())
		.arg(treeBaseColor.blue())
		.arg(treeBaseColor.alpha())
		.arg(treeAlternateColor.red())
		.arg(treeAlternateColor.green())
		.arg(treeAlternateColor.blue())
		.arg(treeAlternateColor.alpha())
		.arg(panelTextColor.red())
		.arg(panelTextColor.green())
		.arg(panelTextColor.blue())
		.arg(treeTextColor.red())
		.arg(treeTextColor.green())
		.arg(treeTextColor.blue()));

	QPalette wrapperPalette = wrapper->palette();
	wrapperPalette.setColor(QPalette::WindowText, contrastColor);
	wrapperPalette.setColor(QPalette::Text, contrastColor);
	wrapperPalette.setColor(QPalette::ButtonText, contrastColor);
	wrapperPalette.setColor(QPalette::HighlightedText, darkBackground ? QColor(255, 255, 255) : QColor(0, 0, 0));
	wrapper->setPalette(wrapperPalette);
	wrapper->setProperty("overlayPanelLightText", panelTextColor.lightnessF() > 0.5);
	wrapper->setProperty("overlayViewerLightText", viewerTextColor.lightnessF() > 0.5);
	wrapper->setProperty("overlayPanelTreeLightText", treeTextColor.lightnessF() > 0.5);

	const auto navigationDescendants = wrapper->findChildren<QWidget*>();
	for (QWidget* child : navigationDescendants)
	{
		if (!child)
			continue;

		const bool treeLike = qobject_cast<QTreeView*>(child) != nullptr;
		const bool transparentOverlayText = child->property("transparentOverlayText").toBool();
		const QColor childTextColor = (treeLike || transparentOverlayText) ? viewerTextColor : panelTextColor;
		child->setProperty("overlayPanelLightText", childTextColor.lightnessF() > 0.5);
		child->setProperty("overlayViewerLightText", viewerTextColor.lightnessF() > 0.5);
		QPalette palette = child->palette();
		palette.setColor(QPalette::WindowText, childTextColor);
		palette.setColor(QPalette::Text, childTextColor);
		palette.setColor(QPalette::ButtonText, childTextColor);
		palette.setColor(QPalette::HighlightedText, treeLike ? viewerTextColor : panelTextColor);
		child->setPalette(palette);

		if (auto* treeView = qobject_cast<QTreeView*>(child))
		{
			QPalette viewportPalette = treeView->viewport()->palette();
			viewportPalette.setColor(QPalette::WindowText, viewerTextColor);
			viewportPalette.setColor(QPalette::Text, viewerTextColor);
			viewportPalette.setColor(QPalette::ButtonText, viewerTextColor);
			viewportPalette.setColor(QPalette::HighlightedText, viewerTextColor);
			treeView->viewport()->setPalette(viewportPalette);
			treeView->viewport()->update();
			treeView->update();
		}
		else if (auto* variantsPanel = qobject_cast<MaterialVariantsPanel*>(child))
		{
			variantsPanel->refreshDetachedOverlayTheme();
		}
		else if (auto* animationsPanel = qobject_cast<AnimationsPanel*>(child))
		{
			animationsPanel->refreshDetachedOverlayTheme();
		}
	}
}

void ViewportWidget::refreshNavigationOverlayStyle()
{
	if (_navigationOverlayPanel)
		applyOverlayPanelStyle(_navigationOverlayPanel, QStringLiteral("navigationOverlayPanel"));
}

void ViewportWidget::setClippingPlaneHatchMode(ClippingPlaneHatchMode mode)
{
	_renderCtrl.setHatchMode(mode);
	update();
}

void ViewportWidget::setClippingPlaneHatchPattern(HatchPattern pattern)
{
	_renderCtrl.setHatchPattern(pattern);
	update();
}

void ViewportWidget::setHatchTiling(int tiling)
{
	_renderCtrl.setHatchTiling(tiling);
	update();
}

void ViewportWidget::setHatchLineThickness(float width)
{
	_renderCtrl.setHatchThickness(width);
	update();
}

void ViewportWidget::setHatchIntensity(float spacing)
{
	_renderCtrl.setHatchIntensity(spacing);
	update();
}

void ViewportWidget::setHatchLayers(int layers)
{
	_renderCtrl.setHatchLayers(layers);
	update();
}

void ViewportWidget::setHatchLineColor(const QColor& color)
{
	_renderCtrl.setHatchLineColor(QVector3D(color.redF(), color.greenF(), color.blueF()));
}

void ViewportWidget::setHatchTexture(const QString& path)
{
	_renderCtrl.setHatchTexturePath(path);
	_renderCtrl.setCappingTexture(loadTextureFromFile(path.toStdString().c_str()));
	glActiveTexture(GL_TEXTURE6);
	glBindTexture(GL_TEXTURE_2D, _renderCtrl.cappingTexture());
	update();
}

void ViewportWidget::showAxis(bool show)
{
	_viewCtrl.setShowAxis(show);
	_renderCtrl.fgShader()->bind();
	_renderCtrl.fgShader()->setUniformValue("showAxis", _viewCtrl.showAxis());
	update();
}

void ViewportWidget::showTransformGizmoForSelection(bool show)
{
	if (!show && _viewCtrl.transformGizmoTranslating())
		finishTransformGizmoTranslationDrag(false);
	if (!show && _viewCtrl.transformGizmoScaling())
		finishTransformGizmoScaleDrag(false);
	if (!show && _viewCtrl.transformGizmoRotating())
		finishTransformGizmoRotationDrag(false);
	_viewCtrl.setTransformGizmoRequested(show);
	if (show)
		MainWindow::showStatusMessage(
			_explodedViewCtrl.isManualPlacementActive()
				? tr("Manual exploded placement active: translate or rotate the selected meshes to refine the staged pose")
				: tr("Transform gizmo active: drag the corner box handle to scale uniformly"),
			5000);
	else
		MainWindow::showStatusMessage(QString(), 0);
	syncTransformGizmoToSelection();
	update();
}

bool ViewportWidget::beginExplodedViewManualPlacement(const QVector<QUuid>& selectionUuids)
{
	if (!_viewer || !_selectionManager)
		return false;

	_explodedViewCtrl.resetSessionTransforms();
	if (!selectionUuids.isEmpty())
	{
		for (const QUuid& uuid : selectionUuids)
		{
			SceneMesh* mesh = getMeshByUuid(uuid);
			if (uuid.isNull() || !mesh)
				continue;

			_explodedViewCtrl.manualPlacementSessionUuids().insert(uuid);
			_explodedViewCtrl.manualSessionStartStates().insert(uuid, TransformState(
				mesh->getExplodedViewTranslation(),
				mesh->getExplodedViewRotation(),
				mesh->getExplodedViewScaling(),
				mesh->getExplodedViewRotationQuaternion()));
			_explodedViewCtrl.manualSessionStartMatrices().insert(uuid, mesh->getExplodedViewTransformation());
			if (_explodedViewCtrl.manualOriginalStates().contains(uuid))
				continue;

			_explodedViewCtrl.manualOriginalStates().insert(uuid, TransformState(
				mesh->getExplodedViewTranslation(),
				mesh->getExplodedViewRotation(),
				mesh->getExplodedViewScaling(),
				mesh->getExplodedViewRotationQuaternion()));
		}
	}
	else
	{
		const QList<int> selectedIds = _selectionManager->getSelectedIds();
		if (selectedIds.isEmpty())
			return false;

		for (int id : selectedIds)
		{
			const QUuid uuid = getUuidByIndex(id);
			SceneMesh* mesh = getMeshByIndex(id);
			if (uuid.isNull() || !mesh)
				continue;

			_explodedViewCtrl.manualPlacementSessionUuids().insert(uuid);
			_explodedViewCtrl.manualSessionStartStates().insert(uuid, TransformState(
				mesh->getExplodedViewTranslation(),
				mesh->getExplodedViewRotation(),
				mesh->getExplodedViewScaling(),
				mesh->getExplodedViewRotationQuaternion()));
			_explodedViewCtrl.manualSessionStartMatrices().insert(uuid, mesh->getExplodedViewTransformation());
			if (_explodedViewCtrl.manualOriginalStates().contains(uuid))
				continue;

			_explodedViewCtrl.manualOriginalStates().insert(uuid, TransformState(
				mesh->getExplodedViewTranslation(),
				mesh->getExplodedViewRotation(),
				mesh->getExplodedViewScaling(),
				mesh->getExplodedViewRotationQuaternion()));
		}
	}

	if (_explodedViewCtrl.manualPlacementSessionUuids().isEmpty())
		return false;

	_explodedViewCtrl.setManualPlacementActive(true);
	_explodedViewCtrl.setManualSessionStartPivot(computeTransformGizmoPivot());
	showTransformGizmoForSelection(true);
	emit explodedViewManualPlacementChanged();
	return true;
}

bool ViewportWidget::hasExplodedViewManualTransformChanges() const
{
	for (auto it = _explodedViewCtrl.manualOriginalStates().cbegin(); it != _explodedViewCtrl.manualOriginalStates().cend(); ++it)
	{
		TransformState currentState;
		if (_explodedViewCtrl.isManualPlacementSuppressed())
		{
			auto hiddenIt = _explodedViewCtrl.manualHiddenStates().find(it.key());
			if (hiddenIt == _explodedViewCtrl.manualHiddenStates().end())
				continue;
			currentState = hiddenIt.value();
		}
		else
		{
			const SceneMesh* mesh = getMeshByUuid(it.key());
			if (!mesh)
				continue;

			currentState = TransformState(
				mesh->getExplodedViewTranslation(),
				mesh->getExplodedViewRotation(),
				mesh->getExplodedViewScaling(),
				mesh->getExplodedViewRotationQuaternion());
		}
		if (!ExplodedViewRuntimeController::transformStatesNearlyEqual(it.value(), currentState))
			return true;
	}

	return false;
}

QSet<QUuid> ViewportWidget::explodedViewManualPlacementUuids() const
{
	QSet<QUuid> uuids;
	for (auto it = _explodedViewCtrl.manualOriginalStates().cbegin(); it != _explodedViewCtrl.manualOriginalStates().cend(); ++it)
	{
		TransformState currentState;
		if (_explodedViewCtrl.isManualPlacementSuppressed())
		{
			auto hiddenIt = _explodedViewCtrl.manualHiddenStates().find(it.key());
			if (hiddenIt == _explodedViewCtrl.manualHiddenStates().end())
				continue;
			currentState = hiddenIt.value();
		}
		else
		{
			const SceneMesh* mesh = getMeshByUuid(it.key());
			if (!mesh)
				continue;

			currentState = TransformState(
				mesh->getExplodedViewTranslation(),
				mesh->getExplodedViewRotation(),
				mesh->getExplodedViewScaling(),
				mesh->getExplodedViewRotationQuaternion());
		}
		if (!ExplodedViewRuntimeController::transformStatesNearlyEqual(it.value(), currentState))
			uuids.insert(it.key());
	}

	return uuids;
}

QVector3D ViewportWidget::explodedViewManualPlacementTranslationDelta() const
{
	return _explodedViewCtrl.manualSessionTranslationDelta();
}

QVector3D ViewportWidget::explodedViewManualPlacementRotationDelta() const
{
	return _explodedViewCtrl.manualSessionRotationEuler();
}

QMap<QUuid, TransformState> ViewportWidget::explodedViewManualStates() const
{
	QMap<QUuid, TransformState> states;
	for (auto it = _explodedViewCtrl.manualOriginalStates().cbegin(); it != _explodedViewCtrl.manualOriginalStates().cend(); ++it)
	{
		TransformState currentState;
		if (_explodedViewCtrl.isManualPlacementSuppressed())
		{
			auto hiddenIt = _explodedViewCtrl.manualHiddenStates().find(it.key());
			if (hiddenIt == _explodedViewCtrl.manualHiddenStates().end())
				continue;
			currentState = hiddenIt.value();
		}
		else
		{
			const SceneMesh* mesh = getMeshByUuid(it.key());
			if (!mesh)
				continue;

			currentState = TransformState(
				mesh->getExplodedViewTranslation(),
				mesh->getExplodedViewRotation(),
				mesh->getExplodedViewScaling(),
				mesh->getExplodedViewRotationQuaternion());
		}
		if (!ExplodedViewRuntimeController::transformStatesNearlyEqual(it.value(), currentState))
			states.insert(it.key(), currentState);
	}

	return states;
}

void ViewportWidget::restoreExplodedViewManualStates(const QMap<QUuid, TransformState>& states)
{
	clearExplodedViewManualPlacement();
	if (states.isEmpty())
		return;

	_explodedViewCtrl.manualOriginalStates().clear();
	for (auto it = states.cbegin(); it != states.cend(); ++it)
	{
		SceneMesh* mesh = getMeshByUuid(it.key());
		if (!mesh)
			continue;

		_explodedViewCtrl.manualOriginalStates().insert(it.key(), TransformState());
		ExplodedViewRuntimeController::applyExplodedViewTransformState(mesh, it.value(), false);

		const QVector3D savedExplosionOffset = mesh->explosionOffset();
		mesh->setExplosionOffset(QVector3D());
		mesh->fullUpdateRuntimeBounds();
		mesh->setExplosionOffset(savedExplosionOffset);
		mesh->setSceneRenderTransformFast(mesh->getSceneRenderTransform());
	}

	_renderCtrl.setShadowMapNeedsInitialization(true);
	update();
	emit explodedViewManualPlacementChanged();
}

void ViewportWidget::setExplodedViewManualPlacementTranslationDelta(const QVector3D& delta)
{
	if (!_explodedViewCtrl.isManualPlacementActive() || _explodedViewCtrl.manualSessionStartStates().isEmpty())
		return;

	_explodedViewCtrl.setManualSessionTranslationDelta(delta);
	applyExplodedViewManualPlacementSessionTransform();
	emit explodedViewManualPlacementChanged();
}

void ViewportWidget::setExplodedViewManualPlacementRotationDelta(const QVector3D& delta)
{
	if (!_explodedViewCtrl.isManualPlacementActive() || _explodedViewCtrl.manualSessionStartStates().isEmpty())
		return;

	_explodedViewCtrl.setManualSessionRotationEuler(delta);
	_explodedViewCtrl.setManualSessionRotationQuat(MeshMathUtils::quaternionFromMeshEuler(delta));
	applyExplodedViewManualPlacementSessionTransform();
	emit explodedViewManualPlacementChanged();
}

void ViewportWidget::finishExplodedViewManualPlacement()
{
	QVector<QUuid> changedUuids;
	changedUuids.reserve(_explodedViewCtrl.manualOriginalStates().size());
	for (auto it = _explodedViewCtrl.manualOriginalStates().cbegin(); it != _explodedViewCtrl.manualOriginalStates().cend(); ++it)
	{
		SceneMesh* mesh = getMeshByUuid(it.key());
		if (!mesh)
			continue;

		const TransformState currentState(
			mesh->getExplodedViewTranslation(),
			mesh->getExplodedViewRotation(),
			mesh->getExplodedViewScaling(),
			mesh->getExplodedViewRotationQuaternion());
		if (!ExplodedViewRuntimeController::transformStatesNearlyEqual(it.value(), currentState))
			changedUuids.append(it.key());
	}

	showTransformGizmoForSelection(false);
	_explodedViewCtrl.setManualPlacementActive(false);
	_explodedViewCtrl.resetSessionTransforms();

	for (const QUuid& uuid : changedUuids)
	{
		if (SceneMesh* mesh = getMeshByUuid(uuid))
		{
			const QVector3D savedExplosionOffset = mesh->explosionOffset();
			mesh->setExplosionOffset(QVector3D());
			mesh->fullUpdateRuntimeBounds();
			mesh->setExplosionOffset(savedExplosionOffset);
			mesh->setSceneRenderTransformFast(mesh->getSceneRenderTransform());
		}
	}

	syncMeshSelectionVisualState();

	_renderCtrl.setShadowMapNeedsInitialization(true);
	update();
	emit explodedViewManualPlacementChanged();
}

void ViewportWidget::clearExplodedViewManualPlacement()
{
	QHash<QUuid, QVector3D> savedExplosionOffsets;
	savedExplosionOffsets.reserve(static_cast<int>(_sceneRuntime.meshStore().size()));
	for (const SceneMeshRecord& meshRecord : _sceneRuntime.meshStore())
	{
		SceneMesh* mesh = meshRecord.mesh;
		if (!mesh)
			continue;

		savedExplosionOffsets.insert(mesh->uuid(), mesh->explosionOffset());
		mesh->setExplosionOffset(QVector3D());
	}

	// Restore meshes to their original states before clearing the session.
	for (auto it = _explodedViewCtrl.manualOriginalStates().cbegin(); it != _explodedViewCtrl.manualOriginalStates().cend(); ++it)
	{
		SceneMesh* mesh = getMeshByUuid(it.key());
		if (!mesh)
			continue;

		ExplodedViewRuntimeController::applyExplodedViewTransformState(mesh, it.value(), false);
	}

	for (const SceneMeshRecord& meshRecord : _sceneRuntime.meshStore())
	{
		SceneMesh* mesh = meshRecord.mesh;
		if (!mesh)
			continue;

		mesh->setExplosionOffset(savedExplosionOffsets.value(mesh->uuid()));
		mesh->setSceneRenderTransformFast(mesh->getSceneRenderTransform());
	}

	_explodedViewCtrl.clearManualPlacement();
	showTransformGizmoForSelection(false);
	_renderCtrl.setShadowMapNeedsInitialization(true);
	update();
	emit explodedViewManualPlacementChanged();
}

void ViewportWidget::showShadows(bool show)
{
	_renderCtrl.setShadowsEnabled(show);
	_renderCtrl.fgShader()->bind();
	_renderCtrl.fgShader()->setUniformValue("shadowsEnabled", _renderCtrl.shadowsEnabled());
	// Ray-traced mode reads shadowsEnabled fresh into every new scene
	// snapshot it builds (see the RtSceneBuilder::build() call site) - a
	// snapshot only gets rebuilt when the idle-settle countdown fires, not
	// on every raster-state change, so this needs an explicit re-arm to
	// pick the new value up rather than continuing to show whatever was
	// already converged under the old setting.
	if (_rtInteractionCtrl->armed())
		_rtInteractionCtrl->notifySceneContentMutated();
	update();
}

void ViewportWidget::showSelfShadows(bool show)
{
	_renderCtrl.setSelfShadowsEnabled(show);
	_renderCtrl.fgShader()->bind();
	_renderCtrl.fgShader()->setUniformValue("selfShadowsEnabled", _renderCtrl.selfShadowsEnabled());
	// See showShadows() above for why this needs an explicit re-arm too.
	if (_rtInteractionCtrl->armed())
		_rtInteractionCtrl->notifySceneContentMutated();
	update();
}

void ViewportWidget::showEnvironment(bool show)
{
	_renderCtrl.setEnvMapEnabled(show);
	_renderCtrl.fgShader()->bind();
	_renderCtrl.fgShader()->setUniformValue("envMapEnabled", _renderCtrl.envMapEnabled());
	update();
	notifyRayTracedSceneMutated();
}

void ViewportWidget::showSkyBox(bool show)
{
	_renderCtrl.setSkyBoxEnabled(show);
	_renderCtrl.fgShader()->bind();
	_renderCtrl.fgShader()->setUniformValue("skyBoxEnabled", _renderCtrl.skyBoxEnabled());
	update();

	// Toggling this doesn't go through the undo stack (it's a display/
	// visualization setting, not a document edit), so onUndoStackChanged()'s
	// notifyRayTracedSceneMutated() hook never sees it - without an
	// explicit restart, an already-converged ray-traced frame captured
	// with the old showBackground state would just keep being displayed
	// unchanged. Camera-grade restart only (was notifyRayTracedSceneMutated,
	// downgraded): showBackground is a per-launch environment scalar now
	// (see RtOptixSceneTracer::renderScene()), so bumping the scene revision
	// here only forced a pointless full GPU GAS/texture rebuild.
	_rtInteractionCtrl->notifySceneContentMutated();
}

void ViewportWidget::showReflections(bool show)
{
	_renderCtrl.setReflectionsEnabled(show);
	_renderCtrl.fgShader()->bind();
	_renderCtrl.fgShader()->setUniformValue("reflectionsEnabled", _renderCtrl.reflectionsEnabled());
	update();

	// Same as showSkyBox() - this doesn't go through the undo stack, so
	// onUndoStackChanged()'s notifyRayTracedSceneMutated() hook never sees
	// it; without this, an already-converged ray-traced frame captured
	// with the old floor-roughness override would just keep being displayed
	// unchanged.
	notifyRayTracedSceneMutated();
}

void ViewportWidget::setShadowCatcherDarkness(float darkness)
{
	_renderCtrl.setShadowCatcherDarkness(darkness);
	update();
	notifyRayTracedSceneMutated();
}

void ViewportWidget::setShadowCatcherBaseColor(const QVector3D& color)
{
	_renderCtrl.setShadowCatcherBaseColor(color);
	update();
	notifyRayTracedSceneMutated();
}

void ViewportWidget::setShadowCatcherMetalness(float metalness)
{
	_renderCtrl.setShadowCatcherMetalness(metalness);
	update();
	notifyRayTracedSceneMutated();
}

void ViewportWidget::setShadowCatcherRoughness(float roughness)
{
	_renderCtrl.setShadowCatcherRoughness(roughness);
	update();
	notifyRayTracedSceneMutated();
}

void ViewportWidget::setGroundMode(GroundMode mode)
{
	if (_renderCtrl.groundMode() == mode)
		return;

	_renderCtrl.setGroundMode(mode);
	updateFloorPlane();
	update();
	emit floorShown(_renderCtrl.groundMode() == GroundMode::Floor);

	// Adding/removing the floor is a genuine GEOMETRY change (RtSceneBuilder::
	// build() only calls addFloorInstance() when groundMode==Floor - see its
	// own doc comment), unlike the lightweight environment scalars that
	// deliberately flow per-launch without a revision bump. CPU's session
	// rebuilds its Embree scene unconditionally on every start() regardless
	// of revision (see RtRayTracingSession::start()), so it picks up a floor
	// toggle on its very next render for free; GPU's RtOptixSceneTracer::
	// buildScene() only rebuilds the GAS/IAS when the scene revision actually
	// changes (see RtOptixRayTracingSession::start()'s revision-gate), so
	// without this it kept reusing whichever GAS/IAS (with or without a
	// floor instance) happened to be built before the toggle - same bug
	// class as the earlier skybox-visibility fix, just for real geometry
	// instead of a scalar.
	notifyRayTracedSceneMutated();
}

void ViewportWidget::setFloorTexture(QImage img)
{
	_floorTexImage = convertToGLFormat(img);
	syncFloorPlaneAlbedoTexture();
	// Same bug class as setGroundMode()'s identical fix just above - PT's
	// snapshot correctly re-reads the floor's live Material/texture on every
	// build() call, but GPU's RtOptixSceneTracer::buildScene() only actually
	// RUNS (re-uploading the new texture) when the scene revision changes -
	// without this, a floor texture edit stayed stale on GPU (picked up
	// immediately on CPU, which rebuilds unconditionally every session
	// start) until something ELSE happened to bump the revision.
	notifyRayTracedSceneMutated();
}

void ViewportWidget::showFloorTexture(bool show)
{
	_renderCtrl.setFloorTextureDisplayed(show);
	syncFloorPlaneAlbedoTexture();
	notifyRayTracedSceneMutated(); // see setFloorTexture()'s identical doc comment
}

void ViewportWidget::addToDisplay(SceneMesh* mesh)
{	
	if(mesh == nullptr)
	{
		qDebug() << "Error: Attempted to add a null mesh to display.";
		return;
	}
	_sceneRuntime.addMeshToDisplay(mesh);

	//if(_sceneRuntime.progressiveLoadingEnabled())
		//_viewer->updateDisplayList();	
}

void ViewportWidget::removeFromDisplay(int index)
{
	const bool wasSwapped = _sceneRuntime.visibleSwapped();
	SceneMesh* mesh = _sceneRuntime.detachMeshAt(index);
	if (!mesh)
		return;
	delete mesh;
	if (wasSwapped && !_sceneRuntime.visibleSwapped())
		emit visibleSwapped(_sceneRuntime.visibleSwapped());
	// If display list is empty, clear punctual lights
		if (_sceneRuntime.displayedObjectsIds().empty())
	{
		_animCtrl.clearParsedLights();
		_animCtrl.clearAllAnimatedState();
		_renderCtrl.punctualLights()->setLights({});
		syncPunctualLightUniforms(0, false);
	}
}

void ViewportWidget::centerScreen(std::vector<int> selectedIDs)
{
	_sceneRuntime.centerScreenObjectIDs().clear();
	_sceneRuntime.centerScreenObjectIDs() = selectedIDs;
	_viewCtrl.resetSelectionBoundingSphere();
	if (_displayedObjectsMemSize > MAX_MODEL_SIZE_BYTES)
		_renderCtrl.setLowResEnabled(true);
	int count = 0;
	for (int id : _sceneRuntime.centerScreenObjectIDs())
	{
		SceneMesh* mesh = _sceneRuntime.meshAt(id);
		if (mesh)
		{
			if (count == 0)
				_viewCtrl.setSelectionBoundingSphere(mesh->getBoundingSphere());
			else
				_viewCtrl.addSelectionBoundingSphere(mesh->getBoundingSphere());
		}
		count++;
	}
	if (!_animateCenterScreenTimer->isActive())
	{
		_keyboardNavTimer->stop();
		_animateCenterScreenTimer->start(5);
		_viewCtrl.resetSlerpStep();
	}
}

bool ViewportWidget::loadAssImpModel(const QString& fileName, const UVMethod& uvMethod, QString& error, bool progressiveLoading)
{
	_sceneRuntime.setProgressiveLoadingEnabled(progressiveLoading);
	_sceneRuntime.setCancelRequested(false);
	_sceneRuntime.setLoadCancelled(false);
	_sceneRuntime.pendingSceneUuids().clear();
	const bool hadExistingMeshes = !_sceneRuntime.meshStore().empty();
	MainWindow::clearFileLoadCancel();
	bool success = false;

	makeCurrent();
	QString displayFileName = fileName;
	if (fileName.length() > 100)
	{
		// Extract just the filename from the full path
		QString fileOnly = fileName.section('/', -1);
		// Calculate how much of the path to truncate
		int remainingLength = 100 - fileOnly.length() - 3; // 3 for "..."
		QString truncatedPath = fileName.left(remainingLength).section('/', 0, -2);
		displayFileName = truncatedPath + "/.../" + fileOnly;
	}
	MainWindow::showStatusMessage(tr("Reading file: ") + displayFileName);
	MainWindow::showProgressBar();
	if (_assimpModelLoader)
	{
		AssImpModelLoader* loadingWorker = new AssImpModelLoader();
		loadingWorker->setUVDecisionCallback(
			[this](int totalTriangles, UVMethod currentMethod) -> UVMethod {
				if (QThread::currentThread() != this->thread())
				{
					UVMethod result = currentMethod;
					QMetaObject::invokeMethod(this, [this, totalTriangles, currentMethod, &result]() {
						result = promptLargeModelUVDecision(totalTriangles, currentMethod);
						}, Qt::BlockingQueuedConnection);
					return result;
				}
				return promptLargeModelUVDecision(totalTriangles, currentMethod);
			});

		QEventLoop waitLoop;
		QThread loadingThread;
		bool loadingCompleted = false;

		QMetaObject::Connection finishedConnection = connect(
			loadingWorker,
			&AssImpModelLoader::loadingFinished,
			this,
			[this, loadingWorker, &success, &error, &loadingCompleted, &waitLoop](bool successFlag, const aiScene* /*scene*/) {
				loadingCompleted = true;
				success = successFlag;
				error = loadingWorker->getErrorMessage();
				if (error == "Model loading cancelled by user.")
				{
					_sceneRuntime.setLoadCancelled(true);
				}
				waitLoop.quit();
			},
			Qt::QueuedConnection);

		QMetaObject::Connection cancelledConnection = connect(
			loadingWorker,
			&AssImpModelLoader::loadingCancelled,
			this,
			[this, &error]() {
				_sceneRuntime.setLoadCancelled(true);
				error = "Model loading cancelled by user.";
			},
			Qt::QueuedConnection);

		QMetaObject::Connection fileProgressConnection = connect(
			loadingWorker,
			&AssImpModelLoader::fileReadProcessed,
			this,
			&ViewportWidget::showFileReadingProgress,
			Qt::QueuedConnection);

		QMetaObject::Connection meshProgressConnection = connect(
			loadingWorker,
			&AssImpModelLoader::verticesProcessed,
			this,
			&ViewportWidget::showMeshLoadingProgress,
			Qt::QueuedConnection);

		QMetaObject::Connection nodeProgressConnection = connect(
			loadingWorker,
			&AssImpModelLoader::nodeMeshProgressUpdated,
			this,
			&ViewportWidget::showNodeMeshLoadingProgress,
			Qt::QueuedConnection);

		QMetaObject::Connection upAxisConnection = connect(
			loadingWorker,
			&AssImpModelLoader::sceneUpAxisDetected,
			this,
			[this, hadExistingMeshes](SceneUpAxis sceneUpAxis, bool autoOrientCameraEnabled) {
				if (!autoOrientCameraEnabled || hadExistingMeshes)
					return;
				applyAutoOrientCameraConvention(sceneUpAxis);
			},
			Qt::BlockingQueuedConnection);

		QMetaObject::Connection batchConnection = connect(
			loadingWorker,
			&AssImpModelLoader::meshBatchReady,
			this,
			&ViewportWidget::onMeshBatchReady,
			Qt::BlockingQueuedConnection);

		QMetaObject::Connection cancelRequestConnection = connect(
			this,
			&ViewportWidget::loadingAssImpModelCancelled,
			loadingWorker,
			&AssImpModelLoader::cancelLoading,
			Qt::QueuedConnection);

		QMetaObject::Connection lightsConnection = connect(
			loadingWorker,
			&AssImpModelLoader::lightsLoaded,
			this,
			[this](const GltfLightData& lights) {
				setParsedLights(lights);
			},
			Qt::QueuedConnection);

		loadingWorker->moveToThread(&loadingThread);

		loadingThread.start();
		QMetaObject::invokeMethod(
			loadingWorker,
			[loadingWorker, fileName, uvMethod, progressiveLoading]() {
				loadingWorker->setUVGenerationMethod(uvMethod);
				loadingWorker->loadModel(fileName.toStdString(), progressiveLoading);
			},
			Qt::QueuedConnection);

		waitLoop.exec();

		loadingThread.quit();
		loadingThread.wait();

		if (!loadingCompleted)
		{
			success = false;
			if (error.isEmpty())
			{
				error = tr("Model loading did not finish correctly.");
			}
		}

		makeCurrent();
		std::vector<AssImpMeshData> meshes = loadingWorker->getMeshes();
		if (meshes.empty())
		{
			if (error.isEmpty())
			{
				error = loadingWorker->getErrorMessage();
			}
			success = false;
			if (error == "Model loading cancelled by user.")
			{
				_sceneRuntime.setLoadCancelled(true);
			}
		}
		else
		{
			success = true;
			if (!_sceneRuntime.progressiveLoadingEnabled())
			{
				for (const AssImpMeshData& meshData : meshes)
				{
					SceneMesh* mesh = createMeshFromData(meshData);
					addToDisplay(mesh);
					_sceneRuntime.pendingSceneUuids().append(mesh->uuid());
				}
			}
		}

		_sceneRuntime.setAssimpScene(loadingWorker->getScene());
		_sceneRuntime.globalSceneTransform() = loadingWorker->getGlobalSceneTransform();
		const bool autoOrientCameraEnabled = loadingWorker->isAutoOrientActive();
		const SceneUpAxis sceneUpAxis = loadingWorker->sceneUpAxis();
		if (_sceneRuntime.assimpScene())
		{
			// Populate the SceneGraph before the scene is deep-copied and merged,
			// while the original aiNode* tree is still intact and unmodified.
			if (success && !_sceneRuntime.pendingSceneUuids().isEmpty())
			{
				_viewer->sceneGraph()->appendFromScene(
					_sceneRuntime.assimpScene(), fileName, _sceneRuntime.pendingSceneUuids(),
					SceneUtils::glmToAiMatrix(_sceneRuntime.globalSceneTransform()),
					loadingWorker->wasAutoOrientApplied(),
					loadingWorker->wasAutoScaleApplied());

				// Register per-file punctual lights (if any) for this file.
				// setLightData() emits lightDataChanged() which triggers the
				// PunctualLightsPanel to refresh and ViewportWidget to rebuild the GPU list.
				if (!_animCtrl.pendingLightData().isEmpty())
				{
					_animCtrl.pendingLightData().sourceFile = fileName; // authoritative path
					_viewer->sceneGraph()->setLightData(fileName, _animCtrl.pendingLightData());
				}

				// Register KHR_materials_variants data (if any) for this file.
				const GltfVariantData& vd = loadingWorker->getVariantData();
				if (!vd.isEmpty())
					_viewer->sceneGraph()->setVariantData(fileName, vd);

				const GltfAnimationData& ad = loadingWorker->getAnimationData();
				const bool preservesNodeTransforms =
					std::any_of(meshes.begin(), meshes.end(),
						[](const AssImpMeshData& meshData)
						{
							return meshData.preserveNodeTransform;
						});
				if (!ad.isEmpty() || ad.hasSkinning)
				{
					_viewer->sceneGraph()->setAnimationData(fileName, ad);
					_animCtrl.removeAnimationFile(fileName);
					if (_animCtrl.activeAnimationFile() == fileName)
					{
						_animCtrl.animationTimer()->stop();
						_animCtrl.resetPlayback();
					}
					syncFileNodeTransforms(fileName);
					if (!ad.clips.isEmpty())
						setActiveAnimation(fileName, 0);
				}
				else if (preservesNodeTransforms)
				{
					_animCtrl.removeAnimationFile(fileName);
					syncFileNodeTransforms(fileName);
				}

				// Register glTF camera data (if any) for this file.
				const GltfCameraData& cd = loadingWorker->getCameraData();
				if (!cd.isEmpty())
					_viewer->sceneGraph()->setGltfCameraData(fileName, cd);
			}
			_sceneRuntime.pendingSceneUuids().clear();

			// Record how many meshes were in _sceneRuntime.globalScene() BEFORE merging.
			// Each newly loaded SceneMesh has a sceneIndex relative to its
			// own per-model aiScene (0-based).  After mergeScene() appends the
			// new model's meshes starting at oldMeshCount, those per-model
			// indices become stale.  We fix them up here so that every
			// SceneMesh in _sceneRuntime.meshStore() always holds its correct position in
			// _sceneRuntime.globalScene()->mMeshes[], which syncSceneToMeshStore() relies on.
			const unsigned int oldMeshCount =
				_sceneRuntime.globalScene() ? _sceneRuntime.globalScene()->mNumMeshes : 0u;

			aiScene* copiedScene = SceneUtils::deepCopyScene(_sceneRuntime.assimpScene());
			SceneUtils::mergeScene(&_sceneRuntime.globalScene(), copiedScene);

			// Offset the sceneIndices of the meshes that were just added.
			// They are the last meshes.size() entries in _sceneRuntime.meshStore() because
			// addToDisplay() always appends.
			if (oldMeshCount > 0 && !meshes.empty())
			{
				const int newCount = static_cast<int>(meshes.size());
				const int storeSize = static_cast<int>(_sceneRuntime.meshStore().size());
				const int firstNew  = storeSize - newCount;
				for (int i = firstNew; i < storeSize; ++i)
				{
					SceneMesh* tm = _sceneRuntime.meshAt(i);
					if (tm && tm->getSceneIndex() >= 0)
						tm->setSceneIndex(
							static_cast<int>(oldMeshCount) + tm->getSceneIndex());
				}
			}
		}
		_sceneRuntime.setAssimpScene(nullptr);

		if (success && autoOrientCameraEnabled)
		{
			const bool sceneIsZUp = CoordinateSystemHelper::sceneUpAxisIsZUp(sceneUpAxis);
			if (!hadExistingMeshes)
			{
				applyAutoOrientCameraConvention(sceneUpAxis);
			}
			else if (sceneIsZUp != _viewCtrl.cameraUpAxisZUp())
			{
				warnOnConflictingImportedSceneUpAxis(fileName, sceneUpAxis);
			}
		}

		disconnect(finishedConnection);
		disconnect(cancelledConnection);
		disconnect(fileProgressConnection);
		disconnect(meshProgressConnection);
		disconnect(nodeProgressConnection);
		disconnect(upAxisConnection);
		disconnect(batchConnection);
		disconnect(cancelRequestConnection);
		disconnect(lightsConnection);

		loadingWorker->moveToThread(this->thread());
		delete loadingWorker;
	}

	if (_sceneRuntime.loadCancelled())
	{
		if (!_sceneRuntime.meshStore().empty())
		{
			success = true;
		}
		if (_sceneRuntime.meshStore().empty())
		{
			MainWindow::showStatusMessage(tr("Model loading cancelled"), 3000);
		}
		else
		{
			MainWindow::showStatusMessage(
				tr("Model loading cancelled after importing %1 meshes").arg(_sceneRuntime.meshStore().size()),
				4000);
		}
	}
	else
	{
		MainWindow::showStatusMessage("");
	}

	MainWindow::setProgressValue(0);
	MainWindow::hideProgressBar();
	_sceneRuntime.setCancelRequested(false);

	return success;
}

bool ViewportWidget::generateUVsForMeshes(const std::vector<int>& ids, const UVMethod& uvMethod, const UVConfig& uvConfig, QString& error)
{
	int meshCnt = ids.size();
	if (meshCnt == 0)
		return false;
	bool success = true;
	makeCurrent();
	MainWindow::showProgressBar(false);
	MainWindow::setProgressValue(0);
	MainWindow::showStatusMessage(tr("Generating UVs for %1 meshes").arg(meshCnt));
	float count = 0;
	for (int id : ids)
	{
		try
		{
			SceneMesh* mesh = _sceneRuntime.meshAt(id);
			if (mesh)
			{								
				success = _assimpModelLoader->regenerateUVs(mesh, uvMethod, uvConfig);
				if (success)
				{
					MainWindow::showStatusMessage(tr("Updating mesh: ") + mesh->getName());
					int progress = static_cast<int>((++count / meshCnt) * 100.0f);					
					MainWindow::setProgressValue(progress);					
				}
				else
				{
					error = _assimpModelLoader->getErrorMessage();
					success = false;
					break;
				}
			}
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in ViewportWidget::generateUVs\n" << ex.what() << std::endl;
		}
	}
	MainWindow::showStatusMessage("");
	MainWindow::setProgressValue(0);
	MainWindow::hideProgressBar();
	return success;
}


void ViewportWidget::showFileReadingProgress(float percent)
{
	MainWindow::setProgressValue((int)((float)percent * 100.0f));
	makeCurrent();
}

void ViewportWidget::showMeshLoadingProgress(float /*percent*/)
{
	makeCurrent();
}

void ViewportWidget::showNodeMeshLoadingProgress(int processedNodes, int totalNodes, int processedMeshes, int totalMeshes, bool uvProcessed)
{
	QString statusMessage = (uvProcessed) ? tr("Generating UVs... ") : "";
	statusMessage += QString(tr("Processing node: %1/%2  Mesh: %3/%4"))
		.arg(processedNodes)
		.arg(totalNodes)
		.arg(processedMeshes)
		.arg(totalMeshes);
	MainWindow::showStatusMessage(statusMessage);

	if (totalNodes > 0)
	{
		MainWindow::setProgressValue(static_cast<int>((static_cast<float>(processedNodes) / static_cast<float>(totalNodes)) * 100.0f));
	}

	makeCurrent();
}

void ViewportWidget::swapVisible(bool checked)
{
	if (!_sceneRuntime.swapVisible(checked))
		return;
	recalculateVisibleSceneStats(false);
	triggerShadowRecomputation();
	updateFloorPlane();
	fitAll();

	emit visibleSwapped(checked);
}

void ViewportWidget::cancelAssImpModelLoading()
{
	if (_sceneRuntime.cancelRequested())
		return;

	_sceneRuntime.setCancelRequested(true);
	MainWindow::requestFileLoadCancel();
	MainWindow::setCancelButtonEnabled(false);
	MainWindow::setCancelButtonText(tr("Cancelling..."));
	MainWindow::showStatusMessage(tr("Cancelling model load..."));
	emit loadingAssImpModelCancelled();	
}



void ViewportWidget::setMaterialToObjects(const std::vector<int>& ids, const Material& mat)
{
	if (_sceneRuntime.applyMaterialToMeshes(ids, mat))
		setTransmissionEnabled(true);
}

void ViewportWidget::setTexturesToObjects(const std::vector<int>& ids, const Material& mat)
{
	const Material resolved = resolveMaterialTextures(this, mat);
	for (int id : ids)
		_sceneRuntime.applyTextureMapsToMesh(id, resolved);
}

void ViewportWidget::synchronizeTextureCache(const Material* material, Material::TextureType type)
{
	if (!material) return;

	const Material::Texture& matTex = material->texture(type);
	if (matTex.path.empty()) return;

	TextureSamplerSettings samplers{
		matTex.wrapS,
		matTex.wrapT,
		matTex.minFilter,
		matTex.magFilter
	};

	makeCurrent();
	getOrLoadTextureCached(QString::fromStdString(matTex.path), samplers);
}

void ViewportWidget::clearTextureCache()
{
	const std::vector<unsigned int> gpuIds = _sceneRuntime.drainTextureCacheGpuIds();
	for (unsigned int id : gpuIds)
		glDeleteTextures(1, &id);
}

void ViewportWidget::setTransformation(const std::vector<int>& ids, const QVector3D& trans, const QVector3D& rot, const QVector3D& scale)
{
	_sceneRuntime.setMeshTransforms(ids, trans, rot, scale);

	// Lights/cameras follow automatically: updatePunctualLights() derives the
	// per-file user transform straight from the meshes' TRS state.
	recalculateVisibleSceneStats(false);
	if (_explodedViewPanel && _explodedViewPanel->isVisible())
		updateExplosion();
	updatePunctualLights();
	triggerShadowRecomputation();
	updateFloorPlane();
	fitAll();
}

void ViewportWidget::resetTransformation(const std::vector<int>& ids)
{
	_sceneRuntime.resetMeshTransforms(ids);

	// Reset light offsets when model transformations are reset
	_renderCtrl.setLightOffset(QVector3D(0.0f, 0.0f, 0.0f));

	recalculateVisibleSceneStats(false);
	if (_explodedViewPanel && _explodedViewPanel->isVisible())
		updateExplosion();
	updatePunctualLights();
	fitAll();
	triggerShadowRecomputation();
}

void ViewportWidget::applyTransforms(const QMap<int, TransformState>& transforms, bool fitView)
{
	if (transforms.isEmpty())
		return;

	makeCurrent();

	_sceneRuntime.applyMeshTransforms(transforms);
	const bool isModelLevelTransform = _sceneRuntime.isModelLevelTransform(transforms.size());

	// Lights/cameras follow automatically: updatePunctualLights() derives the
	// per-file user transform straight from the meshes' TRS state.

	// Update all dependent systems once
	recalculateVisibleSceneStats(false);
	if (_explodedViewPanel && _explodedViewPanel->isVisible())
		updateExplosion();
	updatePunctualLights();
	if (isModelLevelTransform)
		reapplyGltfCameraAfterTransform();
	triggerShadowRecomputation();
	updateFloorPlane();
	if (fitView && !isGltfCameraActive())
	{
		fitAll();
	}

	doneCurrent();
}

void ViewportWidget::createShaderPrograms()
{
	const QString path = PathUtils::getDataDirectory() + "/";
	_renderCtrl.initShaders(path);
	_rtPresenter.initialize(path);
}

void ViewportWidget::createCappingPlanes()
{
    const QString path = PathUtils::getDataDirectory() + "/";
	if (_clippingPlaneXY == nullptr)
	{
		_clippingPlaneXY = new PlaneRenderable(_renderCtrl.clippingPlaneShader(), QVector3D(0, 0, 0), 1000, 1000, 1, 1);
		_clippingPlaneYZ = new PlaneRenderable(_renderCtrl.clippingPlaneShader(), QVector3D(0, 0, 0), 1000, 1000, 1, 1);
		_clippingPlaneZX = new PlaneRenderable(_renderCtrl.clippingPlaneShader(), QVector3D(0, 0, 0), 1000, 1000, 1, 1);
		registerDecorationGpuResource(_clippingPlaneXY, [this] { return _renderCtrl.clippingPlaneShader(); });
		registerDecorationGpuResource(_clippingPlaneYZ, [this] { return _renderCtrl.clippingPlaneShader(); });
		registerDecorationGpuResource(_clippingPlaneZX, [this] { return _renderCtrl.clippingPlaneShader(); });
	}
    _renderCtrl.setCappingTexture(loadTextureFromFile(QString(path + "textures/patterns/hatch_03.png").toStdString().c_str()));
	glActiveTexture(GL_TEXTURE6);
	glBindTexture(GL_TEXTURE_2D, _renderCtrl.cappingTexture());

	// Stable sampling for any scale
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glGenerateMipmap(GL_TEXTURE_2D);
	// (Optional) if supported:
	GLfloat aniso = 8.0f;
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso);
}

void ViewportWidget::createLights()
{
	if (_lightCube == nullptr)
	{
		_lightCube = new CubeRenderable(_renderCtrl.lightCubeShader(), 10);
		_lightSphere = new SphereRenderable(_renderCtrl.lightCubeShader(), 1, 16, 16);
		registerDecorationGpuResource(_lightCube, [this] { return _renderCtrl.lightCubeShader(); });
		registerDecorationGpuResource(_lightSphere, [this] { return _renderCtrl.lightCubeShader(); });
	}
}

void ViewportWidget::createFullscreenTriangle()
{
	_renderCtrl.initFullscreenTriangle();
}

void ViewportWidget::drawFullscreenTriangle()
{
	_renderCtrl.drawFullscreenTriangle();
}

void ViewportWidget::setIBLFaceBasis(QOpenGLShaderProgram* prog, int faceIndex)
{
	auto setM = [prog](const QVector3D& U, const QVector3D& V, const QVector3D& W) {
		QMatrix3x3 m;
		m(0, 0) = U.x(); m(1, 0) = U.y(); m(2, 0) = U.z();
		m(0, 1) = V.x(); m(1, 1) = V.y(); m(2, 1) = V.z();
		m(0, 2) = W.x(); m(1, 2) = W.y(); m(2, 2) = W.z();
		prog->setUniformValue("faceBasis", m);
		};

	// Basis vectors with 90° X-axis rotation applied
	// (Same rotation as: model.rotate(90.0f, QVector3D(1.0f, 0.0f, 0.0f)))
	switch (faceIndex)
	{
	case 0: // Right (+X)
		setM(QVector3D(0.0f, 1.0f, 0.0f),
			QVector3D(0.0f, 0.0f, 1.0f),
			QVector3D(1.0f, 0.0f, 0.0f));
		break;

	case 1: // Left (-X)
		setM(QVector3D(0.0f, -1.0f, 0.0f),
			QVector3D(0.0f, 0.0f, 1.0f),
			QVector3D(-1.0f, 0.0f, 0.0f));
		break;

	case 2: // Top (+Y)
		setM(QVector3D(1.0f, 0.0f, 0.0f),
			QVector3D(0.0f, -1.0f, 0.0f),
			QVector3D(0.0f, 0.0f, -1.0f));
		break;

	case 3: // Bottom (-Y)
		setM(QVector3D(1.0f, 0.0f, 0.0f),
			QVector3D(0.0f, 1.0f, 0.0f),
			QVector3D(0.0f, 0.0f, 1.0f));
		break;

	case 4: // Front (+Z)
		setM(QVector3D(1.0f, 0.0f, 0.0f),
			QVector3D(0.0f, 0.0f, 1.0f),
			QVector3D(0.0f, -1.0f, 0.0f));
		break;

	case 5: // Back (-Z)
		setM(QVector3D(-1.0f, 0.0f, 0.0f),
			QVector3D(0.0f, 0.0f, 1.0f),
			QVector3D(0.0f, 1.0f, 0.0f));
		break;
	}
}

void ViewportWidget::ensureShadowMapResources()
{
	_renderCtrl.ensureShadowMap(defaultFramebufferObject());
}

void ViewportWidget::loadFloor()
{
	ensureShadowMapResources();

	// Use helper to update floor geometry
	float halfObjectSize = updateFloorGeometry();

	// Use helper to set main light position
	updateMainLightPosition(halfObjectSize);

	float floorPlaneCoeff = _sceneRuntime.meshStore().empty()
		? -_floorSize - (_floorSize * 0.05f)
		: CoordinateSystemHelper::groundPlaneZ(
			_viewCtrl.boundingBox(),
			_viewCtrl.cameraUpAxisZUp(),
			_floorSize,
			_renderCtrl.floorOffsetPercent(),
			SceneRenderController::computeFloorDepthBias(
				static_cast<float>(std::max({
					_viewCtrl.boundingBox().getXSize(),
					_viewCtrl.boundingBox().getYSize(),
					_viewCtrl.boundingBox().getZSize()
				})),
				_floorSize));
	_floorPlaneZ = floorPlaneCoeff;

	const float groundExtent = CoordinateSystemHelper::groundPlaneExtent(
		_floorSize,
		_floorSizeFactor,
		_renderCtrl.groundMode());
	if (_floorPlane == nullptr)
	{
		_floorPlane = new FloorPlane(
			_renderCtrl.fgShader(), _floorCenter, groundExtent, groundExtent, 1, 1,
			floorPlaneCoeff, _renderCtrl.floorTexRepeatS(), _renderCtrl.floorTexRepeatT(),
			CoordinateSystemHelper::floorPlaneOrientation(_viewCtrl.cameraUpAxisZUp()));
		registerDecorationGpuResource(_floorPlane, [this] { return _renderCtrl.fgShader(); });
	}
	else
	{
		_floorPlane->setPlane(
			_renderCtrl.fgShader(), _floorCenter, groundExtent, groundExtent, 1, 1,
			floorPlaneCoeff, _renderCtrl.floorTexRepeatS(), _renderCtrl.floorTexRepeatT(),
			CoordinateSystemHelper::floorPlaneOrientation(_viewCtrl.cameraUpAxisZUp()));
	}

	// Use helper to apply common material/texture settings
	applyFloorPlaneMaterialSettings();
}

void ViewportWidget::applyFloorPlaneMaterialSettings()
{
	if (_floorPlane == nullptr)
		return;

	_floorPlane->setAmbientMaterial(QVector3D(0.0f, 0.0f, 0.0f));
	_floorPlane->setDiffuseMaterial(QVector3D(1.0f, 1.0f, 1.0f));
	_floorPlane->setSpecularMaterial(QVector3D(0.5f, 0.5f, 0.5f));
	_floorPlane->setShininess(16.0f);
	syncFloorPlaneAlbedoTexture();
}

void ViewportWidget::syncFloorPlaneAlbedoTexture()
{
	if (_floorPlane == nullptr)
		return;

	Material material = _floorPlane->getMaterial();
	const GLuint oldAlbedoTex = static_cast<GLuint>(material.albedoTextureId());
	const GLuint oldDiffuseTex = static_cast<GLuint>(material.diffuseTextureId());

	if (!_renderCtrl.floorTextureDisplayed() || _floorTexImage.isNull())
	{
		if (oldAlbedoTex != 0)
		{
			glDeleteTextures(1, &oldAlbedoTex);
		}
		if (oldDiffuseTex != 0 && oldDiffuseTex != oldAlbedoTex)
		{
			glDeleteTextures(1, &oldDiffuseTex);
		}

		Material::Texture resetAlbedo;
		resetAlbedo.type = "albedo";
		Material::Texture resetDiffuse;
		resetDiffuse.type = "diffuse";

		material.setTexture(Material::TextureType::Albedo, resetAlbedo);
		material.setTexture(Material::TextureType::Diffuse, resetDiffuse);
		material.setAlbedoMap(QString());
		material.setAlbedoTextureId(0);
		material.setDiffuseMap(QString());
		material.setDiffuseTextureId(0);
		_floorPlane->setMaterial(material);
		return;
	}

	const TextureSamplerSettings samplers{
		GL_REPEAT,
		GL_REPEAT,
		GL_LINEAR_MIPMAP_LINEAR,
		GL_LINEAR
	};

	const GLuint newFloorTex = createGPUTextureFromImage(_floorTexImage, samplers);
	if (newFloorTex == 0)
		return;

	if (oldAlbedoTex != 0)
	{
		glDeleteTextures(1, &oldAlbedoTex);
	}
	if (oldDiffuseTex != 0 && oldDiffuseTex != oldAlbedoTex)
	{
		glDeleteTextures(1, &oldDiffuseTex);
	}

	Material::Texture albedoTexture = material.texture(Material::TextureType::Albedo);
	albedoTexture.id = newFloorTex;
	albedoTexture.type = "albedo";
	albedoTexture.path = "generated://floor-albedo";
	albedoTexture.hasAlpha = _floorTexImage.hasAlphaChannel();
	albedoTexture.wrapS = samplers.wrapS;
	albedoTexture.wrapT = samplers.wrapT;
	albedoTexture.minFilter = samplers.minFilter;
	albedoTexture.magFilter = samplers.magFilter;
	albedoTexture.imageData = _floorTexImage;
	material.setTexture(Material::TextureType::Albedo, albedoTexture);
	material.setAlbedoMap("generated://floor-albedo");
	material.setAlbedoTextureId(static_cast<int>(newFloorTex));

	Material::Texture resetDiffuse;
	resetDiffuse.type = "diffuse";
	material.setTexture(Material::TextureType::Diffuse, resetDiffuse);
	material.setDiffuseMap(QString());
	material.setDiffuseTextureId(0);
	_floorPlane->setMaterial(material);
}

QVector3D ViewportWidget::effectiveWorldLightOffset() const
{
	return CoordinateSystemHelper::transformVectorForCameraUpAxis(
		_viewCtrl.cameraUpAxisZUp(),
		_renderCtrl.lightOffset());
}

QVector3D ViewportWidget::effectiveWorldLightPosition() const
{
	return _lightPosition + effectiveWorldLightOffset();
}

void ViewportWidget::updateMainLightPosition(float halfObjectSize)
{
	const QVector3D lateralAxis1 = CoordinateSystemHelper::transformVectorForCameraUpAxis(
		_viewCtrl.cameraUpAxisZUp(),
		QVector3D(1.0f, 0.0f, 0.0f)).normalized();
	// Z-up: (1,0,0)+(0,1,0) → light at X+Y+ corner, height in Z  (illuminates X+/Y+/Z+ faces)
	// Y-up: (1,0,0)+(0,0,-1) → light at X+Z- corner, height in Y.
	// This preserves the original isometric "shadow falls in front" feel after
	// rotating the viewer convention from Z-up to Y-up.
	const QVector3D lateralAxis2 = _viewCtrl.cameraUpAxisZUp()
		? CoordinateSystemHelper::transformVectorForCameraUpAxis(
			_viewCtrl.cameraUpAxisZUp(),
			QVector3D(0.0f, 1.0f, 0.0f)).normalized()
		: QVector3D(0.0f, 0.0f, -1.0f);
	_lightPosition = _floorCenter + (lateralAxis1 + lateralAxis2) * (_floorSize * 1.25f);

	if (_sceneRuntime.meshStore().empty())
	{
		CoordinateSystemHelper::setCoordinateAlongCurrentWorldUp(
			_viewCtrl.cameraUpAxisZUp(), _lightPosition, _floorSize);
	}
	else
	{
		float highestUp = CoordinateSystemHelper::coordinateAlongCurrentWorldUp(
			_viewCtrl.cameraUpAxisZUp(),
			_viewCtrl.boundingSphere().getCenter()) + _viewCtrl.boundingSphere().getRadius();
		CoordinateSystemHelper::setCoordinateAlongCurrentWorldUp(_viewCtrl.cameraUpAxisZUp(), _lightPosition,
			highestUp + halfObjectSize * 5.0f + (_floorSize * _renderCtrl.floorOffsetPercent()));
	}
}

float ViewportWidget::updateFloorGeometry()
{
	float halfObjectSize = _viewCtrl.boundingSphere().getRadius();
	_floorCenter = _viewCtrl.boundingSphere().getCenter();

	if (_viewCtrl.cameraUpAxisZUp())
	{
		// Z is up: floor footprint is XY; if the height (Z) dominates, size to it
		if (_viewCtrl.boundingBox().getZSize() >= _viewCtrl.boundingBox().getXSize() && _viewCtrl.boundingBox().getZSize() >= _viewCtrl.boundingBox().getYSize())
			_floorSize = _viewCtrl.boundingBox().getZSize();
		else
			_floorSize = std::max(_viewCtrl.boundingBox().getYSize(), _viewCtrl.boundingBox().getXSize()) / 1.25f;
	}
	else
	{
		// Y is up: floor footprint is XZ; if the height (Y) dominates, size to it
		if (_viewCtrl.boundingBox().getYSize() >= _viewCtrl.boundingBox().getXSize() && _viewCtrl.boundingBox().getYSize() >= _viewCtrl.boundingBox().getZSize())
			_floorSize = _viewCtrl.boundingBox().getYSize();
		else
			_floorSize = std::max(_viewCtrl.boundingBox().getXSize(), _viewCtrl.boundingBox().getZSize()) / 1.25f;
	}

	_lightCube->setSize(halfObjectSize * 0.1f);

	return halfObjectSize;
}

bool ViewportWidget::userModelTransformForFile(const QString& sourceFile,
                                         QMatrix4x4& outTransform) const
{
	return _sceneRuntime.userModelTransformForFile(sourceFile, outTransform);
}

namespace
{
// Average axis scale of the upper-3x3 — matches the avgScale convention used
// for light range/intensity compensation elsewhere (export, import correction).
float uniformScaleOf(const QMatrix4x4& m)
{
	const float sx = QVector3D(m(0, 0), m(1, 0), m(2, 0)).length();
	const float sy = QVector3D(m(0, 1), m(1, 1), m(2, 1)).length();
	const float sz = QVector3D(m(0, 2), m(1, 2), m(2, 2)).length();
	return (sx + sy + sz) / 3.0f;
}
} // namespace

void ViewportWidget::updatePunctualLights()
{
	if (_animCtrl.originalParsedLights().empty())
	{
		return;
	}

	const SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
	const std::vector<GPULight> uploadLights =
		_animCtrl.buildUploadLightsWithSceneGraph(
			[this](const QString& file) {
				QMatrix4x4 m;
				userModelTransformForFile(file, m);
				return m;
			},
			sg);

	_renderCtrl.punctualLights()->setLights(uploadLights);
	syncPunctualLightUniforms(static_cast<int>(uploadLights.size()),
	                          !uploadLights.empty());
}

void ViewportWidget::setAnimatedLightVisibilityState(const QString& sourceFile, const QVector<bool>& visibleByParsedLight)
{
	_animCtrl.setAnimatedLightVisibility(sourceFile, visibleByParsedLight);
	updatePunctualLights();
}

void ViewportWidget::setAnimatedLightTransformState(const QString& sourceFile, const std::vector<GPULight>& animatedLights)
{
	_animCtrl.setAnimatedLightTransform(sourceFile, animatedLights);
	updatePunctualLights();
}

void ViewportWidget::clearAnimatedLightTransformState(const QString& sourceFile)
{
	_animCtrl.clearAnimatedLightTransform(sourceFile);
	updatePunctualLights();
}

void ViewportWidget::clearAnimatedLightVisibilityState(const QString& sourceFile)
{
	_animCtrl.clearAnimatedLightVisibility(sourceFile);
	updatePunctualLights();
}

void ViewportWidget::setAnimatedMeshVisibilityState(const QString& sourceFile, const QSet<QUuid>& hiddenMeshUuids)
{
	const bool activatingForFile = (_animCtrl.animatedMeshVisibilitySourceFile() != sourceFile);
	_animCtrl.setAnimatedMeshVisibility(sourceFile, hiddenMeshUuids);
	recalculateVisibleSceneStats();
	updatePunctualLights();
	if (activatingForFile)
		fitAll();
}

void ViewportWidget::clearAnimatedMeshVisibilityState(const QString& sourceFile)
{
	_animCtrl.clearAnimatedMeshVisibility(sourceFile);
	recalculateVisibleSceneStats();
	updatePunctualLights();
}

void ViewportWidget::loadEnvMap(bool allowCacheReuse)
{
    const QString path = PathUtils::getDataDirectory() + "/";

	if (_skyBox == nullptr)
	{
		_skyBox = new CubeRenderable(_renderCtrl.skyBoxShader(), 1);
		registerDecorationGpuResource(_skyBox, [this] { return _renderCtrl.skyBoxShader(); });
	}
	_renderCtrl.skyBoxShader()->bind();
	_renderCtrl.skyBoxShader()->setUniformValue("skybox", 1);

	glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
	const bool hadEnvironmentMap = (_renderCtrl.environmentMap() != 0);
	if (_renderCtrl.environmentMap() == 0)
		_renderCtrl.createEnvironmentMapTexture();

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_CUBE_MAP, _renderCtrl.environmentMap());

	if (_renderCtrl.currentSkyboxFolder().isEmpty())
		_renderCtrl.setCurrentSkyboxFolder(path + "textures/envmap/skyboxes/LDRI/@Default");

	// allowCacheReuse=true only from initializeGL()'s context-recreation
	// path (see loadIrradianceMap()'s identical reasoning) - every other
	// caller (getEnvironmentMap(regenerate=true), used to load a newly
	// selected skybox folder) means the environment itself may have
	// changed, so hadEnvironmentMap being true there does NOT mean the
	// existing texture still holds the right pixels - it must always
	// re-upload regardless of context sharing.
	if (!allowCacheReuse || !IGpuContextResource::contextsAreShared() || !hadEnvironmentMap)
		setSkyBoxTextureFolder(_renderCtrl.currentSkyboxFolder());
}

void ViewportWidget::loadIrradianceMap(bool allowCacheReuse)
{
	const bool hasIblCache =
		_renderCtrl.irradianceMap() != 0 &&
		_renderCtrl.prefilterMap() != 0 &&
		_renderCtrl.sheenPrefilterMap() != 0 &&
		_renderCtrl.brdfLUTTexture() != 0 &&
		_renderCtrl.charlieLUTTexture() != 0 &&
		_renderCtrl.sheenELUTTexture() != 0;
	if (!allowCacheReuse || !IGpuContextResource::contextsAreShared() || !hasIblCache)
	{
		_renderCtrl.generateIBL(defaultFramebufferObject());
	}
	else
	{
		// Texture bindings are context state, not object state: after a shared
		// context recreation the textures still exist, but the new context
		// needs their unit bindings re-established.
		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_CUBE_MAP, _renderCtrl.irradianceMap());
		glActiveTexture(GL_TEXTURE4);
		glBindTexture(GL_TEXTURE_CUBE_MAP, _renderCtrl.prefilterMap());
		glActiveTexture(GL_TEXTURE5);
		glBindTexture(GL_TEXTURE_2D, _renderCtrl.brdfLUTTexture());
		glActiveTexture(GL_TEXTURE7);
		glBindTexture(GL_TEXTURE_CUBE_MAP, _renderCtrl.sheenPrefilterMap());
		glActiveTexture(GL_TEXTURE8);
		glBindTexture(GL_TEXTURE_2D, _renderCtrl.charlieLUTTexture());
		glActiveTexture(GL_TEXTURE9);
		glBindTexture(GL_TEXTURE_2D, _renderCtrl.sheenELUTTexture());
		glActiveTexture(GL_TEXTURE0);
	}
}

// Helper: Load HDR file and convert to cubemap
// Returns the created cubemap texture ID (or 0 on failure)
GLuint ViewportWidget::loadPresetEnvironmentMap(const QString& hdrFilePath)
{
	return _renderCtrl.loadPresetEnvMap(hdrFilePath, defaultFramebufferObject());
}

// Helper: Generate irradiance and prefilter maps for a preset cubemap
// Returns true on success
bool ViewportWidget::generatePresetIBLMaps(GLuint sourceCubemap, GLuint& outIrradianceMap, GLuint& outPrefilterMap, GLuint& outSheenPrefilterMap)
{
	return _renderCtrl.generatePresetIBLMaps(sourceCubemap, outIrradianceMap, outPrefilterMap, outSheenPrefilterMap, defaultFramebufferObject());
}

GLuint ViewportWidget::getEnvironmentMap(int index, bool regenerate)
{
	switch(index)
	{
		case 0:  // ViewerIBL
			if (regenerate && !_renderCtrl.currentSkyboxFolder().isEmpty())
			{
				loadEnvMap();
			}
			return _renderCtrl.environmentMap();
		case 1:  // Studio
			return _renderCtrl.studioEnvironmentMap();
		case 2:  // Outdoor
			return _renderCtrl.outdoorEnvironmentMap();
		case 3:  // Office
			return _renderCtrl.officeEnvironmentMap();
		default:
			return 0;
	}
}

GLuint ViewportWidget::getIrradianceMap(int index, bool regenerate)
{
	switch(index)
	{
		case 0:  // ViewerIBL
			if (regenerate && !_renderCtrl.currentSkyboxFolder().isEmpty())
			{
				loadIrradianceMap();
			}
			return _renderCtrl.irradianceMap();
		case 1:  // Studio
			return _renderCtrl.studioIrradianceMap();
		case 2:  // Outdoor
			return _renderCtrl.outdoorIrradianceMap();
		case 3:  // Office
			return _renderCtrl.officeIrradianceMap();
		default:
			return 0;
	}
}

GLuint ViewportWidget::getPrefilterMap(int index, bool regenerate)
{
	switch(index)
	{
		case 0:  // ViewerIBL
			if (regenerate && !_renderCtrl.currentSkyboxFolder().isEmpty())
			{
				loadIrradianceMap();  // This creates both irradiance AND prefilter
			}
			return _renderCtrl.prefilterMap();
		case 1:  // Studio
			return _renderCtrl.studioPrefilterMap();
		case 2:  // Outdoor
			return _renderCtrl.outdoorPrefilterMap();
		case 3:  // Office
			return _renderCtrl.officePrefilterMap();
		default:
			return 0;
	}
}

GLuint ViewportWidget::getSheenPrefilterMap(int index, bool regenerate)
{
	switch(index)
	{
		case 0:  // ViewerIBL
			if (regenerate && !_renderCtrl.currentSkyboxFolder().isEmpty())
			{
				loadIrradianceMap();
			}
			return _renderCtrl.sheenPrefilterMap();
		case 1:  // Studio
			return _renderCtrl.studioSheenPrefilterMap();
		case 2:  // Outdoor
			return _renderCtrl.outdoorSheenPrefilterMap();
		case 3:  // Office
			return _renderCtrl.officeSheenPrefilterMap();
		default:
			return 0;
	}
}

void ViewportWidget::renderSingleView(QColor& topColor, QColor& botColor)
{
	QMatrix4x4 projection;
	projection.ortho(QRect(0.0f, 0.0f, static_cast<float>(width()), static_cast<float>(height())));
	_renderCtrl.textShader()->bind();
	_renderCtrl.textShader()->setUniformValue("projection", projection);
	_renderCtrl.textShader()->release();
	glViewport(0, 0, width(), height());
	// showShadows (setCommonUniforms()) already skips SAMPLING the shadow map
	// during this same lowResEnabled+oversized window, but that alone leaves
	// the full-resolution shadow depth pass itself still running for nothing
	// - skip generating it too, matching the same condition.
	if (_renderCtrl.shadowsEnabled()
		&& !(_renderCtrl.lowResEnabled() && _displayedObjectsMemSize > MAX_MODEL_SIZE_BYTES))
		renderToShadowBuffer();

	if (sceneHasVisibleSSSMaterials())
		renderToSSSBuffer(_primaryCamera);

	if (_renderCtrl.transmissionEnabled() && sceneHasVisibleTransmissionMaterials())
		renderToTransmissionBuffer(_primaryCamera, topColor, botColor);

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	gradientBackground(topColor.redF(), topColor.greenF(), topColor.blueF(), topColor.alphaF(),
		botColor.redF(), botColor.greenF(), botColor.blueF(), botColor.alphaF(), _renderCtrl.gradientStyle());
	render(_primaryCamera);
	drawTransformGizmo(_primaryCamera);
	drawMeasurementOverlay(_primaryCamera);
}

void ViewportWidget::applyExplodedViewTransforms(const QMap<int, TransformState>& transforms, bool fitView)
{
	Q_UNUSED(fitView);

	QHash<QUuid, QVector3D> savedExplosionOffsets;
	savedExplosionOffsets.reserve(transforms.size());

	for (auto it = transforms.cbegin(); it != transforms.cend(); ++it)
	{
		const int index = it.key();
		if (index < 0 || index >= static_cast<int>(_sceneRuntime.meshStore().size()))
			continue;

		SceneMesh* mesh = _sceneRuntime.meshAt(index);
		if (!mesh)
			continue;

		savedExplosionOffsets.insert(mesh->uuid(), mesh->explosionOffset());
		mesh->setExplosionOffset(QVector3D());
		ExplodedViewRuntimeController::applyExplodedViewTransformState(mesh, it.value(), false);
		mesh->fullUpdateRuntimeBounds();
	}

	for (auto it = transforms.cbegin(); it != transforms.cend(); ++it)
	{
		const int index = it.key();
		if (index < 0 || index >= static_cast<int>(_sceneRuntime.meshStore().size()))
			continue;

		SceneMesh* mesh = _sceneRuntime.meshAt(index);
		if (!mesh)
			continue;

		mesh->setExplosionOffset(savedExplosionOffsets.value(mesh->uuid()));
		mesh->setSceneRenderTransformFast(mesh->getSceneRenderTransform());
	}

	emit explodedViewManualPlacementChanged();
	update();
}

void ViewportWidget::renderMultiView(QColor& topColor, QColor& botColor)
{
	glViewport(0, 0, width(), height());
	// See renderSingleView()'s identical condition for why this is skipped
	// (not just the shadow-map SAMPLING that showShadows already gates) during
	// interaction on oversized models.
	if (_renderCtrl.shadowsEnabled()
		&& !(_renderCtrl.lowResEnabled() && _displayedObjectsMemSize > MAX_MODEL_SIZE_BYTES))
		renderToShadowBuffer();

	if (sceneHasVisibleSSSMaterials())
		renderToSSSBuffer(_primaryCamera);

	if (_renderCtrl.transmissionEnabled() && sceneHasVisibleTransmissionMaterials())
		renderToTransmissionBuffer(_primaryCamera, topColor, botColor);

	gradientBackground(topColor.redF(), topColor.greenF(), topColor.blueF(), topColor.alphaF(),
		botColor.redF(), botColor.greenF(), botColor.blueF(), botColor.alphaF(), _renderCtrl.gradientStyle());
	const std::vector<QVector3D> multiViewCorners = collectVisibleCorners();
	const QVector3D sharedMultiViewCenter = _primaryCamera->getPosition();
	// Computed range (eye-relative from orbit center) gives the correct full-scene fit.
	// Scale it by the perspective zoom ratio so ISO zoom/pan drives ortho zoom/pan.
	const float zoomScale = (_viewCtrl.viewBoundingSphereDia() > 0.0f)
		? (_viewCtrl.viewRange() / _viewCtrl.viewBoundingSphereDia()) : 1.0f;
	const float sharedMultiViewRange = computeSharedOrthographicMultiViewRange(
		multiViewCorners, width() / 2, height() / 2, sharedMultiViewCenter) * zoomScale;
	// Render orthographic views with ortho view camera
	// Top View
	glViewport(0, 0, width() / 2, height() / 2);
	configureOrthoSubviewCamera(
		ViewMode::TOP, multiViewCorners, width() / 2, height() / 2, sharedMultiViewCenter, sharedMultiViewRange);
	render(_orthoViewsCamera);
	_textRenderer->RenderText(_labelTop.toStdString(), -50, 5, 1.6f, QVector3D(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VTOP, TextRenderer::HAlignment::HRIGHT);

	// Front View
	glViewport(0, height() / 2, width() / 2, height() / 2);
	configureOrthoSubviewCamera(
		ViewMode::FRONT, multiViewCorners, width() / 2, height() / 2, sharedMultiViewCenter, sharedMultiViewRange);
	render(_orthoViewsCamera);
	_textRenderer->RenderText(_labelFront.toStdString(), -50, 5, 1.6f, QVector3D(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VTOP, TextRenderer::HAlignment::HRIGHT);

	// Left View
	glViewport(width() / 2, height() / 2, width() / 2, height() / 2);
	configureOrthoSubviewCamera(
		ViewMode::LEFT, multiViewCorners, width() / 2, height() / 2, sharedMultiViewCenter, sharedMultiViewRange);
	render(_orthoViewsCamera);
	_textRenderer->RenderText(_labelLeft.toStdString(), -50, 5, 1.6f, QVector3D(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VTOP, TextRenderer::HAlignment::HRIGHT);

	// Render isometric view with primary camera
	// Isometric View
	glViewport(width() / 2, 0, width() / 2, height() / 2);
	render(_primaryCamera);
	//std::string viewLabel = viewCtrl.viewMode() == ViewMode::DIMETRIC ? "Dimetric" : ...
		//== ViewMode::TRIMETRIC ? "Trimetric" : "Isometric";
	QString viewLabel;
	switch (_viewCtrl.viewMode())
	{
	case ViewMode::DIMETRIC: viewLabel = _labelDimetric; break;
	case ViewMode::TRIMETRIC: viewLabel = _labelTrimetric; break;
	default: viewLabel = _labelIsometric; break;
	}
	_textRenderer->RenderText(viewLabel.toStdString(), -50, 5, 1.6f, QVector3D(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VTOP, TextRenderer::HAlignment::HRIGHT);

	// draw screen partitioning lines
	splitScreen();
}

void ViewportWidget::drawFloor(const bool& drawReflection)
{
	RenderableMesh::resetTextureBindingCacheForCurrentContext();

	// Per-pass uniforms: 3 that differ between passes + 2 that drawMesh() may overwrite.
	// bind() is required here: drawMesh() ends with _prog->release() per mesh, leaving
	// no program active; the bind before each pass restores _renderCtrl.fgShader() so the uniform
	// uploads and the matrix uploads in FloorPlane::render() reach the right program.
	auto configureGroundPass = [this](bool reflectedPass, bool textureEnabled)
	{
		_renderCtrl.fgShader()->bind();
		_renderCtrl.fgShader()->setUniformValue("sssCapture", false);
		_renderCtrl.fgShader()->setUniformValue("envMapEnabled", false);
		_renderCtrl.fgShader()->setUniformValue("isReflectedPass", reflectedPass);
		_renderCtrl.fgShader()->setUniformValue("renderingMode", static_cast<int>(RenderingMode::ADS_BLINN_PHONG));
		_renderCtrl.fgShader()->setUniformValue("floorTextureEnabled", textureEnabled);
	};

	// Units 32/33: prevent the floor from sampling the transmission FBO while it is
	// the active render target (or stale from a previous frame). Scene meshes in the
	// reflection pass do not rebind these units, so one call covers all three passes.
	// Units 37/38 (sssDiffuseTexture/sssDepthTexture) need no protection here:
	// floor rendering never has hasVolumeScattering=true so sampleCapturedSSSDiffuse
	// is never called.
	glActiveTexture(GL_TEXTURE0 + 32);
	glBindTexture(GL_TEXTURE_2D, _renderCtrl.whiteTexture());
	glActiveTexture(GL_TEXTURE0 + 33);
	glBindTexture(GL_TEXTURE_2D, _renderCtrl.whiteTexture());
	glActiveTexture(GL_TEXTURE0);

	// Upload uniforms shared by both floor passes once; only per-pass values are
	// re-uploaded via configureGroundPass() before each individual pass.
	_renderCtrl.fgShader()->bind();
	_renderCtrl.fgShader()->setUniformValue("floorRendering", true);
	_renderCtrl.fgShader()->setUniformValue("groundMode", static_cast<int>(_renderCtrl.groundMode()));
	_renderCtrl.fgShader()->setUniformValue("topColor", QVector4D(_renderCtrl.bgTopColor().red(), _renderCtrl.bgTopColor().green(), _renderCtrl.bgTopColor().blue(), _renderCtrl.bgTopColor().alpha()));
	_renderCtrl.fgShader()->setUniformValue("botColor", QVector4D(_renderCtrl.bgBotColor().red(), _renderCtrl.bgBotColor().green(), _renderCtrl.bgBotColor().blue(), _renderCtrl.bgBotColor().alpha()));
	_renderCtrl.fgShader()->setUniformValue("screenSize", QVector2D(width(), height()));
	_renderCtrl.fgShader()->setUniformValue("screenCenter", _floorCenter);
	_renderCtrl.fgShader()->setUniformValue("gradientStyle", _renderCtrl.gradientStyle());
	_renderCtrl.fgShader()->setUniformValue("floorSize",
		CoordinateSystemHelper::groundPlaneExtent(_floorSize, _floorSizeFactor, _renderCtrl.groundMode()));
	_renderCtrl.fgShader()->setUniformValue("groundReferenceSize", _floorSize);
	_renderCtrl.fgShader()->setUniformValue("worldUpAxis", _viewCtrl.cameraUpAxisZUp() ? 2 : 1);

	//https://open.gl/depthstencils
	glEnable(GL_STENCIL_TEST);
	glClear(GL_STENCIL_BUFFER_BIT);
	glStencilMask(0x0);
	glStencilFunc(GL_ALWAYS, 1, 0xFF);
	glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
	glStencilMask(0xFF);
	glDepthMask(GL_FALSE);
	glClear(GL_STENCIL_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_FRONT);

	// Draw floor
	configureGroundPass(true, false);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	_floorPlane->setOpacity(0.1f);
	_floorPlane->render();
	// FloorPlane writes mesh-like material uniforms through _renderCtrl.fgShader() without
	// participating in SceneMesh's shared material-uniform cache. Invalidate
	// that cache so subsequent scene meshes always republish their own state.
	SceneMesh::resetSharedUniformStateCache();
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);

	
	// Draw model reflection
	glStencilFunc(GL_EQUAL, 1, 0xFF);
	glStencilMask(0x00);
	glDepthMask(GL_TRUE);

	QMatrix4x4 model;

	// Mirror the scene across the actual floor plane. Using live animated bounds
	// to derive this offset makes reflected animated parts appear to bob the
	// entire reflection up and down as their runtime AABBs change frame to frame.
	const float floorPos = _floorPlaneZ;
	if (_viewCtrl.cameraUpAxisZUp())
	{
		model.translate(0.0f, 0.0f, 2.0f * floorPos);
		model.scale(1.0f, 1.0f, -1.0f);
	}
	else
	{
		model.translate(0.0f, 2.0f * floorPos, 0.0f);
		model.scale(1.0f, -1.0f, 1.0f);
	}

	_renderCtrl.fgShader()->bind();
	_renderCtrl.fgShader()->setUniformValue("sssCapture", false);
	_renderCtrl.fgShader()->setUniformValue("modelMatrix", model);
	RenderableMesh::setCurrentRenderContext(model, _viewCtrl.viewMatrix());
	if (_renderCtrl.reflectionsEnabled() && drawReflection)
	{
		_renderCtrl.fgShader()->setUniformValue("renderingMode", static_cast<int>(_renderCtrl.renderingMode()));
		drawMesh(_renderCtrl.fgShader());
	}

	glStencilMask(0x00);
	glDisable(GL_STENCIL_TEST);
		
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_FRONT);
	configureGroundPass(false, _renderCtrl.floorTextureDisplayed());
	_renderCtrl.fgShader()->setProperty("globalModelMatrix", QVariant::fromValue(_viewCtrl.modelMatrix()));
	RenderableMesh::setCurrentRenderContext(_viewCtrl.modelMatrix(), _viewCtrl.viewMatrix());
	_floorPlane->setOpacity(0.95f);
	_floorPlane->render();
	// The final visible floor pass also mutates _renderCtrl.fgShader() material uniforms,
	// so clear the shared cache before later transparent scene draws.
	SceneMesh::resetSharedUniformStateCache();
	glDisable(GL_CULL_FACE);
	_renderCtrl.fgShader()->bind();
	_renderCtrl.fgShader()->setUniformValue("floorRendering", false);
	_renderCtrl.fgShader()->setUniformValue("groundMode", static_cast<int>(GroundMode::None));
	_renderCtrl.fgShader()->setUniformValue("renderingMode", static_cast<int>(_renderCtrl.renderingMode()));
	glDisable(GL_BLEND);

	_renderCtrl.fgShader()->setUniformValue("envMapEnabled", _renderCtrl.envMapEnabled());
	glActiveTexture(GL_TEXTURE0);
}

void ViewportWidget::drawGrid()
{
	if (!_renderCtrl.gridShader())
		return;

	QMatrix4x4 viewProjection = _viewCtrl.projectionMatrix() * _viewCtrl.viewMatrix();
	QMatrix4x4 inverseViewProjection = viewProjection.inverted();
	_renderCtrl.gridShader()->bind();
	_renderCtrl.gridShader()->setUniformValue("inverseViewProjectionMatrix", inverseViewProjection);
	_renderCtrl.gridShader()->setUniformValue("viewProjectionMatrix", viewProjection);
	_renderCtrl.gridShader()->setUniformValue("cameraPos", _primaryCamera->getRenderPosition());
	_renderCtrl.gridShader()->setUniformValue("screenCenter", _floorCenter);
	_renderCtrl.gridShader()->setUniformValue("groundReferenceSize", _floorSize);
	_renderCtrl.gridShader()->setUniformValue("floorSize",
		CoordinateSystemHelper::groundPlaneExtent(_floorSize, _floorSizeFactor, _renderCtrl.groundMode()));
	_renderCtrl.gridShader()->setUniformValue("gridPlaneZ", _floorPlaneZ);
	_renderCtrl.gridShader()->setUniformValue("worldUpAxis", _viewCtrl.cameraUpAxisZUp() ? 2 : 1);
	_renderCtrl.gridShader()->setUniformValue("opacity", 0.95f);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	drawFullscreenTriangle();
	glDisable(GL_BLEND);
}

void ViewportWidget::drawSkyBox(const QMatrix4x4* overrideViewMatrix)
{
	_skyBox->setProg(_renderCtrl.skyBoxShader());
	_renderCtrl.skyBoxShader()->bind();
	const bool usePrefilterBlur = _renderCtrl.skyBoxBlurPercent() > 0 && _renderCtrl.prefilterMap() != 0 && _renderCtrl.prefilterMipLevels() > 0;
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_CUBE_MAP, usePrefilterBlur ? _renderCtrl.prefilterMap() : _renderCtrl.environmentMap());
	_renderCtrl.skyBoxShader()->setUniformValue("skybox", 1);
	QMatrix4x4 projection;
	projection.perspective(_renderCtrl.skyBoxFOV(), (float)width() / (float)height(), 0.1f, 100.0f);
	QMatrix4x4 view = overrideViewMatrix ? *overrideViewMatrix : _viewCtrl.viewMatrix();
	// Remove translation
	view.setColumn(3, QVector4D(0, 0, 0, 1));
	QMatrix4x4 model;
	model.rotate(CoordinateSystemHelper::cameraUpAxisConventionRotation(_viewCtrl.cameraUpAxisZUp()));
	if (!usePrefilterBlur)
		model.rotate(90.0f, QVector3D(1.0f, 0.0f, 0.0f)); // Z-up correction for raw env map
	model.rotate(_renderCtrl.skyBoxZRotation(), QVector3D(0.0f, 1.0f, 0.0f)); // User Z rotation (always applied)
	float skyboxLod = 0.0f;
	if (usePrefilterBlur)
	{
		// Reserve the top 10% of the old LOD range to avoid visible
		// banding/pixelation in the blurriest prefilter mips.
		const float t = (static_cast<float>(_renderCtrl.skyBoxBlurPercent()) / 100.0f) * 0.9f;
		skyboxLod = std::pow(t, 1.5f) * static_cast<float>(_renderCtrl.prefilterMipLevels() - 1);
	}
	_skyBox->setSceneRenderTransformFast(model);
	_renderCtrl.skyBoxShader()->setProperty("globalModelMatrix", QVariant::fromValue(QMatrix4x4()));
	_renderCtrl.skyBoxShader()->setProperty("viewMatrix", QVariant::fromValue(view));
	_renderCtrl.skyBoxShader()->setUniformValue("modelMatrix", model);
	_renderCtrl.skyBoxShader()->setUniformValue("viewMatrix", view);
	_renderCtrl.skyBoxShader()->setUniformValue("projectionMatrix", projection);
	_renderCtrl.skyBoxShader()->setUniformValue("hdrToneMapping", _renderCtrl.hdrToneMapping());
	_renderCtrl.skyBoxShader()->setUniformValue("gammaCorrection", _renderCtrl.gammaCorrection());
	_renderCtrl.skyBoxShader()->setUniformValue("screenGamma", _renderCtrl.screenGamma());
	_renderCtrl.skyBoxShader()->setUniformValue("envMapExposure", _renderCtrl.envMapExposure());
	_renderCtrl.skyBoxShader()->setUniformValue("iblExposure", _renderCtrl.iblExposure());
	_renderCtrl.skyBoxShader()->setUniformValue("toneMapMode", static_cast<int>(_renderCtrl.toneMappingMode()));
	_renderCtrl.skyBoxShader()->setUniformValue("useSkyboxLod", usePrefilterBlur);
	_renderCtrl.skyBoxShader()->setUniformValue("skyboxLod", skyboxLod);
	
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL); // change depth function so depth test passes when values are equal to depth buffer's content
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	_skyBox->render();
	glDepthFunc(GL_LESS); // set depth function back to default
	glDisable((GL_DEPTH_TEST));
}

void ViewportWidget::drawMesh(QOpenGLShaderProgram* prog, int activeCapPlaneIndex)
{
	QVector3D camPos = _primaryCamera->getRenderPosition();
	setupClippingUniforms(prog, camPos);
	if (_shadingNormalMode == ShadingNormalMode::FLAT &&
		prog == _renderCtrl.fgShader() &&
		_renderCtrl.fgFlatShader() && _renderCtrl.fgFlatShader()->isLinked())
	{
		syncUniformsToFlatShader();
	}

	if (_sceneRuntime.meshStore().empty()) return;

	const std::vector<int>& objectIds = _sceneRuntime.currentVisibleObjectIds();

	// Split — applying cap-plane straddle culling during collection
	std::vector<int> opaqueIds;
	std::vector<std::pair<float, int>> transparent; // (distance, id)

	opaqueIds.reserve(objectIds.size());
	transparent.reserve(objectIds.size());

	for (int id : objectIds)
	{
		if (SceneMesh* mesh = _sceneRuntime.meshAt(id))
		{
			// Capping stencil pass: skip meshes outside frustum or that don't
			// intersect the active cap plane — they contribute nothing to stencil.
			// Skinned meshes are exempt from frustum culling (same rationale as
			// isMeshVisible): their bind-pose AABB is stale after the initial
			// animation pose is applied, so the test would incorrectly drop them.
			if (activeCapPlaneIndex >= 0)
			{
				namespace VCH = VisibilityComputationHelper;
				if (!mesh->hasSkinning() && VCH::isMeshOutside(mesh, _frustumCtx)) continue;
				if (!VCH::isMeshStraddlesCapPlane(mesh, activeCapPlaneIndex, _clippingCtx)) continue;
			}

			if (mesh->isTransparent())
			{
				// Use a stable distance metric (camera -> mesh bounds center in world space)
				const QVector3D c = mesh->getBoundingSphere().getCenter();   // return center in world space
				const float R = mesh->getBoundingSphere().getRadius();
				const float d = (c - camPos).length();     // squared is fine for sorting
				// farthest point distance
				float farthest = d + R;
				transparent.emplace_back(farthest, id);
			}
			else
			{
				opaqueIds.push_back(id);
			}
		}
	}

	// 1) OPAQUE PASS: depth test ON, depth writes ON, blending OFF
	glDisable(GL_BLEND);
	glDepthMask(GL_TRUE);

	for (int id : opaqueIds)
	{
		if (SceneMesh* mesh = _sceneRuntime.meshAt(id))
		{
			mesh->setProg(prog);
			//mesh->render();             // render must NOT disable depth writes here
			renderMeshExploded(mesh, _displayMode);
		}
	}

	// 2) TRANSPARENT PASS: depth test ON, depth writes OFF, blending ON
	//    sort BACK-TO-FRONT (farthest first)
	std::sort(transparent.begin(), transparent.end(),
		[](const auto& a, const auto& b) { return a.first > b.first; });

	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_TRUE);

	for (auto& it : transparent)
	{
		if (SceneMesh* mesh = _sceneRuntime.meshAt(it.second))
		{
			mesh->setProg(prog);
			//mesh->render();             // render must preserve writes-off for this pass
			renderMeshExploded(mesh, _displayMode);
		}
	}

	// restore baseline
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
}

void ViewportWidget::drawOpaqueMeshes(QOpenGLShaderProgram* prog, int activeClipPlaneIndex)
{
	RenderableMesh::resetTextureBindingCacheForCurrentContext();
	RenderableMesh::resetBoundProgramCacheForCurrentContext();

	QVector3D camPos = _primaryCamera->getRenderPosition();
	setupClippingUniforms(prog, camPos);

	if (_sceneRuntime.meshStore().empty()) return;

	glDisable(GL_BLEND);
	glDepthMask(GL_TRUE);

	// Bind shader and set uniforms that are identical for every opaque mesh once,
	// outside the loop, to avoid redundant driver calls per draw.
	prog->bind();
	RenderableMesh::recordProgramBindCall(true);
	RenderableMesh::notifyProgramBound(prog);
	// Suppress hover highlighting while Ctrl is held — avoids flashes during
	// Ctrl+drag view manipulation as the pointer crosses mesh boundaries.
	const bool ctrlHeld = QGuiApplication::queryKeyboardModifiers() & Qt::ControlModifier;
	const bool hoverHighlightingEnabled = !ctrlHeld &&
		(_selectionManager->getHoverMode() != HoverHighlightMode::Disabled);
	prog->setUniformValue("hoverHighlighting", hoverHighlightingEnabled);
	prog->setUniformValue("hoverColor", QVector3D(1.0f, 0.84f, 0.0f));
	const int sssObjectIdLocation = prog->uniformLocation("sssObjectId");
	QOpenGLShaderProgram* flatProg = nullptr;
	int flatSssObjectIdLocation = -1;
	if (_shadingNormalMode == ShadingNormalMode::FLAT &&
		_renderCtrl.fgFlatShader() && _renderCtrl.fgFlatShader()->isLinked())
	{
		syncUniformsToFlatShader();
		flatProg = _renderCtrl.fgFlatShader();
		flatProg->setUniformValue("hoverHighlighting", hoverHighlightingEnabled);
		flatProg->setUniformValue("hoverColor", QVector3D(1.0f, 0.84f, 0.0f));
		flatSssObjectIdLocation = flatProg->uniformLocation("sssObjectId");
	}
	// Collect visible opaque meshes, then sort by texture signature to
	// minimise GPU texture state changes across consecutive draw calls.
	std::vector<std::pair<uint64_t, int>> opaque;
	if (_sceneRuntime.pendingSceneUuids().isEmpty() &&
		_sceneRuntime.runtimeVisibilityPrepared() &&
		_sceneRuntime.runtimeVisibilityRootIndex() >= 0)
	{
		std::vector<int> candidateIds;
		candidateIds.reserve(_sceneRuntime.currentVisibleObjectIds().size());
		collectVisibleMeshIdsForPass(_sceneRuntime.runtimeVisibilityRootIndex(), activeClipPlaneIndex, false, candidateIds);
		opaque.reserve(candidateIds.size());
		for (int id : candidateIds)
		{
			if (SceneMesh* mesh = _sceneRuntime.meshAt(id))
				opaque.emplace_back(mesh->getRenderMaterialSortKey(), id);
		}
	}
	else
	{
		const std::vector<int>& objectIds = _sceneRuntime.currentVisibleObjectIds();
		opaque.reserve(objectIds.size());
		for (int id : objectIds)
		{
			if (SceneMesh* mesh = _sceneRuntime.meshAt(id))
				if (!mesh->isTransparent() && isMeshVisible(mesh, activeClipPlaneIndex))
					opaque.emplace_back(mesh->getRenderMaterialSortKey(), id);
		}
	}
	std::sort(opaque.begin(), opaque.end(),
		[](const auto& a, const auto& b) { return a.first < b.first; });
	// Lightweight wire shader requires no active clip plane (it has no gl_ClipDistance).
	// The fast path supports both static and skinned meshes; wireframe rendering
	// intentionally uses a geometry-first contract rather than material-faithful
	// alpha/transmission behavior.
	const bool useWireShader = _renderCtrl.wireframeShader() && _renderCtrl.wireframeShader()->isLinked()
	    && activeClipPlaneIndex < 0;
	if (_displayMode == DisplayMode::HOLLOW_MESH)
	{
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glLineWidth(1.25f);
		glDisable(GL_POLYGON_OFFSET_FILL);
		if (useWireShader)
		{
			const QList<int> selIds = _selectionManager->getSelectedIds();
			RenderableMesh::bindProgramCached(_renderCtrl.wireframeShader());
			_renderCtrl.wireframeShader()->setUniformValue("viewMatrix",       _viewCtrl.viewMatrix());
			_renderCtrl.wireframeShader()->setUniformValue("projectionMatrix", _viewCtrl.projectionMatrix());
			_renderCtrl.wireframeShader()->setUniformValue("isWireframePass",  false);
			// Pass-level defaults: renderWireframeFast only uploads when non-default.
			_renderCtrl.wireframeShader()->setUniformValue("hasVertexColors", false);
			_renderCtrl.wireframeShader()->setUniformValue("hasAlbedoMap",    false);
			_renderCtrl.wireframeShader()->setUniformValue("hasSkinning",     false);
			_renderCtrl.wireframeShader()->setUniformValue("jointCount",      0);			
			_renderCtrl.wireframeShader()->setUniformValue("hoverColor", QVector3D(1.0f, 0.84f, 0.0f));
			_renderCtrl.wireframeShader()->setUniformValue("hovered", false);			
			_renderCtrl.wireframeShader()->setUniformValue("selectedColor", QVector3D(0.0f, 0.5f, 1.0f));	
			_renderCtrl.wireframeShader()->setUniformValue("selected", false);
			for (auto& [key, id] : opaque)
			{
				if (SceneMesh* mesh = _sceneRuntime.meshAt(id))
				{
					const bool isSel = selIds.contains(id);
					_renderCtrl.wireframeShader()->setUniformValue("selected", isSel);
					_renderCtrl.wireframeShader()->setUniformValue("hovered",
						!isSel && hoverHighlightingEnabled && id == _selectionManager->getHoveredId());
					mesh->renderWireframeFast(_renderCtrl.wireframeShader());
				}
			}
			_renderCtrl.wireframeShader()->setUniformValue("hovered", false);
			_renderCtrl.wireframeShader()->setUniformValue("selected", false);
		}
		else
		{
			for (auto& [key, id] : opaque)
			{
				if (SceneMesh* mesh = _sceneRuntime.meshAt(id))
				{
					mesh->setProg(prog);
					RenderableMesh::bindProgramCached(prog);
					prog->setUniformValue("hovered",
						hoverHighlightingEnabled && id == _selectionManager->getHoveredId());
					if (sssObjectIdLocation >= 0)
						prog->setUniformValue(sssObjectIdLocation, float(id + 1));
					mesh->render();
				}
			}
		}
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glLineWidth(1.0f);
	}
	else if (_displayMode == DisplayMode::MESH_EDGES)
	{
		// Solid pass — bias fill depth slightly back so the line overlay can sit on top stably.
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glLineWidth(1.0f);
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(1.25f, 1.25f);
		prog->setUniformValue("isWireframePass", false);
		for (auto& [key, id] : opaque)
		{
			if (SceneMesh* mesh = _sceneRuntime.meshAt(id))
			{
				mesh->setProg(prog);
				RenderableMesh::bindProgramCached(prog);
				prog->setUniformValue("hovered",
					hoverHighlightingEnabled && id == _selectionManager->getHoveredId());
				if (sssObjectIdLocation >= 0)
					prog->setUniformValue(sssObjectIdLocation, float(id + 1));
				mesh->render();
			}
		}
		glDisable(GL_POLYGON_OFFSET_FILL);

		// Wire pass — draw edges at true depth; no line offset needed.
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glLineWidth(1.5f);
		if (useWireShader)
		{			
			RenderableMesh::bindProgramCached(_renderCtrl.wireframeShader());
			_renderCtrl.wireframeShader()->setUniformValue("viewMatrix",       _viewCtrl.viewMatrix());
			_renderCtrl.wireframeShader()->setUniformValue("projectionMatrix", _viewCtrl.projectionMatrix());
			_renderCtrl.wireframeShader()->setUniformValue("isWireframePass",  true);
			// Pass-level defaults: renderWireframeFast only uploads when non-default.
			_renderCtrl.wireframeShader()->setUniformValue("hasVertexColors", false);
			_renderCtrl.wireframeShader()->setUniformValue("hasAlbedoMap",    false);
			_renderCtrl.wireframeShader()->setUniformValue("hasSkinning",     false);
			_renderCtrl.wireframeShader()->setUniformValue("jointCount",      0);			
			for (auto& [key, id] : opaque)
			{
				if (SceneMesh* mesh = _sceneRuntime.meshAt(id))					
					mesh->renderWireframeFast(_renderCtrl.wireframeShader());				
			}			
		}
		else
		{
			prog->setUniformValue("isWireframePass", true);
			for (auto& [key, id] : opaque)
			{
				if (SceneMesh* mesh = _sceneRuntime.meshAt(id))
				{
					mesh->setProg(prog);
					RenderableMesh::bindProgramCached(prog);
					prog->setUniformValue("hovered",
						hoverHighlightingEnabled && id == _selectionManager->getHoveredId());
					if (sssObjectIdLocation >= 0)
						prog->setUniformValue(sssObjectIdLocation, float(id + 1));
					mesh->render();
				}
			}
		}
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glLineWidth(1.0f);
		RenderableMesh::bindProgramCached(prog);
		prog->setUniformValue("isWireframePass", false);
	}
	else if (_displayMode == DisplayMode::WIREFRAME && useWireShader)
	{
		// True feature-edge wireframe — draw only pre-computed crease/boundary edges via GL_LINES.
		// No glPolygonMode needed; the feature-edge VAO draws GL_LINES primitives directly.
		glDisable(GL_POLYGON_OFFSET_FILL);
		glLineWidth(1.75f);
		RenderableMesh::bindProgramCached(_renderCtrl.wireframeShader());
		_renderCtrl.wireframeShader()->setUniformValue("viewMatrix",       _viewCtrl.viewMatrix());
		_renderCtrl.wireframeShader()->setUniformValue("projectionMatrix", _viewCtrl.projectionMatrix());
		_renderCtrl.wireframeShader()->setUniformValue("isWireframePass",  false);
		_renderCtrl.wireframeShader()->setUniformValue("hasVertexColors", false);
		_renderCtrl.wireframeShader()->setUniformValue("hasAlbedoMap",    false);
		_renderCtrl.wireframeShader()->setUniformValue("hasSkinning",     false);
		_renderCtrl.wireframeShader()->setUniformValue("jointCount",      0);
		_renderCtrl.wireframeShader()->setUniformValue("hoverColor",    QVector3D(1.0f, 0.84f, 0.0f));
		_renderCtrl.wireframeShader()->setUniformValue("hovered",       false);
		_renderCtrl.wireframeShader()->setUniformValue("selectedColor", QVector3D(0.25f, 0.55f, 1.0f));
		_renderCtrl.wireframeShader()->setUniformValue("selected",      false);
		{
			const QList<int> selIds = _selectionManager->getSelectedIds();
			for (auto& [key, id] : opaque)
			{
				if (SceneMesh* mesh = _sceneRuntime.meshAt(id))
				{
					const bool isSel = selIds.contains(id);
					_renderCtrl.wireframeShader()->setUniformValue("selected", isSel);
					_renderCtrl.wireframeShader()->setUniformValue("hovered",
					    !isSel && hoverHighlightingEnabled && id == _selectionManager->getHoveredId());
					mesh->renderFeatureEdgesFast(_renderCtrl.wireframeShader());
				}
			}
		}
		_renderCtrl.wireframeShader()->setUniformValue("hovered",  false);
		_renderCtrl.wireframeShader()->setUniformValue("selected", false);
		glLineWidth(1.0f);
	}
	else if (_displayMode == DisplayMode::SHADED_WITH_EDGES && useWireShader)
	{
		// Shaded with feature edges — solid PBR pass (offset back), then feature-edge GL_LINES overlay.
		// GL_POLYGON_OFFSET_FILL on the solid pass pushes polygon depth values slightly away from the
		// camera, so the GL_LINES overlay at the true surface depth always passes the depth test.
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glLineWidth(1.0f);
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(1.25f, 1.25f);
		prog->setUniformValue("isWireframePass", false);
		for (auto& [key, id] : opaque)
		{
			if (SceneMesh* mesh = _sceneRuntime.meshAt(id))
			{
				mesh->setProg(prog);
				RenderableMesh::bindProgramCached(prog);
				prog->setUniformValue("hovered",
					hoverHighlightingEnabled && id == _selectionManager->getHoveredId());
				if (sssObjectIdLocation >= 0)
					prog->setUniformValue(sssObjectIdLocation, float(id + 1));
				mesh->render();
			}
		}
		// Feature-edge overlay — no offset needed; solid was already pushed back.
		glDisable(GL_POLYGON_OFFSET_FILL);
		glLineWidth(1.5f);
		RenderableMesh::bindProgramCached(_renderCtrl.wireframeShader());
		_renderCtrl.wireframeShader()->setUniformValue("viewMatrix",       _viewCtrl.viewMatrix());
		_renderCtrl.wireframeShader()->setUniformValue("projectionMatrix", _viewCtrl.projectionMatrix());
		_renderCtrl.wireframeShader()->setUniformValue("isWireframePass",  true);
		_renderCtrl.wireframeShader()->setUniformValue("hasVertexColors", false);
		_renderCtrl.wireframeShader()->setUniformValue("hasAlbedoMap",    false);
		_renderCtrl.wireframeShader()->setUniformValue("hasSkinning",     false);
		_renderCtrl.wireframeShader()->setUniformValue("jointCount",      0);
		_renderCtrl.wireframeShader()->setUniformValue("hoverColor",    QVector3D(1.0f, 0.84f, 0.0f));
		_renderCtrl.wireframeShader()->setUniformValue("hovered",       false);
		_renderCtrl.wireframeShader()->setUniformValue("selectedColor", QVector3D(0.25f, 0.55f, 1.0f));
		_renderCtrl.wireframeShader()->setUniformValue("selected",      false);
		{
			const QList<int> selIds = _selectionManager->getSelectedIds();
			for (auto& [key, id] : opaque)
			{
				if (SceneMesh* mesh = _sceneRuntime.meshAt(id))
				{
					const bool isSel = selIds.contains(id);
					_renderCtrl.wireframeShader()->setUniformValue("selected", isSel);
					_renderCtrl.wireframeShader()->setUniformValue("hovered",
					    !isSel && hoverHighlightingEnabled && id == _selectionManager->getHoveredId());
					mesh->renderFeatureEdgesFast(_renderCtrl.wireframeShader());
				}
			}
		}
		_renderCtrl.wireframeShader()->setUniformValue("hovered",  false);
		_renderCtrl.wireframeShader()->setUniformValue("selected", false);
		glDisable(GL_POLYGON_OFFSET_FILL);
		glLineWidth(1.0f);
		RenderableMesh::bindProgramCached(prog);
		prog->setUniformValue("isWireframePass", false);
	}
	else
	{
		for (auto& [key, id] : opaque)
		{
			if (SceneMesh* mesh = _sceneRuntime.meshAt(id))
			{
				QOpenGLShaderProgram* activeProg = prog;
				int activeSssObjectIdLocation = sssObjectIdLocation;
				if (flatProg && mesh->getPrimitiveMode() == GL_TRIANGLES)
				{
					activeProg = flatProg;
					activeSssObjectIdLocation = flatSssObjectIdLocation;
				}
				mesh->setProg(activeProg);
				RenderableMesh::bindProgramCached(activeProg);
				activeProg->setUniformValue("hovered",
					hoverHighlightingEnabled && id == _selectionManager->getHoveredId());
				if (activeSssObjectIdLocation >= 0)
					activeProg->setUniformValue(activeSssObjectIdLocation, float(id + 1));
				renderMeshExploded(mesh, _displayMode);
			}
		}
	}
}


void ViewportWidget::drawTransparentMeshes(QOpenGLShaderProgram* prog, int activeClipPlaneIndex)
{
	RenderableMesh::resetTextureBindingCacheForCurrentContext();
	RenderableMesh::resetBoundProgramCacheForCurrentContext();

	QVector3D camPos = _primaryCamera->getRenderPosition();
	setupClippingUniforms(prog, camPos);

	if (_sceneRuntime.meshStore().empty()) return;

	std::vector<std::pair<float, int>> transparent;
	if (_sceneRuntime.pendingSceneUuids().isEmpty() &&
		_sceneRuntime.runtimeVisibilityPrepared() &&
		_sceneRuntime.runtimeVisibilityRootIndex() >= 0)
	{
		std::vector<int> candidateIds;
		candidateIds.reserve(_sceneRuntime.currentVisibleObjectIds().size());
		collectVisibleMeshIdsForPass(_sceneRuntime.runtimeVisibilityRootIndex(), activeClipPlaneIndex, true, candidateIds);
		transparent.reserve(candidateIds.size());
		for (int id : candidateIds)
		{
			if (SceneMesh* mesh = _sceneRuntime.meshAt(id))
			{
				const QVector3D c = mesh->getBoundingSphere().getCenter();
				const float R = mesh->getBoundingSphere().getRadius();
				const float d = (c - camPos).length();
				float farthest = d + R;
				transparent.emplace_back(farthest, id);
			}
		}
	}
	else
	{
		const std::vector<int>& objectIds = _sceneRuntime.currentVisibleObjectIds();
		transparent.reserve(objectIds.size());

		for (int id : objectIds)
		{
			if (SceneMesh* mesh = _sceneRuntime.meshAt(id))
			{
				if (mesh->isTransparent())
				{
					if (!isMeshVisible(mesh, activeClipPlaneIndex)) continue;
					const QVector3D c = mesh->getBoundingSphere().getCenter();
					const float R = mesh->getBoundingSphere().getRadius();
					const float d = (c - camPos).length();
					float farthest = d + R;
					transparent.emplace_back(farthest, id);
				}
			}
		}
	}

	// Sort far-to-near
	std::sort(transparent.begin(), transparent.end(),
		[](const auto& a, const auto& b) { return a.first > b.first; });

	glEnable(GL_BLEND);
	glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA,
		GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	// Bind once and set uniforms constant across all transparent meshes
	prog->bind();
	RenderableMesh::recordProgramBindCall(true);
	RenderableMesh::notifyProgramBound(prog);
	const bool ctrlHeldT = QGuiApplication::queryKeyboardModifiers() & Qt::ControlModifier;
	const bool hoverHighlightingEnabledT = !ctrlHeldT &&
		(_selectionManager->getHoverMode() != HoverHighlightMode::Disabled);
	prog->setUniformValue("hoverHighlighting", hoverHighlightingEnabledT);
	prog->setUniformValue("hoverColor", QVector3D(1.0f, 0.84f, 0.0f));
	const int sssObjectIdLocation = prog->uniformLocation("sssObjectId");
	QOpenGLShaderProgram* flatProg = nullptr;
	int flatSssObjectIdLocation = -1;
	if (_shadingNormalMode == ShadingNormalMode::FLAT &&
		_renderCtrl.fgFlatShader() && _renderCtrl.fgFlatShader()->isLinked())
	{
		syncUniformsToFlatShader();
		flatProg = _renderCtrl.fgFlatShader();
		flatProg->setUniformValue("hoverHighlighting", hoverHighlightingEnabledT);
		flatProg->setUniformValue("hoverColor", QVector3D(1.0f, 0.84f, 0.0f));
		flatSssObjectIdLocation = flatProg->uniformLocation("sssObjectId");
	}
	const bool useWireShaderT = _renderCtrl.wireframeShader() && _renderCtrl.wireframeShader()->isLinked()
	    && activeClipPlaneIndex < 0;
	if (_displayMode == DisplayMode::HOLLOW_MESH)
	{
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glLineWidth(1.25f);
		glDisable(GL_POLYGON_OFFSET_FILL);
		if (useWireShaderT)
		{
			const QList<int> selIds = _selectionManager->getSelectedIds();
			RenderableMesh::bindProgramCached(_renderCtrl.wireframeShader());
			_renderCtrl.wireframeShader()->setUniformValue("viewMatrix",       _viewCtrl.viewMatrix());
			_renderCtrl.wireframeShader()->setUniformValue("projectionMatrix", _viewCtrl.projectionMatrix());
			_renderCtrl.wireframeShader()->setUniformValue("isWireframePass",  false);
			// Pass-level defaults: renderWireframeFast only uploads when non-default.
			_renderCtrl.wireframeShader()->setUniformValue("hasVertexColors", false);
			_renderCtrl.wireframeShader()->setUniformValue("hasAlbedoMap",    false);
			_renderCtrl.wireframeShader()->setUniformValue("hasSkinning",     false);
			_renderCtrl.wireframeShader()->setUniformValue("jointCount",      0);
			_renderCtrl.wireframeShader()->setUniformValue("hoverColor", QVector3D(1.0f, 0.84f, 0.0f));	
			_renderCtrl.wireframeShader()->setUniformValue("hovered", false);
			_renderCtrl.wireframeShader()->setUniformValue("selectedColor", QVector3D(0.0f, 0.5f, 1.0f));
			_renderCtrl.wireframeShader()->setUniformValue("selected", false);
			for (auto& it : transparent)
			{
				if (SceneMesh* mesh = _sceneRuntime.meshAt(it.second))
				{
					const bool isSel = selIds.contains(it.second);
					_renderCtrl.wireframeShader()->setUniformValue("selected", isSel);
					_renderCtrl.wireframeShader()->setUniformValue("hovered",
						!isSel && hoverHighlightingEnabledT && it.second == _selectionManager->getHoveredId());
					mesh->renderWireframeFast(_renderCtrl.wireframeShader());
				}
			}
			_renderCtrl.wireframeShader()->setUniformValue("hovered", false);
			_renderCtrl.wireframeShader()->setUniformValue("selected", false);
		}
		else
		{
			for (auto& it : transparent)
			{
				const int id = it.second;
				if (SceneMesh* mesh = _sceneRuntime.meshAt(id))
				{
					mesh->setProg(prog);
					RenderableMesh::bindProgramCached(prog);
					prog->setUniformValue("hovered",
						hoverHighlightingEnabledT && id == _selectionManager->getHoveredId());
					if (sssObjectIdLocation >= 0)
						prog->setUniformValue(sssObjectIdLocation, float(id + 1));
					mesh->render();
				}
			}
		}
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glLineWidth(1.0f);
	}
	else if (_displayMode == DisplayMode::MESH_EDGES)
	{
		// Solid pass — bias fill depth slightly back so the line overlay can sit on top stably.
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glLineWidth(1.0f);
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(1.25f, 1.25f);
		prog->setUniformValue("isWireframePass", false);
		for (auto& it : transparent)
		{
			if (SceneMesh* mesh = _sceneRuntime.meshAt(it.second))
			{
				const int id = it.second;
				mesh->setProg(prog);
				RenderableMesh::bindProgramCached(prog);
				prog->setUniformValue("hovered",
					hoverHighlightingEnabledT && id == _selectionManager->getHoveredId());
				if (sssObjectIdLocation >= 0)
					prog->setUniformValue(sssObjectIdLocation, float(id + 1));
				mesh->render();
			}
		}
		glDisable(GL_POLYGON_OFFSET_FILL);

		// Wire pass — draw edges at true depth; no line offset needed.
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glLineWidth(1.5f);
		if (useWireShaderT)
		{
			RenderableMesh::bindProgramCached(_renderCtrl.wireframeShader());
			_renderCtrl.wireframeShader()->setUniformValue("viewMatrix",       _viewCtrl.viewMatrix());
			_renderCtrl.wireframeShader()->setUniformValue("projectionMatrix", _viewCtrl.projectionMatrix());
			_renderCtrl.wireframeShader()->setUniformValue("isWireframePass",  true);
			// Pass-level defaults: renderWireframeFast only uploads when non-default.
			_renderCtrl.wireframeShader()->setUniformValue("hasVertexColors", false);
			_renderCtrl.wireframeShader()->setUniformValue("hasAlbedoMap",    false);
			_renderCtrl.wireframeShader()->setUniformValue("hasSkinning",     false);
			_renderCtrl.wireframeShader()->setUniformValue("jointCount",      0);
			for (auto& it : transparent)
			{
				if (SceneMesh* mesh = _sceneRuntime.meshAt(it.second))
					mesh->renderWireframeFast(_renderCtrl.wireframeShader());
			}
		}
		else
		{
			prog->setUniformValue("isWireframePass", true);
			for (auto& it : transparent)
			{
				const int id = it.second;
				if (SceneMesh* mesh = _sceneRuntime.meshAt(id))
				{
					mesh->setProg(prog);
					RenderableMesh::bindProgramCached(prog);
					prog->setUniformValue("hovered",
						hoverHighlightingEnabledT && id == _selectionManager->getHoveredId());
					if (sssObjectIdLocation >= 0)
						prog->setUniformValue(sssObjectIdLocation, float(id + 1));
					mesh->render();
				}
			}
		}
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glLineWidth(1.0f);
		RenderableMesh::bindProgramCached(prog);
		prog->setUniformValue("isWireframePass", false);
	}
	else if (_displayMode == DisplayMode::WIREFRAME && useWireShaderT)
	{
		glDisable(GL_POLYGON_OFFSET_FILL);
		glLineWidth(1.75f);
		RenderableMesh::bindProgramCached(_renderCtrl.wireframeShader());
		_renderCtrl.wireframeShader()->setUniformValue("viewMatrix",       _viewCtrl.viewMatrix());
		_renderCtrl.wireframeShader()->setUniformValue("projectionMatrix", _viewCtrl.projectionMatrix());
		_renderCtrl.wireframeShader()->setUniformValue("isWireframePass",  false);
		_renderCtrl.wireframeShader()->setUniformValue("hasVertexColors", false);
		_renderCtrl.wireframeShader()->setUniformValue("hasAlbedoMap",    false);
		_renderCtrl.wireframeShader()->setUniformValue("hasSkinning",     false);
		_renderCtrl.wireframeShader()->setUniformValue("jointCount",      0);
		_renderCtrl.wireframeShader()->setUniformValue("hoverColor",    QVector3D(1.0f, 0.84f, 0.0f));
		_renderCtrl.wireframeShader()->setUniformValue("hovered",       false);
		_renderCtrl.wireframeShader()->setUniformValue("selectedColor", QVector3D(0.25f, 0.55f, 1.0f));
		_renderCtrl.wireframeShader()->setUniformValue("selected",      false);
		{
			const QList<int> selIds = _selectionManager->getSelectedIds();
			for (auto& it : transparent)
			{
				if (SceneMesh* mesh = _sceneRuntime.meshAt(it.second))
				{
					const int id = it.second;
					const bool isSel = selIds.contains(id);
					_renderCtrl.wireframeShader()->setUniformValue("selected", isSel);
					_renderCtrl.wireframeShader()->setUniformValue("hovered",
					    !isSel && hoverHighlightingEnabledT && id == _selectionManager->getHoveredId());
					mesh->renderFeatureEdgesFast(_renderCtrl.wireframeShader());
				}
			}
		}
		_renderCtrl.wireframeShader()->setUniformValue("hovered",  false);
		_renderCtrl.wireframeShader()->setUniformValue("selected", false);
		glLineWidth(1.0f);
	}
	else if (_displayMode == DisplayMode::SHADED_WITH_EDGES && useWireShaderT)
	{
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glLineWidth(1.0f);
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(1.25f, 1.25f);
		prog->setUniformValue("isWireframePass", false);
		for (auto& it : transparent)
		{
			if (SceneMesh* mesh = _sceneRuntime.meshAt(it.second))
			{
				const int id = it.second;
				mesh->setProg(prog);
				RenderableMesh::bindProgramCached(prog);
				prog->setUniformValue("hovered",
					hoverHighlightingEnabledT && id == _selectionManager->getHoveredId());
				if (sssObjectIdLocation >= 0)
					prog->setUniformValue(sssObjectIdLocation, float(id + 1));
				mesh->render();
			}
		}
		glDisable(GL_POLYGON_OFFSET_FILL);
		glLineWidth(1.5f);
		RenderableMesh::bindProgramCached(_renderCtrl.wireframeShader());
		_renderCtrl.wireframeShader()->setUniformValue("viewMatrix",       _viewCtrl.viewMatrix());
		_renderCtrl.wireframeShader()->setUniformValue("projectionMatrix", _viewCtrl.projectionMatrix());
		_renderCtrl.wireframeShader()->setUniformValue("isWireframePass",  true);
		_renderCtrl.wireframeShader()->setUniformValue("hasVertexColors", false);
		_renderCtrl.wireframeShader()->setUniformValue("hasAlbedoMap",    false);
		_renderCtrl.wireframeShader()->setUniformValue("hasSkinning",     false);
		_renderCtrl.wireframeShader()->setUniformValue("jointCount",      0);
		_renderCtrl.wireframeShader()->setUniformValue("hoverColor",    QVector3D(1.0f, 0.84f, 0.0f));
		_renderCtrl.wireframeShader()->setUniformValue("hovered",       false);
		_renderCtrl.wireframeShader()->setUniformValue("selectedColor", QVector3D(0.25f, 0.55f, 1.0f));
		_renderCtrl.wireframeShader()->setUniformValue("selected",      false);
		{
			const QList<int> selIds = _selectionManager->getSelectedIds();
			for (auto& it : transparent)
			{
				if (SceneMesh* mesh = _sceneRuntime.meshAt(it.second))
				{
					const int id = it.second;
					const bool isSel = selIds.contains(id);
					_renderCtrl.wireframeShader()->setUniformValue("selected", isSel);
					_renderCtrl.wireframeShader()->setUniformValue("hovered",
					    !isSel && hoverHighlightingEnabledT && id == _selectionManager->getHoveredId());
					mesh->renderFeatureEdgesFast(_renderCtrl.wireframeShader());
				}
			}
		}
		_renderCtrl.wireframeShader()->setUniformValue("hovered",  false);
		_renderCtrl.wireframeShader()->setUniformValue("selected", false);
		glDisable(GL_POLYGON_OFFSET_FILL);
		glLineWidth(1.0f);
		RenderableMesh::bindProgramCached(prog);
		prog->setUniformValue("isWireframePass", false);
	}
	else
	{
		for (auto& it : transparent)
		{
			if (SceneMesh* mesh = _sceneRuntime.meshAt(it.second))
			{
				const int id = it.second;
				QOpenGLShaderProgram* activeProg = prog;
				int activeSssObjectIdLocation = sssObjectIdLocation;
				if (flatProg && mesh->getPrimitiveMode() == GL_TRIANGLES)
				{
					activeProg = flatProg;
					activeSssObjectIdLocation = flatSssObjectIdLocation;
				}
				mesh->setProg(activeProg);
				RenderableMesh::bindProgramCached(activeProg);
				activeProg->setUniformValue("hovered",
					hoverHighlightingEnabledT && id == _selectionManager->getHoveredId());
				if (activeSssObjectIdLocation >= 0)
					activeProg->setUniformValue(activeSssObjectIdLocation, float(id + 1));
				renderMeshExploded(mesh, _displayMode);
			}
		}
	}

	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
}

// ---------------------------------------------------------------------------
// Visibility culling helpers
// ---------------------------------------------------------------------------

void ViewportWidget::collectVisibleMeshIdsForPass(int nodeIndex,
                                            int activeClipPlaneIndex,
                                            bool wantTransparent,
                                            std::vector<int>& out) const
{
	if (nodeIndex < 0 || nodeIndex >= _sceneRuntime.runtimeVisibilityNodes().size())
		return;

	const RuntimeVisibilityNode& runtimeNode = _sceneRuntime.runtimeVisibilityNodes()[nodeIndex];
	if (!runtimeNode.subtreeHasVisibleMeshes)
		return;
	namespace VCH = VisibilityComputationHelper;
	if (VCH::isBoundingBoxOutside(runtimeNode.subtreeBounds, _frustumCtx))
		return;

	if (activeClipPlaneIndex >= 0)
	{
		if (VCH::isBoundingBoxInvisibleInAllClipPasses(runtimeNode.subtreeBounds, _clippingCtx))
			return;
		if (activeClipPlaneIndex == 0 && VCH::isBoundingBoxFullyClipped_X(runtimeNode.subtreeBounds, _clippingCtx))
			return;
		if (activeClipPlaneIndex == 1 && VCH::isBoundingBoxFullyClipped_Y(runtimeNode.subtreeBounds, _clippingCtx))
			return;
		if (activeClipPlaneIndex == 2 && VCH::isBoundingBoxFullyClipped_Z(runtimeNode.subtreeBounds, _clippingCtx))
			return;
	}

	for (int meshIndex : std::as_const(runtimeNode.meshIndices))
	{
		if (meshIndex < 0 || meshIndex >= static_cast<int>(_sceneRuntime.meshStore().size()))
			continue;
		const SceneMesh* mesh = _sceneRuntime.meshAt(meshIndex);
		if (!mesh)
			continue;
		if (meshIndex >= static_cast<int>(_sceneRuntime.runtimeBaseVisibleMask().size()) ||
		    !_sceneRuntime.runtimeBaseVisibleMask()[meshIndex])
			continue;
		if (!isMeshVisible(mesh, activeClipPlaneIndex))
			continue;
		if (mesh->isTransparent() == wantTransparent)
			out.push_back(meshIndex);
	}

	for (int childIndex : std::as_const(runtimeNode.children))
		collectVisibleMeshIdsForPass(childIndex, activeClipPlaneIndex, wantTransparent, out);
}

void ViewportWidget::extractFrustumPlanes()
{
	_viewCtrl.updateFrustumPlanes();
	const QVector4D* planes = _viewCtrl.frustumPlanes();
	for (int i = 0; i < 6; ++i)
		_frustumCtx.planes[i] = planes[i];
}

void ViewportWidget::rebuildClippingContext()
{
	using namespace VisibilityComputationHelper;
	const auto& center = _viewCtrl.boundingBox().center();
	_clippingCtx.x = { _renderCtrl.clippingXCoeff() + static_cast<float>(center.getX()),
	                   _renderCtrl.clippingXFlipped() };
	_clippingCtx.y = { _renderCtrl.clippingYCoeff() + static_cast<float>(center.getY()),
	                   _renderCtrl.clippingYFlipped() };
	_clippingCtx.z = { _renderCtrl.clippingZCoeff() + static_cast<float>(center.getZ()),
	                   _renderCtrl.clippingZFlipped() };
	_clippingCtx.yzEnabled = _renderCtrl.yzClippingEnabled();
	_clippingCtx.zxEnabled = _renderCtrl.zxClippingEnabled();
	_clippingCtx.xyEnabled = _renderCtrl.xyClippingEnabled();
}

// Returns the minimum bounding-sphere radius among meshes that are completely
// enclosed within the current view frustum.  Used to derive a dynamic zoom
// floor: as the camera zooms into a sub-mesh, large meshes leave the frustum
// and the floor shrinks to match the focused geometry, preventing the eye from
// ever entering the mesh the user is actually looking at.
float ViewportWidget::computeFullyVisibleMinMeshRadius() const
{
	const std::vector<int>& ids =
		_sceneRuntime.currentVisibleObjectIds();

	float minRadius = std::numeric_limits<float>::max();
	for (int id : ids)
	{
		if (id < 0 || id >= static_cast<int>(_sceneRuntime.meshStore().size())) continue;
		const SceneMesh* mesh = _sceneRuntime.meshAt(id);
		if (!mesh) continue;
		if (!isMeshAnimationVisible(mesh)) continue;   // hidden by animation

		if (mesh->hasSkinning())
		{
			// The stored AABB is the bind-pose box and is unreliable for
			// frustum testing after skinning is applied.  Skip the frustum
			// test entirely and always count the mesh — this ensures animated
			// models participate in sub-mesh zoom granularity regardless of pose.
			const float r = mesh->getBoundingSphere().getRadius();
			if (r > 0.0f)
				minRadius = std::min(minRadius, r);
			continue;
		}

		if (VisibilityComputationHelper::isMeshOutside(mesh, _frustumCtx))      continue;   // not visible at all
		if (!VisibilityComputationHelper::isMeshFullyInside(mesh, _frustumCtx)) continue;   // only partially in view

		const float r = mesh->getBoundingSphere().getRadius();
		if (r > 0.0f)
			minRadius = std::min(minRadius, r);
	}

	// Fall back to global scene radius when nothing qualifies
	// (e.g. zoomed so far out the whole scene spans the viewport).
	return (minRadius < std::numeric_limits<float>::max())
		? minRadius
		: _viewCtrl.boundingSphere().getRadius();
}

// Update the cached zoom-in limit with asymmetric smoothing:
//   • Decreases are applied immediately so zoom-in is never blocked.
//   • Increases are blended gradually (factor 0.12 ≈ 10 events to reach ~72%)
//     so the clamp creeps back rather than snapping when the focused mesh
//     transitions out of the "fully inside" frustum set.
void ViewportWidget::updateZoomInLimit()
{
	const float rawFloor = computeFullyVisibleMinMeshRadius();
	if (rawFloor <= _viewCtrl.zoomInLimit())
		_viewCtrl.setZoomInLimit(rawFloor);                                         // drop: immediate
	else
		_viewCtrl.setZoomInLimit(_viewCtrl.zoomInLimit()
			+ (rawFloor - _viewCtrl.zoomInLimit()) * 0.12f);                           // rise: gradual
}

bool ViewportWidget::isMeshAnimationVisible(const SceneMesh* mesh) const
{
	if (!mesh)
		return false;
	if (_animCtrl.animatedMeshVisibilitySourceFile().isEmpty())
		return true;
	if (mesh->getSourceFile() != _animCtrl.animatedMeshVisibilitySourceFile())
		return true;
	return !_animCtrl.animatedHiddenMeshUuids().contains(mesh->uuid());
}

bool ViewportWidget::isMeshVisible(const SceneMesh* mesh, int activeClipPlaneIndex) const
{
	if (!isMeshAnimationVisible(mesh)) return false;

	// 1. Frustum cull — applied in every pass, clipping or not.
	// Skip frustum culling for any skinned mesh.
	// For GPU-skinned meshes the world-space bounding box stored in the mesh
	// is the BIND-POSE (T-pose) box — it reflects the raw vertex positions
	// before joint matrices are applied.  Our viewer always applies the
	// animation pose at load time (time = 0), so the actual rendered positions
	// can differ significantly from the bind-pose box even when no animation is
	// actively playing.  A frustum test against the stale bind-pose AABB would
	// incorrectly cull visible limbs, causing them to vanish on zoom-in.
	// Skipping the test for all skinned meshes is safe: the GPU rasterizer
	// clips any geometry that is genuinely outside the viewport at no extra cost,
	// and the performance cost of drawing a few extra off-screen skinned meshes
	// is negligible compared to the cost of the skinning shader itself.
	namespace VCH = VisibilityComputationHelper;
	if (!mesh->hasSkinning() && VCH::isMeshOutside(mesh, _frustumCtx)) return false;

	// 2. No clip planes in this pass → frustum result is final
	if (activeClipPlaneIndex < 0) return true;

	// 3. Pre-pass elimination: if ALL active planes fully clip this mesh it is
	//    invisible across every union pass — skip it entirely
	if (VCH::isMeshInvisibleInAllClipPasses(mesh, _clippingCtx)) return false;

	// 4. Per-pass cull: skip if fully clipped by the one plane active in this pass
	if (activeClipPlaneIndex == 0 && VCH::isMeshFullyClipped_X(mesh, _clippingCtx)) return false;
	if (activeClipPlaneIndex == 1 && VCH::isMeshFullyClipped_Y(mesh, _clippingCtx)) return false;
	if (activeClipPlaneIndex == 2 && VCH::isMeshFullyClipped_Z(mesh, _clippingCtx)) return false;

	return true;
}

bool ViewportWidget::sceneHasVisibleTransmissionMaterials() const
{
	const std::vector<int>& ids = _sceneRuntime.currentVisibleObjectIds();
	for (int id : ids)
	{
		if (id < 0 || id >= static_cast<int>(_sceneRuntime.meshStore().size()))
			continue;
		const SceneMesh* mesh = _sceneRuntime.meshAt(id);
		if (!mesh || !isMeshAnimationVisible(mesh))
			continue;

		const Material& mat = mesh->getMaterial();
		if (mat.hasTransmission() || mat.diffuseTransmissionFactor() > 0.0f)
			return true;
	}
	return false;
}

bool ViewportWidget::sceneHasVisibleSSSMaterials() const
{
	const std::vector<int>& ids = _sceneRuntime.currentVisibleObjectIds();
	for (int id : ids)
	{
		if (id < 0 || id >= static_cast<int>(_sceneRuntime.meshStore().size()))
			continue;
		const SceneMesh* mesh = _sceneRuntime.meshAt(id);
		if (!mesh || !isMeshAnimationVisible(mesh))
			continue;
		if (mesh->getMaterial().hasVolumeScattering())
			return true;
	}
	return false;
}

// ---------------------------------------------------------------------------

// Renders only opaque SSS meshes (hasVolumeScattering) — used exclusively
// by the SSS capture pre-pass to avoid submitting the entire scene.
void ViewportWidget::drawSSSMeshesOnly(QOpenGLShaderProgram* prog, int activeClipPlaneIndex)
{
	RenderableMesh::resetTextureBindingCacheForCurrentContext();

	// Collect SSS-only candidates, mirroring the two-path strategy in drawOpaqueMeshes.
	std::vector<std::pair<uint64_t, int>> sssMeshes;
	if (_sceneRuntime.pendingSceneUuids().isEmpty() &&
		_sceneRuntime.runtimeVisibilityPrepared() &&
		_sceneRuntime.runtimeVisibilityRootIndex() >= 0)
	{
		std::vector<int> candidateIds;
		collectVisibleMeshIdsForPass(_sceneRuntime.runtimeVisibilityRootIndex(),
		                             activeClipPlaneIndex, false, candidateIds);
		sssMeshes.reserve(candidateIds.size());
		for (int id : candidateIds)
		{
			if (const SceneMesh* mesh = _sceneRuntime.meshAt(id))
				if (mesh->getMaterial().hasVolumeScattering())
					sssMeshes.emplace_back(mesh->getRenderMaterialSortKey(), id);
		}
	}
	else
	{
		const std::vector<int>& ids = _sceneRuntime.currentVisibleObjectIds();
		sssMeshes.reserve(ids.size());
		for (int id : ids)
		{
			SceneMesh* mesh = _sceneRuntime.meshAt(id);
			if (!mesh || mesh->isTransparent()) continue;
			if (!mesh->getMaterial().hasVolumeScattering()) continue;
			if (!isMeshVisible(mesh, activeClipPlaneIndex)) continue;
			sssMeshes.emplace_back(mesh->getRenderMaterialSortKey(), id);
		}
	}

	std::sort(sssMeshes.begin(), sssMeshes.end(),
		[](const auto& a, const auto& b) { return a.first < b.first; });

	for (auto& [key, id] : sssMeshes)
	{
		if (SceneMesh* mesh = _sceneRuntime.meshAt(id))
		{
			mesh->setProg(prog);
			RenderableMesh::bindProgramCached(prog);
			mesh->render();
		}
	}
}

void ViewportWidget::drawMeshesWithClipping(QOpenGLShaderProgram* prog,
	bool transparentPass)
{
	RenderableMesh::resetTextureBindingCacheForCurrentContext();

	//glPolygonMode(GL_FRONT_AND_BACK, _displayMode == DisplayMode::HOLLOW_MESH ? GL_LINE : GL_FILL);
	//glLineWidth(_displayMode == DisplayMode::HOLLOW_MESH ? 1.25 : 1.0);

	// https://stackoverflow.com/questions/16901829/how-to-clip-only-intersection-not-union-of-clipping-planes
	// If any clipping is active
	if (_renderCtrl.yzClippingEnabled() || _renderCtrl.zxClippingEnabled() || _renderCtrl.xyClippingEnabled())
	{
		// Then draw meshes with clip planes enabled.
		// Each pass activates one plane to produce the union of all half-spaces.
		// activeClipPlaneIndex (0/1/2) tells the draw functions which single plane
		// is active so per-pass AABB culling tests only that plane.
		if (_renderCtrl.yzClippingEnabled())
		{
			glEnable(GL_CLIP_DISTANCE0);
			if (transparentPass) drawTransparentMeshes(prog, 0);
			else                 drawOpaqueMeshes(prog, 0);
			glDisable(GL_CLIP_DISTANCE0);
		}
		if (_renderCtrl.zxClippingEnabled())
		{
			glEnable(GL_CLIP_DISTANCE1);
			if (transparentPass) drawTransparentMeshes(prog, 1);
			else                 drawOpaqueMeshes(prog, 1);
			glDisable(GL_CLIP_DISTANCE1);
		}
		if (_renderCtrl.xyClippingEnabled())
		{
			glEnable(GL_CLIP_DISTANCE2);
			if (transparentPass) drawTransparentMeshes(prog, 2);
			else                 drawOpaqueMeshes(prog, 2);
			glDisable(GL_CLIP_DISTANCE2);
		}
	}
	else
	{
		// No clipping at all — frustum culling only (activeClipPlaneIndex = -1)
		if (transparentPass) drawTransparentMeshes(prog);
		else                 drawOpaqueMeshes(prog);
	}
}


void ViewportWidget::setCommonUniforms(QOpenGLShaderProgram* prog, Camera* camera)
{
	QVector3D camPos = camera->getRenderPosition();
	QVector3D camDir = camera->getViewDir();
	const QVector3D shaderLightPos = effectiveWorldLightPosition();

	prog->setUniformValue("lightSource.position",
		shaderLightPos);
	prog->setUniformValue("modelViewMatrix", _viewCtrl.modelViewMatrix());
	prog->setUniformValue("normalMatrix", _viewCtrl.modelViewMatrix().normalMatrix());
	const QMatrix4x4 projMatrix = camera->getProjectionMatrix();
	prog->setUniformValue("projectionMatrix", projMatrix);
	prog->setUniformValue("inverseProjectionMatrix", projMatrix.inverted());
	prog->setUniformValue("viewportMatrix", _viewCtrl.viewportMatrix());

	const QVector3D worldUp = CoordinateSystemHelper::currentWorldUpVector(_viewCtrl.cameraUpAxisZUp());
	QVector3D viewDir = _primaryCamera->getViewDir();
	bool floorVisible = QVector3D::dotProduct(viewDir, worldUp) < 0.0f;
	bool showShadows = (_renderCtrl.shadowsEnabled() && floorVisible && !_renderCtrl.lowResEnabled() && camera == _primaryCamera);
	const bool interactionFastPath = _renderCtrl.lowResEnabled() && (_displayedObjectsMemSize > MAX_MODEL_SIZE_BYTES);

	prog->setUniformValue("shadowsEnabled", showShadows);
	prog->setUniformValue("selfShadowsEnabled", _renderCtrl.selfShadowsEnabled());
	prog->setUniformValue("interactionFastPath", interactionFastPath);
	prog->setUniformValue("cameraPos", camPos);
	prog->setUniformValue("cameraDir", camDir);
	prog->setUniformValue("lightPos",
		shaderLightPos);
	RenderableMesh::setCurrentRenderContext(_viewCtrl.modelMatrix(), camera->getViewMatrix());
	// Second consumer of the same interactionFastPath signal that already
	// gates shadow-map sampling and expensive shader branches above - drops
	// eligible meshes to their precomputed coarse LOD1 tier for the same
	// interaction+oversized-model window, snapping back to full resolution
	// the instant lowResEnabled() clears. See RenderableMesh::setLodPolicy().
	RenderableMesh::setLodPolicy(interactionFastPath);
	prog->setUniformValue("modelMatrix", _viewCtrl.modelMatrix());
	prog->setUniformValue("viewMatrix", camera->getViewMatrix());
	prog->setUniformValue("lightSpaceMatrix", _lightSpaceMatrix);
	prog->setUniformValue("lightFarPlane", _renderCtrl.shadowFarDist());
	prog->setUniformValue("hdrToneMapping", _renderCtrl.hdrToneMapping());
	prog->setUniformValue("gammaCorrection", _renderCtrl.gammaCorrection());
	prog->setUniformValue("screenGamma", _renderCtrl.screenGamma());
	prog->setUniformValue("envMapExposure", _renderCtrl.envMapExposure());
	prog->setUniformValue("iblExposure", _renderCtrl.iblExposure());
	prog->setUniformValue("toneMapMode", static_cast<int>(_renderCtrl.toneMappingMode()));	
	prog->setUniformValue("selectionHighlighting", _selectionHighlighting);

	prog->setUniformValue("transmissionFramebufferSize",
		QVector2D(_renderCtrl.transmissionTextureWidth(), _renderCtrl.transmissionTextureHeight()));
	prog->setUniformValue("sssFramebufferSize",
		QVector2D(_renderCtrl.sssTextureWidth(), _renderCtrl.sssTextureHeight()));

	prog->setUniformValue("useDefaultLights", _renderCtrl.useDefaultLights());
	prog->setUniformValue("usePunctualLights", _renderCtrl.usePunctualLights());
	prog->setUniformValue("useIBL", _renderCtrl.useIBL());

	prog->setUniformValue("worldUpAxis", _viewCtrl.cameraUpAxisZUp() ? 2 : 1);

	bindIBLTextures();
}


// ---------------------------------------------------------------------------
// syncUniformsToFlatShader
// Copies every active uniform from _renderCtrl.fgShader() to _renderCtrl.fgFlatShader() using
// glGetActiveUniform / glGetUniform* / glProgramUniform*.  This is called
// once per flat-shaded triangle-mesh draw so that the flat shader always has
// the same per-frame and per-mesh state as the main shader without requiring
// every setUniformValue call site to be duplicated.
//
// glProgramUniform* (OpenGL 4.1 core) writes directly to the named program
// object without needing to bind it — no pipeline state disturbance.
// ---------------------------------------------------------------------------
void ViewportWidget::syncUniformsToFlatShader()
{
    if (!_renderCtrl.fgFlatShader() || !_renderCtrl.fgFlatShader()->isLinked()) return;

    const GLuint srcProg = _renderCtrl.fgShader()->programId();
    const GLuint dstProg = _renderCtrl.fgFlatShader()->programId();

    GLint numUniforms = 0;
    glGetProgramiv(srcProg, GL_ACTIVE_UNIFORMS, &numUniforms);

    for (GLint idx = 0; idx < numUniforms; ++idx)
    {
        char rawName[512];
        GLsizei nameLen = 0;
        GLint   size    = 0;    // array size (1 for non-arrays)
        GLenum  type    = 0;
        glGetActiveUniform(srcProg, static_cast<GLuint>(idx),
                           static_cast<GLsizei>(sizeof(rawName)),
                           &nameLen, &size, &type, rawName);

        // glGetActiveUniform appends "[0]" for the base name of arrays.
        // Strip it so we can re-attach "[N]" ourselves per element.
        QString baseName = QString::fromLatin1(rawName, nameLen);
        if (baseName.endsWith(QLatin1String("[0]")))
            baseName.chop(3);

        for (GLint elem = 0; elem < size; ++elem)
        {
            const QString elemName = (size > 1)
                ? QStringLiteral("%1[%2]").arg(baseName).arg(elem)
                : baseName;

            const QByteArray en   = elemName.toLatin1();
            const char*      encs = en.constData();

            const GLint srcLoc = glGetUniformLocation(srcProg, encs);
            const GLint dstLoc = glGetUniformLocation(dstProg, encs);
            if (srcLoc < 0 || dstLoc < 0) continue;

            switch (type)
            {
            case GL_FLOAT: {
                GLfloat v; glGetUniformfv(srcProg, srcLoc, &v);
                glProgramUniform1f(dstProg, dstLoc, v);
                break;
            }
            case GL_FLOAT_VEC2: {
                GLfloat v[2]; glGetUniformfv(srcProg, srcLoc, v);
                glProgramUniform2fv(dstProg, dstLoc, 1, v);
                break;
            }
            case GL_FLOAT_VEC3: {
                GLfloat v[3]; glGetUniformfv(srcProg, srcLoc, v);
                glProgramUniform3fv(dstProg, dstLoc, 1, v);
                break;
            }
            case GL_FLOAT_VEC4: {
                GLfloat v[4]; glGetUniformfv(srcProg, srcLoc, v);
                glProgramUniform4fv(dstProg, dstLoc, 1, v);
                break;
            }
            case GL_INT:
            case GL_BOOL: {
                GLint v; glGetUniformiv(srcProg, srcLoc, &v);
                glProgramUniform1i(dstProg, dstLoc, v);
                break;
            }
            case GL_UNSIGNED_INT: {
                GLuint v; glGetUniformuiv(srcProg, srcLoc, &v);
                glProgramUniform1ui(dstProg, dstLoc, v);
                break;
            }
            case GL_FLOAT_MAT2: {
                GLfloat v[4]; glGetUniformfv(srcProg, srcLoc, v);
                glProgramUniformMatrix2fv(dstProg, dstLoc, 1, GL_FALSE, v);
                break;
            }
            case GL_FLOAT_MAT3: {
                GLfloat v[9]; glGetUniformfv(srcProg, srcLoc, v);
                glProgramUniformMatrix3fv(dstProg, dstLoc, 1, GL_FALSE, v);
                break;
            }
            case GL_FLOAT_MAT4: {
                GLfloat v[16]; glGetUniformfv(srcProg, srcLoc, v);
                glProgramUniformMatrix4fv(dstProg, dstLoc, 1, GL_FALSE, v);
                break;
            }
            // Sampler types: the uniform value is the texture-unit integer.
            // Texture bindings themselves are global GL state — just copy the unit.
            case GL_SAMPLER_1D:
            case GL_SAMPLER_2D:
            case GL_SAMPLER_3D:
            case GL_SAMPLER_CUBE:
            case GL_SAMPLER_2D_ARRAY:
            case GL_SAMPLER_2D_SHADOW:
            case GL_SAMPLER_CUBE_SHADOW:
            case GL_SAMPLER_BUFFER:
            case GL_INT_SAMPLER_1D:
            case GL_INT_SAMPLER_2D:
            case GL_INT_SAMPLER_3D:
            case GL_INT_SAMPLER_BUFFER:
            case GL_UNSIGNED_INT_SAMPLER_1D:
            case GL_UNSIGNED_INT_SAMPLER_2D:
            case GL_UNSIGNED_INT_SAMPLER_3D:
            case GL_UNSIGNED_INT_SAMPLER_BUFFER: {
                GLint v; glGetUniformiv(srcProg, srcLoc, &v);
                glProgramUniform1i(dstProg, dstLoc, v);
                break;
            }
            default:
                break;
            }
        }
    }
}


void ViewportWidget::drawSectionCapping()
{
	// We use a lightweight shader without lighting and stuff for drawing the clipped mesh
	_renderCtrl.clippedMeshShader()->bind();
	_renderCtrl.clippedMeshShader()->setUniformValue("modelMatrix", _viewCtrl.modelMatrix());
	_renderCtrl.clippedMeshShader()->setUniformValue("viewMatrix", _viewCtrl.viewMatrix());
	_renderCtrl.clippedMeshShader()->setUniformValue("projectionMatrix", _viewCtrl.projectionMatrix());
	QVector3D pos = _primaryCamera->getRenderPosition();

	_renderCtrl.clippedMeshShader()->setUniformValue("clipPlaneX", QVector4D(_viewCtrl.modelViewMatrix().map(QVector3D(_renderCtrl.clippingXFlipped() ? 1 : -1, 0, 0) + pos),
		(_renderCtrl.clippingXFlipped() ? 1 : -1) * (pos.x() - (_renderCtrl.clippingXCoeff() + _viewCtrl.boundingBox().center().getX()))));
	_renderCtrl.clippedMeshShader()->setUniformValue("clipPlaneY", QVector4D(_viewCtrl.modelViewMatrix().map(QVector3D(0, _renderCtrl.clippingYFlipped() ? 1 : -1, 0) + pos),
		(_renderCtrl.clippingYFlipped() ? 1 : -1) * (pos.y() - (_renderCtrl.clippingYCoeff() + _viewCtrl.boundingBox().center().getY()))));
	_renderCtrl.clippedMeshShader()->setUniformValue("clipPlaneZ", QVector4D(_viewCtrl.modelViewMatrix().map(QVector3D(0, 0, _renderCtrl.clippingZFlipped() ? 1 : -1) + pos),
		(_renderCtrl.clippingZFlipped() ? 1 : -1) * (pos.z() - (_renderCtrl.clippingZCoeff() + _viewCtrl.boundingBox().center().getZ()))));
	_renderCtrl.clippedMeshShader()->setUniformValue("clipPlane", QVector4D(_viewCtrl.modelViewMatrix().map(QVector3D(_renderCtrl.clipDX(), _renderCtrl.clipDY(), _renderCtrl.clipDZ()) + pos),
		pos.x() * _renderCtrl.clipDX() + pos.y() * _renderCtrl.clipDY() + pos.z() * _renderCtrl.clipDZ()));

	for (int i = 0; i < 3; ++i)
	{
		// Clipping Planes
		if (_renderCtrl.yzClippingEnabled() && i == 0)
			glEnable(GL_CLIP_DISTANCE0);
		if (_renderCtrl.zxClippingEnabled() && i == 1)
			glEnable(GL_CLIP_DISTANCE1);
		if (_renderCtrl.xyClippingEnabled() && i == 2)
			glEnable(GL_CLIP_DISTANCE2);

		// https://www.opengl.org/archives/resources/code/samples/advanced/advanced97/notes/node10.html
		// https://glbook.gamedev.net/GLBOOK/glbook.gamedev.net/moglgp/advclip.html
		// https://stackoverflow.com/questions/16901829/how-to-clip-only-intersection-not-union-of-clipping-planes
		// 1) The stencil buffer, color buffer, and depth buffer are cleared,
		glClear(GL_STENCIL_BUFFER_BIT);
		glStencilMask(0x0);
		glDisable(GL_DEPTH_TEST);
		// and color buffer writes are disabled.
		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

		glEnable(GL_STENCIL_TEST);
		glStencilMask(0xFF);
		glStencilFunc(GL_ALWAYS, 0, 0);

		// 2) The capping polygon is rendered into the depth buffer,
		// drawCappingPlane

		// then depth buffer writes are disabled.
		glDepthMask(GL_FALSE);

		// 3) The stencil operation is set to increment the stencil value where the depth test passes,
		glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);

		// and the model is drawn with glCullFace(GL FRONT).
		glEnable(GL_CULL_FACE);
		glCullFace(GL_FRONT);
		drawMesh(_renderCtrl.clippedMeshShader(), i);

		// 4) The stencil operation is then set to decrement the stencil value where the depth test passes,
		glStencilOp(GL_KEEP, GL_KEEP, GL_DECR);

		// and the model is drawn with glCullFace(GL BACK)
		glCullFace(GL_BACK);
		drawMesh(_renderCtrl.clippedMeshShader(), i);
		glDisable(GL_CULL_FACE);

		//At this point, the stencil buffer is 1 wherever the clipping plane is enclosed by
		// the frontfacing and backfacing surfaces of the object.
		// 5) The depth buffer is cleared, color buffer writes are enabled,
		//glClear(GL_DEPTH_BUFFER_BIT);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glEnable(GL_DEPTH_TEST);

		// and the polygon representing the clipping plane is now drawn using whatever material properties are desired,
		// with the stencil function set to GL EQUAL and the reference value set to 1.
		// This draws the color and depth values of the cap into the framebuffer only where the stencil values equal 1.
		glStencilFunc(GL_EQUAL, 1, 0xFF);		
		glDepthMask(GL_TRUE);
		glEnable(GL_DEPTH_TEST);
		glDisable(GL_CLIP_DISTANCE0);
		glDisable(GL_CLIP_DISTANCE1);
		glDisable(GL_CLIP_DISTANCE2);
		// drawCappingPlane
		{
			QMatrix4x4 model;
			Point P = _viewCtrl.boundingBox().center();

			_renderCtrl.clippingPlaneShader()->bind();
			_renderCtrl.clippingPlaneShader()->setProperty("globalModelMatrix", QVariant::fromValue(QMatrix4x4()));
			_renderCtrl.clippingPlaneShader()->setProperty("viewMatrix", QVariant::fromValue(_viewCtrl.viewMatrix()));
			_renderCtrl.clippingPlaneShader()->setUniformValue("viewMatrix", _viewCtrl.viewMatrix());
			_renderCtrl.clippingPlaneShader()->setUniformValue("projectionMatrix", _viewCtrl.projectionMatrix());
			glActiveTexture(GL_TEXTURE6);
			glBindTexture(GL_TEXTURE_2D, _renderCtrl.cappingTexture());
			_renderCtrl.clippingPlaneShader()->setUniformValue("hatchMap", 6);
			float yAng = _renderCtrl.clippingXFlipped() || _renderCtrl.clippingXCoeff() > 0 ? 90.0f : -90.0f;
			float xAng = _renderCtrl.clippingYFlipped() || _renderCtrl.clippingYCoeff() > 0 ? 90.0f : -90.0f;
			float zAng = _renderCtrl.clippingZFlipped() || _renderCtrl.clippingZCoeff() > 0 ? 0.0f : 180.0f;

			bool wantTexture = _renderCtrl.hatchMode() == ClippingPlaneHatchMode::TEXTURE/* read from UI or stored flag */;
			bool wantFlipU = false/* read from UI or stored flag */;
			bool wantFlipV = false/* read from UI or stored flag */;

			// Pick a consistent density: e.g., ~3 tiles across the model diagonal
			const float sceneDiag = _viewCtrl.boundingBox().boundingRadius() * 2.0f;
			const float tilesAcross = wantTexture ? 3.0f : _renderCtrl.hatchTiling();
			const float worldUnitsPerTile = sceneDiag / tilesAcross;

			_renderCtrl.clippingPlaneShader()->setUniformValue("worldUnitsPerTile", worldUnitsPerTile);
			// procedural hatch params (tweak to taste)
			_renderCtrl.clippingPlaneShader()->setUniformValue("hatchThickness", _renderCtrl.hatchThickness());
			_renderCtrl.clippingPlaneShader()->setUniformValue("hatchIntensity", _renderCtrl.hatchIntensity());
			_renderCtrl.clippingPlaneShader()->setUniformValue("hatchLayers", _renderCtrl.hatchLayers());
			_renderCtrl.clippingPlaneShader()->setUniformValue("hatchLineColor", _renderCtrl.hatchLineColor());
			_renderCtrl.clippingPlaneShader()->setUniformValue("hatchPattern", static_cast<int>(_renderCtrl.hatchPattern()));
			
			_renderCtrl.clippingPlaneShader()->setUniformValue("useTexture", wantTexture);

			// texture flip control: (1,1) normal; (-1,1) flip U; (1,-1) flip V			
			QVector2D texFlip = QVector2D(wantFlipU ? -1.0f : 1.0f, wantFlipV ? -1.0f : 1.0f);
			_renderCtrl.clippingPlaneShader()->setUniformValue("textureFlip", texFlip);

			// YZ Plane			
			model.translate(QVector3D(P.getX(), P.getY(), P.getZ()));
			model.rotate(yAng, QVector3D(0.0f, 1.0f, 0.0f));
			_renderCtrl.clippingPlaneShader()->bind();
			_clippingPlaneYZ->setSceneRenderTransformFast(model);
			_renderCtrl.clippingPlaneShader()->setUniformValue("planeColor", QVector3D(0.20f, 0.5f, 0.5f));			
			if (_renderCtrl.yzClippingEnabled() && i == 0)
			{
				const float xPlane = P.getX() + _renderCtrl.clippingXCoeff();
				// Origin at plane through bbox center
				_renderCtrl.clippingPlaneShader()->setUniformValue("hatchOrigin", QVector3D(xPlane, P.getY(), P.getZ()));
				// World-space basis on the plane: U=+Y, V=+Z
				_renderCtrl.clippingPlaneShader()->setUniformValue("uDir", QVector3D(0.f, 1.f, 0.f));
				_renderCtrl.clippingPlaneShader()->setUniformValue("vDir", QVector3D(0.f, 0.f, 1.f));
				_clippingPlaneYZ->render();
			}

			// ZX Plane
			model.setToIdentity();
			model.translate(QVector3D(P.getX(), P.getY(), P.getZ()));
			model.rotate(xAng, QVector3D(1.0f, 0.0f, 0.0f));
			_renderCtrl.clippingPlaneShader()->bind();
			_clippingPlaneZX->setSceneRenderTransformFast(model);
			_renderCtrl.clippingPlaneShader()->setUniformValue("planeColor", QVector3D(0.5f, 0.20f, 0.5f));
			if (_renderCtrl.zxClippingEnabled() && i == 1)
			{
				const float yPlane = P.getY() + _renderCtrl.clippingYCoeff();
				_renderCtrl.clippingPlaneShader()->setUniformValue("hatchOrigin", QVector3D(P.getX(), yPlane, P.getZ()));
				// U=+Z, V=+X
				_renderCtrl.clippingPlaneShader()->setUniformValue("uDir", QVector3D(0.f, 0.f, 1.f));
				_renderCtrl.clippingPlaneShader()->setUniformValue("vDir", QVector3D(1.f, 0.f, 0.f));
				_clippingPlaneZX->render();
			}

			// XY Plane
			model.setToIdentity();
			model.translate(QVector3D(P.getX(), P.getY(), P.getZ()));
			model.rotate(zAng, QVector3D(1.0f, 0.0f, 0.0f));
			_renderCtrl.clippingPlaneShader()->bind();
			_clippingPlaneXY->setSceneRenderTransformFast(model);
			_renderCtrl.clippingPlaneShader()->setUniformValue("planeColor", QVector3D(0.5f, 0.5f, 0.20f));
			if (_renderCtrl.xyClippingEnabled() && i == 2)
			{
				const float zPlane = P.getZ() + _renderCtrl.clippingZCoeff();
				_renderCtrl.clippingPlaneShader()->setUniformValue("hatchOrigin", QVector3D(P.getX(), P.getY(), zPlane));
				// U=+X, V=+Y
				_renderCtrl.clippingPlaneShader()->setUniformValue("uDir", QVector3D(1.f, 0.f, 0.f));
				_renderCtrl.clippingPlaneShader()->setUniformValue("vDir", QVector3D(0.f, 1.f, 0.f));
				_clippingPlaneXY->render();
			}
		}

		// Clipping Planes
		if (_renderCtrl.yzClippingEnabled() && i == 0)
			glDisable(GL_CLIP_DISTANCE0);
		if (_renderCtrl.zxClippingEnabled() && i == 1)
			glDisable(GL_CLIP_DISTANCE1);
		if (_renderCtrl.xyClippingEnabled() && i == 2)
			glDisable(GL_CLIP_DISTANCE2);
	}

	// 6) Finally, stenciling is disabled, the OpenGL clipping plane is applied, and the
	// clipped object is drawn with color and depth enabled.
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_CULL_FACE);	
}

void ViewportWidget::drawVertexNormals()
{
    if (!isVertexNormalsShown())
        return;

    RenderableMesh::setCurrentRenderContext(_viewCtrl.modelMatrix(), _viewCtrl.viewMatrix());

	QVector3D pos = _primaryCamera->getRenderPosition();
	setupClippingUniforms(_renderCtrl.vertexNormalShader(), pos);
    const float normalMagnitude =
        std::max(std::max(_viewCtrl.boundingSphere().getRadius() * 0.02f, _viewCtrl.viewRange() * 0.01f), 0.001f);
    _renderCtrl.vertexNormalShader()->setUniformValue("normalMagnitude", normalMagnitude);

	if (_sceneRuntime.meshStore().size() != 0)
	{
		for (int i : _sceneRuntime.currentVisibleObjectIds())
		{
			SceneMesh* mesh = _sceneRuntime.meshAt(i);
			mesh->setProg(_renderCtrl.vertexNormalShader());
            mesh->render();
		}
	}
}

void ViewportWidget::drawFaceNormals()
{
    if (!isFaceNormalsShown())
        return;

    RenderableMesh::setCurrentRenderContext(_viewCtrl.modelMatrix(), _viewCtrl.viewMatrix());

	QVector3D pos = _primaryCamera->getRenderPosition();
	setupClippingUniforms(_renderCtrl.faceNormalShader(), pos);
    const float normalMagnitude =
        std::max(std::max(_viewCtrl.boundingSphere().getRadius() * 0.02f, _viewCtrl.viewRange() * 0.01f), 0.001f);
    _renderCtrl.faceNormalShader()->setUniformValue("normalMagnitude", normalMagnitude);

	if (_sceneRuntime.meshStore().size() != 0)
	{
		for (int i : _sceneRuntime.currentVisibleObjectIds())
		{
			SceneMesh* mesh = _sceneRuntime.meshAt(i);
			mesh->setProg(_renderCtrl.faceNormalShader());
            mesh->render();
		}
	}
}

void ViewportWidget::drawBoundingBoxOverlay()
{
    if (!isBoundingBoxShown() || !_renderCtrl.axisShader())
        return;

    BoundingBox bounds;
    bool hasBounds = false;

    auto accumulateBounds = [this, &bounds, &hasBounds](int meshId) {
        if (meshId < 0 || meshId >= static_cast<int>(_sceneRuntime.meshStore().size()))
            return;

        SceneMesh* mesh = _sceneRuntime.meshAt(meshId);
        if (!mesh)
            return;

        if (!hasBounds)
        {
            bounds = mesh->getBoundingBox();
            hasBounds = true;
        }
        else
        {
            bounds.addBox(mesh->getBoundingBox());
        }
    };

    const QList<int> selectedIds = _selectionManager ? _selectionManager->getSelectedIds() : QList<int>{};
    if (!selectedIds.isEmpty())
    {
        for (int meshId : selectedIds)
            accumulateBounds(meshId);
    }
    else
    {
        for (int meshId : _sceneRuntime.currentVisibleObjectIds())
            accumulateBounds(meshId);
    }

    if (!hasBounds)
        return;

    const std::vector<QVector3D> corners = bounds.getCorners();
    static const int edgeIndices[][2] = {
        {0, 1}, {0, 2}, {0, 4}, {1, 3}, {1, 5}, {2, 3},
        {2, 6}, {3, 7}, {4, 5}, {4, 6}, {5, 7}, {6, 7}
    };

    std::vector<float> vertices;
    vertices.reserve(24 * 6);
    const QVector3D color(0.98f, 0.78f, 0.14f);

    for (const auto& edge : edgeIndices)
    {
        const QVector3D& a = corners[edge[0]];
        const QVector3D& b = corners[edge[1]];

        vertices.insert(vertices.end(), { a.x(), a.y(), a.z(), color.x(), color.y(), color.z() });
        vertices.insert(vertices.end(), { b.x(), b.y(), b.z(), color.x(), color.y(), color.z() });
    }

    _renderCtrl.initDebugOverlayGeometry(vertices);
    glBindVertexArray(_renderCtrl.debugOverlayBoxVAO());
    glBindBuffer(GL_ARRAY_BUFFER, _renderCtrl.debugOverlayBoxVBO());
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                 vertices.data(),
                 GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void*>(3 * sizeof(float)));

    _renderCtrl.axisShader()->bind();
    _renderCtrl.axisShader()->setUniformValue("modelViewMatrix", _viewCtrl.viewMatrix());
    _renderCtrl.axisShader()->setUniformValue("projectionMatrix", _viewCtrl.projectionMatrix());
    _renderCtrl.axisShader()->setUniformValue("renderCone", false);
    glLineWidth(2.0f);
    glDrawArrays(GL_LINES, 0, 24);
    glLineWidth(1.0f);
    _renderCtrl.axisShader()->release();

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void ViewportWidget::drawDebugOverlay(Camera* camera)
{
    if (!camera || !_renderCtrl.debugOverlayEnabled())
        return;

    switch (_renderCtrl.debugOverlayMode())
    {
    case DebugOverlayMode::BoundingBox:
        drawBoundingBoxOverlay();
        break;
    case DebugOverlayMode::VertexNormals:
        drawVertexNormals();
        break;
    case DebugOverlayMode::FaceNormals:
        drawFaceNormals();
        break;
    }
}

void ViewportWidget::drawAxis(Camera* camera, const QMatrix4x4* overrideViewMatrix)
{
	if (!camera)
		return;

	// overrideViewMatrix lets a caller draw this against the camera pose an
	// interactive PT frame was actually rendered with (see paintGL()'s post-
	// overlay call site), rather than the always-current _viewCtrl view/
	// model-view matrices - same reasoning as drawSkyBox()'s identical
	// parameter: those matrices update every frame regardless of PT's own
	// accumulation/publish lag, so during an active drag the axis would
	// otherwise visibly move ahead of the still-catching-up PT model instead
	// of staying visually locked to it.
	const QMatrix4x4& viewMat = overrideViewMatrix ? *overrideViewMatrix : _viewCtrl.viewMatrix();
	const QMatrix4x4& modelViewMat = overrideViewMatrix ? *overrideViewMatrix : _viewCtrl.modelViewMatrix();

	const float axisViewRange = std::max(camera->getViewRange(), 0.0001f);
	float size = 15;
	// Labels
	QVector3D xAxis(axisViewRange / size, 0, 0);
	xAxis = xAxis.project(modelViewMat, _viewCtrl.projectionMatrix(), QRect(0, 0, width(), height()));
	_axisTextRenderer->RenderText(_labelAxisX.toStdString(), xAxis.x(), height() - xAxis.y(), 1, QVector3D(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VBOTTOM);

	QVector3D yAxis(0, axisViewRange / size, 0);
	yAxis = yAxis.project(modelViewMat, _viewCtrl.projectionMatrix(), QRect(0, 0, width(), height()));
	_axisTextRenderer->RenderText(_labelAxisY.toStdString(), yAxis.x(), height() - yAxis.y(), 1, QVector3D(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VBOTTOM);

	QVector3D zAxis(0, 0, axisViewRange / size);
	zAxis = zAxis.project(modelViewMat, _viewCtrl.projectionMatrix(), QRect(0, 0, width(), height()));
	_axisTextRenderer->RenderText(_labelAxisZ.toStdString(), zAxis.x(), height() - zAxis.y(), 1, QVector3D(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VBOTTOM);

	// Axes Lines
	_renderCtrl.initAxisGeometry(axisViewRange / size);

	_renderCtrl.axisShader()->bind();

	_renderCtrl.axisVBO().bind();
	_renderCtrl.axisShader()->enableAttributeArray("vertexPosition");
	_renderCtrl.axisShader()->setAttributeBuffer("vertexPosition", GL_FLOAT, 0, 3);

	_renderCtrl.axisCBO().bind();
	_renderCtrl.axisShader()->enableAttributeArray("vertexColor");
	_renderCtrl.axisShader()->setAttributeBuffer("vertexColor", GL_FLOAT, 0, 3);

	_renderCtrl.axisShader()->setUniformValue("modelViewMatrix", modelViewMat);
	_renderCtrl.axisShader()->setUniformValue("projectionMatrix", _viewCtrl.projectionMatrix());

	_renderCtrl.axisShader()->setUniformValue("renderCone", false);

	_renderCtrl.axisVAO().bind();
	glLineWidth(2.5);
	glDrawArrays(GL_LINES, 0, 6);
	glLineWidth(1);

	// Axes Cones
	// X Axis
	_axisCone->setParameters(axisViewRange / size / 15, axisViewRange / size / 5, 8u, 1u);
	_renderCtrl.axisShader()->setUniformValue("renderCone", true);
	QMatrix4x4 model;
	model.translate(axisViewRange / size, 0, 0);
	model.rotate(90, QVector3D(0, 1.0f, 0));
	_renderCtrl.axisShader()->setUniformValue("coneColor", QVector3D(1.0f, 0.0, 0.0));
	_renderCtrl.axisShader()->setUniformValue("modelViewMatrix", viewMat * model);
	_axisCone->getVAO().bind();
	glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(_axisCone->getPoints().size()), GL_UNSIGNED_INT, 0);
	_axisCone->getVAO().release();

	// Y Axis
	model.setToIdentity();
	model.translate(0, axisViewRange / size, 0);
	model.rotate(90, QVector3D(-1.0f, 0, 0));
	_renderCtrl.axisShader()->bind();
	_renderCtrl.axisShader()->setUniformValue("coneColor", QVector3D(0.0, 1.0f, 0.0));
	_renderCtrl.axisShader()->setUniformValue("modelViewMatrix", viewMat * model);
	_axisCone->getVAO().bind();
	glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(_axisCone->getPoints().size()), GL_UNSIGNED_INT, 0);
	_axisCone->getVAO().release();

	// Z Axis
	model.setToIdentity();
	model.translate(0, 0, axisViewRange / size);
	_renderCtrl.axisShader()->bind();
	_renderCtrl.axisShader()->setUniformValue("coneColor", QVector3D(0.0, 0.0, 1.0f));
	_renderCtrl.axisShader()->setUniformValue("modelViewMatrix", viewMat * model);
	_axisCone->getVAO().bind();
	glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(_axisCone->getPoints().size()), GL_UNSIGNED_INT, 0);
	_axisCone->getVAO().release();

	_renderCtrl.axisVAO().release();
	_renderCtrl.axisShader()->release();
}

void ViewportWidget::drawTransformGizmo(Camera* camera)
{
	if (!_transformGizmo || !_viewCtrl.transformGizmoRequested())
		return;

	syncTransformGizmoToSelection();
	if (!_transformGizmo->isVisible())
		return;

	_transformGizmo->render(_renderCtrl.axisShader(), _axisCone, camera, _viewCtrl.viewMatrix(), _viewCtrl.projectionMatrix(), kTransformGizmoMinWorldScale);
}

BoundingSphere ViewportWidget::computeTransformGizmoSelectionSphere() const
{
	if (!_viewer)
		return BoundingSphere();

	const std::vector<int> selectedIds = activeTransformGizmoSelectionIds();
	BoundingSphere combinedSphere;
	QVector<QVector3D> centers;
	QVector<float> radii;

	for (int id : selectedIds)
	{
		if (id < 0 || id >= static_cast<int>(_sceneRuntime.meshStore().size()))
			continue;

		const SceneMesh* mesh = _sceneRuntime.meshAt(id);
		if (!mesh)
			continue;

		centers.push_back(mesh->getStableTransformCenter());
		radii.push_back(mesh->getStableTransformRadius());
	}

	if (centers.isEmpty())
		return BoundingSphere();

	QVector3D cog(0.0f, 0.0f, 0.0f);
	for (const QVector3D& center : centers)
		cog += center;
	cog /= static_cast<float>(centers.size());

	float combinedRadius = 0.0f;
	for (int i = 0; i < centers.size(); ++i)
	{
		combinedRadius = (std::max)(combinedRadius, (centers[i] - cog).length() + radii[i]);
	}

	combinedSphere.setCenter(cog);
	combinedSphere.setRadius(combinedRadius);
	return combinedSphere;
}

QVector3D ViewportWidget::computeTransformGizmoPivot() const
{
	return computeTransformGizmoSelectionSphere().getCenter();
}

void ViewportWidget::applyExplodedViewManualPlacementSessionTransform()
{
	if (_explodedViewCtrl.manualSessionStartStates().isEmpty())
		return;

	QMatrix4x4 rotationAroundPivot;
	rotationAroundPivot.setToIdentity();
	rotationAroundPivot.translate(_explodedViewCtrl.manualSessionStartPivot());
	rotationAroundPivot.rotate(_explodedViewCtrl.manualSessionRotationQuat());
	rotationAroundPivot.translate(-_explodedViewCtrl.manualSessionStartPivot());

	QMatrix4x4 translationMatrix;
	translationMatrix.setToIdentity();
	translationMatrix.translate(_explodedViewCtrl.manualSessionTranslationDelta());

	for (auto it = _explodedViewCtrl.manualSessionStartStates().cbegin();
	     it != _explodedViewCtrl.manualSessionStartStates().cend(); ++it)
	{
		SceneMesh* mesh = getMeshByUuid(it.key());
		if (!mesh)
			continue;

		const TransformState& startState = it.value();
		const QMatrix4x4 startMatrix = _explodedViewCtrl.manualSessionStartMatrices().value(
			it.key(), mesh->getExplodedViewTransformation());
		const QMatrix4x4 combinedMatrix = translationMatrix * rotationAroundPivot * startMatrix;
		const QVector3D exactTranslation(
			combinedMatrix(0, 3),
			combinedMatrix(1, 3),
			combinedMatrix(2, 3));

		const QQuaternion baseQuat = startState.hasExactRotation
			? startState.rotationQuat.normalized()
			: MeshMathUtils::quaternionFromMeshEuler(startState.rotation);
		const QQuaternion exactRotationQuat =
			(_explodedViewCtrl.manualSessionRotationQuat() * baseQuat).normalized();

		QMatrix4x4 displayRotationMatrix;
		displayRotationMatrix.setToIdentity();
		displayRotationMatrix.rotate(exactRotationQuat);
		const QVector3D displayRotation = MeshMathUtils::extractMeshRotationFromMatrix(displayRotationMatrix);

		mesh->setExplodedViewTranslationFast(exactTranslation);
		mesh->setExplodedViewRotationQuaternionFast(exactRotationQuat, displayRotation);
		mesh->setExplodedViewScalingFast(startState.scale);
	}

	_renderCtrl.setShadowMapNeedsInitialization(true);
	update();
}

std::vector<int> ViewportWidget::activeTransformGizmoSelectionIds() const
{
	if (_explodedViewCtrl.isManualPlacementActive())
	{
		std::vector<int> lockedIds;
		lockedIds.reserve(_explodedViewCtrl.manualPlacementSessionUuids().size());
		for (const QUuid& uuid : _explodedViewCtrl.manualPlacementSessionUuids())
		{
			const int id = getIndexByUuid(uuid);
			if (id >= 0)
				lockedIds.push_back(id);
		}
		return lockedIds;
	}

	return _viewer ? _viewer->getSelectedIDs() : std::vector<int>();
}

void ViewportWidget::syncTransformGizmoToSelection()
{
	if (!_transformGizmo)
		return;

	if (!_viewCtrl.transformGizmoRequested() || !_viewer)
	{
		if (_viewCtrl.transformGizmoTranslating())
			finishTransformGizmoTranslationDrag(false);
		if (_viewCtrl.transformGizmoScaling())
			finishTransformGizmoScaleDrag(false);
		if (_viewCtrl.transformGizmoRotating())
			finishTransformGizmoRotationDrag(false);
		_transformGizmo->setVisible(false);
		return;
	}

	const std::vector<int> selectedIds = activeTransformGizmoSelectionIds();
	if (selectedIds.empty())
	{
		if (_viewCtrl.transformGizmoTranslating())
			finishTransformGizmoTranslationDrag(false);
		if (_viewCtrl.transformGizmoScaling())
			finishTransformGizmoScaleDrag(false);
		if (_viewCtrl.transformGizmoRotating())
			finishTransformGizmoRotationDrag(false);
		_transformGizmo->setVisible(false);
		return;
	}

	if (_viewCtrl.transformGizmoRotating() || _viewCtrl.transformGizmoScaling())
		_transformGizmo->setPivot(_viewCtrl.transformGizmoStartPivot());
	else
		_transformGizmo->setPivot(computeTransformGizmoPivot());
	_transformGizmo->setVisible(true);
}

bool ViewportWidget::beginTransformGizmoDrag(TransformGizmo::Handle handle, const QPoint& pixel)
{
	switch (handle)
	{
	case TransformGizmo::Handle::TranslateX:
	case TransformGizmo::Handle::TranslateY:
	case TransformGizmo::Handle::TranslateZ:
		return beginTransformGizmoTranslationDrag(handle, pixel);
	case TransformGizmo::Handle::UniformScale:
		return beginTransformGizmoScaleDrag(handle, pixel, true);
	case TransformGizmo::Handle::ScaleX:
	case TransformGizmo::Handle::ScaleY:
	case TransformGizmo::Handle::ScaleZ:
		return beginTransformGizmoScaleDrag(handle, pixel, false);
	case TransformGizmo::Handle::RotateXY:
	case TransformGizmo::Handle::RotateYZ:
	case TransformGizmo::Handle::RotateZX:
		return beginTransformGizmoRotationDrag(handle, pixel);
	default:
		return false;
	}
}

bool ViewportWidget::beginTransformGizmoTranslationDrag(TransformGizmo::Handle handle, const QPoint& pixel)
{
	if (!_viewCtrl.transformGizmoRequested() || !_transformGizmo || !_viewer)
		return false;

	QVector3D axis;
	switch (handle)
	{
	case TransformGizmo::Handle::TranslateX:
		axis = QVector3D(1.0f, 0.0f, 0.0f);
		break;
	case TransformGizmo::Handle::TranslateY:
		axis = QVector3D(0.0f, 1.0f, 0.0f);
		break;
	case TransformGizmo::Handle::TranslateZ:
		axis = QVector3D(0.0f, 0.0f, 1.0f);
		break;
	default:
		return false;
	}

	_viewCtrl.setTransformGizmoMode(true, false, false, false);
	_viewCtrl.setTransformGizmoDragStartPixel(pixel);
	_viewCtrl.setTransformGizmoDragAxis(axis);
	const BoundingSphere selectionSphere = computeTransformGizmoSelectionSphere();
	_viewCtrl.setTransformGizmoStartPivot(selectionSphere.getCenter());
	const float selectionRadius = selectionSphere.getRadius() > 0.0f
		? selectionSphere.getRadius()
		: _viewCtrl.boundingSphere().getRadius();
	_viewCtrl.setTransformGizmoDragScale((std::max)(selectionRadius * 0.9f, 0.01f));
	_viewCtrl.resetTransformGizmoDragSession();
	if (_explodedViewCtrl.isManualPlacementActive())
		_explodedViewCtrl.setManualDragStartTranslationDelta(_explodedViewCtrl.manualSessionTranslationDelta());

	for (int id : activeTransformGizmoSelectionIds())
	{
		if (id < 0 || id >= static_cast<int>(_sceneRuntime.meshStore().size()))
			continue;

		if (SceneMesh* mesh = _sceneRuntime.meshAt(id))
		{
			_viewCtrl.transformGizmoStartStates()[id] = _explodedViewCtrl.isManualPlacementActive()
				? ExplodedViewRuntimeController::explodedViewTransformState(mesh)
				: TransformState(
					mesh->getTranslation(),
					mesh->getRotation(),
					mesh->getScaling(),
					mesh->getRotationQuaternion());
			_viewCtrl.transformGizmoStartCenters()[id] = mesh->getBoundingSphere().getCenter();
			_viewCtrl.transformGizmoStartMatrices()[id] = _explodedViewCtrl.isManualPlacementActive()
				? ExplodedViewRuntimeController::explodedViewTransformMatrix(mesh)
				: mesh->getTransformation();
		}
	}

	if (_viewer->tabWidgetVizAttribs->currentIndex() == 1)
	{
		_viewer->objectTransformPanel->setTranslationValues(QVector3D(0.0f, 0.0f, 0.0f));
	}

	return !_viewCtrl.transformGizmoStartStates().isEmpty();
}

void ViewportWidget::updateTransformGizmoTranslationDrag(const QPoint& pixel)
{
	if (!_viewCtrl.transformGizmoTranslating() || !_viewer || _viewCtrl.transformGizmoStartStates().isEmpty())
		return;

	const QRect viewport = PickingHelper::viewportRectForPoint(pixel, width(), height(), _viewCtrl.multiViewActive());
	const Camera* camera = getCameraForPoint(pixel);
	if (!camera)
		return;

	const QMatrix4x4 viewMatrix = camera->getViewMatrix();
	const QMatrix4x4 projectionMatrix = camera->getProjectionMatrix();
	const QVector3D pivotScreen3 = _viewCtrl.transformGizmoStartPivot().project(viewMatrix, projectionMatrix, viewport);
	const QVector3D axisEndWorld = _viewCtrl.transformGizmoStartPivot() +
		(_viewCtrl.transformGizmoDragAxis() * _viewCtrl.transformGizmoDragScale());
	const QVector3D axisEndScreen3 = axisEndWorld.project(viewMatrix, projectionMatrix, viewport);

	const QVector2D pivotScreen(pivotScreen3.x(), pivotScreen3.y());
	const QVector2D axisScreen = QVector2D(axisEndScreen3.x(), axisEndScreen3.y()) - pivotScreen;
	const float axisScreenLength = axisScreen.length();
	if (axisScreenLength <= 1.0e-4f)
		return;

	const QVector2D axisScreenDir = axisScreen / axisScreenLength;
	const QVector2D mouseDelta = QVector2D(pixel.x() - _viewCtrl.transformGizmoDragStartPixel().x(),
		_viewCtrl.transformGizmoDragStartPixel().y() - pixel.y());
	const float projectedPixels = QVector2D::dotProduct(mouseDelta, axisScreenDir);
	const float worldDistance = (projectedPixels / axisScreenLength) * _viewCtrl.transformGizmoDragScale();
	_viewCtrl.setTransformGizmoCurrentTranslationDelta(_viewCtrl.transformGizmoDragAxis() * worldDistance);

	for (auto it = _viewCtrl.transformGizmoStartStates().begin(); it != _viewCtrl.transformGizmoStartStates().end(); ++it)
	{
		const int id = it.key();
		if (id < 0 || id >= static_cast<int>(_sceneRuntime.meshStore().size()))
			continue;

		if (SceneMesh* mesh = _sceneRuntime.meshAt(id))
		{
			const TransformState& startState = it.value();
			// Use fast setters during drag — only updates AABB from 8 corners (O(1))
			// instead of re-transforming all vertices (O(N)) for each mouse event.
			if (_explodedViewCtrl.isManualPlacementActive())
			{
				mesh->setExplodedViewTranslationFast(startState.translation + _viewCtrl.transformGizmoCurrentTranslationDelta());
				if (startState.hasExactRotation)
					mesh->setExplodedViewRotationQuaternionFast(startState.rotationQuat, startState.rotation);
				else
					mesh->setExplodedViewRotationFast(startState.rotation);
				mesh->setExplodedViewScalingFast(startState.scale);
			}
			else
			{
				mesh->setTranslationFast(startState.translation + _viewCtrl.transformGizmoCurrentTranslationDelta());
				mesh->setRotationFast(startState.rotation);
				mesh->setScalingFast(startState.scale);
			}
		}
	}

	if (_viewer->tabWidgetVizAttribs->currentIndex() == 1)
	{
		_viewer->objectTransformPanel->setTranslationValues(_viewCtrl.transformGizmoCurrentTranslationDelta());
	}
	if (_explodedViewCtrl.isManualPlacementActive())
	{
		_explodedViewCtrl.setManualSessionTranslationDelta(
			_explodedViewCtrl.manualDragStartTranslationDelta() + _viewCtrl.transformGizmoCurrentTranslationDelta());
		emit explodedViewManualPlacementChanged();
	}

	_renderCtrl.setShadowMapNeedsInitialization(true);
	update();
}

void ViewportWidget::finishTransformGizmoTranslationDrag(bool commit)
{
	if (!_viewCtrl.transformGizmoTranslating())
		return;

	_viewCtrl.setTransformGizmoTranslating(false);

	if (!_viewer || _viewCtrl.transformGizmoStartStates().isEmpty())
		return;

	QMap<QUuid, TransformState> oldStatesByUuid;
	QMap<QUuid, TransformState> newStatesByUuid;

	for (auto it = _viewCtrl.transformGizmoStartStates().begin(); it != _viewCtrl.transformGizmoStartStates().end(); ++it)
	{
		const int id = it.key();
		if (id < 0 || id >= static_cast<int>(_sceneRuntime.meshStore().size()))
			continue;

		SceneMesh* mesh = _sceneRuntime.meshAt(id);
		if (!mesh)
			continue;

		const QUuid uuid = getUuidByIndex(id);
		if (uuid.isNull())
			continue;

		oldStatesByUuid.insert(uuid, it.value());
		newStatesByUuid.insert(uuid, _explodedViewCtrl.isManualPlacementActive()
			? ExplodedViewRuntimeController::explodedViewTransformState(mesh)
			: TransformState(
				mesh->getTranslation(),
				mesh->getRotation(),
				mesh->getScaling(),
				mesh->getRotationQuaternion()));
	}

	const bool moved = _viewCtrl.transformGizmoCurrentTranslationDelta().lengthSquared() > 1.0e-8f;

	if (_explodedViewCtrl.isManualPlacementActive())
	{
		if (commit && moved && !oldStatesByUuid.isEmpty())
		{
			_viewer->getUndoStack()->push(new TransformCommand(
				_viewer,
				this,
				oldStatesByUuid,
				newStatesByUuid,
				tr("Translate Exploded Placement"),
				false,
				TransformCommand::Target::ExplodedViewTransform));
		}
		else if (!commit)
			_explodedViewCtrl.setManualSessionTranslationDelta(_explodedViewCtrl.manualDragStartTranslationDelta());
		emit explodedViewManualPlacementChanged();
		update();
	}
	else if (commit && moved && !oldStatesByUuid.isEmpty())
	{
		_viewer->getUndoStack()->push(new TransformCommand(
			_viewer, this, oldStatesByUuid, newStatesByUuid, tr("Translate Selection"), false));
	}
	else
	{
		for (auto it = _viewCtrl.transformGizmoStartStates().begin(); it != _viewCtrl.transformGizmoStartStates().end(); ++it)
		{
			const int id = it.key();
			if (id < 0 || id >= static_cast<int>(_sceneRuntime.meshStore().size()))
				continue;

			if (SceneMesh* mesh = _sceneRuntime.meshAt(id))
			{
				const TransformState& startState = it.value();
				if (_explodedViewCtrl.isManualPlacementActive())
					ExplodedViewRuntimeController::applyExplodedViewTransformState(mesh, startState, false);
				else
				{
					mesh->setTranslation(startState.translation);
					if (startState.hasExactRotation)
						mesh->setRotationQuaternion(startState.rotationQuat, startState.rotation);
					else
						mesh->setRotation(startState.rotation);
					mesh->setScaling(startState.scale);
				}
			}
		}
		update();
	}

	_viewCtrl.resetTransformGizmoDragSession();
	_viewer->updateTransformationValues();
}

bool ViewportWidget::beginTransformGizmoScaleDrag(TransformGizmo::Handle handle, const QPoint& pixel, bool uniformScale)
{
	if (!_viewCtrl.transformGizmoRequested() || !_transformGizmo || !_viewer)
		return false;
	if (_explodedViewCtrl.isManualPlacementActive())
		return false;

	QVector3D axis;
	switch (handle)
	{
	case TransformGizmo::Handle::TranslateX:
	case TransformGizmo::Handle::ScaleX:
		axis = QVector3D(1.0f, 0.0f, 0.0f);
		break;
	case TransformGizmo::Handle::TranslateY:
	case TransformGizmo::Handle::ScaleY:
		axis = QVector3D(0.0f, 1.0f, 0.0f);
		break;
	case TransformGizmo::Handle::TranslateZ:
	case TransformGizmo::Handle::ScaleZ:
		axis = QVector3D(0.0f, 0.0f, 1.0f);
		break;
	case TransformGizmo::Handle::UniformScale:
		axis = QVector3D(1.0f, 1.0f, 1.0f).normalized();
		break;
	default:
		return false;
	}

	_viewCtrl.setTransformGizmoMode(false, true, uniformScale, false);
	_viewCtrl.setTransformGizmoDragStartPixel(pixel);
	_viewCtrl.setTransformGizmoDragAxis(axis);
	const BoundingSphere selectionSphere = computeTransformGizmoSelectionSphere();
	_viewCtrl.setTransformGizmoStartPivot(selectionSphere.getCenter());
	const float selectionRadius = selectionSphere.getRadius() > 0.0f
		? selectionSphere.getRadius()
		: _viewCtrl.boundingSphere().getRadius();
	_viewCtrl.setTransformGizmoDragScale((std::max)(selectionRadius * 0.9f, 0.01f));
	_viewCtrl.resetTransformGizmoDragSession();

	for (int id : activeTransformGizmoSelectionIds())
	{
		if (id < 0 || id >= static_cast<int>(_sceneRuntime.meshStore().size()))
			continue;

		if (SceneMesh* mesh = _sceneRuntime.meshAt(id))
		{
			_viewCtrl.transformGizmoStartStates()[id] = _explodedViewCtrl.isManualPlacementActive()
				? ExplodedViewRuntimeController::explodedViewTransformState(mesh)
				: TransformState(
					mesh->getTranslation(),
					mesh->getRotation(),
					mesh->getScaling(),
					mesh->getRotationQuaternion());
			_viewCtrl.transformGizmoStartCenters()[id] = mesh->getBoundingSphere().getCenter();
			_viewCtrl.transformGizmoStartMatrices()[id] = _explodedViewCtrl.isManualPlacementActive()
				? ExplodedViewRuntimeController::explodedViewTransformMatrix(mesh)
				: mesh->getTransformation();
		}
	}

	if (_viewer->tabWidgetVizAttribs->currentIndex() == 1)
	{
		_viewer->objectTransformPanel->setScaleValues(QVector3D(1.0f, 1.0f, 1.0f));
	}

	return !_viewCtrl.transformGizmoStartStates().isEmpty();
}

void ViewportWidget::updateTransformGizmoScaleDrag(const QPoint& pixel)
{
	if (!_viewCtrl.transformGizmoScaling() || !_viewer || _viewCtrl.transformGizmoStartStates().isEmpty())
		return;

	const QRect viewport = PickingHelper::viewportRectForPoint(pixel, width(), height(), _viewCtrl.multiViewActive());
	const Camera* camera = getCameraForPoint(pixel);
	if (!camera)
		return;

	const QMatrix4x4 viewMatrix = camera->getViewMatrix();
	const QMatrix4x4 projectionMatrix = camera->getProjectionMatrix();
	const QVector3D pivotScreen3 = _viewCtrl.transformGizmoStartPivot().project(viewMatrix, projectionMatrix, viewport);
	const QVector3D axisEndWorld = _viewCtrl.transformGizmoStartPivot() +
		(_viewCtrl.transformGizmoDragAxis() * _viewCtrl.transformGizmoDragScale());
	const QVector3D axisEndScreen3 = axisEndWorld.project(viewMatrix, projectionMatrix, viewport);

	const QVector2D pivotScreen(pivotScreen3.x(), pivotScreen3.y());
	const QVector2D axisScreen = QVector2D(axisEndScreen3.x(), axisEndScreen3.y()) - pivotScreen;
	const float axisScreenLength = axisScreen.length();
	if (axisScreenLength <= 1.0e-4f)
		return;

	const QVector2D axisScreenDir = axisScreen / axisScreenLength;
	const QVector2D mouseDelta = QVector2D(pixel.x() - _viewCtrl.transformGizmoDragStartPixel().x(),
		_viewCtrl.transformGizmoDragStartPixel().y() - pixel.y());
	const float projectedPixels = QVector2D::dotProduct(mouseDelta, axisScreenDir);
	const float uniformFactor = (std::max)(0.01f, 1.0f + (projectedPixels / axisScreenLength));

	if (_viewCtrl.transformGizmoUniformScaling())
	{
		_viewCtrl.setTransformGizmoCurrentScaleDelta(QVector3D(uniformFactor, uniformFactor, uniformFactor));
	}
	else if (_viewCtrl.transformGizmoDragAxis().x() > 0.5f)
	{
		_viewCtrl.setTransformGizmoCurrentScaleDelta(QVector3D(uniformFactor, 1.0f, 1.0f));
	}
	else if (_viewCtrl.transformGizmoDragAxis().y() > 0.5f)
	{
		_viewCtrl.setTransformGizmoCurrentScaleDelta(QVector3D(1.0f, uniformFactor, 1.0f));
	}
	else
	{
		_viewCtrl.setTransformGizmoCurrentScaleDelta(QVector3D(1.0f, 1.0f, uniformFactor));
	}

	for (auto it = _viewCtrl.transformGizmoStartStates().begin(); it != _viewCtrl.transformGizmoStartStates().end(); ++it)
	{
		const int id = it.key();
		if (id < 0 || id >= static_cast<int>(_sceneRuntime.meshStore().size()))
			continue;

		if (SceneMesh* mesh = _sceneRuntime.meshAt(id))
		{
			const TransformState& startState = it.value();
			QVector3D scaledTranslation = startState.translation;
			QVector3D scaledScale = startState.scale;

			if (_viewCtrl.transformGizmoUniformScaling())
			{
				scaledTranslation = _viewCtrl.transformGizmoStartPivot() +
					(startState.translation - _viewCtrl.transformGizmoStartPivot()) * uniformFactor;
				scaledScale = startState.scale * uniformFactor;
			}
			else if (_viewCtrl.transformGizmoDragAxis().x() > 0.5f)
			{
				scaledTranslation.setX(_viewCtrl.transformGizmoStartPivot().x() +
					(startState.translation.x() - _viewCtrl.transformGizmoStartPivot().x()) * uniformFactor);
				scaledScale.setX(startState.scale.x() * uniformFactor);
			}
			else if (_viewCtrl.transformGizmoDragAxis().y() > 0.5f)
			{
				scaledTranslation.setY(_viewCtrl.transformGizmoStartPivot().y() +
					(startState.translation.y() - _viewCtrl.transformGizmoStartPivot().y()) * uniformFactor);
				scaledScale.setY(startState.scale.y() * uniformFactor);
			}
			else
			{
				scaledTranslation.setZ(_viewCtrl.transformGizmoStartPivot().z() +
					(startState.translation.z() - _viewCtrl.transformGizmoStartPivot().z()) * uniformFactor);
				scaledScale.setZ(startState.scale.z() * uniformFactor);
			}

			mesh->setTranslationFast(scaledTranslation);
			if (startState.hasExactRotation)
				mesh->setRotationQuaternionFast(startState.rotationQuat, startState.rotation);
			else
				mesh->setRotationFast(startState.rotation);
			mesh->setScalingFast(scaledScale);
		}
	}

	if (_viewer->tabWidgetVizAttribs->currentIndex() == 1)
	{
		_viewer->objectTransformPanel->setScaleValues(_viewCtrl.transformGizmoCurrentScaleDelta());
	}

	_renderCtrl.setShadowMapNeedsInitialization(true);
	update();
}

void ViewportWidget::finishTransformGizmoScaleDrag(bool commit)
{
	if (!_viewCtrl.transformGizmoScaling())
		return;

	_viewCtrl.setTransformGizmoScaling(false);
	_viewCtrl.setTransformGizmoUniformScaling(false);

	if (!_viewer || _viewCtrl.transformGizmoStartStates().isEmpty())
		return;

	QMap<QUuid, TransformState> oldStatesByUuid;
	QMap<QUuid, TransformState> newStatesByUuid;

	for (auto it = _viewCtrl.transformGizmoStartStates().begin(); it != _viewCtrl.transformGizmoStartStates().end(); ++it)
	{
		const int id = it.key();
		if (id < 0 || id >= static_cast<int>(_sceneRuntime.meshStore().size()))
			continue;

		SceneMesh* mesh = _sceneRuntime.meshAt(id);
		if (!mesh)
			continue;

		const QUuid uuid = getUuidByIndex(id);
		if (uuid.isNull())
			continue;

		oldStatesByUuid.insert(uuid, it.value());
		newStatesByUuid.insert(uuid, TransformState(
			mesh->getTranslation(),
			mesh->getRotation(),
			mesh->getScaling(),
			mesh->getRotationQuaternion()));
	}

	const QVector3D scaleDelta = _viewCtrl.transformGizmoCurrentScaleDelta() - QVector3D(1.0f, 1.0f, 1.0f);
	const bool scaled = scaleDelta.lengthSquared() > 1.0e-8f;

	if (_explodedViewCtrl.isManualPlacementActive())
	{
		update();
	}
	else if (commit && scaled && !oldStatesByUuid.isEmpty())
	{
		_viewer->getUndoStack()->push(new TransformCommand(
			_viewer, this, oldStatesByUuid, newStatesByUuid, tr("Scale Selection"), false));
	}
	else
	{
		for (auto it = _viewCtrl.transformGizmoStartStates().begin(); it != _viewCtrl.transformGizmoStartStates().end(); ++it)
		{
			const int id = it.key();
			if (id < 0 || id >= static_cast<int>(_sceneRuntime.meshStore().size()))
				continue;

			if (SceneMesh* mesh = _sceneRuntime.meshAt(id))
			{
				const TransformState& startState = it.value();
				mesh->setTranslation(startState.translation);
				if (startState.hasExactRotation)
					mesh->setRotationQuaternion(startState.rotationQuat, startState.rotation);
				else
					mesh->setRotation(startState.rotation);
				mesh->setScaling(startState.scale);
			}
		}
		update();
	}

	_viewCtrl.resetTransformGizmoDragSession();
	_viewer->updateTransformationValues();
}

bool ViewportWidget::beginTransformGizmoRotationDrag(TransformGizmo::Handle handle, const QPoint& pixel)
{
	if (!_viewCtrl.transformGizmoRequested() || !_transformGizmo || !_viewer)
		return false;

	QVector3D axis;
	switch (handle)
	{
	case TransformGizmo::Handle::RotateXY:
		axis = QVector3D(0.0f, 0.0f, 1.0f);
		break;
	case TransformGizmo::Handle::RotateYZ:
		axis = QVector3D(1.0f, 0.0f, 0.0f);
		break;
	case TransformGizmo::Handle::RotateZX:
		axis = QVector3D(0.0f, 1.0f, 0.0f);
		break;
	default:
		return false;
	}

	const QRect viewport = PickingHelper::viewportRectForPoint(pixel, width(), height(), _viewCtrl.multiViewActive());
	const Camera* camera = getCameraForPoint(pixel);
	if (!camera)
		return false;

	QVector3D rayOrigin;
	QVector3D rayDir;
	if (!ViewportInteractionController::convertPixelToRay(pixel, viewport, height(), camera->getViewMatrix(), camera->getProjectionMatrix(), rayOrigin, rayDir))
		return false;

	const BoundingSphere selectionSphere = computeTransformGizmoSelectionSphere();
	const QVector3D pivot = selectionSphere.getCenter();
	QVector3D hitPoint;
	if (!ViewportInteractionController::intersectRayPlane(rayOrigin, rayDir, pivot, axis, hitPoint))
		return false;

	QVector3D startVector = hitPoint - pivot;
	if (startVector.lengthSquared() <= 1.0e-8f)
		return false;
	startVector.normalize();

	_viewCtrl.setTransformGizmoMode(false, false, false, true);
	_viewCtrl.setTransformGizmoDragStartPixel(pixel);
	_viewCtrl.setTransformGizmoRotationPlaneNormal(axis);
	_viewCtrl.setTransformGizmoRotationStartVector(startVector);
	_viewCtrl.setTransformGizmoStartPivot(pivot);
	_viewCtrl.resetTransformGizmoDragSession();
	if (_explodedViewCtrl.isManualPlacementActive())
	{
		_explodedViewCtrl.setManualDragStartRotationQuat(_explodedViewCtrl.manualSessionRotationQuat());
		_explodedViewCtrl.setManualDragStartRotationEuler(_explodedViewCtrl.manualSessionRotationEuler());
	}

	for (int id : activeTransformGizmoSelectionIds())
	{
		if (id < 0 || id >= static_cast<int>(_sceneRuntime.meshStore().size()))
			continue;

		if (SceneMesh* mesh = _sceneRuntime.meshAt(id))
		{
			_viewCtrl.transformGizmoStartStates()[id] = TransformState(
				mesh->getTranslation(),
				mesh->getRotation(),
				mesh->getScaling(),
				mesh->getRotationQuaternion());
			_viewCtrl.transformGizmoStartCenters()[id] = mesh->getBoundingSphere().getCenter();
			_viewCtrl.transformGizmoStartMatrices()[id] = mesh->getTransformation();
		}
	}

	if (_viewer->tabWidgetVizAttribs->currentIndex() == 1)
	{
		_viewer->objectTransformPanel->setRotationValues(QVector3D(0.0f, 0.0f, 0.0f));
	}

	return !_viewCtrl.transformGizmoStartStates().isEmpty();
}

void ViewportWidget::updateTransformGizmoRotationDrag(const QPoint& pixel)
{
	if (!_viewCtrl.transformGizmoRotating() || !_viewer || _viewCtrl.transformGizmoStartStates().isEmpty())
		return;

	const QRect viewport = PickingHelper::viewportRectForPoint(pixel, width(), height(), _viewCtrl.multiViewActive());
	const Camera* camera = getCameraForPoint(pixel);
	if (!camera)
		return;

	QVector3D rayOrigin;
	QVector3D rayDir;
	if (!ViewportInteractionController::convertPixelToRay(pixel, viewport, height(), camera->getViewMatrix(), camera->getProjectionMatrix(), rayOrigin, rayDir))
		return;

	QVector3D hitPoint;
	if (!ViewportInteractionController::intersectRayPlane(rayOrigin, rayDir, _viewCtrl.transformGizmoStartPivot(),
		_viewCtrl.transformGizmoRotationPlaneNormal(), hitPoint))
		return;

	QVector3D currentVector = hitPoint - _viewCtrl.transformGizmoStartPivot();
	if (currentVector.lengthSquared() <= 1.0e-8f)
		return;
	currentVector.normalize();

	const QVector3D crossVec = QVector3D::crossProduct(_viewCtrl.transformGizmoRotationStartVector(), currentVector);
	const float sinAngle = QVector3D::dotProduct(_viewCtrl.transformGizmoRotationPlaneNormal(), crossVec);
	const float cosAngle = QVector3D::dotProduct(_viewCtrl.transformGizmoRotationStartVector(), currentVector);
	const float angleDegrees = qRadiansToDegrees(std::atan2(sinAngle, cosAngle));

	if (_viewCtrl.transformGizmoRotationPlaneNormal().x() > 0.5f)
		_viewCtrl.setTransformGizmoCurrentRotationDelta(QVector3D(angleDegrees, 0.0f, 0.0f));
	else if (_viewCtrl.transformGizmoRotationPlaneNormal().y() > 0.5f)
		_viewCtrl.setTransformGizmoCurrentRotationDelta(QVector3D(0.0f, angleDegrees, 0.0f));
	else
		_viewCtrl.setTransformGizmoCurrentRotationDelta(QVector3D(0.0f, 0.0f, angleDegrees));

	for (auto it = _viewCtrl.transformGizmoStartStates().begin(); it != _viewCtrl.transformGizmoStartStates().end(); ++it)
	{
		const int id = it.key();
		if (id < 0 || id >= static_cast<int>(_sceneRuntime.meshStore().size()))
			continue;

		if (SceneMesh* mesh = _sceneRuntime.meshAt(id))
		{
			const TransformState& startState = it.value();
			QMatrix4x4 deltaMatrix;
			deltaMatrix.setToIdentity();
			deltaMatrix.translate(_viewCtrl.transformGizmoStartPivot());
			deltaMatrix.rotate(angleDegrees, _viewCtrl.transformGizmoRotationPlaneNormal());
			deltaMatrix.translate(-_viewCtrl.transformGizmoStartPivot());

			QMatrix4x4 rotationOnlyMatrix;
			rotationOnlyMatrix.setToIdentity();
			rotationOnlyMatrix.rotate(angleDegrees, _viewCtrl.transformGizmoRotationPlaneNormal());
			const QMatrix4x4 startMatrix = _viewCtrl.transformGizmoStartMatrices().value(
				id, _explodedViewCtrl.isManualPlacementActive() ? mesh->getExplodedViewTransformation() : mesh->getTransformation());
			const QMatrix4x4 combinedMatrix = deltaMatrix * startMatrix;
			const QVector3D exactTranslation(
				combinedMatrix(0, 3),
				combinedMatrix(1, 3),
				combinedMatrix(2, 3));

			const QQuaternion deltaQuat =
				QQuaternion::fromRotationMatrix(rotationOnlyMatrix.toGenericMatrix<3, 3>()).normalized();
			const QQuaternion exactRotationQuat = startState.hasExactRotation
				? (deltaQuat * startState.rotationQuat).normalized()
				: deltaQuat;
			QMatrix4x4 displayRotationMatrix;
			displayRotationMatrix.setToIdentity();
			displayRotationMatrix.rotate(exactRotationQuat);
			const QVector3D displayRotation = MeshMathUtils::extractMeshRotationFromMatrix(displayRotationMatrix);
			if (_explodedViewCtrl.isManualPlacementActive())
			{
				mesh->setExplodedViewTranslationFast(exactTranslation);
				mesh->setExplodedViewRotationQuaternionFast(exactRotationQuat, displayRotation);
				mesh->setExplodedViewScalingFast(startState.scale);
			}
			else
			{
				mesh->setTranslationFast(exactTranslation);
				mesh->setRotationQuaternionFast(exactRotationQuat, displayRotation);
				mesh->setScalingFast(startState.scale);
			}
		}
	}

	if (_viewer->tabWidgetVizAttribs->currentIndex() == 1)
	{
		_viewer->objectTransformPanel->setRotationValues(_viewCtrl.transformGizmoCurrentRotationDelta());
	}
	if (_explodedViewCtrl.isManualPlacementActive())
	{
		QMatrix4x4 rotationOnlyMatrix;
		rotationOnlyMatrix.setToIdentity();
		rotationOnlyMatrix.rotate(angleDegrees, _viewCtrl.transformGizmoRotationPlaneNormal());
		const QQuaternion deltaQuat =
			QQuaternion::fromRotationMatrix(rotationOnlyMatrix.toGenericMatrix<3, 3>()).normalized();
		_explodedViewCtrl.setManualSessionRotationQuat(
			(deltaQuat * _explodedViewCtrl.manualDragStartRotationQuat()).normalized());

		QMatrix4x4 sessionRotationMatrix;
		sessionRotationMatrix.setToIdentity();
		sessionRotationMatrix.rotate(_explodedViewCtrl.manualSessionRotationQuat());
		_explodedViewCtrl.setManualSessionRotationEuler(
			MeshMathUtils::extractMeshRotationFromMatrix(sessionRotationMatrix));
		emit explodedViewManualPlacementChanged();
	}

	_renderCtrl.setShadowMapNeedsInitialization(true);
	update();
}

void ViewportWidget::finishTransformGizmoRotationDrag(bool commit)
{
	if (!_viewCtrl.transformGizmoRotating())
		return;

	_viewCtrl.setTransformGizmoRotating(false);

	if (!_viewer || _viewCtrl.transformGizmoStartStates().isEmpty())
		return;

	QMap<QUuid, TransformState> oldStatesByUuid;
	QMap<QUuid, TransformState> newStatesByUuid;

	for (auto it = _viewCtrl.transformGizmoStartStates().begin(); it != _viewCtrl.transformGizmoStartStates().end(); ++it)
	{
		const int id = it.key();
		if (id < 0 || id >= static_cast<int>(_sceneRuntime.meshStore().size()))
			continue;

		SceneMesh* mesh = _sceneRuntime.meshAt(id);
		if (!mesh)
			continue;

		const QUuid uuid = getUuidByIndex(id);
		if (uuid.isNull())
			continue;

		oldStatesByUuid.insert(uuid, it.value());
		newStatesByUuid.insert(uuid, _explodedViewCtrl.isManualPlacementActive()
			? ExplodedViewRuntimeController::explodedViewTransformState(mesh)
			: TransformState(
				mesh->getTranslation(),
				mesh->getRotation(),
				mesh->getScaling(),
				mesh->getRotationQuaternion()));
	}

	const bool moved = _viewCtrl.transformGizmoCurrentRotationDelta().lengthSquared() > 1.0e-8f;

	if (_explodedViewCtrl.isManualPlacementActive())
	{
		if (commit && moved && !oldStatesByUuid.isEmpty())
		{
			_viewer->getUndoStack()->push(new TransformCommand(
				_viewer,
				this,
				oldStatesByUuid,
				newStatesByUuid,
				tr("Rotate Exploded Placement"),
				false,
				TransformCommand::Target::ExplodedViewTransform));
		}
		else if (!commit)
		{
			_explodedViewCtrl.setManualSessionRotationQuat(_explodedViewCtrl.manualDragStartRotationQuat());
			_explodedViewCtrl.setManualSessionRotationEuler(_explodedViewCtrl.manualDragStartRotationEuler());
		}
		emit explodedViewManualPlacementChanged();
		update();
	}
	else if (commit && moved && !oldStatesByUuid.isEmpty())
	{
		_viewer->getUndoStack()->push(new TransformCommand(
			_viewer, this, oldStatesByUuid, newStatesByUuid, tr("Rotate Selection"), false));
	}
	else
	{
		for (auto it = _viewCtrl.transformGizmoStartStates().begin(); it != _viewCtrl.transformGizmoStartStates().end(); ++it)
		{
			const int id = it.key();
			if (id < 0 || id >= static_cast<int>(_sceneRuntime.meshStore().size()))
				continue;

			if (SceneMesh* mesh = _sceneRuntime.meshAt(id))
			{
				const TransformState& startState = it.value();
				if (_explodedViewCtrl.isManualPlacementActive())
					ExplodedViewRuntimeController::applyExplodedViewTransformState(mesh, startState, false);
				else
				{
					mesh->setTranslation(startState.translation);
					if (startState.hasExactRotation)
						mesh->setRotationQuaternion(startState.rotationQuat, startState.rotation);
					else
						mesh->setRotation(startState.rotation);
					mesh->setScaling(startState.scale);
				}
			}
		}
		update();
	}

	_viewCtrl.resetTransformGizmoDragSession();
	_viewer->updateTransformationValues();
}

void ViewportWidget::drawCornerAxis(CornerAxisPosition position, const QMatrix4x4* overrideRotationMatrix)
{
	const int axisSize = std::max(1, std::min(width(), height()) / 10);
	int viewportX = 0;
	int viewportY = 0;

	// Determine the viewport position based on the CornerAxisPosition
	switch (position)
	{
	case CornerAxisPosition::TOP_LEFT:
		viewportX = 0;
		viewportY = height() - axisSize;
		break;
	case CornerAxisPosition::TOP_RIGHT:
		viewportX = width() - axisSize;
		viewportY = height() - axisSize;
		break;
	case CornerAxisPosition::BOTTOM_LEFT:
		viewportX = 0;
		viewportY = 0;
		break;
	case CornerAxisPosition::BOTTOM_RIGHT:
		viewportX = width() - axisSize;
		viewportY = 0;
		break;
	}

	// Set the viewport for the corner axis
	glViewport(viewportX, viewportY, axisSize, axisSize);

	// overrideRotationMatrix: same reasoning as drawAxis()'s identical
	// parameter - lets a caller draw this against the (rotation-only) camera
	// orientation an interactive PT frame was actually rendered with, rather
	// than the always-current _viewCtrl.viewMatrix(), so this indicator
	// doesn't visibly rotate ahead of a still-catching-up PT model during an
	// active drag. Already rotation-only by construction (translation
	// stripped below either way), matching what the caller builds.
	QMatrix4x4 mat = overrideRotationMatrix ? *overrideRotationMatrix : _viewCtrl.viewMatrix();
	mat.setColumn(3, QVector4D(0, 0, 0, 1));
	mat.setRow(3, QVector4D(0, 0, 0, 1));
	QMatrix4x4 axisProjection;
	axisProjection.ortho(-1.6f, 1.6f, -1.6f, 1.6f, -4.0f, 4.0f);

	const float axisLength = 1.0f;
	const float labelScale = std::max(0.55f, axisSize / 110.0f);

	const unsigned int prevTextWidth = _axisTextRenderer->width();
	const unsigned int prevTextHeight = _axisTextRenderer->height();
	_axisTextRenderer->setWidth(axisSize);
	_axisTextRenderer->setHeight(axisSize);

	QMatrix4x4 textProjection;
	textProjection.ortho(QRect(0.0f, 0.0f, static_cast<float>(axisSize), static_cast<float>(axisSize)));
	_renderCtrl.textShader()->bind();
	_renderCtrl.textShader()->setUniformValue("projection", textProjection);
	_renderCtrl.textShader()->release();

	// Labels
	QVector3D xAxis(axisLength, 0, 0);
	xAxis = xAxis.project(mat, axisProjection, QRect(0, 0, axisSize, axisSize));
	_axisTextRenderer->RenderText(_labelAxisX.toStdString(), xAxis.x(), axisSize - xAxis.y(), labelScale, QVector3D(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VBOTTOM);

	QVector3D yAxis(0, axisLength, 0);
	yAxis = yAxis.project(mat, axisProjection, QRect(0, 0, axisSize, axisSize));
	_axisTextRenderer->RenderText(_labelAxisY.toStdString(), yAxis.x(), axisSize - yAxis.y(), labelScale, QVector3D(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VBOTTOM);

	QVector3D zAxis(0, 0, axisLength);
	zAxis = zAxis.project(mat, axisProjection, QRect(0, 0, axisSize, axisSize));
	_axisTextRenderer->RenderText(_labelAxisZ.toStdString(), zAxis.x(), axisSize - zAxis.y(), labelScale, QVector3D(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VBOTTOM);

	// Axes
	if (!_renderCtrl.axisVAO().isCreated())
	{
		_renderCtrl.axisVAO().create();
		_renderCtrl.axisVAO().bind();
	}

	// Vertex Buffer
	if (!_renderCtrl.axisVBO().isCreated())
	{
		_renderCtrl.axisVBO() = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
		_renderCtrl.axisVBO().create();
	}
	_renderCtrl.axisVBO().bind();
	_renderCtrl.axisVBO().setUsagePattern(QOpenGLBuffer::StaticDraw);
	std::vector<float> vertices = {
		0, 0, 0,
		axisLength, 0, 0,
		0, 0, 0,
		0, axisLength, 0,
		0, 0, 0,
		0, 0, axisLength };
	_renderCtrl.axisVBO().allocate(vertices.data(), static_cast<int>(vertices.size() * sizeof(float)));

	// Color Buffer
	if (!_renderCtrl.axisCBO().isCreated())
	{
		_renderCtrl.axisCBO() = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
		_renderCtrl.axisCBO().create();
	}
	_renderCtrl.axisCBO().bind();
	_renderCtrl.axisCBO().setUsagePattern(QOpenGLBuffer::StaticDraw);
	std::vector<float> colors = {
		1, 1, 1,
		1, 1, 1,
		1, 1, 1,
		1, 1, 1,
		1, 1, 1,
		1, 1, 1 };
	_renderCtrl.axisCBO().allocate(colors.data(), static_cast<int>(colors.size() * sizeof(float)));

	_renderCtrl.axisShader()->bind();

	_renderCtrl.axisVBO().bind();
	_renderCtrl.axisShader()->enableAttributeArray("vertexPosition");
	_renderCtrl.axisShader()->setAttributeBuffer("vertexPosition", GL_FLOAT, 0, 3);

	_renderCtrl.axisCBO().bind();
	_renderCtrl.axisShader()->enableAttributeArray("vertexColor");
	_renderCtrl.axisShader()->setAttributeBuffer("vertexColor", GL_FLOAT, 0, 3);

	_renderCtrl.axisShader()->setUniformValue("modelViewMatrix", mat);
	_renderCtrl.axisShader()->setUniformValue("projectionMatrix", axisProjection);

	_renderCtrl.axisShader()->setUniformValue("renderCone", false);

	_renderCtrl.axisVAO().bind();
	glLineWidth(2.0);
	glDrawArrays(GL_LINES, 0, 6);
	glLineWidth(1);

	// Axes Cones
	// X Axis
	_axisCone->setParameters(axisLength / 15.0f, axisLength / 5.0f, 8u, 1u);
	_renderCtrl.axisShader()->setUniformValue("renderCone", true);
	mat.translate(axisLength, 0, 0);
	mat.rotate(90, QVector3D(0, 1.0f, 0));
	_renderCtrl.axisShader()->bind();
	_renderCtrl.axisShader()->setUniformValue("coneColor", QVector3D(1.0f, 1.0f, 1.0f));
	_renderCtrl.axisShader()->setUniformValue("modelViewMatrix", mat);
	_axisCone->getVAO().bind();
	glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(_axisCone->getPoints().size()), GL_UNSIGNED_INT, 0);
	_axisCone->getVAO().release();

	// Y Axis
	mat = _viewCtrl.viewMatrix();
	mat.setColumn(3, QVector4D(0, 0, 0, 1));
	mat.setRow(3, QVector4D(0, 0, 0, 1));
	mat.translate(0, axisLength, 0);
	mat.rotate(90, QVector3D(-1.0f, 0, 0));
	_renderCtrl.axisShader()->bind();
	_renderCtrl.axisShader()->setUniformValue("modelViewMatrix", mat);
	_axisCone->getVAO().bind();
	glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(_axisCone->getPoints().size()), GL_UNSIGNED_INT, 0);
	_axisCone->getVAO().release();

	// Z Axis
	mat = _viewCtrl.viewMatrix();
	mat.setColumn(3, QVector4D(0, 0, 0, 1));
	mat.setRow(3, QVector4D(0, 0, 0, 1));
	mat.translate(0, 0, axisLength);
	_renderCtrl.axisShader()->bind();
	_renderCtrl.axisShader()->setUniformValue("modelViewMatrix", mat);
	_axisCone->getVAO().bind();
	glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(_axisCone->getPoints().size()), GL_UNSIGNED_INT, 0);
	_axisCone->getVAO().release();

	_renderCtrl.axisVAO().release();
	_renderCtrl.axisShader()->release();

	_axisTextRenderer->setWidth(prevTextWidth);
	_axisTextRenderer->setHeight(prevTextHeight);
	QMatrix4x4 projection;
	projection.ortho(QRect(0.0f, 0.0f, static_cast<float>(width()), static_cast<float>(height())));
	_renderCtrl.textShader()->bind();
	_renderCtrl.textShader()->setUniformValue("projection", projection);
	_renderCtrl.textShader()->release();

	glViewport(0, 0, width(), height());
}

QRect ViewportWidget::viewCubeRect() const
{
	const int side = std::max(SceneRenderController::kViewCubeStyle.minViewportSize,
		std::min(std::min(width(), height()) / 5, SceneRenderController::kViewCubeStyle.maxViewportSize));
	const int padding = SceneRenderController::kViewCubeStyle.viewportPadding;
	return QRect(width() - side - padding, padding, side, side);
}

QRect ViewportWidget::viewCubeScreenRect() const
{
	const QRect viewportRect = viewCubeRect();
	return QRect(viewportRect.x(),
	             height() - viewportRect.y() - viewportRect.height(),
	             viewportRect.width(),
	             viewportRect.height());
}

bool ViewportWidget::computeViewCubeRenderState(QRect& viewportRect,
                                          QMatrix4x4& viewMatrix,
                                          QMatrix4x4& projectionMatrix,
                                          QMatrix4x4& modelMatrix,
                                          float& cubeScale,
                                          const QMatrix4x4* overrideRotationMatrix) const
{
	if (!_viewCube)
		return false;

	viewportRect = viewCubeRect();
	if (viewportRect.width() <= 0 || viewportRect.height() <= 0)
		return false;

	// overrideRotationMatrix: same reasoning as drawCornerAxis()'s identical
	// parameter - lets paintGL() draw this against the (rotation-only)
	// camera orientation an interactive PT frame was actually rendered with,
	// rather than the always-current _viewCtrl.viewMatrix(), so the cube
	// doesn't visibly rotate ahead of a still-catching-up PT model during an
	// active drag. Only the drawViewCube() call site passes this - picking
	// (pickViewCubeRegionAtPixel()) always wants the LIVE camera, since a
	// click should map to what's actually clickable right now.
	QMatrix4x4 viewRotation = overrideRotationMatrix ? *overrideRotationMatrix : _viewCtrl.viewMatrix();
	viewRotation.setColumn(3, QVector4D(0.0f, 0.0f, 0.0f, 1.0f));
	viewRotation.setRow(3, QVector4D(0.0f, 0.0f, 0.0f, 1.0f));

	viewMatrix.setToIdentity();
	viewMatrix.translate(0.0f, 0.0f, -SceneRenderController::kViewCubeStyle.eyeDistance);
	viewMatrix *= viewRotation;

	projectionMatrix.setToIdentity();
	const float aspect = static_cast<float>(viewportRect.width()) / static_cast<float>(std::max(1, viewportRect.height()));
	cubeScale = 1.0f;
	if (_viewCtrl.projection() == ViewProjection::ORTHOGRAPHIC)
	{
		const float orthoHalfHeight = SceneRenderController::kViewCubeStyle.orthographicHalfHeight;
		const float orthoHalfWidth = orthoHalfHeight * aspect;
		projectionMatrix.ortho(-orthoHalfWidth, orthoHalfWidth, -orthoHalfHeight, orthoHalfHeight, 0.1f, 10.0f);
		cubeScale = SceneRenderController::kViewCubeStyle.orthographicScale;
	}
	else
	{
		projectionMatrix.perspective(26.0f, aspect, 0.1f, 10.0f);
		cubeScale = SceneRenderController::kViewCubeStyle.perspectiveScale;
	}

	modelMatrix.setToIdentity();
	modelMatrix.scale(cubeScale);
	return true;
}

bool ViewportWidget::orientCameraToViewCubeNormal(const QVector3D& outwardNormal)
{
	if (!_primaryCamera || outwardNormal.lengthSquared() <= 1.0e-8f || isGltfCameraActive())
		return false;

	checkAndStopTimers();
	_keyboardNavTimer->stop();

	const QVector3D normalizedNormal = outwardNormal.normalized();
	const auto isAxisNormal = [&normalizedNormal](const QVector3D& axis) {
		return (normalizedNormal - axis).lengthSquared() <= 1.0e-6f;
	};

	const QVector3D topNormal = CoordinateSystemHelper::transformVectorForCameraUpAxis(_viewCtrl.cameraUpAxisZUp(), QVector3D(0.0f, 0.0f, 1.0f));
	const QVector3D bottomNormal = CoordinateSystemHelper::transformVectorForCameraUpAxis(_viewCtrl.cameraUpAxisZUp(), QVector3D(0.0f, 0.0f, -1.0f));
	const QVector3D frontNormal = CoordinateSystemHelper::transformVectorForCameraUpAxis(_viewCtrl.cameraUpAxisZUp(), QVector3D(0.0f, -1.0f, 0.0f));
	const QVector3D rearNormal = CoordinateSystemHelper::transformVectorForCameraUpAxis(_viewCtrl.cameraUpAxisZUp(), QVector3D(0.0f, 1.0f, 0.0f));
	const QVector3D leftNormal = CoordinateSystemHelper::transformVectorForCameraUpAxis(_viewCtrl.cameraUpAxisZUp(), QVector3D(-1.0f, 0.0f, 0.0f));
	const QVector3D rightNormal = CoordinateSystemHelper::transformVectorForCameraUpAxis(_viewCtrl.cameraUpAxisZUp(), QVector3D(1.0f, 0.0f, 0.0f));

	if (isAxisNormal(topNormal))
	{
		_viewCtrl.setCustomViewTargetRotation(CoordinateSystemHelper::standardViewRotation(_viewCtrl.cameraUpAxisZUp(), ViewMode::TOP));
		_viewCtrl.setViewMode(ViewMode::TOP);
		if (_viewToolbar)
			_viewToolbar->setDefaultStandardViewAction(StandardViewActions::TOP);
	}
	else if (isAxisNormal(bottomNormal))
	{
		_viewCtrl.setCustomViewTargetRotation(CoordinateSystemHelper::standardViewRotation(_viewCtrl.cameraUpAxisZUp(), ViewMode::BOTTOM));
		_viewCtrl.setViewMode(ViewMode::BOTTOM);
		if (_viewToolbar)
			_viewToolbar->setDefaultStandardViewAction(StandardViewActions::BOTTOM);
	}
	else if (isAxisNormal(frontNormal))
	{
		_viewCtrl.setCustomViewTargetRotation(CoordinateSystemHelper::standardViewRotation(_viewCtrl.cameraUpAxisZUp(), ViewMode::FRONT));
		_viewCtrl.setViewMode(ViewMode::FRONT);
		if (_viewToolbar)
			_viewToolbar->setDefaultStandardViewAction(StandardViewActions::FRONT);
	}
	else if (isAxisNormal(rearNormal))
	{
		_viewCtrl.setCustomViewTargetRotation(CoordinateSystemHelper::standardViewRotation(_viewCtrl.cameraUpAxisZUp(), ViewMode::BACK));
		_viewCtrl.setViewMode(ViewMode::BACK);
		if (_viewToolbar)
			_viewToolbar->setDefaultStandardViewAction(StandardViewActions::REAR);
	}
	else if (isAxisNormal(leftNormal))
	{
		_viewCtrl.setCustomViewTargetRotation(CoordinateSystemHelper::standardViewRotation(_viewCtrl.cameraUpAxisZUp(), ViewMode::LEFT));
		_viewCtrl.setViewMode(ViewMode::LEFT);
		if (_viewToolbar)
			_viewToolbar->setDefaultStandardViewAction(StandardViewActions::LEFT);
	}
	else if (isAxisNormal(rightNormal))
	{
		_viewCtrl.setCustomViewTargetRotation(CoordinateSystemHelper::standardViewRotation(_viewCtrl.cameraUpAxisZUp(), ViewMode::RIGHT));
		_viewCtrl.setViewMode(ViewMode::RIGHT);
		if (_viewToolbar)
			_viewToolbar->setDefaultStandardViewAction(StandardViewActions::RIGHT);
	}
	else
	{
		const QVector3D viewDir = -normalizedNormal;
		// Keep the View Cube aligned to the viewer's Z-up convention while
		// picking a stable screen-up vector for face, edge, and corner targets.
		QVector3D upSeed = _viewCtrl.cameraUpAxisZUp()
			? QVector3D(0.0f, 0.0f, 1.0f)
			: QVector3D(0.0f, 1.0f, 0.0f);
		if (std::abs(QVector3D::dotProduct(viewDir, upSeed)) > 0.95f)
			upSeed = QVector3D(1.0f, 0.0f, 0.0f);

		QVector3D right = QVector3D::crossProduct(viewDir, upSeed);
		if (right.lengthSquared() <= 1.0e-8f)
			right = QVector3D::crossProduct(viewDir, QVector3D(1.0f, 0.0f, 0.0f));
		if (right.lengthSquared() <= 1.0e-8f)
			return false;
		right.normalize();
		QVector3D up = QVector3D::crossProduct(right, viewDir).normalized();

		QMatrix4x4 targetMatrix;
		targetMatrix.setRow(0, QVector4D(right, 0.0f));
		targetMatrix.setRow(1, QVector4D(up, 0.0f));
		targetMatrix.setRow(2, QVector4D(-viewDir, 0.0f));
		targetMatrix.setRow(3, QVector4D(0.0f, 0.0f, 0.0f, 1.0f));
		_viewCtrl.setCustomViewTargetRotation(QQuaternion::fromRotationMatrix(targetMatrix.toGenericMatrix<3, 3>()).normalized());
		_viewCtrl.setViewMode(ViewMode::NONE);
	}

	const std::vector<int>& visibleIds = _sceneRuntime.currentVisibleObjectIds();
	if (!_sceneRuntime.meshStore().empty() && !visibleIds.empty())
	{
		const QMatrix4x4 targetRotationMatrix(_viewCtrl.customViewTargetRotation().toRotationMatrix());
		const QVector3D right = targetRotationMatrix.row(0).toVector3D().normalized();
		const QVector3D up = targetRotationMatrix.row(1).toVector3D().normalized();
		const QVector3D viewDir = -targetRotationMatrix.row(2).toVector3D().normalized();
		QVector3D projCenter;
		_viewCtrl.setViewBoundingSphereDia(computeFitViewRange(right, up, viewDir, &projCenter));
		_viewCtrl.setBoundingSphereCenter(projCenter);
	}
	else
	{
		_viewCtrl.setViewBoundingSphereDia(_viewCtrl.currentViewRange());
	}

	_viewCtrl.setCustomViewAnimationActive(true);
	_viewCtrl.resetSlerpStep();
	if (!_animateViewTimer->isActive())
		_animateViewTimer->start(5);
	return true;
}

bool ViewportWidget::handleViewCubeClick(const QPoint& pixel)
{
	if (!_viewCtrl.showViewCubeOverride() || !_viewCube || !_primaryCamera || !viewCubeScreenRect().contains(pixel) || isGltfCameraActive())
		return false;

	QVector3D outwardNormal;
	int regionId = -1;
	if (!pickViewCubeRegionAtPixel(pixel, outwardNormal, &regionId))
		return true;

	orientCameraToViewCubeNormal(outwardNormal);
	return true;
}

bool ViewportWidget::pickViewCubeRegionAtPixel(const QPoint& pixel, QVector3D& outwardNormal, int* regionId) const
{
	outwardNormal = QVector3D();
	if (regionId)
		*regionId = -1;
	if (!_viewCtrl.showViewCubeOverride() || !_viewCube || !_primaryCamera || !viewCubeScreenRect().contains(pixel) || isGltfCameraActive())
		return false;

	QRect viewportRect;
	QMatrix4x4 viewMatrix;
	QMatrix4x4 projectionMatrix;
	QMatrix4x4 modelMatrix;
	float cubeScale = 1.0f;
	if (!computeViewCubeRenderState(viewportRect, viewMatrix, projectionMatrix, modelMatrix, cubeScale))
		return false;

	const int glX = pixel.x();
	const int glY = height() - pixel.y() - 1;
	const QVector3D nearPoint(glX, glY, 0.0f);
	const QVector3D farPoint(glX, glY, 1.0f);
	const QVector3D rayOrigin = nearPoint.unproject(viewMatrix, projectionMatrix, viewportRect);
	const QVector3D rayFar = farPoint.unproject(viewMatrix, projectionMatrix, viewportRect);
	QVector3D rayDir = rayFar - rayOrigin;
	if (rayDir.lengthSquared() <= 1.0e-8f)
		return false;
	rayDir.normalize();

	ViewCubeMesh::RegionHit hit;
	if (!_viewCube->pickRegion(rayOrigin, rayDir, modelMatrix, hit))
		return false;

	outwardNormal = hit.outwardNormal;
	if (regionId)
		*regionId = hit.regionId;
	return true;
}

void ViewportWidget::updateViewCubeHover(const QPoint& pixel, Qt::MouseButtons buttons)
{
	const int previousRegionId = _viewCtrl.viewCubeHoveredRegionId();
	if (!_viewCtrl.showViewCubeOverride() || buttons != Qt::NoButton || !_viewCube || isGltfCameraActive() || !viewCubeScreenRect().contains(pixel))
	{
		_viewCtrl.setViewCubeHoveredRegionId(-1);
	}
	else
	{
		QVector3D outwardNormal;
		int regionId = -1;
		_viewCtrl.setViewCubeHoveredRegionId(
			pickViewCubeRegionAtPixel(pixel, outwardNormal, &regionId) ? regionId : -1);
	}

	if (_viewCtrl.viewCubeHoveredRegionId() != previousRegionId)
		update();
}

void ViewportWidget::initializeViewCubeLabels()
{
	if (!_renderCtrl.viewCubeLabelShader())
		return;

	const auto labelFaces = SceneRenderController::buildViewCubeLabelFaces(
		_labelTop, _labelFront, _labelLeft,
		tr("Bottom"), tr("Rear"), tr("Right"),
		CoordinateSystemHelper::cameraUpAxisConventionRotation(_viewCtrl.cameraUpAxisZUp()));

	const TextureSamplerSettings samplers = {
		GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR
	};

	for (GLuint& texture : _renderCtrl.viewCubeLabelTextures())
	{
		if (texture != 0)
		{
			glDeleteTextures(1, &texture);
			texture = 0;
		}
	}

	for (int i = 0; i < static_cast<int>(labelFaces.size()); ++i)
	{
		QImage image(SceneRenderController::kViewCubeStyle.labelTextureSize, SceneRenderController::kViewCubeStyle.labelTextureSize, QImage::Format_RGBA8888);
		image.fill(Qt::transparent);

		QPainter painter(&image);
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setRenderHint(QPainter::TextAntialiasing, true);
		QFont font(QStringLiteral("Arial"));
		font.setBold(true);
		font.setPixelSize(SceneRenderController::kViewCubeStyle.labelFontPixelSize);
		font.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
		painter.setFont(font);
		painter.setPen(SceneRenderController::kViewCubeStyle.labelTextColor);
		painter.drawText(image.rect(), Qt::AlignCenter, labelFaces[i].text);
		painter.end();

		_renderCtrl.viewCubeLabelTextures()[i] = uploadDecodedTextureImage(image, samplers);
	}

	_renderCtrl.initViewCubeLabelGeometry();

	const float quadVertices[] = {
		-0.5f, -0.5f, 0.0f, 0.0f, 1.0f,
		 0.5f, -0.5f, 0.0f, 1.0f, 1.0f,
		 0.5f,  0.5f, 0.0f, 1.0f, 0.0f,
		-0.5f, -0.5f, 0.0f, 0.0f, 1.0f,
		 0.5f,  0.5f, 0.0f, 1.0f, 0.0f,
		-0.5f,  0.5f, 0.0f, 0.0f, 0.0f
	};

	glBindVertexArray(_renderCtrl.viewCubeLabelVAO());
	glBindBuffer(GL_ARRAY_BUFFER, _renderCtrl.viewCubeLabelVBO());
	glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(0));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
	glBindVertexArray(0);
}

void ViewportWidget::drawViewCube(const QMatrix4x4* overrideRotationMatrix)
{
	if (!_viewCtrl.showViewCubeOverride() || !_viewCube || !_renderCtrl.viewCubeShader())
		return;

	QRect viewportRect;
	QMatrix4x4 viewMatrix;
	QMatrix4x4 projectionMatrix;
	QMatrix4x4 modelMatrix;
	float cubeScale = 1.0f;
	if (!computeViewCubeRenderState(viewportRect, viewMatrix, projectionMatrix, modelMatrix, cubeScale, overrideRotationMatrix))
		return;

	int prevViewport[4];
	glGetIntegerv(GL_VIEWPORT, prevViewport);

	GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
	GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);
	GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
	GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
	GLboolean stencilWasEnabled = glIsEnabled(GL_STENCIL_TEST);
	GLboolean polygonOffsetFillWasEnabled = glIsEnabled(GL_POLYGON_OFFSET_FILL);
	GLint frontFaceMode = GL_CCW;
	GLint cullFaceMode = GL_BACK;
	GLint depthFuncMode = GL_LESS;
	glGetIntegerv(GL_FRONT_FACE, &frontFaceMode);
	glGetIntegerv(GL_CULL_FACE_MODE, &cullFaceMode);
	glGetIntegerv(GL_DEPTH_FUNC, &depthFuncMode);

	// The main scene may leave behind render-state choices that are correct for
	// PBR/ADS mesh passes but wrong for the View Cube overlay. Establish a
	// known-good state here so the cube remains visually stable across display
	// modes, rendering modes, and intermediate overlay passes.
	glEnable(GL_SCISSOR_TEST);
	glScissor(viewportRect.x(), viewportRect.y(), viewportRect.width(), viewportRect.height());
	glClear(GL_DEPTH_BUFFER_BIT);
	glViewport(viewportRect.x(), viewportRect.y(), viewportRect.width(), viewportRect.height());
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glDepthMask(GL_TRUE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glFrontFace(GL_CCW);
	glCullFace(GL_BACK);
	glDepthFunc(GL_LEQUAL);
	_viewCube->setSceneRenderTransformFast(modelMatrix);
	_viewCube->setProg(_renderCtrl.viewCubeShader());

	_renderCtrl.viewCubeShader()->bind();
	_renderCtrl.viewCubeShader()->setProperty("globalModelMatrix", QVariant::fromValue(QMatrix4x4()));
	_renderCtrl.viewCubeShader()->setProperty("viewMatrix", QVariant::fromValue(viewMatrix));
	_renderCtrl.viewCubeShader()->setUniformValue("viewMatrix", viewMatrix);
	_renderCtrl.viewCubeShader()->setUniformValue("projectionMatrix", projectionMatrix);
	_renderCtrl.viewCubeShader()->setUniformValue("lightDirView", QVector3D(0.0f, 0.0f, 1.0f));
	_renderCtrl.viewCubeShader()->setUniformValue("baseColor", SceneRenderController::kViewCubeStyle.baseFaceColor);
	_renderCtrl.viewCubeShader()->setUniformValue("ambientStrength", SceneRenderController::kViewCubeStyle.baseAmbient);
	_renderCtrl.viewCubeShader()->setUniformValue("diffuseStrength", SceneRenderController::kViewCubeStyle.baseDiffuse);
	_viewCube->render();
	_renderCtrl.viewCubeShader()->setUniformValue("baseColor", SceneRenderController::kViewCubeStyle.primaryFaceColor);
	_renderCtrl.viewCubeShader()->setUniformValue("ambientStrength", SceneRenderController::kViewCubeStyle.primaryAmbient);
	_renderCtrl.viewCubeShader()->setUniformValue("diffuseStrength", SceneRenderController::kViewCubeStyle.primaryDiffuse);
	for (int regionId = 0; regionId < _viewCube->regionCount(); ++regionId)
	{
		if (_viewCube->isPrimaryFaceRegion(regionId))
			_viewCube->renderRegion(regionId);
	}
	if (_viewCtrl.viewCubeHoveredRegionId() >= 0)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_CULL_FACE);
		_renderCtrl.viewCubeShader()->setUniformValue("baseColor", SceneRenderController::kViewCubeStyle.hoverFaceColor);
		_renderCtrl.viewCubeShader()->setUniformValue("ambientStrength", SceneRenderController::kViewCubeStyle.hoverAmbient);
		_renderCtrl.viewCubeShader()->setUniformValue("diffuseStrength", SceneRenderController::kViewCubeStyle.hoverDiffuse);
		_viewCube->renderRegion(_viewCtrl.viewCubeHoveredRegionId());
		glDisable(GL_BLEND);
		if (cullWasEnabled)
			glEnable(GL_CULL_FACE);
	}
	drawViewCubeLabels(viewMatrix, projectionMatrix, cubeScale);

	if (!depthWasEnabled)
		glDisable(GL_DEPTH_TEST);
	// All scene rendering is complete by this point; restore the engine's
	// canonical writable masks instead of querying them from the driver.
	glDepthMask(GL_TRUE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	if (cullWasEnabled)
		glEnable(GL_CULL_FACE);
	else
		glDisable(GL_CULL_FACE);
	glCullFace(cullFaceMode);
	glFrontFace(frontFaceMode);
	glDepthFunc(depthFuncMode);
	if (blendWasEnabled)
		glEnable(GL_BLEND);
	if (stencilWasEnabled)
		glEnable(GL_STENCIL_TEST);
	if (polygonOffsetFillWasEnabled)
		glEnable(GL_POLYGON_OFFSET_FILL);
	if (!scissorWasEnabled)
		glDisable(GL_SCISSOR_TEST);

	glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
}

void ViewportWidget::drawViewCubeLabels(const QMatrix4x4& viewMatrix, const QMatrix4x4& projectionMatrix, float cubeScale)
{
	if (!_renderCtrl.viewCubeLabelShader() || _renderCtrl.viewCubeLabelVAO() == 0)
		return;

	const std::array<QMatrix4x4, 6> labelTransforms = [this, cubeScale]() {
		std::array<QMatrix4x4, 6> transforms;
		const float offset = SceneRenderController::kViewCubeStyle.labelFaceOffset * cubeScale;
		const float scale = SceneRenderController::kViewCubeStyle.labelFaceScale * cubeScale;

		auto faceTransform = [offset, scale](const QVector3D& center,
			const QVector3D& right,
			const QVector3D& up,
			const QVector3D& normal) {
			QMatrix4x4 matrix;
			matrix.setColumn(0, QVector4D(right.normalized() * scale, 0.0f));
			matrix.setColumn(1, QVector4D(up.normalized() * scale, 0.0f));
			matrix.setColumn(2, QVector4D(normal.normalized(), 0.0f));
			matrix.setColumn(3, QVector4D(center + normal.normalized() * offset, 1.0f));
			return matrix;
		};

		const auto faces = SceneRenderController::buildViewCubeLabelFaces(
			_labelTop, _labelFront, _labelLeft,
			tr("Bottom"), tr("Rear"), tr("Right"),
			CoordinateSystemHelper::cameraUpAxisConventionRotation(_viewCtrl.cameraUpAxisZUp()));
		for (int i = 0; i < static_cast<int>(faces.size()); ++i)
			transforms[i] = faceTransform(QVector3D(0.0f, 0.0f, 0.0f), faces[i].right, faces[i].up, faces[i].normal);

		return transforms;
	}();

	GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
	GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_CULL_FACE);

	_renderCtrl.viewCubeLabelShader()->bind();
	_renderCtrl.viewCubeLabelShader()->setUniformValue("viewMatrix", viewMatrix);
	_renderCtrl.viewCubeLabelShader()->setUniformValue("projectionMatrix", projectionMatrix);
	_renderCtrl.viewCubeLabelShader()->setUniformValue("labelTexture", 0);

	glActiveTexture(GL_TEXTURE0);
	glBindVertexArray(_renderCtrl.viewCubeLabelVAO());

	for (int i = 0; i < static_cast<int>(_renderCtrl.viewCubeLabelTextures().size()); ++i)
	{
		if (_renderCtrl.viewCubeLabelTextures()[i] == 0)
			continue;
		glBindTexture(GL_TEXTURE_2D, _renderCtrl.viewCubeLabelTextures()[i]);
		_renderCtrl.viewCubeLabelShader()->setUniformValue("modelMatrix", labelTransforms[i]);
		glDrawArrays(GL_TRIANGLES, 0, 6);
	}

	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	if (!blendWasEnabled)
		glDisable(GL_BLEND);
	if (cullWasEnabled)
		glEnable(GL_CULL_FACE);
}

void ViewportWidget::drawLights()
{
	QMatrix4x4 model;
	model.translate(effectiveWorldLightPosition());
	_renderCtrl.lightCubeShader()->bind();
	QMatrix4x4 viewMat = _viewCtrl.viewMatrix();
	_renderCtrl.lightCubeShader()->setProperty("globalModelMatrix", QVariant::fromValue(QMatrix4x4()));
	_renderCtrl.lightCubeShader()->setProperty("viewMatrix", QVariant::fromValue(viewMat));
	_renderCtrl.lightCubeShader()->setUniformValue("viewMatrix", viewMat);
	_renderCtrl.lightCubeShader()->setUniformValue("projectionMatrix", _viewCtrl.projectionMatrix());
	_renderCtrl.lightCubeShader()->setUniformValue("lightColor", _diffuseLight.toVector3D());	
	_renderCtrl.lightCubeShader()->setUniformValue("intensity", 1.0f);
	_renderCtrl.lightCubeShader()->setUniformValue("intensityScale", 1.0f);  // Tune brightness
	_lightCube->setSceneRenderTransformFast(model);
	_lightCube->render();

	// Draw punctual lights — only those whose enabled flag is set (tree checkbox AND Show Lights).
	{
		const std::vector<GPULight> gizmoLights =
		    _animCtrl.buildGizmoLights(_viewer ? _viewer->sceneGraph() : nullptr);
		if (!gizmoLights.empty())
		{
		const float sceneRadius = std::max(_viewCtrl.boundingSphere().getRadius(), 0.001f);
		const float pointSpotScale = sceneRadius * 0.05f;
		const float directionalScale = pointSpotScale * 0.75f;
		const QVector3D sceneCenter = _viewCtrl.boundingSphere().getCenter();

		for (const GPULight& light : gizmoLights)
		{
			// Map intensity logarithmically to [0.3, 1.0] so gizmos are always visible
			// (even at intensity=0) while brighter lights appear more luminous.
			// log10(intensity + 1) / 3  maps  0 → 0,  100 → 0.67,  999 → 1.0
			const float logVal            = std::log10(light.intensity + 1.0f) / 3.0f;
			const float displayBrightness = 0.3f + 0.7f * std::min(logVal, 1.0f);

			// Emission colour: the light's own colour modulated by display brightness.
			const QVector3D emissiveColor(light.color.x * displayBrightness,
			                              light.color.y * displayBrightness,
			                              light.color.z * displayBrightness);

			QMatrix4x4 lightModel;
			const LightType lightType = static_cast<LightType>(light.type);

			if (lightType == LightType::Directional)
			{
				// Directional lights are defined by direction rather than emitter position.
				// Show them as a small cube offset from the scene center along the incoming direction
				// so multiple directional lights don't collapse visually at the origin.
				QVector3D dir(light.direction.x, light.direction.y, light.direction.z);
				if (dir.lengthSquared() < 1e-6f)
					dir = QVector3D(0.0f, 0.0f, -1.0f);
				dir.normalize();
				const QVector3D markerPos = sceneCenter - dir * (sceneRadius + directionalScale * 6.0f);
				lightModel.translate(markerPos);
				lightModel.scale(directionalScale);
			}
			else
			{
				lightModel.translate(light.position.x, light.position.y, light.position.z);
				lightModel.scale(pointSpotScale);
			}

			_renderCtrl.lightCubeShader()->bind();
			_renderCtrl.lightCubeShader()->setProperty("globalModelMatrix", QVariant::fromValue(QMatrix4x4()));
			_renderCtrl.lightCubeShader()->setProperty("viewMatrix", QVariant::fromValue(viewMat));
			_renderCtrl.lightCubeShader()->setUniformValue("viewMatrix", viewMat);
			_renderCtrl.lightCubeShader()->setUniformValue("projectionMatrix", _viewCtrl.projectionMatrix());
			_renderCtrl.lightCubeShader()->setUniformValue("lightColor", emissiveColor);
			_renderCtrl.lightCubeShader()->setUniformValue("intensity", 1.0f);
			_renderCtrl.lightCubeShader()->setUniformValue("intensityScale", 1.0f);

			if (lightType == LightType::Directional)
			{
				_lightCube->setSceneRenderTransformFast(lightModel);
				_lightCube->render();
			}
			else
			{
				_lightSphere->setSceneRenderTransformFast(lightModel);
				_lightSphere->render();
			}
		}
	}
	}
}

void ViewportWidget::bindIBLTextures()
{
	_renderCtrl.fgShader()->setUniformValue("irradianceMap", 3);
	glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_CUBE_MAP, _renderCtrl.irradianceMap());
	_renderCtrl.fgShader()->setUniformValue("prefilterMap", 4);
	glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_CUBE_MAP, _renderCtrl.prefilterMap());
	_renderCtrl.fgShader()->setUniformValue("brdfLUT", 5);
	glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_2D, _renderCtrl.brdfLUTTexture());
	_renderCtrl.fgShader()->setUniformValue("sheenPrefilterMap", 7);
	glActiveTexture(GL_TEXTURE7); glBindTexture(GL_TEXTURE_CUBE_MAP, _renderCtrl.sheenPrefilterMap());
	_renderCtrl.fgShader()->setUniformValue("charlieLUT", 8);
	glActiveTexture(GL_TEXTURE8); glBindTexture(GL_TEXTURE_2D, _renderCtrl.charlieLUTTexture());
	_renderCtrl.fgShader()->setUniformValue("sheenELUT",  9);
	glActiveTexture(GL_TEXTURE9); glBindTexture(GL_TEXTURE_2D, _renderCtrl.sheenELUTTexture());
	// Effective mip count for sheen LOD: lod = roughness * (sheenPrefilterMipLevels - 1)
	int sheenMips = (_renderCtrl.sheenPrefilterMipLevels() > 0) ? (int)_renderCtrl.sheenPrefilterMipLevels() : 5;
	_renderCtrl.fgShader()->setUniformValue("sheenPrefilterMipLevels", sheenMips);
	// Effective mip count for GGX specular LOD: lod = roughness * (prefilterMipLevels - 1)
	int prefilterMips = (_renderCtrl.prefilterMipLevels() > 0) ? (int)_renderCtrl.prefilterMipLevels() : 5;
	_renderCtrl.fgShader()->setUniformValue("prefilterMipLevels", prefilterMips);
}


void ViewportWidget::render(Camera* camera)
{
	QElapsedTimer frameTimer;
	const bool profileRendering =
		QSettings().value(QStringLiteral("profileRenderingCheckBox"), false).toBool();
	RenderableMesh::beginRenderDiagnosticsFrame(profileRendering);
	if (profileRendering)
		frameTimer.start();

	glEnable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	if (_renderCtrl.backfaceCulling()) { glEnable(GL_CULL_FACE); glCullFace(GL_BACK); }
	else glDisable(GL_CULL_FACE);
	glFrontFace(GL_CCW);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glDisable(GL_POLYGON_OFFSET_LINE);
	glDepthMask(GL_TRUE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glStencilMask(0xFF);
	glDisable(GL_STENCIL_TEST);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glLineWidth(1.0f);

	// Keep the scene radius in the camera current every frame so the ortho
	// near/far planes always cover the full scene, including animated models
	// whose GPU-skinned geometry extends beyond the bind-pose bounding sphere.
	camera->setSceneRadius(_viewCtrl.boundingSphere().getRadius());
	_viewCtrl.syncMatricesFromCamera(*camera);

	// Extract frustum planes and clipping context once per frame for AABB culling
	extractFrustumPlanes();
	rebuildClippingContext();
	_sceneRuntime.refreshRuntimeVisibilityCacheForCurrentView(
		(_viewer && _viewer->sceneGraph()) ? _viewer->sceneGraph()->root() : nullptr,
		RenderableMesh::currentRuntimeBoundsRevision(),
		[this](const SceneMesh* mesh) { return isMeshAnimationVisible(mesh); });

	// While an interactive PT frame is being composited for the primary
	// view, keep the raster skybox (drawn from the PT frame's own published
	// camera pose when available) but suppress raster mesh passes
	// underneath. That preserves a coherent background without letting a
	// newer raster model show under an older PT chunk.
	const bool interactivePtOverlayShowing =
		camera == _primaryCamera &&
		_rtInteractionCtrl->armed() &&
		_rayTracedInteractiveActive &&
		_rtPresenter.hasFrame();
	QMatrix4x4 interactivePtSkyboxView;
	const QMatrix4x4* skyboxViewOverride = nullptr;
	// Whenever GPU ray tracing is armed for the primary view - interactive
	// OR settled - build the skybox's view matrix directly from the
	// camera's own forward/right/up basis rather than QMatrix4x4::lookAt().
	// lookAt() silently RE-DERIVES its own "side" vector as cross(forward,
	// up), discarding whatever this camera's OWN stored right vector
	// actually is. RtOptixScene.cu's raygen program uses camForward/
	// camRight/camUp directly, as independently-stored vectors (see
	// RtSceneBuilder::buildCamera()) - and Camera::rotateX()/rotateY()
	// update _upVector and _rightVector in a slightly inconsistent order
	// (rotateY derives the new right from an up that was orthogonalized
	// against the PRE-yaw view direction), so right can drift measurably
	// away from cross(forward, up) over a sustained drag. Reconstructing
	// the skybox's basis via lookAt() ignores that drift and shows a
	// "clean" orientation instead of the actual (possibly skewed) one the
	// PT model was rendered with.
	//
	// Applying this construction on BOTH sides of the interactive->settled
	// handoff (only the SOURCE camera differs: the lagged last-published
	// interactive pose while a drag is in flight, the live camera once
	// settled) matters just as much as picking the right basis in the first
	// place - using lookAt() on one side and this raw-basis construction on
	// the other means the handoff itself introduces a visible skybox/floor
	// jump purely from switching reconstruction methods, independent of any
	// actual camera motion (which has long since caught up by the time
	// settle fires 450ms after the last drag event).
	if (camera == _primaryCamera && _rtInteractionCtrl->armed() &&
		effectiveRayTracingEnginePreference() == RtRayTracingEnginePreference::GPU)
	{
		glm::vec3 f, r, u;
		if (interactivePtOverlayShowing && _rtInteractivePreviewCameraValid)
		{
			f = _rtInteractivePreviewCamera.forward;
			r = _rtInteractivePreviewCamera.right;
			u = _rtInteractivePreviewCamera.up;
		}
		else
		{
			const QVector3D qf = _primaryCamera->getViewDir();
			const QVector3D qr = _primaryCamera->getRightVector();
			const QVector3D qu = _primaryCamera->getUpVector();
			f = glm::vec3(qf.x(), qf.y(), qf.z());
			r = glm::vec3(qr.x(), qr.y(), qr.z());
			u = glm::vec3(qu.x(), qu.y(), qu.z());
		}
		interactivePtSkyboxView.setToIdentity();
		interactivePtSkyboxView(0, 0) = r.x; interactivePtSkyboxView(0, 1) = r.y; interactivePtSkyboxView(0, 2) = r.z;
		interactivePtSkyboxView(1, 0) = u.x; interactivePtSkyboxView(1, 1) = u.y; interactivePtSkyboxView(1, 2) = u.z;
		interactivePtSkyboxView(2, 0) = -f.x; interactivePtSkyboxView(2, 1) = -f.y; interactivePtSkyboxView(2, 2) = -f.z;
		skyboxViewOverride = &interactivePtSkyboxView;
	}

	// --- 1) Skybox ---
	if (_renderCtrl.skyBoxEnabled())
	{
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		drawSkyBox(skyboxViewOverride);
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);
	}

	// --- 2) Opaque meshes (with clipping) ---
	glActiveTexture(GL_TEXTURE0 + 37);
	glBindTexture(GL_TEXTURE_2D, _renderCtrl.sssCaptureTexture() != 0 ? _renderCtrl.sssCaptureTexture() : _renderCtrl.whiteTexture());
	glActiveTexture(GL_TEXTURE0 + 38);
	glBindTexture(GL_TEXTURE_2D, _renderCtrl.sssDepthTexture() != 0 ? _renderCtrl.sssDepthTexture() : _renderCtrl.whiteTexture());
	glActiveTexture(GL_TEXTURE0);

	if (!interactivePtOverlayShowing)
	{
		_renderCtrl.fgShader()->bind();
		RenderableMesh::recordProgramBindCall(true);
		setCommonUniforms(_renderCtrl.fgShader(), camera);	
		{
			QElapsedTimer opaqueTimer;
			if (profileRendering)
				opaqueTimer.start();
			drawMeshesWithClipping(_renderCtrl.fgShader(), false); // opaque pass
			if (profileRendering)
				RenderableMesh::recordOpaquePassCpuMs(static_cast<double>(opaqueTimer.nsecsElapsed()) / 1000000.0);
		}
		_renderCtrl.fgShader()->release();
	}

	// --- 2.5) Section caps (after opaque, before floor & transparents) ---
	if (!interactivePtOverlayShowing &&
		_renderCtrl.cappingEnabled() &&
		!_renderCtrl.sectionCapsSuppressedDuringInteraction() &&
		(_renderCtrl.yzClippingEnabled() || _renderCtrl.zxClippingEnabled() || _renderCtrl.xyClippingEnabled()))
	{
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(1.0f, 1.0f); // pull forward
		drawSectionCapping();
		glDisable(GL_POLYGON_OFFSET_FILL);
	}

	// --- 3) Ground ---
	if (_realismEnabled &&
		_renderCtrl.groundMode() != GroundMode::None && !_renderCtrl.cappingEnabled() &&
		!_sceneRuntime.meshStore().empty() &&
		camera != _orthoViewsCamera)
	{
		// While the ray-traced overlay is actually being composited over
		// this frame (see paintGL()'s matching _rayTracedArmed/idle-timer/
		// _rayTracedInteractiveActive/hasFrame() gate), it draws its own,
		// deliberately much smaller floor instance (RtSceneBuilder::
		// addFloorInstance() - sized from the scene bounding box, not the
		// raster floor's large aesthetic fade-out extent) with alpha=1 where
		// the primary ray hit it. Elsewhere (alpha=0) the raster frame
		// underneath still shows through by design (see RtPresenter's alpha
		// blending, added to fix skybox/gradient background sync) - but that
		// same mechanism was letting *this* raster floor's much larger
		// extent bleed through around the edges of the smaller ray-traced
		// one. Skipping this raster floor draw specifically when the overlay
		// is about to cover the primary view avoids drawing a floor that
		// would only be visible in the gap between the two extents. Must
		// match paintGL()'s gate exactly, including the GPU-interactive
		// relaxation - otherwise every drag frame on GPU/OptiX would show
		// both floors at once (the very bug this condition exists to avoid).
		const bool rayTracedOverlayShowing =
			camera == _primaryCamera && _rtInteractionCtrl->armed()
			&& (_rayTracedInteractiveActive || !_rayTracedIdleTimer->isActive())
			&& _rtPresenter.hasFrame();

		if (_renderCtrl.groundMode() == GroundMode::Floor)
		{
			if (!rayTracedOverlayShowing)
			{
				glActiveTexture(GL_TEXTURE0 + 32);
				glBindTexture(GL_TEXTURE_2D,
					(camera == _primaryCamera && _renderCtrl.transmissionColorTexture() != 0) ? _renderCtrl.transmissionColorTexture() : _renderCtrl.whiteTexture());
				glActiveTexture(GL_TEXTURE0 + 33);
				glBindTexture(GL_TEXTURE_2D,
					(camera == _primaryCamera && _renderCtrl.transmissionDepthTexture() != 0) ? _renderCtrl.transmissionDepthTexture() : _renderCtrl.whiteTexture());
				glActiveTexture(GL_TEXTURE0);
				QElapsedTimer floorTimer;
				if (profileRendering)
					floorTimer.start();
				drawFloor();
				if (profileRendering)
					RenderableMesh::recordFloorPassCpuMs(static_cast<double>(floorTimer.nsecsElapsed()) / 1000000.0);
			}
		}
		else if (_renderCtrl.groundMode() == GroundMode::Grid)
		{
			drawGrid();
		}
	}

	if (camera == _primaryCamera)
	{
		// Bind transmission texture for shader sampling
		glActiveTexture(GL_TEXTURE0 + 32);
		glBindTexture(GL_TEXTURE_2D, _renderCtrl.transmissionColorTexture());

		glActiveTexture(GL_TEXTURE0 + 33);
		glBindTexture(GL_TEXTURE_2D, _renderCtrl.transmissionDepthTexture());
	}

	// --- 4) Transparent meshes (with clipping) ---
	if (!interactivePtOverlayShowing)
	{
		_renderCtrl.fgShader()->bind();
		RenderableMesh::recordProgramBindCall(true);
		setCommonUniforms(_renderCtrl.fgShader(), camera);
		{
			QElapsedTimer transparentTimer;
			if (profileRendering)
				transparentTimer.start();
			drawMeshesWithClipping(_renderCtrl.fgShader(), true); // transparent pass
			if (profileRendering)
				RenderableMesh::recordTransparentPassCpuMs(static_cast<double>(transparentTimer.nsecsElapsed()) / 1000000.0);
		}
		_renderCtrl.fgShader()->release();
	}

	// --- 5) Overlays ---
    drawDebugOverlay(camera);
	// Single-view mode draws this AFTER the ray-traced overlay instead (see
	// paintGL()'s post-overlay block) so it isn't wiped out by PT's force-
	// opaque composite - drawing it here too would just double-draw it
	// (harmless but wasteful) for that case. Multi-view has no PT overlay to
	// worry about and still wants its own per-viewport axis indicator here.
	if (_viewCtrl.showAxis() && _viewCtrl.userShowAxisOverride() && !_capturingCleanFrame && _viewCtrl.multiViewActive())
		drawAxis(camera);
	// Same reasoning as drawAxis() just above: renderSingleView() already
	// draws this once for the single-view case (after render() returns), so
	// only draw it here for multi-view's per-sub-viewport pass to avoid
	// double-drawing.
	if (_viewCtrl.multiViewActive())
		drawMeasurementOverlay(camera);
	if (_renderCtrl.showLights()) drawLights();
	if (profileRendering)
		RenderableMesh::recordFrameCpuMs(static_cast<double>(frameTimer.nsecsElapsed()) / 1000000.0);
	RenderableMesh::flushRenderDiagnostics();
}



void ViewportWidget::renderToShadowBuffer()
{
	if (!_renderCtrl.shadowMapNeedsInitialization())
		return;
	_renderCtrl.setShadowMapNeedsInitialization(false);

	// save current viewport
	int viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);

	/// Shadow Mapping
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glViewport(0, 0, _renderCtrl.shadowWidth(), _renderCtrl.shadowHeight());
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _renderCtrl.shadowMapFBO());
	glClear(GL_DEPTH_BUFFER_BIT);
	glDisable(GL_CULL_FACE);

	// 1. render depth of scene to texture (from light's perspective)
	// --------------------------------------------------------------
	QMatrix4x4 lightProjection, lightView;	
	QVector3D center = _viewCtrl.boundingSphere().getCenter();
	float radius = _viewCtrl.boundingSphere().getRadius();
	QVector3D lightPos = effectiveWorldLightPosition();

	// Light looks at scene center
	QVector3D lightDir = center;
	QVector3D lightUp = CoordinateSystemHelper::currentWorldUpVector(_viewCtrl.cameraUpAxisZUp());
	if (std::abs(QVector3D::dotProduct((lightDir - lightPos).normalized(), lightUp)) > 0.98f)
	{
		// Light direction nearly collinear with world-up: pick the least-aligned world axis
		const float ax = std::abs(lightUp.x());
		const float ay = std::abs(lightUp.y());
		const float az = std::abs(lightUp.z());
		lightUp = (ax <= ay && ax <= az) ? QVector3D(1.0f, 0.0f, 0.0f)
		        : (ay <= az)             ? QVector3D(0.0f, 1.0f, 0.0f)
		                                 : QVector3D(0.0f, 0.0f, 1.0f);
	}

	lightView.lookAt(lightPos, lightDir, lightUp);

	BoundingBox shadowCasterBounds;
	BoundingBox skinnedBounds;
	bool hasShadowCasterBounds = false;
	bool hasSkinnedBounds = false;
	const std::vector<int>& visibleIds = _sceneRuntime.currentVisibleObjectIds();
	for (int i : visibleIds)
	{
		try
		{
			const SceneMesh* mesh = _sceneRuntime.meshAt(i);
			if (!mesh || !isMeshAnimationVisible(mesh))
				continue;

			const BoundingBox bb = mesh->getBoundingBox();
			if (!hasShadowCasterBounds)
			{
				shadowCasterBounds = bb;
				hasShadowCasterBounds = true;
			}
			else
			{
				shadowCasterBounds.addBox(bb);
			}

			if (mesh->hasSkinning())
			{
				if (!hasSkinnedBounds) { skinnedBounds = bb; hasSkinnedBounds = true; }
				else skinnedBounds.addBox(bb);
			}
		}
		catch (const std::exception&)
		{
		}
	}

	if (!hasShadowCasterBounds)
	{
		const float fallbackExtent = (std::max)(radius, 0.001f);
		shadowCasterBounds.setLimits(
			center.x() - fallbackExtent, center.x() + fallbackExtent,
			center.y() - fallbackExtent, center.y() + fallbackExtent,
			center.z() - fallbackExtent, center.z() + fallbackExtent);
	}

	if (hasSkinnedBounds)
	{
		const float skinPad = (std::max)(static_cast<float>(skinnedBounds.getMaxDimension()) * 0.1f, 0.01f);
		skinnedBounds.setLimits(
			skinnedBounds.xMin() - skinPad, skinnedBounds.xMax() + skinPad,
			skinnedBounds.yMin() - skinPad, skinnedBounds.yMax() + skinPad,
			skinnedBounds.zMin() - skinPad, skinnedBounds.zMax() + skinPad);
		shadowCasterBounds.addBox(skinnedBounds);
	}

	float lsMinX =  std::numeric_limits<float>::max();
	float lsMaxX = -std::numeric_limits<float>::max();
	float lsMinY =  std::numeric_limits<float>::max();
	float lsMaxY = -std::numeric_limits<float>::max();
	float lsMinZ =  std::numeric_limits<float>::max();
	float lsMaxZ = -std::numeric_limits<float>::max();
	const QMatrix4x4 lightModel = lightView * _viewCtrl.modelMatrix();
	for (const QVector3D& corner : shadowCasterBounds.getCorners())
	{
		const QVector3D ls = lightModel.map(corner);
		lsMinX = (std::min)(lsMinX, ls.x());
		lsMaxX = (std::max)(lsMaxX, ls.x());
		lsMinY = (std::min)(lsMinY, ls.y());
		lsMaxY = (std::max)(lsMaxY, ls.y());
		lsMinZ = (std::min)(lsMinZ, ls.z());
		lsMaxZ = (std::max)(lsMaxZ, ls.z());
	}

	const float rawW = lsMaxX - lsMinX;
	const float rawH = lsMaxY - lsMinY;
	_renderCtrl.setShadowFrustumExtentW(rawW);
	_renderCtrl.setShadowFrustumExtentH(rawH);
	const float texelPadX = rawW / (std::max)(static_cast<float>(_renderCtrl.shadowWidth()), 1.0f) * 4.0f;
	const float texelPadY = rawH / (std::max)(static_cast<float>(_renderCtrl.shadowHeight()), 1.0f) * 4.0f;
	const float xyPad = (std::max)((std::max)(texelPadX, texelPadY), (std::max)(radius * 0.005f, 0.001f));
	const float nearDist = (std::max)(0.01f, -lsMaxZ);
	const float farDist = (std::max)(nearDist + 0.01f, -lsMinZ + radius * 1.5f);
	_renderCtrl.setShadowFarDist(farDist);

	lightProjection.ortho(
		lsMinX - xyPad, lsMaxX + xyPad,
		lsMinY - xyPad, lsMaxY + xyPad,
		nearDist, farDist
	);

	_lightSpaceMatrix = lightProjection * lightView;
	RenderableMesh::setCurrentRenderContext(_viewCtrl.modelMatrix(), lightView);

	// render scene from light's point of view
	_renderCtrl.shadowMappingShader()->bind();
	_renderCtrl.shadowMappingShader()->setUniformValue("lightSpaceMatrix", _lightSpaceMatrix);
	_renderCtrl.shadowMappingShader()->setUniformValue("model", _viewCtrl.modelMatrix());
	_renderCtrl.shadowMappingShader()->setProperty("globalModelMatrix", QVariant::fromValue(_viewCtrl.modelMatrix()));

	const float fsMinX = lsMinX - xyPad;
	const float fsMaxX = lsMaxX + xyPad;
	const float fsMinY = lsMinY - xyPad;
	const float fsMaxY = lsMaxY + xyPad;
	const float fsMinZ = -farDist;
	const float fsMaxZ = -nearDist;

	auto isMeshOutsideShadowVolume = [&](const SceneMesh* mesh) -> bool
	{
		if (!mesh)
			return true;

		const BoundingSphere sphere = mesh->getBoundingSphere();
		// sphere.getCenter() and sphere.getRadius() are in world space — they come from
		// the world-space bounding sphere from computeBounds() derives from _trsfPoints, and _trsfPoints
		// is already combinedRenderTransform() * raw_vertices.  Map to light space using
		// the same lightView * modelMatrix used for the shadow AABB above; do NOT apply
		// combinedRenderTransform() again or the center gets double-translated (position T
		// becomes 2T, landing outside the frustum that correctly spans T±r).
		const float radiusWithSlack = (std::max)(sphere.getRadius(), 0.001f) * 1.05f;
		const QVector3D centerLS = (lightView * _viewCtrl.modelMatrix()).map(sphere.getCenter());

		return centerLS.x() + radiusWithSlack < fsMinX ||
		       centerLS.x() - radiusWithSlack > fsMaxX ||
		       centerLS.y() + radiusWithSlack < fsMinY ||
		       centerLS.y() - radiusWithSlack > fsMaxY ||
		       centerLS.z() + radiusWithSlack < fsMinZ ||
		       centerLS.z() - radiusWithSlack > fsMaxZ;
	};

	if (_sceneRuntime.meshStore().size() != 0)
	{
		for (int i : visibleIds)
		{
			try
			{
				SceneMesh* mesh = _sceneRuntime.meshAt(i);
				if (mesh && isMeshAnimationVisible(mesh) && !isMeshOutsideShadowVolume(mesh))
				{
					mesh->setProg(_renderCtrl.shadowMappingShader());
					mesh->getVAO().bind();					
					mesh->renderShadow();
					mesh->getVAO().release();
				}
			}
			catch (const std::exception& ex)
			{
				std::cout << "Exception raised in ViewportWidget::renderToShadowBuffer\n" << ex.what() << std::endl;
			}
		}
	}

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, defaultFramebufferObject());
	// End Shadow Mapping
	// restore viewport
	glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

void ViewportWidget::renderQuad()
{
	_renderCtrl.renderQuad();
}

void ViewportWidget::renderMeshWithDisplayMode(SceneMesh* mesh, DisplayMode mode)
{
	QElapsedTimer meshModeTimer;
	const bool profiling = RenderableMesh::renderDiagnosticsEnabled();
	if (profiling)
		meshModeTimer.start();

	switch (mode)
	{
		// ============================================
	case DisplayMode::SHADED:
		// ============================================
		// SHADED: Solid rendering, optionally with flat shading geometry shader
		// ============================================
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glLineWidth(1.0f);
		glDisable(GL_POLYGON_OFFSET_FILL);
		if (_shadingNormalMode == ShadingNormalMode::FLAT &&
			_renderCtrl.fgFlatShader() && _renderCtrl.fgFlatShader()->isLinked() &&
			mesh->getPrimitiveMode() == GL_TRIANGLES &&
			mesh->prog() == _renderCtrl.fgShader())
		{
			RenderableMesh::bindProgramCached(_renderCtrl.fgFlatShader());
			mesh->setProg(_renderCtrl.fgFlatShader());
		}
		mesh->render();
		break;

		// ============================================
	case DisplayMode::HOLLOW_MESH:
		// ============================================
		// HOLLOW MESH: All triangle edges via glPolygonMode
		// ============================================
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glLineWidth(1.25f);
		glDisable(GL_POLYGON_OFFSET_FILL);
		mesh->render();
		break;

		// ============================================
	case DisplayMode::MESH_EDGES:
		// Pass 1: Bias fill depth slightly back so the line overlay can sit on top stably.
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glLineWidth(1.0f);
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(1.25f, 1.25f);
		_renderCtrl.fgShader()->setUniformValue("isWireframePass", false);
		mesh->render();
		glDisable(GL_POLYGON_OFFSET_FILL);

		// Pass 2: All triangle edges overlay at true depth.
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glLineWidth(1.5f);
		_renderCtrl.fgShader()->setUniformValue("isWireframePass", true);
		mesh->render();

		_renderCtrl.fgShader()->setUniformValue("isWireframePass", false);
		break;

		// ============================================
	case DisplayMode::WIREFRAME:
	case DisplayMode::SHADED_WITH_EDGES:
		// Feature-edge modes are handled at the render-loop level (they need
		// access to the wireframe shader and the feature-edge VAO via
		// renderFeatureEdgesFast). renderMeshWithDisplayMode is used for the
		// exploded-view and per-mesh paths; fall back to solid rendering there.
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glLineWidth(1.0f);
		glDisable(GL_POLYGON_OFFSET_FILL);
		mesh->render();
		break;

	default:
		// Safety fallback: solid rendering
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glLineWidth(1.0f);
		glDisable(GL_POLYGON_OFFSET_FILL);
		mesh->render();
		break;
	}

	// Reset to default state (important!)
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glLineWidth(1.0f);
	glDisable(GL_POLYGON_OFFSET_FILL);
	if (profiling)
		RenderableMesh::recordRenderMeshWithDisplayModeCpuMs(static_cast<double>(meshModeTimer.nsecsElapsed()) / 1000000.0);
}

void ViewportWidget::gradientBackground(float top_r, float top_g, float top_b, float top_a,
	float bot_r, float bot_g, float bot_b, float bot_a, int gradientStyle)
{
	glViewport(0, 0, width(), height());
	if (!_renderCtrl.bgVAO().isCreated())
	{
		_renderCtrl.bgVAO().create();
	}

	glDisable(GL_DEPTH_TEST);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	_renderCtrl.bgShader()->bind();		
	_renderCtrl.bgShader()->setUniformValue("top_color", QVector4D(top_r, top_g, top_b, top_a));
	_renderCtrl.bgShader()->setUniformValue("bot_color", QVector4D(bot_r, bot_g, bot_b, bot_a));
	_renderCtrl.bgShader()->setUniformValue("gradient_style", gradientStyle);  // Pass the gradient style

	_renderCtrl.bgVAO().bind();
	glDrawArrays(GL_TRIANGLES, 0, 3);

	glEnable(GL_DEPTH_TEST);

	_renderCtrl.bgVAO().release();
	_renderCtrl.bgShader()->release();
}

void ViewportWidget::loadBgColorSettings()
{
	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());

	// Retrieve and validate top color
	QVariant topColorValue = settings.value("Background/TopColor");
	if (topColorValue.isValid() && topColorValue.canConvert<QColor>())
		_renderCtrl.setBgTopColor(topColorValue.value<QColor>());
	else
		_renderCtrl.setBgTopColor(QColor::fromRgbF(0.45f, 0.45f, 0.45f, 1.0f));

	// Retrieve and validate bottom color
	QVariant bottomColorValue = settings.value("Background/BottomColor");
	if (bottomColorValue.isValid() && bottomColorValue.canConvert<QColor>())
		_renderCtrl.setBgBotColor(bottomColorValue.value<QColor>());
	else
		_renderCtrl.setBgBotColor(QColor::fromRgbF(0.9f, 0.9f, 0.9f, 1.0f));

	// Retrieve and validate gradient style
	QVariant gradientStyleValue = settings.value("Background/GradientStyle");
	if (gradientStyleValue.isValid() && gradientStyleValue.canConvert<int>())
	{
		int style = gradientStyleValue.toInt();
		if (style >= 0 && style <= 3)
		{
			_renderCtrl.setGradientStyle(style);
		}
		else
		{
			_renderCtrl.setGradientStyle(0); // Default to vertical gradient
		}
	}
	else
	{
		_renderCtrl.setGradientStyle(0); // Default to vertical gradient
	}

	// Background style mode: 0=Gradient, 1=Solid
	QVariant bgStyleValue = settings.value("Background/StyleIndex");
	_renderCtrl.setBgStyleIndex(
		bgStyleValue.isValid() && bgStyleValue.canConvert<int>() ? bgStyleValue.toInt() : 0);
}

void ViewportWidget::loadNavigationSettings()
{
	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());

	_invertZoom        = settings.value("checkInvertZoom", false).toBool();
	_invertYAxis       = settings.value("invertYAxisCheckBox", false).toBool();
	_smoothNavigation  = settings.value("smoothNavigationCheckBox", true).toBool();

	// Sliders run 1–10; 5 = neutral (1.0×). Map linearly: value/5.0.
	int mouseSens = settings.value("mouseSensitivitySlider", 5).toInt();
	_mouseSensitivity = std::clamp(mouseSens, 1, 10) / 5.0f;

	int wheelSens = settings.value("wheelSensitivitySlider", 5).toInt();
	_wheelSensitivity = std::clamp(wheelSens, 1, 10) / 5.0f;
}

void ViewportWidget::loadRenderSettings()
{
	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());

	// Backface culling — applied in render() GL state setup; no GL context needed
	_renderCtrl.setBackfaceCulling(settings.value("checkBackfaceCulling", false).toBool());

	if (!_renderCtrl.isOpenGLInitialized())
		return;

	// Shadows
	showShadows(settings.value("enableShadowsCheckBox", false).toBool());

	// Display mode: 0=Shaded, 1=Hollow Mesh, 2=Mesh Edges, 3=Wireframe, 4=Shaded with Edges
	// (1:1 with the DisplayMode enum order)
	const int displayIdx = std::clamp(settings.value("comboShadingMode", 0).toInt(), 0, 4);
	setDisplayMode(static_cast<DisplayMode>(displayIdx));

	// Rendering model: 0=Blinn-Phong (ADS), 1=PBR
	// Route through the same path as the toolbar (ModelViewer::onRenderingModeSelected)
	// so that PBR also enables environment mapping / IBL / HDRI sky, not just the equation.
	const int renderIdx = settings.value("shaderModelComboBox", 0).toInt();
	if (_viewer)
		_viewer->onRenderingModeSelected(renderIdx == 1 ? QStringLiteral("PBR") : QStringLiteral("ADS"));
	else
		setRenderingMode(renderIdx == 1 ? RenderingMode::PHYSICALLY_BASED_RENDERING
		                                 : RenderingMode::ADS_BLINN_PHONG);

	// Shading normal: 0=Smooth, 1=Flat
	const int shadingNormalIdx = settings.value("shadingNormalComboBox", 0).toInt();
	setShadingNormalMode(shadingNormalIdx == 1 ? ShadingNormalMode::FLAT
	                                            : ShadingNormalMode::SMOOTH);

	_renderCtrl.fgShader()->bind();

	// Lighting enable: maps to the default-lights flag
	const bool lightingOn = settings.value("enableLightingCheckBox", true).toBool();
	_renderCtrl.setUseDefaultLights(lightingOn);
	_renderCtrl.fgShader()->setUniformValue("useDefaultLights", lightingOn);

	// Ambient / diffuse / specular intensity sliders (0–100; 100 = full default colour)
	const int ambientVal  = std::clamp(settings.value("ambientLightSlider",  20).toInt(), 0, 100);
	const int diffuseVal  = std::clamp(settings.value("diffuseLightSlider",  80).toInt(), 0, 100);
	const int specularVal = std::clamp(settings.value("specularLightSlider", 50).toInt(), 0, 100);
	const QVector4D& dlc = _renderCtrl.defaultLightColor();
	_ambientLight  = dlc * (ambientVal  / 100.0f);
	_diffuseLight  = dlc * (diffuseVal  / 100.0f);
	_specularLight = dlc * (specularVal / 100.0f);
	_renderCtrl.fgShader()->setUniformValue("lightSource.ambient",  _ambientLight.toVector3D());
	_renderCtrl.fgShader()->setUniformValue("lightSource.diffuse",  _diffuseLight.toVector3D());
	_renderCtrl.fgShader()->setUniformValue("lightSource.specular", _specularLight.toVector3D());
	_renderCtrl.fgShader()->release();

	update();
}

ViewportWidget::CameraPose ViewportWidget::saveCameraPose() const
{
	return {
		_primaryCamera->getPosition(),
		_primaryCamera->getViewDir(),
		_primaryCamera->getUpVector(),
		_primaryCamera->getRightVector(),
		_viewCtrl.viewRange()
	};
}

void ViewportWidget::restoreCameraPose(const CameraPose& pose)
{
	_viewCtrl.setViewRange(pose.viewRange);
	_viewCtrl.setCurrentViewRange(pose.viewRange);
	_primaryCamera->setViewRange(pose.viewRange);
	_primaryCamera->setView(pose.position, pose.viewDir, pose.upVector, pose.rightVector);
	_viewCtrl.syncPoseFromCamera(*_primaryCamera);
	update();
	_rtInteractionCtrl->notifyCameraJumpNonInteractive();
}


void ViewportWidget::splitScreen()
{
	if (!_renderCtrl.bgSplitVAO().isCreated())
	{
		_renderCtrl.bgSplitVAO().create();
		_renderCtrl.bgSplitVAO().bind();
	}

	if (!_renderCtrl.bgSplitVBO().isCreated())
	{
		_renderCtrl.bgSplitVBO() = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
		_renderCtrl.bgSplitVBO().create();
		_renderCtrl.bgSplitVBO().bind();
		_renderCtrl.bgSplitVBO().setUsagePattern(QOpenGLBuffer::StaticDraw);

		static const std::vector<float> vertices = {
			-static_cast<float>(width()) / 2,
			0,
			static_cast<float>(width()) / 2,
			0,
			0,
			-static_cast<float>(height()) / 2,
			0,
			static_cast<float>(height()) / 2,
		};

		_renderCtrl.bgSplitVBO().allocate(vertices.data(), static_cast<int>(vertices.size() * sizeof(float)));

		_renderCtrl.bgSplitShader()->bind();
		_renderCtrl.bgSplitShader()->enableAttributeArray("vertexPosition");
		_renderCtrl.bgSplitShader()->setAttributeBuffer("vertexPosition", GL_FLOAT, 0, 2);

		_renderCtrl.bgSplitVBO().release();
	}

	glViewport(0, 0, width(), height());

	glDisable(GL_DEPTH_TEST);

	_renderCtrl.bgSplitVAO().bind();
	glLineWidth(0.5);
	glDrawArrays(GL_LINES, 0, 4);
	glLineWidth(1);

	glEnable(GL_DEPTH_TEST);

	_renderCtrl.bgSplitVAO().release();
	_renderCtrl.bgSplitShader()->release();
}

void ViewportWidget::setupClippingUniforms(QOpenGLShaderProgram* prog, QVector3D pos)
{
	prog->bind();
	RenderableMesh::recordProgramBindCall(true);
	if (_renderCtrl.yzClippingEnabled() || _renderCtrl.zxClippingEnabled() || _renderCtrl.xyClippingEnabled() || !(_renderCtrl.clipDX() == 0 && _renderCtrl.clipDY() == 0 && _renderCtrl.clipDZ() == 0))
	{
		prog->setUniformValue("sectionActive", true);
	}
	else
	{
		prog->setUniformValue("sectionActive", false);
	}
	prog->setUniformValue("modelViewMatrix", _viewCtrl.modelViewMatrix());
	prog->setUniformValue("projectionMatrix", _viewCtrl.projectionMatrix());
	prog->setUniformValue("clipPlaneX", QVector4D(_viewCtrl.modelViewMatrix().map(QVector3D(_renderCtrl.clippingXFlipped() ? 1 : -1, 0, 0) + pos),
		(_renderCtrl.clippingXFlipped() ? 1 : -1) * (pos.x() - (_renderCtrl.clippingXCoeff() + _viewCtrl.boundingBox().center().getX()))));
	prog->setUniformValue("clipPlaneY", QVector4D(_viewCtrl.modelViewMatrix().map(QVector3D(0, _renderCtrl.clippingYFlipped() ? 1 : -1, 0) + pos),
		(_renderCtrl.clippingYFlipped() ? 1 : -1) * (pos.y() - (_renderCtrl.clippingYCoeff() + _viewCtrl.boundingBox().center().getY()))));
	prog->setUniformValue("clipPlaneZ", QVector4D(_viewCtrl.modelViewMatrix().map(QVector3D(0, 0, _renderCtrl.clippingZFlipped() ? 1 : -1) + pos),
		(_renderCtrl.clippingZFlipped() ? 1 : -1) * (pos.z() - (_renderCtrl.clippingZCoeff() + _viewCtrl.boundingBox().center().getZ()))));
	prog->setUniformValue("clipPlane", QVector4D(_viewCtrl.modelViewMatrix().map(QVector3D(_renderCtrl.clipDX(), _renderCtrl.clipDY(), _renderCtrl.clipDZ()) + pos),
		pos.x() * _renderCtrl.clipDX() + pos.y() * _renderCtrl.clipDY() + pos.z() * _renderCtrl.clipDZ()));
}


SceneMesh* ViewportWidget::createMeshFromData(const AssImpMeshData& meshData)
{
	return AssImpMeshBuilder::build(meshData, _renderCtrl.fgShader(), this);
}

void ViewportWidget::syncFileNodeTransforms(const QString& sourceFile)
{
	if (!_viewer || !_viewer->sceneGraph())
		return;

	SceneGraph* sceneGraph = _viewer->sceneGraph();
	SceneNode* fileNode = sceneGraph->findFileNode(sourceFile);
	if (!fileNode)
		return;

	const SceneGraphWorldTransforms evaluatedWorlds = sceneGraph->evaluateWorldTransformsForFile(sourceFile);

	RuntimeAnimationFileState& runtime = _animCtrl.runtimeAnimationsByFile()[sourceFile];
	runtime.data = sceneGraph->animationDataForFile(sourceFile);
	runtime.defaultNodeTransformsByUuid.clear();
	runtime.defaultNodeMorphWeightsByUuid.clear();
	runtime.defaultMeshMaterials.clear();
	runtime.meshUuidsByMaterialIndex.clear();
	runtime.nodeUuidByName.clear();
	runtime.nodeUuidByIndex.clear();
	runtime.nodeIndexByUuid.clear();

	SceneNode* aiRootNode = fileNode->children.isEmpty() ? nullptr : fileNode->children.first();
	for (const GltfAnimationNodeBinding& binding : runtime.data.nodeBindings)
	{
		SceneNode* targetNode = nullptr;
		if (binding.hasAiChildPath)
			targetNode = findSceneNodeByAiChildPath(aiRootNode, binding.aiChildPath);

		if (!targetNode && !binding.nodeName.isEmpty())
		{
			std::function<SceneNode*(SceneNode*)> findByName = [&](SceneNode* node) -> SceneNode*
			{
				if (!node)
					return nullptr;
				if (node->name == binding.nodeName)
					return node;
				for (SceneNode* child : node->children)
				{
					if (SceneNode* match = findByName(child))
						return match;
				}
				return nullptr;
			};

			for (SceneNode* child : fileNode->children)
			{
				targetNode = findByName(child);
				if (targetNode)
					break;
			}
		}

		if (targetNode && binding.nodeIndex >= 0 && !runtime.nodeUuidByIndex.contains(binding.nodeIndex))
		{
			runtime.nodeUuidByIndex.insert(binding.nodeIndex, targetNode->nodeUuid);
			runtime.nodeIndexByUuid.insert(targetNode->nodeUuid, binding.nodeIndex);
		}
	}

	std::function<void(SceneNode*)> collect = [&](SceneNode* node)
	{
		if (!node)
			return;

		runtime.defaultNodeTransformsByUuid.insert(node->nodeUuid, AnimationRuntimeController::decomposeNodeTransform(node->localTransform));
		if (!node->name.isEmpty() && !runtime.nodeUuidByName.contains(node->name))
			runtime.nodeUuidByName.insert(node->name, node->nodeUuid);
		for (const QUuid& uuid : node->meshUuids)
		{
			if (SceneMesh* mesh = getMeshByUuid(uuid))
			{
				if (evaluatedWorlds.meshWorldByUuid.contains(uuid))
					mesh->setSceneRenderTransform(evaluatedWorlds.meshWorldByUuid.value(uuid));
				if (mesh->hasMorphTargets())
				{
					// Assimp appends "_node" to mesh-owning nodes when re-importing a glTF/GLB
					// whose node name matches its mesh name (e.g. "AnimatedMorphCube" → child
					// "AnimatedMorphCube_node" owns the mesh).  The morph-weight animation
					// targets the PARENT ("AnimatedMorphCube") in the JSON.  Use the parent
					// UUID so that the animation lookup resolves correctly.
					const SceneNode* morphNode = node;
					if (node->parent
					    && node->name.endsWith(QStringLiteral("_node"))
					    && node->parent->name == node->name.left(node->name.length() - 5))
					{
						morphNode = node->parent;
					}
					if (!runtime.defaultNodeMorphWeightsByUuid.contains(morphNode->nodeUuid))
						runtime.defaultNodeMorphWeightsByUuid.insert(morphNode->nodeUuid, mesh->defaultMorphWeights());
				}
				runtime.defaultMeshMaterials.insert(uuid, mesh->getMaterial());
				if (mesh->getOriginalMaterialIndex() >= 0)
					runtime.meshUuidsByMaterialIndex.insert(mesh->getOriginalMaterialIndex(), uuid);
			}
		}

		for (SceneNode* child : node->children)
			collect(child);
	};

	for (SceneNode* child : fileNode->children)
		collect(child);
}

void ViewportWidget::reapplyGltfCameraAfterTransform()
{
	if (!isGltfCameraActive() || !_viewer)
		return;

	const GltfCameraData camData =
		_viewer->sceneGraph()->gltfCameraDataForFile(_animCtrl.activeGltfCameraFile());
	if (_animCtrl.activeGltfCameraIndex() < 0 ||
		_animCtrl.activeGltfCameraIndex() >= camData.cameras.size())
	{
		return;
	}

	const GltfCameraEntry& cam = camData.cameras[_animCtrl.activeGltfCameraIndex()];
	applyGltfCameraEntryTransform(cam);

	const bool animationOwnsThisFile =
		_animCtrl.activeAnimationFile() == _animCtrl.activeGltfCameraFile() &&
		_animCtrl.activeAnimationClip() >= 0;
	if (animationOwnsThisFile)
	{
		// applyAnimationPose() already notifies once, unconditionally, at its
		// own end - see applyGltfCameraEntryTransform()'s doc comment for why
		// this call must NOT also notify itself when that's about to happen.
		applyAnimationPose(_animCtrl.activeAnimationFile(),
			_animCtrl.activeAnimationClip(),
			_animCtrl.animationCurrentTimeSeconds());
	}
	else
	{
		// Genuine one-shot jump - no animation clip will notify on this
		// call's behalf, so this is the only place that will. No explicit
		// update() needed - applyGltfCameraEntryTransform()'s own resizeGL()
		// call already repaints unconditionally.
		_rtInteractionCtrl->notifyCameraJumpNonInteractive();
	}
}

void ViewportWidget::setActiveAnimation(const QString& sourceFile, int clipIndex)
{
	if (!_viewer || !_viewer->sceneGraph())
		return;

	const GltfAnimationData data = _viewer->sceneGraph()->animationDataForFile(sourceFile);
	if (clipIndex < 0 || clipIndex >= data.clips.size())
		return;

	// Rebuild runtime defaults from the authoritative SceneGraph state before
	// sampling frame 0. Newly-created clips can otherwise be applied against a
	// stale runtime cache, which makes meshes appear to "stick" in the wrong pose.
	syncFileNodeTransforms(sourceFile);

	_animCtrl.setActiveAnimationFile(sourceFile);
	_animCtrl.setActiveAnimationClip(clipIndex);
	_animCtrl.setAnimationCurrentTimeSeconds(0.0);
	_animCtrl.setPlaying(false);
	_animCtrl.animationTimer()->stop();
	_viewer->sceneGraph()->setActiveAnimationClip(sourceFile, clipIndex);
	applyAnimationPose(sourceFile, clipIndex, 0.0);
	emit animationStateChanged();
}

void ViewportWidget::clearAnimationRuntimeForFile(const QString& sourceFile)
{
	const bool wasActive = (_animCtrl.activeAnimationFile() == sourceFile);
	_animCtrl.removeAnimationFile(sourceFile);
	if (wasActive)
	{
		_animCtrl.animationTimer()->stop();
		_animCtrl.resetPlayback();

		// If other animated files remain, activate the first one so their
		// animation continues rather than going dark.
		const QStringList remaining = _viewer && _viewer->sceneGraph()
			? _viewer->sceneGraph()->filesWithAnimations()
			: QStringList();
		if (!remaining.isEmpty())
		{
			const QString& nextFile = remaining.first();
			const GltfAnimationData data = _viewer->sceneGraph()->animationDataForFile(nextFile);
			if (!data.clips.isEmpty())
				setActiveAnimation(nextFile, 0);
			else
				emit animationStateChanged();
		}
		else
		{
			emit animationStateChanged();
		}
	}
}

void ViewportWidget::syncRuntimeNodeTransforms(const QString& sourceFile)
{
	syncFileNodeTransforms(sourceFile);
}

void ViewportWidget::setAnimationPlaying(bool playing)
{
	if (playing) _animCtrl.resumePlayback();
	else         _animCtrl.pausePlayback();
	emit animationStateChanged();
}

void ViewportWidget::seekAnimation(double timeSeconds)
{
	if (_animCtrl.activeAnimationFile().isEmpty() || _animCtrl.activeAnimationClip() < 0)
		return;

	_animCtrl.setAnimationCurrentTimeSeconds(std::max(0.0, timeSeconds));
	applyAnimationPose(_animCtrl.activeAnimationFile(), _animCtrl.activeAnimationClip(), _animCtrl.animationCurrentTimeSeconds());
	emit animationStateChanged();
}

void ViewportWidget::setAnimationPlaybackSpeed(double speed)
{
	_animCtrl.applyPlaybackSpeed(speed);
	emit animationStateChanged();
}

// ---------------------------------------------------------------------------
// glTF camera switching
// ---------------------------------------------------------------------------

void ViewportWidget::activateGltfCamera(const QString& sourceFile, int cameraIndex)
{
	if (!_viewer || !_primaryCamera)
		return;

	const GltfCameraData cd = _viewer->sceneGraph()->gltfCameraDataForFile(sourceFile);
	if (cameraIndex < 0 || cameraIndex >= cd.cameras.size())
		return;

	const GltfCameraEntry& cam = cd.cameras[cameraIndex];

	// Save the current system camera state before the first glTF switch so the
	// user can get back to exactly where they were.
	if (!_viewCtrl.systemCameraStateSaved())
	{
		_viewCtrl.saveSystemCameraState(*_primaryCamera);
	}

	_animCtrl.setActiveGltfCamera(sourceFile, cameraIndex);

	// Always apply the static camera position/FOV from the parsed glTF camera
	// entry.  This handles the common case of cameras with no animation
	// (e.g. AnimationPointerUVs, where cameras have static matrix transforms
	// but are not targeted by any animation channel).
	applyGltfCameraEntryTransform(cam);

	// If an animation clip is active for this file, re-run applyAnimationPose
	// so that animated cameras (e.g. DiffuseTransmissionPlant) get overridden
	// by the animation's worldTransforms.  For static (non-animated) cameras,
	// applyAnimationPose's worldTransformsByNodeUuid won't contain the camera
	// node UUID and the view set above is preserved unchanged.
	const bool animationOwnsThisFile =
		_animCtrl.activeAnimationFile() == sourceFile && _animCtrl.activeAnimationClip() >= 0;
	if (animationOwnsThisFile)
	{
		// applyAnimationPose() already notifies once, unconditionally, at its
		// own end - see applyGltfCameraEntryTransform()'s doc comment for why
		// this call must NOT also notify itself when that's about to happen.
		applyAnimationPose(sourceFile, _animCtrl.activeAnimationClip(), _animCtrl.animationCurrentTimeSeconds());
	}
	else
	{
		// Genuine one-shot jump - no animation clip will notify on this
		// call's behalf, so this is the only place that will.
		// No explicit update() needed - applyGltfCameraEntryTransform()'s own
		// resizeGL() call already repainted unconditionally.
		_rtInteractionCtrl->notifyCameraJumpNonInteractive();
	}
}

void ViewportWidget::resetToSystemCamera()
{
	if (_viewCtrl.systemCameraStateSaved())
	{
		_viewCtrl.restoreSystemCameraState(*_primaryCamera);
		_viewCtrl.setViewRange(_viewCtrl.savedCameraViewRange());
		_viewCtrl.syncCurrentViewRange();
		_viewCtrl.setProjection((_viewCtrl.savedProjectionType() == Camera::ProjectionType::PERSPECTIVE)
			? ViewProjection::PERSPECTIVE
			: ViewProjection::ORTHOGRAPHIC);
		_viewCtrl.setPreviousProjection(_viewCtrl.savedProjectionType());
		_viewCtrl.clearSystemCameraState();
	}

	_animCtrl.setActiveGltfCamera(QString(), -1);

	update();
	_rtInteractionCtrl->notifyCameraJumpNonInteractive();
}

GltfCameraEntry ViewportWidget::captureCurrentCameraEntry(const QString& name) const
{
	GltfCameraEntry entry;
	entry.name = name;
	entry.nodeName = name;
	entry.needsNewNode = true;
	entry.needsModelTransformCompensation = false;

	if (_primaryCamera)
	{
		// _primaryCamera->getProjectionType() is NOT the authoritative
		// answer here: the toolbar's Perspective/Ortho toggle
		// (ViewportWidget::setProjection()) only ever updates
		// _viewCtrl.setProjection() - it never touches _primaryCamera's own
		// projectionType field, which stays wherever applyGltfCameraEntryTransform()/
		// resetToSystemCamera() last explicitly set it (typically Perspective,
		// its default). resizeGL() and the rest of the render path all read
		// _viewCtrl.projection() to decide what's actually on screen, so
		// that's what a capture needs to match too.
		entry.type = (_viewCtrl.projection() == ViewProjection::PERSPECTIVE)
			? GltfCameraType::Perspective
			: GltfCameraType::Orthographic;
		entry.fovYRadians = qDegreesToRadians(_primaryCamera->getFOV());

		// xMag/yMag are the spec-correct glTF orthographic half-extents
		// (also what gets exported), but capturedViewRange is what
		// applyGltfCameraEntryTransform() actually uses to restore this
		// exact pose in either projection - see GltfCameraData.h.
		const float viewRange = _primaryCamera->getViewRange();
		entry.xMag = std::max(viewRange * 0.5f, 0.0001f);
		entry.yMag = entry.xMag;
		entry.capturedViewRange = viewRange;

		// getPosition() is NOT the eye position in Orbit mode - it's the
		// look-at pivot/target (Camera::updateViewMatrix() explicitly uses
		// getRenderPosition() as eye and _position as the lookAt point for
		// CameraMode::Orbit). worldPosition here means true eye position,
		// matching how an authored glTF camera's node transform works and
		// what applyGltfCameraEntryTransform()'s pivotPos = worldPos +
		// worldDir*orbitDist reconstruction expects - feeding it the pivot
		// instead double-shifts the math and lands the reactivated camera
		// back at the original look-at target instead of where it was
		// actually standing. This also matters for export: the node
		// GltfPostProcessor::writeGltfCameras() creates uses worldPosition
		// as a literal glTF node translation, which external viewers read
		// as eye position with no concept of an "orbit pivot" at all.
		entry.worldPosition  = _primaryCamera->getRenderPosition();
		entry.worldDirection = _primaryCamera->getViewDir();
		entry.worldUp        = _primaryCamera->getUpVector();
	}

	return entry;
}

// ---------------------------------------------------------------------------
// Measurement tool
// ---------------------------------------------------------------------------

void ViewportWidget::setMeasurementTool(MeasurementTool tool)
{
	if (_measurementTool == tool)
		return;

	const bool wasArmed = (_measurementTool != MeasurementTool::None);
	const bool nowArmed = (tool != MeasurementTool::None);

	if (_selectionManager)
	{
		if (nowArmed && !wasArmed)
		{
			// Suppress the normal whole-mesh hover highlight while a tool is
			// armed - it's ambiguous (doesn't say WHERE a click will land).
			// mouseMoveEvent() shows the actual snap-able point instead via
			// _measurementHoverAnchor. Reuses the existing hover-mode
			// mechanism rather than a parallel "don't highlight" flag, and
			// its own Disabled-transition handling clears any highlight
			// that was already showing right when the tool got armed.
			_savedHoverHighlightModeBeforeMeasurement = _selectionManager->getHoverMode();
			_selectionManager->setHoverHighlightMode(HoverHighlightMode::Disabled);
		}
		else if (!nowArmed && wasArmed)
		{
			_selectionManager->setHoverHighlightMode(_savedHoverHighlightModeBeforeMeasurement);
		}
	}

	_measurementTool = tool;
	_pendingMeasurementAnchors.clear();
	_measurementClickCandidate = false;
	_measurementHoverAnchor = MeshSurfaceAnchor();
	_measurementEdgeHoverAnchor = MeshEdgeCircleAnchor();
	_measurementEdgeHoverIsCenterPick = false;
	_hoveredMeasurementId = QUuid();
	update();
	emit measurementToolChanged(_measurementTool);
	emit measurementProgressChanged(0, measurementToolRequiredAnchorCount(_measurementTool));
}

QVector3D ViewportWidget::resolveMeasurementAnchor(const MeasurementAnchorRef& ref) const
{
	if (ref.edgeIndex >= 0)
	{
		// A circular-edge-derived point anchor - any arbitrary-point pick
		// (Point, Distance, both arc tools, Point-to-Face's point anchor,
		// Edge-to-Vertex's vertex anchor) snapped to a nearby circular OCC
		// edge's exact analytic center instead of requiring a literal
		// surface hit at that position (a through-hole's center is empty
		// space - see SelectionManager::pickCircularEdgeCenterAnchor()'s
		// doc comment). Falls through to the ordinary triangle/vertex
		// resolution below if this isn't actually a circle (shouldn't
		// happen in practice, since only pickCircularEdgeCenterAnchor()
		// ever produces this kind of anchor, but a saved measurement could
		// in principle outlive a mesh reload that changes topology) - that
		// path correctly returns a null QVector3D since triangleIndex/
		// snappedVertexIndex are also unset for a pure edge anchor.
		QVector3D center, axis;
		float radius = 0.0f;
		if (resolveMeasurementEdgeCircle(ref, center, axis, radius))
			return center;
	}

	SceneMesh* mesh = getMeshByUuid(ref.meshUuid);
	if (!mesh)
		return QVector3D();

	const std::vector<float>& trsfPoints = mesh->getTrsfPoints();
	auto vertexPos = [&trsfPoints](int vIdx) -> QVector3D {
		if (vIdx < 0)
			return QVector3D();
		const size_t p = static_cast<size_t>(vIdx) * 3;
		if (p + 2 >= trsfPoints.size())
			return QVector3D();
		return QVector3D(trsfPoints[p], trsfPoints[p + 1], trsfPoints[p + 2]);
	};

	if (ref.snappedVertexIndex >= 0)
		return vertexPos(ref.snappedVertexIndex);

	if (ref.triangleIndex < 0)
		return QVector3D();

	const std::vector<unsigned int> indices = mesh->indices();
	const size_t base = static_cast<size_t>(ref.triangleIndex) * 3;
	if (base + 2 >= indices.size())
		return QVector3D();

	const QVector3D p0 = vertexPos(static_cast<int>(indices[base]));
	const QVector3D p1 = vertexPos(static_cast<int>(indices[base + 1]));
	const QVector3D p2 = vertexPos(static_cast<int>(indices[base + 2]));
	return p0 * ref.barycentric.x() + p1 * ref.barycentric.y() + p2 * ref.barycentric.z();
}

QString ViewportWidget::measurementSummaryText(const Measurement& m) const
{
	switch (m.type)
	{
	case MeasurementType::Point:
	{
		if (m.anchors.isEmpty())
			return QString();
		const QVector3D p = resolveMeasurementAnchor(m.anchors[0]);
		return tr("Point: (%1, %2, %3)")
			.arg(p.x(), 0, 'f', 3).arg(p.y(), 0, 'f', 3).arg(p.z(), 0, 'f', 3);
	}
	case MeasurementType::Distance:
	{
		if (m.anchors.size() < 2)
			return QString();
		const QVector3D a = resolveMeasurementAnchor(m.anchors[0]);
		const QVector3D b = resolveMeasurementAnchor(m.anchors[1]);
		return tr("Distance: %1").arg(a.distanceToPoint(b), 0, 'f', 3);
	}
	case MeasurementType::ArcRadius3Point:
	{
		if (m.anchors.size() < 3)
			return QString();
		const QVector3D p0 = resolveMeasurementAnchor(m.anchors[0]);
		const QVector3D p1 = resolveMeasurementAnchor(m.anchors[1]);
		const QVector3D p2 = resolveMeasurementAnchor(m.anchors[2]);
		QVector3D center, normal;
		float radius = 0.0f;
		if (!MeasurementGeometry::circumcircle3Point(p0, p1, p2, center, normal, radius))
			return tr("3-Point Arc Radius: (degenerate - points are collinear)");
		return tr("3-Point Arc Radius: %1").arg(radius, 0, 'f', 3);
	}
	case MeasurementType::ArcRadiusCenterPoint:
	{
		if (m.anchors.size() < 3)
			return QString();
		const QVector3D center = resolveMeasurementAnchor(m.anchors[0]);
		const QVector3D p1 = resolveMeasurementAnchor(m.anchors[1]);
		const QVector3D p2 = resolveMeasurementAnchor(m.anchors[2]);
		QVector3D normal;
		float radius = 0.0f;
		if (!MeasurementGeometry::circleFromCenterAndTwoPoints(center, p1, p2, normal, radius))
			return tr("Center + 2-Point Arc Radius: (degenerate - points are collinear)");
		return tr("Center + 2-Point Arc Radius: %1").arg(radius, 0, 'f', 3);
	}
	case MeasurementType::EdgeRadius:
	{
		if (m.anchors.isEmpty())
			return QString();
		QVector3D center, axis;
		float radius = 0.0f;
		if (!resolveMeasurementEdgeCircle(m.anchors[0], center, axis, radius))
			return tr("Edge Radius: (edge no longer available)");
		return tr("Edge Radius: %1").arg(radius, 0, 'f', 3);
	}
	case MeasurementType::FaceToFace:
	{
		if (m.anchors.size() < 2)
			return QString();
		QVector3D p1, n1, p2, n2;
		if (!resolveMeasurementAnchorPlane(m.anchors[0], p1, n1) || !resolveMeasurementAnchorPlane(m.anchors[1], p2, n2))
			return tr("Face to Face: (face no longer available)");
		const MeasurementGeometry::FaceToFaceResult result = MeasurementGeometry::compareFaces(p1, n1, p2, n2);
		return result.isParallel
			? tr("Face to Face: %1").arg(result.distance, 0, 'f', 3)
			: tr("Face to Face: %1°").arg(result.angleDegrees, 0, 'f', 2);
	}
	case MeasurementType::PointToFace:
	{
		if (m.anchors.size() < 2)
			return QString();
		const QVector3D point = resolveMeasurementAnchor(m.anchors[0]);
		QVector3D facePos, faceNormal;
		if (!resolveMeasurementAnchorPlane(m.anchors[1], facePos, faceNormal))
			return tr("Point to Face: (face no longer available)");
		return tr("Point to Face: %1").arg(MeasurementGeometry::pointToPlaneDistance(point, facePos, faceNormal), 0, 'f', 3);
	}
	case MeasurementType::EdgeLength:
	{
		if (m.anchors.isEmpty())
			return QString();
		QVector3D start, end;
		float length = 0.0f;
		if (!resolveMeasurementEdgeGeometry(m.anchors[0], start, end, length))
			return tr("Edge Length: (edge no longer available)");
		return tr("Edge Length: %1").arg(length, 0, 'f', 3);
	}
	case MeasurementType::EdgeToVertex:
	{
		if (m.anchors.size() < 2)
			return QString();
		QVector3D edgeStart, edgeEnd;
		float edgeLength = 0.0f;
		if (!resolveMeasurementEdgeGeometry(m.anchors[0], edgeStart, edgeEnd, edgeLength))
			return tr("Edge to Vertex: (edge no longer available)");
		const QVector3D point = resolveMeasurementAnchor(m.anchors[1]);
		return tr("Edge to Vertex: %1").arg(
			MeasurementGeometry::pointToLineDistance(point, edgeStart, edgeEnd - edgeStart), 0, 'f', 3);
	}
	case MeasurementType::EdgeToEdge:
	{
		if (m.anchors.size() < 2)
			return QString();
		QVector3D start1, end1, start2, end2;
		float len1 = 0.0f, len2 = 0.0f;
		if (!resolveMeasurementEdgeGeometry(m.anchors[0], start1, end1, len1)
			|| !resolveMeasurementEdgeGeometry(m.anchors[1], start2, end2, len2))
			return tr("Edge to Edge: (edge no longer available)");
		const MeasurementGeometry::EdgeToEdgeResult result =
			MeasurementGeometry::compareLines(start1, end1 - start1, start2, end2 - start2);
		return result.isParallel
			? tr("Edge to Edge: %1").arg(result.distance, 0, 'f', 3)
			: tr("Edge to Edge: %1°").arg(result.angleDegrees, 0, 'f', 2);
	}
	case MeasurementType::EdgeToFace:
	{
		if (m.anchors.size() < 2)
			return QString();
		QVector3D edgeStart, edgeEnd;
		float edgeLength = 0.0f;
		if (!resolveMeasurementEdgeGeometry(m.anchors[0], edgeStart, edgeEnd, edgeLength))
			return tr("Edge to Face: (edge no longer available)");
		QVector3D facePos, faceNormal;
		if (!resolveMeasurementAnchorPlane(m.anchors[1], facePos, faceNormal))
			return tr("Edge to Face: (face no longer available)");
		const MeasurementGeometry::EdgeToFaceResult result =
			MeasurementGeometry::compareEdgeToFace(edgeStart, edgeEnd - edgeStart, facePos, faceNormal);
		return result.isParallel
			? tr("Edge to Face: %1").arg(result.distance, 0, 'f', 3)
			: tr("Edge to Face: %1°").arg(result.angleDegrees, 0, 'f', 2);
	}
	case MeasurementType::PitchCircle:
	{
		if (m.anchors.size() < 3)
			return QString();
		QVector<QVector3D> points;
		points.reserve(m.anchors.size());
		for (const MeasurementAnchorRef& a : m.anchors)
			points.append(resolveMeasurementAnchor(a));
		const MeasurementGeometry::PitchCircleResult result = MeasurementGeometry::fitPitchCircle(points);
		if (!result.valid)
			return tr("Pitch Circle: (degenerate - points are collinear or coincident)");

		const int n = result.gapAnglesDegrees.size();
		float minGap = result.gapAnglesDegrees.first();
		float maxGap = minGap;
		for (float g : result.gapAnglesDegrees)
		{
			minGap = std::min(minGap, g);
			maxGap = std::max(maxGap, g);
		}
		// Tight on purpose: a bolt pattern can be deliberately keyed with
		// one hole shifted a few degrees so the part only assembles one
		// way - a loose tolerance would silently call that "uniform" and
		// hide exactly the thing this measurement exists to catch.
		constexpr float kUniformToleranceDegrees = 0.5f;
		const float diameter = result.radius * 2.0f;

		// Always a headline (diameter + hole count) plus a spacing detail
		// line, joined with '\n' - two short lines read far better than
		// one long one, both as a floating 3D label (see
		// drawMeasurementOverlay()'s label loop, which splits on '\n') and
		// as the Measurement dialog's list row (Qt's default item drawing
		// already honors an embedded '\n' as a line break).
		const QString headline = tr("Pitch Circle: %1, %2 holes").arg(diameter, 0, 'f', 3).arg(n);

		if (maxGap - minGap <= kUniformToleranceDegrees)
			return headline + "\n" + tr("@ %1° spacing").arg(360.0f / static_cast<float>(n), 0, 'f', 2);

		// Itemized gaps read fine for a small pattern but would grow
		// unbounded with hole count - a compact min-max range takes over
		// past a handful of holes.
		if (n <= 6)
		{
			QStringList gapStrs;
			for (float g : result.gapAnglesDegrees)
				gapStrs << QString::number(g, 'f', 2) + QChar(0xB0);
			return headline + "\n" + tr("gaps: %1").arg(gapStrs.join(", "));
		}

		return headline + "\n" + tr("gaps %1°-%2° (not uniform)").arg(minGap, 0, 'f', 2).arg(maxGap, 0, 'f', 2);
	}
	case MeasurementType::Concentricity:
	{
		if (m.anchors.size() < 2)
			return QString();
		QVector3D center1, axis1, center2, axis2;
		float radius1 = 0.0f, radius2 = 0.0f;
		if (!resolveMeasurementEdgeCircle(m.anchors[0], center1, axis1, radius1)
			|| !resolveMeasurementEdgeCircle(m.anchors[1], center2, axis2, radius2))
			return tr("Concentricity: (circle no longer available)");
		const MeasurementGeometry::ConcentricityResult result =
			MeasurementGeometry::compareCircles(center1, axis1, center2, axis2);
		return tr("Concentricity: %1 offset, %2° axis").arg(result.centerOffset, 0, 'f', 3).arg(result.axisAngleDegrees, 0, 'f', 2);
	}
	case MeasurementType::AngleThreePoint:
	{
		if (m.anchors.size() < 3)
			return QString();
		const QVector3D vertex = resolveMeasurementAnchor(m.anchors[0]);
		const QVector3D p1 = resolveMeasurementAnchor(m.anchors[1]);
		const QVector3D p2 = resolveMeasurementAnchor(m.anchors[2]);
		if ((p1 - vertex).lengthSquared() < 1.0e-8f || (p2 - vertex).lengthSquared() < 1.0e-8f)
			return tr("3-Point Angle: (degenerate - a ray point coincides with the vertex)");
		return tr("3-Point Angle: %1°").arg(MeasurementGeometry::angleBetweenRays(vertex, p1, p2), 0, 'f', 2);
	}
	case MeasurementType::EdgeChain:
	{
		if (m.anchors.size() < 2)
			return QString();
		float total = 0.0f;
		int resolvedCount = 0;
		for (const MeasurementAnchorRef& a : m.anchors)
		{
			QVector3D start, end;
			float length = 0.0f;
			if (resolveMeasurementEdgeGeometry(a, start, end, length))
			{
				total += length;
				++resolvedCount;
			}
		}
		if (resolvedCount == 0)
			return tr("Chain Length: (no edges available)");
		if (resolvedCount < m.anchors.size())
			return tr("Chain Length: %1, %2 of %3 edges available").arg(total, 0, 'f', 3).arg(resolvedCount).arg(m.anchors.size());
		return tr("Chain Length: %1, %2 edges").arg(total, 0, 'f', 3).arg(resolvedCount);
	}
	}
	return QString();
}

bool ViewportWidget::resolveMeasurementEdgeCircle(const MeasurementAnchorRef& ref,
	QVector3D& outCenter, QVector3D& outAxis, float& outRadius) const
{
	if (ref.edgeIndex < 0)
		return false;

	SceneMesh* mesh = getMeshByUuid(ref.meshUuid);
	if (!mesh)
		return false;

	const std::vector<OccEdgeCircleInfo>& circles = mesh->getOccEdgeCircles();
	if (ref.edgeIndex >= static_cast<int>(circles.size()) || !circles[ref.edgeIndex].isCircle)
		return false;

	const OccEdgeCircleInfo& c = circles[ref.edgeIndex];
	const QVector3D centerLocal(static_cast<float>(c.centerX), static_cast<float>(c.centerY), static_cast<float>(c.centerZ));
	const QVector3D axisLocal(static_cast<float>(c.axisX), static_cast<float>(c.axisY), static_cast<float>(c.axisZ));

	// Derive the world-space center/axis/radius purely via the mesh's
	// current combinedRenderTransform() - the same matrix getTrsfPoints()
	// uses for every other measurement anchor, so this stays correct under
	// mesh transform-panel edits/exploded view exactly like they do. Radius
	// is recovered by transforming an arbitrary in-plane rim point too and
	// measuring its distance from the transformed center, rather than just
	// scaling the local radius by a scalar - exact for the common uniform-
	// scale case, and degrades gracefully (an "effective" radius) under a
	// non-uniform scale, where a true circle wouldn't stay a circle anyway.
	const QVector3D axisNormalizedLocal = axisLocal.normalized();
	const QVector3D reference = (std::abs(QVector3D::dotProduct(axisNormalizedLocal, QVector3D(0.0f, 1.0f, 0.0f))) < 0.9f)
		? QVector3D(0.0f, 1.0f, 0.0f)
		: QVector3D(1.0f, 0.0f, 0.0f);
	const QVector3D u = QVector3D::crossProduct(axisNormalizedLocal, reference).normalized();
	const QVector3D rimLocal = centerLocal + u * static_cast<float>(c.radius);

	const QMatrix4x4 combined = mesh->combinedRenderTransform();
	outCenter = combined.map(centerLocal);
	const QVector3D rimWorld = combined.map(rimLocal);
	outRadius = outCenter.distanceToPoint(rimWorld);
	outAxis = combined.mapVector(axisLocal).normalized();
	return true;
}

bool ViewportWidget::resolveMeasurementEdgeGeometry(const MeasurementAnchorRef& ref,
	QVector3D& outStart, QVector3D& outEnd, float& outLength) const
{
	if (ref.edgeIndex < 0)
		return false;

	SceneMesh* mesh = getMeshByUuid(ref.meshUuid);
	if (!mesh)
		return false;

	const std::vector<int>& occBounds = mesh->getOccEdgeBoundaries();
	if (!occBounds.empty())
	{
		// CAD mesh - sum the OCC edge's tessellated segment lengths (works
		// for any curve type, no classification needed).
		if (ref.edgeIndex + 1 >= static_cast<int>(occBounds.size()))
			return false;

		const std::vector<float>& segments = mesh->getOccEdgeSegments();
		const int startVec3 = occBounds[ref.edgeIndex];
		const int endVec3 = occBounds[ref.edgeIndex + 1];
		if (startVec3 < 0 || endVec3 <= startVec3 || static_cast<size_t>(endVec3) * 3 > segments.size())
			return false;

		const QMatrix4x4 combined = mesh->combinedRenderTransform();
		auto worldPointAt = [&](int v) -> QVector3D {
			const size_t p = static_cast<size_t>(v) * 3;
			return combined.map(QVector3D(segments[p], segments[p + 1], segments[p + 2]));
		};

		outStart = worldPointAt(startVec3);
		outEnd = worldPointAt(endVec3 - 1);
		outLength = 0.0f;
		for (int v = startVec3; v + 1 < endVec3; v += 2)
			outLength += worldPointAt(v).distanceToPoint(worldPointAt(v + 1));
		return true;
	}

	// Non-CAD mesh - a single straight feature edge (see
	// SceneMesh::getFeatureEdgeIndices()'s doc comment).
	const std::vector<uint32_t>& featureEdges = mesh->getFeatureEdgeIndices();
	const size_t base = static_cast<size_t>(ref.edgeIndex) * 2;
	if (base + 1 >= featureEdges.size())
		return false;

	const std::vector<float>& trsfPoints = mesh->getTrsfPoints();
	auto vertexPos = [&trsfPoints](uint32_t vIdx) -> QVector3D {
		const size_t p = static_cast<size_t>(vIdx) * 3;
		if (p + 2 >= trsfPoints.size())
			return QVector3D();
		return QVector3D(trsfPoints[p], trsfPoints[p + 1], trsfPoints[p + 2]);
	};

	outStart = vertexPos(featureEdges[base]);
	outEnd = vertexPos(featureEdges[base + 1]);
	outLength = outStart.distanceToPoint(outEnd);
	return true;
}

bool ViewportWidget::resolveMeasurementEdgePolyline(const MeasurementAnchorRef& ref, QVector<QVector3D>& outPoints) const
{
	outPoints.clear();
	if (ref.edgeIndex < 0)
		return false;

	SceneMesh* mesh = getMeshByUuid(ref.meshUuid);
	if (!mesh)
		return false;

	const std::vector<int>& occBounds = mesh->getOccEdgeBoundaries();
	if (!occBounds.empty())
	{
		if (ref.edgeIndex + 1 >= static_cast<int>(occBounds.size()))
			return false;

		const std::vector<float>& segments = mesh->getOccEdgeSegments();
		const int startVec3 = occBounds[ref.edgeIndex];
		const int endVec3 = occBounds[ref.edgeIndex + 1];
		if (startVec3 < 0 || endVec3 <= startVec3 || static_cast<size_t>(endVec3) * 3 > segments.size())
			return false;

		const QMatrix4x4 combined = mesh->combinedRenderTransform();
		auto worldPointAt = [&](int v) -> QVector3D {
			const size_t p = static_cast<size_t>(v) * 3;
			return combined.map(QVector3D(segments[p], segments[p + 1], segments[p + 2]));
		};

		// Segments within one OCC edge's range are emitted in connected,
		// head-to-tail order along the curve by construction (see
		// BRepToAssimpConverter::extractEdgesFromFaceGroup()'s
		// GCPnts_TangentialDeflection walk) - segment N's second point and
		// segment N+1's first point are the same value, so one point per
		// pair plus a final closing point traces the whole path with no
		// duplicates.
		outPoints.reserve((endVec3 - startVec3) / 2 + 1);
		for (int v = startVec3; v + 1 < endVec3; v += 2)
			outPoints.append(worldPointAt(v));
		outPoints.append(worldPointAt(endVec3 - 1));
		return true;
	}

	// Non-CAD mesh - a single straight feature edge, so its "polyline" is
	// just its two endpoints.
	const std::vector<uint32_t>& featureEdges = mesh->getFeatureEdgeIndices();
	const size_t base = static_cast<size_t>(ref.edgeIndex) * 2;
	if (base + 1 >= featureEdges.size())
		return false;

	const std::vector<float>& trsfPoints = mesh->getTrsfPoints();
	auto vertexPos = [&trsfPoints](uint32_t vIdx) -> QVector3D {
		const size_t p = static_cast<size_t>(vIdx) * 3;
		if (p + 2 >= trsfPoints.size())
			return QVector3D();
		return QVector3D(trsfPoints[p], trsfPoints[p + 1], trsfPoints[p + 2]);
	};
	outPoints.append(vertexPos(featureEdges[base]));
	outPoints.append(vertexPos(featureEdges[base + 1]));
	return true;
}

bool ViewportWidget::resolveMeasurementAnchorPlane(const MeasurementAnchorRef& ref,
	QVector3D& outPosition, QVector3D& outNormal) const
{
	if (ref.triangleIndex < 0)
		return false;

	SceneMesh* mesh = getMeshByUuid(ref.meshUuid);
	if (!mesh)
		return false;

	const std::vector<float>& trsfPoints = mesh->getTrsfPoints();
	auto vertexPos = [&trsfPoints](int vIdx) -> QVector3D {
		if (vIdx < 0)
			return QVector3D();
		const size_t p = static_cast<size_t>(vIdx) * 3;
		if (p + 2 >= trsfPoints.size())
			return QVector3D();
		return QVector3D(trsfPoints[p], trsfPoints[p + 1], trsfPoints[p + 2]);
	};

	const std::vector<unsigned int> indices = mesh->indices();
	const size_t base = static_cast<size_t>(ref.triangleIndex) * 3;
	if (base + 2 >= indices.size())
		return false;

	const QVector3D p0 = vertexPos(static_cast<int>(indices[base]));
	const QVector3D p1 = vertexPos(static_cast<int>(indices[base + 1]));
	const QVector3D p2 = vertexPos(static_cast<int>(indices[base + 2]));

	const QVector3D normal = QVector3D::crossProduct(p1 - p0, p2 - p0);
	if (normal.lengthSquared() < 1.0e-12f)
		return false;  // degenerate (near-zero-area) triangle

	outPosition = resolveMeasurementAnchor(ref);  // respects vertex snap, same as every other tool
	outNormal = normal.normalized();
	return true;
}

bool ViewportWidget::resolveMeasurementDimensionSegment(const Measurement& m, QVector3D& outA, QVector3D& outB) const
{
	switch (m.type)
	{
	case MeasurementType::Distance:
	{
		if (m.anchors.size() < 2)
			return false;
		outA = resolveMeasurementAnchor(m.anchors[0]);
		outB = resolveMeasurementAnchor(m.anchors[1]);
		return true;
	}
	case MeasurementType::PointToFace:
	{
		if (m.anchors.size() < 2)
			return false;
		const QVector3D point = resolveMeasurementAnchor(m.anchors[0]);
		QVector3D facePos, faceNormal;
		if (!resolveMeasurementAnchorPlane(m.anchors[1], facePos, faceNormal))
			return false;
		outA = point;
		outB = point - faceNormal * QVector3D::dotProduct(point - facePos, faceNormal);
		return true;
	}
	case MeasurementType::FaceToFace:
	{
		if (m.anchors.size() < 2)
			return false;
		QVector3D p1, n1, p2, n2;
		if (!resolveMeasurementAnchorPlane(m.anchors[0], p1, n1) || !resolveMeasurementAnchorPlane(m.anchors[1], p2, n2))
			return false;
		const MeasurementGeometry::FaceToFaceResult result = MeasurementGeometry::compareFaces(p1, n1, p2, n2);
		if (!result.isParallel)
			return false;  // angle case has an arc, not a straight dimension line to drag
		outA = p1;
		outB = p1 + n1 * QVector3D::dotProduct(p2 - p1, n1);
		return true;
	}
	case MeasurementType::EdgeLength:
	{
		if (m.anchors.isEmpty())
			return false;
		float length = 0.0f;
		return resolveMeasurementEdgeGeometry(m.anchors[0], outA, outB, length);
	}
	case MeasurementType::EdgeToVertex:
	{
		if (m.anchors.size() < 2)
			return false;
		QVector3D edgeStart, edgeEnd;
		float edgeLength = 0.0f;
		if (!resolveMeasurementEdgeGeometry(m.anchors[0], edgeStart, edgeEnd, edgeLength))
			return false;
		outA = resolveMeasurementAnchor(m.anchors[1]);
		outB = MeasurementGeometry::closestPointOnLine(outA, edgeStart, edgeEnd - edgeStart);
		return true;
	}
	case MeasurementType::EdgeToEdge:
	{
		if (m.anchors.size() < 2)
			return false;
		QVector3D start1, end1, start2, end2;
		float len1 = 0.0f, len2 = 0.0f;
		if (!resolveMeasurementEdgeGeometry(m.anchors[0], start1, end1, len1)
			|| !resolveMeasurementEdgeGeometry(m.anchors[1], start2, end2, len2))
			return false;
		const MeasurementGeometry::EdgeToEdgeResult result =
			MeasurementGeometry::compareLines(start1, end1 - start1, start2, end2 - start2);
		if (!result.isParallel)
			return false;  // angle case has legs+arc instead, not a straight dimension line to drag
		outA = start1;
		outB = MeasurementGeometry::closestPointOnLine(start1, start2, end2 - start2);
		return true;
	}
	case MeasurementType::EdgeToFace:
	{
		if (m.anchors.size() < 2)
			return false;
		QVector3D edgeStart, edgeEnd;
		float edgeLength = 0.0f;
		if (!resolveMeasurementEdgeGeometry(m.anchors[0], edgeStart, edgeEnd, edgeLength))
			return false;
		QVector3D facePos, faceNormal;
		if (!resolveMeasurementAnchorPlane(m.anchors[1], facePos, faceNormal))
			return false;
		const MeasurementGeometry::EdgeToFaceResult result =
			MeasurementGeometry::compareEdgeToFace(edgeStart, edgeEnd - edgeStart, facePos, faceNormal);
		if (!result.isParallel)
			return false;  // angle case has legs+arc instead, not a straight dimension line to drag
		outA = edgeStart;
		outB = edgeStart - faceNormal.normalized() * QVector3D::dotProduct(edgeStart - facePos, faceNormal.normalized());
		return true;
	}
	default:
		return false;
	}
}

QVector3D ViewportWidget::dimensionLinePerp(const QVector3D& a, const QVector3D& b,
	const QVector3D& referenceDir, Camera* camera) const
{
	const QVector3D delta = b - a;
	if (delta.lengthSquared() < 1.0e-12f || !camera)
		return QVector3D(0.0f, 1.0f, 0.0f);
	const QVector3D dirN = delta.normalized();

	const bool haveReference = referenceDir.lengthSquared() > 1.0e-8f;
	QVector3D perp = QVector3D::crossProduct(dirN, haveReference ? referenceDir : camera->getViewDir());
	if (perp.lengthSquared() < 1.0e-8f)
		perp = QVector3D::crossProduct(dirN, camera->getUpVector());
	if (perp.lengthSquared() < 1.0e-8f)
		perp = QVector3D::crossProduct(dirN, QVector3D(0.0f, 1.0f, 0.0f));
	return perp.normalized();
}

float ViewportWidget::defaultDimensionOffsetMagnitude(Camera* camera) const
{
	const float markerSize = camera ? std::max(camera->getViewRange(), 0.0001f) * 0.01f : 0.01f;
	return markerSize * 6.0f;
}

QVector3D ViewportWidget::resolveDimensionOffsetVector(const QVector3D& a, const QVector3D& b,
	const Measurement& m, Camera* camera) const
{
	if (m.offsetVector.lengthSquared() > 1.0e-10f)
		return m.offsetVector;  // user has dragged this - use the exact vector (direction + magnitude)
	return dimensionLinePerp(a, b, m.offsetReferenceDir, camera) * defaultDimensionOffsetMagnitude(camera);
}

bool ViewportWidget::resolveMeasurementAngleGeometry(const Measurement& m, Camera* camera, QVector3D& outVertex,
	QVector3D& outU, QVector3D& outV, float& outAngleRad, float& outRadius) const
{
	if (m.anchors.size() < 2)
		return false;

	// deg -> rad, same constant used throughout this file (kDegToRadLocal).
	constexpr float kDegToRad = 0.017453292519943295f;
	const float markerSize = camera ? std::max(camera->getViewRange(), 0.0001f) * 0.01f : 0.01f;

	// outRadius is the ARC's radius specifically (what hit-testing/dragging
	// treat as "the" draggable value) - the legs themselves extend a bit
	// further out than the arc (see drawMeasurementOverlay()'s
	// legLength = outRadius / 0.85f), matching the original fixed 85%
	// arc-inset-from-leg-tip look, now expressed the other way around so a
	// dragged value means exactly what the user grabbed (the arc). Shared by
	// all three cases below.
	auto finishBasis = [&](const QVector3D& u, const QVector3D& secondDir, float angleDegrees,
		float defaultLegLength) -> bool
	{
		outU = u;
		QVector3D v = secondDir - u * QVector3D::dotProduct(secondDir, u);
		if (v.lengthSquared() < 1.0e-8f)
			return false;  // degenerate - shouldn't happen given the parallel case was already ruled out
		outV = v.normalized();
		outAngleRad = angleDegrees * kDegToRad;
		const float defaultRadius = std::max(defaultLegLength, markerSize * 4.0f) * 0.85f;
		outRadius = (m.offsetDistance >= 0.0f) ? m.offsetDistance : defaultRadius;
		return true;
	};

	switch (m.type)
	{
	case MeasurementType::FaceToFace:
	{
		QVector3D p1, n1, p2, n2;
		if (!resolveMeasurementAnchorPlane(m.anchors[0], p1, n1) || !resolveMeasurementAnchorPlane(m.anchors[1], p2, n2))
			return false;
		const MeasurementGeometry::FaceToFaceResult result = MeasurementGeometry::compareFaces(p1, n1, p2, n2);
		if (result.isParallel)
			return false;  // parallel case has a straight dimension line instead - see resolveMeasurementDimensionSegment()

		outVertex = (p1 + p2) * 0.5f;
		const QVector3D n2Effective = (QVector3D::dotProduct(n1, n2) >= 0.0f) ? n2 : -n2;
		return finishBasis(n1, n2Effective, result.angleDegrees, (p1 - p2).length() * 0.5f);
	}
	case MeasurementType::EdgeToEdge:
	{
		QVector3D start1, end1, start2, end2;
		float len1 = 0.0f, len2 = 0.0f;
		if (!resolveMeasurementEdgeGeometry(m.anchors[0], start1, end1, len1)
			|| !resolveMeasurementEdgeGeometry(m.anchors[1], start2, end2, len2))
			return false;
		const QVector3D d1 = end1 - start1;
		const QVector3D d2 = end2 - start2;
		const MeasurementGeometry::EdgeToEdgeResult result = MeasurementGeometry::compareLines(start1, d1, start2, d2);
		if (result.isParallel)
			return false;  // parallel case has a straight dimension line instead - see resolveMeasurementDimensionSegment()

		outVertex = ((start1 + end1) * 0.5f + (start2 + end2) * 0.5f) * 0.5f;
		const QVector3D d1n = d1.normalized();
		const QVector3D d2nRaw = d2.normalized();
		const QVector3D d2n = (QVector3D::dotProduct(d1n, d2nRaw) >= 0.0f) ? d2nRaw : -d2nRaw;
		return finishBasis(d1n, d2n, result.angleDegrees, std::max(len1, len2) * 0.5f);
	}
	case MeasurementType::EdgeToFace:
	{
		QVector3D edgeStart, edgeEnd;
		float edgeLength = 0.0f;
		if (!resolveMeasurementEdgeGeometry(m.anchors[0], edgeStart, edgeEnd, edgeLength))
			return false;
		QVector3D facePos, faceNormal;
		if (!resolveMeasurementAnchorPlane(m.anchors[1], facePos, faceNormal))
			return false;
		const QVector3D edgeDir = edgeEnd - edgeStart;
		const MeasurementGeometry::EdgeToFaceResult result =
			MeasurementGeometry::compareEdgeToFace(edgeStart, edgeDir, facePos, faceNormal);
		if (result.isParallel)
			return false;  // parallel case has a straight dimension line instead - see resolveMeasurementDimensionSegment()

		// Grounded at the edge's own start point (a real point, unlike
		// FaceToFace/EdgeToEdge's "floating midpoint") - one leg along the
		// edge itself, the other along the edge's own projection onto the
		// face's plane, sweeping the angle between them.
		const QVector3D dN = edgeDir.normalized();
		const QVector3D nN = faceNormal.normalized();
		QVector3D projectedDir = dN - nN * QVector3D::dotProduct(dN, nN);
		if (projectedDir.lengthSquared() < 1.0e-8f)
		{
			// The edge is (very close to) exactly perpendicular to the
			// face - the 90-degree case, and a common one in practice (a
			// hole's axis edge square to the face it's drilled into isn't
			// a rare configuration). There's no uniquely-defined
			// "projection direction" within the face's plane at exactly
			// 90 degrees - every in-plane direction is equally valid - so
			// rather than bailing out with no arc at all (which is what
			// used to happen here), pick an arbitrary but deterministic
			// one, same construction as MeasurementGeometry::
			// orthonormalBasis(): whichever world axis is least parallel
			// to the face normal, cross product to land in-plane.
			const QVector3D reference = (std::abs(QVector3D::dotProduct(nN, QVector3D(0.0f, 1.0f, 0.0f))) < 0.9f)
				? QVector3D(0.0f, 1.0f, 0.0f)
				: QVector3D(1.0f, 0.0f, 0.0f);
			projectedDir = QVector3D::crossProduct(nN, reference).normalized();
		}
		else
		{
			projectedDir.normalize();
		}

		outVertex = edgeStart;
		return finishBasis(dN, projectedDir, result.angleDegrees, edgeLength * 0.5f);
	}
	case MeasurementType::AngleThreePoint:
	{
		if (m.anchors.size() < 3)
			return false;
		const QVector3D vertex = resolveMeasurementAnchor(m.anchors[0]);
		const QVector3D p1 = resolveMeasurementAnchor(m.anchors[1]);
		const QVector3D p2 = resolveMeasurementAnchor(m.anchors[2]);
		const QVector3D v1 = p1 - vertex;
		const QVector3D v2 = p2 - vertex;
		if (v1.lengthSquared() < 1.0e-8f || v2.lengthSquared() < 1.0e-8f)
			return false;  // a ray needs a real length - one of the picks landed on (or failed to resolve away from) the vertex itself

		outVertex = vertex;
		const QVector3D u = v1.normalized();
		const float angleDeg = MeasurementGeometry::angleBetweenRays(vertex, p1, p2);
		return finishBasis(u, v2, angleDeg, std::max(v1.length(), v2.length()) * 0.5f);
	}
	default:
		return false;
	}
}

void ViewportWidget::handleMeasurementClick(const QPoint& clickPoint)
{
	if (!_selectionManager || !_viewer || !_viewer->sceneGraph() || _measurementTool == MeasurementTool::None)
		return;

	MeasurementAnchorRef ref;

	if (_measurementTool == MeasurementTool::EdgeRadius || _measurementTool == MeasurementTool::Concentricity)
	{
		// A wholly different pick from every other tool - identifies a
		// topological B-Rep edge, not a surface point (see
		// MeshEdgeCircleAnchor's doc comment). Concentricity needs this for
		// BOTH its anchors, unconditionally (same as EdgeToEdge always
		// picking an edge for both of its anchors below) - it compares two
		// real analytic circles, not two arbitrary points.
		const MeshEdgeCircleAnchor edgeAnchor = _selectionManager->pickEdgeCircleAnchor(clickPoint);
		if (!edgeAnchor.isValid())
			return;  // no circular edge under the cursor - stay armed, don't cancel the tool
		ref.meshUuid  = edgeAnchor.meshUuid;
		ref.edgeIndex = edgeAnchor.edgeIndex;
	}
	else if (_measurementTool == MeasurementTool::EdgeLength
		|| _measurementTool == MeasurementTool::EdgeToEdge
		|| _measurementTool == MeasurementTool::EdgeChain
		|| (_measurementTool == MeasurementTool::EdgeToVertex && _pendingMeasurementAnchors.isEmpty())
		|| (_measurementTool == MeasurementTool::EdgeToFace && _pendingMeasurementAnchors.isEmpty()))
	{
		// EdgeLength, EdgeToEdge, and EdgeChain always pick an edge (every
		// anchor - EdgeChain's count is variable, but every one of them is
		// still an edge, same as EdgeToEdge's fixed two); EdgeToVertex/
		// EdgeToFace only pick one for their FIRST anchor - the second (a
		// vertex/point or a face) falls through to the normal
		// pickSurfaceAnchor branch below, same as every other point/face pick.
		const MeshEdgeCircleAnchor edgeAnchor = _selectionManager->pickStraightEdgeAnchor(clickPoint);
		if (!edgeAnchor.isValid())
			return;  // no edge under the cursor - stay armed, don't cancel the tool
		ref.meshUuid  = edgeAnchor.meshUuid;
		ref.edgeIndex = edgeAnchor.edgeIndex;
	}
	else if (_measurementTool == MeasurementTool::FaceToFace
		|| ((_measurementTool == MeasurementTool::PointToFace || _measurementTool == MeasurementTool::EdgeToFace)
			&& !_pendingMeasurementAnchors.isEmpty())
		|| _measurementTool == MeasurementTool::ArcRadius3Point
		|| (_measurementTool == MeasurementTool::ArcRadiusCenterPoint && !_pendingMeasurementAnchors.isEmpty()))
	{
		// Two different reasons land here, but both need the same plain
		// surface pick with no circular-edge-center snap attempted:
		//  - FACE picks (FaceToFace's two anchors; PointToFace/EdgeToFace's
		//    second anchor - their first is a point/edge, already routed
		//    above) need real triangle/normal data to resolve a plane
		//    from - a circle's center has none.
		//  - ARC-RIM picks (3-Point Arc Radius's 3 points; Center+2-Point
		//    Arc Radius's own p1/p2, as opposed to its CENTER anchor at
		//    index 0, handled below) must land on distinct points actually
		//    ON the circle - snapping any of them to the shared center
		//    instead would collapse the fit (circumcircle3Point() sees
		//    near-coincident points; circleFromCenterAndTwoPoints() sees a
		//    zero-length center-to-point vector) rather than measuring the
		//    circle at all.
		const MeshSurfaceAnchor anchor = _selectionManager->pickSurfaceAnchor(clickPoint);
		if (!anchor.isValid())
			return;  // clicked empty space - stay armed, don't cancel the tool

		ref.meshUuid           = anchor.meshUuid;
		ref.triangleIndex      = anchor.triangleIndex;
		ref.barycentric        = anchor.barycentric;
		ref.snappedVertexIndex = anchor.snappedVertexIndex;
	}
	else
	{
		// Every remaining pick genuinely wants an arbitrary POINT with no
		// "must be a distinct point on this rim" constraint (Point,
		// Distance, Point-to-Face's point anchor, Edge-to-Vertex's vertex
		// anchor, Center+2-Point Arc Radius's own CENTER anchor
		// specifically - its p1/p2 are excluded above - every one of
		// Pitch Circle's hole-center picks, and every one of 3-Point
		// Angle's vertex/ray picks). Prefer snapping to a nearby
		// circular B-Rep edge's exact analytic center (see
		// SelectionManager::pickCircularEdgeCenterAnchor()'s doc comment) -
		// a hole/boss center is very often exactly the point actually
		// wanted, and there's no other way to land on one precisely (it's
		// often empty space, not real geometry a plain surface pick could
		// ever hit). Falls back to the ordinary triangle-surface pick if no
		// circular edge is nearby, preserving plain point-picking on
		// glTF/OBJ meshes and everywhere else on CAD parts.
		const MeshEdgeCircleAnchor centerAnchor = _selectionManager->pickCircularEdgeCenterAnchor(clickPoint);
		if (centerAnchor.isValid())
		{
			ref.meshUuid  = centerAnchor.meshUuid;
			ref.edgeIndex = centerAnchor.edgeIndex;
		}
		else
		{
			const MeshSurfaceAnchor anchor = _selectionManager->pickSurfaceAnchor(clickPoint);
			if (!anchor.isValid())
				return;  // clicked empty space - stay armed, don't cancel the tool

			ref.meshUuid           = anchor.meshUuid;
			ref.triangleIndex      = anchor.triangleIndex;
			ref.barycentric        = anchor.barycentric;
			ref.snappedVertexIndex = anchor.snappedVertexIndex;
		}
	}

	_pendingMeasurementAnchors.append(ref);

	const int required = measurementToolRequiredAnchorCount(_measurementTool);
	emit measurementProgressChanged(_pendingMeasurementAnchors.size(), required);

	// A variable-length tool (PitchCircle, EdgeChain) never auto-completes
	// at `required` - that's its MINIMUM, not a target - it stays armed
	// regardless of count until finishVariableLengthMeasurement() is
	// called explicitly (Enter, or the dialog's Finish button).
	if (measurementToolHasVariableAnchorCount(_measurementTool))
		return;

	if (_pendingMeasurementAnchors.size() >= required)
		finalizePendingMeasurement();
}

void ViewportWidget::finalizePendingMeasurement()
{
	Measurement m;
	m.id = QUuid::createUuid();
	m.type = measurementTypeForTool(_measurementTool);
	m.anchors = _pendingMeasurementAnchors;
	// Captured once, here, at creation - see Measurement::offsetReferenceDir's
	// doc comment for why this must NOT be re-derived from the live
	// camera on every render frame.
	if (_primaryCamera)
		m.offsetReferenceDir = _primaryCamera->getViewDir();
	_viewer->addMeasurement(m);  // undoable - see AddMeasurementCommand
	_pendingMeasurementAnchors.clear();
	emit measurementProgressChanged(0, measurementToolRequiredAnchorCount(_measurementTool));
}

void ViewportWidget::finishVariableLengthMeasurement()
{
	if (!_viewer || !_viewer->sceneGraph() || !measurementToolHasVariableAnchorCount(_measurementTool))
		return;
	if (_pendingMeasurementAnchors.size() < measurementToolRequiredAnchorCount(_measurementTool))
		return;  // below the minimum - the Finish button should be disabled in this state anyway
	finalizePendingMeasurement();
}

void ViewportWidget::setSelectedMeasurementIds(const QSet<QUuid>& ids)
{
	if (_selectedMeasurementIds == ids)
		return;
	_selectedMeasurementIds = ids;
	update();
	emit measurementSelectionChanged(_selectedMeasurementIds);
}

QUuid ViewportWidget::hitTestMeasurement(const QPoint& pixel, Camera* camera, int pixelRadius) const
{
	if (!camera || !_viewer || !_viewer->sceneGraph())
		return QUuid();

	const QRect viewportRect(0, 0, width(), height());
	auto toScreen = [&](const QVector3D& worldPos) -> QVector2D {
		const QVector3D projected = worldPos.project(camera->getViewMatrix(), camera->getProjectionMatrix(), viewportRect);
		// Same y-flip as pickSurfaceAnchor()'s vertex-snap projection -
		// project() is OpenGL (bottom-up), pixel is Qt (top-down).
		return QVector2D(projected.x(), static_cast<float>(height()) - projected.y());
	};

	auto distPointToSegment = [](const QVector2D& p, const QVector2D& a, const QVector2D& b) -> float {
		const QVector2D ab = b - a;
		const float abLenSq = QVector2D::dotProduct(ab, ab);
		float t = abLenSq > 1.0e-6f ? QVector2D::dotProduct(p - a, ab) / abLenSq : 0.0f;
		t = std::clamp(t, 0.0f, 1.0f);
		return (p - (a + ab * t)).length();
	};

	const QVector2D clickPt(static_cast<float>(pixel.x()), static_cast<float>(pixel.y()));
	QUuid bestId;
	float bestDist = static_cast<float>(pixelRadius);

	for (const Measurement& m : _viewer->sceneGraph()->measurements())
	{
		if (!m.visible)
			continue;

		if (m.type == MeasurementType::Point && !m.anchors.isEmpty())
		{
			const QVector2D sp = toScreen(resolveMeasurementAnchor(m.anchors[0]));
			const float d = (clickPt - sp).length();
			if (d < bestDist)
			{
				bestDist = d;
				bestId = m.id;
			}
		}
		else if (m.type == MeasurementType::Distance && m.anchors.size() >= 2)
		{
			const QVector2D sa = toScreen(resolveMeasurementAnchor(m.anchors[0]));
			const QVector2D sb = toScreen(resolveMeasurementAnchor(m.anchors[1]));
			const float d = distPointToSegment(clickPt, sa, sb);
			if (d < bestDist)
			{
				bestDist = d;
				bestId = m.id;
			}
		}
		else if (m.type == MeasurementType::ArcRadius3Point && m.anchors.size() >= 3)
		{
			const QVector3D p0 = resolveMeasurementAnchor(m.anchors[0]);
			const QVector3D p1 = resolveMeasurementAnchor(m.anchors[1]);
			const QVector3D p2 = resolveMeasurementAnchor(m.anchors[2]);
			QVector3D center, normal;
			float radius = 0.0f;
			if (MeasurementGeometry::circumcircle3Point(p0, p1, p2, center, normal, radius))
			{
				const QVector<QVector3D> circle = MeasurementGeometry::circlePolyline(center, normal, radius);
				for (int i = 0; i < circle.size(); ++i)
				{
					const float d = distPointToSegment(clickPt, toScreen(circle[i]), toScreen(circle[(i + 1) % circle.size()]));
					if (d < bestDist)
					{
						bestDist = d;
						bestId = m.id;
					}
				}
				const float dc = (clickPt - toScreen(center)).length();
				if (dc < bestDist)
				{
					bestDist = dc;
					bestId = m.id;
				}
			}
		}
		else if (m.type == MeasurementType::ArcRadiusCenterPoint && m.anchors.size() >= 3)
		{
			const QVector3D center = resolveMeasurementAnchor(m.anchors[0]);
			const QVector3D p1 = resolveMeasurementAnchor(m.anchors[1]);
			const QVector3D p2 = resolveMeasurementAnchor(m.anchors[2]);
			QVector3D normal;
			float radius = 0.0f;
			if (MeasurementGeometry::circleFromCenterAndTwoPoints(center, p1, p2, normal, radius))
			{
				const QVector<QVector3D> circle = MeasurementGeometry::circlePolyline(center, normal, radius);
				for (int i = 0; i < circle.size(); ++i)
				{
					const float d = distPointToSegment(clickPt, toScreen(circle[i]), toScreen(circle[(i + 1) % circle.size()]));
					if (d < bestDist)
					{
						bestDist = d;
						bestId = m.id;
					}
				}
				// Also test the two center-to-point spokes and the center
				// marker itself - clicking directly on a spoke or the center
				// dot should select the measurement too, not just the rim.
				const float dSpoke1 = distPointToSegment(clickPt, toScreen(center), toScreen(p1));
				const float dSpoke2 = distPointToSegment(clickPt, toScreen(center), toScreen(p2));
				const float dCenter = (clickPt - toScreen(center)).length();
				const float dBest = std::min({ dSpoke1, dSpoke2, dCenter });
				if (dBest < bestDist)
				{
					bestDist = dBest;
					bestId = m.id;
				}
			}
		}
		else if (m.type == MeasurementType::EdgeRadius && !m.anchors.isEmpty())
		{
			QVector3D center, axis;
			float radius = 0.0f;
			if (resolveMeasurementEdgeCircle(m.anchors[0], center, axis, radius))
			{
				const QVector<QVector3D> circle = MeasurementGeometry::circlePolyline(center, axis, radius);
				for (int i = 0; i < circle.size(); ++i)
				{
					const float d = distPointToSegment(clickPt, toScreen(circle[i]), toScreen(circle[(i + 1) % circle.size()]));
					if (d < bestDist)
					{
						bestDist = d;
						bestId = m.id;
					}
				}
				const float dCenter = (clickPt - toScreen(center)).length();
				if (dCenter < bestDist)
				{
					bestDist = dCenter;
					bestId = m.id;
				}
			}
		}
		else if (m.type == MeasurementType::FaceToFace && m.anchors.size() >= 2)
		{
			QVector3D p1, n1, p2, n2;
			if (resolveMeasurementAnchorPlane(m.anchors[0], p1, n1) && resolveMeasurementAnchorPlane(m.anchors[1], p2, n2))
			{
				const float d1 = (clickPt - toScreen(p1)).length();
				const float d2 = (clickPt - toScreen(p2)).length();
				const float dSeg = distPointToSegment(clickPt, toScreen(p1), toScreen(p2));
				const float dBest = std::min({ d1, d2, dSeg });
				if (dBest < bestDist)
				{
					bestDist = dBest;
					bestId = m.id;
				}
			}
		}
		else if (m.type == MeasurementType::PointToFace && m.anchors.size() >= 2)
		{
			const QVector3D point = resolveMeasurementAnchor(m.anchors[0]);
			QVector3D facePos, faceNormal;
			if (resolveMeasurementAnchorPlane(m.anchors[1], facePos, faceNormal))
			{
				const float dSeg = distPointToSegment(clickPt, toScreen(point), toScreen(facePos));
				if (dSeg < bestDist)
				{
					bestDist = dSeg;
					bestId = m.id;
				}
			}
		}
		else if (m.type == MeasurementType::EdgeLength && !m.anchors.isEmpty())
		{
			QVector3D start, end;
			float length = 0.0f;
			if (resolveMeasurementEdgeGeometry(m.anchors[0], start, end, length))
			{
				const float dSeg = distPointToSegment(clickPt, toScreen(start), toScreen(end));
				if (dSeg < bestDist)
				{
					bestDist = dSeg;
					bestId = m.id;
				}
			}
		}
		else if (m.type == MeasurementType::EdgeToVertex && m.anchors.size() >= 2)
		{
			QVector3D edgeStart, edgeEnd;
			float edgeLength = 0.0f;
			if (resolveMeasurementEdgeGeometry(m.anchors[0], edgeStart, edgeEnd, edgeLength))
			{
				const QVector3D point = resolveMeasurementAnchor(m.anchors[1]);
				const float dEdge = distPointToSegment(clickPt, toScreen(edgeStart), toScreen(edgeEnd));
				const float dPoint = (clickPt - toScreen(point)).length();
				const float dBest = std::min(dEdge, dPoint);
				if (dBest < bestDist)
				{
					bestDist = dBest;
					bestId = m.id;
				}
			}
		}
		else if (m.type == MeasurementType::EdgeToEdge && m.anchors.size() >= 2)
		{
			QVector3D start1, end1, start2, end2;
			float len1 = 0.0f, len2 = 0.0f;
			if (resolveMeasurementEdgeGeometry(m.anchors[0], start1, end1, len1)
				&& resolveMeasurementEdgeGeometry(m.anchors[1], start2, end2, len2))
			{
				const float d1 = distPointToSegment(clickPt, toScreen(start1), toScreen(end1));
				const float d2 = distPointToSegment(clickPt, toScreen(start2), toScreen(end2));
				const float dBest = std::min(d1, d2);
				if (dBest < bestDist)
				{
					bestDist = dBest;
					bestId = m.id;
				}
			}
		}
		else if (m.type == MeasurementType::EdgeToFace && m.anchors.size() >= 2)
		{
			QVector3D edgeStart, edgeEnd;
			float edgeLength = 0.0f;
			if (resolveMeasurementEdgeGeometry(m.anchors[0], edgeStart, edgeEnd, edgeLength))
			{
				QVector3D facePos, faceNormal;
				if (resolveMeasurementAnchorPlane(m.anchors[1], facePos, faceNormal))
				{
					const float dEdge = distPointToSegment(clickPt, toScreen(edgeStart), toScreen(edgeEnd));
					const float dFace = (clickPt - toScreen(facePos)).length();
					const float dBest = std::min(dEdge, dFace);
					if (dBest < bestDist)
					{
						bestDist = dBest;
						bestId = m.id;
					}
				}
			}
		}
		else if (m.type == MeasurementType::PitchCircle && m.anchors.size() >= 3)
		{
			QVector<QVector3D> points;
			points.reserve(m.anchors.size());
			for (const MeasurementAnchorRef& a : m.anchors)
				points.append(resolveMeasurementAnchor(a));
			const MeasurementGeometry::PitchCircleResult result = MeasurementGeometry::fitPitchCircle(points);
			if (result.valid)
			{
				const QVector<QVector3D> circle = MeasurementGeometry::circlePolyline(result.center, result.normal, result.radius);
				for (int i = 0; i < circle.size(); ++i)
				{
					const float d = distPointToSegment(clickPt, toScreen(circle[i]), toScreen(circle[(i + 1) % circle.size()]));
					if (d < bestDist)
					{
						bestDist = d;
						bestId = m.id;
					}
				}
				const float dc = (clickPt - toScreen(result.center)).length();
				if (dc < bestDist)
				{
					bestDist = dc;
					bestId = m.id;
				}
			}
		}
		else if (m.type == MeasurementType::Concentricity && m.anchors.size() >= 2)
		{
			QVector3D center1, axis1, center2, axis2;
			float radius1 = 0.0f, radius2 = 0.0f;
			if (resolveMeasurementEdgeCircle(m.anchors[0], center1, axis1, radius1)
				&& resolveMeasurementEdgeCircle(m.anchors[1], center2, axis2, radius2))
			{
				const QVector<QVector3D> circle1 = MeasurementGeometry::circlePolyline(center1, axis1, radius1);
				for (int i = 0; i < circle1.size(); ++i)
				{
					const float d = distPointToSegment(clickPt, toScreen(circle1[i]), toScreen(circle1[(i + 1) % circle1.size()]));
					if (d < bestDist)
					{
						bestDist = d;
						bestId = m.id;
					}
				}
				const QVector<QVector3D> circle2 = MeasurementGeometry::circlePolyline(center2, axis2, radius2);
				for (int i = 0; i < circle2.size(); ++i)
				{
					const float d = distPointToSegment(clickPt, toScreen(circle2[i]), toScreen(circle2[(i + 1) % circle2.size()]));
					if (d < bestDist)
					{
						bestDist = d;
						bestId = m.id;
					}
				}
				const float dc1 = (clickPt - toScreen(center1)).length();
				if (dc1 < bestDist)
				{
					bestDist = dc1;
					bestId = m.id;
				}
				const float dc2 = (clickPt - toScreen(center2)).length();
				if (dc2 < bestDist)
				{
					bestDist = dc2;
					bestId = m.id;
				}
			}
		}
		else if (m.type == MeasurementType::AngleThreePoint && m.anchors.size() >= 3)
		{
			const QVector3D vertex = resolveMeasurementAnchor(m.anchors[0]);
			const QVector3D p1 = resolveMeasurementAnchor(m.anchors[1]);
			const QVector3D p2 = resolveMeasurementAnchor(m.anchors[2]);
			const float d1 = distPointToSegment(clickPt, toScreen(vertex), toScreen(p1));
			const float d2 = distPointToSegment(clickPt, toScreen(vertex), toScreen(p2));
			const float dBest = std::min(d1, d2);
			if (dBest < bestDist)
			{
				bestDist = dBest;
				bestId = m.id;
			}
		}
		else if (m.type == MeasurementType::EdgeChain && m.anchors.size() >= 2)
		{
			// Same true-path tracing as the render branch below (see
			// resolveMeasurementEdgePolyline()'s doc comment) - hit-testing
			// against just the chord would miss clicks along a curved or
			// filleted edge's actual (highlighted) path.
			for (const MeasurementAnchorRef& a : m.anchors)
			{
				QVector<QVector3D> polyline;
				if (!resolveMeasurementEdgePolyline(a, polyline) || polyline.size() < 2)
					continue;
				for (int i = 0; i + 1 < polyline.size(); ++i)
				{
					const float d = distPointToSegment(clickPt, toScreen(polyline[i]), toScreen(polyline[i + 1]));
					if (d < bestDist)
					{
						bestDist = d;
						bestId = m.id;
					}
				}
			}
		}
	}

	return bestId;
}

ViewportWidget::DimensionHit ViewportWidget::hitTestDimensionLine(const QPoint& pixel, Camera* camera, int pixelRadius) const
{
	DimensionHit hit;
	if (!camera || !_viewer || !_viewer->sceneGraph())
		return hit;

	const QRect viewportRect(0, 0, width(), height());
	auto toScreen = [&](const QVector3D& worldPos) -> QVector2D {
		const QVector3D projected = worldPos.project(camera->getViewMatrix(), camera->getProjectionMatrix(), viewportRect);
		return QVector2D(projected.x(), static_cast<float>(height()) - projected.y());
	};
	auto distPointToSegment = [](const QVector2D& p, const QVector2D& a, const QVector2D& b) -> float {
		const QVector2D ab = b - a;
		const float abLenSq = QVector2D::dotProduct(ab, ab);
		float t = abLenSq > 1.0e-6f ? QVector2D::dotProduct(p - a, ab) / abLenSq : 0.0f;
		t = std::clamp(t, 0.0f, 1.0f);
		return (p - (a + ab * t)).length();
	};

	const QVector2D clickPt(static_cast<float>(pixel.x()), static_cast<float>(pixel.y()));
	float bestDist = static_cast<float>(pixelRadius);

	for (const Measurement& m : _viewer->sceneGraph()->measurements())
	{
		if (!m.visible)
			continue;

		QVector3D a, b;
		if (resolveMeasurementDimensionSegment(m, a, b))
		{
			const QVector3D offsetVec = resolveDimensionOffsetVector(a, b, m, camera);
			const QVector3D aOff = a + offsetVec;
			const QVector3D bOff = b + offsetVec;

			const float d = distPointToSegment(clickPt, toScreen(aOff), toScreen(bOff));
			if (d < bestDist)
			{
				bestDist = d;
				hit.measurementId = m.id;
				hit.kind = DimensionDragKind::Linear;
			}
			continue;
		}

		QVector3D vertex, u, v;
		float angleRad = 0.0f;
		float radius = 0.0f;
		if (resolveMeasurementAngleGeometry(m, camera, vertex, u, v, angleRad, radius))
		{
			// Walk just the arc segment [0, angleRad] directly in the (u,v)
			// basis - circlePolyline() sweeps a full circle in a basis with
			// no relation to u/v's angular origin, so it isn't reusable here.
			constexpr int arcSegments = 24;
			QVector3D prevPoint = vertex + u * radius;
			for (int i = 1; i <= arcSegments; ++i)
			{
				const float t = angleRad * (static_cast<float>(i) / static_cast<float>(arcSegments));
				const QVector3D nextPoint = vertex + (u * std::cos(t) + v * std::sin(t)) * radius;
				const float d = distPointToSegment(clickPt, toScreen(prevPoint), toScreen(nextPoint));
				if (d < bestDist)
				{
					bestDist = d;
					hit.measurementId = m.id;
					hit.kind = DimensionDragKind::AngleRadius;
				}
				prevPoint = nextPoint;
			}
		}
	}

	return hit;
}

void ViewportWidget::beginDimensionLineDrag(const QUuid& measurementId, DimensionDragKind kind, Camera* camera)
{
	if (!camera || !_viewer || !_viewer->sceneGraph() || kind == DimensionDragKind::None)
		return;

	const int index = _viewer->sceneGraph()->measurementIndexById(measurementId);
	if (index < 0)
		return;
	const Measurement& m = _viewer->sceneGraph()->measurements().at(index);

	_dimensionDragKind = kind;

	if (kind == DimensionDragKind::Linear)
	{
		QVector3D a, b;
		if (!resolveMeasurementDimensionSegment(m, a, b))
		{
			_dimensionDragKind = DimensionDragKind::None;
			return;
		}
		_dimensionDragPivot = (a + b) * 0.5f;
		// The dimension line's own direction - the NORMAL of the plane the
		// drag freely repositions the offset within (see
		// updateDimensionLineDrag()'s ray/plane intersection).
		_dimensionDragAxis = (b - a).normalized();
		_dimensionDragStartOffsetVector = resolveDimensionOffsetVector(a, b, m, camera);
	}
	else  // AngleRadius
	{
		QVector3D vertex, u, v;
		float angleRad = 0.0f;
		float radius = 0.0f;
		if (!resolveMeasurementAngleGeometry(m, camera, vertex, u, v, angleRad, radius))
		{
			_dimensionDragKind = DimensionDragKind::None;
			return;
		}
		_dimensionDragPivot = vertex;
		// Bisector of the two legs - the 1D direction the radius drag
		// measures magnitude along.
		const QVector3D bisector = (u * std::cos(angleRad * 0.5f) + v * std::sin(angleRad * 0.5f));
		_dimensionDragAxis = bisector.lengthSquared() > 1.0e-8f ? bisector.normalized() : u;
		_dimensionDragStartOffsetScalar = radius;
		// Reference length for the world-per-screen-pixel ratio (same
		// technique as updateTransformGizmoTranslationDrag()'s dragScale).
		_dimensionDragRefLength = std::max(_dimensionDragStartOffsetScalar, 0.01f);
	}

	_dimensionDragActive = true;
}

void ViewportWidget::updateDimensionLineDrag(const QPoint& pixel, Camera* camera)
{
	if (!camera || !_viewer || !_viewer->sceneGraph() || !_dimensionDragActive)
		return;

	const QRect viewport(0, 0, width(), height());
	const QMatrix4x4 viewMatrix = camera->getViewMatrix();
	const QMatrix4x4 projMatrix = camera->getProjectionMatrix();

	if (_dimensionDragKind == DimensionDragKind::Linear)
	{
		// True ray/plane intersection: cast a ray from the camera through
		// the current mouse pixel, intersect it with the plane through
		// _dimensionDragPivot whose normal is the dimension line's own
		// direction (_dimensionDragAxis) - i.e. the plane containing every
		// valid perpendicular offset. The intersection point minus the
		// pivot IS the new offset vector directly - this is what lets the
		// drag both "pivot" (change direction) and "extend" (change
		// magnitude) in one continuous motion, unlike a single-axis ratio
		// drag which can only ever change magnitude along one fixed axis.
		const int glX = pixel.x();
		const int glY = height() - pixel.y() - 1;  // Qt top-down -> GL bottom-up, same convention used elsewhere for unproject()
		const QVector3D rayOrigin = QVector3D(static_cast<float>(glX), static_cast<float>(glY), 0.0f).unproject(viewMatrix, projMatrix, viewport);
		QVector3D rayDir = QVector3D(static_cast<float>(glX), static_cast<float>(glY), 1.0f).unproject(viewMatrix, projMatrix, viewport) - rayOrigin;
		if (rayDir.lengthSquared() < 1.0e-12f)
			return;
		rayDir.normalize();

		const float denom = QVector3D::dotProduct(rayDir, _dimensionDragAxis);
		if (std::abs(denom) < 1.0e-6f)
			return;  // ray parallel to the plane (viewing exactly edge-on) - leave the offset unchanged this frame rather than divide by ~0

		const float t = QVector3D::dotProduct(_dimensionDragPivot - rayOrigin, _dimensionDragAxis) / denom;
		if (t < 0.0f)
			return;  // intersection behind the camera - degenerate, ignore this frame

		const QVector3D hitPoint = rayOrigin + rayDir * t;
		QVector3D newOffset = hitPoint - _dimensionDragPivot;
		// Project out any residual component along the dimension-line axis
		// (should already be ~0 since hitPoint lies in the plane, but keep
		// this exact against floating-point drift) so the dimension line
		// stays exactly parallel to the measured segment.
		newOffset -= _dimensionDragAxis * QVector3D::dotProduct(newOffset, _dimensionDragAxis);

		// Floored magnitude, not allowed to collapse to ~0 - that would put
		// the dimension line on top of the actual measured geometry,
		// defeating the point of having one. Direction is preserved.
		const float mag = newOffset.length();
		constexpr float kMinOffsetMagnitude = 0.01f;
		if (mag < kMinOffsetMagnitude)
			newOffset = (mag > 1.0e-8f ? newOffset / mag : _dimensionDragAxis) * kMinOffsetMagnitude;

		_viewer->sceneGraph()->setMeasurementOffsetVector(_dimensionDragCandidateId, newOffset);
	}
	else  // AngleRadius
	{
		// Screen-space axis projection + world-per-pixel rescaling -
		// identical technique to updateTransformGizmoTranslationDrag()'s
		// single-axis translate drag: project two known points on the fixed
		// drag axis to screen space, dot the mouse's pixel delta against
		// that 2D direction, then rescale by (refLength / axisScreenLength)
		// to recover a world-space distance. Extension only, along the
		// fixed bisector - no plane/pivot freedom for the angle case.
		const QVector3D pivotScreen3 = _dimensionDragPivot.project(viewMatrix, projMatrix, viewport);
		const QVector3D axisEndWorld = _dimensionDragPivot + _dimensionDragAxis * _dimensionDragRefLength;
		const QVector3D axisEndScreen3 = axisEndWorld.project(viewMatrix, projMatrix, viewport);

		const QVector2D pivotScreen(pivotScreen3.x(), pivotScreen3.y());
		const QVector2D axisScreen = QVector2D(axisEndScreen3.x(), axisEndScreen3.y()) - pivotScreen;
		const float axisScreenLength = axisScreen.length();
		if (axisScreenLength <= 1.0e-4f)
			return;

		const QVector2D axisScreenDir = axisScreen / axisScreenLength;
		const QVector2D mouseDelta(static_cast<float>(pixel.x() - _dimensionDragStartPixel.x()),
			static_cast<float>(_dimensionDragStartPixel.y() - pixel.y()));  // Y-flip, same convention as updateTransformGizmoTranslationDrag()
		const float projectedPixels = QVector2D::dotProduct(mouseDelta, axisScreenDir);
		const float worldDelta = (projectedPixels / axisScreenLength) * _dimensionDragRefLength;

		const float newOffset = std::max(_dimensionDragStartOffsetScalar + worldDelta, 0.01f);
		_viewer->sceneGraph()->setMeasurementOffsetDistance(_dimensionDragCandidateId, newOffset);
	}

	update();
}

void ViewportWidget::finishDimensionLineDrag()
{
	if (_dimensionDragActive && _viewer && _viewer->sceneGraph())
	{
		const int index = _viewer->sceneGraph()->measurementIndexById(_dimensionDragCandidateId);
		const Measurement* mm = (index >= 0) ? &_viewer->sceneGraph()->measurements().at(index) : nullptr;

		// Redundant re-apply of the same final value on redo() (it's already
		// live from the drag) but establishes the undo edge - same "one
		// command on release" shape as TransformCommand's gizmo-drag pattern.
		if (_dimensionDragKind == DimensionDragKind::Linear)
		{
			const QVector3D finalOffset = mm ? mm->offsetVector : _dimensionDragStartOffsetVector;
			if ((finalOffset - _dimensionDragStartOffsetVector).lengthSquared() > 1.0e-10f && _viewer->getUndoStack())
			{
				_viewer->getUndoStack()->push(new MeasurementOffsetVectorCommand(_viewer, this,
					_dimensionDragCandidateId, _dimensionDragStartOffsetVector, finalOffset));
			}
		}
		else if (_dimensionDragKind == DimensionDragKind::AngleRadius)
		{
			const float finalOffset = mm ? mm->offsetDistance : _dimensionDragStartOffsetScalar;
			if (std::abs(finalOffset - _dimensionDragStartOffsetScalar) > 1.0e-5f && _viewer->getUndoStack())
			{
				_viewer->getUndoStack()->push(new MeasurementOffsetCommand(_viewer, this,
					_dimensionDragCandidateId, _dimensionDragStartOffsetScalar, finalOffset));
			}
		}
	}

	_dimensionDragActive = false;
	_dimensionDragCandidate = false;
	_dimensionDragCandidateId = QUuid();
	_dimensionDragKind = DimensionDragKind::None;
}

void ViewportWidget::drawMeasurementOverlay(Camera* camera)
{
	if (!camera || !_renderCtrl.axisShader() || !_viewer || !_viewer->sceneGraph())
		return;

	const QVector<Measurement>& measurements = _viewer->sceneGraph()->measurements();
	if (measurements.isEmpty() && _pendingMeasurementAnchors.isEmpty()
		&& !_measurementHoverAnchor.isValid() && !_measurementEdgeHoverAnchor.isValid())
		return;

	struct LabelEntry { QVector3D worldPos; QString text; };
	std::vector<float> lineVertices;
	std::vector<float> triangleVertices;  // dimension-line arrowhead cones - see addCone() below
	QVector<LabelEntry> labels;

	// M_PI isn't guaranteed available (MSVC needs _USE_MATH_DEFINES before
	// <cmath>) - same local-constant convention as MeasurementGeometry.cpp.
	constexpr float kTwoPiLocal = 6.283185307179586f;

	const QVector3D pointColor(0.15f, 0.85f, 1.0f);
	const QVector3D distanceColor(1.0f, 0.82f, 0.15f);
	const QVector3D pendingColor(1.0f, 1.0f, 1.0f);
	const QVector3D hoverSnapColor(0.25f, 1.0f, 0.35f);  // will snap to this vertex
	const QVector3D hoverRawColor(0.65f, 0.65f, 0.65f);  // raw surface pick, no snap nearby
	const QVector3D selectedColor(1.0f, 0.35f, 0.05f);   // orange - already selected

	auto addSegment = [&lineVertices](const QVector3D& a, const QVector3D& b, const QVector3D& color) {
		lineVertices.insert(lineVertices.end(), { a.x(), a.y(), a.z(), color.x(), color.y(), color.z() });
		lineVertices.insert(lineVertices.end(), { b.x(), b.y(), b.z(), color.x(), color.y(), color.z() });
	};

	// Marker cross size scales with the camera's current view range so it
	// stays a sensible on-screen size whether the user is zoomed in on a
	// small detail or looking at the whole model. sizeMultiplier gives
	// hover/selection extra visual weight beyond just a color change.
	const float markerSize = std::max(camera->getViewRange(), 0.0001f) * 0.01f;

	// Arrowhead cones use a per-point constant-screen-size scale instead -
	// mirrors TransformGizmo::computeWorldScale()'s exact technique (same
	// ortho-vs-perspective split, same idea of reacting to THIS point's own
	// depth rather than a single scene-wide value). markerSize above is
	// scene-wide (camera->getViewRange() alone), which is fine for small
	// point-cross markers but wrong for a dimension's arrowheads under
	// perspective projection: a dimension sitting much closer to the
	// camera than the current orbit pivot would get an oversized cone,
	// and one much farther away an undersized one, even though the
	// overall "zoom" (viewRange) hasn't changed at all.
	auto coneScaleAt = [&](const QVector3D& worldPos) -> float {
		if (camera->getProjectionType() == Camera::ProjectionType::ORTHOGRAPHIC)
			return markerSize;
		const float distance = (camera->getRenderPosition() - worldPos).length();
		return std::max(distance * 0.01f, 0.0001f);
	};

	auto addMarker = [&](const QVector3D& p, const QVector3D& color, float sizeMultiplier = 1.0f) {
		const float s = markerSize * sizeMultiplier;
		addSegment(p - QVector3D(s, 0, 0), p + QVector3D(s, 0, 0), color);
		addSegment(p - QVector3D(0, s, 0), p + QVector3D(0, s, 0), color);
		addSegment(p - QVector3D(0, 0, s), p + QVector3D(0, 0, s), color);
	};

	auto addCircleOutline = [&](const QVector3D& center, const QVector3D& normal, float radius, const QVector3D& color) {
		const QVector<QVector3D> circle = MeasurementGeometry::circlePolyline(center, normal, radius);
		for (int i = 0; i < circle.size(); ++i)
			addSegment(circle[i], circle[(i + 1) % circle.size()], color);
	};

	// CAD-style dimension-line arrowhead: a solid cone with its apex at
	// `apex`, pointing along `direction` (base sits behind the apex, at
	// apex - direction*height). Emits triangleVertices, not lineVertices -
	// a flat-shaded cone needs real triangles, not GL_LINES, so this is
	// drawn in a separate pass/buffer below (see the GL_TRIANGLES block near
	// the end of this function).
	auto addCone = [&](const QVector3D& apex, const QVector3D& direction, float radius, float height, const QVector3D& color) {
		const QVector3D dir = direction.normalized();
		const QVector3D reference = (std::abs(QVector3D::dotProduct(dir, QVector3D(0.0f, 1.0f, 0.0f))) < 0.9f)
			? QVector3D(0.0f, 1.0f, 0.0f)
			: QVector3D(1.0f, 0.0f, 0.0f);
		const QVector3D u = QVector3D::crossProduct(dir, reference).normalized();
		const QVector3D v = QVector3D::crossProduct(dir, u).normalized();
		const QVector3D base = apex - dir * height;

		constexpr int coneSegments = 10;
		auto ringPoint = [&](int i) {
			const float theta = (kTwoPiLocal * static_cast<float>(i)) / static_cast<float>(coneSegments);
			return base + (u * std::cos(theta) + v * std::sin(theta)) * radius;
		};
		auto pushTri = [&](const QVector3D& a, const QVector3D& b, const QVector3D& c) {
			triangleVertices.insert(triangleVertices.end(), { a.x(), a.y(), a.z(), color.x(), color.y(), color.z() });
			triangleVertices.insert(triangleVertices.end(), { b.x(), b.y(), b.z(), color.x(), color.y(), color.z() });
			triangleVertices.insert(triangleVertices.end(), { c.x(), c.y(), c.z(), color.x(), color.y(), color.z() });
		};
		for (int i = 0; i < coneSegments; ++i)
		{
			const QVector3D b0 = ringPoint(i);
			const QVector3D b1 = ringPoint(i + 1);
			pushTri(apex, b0, b1);   // lateral surface
			pushTri(base, b1, b0);   // base cap (visible when viewed from behind)
		}
	};

	// A measured segment rendered with CAD-style arrowheads: the connecting
	// line plus a cone at each end, tip touching the endpoint and pointing
	// outward, base set back toward the opposite end. 1:3 radius:height
	// ratio per CAD convention (a slender arrow, not a fat one). Capped so
	// arrowheads on a very short dimension don't overlap each other.
	auto addDimensionLine = [&](const QVector3D& a, const QVector3D& b, const QVector3D& color) {
		addSegment(a, b, color);
		const QVector3D delta = b - a;
		const float len = delta.length();
		if (len < 1.0e-6f)
			return;
		const QVector3D dirN = delta / len;
		// One shared scale for both cones (evaluated at the line's own
		// midpoint) rather than one per end - a dimension line is short
		// relative to camera distance in practice, so the two ends'
		// individual depths rarely differ enough to matter, and matching
		// cone sizes at both ends reads better than two subtly different
		// ones on the same line.
		// 0.6f = half of the original 1.2f base radius factor (1:3
		// radius:height ratio preserved below, since coneHeight is derived
		// straight from coneRadius).
		const float coneRadius = coneScaleAt((a + b) * 0.5f) * 0.6f;
		const float coneHeight = std::min(coneRadius * 3.0f, len * 0.4f);
		addCone(a, -dirN, coneRadius, coneHeight, color);
		addCone(b, dirN, coneRadius, coneHeight, color);
	};

	// Full CAD-style linear dimension: the dimension line itself is offset
	// away from the actual measured points [a,b] (not coincident with them),
	// connected back via thin extension ("witness") lines - the standard CAD
	// drafting convention, and the reason a dimension floats clear of the
	// part instead of embedding in/behind it. The offset vector comes from
	// resolveDimensionOffsetVector() - the same query hitTestDimensionLine()/
	// the drag interaction use, so rendering and interaction can never
	// disagree about where the dimension line actually is (and, once
	// dragged, the extension lines "pivot" to match - see
	// Measurement::offsetVector's doc comment). Returns the offset
	// dimension line's midpoint, for label placement.
	auto addOffsetDimension = [&](const QVector3D& a, const QVector3D& b, const QVector3D& color, const Measurement& mm) -> QVector3D {
		const QVector3D delta = b - a;
		const float len = delta.length();
		if (len < 1.0e-6f)
		{
			addMarker(a, color);
			return a;
		}

		const QVector3D offsetVec = resolveDimensionOffsetVector(a, b, mm, camera);
		const QVector3D aOff = a + offsetVec;
		const QVector3D bOff = b + offsetVec;

		addSegment(a, aOff, color);  // extension line at a
		addSegment(b, bOff, color);  // extension line at b
		addDimensionLine(aOff, bOff, color);
		return (aOff + bOff) * 0.5f;
	};

	// The floating-vertex angular dimension's full visual: two legs from
	// `vertex` (one along `u`, the other along whatever direction is
	// exactly `angleRad` around from `u` toward `v` - by construction that's
	// the original second direction the angle was measured against, so it
	// doesn't need to be passed in separately), a swept arc at `radius`
	// between them, tangent arrowhead cones at each end, and the angle text
	// at the arc's midpoint. Shared by every measurement type whose non-
	// parallel case renders this way (FaceToFace, EdgeToEdge, EdgeToFace) -
	// geometry comes from resolveMeasurementAngleGeometry(), the same query
	// hitTestDimensionLine()/the drag interaction use, so none of them can
	// ever disagree about where the arc actually is. Returns the label
	// position.
	auto addAngleArc = [&](const QVector3D& vertex, const QVector3D& u, const QVector3D& v,
		float angleRad, float radius, const QVector3D& color) -> QVector3D {
		const float legLength = radius / 0.85f;
		const QVector3D secondDir = u * std::cos(angleRad) + v * std::sin(angleRad);
		addSegment(vertex, vertex + u * legLength, color);
		addSegment(vertex, vertex + secondDir * legLength, color);

		constexpr int arcSegments = 24;
		QVector3D prevPoint = vertex + u * radius;
		for (int i = 1; i <= arcSegments; ++i)
		{
			const float t = angleRad * (static_cast<float>(i) / static_cast<float>(arcSegments));
			const QVector3D nextPoint = vertex + (u * std::cos(t) + v * std::sin(t)) * radius;
			addSegment(prevPoint, nextPoint, color);
			prevPoint = nextPoint;
		}

		// Arrowheads tangent to the arc at each end, pointing outward (away
		// from the arc's middle) - mirrors addDimensionLine()'s "tips touch
		// the endpoints, pointing away from the middle" convention. Scaled
		// at the arc's own vertex (both cones share it, same reasoning as
		// addDimensionLine()'s shared midpoint scale).
		// 0.6f = half of the original 1.2f base radius factor, same as
		// addDimensionLine()'s cones (1:3 ratio preserved below).
		const float coneRadius = coneScaleAt(vertex) * 0.6f;
		const float coneHeight = std::min(coneRadius * 3.0f, radius * 0.3f);
		const QVector3D startPoint = vertex + u * radius;
		addCone(startPoint, -v, coneRadius, coneHeight, color);  // derivative at t=0 is +v; outward is reversed
		const QVector3D endPoint = vertex + secondDir * radius;
		const QVector3D endTangentOutward = -std::sin(angleRad) * u + std::cos(angleRad) * v;  // derivative at t=angleRad
		addCone(endPoint, endTangentOutward, coneRadius, coneHeight, color);

		const float midT = angleRad * 0.5f;
		return vertex + (u * std::cos(midT) + v * std::sin(midT)) * (radius * 1.15f);
	};

	for (const Measurement& m : measurements)
	{
		if (!m.visible)
			continue;

		const bool isSelected = _selectedMeasurementIds.contains(m.id);
		// Selection is the stronger cue and wins if somehow both apply
		// (shouldn't normally happen - hover-select only runs while nothing
		// new is being placed - but a stale hover from just before a click
		// landed is a real possibility for one frame).
		const bool isHovered = !isSelected && (m.id == _hoveredMeasurementId);

		QVector3D color = isSelected ? selectedColor
			: (m.type == MeasurementType::Point ? pointColor : distanceColor);
		if (isHovered)
			color = color * 0.5f + QVector3D(1.0f, 1.0f, 1.0f) * 0.5f;  // blend toward white - a lighter preview than full selection
		const float sizeMultiplier = isSelected ? 1.5f : (isHovered ? 1.25f : 1.0f);
		const QString summary = measurementSummaryText(m);

		// Separate, stronger hover cue for the draggable dimension line/arc
		// specifically (see mouseMoveEvent()'s _hoveredDimensionId update) -
		// distinct from `color` above (used for markers/legs/normal-
		// indicators, which don't change) so the exact grabbable part reads
		// clearly, not the whole measurement.
		const bool isDimensionHovered = !isSelected && (m.id == _hoveredDimensionId);
		const QVector3D dimensionColor = isDimensionHovered
			? (color * 0.4f + QVector3D(1.0f, 1.0f, 1.0f) * 0.6f)
			: color;

		if (m.type == MeasurementType::Point && !m.anchors.isEmpty())
		{
			const QVector3D p = resolveMeasurementAnchor(m.anchors[0]);
			addMarker(p, color, sizeMultiplier);
			labels.append({ p, summary });
		}
		else if (m.type == MeasurementType::Distance && m.anchors.size() >= 2)
		{
			const QVector3D a = resolveMeasurementAnchor(m.anchors[0]);
			const QVector3D b = resolveMeasurementAnchor(m.anchors[1]);
			addMarker(a, color, sizeMultiplier);
			addMarker(b, color, sizeMultiplier);
			const QVector3D labelPos = addOffsetDimension(a, b, dimensionColor, m);
			labels.append({ labelPos, summary });
		}
		else if (m.type == MeasurementType::ArcRadius3Point && m.anchors.size() >= 3)
		{
			const QVector3D p0 = resolveMeasurementAnchor(m.anchors[0]);
			const QVector3D p1 = resolveMeasurementAnchor(m.anchors[1]);
			const QVector3D p2 = resolveMeasurementAnchor(m.anchors[2]);
			QVector3D center, normal;
			float radius = 0.0f;
			if (MeasurementGeometry::circumcircle3Point(p0, p1, p2, center, normal, radius))
			{
				addMarker(p0, color, sizeMultiplier);
				addMarker(p1, color, sizeMultiplier);
				addMarker(p2, color, sizeMultiplier);
				addMarker(center, color, sizeMultiplier * 0.6f);
				addCircleOutline(center, normal, radius, color);
				labels.append({ center, summary });
			}
		}
		else if (m.type == MeasurementType::ArcRadiusCenterPoint && m.anchors.size() >= 3)
		{
			const QVector3D center = resolveMeasurementAnchor(m.anchors[0]);
			const QVector3D p1 = resolveMeasurementAnchor(m.anchors[1]);
			const QVector3D p2 = resolveMeasurementAnchor(m.anchors[2]);
			QVector3D normal;
			float radius = 0.0f;
			if (MeasurementGeometry::circleFromCenterAndTwoPoints(center, p1, p2, normal, radius))
			{
				addMarker(center, color, sizeMultiplier * 0.6f);
				addMarker(p1, color, sizeMultiplier);
				addMarker(p2, color, sizeMultiplier);
				addSegment(center, p1, color);
				addSegment(center, p2, color);
				addCircleOutline(center, normal, radius, color);
				labels.append({ center, summary });
			}
		}
		else if (m.type == MeasurementType::EdgeRadius && !m.anchors.isEmpty())
		{
			QVector3D center, axis;
			float radius = 0.0f;
			if (resolveMeasurementEdgeCircle(m.anchors[0], center, axis, radius))
			{
				addMarker(center, color, sizeMultiplier * 0.6f);
				addCircleOutline(center, axis, radius, color);
				labels.append({ center, summary });
			}
		}
		else if (m.type == MeasurementType::FaceToFace && m.anchors.size() >= 2)
		{
			QVector3D p1, n1, p2, n2;
			if (resolveMeasurementAnchorPlane(m.anchors[0], p1, n1) && resolveMeasurementAnchorPlane(m.anchors[1], p2, n2))
			{
				const float normalLen = markerSize * 3.0f;
				addMarker(p1, color, sizeMultiplier);
				addMarker(p2, color, sizeMultiplier);
				addSegment(p1, p1 + n1 * normalLen, color);
				addSegment(p2, p2 + n2 * normalLen, color);

				const MeasurementGeometry::FaceToFaceResult result = MeasurementGeometry::compareFaces(p1, n1, p2, n2);
				if (result.isParallel)
				{
					// Dimension line between the two (near-)parallel planes:
					// from p1, straight along n1, to the point that's
					// coplanar with p2 - offset + extension lines +
					// arrowheads via addOffsetDimension().
					const QVector3D projected = p1 + n1 * QVector3D::dotProduct(p2 - p1, n1);
					const QVector3D labelPos = addOffsetDimension(p1, projected, dimensionColor, m);
					labels.append({ labelPos, summary });
				}
				else
				{
					// Angular dimension: since two arbitrary faces have no
					// natural shared vertex/edge, the angle is shown
					// "floating" at the midpoint between the two picks.
					// Vertex/basis/angle/radius all come from
					// resolveMeasurementAngleGeometry() - the same query
					// hitTestDimensionLine()/the drag interaction use, so
					// this can't disagree with either about where the arc
					// actually is.
					QVector3D vertex, u, v;
					float angleRad = 0.0f;
					float arcRadius = 0.0f;
					if (resolveMeasurementAngleGeometry(m, camera, vertex, u, v, angleRad, arcRadius))
					{
						const QVector3D labelPos = addAngleArc(vertex, u, v, angleRad, arcRadius, dimensionColor);
						labels.append({ labelPos, summary });
					}
				}
			}
		}
		else if (m.type == MeasurementType::PointToFace && m.anchors.size() >= 2)
		{
			const QVector3D point = resolveMeasurementAnchor(m.anchors[0]);
			QVector3D facePos, faceNormal;
			if (resolveMeasurementAnchorPlane(m.anchors[1], facePos, faceNormal))
			{
				const float normalLen = markerSize * 3.0f;
				addMarker(point, color, sizeMultiplier);
				addMarker(facePos, color, sizeMultiplier);
				addSegment(facePos, facePos + faceNormal * normalLen, color);

				// Dimension line from the point straight down to its
				// projection onto the face's plane, offset + extension
				// lines + arrowheads via addOffsetDimension().
				const QVector3D projected = point - faceNormal * QVector3D::dotProduct(point - facePos, faceNormal);
				const QVector3D labelPos = addOffsetDimension(point, projected, dimensionColor, m);
				labels.append({ labelPos, summary });
			}
		}
		else if (m.type == MeasurementType::EdgeLength && !m.anchors.isEmpty())
		{
			QVector3D start, end;
			float length = 0.0f;
			if (resolveMeasurementEdgeGeometry(m.anchors[0], start, end, length))
			{
				// Same offset + extension-line + drag treatment as every
				// other linear dimension (see addOffsetDimension()) -
				// consistent with Distance/Point-to-Face/Face-to-Face even
				// though the edge itself is already visible geometry, so
				// the dimension doesn't have to sit exactly on top of the
				// model's own edge to be measured.
				addMarker(start, color, sizeMultiplier);
				addMarker(end, color, sizeMultiplier);
				const QVector3D labelPos = addOffsetDimension(start, end, dimensionColor, m);
				labels.append({ labelPos, summary });
			}
		}
		else if (m.type == MeasurementType::EdgeToVertex && m.anchors.size() >= 2)
		{
			QVector3D edgeStart, edgeEnd;
			float edgeLength = 0.0f;
			if (resolveMeasurementEdgeGeometry(m.anchors[0], edgeStart, edgeEnd, edgeLength))
			{
				const QVector3D point = resolveMeasurementAnchor(m.anchors[1]);
				const QVector3D projected = MeasurementGeometry::closestPointOnLine(point, edgeStart, edgeEnd - edgeStart);

				// The edge itself, highlighted as reference context (not
				// draggable - only the point-to-edge dimension line is).
				addSegment(edgeStart, edgeEnd, color);
				addMarker(edgeStart, color, sizeMultiplier);
				addMarker(edgeEnd, color, sizeMultiplier);
				addMarker(point, color, sizeMultiplier);

				// Dimension line from the point to its perpendicular foot on
				// the edge's infinite line, offset + extension lines +
				// arrowheads via addOffsetDimension().
				const QVector3D labelPos = addOffsetDimension(point, projected, dimensionColor, m);
				labels.append({ labelPos, summary });
			}
		}
		else if (m.type == MeasurementType::EdgeToEdge && m.anchors.size() >= 2)
		{
			QVector3D start1, end1, start2, end2;
			float len1 = 0.0f, len2 = 0.0f;
			if (resolveMeasurementEdgeGeometry(m.anchors[0], start1, end1, len1)
				&& resolveMeasurementEdgeGeometry(m.anchors[1], start2, end2, len2))
			{
				// Both edges, highlighted as reference context (not
				// draggable themselves - only the resulting dimension is).
				addSegment(start1, end1, color);
				addSegment(start2, end2, color);
				addMarker(start1, color, sizeMultiplier);
				addMarker(end1, color, sizeMultiplier);
				addMarker(start2, color, sizeMultiplier);
				addMarker(end2, color, sizeMultiplier);

				const QVector3D d1 = end1 - start1;
				const QVector3D d2 = end2 - start2;
				const MeasurementGeometry::EdgeToEdgeResult result =
					MeasurementGeometry::compareLines(start1, d1, start2, d2);

				if (result.isParallel)
				{
					// Dimension line from a point on edge1 to its
					// projection onto edge2's infinite line - offset +
					// extension lines + arrowheads via addOffsetDimension().
					const QVector3D projected = MeasurementGeometry::closestPointOnLine(start1, start2, d2);
					const QVector3D labelPos = addOffsetDimension(start1, projected, dimensionColor, m);
					labels.append({ labelPos, summary });
				}
				else
				{
					// Angular dimension - same visual language as Face to
					// Face's angle case (floating vertex + legs + arc +
					// tangent arrowheads), now drag-adjustable the same way
					// too. Vertex/basis/angle/radius all come from
					// resolveMeasurementAngleGeometry() - the same query
					// hitTestDimensionLine()/the drag interaction use.
					QVector3D vertex, u, v;
					float angleRad = 0.0f;
					float arcRadius = 0.0f;
					if (resolveMeasurementAngleGeometry(m, camera, vertex, u, v, angleRad, arcRadius))
					{
						const QVector3D labelPos = addAngleArc(vertex, u, v, angleRad, arcRadius, dimensionColor);
						labels.append({ labelPos, summary });
					}
				}
			}
		}
		else if (m.type == MeasurementType::EdgeToFace && m.anchors.size() >= 2)
		{
			QVector3D edgeStart, edgeEnd;
			float edgeLength = 0.0f;
			if (resolveMeasurementEdgeGeometry(m.anchors[0], edgeStart, edgeEnd, edgeLength))
			{
				QVector3D facePos, faceNormal;
				if (resolveMeasurementAnchorPlane(m.anchors[1], facePos, faceNormal))
				{
					const float normalLen = markerSize * 3.0f;

					// Both the edge and the face, highlighted as reference context.
					addSegment(edgeStart, edgeEnd, color);
					addMarker(edgeStart, color, sizeMultiplier);
					addMarker(edgeEnd, color, sizeMultiplier);
					addMarker(facePos, color, sizeMultiplier);
					addSegment(facePos, facePos + faceNormal.normalized() * normalLen, color);

					const QVector3D edgeDir = edgeEnd - edgeStart;
					const MeasurementGeometry::EdgeToFaceResult result =
						MeasurementGeometry::compareEdgeToFace(edgeStart, edgeDir, facePos, faceNormal);

					if (result.isParallel)
					{
						// Dimension line from the edge straight down to its
						// projection onto the face's plane, offset +
						// extension lines + arrowheads via addOffsetDimension().
						const QVector3D nN = faceNormal.normalized();
						const QVector3D projected = edgeStart - nN * QVector3D::dotProduct(edgeStart - facePos, nN);
						const QVector3D labelPos = addOffsetDimension(edgeStart, projected, dimensionColor, m);
						labels.append({ labelPos, summary });
					}
					else
					{
						// Angular dimension: grounded at the edge's own
						// start point (a real point, unlike Face-to-Face/
						// Edge-to-Edge's "floating midpoint") - one leg
						// along the edge itself, the other along the
						// edge's own projection onto the face's plane,
						// sweeping the angle between them. Vertex/basis/
						// angle/radius all come from
						// resolveMeasurementAngleGeometry() - the same
						// query hitTestDimensionLine()/the drag
						// interaction use.
						QVector3D vertex, u, v;
						float angleRad = 0.0f;
						float arcRadius = 0.0f;
						if (resolveMeasurementAngleGeometry(m, camera, vertex, u, v, angleRad, arcRadius))
						{
							const QVector3D labelPos = addAngleArc(vertex, u, v, angleRad, arcRadius, dimensionColor);
							labels.append({ labelPos, summary });
						}
					}
				}
			}
		}
		else if (m.type == MeasurementType::PitchCircle && m.anchors.size() >= 3)
		{
			QVector<QVector3D> points;
			points.reserve(m.anchors.size());
			for (const MeasurementAnchorRef& a : m.anchors)
				points.append(resolveMeasurementAnchor(a));
			const MeasurementGeometry::PitchCircleResult result = MeasurementGeometry::fitPitchCircle(points);
			if (result.valid)
			{
				for (const QVector3D& p : points)
					addMarker(p, color, sizeMultiplier);
				addMarker(result.center, color, sizeMultiplier * 0.6f);
				addCircleOutline(result.center, result.normal, result.radius, color);
				labels.append({ result.center, summary });
			}
		}
		else if (m.type == MeasurementType::Concentricity && m.anchors.size() >= 2)
		{
			QVector3D center1, axis1, center2, axis2;
			float radius1 = 0.0f, radius2 = 0.0f;
			if (resolveMeasurementEdgeCircle(m.anchors[0], center1, axis1, radius1)
				&& resolveMeasurementEdgeCircle(m.anchors[1], center2, axis2, radius2))
			{
				addCircleOutline(center1, axis1, radius1, color);
				addCircleOutline(center2, axis2, radius2, color);
				addMarker(center1, color, sizeMultiplier * 0.6f);
				addMarker(center2, color, sizeMultiplier * 0.6f);
				// The connecting line between the two centers IS the
				// measured quantity (its length is the reported offset) -
				// dimensionColor/hover-highlighted like every other
				// measurement's actual result, not just reference context.
				addSegment(center1, center2, dimensionColor);
				labels.append({ (center1 + center2) * 0.5f, summary });
			}
		}
		else if (m.type == MeasurementType::AngleThreePoint && m.anchors.size() >= 3)
		{
			const QVector3D vertex = resolveMeasurementAnchor(m.anchors[0]);
			const QVector3D p1 = resolveMeasurementAnchor(m.anchors[1]);
			const QVector3D p2 = resolveMeasurementAnchor(m.anchors[2]);

			// The actual picked rays, as reference context - not the
			// dimension arc's own legs below, which are drawn at a fixed
			// CAD-style radius (see resolveMeasurementAngleGeometry()), not
			// the true pick-to-pick distance.
			addMarker(vertex, color, sizeMultiplier);
			addMarker(p1, color, sizeMultiplier);
			addMarker(p2, color, sizeMultiplier);
			addSegment(vertex, p1, color);
			addSegment(vertex, p2, color);

			QVector3D angleVertex, u, v;
			float angleRad = 0.0f, arcRadius = 0.0f;
			if (resolveMeasurementAngleGeometry(m, camera, angleVertex, u, v, angleRad, arcRadius))
			{
				const QVector3D labelPos = addAngleArc(angleVertex, u, v, angleRad, arcRadius, dimensionColor);
				labels.append({ labelPos, summary });
			}
		}
		else if (m.type == MeasurementType::EdgeChain && m.anchors.size() >= 2)
		{
			// No single "dimension line" to offset+drag the way EdgeLength's
			// one edge gets (see addOffsetDimension()) - a sum over N edges
			// has no one line to put it on, so each edge is just highlighted
			// directly, and the label sits at the centroid of all their
			// midpoints. Traces each edge's TRUE tessellated path (see
			// resolveMeasurementEdgePolyline()'s doc comment), not a
			// straight chord between its two ends - unlike EdgeLength's own
			// offset dimension line (deliberately straight, floating clear
			// of the part), this draws directly on/near the part, where a
			// chord across a curved or filleted edge reads as if the wrong
			// edge got picked even though the length is correct.
			QVector3D labelCentroid;
			int resolvedCount = 0;
			for (const MeasurementAnchorRef& a : m.anchors)
			{
				QVector<QVector3D> polyline;
				if (!resolveMeasurementEdgePolyline(a, polyline) || polyline.size() < 2)
					continue;

				for (int i = 0; i + 1 < polyline.size(); ++i)
					addSegment(polyline[i], polyline[i + 1], color);
				addMarker(polyline.first(), color, sizeMultiplier);
				addMarker(polyline.last(), color, sizeMultiplier);
				labelCentroid += (polyline.first() + polyline.last()) * 0.5f;
				++resolvedCount;
			}
			if (resolvedCount > 0)
			{
				labelCentroid /= static_cast<float>(resolvedCount);
				labels.append({ labelCentroid, summary });
			}
		}
	}

	// In-progress measurement: N of the required anchors already picked,
	// waiting on the next click. Works uniformly for every tool - a plain
	// marker (or, for an edge pick, the edge's own highlighted chord) per
	// pick already made, a straight preview line connecting consecutive
	// picks (useful feedback even for a 3rd arc point that hasn't landed
	// yet), and a prompt for what to click next.
	if (!_pendingMeasurementAnchors.isEmpty())
	{
		// Mirrors handleMeasurementClick()'s own tool/anchor-index dispatch
		// for which picks came from pickStraightEdgeAnchor() rather than
		// pickSurfaceAnchor()/pickCircularEdgeCenterAnchor() - an edge
		// anchor has no single "point" to resolve (resolveMeasurementAnchor()
		// would only find one for a CIRCULAR edge, via its center; a
		// straight edge anchor has neither triangleIndex/snappedVertexIndex
		// nor a circle to resolve, so it would silently render at the
		// origin instead of showing the edge that was actually picked).
		auto isEdgeChordAnchor = [](MeasurementTool tool, int anchorIndex) -> bool {
			switch (tool)
			{
			case MeasurementTool::EdgeLength:
			case MeasurementTool::EdgeToEdge:
			case MeasurementTool::EdgeChain:
				return true;  // every anchor is an edge
			case MeasurementTool::EdgeToVertex:
			case MeasurementTool::EdgeToFace:
				return anchorIndex == 0;  // only the first anchor is an edge; the second is a point/face
			default:
				return false;
			}
		};

		QVector3D lastPicked;
		for (int i = 0; i < _pendingMeasurementAnchors.size(); ++i)
		{
			const MeasurementAnchorRef& pendingRef = _pendingMeasurementAnchors[i];
			QVector3D p;
			if (isEdgeChordAnchor(_measurementTool, i))
			{
				// True tessellated path, not just the chord (see
				// resolveMeasurementEdgePolyline()'s doc comment) - a
				// curved or filleted edge should preview as itself while
				// still being picked, same reasoning as Chain Length's
				// completed-measurement rendering below.
				QVector<QVector3D> polyline;
				if (resolveMeasurementEdgePolyline(pendingRef, polyline) && polyline.size() >= 2)
				{
					for (int seg = 0; seg + 1 < polyline.size(); ++seg)
						addSegment(polyline[seg], polyline[seg + 1], pendingColor);
					addMarker(polyline.first(), pendingColor);
					addMarker(polyline.last(), pendingColor);
					p = (polyline.first() + polyline.last()) * 0.5f;
				}
			}
			else
			{
				p = resolveMeasurementAnchor(pendingRef);
				addMarker(p, pendingColor);
			}

			if (i > 0)
				addSegment(lastPicked, p, pendingColor);
			lastPicked = p;
		}
		labels.append({ lastPicked, measurementToolPickPrompt(_measurementTool, _pendingMeasurementAnchors.size()) });
	}

	// Live hover preview: the exact point a click would place right now,
	// including vertex snap (see mouseMoveEvent()'s _measurementHoverAnchor
	// update). Distinct color and a larger cross when it WILL snap, so the
	// snap itself reads unambiguously rather than looking like just another
	// raw surface point.
	if (_measurementHoverAnchor.isValid())
	{
		const bool snapped = _measurementHoverAnchor.snappedVertexIndex >= 0;
		const QVector3D hp = _measurementHoverAnchor.worldPosition;
		const QVector3D hoverColor = snapped ? hoverSnapColor : hoverRawColor;
		const float hoverMarkerSize = markerSize * (snapped ? 1.6f : 1.0f);
		addSegment(hp - QVector3D(hoverMarkerSize, 0, 0), hp + QVector3D(hoverMarkerSize, 0, 0), hoverColor);
		addSegment(hp - QVector3D(0, hoverMarkerSize, 0), hp + QVector3D(0, hoverMarkerSize, 0), hoverColor);
		addSegment(hp - QVector3D(0, 0, hoverMarkerSize), hp + QVector3D(0, 0, hoverMarkerSize), hoverColor);
	}

	// Live hover preview for edge-based picks and circular-edge-center
	// point picks alike (see mouseMoveEvent()'s _measurementEdgeHoverAnchor/
	// _measurementEdgeHoverIsCenterPick update). Edge Radius and
	// Concentricity (both circular-edge picks) preview the resolved circle;
	// any POINT pick that's snapping to a circular edge's center (Point,
	// Distance, both arc tools, Point-to-Face's point anchor, Edge-to-
	// Vertex's vertex anchor) previews just the resolved center point;
	// every genuine edge-target tool (EdgeLength/EdgeToEdge/EdgeToVertex's
	// first anchor/EdgeToFace's first anchor) previews the edge's own
	// chord as a straight line instead.
	if (_measurementEdgeHoverAnchor.isValid())
	{
		MeasurementAnchorRef hoverRef;
		hoverRef.meshUuid  = _measurementEdgeHoverAnchor.meshUuid;
		hoverRef.edgeIndex = _measurementEdgeHoverAnchor.edgeIndex;

		if (_measurementTool == MeasurementTool::EdgeRadius || _measurementTool == MeasurementTool::Concentricity)
		{
			QVector3D center, axis;
			float radius = 0.0f;
			if (resolveMeasurementEdgeCircle(hoverRef, center, axis, radius))
			{
				addCircleOutline(center, axis, radius, hoverSnapColor);
				const float s = markerSize * 1.6f;
				addSegment(center - QVector3D(s, 0, 0), center + QVector3D(s, 0, 0), hoverSnapColor);
				addSegment(center - QVector3D(0, s, 0), center + QVector3D(0, s, 0), hoverSnapColor);
				addSegment(center - QVector3D(0, 0, s), center + QVector3D(0, 0, s), hoverSnapColor);
			}
		}
		else if (_measurementEdgeHoverIsCenterPick)
		{
			QVector3D center, axis;
			float radius = 0.0f;
			if (resolveMeasurementEdgeCircle(hoverRef, center, axis, radius))
			{
				const float s = markerSize * 1.6f;
				addSegment(center - QVector3D(s, 0, 0), center + QVector3D(s, 0, 0), hoverSnapColor);
				addSegment(center - QVector3D(0, s, 0), center + QVector3D(0, s, 0), hoverSnapColor);
				addSegment(center - QVector3D(0, 0, s), center + QVector3D(0, 0, s), hoverSnapColor);
			}
		}
		else
		{
			// True tessellated path, not just the chord - same reasoning as
			// the pending-pick and completed-measurement previews (see
			// resolveMeasurementEdgePolyline()'s doc comment).
			QVector<QVector3D> polyline;
			if (resolveMeasurementEdgePolyline(hoverRef, polyline) && polyline.size() >= 2)
			{
				for (int i = 0; i + 1 < polyline.size(); ++i)
					addSegment(polyline[i], polyline[i + 1], hoverSnapColor);
			}
		}
	}

	// Dimension geometry (lines + arrowhead cones) must never be hidden
	// behind shaded model surfaces - the whole point of a CAD-style
	// dimension is that it stays legible regardless of what's in front of
	// it at that depth. Saved/restored (not just force-disabled) so this
	// doesn't leak into whatever renders after this function.
	const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
	glDisable(GL_DEPTH_TEST);

	if (!lineVertices.empty())
	{
		// Mirrors drawBoundingBoxOverlay()'s exact upload/draw pattern.
		_renderCtrl.initMeasurementOverlayGeometry(lineVertices);
		glBindVertexArray(_renderCtrl.measurementOverlayVAO());
		glBindBuffer(GL_ARRAY_BUFFER, _renderCtrl.measurementOverlayVBO());
		glBufferData(GL_ARRAY_BUFFER,
		             static_cast<GLsizeiptr>(lineVertices.size() * sizeof(float)),
		             lineVertices.data(),
		             GL_DYNAMIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void*>(0));
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void*>(3 * sizeof(float)));

		_renderCtrl.axisShader()->bind();
		_renderCtrl.axisShader()->setUniformValue("modelViewMatrix", camera->getViewMatrix());
		_renderCtrl.axisShader()->setUniformValue("projectionMatrix", camera->getProjectionMatrix());
		_renderCtrl.axisShader()->setUniformValue("renderCone", false);
		glLineWidth(2.0f);
		glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineVertices.size() / 6));
		glLineWidth(1.0f);
		_renderCtrl.axisShader()->release();

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);
	}

	if (!triangleVertices.empty())
	{
		// Dimension-line arrowhead cones - same shader/upload pattern as the
		// line pass above, separate buffer/draw call since these are solid
		// GL_TRIANGLES, not GL_LINES (see addCone()). Cull state is saved/
		// restored rather than assumed, matching this file's existing
		// convention elsewhere (e.g. drawSelectionOutline()) - addCone()'s
		// winding isn't guaranteed consistent from every possible viewing
		// direction, and the shader is fully unlit/flat-color regardless of
		// facing, so there's no correctness reason to cull either face here.
		const GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);
		glDisable(GL_CULL_FACE);

		_renderCtrl.initMeasurementConeGeometry(triangleVertices);
		glBindVertexArray(_renderCtrl.measurementConeVAO());
		glBindBuffer(GL_ARRAY_BUFFER, _renderCtrl.measurementConeVBO());
		glBufferData(GL_ARRAY_BUFFER,
		             static_cast<GLsizeiptr>(triangleVertices.size() * sizeof(float)),
		             triangleVertices.data(),
		             GL_DYNAMIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void*>(0));
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void*>(3 * sizeof(float)));

		_renderCtrl.axisShader()->bind();
		_renderCtrl.axisShader()->setUniformValue("modelViewMatrix", camera->getViewMatrix());
		_renderCtrl.axisShader()->setUniformValue("projectionMatrix", camera->getProjectionMatrix());
		_renderCtrl.axisShader()->setUniformValue("renderCone", false);
		glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(triangleVertices.size() / 6));
		_renderCtrl.axisShader()->release();

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);

		if (cullWasEnabled)
			glEnable(GL_CULL_FACE);
	}

	if (depthWasEnabled)
		glEnable(GL_DEPTH_TEST);

	if (_axisTextRenderer)
	{
		const QRect viewportRect(0, 0, width(), height());
		// RenderText() has no concept of a line break (see its doc comment
		// in TextRenderer.h) - a label containing '\n' (currently just
		// Pitch Circle's headline/detail summary) is split here and its
		// lines stacked upward from the anchor point, one fontSize()-tall
		// step apart, so the LAST line lands exactly where a single-line
		// label always has (VBOTTOM's usual anchor) and earlier lines sit
		// above it - single-line labels render identically to before
		// (the loop below just runs once, at zero offset).
		const float lineHeight = static_cast<float>(_axisTextRenderer->fontSize()) * 1.2f;
		for (const LabelEntry& entry : labels)
		{
			const QVector3D projected = entry.worldPos.project(
				camera->getViewMatrix(), camera->getProjectionMatrix(), viewportRect);
			const float baseY = height() - projected.y();

			const QStringList textLines = entry.text.split(QChar('\n'));
			for (int i = 0; i < textLines.size(); ++i)
			{
				const float y = baseY - lineHeight * static_cast<float>(textLines.size() - 1 - i);
				_axisTextRenderer->RenderText(textLines[i].toStdString(),
					projected.x(), y, 1,
					QVector3D(1.0f, 1.0f, 1.0f), TextRenderer::VAlignment::VBOTTOM);
			}
		}
	}
}

GltfCameraData ViewportWidget::cameraDataForMvfSave(const GltfCameraData& source) const
{
	GltfCameraData result = source;

	// Bake the user model-level transform (if any) of the cameras' own file —
	// the same matrix the meshes and lights follow.
	QMatrix4x4 userTransform;
	const bool hasUserTransform =
		userModelTransformForFile(result.sourceFile, userTransform);

	for (GltfCameraEntry& cam : result.cameras)
	{
		QVector3D worldPos = cam.worldPosition;
		QVector3D worldDir = cam.worldDirection.normalized();
		QVector3D worldUp = cam.worldUp.normalized();

		// Mirror the same gate used by applyGltfCameraEntryTransform:
		// only bake the model-transform compensation when the flag is set.
		// Without this guard, each save/reload cycle bakes another rotation
		// layer that activation never applies back (flag is already false after
		// the first round-trip), causing cameras to drift on every save.
		if (cam.needsModelTransformCompensation && hasUserTransform)
		{
			worldPos = userTransform.map(worldPos);
			worldDir = userTransform.mapVector(worldDir).normalized();
			worldUp  = userTransform.mapVector(worldUp).normalized();
		}

		cam.worldPosition = worldPos;
		cam.worldDirection = worldDir;
		cam.worldUp = worldUp;
		cam.needsModelTransformCompensation = false;
	}

	return result;
}

void ViewportWidget::applyGltfCameraEntryTransform(const GltfCameraEntry& cam)
{
	if (!_primaryCamera)
		return;

	QVector3D worldPos = cam.worldPosition;
	QVector3D worldDir = cam.worldDirection.normalized();
	QVector3D worldUp  = cam.worldUp.normalized();

	// Follow the user model-level transform of the camera's own file — the
	// same matrix the meshes and lights follow.  modelScale also widens the
	// orthographic view range below so the framing tracks a scaled model.
	float modelScale = 1.0f;
	{
		QMatrix4x4 userTransform;
		if (userModelTransformForFile(_animCtrl.activeGltfCameraFile(), userTransform))
		{
			modelScale = uniformScaleOf(userTransform);
			if (cam.needsModelTransformCompensation)
			{
				worldPos = userTransform.map(worldPos);
				worldDir = userTransform.mapVector(worldDir).normalized();
				worldUp  = userTransform.mapVector(worldUp).normalized();
			}
		}
	}

	if (cam.type == GltfCameraType::Perspective)
	{
		_primaryCamera->setProjectionType(Camera::ProjectionType::PERSPECTIVE);
		_primaryCamera->setFOV(qRadiansToDegrees(cam.fovYRadians));

		if (cam.capturedViewRange >= 0.0f)
		{
			// Captured view: restore the exact range recorded at capture
			// time rather than re-deriving one - see GltfCameraData.h for
			// why the heuristic below isn't reliable for an arbitrarily
			// navigated view.
			_primaryCamera->setViewRange(cam.capturedViewRange);
		}
		else
		{
			// Authored glTF camera: no recorded range to restore, so derive
			// one that lands the orbit pivot at the scene centre, matching
			// the glTF author's framing intent.
			// orbitDist ≈ viewRange * 1.25 (maxShiftFactor clamp in computeViewShift),
			// so: viewRange = distToScene / 1.25.
			const QVector3D sceneCenter(
				static_cast<float>(_viewCtrl.boundingSphere().getCenter().x()),
				static_cast<float>(_viewCtrl.boundingSphere().getCenter().y()),
				static_cast<float>(_viewCtrl.boundingSphere().getCenter().z()));
			const float distToScene = QVector3D::dotProduct(sceneCenter - worldPos, worldDir);
			// Clamp to at least scene radius so we never zoom in past the model.
			const float clampedDist = std::max(distToScene, _viewCtrl.boundingSphere().getRadius());
			_primaryCamera->setViewRange(clampedDist / 1.25f);
		}
		_viewCtrl.setProjection(ViewProjection::PERSPECTIVE);
		_viewCtrl.setPreviousProjection(Camera::ProjectionType::PERSPECTIVE);
	}
	else
	{
		_primaryCamera->setProjectionType(Camera::ProjectionType::ORTHOGRAPHIC);
		const float orthoRange = (cam.capturedViewRange >= 0.0f)
			? cam.capturedViewRange
			: std::max(cam.xMag, cam.yMag) * 2.0f * modelScale;
		_primaryCamera->setViewRange(std::max(orthoRange, 0.0001f));
		_viewCtrl.setProjection(ViewProjection::ORTHOGRAPHIC);
		_viewCtrl.setPreviousProjection(Camera::ProjectionType::ORTHOGRAPHIC);
	}

	_viewCtrl.setViewRange(_primaryCamera->getViewRange());
	_viewCtrl.syncCurrentViewRange();

	const QVector3D right = QVector3D::crossProduct(worldDir, worldUp).normalized();
	const QVector3D pivotPos = (_primaryCamera->getMode() == Camera::CameraMode::Orbit)
		? worldPos + worldDir * (_primaryCamera->getProjectionType() == Camera::ProjectionType::ORTHOGRAPHIC
			? _primaryCamera->getOrthoViewDistance()
			: _primaryCamera->getOrbitDistance())
		: worldPos;
	_primaryCamera->setView(pivotPos, worldDir, worldUp, right);

	// Rebuilds _viewCtrl's own cached projection matrix from _primaryCamera
	// (Codex audit catch): this function changes projection type/FOV/view
	// range directly on _primaryCamera, whose own _projectionMatrix is
	// already correct the instant those setters return (Camera::setFOV()/
	// setViewRange()/setProjectionType() each call updateProjectionMatrix()
	// synchronously) - RtSceneBuilder::buildCamera() reads straight from that,
	// so RT was never actually at risk. But _viewCtrl keeps its own SEPARATE
	// cached copy (_viewCtrl.projectionMatrix(), what raster rendering
	// actually uses), refreshed only via syncMatricesFromCamera() inside
	// resizeGL() - without this call, raster kept showing the projection
	// from BEFORE this camera switch until something unrelated happened to
	// trigger the next resizeGL(). Calling it here with the unchanged
	// width()/height() is cheap and already this codebase's convention (every
	// scripted view-animation tick does the same).
	resizeGL(width(), height());

	// No notifyCameraJumpNonInteractive() here anymore (Codex-
	// prompted audit catch): this function is called both as a genuine
	// one-shot jump (activateGltfCamera(), reapplyGltfCameraAfterTransform())
	// AND, every single frame, from applyAnimatedCamera() - itself only ever
	// called from within applyAnimationPose()'s per-frame clip sampling,
	// which already notifies exactly once (notifyRayTracedAnimationMutated()
	// -> notifyContentAnimationTick(), the keep-the-interactive-session-alive
	// path) at the end of THAT function, regardless of which channels the
	// clip touched. This function used to unconditionally fire the "jump"
	// notify (a hard teardown) immediately before that keep-alive attempt on
	// every animated-camera frame, which tore the interactive session down
	// right before the very code path that exists to keep it alive got a
	// chance to run - so any glTF file with an animated camera silently lost
	// the "stay interactive through playback" behavior every other animated
	// channel already got, falling back to raster for the whole clip instead.
	// The two genuine one-shot callers now notify for themselves (see their
	// own call sites) only when they are NOT about to call
	// applyAnimationPose() right after.
}

void ViewportWidget::refreshAnimationMaterialState(const QString& sourceFile)
{
	RuntimeAnimationFileState& runtime = _animCtrl.runtimeAnimationsByFile()[sourceFile];
	if (runtime.data.sourceFile.isEmpty())
		runtime.data = _viewer && _viewer->sceneGraph()
			? _viewer->sceneGraph()->animationDataForFile(sourceFile)
			: GltfAnimationData();

	runtime.defaultMeshMaterials.clear();

	const std::vector<SceneMesh*>& meshes = getMeshStore();
	for (SceneMesh* mesh : meshes)
	{
		if (!mesh || mesh->getSourceFile() != sourceFile)
			continue;

		runtime.defaultMeshMaterials.insert(mesh->uuid(), mesh->getMaterial());
	}

	if (_animCtrl.activeAnimationFile() == sourceFile && _animCtrl.activeAnimationClip() >= 0)
		applyAnimationPose(sourceFile, _animCtrl.activeAnimationClip(), _animCtrl.animationCurrentTimeSeconds());
}

void ViewportWidget::onAnimationTick()
{
	if (!_animCtrl.isPlaying() || _animCtrl.activeAnimationFile().isEmpty() || _animCtrl.activeAnimationClip() < 0)
		return;

	const RuntimeAnimationFileState runtime = _animCtrl.runtimeAnimationsByFile().value(_animCtrl.activeAnimationFile());
	if (_animCtrl.activeAnimationClip() >= runtime.data.clips.size())
		return;

	const double deltaSeconds = _animCtrl.animationElapsed().isValid()
		? static_cast<double>(_animCtrl.animationElapsed().restart()) / 1000.0
		: 0.016;
	const GltfAnimationClip& clip = runtime.data.clips[_animCtrl.activeAnimationClip()];
	if (clip.durationSeconds <= 0.0)
		return;

	_animCtrl.setAnimationCurrentTimeSeconds(_animCtrl.animationCurrentTimeSeconds() + deltaSeconds * _animCtrl.playbackSpeed());
	if (_animCtrl.animationCurrentTimeSeconds() >= clip.durationSeconds)
	{
		if (_animCtrl.isLooping())
			_animCtrl.setAnimationCurrentTimeSeconds(std::fmod(_animCtrl.animationCurrentTimeSeconds(), clip.durationSeconds));
		else
		{
			_animCtrl.setAnimationCurrentTimeSeconds(clip.durationSeconds);
			_animCtrl.setPlaying(false);
			_animCtrl.animationTimer()->stop();
		}
	}

	applyAnimationPose(_animCtrl.activeAnimationFile(), _animCtrl.activeAnimationClip(), _animCtrl.animationCurrentTimeSeconds());
	emit animationStateChanged();
}

void ViewportWidget::resetAnimationPose(const QString& sourceFile)
{
	if (!_viewer || !_viewer->sceneGraph())
		return;

	SceneNode* fileNode = _viewer->sceneGraph()->findFileNode(sourceFile);
	if (!fileNode)
		return;

	const RuntimeAnimationFileState runtime = _animCtrl.runtimeAnimationsByFile().value(sourceFile);
	const bool needsRuntimeNodeTransforms =
		runtime.data.hasNodeAnimations || runtime.data.hasSkinning;
	const std::vector<SceneMesh*>& meshes = getMeshStore();

	if (needsRuntimeNodeTransforms)
	{
		std::function<void(SceneNode*, const QMatrix4x4&)> applyNode =
			[&](SceneNode* node, const QMatrix4x4& parentWorld)
		{
			if (!node)
				return;

			const QMatrix4x4 local = AnimationRuntimeController::aiToQMatrix(node->localTransform);
			const QMatrix4x4 world = parentWorld * local;
			for (const QUuid& uuid : node->meshUuids)
			{
				if (SceneMesh* mesh = getMeshByUuid(uuid))
				{
					if (!mesh->hasSkinning())
						mesh->setSceneRenderTransformFast(world);
					else
						mesh->setJointPalette({});
				}
			}

			for (SceneNode* child : node->children)
				applyNode(child, world);
		};

		for (SceneNode* child : fileNode->children)
			applyNode(child, QMatrix4x4());
	}

	for (auto it = runtime.defaultMeshMaterials.constBegin(); it != runtime.defaultMeshMaterials.constEnd(); ++it)
	{
		if (SceneMesh* mesh = getMeshByUuid(it.key()))
		{
			if (mesh->hasMorphTargets())
				mesh->resetMorphTargets();
			mesh->setMaterial(it.value());
		}
	}

	for (SceneMesh* mesh : meshes)
	{
		if (!mesh || mesh->getSourceFile() != sourceFile || !mesh->hasMorphTargets())
			continue;
		mesh->resetMorphTargets();
	}

	if (!runtime.data.lightBindings.isEmpty())
	{
		clearAnimatedLightTransformState(sourceFile);
		const QHash<int, bool> effectiveVisibility =
			_animCtrl.buildEffectiveNodeVisibility(runtime, QHash<int, bool>{});
		const QVector<bool> visibleByParsedLight =
			_animCtrl.buildAnimatedLightVisibilityMask(runtime, effectiveVisibility);
		setAnimatedLightVisibilityState(sourceFile, visibleByParsedLight);
	}
	else
	{
		clearAnimatedLightTransformState(sourceFile);
		clearAnimatedLightVisibilityState(sourceFile);
	}

	if (!runtime.data.nodeVisibilityStates.isEmpty())
	{
		const QHash<int, bool> effectiveVisibility =
			_animCtrl.buildEffectiveNodeVisibility(runtime, QHash<int, bool>{});
		const QSet<QUuid> hiddenMeshUuids =
			_animCtrl.collectHiddenAnimatedMeshUuids(runtime, effectiveVisibility, fileNode);
		setAnimatedMeshVisibilityState(sourceFile, hiddenMeshUuids);
	}
	else
	{
		clearAnimatedMeshVisibilityState(sourceFile);
	}

	// Same reasoning as applyAnimationPose()'s identical call - resetting to
	// bind pose/default material/visible-again is just as real a content
	// change as animating away from it, and this function is what
	// applyAnimationPose() itself calls (then returns immediately) whenever
	// clipIndex < 0, so without this call here a PT session would never
	// learn a "reset" happened at all. But unlike an arbitrary scene edit,
	// this is still part of the animation system, so GPU PT should try to
	// stay live instead of dropping straight to raster/PBR.
	notifyRayTracedAnimationMutated();
	update();
}

void ViewportWidget::updateAnimatedMeshState(const QString& sourceFile,
	const QHash<QUuid, QMatrix4x4>& worldTransformsByNodeUuid)
{
	if (!_viewer || !_viewer->sceneGraph())
		return;

	SceneNode* fileNode = _viewer->sceneGraph()->findFileNode(sourceFile);
	if (!fileNode)
		return;

	const RuntimeAnimationFileState runtime = _animCtrl.runtimeAnimationsByFile().value(sourceFile);
	if (!runtime.data.hasNodeAnimations && !runtime.data.hasSkinning)
		return;

	std::function<void(SceneNode*)> applyToMeshes = [&](SceneNode* node)
	{
		if (!node)
			return;

		const QMatrix4x4 world = worldTransformsByNodeUuid.value(node->nodeUuid, AnimationRuntimeController::aiToQMatrix(node->localTransform));
		for (const QUuid& uuid : node->meshUuids)
		{
			if (SceneMesh* mesh = getMeshByUuid(uuid))
			{
				if (!mesh->hasSkinning())
				{
					mesh->setSceneRenderTransformFast(world);
				}
				else
				{
					QVector<QMatrix4x4> palette;
					palette.reserve(mesh->skinJoints().size());
					const QMatrix4x4 meshWorldInverse = world.inverted();
					for (const GltfSkinJoint& joint : mesh->skinJoints())
					{
						const QUuid jointNodeUuid = AnimationRuntimeController::resolveRuntimeNodeUuid(runtime, -1, joint.nodeName);
						const QMatrix4x4 jointWorld = worldTransformsByNodeUuid.value(jointNodeUuid, world);
						palette.append(meshWorldInverse * jointWorld * AnimationRuntimeController::aiToQMatrix(joint.inverseBindMatrix));
					}
					mesh->setJointPalette(palette);
					mesh->setSceneRenderTransformFast(world);
				}
			}
		}

		for (SceneNode* child : node->children)
			applyToMeshes(child);
	};

	for (SceneNode* child : fileNode->children)
		applyToMeshes(child);

	// Mutations above (setSceneRenderTransformFast()'s rigid node transform,
	// setJointPalette()'s per-frame skinning pose) feed directly into
	// RtSceneBuilder::convertGeometry()'s next flatten pass - PT invalidation
	// for these, and for every OTHER animation-driven mutation this same
	// tick may also apply (morph weights, KHR_animation_pointer material/UV
	// changes, node visibility, light transforms, camera), is now handled
	// once at the end of applyAnimationPose()/resetAnimationPose() instead
	// of narrowly here (this function's own hasNodeAnimations||hasSkinning
	// guard above previously meant a pointer-only or morph-only clip - no
	// node/skinning channels at all - never invalidated PT, even though
	// applyAnimatedMaterialChanges()/applyMorphTargetWeights() etc. were
	// still visibly changing the raster mesh underneath it).
}

void ViewportWidget::applyNodeTransformsToMeshes(
	const QString& sourceFile,
	const AnimationRuntimeController::RuntimeAnimationFileState& runtime,
	AnimationRuntimeController::AnimationSampleResult& result,
	SceneNode* fileNode)
{
	if (!fileNode || !(runtime.data.hasNodeAnimations || runtime.data.hasSkinning))
		return;

	std::function<void(SceneNode*, const QMatrix4x4&)> evalNode =
		[&](SceneNode* node, const QMatrix4x4& parentWorld)
	{
		if (!node)
			return;

		const RuntimeNodeTransform nodeTransform =
			result.nodeTransforms.value(node->nodeUuid, AnimationRuntimeController::decomposeNodeTransform(node->localTransform));
		const QMatrix4x4 local = AnimationRuntimeController::composeNodeTransform(nodeTransform);
		const QMatrix4x4 world = parentWorld * local;
		result.worldTransforms.insert(node->nodeUuid, world);

		for (SceneNode* child : node->children)
			evalNode(child, world);
	};

	for (SceneNode* child : fileNode->children)
		evalNode(child, QMatrix4x4());

	updateAnimatedMeshState(sourceFile, result.worldTransforms);

	for (auto meshIt = result.meshTransforms.cbegin(); meshIt != result.meshTransforms.cend(); ++meshIt)
	{
		SceneMesh* mesh = getMeshByUuid(meshIt.key());
		const SceneNode* ownerNode = mesh ? _viewer->sceneGraph()->findNodeForMesh(meshIt.key()) : nullptr;
		if (!mesh || !ownerNode || mesh->getSourceFile() != sourceFile)
			continue;

		const QMatrix4x4 ownerWorld = result.worldTransforms.value(ownerNode->nodeUuid,
			AnimationRuntimeController::aiToQMatrix(ownerNode->localTransform));
		QMatrix4x4 meshLocal;
		meshLocal.translate(meshIt->translation);
		meshLocal.rotate(meshIt->rotation);
		meshLocal.scale(meshIt->scale);
		mesh->setSceneRenderTransformFast(ownerWorld * meshLocal);
	}
}

void ViewportWidget::applyMorphTargetWeights(
	const QString& sourceFile,
	const AnimationRuntimeController::AnimationSampleResult& result)
{
	const std::vector<SceneMesh*>& meshes = getMeshStore();
	for (SceneMesh* mesh : meshes)
	{
		if (!mesh || mesh->getSourceFile() != sourceFile || !mesh->hasMorphTargets())
			continue;

		const SceneNode* ownerNode = _viewer->sceneGraph()->findNodeForMesh(mesh->uuid());
		if (!ownerNode)
		{
			mesh->resetMorphTargets();
			continue;
		}

		const SceneNode* lookupNode = ownerNode;
		if (ownerNode->parent
			&& ownerNode->name.endsWith(QStringLiteral("_node"))
			&& ownerNode->parent->name == ownerNode->name.left(ownerNode->name.length() - 5))
		{
			lookupNode = ownerNode->parent;
		}
		const QVector<float> weights = result.morphWeights.value(lookupNode->nodeUuid);
		if (!weights.isEmpty())
			mesh->applyMorphWeights(weights);
		else
			mesh->resetMorphTargets();
	}
}

void ViewportWidget::applyAnimatedMaterialChanges(const AnimationRuntimeController::AnimationSampleResult& result)
{
	for (auto it = result.animatedMaterials.constBegin(); it != result.animatedMaterials.constEnd(); ++it)
	{
		if (SceneMesh* mesh = getMeshByUuid(it.key()))
			mesh->setMaterial(it.value());
	}
}

void ViewportWidget::applyAnimatedMeshVisibility(
	const QString& sourceFile,
	const AnimationRuntimeController::RuntimeAnimationFileState& runtime,
	const AnimationRuntimeController::AnimationSampleResult& result,
	SceneNode* fileNode)
{
	if (!fileNode || runtime.data.nodeVisibilityStates.isEmpty())
	{
		clearAnimatedMeshVisibilityState(sourceFile);
		return;
	}

	const QHash<int, bool> effectiveVisibility =
		_animCtrl.buildEffectiveNodeVisibility(runtime, result.nodeVisibility);
	const QSet<QUuid> hiddenMeshUuids =
		_animCtrl.collectHiddenAnimatedMeshUuids(runtime, effectiveVisibility, fileNode);
	setAnimatedMeshVisibilityState(sourceFile, hiddenMeshUuids);
}

void ViewportWidget::applyAnimatedLightTransforms(
	const QString& sourceFile,
	const AnimationRuntimeController::RuntimeAnimationFileState& runtime,
	const AnimationRuntimeController::AnimationSampleResult& result,
	SceneNode* fileNode)
{
	std::vector<GPULight> animatedLights;
	if (_animCtrl.buildAnimatedLightTransformState(
		sourceFile,
		runtime,
		result,
		fileNode,
		animatedLights))
	{
		setAnimatedLightTransformState(sourceFile, animatedLights);
	}
	else
	{
		clearAnimatedLightTransformState(sourceFile);
	}

	if (!runtime.data.lightBindings.isEmpty())
	{
		const QHash<int, bool> effectiveVisibility =
			_animCtrl.buildEffectiveNodeVisibility(runtime, result.nodeVisibility);
		const QVector<bool> visibleByParsedLight =
			_animCtrl.buildAnimatedLightVisibilityMask(runtime, effectiveVisibility);
		setAnimatedLightVisibilityState(sourceFile, visibleByParsedLight);
	}
	else
	{
		clearAnimatedLightVisibilityState(sourceFile);
	}
}

void ViewportWidget::applyAnimatedCamera(
	const QString& sourceFile,
	const AnimationRuntimeController::RuntimeAnimationFileState& runtime,
	const AnimationRuntimeController::AnimationSampleResult& result)
{
	if (_animCtrl.activeGltfCameraFile() != sourceFile || _animCtrl.activeGltfCameraIndex() < 0 || !_primaryCamera)
		return;

	const GltfCameraData camData = _viewer->sceneGraph()->gltfCameraDataForFile(sourceFile);
	if (_animCtrl.activeGltfCameraIndex() >= camData.cameras.size())
		return;

	const GltfCameraEntry& cam = camData.cameras[_animCtrl.activeGltfCameraIndex()];
	const QUuid cameraNodeUuid = AnimationRuntimeController::resolveRuntimeNodeUuid(runtime, cam.nodeIndex, cam.nodeName);
	if (cameraNodeUuid.isNull() || !result.worldTransforms.contains(cameraNodeUuid))
		return;

	const QMatrix4x4& world = result.worldTransforms.value(cameraNodeUuid);
	const QVector3D worldPos(world(0, 3), world(1, 3), world(2, 3));
	const QVector3D worldDir = world.mapVector(QVector3D(0.0f, 0.0f, -1.0f)).normalized();
	const QVector3D worldUp = world.mapVector(QVector3D(0.0f, 1.0f, 0.0f)).normalized();
	GltfCameraEntry runtimeCam = cam;
	runtimeCam.worldPosition = worldPos;
	runtimeCam.worldDirection = worldDir;
	runtimeCam.worldUp = worldUp;
	applyGltfCameraEntryTransform(runtimeCam);
}

void ViewportWidget::applyAnimationPose(const QString& sourceFile, int clipIndex, double timeSeconds)
{
	if (!_viewer || !_viewer->sceneGraph())
		return;
	SceneGraph* sceneGraph = _viewer->sceneGraph();

	RuntimeAnimationFileState& runtime = _animCtrl.runtimeAnimationsByFile()[sourceFile];
	if (runtime.data.sourceFile.isEmpty())
		runtime.data = sceneGraph->animationDataForFile(sourceFile);

	if (clipIndex < 0 || clipIndex >= runtime.data.clips.size())
	{
		resetAnimationPose(sourceFile);
		return;
	}

	SceneNode* fileNode = sceneGraph->findFileNode(sourceFile);
	if (!fileNode)
		return;

	const GltfAnimationClip& clip = runtime.data.clips[clipIndex];
	const AnimationRuntimeController::AnimationSampleResult sample =
		_animCtrl.sampleClip(runtime, clip, timeSeconds, sceneGraph);
	const bool animationAffectsShadowCasters = sample.affectsShadowCasters;
	AnimationRuntimeController::AnimationSampleResult mutableSample = sample;
	applyNodeTransformsToMeshes(sourceFile, runtime, mutableSample, fileNode);
	applyMorphTargetWeights(sourceFile, mutableSample);
	applyAnimatedMaterialChanges(mutableSample);
	applyAnimatedMeshVisibility(sourceFile, runtime, mutableSample, fileNode);
	applyAnimatedLightTransforms(sourceFile, runtime, mutableSample, fileNode);
	applyAnimatedCamera(sourceFile, runtime, mutableSample);
	if (animationAffectsShadowCasters)
		_renderCtrl.setShadowMapNeedsInitialization(true);

	// Covers every category the calls above may have just changed - node/
	// skinning transforms, morph target weights, KHR_animation_pointer
	// material/UV-transform values, node visibility, light transforms/
	// visibility, camera parameters. Unlike a generic scene edit, animation
	// playback is now allowed to keep the interactive GPU PT path live by
	// rebuilding that renderer against the new scene revision, instead of
	// unconditionally dropping to raster/PBR for the whole clip.
	// Called unconditionally (not gated on which specific channels this clip
	// has) since this function already only runs when a clip is genuinely
	// being sampled/applied.
	notifyRayTracedAnimationMutated();
	update();
}

void ViewportWidget::onMeshBatchReady(const std::vector<AssImpMeshData>& batch)
{
	makeCurrent();
	for (const AssImpMeshData& meshData : batch)
	{
		SceneMesh* mesh = createMeshFromData(meshData);
		addToDisplay(mesh);
		_sceneRuntime.pendingSceneUuids().append(mesh->uuid());
	}
	_viewer->updateDisplayList();

	// Progressive AssImp loading emits batches from a worker thread via
	// BlockingQueuedConnection. Yield once here so paint/update events run
	// before the next batch arrives, making meshes appear incrementally.
	if (_sceneRuntime.progressiveLoadingEnabled())
		QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

UVMethod ViewportWidget::promptLargeModelUVDecision(int totalTriangles, UVMethod currentMethod)
{
	QMessageBox msgBox(this);
	msgBox.setWindowTitle(tr("Performance Warning!"));
	msgBox.setText(tr("The model contains more than %1 triangles and the current method of UV generation is \"Smart UV\" which is time consuming.\nDo you want to continue generating the UV?")
		.arg(totalTriangles));
	msgBox.setIcon(QMessageBox::Question);

	QPushButton* yesButton = msgBox.addButton(QMessageBox::Yes);
	QPushButton* noButton = msgBox.addButton(QMessageBox::No);
	QPushButton* changeSettingsButton = msgBox.addButton(tr("Change Settings"), QMessageBox::ActionRole);

	msgBox.setDefaultButton(QMessageBox::Yes);
	msgBox.exec();

	if (msgBox.clickedButton() == noButton)
	{
		qDebug() << "User chose not to generate UVs, using None method.";
		return UVMethod::None;
	}

	if (msgBox.clickedButton() == changeSettingsButton)
	{
		return ModelViewer::askUserForUVMethod(this).method;
	}

	Q_UNUSED(yesButton);
	return currentMethod;
}

GLuint ViewportWidget::createGPUTextureFromImage(const QImage& image, const TextureSamplerSettings& samplers)
{
	if (image.isNull())
	{
		return 0;
	}

	GLenum internalFormat = GL_RGBA8;
	GLenum dataFormat = GL_RGBA;
	GLenum dataType = GL_UNSIGNED_BYTE;

	QImage glImage;

	switch (image.format())
	{
	case QImage::Format_RGB888:
		glImage = image;
		internalFormat = GL_RGB8;
		dataFormat = GL_RGB;
		break;

	case QImage::Format_RGBA8888:
	case QImage::Format_RGBA8888_Premultiplied:
		glImage = image;
		internalFormat = GL_RGBA8;
		dataFormat = GL_RGBA;
		break;

	case QImage::Format_Grayscale8:
		// Expand to RGBA so all three colour channels are populated.
		// Uploading as GL_RED leaves G and B at 0, making the texture appear red.
		glImage = image.convertToFormat(QImage::Format_RGBA8888);
		internalFormat = GL_RGBA8;
		dataFormat = GL_RGBA;
		break;

	case QImage::Format_Indexed8:
		glImage = image.convertToFormat(QImage::Format_RGBA8888);
		internalFormat = GL_RGBA8;
		dataFormat = GL_RGBA;
		break;

	default:
		glImage = image.convertToFormat(QImage::Format_RGBA8888);
		internalFormat = GL_RGBA8;
		dataFormat = GL_RGBA;
		break;
	}

	GLuint textureID;
	glGenTextures(1, &textureID);

	glBindTexture(GL_TEXTURE_2D, textureID);
	glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, glImage.width(), glImage.height(), 0,
		dataFormat, dataType, glImage.constBits());
	glGenerateMipmap(GL_TEXTURE_2D);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, samplers.wrapS);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, samplers.wrapT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, samplers.minFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, samplers.magFilter);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, _renderCtrl.anisotropicFilteringLevel());

	return textureID;
}

GLuint ViewportWidget::uploadDecodedTexture(Material::Texture& texture, const QImage& image)
{
	TextureSamplerSettings samplers{ texture.wrapS, texture.wrapT, texture.minFilter, texture.magFilter };
	GLuint textureId = uploadDecodedTextureImage(image, samplers);
	texture.id = textureId;
	return textureId;
}

GLuint ViewportWidget::uploadDecodedTextureImage(const QImage& image, const TextureSamplerSettings& samplers)
{
	makeCurrent();
	return createGPUTextureFromImage(image, samplers);
}

GLuint ViewportWidget::uploadKtx2TextureImage(const QString& path, const std::string& mapType, const TextureSamplerSettings& samplers,
	QImage* outDecodedImage)
{
	if (path.isEmpty())
	{
		return 0;
	}

	makeCurrent();

	TranscodedTexture transcodedTexture;
	if (!_ktx2Loader.loadKTX2(path.toStdString(), transcodedTexture, _gpuCapabilities, mapType))
	{
		qWarning() << "ViewportWidget::uploadKtx2Texture - Failed to load KTX2 file:" << path;
		return 0;
	}

	GLuint textureId = _ktx2Loader.uploadToGPU(transcodedTexture);
	if (textureId == 0)
	{
		qWarning() << "ViewportWidget::uploadKtx2Texture - Failed to upload KTX2 texture:" << path;
		return 0;
	}

	glBindTexture(GL_TEXTURE_2D, textureId);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, samplers.minFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, samplers.magFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, samplers.wrapS);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, samplers.wrapT);
	glBindTexture(GL_TEXTURE_2D, 0);

	// KTX2/Basis Universal textures used to only ever produce a GPU-resident
	// texture handle here, with no CPU-side decoded pixels anywhere - fine
	// for raster (which only ever needs the GL handle), but RtSceneBuilder::
	// extractTextureSample() needs real CPU pixels for the software path
	// tracer, and its cache-lookup fallback (SceneRuntime::texCache()'s
	// per-path CachedTextureEntry::image) was always finding a null QImage
	// for KTX2 textures - extractTextureSample() then silently treated the
	// texture as absent, falling back to the material's flat factor (often
	// white), which is why KTX2-textured materials rendered flat/blown-out
	// in PT while looking correct in raster. Only handled for the default
	// uncompressed transcode target (cTFRGBA32) - that's already tightly-
	// packed RGBA8 data, directly wrappable as a QImage. BC7/ASTC-compressed
	// transcode targets (an explicit user compression-mode choice, not the
	// default) still have no CPU-side representation; decoding those back to
	// raw pixels for PT is a separate, narrower follow-up if it matters.
	if (outDecodedImage && !transcodedTexture.isCompressed &&
		transcodedTexture.format == basist::transcoder_texture_format::cTFRGBA32 &&
		transcodedTexture.width > 0 && transcodedTexture.height > 0 &&
		transcodedTexture.data.size() >= static_cast<size_t>(transcodedTexture.width) * transcodedTexture.height * 4)
	{
		const QImage view(transcodedTexture.data.data(),
			static_cast<int>(transcodedTexture.width), static_cast<int>(transcodedTexture.height),
			static_cast<int>(transcodedTexture.width) * 4, QImage::Format_RGBA8888);
		*outDecodedImage = view.copy(); // deep copy - transcodedTexture.data is about to go out of scope
	}

	return textureId;
}

GLuint ViewportWidget::uploadKtx2Texture(const QString& path, const std::string& mapType, Material::Texture& texture)
{
	TextureSamplerSettings samplers{ texture.wrapS, texture.wrapT, texture.minFilter, texture.magFilter };
	GLuint textureId = uploadKtx2TextureImage(path, mapType, samplers);
	texture.id = textureId;
	return textureId;
}

unsigned int ViewportWidget::getOrCreateTextureCached(
	const QString& cacheKey,
	const QImage& image,
	const TextureSamplerSettings& samplers)
{
	if (cacheKey.isEmpty() || image.isNull())
	{
		return 0;
	}

	auto it = _sceneRuntime.texCache().find(cacheKey);
	if (it != _sceneRuntime.texCache().end())
	{
		CachedTextureEntry& entry = it->second;
		if (entry.lastGPUTexture != 0 && entry.lastSamplerSettings == samplers)
		{
			retainTexture(entry.lastGPUTexture);
			return entry.lastGPUTexture;
		}

		if (!entry.image.isNull())
		{
			makeCurrent();
			GLuint newTexID = createGPUTextureFromImage(entry.image, samplers);
			if (newTexID != 0)
			{
				entry.lastGPUTexture = newTexID;
				entry.lastSamplerSettings = samplers;
				_sceneRuntime.texRefCount()[newTexID] = 1;
				return newTexID;
			}
		}
	}

	makeCurrent();
	GLuint texID = createGPUTextureFromImage(image, samplers);
	if (texID == 0)
	{
		return 0;
	}

	CachedTextureEntry newEntry;
	newEntry.image = image;
	newEntry.lastGPUTexture = texID;
	newEntry.lastSamplerSettings = samplers;
	newEntry.imageWidth = image.width();
	newEntry.imageHeight = image.height();

	_sceneRuntime.texCache()[cacheKey] = newEntry;
	_sceneRuntime.texRefCount()[texID] = 1;
	return texID;
}

unsigned int ViewportWidget::getOrLoadKtx2TextureCached(
	const QString& path,
	const std::string& mapType,
	const TextureSamplerSettings& samplers)
{
	if (path.isEmpty())
	{
		return 0;
	}

	const QString cacheKey = QStringLiteral("ktx2://%1::%2")
		.arg(path, QString::fromStdString(mapType));

	auto it = _sceneRuntime.texCache().find(cacheKey);
	if (it != _sceneRuntime.texCache().end())
	{
		CachedTextureEntry& entry = it->second;
		if (entry.lastGPUTexture != 0 && entry.lastSamplerSettings == samplers)
		{
			retainTexture(entry.lastGPUTexture);
			return entry.lastGPUTexture;
		}
	}

	// See uploadKtx2TextureImage()'s doc comment - decodedImage is the CPU-
	// side pixel data (uncompressed-transcode case only) that RtSceneBuilder::
	// extractTextureSample() needs for the software path tracer; this cache
	// entry's .image field is exactly what that lookup reads.
	QImage decodedImage;
	GLuint texID = uploadKtx2TextureImage(path, mapType, samplers, &decodedImage);
	if (texID == 0)
	{
		return 0;
	}

	CachedTextureEntry& entry = _sceneRuntime.texCache()[cacheKey];
	entry.lastGPUTexture = texID;
	entry.lastSamplerSettings = samplers;
	entry.image = decodedImage;
	entry.imageWidth = decodedImage.width();
	entry.imageHeight = decodedImage.height();

	// RtSceneBuilder::extractTextureSample()'s Tier 3 fallback looks the
	// decoded image up by the material's plain glTF-declared map path (it
	// has no notion of this cache's "ktx2://<path>::<mapType>" dedup key
	// convention, and doesn't know mapType at all) - mirror the same
	// CachedTextureEntry under the plain path too, so that lookup finds it.
	// A second copy of the QImage (implicitly shared, so cheap) rather than
	// teaching RtSceneBuilder about this cache's internal key format.
	if (!decodedImage.isNull())
		_sceneRuntime.texCache()[path] = entry;
	_sceneRuntime.texRefCount()[texID] = 1;
	return texID;
}

unsigned int ViewportWidget::getOrLoadTextureCached(
	const QString& path,
	const TextureSamplerSettings& samplers)
{
	if (path.isEmpty()) return 0;

	auto it = _sceneRuntime.texCache().find(path);

	// Cache hit - image exists
	if (it != _sceneRuntime.texCache().end())
	{
		CachedTextureEntry& entry = it->second;

		// Exact match (same image + same samplers)
		if (entry.lastGPUTexture != 0 && entry.lastSamplerSettings == samplers)
		{
			retainTexture(entry.lastGPUTexture);
			return entry.lastGPUTexture;
		}

		// Same image, different samplers - create new GPU texture from cached image
		if (!entry.image.isNull())
		{
			makeCurrent();
			GLuint newTexID = createGPUTextureFromImage(entry.image, samplers);
			if (newTexID != 0)
			{
				// CRITICAL FIX: Release the old texture before replacing it
				// Without this, orphaned GPU texture IDs cause context corruption
				GLuint oldTexID = entry.lastGPUTexture;
				if (oldTexID != 0)
				{
					releaseTexture(oldTexID);
				}

				entry.lastGPUTexture = newTexID;
				entry.lastSamplerSettings = samplers;
				_sceneRuntime.texRefCount()[newTexID] = 1;
				return newTexID;
			}
		}
	}

	// Cache miss - image not cached, load from disk
	makeCurrent();
	GLuint texID = loadTextureFromFile(
		path.toStdString().c_str(),
		samplers.wrapS,
		samplers.wrapT,
		samplers.minFilter,
		samplers.magFilter,
		false);

	if (texID == 0) return 0;

	// Cache the image for future use with different samplers
	CachedTextureEntry newEntry;
	newEntry.image = QImage(path);  // Cache the image
	newEntry.lastGPUTexture = texID;
	newEntry.lastSamplerSettings = samplers;
	newEntry.imageWidth = newEntry.image.width();
	newEntry.imageHeight = newEntry.image.height();

	_sceneRuntime.texCache()[path] = newEntry;
	_sceneRuntime.texRefCount()[texID] = 1;

	return texID;
}

void ViewportWidget::retainTexture(unsigned int texId)
{
	if (texId == 0) return;
	auto it = _sceneRuntime.texRefCount().find(texId);
	if (it != _sceneRuntime.texRefCount().end()) it->second++;
	else _sceneRuntime.texRefCount()[texId] = 1;
}

void ViewportWidget::releaseTexture(unsigned int texId)
{
	if (texId == 0) return;
	auto it = _sceneRuntime.texRefCount().find(texId);
	if (it == _sceneRuntime.texRefCount().end()) return;
	if (--(it->second) <= 0)
	{
		// remove from path map too
		for (auto pit = _sceneRuntime.texCache().begin(); pit != _sceneRuntime.texCache().end(); )
		{
			if (pit->second.lastGPUTexture == texId) pit = _sceneRuntime.texCache().erase(pit); else ++pit;
		}
		glDeleteTextures(1, &texId);
		_sceneRuntime.texRefCount().erase(texId);
	}
}

Material ViewportWidget::resolveMaterialTextures(ViewportWidget* w, const Material& src)
{
	Material m = src;
	auto resolveTexturePath = [w](const QString& path,
		const std::string& mapType,
		const TextureSamplerSettings& samplers) -> unsigned int
	{
		if (path.isEmpty())
			return 0;
		if (path.endsWith(".ktx2", Qt::CaseInsensitive))
			return w->getOrLoadKtx2TextureCached(path, mapType, samplers);
		return w->getOrLoadTextureCached(path, samplers);
	};
	auto resolveTexturePathOrKeepId = [&](const QString& path,
		const std::string& mapType,
		const TextureSamplerSettings& samplers,
		int currentId) -> unsigned int
	{
		if (path.isEmpty())
			return static_cast<unsigned int>(std::max(0, currentId));
		return resolveTexturePath(path, mapType, samplers);
	};

	if (m.hasAlbedoMap())
	{
		const Material::Texture& tex = m.texture(Material::TextureType::Albedo);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setAlbedoTextureId(resolveTexturePathOrKeepId(m.albedoMapPath(), "albedoMap", samplers, m.albedoTextureId()));
	}
	if (m.hasMetallicMap())
	{
		const Material::Texture& tex = m.texture(Material::TextureType::Metallic);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setMetallicTextureId(resolveTexturePathOrKeepId(m.metallicMapPath(), "metallicMap", samplers, m.metallicTextureId()));
	}
	if (m.hasRoughnessMap())
	{
		const Material::Texture& tex = m.texture(Material::TextureType::Roughness);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setRoughnessTextureId(resolveTexturePathOrKeepId(m.roughnessMapPath(), "roughnessMap", samplers, m.roughnessTextureId()));
	}
	if (m.hasNormalMap())
	{
		const Material::Texture& tex = m.texture(Material::TextureType::Normal);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setNormalTextureId(resolveTexturePathOrKeepId(m.normalMapPath(), "normalMap", samplers, m.normalTextureId()));
	}
	if (m.hasAOMap())
	{
		const Material::Texture& tex = m.texture(Material::TextureType::AmbientOcclusion);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setOcclusionTextureId(resolveTexturePathOrKeepId(m.aoMapPath(), "aoMap", samplers, m.occlusionTextureId()));
	}
	if (m.hasOpacityMap())
	{
		const Material::Texture& tex = m.texture(Material::TextureType::Opacity);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setOpacityTextureId(resolveTexturePathOrKeepId(m.opacityMapPath(), "opacityMap", samplers, m.opacityTextureId()));
	}
	if (m.hasHeightMap())
	{
		const Material::Texture& tex = m.texture(Material::TextureType::Height);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setHeightTextureId(resolveTexturePathOrKeepId(m.heightMapPath(), "heightMap", samplers, m.heightTextureId()));
	}
	if (m.hasEmissiveMap())
	{
		const Material::Texture& tex = m.texture(Material::TextureType::Emissive);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setEmissiveTextureId(resolveTexturePathOrKeepId(m.emissiveMapPath(), "emissiveMap", samplers, m.emissiveTextureId()));
	}
	if (m.hasTransmissionMap())
	{
		const Material::Texture& tex = m.texture(Material::TextureType::Transmission);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setTransmissionTextureId(resolveTexturePathOrKeepId(m.transmissionMapPath(), "transmissionMap", samplers, m.transmissionTextureId()));
	}
	if (m.hasIORMap())
	{
		const Material::Texture& tex = m.texture(Material::TextureType::IOR);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setIORTextureId(resolveTexturePathOrKeepId(m.iorMapPath(), "iorMap", samplers, m.iorTextureId()));
	}
	if (m.hasSheenColorMap())
	{
		const Material::Texture& tex = m.texture(Material::TextureType::SheenColor);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setSheenColorTextureId(resolveTexturePathOrKeepId(m.sheenColorMapPath(), "sheenColorMap", samplers, m.sheenColorTextureId()));
	}
	if (m.hasSheenRoughnessMap())
	{
		const Material::Texture& tex = m.texture(Material::TextureType::SheenRoughness);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setSheenRoughnessTextureId(resolveTexturePathOrKeepId(m.sheenRoughnessMapPath(), "sheenRoughnessMap", samplers, m.sheenRoughnessTextureId()));
	}
	if (m.hasClearcoatColorMap())
	{
		const Material::Texture& tex = m.texture(Material::TextureType::ClearcoatColor);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setClearcoatColorTextureId(resolveTexturePath(m.clearcoatColorMapPath(), "clearcoatColorMap", samplers));
	}
	if (m.hasClearcoatRoughnessMap())
	{
		const Material::Texture& tex = m.texture(Material::TextureType::ClearcoatRoughness);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setClearcoatRoughnessTextureId(resolveTexturePath(m.clearcoatRoughnessMapPath(), "clearcoatRoughnessMap", samplers));
	}
	if (m.hasClearcoatNormalMap())
	{
		const Material::Texture& tex = m.texture(Material::TextureType::ClearcoatNormal);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setClearcoatNormalTextureId(resolveTexturePath(m.clearcoatNormalMapPath(), "clearcoatNormalMap", samplers));
	}
	if (m.hasIridescenceMap())
	{
		const Material::Texture& tex = m.texture(Material::TextureType::Iridescence);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setIridescenceTextureId(resolveTexturePath(m.iridescenceMap(), "iridescenceMap", samplers));
	}
	if (m.hasIridescenceThicknessMap())
	{
		const Material::Texture& tex = m.texture(Material::TextureType::IridescenceThickness);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setIridescenceThicknessTextureId(resolveTexturePath(m.iridescenceThicknessMap(), "iridescenceThicknessMap", samplers));
	}
	if (m.hasSpecularColorMap())
	{
		const Material::Texture& tex = m.texture(Material::TextureType::SpecularColor);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setSpecularColorTextureId(resolveTexturePath(m.specularColorMap(), "specularColorMap", samplers));
	}
	if (m.hasSpecularFactorMap())
	{
		const Material::Texture& tex = m.texture(Material::TextureType::SpecularFactor);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setSpecularFactorTextureId(resolveTexturePath(m.specularFactorMap(), "specularFactorMap", samplers));
	}
	if (m.hasAnisotropyMap())
	{
		const Material::Texture& tex = m.texture(Material::TextureType::Anisotropy);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setAnisotropyTextureId(resolveTexturePath(m.anisotropyMap(), "anisotropyMap", samplers));
	}
	if (m.hasThicknessMap())
	{
		const Material::Texture& tex = m.texture(Material::TextureType::Thickness);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setThicknessTextureId(resolveTexturePath(m.thicknessMap(), "thicknessMap", samplers));
	}
	if (m.hasDiffuseMap())
	{
		const Material::Texture& tex = m.texture(Material::TextureType::Diffuse);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setDiffuseTextureId(resolveTexturePath(m.diffuseMap(), "diffuseMap", samplers));
	}
	if (m.hasDiffuseTransmissionMap())
	{
		const Material::Texture& tex = m.texture(Material::TextureType::DiffuseTransmission);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setDiffuseTransmissionTextureId(resolveTexturePath(m.diffuseTransmissionMap(), "diffuseTransmissionMap", samplers));
	}
	if (m.hasDiffuseTransmissionColorMap())
	{
		const Material::Texture& tex = m.texture(Material::TextureType::DiffuseTransmissionColor);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setDiffuseTransmissionColorTextureId(resolveTexturePath(m.diffuseTransmissionColorMap(), "diffuseTransmissionColorMap", samplers));
	}
	if (m.hasSpecularGlossinessMap())
	{
		const Material::Texture& tex = m.texture(Material::TextureType::SpecularGlossiness);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setSpecularGlossinessTextureId(resolveTexturePath(m.specularGlossinessMap(), "specularGlossinessMap", samplers));
	}

	m.syncTextureParameters();

	// Ensure ADS values are recalculated after copy and texture resolution
	// (copy assignment operator at line 6998 doesn't call updateConsistency)
	m.updateConsistency();

	return m;
}

void ViewportWidget::initTransmissionBuffer()
{
	_renderCtrl.initTransmissionBuffer(width(), height());
}

void ViewportWidget::resizeTransmissionBuffer(int width, int height)
{
	_renderCtrl.initTransmissionBuffer(width, height);
}

void ViewportWidget::generateCubemapMipmaps(GLuint cubemapTexture)
{
	_renderCtrl.generateCubemapMipmaps(cubemapTexture, width(), height(), defaultFramebufferObject());
}



void ViewportWidget::renderToTransmissionBuffer(Camera* camera, const QColor& topColor, const QColor& botColor)
{
	if (!_renderCtrl.transmissionEnabled())
		return;

	resizeTransmissionBuffer(width(), height());

	// --- SETUP STATE ---
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glFrontFace(GL_CCW);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glDepthMask(GL_TRUE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glStencilMask(0xFF);
	glDisable(GL_STENCIL_TEST);

	// --- BIND FBO ---
	glBindFramebuffer(GL_FRAMEBUFFER, _renderCtrl.transmissionFBO());
	glViewport(0, 0, _renderCtrl.transmissionTextureWidth(), _renderCtrl.transmissionTextureHeight());

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// --- Setup matrices ---
	_viewCtrl.syncMatricesFromCamera(*_primaryCamera);

	// --- RENDER 1: BACKGROUND (gradient or skybox) ---
	if (_renderCtrl.skyBoxEnabled())
	{
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		drawSkyBox();
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);
	}
	else
	{
		// Render gradient background (same as main framebuffer)
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		gradientBackground(topColor.redF(), topColor.greenF(), topColor.blueF(), topColor.alphaF(),
			botColor.redF(), botColor.greenF(), botColor.blueF(), botColor.alphaF(), _renderCtrl.gradientStyle());
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);
	}

	// --- RENDER 2: OPAQUE MESHES (with clipping) ---
	// Units 32/33: prevent feedback — the transmission FBO is currently the render target,
	// so bind white instead of the real transmission textures.
	glActiveTexture(GL_TEXTURE0 + 32);
	glBindTexture(GL_TEXTURE_2D, _renderCtrl.whiteTexture());
	glActiveTexture(GL_TEXTURE0 + 33);
	glBindTexture(GL_TEXTURE_2D, _renderCtrl.whiteTexture());
	// Units 37/38: SSS irradiance/depth from the SSS capture pass.
	glActiveTexture(GL_TEXTURE0 + 37);
	glBindTexture(GL_TEXTURE_2D, _renderCtrl.sssCaptureTexture() != 0 ? _renderCtrl.sssCaptureTexture() : _renderCtrl.whiteTexture());
	glActiveTexture(GL_TEXTURE0 + 38);
	glBindTexture(GL_TEXTURE_2D, _renderCtrl.sssDepthTexture() != 0 ? _renderCtrl.sssDepthTexture() : _renderCtrl.whiteTexture());
	glActiveTexture(GL_TEXTURE0);

	_renderCtrl.fgShader()->bind();
	setCommonUniforms(_renderCtrl.fgShader(), _primaryCamera);
	drawMeshesWithClipping(_renderCtrl.fgShader(), false); // opaque pass only
	_renderCtrl.fgShader()->release();

	// --- RENDER 3: SECTION CAPS ---
	if (_renderCtrl.cappingEnabled() &&
		!_renderCtrl.sectionCapsSuppressedDuringInteraction() &&
		(_renderCtrl.yzClippingEnabled() || _renderCtrl.zxClippingEnabled() || _renderCtrl.xyClippingEnabled()))
	{
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(1.0f, 1.0f);
		drawSectionCapping();
		glDisable(GL_POLYGON_OFFSET_FILL);
	}

	// --- RENDER 4: GROUND ---
	if (_realismEnabled &&
		_renderCtrl.groundMode() != GroundMode::None && !_renderCtrl.cappingEnabled() &&
		!_sceneRuntime.meshStore().empty() &&
		camera != _orthoViewsCamera)
	{
		if (_renderCtrl.groundMode() == GroundMode::Floor)
		{
			// Avoid sampling from the transmission render target while it is bound
			// as the current framebuffer color attachment.
			glActiveTexture(GL_TEXTURE0 + 32);
			glBindTexture(GL_TEXTURE_2D, _renderCtrl.whiteTexture());
			glActiveTexture(GL_TEXTURE0 + 33);
			glBindTexture(GL_TEXTURE_2D, _renderCtrl.whiteTexture());
			glActiveTexture(GL_TEXTURE0);
			drawFloor(false);
		}
		else if (_renderCtrl.groundMode() == GroundMode::Grid)
		{
			drawGrid();
		}
	}

	// IMPORTANT: After rendering, generate mipmaps
	glBindTexture(GL_TEXTURE_2D, _renderCtrl.transmissionColorTexture());
	glHint(GL_GENERATE_MIPMAP_HINT, GL_NICEST);
	glGenerateMipmap(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, 0);

	// --- UNBIND FBO ---
	glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
	glViewport(0, 0, width(), height());
}

// ============================================================================
// SSS capture pass
// Renders only hasVolumeScattering meshes into _renderCtrl.sssFBO() with sssCapture=true,
// which makes the shader output raw linear diffuse irradiance.
// The result in _renderCtrl.sssCaptureTexture() feeds the blur passes in Sequence 4.
// ============================================================================

void ViewportWidget::renderToSSSBuffer(Camera* camera)
{
	if (!sceneHasVisibleSSSMaterials())
		return;

	resizeSSSBuffer(width(), height());

	// --- SETUP STATE ---
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glFrontFace(GL_CCW);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glDepthMask(GL_TRUE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glStencilMask(0xFF);
	glDisable(GL_STENCIL_TEST);

	// --- BIND FBO ---
	glBindFramebuffer(GL_FRAMEBUFFER, _renderCtrl.sssFBO());
	glViewport(0, 0, _renderCtrl.sssTextureWidth(), _renderCtrl.sssTextureHeight());

	// Black background — non-SSS pixels are discarded by the shader so nothing
	// writes to them; black is the correct additive identity for the blur.
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// --- Setup matrices ---
	_viewCtrl.syncMatricesFromCamera(*camera);

	// Bind white dummy textures on the SSS sampler slots (units 37/38) so the shader's
	// sampleCapturedSSSDiffuse() sees a valid, neutral value during the capture pass itself.
	glActiveTexture(GL_TEXTURE0 + 37);
	glBindTexture(GL_TEXTURE_2D, _renderCtrl.whiteTexture());
	glActiveTexture(GL_TEXTURE0 + 38);
	glBindTexture(GL_TEXTURE_2D, _renderCtrl.whiteTexture());
	glActiveTexture(GL_TEXTURE0);

	// --- RENDER: SSS opaque meshes only ---
	// sssCapture=true tells the shader to output raw linear diffuse for SSS
	// meshes and discard everything else.  We only submit SSS meshes here
	// (drawSSSMeshesOnly) so no vertex work is wasted on non-SSS geometry.
	_renderCtrl.fgShader()->bind();
	setCommonUniforms(_renderCtrl.fgShader(), camera);
	_renderCtrl.fgShader()->setUniformValue("sssCapture", true);

	if (_renderCtrl.yzClippingEnabled() || _renderCtrl.zxClippingEnabled() || _renderCtrl.xyClippingEnabled())
	{
		if (_renderCtrl.yzClippingEnabled())
		{
			glEnable(GL_CLIP_DISTANCE0);
			drawSSSMeshesOnly(_renderCtrl.fgShader(), 0);
			glDisable(GL_CLIP_DISTANCE0);
		}
		if (_renderCtrl.zxClippingEnabled())
		{
			glEnable(GL_CLIP_DISTANCE1);
			drawSSSMeshesOnly(_renderCtrl.fgShader(), 1);
			glDisable(GL_CLIP_DISTANCE1);
		}
		if (_renderCtrl.xyClippingEnabled())
		{
			glEnable(GL_CLIP_DISTANCE2);
			drawSSSMeshesOnly(_renderCtrl.fgShader(), 2);
			glDisable(GL_CLIP_DISTANCE2);
		}
	}
	else
	{
		drawSSSMeshesOnly(_renderCtrl.fgShader());
	}

	_renderCtrl.fgShader()->setUniformValue("sssCapture", false); // reset before release
	_renderCtrl.fgShader()->release();

	// No mipmaps needed — the blur passes sample at full resolution.

	// --- UNBIND FBO ---
	glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
	glViewport(0, 0, width(), height());
}

void ViewportWidget::cleanupTransmissionBuffer()
{
	_renderCtrl.cleanupTransmissionBuffer();
}

// ============================================================================
// SSS (Subsurface Scattering) Buffer
// Two-FBO ping-pong layout:
//   _renderCtrl.sssFBO()    + _renderCtrl.sssCaptureTexture()  — capture pass output / V-blur output
//   _renderCtrl.sssBlurFBO() + _renderCtrl.sssBlurTexture()    — H-blur output / V-blur input
// Both are RGBA16F (no mipmaps needed — they are blur intermediates).
// _renderCtrl.sssDepthTexture() is shared by the capture FBO for correct depth occlusion.
// ============================================================================

void ViewportWidget::initSSSBuffer()
{
	_renderCtrl.initSSSBuffer(width(), height());
}

void ViewportWidget::resizeSSSBuffer(int width, int height)
{
	_renderCtrl.initSSSBuffer(width, height);
}

void ViewportWidget::cleanupSSSBuffer()
{
	_renderCtrl.cleanupSSSBuffer();
}

void ViewportWidget::checkAndStopTimers()
{
	if (_animateViewTimer->isActive())
	{
		_animateViewTimer->stop();
		// Set all defaults
		_viewCtrl.syncPoseFromCamera(*_primaryCamera);
		_viewCtrl.setCurrentViewRange(_viewCtrl.viewRange());
		_viewCtrl.resetSlerpStep();
		_viewCtrl.setCustomViewAnimationActive(false);
		emit rotationsSet();
	}
	if (_animateFitAllTimer->isActive())
	{
		_animateFitAllTimer->stop();
		// Set all defaults
		_viewCtrl.setCurrentTranslation(_primaryCamera->getPosition());
		_viewCtrl.setCurrentViewRange(_viewCtrl.viewRange());
		_viewCtrl.resetSlerpStep();
		emit zoomAndPanSet();
	}
	if (_animateWindowZoomTimer->isActive())
	{
		_animateWindowZoomTimer->stop();
		_animateFitAllTimer->stop();
		// Set all defaults
		_viewCtrl.setCurrentTranslation(_primaryCamera->getPosition());
		_viewCtrl.setCurrentViewRange(_viewCtrl.viewRange());
		_viewCtrl.resetSlerpStep();
		emit zoomAndPanSet();
	}
	if (_animateCenterScreenTimer->isActive())
	{
		_animateCenterScreenTimer->stop();
		_animateFitAllTimer->stop();
		// Set all defaults
		_viewCtrl.setCurrentTranslation(_primaryCamera->getPosition());
		_viewCtrl.setCurrentViewRange(_viewCtrl.viewRange());
		_viewCtrl.resetSlerpStep();
		emit zoomAndPanSet();
	}
}

void ViewportWidget::disableLowRes()
{
	_renderCtrl.setLowResEnabled(false);
	update();
}

void ViewportWidget::setSectionCapsInteractionSuppressed(bool suppressed)
{
	bool actual = suppressed && _renderCtrl.dynamicCappingEnabled();
	if (_renderCtrl.sectionCapsSuppressedDuringInteraction() == actual)
		return;

	_renderCtrl.setSectionCapsSuppressedDuringInteraction(actual);
	update();
}

void ViewportWidget::resizeEvent(QResizeEvent* event)
{
	if (_viewToolbar)
	{
		_viewToolbar->reposition(width(), height()); // Move completely below widget
	}
	QOpenGLWidget::resizeEvent(event);
	if (_viewer)
	{
		_viewer->updateNavigationOverlayGeometry();
		QMetaObject::invokeMethod(this, [this]()
		{
			if (_viewer)
				_viewer->updateNavigationOverlayGeometry();
		}, Qt::QueuedConnection);
	}
}

void ViewportWidget::showEvent(QShowEvent* event)
{
	QOpenGLWidget::showEvent(event);

	// This app is MDI (MainWindow.cpp's QMdiArea) - switching between
	// maximized document sub-windows genuinely hides/shows each one's
	// ViewportWidget (unlike being covered by an unrelated top-level app
	// window, which doesn't touch Qt's visibility state at all). hideEvent()
	// below still pauses the ray-traced session while hidden (a resource-
	// management stop, not something that should itself count as a
	// "trigger"). This used to also force a restart here on the way back in,
	// but the only thing that should re-arm/restart ray tracing is actual
	// camera movement - MDI visibility changes shouldn't. paintGL()'s own
	// self-healing watchdog (see paintGL()'s "!_rayTracedIdleTimer->
	// isActive() && !rayTracedSessionRunning() && !_rtPresenter.hasFrame()"
	// check) already restarts a stuck session the moment this widget is
	// next painted, which happens naturally as soon as Qt shows it again -
	// so no dedicated restart is needed here.
}

void ViewportWidget::hideEvent(QHideEvent* event)
{
	// Reported bug: after switching to another MDI document and back,
	// ray-traced mode "never shows up again, always PBR". Root cause: the
	// background tracer thread and refresh timer kept running/publishing
	// frames for a now-hidden widget with nothing paying attention. Stopping
	// them here (a plain resource-management pause, not itself a "trigger")
	// is enough - paintGL()'s self-healing watchdog restarts the session on
	// its own the moment this widget is next painted (see showEvent()),
	// without needing a dedicated restart call here or there. Mirrors
	// disarmRayTracedRenderingMode()'s cleanup but deliberately leaves
	// _rayTracedArmed set so that watchdog knows ray tracing is still
	// meant to be active once repainting resumes.
	// Mirrors any other teardown-while-still-armed event
	// (notifySceneContentMutated()) - tears the interactive accumulator down
	// and debounces its resume, but leaves ray tracing itself armed so the
	// self-healing watchdog/applicationStateChanged handler know it's still
	// meant to be active once repainting resumes. Safe to let the resume
	// timer actually fire while hidden - the warm-up itself is pure CUDA/
	// OptiX work with no dependency on this widget's own GL context or
	// visibility, so the GAS/IAS rebuild effectively happens "for free" in
	// the background before the user ever switches back, rather than only
	// starting once they do.
	_rtInteractionCtrl->notifySceneContentMutated();

	QOpenGLWidget::hideEvent(event);
}

void ViewportWidget::mousePressEvent(QMouseEvent* e)
{
	setFocus();
	checkAndStopTimers();
	// A plain click (selection, gizmo activation, view-cube click, focus
	// grab) is not camera movement and should not start the interactive PT
	// preview. The actual orbit/pan/zoom branches trigger
	// notifyCameraInteracting() on first real movement instead.

	// Reset inertia on new mouse press
	_viewCtrl.clearInertiaState();
	if (_inertiaTimer) _inertiaTimer->stop();

	// Reset movement tracking
	_viewCtrl.setMouseMovedSincePress(false);
	_viewCtrl.setLastMouseMoveTime(0);
	_viewCtrl.setLastMousePos(e->pos());
	_viewCtrl.setLastMouseTime(e->timestamp());

	if (e->button() & Qt::LeftButton)
	{
		const QPoint clickPoint(e->position().x(), e->position().y());

		// While a measurement tool is armed, a plain left click arms a
		// pending point (committed in mouseReleaseEvent() only if the mouse
		// didn't drag - see _measurementClickCandidate's doc comment). Same
		// gate clickSelect() uses below, and for the same reason: without
		// it, starting an unrelated drag gesture (Ctrl+drag rotate, an
		// explicit Rotate/Pan/Zoom mode, a window-zoom drag) also placed a
		// spurious point right at the press location before the gesture
		// even got going. When one of those IS in progress, fall through
		// instead of intercepting, so the gesture works exactly as if no
		// tool were armed - the tool simply doesn't register a point for it.
		if (_measurementTool != MeasurementTool::None
			&& !(e->modifiers() & Qt::ControlModifier) && !(e->modifiers() & Qt::ShiftModifier)
			&& !_viewCtrl.windowZoomActive() && !_viewCtrl.viewRotating()
			&& !_viewCtrl.viewPanning() && !_viewCtrl.viewZooming())
		{
			_measurementClickCandidate = true;
			_measurementClickPressPos = clickPoint;
			return;
		}

		// No tool armed: a plain click can instead SELECT an existing
		// measurement (independent of mesh selection) so it can be deleted -
		// same navigation gate as above and as clickSelect() below, so this
		// doesn't fire mid-rotate/pan/zoom either. Hitting a measurement
		// consumes the click (skips normal gizmo/view-cube/mesh selection
		// below) since the user is clearly aiming at the measurement, not
		// whatever mesh happens to be behind it; missing clears any previous
		// measurement selection and falls through to normal handling.
		else if (_measurementTool == MeasurementTool::None
			&& !(e->modifiers() & Qt::ControlModifier) && !(e->modifiers() & Qt::ShiftModifier)
			&& !_viewCtrl.windowZoomActive() && !_viewCtrl.viewRotating()
			&& !_viewCtrl.viewPanning() && !_viewCtrl.viewZooming())
		{
			// A click directly on a dimension line's own drawn (offset)
			// position - not just anywhere on the measurement - arms a drag
			// candidate first, resolved in mouseMoveEvent()/mouseReleaseEvent()
			// into either a real drag (mouse crosses the click threshold) or
			// a plain select-click (it doesn't) - same press-vs-drag
			// disambiguation shape as _measurementClickCandidate above, for
			// point placement. Works even on a not-yet-selected measurement,
			// matching how real CAD tools let you grab a dimension directly.
			const DimensionHit hitDimension = hitTestDimensionLine(clickPoint, _primaryCamera, 8);
			if (hitDimension.kind != DimensionDragKind::None)
			{
				_dimensionDragCandidate = true;
				_dimensionDragCandidateId = hitDimension.measurementId;
				_dimensionDragKind = hitDimension.kind;
				_dimensionDragStartPixel = clickPoint;
				return;
			}

			const QUuid hitMeasurement = hitTestMeasurement(clickPoint, _primaryCamera, 8);
			// A plain click in the 3D view always replaces the whole
			// selection with (at most) one id - multi-select is a
			// Measurement-dialog-only affordance (its list's
			// ExtendedSelection mode), not wired to viewport clicks.
			setSelectedMeasurementIds(hitMeasurement.isNull() ? QSet<QUuid>() : QSet<QUuid>{ hitMeasurement });
			if (!hitMeasurement.isNull())
			{
				if (_selectionManager)
					_selectionManager->setSelectedIds({});
				return;
			}
		}

		if (!(e->modifiers() & Qt::ControlModifier) &&
			_viewCtrl.transformGizmoRequested() && _transformGizmo &&
			_transformGizmo->activateHandleAt(clickPoint, _primaryCamera, _viewCtrl.viewMatrix(), _viewCtrl.projectionMatrix(),
				QRect(0, 0, width(), height()), kTransformGizmoMinWorldScale))
		{
			if (beginTransformGizmoDrag(_transformGizmo->activeHandle(), clickPoint))
			{
				update();
				return;
			}
		}
		if (_transformGizmo)
			_transformGizmo->clearInteraction();
		if (!(e->modifiers() & Qt::ControlModifier) && !(e->modifiers() & Qt::ShiftModifier)
			&& !_viewCtrl.windowZoomActive() && !_viewCtrl.viewRotating() && !_viewCtrl.viewPanning() && !_viewCtrl.viewZooming()
			&& handleViewCubeClick(clickPoint))
		{
			return;
		}

		_viewCtrl.setLeftButtonPoint(e->position().toPoint());

		// Track if Shift is held for drag selection mode
		_viewCtrl.setShiftDragActive((e->modifiers() & Qt::ShiftModifier) != 0);
		_viewCtrl.setSweepStartPoint(e->position().toPoint());

		if (_viewCtrl.viewPanning() || _viewCtrl.viewZooming() || _viewCtrl.viewRotating())
		{
			_viewCtrl.setNavigationLock(
				PickingHelper::viewportRectForPoint(e->pos(), width(), height(), _viewCtrl.multiViewActive()),
				PickingHelper::clientRectForPoint(e->pos(), width(), height(), _viewCtrl.multiViewActive()));
		}

		if (!(e->modifiers() & Qt::ControlModifier) && !(e->modifiers() & Qt::ShiftModifier)
			&& !_viewCtrl.windowZoomActive() && !_viewCtrl.viewRotating() && !_viewCtrl.viewPanning() && !_viewCtrl.viewZooming())
		{
			// Selection
			_selectionManager->clickSelect(clickPoint);
		}


		_rubberBand->setGeometry(QRect(_viewCtrl.leftButtonPoint(), QSize()));
		_rubberBand->show();
	}

	if ((e->button() & Qt::RightButton) || ((e->button() & Qt::LeftButton) && _viewCtrl.viewPanning()))
	{
		_viewCtrl.setRightButtonPoint(e->position().toPoint());
		_viewCtrl.setLastPanPoint(e->pos());
		_viewCtrl.setNavigationLock(
			PickingHelper::viewportRectForPoint(e->pos(), width(), height(), _viewCtrl.multiViewActive()),
			PickingHelper::clientRectForPoint(e->pos(), width(), height(), _viewCtrl.multiViewActive()));
	}

	if (e->button() & Qt::MiddleButton || ((e->button() & Qt::LeftButton) && _viewCtrl.viewRotating()))
	{
		_viewCtrl.setMiddleButtonPoint(e->position().toPoint());
		if (e->button() & Qt::MiddleButton)
		{
			_viewCtrl.setNavigationLock(
				PickingHelper::viewportRectForPoint(e->pos(), width(), height(), _viewCtrl.multiViewActive()),
				PickingHelper::clientRectForPoint(e->pos(), width(), height(), _viewCtrl.multiViewActive()));
		}
	}
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* e)
{
	if ((e->button() & Qt::LeftButton) && _viewCtrl.transformGizmoTranslating())
	{
		finishTransformGizmoTranslationDrag(true);
		update();
		return;
	}
	if ((e->button() & Qt::LeftButton) && _viewCtrl.transformGizmoScaling())
	{
		finishTransformGizmoScaleDrag(true);
		update();
		return;
	}
	if ((e->button() & Qt::LeftButton) && _viewCtrl.transformGizmoRotating())
	{
		finishTransformGizmoRotationDrag(true);
		update();
		return;
	}

	if ((e->button() & Qt::LeftButton) && _dimensionDragCandidate)
	{
		if (_dimensionDragActive)
		{
			finishDimensionLineDrag();
		}
		else
		{
			// No real drag happened (mouse never crossed the threshold in
			// mouseMoveEvent()) - treat as a plain select-click on the
			// measurement whose dimension line was pressed, same as
			// hitTestMeasurement()'s select branch just below handles for
			// every other part of a measurement.
			setSelectedMeasurementIds({ _dimensionDragCandidateId });
			if (_selectionManager)
				_selectionManager->setSelectedIds({});
			_dimensionDragCandidate = false;
			_dimensionDragCandidateId = QUuid();
			_dimensionDragKind = DimensionDragKind::None;
		}
		update();
		return;
	}

	if ((e->button() & Qt::LeftButton) && _measurementClickCandidate)
	{
		_measurementClickCandidate = false;
		// Only commit if this was genuinely a click, not a drag that
		// happened to end without matching one of the navigation-mode
		// checks mousePressEvent already gated on (e.g. plain rubber-band-
		// style movement with no modifier) - same small-motion tolerance
		// convention as kCameraDragThresholdPx elsewhere in this file.
		constexpr int kMeasurementClickThresholdPx = 4;
		if ((e->pos() - _measurementClickPressPos).manhattanLength() < kMeasurementClickThresholdPx)
			handleMeasurementClick(_measurementClickPressPos);
		update();
		return;
	}

	if (e->button() & Qt::LeftButton)
	{
        _rubberBand->hide();
		if (_viewCtrl.windowZoomActive())
		{
			performWindowZoom();
		}
		else if (!(e->modifiers() & Qt::ControlModifier) && !_viewCtrl.viewRotating() && !_viewCtrl.viewPanning() && !_viewCtrl.viewZooming())
		{
			// Sweep select: check shift status at release time to determine if we should add to selection
			bool shiftHeldAtRelease = (e->modifiers() & Qt::ShiftModifier) != 0;
			// Prefer the current shift state at release time over the state at press time
			bool addToSelection = shiftHeldAtRelease || _viewCtrl.shiftDragActive();

			sweepSelect(e->pos(), addToSelection);
			_viewCtrl.setShiftDragActive(false);  // Reset the flag
		}
	}

	if (e->button() & Qt::RightButton)
	{
		_viewCtrl.setLastPanPoint(e->pos());
	}

	if (e->button() & Qt::MiddleButton)
	{
		if (e->modifiers() == Qt::NoModifier)
		{
			if (!_viewCtrl.multiViewActive())
			{
				QPoint o(width() / 2, height() / 2);
				QPoint p = e->pos();

				QVector3D OP = get3dTranslationVectorFromMousePoints(o, p);
				_primaryCamera->move(OP.x(), OP.y(), OP.z());
				_viewCtrl.syncTranslationFromCamera(*_primaryCamera);
				update();
			}
		}
	}

	_renderCtrl.setLowResEnabled(false);
	if (!_viewCtrl.viewRotating() && !_viewCtrl.viewPanning() && !_viewCtrl.viewZooming())
	{
		setCursor(QCursor(Qt::ArrowCursor));
	}

	// Only start inertia if mouse was moving recently
	qint64 now = e->timestamp();
	const qint64 maxIdleMs = 50; // adjust as needed
	bool recentMove = (_viewCtrl.lastMouseMoveTime() > 0) && ((now - _viewCtrl.lastMouseMoveTime()) < maxIdleMs);

	// Start inertia if velocity is significant and smooth navigation is enabled
	if (_smoothNavigation && _viewCtrl.mouseMovedSincePress() && recentMove &&
		(_viewCtrl.inertiaPanVelocity().lengthSquared() > 1.0f ||
			std::abs(_viewCtrl.inertiaZoomVelocity()) > 0.01f ||
			_viewCtrl.inertiaRotateVelocity().lengthSquared() > 1.0f))
	{
		if (_inertiaTimer) _inertiaTimer->start();
	}
	else
	{
		_viewCtrl.clearInertiaState();
		if (_renderCtrl.sectionCapsSuppressedDuringInteraction())
			QTimer::singleShot(100, this, &ViewportWidget::disableSectionCapsInteractionSuppression);
	}

	if (e->buttons() == Qt::NoButton)
	{
		_viewCtrl.clearNavigationLock();
	}

	update();

	// If a GPU interactive PT trace was live and this release does NOT kick
	// off inertia (the "stop moving, then release" case - see the
	// recentMove/inertia branch above), whatever render was in flight for
	// the pose at the moment motion stopped may not have finished by the
	// time this update() above ran - the next thing that would notice its
	// completion and redraw is the passive _rayTracedRefreshTimer, which
	// polls only every 100ms (see its own doc comment on why that interval
	// is deliberately not faster). That "up to 100ms, whatever the timer's
	// phase happens to be" wait is what reads as a residual stutter right
	// after a manual stop - inertia's own decaying coast doesn't have this
	// gap because onInertiaTimer() keeps calling update() every tick on its
	// own until it stops itself. A couple of cheap, one-shot follow-up
	// repaints shortly after this release - not a faster steady-state poll
	// rate, which was already tried and reverted for the GPU/CPU contention
	// reasons in that doc comment - close most of that gap for the common
	// case without touching the render/publish pipeline itself at all: if
	// the trace already finished by the time either fires, this just shows
	// it sooner than the passive timer would have; if it's somehow still
	// running, this is a harmless no-op repaint of whatever's already
	// there.
	if (_rtInteractionCtrl->armed() && _rayTracedInteractiveActive &&
		effectiveRayTracingEnginePreference() == RtRayTracingEnginePreference::GPU &&
		!(_inertiaTimer && _inertiaTimer->isActive()))
	{
		QTimer::singleShot(20, this, [this]() { if (_rayTracedInteractiveActive) update(); });
		QTimer::singleShot(60, this, [this]() { if (_rayTracedInteractiveActive) update(); });
	}
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* e)
{
	QPoint currentPos = e->pos();
	qint64 currentTime = e->timestamp();
	QPoint delta = currentPos - _viewCtrl.lastMousePos();
	float dt = (currentTime - _viewCtrl.lastMouseTime()) / 1000.0f; // seconds
	const bool anyButtonDown = e->buttons() != Qt::NoButton;
	if (anyButtonDown && !delta.isNull())
	{
		_viewCtrl.setMouseMovedSincePress(true);
		_viewCtrl.setLastMouseMoveTime(e->timestamp());
	}

	QPoint downPoint(e->position().x(), e->position().y());
	constexpr int kCameraDragThresholdPx = 3;

	if (_dimensionDragCandidate && (e->buttons() & Qt::LeftButton))
	{
		if (!_dimensionDragActive)
		{
			const QPoint moved = e->pos() - _dimensionDragStartPixel;
			if (moved.manhattanLength() >= kCameraDragThresholdPx)
				beginDimensionLineDrag(_dimensionDragCandidateId, _dimensionDragKind, _primaryCamera);
		}
		if (_dimensionDragActive)
		{
			updateDimensionLineDrag(e->pos(), _primaryCamera);
			_viewCtrl.setLastMousePos(currentPos);
			_viewCtrl.setLastMouseTime(currentTime);
			return;
		}
	}

	if (_viewCtrl.transformGizmoTranslating() && (e->buttons() & Qt::LeftButton))
	{
		notifyRayTracedSceneMutated();
		updateTransformGizmoTranslationDrag(e->pos());
		_viewCtrl.setLastMousePos(currentPos);
		_viewCtrl.setLastMouseTime(currentTime);
		return;
	}
	if (_viewCtrl.transformGizmoScaling() && (e->buttons() & Qt::LeftButton))
	{
		notifyRayTracedSceneMutated();
		updateTransformGizmoScaleDrag(e->pos());
		_viewCtrl.setLastMousePos(currentPos);
		_viewCtrl.setLastMouseTime(currentTime);
		return;
	}
	if (_viewCtrl.transformGizmoRotating() && (e->buttons() & Qt::LeftButton))
	{
		notifyRayTracedSceneMutated();
		updateTransformGizmoRotationDrag(e->pos());
		_viewCtrl.setLastMousePos(currentPos);
		_viewCtrl.setLastMouseTime(currentTime);
		return;
	}

	if (e->buttons() == Qt::LeftButton && !_viewCtrl.viewPanning() && !_viewCtrl.viewZooming())
	{
		if (!(e->modifiers() & Qt::ControlModifier) && !_viewCtrl.viewRotating() && !_viewCtrl.viewPanning() && !_viewCtrl.viewZooming())
		{
            _rubberBand->setGeometry(QRect(_viewCtrl.leftButtonPoint(), e->pos()).normalized());
		}
		if (_viewCtrl.windowZoomActive())
		{
			setCursor(QCursor(QPixmap(":/icons/res/window-zoom-cursor.png"), 12, 12));
		}
		else if (((e->modifiers() & Qt::ControlModifier) || _viewCtrl.viewRotating()) && !isGltfCameraActive())
		{
			const QPoint rotate = _viewCtrl.leftButtonPoint() - downPoint;
			if (rotate.manhattanLength() < kCameraDragThresholdPx)
			{
				_viewCtrl.setLastMousePos(currentPos);
				_viewCtrl.setLastMouseTime(currentTime);
				return;
			}

			if (_displayedObjectsMemSize > MAX_MODEL_SIZE_BYTES)
				_renderCtrl.setLowResEnabled(true);
			setSectionCapsInteractionSuppressed(true);

			if (_primaryCamera->getMode() == Camera::CameraMode::Orbit)
			{
				const float yDelta = (_invertYAxis ? -rotate.y() : rotate.y()) * _mouseSensitivity;
				_primaryCamera->rotateX(yDelta / 2.0);
				_primaryCamera->rotateY(rotate.x() * _mouseSensitivity / 2.0);
			}
			else if (_primaryCamera->getMode() == Camera::CameraMode::Fly || _primaryCamera->getMode() == Camera::CameraMode::FirstPerson)
			{
				_primaryCamera->getYaw() += rotate.x() * 0.2f * _mouseSensitivity;
				_primaryCamera->getPitch() += rotate.y() * 0.2f * (_invertYAxis ? -1.0f : 1.0f) * _mouseSensitivity;

				if (_primaryCamera->getMode() == Camera::CameraMode::FirstPerson)
					_primaryCamera->getPitch() = std::clamp(_primaryCamera->getPitch(), -60.0f, 60.0f);
				else
					_primaryCamera->getPitch() = std::clamp(_primaryCamera->getPitch(), -89.0f, 89.0f);

				_primaryCamera->updateFlyView();
			}

			// Notify AFTER this event's own camera update above, not before -
			// see notifyCameraInteracting()'s other call sites in this file for
			// the same fix applied to every other interaction handler. Calling
			// this first (the previous order) fed the interactive PT renderer
			// last event's camera pose instead of this one's, so the
			// displayed frame was always exactly one mouse-move behind the
			// camera's actual live position - imperceptible mid-drag (the lag
			// just chases a continuously-moving target), but visible the
			// instant something else forces a fresh capture of the NOW-
			// current camera (e.g. Ctrl+Shift+R while already armed), which
			// would show the true final pose the stale accumulated frame was
			// still missing.
			_rtInteractionCtrl->notifyCameraInteracting();
			_viewCtrl.syncRotationFromCamera(*_primaryCamera);
			_viewCtrl.setLeftButtonPoint(downPoint);
			setCursor(QCursor(QPixmap(":/icons/res/rotatecursor.png")));
			_viewCtrl.setViewMode(ViewMode::NONE);

			const float maxInertiaVelocity = 10.0f; // Adjust as needed
			if (dt > 0) {
				_viewCtrl.setInertiaRotateVelocity(-QVector2D(delta) / dt);
				if (_viewCtrl.inertiaRotateVelocity().length() > maxInertiaVelocity)
					_viewCtrl.setInertiaRotateVelocity(_viewCtrl.inertiaRotateVelocity().normalized() * maxInertiaVelocity);
			}
		}

		update();
	}
	else if (e->buttons() == Qt::RightButton && !(e->modifiers() & Qt::ControlModifier) &&
		(_primaryCamera->getMode() == Camera::CameraMode::Fly ||
		 _primaryCamera->getMode() == Camera::CameraMode::FirstPerson) &&
		!isGltfCameraActive())
	{
		const QPoint look = _viewCtrl.rightButtonPoint() - downPoint;
		if (look.manhattanLength() < kCameraDragThresholdPx)
		{
			_viewCtrl.setLastMousePos(currentPos);
			_viewCtrl.setLastMouseTime(currentTime);
			return;
		}

		// Free-look in Fly/FP mode: RMB drag rotates the view via yaw/pitch
		_primaryCamera->getYaw()   += look.x() * 0.2f * _mouseSensitivity;
		_primaryCamera->getPitch() += look.y() * 0.2f * _mouseSensitivity;

		if (_primaryCamera->getMode() == Camera::CameraMode::FirstPerson)
			_primaryCamera->getPitch() = std::clamp(_primaryCamera->getPitch(), -60.0f, 60.0f);
		else
			_primaryCamera->getPitch() = std::clamp(_primaryCamera->getPitch(), -89.0f, 89.0f);

		_primaryCamera->updateFlyView();
		// See the LMB/Ctrl rotate handler above for why this must run AFTER
		// the camera update, not before.
		_rtInteractionCtrl->notifyCameraInteracting();
		_viewCtrl.syncRotationFromCamera(*_primaryCamera);
		_viewCtrl.setRightButtonPoint(downPoint);
		setCursor(QCursor(QPixmap(":/icons/res/rotatecursor.png")));

		if (dt > 0) {
			_viewCtrl.setInertiaRotateVelocity(-QVector2D(look) / dt);
			const float maxVel = 10.0f;
			if (_viewCtrl.inertiaRotateVelocity().length() > maxVel)
				_viewCtrl.setInertiaRotateVelocity(_viewCtrl.inertiaRotateVelocity().normalized() * maxVel);
		}

		update();
	}
	else if (((e->buttons() == Qt::RightButton && e->modifiers() & Qt::ControlModifier) || (e->buttons() == Qt::LeftButton && _viewCtrl.viewPanning())) && !isGltfCameraActive())
	{
		const QPoint panDelta = downPoint - _viewCtrl.rightButtonPoint();
		if (panDelta.manhattanLength() < kCameraDragThresholdPx)
		{
			_viewCtrl.setLastMousePos(currentPos);
			_viewCtrl.setLastMouseTime(currentTime);
			return;
		}

		if (_displayedObjectsMemSize > MAX_MODEL_SIZE_BYTES)
			_renderCtrl.setLowResEnabled(true);
		setSectionCapsInteractionSuppressed(true);
		QVector3D OP = get3dTranslationVectorFromMousePoints(downPoint, _viewCtrl.rightButtonPoint()) * _mouseSensitivity;
		_primaryCamera->move(OP.x(), OP.y(), OP.z());
		// See the rotate handlers above for why this must run AFTER the
		// camera update, not before.
		_rtInteractionCtrl->notifyCameraInteracting();
		_viewCtrl.syncTranslationFromCamera(*_primaryCamera);

		_viewCtrl.setRightButtonPoint(downPoint);
		setCursor(QCursor(QPixmap(":/icons/res/pancursor.png")));

		// Clamp pan inertia velocity
		const float maxPanInertiaVelocity = 20.0f; // Adjust as needed
		if (dt > 0) {
			_viewCtrl.setInertiaPanVelocity(QVector2D(delta) / dt);
			if (_viewCtrl.inertiaPanVelocity().length() > maxPanInertiaVelocity)
				_viewCtrl.setInertiaPanVelocity(_viewCtrl.inertiaPanVelocity().normalized() * maxPanInertiaVelocity);

			_viewCtrl.setInertiaZoomPanVelocity(OP);
		}

		update();
	}
	else if (((e->buttons() == Qt::MiddleButton && e->modifiers() & Qt::ControlModifier) || (e->buttons() == Qt::LeftButton && _viewCtrl.viewZooming())) && !isGltfCameraActive())
	{
		const QPoint frameDelta = downPoint - _viewCtrl.middleButtonPoint();
		if (frameDelta.manhattanLength() < kCameraDragThresholdPx)
		{
			_viewCtrl.setLastMousePos(currentPos);
			_viewCtrl.setLastMouseTime(currentTime);
			return;
		}

		if (_displayedObjectsMemSize > MAX_MODEL_SIZE_BYTES)
			_renderCtrl.setLowResEnabled(true);
		setSectionCapsInteractionSuppressed(true);
		// Zoom — scale by actual pixel delta so slow drag = slow zoom
		// Right/up = zoom in; left/down = zoom out (horizontal dominates if larger)
		const float pixelDelta = std::abs(frameDelta.x()) >= std::abs(frameDelta.y())
		    ? static_cast<float>(frameDelta.x())
		    : static_cast<float>(-frameDelta.y());
		if (std::abs(pixelDelta) > 0.5f)
		{
			// 0.005 coefficient: 10 px/frame ≈ 5% step at default sensitivity
			const float dragZoomFactor = std::max(1.0f + std::abs(pixelDelta) * 0.005f * _mouseSensitivity, 1.0001f);
			if (pixelDelta > 0) {
				_viewCtrl.setViewRange(_viewCtrl.viewRange() / dragZoomFactor);
				_viewCtrl.setLastZoomDirection(1);
			} else {
				_viewCtrl.setViewRange(_viewCtrl.viewRange() * dragZoomFactor);
				_viewCtrl.setLastZoomDirection(-1);
			}
		}
		
		// Perspective: floor is the focused sub-mesh radius × 0.5 so the orbit
		// distance stays outside whatever mesh is currently fully in view.
		// As the user zooms in, large meshes leave the frustum and the floor
		// shrinks automatically to match the focused geometry.
		// Ortho: the near/far floor in Camera handles tearing without any
		// zoom restriction, so only a minimal absolute floor is applied.
		updateZoomInLimit();
		const float focusRadius = _viewCtrl.zoomInLimit();
		const float minVR = (_viewCtrl.projection() == ViewProjection::PERSPECTIVE)
		    ? std::max(focusRadius * 0.5f, 0.0001f)
		    : std::max(focusRadius / 100.0f, 0.00001f);
		if (_viewCtrl.viewRange() < minVR)
			_viewCtrl.setViewRange(minVR);
		if (_viewCtrl.viewRange() > _viewCtrl.boundingSphere().getRadius() * 100.0f)
			_viewCtrl.setViewRange(_viewCtrl.boundingSphere().getRadius() * 100.0f);
		_viewCtrl.syncCurrentViewRange();

		// Translate to focus on mouse center
		QPoint cen = PickingHelper::clientRectForPoint(downPoint, width(), height(), _viewCtrl.multiViewActive()).center();
		float sign = (pixelDelta > 0) ? 1.0f : -1.0f;
		QVector3D OP = get3dTranslationVectorFromMousePoints(cen, _viewCtrl.middleButtonPoint());
		OP *= sign * 0.05f * _mouseSensitivity;
		_primaryCamera->move(OP.x(), OP.y(), OP.z());
		_viewCtrl.syncTranslationFromCamera(*_primaryCamera);
		_viewCtrl.setLastZoomPanVector(OP); // Store for inertia

		if (dt > 0) {
			_viewCtrl.setInertiaZoomVelocity(_viewCtrl.lastZoomDirection()); // +1 or -1
		}

		// resizeGL() is what actually recomputes the projection matrix for
		// the viewRange change just applied above (setViewRange() alone only
		// updates the stored value) - RtSceneBuilder::buildCamera() reads
		// that matrix's own [1][1] element for tanHalfFovY/orthoHalfHeight,
		// so notifying PT (which captures a fresh RtCamera) BEFORE this call
		// fed it this tick's new translation against the PREVIOUS tick's
		// stale zoom scale - translation and zoom visibly out of sync in PT
		// for exactly one tick, unlike raster (whose own projection matrix
		// is only ever read at actual draw time, already past this point).
		resizeGL(width(), height());
		_rtInteractionCtrl->notifyCameraInteracting();

		_viewCtrl.setMiddleButtonPoint(downPoint);
		setCursor(QCursor(QPixmap(":/icons/res/zoomcursor.png")));

		update();
	}
	else
	{
		_renderCtrl.setLowResEnabled(false);
	}

	updateViewCubeHover(e->pos(), e->buttons());


	// Auto-hide/show the view toolbar
	if (_viewToolbar && e->buttons() == Qt::NoButton)
	{
		const int revealMargin = 30; // e.g., 30 px threshold

		QRect hidden = _viewToolbar->hiddenRect();
		QRect revealArea(hidden.left(), hidden.top() - revealMargin, hidden.width(), revealMargin * 2);

		if (revealArea.contains(e->pos()) || _viewToolbar->underMouse())
		{
			_viewToolbar->showAnimated();
		}
		else
		{
			// Store the timer as a member (optional) to manage it better
			auto timer = new QTimer(this);
			timer->setSingleShot(true);
			connect(timer, &QTimer::timeout, this, [this, timer]() {
				if (!_viewToolbar)
				{
					timer->deleteLater(); // Clean up the timer
					return; // Exit safely
				}

				QPoint globalPos = QCursor::pos();
				QPoint localPos = mapFromGlobal(globalPos);
				QRect hidden = _viewToolbar->hiddenRect();
				QRect revealArea(hidden.left(), hidden.top() - 30, hidden.width(), 60);

				bool isFlyoutVisible = _viewToolbar->isFlyoutMenuVisible();

				if (!revealArea.contains(localPos) &&
					!_viewToolbar->underMouse() &&
					!isFlyoutVisible)
				{
					_viewToolbar->hideAnimated();
				}

				timer->deleteLater(); // Clean up the timer
				});

			// Start the timer
			timer->start(2000);

			// Ensure proper cleanup of the timer if the toolbar is deleted
			connect(_viewToolbar, &QObject::destroyed, timer, [timer]() {
				timer->stop();
				timer->deleteLater();
				});
		}
	}

	// Hover highlight feedback for the transform gizmo.
	bool gizmoHovered = false;
	if (e->buttons() == Qt::NoButton)
	{
		if (!(e->modifiers() & Qt::ControlModifier) &&
			_viewCtrl.transformGizmoRequested() && _transformGizmo &&
			_transformGizmo->updateHover(e->pos(), _primaryCamera, _viewCtrl.viewMatrix(), _viewCtrl.projectionMatrix(),
				QRect(0, 0, width(), height()), kTransformGizmoMinWorldScale))
		{
			gizmoHovered = true;
			update();
		}
		else if (_transformGizmo)
		{
			const bool hadHover = (_transformGizmo->hoveredHandle() != TransformGizmo::Handle::None);
			_transformGizmo->updateHover(QPoint(-1, -1), _primaryCamera, _viewCtrl.viewMatrix(), _viewCtrl.projectionMatrix(),
				QRect(0, 0, width(), height()), kTransformGizmoMinWorldScale);
			if (hadHover)
				update();
		}
	}

	// Hover highlight feedback (visual preview, independent of actual selection)
	if (e->buttons() == Qt::NoButton && _selectionManager->getHoverMode() != HoverHighlightMode::Disabled)
	{
		if (!gizmoHovered && (!_viewCtrl.showViewCubeOverride() || !viewCubeScreenRect().contains(e->pos())))
		{
			// Compute hovered mesh (SelectionManager will emit hoverChanged signal if it changed)
			_selectionManager->hoverSelect(e->pos());
		}
	}

	// Measurement tool's own hover preview: the whole-mesh highlight above
	// is suppressed while a tool is armed (setMeasurementTool() switches
	// _selectionManager to HoverHighlightMode::Disabled) since it doesn't
	// say WHERE a click will land - show the actual snap-able point instead
	// (drawMeasurementOverlay() renders _measurementHoverAnchor).
	if (e->buttons() == Qt::NoButton && _measurementTool != MeasurementTool::None && _selectionManager)
	{
		_measurementEdgeHoverIsCenterPick = false;
		if (_measurementTool == MeasurementTool::EdgeRadius || _measurementTool == MeasurementTool::Concentricity)
		{
			_measurementEdgeHoverAnchor = _selectionManager->pickEdgeCircleAnchor(e->pos());
			_measurementHoverAnchor = MeshSurfaceAnchor();
		}
		else if (_measurementTool == MeasurementTool::EdgeLength
			|| _measurementTool == MeasurementTool::EdgeToEdge
			|| _measurementTool == MeasurementTool::EdgeChain
			|| (_measurementTool == MeasurementTool::EdgeToVertex && _pendingMeasurementAnchors.isEmpty())
			|| (_measurementTool == MeasurementTool::EdgeToFace && _pendingMeasurementAnchors.isEmpty()))
		{
			_measurementEdgeHoverAnchor = _selectionManager->pickStraightEdgeAnchor(e->pos());
			_measurementHoverAnchor = MeshSurfaceAnchor();
		}
		else if (_measurementTool == MeasurementTool::FaceToFace
			|| ((_measurementTool == MeasurementTool::PointToFace || _measurementTool == MeasurementTool::EdgeToFace)
				&& !_pendingMeasurementAnchors.isEmpty())
			|| _measurementTool == MeasurementTool::ArcRadius3Point
			|| (_measurementTool == MeasurementTool::ArcRadiusCenterPoint && !_pendingMeasurementAnchors.isEmpty()))
		{
			// A FACE pick, or an ARC-RIM pick that must land on the circle
			// itself (not its center) - no circular-edge-center preview,
			// same reasoning as handleMeasurementClick()'s matching branch.
			_measurementHoverAnchor = _selectionManager->pickSurfaceAnchor(e->pos());
			_measurementEdgeHoverAnchor = MeshEdgeCircleAnchor();
		}
		else
		{
			// Every remaining pick genuinely wants an arbitrary POINT -
			// prefer the circular-edge-center preview; fall back to the
			// ordinary surface-hover preview if nothing's nearby - mirrors
			// handleMeasurementClick()'s own fallback.
			_measurementEdgeHoverAnchor = _selectionManager->pickCircularEdgeCenterAnchor(e->pos());
			if (_measurementEdgeHoverAnchor.isValid())
			{
				_measurementEdgeHoverIsCenterPick = true;
				_measurementHoverAnchor = MeshSurfaceAnchor();
			}
			else
			{
				_measurementHoverAnchor = _selectionManager->pickSurfaceAnchor(e->pos());
			}
		}
	}
	// No tool armed: preview which EXISTING measurement a click would
	// select/delete, before the user commits to clicking - same idea as the
	// snap-point preview above, just for the "select to delete" workflow
	// instead of the "place a new point" one. Gated the same as
	// mousePressEvent()'s select-a-measurement branch (buttons() ==
	// NoButton here stands in for "not mid-navigation-drag", since a real
	// drag always has a button held).
	else if (e->buttons() == Qt::NoButton && _measurementTool == MeasurementTool::None && !gizmoHovered
		&& (!_viewCtrl.showViewCubeOverride() || !viewCubeScreenRect().contains(e->pos())))
	{
		// A dimension-line/arc hover is more specific than a whole-
		// measurement hover - it's telling the user exactly what they can
		// grab and drag (see beginDimensionLineDrag()) - so it takes
		// priority; only fall back to the whole-measurement preview when
		// nothing more specific is under the cursor.
		const DimensionHit hoveredDimension = hitTestDimensionLine(e->pos(), _primaryCamera, 8);
		_hoveredDimensionId = hoveredDimension.measurementId;
		_hoveredDimensionKind = hoveredDimension.kind;
		_hoveredMeasurementId = (hoveredDimension.kind != DimensionDragKind::None)
			? QUuid()
			: hitTestMeasurement(e->pos(), _primaryCamera, 8);
	}

	update();

	_viewCtrl.setLastMousePos(currentPos);
	_viewCtrl.setLastMouseTime(currentTime);
}

void ViewportWidget::wheelEvent(QWheelEvent* e)
{
	// Stop any ongoing inertia when wheel zooming
	_viewCtrl.clearInertiaState();
	if (_inertiaTimer && _inertiaTimer->isActive())
		_inertiaTimer->stop();

	// Scroll-wheel zoom is disabled when a glTF camera is active (read-only view).
	if (isGltfCameraActive())
		return;

	if (_displayedObjectsMemSize > MAX_MODEL_SIZE_BYTES)
		_renderCtrl.setLowResEnabled(true);
	setSectionCapsInteractionSuppressed(true);

	if (_primaryCamera->getMode() == Camera::CameraMode::Fly ||
		_primaryCamera->getMode() == Camera::CameraMode::FirstPerson)
	{
		QPoint numDegrees = e->angleDelta() / 8;
		QPoint numSteps = numDegrees / 30;
		float zoomStep = numSteps.y();
		if (zoomStep != 0.0f)
		{
			const float moveDist = _viewCtrl.viewRange() * 0.08f * std::abs(zoomStep);
			_primaryCamera->moveForward(zoomStep > 0.0f ? moveDist : -moveDist);
			_viewCtrl.syncTranslationFromCamera(*_primaryCamera);
			_viewCtrl.setInertiaZoomVelocity(0.0f);
			// See the drag handlers above for why this must run AFTER the
			// camera update, not before - and only when this event actually
			// moved the camera (this branch's own zoomStep!=0.0f gate),
			// unlike the removed top-of-function call which fired even when
			// nothing changed (e.g. the isGltfCameraActive() early-return
			// below, or a zero-magnitude wheel event). Also after resizeGL()
			// itself - see the orbit zoom handlers' identical fix for why
			// notifying before the projection matrix is actually rebuilt
			// captures a stale zoom scale.
			resizeGL(width(), height());
			_rtInteractionCtrl->notifyCameraInteracting();
			update();
		}
		return;
	}

	// Zoom
	QPoint numDegrees = e->angleDelta() / 8;
	QPoint numSteps = numDegrees / 30;
	float zoomStep = numSteps.y();
	if (_invertZoom)
		zoomStep = -zoomStep;
	const float rawFactor = std::abs(zoomStep) + 0.05f;
	const float zoomFactor = std::max(1.0f + (rawFactor - 1.0f) * _wheelSensitivity, 1.001f);
	const float oldViewRange = _viewCtrl.viewRange();

	if (zoomStep < 0)
		_viewCtrl.setViewRange(_viewCtrl.viewRange() * zoomFactor);
	else
		_viewCtrl.setViewRange(_viewCtrl.viewRange() / zoomFactor);

	{
		updateZoomInLimit();
		const float focusRadius = _viewCtrl.zoomInLimit();
		const float minVR = (_viewCtrl.projection() == ViewProjection::PERSPECTIVE)
		    ? std::max(focusRadius * 0.5f, 0.0001f)
		    : std::max(focusRadius / 100.0f, 0.00001f);
		if (_viewCtrl.viewRange() < minVR)
			_viewCtrl.setViewRange(minVR);
		if (_viewCtrl.viewRange() > _viewCtrl.boundingSphere().getRadius() * 100.0f)
			_viewCtrl.setViewRange(_viewCtrl.boundingSphere().getRadius() * 100.0f);
	}

	_viewCtrl.syncCurrentViewRange();

	// Translate to focus on mouse center
	QPoint cen = PickingHelper::clientRectForPoint(e->position().toPoint(), width(), height(), _viewCtrl.multiViewActive()).center();
	QVector3D OP = get3dTranslationVectorFromMousePoints(cen, e->position().toPoint());
	const float rangeScale = (oldViewRange > 0.0f) ? (_viewCtrl.viewRange() / oldViewRange) : 1.0f;
	OP *= (1.0f - rangeScale);
	_primaryCamera->move(OP.x(), OP.y(), OP.z());
	_viewCtrl.syncTranslationFromCamera(*_primaryCamera);

	// Add inertia for wheel zoom
	if (_smoothNavigation)
	{
		_viewCtrl.setInertiaZoomVelocity((e->angleDelta().y() / 120.0f) * 0.05f); // scale as needed
		if (_inertiaTimer) _inertiaTimer->start();
	}
	else
	{
		_viewCtrl.setInertiaZoomVelocity(0.0f);
	}

	_viewCtrl.addInertiaZoomPanVelocity(OP);

	// resizeGL() recomputes the projection matrix for the viewRange change
	// applied above - RtSceneBuilder::buildCamera() reads that matrix for
	// tanHalfFovY/orthoHalfHeight, so this must run BEFORE notifying PT (see
	// the zoom-drag handler's identical fix for the full reasoning), or PT
	// captures this tick's new translation against the previous tick's
	// stale zoom scale - visibly out of sync, unlike raster.
	resizeGL(width(), height());
	_rtInteractionCtrl->notifyCameraInteracting();
	update();
}

void ViewportWidget::keyPressEvent(QKeyEvent* event)
{
	QWidget::keyPressEvent(event);

	const auto key = event->key();

	if (key == Qt::Key_Escape && _measurementTool != MeasurementTool::None)
	{
		setMeasurementTool(MeasurementTool::None);
		return;
	}
	if ((key == Qt::Key_Return || key == Qt::Key_Enter) && measurementToolHasVariableAnchorCount(_measurementTool))
	{
		finishVariableLengthMeasurement();
		return;
	}
	const bool modifierOnlyKey =
		key == Qt::Key_Control ||
		key == Qt::Key_Shift ||
		key == Qt::Key_Alt ||
		key == Qt::Key_Meta;

	// Must match every key performKeyboardNav() actually reacts to (see that
	// function's body) - move (W/A/S/D/Q/E/arrows), look/rotate (J/L/I/K/M/N,
	// both fly and orbit modes), and orbit zoom (X/Z). PageUp/PageDown are
	// NOT nav keys anywhere in this codebase - deliberately excluded rather
	// than left in as a guess.
	const bool cameraNavKey =
		key == Qt::Key_W || key == Qt::Key_A || key == Qt::Key_S || key == Qt::Key_D ||
		key == Qt::Key_Q || key == Qt::Key_E ||
		key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Left || key == Qt::Key_Right ||
		key == Qt::Key_J || key == Qt::Key_L || key == Qt::Key_I || key == Qt::Key_K ||
		key == Qt::Key_M || key == Qt::Key_N ||
		key == Qt::Key_X || key == Qt::Key_Z;

	// Only real navigation keys should kick the GPU interactive PT path on
	// keydown. Broader "any non-modifier key" behavior made unrelated keys
	// (and shortcut handling around them) wake PT even when the camera never
	// moved at all.
	if (!modifierOnlyKey && cameraNavKey)
		_rtInteractionCtrl->notifyCameraInteracting();

	if (key == Qt::Key_Escape)
	{
		_viewCtrl.clearNavigationModes();
		_viewCtrl.setWindowZoomActive(false);
		setCursor(QCursor(Qt::ArrowCursor));
		MainWindow::showStatusMessage("");

		// Deactivate navigation mode buttons in toolbar
		if (_viewToolbar)
			_viewToolbar->deactivateAllNavigationModes();

		if (_selectionManager)
			_selectionManager->syncSelectedIds(QList<int>{});  // Clear viewport selection state immediately
		_viewer->deselectAllWithUndo();     // Clear viewer selection and push an undo entry
	}
	else if (key == Qt::Key_F)
	{
		fitAll();
	}
	// Key_Delete is deliberately not handled here - MainWindow.cpp's global
	// Delete QShortcut (WindowShortcut context) always intercepts the key
	// before it would reach this keyPressEvent(), so a branch here would be
	// unreachable dead code; see that shortcut's connect() for the actual
	// viewport-selection-aware Delete handling.
	else if (key == Qt::Key_Space)
	{
		if (event->modifiers() & Qt::ShiftModifier)
			_viewer->showOnlySelectedItems();
		else
			_sceneRuntime.visibleSwapped() ? _viewer->showSelectedItems() : _viewer->hideSelectedItems();
	}
	else if (key == Qt::Key_S && (event->modifiers() & Qt::AltModifier))
	{
		swapVisible(!_sceneRuntime.visibleSwapped());
	}
	else if (!modifierOnlyKey)
		_keys.insert(key);

	// Camera mode switching
	if (key == Qt::Key_1) setCameraMode(Camera::CameraMode::Orbit);
	if (key == Qt::Key_2) setCameraMode(Camera::CameraMode::Fly);
	if (key == Qt::Key_3) setCameraMode(Camera::CameraMode::FirstPerson);


	update();
}

void ViewportWidget::keyReleaseEvent(QKeyEvent* event)
{
	_keys.remove(event->key());
	QWidget::keyReleaseEvent(event);
}

void ViewportWidget::performKeyboardNav()
{
	// Keyboard navigation is disabled when a glTF camera is active (read-only view).
	if (isGltfCameraActive())
		return;

	const Qt::KeyboardModifiers modifiers = QApplication::keyboardModifiers();
	const bool allowGameplayModifiers = (modifiers == Qt::NoModifier || modifiers == Qt::ShiftModifier);

	if (_keys.empty() == false && allowGameplayModifiers)
	{
		const float sceneScale = std::max(_viewCtrl.boundingSphere().getRadius(), 0.001f);
		float factor = std::max(sceneScale * 0.02f, _viewCtrl.viewRange() * 0.01f);
		if (modifiers & Qt::ShiftModifier)
			factor *= 3.0f;

		// https://forum.qt.io/topic/28327/big-issue-with-qt-key-inputs-for-gaming/4
		if (_primaryCamera->getMode() == Camera::CameraMode::Fly || _primaryCamera->getMode() == Camera::CameraMode::FirstPerson)
		{
			const bool firstPerson = _primaryCamera->getMode() == Camera::CameraMode::FirstPerson;
			if (firstPerson)
			{
				if (_keys.contains(Qt::Key_W) || _keys.contains(Qt::Key_Up))
					_primaryCamera->moveForwardPlanar(factor);
				if (_keys.contains(Qt::Key_S) || _keys.contains(Qt::Key_Down))
					_primaryCamera->moveForwardPlanar(-factor);
				if (_keys.contains(Qt::Key_A) || _keys.contains(Qt::Key_Left))
					_primaryCamera->moveAcrossPlanar(-factor);
				if (_keys.contains(Qt::Key_D) || _keys.contains(Qt::Key_Right))
					_primaryCamera->moveAcrossPlanar(factor);
			}
			else
			{
				if (_keys.contains(Qt::Key_W) || _keys.contains(Qt::Key_Up))
					_primaryCamera->moveForward(factor);
				if (_keys.contains(Qt::Key_S) || _keys.contains(Qt::Key_Down))
					_primaryCamera->moveForward(-factor);
				if (_keys.contains(Qt::Key_A) || _keys.contains(Qt::Key_Left))
					_primaryCamera->moveAcross(-factor);
				if (_keys.contains(Qt::Key_D) || _keys.contains(Qt::Key_Right))
					_primaryCamera->moveAcross(factor);
				if (_keys.contains(Qt::Key_Q))
					_primaryCamera->moveWorldUp(-factor);
				if (_keys.contains(Qt::Key_E))
					_primaryCamera->moveWorldUp(factor);
			}
		}
		else
		{
			// Use Orbit-style orthographic nav (as before)
			if (_keys.contains(Qt::Key_A) || _keys.contains(Qt::Key_Left))
				_primaryCamera->moveAcross(factor);
			if (_keys.contains(Qt::Key_D) || _keys.contains(Qt::Key_Right))
				_primaryCamera->moveAcross(-factor);
			if (_keys.contains(Qt::Key_W) || _keys.contains(Qt::Key_Up))
				_primaryCamera->moveUpward(-factor);
			if (_keys.contains(Qt::Key_S) || _keys.contains(Qt::Key_Down))
				_primaryCamera->moveUpward(factor);
		}

		if (_primaryCamera->getMode() == Camera::CameraMode::Fly ||
			_primaryCamera->getMode() == Camera::CameraMode::FirstPerson)
		{
			bool updatedLook = false;
			if (_keys.contains(Qt::Key_J))
			{
				_primaryCamera->getYaw() += 2.0f;
				updatedLook = true;
			}
			if (_keys.contains(Qt::Key_L))
			{
				_primaryCamera->getYaw() -= 2.0f;
				updatedLook = true;
			}
			if (_keys.contains(Qt::Key_I))
			{
				_primaryCamera->getPitch() += 2.0f;
				updatedLook = true;
			}
			if (_keys.contains(Qt::Key_K))
			{
				_primaryCamera->getPitch() -= 2.0f;
				updatedLook = true;
			}

			if (updatedLook)
			{
				const float pitchLimit = (_primaryCamera->getMode() == Camera::CameraMode::FirstPerson) ? 60.0f : 89.0f;
				_primaryCamera->getPitch() = std::clamp(_primaryCamera->getPitch(), -pitchLimit, pitchLimit);
				_primaryCamera->updateFlyView();
			}
		}
		else
		{
			if (_keys.contains(Qt::Key_J))
				_primaryCamera->rotateY(2.0f);
			if (_keys.contains(Qt::Key_L))
				_primaryCamera->rotateY(-2.0f);
			if (_keys.contains(Qt::Key_I))
				_primaryCamera->rotateX(2.0f);
			if (_keys.contains(Qt::Key_K))
				_primaryCamera->rotateX(-2.0f);
			if (_keys.contains(Qt::Key_M))
				_primaryCamera->rotateZ(2.0f);
			if (_keys.contains(Qt::Key_N))
				_primaryCamera->rotateZ(-2.0f);
		}
		if (_keys.contains(Qt::Key_X) || _keys.contains(Qt::Key_Z))
		{
			if(_primaryCamera->getMode() == Camera::CameraMode::Orbit)
			{
				// Zoom only if Orbit camera mode
				if (_keys.contains(Qt::Key_X))
					_viewCtrl.setViewRange(_viewCtrl.viewRange() / 1.05f);
				else
					_viewCtrl.setViewRange(_viewCtrl.viewRange() * 1.05f);
				{
					updateZoomInLimit();
		const float focusRadius = _viewCtrl.zoomInLimit();
					const float minVR = (_viewCtrl.projection() == ViewProjection::PERSPECTIVE)
					    ? std::max(focusRadius * 0.5f, 0.0001f)
					    : std::max(focusRadius / 100.0f, 0.00001f);
					if (_viewCtrl.viewRange() < minVR) _viewCtrl.setViewRange(minVR);
					if (_viewCtrl.viewRange() > _viewCtrl.boundingSphere().getRadius() * 100.0f)
						_viewCtrl.setViewRange(_viewCtrl.boundingSphere().getRadius() * 100.0f);
				}
				// Translate to focus on mouse center
				QPoint pos = mapFromGlobal(QCursor::pos());
				QPoint cen = PickingHelper::clientRectForPoint(pos, width(), height(), _viewCtrl.multiViewActive()).center();
				float sign = (pos.x() > cen.x() || pos.y() < cen.y() ||
					(pos.x() < cen.x() && pos.y() > cen.y())) && _keys.contains(Qt::Key_Q) ? 1.0f : -1.0f;
				QVector3D OP = get3dTranslationVectorFromMousePoints(cen, pos);
				OP *= sign * 0.05f;
				_primaryCamera->move(OP.x(), OP.y(), OP.z());
			}
		}

		_viewCtrl.syncPoseAndRangeFromCamera(*_primaryCamera);
		// This per-frame timer callback is genuine, continuous camera
		// movement while a nav key is held - same treatment as
		// onInertiaTimer()'s coasting (see notifyCameraInteracting()'s doc
		// comment). keyPressEvent()'s own call (notifySceneContentMutated()
		// by default) only covers the initial keydown; without this, holding a
		// nav key would let the idle timer expire mid-navigation and hard-
		// fall-back to raster on GPU. Called here, AFTER this tick's own
		// camera movement above AND after resizeGL() (which rebuilds the
		// projection matrix the X/Z zoom keys' viewRange change needs -
		// see the mouse-wheel zoom handler's identical fix for why getting
		// this order wrong feeds PT a stale zoom scale against this tick's
		// new translation), so the interactive PT renderer is always fed
		// this tick's actual resulting pose instead of the previous tick's.
		resizeGL(width(), height());
		_rtInteractionCtrl->notifyCameraInteracting();
		update();
	}
}

void ViewportWidget::animateViewChange()
{
	// This is a per-frame animation callback (Home/standard-view/axonometric
	// transitions) - genuine camera movement, but UNLIKE onInertiaTimer()'s
	// coasting, deliberately kept OUT of the interactive-GPU-PT path
	// (cameraInteracting=false, not true - see setViewMode()'s doc comment
	// for the full reasoning). This animation's slerp step doesn't decay
	// toward zero the way inertia's velocity does, so the interactive
	// renderer's inherent one-tick-behind lag would show up as a full-sized
	// jerk right at the end of every Home/standard-view/axonometric
	// transition - plain raster/PBR for the whole animation, settling into
	// full-quality PT once it's actually done, is the smoother result.
	//
	// No explicit update() here (unlike most other notify*() call sites) -
	// unlike those, this function's own resizeGL() call at the very end
	// (both paths below) already triggers an unconditional repaint every
	// single tick regardless of PT armed state. Adding one here too used to
	// be harmless when it only fired while PT was armed (the old
	// resetRayTracedIdleTimer() this replaced was gated behind
	// `if (!_rayTracedArmed) return;` at its very top), but making it
	// unconditional tripled the repaints on every single 5ms animation tick
	// even with PT off entirely - a real, reported "animation drastically
	// slower" regression, not a cosmetic one.
	_rtInteractionCtrl->notifyCameraAnimationTick();

	setSectionCapsInteractionSuppressed(true);
	if (_displayedObjectsMemSize > MAX_MODEL_SIZE_BYTES)
		_renderCtrl.setLowResEnabled(true);
	if (_viewCtrl.customViewAnimationActive())
	{
		animateToRotation(_viewCtrl.customViewTargetRotation());
		resizeGL(width(), height());
		return;
	}
	if (_viewCtrl.viewMode() == ViewMode::TOP)
	{
		setRotations(0.0f, 0.0f, 0.0f);
	}
	if (_viewCtrl.viewMode() == ViewMode::BOTTOM)
	{
		setRotations(0.0f, 180.0f, 0.0f);
	}
	if (_viewCtrl.viewMode() == ViewMode::LEFT)
	{
		setRotations(0.0f, -90.0f, 90.0f);
	}
	if (_viewCtrl.viewMode() == ViewMode::RIGHT)
	{
		setRotations(0.0f, -90.0f, -90.0f);
	}
	if (_viewCtrl.viewMode() == ViewMode::FRONT)
	{
		setRotations(0.0f, -90.0f, 0.0f);
	}
	if (_viewCtrl.viewMode() == ViewMode::BACK)
	{
		setRotations(0.0f, -90.0f, 180.0f);
	}
	if (_viewCtrl.viewMode() == ViewMode::ISOMETRIC)
	{
        setRotations(-45.0f, -54.7356f, 0.0f);
	}
	if (_viewCtrl.viewMode() == ViewMode::DIMETRIC)
	{
        setRotations(-20.7048f, -70.5288f, 0.0f);
	}
	if (_viewCtrl.viewMode() == ViewMode::TRIMETRIC)
	{
        setRotations(-30.0f, -55.0f, 0.0f);
	}

	resizeGL(width(), height());
}

void ViewportWidget::animateFitAll()
{
	// See animateViewChange()'s identical comment - fitAll()'s per-frame
	// animation is genuine camera movement too, but stays out of the
	// interactive-GPU-PT path for the same reason (non-decaying per-tick
	// delta -> a visible jerk at the end of the transition otherwise).
	//
	// No notifyCameraAnimationTick()/update() call here (Codex audit catch):
	// setZoomAndPan() below already notifies exactly once per tick on this
	// function's behalf - it's the single owner of that call for the whole
	// zoom/pan animation family (also covers animateWindowZoom() and
	// animateCenterScreen(), the latter of which has NO notify call of its
	// own and relies on setZoomAndPan() entirely). Calling it here too would
	// double the PT teardown/resume-timer churn on every 5ms tick whenever
	// PT is armed - unnecessary overhead in the hottest path, not just a
	// cosmetic duplicate.
	setSectionCapsInteractionSuppressed(true);
	if (_displayedObjectsMemSize > MAX_MODEL_SIZE_BYTES)
		_renderCtrl.setLowResEnabled(true);

	setZoomAndPan(_viewCtrl.viewBoundingSphereDia(), -_viewCtrl.currentTranslation() + _viewCtrl.boundingSphere().getCenter());
	//fitBoxToScreen(_viewCtrl.boundingBox());

	resizeGL(width(), height());
}

void ViewportWidget::animateWindowZoom()
{
	// See animateViewChange()'s identical comment - the window-zoom
	// animation is genuine camera movement too, but stays out of the
	// interactive-GPU-PT path for the same reason (non-decaying per-tick
	// delta -> a visible jerk at the end of the transition otherwise).
	//
	// No notifyCameraAnimationTick()/update() call here - see
	// animateFitAll()'s identical doc comment: setZoomAndPan() below is the
	// sole owner of that call for the whole zoom/pan animation family.
	setSectionCapsInteractionSuppressed(true);
	if (_displayedObjectsMemSize > MAX_MODEL_SIZE_BYTES)
		_renderCtrl.setLowResEnabled(true);
	setZoomAndPan(_viewCtrl.currentViewRange() / _viewCtrl.rubberBandZoomRatio(), _viewCtrl.rubberBandPan());
	resizeGL(width(), height());
}

void ViewportWidget::animateCenterScreen()
{
	setSectionCapsInteractionSuppressed(true);
	setZoomAndPan(_viewCtrl.selectionBoundingSphere().getRadius() * 2,
		-_viewCtrl.currentTranslation() + _viewCtrl.selectionBoundingSphere().getCenter());
	resizeGL(width(), height());
}

void ViewportWidget::onInertiaTimer()
{
	// Inertia effects are suppressed when a glTF camera is active (read-only view).
	if (isGltfCameraActive())
		return;

	bool active = false;

	// --- Pan inertia ---
	if (_viewCtrl.inertiaPanVelocity().lengthSquared() > 0.01f) {
		// Apply pan inertia from the last pan point, in the same way as interactive panning
		QPointF panDelta(-_viewCtrl.inertiaPanVelocity().x(), -_viewCtrl.inertiaPanVelocity().y());
		QPoint newPanPoint = _viewCtrl.lastPanPoint() + panDelta.toPoint();
		QVector3D OP = get3dTranslationVectorFromMousePoints(_viewCtrl.lastPanPoint(), newPanPoint);
		_primaryCamera->move(OP.x(), OP.y(), OP.z());
		_viewCtrl.syncTranslationFromCamera(*_primaryCamera);
		_viewCtrl.setLastPanPoint(newPanPoint); // Update for next frame
		_viewCtrl.scaleInertiaPanVelocity(_viewCtrl.inertiaDamping());
		active = true;
	}

	// --- Zoom inertia ---
		if (std::abs(_viewCtrl.inertiaZoomVelocity()) > 0.001f) {
		float zoomFactor = 1.005f;
		if (_viewCtrl.inertiaZoomVelocity() > 0)
			_viewCtrl.setViewRange(_viewCtrl.viewRange() / zoomFactor);
		else
			_viewCtrl.setViewRange(_viewCtrl.viewRange() * zoomFactor);

		QPoint cen = PickingHelper::viewportRectForPoint(mapFromGlobal(QCursor::pos()), width(), height(), _viewCtrl.multiViewActive()).center();
		QVector3D OP = get3dTranslationVectorFromMousePoints(cen, cen);
		OP *= -_viewCtrl.inertiaZoomPanVelocity() * 0.05f;
		_primaryCamera->move(OP.x(), OP.y(), OP.z());
		_viewCtrl.syncTranslationFromCamera(*_primaryCamera);

		// Decay inertia
		_viewCtrl.scaleInertiaZoomVelocity(_viewCtrl.inertiaDamping() * 0.1f);
		_viewCtrl.scaleInertiaZoomPanVelocity(_viewCtrl.inertiaDamping() * 0.1f);

		if (std::abs(_viewCtrl.inertiaZoomVelocity()) > 0.001f)
			active = true;
		else
			_viewCtrl.setInertiaZoomVelocity(0.0f);

		resizeGL(width(), height());

		updateZoomInLimit();
		const float focusRadius = _viewCtrl.zoomInLimit();
		const float minRange = (_viewCtrl.projection() == ViewProjection::PERSPECTIVE)
		    ? std::max(focusRadius * 0.5f, 0.0001f)
		    : std::max(focusRadius / 100.0f, 0.00001f);
		const float maxRange = _viewCtrl.boundingSphere().getRadius() * 100.0f;
		if (_viewCtrl.viewRange() < minRange) _viewCtrl.setViewRange(minRange);
		if (_viewCtrl.viewRange() > maxRange) _viewCtrl.setViewRange(maxRange);
		_viewCtrl.syncCurrentViewRange();

		active = true;
	}

	// --- Rotation inertia ---
	if (_viewCtrl.inertiaRotateVelocity().lengthSquared() > 0.01f) {
		if (_primaryCamera->getMode() == Camera::CameraMode::Fly ||
		    _primaryCamera->getMode() == Camera::CameraMode::FirstPerson)
		{
			_primaryCamera->getYaw()   += _viewCtrl.inertiaRotateVelocity().x() / 2.0f;
			_primaryCamera->getPitch() += _viewCtrl.inertiaRotateVelocity().y() / 2.0f;
			if (_primaryCamera->getMode() == Camera::CameraMode::FirstPerson)
				_primaryCamera->getPitch() = std::clamp(_primaryCamera->getPitch(), -60.0f, 60.0f);
			else
				_primaryCamera->getPitch() = std::clamp(_primaryCamera->getPitch(), -89.0f, 89.0f);
			_primaryCamera->updateFlyView();
		}
		else
		{
			_primaryCamera->rotateX(_viewCtrl.inertiaRotateVelocity().y() / 2.0);
			_primaryCamera->rotateY(_viewCtrl.inertiaRotateVelocity().x() / 2.0);
		}
		_viewCtrl.syncRotationFromCamera(*_primaryCamera);
		_viewCtrl.scaleInertiaRotateVelocity(_viewCtrl.inertiaDamping());
		active = true;
	}

	if (!active) {
		_inertiaTimer->stop();
		_viewCtrl.clearInertiaState();
		QTimer::singleShot(100, this, &ViewportWidget::disableSectionCapsInteractionSuppression);
	}

	// Inertia keeps moving the camera after mouse-up - keep deferring the
	// settle countdown for as long as this timer keeps firing (it stops
	// itself once the decaying velocity drops below its own threshold
	// above). notifyCameraInteracting(): inertial coasting is genuine camera
	// movement, same as a live drag. Called here, AFTER this tick's own pan/zoom/rotation blocks
	// above (not before, as this used to be positioned) - the interactive
	// GPU PT renderer is fed whatever camera state this call captures, so
	// calling it before applying this tick's own movement fed it last
	// tick's pose instead of this one's: the displayed frame was always
	// exactly one inertia tick behind the camera's actual position, most
	// noticeable as a residual shift in the same direction as the motion
	// right before it stopped, once something else (e.g. re-arming path-
	// traced mode) forced a fresh capture of the truly-current camera.
	_rtInteractionCtrl->notifyCameraInteracting();

	update();
}

void ViewportWidget::stopAnimations()
{
	_animateViewTimer->stop();
	_animateFitAllTimer->stop();
	_animateWindowZoomTimer->stop();
	_animateCenterScreenTimer->stop();
	_keyboardNavTimer->start();
	QTimer::singleShot(100, this, &ViewportWidget::disableLowRes);
	QTimer::singleShot(100, this, &ViewportWidget::disableSectionCapsInteractionSuppression);
}


// Note: convertClickToRay is now implemented in SelectionManager
// Keeping getViewportFromPoint and getClientRectFromPoint as they're still used by ViewportWidget

Camera* ViewportWidget::getCameraForPoint(const QPoint& pixel)
{
	if (!_viewCtrl.multiViewActive())
		return _primaryCamera;

	// Determine which ortho view contains this pixel (same quadrant logic as getViewportFromPoint).
	// Isometric viewport (bottom-right) uses the primary camera unchanged.
	ViewMode viewMode;
	if (pixel.x() < width() / 2 && pixel.y() > height() / 2)
		viewMode = ViewMode::TOP;
	else if (pixel.x() < width() / 2 && pixel.y() <= height() / 2)
		viewMode = ViewMode::FRONT;
	else if (pixel.x() >= width() / 2 && pixel.y() < height() / 2)
		viewMode = ViewMode::LEFT;
	else
		return _primaryCamera; // Isometric viewport

	// Configure the shared ortho camera exactly as the matching pane was rendered.
	const std::vector<QVector3D> multiViewCorners = collectVisibleCorners();
	const QVector3D sharedMultiViewCenter = _primaryCamera->getPosition();
	const float zoomScale = (_viewCtrl.viewBoundingSphereDia() > 0.0f)
		? (_viewCtrl.viewRange() / _viewCtrl.viewBoundingSphereDia()) : 1.0f;
	configureOrthoSubviewCamera(
		viewMode,
		multiViewCorners,
		width() / 2,
		height() / 2,
		sharedMultiViewCenter,
		computeSharedOrthographicMultiViewRange(multiViewCorners, width() / 2, height() / 2, sharedMultiViewCenter) * zoomScale);
	return _orthoViewsCamera;
}

QVector3D ViewportWidget::get3dTranslationVectorFromMousePoints(const QPoint& start, const QPoint& end)
{
	if (width() <= 0 || height() <= 0)
		return QVector3D(0, 0, 0);

	auto clampPointToRect = [](const QPoint& point, const QRect& rect) {
		if (rect.width() <= 0 || rect.height() <= 0)
			return point;

		return QPoint(
			std::clamp(point.x(), rect.left(), rect.right()),
			std::clamp(point.y(), rect.top(), rect.bottom()));
	};

	// Determine viewport and camera
	const QRect widgetRect(0, 0, width(), height());
	const QPoint safeStart = clampPointToRect(start, widgetRect);
	QRect viewport = _viewCtrl.navigationViewportLocked()
		? _viewCtrl.navigationLockedViewport()
		: PickingHelper::viewportRectForPoint(safeStart, width(), height(), _viewCtrl.multiViewActive());
	QRect clientRect = _viewCtrl.navigationViewportLocked()
		? _viewCtrl.navigationLockedClientRect()
		: PickingHelper::clientRectForPoint(safeStart, width(), height(), _viewCtrl.multiViewActive());
	if (viewport.width() <= 0 || viewport.height() <= 0)
		return QVector3D(0, 0, 0);

	const QPoint clampedStart = clampPointToRect(safeStart, clientRect);
	const QPoint clampedEnd = clampPointToRect(clampPointToRect(end, widgetRect), clientRect);
	Camera* camera = _viewCtrl.multiViewActive() && (viewport.x() != viewport.width() || viewport.y() != 0)
		? _orthoViewsCamera
		: _primaryCamera;

	QVector3D viewCenter = (camera->getMode() == Camera::CameraMode::Orbit)
		? camera->getPosition()
		: _viewCtrl.boundingSphere().getCenter();
	// Get view and projection matrices
	QMatrix4x4 view = camera->getViewMatrix();
	QMatrix4x4 projection = camera->getProjectionMatrix();
	QMatrix4x4 inv = (projection * view).inverted();

	if (camera->getProjectionType() == Camera::ProjectionType::ORTHOGRAPHIC) {
		const float viewRange = std::max(camera->getViewRange(), 0.0001f);
		const float viewportWidth = static_cast<float>(std::max(viewport.width(), 1));
		const float viewportHeight = static_cast<float>(std::max(viewport.height(), 1));

		float halfWidth = 0.0f;
		float halfHeight = 0.0f;
		if (viewportWidth <= viewportHeight)
		{
			halfWidth = viewRange * 0.5f;
			halfHeight = halfWidth * (viewportHeight / viewportWidth);
		}
		else
		{
			halfHeight = viewRange * 0.5f;
			halfWidth = halfHeight * (viewportWidth / viewportHeight);
		}

		const float unitsPerPixelX = (2.0f * halfWidth) / viewportWidth;
		const float unitsPerPixelY = (2.0f * halfHeight) / viewportHeight;
		const float dxPixels = static_cast<float>(clampedEnd.x() - clampedStart.x());
		const float dyPixels = static_cast<float>(clampedEnd.y() - clampedStart.y());

		const QVector3D right = camera->getRightVector().normalized();
		const QVector3D up = camera->getUpVector().normalized();
		return right * (dxPixels * unitsPerPixelX) - up * (dyPixels * unitsPerPixelY);
	}
	else {
		auto sampleSceneDepth = [&](const QPoint& point) {
			makeCurrent();

			float rawDepth = 1.0f;
			const QPoint clampedPoint = clampPointToRect(point, viewport);
			const int cx = clampedPoint.x();
			const int cy = height() - clampedPoint.y() - 1;
			glReadPixels(cx, cy, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &rawDepth);

			if (rawDepth >= 1.0f)
			{
				const int halfGrid = 4;
				const int x0 = std::max(0, cx - halfGrid);
				const int y0 = std::max(0, cy - halfGrid);
				const int x1 = std::min(width() - 1, cx + halfGrid);
				const int y1 = std::min(height() - 1, cy + halfGrid);
				const int sw = x1 - x0 + 1;
				const int sh = y1 - y0 + 1;
				if (sw > 0 && sh > 0)
				{
					std::vector<float> depthBuf(sw * sh, 1.0f);
					glReadPixels(x0, y0, sw, sh, GL_DEPTH_COMPONENT, GL_FLOAT, depthBuf.data());

					float minDepth = 1.0f;
					for (float d : depthBuf)
					{
						if (d < minDepth)
							minDepth = d;
					}

					if (minDepth < 1.0f)
						rawDepth = minDepth;
				}
			}

			if (rawDepth >= 1.0f)
			{
				const QVector3D projectedCenter = viewCenter.project(view, projection, viewport);
				rawDepth = projectedCenter.z();
			}

			return rawDepth;
		};

		const float depthZ = sampleSceneDepth(clampedStart);
		QVector3D worldStart(clampedStart.x(), height() - clampedStart.y(), depthZ);
		QVector3D worldEnd(clampedEnd.x(), height() - clampedEnd.y(), depthZ);

		const QVector3D startWorld = worldStart.unproject(view, projection, viewport);
		const QVector3D endWorld = worldEnd.unproject(view, projection, viewport);
		return endWorld - startWorld;
	}
}


unsigned int ViewportWidget::loadTextureFromFile(
	const char* path,
	GLenum wrapS, GLenum wrapT,
	GLenum minFilter, GLenum magFilter,
	bool flipY)
{
	GLuint textureID = 0;

	// Load image using Qt
	QImageReader reader(path);
	reader.setAutoTransform(true); // respects EXIF orientation

	QImage image = reader.read();
	if (image.isNull())
	{
		qWarning() << "Texture failed to load:" << path
			<< reader.errorString();
		return 0;
	}

	// Optional vertical flip (OpenGL vs Qt coordinate difference)
	if (flipY)
		image = image.flipped(Qt::Vertical);

	GLenum internalFormat = GL_RGBA8;
	GLenum dataFormat = GL_RGBA;
	GLenum dataType = GL_UNSIGNED_BYTE;

	QImage glImage;

	switch (image.format())
	{
	case QImage::Format_RGB888:
		glImage = image;
		internalFormat = GL_RGB8;
		dataFormat = GL_RGB;
		break;

	case QImage::Format_RGBA8888:
	case QImage::Format_RGBA8888_Premultiplied:
		glImage = image;
		internalFormat = GL_RGBA8;
		dataFormat = GL_RGBA;
		break;

	case QImage::Format_Grayscale8:
		// Expand to RGBA so all three colour channels are populated.
		// Uploading as GL_RED leaves G and B at 0, making the texture appear red.
		glImage = image.convertToFormat(QImage::Format_RGBA8888);
		internalFormat = GL_RGBA8;
		dataFormat = GL_RGBA;
		break;

	case QImage::Format_Indexed8:
		glImage = image.convertToFormat(QImage::Format_RGBA8888);
		internalFormat = GL_RGBA8;
		dataFormat = GL_RGBA;
		break;

	default:
		// Fallback for uncommon formats (ARGB32, RGB32, etc.)
		glImage = image.convertToFormat(QImage::Format_RGBA8888);
		internalFormat = GL_RGBA8;
		dataFormat = GL_RGBA;
		break;
	}

	// Create OpenGL texture
	glGenTextures(1, &textureID);
	glBindTexture(GL_TEXTURE_2D, textureID);

	glTexImage2D(
		GL_TEXTURE_2D,
		0,
		internalFormat,
		glImage.width(),
		glImage.height(),
		0,
		dataFormat,
		dataType,
		glImage.constBits()
	);

	glGenerateMipmap(GL_TEXTURE_2D);

	// Texture parameters
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);

	// Anisotropic filtering (if supported)
	bool hasAnisotropy =
		context()->hasExtension("GL_EXT_texture_filter_anisotropic");

	if (hasAnisotropy)
	{
		GLfloat maxAniso = 0.0f;
		glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);

		GLfloat aniso = qMin(_renderCtrl.anisotropicFilteringLevel(), maxAniso);

		glTexParameterf(
			GL_TEXTURE_2D,
			GL_TEXTURE_MAX_ANISOTROPY_EXT,
			aniso
		);
	}

	glBindTexture(GL_TEXTURE_2D, 0);
	return textureID;
}

QList<int> ViewportWidget::sweepSelect(const QPoint& pixel, bool addToSelection)
{
	if (!_selectionManager || !_rubberBand || _rubberBand->geometry().isNull())
		return _selectionManager ? _selectionManager->getSelectedIds() : QList<int>{};

	const QList<int> selectedIds = _selectionManager->sweepSelect(_viewCtrl.leftButtonPoint(), pixel, addToSelection);
	emit selectionChanged(selectedIds);
	emit sweepSelectionDone(selectedIds);
	return selectedIds;
}

void ViewportWidget::setView(QVector3D viewPos, QVector3D viewDir, QVector3D upDir, QVector3D rightDir)
{
	_primaryCamera->setView(viewPos, viewDir, upDir, rightDir);
	emit viewSet();
}

// Collect a representative set of world-space vertex positions for every
// visible mesh.  Using actual vertices (sampled for large meshes) gives a
// genuinely tight projected silhouette — no phantom corners that arise when
// an AABB combines, say, the maximum-X from the arm tip with the maximum-Y
// from the lamp body at a point that never exists in the geometry.
// Sampling cap: at most MAX_SAMPLES_PER_MESH positions per mesh so that
// fitting remains fast even for high-poly scenes.
std::vector<QVector3D> ViewportWidget::collectVisibleCorners() const
{
	constexpr int MAX_SAMPLES_PER_MESH = 1024;

	const auto& ids = _sceneRuntime.currentVisibleObjectIds();
	std::vector<QVector3D> points;
	points.reserve(ids.size() * MAX_SAMPLES_PER_MESH);

	for (int i : ids)
	{
		try
		{
			const SceneMesh* mesh = _sceneRuntime.meshAt(i);
			const std::vector<float>& pts = mesh->getTrsfPoints();
			const int nVerts = static_cast<int>(pts.size()) / 3;

			const QVector3D expOff = mesh->explosionOffset();

			if (nVerts <= 0)
			{
				// Fallback: use the 8 AABB corners if vertex data is absent
				for (const QVector3D& c : mesh->getBoundingBox().getCorners())
					points.push_back(c + expOff);
				continue;
			}

			// Uniform stride so we always inspect ≤ MAX_SAMPLES_PER_MESH vertices
			// while still touching the full extent of the mesh (first + last are
			// always included, then evenly-spaced interior samples).
			const int stride = std::max(1, nVerts / MAX_SAMPLES_PER_MESH);
			for (int j = 0; j < nVerts; j += stride)
			{
				const int b = j * 3;
				points.emplace_back(pts[b] + expOff.x(), pts[b + 1] + expOff.y(), pts[b + 2] + expOff.z());
			}
			// Always include the last vertex so we never miss a boundary point
			if (nVerts > 0)
			{
				const int b = (nVerts - 1) * 3;
				points.emplace_back(pts[b] + expOff.x(), pts[b + 1] + expOff.y(), pts[b + 2] + expOff.z());
			}
		}
		catch (const std::out_of_range&) {}
	}

    // Fallback: if somehow empty, use visible mesh AABBs with explosion offsets.
    if (points.empty())
    {
        for (int i : ids)
        {
            try
            {
                const SceneMesh* mesh = _sceneRuntime.meshAt(i);
                if (!mesh)
                    continue;

                const QVector3D expOff = mesh->explosionOffset();
                for (const QVector3D& c : mesh->getBoundingBox().getCorners())
                    points.push_back(c + expOff);
            }
            catch (const std::out_of_range&) {}
        }
    }

    // Final fallback: scene AABB if no visible mesh points could be gathered.
    if (points.empty())
        return _viewCtrl.boundingBox().getCorners();
    return points;
}

// Convenience: read axes from the current view matrix, then delegate.
float ViewportWidget::computeFitViewRange(QVector3D* outCenter) const
{
	const QMatrix4x4 V = _primaryCamera->getViewMatrix();
	return computeFitViewRange(
		 V.row(0).toVector3D().normalized(),
		 V.row(1).toVector3D().normalized(),
		-V.row(2).toVector3D().normalized(),
		outCenter);
}

// Convenience: collect visible corners, then delegate to the core.
// Used by setViewMode() with the destination quaternion's axes so that
// rotation and zoom can animate concurrently.
float ViewportWidget::computeFitViewRange(
	const QVector3D& right, const QVector3D& up, const QVector3D& viewDir,
	QVector3D* outCenter) const
{
	return computeFitViewRange(collectVisibleCorners(), right, up, viewDir, outCenter);
}

float ViewportWidget::computeOrthographicFitViewRangeForViewport(
	const std::vector<QVector3D>& corners,
	const QVector3D& right,
	const QVector3D& up,
	const QVector3D& viewDir,
	int viewportWidth,
	int viewportHeight,
	QVector3D* outCenter,
	const QVector3D& eyePos) const
{
	if (corners.empty())
	{
		if (outCenter) *outCenter = _viewCtrl.boundingSphere().getCenter();
		return std::max(_viewCtrl.boundingSphere().getRadius() * 2.0f, 0.0001f);
	}

	float xMin_v = std::numeric_limits<float>::max();
	float xMax_v = -std::numeric_limits<float>::max();
	float yMin_v = std::numeric_limits<float>::max();
	float yMax_v = -std::numeric_limits<float>::max();
	float zMin_v = std::numeric_limits<float>::max();
	float zMax_v = -std::numeric_limits<float>::max();

	for (const QVector3D& c : corners)
	{
		// Project relative to eyePos so the range is the actual screen-space extent
		// from the ortho camera's origin (the orbit center / look-at point).
		const QVector3D rel = c - eyePos;
		const float xc = QVector3D::dotProduct(rel, right);
		const float yc = QVector3D::dotProduct(rel, up);
		const float zc = QVector3D::dotProduct(rel, viewDir);
		xMin_v = std::min(xMin_v, xc);  xMax_v = std::max(xMax_v, xc);
		yMin_v = std::min(yMin_v, yc);  yMax_v = std::max(yMax_v, yc);
		zMin_v = std::min(zMin_v, zc);  zMax_v = std::max(zMax_v, zc);
	}

	// The ortho window is symmetric around the eye, so the required half-extent
	// is the farthest corner distance from the eye on each screen axis.
	const float halfX = std::max(std::abs(xMin_v), std::abs(xMax_v));
	const float halfY = std::max(std::abs(yMin_v), std::abs(yMax_v));
	const float cx = (xMin_v + xMax_v) * 0.5f;
	const float cy = (yMin_v + yMax_v) * 0.5f;
	const float cz = (zMin_v + zMax_v) * 0.5f;
	if (outCenter)
		*outCenter = eyePos + right * cx + up * cy + viewDir * cz;

	const int safeWidth = std::max(viewportWidth, 1);
	const int safeHeight = std::max(viewportHeight, 1);
	const float aspect = static_cast<float>(safeWidth) / static_cast<float>(safeHeight);
	constexpr float margin = 1.05f;

	float halfRange;
	if (safeWidth > safeHeight)
		halfRange = std::max(halfX / std::max(aspect, 0.001f), halfY);
	else
		halfRange = std::max(halfX, halfY * aspect);

	return std::max(halfRange * 2.0f * margin, 0.0001f);
}

QVector3D ViewportWidget::computeVisibleWorldCenter(const std::vector<QVector3D>& corners) const
{
	if (corners.empty())
	{
		return QVector3D(
			static_cast<float>((_viewCtrl.boundingBox().xMin() + _viewCtrl.boundingBox().xMax()) * 0.5),
			static_cast<float>((_viewCtrl.boundingBox().yMin() + _viewCtrl.boundingBox().yMax()) * 0.5),
			static_cast<float>((_viewCtrl.boundingBox().zMin() + _viewCtrl.boundingBox().zMax()) * 0.5));
	}

	float minX = std::numeric_limits<float>::max();
	float minY = std::numeric_limits<float>::max();
	float minZ = std::numeric_limits<float>::max();
	float maxX = -std::numeric_limits<float>::max();
	float maxY = -std::numeric_limits<float>::max();
	float maxZ = -std::numeric_limits<float>::max();

	for (const QVector3D& c : corners)
	{
		minX = std::min(minX, c.x());
		minY = std::min(minY, c.y());
		minZ = std::min(minZ, c.z());
		maxX = std::max(maxX, c.x());
		maxY = std::max(maxY, c.y());
		maxZ = std::max(maxZ, c.z());
	}

	return QVector3D(
		(minX + maxX) * 0.5f,
		(minY + maxY) * 0.5f,
		(minZ + maxZ) * 0.5f);
}

float ViewportWidget::computeSharedOrthographicMultiViewRange(
	const std::vector<QVector3D>& corners,
	int viewportWidth,
	int viewportHeight,
	const QVector3D& eyePos) const
{
	float sharedRange = 0.0001f;
	for (const ViewMode viewMode : { ViewMode::TOP, ViewMode::FRONT, ViewMode::LEFT })
	{
		QVector3D viewDir;
		QVector3D upDir;
		QVector3D rightDir;
		CoordinateSystemHelper::standardViewBasis(_viewCtrl.cameraUpAxisZUp(), viewMode, viewDir, upDir, rightDir);
		sharedRange = std::max(
			sharedRange,
			computeOrthographicFitViewRangeForViewport(
				corners, rightDir, upDir, viewDir, viewportWidth, viewportHeight, nullptr, eyePos));
	}
	return sharedRange;
}

void ViewportWidget::configureOrthoSubviewCamera(
	ViewMode viewMode,
	const std::vector<QVector3D>& corners,
	int viewportWidth,
	int viewportHeight,
	const QVector3D& sharedCenter,
	float sharedViewRange)
{
	Q_UNUSED(corners);

	QVector3D viewDir;
	QVector3D upDir;
	QVector3D rightDir;
	CoordinateSystemHelper::standardViewBasis(_viewCtrl.cameraUpAxisZUp(), viewMode, viewDir, upDir, rightDir);

	_orthoViewsCamera->setScreenSize(viewportWidth, viewportHeight);
	_orthoViewsCamera->setViewRange(sharedViewRange);
	_orthoViewsCamera->setSceneRadius(_viewCtrl.boundingSphere().getRadius());
	_orthoViewsCamera->setProjectionType(Camera::ProjectionType::ORTHOGRAPHIC);
	_orthoViewsCamera->setView(sharedCenter, viewDir, upDir, rightDir);
}

// Core implementation: fits an arbitrary set of world-space corners given
// explicit view axes.  Analytical for both ortho and perspective.
float ViewportWidget::computeFitViewRange(const std::vector<QVector3D>& corners,
	const QVector3D& right, const QVector3D& up, const QVector3D& viewDir,
	QVector3D* outCenter) const
{
	if (corners.empty())
	{
		if (outCenter) *outCenter = _viewCtrl.boundingSphere().getCenter();
		return _viewCtrl.boundingSphere().getRadius() * 2.0f;
	}

	// Project every corner onto the view axes using ABSOLUTE dot products.
	// The midpoint of the resulting intervals is the "visual centre" of the
	// scene for this orientation — the point that should appear at screen centre
	// so that equal margins surround the geometry on every side.
	float xMin_v =  std::numeric_limits<float>::max();
	float xMax_v = -std::numeric_limits<float>::max();
	float yMin_v =  std::numeric_limits<float>::max();
	float yMax_v = -std::numeric_limits<float>::max();
	float zMin_v =  std::numeric_limits<float>::max();
	float zMax_v = -std::numeric_limits<float>::max();

	for (const QVector3D& c : corners)
	{
		const float xc = QVector3D::dotProduct(c, right);
		const float yc = QVector3D::dotProduct(c, up);
		const float zc = QVector3D::dotProduct(c, viewDir);
		xMin_v = std::min(xMin_v, xc);  xMax_v = std::max(xMax_v, xc);
		yMin_v = std::min(yMin_v, yc);  yMax_v = std::max(yMax_v, yc);
		zMin_v = std::min(zMin_v, zc);  zMax_v = std::max(zMax_v, zc);
	}

	// Half-spans: these are the minimum extents required on each side of the
	// projected centre — independent of the old bounding-sphere centre.
	const float halfX = (xMax_v - xMin_v) * 0.5f;
	const float halfY = (yMax_v - yMin_v) * 0.5f;

	if (halfX <= 0.0f && halfY <= 0.0f)
	{
		if (outCenter) *outCenter = _viewCtrl.boundingSphere().getCenter();
		return _viewCtrl.boundingSphere().getRadius() * 2.0f;
	}

	// Projected visual centre — the point in 3-D whose view-space coordinates
	// are the midpoints of the extent intervals.  Callers use this as the new
	// orbit/pan target so the scene is centred on screen after a fit operation.
	const float cx = (xMin_v + xMax_v) * 0.5f;
	const float cy = (yMin_v + yMax_v) * 0.5f;
	const float cz = (zMin_v + zMax_v) * 0.5f;
	const QVector3D projCenter = right * cx + up * cy + viewDir * cz;
	if (outCenter) *outCenter = projCenter;

	const float aspect = static_cast<float>(width()) / static_cast<float>(height());
	constexpr float margin = 1.05f;
	float viewRange = 0.0f;

	if (_viewCtrl.projection() == ViewProjection::ORTHOGRAPHIC)
	{
		// The ortho projection maps halfRange to the shorter screen dimension:
		//   landscape (w > h): half-height = halfRange, half-width = halfRange * aspect
		//   portrait  (w ≤ h): half-width  = halfRange, half-height = halfRange / aspect
		// Using halfX = xSpan/2 (relative to the projected centre) ensures
		// equal margins on both sides and no wasted screen space.
		float halfRange;
		if (width() > height())
			halfRange = std::max(halfX / aspect, halfY);
		else
			halfRange = std::max(halfX, halfY * aspect);

		viewRange = halfRange * 2.0f * margin;
	}
	else // PERSPECTIVE
	{
		// For each corner at view-space offset (xc_rel, yc_rel, dc) from the
		// projected centre:
		//   shiftFactor * viewRange ≥ max(|xc_rel|/tan_half_x, |yc_rel|/tan_half_y) − dc
		// Near-side corners (dc < 0) increase the requirement; far corners reduce it.
		const float fovRad      = qDegreesToRadians(_viewCtrl.FOV());
		const float tanHalfFov  = std::tan(fovRad * 0.5f);
		const float sinHalfFov  = std::sin(fovRad * 0.5f);
		const float shiftFactor = std::min(1.05f / sinHalfFov, 1.25f);

		float maxReq = 0.0f;
		for (const QVector3D& c : corners)
		{
			const float xc_rel = QVector3D::dotProduct(c, right)   - cx;
			const float yc_rel = QVector3D::dotProduct(c, up)      - cy;
			const float dc     = QVector3D::dotProduct(c, viewDir) - cz;

			float req;
			if (aspect >= 1.0f) // landscape: tan_half_x = tanHalfFov * aspect
				req = std::max(std::abs(xc_rel) / aspect, std::abs(yc_rel)) / tanHalfFov - dc;
			else               // portrait:  tan_half_x = tanHalfFov
				req = std::max(std::abs(xc_rel), std::abs(yc_rel) * aspect) / tanHalfFov - dc;

			maxReq = std::max(maxReq, req);
		}

		viewRange = maxReq / shiftFactor * margin;
	}

	return std::max(viewRange, 0.0001f);
}

// Improved approach based on rubberband zoom technique
void ViewportWidget::fitBoxToScreen(const BoundingBox& box)
{			
	// Project bounding box corners to screen space
	std::vector<Point> corners = box.corners();
	std::vector<QVector3D> vcorners =	
	{
	QVector3D(corners[0].getX(), corners[0].getY(), corners[0].getZ()),
	QVector3D(corners[1].getX(), corners[1].getY(), corners[1].getZ()),
	QVector3D(corners[2].getX(), corners[2].getY(), corners[2].getZ()),
	QVector3D(corners[3].getX(), corners[3].getY(), corners[3].getZ()),
	QVector3D(corners[4].getX(), corners[4].getY(), corners[4].getZ()),
	QVector3D(corners[5].getX(), corners[5].getY(), corners[5].getZ()),
	QVector3D(corners[6].getX(), corners[6].getY(), corners[6].getZ()),
	QVector3D(corners[7].getX(), corners[7].getY(), corners[7].getZ())
	};

	QRect screenBounds;
	bool firstPoint = true;

	for (const auto& corner : vcorners)
	{
		// Project point to screen coordinates
		QVector4D clipCoords = _viewCtrl.projectionMatrix() * _viewCtrl.viewMatrix() * QVector4D(corner, 1.0f);

		QVector3D screenPoint(clipCoords.x() / clipCoords.w(),
                           clipCoords.y() / clipCoords.w(), 
                           clipCoords.z() / clipCoords.w());
				
		QPoint pixelPoint(
			static_cast<int>((clipCoords.x() + 1.0f) * 0.5f * width()),
			static_cast<int>(height() - (clipCoords.y() + 1.0f) * 0.5f * height())
		);

		// Update screen bounds
		if (firstPoint) {
			screenBounds = QRect(pixelPoint, QSize(1, 1));
			firstPoint = false;
		}
		else {
			screenBounds = screenBounds.united(QRect(pixelPoint, QSize(1, 1)));
		}
	}

	// Calculate client rect (full viewport)
	QRect clientRect(0, 0, width(), height());

	// Calculate zoom ratio using the same approach as window zoom
	double widthRatio = static_cast<double>(clientRect.width()) / screenBounds.width();
	double heightRatio = static_cast<double>(clientRect.height()) / screenBounds.height();

	// Use the smaller ratio to ensure the box fits in both dimensions
	// Apply a factor of 0.95 to leave a small margin around the object
	double zoomRatio = std::min(widthRatio, heightRatio) * 0.95;
		
	// Get center points for screen and box
	QPoint screenCenter = screenBounds.center();
	QPoint viewportCenter = clientRect.center();

	// Convert pan offset to 3D space
	// First, get world coordinates at screen center with current Z
	QVector3D screenCenterWorld(screenCenter.x(), height() - screenCenter.y(), 0.5);
	QVector3D viewportCenterWorld(viewportCenter.x(), height() - viewportCenter.y(), 0.5);

	// Unproject both points to get world coordinates
	QVector3D screenCenterPoint = screenCenterWorld.unproject(
		_viewCtrl.viewMatrix() * _viewCtrl.modelMatrix(),
		_viewCtrl.projectionMatrix(),
		QRect(0, 0, width(), height()));
	QVector3D viewportCenterPoint = viewportCenterWorld.unproject(
		_viewCtrl.viewMatrix() * _viewCtrl.modelMatrix(),
		_viewCtrl.projectionMatrix(),
		QRect(0, 0, width(), height()));

	// Calculate the pan vector
	QVector3D panVector = screenCenterPoint - viewportCenterPoint;

	
	setZoomAndPan(_viewCtrl.currentViewRange() / zoomRatio, panVector);
}


void ViewportWidget::animateToRotation(const QQuaternion& targetRotation)
{
	// Only ever reached through animateViewChange() (directly, or via
	// setRotations() - itself only called from within animateViewChange())
	// - same Home/standard-view/axonometric animation family, same
	// non-decaying-per-tick-delta reasoning, so this stays out of the
	// interactive-GPU-PT path too.
	//
	// No notifyCameraAnimationTick() call here (Codex audit catch):
	// animateViewChange() already notifies exactly once, unconditionally, at
	// the very top of every tick before reaching either of the two paths
	// that end up calling this function - notifying again here just doubles
	// the PT teardown/resume-timer churn on every 5ms tick whenever PT is
	// armed, for no benefit (this function has no OTHER caller that could
	// otherwise go unnotified, unlike setZoomAndPan()'s identical situation
	// with animateCenterScreen()).
	QQuaternion curRot = QQuaternion::slerp(_viewCtrl.currentRotation(), targetRotation, _viewCtrl.advanceSlerpStep());

	QMatrix4x4 rotMat = QMatrix4x4(curRot.toRotationMatrix());
	QVector3D viewDir = -rotMat.row(2).toVector3D();
	QVector3D upDir = rotMat.row(1).toVector3D();
	QVector3D rightDir = rotMat.row(0).toVector3D();

	float scaleStep = (_viewCtrl.currentViewRange() - _viewCtrl.viewBoundingSphereDia()) * _viewCtrl.slerpFrac();
	_viewCtrl.setViewRange(_viewCtrl.viewRange() - scaleStep);

	QVector3D curPos;
	if (_primaryCamera->getMode() == Camera::CameraMode::Fly ||
		_primaryCamera->getMode() == Camera::CameraMode::FirstPerson)
	{
		QMatrix4x4 targetRotMat(targetRotation.toRotationMatrix());
		QVector3D targetViewDir = -targetRotMat.row(2).toVector3D().normalized();
		const float fovRad = qDegreesToRadians(_viewCtrl.FOV());
		const float sinHalfFov = std::max(std::sin(fovRad * 0.5f), 0.001f);
		const float shiftFactor = std::min(1.05f / sinHalfFov, 1.25f);
		const float targetDistance = shiftFactor * _viewCtrl.viewBoundingSphereDia();
		const QVector3D targetEye = _viewCtrl.boundingSphere().getCenter() - targetViewDir * targetDistance;
		curPos = _viewCtrl.currentTranslation() - (_viewCtrl.slerpStep() * _viewCtrl.currentTranslation()) + (targetEye * _viewCtrl.slerpStep());
	}
	else
	{
		curPos = _viewCtrl.currentTranslation() - (_viewCtrl.slerpStep() * _viewCtrl.currentTranslation()) + (_viewCtrl.boundingSphere().getCenter() * _viewCtrl.slerpStep());
	}

	_primaryCamera->setView(curPos, viewDir, upDir, rightDir);
	if (_primaryCamera->getMode() == Camera::CameraMode::Fly ||
		_primaryCamera->getMode() == Camera::CameraMode::FirstPerson)
	{
		_primaryCamera->setYawPitchFromViewDir();
	}

	if (qFuzzyCompare(_viewCtrl.slerpStep(), 1.0f))
	{
		if (_primaryCamera->getMode() == Camera::CameraMode::Fly ||
			_primaryCamera->getMode() == Camera::CameraMode::FirstPerson)
		{
			QMatrix4x4 targetRotMat(targetRotation.toRotationMatrix());
			QVector3D targetViewDir = -targetRotMat.row(2).toVector3D().normalized();
			QVector3D targetUpDir = targetRotMat.row(1).toVector3D().normalized();
			QVector3D targetRightDir = targetRotMat.row(0).toVector3D().normalized();
			const float fovRad = qDegreesToRadians(_viewCtrl.FOV());
			const float sinHalfFov = std::max(std::sin(fovRad * 0.5f), 0.001f);
			const float shiftFactor = std::min(1.05f / sinHalfFov, 1.25f);
			const float targetDistance = shiftFactor * _viewCtrl.viewBoundingSphereDia();
			const QVector3D targetEye = _viewCtrl.boundingSphere().getCenter() - targetViewDir * targetDistance;
			_primaryCamera->setView(targetEye, targetViewDir, targetUpDir, targetRightDir);
			_primaryCamera->setYawPitchFromViewDir();
			_primaryCamera->updateFlyView();
		}
		else
		{
			_primaryCamera->setView(curPos, viewDir, upDir, rightDir);
		}

		_viewCtrl.syncPoseFromCamera(*_primaryCamera);
		_viewCtrl.setCurrentViewRange(_viewCtrl.viewRange());
		_viewCtrl.resetSlerpStep();
		_viewCtrl.setCustomViewAnimationActive(false);

		emit rotationsSet();
	}
}

void ViewportWidget::setRotations(float xRot, float yRot, float zRot)
{
	QQuaternion targetRotation = QQuaternion::fromEulerAngles(yRot, zRot, xRot); //Pitch, Yaw, Roll
	animateToRotation(targetRotation);
}

void ViewportWidget::setZoomAndPan(float zoom, QVector3D pan)
{
	// Every live caller (animateFitAll(), animateWindowZoom(),
	// animateCenterScreen()) is one of the per-frame animation-timer
	// callbacks already kept out of the interactive-GPU-PT path - see
	// animateViewChange()'s doc comment. This call used to silently undo
	// that by re-enabling it right back. (fitBoxToScreen()'s call site is
	// dead code - only referenced in a commented-out line - so irrelevant
	// here either way.)
	//
	// No explicit update() here - every live caller always reaches its own
	// trailing resizeGL() call later in the same tick, which already
	// repaints unconditionally regardless of PT armed state - see
	// animateViewChange()'s identical doc comment for why adding one here
	// too tripled per-tick repaints even with PT off entirely.
	_rtInteractionCtrl->notifyCameraAnimationTick();

	_viewCtrl.advanceSlerpStep();

	// Translation
	QVector3D curPos = pan * _viewCtrl.slerpFrac();
	_primaryCamera->move(curPos.x(), curPos.y(), curPos.z());

	// Set zoom
	float scaleStep = (_viewCtrl.currentViewRange() - zoom) * _viewCtrl.slerpFrac();
	_viewCtrl.setViewRange(_viewCtrl.viewRange() - scaleStep);

	if (qFuzzyCompare(_viewCtrl.slerpStep(), 1.0f))
	{
		// Set all defaults
		_viewCtrl.setCurrentTranslation(_primaryCamera->getPosition());
		_viewCtrl.setCurrentViewRange(_viewCtrl.viewRange());
		_viewCtrl.resetSlerpStep();

		emit zoomAndPanSet();
	}
}

void ViewportWidget::closeEvent(QCloseEvent* event)
{
	event->accept();
}

void ViewportWidget::showLights(bool showLights)
{
	_renderCtrl.setShowLights(showLights);
	update();
}


void ViewportWidget::applyEnabledLightList(const std::vector<GPULight>& enabledLights)
{
	makeCurrent();
	_renderCtrl.punctualLights()->setLights(enabledLights);
	syncPunctualLightUniforms(static_cast<int>(enabledLights.size()),
	                          !enabledLights.empty());

	// Per-light enable/disable checkbox toggles reach here (see
	// VisualizationEnvironmentPanel::onPunctualLightItemChanged()) - must be
	// notifyRayTracedSceneMutated(), not just notifyCameraInteracting()/
	// notifySceneContentMutated(): see useDefaultLights()/usePunctualLights()'s
	// doc comment in ViewportWidget.h for why a bare idle-timer restart alone
	// isn't enough to make the GPU (OptiX) session actually re-upload its
	// stale lights buffer.
	notifyRayTracedSceneMutated();
}


void ViewportWidget::setRenderingMode(const RenderingMode& renderingMode)
{
	_renderCtrl.setRenderingMode(renderingMode);
	_renderCtrl.fgShader()->bind();
	_renderCtrl.fgShader()->setUniformValue("renderingMode", static_cast<int>(_renderCtrl.renderingMode()));

	// Mark textures as dirty to ensure they are reloaded
	for (const SceneMeshRecord& meshRecord : _sceneRuntime.meshStore())
	{
		SceneMesh* mesh = meshRecord.mesh;
		if (!mesh)
			continue;
		mesh->markTexturesDirty();
		mesh->markUniformsDirty();
	}

	_renderCtrl.fgShader()->release();
	update();
	emit renderingModeChanged(static_cast<int>(_renderCtrl.renderingMode()));
}

// ---------------------------------------------------------------------------
// Ray-traced rendering mode
// ---------------------------------------------------------------------------

void ViewportWidget::loadRayTracingSettingsFromDisk()
{
	QSettings settings;

	// One-time migration from this app's older "pathtracing/*" QSettings key
	// prefix (pre-dating the Path Tracing -> Ray Tracing terminology rename)
	// to "raytracing/*" - without this, every value below would silently
	// fall back to its hardcoded default the first time a user upgrades,
	// discarding their previously saved samples/bounces/denoiser/engine
	// preferences even though the data is still sitting right there under
	// the old key name. Copies forward once (contains() on the NEW key
	// guards against re-copying on every subsequent load), then the reads
	// below proceed exactly as before using only the new keys - saveSettings()
	// (RtRenderDialog.cpp) already writes exclusively under the new prefix,
	// so this old data is never touched again after this first migration.
	static const char* const kMigratedKeys[] = {
		"maxSamples", "maxBounces", "denoiserEnabled", "fireflyClamp",
		"maxTransmissionBounces", "russianRouletteDepth", "maxShadowRayHits",
		"maxVolumeScatterBounces", "envImportanceSampling",
		"denoiserDevicePreference", "enginePreference"
	};
	for (const char* key : kMigratedKeys)
	{
		const QString newKey = QString("raytracing/") + key;
		const QString oldKey = QString("pathtracing/") + key;
		if (!settings.contains(newKey) && settings.contains(oldKey))
			settings.setValue(newKey, settings.value(oldKey));
	}

	_ptMaxSamples = settings.value("raytracing/maxSamples", _ptMaxSamples).toUInt();
	_ptMaxBounces = settings.value("raytracing/maxBounces", _ptMaxBounces).toInt();
	_ptDenoiserEnabled = settings.value("raytracing/denoiserEnabled", _ptDenoiserEnabled).toBool();
	_ptFireflyClampThreshold = settings.value("raytracing/fireflyClamp", _ptFireflyClampThreshold).toFloat();
	_ptMaxTransmissionBounces = settings.value("raytracing/maxTransmissionBounces", _ptMaxTransmissionBounces).toInt();
	_ptRussianRouletteStartDepth = settings.value("raytracing/russianRouletteDepth", _ptRussianRouletteStartDepth).toInt();
	_ptMaxVolumeScatterBounces = settings.value("raytracing/maxVolumeScatterBounces", _ptMaxVolumeScatterBounces).toInt();
	_ptMaxShadowRayHits = settings.value("raytracing/maxShadowRayHits", _ptMaxShadowRayHits).toInt();
	_ptEnvImportanceSamplingEnabled = settings.value("raytracing/envImportanceSampling", _ptEnvImportanceSamplingEnabled).toBool();
	_ptDenoiserDevicePreference = static_cast<DenoiserDevicePreference>(
		settings.value("raytracing/denoiserDevicePreference", static_cast<int>(_ptDenoiserDevicePreference)).toInt());
	_ptEnginePreference = static_cast<RtRayTracingEnginePreference>(
		settings.value("raytracing/enginePreference", static_cast<int>(_ptEnginePreference)).toInt());
}

void ViewportWidget::armRayTracedRenderingMode(bool startInteractiveSessionNow)
{
	// RtInteractionController::arm() reproduces this method's
	// original behavior exactly (see its own doc comment): a no-op if
	// already armed, the startInteractiveSessionNow=false early-out for
	// requestRayTracedRenderNow(), and otherwise starting the continuous
	// interactive accumulator immediately for GPU or falling through to the
	// idle-then-settle countdown for CPU/Embree.
	_rtInteractionCtrl->arm(startInteractiveSessionNow);
}

void ViewportWidget::warmUpInteractiveRayTracedGpuSession()
{
	if (!_rayTracedInteractiveActive || !_rtInteractiveRendererSnapshot)
		return; // startInteractiveRayTracedGpuSession() didn't actually start anything (e.g. OptiX unavailable) - nothing to warm up

	// First tick(): nothing in flight yet, so this submits the one and only
	// launch that pays the GAS/IAS build + pipeline/cold-cache warm-up cost -
	// see this method's header doc comment.
	_rtInteractiveRenderer.tick(_rtInteractiveRendererSnapshot->environment,
		_rtInteractiveRendererSnapshot->shadowsEnabled, _rtInteractiveRendererSnapshot->selfShadowsEnabled,
		_ptEnvImportanceSamplingEnabled);

	// Bounded busy-wait (mirrors RtInteractiveRenderer::drainSlot()'s own
	// pattern/bound) - reached once per slow-path rebuild (see this method's
	// header doc comment for all three callers), never from the per-paint
	// tick() call paintGL() makes, so a short block on the GUI thread exactly
	// when a rebuild happens doesn't reintroduce the per-frame stall this
	// whole redesign exists to avoid.
	constexpr int kMaxWarmUpWaitMs = 5000;
	constexpr int kPollIntervalMs = 1;
	int waitedMs = 0;
	while (_rtInteractiveRenderer.isFrameInFlight() && waitedMs < kMaxWarmUpWaitMs)
	{
		QThread::msleep(kPollIntervalMs);
		waitedMs += kPollIntervalMs;
	}
	if (waitedMs >= kMaxWarmUpWaitMs)
		qWarning() << "warmUpInteractiveRayTracedGpuSession: timed out waiting for the warm-up launch to "
			"complete - proceeding anyway (the first real drag frame may still show the one-time lag this "
			"warm-up exists to avoid).";

	// Second tick(): marks that now-completed warm-up launch ready (denoise +
	// publish, same as any other completed launch) and immediately submits
	// the next accumulation sample into the other slot - exactly what would
	// happen naturally across the next two paintGL() calls, just forced to
	// happen now instead of racing the user's first mouse-move. The next
	// paintGL() then finds a frame already waiting in pollCompletedFrame(),
	// no in-flight launch left to lag behind.
	_rtInteractiveRenderer.tick(_rtInteractiveRendererSnapshot->environment,
		_rtInteractiveRendererSnapshot->shadowsEnabled, _rtInteractiveRendererSnapshot->selfShadowsEnabled,
		_ptEnvImportanceSamplingEnabled);
}

void ViewportWidget::notifyRayTracedSceneMutated()
{
	++_rayTracedSceneRevision;
	_rtInteractionCtrl->notifySceneContentMutated();
	// The old resetRayTracedIdleTimer(false) path this used to funnel
	// through always called update() itself on this branch, unconditionally -
	// several callers (e.g. setCameraUpAxisZUp()) rely on that immediate
	// repaint-to-raster and don't trigger one on their own. Codex's audit
	// caught this: notifySceneContentMutated() only tears the PT session down,
	// it doesn't touch Qt's repaint scheduling (deliberately - the controller
	// owns PT policy, not rendering), so this caller-side call is load-bearing.
	update();
}

void ViewportWidget::notifyRayTracedAnimationMutated()
{
	++_rayTracedSceneRevision;
	// RtInteractionController::notifyContentAnimationTick() mirrors
	// this method's original control flow exactly: for GPU, attempt an
	// in-place scene-snapshot refresh against the SAME live interactive
	// accumulator first (via the startInteractiveSessionWithSceneRefresh
	// callback) and only fall back to the normal teardown+debounced-resume
	// transition if that attempt didn't actually leave a live session behind
	// (isInteractiveSessionLive callback) - see its own doc comment.
	_rtInteractionCtrl->notifyContentAnimationTick();
}

// Tears down whichever GPU PT session(s) are currently active/converging -
// shared by RtInteractionController's Recovering-entry teardown,
// hideEvent(), and disarmRayTracedRenderingMode(). These three used to each reimplement
// this teardown slightly differently, which is exactly how hideEvent() ended
// up leaving _rayTracedInteractiveActive true (see its own doc comment for
// that bug) while the OTHER two callers already cleared it - a single shared
// implementation makes that whole class of divergence structurally
// impossible going forward. Deliberately does NOT touch _rayTracedArmed,
// _rayTracedIdleTimer, or _rayTracedResumeWarmUpTimer - callers decide
// those independently based on their own context (disarming entirely vs.
// still armed but invalidated vs. temporarily hidden).
void ViewportWidget::teardownActiveRayTracedSessions()
{
	_rtSession.stop();
	_ptOptixSession.stop();
	stopRtInteractiveRenderer();
	_rtPresenter.invalidate();
	_rtInteractivePreviewCameraValid = false;
	_rayTracedInteractiveActive = false;
	if (_rayTracedRefreshTimer)
		_rayTracedRefreshTimer->stop();
}

// Forwards the resume timer's signal into RtInteractionController -
// see that class's own doc comment for the full state-machine rationale (in
// particular why the Recovering state's resume debounce is itself what
// protects a scripted animation from paying a wasted mid-animation rebuild,
// replacing the old "peek at whether an animation timer happens to still be
// active" guard here). Only the controller may call RtInteractiveRenderer::
// setCameraSettled()/setInteractiveBudget()/requestFullResolution(), and only
// it starts/stops the settle/resume timers - kept as a private method reached
// through the friend declaration in RtInteractionController.h.
void ViewportWidget::onRayTracedResumeWarmUpTimeout()
{
	_rtInteractionCtrl->onResumeTimerFired();
}

void ViewportWidget::disarmRayTracedRenderingMode()
{
	_rtInteractionCtrl->disarm();
	update(); // drop back to pure raster immediately
}

void ViewportWidget::onRayTracedIdleTimeout()
{
	if (!_rtInteractionCtrl->armed())
		return;
	_rtInteractionCtrl->onIdleTimerFired();
	update();
}

std::shared_ptr<const RtSceneSnapshot> ViewportWidget::buildRayTracedSnapshot(int width, int height,
	const RtEnvironment* reusedEnvironment)
{
	if (!_primaryCamera)
		return nullptr;

	std::vector<GPULight> lights = _renderCtrl.punctualLights()->getLights();
	// Honors the Default Lights toggle literally - no "|| lights.empty()"
	// safety net to avoid an all-black render when everything is disabled.
	// An earlier version had that fallback here (misleadingly named
	// addRasterDefaultLight despite this being the PT-only snapshot builder -
	// raster's own useDefaultLights GL uniform has no such fallback), which
	// meant explicitly turning Default Lights off only worked if at least
	// one punctual light was also enabled at the same time; disabling
	// everything silently re-lit the ray-traced scene anyway, contradicting
	// what the toggle visibly showed.
	if (_renderCtrl.useDefaultLights())
	{
		// Raster's built-in lightSource is not a KHR_lights_punctual point
		// light: it uses a positional direction per fragment
		// (normalize(lightSource.position - v_position)) but a CONSTANT
		// diffuse intensity (lightSource.diffuse), with no inverse-square
		// falloff. That shape is especially visible on KHR_materials_sheen:
		// the Charlie lobe is what makes cloth read as velvet instead of a
		// silky environment reflection. Encode this app/default light with
		// range < 0; the CPU/GPU PT evaluators treat that sentinel as
		// constant-intensity while keeping real glTF point/spot lights
		// physically attenuated.
		const QVector3D fallbackLightPos = effectiveWorldLightPosition();
		const QVector3D diffuseLight = _diffuseLight.toVector3D();

		GPULight keyLight{};
		keyLight.type = static_cast<int>(LightType::Point);
		keyLight.position = glm::vec3(fallbackLightPos.x(), fallbackLightPos.y(), fallbackLightPos.z());
		keyLight.color = glm::vec3(diffuseLight.x(), diffuseLight.y(), diffuseLight.z());
		keyLight.intensity = 1.0f;
		keyLight.range = -1.0f;
		keyLight.direction = glm::vec3(0.0f, 0.0f, -1.0f);
		keyLight.innerConeCos = 1.0f;
		keyLight.outerConeCos = 0.70710678f; // cos(45 deg) - unused anyway, type is Point not Spot
		keyLight.padding = glm::vec2(0.0f);
		lights.push_back(keyLight);
	}

	RtEnvironment environment;
	if (reusedEnvironment)
	{
		// Animation-driven PT updates need a fresh geometry/material snapshot,
		// but the HDR environment texels/IBL mips almost never change from
		// tick to tick. Reusing them avoids the synchronous cubemap readbacks
		// below on every animation frame.
		environment = *reusedEnvironment;
	}
	else
	{
		// captureEnvironmentCubemapCPU() does a synchronous GPU readback
		// (glGetTexImage) - needs this widget's context current, which isn't
		// guaranteed here since this function can run off the idle QTimer rather
		// than from within paintGL().
		makeCurrent();
		_renderCtrl.captureEnvironmentCubemapCPU(environment.faces, environment.faceSize);
		_renderCtrl.captureIrradianceCubemapCPU(environment.irradianceFaces, environment.irradianceFaceSize);
		{
			std::vector<SceneRenderController::PrefilterMipCPU> prefilterMips;
			if (_renderCtrl.capturePrefilterCubemapCPU(prefilterMips))
			{
				environment.prefilterMips.reserve(prefilterMips.size());
				for (SceneRenderController::PrefilterMipCPU& mip : prefilterMips)
				{
					RtEnvironment::PrefilterMip rtMip;
					rtMip.faceSize = mip.faceSize;
					for (int i = 0; i < 6; ++i)
						rtMip.faces[i] = std::move(mip.faces[i]);
					environment.prefilterMips.push_back(std::move(rtMip));
				}
			}
		}
		{
			std::vector<SceneRenderController::PrefilterMipCPU> sheenPrefilterMips;
			if (_renderCtrl.captureSheenPrefilterCubemapCPU(sheenPrefilterMips))
			{
				environment.sheenPrefilterMips.reserve(sheenPrefilterMips.size());
				for (SceneRenderController::PrefilterMipCPU& mip : sheenPrefilterMips)
				{
					RtEnvironment::PrefilterMip rtMip;
					rtMip.faceSize = mip.faceSize;
					for (int i = 0; i < 6; ++i)
						rtMip.faces[i] = std::move(mip.faces[i]);
					environment.sheenPrefilterMips.push_back(std::move(rtMip));
				}
			}
		}
	}

	environment.showBackground = _renderCtrl.skyBoxEnabled();
	environment.cameraUpAxisZUp = _viewCtrl.cameraUpAxisZUp();
	environment.skyBoxZRotationDegrees = _renderCtrl.skyBoxZRotation();
	environment.envMapExposure = _renderCtrl.envMapExposure();
	environment.skyBoxFOV = _renderCtrl.skyBoxFOV();
	const QColor topColor = _renderCtrl.bgTopColor();
	const QColor botColor = _renderCtrl.bgBotColor();

	// QColor::redF()/greenF()/blueF() are sRGB-encoded (display/UI space),
	// but ray_traced_present.frag runs the tracer's whole output through an
	// ACES tonemap + gamma-encode pass intended for linear HDR radiance -
	// feeding it an already gamma-encoded value double-brightens it (washes
	// a mid-gray toward white). Approximate sRGB->linear with pow(x, 2.2)
	// (close enough for a flat UI color; not worth the exact piecewise sRGB
	// curve here) so it survives that pipeline looking like what raster
	// actually shows.
	auto srgbToLinearApprox = [](float c) { return std::pow(std::max(c, 0.0f), 2.2f); };
	environment.fallbackTopColor = glm::vec3(
		srgbToLinearApprox(static_cast<float>(topColor.redF())),
		srgbToLinearApprox(static_cast<float>(topColor.greenF())),
		srgbToLinearApprox(static_cast<float>(topColor.blueF())));
	environment.fallbackBottomColor = glm::vec3(
		srgbToLinearApprox(static_cast<float>(botColor.redF())),
		srgbToLinearApprox(static_cast<float>(botColor.greenF())),
		srgbToLinearApprox(static_cast<float>(botColor.blueF())));
	environment.fallbackGradientStyle = _renderCtrl.gradientStyle();

	// GroundMode::InfinitePlane is a UI-only concept (its own radio button,
	// mutually exclusive with None/Floor/Grid) - RtSceneBuilder itself only
	// knows "Floor mode" + "shadow catcher on/off" (exactly today's
	// Floor+checkbox combination), so it's translated here rather than
	// threaded through the PT engine as a distinct case. Raster has no
	// equivalent for this mode at all (see ViewportWidget's ground-drawing
	// if/else-if chain, which simply draws nothing when groundMode is
	// neither Floor nor Grid), matching the fact that shadow-catcher
	// substitution only ever applies to the ray-traced render.
	const bool infinitePlaneMode = _renderCtrl.groundMode() == GroundMode::InfinitePlane;

	RtFloorParams floorParams;
	floorParams.floorMesh        = _floorPlane;
	floorParams.groundMode       = infinitePlaneMode ? GroundMode::Floor : _renderCtrl.groundMode();
	floorParams.sceneBoundingBox = _viewCtrl.boundingBox();
	floorParams.center           = _floorCenter;
	floorParams.planeLevel       = _floorPlaneZ;
	floorParams.cameraUpAxisZUp  = _viewCtrl.cameraUpAxisZUp();
	floorParams.rasterFloorExtent = CoordinateSystemHelper::groundPlaneExtent(_floorSize, _floorSizeFactor, _renderCtrl.groundMode());
	floorParams.texRepeatS       = _renderCtrl.floorTexRepeatS();
	floorParams.texRepeatT       = _renderCtrl.floorTexRepeatT();
	floorParams.shadowCatcherEnabled = infinitePlaneMode;
	floorParams.shadowCatcherDarkness = _renderCtrl.shadowCatcherDarkness();
	floorParams.shadowCatcherBaseColor = _renderCtrl.shadowCatcherBaseColor();
	floorParams.shadowCatcherMetalness = _renderCtrl.shadowCatcherMetalness();
	floorParams.shadowCatcherRoughness = _renderCtrl.shadowCatcherRoughness();

	// Recomputed from the OUTPUT resolution rather than reusing the
	// camera's own configured aspect - for the interactive session these
	// are already numerically identical (the camera's aspect is kept in
	// sync with the viewport's own on-screen shape elsewhere), but for an
	// offline export at a genuinely different aspect ratio, this is what
	// makes the render frame correctly to the requested WxH instead of
	// stretching/squishing the same framing the live viewport uses.
	const float aspectRatio = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
	auto snapshot = RtSceneBuilder::build(
		_sceneRuntime, *_primaryCamera, aspectRatio,
		lights, _rayTracedSceneRevision, &environment, &floorParams,
		_renderCtrl.shadowsEnabled(), _renderCtrl.selfShadowsEnabled());

	// Cache which side of the floor's plane the camera was on for THIS
	// build - see _rtLastBuildCameraAboveFloor's own doc comment for why
	// this needs tracking separately from build()'s own (correct, but only
	// evaluated HERE) camera-vs-floor-plane decision.
	_rtLastBuildCameraAboveFloor = isCameraAboveFloorPlane();

	// KHR_materials_transmission without KHR_materials_volume ("thin-walled")
	// passes rays through completely undeviated per spec - under
	// orthographic projection every pixel's camera ray shares the exact
	// same direction, so every point on a thin-walled transmissive surface
	// ends up sampling the identical environment-map direction: a genuine
	// mathematical degenerate case (flat/uniform result), not a rendering
	// bug - see the GlassBrokenWindow investigation. Detected here (once
	// per snapshot build, not per-pixel) so RtRenderDialog can surface it
	// via rayTracingOrthoThinWallWarningActive() instead of a user
	// assuming their glass is broken.
	_ptOrthoThinWallWarningActive = false;
	if (snapshot && snapshot->camera.orthographic)
	{
		for (const RtMaterial& mat : snapshot->materials)
		{
			if (mat.transmission > 0.001f && !mat.hasVolume)
			{
				_ptOrthoThinWallWarningActive = true;
				break;
			}
		}
	}

	return snapshot;
}

void ViewportWidget::startRayTracedSession()
{
	if (!_renderCtrl.isOpenGLInitialized() || !_primaryCamera)
		return;

	// Re-derive the current device-pixel size fresh rather than trusting
	// whatever resizeGL() last recorded - minimizing/restoring the window (or
	// switching focus away and back) can leave a stale/degenerate size cached
	// if Qt fires resizeGL() with a transient 0x0 during that sequence with no
	// further resize once the window is genuinely back to its real size. This
	// makes every new session self-healing instead of permanently stuck.
	const qreal dpr = devicePixelRatioF();
	const int fbWidth  = static_cast<int>(width()  * dpr);
	const int fbHeight = static_cast<int>(height() * dpr);
	if (fbWidth <= 0 || fbHeight <= 0)
		return; // genuinely not visible right now (e.g. still minimized) - nothing to render into

	if (effectiveRayTracingEnginePreference() == RtRayTracingEnginePreference::GPU)
	{
		startOptixTestRayTracedSession(fbWidth, fbHeight);
		return;
	}

	_ptOptixSession.stop(); // switching to the CPU engine - don't leave a GPU worker thread running behind it
	stopRtInteractiveRenderer();

	auto snapshot = buildRayTracedSnapshot(fbWidth, fbHeight);
	if (!snapshot)
		return;

	_rtSession.setResolution(fbWidth, fbHeight);
	_rtSession.setMaxSamples(_ptMaxSamples);
	_rtSession.setDenoiserEnabled(_ptDenoiserEnabled);
	// Forwarded as-is, including OptiX: RtDenoiser owns its own standalone
	// OptixDeviceContext (see RtDenoiser.cpp's Impl::optixContext doc
	// comment), so the native OptiX denoiser works regardless of which
	// render engine (this CPU/Embree session or the GPU/OptiX one) actually
	// produced the frame being denoised.
	_rtSession.setDenoiserDevicePreference(_ptDenoiserDevicePreference);
	{
		CpuPathTracer::Settings settings = _rtSession.tracerSettings();
		settings.maxBounces                        = _ptMaxBounces;
		settings.fireflyClampThreshold              = _ptFireflyClampThreshold;
		settings.maxTransmissionBounces             = _ptMaxTransmissionBounces;
		settings.russianRouletteStartDepth          = _ptRussianRouletteStartDepth;
		settings.enableEnvironmentImportanceSampling = _ptEnvImportanceSamplingEnabled;
		settings.maxShadowRayHits                   = _ptMaxShadowRayHits;
		settings.maxVolumeScatterBounces             = _ptMaxVolumeScatterBounces;
		_rtSession.setTracerSettings(settings);
	}
	if (!_preservePtPresenterOnNextStart)
		_rtPresenter.invalidate(); // suppress the (now stale) previous frame until the first new pass publishes
	_rtSession.start(snapshot);
	_preservePtPresenterOnNextStart = false;
	_ptSessionElapsedTimer.start(); // see rayTracingElapsedMs()'s doc comment

	if (_rayTracedRefreshTimer)
		_rayTracedRefreshTimer->start();
}

void ViewportWidget::startOptixTestRayTracedSession(int fbWidth, int fbHeight)
{
	// This is the manual/dialog-configured "settled" session - RtRenderDialog's
	// Render button reaches here via requestRayTracedRenderNow(), which passes
	// armRayTracedRenderingMode() startInteractiveSessionNow=false specifically
	// so arming doesn't ALSO start/warm up the interactive accumulator just to
	// have it torn back down a moment later (see that method's own doc
	// comment) - wasted GPU work, since this call wants the REAL dialog-
	// configured samples/bounces displayed, not the interactive accumulator's
	// fixed, deliberately-capped budget. The teardown below is still needed
	// regardless: if PT was already armed with a live interactive session
	// (e.g. the user was mid-drag, then opened the dialog and pressed Render),
	// armRayTracedRenderingMode()'s own no-op-when-already-armed guard means
	// that session is still running here. Leaving it running would keep
	// _rayTracedInteractiveActive true, which would make paintGL()'s
	// interactive-pull block keep overwriting the presenter with
	// _rtInteractiveRenderer's output every paint AND make
	// onRayTracedRefreshTimer() skip _ptOptixSession's frames entirely (its
	// own "only the settled session may publish" gate, keyed off this same
	// flag) - _ptOptixSession would run with the user's actual dialog values,
	// but none of that would ever reach the screen. Mirrors this function's sibling
	// CPU branch in startRayTracedSession(), which already does the
	// equivalent _ptOptixSession.stop()/stopRtInteractiveRenderer() teardown
	// when switching engines.
	stopRtInteractiveRenderer();
	_rayTracedInteractiveActive = false;

	_rtSession.stop(); // switching to the GPU engine - don't leave a CPU worker thread running behind it
	if (!_preservePtPresenterOnNextStart)
		_rtPresenter.invalidate(); // suppress the (now stale) previous frame until the first chunk publishes

	if (!_ptOptixSession.isAvailable())
	{
		qWarning() << "startOptixTestRayTracedSession: OptiX unavailable on this machine "
			"(see the RtOptixContext/RtOptixSceneTracer log above) - nothing to display.";
		_preservePtPresenterOnNextStart = false;
		update();
		return;
	}

	auto snapshot = buildRayTracedSnapshot(fbWidth, fbHeight);
	if (!snapshot)
	{
		_preservePtPresenterOnNextStart = false;
		update();
		return;
	}

	// An empty scene (e.g. every mesh just got deleted) is a valid, benign
	// state, not a failure - RtOptixSceneTracer::buildScene() itself treats
	// "no valid instances" as un-renderable and returns false, which would
	// otherwise cascade into a THIRD stacked qWarning() here on top of its
	// own and RtOptixRayTracingSession::start()'s (all three tracing back to
	// this exact same root cause). Bailing out here first, silently, mirrors
	// the !snapshot case immediately above and avoids ever reaching either
	// of those warnings.
	if (snapshot->instances.empty())
	{
		_preservePtPresenterOnNextStart = false;
		update();
		return;
	}

	// RtOptixRayTracingSession itself only rebuilds the GPU acceleration
	// structure when the scene actually changed (see its start() doc
	// comment) - camera-only movement reuses the existing GAS/IAS and just
	// re-renders through the new camera, matching RtEmbreeScene's own
	// rebuild-on-revision-change contract.
	_ptOptixSession.setResolution(fbWidth, fbHeight);
	_ptOptixSession.setMaxSamples(_ptMaxSamples);
	// Small chunk size (the class's own default - see setSamplesPerChunk()'s
	// doc comment) for genuine per-chunk progress-bar/preview updates. This
	// used to be forced to _ptMaxSamples (one giant chunk covering the whole
	// budget) back when _ptOptixSession also doubled as the interactive-
	// settle handoff target - minimizing chunk count avoided a late "pop"
	// once the final chunk's denoise landed, well after the camera had
	// already visually stopped (see git history for the measurements). That
	// handoff no longer exists (see RtInteractiveRenderer/Phase 4's continuous
	// accumulator, which replaced it) - this session is now reached ONLY via
	// RtRenderDialog's Render button, where the one-giant-chunk behavior
	// instead meant currentSampleCount() stayed 0 the whole render and jumped
	// straight to the target at the very end, defeating the dialog's progress
	// bar entirely.
	_ptOptixSession.setMaxBounces(static_cast<uint32_t>(std::max(_ptMaxBounces, 1)));
	_ptOptixSession.setEnvironmentImportanceSamplingEnabled(_ptEnvImportanceSamplingEnabled);
	_ptOptixSession.setMaxTransmissionBounces(static_cast<uint32_t>(std::max(_ptMaxTransmissionBounces, 1)));
	_ptOptixSession.setFireflyClampThreshold(_ptFireflyClampThreshold);
	_ptOptixSession.setRussianRouletteStartDepth(static_cast<uint32_t>(std::max(_ptRussianRouletteStartDepth, 1)));
	_ptOptixSession.setMaxVolumeScatterBounces(static_cast<uint32_t>(std::max(_ptMaxVolumeScatterBounces, 1)));
	_ptOptixSession.setDenoiserEnabled(_ptDenoiserEnabled);
	_ptOptixSession.setDenoiserDevicePreference(_ptDenoiserDevicePreference);
	if (!_ptOptixSession.start(snapshot))
	{
		qWarning() << "startOptixTestRayTracedSession: RtOptixRayTracingSession::start() failed.";
		_preservePtPresenterOnNextStart = false;
		update();
		return;
	}
	_preservePtPresenterOnNextStart = false;
	_ptSessionElapsedTimer.start(); // see rayTracingElapsedMs()'s doc comment

	if (_rayTracedRefreshTimer)
		_rayTracedRefreshTimer->start();
}

void ViewportWidget::stopRtInteractiveRenderer()
{
	_rtInteractiveRenderer.releaseResources();
	_rtInteractiveRendererSnapshot.reset();
	_lastConsumedRtInteractiveRendererGeneration = 0;
}

// GPU/OptiX-only reduced-quality trace kicked off while the camera is
// actively moving - see RtInteractionController::
// notifyCameraInteracting()'s doc comment for when this is called instead of
// the raster-fallback path. Targets
// _rtInteractiveRenderer (a same-frame, non-blocking-submission renderer -
// see that class's own doc comment), never _ptOptixSession, which is now
// used exclusively for the settled/full-quality session.
void ViewportWidget::startInteractiveRayTracedGpuSession(bool forceSceneRefresh)
{
	if (!_renderCtrl.isOpenGLInitialized() || !_primaryCamera || !_ptOptixSession.isAvailable())
		return;

	const qreal dpr = devicePixelRatioF();
	const int fbWidth  = static_cast<int>(width()  * dpr);
	const int fbHeight = static_cast<int>(height() * dpr);
	if (fbWidth <= 0 || fbHeight <= 0)
		return; // genuinely not visible right now (e.g. still minimized)

	if (_rayTracedInteractiveActive &&
		fbWidth == _rtInteractiveRenderer.width() && fbHeight == _rtInteractiveRenderer.height())
	{
		const float aspectRatio = fbHeight > 0 ? static_cast<float>(fbWidth) / static_cast<float>(fbHeight) : 1.0f;
		const RtCamera camera = RtSceneBuilder::buildCamera(*_primaryCamera, aspectRatio);

		// RtSceneBuilder::build() decides whether to include the real,
		// finite floor MESH (addFloorInstance()) based on which side of its
		// plane the camera is on - but that decision is only re-evaluated
		// when build() actually runs, and the fast path below deliberately
		// never calls it (that's the whole point of the fast path). Without
		// this check, orbiting the camera below the floor mid-drag would
		// never hide the mesh - build() ran once, before the crossing, and
		// the fast path would just keep reusing that stale snapshot
		// forever.
		//
		// Only relevant for plain GroundMode::Floor now (see
		// RtSceneBuilder::build()'s own doc comment on this same gate for
		// the full write-up): Infinite Plane/Shadow Catcher mode's ground
		// is a purely analytic per-ray plane test (no BVH geometry), which
		// already self-hides from a camera below it every frame for free -
		// forcing a rebuild here for that mode too used to cause a visible
		// jerk every time the camera crossed the plane's height, which
		// happens constantly when orbiting around a subject sitting right
		// at that height.
		const bool floorCrossingRelevant = _renderCtrl.groundMode() == GroundMode::Floor;
		const bool floorSideUnchanged = !floorCrossingRelevant || isCameraAboveFloorPlane() == _rtLastBuildCameraAboveFloor;
		if (!floorSideUnchanged)
		{
			// The rebuild path below is gated on _rayTracedSceneRevision
			// (RtInteractiveRenderer::applySceneSnapshot() only pays for a
			// real GAS/IAS rebuild when snapshot->revisionId actually
			// changes) - without bumping it here, buildRayTracedSnapshot()
			// would correctly add/drop the floor mesh in the CPU-side
			// RtSceneSnapshot, but the GPU scene actually raytraced against
			// would silently keep the OLD mesh list, same as any other
			// mutation that needs this bump (see notifyRayTracedSceneMutated()'s
			// identical increment).
			++_rayTracedSceneRevision;
		}

		if (!forceSceneRefresh && floorSideUnchanged)
		{
			// Fast path: already at this resolution - just hand over the
			// current camera pose (cheap: no snapshot rebuild, no resize, no
			// GPU work happens here - tick() in paintGL() is what actually
			// submits).
			_rtInteractiveRenderer.updateCamera(camera);
			_rayTracedInteractiveActive = true;
			if (_rayTracedRefreshTimer && !_rayTracedRefreshTimer->isActive())
				_rayTracedRefreshTimer->start();
			return;
		}

		// Animation-driven scene revision (forceSceneRefresh) OR a floor
		// plane crossing (floorSideUnchanged false) on an already-live
		// interactive session: rebuild/queue the NEW snapshot against the
		// same renderer instance instead of tearing it down and starting
		// over. RtSceneBuilder::build(), reached via buildRayTracedSnapshot()
		// below, is what actually re-evaluates whether the floor should be
		// included for the crossing case.
		const RtEnvironment* reusedEnvironment =
			_rtInteractiveRendererSnapshot ? &_rtInteractiveRendererSnapshot->environment : nullptr;
		auto snapshot = buildRayTracedSnapshot(fbWidth, fbHeight, reusedEnvironment);
		if (!snapshot)
		{
			// _rayTracedInteractiveActive was already true on entry to this
			// branch (that's this branch's own condition) - leaving it true
			// on failure would make callers like notifyRayTracedAnimationMutated()
			// wrongly believe the refresh succeeded and skip their raster-
			// fallback path, silently freezing the viewport on a stale
			// pre-failure frame with no retry. Clear it so failure is visible
			// and the normal idle-timer/fallback machinery takes back over.
			_rayTracedInteractiveActive = false;
			return;
		}
		if (!_rtInteractiveRenderer.ensureSceneResources(snapshot))
		{
			_rayTracedInteractiveActive = false;
			return;
		}
		_rtInteractiveRendererSnapshot = snapshot;
		_rtInteractiveRenderer.updateCamera(camera);
		_rayTracedInteractiveActive = true;
		if (_rayTracedRefreshTimer && !_rayTracedRefreshTimer->isActive())
			_rayTracedRefreshTimer->start();
		if (_rtInteractiveRendererSnapshot)
		{
			_rtInteractiveRenderer.tick(_rtInteractiveRendererSnapshot->environment,
				_rtInteractiveRendererSnapshot->shadowsEnabled, _rtInteractiveRendererSnapshot->selfShadowsEnabled,
				_ptEnvImportanceSamplingEnabled);
		}
		return;
	}

	// Slow path: first tick of a new interactive burst (fresh arm, or resuming
	// after ANY teardown - a scene mutation, a scripted view animation, a
	// real resize, hiding/showing this widget's MDI document, ...) or a
	// mid-drag resolution change - real snapshot (geometry/environment/
	// lights) and GAS/IAS rebuild unavoidable here. Ordinary interaction-
	// driven restarts stay throttled since unlike the fast path above, this is
	// genuinely expensive. Forced scene-refresh callers (animation playback)
	// deliberately BYPASS that throttle: showing a stale PT snapshot while the
	// mode still claims PT is active is worse than paying the rebuild cost,
	// and pointer/material animations in particular have no geometric motion to
	// mask a stale frame.
	if (!forceSceneRefresh)
	{
		const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
		if (nowMs - _lastInteractiveGpuRestartMs < kInteractiveGpuRestartMinIntervalMs)
			return;
		_lastInteractiveGpuRestartMs = nowMs;
	}

	const RtEnvironment* reusedEnvironment =
		(forceSceneRefresh && _rtInteractiveRendererSnapshot) ? &_rtInteractiveRendererSnapshot->environment : nullptr;
	auto snapshot = buildRayTracedSnapshot(fbWidth, fbHeight, reusedEnvironment);
	if (!snapshot)
	{
		// Reached with _rayTracedInteractiveActive possibly still true from
		// before (e.g. a mid-animation resolution change took this slow
		// path) - see the fast-path branch above for why leaving it true on
		// failure is wrong.
		_rayTracedInteractiveActive = false;
		return;
	}

	if (!_rtInteractiveRenderer.ensureSceneResources(snapshot))
	{
		_rayTracedInteractiveActive = false;
		return;
	}
	_rtInteractiveRenderer.resize(fbWidth, fbHeight);
	// setInteractiveBudget() is deliberately NOT called here anymore -
	// RtInteractionController is the sole owner of that call (see
	// its own class doc comment for the invariant) and always sets it
	// immediately before invoking whichever callback reaches this function,
	// EXCEPT notifyContentAnimationTick(), which deliberately leaves
	// whatever budget/settle configuration was already in effect (Interacting
	// or Settled) unchanged. This function used to unconditionally reset
	// resolutionAdaptiveEnabled back to true on every rebuild here, which
	// silently undid that invariant whenever a content animation's forced
	// scene-refresh happened to land on this slow (real rebuild) path while
	// the controller was Settled - resolution-adaptive scaling would kick
	// back in and the one-shot settle denoise would never re-fire, even
	// though the camera itself never moved. resize()/ensureSceneResources()
	// above don't touch _samplesPerLaunch/_maxAccumulatedSamples/_maxBounces/
	// _targetFrameTimeMs/_resolutionAdaptiveEnabled at all (confirmed in
	// RtInteractiveRenderer::applyInternalResolution()/resize()), so whatever
	// the controller last configured simply persists correctly through this
	// rebuild without needing to be re-asserted here. maxBounces/
	// maxTransmissionBounces/maxVolumeScatterBounces used to be separate,
	// lower, hardcoded interactive-only constants (kInteractivePtMaxBounces
	// etc.) rather than the user's actual RtRenderDialog settings - a
	// real, confirmed discrepancy (interactive silently capped bounces at 4
	// regardless of what the dialog was set to) that produces a systematic
	// BIAS, not noise, in scenes with meaningful inter-reflection between
	// nearby objects (e.g. a dense grid of reflective spheres) - truncating
	// those light paths a couple of bounces early is a real,
	// no-amount-of-samples-fixes-it energy loss, not something the
	// accumulator/denoiser could ever converge away. maxBounces itself is
	// still owned by the controller (via setInteractiveBudget()); the
	// transmission/volume-scatter/firefly/Russian-roulette settings below
	// aren't part of that invariant and are refreshed unconditionally here.
	_rtInteractiveRenderer.setMaxTransmissionBounces(static_cast<uint32_t>(std::max(_ptMaxTransmissionBounces, 1)));
	_rtInteractiveRenderer.setFireflyClampThreshold(_ptFireflyClampThreshold);
	_rtInteractiveRenderer.setRussianRouletteStartDepth(static_cast<uint32_t>(std::max(_ptRussianRouletteStartDepth, 1)));
	_rtInteractiveRenderer.setMaxVolumeScatterBounces(static_cast<uint32_t>(std::max(_ptMaxVolumeScatterBounces, 1)));
	// Same PT-dialog-backed denoiser settings the settled/manual sessions use
	// (_rtSession/_ptOptixSession) - see RtInteractiveRenderer::
	// setDenoiserEnabled()'s doc comment for why this is its own RtDenoiser
	// instance rather than a shared one.
	_rtInteractiveRenderer.setDenoiserEnabled(_ptDenoiserEnabled);
	_rtInteractiveRenderer.setDenoiserDevicePreference(_ptDenoiserDevicePreference);

	// Retained so paintGL()'s tick() call can keep supplying
	// environment/shadow-setting parameters without rebuilding - see
	// _rtInteractiveRendererSnapshot's own doc comment.
	_rtInteractiveRendererSnapshot = snapshot;
	_lastConsumedRtInteractiveRendererGeneration = 0;

	const float aspectRatio = fbHeight > 0 ? static_cast<float>(fbWidth) / static_cast<float>(fbHeight) : 1.0f;
	const RtCamera camera = RtSceneBuilder::buildCamera(*_primaryCamera, aspectRatio);
	_rtInteractiveRenderer.updateCamera(camera);

	_rayTracedInteractiveActive = true;
	_ptSessionElapsedTimer.start();
	if (_rayTracedRefreshTimer && !_rayTracedRefreshTimer->isActive())
		_rayTracedRefreshTimer->start();

	// User-driven camera interaction still pays the one-time warm-up cost
	// synchronously to avoid the stale-pose snap the original design had at
	// drag start. Animation-driven forced scene refreshes intentionally skip
	// this: doing rebuild + warm-up directly on every animation tick starves
	// playback and makes the clip appear stuck. Instead, submit exactly one
	// fresh asynchronous launch now and let the normal non-blocking
	// tick()/publish loop carry it the rest of the way.
	if (!forceSceneRefresh)
		warmUpInteractiveRayTracedGpuSession();
	else if (_rtInteractiveRendererSnapshot)
		_rtInteractiveRenderer.tick(_rtInteractiveRendererSnapshot->environment,
			_rtInteractiveRendererSnapshot->shadowsEnabled, _rtInteractiveRendererSnapshot->selfShadowsEnabled,
			_ptEnvImportanceSamplingEnabled);
}

bool ViewportWidget::renderRayTracedOffline(int width, int height,
	const std::function<void(uint32_t currentSample, uint32_t maxSamples)>& onProgress,
	std::vector<glm::vec3>& outLinearRgb, bool* outCancelled)
{
	if (outCancelled)
		*outCancelled = false;
	_ptOfflineCancelRequested.store(false, std::memory_order_release); // see cancelRayTracedOfflineRender()'s doc comment

	if (width <= 0 || height <= 0)
		return false;

	// Stop whichever interactive session might currently be running - same
	// "never run two PT backends at once" discipline resetRayTracedIdleTimer()/
	// startRayTracedSession()/startOptixTestRayTracedSession() already
	// enforce elsewhere. Without this, an offline export triggered while the
	// interactive GPU session is still actively rendering would have its
	// background worker thread issuing CUDA/OptiX calls on the same device/
	// context concurrently with this call's own (on the GPU engine - see
	// renderRayTracedOfflineGpu()) - genuinely concurrent, unsynchronized
	// CUDA calls from two different host threads, on top of the interactive
	// session's already-built GAS/IAS/textures needlessly doubling peak VRAM
	// alongside this call's own fresh, independent scene build for the whole
	// export duration. Both stopped unconditionally (matching the existing
	// pattern's own style) rather than only whichever engine is about to be
	// used, since a stale worker from a PREVIOUS engine switch could still be
	// running too.
	_rtSession.stop();
	_ptOptixSession.stop();
	stopRtInteractiveRenderer(); // same reasoning - drains/releases the interactive PT renderer too, if it was the thing running
	// Unlike every OTHER stopRtInteractiveRenderer() call site in this file
	// (see e.g. disarmRayTracedRenderingMode()), this one previously left
	// _rayTracedInteractiveActive stuck true if the interactive GPU session
	// had been the thing running - rayTracingProgress()'s "if (gpu &&
	// _rayTracedInteractiveActive)" branch then kept reading the just-
	// stopped _rtInteractiveRenderer (currentSampleCount() reset to 0, but
	// maxSampleCount() still its configured cap), so outRunning read true
	// (0 < cap) even though nothing was actually rendering - leaving
	// RtRenderDialog's Render/Export buttons stuck disabled and Stop
	// stuck enabled after this offline export finished, until the user
	// force-stopped (which happens to go through a call site that DOES
	// reset this flag).
	_rayTracedInteractiveActive = false;

	// See rayTracingElapsedMs()'s doc comment - (re)started at the single
	// place a render actually begins, same as startRayTracedSession()/
	// startOptixTestRayTracedSession()'s identical calls. Without this, an
	// offline export's elapsed-time display (RtRenderDialog::
	// onExportClicked()'s progress callback) would show whatever stale
	// value was left over from the last INTERACTIVE session instead of the
	// export's own actual duration.
	_ptSessionElapsedTimer.start();

	auto snapshot = buildRayTracedSnapshot(width, height);
	if (!snapshot)
		return false;

	if (effectiveRayTracingEnginePreference() == RtRayTracingEnginePreference::GPU)
		return renderRayTracedOfflineGpu(width, height, *snapshot, onProgress, outLinearRgb, outCancelled);

	// A fresh, independent BVH/environment-sampler/accumulator - entirely
	// separate from _rtSession/_embreeScene (whatever the interactive
	// viewport is doing, if anything, keeps running completely undisturbed
	// by this call).
	RtEmbreeScene embreeScene;
	embreeScene.build(snapshot);

	RtEnvironmentSampler envSampler;
	envSampler.build(snapshot->environment);

	CpuPathTracer tracer;
	CpuPathTracer::Settings settings;
	settings.maxBounces                         = _ptMaxBounces;
	settings.fireflyClampThreshold               = _ptFireflyClampThreshold;
	settings.maxTransmissionBounces              = _ptMaxTransmissionBounces;
	settings.russianRouletteStartDepth           = _ptRussianRouletteStartDepth;
	settings.enableEnvironmentImportanceSampling = _ptEnvImportanceSamplingEnabled;
	settings.maxShadowRayHits                    = _ptMaxShadowRayHits;
	settings.maxVolumeScatterBounces              = _ptMaxVolumeScatterBounces;
	tracer.setSettings(settings);

	RtFrameAccumulator accumulator;
	accumulator.resize(width, height);
	accumulator.reset();

	for (uint32_t sample = 0; sample < _ptMaxSamples; ++sample)
	{
		if (_ptOfflineCancelRequested.load(std::memory_order_acquire))
		{
			if (outCancelled) *outCancelled = true;
			return false;
		}

		std::vector<glm::vec3> passResult;
		std::vector<float> hitMask;
		std::vector<glm::vec3> albedoResult, normalResult;
		tracer.renderPass(embreeScene, *snapshot, envSampler, width, height, sample, passResult,
			&_ptOfflineCancelRequested, &hitMask, &albedoResult, &normalResult);
		if (_ptOfflineCancelRequested.load(std::memory_order_acquire))
		{
			// passResult may only be partially filled - see renderPass()'s
			// own cancelFlag doc comment - must be discarded, not accumulated.
			if (outCancelled) *outCancelled = true;
			return false;
		}
		accumulator.accumulate(passResult, &hitMask, &albedoResult, &normalResult);

		if (onProgress)
			onProgress(sample + 1, _ptMaxSamples);
	}

	std::vector<glm::vec3> resolved = accumulator.resolve();
	outLinearRgb = resolved;

	if (_ptDenoiserEnabled)
	{
		// Forwarded as-is (see startRayTracedSession()'s identical comment) -
		// the native OptiX denoiser works regardless of which engine rendered
		// this frame, since RtDenoiser owns its own standalone
		// OptixDeviceContext rather than reusing the path tracer's.
		RtDenoiser denoiser(_ptDenoiserDevicePreference);
		std::vector<glm::vec3> denoised;
		const std::vector<glm::vec3> albedo = accumulator.resolveAlbedo();
		const std::vector<glm::vec3> normal = accumulator.resolveNormal();
		denoiser.denoise(resolved, width, height, denoised, _ptMaxSamples, &albedo, &normal);

		// Restore the raw (undenoised) value for pixels whose primary ray
		// never hit geometry - matches RtRayTracingSession::publishLatest()'s
		// exact same reasoning (OIDN has no guide data to work from for a
		// pure environment-miss pixel, so left to its own devices it
		// over-smooths sharp background/skybox detail into a blurred
		// prefilter-map look).
		const std::vector<float>& hitCounts = accumulator.hitCounts();
		if (hitCounts.size() == denoised.size())
		{
			for (size_t i = 0; i < denoised.size(); ++i)
				if (hitCounts[i] <= 0.0f)
					denoised[i] = resolved[i];
		}

		outLinearRgb = std::move(denoised);
	}

	return true;
}

bool ViewportWidget::renderRayTracedOfflineGpu(int width, int height, const RtSceneSnapshot& snapshot,
	const std::function<void(uint32_t currentSample, uint32_t maxSamples)>& onProgress,
	std::vector<glm::vec3>& outLinearRgb, bool* outCancelled)
{
	// A fresh, independent tracer/GAS-IAS build - entirely separate from
	// _ptOptixSession (whatever the interactive GPU session, if any, keeps
	// running completely undisturbed by this call) - mirrors the CPU path's
	// identical fresh-RtEmbreeScene rationale above.
	RtOptixSceneTracer tracer;
	if (!tracer.isAvailable())
		return false;
	// An empty scene is a valid, benign state, not a failure - see
	// startOptixTestRayTracedSession()'s identical check for the full
	// write-up (this call site doesn't add its own qWarning() on top, but
	// skipping the call entirely still avoids buildScene()'s own).
	if (snapshot.instances.empty())
		return false;
	if (!tracer.buildScene(snapshot))
		return false;

	const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
	const uint32_t maxSamples = std::max<uint32_t>(_ptMaxSamples, 1);

	// Running means in double precision, accumulated across chunks - same
	// numerically-steadier-than-fp32 rationale as RtOptixRayTracingSession::
	// workerLoop(), which this loop otherwise mirrors exactly (just as a
	// plain blocking loop instead of a background worker - see this
	// function's own doc comment in ViewportWidget.h).
	std::vector<glm::dvec3> runningMean(pixelCount, glm::dvec3(0.0));
	std::vector<glm::dvec3> runningMeanAlbedo(pixelCount, glm::dvec3(0.0));
	std::vector<glm::dvec3> runningMeanNormal(pixelCount, glm::dvec3(0.0));
	std::vector<double>     runningMeanAlpha(pixelCount, 0.0);
	uint32_t sampleCount = 0;

	// Chunk size of 1 keeps onProgress's documented "once per completed
	// sample" contract identical to the CPU path above, rather than only
	// updating once per (potentially large) GPU launch chunk.
	while (sampleCount < maxSamples)
	{
		if (_ptOfflineCancelRequested.load(std::memory_order_acquire))
		{
			if (outCancelled) *outCancelled = true;
			return false;
		}

		std::vector<glm::vec3> chunkFrame, chunkAlbedo, chunkNormal;
		std::vector<float> chunkAlpha;
		if (!tracer.renderScene(snapshot.camera, snapshot.environment, width, height, 1, sampleCount,
			static_cast<unsigned int>(std::max(_ptMaxBounces, 1)),
			snapshot.shadowsEnabled, snapshot.selfShadowsEnabled, _ptEnvImportanceSamplingEnabled,
			static_cast<unsigned int>(std::max(_ptMaxTransmissionBounces, 1)), _ptFireflyClampThreshold,
			static_cast<unsigned int>(std::max(_ptRussianRouletteStartDepth, 1)),
			static_cast<unsigned int>(std::max(_ptMaxVolumeScatterBounces, 1)),
			chunkFrame, chunkAlbedo, chunkNormal, chunkAlpha))
			return false;
		if (chunkFrame.size() != pixelCount)
			return false;

		const uint32_t newSampleCount = sampleCount + 1;
		const double chunkWeight = 1.0 / static_cast<double>(newSampleCount);
		for (size_t i = 0; i < pixelCount; ++i)
		{
			runningMean[i]       += (glm::dvec3(chunkFrame[i])  - runningMean[i])       * chunkWeight;
			runningMeanAlbedo[i] += (glm::dvec3(chunkAlbedo[i]) - runningMeanAlbedo[i]) * chunkWeight;
			runningMeanNormal[i] += (glm::dvec3(chunkNormal[i]) - runningMeanNormal[i]) * chunkWeight;
			runningMeanAlpha[i]  += (static_cast<double>(chunkAlpha[i]) - runningMeanAlpha[i]) * chunkWeight;
		}
		sampleCount = newSampleCount;

		if (onProgress)
			onProgress(sampleCount, maxSamples);
	}

	std::vector<glm::vec3> resolved(pixelCount);
	for (size_t i = 0; i < pixelCount; ++i)
		resolved[i] = glm::vec3(runningMean[i]);
	outLinearRgb = resolved;

	if (_ptDenoiserEnabled)
	{
		std::vector<glm::vec3> resolvedAlbedo(pixelCount), resolvedNormal(pixelCount);
		for (size_t i = 0; i < pixelCount; ++i)
		{
			resolvedAlbedo[i] = glm::vec3(runningMeanAlbedo[i]);
			resolvedNormal[i] = glm::vec3(runningMeanNormal[i]);
		}

		RtDenoiser denoiser(_ptDenoiserDevicePreference);
		std::vector<glm::vec3> denoised;
		denoiser.denoise(resolved, width, height, denoised, maxSamples, &resolvedAlbedo, &resolvedNormal);

		// Pure-background pixels (no primary ray ever hit geometry) keep the
		// raw accumulated value - OIDN over-smooths a sharp traced background
		// it has no guide values for - matches RtOptixRayTracingSession::
		// workerLoop()'s identical restoration exactly.
		for (size_t i = 0; i < pixelCount; ++i)
			if (runningMeanAlpha[i] <= 0.0)
				denoised[i] = resolved[i];

		outLinearRgb = std::move(denoised);
	}

	return true;
}

void ViewportWidget::onRayTracedRefreshTimer()
{
	if (!_rtInteractionCtrl->armed())
	{
		_rayTracedRefreshTimer->stop();
		return;
	}

	int frameWidth = 0, frameHeight = 0;
	uint32_t sampleCount = 0;

	// While an interactive GPU session is active, _rtInteractiveRenderer owns
	// the presenter texture exclusively - paintGL()'s own interactive-pull
	// block (tick()/pollCompletedFrame()) uploads it every paint. Pulling
	// _ptOptixSession's (settled-only, see its own doc comment) latestFrame()
	// here too, unconditionally, would race that: _ptOptixSession is reached
	// via RtRenderDialog's Render button or the app-reactivation/self-
	// healing watchdog restart paths, both of which correctly clear
	// _rayTracedInteractiveActive before starting it (see
	// startOptixTestRayTracedSession()'s own doc comment for why that
	// teardown matters) - but if that ever regressed and both ended up
	// "active" at once, this timer would keep re-uploading _ptOptixSession's
	// frames on its own cadence, fighting the interactive accumulator's own
	// per-paint uploads and making the on-screen pose visibly hop back and
	// forth between the two. Only the settled session may publish to the
	// presenter here.
	if (effectiveRayTracingEnginePreference() == RtRayTracingEnginePreference::GPU && !_rayTracedInteractiveActive)
	{
		std::vector<float> alpha;
		std::vector<glm::vec3> frame = _ptOptixSession.latestFrame(frameWidth, frameHeight, sampleCount, &alpha);
		if (!frame.empty())
			_rtPresenter.upload(frame, frameWidth, frameHeight, &alpha);
	}
	else if (effectiveRayTracingEnginePreference() != RtRayTracingEnginePreference::GPU)
	{
		std::vector<float> alpha;
		std::vector<glm::vec3> frame = _rtSession.latestFrame(frameWidth, frameHeight, sampleCount, &alpha);
		if (!frame.empty())
			_rtPresenter.upload(frame, frameWidth, frameHeight, &alpha);
	}

	update();
}

void ViewportWidget::setFloorTexRepeatT(double floorTexRepeatT)
{
	_renderCtrl.setFloorTexRepeatT(static_cast<float>(floorTexRepeatT));
	updateFloorPlane();
	update();
	notifyRayTracedSceneMutated(); // see setFloorTexture()'s identical doc comment - floor UV tiling is baked into PT's snapshot geometry
}

void ViewportWidget::setFloorTexRepeatS(double floorTexRepeatS)
{
	_renderCtrl.setFloorTexRepeatS(static_cast<float>(floorTexRepeatS));
	updateFloorPlane();
	update();
	notifyRayTracedSceneMutated(); // see setFloorTexture()'s identical doc comment - floor UV tiling is baked into PT's snapshot geometry
}

void ViewportWidget::setFloorOffsetPercent(double value)
{
	_renderCtrl.setFloorOffsetPercent(static_cast<float>(value / 100.0f));
	updateFloorPlane();
	update();
	notifyRayTracedSceneMutated(); // see setFloorTexture()'s identical doc comment - floor offset moves PT's snapshot geometry
}

void ViewportWidget::setPerspFOV(int fovDegrees)
{
	_viewCtrl.setFOV(static_cast<float>(qBound(1, fovDegrees, 179)));
	_primaryCamera->setFOV(_viewCtrl.FOV());
	resizeGL(width(), height());
	update();
}

void ViewportWidget::setSkyBoxZRotation(int index)
{
	// Map combo index to Y-axis rotation angle (OpenGL Y = world Z-up)
	// X+ → 0°, X- → 180°, Y-Z+ → 90°, Y+ → 270°
	static constexpr float angles[] = { 0.0f, 180.0f, 90.0f, 270.0f };
	setSkyBoxZRotationDegrees(angles[index % 4]);
}

void ViewportWidget::setSkyBoxZRotationDegrees(float degrees)
{
	_renderCtrl.setSkyBoxZRotation(degrees);
	updateEnvMapRotationMatrix();
	update();
	// Camera-grade restart only (was notifyRayTracedSceneMutated,
	// downgraded) - skyBoxZRotationDegrees is a per-launch environment
	// scalar now, same reasoning as showSkyBox().
	_rtInteractionCtrl->notifySceneContentMutated();
}

void ViewportWidget::updateEnvMapRotationMatrix()
{
	// _renderCtrl.fgShader() is null before initializeGL() — the call from the constructor
	// (settings-based setCameraUpAxisZUp) is a no-op; initializeGL() picks up
	// the correct state at line 1729 after createShaderPrograms().
	if (!_renderCtrl.fgShader())
		return;

	// Build Ry(-theta) · Rx(-90°) using Qt post-multiply (M = M*R):
	//
	//   envMapRotationMatrix = Ry(-theta) · Rx(-90°)
	//
	// This converts a Z-up world direction into a cubemap sample direction:
	//   1. Rx(-90°)  — maps world Z-up to cubemap Y-up (Z-up correction)
	//   2. Ry(-theta)— rotates horizontally around the (now corrected) Y axis,
	//                   matching the user's Z-up world rotation
	//
	// Ordering matters: Rx(-90°) must be the INNER transform and Ry(-theta)
	// the OUTER, so that Ry acts in cubemap (Y-up) space, not raw Z-up space.
	// Reversing the order would tilt the sky axis as the environment rotates.
	QMatrix4x4 envMapRot;
	envMapRot.rotate(-_renderCtrl.skyBoxZRotation(), 0, 1, 0); // Qt post-mul: M = Ry(-theta)
	envMapRot.rotate(-90.0f, 1, 0, 0);            // Qt post-mul: M = Ry(-theta) · Rx(-90°)
	envMapRot.rotate(CoordinateSystemHelper::cameraUpAxisConventionRotation(_viewCtrl.cameraUpAxisZUp()).inverted());
	_renderCtrl.fgShader()->bind();
	_renderCtrl.fgShader()->setUniformValue("envMapRotationMatrix", envMapRot.toGenericMatrix<3, 3>());
}

QColor ViewportWidget::getBgBotColor() const
{
	return _renderCtrl.bgBotColor();
}

void ViewportWidget::updateOverlayEditorTheme()
{
	emit backgroundColorChanged(_renderCtrl.bgTopColor(), _renderCtrl.bgBotColor());

	const QColor averageBackgroundColor(
		(_renderCtrl.bgTopColor().red() + _renderCtrl.bgBotColor().red()) / 2,
		(_renderCtrl.bgTopColor().green() + _renderCtrl.bgBotColor().green()) / 2,
		(_renderCtrl.bgTopColor().blue() + _renderCtrl.bgBotColor().blue()) / 2,
		(_renderCtrl.bgTopColor().alpha() + _renderCtrl.bgBotColor().alpha()) / 2);
	const QColor contrastColor = (averageBackgroundColor.lightnessF() < 0.5)
		? QColor(255, 255, 255)
		: QColor(0, 0, 0);

	if (QTabWidget* tabs = _viewer ? _viewer->findChild<QTabWidget*>("tabWidget") : nullptr) {
		const QString tabStyleSheet = QString("color: rgb(%1, %2, %3);")
									  .arg(contrastColor.red())
									  .arg(contrastColor.green())
									  .arg(contrastColor.blue()) +
									  "background-color: rgba(255, 255, 255, 0);";
		tabs->setStyleSheet(tabStyleSheet);
	}
}

void ViewportWidget::setBgBotColor(const QColor& bgBotColor)
{
	_renderCtrl.setBgBotColor(bgBotColor);
	updateOverlayEditorTheme();
	refreshNavigationOverlayStyle();
	// Feeds the PT snapshot's fallback-background scalars - see
	// setBgGradientStyle()'s doc comment (ViewportWidget.h) for why this
	// needs a camera-grade PT restart (and only that - no revision bump).
	_rtInteractionCtrl->notifySceneContentMutated();
	update();
}

QColor ViewportWidget::getBgTopColor() const
{
	return _renderCtrl.bgTopColor();
}

void ViewportWidget::setBgTopColor(const QColor& bgTopColor)
{
	_renderCtrl.setBgTopColor(bgTopColor);
	updateOverlayEditorTheme();
	refreshNavigationOverlayStyle();
	// See setBgBotColor() above.
	_rtInteractionCtrl->notifySceneContentMutated();
	update();
}

BoundingSphere ViewportWidget::getBoundingSphere() const
{
	return _viewCtrl.boundingSphere();
}

std::vector<int> ViewportWidget::getDisplayedObjectsIds() const
{
	return _sceneRuntime.displayedObjectsIds();
}

std::vector<int> ViewportWidget::getHiddenObjectsIds() const
{
	return _sceneRuntime.hiddenObjectsIds();
}



bool ViewportWidget::isVisibleSwapped() const
{
	return _sceneRuntime.visibleSwapped();
}

void ViewportWidget::setShowFaceNormals(bool showFaceNormals)
{
    if (showFaceNormals)
    {
        setDebugOverlayMode(DebugOverlayMode::FaceNormals);
        setDebugOverlayEnabled(true);
    }
    else if (_renderCtrl.debugOverlayMode() == DebugOverlayMode::FaceNormals)
    {
        setDebugOverlayEnabled(false);
    }
}


void ViewportWidget::setShowVertexNormals(bool showVertexNormals)
{
    if (showVertexNormals)
    {
        setDebugOverlayMode(DebugOverlayMode::VertexNormals);
        setDebugOverlayEnabled(true);
    }
    else if (_renderCtrl.debugOverlayMode() == DebugOverlayMode::VertexNormals)
    {
        setDebugOverlayEnabled(false);
    }
}

void ViewportWidget::setShowBoundingBox(bool showBoundingBox)
{
    if (showBoundingBox)
    {
        setDebugOverlayMode(DebugOverlayMode::BoundingBox);
        setDebugOverlayEnabled(true);
    }
    else if (_renderCtrl.debugOverlayMode() == DebugOverlayMode::BoundingBox)
    {
        setDebugOverlayEnabled(false);
    }
}

void ViewportWidget::setDebugOverlayMode(DebugOverlayMode mode)
{
    _renderCtrl.setDebugOverlayMode(mode);

    const bool requestedModeAvailable =
        (_renderCtrl.debugOverlayMode() == DebugOverlayMode::BoundingBox && _renderCtrl.debugBoundingBoxAvailable()) ||
        (_renderCtrl.debugOverlayMode() == DebugOverlayMode::VertexNormals && _renderCtrl.debugVertexNormalsAvailable()) ||
        (_renderCtrl.debugOverlayMode() == DebugOverlayMode::FaceNormals && _renderCtrl.debugFaceNormalsAvailable());

    if (!requestedModeAvailable)
    {
        if (_renderCtrl.debugBoundingBoxAvailable())
            _renderCtrl.setDebugOverlayMode(DebugOverlayMode::BoundingBox);
        else if (_renderCtrl.debugVertexNormalsAvailable())
            _renderCtrl.setDebugOverlayMode(DebugOverlayMode::VertexNormals);
        else if (_renderCtrl.debugFaceNormalsAvailable())
            _renderCtrl.setDebugOverlayMode(DebugOverlayMode::FaceNormals);
    }

    _renderCtrl.setShowBoundingBox(_renderCtrl.debugOverlayEnabled() && _renderCtrl.debugOverlayMode() == DebugOverlayMode::BoundingBox);
    _renderCtrl.setShowVertexNormals(_renderCtrl.debugOverlayEnabled() && _renderCtrl.debugOverlayMode() == DebugOverlayMode::VertexNormals);
    _renderCtrl.setShowFaceNormals(_renderCtrl.debugOverlayEnabled() && _renderCtrl.debugOverlayMode() == DebugOverlayMode::FaceNormals);

    if (_viewToolbar)
    {
        DebugOverlayActions action = DebugOverlayActions::BOUNDING_BOX;
        if (_renderCtrl.debugOverlayMode() == DebugOverlayMode::VertexNormals)
            action = DebugOverlayActions::VERTEX_NORMALS;
        else if (_renderCtrl.debugOverlayMode() == DebugOverlayMode::FaceNormals)
            action = DebugOverlayActions::FACE_NORMALS;

        _viewToolbar->setDebugOverlayState(action, _renderCtrl.debugOverlayEnabled());
    }

    update();
}

void ViewportWidget::setDebugOverlayEnabled(bool enabled)
{
    const bool hasAnyOverlay =
        _renderCtrl.debugBoundingBoxAvailable() || _renderCtrl.debugVertexNormalsAvailable() || _renderCtrl.debugFaceNormalsAvailable();

    if (!hasAnyOverlay)
        enabled = false;

    if (enabled)
    {
        const bool currentModeAvailable =
            (_renderCtrl.debugOverlayMode() == DebugOverlayMode::BoundingBox && _renderCtrl.debugBoundingBoxAvailable()) ||
            (_renderCtrl.debugOverlayMode() == DebugOverlayMode::VertexNormals && _renderCtrl.debugVertexNormalsAvailable()) ||
            (_renderCtrl.debugOverlayMode() == DebugOverlayMode::FaceNormals && _renderCtrl.debugFaceNormalsAvailable());

        if (!currentModeAvailable)
        {
            if (_renderCtrl.debugBoundingBoxAvailable())
                _renderCtrl.setDebugOverlayMode(DebugOverlayMode::BoundingBox);
            else if (_renderCtrl.debugVertexNormalsAvailable())
                _renderCtrl.setDebugOverlayMode(DebugOverlayMode::VertexNormals);
            else
                _renderCtrl.setDebugOverlayMode(DebugOverlayMode::FaceNormals);
        }
    }

    _renderCtrl.setDebugOverlayEnabled(enabled);
    _renderCtrl.setShowBoundingBox(_renderCtrl.debugOverlayEnabled() && _renderCtrl.debugOverlayMode() == DebugOverlayMode::BoundingBox);
    _renderCtrl.setShowVertexNormals(_renderCtrl.debugOverlayEnabled() && _renderCtrl.debugOverlayMode() == DebugOverlayMode::VertexNormals);
    _renderCtrl.setShowFaceNormals(_renderCtrl.debugOverlayEnabled() && _renderCtrl.debugOverlayMode() == DebugOverlayMode::FaceNormals);

    if (_viewToolbar)
    {
        DebugOverlayActions action = DebugOverlayActions::BOUNDING_BOX;
        if (_renderCtrl.debugOverlayMode() == DebugOverlayMode::VertexNormals)
            action = DebugOverlayActions::VERTEX_NORMALS;
        else if (_renderCtrl.debugOverlayMode() == DebugOverlayMode::FaceNormals)
            action = DebugOverlayActions::FACE_NORMALS;

        _viewToolbar->setDebugOverlayState(action, _renderCtrl.debugOverlayEnabled());
    }

    update();
}

void ViewportWidget::setDebugOverlayAvailability(bool boundingBox, bool vertexNormals, bool faceNormals)
{
    _renderCtrl.setDebugBoundingBoxAvailable(boundingBox);
    _renderCtrl.setDebugVertexNormalsAvailable(vertexNormals);
    _renderCtrl.setDebugFaceNormalsAvailable(faceNormals);

    const bool hasAnyOverlay = boundingBox || vertexNormals || faceNormals;
    if (!hasAnyOverlay)
    {
        _renderCtrl.setDebugOverlayEnabled(false);
    }
    else
    {
        const bool currentModeAvailable =
            (_renderCtrl.debugOverlayMode() == DebugOverlayMode::BoundingBox && _renderCtrl.debugBoundingBoxAvailable()) ||
            (_renderCtrl.debugOverlayMode() == DebugOverlayMode::VertexNormals && _renderCtrl.debugVertexNormalsAvailable()) ||
            (_renderCtrl.debugOverlayMode() == DebugOverlayMode::FaceNormals && _renderCtrl.debugFaceNormalsAvailable());

        if (!currentModeAvailable)
        {
            if (_renderCtrl.debugBoundingBoxAvailable())
                _renderCtrl.setDebugOverlayMode(DebugOverlayMode::BoundingBox);
            else if (_renderCtrl.debugVertexNormalsAvailable())
                _renderCtrl.setDebugOverlayMode(DebugOverlayMode::VertexNormals);
            else
                _renderCtrl.setDebugOverlayMode(DebugOverlayMode::FaceNormals);
        }
    }

    _renderCtrl.setShowBoundingBox(_renderCtrl.debugOverlayEnabled() && _renderCtrl.debugOverlayMode() == DebugOverlayMode::BoundingBox);
    _renderCtrl.setShowVertexNormals(_renderCtrl.debugOverlayEnabled() && _renderCtrl.debugOverlayMode() == DebugOverlayMode::VertexNormals);
    _renderCtrl.setShowFaceNormals(_renderCtrl.debugOverlayEnabled() && _renderCtrl.debugOverlayMode() == DebugOverlayMode::FaceNormals);

    if (_viewToolbar)
    {
        _viewToolbar->setDebugOverlayModesAvailable(boundingBox, vertexNormals, faceNormals);

        DebugOverlayActions action = DebugOverlayActions::BOUNDING_BOX;
        if (_renderCtrl.debugOverlayMode() == DebugOverlayMode::VertexNormals)
            action = DebugOverlayActions::VERTEX_NORMALS;
        else if (_renderCtrl.debugOverlayMode() == DebugOverlayMode::FaceNormals)
            action = DebugOverlayActions::FACE_NORMALS;

        _viewToolbar->setDebugOverlayState(action, _renderCtrl.debugOverlayEnabled());
    }

    update();
}

bool ViewportWidget::isShaded() const
{
	return _displayMode == DisplayMode::SHADED;
}

DisplayMode ViewportWidget::getDisplayMode() const
{
	return _displayMode;
}

void ViewportWidget::setDisplayMode(DisplayMode mode)
{
	_displayMode = mode;

	if (_viewToolbar)
		_viewToolbar->setDefaultDisplayModeAction(static_cast<DisplayModeActions>(_displayMode));

	_renderCtrl.fgShader()->bind();
	_renderCtrl.fgShader()->setUniformValue("displayMode", displayModeShaderInt(_displayMode));
	_renderCtrl.fgShader()->release();
	emit displayModeChanged(static_cast<int>(_displayMode));
}

void ViewportWidget::setRealismEnabled(bool enabled)
{
	_realismEnabled = enabled;
	_renderCtrl.fgShader()->bind();
	_renderCtrl.fgShader()->setUniformValue("realismEnabled", enabled);
	_renderCtrl.fgShader()->release();
	if (_viewToolbar)
		_viewToolbar->setRealisticChecked(enabled);
	emit displayModeChanged(static_cast<int>(_displayMode));
	update();
}

void ViewportWidget::setShadingNormalMode(ShadingNormalMode mode)
{
	_shadingNormalMode = mode;
	_renderCtrl.fgShader()->bind();
	_renderCtrl.fgShader()->setUniformValue("shadingNormalMode", static_cast<int>(mode));
	_renderCtrl.fgShader()->release();
	if (_viewToolbar)
		_viewToolbar->setDefaultShadingNormalModeAction(
			mode == ShadingNormalMode::FLAT
				? ShadingNormalModeActions::FLAT
				: ShadingNormalModeActions::SMOOTH);
	syncUniformsToFlatShader();
	update();
}

void ViewportWidget::setTransmissionEnabled(const bool& enabled)
{
	_renderCtrl.setTransmissionEnabled(enabled);
	if (_renderCtrl.transmissionEnabled())
		initTransmissionBuffer();
	update();
}

void ViewportWidget::showContextMenu(const QPoint& pos)
{
	if (QApplication::keyboardModifiers() != Qt::ControlModifier)
	{
		// Create menu and insert some actions
		QMenu contextMenu;
		SceneTreeWidget* treeWidgetModel = _viewer->getTreeModel();
		if (treeWidgetModel->hasMeshSelection() &&
			(_sceneRuntime.visibleSwapped() ? _sceneRuntime.hiddenObjectsIds().size() != 0 : _sceneRuntime.displayedObjectsIds().size() != 0))
		{
			contextMenu.addAction(tr("Center Screen"), _viewer, &ModelViewer::centerScreen);
			QList<QUuid> selUuids = treeWidgetModel->selectedMeshUuids();
			if (selUuids.count() <= 1)
			{
				// Show "Center Object List" only when the selected mesh is visible
				QSet<QUuid> visibleUuids = treeWidgetModel->getVisibleUuids();
				if (selUuids.isEmpty() || visibleUuids.contains(selUuids.first()))
					contextMenu.addAction(tr("Center Object List"), this, &ViewportWidget::centerDisplayList);
			}
			contextMenu.addSeparator();
			if (_sceneRuntime.visibleSwapped())
				contextMenu.addAction(tr("Show"), _viewer, &ModelViewer::showSelectedItems);
			else
				contextMenu.addAction(tr("Hide"), _viewer, &ModelViewer::hideSelectedItems);
			if (_sceneRuntime.displayedObjectsIds().size() > 1)
				contextMenu.addAction(tr("Show Only"), _viewer, &ModelViewer::showOnlySelectedItems);
			contextMenu.addSeparator();
			contextMenu.addAction(tr("Transformations"), _viewer, &ModelViewer::showTransformationsPage);
			contextMenu.addAction(tr("Edit Material"), _viewer, &ModelViewer::editMeshMaterial);
			contextMenu.addSeparator();			
			contextMenu.addAction(tr("Generate UVs"), _viewer, &ModelViewer::generateUVsForSelectedItems);
			contextMenu.addSeparator();
			contextMenu.addAction(tr("Copy"),   _viewer, &ModelViewer::copySelectedItems);
			contextMenu.addAction(tr("Cut"),    _viewer, &ModelViewer::cutSelectedItems);
			contextMenu.addAction(tr("Delete"), _viewer, &ModelViewer::deleteSelectedItems);			
			contextMenu.addSeparator();
			contextMenu.addAction(tr("Mesh Info"), _viewer, &ModelViewer::displaySelectedMeshInfo);
		}
		else
		{
			QAction* action = nullptr;
			if ((!_sceneRuntime.visibleSwapped() && _sceneRuntime.displayedObjectsIds().size() != 0) || (_sceneRuntime.visibleSwapped() && _sceneRuntime.hiddenObjectsIds().size() != 0))
			{				
				action = contextMenu.addAction(QIcon(":/icons/res/fit-all.png"), tr("Fit All"), this, &ViewportWidget::fitAll);
				action->setShortcut(QKeySequence(Qt::Key_F));
				action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
				addAction(action);

				action = contextMenu.addAction(QIcon(":/icons/res/window-zoom.png"), tr("Zoom Area"));
				action->setShortcut(QKeySequence(Qt::ALT | Qt::Key_W));
				action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
				addAction(action);
				action->setCheckable(true);
				connect(action, &QAction::triggered, this, &ViewportWidget::beginWindowZoom);

				// View manipulation actions				
				contextMenu.addSeparator();

				// If any of the view modes are active, add a menu item named select to disable them
				if (_viewCtrl.viewZooming() || _viewCtrl.viewPanning() || _viewCtrl.viewRotating())
				{
					contextMenu.addSeparator();
					action = contextMenu.addAction(QIcon(":/icons/res/select.png"), tr("Select"));
					connect(action, &QAction::triggered, this, [this]() {
						setZoomingActive(false);
						setPanningActive(false);
						setRotationActive(false);
						setCursor(QCursor(Qt::ArrowCursor));
						});
				}
				action = contextMenu.addAction(QIcon(":/icons/res/zoomview.png"), tr("Zoom"));
				action->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Z));
				action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
				addAction(action);
				connect(action, &QAction::triggered, this, [this]() {
					setZoomingActive(true);
					});

				action = contextMenu.addAction(QIcon(":/icons/res/panview.png"), tr("Pan"));
				action->setShortcut(QKeySequence(Qt::ALT | Qt::Key_P));
				action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
				addAction(action);
				connect(action, &QAction::triggered, this, [this]() {
					setPanningActive(true);
					});

				action = contextMenu.addAction(QIcon(":/icons/res/rotateview.png"), tr("Rotate"));
				action->setShortcut(QKeySequence(Qt::ALT | Qt::Key_R));
				action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
				addAction(action);
				connect(action, &QAction::triggered, this, [this]() {
					setRotationActive(true);
					});								

				contextMenu.addSeparator();
			}			

			if (_sceneRuntime.hiddenObjectsIds().size() != 0)
			{
				action = contextMenu.addAction(QIcon(":/icons/res/showall.png"), tr("Show All"), _viewer, &ModelViewer::showAllItems);
				action->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_A));
				action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
				addAction(action);
			}
			if (_sceneRuntime.displayedObjectsIds().size() != 0)
			{
				action = contextMenu.addAction(QIcon(":/icons/res/hideall.png"), tr("Hide All"), _viewer, &ModelViewer::hideAllItems);
				action->setShortcut(QKeySequence(Qt::ALT | Qt::Key_A));
				action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
				addAction(action);
			}
			if (_sceneRuntime.hiddenObjectsIds().size() != 0)
			{
				action = contextMenu.addAction(QIcon(":/icons/res/swapvisible.png"), tr("Swap Visible"));
				action->setCheckable(true);
				action->setShortcut(QKeySequence(Qt::ALT | Qt::Key_S));
				action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
				addAction(action);
				action->setChecked(_sceneRuntime.visibleSwapped());				
				connect(action, &QAction::triggered, this, [this](bool enabled) {
					swapVisible(enabled);
					});
			}
			contextMenu.addSeparator();
			contextMenu.addAction(QIcon(":/icons/res/environment.png"), tr("Environment Settings"), _viewer, &ModelViewer::showVisualizationModelPage);
			contextMenu.addAction(QIcon(":/icons/res/bg_color.png"), tr("Background Color"), this, &ViewportWidget::setBackgroundColor);
		}
		// Show context menu at handling position
		contextMenu.exec(mapToGlobal(pos));
	}
}

void ViewportWidget::centerDisplayList()
{
	SceneTreeWidget* treeWidgetModel = _viewer->getTreeModel();
	if (treeWidgetModel && treeWidgetModel->hasMeshSelection())
		treeWidgetModel->scrollFirstSelectedToCenter();
}

#include "BackgroundColor.h"
void ViewportWidget::setBackgroundColor()
{
	BackgroundColor bgCol(this);
	bgCol.exec();
}

// ---------------------------------------------------------------------------
// MVF3 mesh loader
// ---------------------------------------------------------------------------

#include "MvfDocument.h"
#include <QFile>
#include <QTemporaryDir>
#include <QUuid>
#include <cstring>

namespace
{
// Read count*componentsOf(type) floats from geometryChunk via an accessor index.
static std::vector<float> readFloatStream(const QByteArray& chunk,
                                           const QJsonArray& accessors,
                                           const QJsonArray& bufferViews,
                                           int accessorIndex)
{
    if (accessorIndex < 0 || accessorIndex >= accessors.size())
        return {};
    const QJsonObject acc = accessors[accessorIndex].toObject();
    const int bvIdx = acc[QStringLiteral("bufferView")].toInt(-1);
    if (bvIdx < 0 || bvIdx >= bufferViews.size())
        return {};
    const QJsonObject bv = bufferViews[bvIdx].toObject();
    if (bv[QStringLiteral("buffer")].toInt(-1) != 0)
        return {};  // not the GEOM buffer

    const int bvOffset   = (int)bv[QStringLiteral("byteOffset")].toDouble(0);
    const int accOffset  = (int)acc[QStringLiteral("byteOffset")].toDouble(0);
    const int byteOffset = bvOffset + accOffset;
    const int count      = (int)acc[QStringLiteral("count")].toDouble(0);

    const QString type = acc[QStringLiteral("type")].toString();
    int components = 1;
    if      (type == QLatin1String("VEC2")) components = 2;
    else if (type == QLatin1String("VEC3")) components = 3;
    else if (type == QLatin1String("VEC4")) components = 4;

    const int totalFloats = count * components;
    if (byteOffset + totalFloats * (int)sizeof(float) > chunk.size())
        return {};

    std::vector<float> result(totalFloats);
    std::memcpy(result.data(), chunk.constData() + byteOffset,
                totalFloats * sizeof(float));
    return result;
}

// Read count unsigned ints from geometryChunk via an accessor index.
static std::vector<unsigned int> readUIntStream(const QByteArray& chunk,
                                                 const QJsonArray& accessors,
                                                 const QJsonArray& bufferViews,
                                                 int accessorIndex)
{
    if (accessorIndex < 0 || accessorIndex >= accessors.size())
        return {};
    const QJsonObject acc = accessors[accessorIndex].toObject();
    const int bvIdx = acc[QStringLiteral("bufferView")].toInt(-1);
    if (bvIdx < 0 || bvIdx >= bufferViews.size())
        return {};
    const QJsonObject bv = bufferViews[bvIdx].toObject();
    if (bv[QStringLiteral("buffer")].toInt(-1) != 0)
        return {};

    const int bvOffset   = (int)bv[QStringLiteral("byteOffset")].toDouble(0);
    const int accOffset  = (int)acc[QStringLiteral("byteOffset")].toDouble(0);
    const int byteOffset = bvOffset + accOffset;
    const int count      = (int)acc[QStringLiteral("count")].toDouble(0);

    if (byteOffset + count * (int)sizeof(unsigned int) > chunk.size())
        return {};

    std::vector<unsigned int> result(count);
    std::memcpy(result.data(), chunk.constData() + byteOffset,
                count * sizeof(unsigned int));
    return result;
}

// Apply a texture info JSON object to the right Material path setter.
static void applyTextureRef(Material& mat,
                             Material::TextureType type,
                             const QJsonObject& texInfo,
                             const QHash<int, QString>& imagePaths,
                             const QJsonArray& textures,
                             const QJsonArray& samplers)
{
    const int texIndex = texInfo[QStringLiteral("index")].toInt(-1);
    if (texIndex < 0 || texIndex >= textures.size())
        return;
    const QJsonObject texObj   = textures[texIndex].toObject();
    const int imgIndex         = texObj[QStringLiteral("image")].toInt(-1);
    const int samplerIndex     = texObj[QStringLiteral("sampler")].toInt(-1);
    const QString path         = imagePaths.value(imgIndex);
    if (path.isEmpty())
        return;

    switch (type)
    {
    case Material::TextureType::Albedo:                   mat.setAlbedoMap(path); break;
    case Material::TextureType::Normal:                   mat.setNormalMap(path); break;
    case Material::TextureType::AmbientOcclusion:         mat.setAOMap(path); break;
    case Material::TextureType::Emissive:                 mat.setEmissiveMap(path); break;
    case Material::TextureType::Metallic:                 mat.setMetallicMap(path); break;
    case Material::TextureType::Roughness:                mat.setRoughnessMap(path); break;
    case Material::TextureType::Transmission:             mat.setTransmissionMap(path); break;
    case Material::TextureType::IOR:                      mat.setIORMap(path); break;
    case Material::TextureType::SheenColor:               mat.setSheenColorMap(path); break;
    case Material::TextureType::SheenRoughness:           mat.setSheenRoughnessMap(path); break;
    case Material::TextureType::ClearcoatColor:           mat.setClearcoatColorMap(path); break;
    case Material::TextureType::ClearcoatRoughness:       mat.setClearcoatRoughnessMap(path); break;
    case Material::TextureType::ClearcoatNormal:          mat.setClearcoatNormalMap(path); break;
    case Material::TextureType::Iridescence:              mat.setIridescenceMap(path); break;
    case Material::TextureType::IridescenceThickness:     mat.setIridescenceThicknessMap(path); break;
    case Material::TextureType::SpecularFactor:           mat.setSpecularFactorMap(path); break;
    case Material::TextureType::SpecularColor:            mat.setSpecularColorMap(path); break;
    case Material::TextureType::Anisotropy:               mat.setAnisotropyMap(path); break;
    case Material::TextureType::Thickness:                mat.setThicknessMap(path); break;
    case Material::TextureType::Diffuse:                  mat.setDiffuseMap(path); break;
    case Material::TextureType::DiffuseTransmission:      mat.setDiffuseTransmissionMap(path); break;
    case Material::TextureType::DiffuseTransmissionColor: mat.setDiffuseTransmissionColorMap(path); break;
    case Material::TextureType::SpecularGlossiness:       mat.setSpecularGlossinessMap(path); break;
    case Material::TextureType::Opacity:                  mat.setOpacityMap(path); break;
    case Material::TextureType::Height:                   mat.setHeightMap(path); break;
    default: return;
    }

    Material::Texture tex = mat.texture(type);
    tex.path = path.toStdString();
    tex.texCoordIndex = texInfo[QStringLiteral("texCoord")].toInt(0);

    if (samplerIndex >= 0 && samplerIndex < samplers.size())
    {
        const QJsonObject samp = samplers[samplerIndex].toObject();
        tex.magFilter = static_cast<GLenum>(samp[QStringLiteral("magFilter")].toInt(GL_LINEAR));
        tex.minFilter = static_cast<GLenum>(samp[QStringLiteral("minFilter")].toInt(GL_LINEAR_MIPMAP_LINEAR));
        tex.wrapS     = static_cast<GLenum>(samp[QStringLiteral("wrapS")].toInt(GL_REPEAT));
        tex.wrapT     = static_cast<GLenum>(samp[QStringLiteral("wrapT")].toInt(GL_REPEAT));
    }

    const QJsonObject extensions = texInfo[QStringLiteral("extensions")].toObject();
    const QJsonObject transform = extensions[QStringLiteral("KHR_texture_transform")].toObject();
    if (!transform.isEmpty())
    {
        const QJsonArray scale = transform[QStringLiteral("scale")].toArray();
        if (scale.size() >= 2)
            tex.scale = glm::vec2(static_cast<float>(scale[0].toDouble(1.0)),
                                  static_cast<float>(scale[1].toDouble(1.0)));

        const QJsonArray offset = transform[QStringLiteral("offset")].toArray();
        if (offset.size() >= 2)
            tex.offset = glm::vec2(static_cast<float>(offset[0].toDouble(0.0)),
                                   static_cast<float>(offset[1].toDouble(0.0)));

        tex.rotation = static_cast<float>(transform[QStringLiteral("rotation")].toDouble(0.0));
    }

    mat.setTexture(type, tex);
}

// Reconstruct a Material from an MVF3 material JSON object.
static Material reconstructMvfMaterial(const QJsonObject& matObj,
                                         const QHash<int, QString>& imagePaths,
                                         const QJsonArray& textures,
                                         const QJsonArray& samplers)
{
    const QString materialName = matObj[QStringLiteral("name")].toString();
    const QJsonObject exts = matObj[QStringLiteral("extensions")].toObject();
    const bool hasRuntimeMaterial =
        !exts[QStringLiteral("MVF_material_runtime")].toObject().isEmpty();

    Material mat = hasRuntimeMaterial
        ? Material::fromVariantMap(exts[QStringLiteral("MVF_material_runtime")].toObject().toVariantMap())
        : Material();
    mat.setName(materialName);

    if (!hasRuntimeMaterial)
    {
        const QString shadingModel = matObj[QStringLiteral("shadingModel")].toString();
        if      (shadingModel == QLatin1String("PBR"))        mat.setShadingModel(Material::ShadingModel::PBR);
        else if (shadingModel == QLatin1String("BlinnPhong")) mat.setShadingModel(Material::ShadingModel::BlinnPhong);
        else if (shadingModel == QLatin1String("Unlit"))      mat.setShadingModel(Material::ShadingModel::Unlit);
        else if (shadingModel == QLatin1String("Toon"))       mat.setShadingModel(Material::ShadingModel::Toon);

        const QString blendMode = matObj[QStringLiteral("blendMode")].toString();
        if      (blendMode == QLatin1String("Opaque"))   mat.setBlendMode(Material::BlendMode::Opaque);
        else if (blendMode == QLatin1String("Masked"))   mat.setBlendMode(Material::BlendMode::Masked);
        else if (blendMode == QLatin1String("Alpha"))    mat.setBlendMode(Material::BlendMode::Alpha);
        else if (blendMode == QLatin1String("Additive")) mat.setBlendMode(Material::BlendMode::Additive);
        else if (blendMode == QLatin1String("Multiply")) mat.setBlendMode(Material::BlendMode::Multiply);

        mat.setTwoSided(matObj[QStringLiteral("doubleSided")].toBool(false));
        mat.setAlphaThreshold((float)matObj[QStringLiteral("alphaCutoff")].toDouble(0.5));
        mat.setOpacity((float)matObj[QStringLiteral("opacity")].toDouble(1.0));

        const QJsonObject pbr = matObj[QStringLiteral("pbr")].toObject();
        const QJsonArray bc = pbr[QStringLiteral("baseColorFactor")].toArray();
        if (bc.size() >= 3)
            mat.setAlbedoColor(QVector3D((float)bc[0].toDouble(),
                                          (float)bc[1].toDouble(),
                                          (float)bc[2].toDouble()));
        mat.setMetalness((float)pbr[QStringLiteral("metallicFactor")].toDouble(0.0));
        mat.setRoughness((float)pbr[QStringLiteral("roughnessFactor")].toDouble(1.0));
    }

    if (!hasRuntimeMaterial && exts.contains(QStringLiteral("MVF_material_ads")))
    {
        const QJsonObject ads = exts[QStringLiteral("MVF_material_ads")].toObject();
        auto v3 = [](const QJsonArray& a, const QVector3D& def = {}) -> QVector3D {
            return a.size() >= 3
                ? QVector3D((float)a[0].toDouble(), (float)a[1].toDouble(), (float)a[2].toDouble())
                : def;
        };
        mat.setAmbient (v3(ads[QStringLiteral("ambient")].toArray()));
        mat.setDiffuse (v3(ads[QStringLiteral("diffuse")].toArray()));
        mat.setSpecular(v3(ads[QStringLiteral("specular")].toArray()));
        mat.setEmissive(v3(ads[QStringLiteral("emissive")].toArray()));
        mat.setShininess((float)ads[QStringLiteral("shininess")].toDouble(32.0));
    }

    if (exts.contains(QStringLiteral("MVF_material_pbr")))
    {
        const QJsonObject mvfPbr = exts[QStringLiteral("MVF_material_pbr")].toObject();

        if (!hasRuntimeMaterial)
        {
            mat.setIOR((float)mvfPbr[QStringLiteral("ior")].toDouble(1.5));
            mat.setTransmission((float)mvfPbr[QStringLiteral("transmission")].toDouble(0.0));
            mat.setClearcoat((float)mvfPbr[QStringLiteral("clearcoat")].toDouble(0.0));
            mat.setClearcoatRoughness((float)mvfPbr[QStringLiteral("clearcoatRoughness")].toDouble(0.0));
            const QJsonArray sc = mvfPbr[QStringLiteral("sheenColor")].toArray();
            if (sc.size() >= 3)
                mat.setSheenColor(QVector3D((float)sc[0].toDouble(),
                                             (float)sc[1].toDouble(),
                                             (float)sc[2].toDouble()));
            mat.setSheenRoughness((float)mvfPbr[QStringLiteral("sheenRoughness")].toDouble(0.0));
        }

        static const struct { const char* key; Material::TextureType type; } kTexKeys[] = {
            {"baseColorTexture",                Material::TextureType::Albedo},
            {"normalTexture",                   Material::TextureType::Normal},
            {"occlusionTexture",                Material::TextureType::AmbientOcclusion},
            {"emissiveTexture",                 Material::TextureType::Emissive},
            {"metallicTexture",                 Material::TextureType::Metallic},
            {"roughnessTexture",                Material::TextureType::Roughness},
            {"transmissionTexture",             Material::TextureType::Transmission},
            {"iorTexture",                      Material::TextureType::IOR},
            {"sheenColorTexture",               Material::TextureType::SheenColor},
            {"sheenRoughnessTexture",           Material::TextureType::SheenRoughness},
            {"clearcoatTexture",                Material::TextureType::ClearcoatColor},
            {"clearcoatRoughnessTexture",       Material::TextureType::ClearcoatRoughness},
            {"clearcoatNormalTexture",          Material::TextureType::ClearcoatNormal},
            {"iridescenceTexture",              Material::TextureType::Iridescence},
            {"iridescenceThicknessTexture",     Material::TextureType::IridescenceThickness},
            {"specularTexture",                 Material::TextureType::SpecularFactor},
            {"specularColorTexture",            Material::TextureType::SpecularColor},
            {"anisotropyTexture",               Material::TextureType::Anisotropy},
            {"thicknessTexture",                Material::TextureType::Thickness},
            {"diffuseTexture",                  Material::TextureType::Diffuse},
            {"diffuseTransmissionTexture",      Material::TextureType::DiffuseTransmission},
            {"diffuseTransmissionColorTexture", Material::TextureType::DiffuseTransmissionColor},
            {"specularGlossinessTexture",       Material::TextureType::SpecularGlossiness},
            {"opacityTexture",                  Material::TextureType::Opacity},
            {"heightTexture",                   Material::TextureType::Height},
        };

        for (const auto& entry : kTexKeys)
        {
            const QString key = QLatin1String(entry.key);
            if (mvfPbr.contains(key))
                applyTextureRef(mat, entry.type, mvfPbr[key].toObject(),
                                imagePaths, textures, samplers);
        }
    }

    // Mirror any MVF-rebound texture slots back into the material's canonical
    // per-map path/id fields before runtime texture resolution.
    mat.syncTextureParameters();
    mat.updateConsistency();
    return mat;
}

static QVector<int> jsonArrayToIntVector(const QJsonArray& array)
{
    QVector<int> values;
    values.reserve(array.size());
    for (const QJsonValue& value : array)
        values.append(value.toInt(-1));
    return values;
}

static QVector<GltfVariantMapping> parseVariantMappings(const QJsonArray& array)
{
    QVector<GltfVariantMapping> mappings;
    mappings.reserve(array.size());
    for (const QJsonValue& value : array)
    {
        const QJsonObject obj = value.toObject();
        GltfVariantMapping mapping;
        mapping.materialIndex = obj[QStringLiteral("materialIndex")].toInt(-1);
        mapping.variantIndices = jsonArrayToIntVector(obj[QStringLiteral("variantIndices")].toArray());
        mappings.append(mapping);
    }
    return mappings;
}
} // anonymous namespace


// ---------------------------------------------------------------------------
// uploadPreparedMvfMeshes — GL-only, must be on main thread
// ---------------------------------------------------------------------------
bool ViewportWidget::uploadPreparedMvfMeshes(const QVector<PreparedMvfMesh>& meshes)
{
    makeCurrent();

    if (!_renderCtrl.fgShader())
    {
        update();
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        makeCurrent();
    }
    if (!_renderCtrl.fgShader())
        return false;

    if (_sceneRuntime.clearMeshStore())
        emit visibleSwapped(_sceneRuntime.visibleSwapped());

    const int totalMeshes = meshes.size();
    QElapsedTimer yieldTimer;
    yieldTimer.start();

    for (int i = 0; i < totalMeshes; ++i)
    {
        const PreparedMvfMesh& pm = meshes[i];

        SceneMesh* mesh = new SceneMesh(_renderCtrl.fgShader(), pm.name,
                                          {}, {}, {}, pm.material, false, pm.primitiveMode);
        mesh->setUuid(pm.uuid);
        mesh->setSceneIndex(pm.sceneIndex);
        mesh->setHasNegativeScale(pm.hasNegativeScale);
        mesh->setOriginalMaterialIndex(pm.originalMaterialIndex);
        mesh->setSourceFile(pm.sourceFile);
        mesh->setSourceNodeName(pm.sourceNodeName);
        mesh->setMeshData(pm.vertices, pm.indices);
        mesh->setVariantMappings(pm.variantMappings);
        mesh->setAllVariantMaterials(pm.allVariantMaterials);
        if (pm.hasSceneRenderTransform)
            mesh->setSceneRenderTransform(pm.sceneRenderTransform);

        // Restore skeletal skinning data so bone animations work after MVF reload.
        if (!pm.skinJoints.isEmpty())
            mesh->setSkinJoints(pm.skinJoints);

        // Restore morph target geometry so blend-shape animations work after MVF reload.
        if (!pm.morphTargets.isEmpty())
            mesh->setMorphTargets(pm.morphTargets, pm.defaultMorphWeights);

        // Restore OCC B-Rep edge segments so STEP/IGES/BREP true wireframe survives MVF round-trip.
        if (!pm.occEdgeSegments.empty())
            mesh->setPrecomputedOccEdges(pm.occEdgeSegments, pm.occEdgeBoundaries, pm.occEdgeCircles);

        const Material resolved = resolveMaterialTextures(this, pm.material);
        mesh->setMaterial(resolved);
        mesh->setTextureMaps(resolved);
        mesh->invertOpacityADSMap(resolved.isOpacityMapInverted());
        mesh->invertOpacityPBRMap(resolved.isOpacityMapInverted());

        // Restore per-mesh user transform (gizmo TRS) saved in the MVF file.
        // Apply non-default values only to avoid clobbering the identity state
        // for meshes that were never transformed by the user.
        {
            const QVector3D defaultScale(1.0f, 1.0f, 1.0f);
            const QVector3D defaultTranslation(0.0f, 0.0f, 0.0f);
            const bool hasTranslation  = !pm.meshTranslation.isNull();
            const bool hasScale        = (pm.meshScale != defaultScale);
            const bool hasRotation     = !pm.meshRotationQuat.isIdentity();
            if (hasTranslation || hasRotation || hasScale)
            {
                mesh->setTranslation(pm.meshTranslation);
                mesh->setRotationQuaternion(pm.meshRotationQuat, pm.meshRotation);
                mesh->setScaling(pm.meshScale);
            }
        }

        addToDisplay(mesh);

        // Yield periodically so the progress bar and event loop stay alive.
        if (yieldTimer.elapsed() >= 8)
        {
            const int pct = 50 + (i + 1) * 40 / totalMeshes;   // 50-90%
            MainWindow::setProgressValue(pct);
            MainWindow::showStatusMessage(
                tr("Uploading mesh %1 / %2").arg(i + 1).arg(totalMeshes));

            doneCurrent();
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            makeCurrent();
            yieldTimer.restart();
        }
    }

    updateView();
    return !_sceneRuntime.meshStore().empty();
}

// ---------------------------------------------------------------------------
// clearMeshStore — delete all meshes and clear display list
// ---------------------------------------------------------------------------
void ViewportWidget::clearMeshStore()
{
    makeCurrent();

    if (_sceneRuntime.clearMeshStore())
        emit visibleSwapped(_sceneRuntime.visibleSwapped());
}

// ---------------------------------------------------------------------------
// uploadOneMvfMesh — single-mesh GL upload for BlockingQueuedConnection
// ---------------------------------------------------------------------------
void ViewportWidget::uploadOneMvfMesh(const PreparedMvfMesh& pm)
{
    makeCurrent();

    // Create mesh on main thread (GL context required)
    SceneMesh* mesh = new SceneMesh(_renderCtrl.fgShader(), pm.name,
                                      {}, {}, {}, pm.material, false, pm.primitiveMode);
    mesh->setUuid(pm.uuid);
    mesh->setSceneIndex(pm.sceneIndex);
    mesh->setHasNegativeScale(pm.hasNegativeScale);
    mesh->setOriginalMaterialIndex(pm.originalMaterialIndex);
    mesh->setSourceFile(pm.sourceFile);
    mesh->setSourceNodeName(pm.sourceNodeName);
    mesh->setVariantMappings(pm.variantMappings);
    mesh->setAllVariantMaterials(pm.allVariantMaterials);

    // Upload VBO data
    mesh->setMeshData(pm.vertices, pm.indices);

    // Restore skeletal skinning data so bone animations work after MVF reload.
    if (!pm.skinJoints.isEmpty())
        mesh->setSkinJoints(pm.skinJoints);

    // Restore morph target geometry so blend-shape animations work after MVF reload.
    if (!pm.morphTargets.isEmpty())
        mesh->setMorphTargets(pm.morphTargets, pm.defaultMorphWeights);

    // Restore OCC B-Rep edge segments so STEP/IGES/BREP true wireframe survives MVF round-trip.
    if (!pm.occEdgeSegments.empty())
        mesh->setPrecomputedOccEdges(pm.occEdgeSegments, pm.occEdgeBoundaries, pm.occEdgeCircles);

    // Resolve textures and set material
    const Material resolved = resolveMaterialTextures(this, pm.material);
    mesh->setMaterial(resolved);
    mesh->setTextureMaps(resolved);
    mesh->invertOpacityADSMap(resolved.isOpacityMapInverted());
    mesh->invertOpacityPBRMap(resolved.isOpacityMapInverted());
    if (pm.hasSceneRenderTransform)
        mesh->setSceneRenderTransform(pm.sceneRenderTransform);

    // Restore per-mesh user transform (gizmo TRS) saved in the MVF file.
    {
        const QVector3D defaultScale(1.0f, 1.0f, 1.0f);
        const bool hasTranslation = !pm.meshTranslation.isNull();
        const bool hasScale       = (pm.meshScale != defaultScale);
        const bool hasRotation    = !pm.meshRotationQuat.isIdentity();
        if (hasTranslation || hasRotation || hasScale)
        {
            mesh->setTranslation(pm.meshTranslation);
            mesh->setRotationQuaternion(pm.meshRotationQuat, pm.meshRotation);
            mesh->setScaling(pm.meshScale);
        }
    }

    // Add to display list and track in pending UUIDs (like AssImp's onMeshBatchReady)
    addToDisplay(mesh);
    _sceneRuntime.pendingSceneUuids().append(mesh->uuid());
}

void ViewportWidget::setParsedLights(const GltfLightData& lightData)
{
    _animCtrl.pendingLightData() = lightData;

    // If this model carries no punctual lights, leave the existing GPU light
    // state intact. Another model already loaded may have punctual lights
    // registered in SceneGraph; onSceneLightDataChanged() is the authoritative
    // rebuilder once the file is registered.
    if (lightData.isEmpty())
        return;

    _animCtrl.setParsedLightsFromSingleFile(lightData);
    _animCtrl.clearAllAnimatedState();

    syncPunctualLightUniforms(static_cast<int>(_animCtrl.originalParsedLights().size()),
                              !_animCtrl.originalParsedLights().empty());
}

// ---------------------------------------------------------------------------
// onSceneLightDataChanged
//
// Slot connected to SceneGraph::lightDataChanged.  Called whenever a file's
// light data is registered (setLightData) or removed (clearLightData).
// Rebuilds SceneRuntime's parsed-light baseline from the full set of SceneGraph-registered
// lights so that gizmos and the repositioning system always reflect the
// current multi-model scene, not just the last model that was loaded.
// ---------------------------------------------------------------------------
void ViewportWidget::onSceneLightDataChanged()
{
    if (!_viewer || !_viewer->sceneGraph())
        return;

    _animCtrl.rebuildParsedLightsFromSceneGraph(_viewer->sceneGraph());

    if (_animCtrl.originalParsedLights().empty())
    {
        _animCtrl.clearParsedLights();
        makeCurrent();
        _renderCtrl.punctualLights()->setLights({});
        syncPunctualLightUniforms(0, false);
        return;
    }

    makeCurrent();
    updatePunctualLights();
}

// ---------------------------------------------------------------------------
// setDebugTextureEnabled / clearDebugTextureOverrides
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Per-unit scalar override helpers
//
// When a texture is disabled the replacement is white (1,1,1) — a neutral
// value that leaves all multiplicative channels visible on a lit mesh.
// That is the right neutral for modulating channels such as AO, roughness,
// metallic and normal: their effect can only be seen if the base mesh remains
// lit and visible.
//
// Emissive is the exception: it is purely additive, so a white replacement
// drives full-strength emission via the scalar factor.  Unit 12 therefore
// gets _renderCtrl.debugBlackTex() instead of white, which silences ADS directly
// (matEmissive = sample(black) = vec3(0)).  Scalar uniforms are also zeroed
// so PBR (emissiveStrength) is suppressed without relying on a bool override.
//
//  Unit 12 – emissive (black texture + scalar zeroing)
//    ADS: matEmissive = sample(black) = vec3(0)        — silenced by texture
//    PBR: pbrLighting.emissiveStrength = 0             — silenced by scalar
//
// All other units (including albedo / diffuse) receive only the white texture
// replacement — their scalar factors are left at their real values so the mesh
// stays normally lit, which is necessary for channels like AO to be visible.
// ---------------------------------------------------------------------------
namespace
{
void setScalarOverridesForUnit(SceneMesh* mesh, int unit)
{
	switch (unit)
	{
	case 12: // emissive — additive channel, must be fully suppressed
		mesh->setDebugUniformOverride("pbrLighting.emissiveStrength",
		    QVariant::fromValue<float>(0.0f));
		mesh->setDebugUniformOverride("material.emission",
		    QVariant::fromValue(QVector3D(0.0f, 0.0f, 0.0f)));
		// ADS: hasEmissiveTexture suppression via bool uniform is unreliable (QVariant
		// type matching). Silence ADS by substituting a black texture for unit 12 instead
		// (matEmissive = sample(black) = vec3(0)). PBR is covered by emissiveStrength=0.
		break;
	default:
		break;
	}
}

void clearScalarOverridesForUnit(SceneMesh* mesh, int unit)
{
	switch (unit)
	{
	case 12:
		mesh->clearDebugUniformOverride("pbrLighting.emissiveStrength");
		mesh->clearDebugUniformOverride("material.emission");
		mesh->markUniformsDirty();  // force setupUniforms to restore the real values
		break;
	default:
		break;
	}
}
} // anonymous namespace

void ViewportWidget::setDebugTextureEnabled(int meshId, int unitIndex, bool enabled)
{
	if (meshId < 0 || meshId >= static_cast<int>(_sceneRuntime.meshStore().size()) || !_sceneRuntime.meshAt(meshId))
		return;

	SceneMesh* mesh = _sceneRuntime.meshAt(meshId);

	if (enabled)
	{
		mesh->clearDebugTextureOverride(unitIndex);
		clearScalarOverridesForUnit(mesh, unitIndex);
	}
	else
	{
		// Normal-map units get flat tangent-space normal (0,0,1).
		// Emissive unit gets black (0,0,0) so the ADS path (which overwrites the
		// scalar directly from the sample) contributes nothing without needing a
		// bool-uniform override. PBR is covered by emissiveStrength=0 scalar.
		// All other units get neutral white (1,1,1).
		const bool isNormalUnit   = (unitIndex == 13 || unitIndex == 20);
		const bool isEmissiveUnit = (unitIndex == 12);
		const GLuint replaceTex = isNormalUnit   ? _renderCtrl.debugNormalTex() :
		                          isEmissiveUnit ? _renderCtrl.debugBlackTex()  : _renderCtrl.debugNeutralTex();
		mesh->setDebugTextureOverride(unitIndex, replaceTex);
		setScalarOverridesForUnit(mesh, unitIndex);
	}
	update();
}

void ViewportWidget::clearDebugTextureOverrides(int meshId)
{
	if (meshId >= 0 && meshId < static_cast<int>(_sceneRuntime.meshStore().size()) && _sceneRuntime.meshAt(meshId))
		_sceneRuntime.meshAt(meshId)->clearAllDebugTextureOverrides();
	update();
}

void ViewportWidget::clearAllDebugOverrides(int meshId)
{
	if (meshId >= 0 && meshId < static_cast<int>(_sceneRuntime.meshStore().size()) && _sceneRuntime.meshAt(meshId))
	{
		SceneMesh* mesh = _sceneRuntime.meshAt(meshId);
		mesh->clearAllDebugTextureOverrides();
		mesh->clearAllDebugUniformOverrides();
		// Re-write the current global channel so debugChannelOutput stays consistent
		// after the override map was wiped.  If the panel is closing, the caller
		// follows up with setGlobalDebugChannel(0) which resets all meshes.
		mesh->setDebugUniformOverride("debugChannelOutput",
		    QVariant::fromValue<int>(_renderCtrl.globalDebugChannel()));
		mesh->markUniformsDirty();
	}
	update();
}

// ---------------------------------------------------------------------------
// applyDebugTextureState
// ---------------------------------------------------------------------------
// Full-state replacement for the per-toggle setDebugTextureEnabled path.
// Called by TextureDebugPanel whenever any checkbox changes so the entire
// enabled/disabled set can be evaluated at once.
// NOTE: does NOT touch debugChannelOutput — that uniform is owned exclusively
// by setGlobalDebugChannel.
void ViewportWidget::applyDebugTextureState(int meshId,
                                       const QSet<int>& enabledUnits,
                                       const QSet<int>& allUnits)
{
	if (meshId < 0 || meshId >= static_cast<int>(_sceneRuntime.meshStore().size()) || !_sceneRuntime.meshAt(meshId))
		return;
	SceneMesh* mesh = _sceneRuntime.meshAt(meshId);

	// All textures active → clear all per-mesh overrides; no replacements needed.
	if (enabledUnits == allUnits)
	{
		for (int unit : allUnits)
		{
			mesh->clearDebugTextureOverride(unit);
			clearScalarOverridesForUnit(mesh, unit);
		}
		mesh->markUniformsDirty();
		update();
		return;
	}

	// Partial selection: replace disabled slots with neutral textures.
	for (int unit : allUnits)
	{
		if (enabledUnits.contains(unit))
		{
			mesh->clearDebugTextureOverride(unit);
			clearScalarOverridesForUnit(mesh, unit);
		}
		else
		{
			const bool isNormalUnit   = (unit == 13 || unit == 20);
			const bool isEmissiveUnit = (unit == 12);
			const GLuint replaceTex = isNormalUnit   ? _renderCtrl.debugNormalTex() :
			                          isEmissiveUnit ? _renderCtrl.debugBlackTex()  : _renderCtrl.debugNeutralTex();
			mesh->setDebugTextureOverride(unit, replaceTex);
			setScalarOverridesForUnit(mesh, unit);
		}
	}
	mesh->markUniformsDirty();
	update();
}

// ---------------------------------------------------------------------------
// setGlobalDebugChannel
// ---------------------------------------------------------------------------
// Activates or clears single-channel isolation for the channel dropdown.
// Applied to every mesh in _sceneRuntime.meshStore() — no mesh selection required.
// channelId == 0 restores normal rendering on all meshes.
void ViewportWidget::setGlobalDebugChannel(int channelId)
{
	_renderCtrl.setGlobalDebugChannel(channelId);
	makeCurrent();
	for (const SceneMeshRecord& meshRecord : _sceneRuntime.meshStore())
	{
		SceneMesh* mesh = meshRecord.mesh;
		if (!mesh) continue;
		if (channelId != 0)
		{
			// Channel isolation must ignore all checkbox/extension override state.
			// Clear every per-mesh debug override first, then install only the
			// requested debugChannelOutput override below.
			mesh->clearAllDebugTextureOverrides();
			mesh->clearAllDebugUniformOverrides();
		}
		mesh->setDebugUniformOverride("debugChannelOutput",
		    QVariant::fromValue<int>(channelId));
		mesh->markUniformsDirty();
	}
	doneCurrent();
	update();
}

// ---------------------------------------------------------------------------
// setDebugExtensionEnabled / clearDebugExtensionOverrides
// ---------------------------------------------------------------------------
// Extension key → { float uniform overrides, vec3 uniform overrides, texture units }
namespace
{
struct ExtOverrideDef
{
	QVector<QPair<QString, float>>      floatUniforms;
	QVector<QPair<QString, QVector3D>>  vec3Uniforms;
	QVector<QPair<QString, bool>>       boolUniforms;
	QVector<int>                        textureUnits;
};

const QMap<QString, ExtOverrideDef>& extensionOverrideDefs()
{
	// Build once, return by const ref.  Uses explicit qMakePair everywhere
	// to avoid MSVC brace-init ambiguity with QPair<QString,T> from const char*.
	static QMap<QString, ExtOverrideDef> defs;
	if (!defs.isEmpty())
		return defs;

	// Sheen
	{
		ExtOverrideDef d;
		d.floatUniforms << qMakePair(QString("pbrLighting.sheenRoughness"), 0.0f);
		d.vec3Uniforms  << qMakePair(QString("pbrLighting.sheenColor"),     QVector3D(0,0,0));
		d.textureUnits  << 26 << 27;
		defs["Sheen"] = d;
	}
	// Clearcoat
	{
		ExtOverrideDef d;
		d.floatUniforms << qMakePair(QString("pbrLighting.clearcoat"), 0.0f);
		d.textureUnits  << 18 << 19 << 20;
		defs["Clearcoat"] = d;
	}
	// Iridescence
	{
		ExtOverrideDef d;
		d.floatUniforms << qMakePair(QString("pbrLighting.iridescenceFactor"), 0.0f);
		d.textureUnits  << 24 << 25;
		defs["Iridescence"] = d;
	}
	// Volume / SSS
	{
		ExtOverrideDef d;
		d.floatUniforms << qMakePair(QString("pbrLighting.thicknessFactor"), 0.0f);
		d.textureUnits  << 30;
		defs["Volume / SSS"] = d;
	}
	// Specular
	{
		ExtOverrideDef d;
		d.floatUniforms << qMakePair(QString("pbrLighting.specularFactor"), 0.0f);
		d.textureUnits  << 21 << 22;
		defs["Specular"] = d;
	}
	// Anisotropy
	{
		ExtOverrideDef d;
		d.floatUniforms << qMakePair(QString("pbrLighting.anisotropyStrength"), 0.0f);
		d.textureUnits  << 23;
		defs["Anisotropy"] = d;
	}
	// Transmission
	{
		ExtOverrideDef d;
		d.floatUniforms << qMakePair(QString("pbrLighting.transmission"), 0.0f);
		d.textureUnits  << 28;
		defs["Transmission"] = d;
	}
	// Diffuse Transmission
	{
		ExtOverrideDef d;
		d.floatUniforms << qMakePair(QString("pbrLighting.diffuseTransmissionFactor"), 0.0f);
		d.textureUnits  << 34 << 35;
		defs["Diffuse Transmission"] = d;
	}
	// IOR — revert to glTF default (1.5) when disabled
	{
		ExtOverrideDef d;
		d.floatUniforms << qMakePair(QString("pbrLighting.ior"), 1.5f);
		d.textureUnits  << 29;
		defs["IOR"] = d;
	}
	// Emissive Strength — revert to neutral multiplier (1.0) when disabled
	{
		ExtOverrideDef d;
		d.floatUniforms << qMakePair(QString("pbrLighting.emissiveStrength"), 1.0f);
		defs["Emissive Strength"] = d;
	}
	// Dispersion — zero out chromatic dispersion when disabled
	{
		ExtOverrideDef d;
		d.floatUniforms << qMakePair(QString("pbrLighting.dispersion"), 0.0f);
		defs["Dispersion"] = d;
	}
	// Volume Scattering — disable the Burley SSS pass when disabled
	{
		ExtOverrideDef d;
		d.boolUniforms << qMakePair(QString("hasVolumeScattering"), false);
		defs["Volume Scattering"] = d;
	}
	return defs;
}
} // anonymous namespace

void ViewportWidget::setDebugExtensionEnabled(int meshId, const QString& extensionKey, bool enabled)
{
	if (meshId < 0 || meshId >= static_cast<int>(_sceneRuntime.meshStore().size()) || !_sceneRuntime.meshAt(meshId))
		return;

	SceneMesh* mesh = _sceneRuntime.meshAt(meshId);
	const auto& defs = extensionOverrideDefs();
	auto it = defs.constFind(extensionKey);
	if (it == defs.constEnd())
		return;

	const ExtOverrideDef& def = it.value();

	if (enabled)
	{
		// Remove overrides; force uniforms to re-run so originals are restored.
		for (const auto& kv : def.floatUniforms)
			mesh->clearDebugUniformOverride(kv.first);
		for (const auto& kv : def.vec3Uniforms)
			mesh->clearDebugUniformOverride(kv.first);
		for (const auto& kv : def.boolUniforms)
			mesh->clearDebugUniformOverride(kv.first);
		for (int unit : def.textureUnits)
			mesh->clearDebugTextureOverride(unit);
		mesh->markUniformsDirty();
	}
	else
	{
		// Suppress the extension's contribution via uniform overrides.
		for (const auto& kv : def.floatUniforms)
			mesh->setDebugUniformOverride(kv.first, QVariant::fromValue<float>(kv.second));
		for (const auto& kv : def.vec3Uniforms)
			mesh->setDebugUniformOverride(kv.first, QVariant::fromValue(kv.second));
		for (const auto& kv : def.boolUniforms)
			mesh->setDebugUniformOverride(kv.first, QVariant::fromValue<bool>(kv.second));
		// Neutral-bind the extension's texture units.
		for (int unit : def.textureUnits)
		{
			const bool isNormalUnit = (unit == 13 || unit == 20);
			mesh->setDebugTextureOverride(unit, isNormalUnit ? _renderCtrl.debugNormalTex() : _renderCtrl.debugNeutralTex());
		}
	}
	update();
}

void ViewportWidget::clearDebugExtensionOverrides(int meshId)
{
	if (meshId < 0 || meshId >= static_cast<int>(_sceneRuntime.meshStore().size()) || !_sceneRuntime.meshAt(meshId))
		return;

	SceneMesh* mesh = _sceneRuntime.meshAt(meshId);
	mesh->clearAllDebugUniformOverrides();
	mesh->markUniformsDirty();
	update();
}

// ---------------------------------------------------------------------------
// requestTextureReadback
// Reads back every per-mesh texture slot for the given _sceneRuntime.meshStore() index and
// emits textureReadbackReady() with one TextureSlotInfo per slot.
// Inactive slots (textureId == 0) are included with isActive = false and a
// null thumbnail so the debug panel can show a placeholder if desired.
// ---------------------------------------------------------------------------
void ViewportWidget::requestTextureReadback(int meshId)
{
	if (meshId < 0 || meshId >= static_cast<int>(_sceneRuntime.meshStore().size()) || !_sceneRuntime.meshAt(meshId))
	{
		emit textureReadbackReady({}, {});
		return;
	}

	makeCurrent();

	SceneMesh*    mesh    = _sceneRuntime.meshAt(meshId);
	const Material& mat    = mesh->getMaterial();
	const QString    meshName = mesh->getName();

	// baseColorTex mirrors the logic in RenderableMesh::setupTextures() so the
	// debug panel shows what is actually bound on unit 10.
	const GLuint baseColorTex = mat.hasAlbedoMap()
		? static_cast<GLuint>(mat.albedoTextureId())
		: (mat.hasDiffuseMap() ? static_cast<GLuint>(mat.diffuseTextureId()) : 0U);

	// Pre-compute extension active flags from the material.
	// These are true whenever the KHR extension is in use — even if no texture
	// is bound (e.g. sheen colour factor set but no sheen texture).
	// specularFactor defaults to 1.0 in glTF, so we consider KHR_materials_specular
	// active when it deviates from the default or a specular texture is present.
	const bool extSheen      = mat.hasSheen()
	                           || mat.hasSheenColorMap()
	                           || mat.hasSheenRoughnessMap();
	const bool extClearcoat  = mat.hasClearcoat()
	                           || mat.hasClearcoatColorMap()
	                           || mat.hasClearcoatRoughnessMap()
	                           || mat.hasClearcoatNormalMap();
	const bool extIridescence= mat.iridescenceFactor() > 0.0f
	                           || mat.hasIridescenceMap()
	                           || mat.hasIridescenceThicknessMap();
	const bool extVolume     = mat.hasVolumeScattering()
	                           || mat.thicknessFactor() > 0.0f
	                           || mat.hasThicknessMap();
	const bool extSpecular   = mat.hasSpecularFactorMap() || mat.hasSpecularColorMap()
	                           || mat.specularFactor() != 1.0f
	                           || mat.specularColorFactor() != QVector3D(1.0f, 1.0f, 1.0f);
	const bool extAnisotropy = mat.anisotropyStrength() != 0.0f || mat.hasAnisotropyMap();
	const bool extTransmission = mat.hasTransmission()
	                             || mat.hasTransmissionMap();
	const bool extDiffuseTrans = mat.diffuseTransmissionFactor() > 0.0f
	                             || mat.hasDiffuseTransmissionMap()
	                             || mat.hasDiffuseTransmissionColorMap();
	// IOR defaults to 1.5 for every material (glTF spec) so mat.ior() > 0 is
	// always true.  Use deviation from 1.5 (explicit extension value) or a
	// texture as the activity signal; scalar marker slot 203 carries this flag.
	const bool extIOR          = mat.hasIORMap();                      // real unit 29
	const bool extIORScalar    = (mat.ior() != 1.5f) || mat.hasIORMap(); // marker unit 203

	struct SlotDef
	{
		QString name;
		int     unit;
		GLuint  texId;
		bool    extEnabled;   // parent KHR extension is active (with or without texture)
	};

	const QVector<SlotDef> defs = {
		{ "albedo / diffuse",         10, baseColorTex,                                                                                    false          },
		{ "metallicMap",              11, mat.hasMetallicMap()            ? static_cast<GLuint>(mat.metallicTextureId())            : 0U,  false          },
		{ "emissiveMap",              12, mat.hasEmissiveMap()             ? static_cast<GLuint>(mat.emissiveTextureId())             : 0U, false          },
		{ "normalMap",                13, mat.hasNormalMap()               ? static_cast<GLuint>(mat.normalTextureId())               : 0U, false          },
		{ "heightMap",                14, mat.hasHeightMap()               ? static_cast<GLuint>(mat.heightTextureId())               : 0U, false          },
		{ "opacityMap",               15, mat.hasOpacityMap()              ? static_cast<GLuint>(mat.opacityTextureId())              : 0U, false          },
		{ "roughnessMap",             16, mat.hasRoughnessMap()            ? static_cast<GLuint>(mat.roughnessTextureId())            : 0U, false          },
		{ "aoMap",                    17, mat.hasAOMap()                   ? static_cast<GLuint>(mat.occlusionTextureId())            : 0U, false          },
		{ "clearcoatColorMap",        18, mat.hasClearcoatColorMap()       ? static_cast<GLuint>(mat.clearcoatColorTextureId())       : 0U, extClearcoat   },
		{ "clearcoatRoughnessMap",    19, mat.hasClearcoatRoughnessMap()   ? static_cast<GLuint>(mat.clearcoatRoughnessTextureId())   : 0U, extClearcoat   },
		{ "clearcoatNormalMap",       20, mat.hasClearcoatNormalMap()      ? static_cast<GLuint>(mat.clearcoatNormalTextureId())      : 0U, extClearcoat   },
		{ "specularFactorMap",        21, mat.hasSpecularFactorMap()       ? static_cast<GLuint>(mat.specularFactorTextureId())       : 0U, extSpecular    },
		{ "specularColorMap",         22, mat.hasSpecularColorMap()        ? static_cast<GLuint>(mat.specularColorTextureId())        : 0U, extSpecular    },
		{ "anisotropyMap",            23, mat.hasAnisotropyMap()           ? static_cast<GLuint>(mat.anisotropyTextureId())           : 0U, extAnisotropy  },
		{ "iridescenceMap",           24, mat.hasIridescenceMap()          ? static_cast<GLuint>(mat.iridescenceTextureId())          : 0U, extIridescence },
		{ "iridescenceThicknessMap",  25, mat.hasIridescenceThicknessMap() ? static_cast<GLuint>(mat.iridescenceThicknessTextureId()) : 0U, extIridescence },
		{ "sheenColorMap",            26, mat.hasSheenColorMap()           ? static_cast<GLuint>(mat.sheenColorTextureId())           : 0U, extSheen       },
		{ "sheenRoughnessMap",        27, mat.hasSheenRoughnessMap()       ? static_cast<GLuint>(mat.sheenRoughnessTextureId())       : 0U, extSheen       },
		{ "transmissionMap",          28, mat.hasTransmissionMap()         ? static_cast<GLuint>(mat.transmissionTextureId())         : 0U, extTransmission},
		{ "iorMap",                   29, mat.hasIORMap()                  ? static_cast<GLuint>(mat.iorTextureId())                  : 0U, extIOR         },
		{ "diffuseTransmissionMap",   34, mat.hasDiffuseTransmissionMap()  ? static_cast<GLuint>(mat.diffuseTransmissionTextureId())  : 0U, extDiffuseTrans},
		{ "diffuseTransmissionColor", 35, mat.hasDiffuseTransmissionColorMap() ? static_cast<GLuint>(mat.diffuseTransmissionColorTextureId()) : 0U, extDiffuseTrans},
		{ "thicknessMap",             30, mat.hasThicknessMap()            ? static_cast<GLuint>(mat.thicknessTextureId())            : 0U, extVolume      },
		// Scalar-activity markers (units 200-203): no real GL texture — used only
		// to drive the extension-panel activity dot for extensions that have no
		// dedicated texture slot.  isMarker is set to true in the loop below.
		{ "ior",                     203, 0U, extIORScalar                                       },
		{ "emissiveStrength",        200, 0U, mat.emissiveStrength() != 1.0f                     },
		{ "dispersion",              201, 0U, mat.dispersion() > 0.0f                            },
		{ "volumeScattering",        202, 0U, mat.hasVolumeScattering()                          },
	};

	constexpr int ThumbSize = 64;
	QVector<TextureSlotInfo> result;
	result.reserve(defs.size());

	for (const auto& d : defs)
	{
		TextureSlotInfo info;
		info.slotName         = d.name;
		info.unitIndex        = d.unit;
		info.textureId        = d.texId;
		info.isActive         = (d.texId != 0U);
		info.extensionEnabled = d.extEnabled;
		info.isMarker         = (d.unit >= 200);   // scalar-activity markers have no real GL unit

		if (info.isActive)
		{
			glBindTexture(GL_TEXTURE_2D, d.texId);
			GLint w = 0, h = 0;
			glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH,  &w);
			glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);

			if (w > 0 && h > 0)
			{
				QByteArray buf(w * h * 4, '\0');
				glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE,
				              reinterpret_cast<void*>(buf.data()));
				QImage img(reinterpret_cast<const uchar*>(buf.constData()),
				           w, h, w * 4, QImage::Format_RGBA8888);
				// Deep-copy before the buffer goes out of scope
				info.thumbnail = QPixmap::fromImage(
				    img.copy().scaled(ThumbSize, ThumbSize,
				                     Qt::KeepAspectRatio, Qt::SmoothTransformation));
			}
		}

		result.push_back(std::move(info));
	}

	doneCurrent();
	emit textureReadbackReady(result, meshName);
}
