
#include "ViewToolbar.h"
#include "FlyOutViewButton.h"
#include "LanguageManager.h"
#include <QHBoxLayout>
#include <QToolButton>
#include <QMenu>
#include <QAction>
#include <QPushButton>
#include <QButtonGroup>
#include <QPainter>
#include <QShortcut>
#include <QScrollBar>
#include <QTimer>

namespace
{
QString debugOverlayIconPath(DebugOverlayActions action, bool enabled)
{
    switch (action)
    {
    case DebugOverlayActions::BOUNDING_BOX:
        return enabled ? QStringLiteral(":/icons/res/show_bounding_box.png")
                       : QStringLiteral(":/icons/res/hide_bounding_box.png");
    case DebugOverlayActions::VERTEX_NORMALS:
        return enabled ? QStringLiteral(":/icons/res/showVertexNormal.png")
                       : QStringLiteral(":/icons/res/hideVertexNormal.png");
    case DebugOverlayActions::FACE_NORMALS:
        return enabled ? QStringLiteral(":/icons/res/showFaceNormal.png")
                       : QStringLiteral(":/icons/res/hideFaceNormal.png");
    }

    return QStringLiteral(":/icons/res/hide_bounding_box.png");
}
}

ViewToolbar::ViewToolbar(QWidget* parent)
    : QWidget(parent)
    , _isRepositioning(false)
    , _autoScrollTimer(nullptr)
    , _hoverDelayTimer(nullptr)
    , _autoScrollLeft(true)
{
    setStyleSheet("background: rgba(255, 255, 255, 100); border: 1px solid gray; border-radius: 4px;");
    setFixedHeight(76);

    QString buttonStyleSheet(
        "QToolButton {"
        "    border: none;"
        "    background: transparent;"
        "    padding: 5px;"
        "    border-radius: 4px;"
        "}"
        "QToolButton:hover {"
        "    background-color: rgba(0, 120, 215, 50);"
        "    border: 1px solid #0078D7;"
        "}"
        "QToolButton:pressed {"
        "    background-color: rgba(0, 120, 215, 100);"
        "    border: 1px solid #005A9E;"
        "}"
        "QToolButton:checked {"
        "    background-color: rgba(0, 150, 100, 100);"
        "    border: 1px solid #008000;"
        "    color: white;"
        "}"
    );

    QString scrollButtonStyleSheet(
        "QToolButton {"
        "    border: none;"
        "    background: rgba(100, 100, 100, 150);"
        "    padding: 2px;"
        "    border-radius: 4px;"
        "    min-width: 20px;"
        "    max-width: 20px;"
        "}"
        "QToolButton:hover {"
        "    background-color: rgba(0, 120, 215, 180);"
        "}"
        "QToolButton:pressed {"
        "    background-color: rgba(0, 120, 215, 220);"
        "}"
    );

    QString flyoutStyleSheet(
        "QMenu {"
        "    background-color: rgba(255, 255, 255, 100);"
        "    border: 1px solid gray;"
        "    border-radius: 4px;"
        "    padding: 2px;"
        "    icon-size: 42px;"
        "}"
        "QMenu::item {"
        "    background: transparent;"
        "    background-color: #f0f0f0;"
        "    border: 1px solid #c0c0c0;"
        "    border-radius: 4px;"
        "    padding: 5px 8px;"
        "    margin: 3px;"
        "    min-width: 120px;"
        "    min-height: 30px;"
        "    font-weight: normal;"
        "    color: black;"
        "}"
        "QMenu::item:selected {"
        "    background-color: #e0e0ff;"
        "    border: 1px solid #a0a0ff;"
        "    color: black;"
        "}"
        "QMenu::item:pressed {"
        "    background-color: #d0d0ff;"
        "    border: 1px solid #8080ff;"
        "    color: black;"
        "}"
        "QMenu::icon {"
        "    padding-left: 10px;"
        "    padding-right: 8px;"
        "}"
        "QMenu::separator {"
        "    height: 1px;"
        "    background-color: #c0c0c0;"
        "    margin: 4px 8px;"
        "}"
    );

    QString flyoutToggleButtonStyleSheet(
        "QToolButton {"
        "    border: none;"
        "    background: transparent;"
        "    padding: 5px;"
        "    border-radius: 4px;"
        "}"
        "QToolButton:hover {"
        "    background-color: rgba(0, 120, 215, 50);"
        "    border: 1px solid #0078D7;"
        "}"
        "QToolButton:pressed {"
        "    background-color: rgba(0, 120, 215, 100);"
        "    border: 1px solid #005A9E;"
        "}"
        "QToolButton:checked {"
        "    background-color: rgba(0, 150, 100, 100);"
        "    border: 1px solid #008000;"
        "    color: white;"
        "}"
        "QToolButton::menu-indicator {"
        "    image: none;"
        "    width: 0px;"
        "    height: 0px;"
        "}"
    );

    // Main layout for the entire toolbar
    QHBoxLayout* outerLayout = new QHBoxLayout(this);
    outerLayout->setContentsMargins(2, 2, 2, 2);
    outerLayout->setSpacing(0);

    // Left scroll button
    _scrollLeftBtn = new QToolButton(this);
    _scrollLeftBtn->setStyleSheet(scrollButtonStyleSheet);
    _scrollLeftBtn->setText("<");
    _scrollLeftBtn->setFixedSize(20, 68);
    _scrollLeftBtn->setVisible(false);
    _scrollLeftBtn->installEventFilter(this);
    outerLayout->addWidget(_scrollLeftBtn);
    connect(_scrollLeftBtn, &QToolButton::clicked, this, &ViewToolbar::scrollLeft);

    // Create scroll area
    _scrollArea = new QScrollArea(this);
    _scrollArea->setFrameShape(QFrame::NoFrame);
    _scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _scrollArea->setWidgetResizable(false);
    _scrollArea->setFixedHeight(72);
    _scrollArea->setStyleSheet("QScrollArea { background: transparent; border: none; }");
    outerLayout->addWidget(_scrollArea, 1);

    // Container widget for buttons inside scroll area
    _buttonContainer = new QWidget();
    _buttonContainer->setStyleSheet("background: transparent;");
    _buttonContainer->setFixedHeight(72);
    _mainLayout = new QHBoxLayout(_buttonContainer);
    _mainLayout->setContentsMargins(4, 4, 4, 4);
    _mainLayout->setSpacing(6);

    _scrollArea->setWidget(_buttonContainer);

    // Right scroll button
    _scrollRightBtn = new QToolButton(this);
    _scrollRightBtn->setStyleSheet(scrollButtonStyleSheet);
    _scrollRightBtn->setText(">");
    _scrollRightBtn->setFixedSize(20, 68);
    _scrollRightBtn->setVisible(false);
    _scrollRightBtn->installEventFilter(this);
    outerLayout->addWidget(_scrollRightBtn);
    connect(_scrollRightBtn, &QToolButton::clicked, this, &ViewToolbar::scrollRight);

    // Connect scroll area scrollbar to update button states
    connect(_scrollArea->horizontalScrollBar(), &QScrollBar::valueChanged,
        this, &ViewToolbar::updateScrollButtons);

    // Now add all the toolbar buttons to _mainLayout
    // (Keep all your existing button creation code here, just replace 'layout' with '_mainLayout')

    // Navigation - Rotate, Pan, Zoom grouped in dropdown
    _toolButtonNavigation = new FlyOutViewButton(this);
    _toolButtonNavigation->setIcon(QIcon(":/icons/res/rotateview.png"));
    _toolButtonNavigation->setIconSize(QSize(48, 48));
    _toolButtonNavigation->setToolTip(tr("Navigation"));
    _toolButtonNavigation->setPopupMode(QToolButton::DelayedPopup);
    _toolButtonNavigation->setAutoRaise(true);
    _mainLayout->addWidget(_toolButtonNavigation);

    QMenu* navigationMenu = new QMenu;
    navigationMenu->setStyleSheet(flyoutStyleSheet);
    _rotateViewAction = navigationMenu->addAction(QIcon(":/icons/res/rotateview.png"), tr("Rotate View"));
    _rotateViewAction->setCheckable(true);
	_rotateViewAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_R));
    _panViewAction = navigationMenu->addAction(QIcon(":/icons/res/panview.png"), tr("Pan View"));
    _panViewAction->setCheckable(true);
    _panViewAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_P));
    _zoomViewAction = navigationMenu->addAction(QIcon(":/icons/res/zoomview.png"), tr("Zoom View"));
    _zoomViewAction->setCheckable(true);
    _zoomViewAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Z));

    connect(_rotateViewAction, &QAction::triggered, this,
        [this]() {
            // Uncheck other navigation modes
            _panViewAction->setChecked(false);
            _zoomViewAction->setChecked(false);
            _rotateViewAction->setChecked(true);
            _toolButtonNavigation->setDefaultAction(_rotateViewAction);
            emit rotateViewRequested();
        }
    );

    connect(_panViewAction, &QAction::triggered, this,
        [this]() {
            // Uncheck other navigation modes
            _rotateViewAction->setChecked(false);
            _zoomViewAction->setChecked(false);
            _panViewAction->setChecked(true);
            _toolButtonNavigation->setDefaultAction(_panViewAction);
            emit panViewRequested();
        }
    );

    connect(_zoomViewAction, &QAction::triggered, this,
        [this]() {
            // Uncheck other navigation modes
            _rotateViewAction->setChecked(false);
            _panViewAction->setChecked(false);
            _zoomViewAction->setChecked(true);
            _toolButtonNavigation->setDefaultAction(_zoomViewAction);
            emit zoomViewRequested();
        }
    );

    _navigationActions[NavigationActions::ROTATE] = _rotateViewAction;
    _navigationActions[NavigationActions::PAN] = _panViewAction;
    _navigationActions[NavigationActions::ZOOM] = _zoomViewAction;

    _toolButtonNavigation->setMenu(navigationMenu);
    _toolButtonNavigation->setDefaultAction(_rotateViewAction);  // Default to Rotate

    // Separate navigation buttons
    _btnFitAll = new QToolButton(this);
    _btnFitAll->setStyleSheet(buttonStyleSheet);
    _btnFitAll->setIcon(QIcon(":/icons/res/fit-all.png"));
    _btnFitAll->setIconSize(QSize(48, 48));
    _btnFitAll->setToolTip(tr("Fit All"));
    _btnFitAll->setShortcut(QKeySequence(Qt::Key_F));
    _btnFitAll->setAutoRaise(true);
    _mainLayout->addWidget(_btnFitAll);
    connect(_btnFitAll, &QToolButton::clicked, this, [this]() { emit fitToViewRequested(); });

    _btnWindowZoom = new QToolButton(this);
    _btnWindowZoom->setStyleSheet(buttonStyleSheet);
    _btnWindowZoom->setIcon(QIcon(":/icons/res/window-zoom.png"));
    _btnWindowZoom->setIconSize(QSize(48, 48));
    _btnWindowZoom->setToolTip(tr("Window Zoom"));
    _btnWindowZoom->setShortcut(QKeySequence(Qt::ALT | Qt::Key_W));
    _btnWindowZoom->setAutoRaise(true);
    _mainLayout->addWidget(_btnWindowZoom);
    connect(_btnWindowZoom, &QToolButton::clicked, this, [this]() { emit windowZoomRequested(); });

    // Camera Modes
    _toolButtonCameraModes = new FlyOutViewButton(this);
    _toolButtonCameraModes->setIcon(QIcon(":/icons/res/camera_orbit.png"));
    _toolButtonCameraModes->setIconSize(QSize(48, 48));
    _toolButtonCameraModes->setToolTip(tr("Camera Modes"));
    _toolButtonCameraModes->setPopupMode(QToolButton::DelayedPopup);
    _toolButtonCameraModes->setAutoRaise(true);
    _mainLayout->addWidget(_toolButtonCameraModes);

    QMenu* camModeMenu = new QMenu;
    camModeMenu->setStyleSheet(flyoutStyleSheet);
    _orbitAction = camModeMenu->addAction(QIcon(":/icons/res/camera_orbit.png"), tr("Orbit"));
    _orbitAction->setShortcut(QKeySequence(Qt::Key_1));
    _flyAction = camModeMenu->addAction(QIcon(":/icons/res/camera_fly.png"), tr("Fly"));
    _flyAction->setShortcut(QKeySequence(Qt::Key_2));
    _firstPersonAction = camModeMenu->addAction(QIcon(":/icons/res/camera_first_person.png"), tr("First Person"));
    _firstPersonAction->setShortcut(QKeySequence(Qt::Key_3));

    connect(_orbitAction, &QAction::triggered, this,
        [this]() {
            _toolButtonCameraModes->setDefaultAction(_orbitAction);
            emit cameraModeSelected("Orbit");
        }
    );

    connect(_flyAction, &QAction::triggered, this,
        [this]() {
            _toolButtonCameraModes->setDefaultAction(_flyAction);
            emit cameraModeSelected("Fly");
        }
    );

    connect(_firstPersonAction, &QAction::triggered, this,
        [this]() {
            _toolButtonCameraModes->setDefaultAction(_firstPersonAction);
            emit cameraModeSelected("First Person");
        }
    );

    _cameraModeActions[CameraModeActions::ORBIT] = _orbitAction;
    _cameraModeActions[CameraModeActions::FLY] = _flyAction;
    _cameraModeActions[CameraModeActions::FIRST_PERSON] = _firstPersonAction;

    _toolButtonCameraModes->setMenu(camModeMenu);
    _toolButtonCameraModes->setDefaultAction(_orbitAction);

    _toolButtonCameraUpAxis = new FlyOutViewButton(this);
    _toolButtonCameraUpAxis->setIcon(QIcon(":/icons/res/camera_z_up.png"));
    _toolButtonCameraUpAxis->setIconSize(QSize(48, 48));
    _toolButtonCameraUpAxis->setToolTip(tr("Camera Up Axis"));
    _toolButtonCameraUpAxis->setPopupMode(QToolButton::DelayedPopup);
    _toolButtonCameraUpAxis->setAutoRaise(true);
    _mainLayout->addWidget(_toolButtonCameraUpAxis);

    QMenu* cameraUpAxisMenu = new QMenu;
    cameraUpAxisMenu->setStyleSheet(flyoutStyleSheet);
    _cameraZUpAction = cameraUpAxisMenu->addAction(QIcon(":/icons/res/camera_z_up.png"), tr("Z-Up"));
    _cameraYUpAction = cameraUpAxisMenu->addAction(QIcon(":/icons/res/camera_y_up.png"), tr("Y-Up"));

    connect(_cameraZUpAction, &QAction::triggered, this, [this]() {
        _toolButtonCameraUpAxis->setDefaultAction(_cameraZUpAction);
        emit cameraUpAxisToggled(true);
    });

    connect(_cameraYUpAction, &QAction::triggered, this, [this]() {
        _toolButtonCameraUpAxis->setDefaultAction(_cameraYUpAction);
        emit cameraUpAxisToggled(false);
    });

    _toolButtonCameraUpAxis->setMenu(cameraUpAxisMenu);
    _toolButtonCameraUpAxis->setDefaultAction(_cameraZUpAction);

    // Standard Views
    _toolButtonViews = new FlyOutViewButton(this);
    _toolButtonViews->setIcon(QIcon(":/icons/res/top.png"));
    _toolButtonViews->setIconSize(QSize(48, 48));
    _toolButtonViews->setToolTip(tr("Standard Views"));
    _toolButtonViews->setPopupMode(QToolButton::DelayedPopup);
    _toolButtonViews->setAutoRaise(true);
    _mainLayout->addWidget(_toolButtonViews);

    QMenu* viewsMenu = new QMenu;
    viewsMenu->setStyleSheet(flyoutStyleSheet);
    _topViewAction = viewsMenu->addAction(QIcon(":/icons/res/top.png"), tr("Top"));
    _topViewAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    _frontViewAction = viewsMenu->addAction(QIcon(":/icons/res/front.png"), tr("Front"));
    _frontViewAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F));
    _leftViewAction = viewsMenu->addAction(QIcon(":/icons/res/left.png"), tr("Left"));
    _leftViewAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    _bottomViewAction = viewsMenu->addAction(QIcon(":/icons/res/bottom.png"), tr("Bottom"));
    _bottomViewAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    _rearViewAction = viewsMenu->addAction(QIcon(":/icons/res/back.png"), tr("Rear"));
    _rearViewAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    _rightViewAction = viewsMenu->addAction(QIcon(":/icons/res/right.png"), tr("Right"));
    _rightViewAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_J));

    connect(_topViewAction, &QAction::triggered, this,
        [this]() {
            _toolButtonViews->setDefaultAction(_topViewAction);
            emit viewSelected("Top");
        }
    );

    connect(_frontViewAction, &QAction::triggered, this,
        [this]() {
            _toolButtonViews->setDefaultAction(_frontViewAction);
            emit viewSelected("Front");
        }
    );

    connect(_leftViewAction, &QAction::triggered, this,
        [this]() {
            _toolButtonViews->setDefaultAction(_leftViewAction);
            emit viewSelected("Left");
        }
    );

    connect(_bottomViewAction, &QAction::triggered, this,
        [this]() {
            _toolButtonViews->setDefaultAction(_bottomViewAction);
            emit viewSelected("Bottom");
        }
    );

    connect(_rearViewAction, &QAction::triggered, this,
        [this]() {
            _toolButtonViews->setDefaultAction(_rearViewAction);
            emit viewSelected("Rear");
        }
    );

    connect(_rightViewAction, &QAction::triggered, this,
        [this]() {
            _toolButtonViews->setDefaultAction(_rightViewAction);
            emit viewSelected("Right");
        }
    );

    _standardViewActions[StandardViewActions::TOP] = _topViewAction;
    _standardViewActions[StandardViewActions::FRONT] = _frontViewAction;
    _standardViewActions[StandardViewActions::LEFT] = _leftViewAction;
    _standardViewActions[StandardViewActions::BOTTOM] = _bottomViewAction;
    _standardViewActions[StandardViewActions::REAR] = _rearViewAction;
    _standardViewActions[StandardViewActions::RIGHT] = _rightViewAction;

    _toolButtonViews->setMenu(viewsMenu);
    _toolButtonViews->setDefaultAction(_topViewAction);

    // Isometric Views
    _toolButtonViewModes = new FlyOutViewButton(this);
    _toolButtonViewModes->setIcon(QIcon(":/icons/res/isometric.png"));
    _toolButtonViewModes->setIconSize(QSize(48, 48));
    _toolButtonViewModes->setToolTip(tr("Axonometric View"));
    _toolButtonViewModes->setPopupMode(QToolButton::DelayedPopup);
    _toolButtonViewModes->setAutoRaise(true);

    QShortcut* defaultShortcut = new QShortcut(QKeySequence(Qt::Key_Home), this);
    connect(defaultShortcut, &QShortcut::activated, _toolButtonViewModes, &QToolButton::click);

    _mainLayout->addWidget(_toolButtonViewModes);

    QMenu* axoMenu = new QMenu;
    axoMenu->setStyleSheet(flyoutStyleSheet);
    _isoAction = axoMenu->addAction(QIcon(":/icons/res/isometric.png"), tr("Isometric"));
    _isoAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    _dimAction = axoMenu->addAction(QIcon(":/icons/res/dimetric.png"), tr("Dimetric"));
    _dimAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_2));
    _triAction = axoMenu->addAction(QIcon(":/icons/res/trimetric.png"), tr("Trimetric"));
    _triAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_3));

    connect(_isoAction, &QAction::triggered, this,
        [this]() {
            _toolButtonViewModes->setDefaultAction(_isoAction);
            // Reset standard views to Top when switching to axonometric
            _toolButtonViews->setDefaultAction(_topViewAction);
            emit axonometricSelected("Isometric");
        }
    );

    connect(_dimAction, &QAction::triggered, this,
        [this]() {
            _toolButtonViewModes->setDefaultAction(_dimAction);
            // Reset standard views to Top when switching to axonometric
            _toolButtonViews->setDefaultAction(_topViewAction);
            emit axonometricSelected("Dimetric");
        }
    );

    connect(_triAction, &QAction::triggered, this,
        [this]() {
            _toolButtonViewModes->setDefaultAction(_triAction);
            // Reset standard views to Top when switching to axonometric
            _toolButtonViews->setDefaultAction(_topViewAction);
            emit axonometricSelected("Trimetric");
        }
    );

    _viewModeActions[ViewModeActions::ISOMETRIC] = _isoAction;
    _viewModeActions[ViewModeActions::DIMETRIC] = _dimAction;
    _viewModeActions[ViewModeActions::TRIMETRIC] = _triAction;

    _toolButtonViewModes->setMenu(axoMenu);
    _toolButtonViewModes->setDefaultAction(_isoAction);

    // Ortho/Perspective Projections
    _projToggleButton = new QToolButton(this);
    _projToggleButton->setStyleSheet(buttonStyleSheet);
    _projToggleButton->setCheckable(true);
    _projToggleButton->setChecked(false);
    _projToggleButton->setIcon(QIcon(":/icons/res/Ortho.png"));
    _projToggleButton->setIconSize(QSize(48, 48));
    _projToggleButton->setToolTip(tr("Toggle Projection"));
    _projToggleButton->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_P));
    _mainLayout->addWidget(_projToggleButton);

    connect(_projToggleButton, &QToolButton::toggled, this, [this](bool checked) {
        if (!checked)
        {
            _projToggleButton->setIcon(QIcon(":/icons/res/Ortho.png"));
            _projToggleButton->setToolTip(tr("Switch to Perspective"));
        }
        else
        {
            _projToggleButton->setToolTip(tr("Switch to Orthographic"));
            _projToggleButton->setIcon(QIcon(":/icons/res/Perspective.png"));
        }
        emit projectionToggled(!checked);
        });

    // Multi View
    _multiBtn = new QToolButton(this);
    _multiBtn->setStyleSheet(buttonStyleSheet);
    _multiBtn->setIcon(QIcon(":/icons/res/multiview.png"));
    _multiBtn->setIconSize(QSize(48, 48));
    _multiBtn->setToolTip(tr("Toggle Multi-View"));
    _multiBtn->setCheckable(true);
    _multiBtn->setAutoRaise(true);
    _multiBtn->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
    _mainLayout->addWidget(_multiBtn);
    connect(_multiBtn, &QToolButton::toggled, this, [this](bool checked) { emit multiViewToggled(checked); });

    // Display Modes
    _toolButtonDisplayModes = new FlyOutViewButton(this);
    _toolButtonDisplayModes->setIcon(QIcon(":/icons/res/shaded.png"));
    _toolButtonDisplayModes->setIconSize(QSize(48, 48));
    _toolButtonDisplayModes->setToolTip(tr("Display Modes"));
    _toolButtonDisplayModes->setPopupMode(QToolButton::DelayedPopup);
    _toolButtonDisplayModes->setAutoRaise(true);
    _mainLayout->addWidget(_toolButtonDisplayModes);

    // Realistic: standalone checkable toggle button (orthogonal to display mode)
    _realisticBtn = new QToolButton(this);
    _realisticBtn->setStyleSheet(buttonStyleSheet);
    _realisticBtn->setIcon(QIcon(":/icons/res/realshaded.png"));
    _realisticBtn->setIconSize(QSize(48, 48));
    _realisticBtn->setToolTip(tr("Realistic Rendering (Shift+R)"));
    _realisticBtn->setCheckable(true);
    _realisticBtn->setAutoRaise(true);
    _realisticBtn->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_R));
    _mainLayout->addWidget(_realisticBtn);
    connect(_realisticBtn, &QToolButton::clicked, this,
        [this]() { emit displayModeSelected("Realistic"); });
    _realistic = nullptr; // no longer a menu action; _realisticBtn is the sole owner

    QMenu* dispModeMenu = new QMenu;
    dispModeMenu->setStyleSheet(flyoutStyleSheet);
    _shaded = dispModeMenu->addAction(QIcon(":/icons/res/shaded.png"), tr("Shaded"));
    _shaded->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_S));
    _hollowMesh = dispModeMenu->addAction(QIcon(":/icons/res/hollow_mesh.png"), tr("Hollow Mesh"));
    _hollowMesh->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_H));
    _meshEdges = dispModeMenu->addAction(QIcon(":/icons/res/mesh_edges.png"), tr("Mesh Edges"));
    _meshEdges->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_M));
    _wireframe = dispModeMenu->addAction(QIcon(":/icons/res/wireframe.png"), tr("Wireframe"));
    _wireframe->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_W));
    _shadedWithEdges = dispModeMenu->addAction(QIcon(":/icons/res/wireshaded.png"), tr("Shaded with Edges"));
    _shadedWithEdges->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_E));

    connect(_shaded, &QAction::triggered, this,
        [this]() {
            _toolButtonDisplayModes->setDefaultAction(_shaded);
            emit displayModeSelected("Shaded");
        }
    );

    connect(_hollowMesh, &QAction::triggered, this,
        [this]() {
            _toolButtonDisplayModes->setDefaultAction(_hollowMesh);
            emit displayModeSelected("HollowMesh");
        }
    );

    connect(_meshEdges, &QAction::triggered, this,
        [this]() {
            _toolButtonDisplayModes->setDefaultAction(_meshEdges);
            emit displayModeSelected("MeshEdges");
        }
    );

    connect(_wireframe, &QAction::triggered, this,
        [this]() {
            _toolButtonDisplayModes->setDefaultAction(_wireframe);
            emit displayModeSelected("Wireframe");
        }
    );

    connect(_shadedWithEdges, &QAction::triggered, this,
        [this]() {
            _toolButtonDisplayModes->setDefaultAction(_shadedWithEdges);
            emit displayModeSelected("ShadedWithEdges");
        }
    );

    _displayModeActions[DisplayModeActions::SHADED]           = _shaded;
    _displayModeActions[DisplayModeActions::HOLLOW_MESH]      = _hollowMesh;
    _displayModeActions[DisplayModeActions::MESH_EDGES]       = _meshEdges;
    _displayModeActions[DisplayModeActions::WIREFRAME]        = _wireframe;
    _displayModeActions[DisplayModeActions::SHADED_WITH_EDGES] = _shadedWithEdges;

    _toolButtonDisplayModes->setMenu(dispModeMenu);
    _toolButtonDisplayModes->setDefaultAction(_shaded);

    // Rendering Mode
    _toolButtonRenderingMode = new FlyOutViewButton(this);
    _toolButtonRenderingMode->setIcon(QIcon(":/icons/res/ads_mode.png"));
    _toolButtonRenderingMode->setIconSize(QSize(48, 48));
    _toolButtonRenderingMode->setToolTip(tr("Rendering Mode"));
    _toolButtonRenderingMode->setPopupMode(QToolButton::DelayedPopup);
    _toolButtonRenderingMode->setAutoRaise(true);
    _mainLayout->addWidget(_toolButtonRenderingMode);

    QMenu* renderingModeMenu = new QMenu;
    renderingModeMenu->setStyleSheet(flyoutStyleSheet);
    _adsAction = renderingModeMenu->addAction(QIcon(":/icons/res/ads_mode.png"), tr("ADS (Blinn-Phong)"));
    _pbrAction = renderingModeMenu->addAction(QIcon(":/icons/res/pbr_mode.png"), tr("PBR (Metallic-Roughness)"));
    _pathTracedAction = renderingModeMenu->addAction(QIcon(":/icons/res/path_tracing_mode.png"), tr("Path Traced"));

    connect(_adsAction, &QAction::triggered, this,
        [this]() {
            _toolButtonRenderingMode->setDefaultAction(_adsAction);
            emit renderingModeSelected("ADS");
        }
    );

    connect(_pbrAction, &QAction::triggered, this,
        [this]() {
            _toolButtonRenderingMode->setDefaultAction(_pbrAction);
            emit renderingModeSelected("PBR");
        }
    );

    connect(_pathTracedAction, &QAction::triggered, this,
        [this]() {
            _toolButtonRenderingMode->setDefaultAction(_pathTracedAction);
            emit renderingModeSelected("PathTraced");
        }
    );

    _renderingModeActions[RenderingModeActions::ADS] = _adsAction;
    _renderingModeActions[RenderingModeActions::PBR] = _pbrAction;
    _renderingModeActions[RenderingModeActions::PATH_TRACED] = _pathTracedAction;

    _toolButtonRenderingMode->setMenu(renderingModeMenu);
    _toolButtonRenderingMode->setDefaultAction(_adsAction);  // Default to ADS

    // Shading Normal Mode (Smooth / Flat)
    _toolButtonShadingNormal = new FlyOutViewButton(this);
    _toolButtonShadingNormal->setIcon(QIcon(":/icons/res/smooth_shaded.png"));
    _toolButtonShadingNormal->setIconSize(QSize(48, 48));
    _toolButtonShadingNormal->setToolTip(tr("Shading Normal"));
    _toolButtonShadingNormal->setPopupMode(QToolButton::DelayedPopup);
    _toolButtonShadingNormal->setAutoRaise(true);
    _mainLayout->addWidget(_toolButtonShadingNormal);

    QMenu* shadingNormalMenu = new QMenu;
    shadingNormalMenu->setStyleSheet(flyoutStyleSheet);
    _flatshaded = shadingNormalMenu->addAction(QIcon(":/icons/res/flat_shaded.png"), tr("Flat Shaded"));
    _flatshaded->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F));
    QAction* _smoothShaded = shadingNormalMenu->addAction(QIcon(":/icons/res/smooth_shaded.png"), tr("Smooth Shaded"));
    _smoothShaded->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_G));

    connect(_flatshaded, &QAction::triggered, this,
        [this]() {
            _toolButtonShadingNormal->setDefaultAction(_flatshaded);
            emit shadingNormalModeSelected("Flat");
        });
    connect(_smoothShaded, &QAction::triggered, this,
        [this, _smoothShaded]() {
            _toolButtonShadingNormal->setDefaultAction(_smoothShaded);
            emit shadingNormalModeSelected("Smooth");
        });

    _shadingNormalActions[ShadingNormalModeActions::FLAT]   = _flatshaded;
    _shadingNormalActions[ShadingNormalModeActions::SMOOTH] = _smoothShaded;

    _toolButtonShadingNormal->setMenu(shadingNormalMenu);
    _toolButtonShadingNormal->setDefaultAction(_smoothShaded);

    // Section View
    _sectionBtn = new QToolButton(this);
    _sectionBtn->setStyleSheet(buttonStyleSheet);
    _sectionBtn->setIcon(QIcon(":/icons/res/section.png"));
    _sectionBtn->setIconSize(QSize(48, 48));
    _sectionBtn->setToolTip(tr("Clipping Planes"));
    _sectionBtn->setCheckable(true);
    _sectionBtn->setAutoRaise(true);
    _mainLayout->addWidget(_sectionBtn);
    connect(_sectionBtn, &QToolButton::toggled, this, [this](bool checked) { emit sectionViewToggled(checked); });

    // Exploded View
    _explodedBtn = new QToolButton(this);
    _explodedBtn->setStyleSheet(buttonStyleSheet);
    _explodedBtn->setIcon(QIcon(":/icons/res/exploded_view.png"));
    _explodedBtn->setIconSize(QSize(48, 48));
    _explodedBtn->setToolTip(tr("Exploded View"));
    _explodedBtn->setCheckable(true);
    _explodedBtn->setAutoRaise(true);
    _mainLayout->addWidget(_explodedBtn);
    connect(_explodedBtn, &QToolButton::toggled, this, [this](bool checked) { emit explodedViewToggled(checked); });

    // Swap Visible View
    _swapBtn = new QToolButton(this);
    _swapBtn->setStyleSheet(buttonStyleSheet);
    _swapBtn->setIcon(QIcon(":/icons/res/swapvisible.png"));
    _swapBtn->setIconSize(QSize(48, 48));
    _swapBtn->setToolTip(tr("Swap Visible"));
    _swapBtn->setCheckable(true);
    _swapBtn->setAutoRaise(true);
    _mainLayout->addWidget(_swapBtn);
    connect(_swapBtn, &QToolButton::toggled, this, [this](bool checked) { emit swapVisibleToggled(checked); });

    // Show/Hide Axis
    _axisBtn = new QToolButton(this);
    _axisBtn->setStyleSheet(buttonStyleSheet);
    _axisBtn->setIcon(QIcon(":/icons/res/showAxis.png"));
    _axisBtn->setIconSize(QSize(48, 48));
    _axisBtn->setToolTip(tr("Show/Hide Axis"));
    _axisBtn->setCheckable(true);
    _axisBtn->setChecked(true);
    _axisBtn->setAutoRaise(true);
    _mainLayout->addWidget(_axisBtn);
    connect(_axisBtn, &QToolButton::toggled, this, [this](bool checked) {
        if (checked)
        {
            _axisBtn->setIcon(QIcon(":/icons/res/showAxis.png"));
            _axisBtn->setToolTip(tr("Show the trihedron"));
        }
        else
        {
            _axisBtn->setIcon(QIcon(":/icons/res/hideAxis.png"));
            _axisBtn->setToolTip(tr("Hide the trihedron"));
        }
        emit axisDisplayToggled(checked);
        });

    // Debug Overlays
    _toolButtonDebugOverlays = new FlyOutViewButton(this);
    _toolButtonDebugOverlays->setStyleSheet(flyoutToggleButtonStyleSheet);
    _toolButtonDebugOverlays->setCheckable(true);
    _toolButtonDebugOverlays->setChecked(false);
    _toolButtonDebugOverlays->setIcon(QIcon(debugOverlayIconPath(_currentDebugOverlayAction, false)));
    _toolButtonDebugOverlays->setIconSize(QSize(48, 48));
    _toolButtonDebugOverlays->setToolTip(tr("Debug Overlays"));
    _toolButtonDebugOverlays->setPopupMode(QToolButton::DelayedPopup);
    _toolButtonDebugOverlays->setAutoRaise(true);
    _mainLayout->addWidget(_toolButtonDebugOverlays);

    QMenu* debugOverlayMenu = new QMenu;
    debugOverlayMenu->setStyleSheet(flyoutStyleSheet);
    _boundingBoxOverlay = debugOverlayMenu->addAction(QIcon(":/icons/res/show_bounding_box.png"), tr("Bounding Box"));
    _boundingBoxOverlay->setCheckable(true);
    _vertexNormalsOverlay = debugOverlayMenu->addAction(QIcon(":/icons/res/showVertexNormal.png"), tr("Vertex Normals"));
    _vertexNormalsOverlay->setCheckable(true);
    _faceNormalsOverlay = debugOverlayMenu->addAction(QIcon(":/icons/res/showFaceNormal.png"), tr("Face Normals"));
    _faceNormalsOverlay->setCheckable(true);

    _debugOverlayActions[DebugOverlayActions::BOUNDING_BOX] = _boundingBoxOverlay;
    _debugOverlayActions[DebugOverlayActions::VERTEX_NORMALS] = _vertexNormalsOverlay;
    _debugOverlayActions[DebugOverlayActions::FACE_NORMALS] = _faceNormalsOverlay;

    auto selectDebugOverlay = [this](DebugOverlayActions action, const QString& type) {
        _currentDebugOverlayAction = action;
        for (auto it = _debugOverlayActions.begin(); it != _debugOverlayActions.end(); ++it)
        {
            if (it.value())
                it.value()->setChecked(it.key() == action);
        }
        _toolButtonDebugOverlays->setIcon(QIcon(debugOverlayIconPath(action, _toolButtonDebugOverlays->isChecked())));
        emit debugOverlaySelected(type);
    };

    connect(_boundingBoxOverlay, &QAction::triggered, this,
        [selectDebugOverlay]() { selectDebugOverlay(DebugOverlayActions::BOUNDING_BOX, QStringLiteral("BoundingBox")); });
    connect(_vertexNormalsOverlay, &QAction::triggered, this,
        [selectDebugOverlay]() { selectDebugOverlay(DebugOverlayActions::VERTEX_NORMALS, QStringLiteral("VertexNormals")); });
    connect(_faceNormalsOverlay, &QAction::triggered, this,
        [selectDebugOverlay]() { selectDebugOverlay(DebugOverlayActions::FACE_NORMALS, QStringLiteral("FaceNormals")); });

    _toolButtonDebugOverlays->setMenu(debugOverlayMenu);
    connect(_toolButtonDebugOverlays, &QToolButton::toggled, this, [this](bool checked) {
        _toolButtonDebugOverlays->setIcon(QIcon(debugOverlayIconPath(_currentDebugOverlayAction, checked)));
        emit debugOverlayToggled(checked);
    });
    setDebugOverlayState(DebugOverlayActions::BOUNDING_BOX, false);

    // Toolbar animations
    _toolbarAnimation = new QPropertyAnimation(this, "geometry", this);
    _toolbarAnimation->setDuration(300);
    _toolbarAnimation->setEasingCurve(QEasingCurve::OutCubic);

    // Auto-scroll timer
    _autoScrollTimer = new QTimer(this);
    _autoScrollTimer->setInterval(50); // Scroll every 50ms for smooth scrolling
    connect(_autoScrollTimer, &QTimer::timeout, this, [this]() {
        if (_autoScrollLeft)
        {
            scrollLeft();
        }
        else
        {
            scrollRight();
        }
        });

    // Hover delay timer
    _hoverDelayTimer = new QTimer(this);
    _hoverDelayTimer->setSingleShot(true);
    _hoverDelayTimer->setInterval(300); // 300ms delay before auto-scroll starts

    retranslateUI();

    connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
        retranslateUI();
        });
}

