#pragma once

#include <QWidget>
#include <QVariantMap>
#include <QToolButton>
#include <QAction>
#include <QPropertyAnimation>
#include <QHBoxLayout>
#include <QScrollArea>
#include "RenderEnums.h"

class FlyOutViewButton;

enum class CameraModeActions { ORBIT, FLY, FIRST_PERSON };
enum class NavigationActions { ROTATE, PAN, ZOOM };
enum class StandardViewActions { TOP, FRONT, LEFT, BOTTOM, REAR, RIGHT };
enum class ViewModeActions { ISOMETRIC, DIMETRIC, TRIMETRIC };
enum class DisplayModeActions { SHADED, HOLLOW_MESH, MESH_EDGES, WIREFRAME, SHADED_WITH_EDGES };
enum class RenderingModeActions { ADS, PBR, RAY_TRACED };
enum class ShadingNormalModeActions { SMOOTH, FLAT };
enum class DebugOverlayActions { BOUNDING_BOX, VERTEX_NORMALS, FACE_NORMALS };

class ViewToolbar : public QWidget
{
    Q_OBJECT

public:
    explicit ViewToolbar(QWidget* viewport, QWidget* parent = nullptr);
    QSize sizeHint() const override;
    void stopScrolling();

    bool isFlyoutMenuVisible() const;

    void setDefaultCameraModeAction(CameraModeActions mode);
    void setDefaultStandardViewAction(StandardViewActions view);
    void setDefaultViewModeAction(ViewModeActions mode);
    // Shows the axonometric type and compass corner on the two axonometric buttons; `active` is whether the
    // view currently IS an axonometric one (the buttons are highlighted only then).
    void setAxonometricState(ViewMode type, IsoCorner corner, bool active);
    void setDefaultDisplayModeAction(DisplayModeActions mode);
    void setDefaultRenderingModeAction(RenderingModeActions mode);
    void setFeatureEdgeModesVisible(bool visible);
    bool featureEdgeModesVisible() const { return _wireframe && _wireframe->isVisible(); }
    void syncMenuState(const QVariantMap& state);
    void setDebugOverlayModesAvailable(bool boundingBox, bool vertexNormals, bool faceNormals);
    void setDebugOverlayState(DebugOverlayActions mode, bool enabled);
    void updateRenderingModeButton(const QString& mode);
    void deactivateAllNavigationModes();

    void setRealisticChecked(bool checked); // syncs _realisticBtn checked state
    void setDefaultShadingNormalModeAction(ShadingNormalModeActions mode);
    void setSwapVisibleChecked(bool checked);
    void setSectionViewChecked(bool checked);
    // Passive sync of the Clipping Planes button's flyout to the panel's current
    // combination: shows the matching preset's icon on the button and checks the
    // matching flyout entry, or falls back to the generic clipping icon when no
    // plane and no box is enabled. Never emits clippingPresetRequested().
    // xy/yz/zx name the planes as the panel does (XY = the Z-normal plane, etc.).
    void setClippingState(bool xy, bool yz, bool zx, bool box);
    void setExplodedViewChecked(bool checked);
    void setCameraUpAxisZUp(bool zUp);
    bool isCameraUpAxisZUp() const;
    void setTurntableChecked(bool checked); // syncs _btnTurntable when stopped externally (e.g. manual camera interaction)
    void setLassoSelectChecked(bool checked); // syncs _btnLassoSelect when disarmed externally (e.g. another tool took over)
    void setSelectionFiltersEnabled(bool enabled);

signals:
    void viewActionsChanged();
    void cameraModeSelected(const QString& type);
    void cameraUpAxisToggled(bool zUp);
    void viewSelected(const QString& viewName);
    void axonometricSelected(const QString& type);
    // "SE", "NE", "NW", "SW" pick a compass corner; "Next" / "Prev" step around.
    void isoCornerSelected(const QString& corner);
    void displayModeSelected(const QString& type);
    void renderingModeSelected(const QString& mode);
    void shadingNormalModeSelected(const QString& mode);
    void projectionToggled(bool isOrtho);
    void fitToViewRequested();
    void zoomViewRequested();
    void panViewRequested();
    void rotateViewRequested();
    void windowZoomRequested();
    void multiViewToggled(bool enabled);
    void sectionViewToggled(bool enabled);
    // A clipping preset was picked from the Clipping Planes flyout: enable exactly
    // this combination of planes (or the box) and disable the rest. The main
    // button click is unchanged and still only shows/hides the panel
    // (sectionViewToggled).
    void clippingPresetRequested(bool xy, bool yz, bool zx, bool box);
    void explodedViewToggled(bool enabled);
    void swapVisibleToggled(bool enabled);
    void axisDisplayToggled(bool enabled);
    void debugOverlaySelected(const QString& overlayType);
    void debugOverlayToggled(bool enabled);
    void turntableToggled(bool enabled);
    void lassoSelectToggled(bool enabled);
    void selectionFilterRequested(const QString& filter);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    // Scopes a toolbar action's shortcut to "fires while the owning
    // explicit owning ViewportWidget, independent of the tab-page parent, or one of
    // its children has focus" - see the .cpp for why this needs both a
    // context change AND an explicit addAction() association, not just
    // setShortcutContext() alone.
    void scopeShortcutToViewport(QAction* action);
    // Same scoping, for buttons: QAbstractButton::setShortcut() has no
    // public API to change its context, so this replaces it with an
    // explicit QShortcut (same pattern already used for the Home shortcut).
    void scopeButtonShortcutToViewport(QAbstractButton* button, const QKeySequence& sequence);
    // The action owns state; existing viewport-scoped QShortcuts remain unchanged.
    QAction* bindButtonAction(QToolButton* button, const QString& name);
    void retranslateUI();
    void updateScrollButtons();
    void scrollLeft();
    void scrollRight();
    void checkScrollButtonsVisibility();
    void startAutoScroll(bool scrollLeft);
    void stopAutoScroll();
    void checkAndStartAutoScrollLeft();
    void checkAndStartAutoScrollRight();

private:
    QWidget* _viewport; // Shortcut owner; the widget parent is the stacked page container.
    // Scroll infrastructure
    QWidget* _buttonContainer;
    QScrollArea* _scrollArea;
    QHBoxLayout* _mainLayout;
    QToolButton* _scrollLeftBtn;
    QToolButton* _scrollRightBtn;
    bool _isRepositioning;
    QTimer* _autoScrollTimer;
    bool _autoScrollLeft;
    QTimer* _hoverDelayTimer;

