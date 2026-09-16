
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

void ViewToolbar::scopeShortcutToViewport(QAction* action)
{
    // One ViewToolbar (and its flyout menus' actions) exists per document -
    // under the old QMdiArea setup, QAction's default WindowShortcut
    // context happened to be scoped per-document for free, because
    // QMdiSubWindow's Qt::SubWindow window flag made QWidget::window() stop
    // there instead of continuing up to MainWindow. CDockWidget (see
    // MainWindow::createDocumentDock()) carries no such flag, so with two-
    // plus documents open every one of these actions' shortcuts resolved to
    // the same top-level MainWindow and Qt disabled them all as ambiguous -
    // this is the "Ambiguous shortcut overload" warning the user's log
    // showed for Ctrl+T/Ctrl+F, unrelated to the earlier-fixed
    // ModelViewer-level shortcuts.
    //
    // Simply switching the context to WidgetWithChildrenShortcut is not
    // enough by itself: these actions are created via QMenu::addAction(),
    // which parents them to their (parentless, never-shown-except-as-a-
    // popup) QMenu - "the widget that defined it" for context-matching
    // purposes would then be that menu, which never has focus during normal
    // 3D-viewport use, so the shortcut would just stop firing entirely
    // rather than becoming merely non-ambiguous. addAction() associates the
    // action with _viewport (the actual ViewportWidget that gets focus
    // on click - see ViewportWidget::mousePressEvent()) directly, so
    // WidgetWithChildrenShortcut's "owner or a child of it has focus" check
    // has the right widget to check against.
    action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    if (QWidget* viewport = _viewport)
        viewport->addAction(action);
}

void ViewToolbar::scopeButtonShortcutToViewport(QAbstractButton* button, const QKeySequence& sequence)
{
    // QAbstractButton::setShortcut() has no public API to change the
    // resulting shortcut's context away from the default WindowShortcut -
    // same per-document ambiguity problem as scopeShortcutToViewport()
    // above, fixed the same way the (already-confirmed-working) Home
    // shortcut is: an explicit QShortcut parented to the ViewportWidget
    // instead, with WidgetWithChildrenShortcut context.
    QShortcut* shortcut = new QShortcut(sequence, _viewport);
    shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(shortcut, &QShortcut::activated, button, &QAbstractButton::click);
}

QAction* ViewToolbar::bindButtonAction(QToolButton* button, const QString& name)
{
    auto* action = new QAction(button->icon(), button->toolTip(), this);
    action->setObjectName(name);
    action->setToolTip(button->toolTip());
    action->setCheckable(button->isCheckable());
    action->setChecked(button->isChecked());
    // Do not assign a shortcut here: scopeButtonShortcutToViewport() owns
    // the sole registration and invokes the button's default action.
    button->setDefaultAction(action);
    return action;
}