void ViewToolbar::showAnimated()
{
	if (_toolbarAnimation->state() == QAbstractAnimation::Running)
		_toolbarAnimation->stop();
	_toolbarAnimation->setStartValue(geometry());
	_toolbarAnimation->setEndValue(_visibleRect);
	_toolbarAnimation->start();
}

void ViewToolbar::hideAnimated()
{
	if (_toolbarAnimation->state() == QAbstractAnimation::Running)
		_toolbarAnimation->stop();
	_toolbarAnimation->setStartValue(geometry());
	_toolbarAnimation->setEndValue(_hiddenRect);
	_toolbarAnimation->start();
}

void ViewToolbar::reposition(int widgetWidth, int widgetHeight)
{
    // Prevent recursive calls
    if (_isRepositioning)
        return;

    _isRepositioning = true;

    // Calculate maximum toolbar width
    int maxToolbarWidth = widgetWidth - 20; // 10px margin on each side

    // Calculate minimum width needed for all buttons
    int totalButtonWidth = 0;
    for (int i = 0; i < _mainLayout->count(); ++i)
    {
        QLayoutItem* item = _mainLayout->itemAt(i);
        if (item && item->widget())
        {
            totalButtonWidth += item->widget()->sizeHint().width();
        }
    }

    // Add spacing and margins
    int buttonCount = _mainLayout->count();
    totalButtonWidth += (buttonCount - 1) * _mainLayout->spacing();
    totalButtonWidth += _mainLayout->contentsMargins().left() + _mainLayout->contentsMargins().right();

    // Account for outer layout margins
    int outerMargins = 4; // 2px on each side

    // Determine toolbar width
    int toolbarWidth;
    bool needsScrolling = (totalButtonWidth + outerMargins) > maxToolbarWidth;

    if (needsScrolling)
    {
        // Use max width when scrolling is needed
        toolbarWidth = maxToolbarWidth;
        _buttonContainer->setMinimumWidth(totalButtonWidth);
        _buttonContainer->setMaximumWidth(totalButtonWidth);
    }
    else
    {
        // Use exact width when no scrolling
        toolbarWidth = totalButtonWidth + outerMargins;
        _buttonContainer->setMinimumWidth(totalButtonWidth);
        _buttonContainer->setMaximumWidth(totalButtonWidth);
    }

    resize(toolbarWidth, 76);

    int x = (widgetWidth - toolbarWidth) / 2;
    int y = widgetHeight - 76 - 10;
    move(x, y);

    _visibleRect = QRect(x, y, toolbarWidth, 76);
    _hiddenRect = _visibleRect.translated(0, 80);

    // Update scroll button visibility after positioning is done
    _isRepositioning = false;
    checkScrollButtonsVisibility();
}

