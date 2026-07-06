#pragma once

#include <QWidget>
#include <QToolButton>
#include <QAction>
#include <QPropertyAnimation>
#include <QHBoxLayout>
#include <QScrollArea>

class FlyOutViewButton;

enum class CameraModeActions { ORBIT, FLY, FIRST_PERSON };
enum class NavigationActions { ROTATE, PAN, ZOOM };
enum class StandardViewActions { TOP, FRONT, LEFT, BOTTOM, REAR, RIGHT };
enum class ViewModeActions { ISOMETRIC, DIMETRIC, TRIMETRIC };
enum class DisplayModeActions { SHADED, HOLLOW_MESH, MESH_EDGES, WIREFRAME, SHADED_WITH_EDGES };
enum class RenderingModeActions { ADS, PBR, PATH_TRACED };
enum class ShadingNormalModeActions { SMOOTH, FLAT };
enum class DebugOverlayActions { BOUNDING_BOX, VERTEX_NORMALS, FACE_NORMALS };

class ViewToolbar : public QWidget
{
    Q_OBJECT

public:
    explicit ViewToolbar(QWidget* parent = nullptr);

    void showAnimated();
    void hideAnimated();
    QRect visibleRect() const;
    QRect hiddenRect() const;
    void reposition(int widgetWidth, int widgetHeight);
    bool isFlyoutMenuVisible() const;

    void setDefaultCameraModeAction(CameraModeActions mode);
    void setDefaultStandardViewAction(StandardViewActions view);
    void setDefaultViewModeAction(ViewModeActions mode);
    void setDefaultDisplayModeAction(DisplayModeActions mode);
    void setDefaultRenderingModeAction(RenderingModeActions mode);
    void setFeatureEdgeModesVisible(bool visible);
    void setDebugOverlayModesAvailable(bool boundingBox, bool vertexNormals, bool faceNormals);
    void setDebugOverlayState(DebugOverlayActions mode, bool enabled);
    void updateRenderingModeButton(const QString& mode);
    void deactivateAllNavigationModes();

    void setRealisticChecked(bool checked); // syncs _realisticBtn checked state
    void setDefaultShadingNormalModeAction(ShadingNormalModeActions mode);
    void setSwapVisibleChecked(bool checked);
    void setSectionViewChecked(bool checked);
    void setExplodedViewChecked(bool checked);
    void setCameraUpAxisZUp(bool zUp);
    bool isCameraUpAxisZUp() const;

signals:
    void cameraModeSelected(const QString& type);
    void cameraUpAxisToggled(bool zUp);
    void viewSelected(const QString& viewName);
    void axonometricSelected(const QString& type);
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
    void explodedViewToggled(bool enabled);
    void swapVisibleToggled(bool enabled);
    void axisDisplayToggled(bool enabled);
    void debugOverlaySelected(const QString& overlayType);
    void debugOverlayToggled(bool enabled);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
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
    QAction* _pathTracedAction;

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
    QAction* _realistic;
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

    // Other buttons
    QToolButton* _sectionBtn;
    QToolButton* _explodedBtn;
    QToolButton* _swapBtn;
    QToolButton* _axisBtn;

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
    QMap<ViewModeActions, QAction*> _viewModeActions;

    FlyOutViewButton* _toolButtonDisplayModes;
    QMap<DisplayModeActions, QAction*> _displayModeActions;
    FlyOutViewButton* _toolButtonDebugOverlays;
    QMap<DebugOverlayActions, QAction*> _debugOverlayActions;
    DebugOverlayActions _currentDebugOverlayAction = DebugOverlayActions::BOUNDING_BOX;

    // Animation
    QPropertyAnimation* _toolbarAnimation;
    QRect _visibleRect;
    QRect _hiddenRect;
};