    // Navigation buttons (Fit All and Window Zoom stay separate)
    QToolButton* _btnFitAll;
    QToolButton* _btnWindowZoom;
    QAction* _fitAllAction;
    QAction* _windowZoomAction;
    QAction* _lassoSelectAction;
    QAction* _filterByMaterialAction;
    QAction* _filterByColorAction;
    QAction* _filterByBoundingBoxAction;
    QAction* _turntableAction;
    QAction* _projectionAction;
    QAction* _multiViewAction;
    QAction* _realisticAction;
    QAction* _sectionAction;
    QAction* _explodedAction;
    QAction* _swapVisibleAction;
    QAction* _axisAction;

    // Navigation actions (Rotate, Pan, Zoom grouped in dropdown)
    QAction* _rotateViewAction;
    QAction* _panViewAction;
    QAction* _zoomViewAction;

    // Camera mode actions
    QAction* _orbitAction;
    QAction* _flyAction;
    QAction* _firstPersonAction;
    QAction* _cameraZUpAction;
    QAction* _cameraYUpAction;

    // View mode actions (grouped in dropdown menu)
    QAction* _topViewAction;
    QAction* _frontViewAction;
    QAction* _leftViewAction;
    QAction* _bottomViewAction;
    QAction* _rearViewAction;
    QAction* _rightViewAction;

    // Rendering mode actions
    QAction* _adsAction;
    QAction* _pbrAction;
    QAction* _rayTracedAction;

    // Axonometric actions
    QAction* _isoAction;
    QAction* _dimAction;
    QAction* _triAction;

    // Projection toggle
    QToolButton* _projToggleButton;

    // Multi view
    QToolButton* _multiBtn;

    // Standalone realism toggle (not part of the display mode group)
    QToolButton* _realisticBtn;

    // Display mode actions
    QAction* _shaded;
    QAction* _hollowMesh;       // all triangle edges (no fill)
    QAction* _meshEdges;        // shaded + all triangle edges
    QAction* _wireframe;        // true feature edges only
    QAction* _shadedWithEdges;  // shaded + true feature edges
    QAction* _flatshaded;

    // Debug overlay actions
    QAction* _boundingBoxOverlay;
    QAction* _vertexNormalsOverlay;
    QAction* _faceNormalsOverlay;

    // Clipping Planes flyout: one action per preset, in the fixed order of
    // kClippingPresets in the .cpp (No Clipping, XY, YZ, ZX, YZ+ZX, XY+ZX, XY+YZ,
    // XY+YZ+ZX, Box).
    QList<QAction*> _clippingPresetActions;
    int _currentClippingPreset = 0; // index into the presets; 0 = No Clipping

    // Other buttons
    QToolButton* _sectionBtn; // a FlyOutViewButton: main click toggles the panel, the flyout picks a preset
    QToolButton* _explodedBtn;
    QToolButton* _swapBtn;
    QToolButton* _axisBtn;
    QToolButton* _btnTurntable;
    QToolButton* _btnLassoSelect;

    // Flyout buttons and action maps
    FlyOutViewButton* _toolButtonCameraModes;
    QMap<CameraModeActions, QAction*> _cameraModeActions;
    FlyOutViewButton* _toolButtonCameraUpAxis;

    FlyOutViewButton* _toolButtonNavigation;
    QMap<NavigationActions, QAction*> _navigationActions;

    FlyOutViewButton* _toolButtonViews;
    QMap<StandardViewActions, QAction*> _standardViewActions;

    FlyOutViewButton* _toolButtonRenderingMode;
    QMap<RenderingModeActions, QAction*> _renderingModeActions;

    FlyOutViewButton* _toolButtonShadingNormal;
    QMap<ShadingNormalModeActions, QAction*> _shadingNormalActions;

    FlyOutViewButton* _toolButtonViewModes;
    // Compass corner of the axonometric views: click steps to the next corner, the flyout picks one.
    FlyOutViewButton* _toolButtonCorner;
    QAction* _cornerNextAction;                 // the button's default action; mirrors the current corner
    QMap<IsoCorner, QAction*> _cornerActions;   // the four flyout entries
    QAction* _axoStepAction;                    // the type button's default action: click steps to the next type
    ViewModeActions _currentViewModeAction = ViewModeActions::ISOMETRIC;   // type shown on the type button
    QAction* _perspectiveAction;                // projection flyout entries
    QAction* _orthographicAction;
    static QString cornerActionText(IsoCorner corner);
    QMap<ViewModeActions, QAction*> _viewModeActions;

    FlyOutViewButton* _toolButtonDisplayModes;
    QMap<DisplayModeActions, QAction*> _displayModeActions;
    FlyOutViewButton* _toolButtonDebugOverlays;
    QMap<DebugOverlayActions, QAction*> _debugOverlayActions;
    DebugOverlayActions _currentDebugOverlayAction = DebugOverlayActions::BOUNDING_BOX;

};