QRect ViewToolbar::visibleRect() const { return _visibleRect; }
QRect ViewToolbar::hiddenRect() const { return _hiddenRect; }

bool ViewToolbar::isFlyoutMenuVisible() const
{
	return (_toolButtonViewModes &&
		_toolButtonViewModes->menu() &&
		_toolButtonViewModes->menu()->isVisible()) ||
		(_toolButtonCameraModes &&
			_toolButtonCameraModes->menu() &&
			_toolButtonCameraModes->menu()->isVisible()) ||
        (_toolButtonDebugOverlays &&
            _toolButtonDebugOverlays->menu() &&
            _toolButtonDebugOverlays->menu()->isVisible()) ||
		(_toolButtonDisplayModes &&
			_toolButtonDisplayModes->menu() &&
			_toolButtonDisplayModes->menu()->isVisible());
}

void ViewToolbar::setDefaultCameraModeAction(CameraModeActions mode)
{
	if (_cameraModeActions.contains(mode))
		_toolButtonCameraModes->setDefaultAction(_cameraModeActions[mode]);
}

void ViewToolbar::setDefaultStandardViewAction(StandardViewActions view)
{
	if (_standardViewActions.contains(view))
		_toolButtonViews->setDefaultAction(_standardViewActions[view]);
}

void ViewToolbar::setDefaultViewModeAction(ViewModeActions mode)
{
	if (_viewModeActions.contains(mode))
		_toolButtonViewModes->setDefaultAction(_viewModeActions[mode]);
}