ViewToolbar::ViewToolbar(QWidget* viewport, QWidget* parent)
    : QWidget(parent ? parent : viewport)
    , _viewport(viewport)
    , _isRepositioning(false)
    , _autoScrollTimer(nullptr)
    , _hoverDelayTimer(nullptr)
    , _autoScrollLeft(true)
{
    setObjectName(QStringLiteral("standardViewportToolbar"));
    // Scope transparency to the toolbar, so it cannot override tooltip styling.
    setStyleSheet("QWidget#standardViewportToolbar { background: transparent; border: none; }");
    setFixedHeight(64);

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

    const QString flyoutStyleSheet = FlyOutViewButton::menuStyleSheet();

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
    _scrollLeftBtn->setFixedSize(20, 56);
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
    _scrollArea->setFixedHeight(60);
    _scrollArea->setStyleSheet("QScrollArea { background: transparent; border: none; }");
    outerLayout->addWidget(_scrollArea, 1);

    // Container widget for buttons inside scroll area
    _buttonContainer = new QWidget();
    _buttonContainer->setObjectName(QStringLiteral("viewToolbarButtons"));
    _buttonContainer->setStyleSheet("QWidget#viewToolbarButtons { background: transparent; }");
    _buttonContainer->setFixedHeight(60);
    _mainLayout = new QHBoxLayout(_buttonContainer);
    _mainLayout->setContentsMargins(4, 4, 4, 4);
    _mainLayout->setSpacing(6);

    _scrollArea->setWidget(_buttonContainer);
    _scrollArea->viewport()->setAutoFillBackground(false);
    _buttonContainer->setAutoFillBackground(false);

    // Right scroll button
    _scrollRightBtn = new QToolButton(this);
    _scrollRightBtn->setStyleSheet(scrollButtonStyleSheet);
    _scrollRightBtn->setText(">");
    _scrollRightBtn->setFixedSize(20, 56);
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
    _toolButtonNavigation->setIconSize(QSize(40, 40));
    _toolButtonNavigation->setToolTip(tr("Navigation"));
    _toolButtonNavigation->setPopupMode(QToolButton::DelayedPopup);
    _toolButtonNavigation->setAutoRaise(true);
    _mainLayout->addWidget(_toolButtonNavigation);

    QMenu* navigationMenu = new QMenu;
    navigationMenu->setStyleSheet(flyoutStyleSheet);
    _rotateViewAction = navigationMenu->addAction(QIcon(":/icons/res/rotateview.png"), tr("Rotate View"));
    _rotateViewAction->setCheckable(true);
	_rotateViewAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_R));
	scopeShortcutToViewport(_rotateViewAction);
    _panViewAction = navigationMenu->addAction(QIcon(":/icons/res/panview.png"), tr("Pan View"));
    _panViewAction->setCheckable(true);
    _panViewAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_P));
    scopeShortcutToViewport(_panViewAction);
    _zoomViewAction = navigationMenu->addAction(QIcon(":/icons/res/zoomview.png"), tr("Zoom View"));
    _zoomViewAction->setCheckable(true);
    _zoomViewAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Z));
    scopeShortcutToViewport(_zoomViewAction);

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
    _btnFitAll->setIconSize(QSize(40, 40));
    _btnFitAll->setToolTip(tr("Fit All"));
    scopeButtonShortcutToViewport(_btnFitAll, QKeySequence(Qt::Key_F));
    _btnFitAll->setAutoRaise(true);
    _mainLayout->addWidget(_btnFitAll);
    _fitAllAction = bindButtonAction(_btnFitAll, QStringLiteral("fitAllAction"));
    connect(_fitAllAction, &QAction::triggered, this, [this]() { emit fitToViewRequested(); });

    _btnWindowZoom = new QToolButton(this);
    _btnWindowZoom->setStyleSheet(buttonStyleSheet);
    _btnWindowZoom->setIcon(QIcon(":/icons/res/window-zoom.png"));
    _btnWindowZoom->setIconSize(QSize(40, 40));
    _btnWindowZoom->setToolTip(tr("Window Zoom"));
    scopeButtonShortcutToViewport(_btnWindowZoom, QKeySequence(Qt::ALT | Qt::Key_W));
    _btnWindowZoom->setAutoRaise(true);
    _mainLayout->addWidget(_btnWindowZoom);
    _windowZoomAction = bindButtonAction(_btnWindowZoom, QStringLiteral("windowZoomAction"));
    connect(_windowZoomAction, &QAction::triggered, this, [this]() { emit windowZoomRequested(); });

    // Lasso Select - freeform-polygon drag selection, stays armed across
    // multiple drags (toggle) rather than Window Zoom's one-shot gesture.
    _btnLassoSelect = new FlyOutViewButton(this);
    _btnLassoSelect->setIcon(QIcon(":/icons/res/lasso_select.png"));
    _btnLassoSelect->setIconSize(QSize(40, 40));
    _btnLassoSelect->setToolTip(tr("Lasso Select"));
    _btnLassoSelect->setCheckable(true);
    _btnLassoSelect->setAutoRaise(true);
    _mainLayout->addWidget(_btnLassoSelect);
    _lassoSelectAction = bindButtonAction(_btnLassoSelect, QStringLiteral("lassoSelectAction"));
    connect(_lassoSelectAction, &QAction::triggered, this, [this](bool checked) { emit lassoSelectToggled(checked); });
    auto* selectionMenu = new QMenu(this);
    selectionMenu->setStyleSheet(flyoutStyleSheet);
    selectionMenu->addAction(_lassoSelectAction);
    selectionMenu->addSeparator();
    const auto addFilter = [this, selectionMenu](const char* icon, const QString& text, const QString& command) {
        auto* action = selectionMenu->addAction(QIcon(QStringLiteral(":/icons/res/") + QLatin1String(icon)), text);
        action->setToolTip(text);
        connect(action, &QAction::triggered, this, [this, action, command] {
            _btnLassoSelect->setDefaultAction(action);
            emit selectionFilterRequested(command);
        });
        return action;
    };
    _filterByMaterialAction = addFilter("filter_by_material.png", tr("Filter by Material..."), QStringLiteral("material"));
    _filterByColorAction = addFilter("filter_by_color.png", tr("Filter by Color..."), QStringLiteral("color"));
    _filterByBoundingBoxAction = addFilter("filter_by_bounding_box.png", tr("Filter by Bounding Box..."), QStringLiteral("boundingBox"));
    connect(_lassoSelectAction, &QAction::triggered, this, [this] { _btnLassoSelect->setDefaultAction(_lassoSelectAction); });
    _btnLassoSelect->setMenu(selectionMenu);
    _btnLassoSelect->setPopupMode(QToolButton::DelayedPopup);
    setSelectionFiltersEnabled(false);

    // Camera Modes
    _toolButtonCameraModes = new FlyOutViewButton(this);
    _toolButtonCameraModes->setIcon(QIcon(":/icons/res/camera_orbit.png"));
    _toolButtonCameraModes->setIconSize(QSize(40, 40));
    _toolButtonCameraModes->setToolTip(tr("Camera Modes"));
    _toolButtonCameraModes->setPopupMode(QToolButton::DelayedPopup);
    _toolButtonCameraModes->setAutoRaise(true);
    _mainLayout->addWidget(_toolButtonCameraModes);

    QMenu* camModeMenu = new QMenu;
    camModeMenu->setStyleSheet(flyoutStyleSheet);
    _orbitAction = camModeMenu->addAction(QIcon(":/icons/res/camera_orbit.png"), tr("Orbit"));
    _orbitAction->setShortcut(QKeySequence(Qt::Key_1));
    scopeShortcutToViewport(_orbitAction);
    _flyAction = camModeMenu->addAction(QIcon(":/icons/res/camera_fly.png"), tr("Fly"));
    _flyAction->setShortcut(QKeySequence(Qt::Key_2));
    scopeShortcutToViewport(_flyAction);
    _firstPersonAction = camModeMenu->addAction(QIcon(":/icons/res/camera_first_person.png"), tr("First Person"));
    _firstPersonAction->setShortcut(QKeySequence(Qt::Key_3));
    scopeShortcutToViewport(_firstPersonAction);

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
    _toolButtonCameraUpAxis->setIconSize(QSize(40, 40));
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

    // Turntable - orthogonal to Camera Modes above (auto-spins the camera
    // while in whatever mode is active), so it's its own toggle rather than
    // a 4th entry in camModeMenu.
    _btnTurntable = new QToolButton(this);
    _btnTurntable->setStyleSheet(buttonStyleSheet);
    _btnTurntable->setIcon(QIcon(":/icons/res/camera_orbit_anim.png"));
    _btnTurntable->setIconSize(QSize(40, 40));
    _btnTurntable->setToolTip(tr("Turntable"));
    _btnTurntable->setCheckable(true);
    _btnTurntable->setAutoRaise(true);
    _mainLayout->addWidget(_btnTurntable);
    _turntableAction = bindButtonAction(_btnTurntable, QStringLiteral("turntableAction"));
    connect(_turntableAction, &QAction::triggered, this, [this](bool checked) { emit turntableToggled(checked); });

    // Standard Views
    _toolButtonViews = new FlyOutViewButton(this);
    _toolButtonViews->setIcon(QIcon(":/icons/res/top.png"));
    _toolButtonViews->setIconSize(QSize(40, 40));
    _toolButtonViews->setToolTip(tr("Standard Views"));
    _toolButtonViews->setPopupMode(QToolButton::DelayedPopup);
    _toolButtonViews->setAutoRaise(true);
    _mainLayout->addWidget(_toolButtonViews);

    QMenu* viewsMenu = new QMenu;
    viewsMenu->setStyleSheet(flyoutStyleSheet);
    _topViewAction = viewsMenu->addAction(QIcon(":/icons/res/top.png"), tr("Top"));
    _topViewAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    scopeShortcutToViewport(_topViewAction);
    _frontViewAction = viewsMenu->addAction(QIcon(":/icons/res/front.png"), tr("Front"));
    _frontViewAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F));
    scopeShortcutToViewport(_frontViewAction);
    _leftViewAction = viewsMenu->addAction(QIcon(":/icons/res/left.png"), tr("Left"));
    _leftViewAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    scopeShortcutToViewport(_leftViewAction);
    _bottomViewAction = viewsMenu->addAction(QIcon(":/icons/res/bottom.png"), tr("Bottom"));
    _bottomViewAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    scopeShortcutToViewport(_bottomViewAction);
    _rearViewAction = viewsMenu->addAction(QIcon(":/icons/res/back.png"), tr("Rear"));
    _rearViewAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    scopeShortcutToViewport(_rearViewAction);
    _rightViewAction = viewsMenu->addAction(QIcon(":/icons/res/right.png"), tr("Right"));
    _rightViewAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_J));
    scopeShortcutToViewport(_rightViewAction);

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
    _toolButtonViewModes->setIconSize(QSize(40, 40));
    _toolButtonViewModes->setToolTip(tr("Axonometric View"));
    _toolButtonViewModes->setPopupMode(QToolButton::DelayedPopup);
    _toolButtonViewModes->setAutoRaise(true);

    // WidgetWithChildrenShortcut, targeted at `_viewport` (the owning
    // ViewportWidget) rather than the toolbar page - see
    // the identical fix/reasoning in ModelViewer's constructor for why
    // WindowShortcut's default scope collides across documents now that
    // they're CDockWidgets rather than QMdiSubWindows. Targeting `this`
    // instead of `parent` would have been backwards: clicking into the 3D
    // view focuses ViewportWidget itself (see its mousePressEvent()), which
    // is ViewToolbar's PARENT, not one of its children - a
    // WidgetWithChildrenShortcut scoped to the toolbar would then only ever
    // fire while focus sat on one of the toolbar's own buttons.
    QShortcut* defaultShortcut = new QShortcut(QKeySequence(Qt::Key_Home), _viewport);
    defaultShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(defaultShortcut, &QShortcut::activated, _toolButtonViewModes, &QToolButton::click);

    _mainLayout->addWidget(_toolButtonViewModes);

    QMenu* axoMenu = new QMenu;
    axoMenu->setStyleSheet(flyoutStyleSheet);
    _isoAction = axoMenu->addAction(QIcon(":/icons/res/isometric.png"), tr("Isometric"));
    _isoAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    scopeShortcutToViewport(_isoAction);
    _dimAction = axoMenu->addAction(QIcon(":/icons/res/dimetric.png"), tr("Dimetric"));
    _dimAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_2));
    scopeShortcutToViewport(_dimAction);
    _triAction = axoMenu->addAction(QIcon(":/icons/res/trimetric.png"), tr("Trimetric"));
    _triAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_3));
    scopeShortcutToViewport(_triAction);

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
    _projToggleButton->setIconSize(QSize(40, 40));
    _projToggleButton->setToolTip(tr("Toggle Projection"));
    scopeButtonShortcutToViewport(_projToggleButton, QKeySequence(Qt::SHIFT | Qt::Key_P));
    _mainLayout->addWidget(_projToggleButton);

    _projectionAction = bindButtonAction(_projToggleButton, QStringLiteral("projectionAction"));
    connect(_projectionAction, &QAction::triggered, this, [this](bool checked) {
        if (!checked)
        {
            _projectionAction->setIcon(QIcon(":/icons/res/Ortho.png"));
            _projectionAction->setToolTip(tr("Switch to Perspective"));
        }
        else
        {
            _projectionAction->setToolTip(tr("Switch to Orthographic"));
            _projectionAction->setIcon(QIcon(":/icons/res/Perspective.png"));
        }
        emit projectionToggled(!checked);
        });

    // Multi View
    _multiBtn = new QToolButton(this);
    _multiBtn->setStyleSheet(buttonStyleSheet);
    _multiBtn->setIcon(QIcon(":/icons/res/multiview.png"));
    _multiBtn->setIconSize(QSize(40, 40));
    _multiBtn->setToolTip(tr("Toggle Multi-View"));
    _multiBtn->setCheckable(true);
    _multiBtn->setAutoRaise(true);
    scopeButtonShortcutToViewport(_multiBtn, QKeySequence(Qt::CTRL | Qt::Key_M));
    _mainLayout->addWidget(_multiBtn);
    _multiViewAction = bindButtonAction(_multiBtn, QStringLiteral("multiViewAction"));
    connect(_multiViewAction, &QAction::triggered, this, [this](bool checked) { emit multiViewToggled(checked); });

    // Display Modes
    _toolButtonDisplayModes = new FlyOutViewButton(this);
    _toolButtonDisplayModes->setIcon(QIcon(":/icons/res/shaded.png"));
    _toolButtonDisplayModes->setIconSize(QSize(40, 40));
    _toolButtonDisplayModes->setToolTip(tr("Display Modes"));
    _toolButtonDisplayModes->setPopupMode(QToolButton::DelayedPopup);
    _toolButtonDisplayModes->setAutoRaise(true);
    _mainLayout->addWidget(_toolButtonDisplayModes);

    // Realistic: standalone checkable toggle button (orthogonal to display mode)
    _realisticBtn = new QToolButton(this);
    _realisticBtn->setStyleSheet(buttonStyleSheet);
    _realisticBtn->setIcon(QIcon(":/icons/res/realshaded.png"));
    _realisticBtn->setIconSize(QSize(40, 40));
    _realisticBtn->setToolTip(tr("Realistic Rendering (Shift+R)"));
    _realisticBtn->setCheckable(true);
    _realisticBtn->setAutoRaise(true);
    scopeButtonShortcutToViewport(_realisticBtn, QKeySequence(Qt::SHIFT | Qt::Key_R));
    _mainLayout->addWidget(_realisticBtn);
    _realisticAction = bindButtonAction(_realisticBtn, QStringLiteral("realisticAction"));
    connect(_realisticAction, &QAction::triggered, this,
        [this]() { emit displayModeSelected("Realistic"); });

    QMenu* dispModeMenu = new QMenu;
    dispModeMenu->setStyleSheet(flyoutStyleSheet);
    _shaded = dispModeMenu->addAction(QIcon(":/icons/res/shaded.png"), tr("Shaded"));
    _shaded->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_S));
    scopeShortcutToViewport(_shaded);
    _hollowMesh = dispModeMenu->addAction(QIcon(":/icons/res/hollow_mesh.png"), tr("Hollow Mesh"));
    _hollowMesh->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_H));
    scopeShortcutToViewport(_hollowMesh);
    _meshEdges = dispModeMenu->addAction(QIcon(":/icons/res/mesh_edges.png"), tr("Mesh Edges"));
    _meshEdges->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_M));
    scopeShortcutToViewport(_meshEdges);
    _wireframe = dispModeMenu->addAction(QIcon(":/icons/res/wireframe.png"), tr("Wireframe"));
    _wireframe->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_W));
    scopeShortcutToViewport(_wireframe);
    _shadedWithEdges = dispModeMenu->addAction(QIcon(":/icons/res/wireshaded.png"), tr("Shaded with Edges"));
    _shadedWithEdges->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_E));
    scopeShortcutToViewport(_shadedWithEdges);

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
    _toolButtonRenderingMode->setIconSize(QSize(40, 40));
    _toolButtonRenderingMode->setToolTip(tr("Rendering Mode"));
    _toolButtonRenderingMode->setPopupMode(QToolButton::DelayedPopup);
    _toolButtonRenderingMode->setAutoRaise(true);
    _mainLayout->addWidget(_toolButtonRenderingMode);

    QMenu* renderingModeMenu = new QMenu;
    renderingModeMenu->setStyleSheet(flyoutStyleSheet);
    _adsAction = renderingModeMenu->addAction(QIcon(":/icons/res/ads_mode.png"), tr("ADS (Blinn-Phong)"));
    _pbrAction = renderingModeMenu->addAction(QIcon(":/icons/res/pbr_mode.png"), tr("PBR (Metallic-Roughness)"));
    _rayTracedAction = renderingModeMenu->addAction(QIcon(":/icons/res/ray_tracing_mode.png"), tr("Ray Traced"));

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

    connect(_rayTracedAction, &QAction::triggered, this,
        [this]() {
            _toolButtonRenderingMode->setDefaultAction(_rayTracedAction);
            emit renderingModeSelected("RayTraced");
        }
    );

    _renderingModeActions[RenderingModeActions::ADS] = _adsAction;
    _renderingModeActions[RenderingModeActions::PBR] = _pbrAction;
    _renderingModeActions[RenderingModeActions::RAY_TRACED] = _rayTracedAction;

    _toolButtonRenderingMode->setMenu(renderingModeMenu);
    _toolButtonRenderingMode->setDefaultAction(_adsAction);  // Default to ADS

    // Shading Normal Mode (Smooth / Flat)
    _toolButtonShadingNormal = new FlyOutViewButton(this);
    _toolButtonShadingNormal->setIcon(QIcon(":/icons/res/smooth_shaded.png"));
    _toolButtonShadingNormal->setIconSize(QSize(40, 40));
    _toolButtonShadingNormal->setToolTip(tr("Shading Normal"));
    _toolButtonShadingNormal->setPopupMode(QToolButton::DelayedPopup);
    _toolButtonShadingNormal->setAutoRaise(true);
    _mainLayout->addWidget(_toolButtonShadingNormal);

    QMenu* shadingNormalMenu = new QMenu;
    shadingNormalMenu->setStyleSheet(flyoutStyleSheet);
    _flatshaded = shadingNormalMenu->addAction(QIcon(":/icons/res/flat_shaded.png"), tr("Flat Shaded"));
    _flatshaded->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F));
    scopeShortcutToViewport(_flatshaded);
    QAction* _smoothShaded = shadingNormalMenu->addAction(QIcon(":/icons/res/smooth_shaded.png"), tr("Smooth Shaded"));
    _smoothShaded->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_G));
    scopeShortcutToViewport(_smoothShaded);

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
    _sectionBtn->setIconSize(QSize(40, 40));
    _sectionBtn->setToolTip(tr("Clipping Planes"));
    _sectionBtn->setCheckable(true);
    _sectionBtn->setAutoRaise(true);
    _mainLayout->addWidget(_sectionBtn);
    _sectionAction = bindButtonAction(_sectionBtn, QStringLiteral("sectionAction"));
    connect(_sectionAction, &QAction::triggered, this, [this](bool checked) { emit sectionViewToggled(checked); });

    // Exploded View
    _explodedBtn = new QToolButton(this);
    _explodedBtn->setStyleSheet(buttonStyleSheet);
    _explodedBtn->setIcon(QIcon(":/icons/res/exploded_view.png"));
    _explodedBtn->setIconSize(QSize(40, 40));
    _explodedBtn->setToolTip(tr("Exploded View"));
    _explodedBtn->setCheckable(true);
    _explodedBtn->setAutoRaise(true);
    _mainLayout->addWidget(_explodedBtn);
    _explodedAction = bindButtonAction(_explodedBtn, QStringLiteral("explodedAction"));
    connect(_explodedAction, &QAction::triggered, this, [this](bool checked) { emit explodedViewToggled(checked); });

    // Swap Visible View
    _swapBtn = new QToolButton(this);
    _swapBtn->setStyleSheet(buttonStyleSheet);
    _swapBtn->setIcon(QIcon(":/icons/res/swapvisible.png"));
    _swapBtn->setIconSize(QSize(40, 40));
    _swapBtn->setToolTip(tr("Swap Visible"));
    _swapBtn->setCheckable(true);
    _swapBtn->setAutoRaise(true);
    _mainLayout->addWidget(_swapBtn);
    _swapVisibleAction = bindButtonAction(_swapBtn, QStringLiteral("swapVisibleAction"));
    connect(_swapVisibleAction, &QAction::triggered, this, [this](bool checked) { emit swapVisibleToggled(checked); });

    // Show/Hide Axis
    _axisBtn = new QToolButton(this);
    _axisBtn->setStyleSheet(buttonStyleSheet);
    _axisBtn->setIcon(QIcon(":/icons/res/showAxis.png"));
    _axisBtn->setIconSize(QSize(40, 40));
    _axisBtn->setToolTip(tr("Show/Hide Axis"));
    _axisBtn->setCheckable(true);
    _axisBtn->setChecked(true);
    _axisBtn->setAutoRaise(true);
    _mainLayout->addWidget(_axisBtn);
    _axisAction = bindButtonAction(_axisBtn, QStringLiteral("axisAction"));
    connect(_axisAction, &QAction::triggered, this, [this](bool checked) {
        if (checked)
        {
            _axisAction->setIcon(QIcon(":/icons/res/showAxis.png"));
            _axisAction->setToolTip(tr("Show the trihedron"));
        }
        else
        {
            _axisAction->setIcon(QIcon(":/icons/res/hideAxis.png"));
            _axisAction->setToolTip(tr("Hide the trihedron"));
        }
        emit axisDisplayToggled(checked);
        });

    // Debug Overlays
    _toolButtonDebugOverlays = new FlyOutViewButton(this);
    _toolButtonDebugOverlays->setStyleSheet(flyoutToggleButtonStyleSheet);
    _toolButtonDebugOverlays->setCheckable(true);
    _toolButtonDebugOverlays->setChecked(false);
    _toolButtonDebugOverlays->setIcon(QIcon(debugOverlayIconPath(_currentDebugOverlayAction, false)));
    _toolButtonDebugOverlays->setIconSize(QSize(40, 40));
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