void ViewToolbar::setDefaultDisplayModeAction(DisplayModeActions mode)
{
	if (_displayModeActions.contains(mode))
		_toolButtonDisplayModes->setDefaultAction(_displayModeActions[mode]);
}

void ViewToolbar::setRealisticChecked(bool checked)
{
	if (_realisticBtn)
		_realisticBtn->setChecked(checked);
	if (_realistic)
		_realistic->setChecked(checked);
}

void ViewToolbar::setDefaultShadingNormalModeAction(ShadingNormalModeActions mode)
{
	if (_shadingNormalActions.contains(mode))
		_toolButtonShadingNormal->setDefaultAction(_shadingNormalActions[mode]);
}

void ViewToolbar::setFeatureEdgeModesVisible(bool visible)
{
	if (_wireframe)
		_wireframe->setVisible(visible);
	if (_shadedWithEdges)
		_shadedWithEdges->setVisible(visible);
	if (!visible && _toolButtonDisplayModes)
	{
		const QAction* current = _toolButtonDisplayModes->defaultAction();
		if (current == _wireframe || current == _shadedWithEdges)
			_toolButtonDisplayModes->setDefaultAction(_shaded);
	}
}

void ViewToolbar::setDebugOverlayModesAvailable(bool boundingBox, bool vertexNormals, bool faceNormals)
{
    if (_boundingBoxOverlay)
        _boundingBoxOverlay->setVisible(boundingBox);
    if (_vertexNormalsOverlay)
        _vertexNormalsOverlay->setVisible(vertexNormals);
    if (_faceNormalsOverlay)
        _faceNormalsOverlay->setVisible(faceNormals);

    const bool hasAnyOverlay = boundingBox || vertexNormals || faceNormals;
    if (_toolButtonDebugOverlays)
        _toolButtonDebugOverlays->setVisible(hasAnyOverlay);

    if (!hasAnyOverlay)
    {
        setDebugOverlayState(_currentDebugOverlayAction, false);
    }
    else
    {
        const bool currentAvailable =
            (_currentDebugOverlayAction == DebugOverlayActions::BOUNDING_BOX && boundingBox) ||
            (_currentDebugOverlayAction == DebugOverlayActions::VERTEX_NORMALS && vertexNormals) ||
            (_currentDebugOverlayAction == DebugOverlayActions::FACE_NORMALS && faceNormals);

        if (!currentAvailable)
        {
            if (boundingBox)
                _currentDebugOverlayAction = DebugOverlayActions::BOUNDING_BOX;
            else if (vertexNormals)
                _currentDebugOverlayAction = DebugOverlayActions::VERTEX_NORMALS;
            else
                _currentDebugOverlayAction = DebugOverlayActions::FACE_NORMALS;
        }

        setDebugOverlayState(_currentDebugOverlayAction,
                             _toolButtonDebugOverlays ? _toolButtonDebugOverlays->isChecked() : false);
    }

    if (parentWidget())
        reposition(parentWidget()->width(), parentWidget()->height());
}

void ViewToolbar::setDebugOverlayState(DebugOverlayActions mode, bool enabled)
{
    _currentDebugOverlayAction = mode;

    for (auto it = _debugOverlayActions.begin(); it != _debugOverlayActions.end(); ++it)
    {
        if (it.value())
            it.value()->setChecked(it.key() == mode);
    }

    if (_toolButtonDebugOverlays)
    {
        const bool oldState = _toolButtonDebugOverlays->blockSignals(true);
        _toolButtonDebugOverlays->setChecked(enabled);
        _toolButtonDebugOverlays->setIcon(QIcon(debugOverlayIconPath(mode, enabled)));
        _toolButtonDebugOverlays->blockSignals(oldState);
    }
}

void ViewToolbar::setSwapVisibleChecked(bool checked)
{
	bool oldState = _swapBtn->blockSignals(true);
	_swapBtn->setChecked(checked);
	_swapBtn->blockSignals(oldState);
}

void ViewToolbar::setSectionViewChecked(bool checked)
{
	bool oldState = _sectionBtn->blockSignals(true);
	_sectionBtn->setChecked(checked);
	_sectionBtn->blockSignals(oldState);
}

void ViewToolbar::setExplodedViewChecked(bool checked)
{
	bool oldState = _explodedBtn->blockSignals(true);
	_explodedBtn->setChecked(checked);
	_explodedBtn->blockSignals(oldState);
}