QSize ViewToolbar::sizeHint() const
{
    ensurePolished();
    return QSize(_mainLayout->sizeHint().width() + 8, 64);
}

void ViewToolbar::stopScrolling()
{
    stopAutoScroll();
    if (_hoverDelayTimer) _hoverDelayTimer->stop();
}

bool ViewToolbar::isFlyoutMenuVisible() const
{
	return (_btnLassoSelect && _btnLassoSelect->menu() && _btnLassoSelect->menu()->isVisible()) || (_toolButtonViewModes &&
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


void ViewToolbar::syncMenuState(const QVariantMap& state)
{
    const auto checked = [&state](const char* key) { return state.value(QLatin1String(key)).toBool(); };
    // Command handlers use triggered(), so assigning state never dispatches a command.
    setLassoSelectChecked(checked("lasso"));
    _turntableAction->setChecked(checked("turntable"));
    _projectionAction->setChecked(checked("perspective"));
    _multiViewAction->setChecked(checked("multi"));
    _realisticAction->setChecked(checked("realistic"));
    _sectionAction->setChecked(checked("clipping"));
    _explodedAction->setChecked(checked("exploded"));
    _swapVisibleAction->setChecked(checked("swap"));
    _axisAction->setChecked(checked("axis"));
    _rotateViewAction->setChecked(checked("rotate"));
    _panViewAction->setChecked(checked("pan"));
    _zoomViewAction->setChecked(checked("zoom"));
    _projectionAction->setIcon(QIcon(checked("perspective") ? ":/icons/res/Perspective.png" : ":/icons/res/Ortho.png"));
    _projectionAction->setToolTip(checked("perspective") ? tr("Switch to Orthographic") : tr("Switch to Perspective"));
    _axisAction->setIcon(QIcon(checked("axis") ? ":/icons/res/showAxis.png" : ":/icons/res/hideAxis.png"));
    _axisAction->setToolTip(checked("axis") ? tr("Hide the trihedron") : tr("Show the trihedron"));
    if (checked("rotate")) _toolButtonNavigation->setDefaultAction(_rotateViewAction);
    else if (checked("pan")) _toolButtonNavigation->setDefaultAction(_panViewAction);
    else if (checked("zoom")) _toolButtonNavigation->setDefaultAction(_zoomViewAction);
    setCameraUpAxisZUp(checked("zUp"));
    if (checked("orbit")) _toolButtonCameraModes->setDefaultAction(_cameraModeActions.value(CameraModeActions::ORBIT));
    if (checked("fly")) _toolButtonCameraModes->setDefaultAction(_cameraModeActions.value(CameraModeActions::FLY));
    if (checked("firstPerson")) _toolButtonCameraModes->setDefaultAction(_cameraModeActions.value(CameraModeActions::FIRST_PERSON));
    if (checked("shaded")) _toolButtonDisplayModes->setDefaultAction(_displayModeActions.value(DisplayModeActions::SHADED));
    if (checked("hollow")) _toolButtonDisplayModes->setDefaultAction(_displayModeActions.value(DisplayModeActions::HOLLOW_MESH));
    if (checked("meshEdges")) _toolButtonDisplayModes->setDefaultAction(_displayModeActions.value(DisplayModeActions::MESH_EDGES));
    if (checked("wireframe")) _toolButtonDisplayModes->setDefaultAction(_displayModeActions.value(DisplayModeActions::WIREFRAME));
    if (checked("shadedEdges")) _toolButtonDisplayModes->setDefaultAction(_displayModeActions.value(DisplayModeActions::SHADED_WITH_EDGES));
    if (checked("ads")) _toolButtonRenderingMode->setDefaultAction(_renderingModeActions.value(RenderingModeActions::ADS));
    if (checked("pbr")) _toolButtonRenderingMode->setDefaultAction(_renderingModeActions.value(RenderingModeActions::PBR));
    if (checked("rayTraced")) _toolButtonRenderingMode->setDefaultAction(_renderingModeActions.value(RenderingModeActions::RAY_TRACED));
    if (checked("smooth")) _toolButtonShadingNormal->setDefaultAction(_shadingNormalActions.value(ShadingNormalModeActions::SMOOTH));
    if (checked("flat")) _toolButtonShadingNormal->setDefaultAction(_shadingNormalActions.value(ShadingNormalModeActions::FLAT));
    setDebugOverlayState(checked("vertexNormals") ? DebugOverlayActions::VERTEX_NORMALS
        : checked("faceNormals") ? DebugOverlayActions::FACE_NORMALS : DebugOverlayActions::BOUNDING_BOX,
        checked("debugEnabled"));
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
    _realisticAction->setChecked(checked);
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
    emit viewActionsChanged();
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

    checkScrollButtonsVisibility();
    updateGeometry();
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
    // Only triggered() dispatches commands; checked-state sync is passive.
    _swapVisibleAction->setChecked(checked);
}

void ViewToolbar::setTurntableChecked(bool checked)
{
    // Only triggered() dispatches commands; checked-state sync is passive.
    _turntableAction->setChecked(checked);
}

void ViewToolbar::setLassoSelectChecked(bool checked)
{
    // Only triggered() dispatches commands; checked-state sync is passive.
    _lassoSelectAction->setChecked(checked);
    if (checked) _btnLassoSelect->setDefaultAction(_lassoSelectAction);
}

void ViewToolbar::setSelectionFiltersEnabled(bool enabled)
{
    _filterByMaterialAction->setEnabled(enabled);
    _filterByColorAction->setEnabled(enabled);
    _filterByBoundingBoxAction->setEnabled(enabled);
    if (!enabled) _btnLassoSelect->setDefaultAction(_lassoSelectAction);
}

void ViewToolbar::setSectionViewChecked(bool checked)
{
    // Only triggered() dispatches commands; checked-state sync is passive.
    _sectionAction->setChecked(checked);
}

void ViewToolbar::setExplodedViewChecked(bool checked)
{
    // Only triggered() dispatches commands; checked-state sync is passive.
    _explodedAction->setChecked(checked);
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
    QWidget::paintEvent(event);
}

void ViewToolbar::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    QTimer::singleShot(0, this, [this]() { checkScrollButtonsVisibility(); });
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
    // Keep action text and tooltip together for these standalone controls.
    _fitAllAction->setText(tr("Fit All"));
    _windowZoomAction->setText(tr("Window Zoom"));
    _lassoSelectAction->setText(tr("Lasso Select"));
    _lassoSelectAction->setToolTip(tr("Lasso Select"));
    _filterByMaterialAction->setText(tr("Filter by Material..."));
    _filterByColorAction->setText(tr("Filter by Color..."));
    _filterByBoundingBoxAction->setText(tr("Filter by Bounding Box..."));
    for (auto* action : {_filterByMaterialAction, _filterByColorAction, _filterByBoundingBoxAction})
        action->setToolTip(action->text());
    _turntableAction->setText(tr("Turntable"));
    _turntableAction->setToolTip(tr("Turntable"));
    _projectionAction->setText(tr("Toggle Projection"));
    _multiViewAction->setText(tr("Toggle Multi-View"));
    _realisticAction->setText(tr("Realistic Rendering"));
    _sectionAction->setText(tr("Clipping Planes"));
    _explodedAction->setText(tr("Exploded View"));
    _swapVisibleAction->setText(tr("Swap Visible"));
    _axisAction->setText(tr("Show/Hide Axis"));

	// Separate navigation buttons
	_fitAllAction->setToolTip(tr("Fit All"));
	_windowZoomAction->setToolTip(tr("Window Zoom"));

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
	_projectionAction->setToolTip(tr("Toggle Projection"));

	// Multi View
	_multiViewAction->setToolTip(tr("Toggle Multi-View"));

	// Display Modes
	_toolButtonDisplayModes->setToolTip(tr("Display Modes"));
	if (_realisticBtn) _realisticAction->setToolTip(tr("Realistic Rendering (Shift+R)"));
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
	_sectionAction->setToolTip(tr("Clipping Planes"));

	// Exploded View
	_explodedAction->setToolTip(tr("Exploded View"));

	// Swap Visible View
	_swapVisibleAction->setToolTip(tr("Swap Visible"));

	// Axis
	_axisAction->setToolTip(tr("Show/Hide Axis"));
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
	else if (mode == "RayTraced")
	{
		_toolButtonRenderingMode->setDefaultAction(_rayTracedAction);
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