void ViewToolbar::setCameraUpAxisZUp(bool zUp)
{
    if (!_toolButtonCameraUpAxis)
        return;

    _toolButtonCameraUpAxis->setDefaultAction(zUp ? _cameraZUpAction : _cameraYUpAction);
}

bool ViewToolbar::isCameraUpAxisZUp() const
{
    return _toolButtonCameraUpAxis &&
        _toolButtonCameraUpAxis->defaultAction() == _cameraZUpAction;
}


void ViewToolbar::paintEvent(QPaintEvent* event)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);

	QRect r = rect();
	QColor bg(255, 255, 255, 100);
	QColor border(100, 100, 100, 160);

	// Draw rounded rectangle background
	painter.setBrush(bg);
	painter.setPen(QPen(border, 1));
	painter.drawRoundedRect(r.adjusted(0, 0, -1, -1), 4, 4);

	QWidget::paintEvent(event); // Optional, not strictly needed here
}

void ViewToolbar::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    if (_isRepositioning)
        return;

    // After resize, check if we need scrolling and adjust toolbar width accordingly
    QTimer::singleShot(0, this, [this]() {
        if (parentWidget())
        {
            reposition(parentWidget()->width(), parentWidget()->height());
        }
        });
}

bool ViewToolbar::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == _scrollLeftBtn)
    {
        if (event->type() == QEvent::Enter)
        {
            // Disconnect any previous connection
            disconnect(_hoverDelayTimer, &QTimer::timeout, this, nullptr);
            // Connect to left scroll check
            connect(_hoverDelayTimer, &QTimer::timeout, this, &ViewToolbar::checkAndStartAutoScrollLeft);
            _hoverDelayTimer->start();
        }
        else if (event->type() == QEvent::Leave)
        {
            stopAutoScroll();
        }
    }
    else if (obj == _scrollRightBtn)
    {
        if (event->type() == QEvent::Enter)
        {
            // Disconnect any previous connection
            disconnect(_hoverDelayTimer, &QTimer::timeout, this, nullptr);
            // Connect to right scroll check
            connect(_hoverDelayTimer, &QTimer::timeout, this, &ViewToolbar::checkAndStartAutoScrollRight);
            _hoverDelayTimer->start();
        }
        else if (event->type() == QEvent::Leave)
        {
            stopAutoScroll();
        }
    }
    return QWidget::eventFilter(obj, event);
}

void ViewToolbar::retranslateUI()
{
	// Separate navigation buttons
	_btnFitAll->setToolTip(tr("Fit All"));
	_btnWindowZoom->setToolTip(tr("Window Zoom"));

	// Navigation dropdown
	_toolButtonNavigation->setToolTip(tr("Navigation"));
	_rotateViewAction->setText(tr("Rotate View"));
	_panViewAction->setText(tr("Pan View"));
	_zoomViewAction->setText(tr("Zoom View"));

	// Camera Modes
	_toolButtonCameraModes->setToolTip(tr("Camera Modes"));
	_orbitAction->setText(tr("Orbit"));
	_flyAction->setText(tr("Fly"));
	_firstPersonAction->setText(tr("First Person"));
    _toolButtonCameraUpAxis->setToolTip(tr("Camera Up Axis"));
    _cameraZUpAction->setText(tr("Z-Up"));
    _cameraYUpAction->setText(tr("Y-Up"));

	// View dropdown button
	_toolButtonViews->setToolTip(tr("Standard Views"));
	_topViewAction->setText(tr("Top"));
	_frontViewAction->setText(tr("Front"));
	_leftViewAction->setText(tr("Left"));
	_bottomViewAction->setText(tr("Bottom"));
	_rearViewAction->setText(tr("Rear"));
	_rightViewAction->setText(tr("Right"));

	// Axonometric Views
	_toolButtonViewModes->setToolTip(tr("Axonometric View"));
	_isoAction->setText(tr("Isometric"));
	_dimAction->setText(tr("Dimetric"));
	_triAction->setText(tr("Trimetric"));

	// Projection toggle
	_projToggleButton->setToolTip(tr("Toggle Projection"));	

	// Multi View
	_multiBtn->setToolTip(tr("Toggle Multi-View"));

	// Display Modes
	_toolButtonDisplayModes->setToolTip(tr("Display Modes"));
	if (_realisticBtn) _realisticBtn->setToolTip(tr("Realistic Rendering (Shift+R)"));
	_shaded->setText(tr("Shaded"));
	// Shading Normal
	_toolButtonShadingNormal->setToolTip(tr("Shading Normal"));
	if (_flatshaded) _flatshaded->setText(tr("Flat Shaded"));
	_hollowMesh->setText(tr("Hollow Mesh"));
	_meshEdges->setText(tr("Mesh Edges"));
	_wireframe->setText(tr("Wireframe"));
	_shadedWithEdges->setText(tr("Shaded with Edges"));

    // Debug overlays
    if (_toolButtonDebugOverlays)
        _toolButtonDebugOverlays->setToolTip(tr("Debug Overlays"));
    if (_boundingBoxOverlay)
        _boundingBoxOverlay->setText(tr("Bounding Box"));
    if (_vertexNormalsOverlay)
        _vertexNormalsOverlay->setText(tr("Vertex Normals"));
    if (_faceNormalsOverlay)
        _faceNormalsOverlay->setText(tr("Face Normals"));

	// Rendering Mode
	_toolButtonRenderingMode->setToolTip(tr("Rendering Mode"));
	_adsAction->setText(tr("ADS (Blinn-Phong)"));
	_pbrAction->setText(tr("PBR (Metallic-Roughness)"));

	// Section View
	_sectionBtn->setToolTip(tr("Clipping Planes"));

	// Exploded View
	_explodedBtn->setToolTip(tr("Exploded View"));

	// Swap Visible View
	_swapBtn->setToolTip(tr("Swap Visible"));

	// Axis
	_axisBtn->setToolTip(tr("Show/Hide Axis"));
}

void ViewToolbar::updateRenderingModeButton(const QString& mode)
{
	if (mode == "ADS")
	{
		_toolButtonRenderingMode->setDefaultAction(_adsAction);
	}
	else if (mode == "PBR")
	{
		_toolButtonRenderingMode->setDefaultAction(_pbrAction);
	}
	else if (mode == "PathTraced")
	{
		_toolButtonRenderingMode->setDefaultAction(_pathTracedAction);
	}
}

void ViewToolbar::deactivateAllNavigationModes()
{
	_rotateViewAction->setChecked(false);
	_panViewAction->setChecked(false);
	_zoomViewAction->setChecked(false);
}

void ViewToolbar::scrollLeft()
{
    QScrollBar* scrollBar = _scrollArea->horizontalScrollBar();
    int currentValue = scrollBar->value();
    int step = 100; // Adjust scroll speed as needed
    scrollBar->setValue(currentValue - step);
}

void ViewToolbar::scrollRight()
{
    QScrollBar* scrollBar = _scrollArea->horizontalScrollBar();
    int currentValue = scrollBar->value();
    int step = 100; // Adjust scroll speed as needed
    scrollBar->setValue(currentValue + step);
}

void ViewToolbar::updateScrollButtons()
{
    QScrollBar* scrollBar = _scrollArea->horizontalScrollBar();

    // Show/hide left button
    _scrollLeftBtn->setEnabled(scrollBar->value() > scrollBar->minimum());

    // Show/hide right button
    _scrollRightBtn->setEnabled(scrollBar->value() < scrollBar->maximum());
}

void ViewToolbar::checkScrollButtonsVisibility()
{
    if (_isRepositioning)
        return;

    // Calculate the total width needed for all buttons using sizeHint
    int totalButtonWidth = 0;
    for (int i = 0; i < _mainLayout->count(); ++i)
    {
        QLayoutItem* item = _mainLayout->itemAt(i);
        if (item && item->widget())
        {
            totalButtonWidth += item->widget()->sizeHint().width();
        }
    }

    // Add spacing and margins
    int buttonCount = _mainLayout->count();
    totalButtonWidth += (buttonCount - 1) * _mainLayout->spacing();
    totalButtonWidth += _mainLayout->contentsMargins().left() + _mainLayout->contentsMargins().right();

    // Calculate how much space we have
    int toolbarWidth = width();
    int outerMargins = 4; // 2px each side
    int scrollButtonWidth = 20;

    // Available width if scroll buttons are NOT visible
    int availableWidthNoScroll = toolbarWidth - outerMargins;

    // Available width if scroll buttons ARE visible
    int availableWidthWithScroll = toolbarWidth - outerMargins - (2 * scrollButtonWidth);

    // Determine if scrolling is needed based on available space WITHOUT scroll buttons
    bool needsScrolling = totalButtonWidth > availableWidthNoScroll;

    _scrollLeftBtn->setVisible(needsScrolling);
    _scrollRightBtn->setVisible(needsScrolling);

    if (needsScrolling)
    {
        // Set button container to full content width
        _buttonContainer->setMinimumWidth(totalButtonWidth);
        _buttonContainer->setMaximumWidth(totalButtonWidth);
        updateScrollButtons();
    }
    else
    {
        // No scrolling needed - container should match available space
        _buttonContainer->setMinimumWidth(totalButtonWidth);
        _buttonContainer->setMaximumWidth(totalButtonWidth);
        _scrollArea->horizontalScrollBar()->setValue(0);
    }
}


void ViewToolbar::startAutoScroll(bool scrollLeft)
{
    _autoScrollLeft = scrollLeft;
    if (!_autoScrollTimer->isActive())
    {
        // Initial scroll immediately
        if (scrollLeft)
        {
            this->scrollLeft();
        }
        else
        {
            this->scrollRight();
        }
        // Then start timer for continuous scrolling
        _autoScrollTimer->start();
    }
}

void ViewToolbar::stopAutoScroll()
{
    _autoScrollTimer->stop();
    _hoverDelayTimer->stop();
}

void ViewToolbar::checkAndStartAutoScrollLeft()
{
    if (_scrollLeftBtn->underMouse())
    {
        startAutoScroll(true);
    }
}

void ViewToolbar::checkAndStartAutoScrollRight()
{
    if (_scrollRightBtn->underMouse())
    {
        startAutoScroll(false);
    }
}
