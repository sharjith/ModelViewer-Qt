#include "ToolsToolbar.h"
#include "FlyOutViewButton.h"
#include "LanguageManager.h"
#include <QAction>
#include <QFrame>
#include <QEvent>
#include <QHBoxLayout>
#include <QMenu>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>

ToolsToolbar::ToolsToolbar(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(64);
    setAutoFillBackground(false);
    setStyleSheet(QStringLiteral("QToolButton { background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 5px; }"
        "QToolButton:hover { background: rgba(0, 120, 215, 50); border-color: #0078D7; }"
        "QToolButton:pressed { background: rgba(0, 120, 215, 100); border-color: #005A9E; }"
        "QToolButton:checked { background: rgba(0, 150, 100, 100); border-color: #008000; }"));
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(2, 2, 2, 2);
    row->setSpacing(2);
    _left = new QToolButton(this);
    _left->setArrowType(Qt::LeftArrow);
    _left->setFixedSize(20, 56);
    _right = new QToolButton(this);
    _right->setArrowType(Qt::RightArrow);
    _right->setFixedSize(20, 56);
    _scroll = new QScrollArea(this);
    _scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
    _scroll->viewport()->setAutoFillBackground(false);
    _scroll->setFrameShape(QFrame::NoFrame);
    _scroll->setWidgetResizable(true);
    _scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _content = new QWidget;
    auto* commands = new QHBoxLayout(_content);
    commands->setContentsMargins(2, 2, 2, 2);
    commands->setSpacing(3);
    _scroll->setWidget(_content);
    // QScrollArea::setWidget enables auto-fill on its content widget.
    _content->setAutoFillBackground(false);
    row->addWidget(_left);
    row->addWidget(_scroll, 1);
    row->addWidget(_right);
    _analysisMenu = new QMenu(this);
    auto add = [this, commands](const char* text, const char* icon, const char* command, bool flyout = false) {
        auto* action = new QAction(QIcon(QStringLiteral(":/icons/res/") + QLatin1String(icon) + QStringLiteral(".png")), tr(text), this);
        auto translate = [action, text]() { action->setText(ToolsToolbar::tr(text)); action->setToolTip(ToolsToolbar::tr(text)); };
        connect(&LanguageManager::instance(), &LanguageManager::languageChanged, action, translate);
        translate();
        connect(action, &QAction::triggered, this, [this, command]() {
            emit commandRequested(QLatin1String(command));
            refreshActiveTools();
        });
        QToolButton* button = flyout ? new FlyOutViewButton(_content) : new QToolButton(_content);
        button->setDefaultAction(action);
        button->setAutoRaise(true);
        button->setIconSize(QSize(40, 40));
        commands->addWidget(button);
        _commandButtons.insert(QLatin1String(command), button);
        return button;
    };
    auto separator = [commands]() { auto* line = new QFrame; line->setFrameShape(QFrame::VLine); commands->addWidget(line); };
    add(QT_TR_NOOP("Measure"), "measure", "measure");
    add(QT_TR_NOOP("Annotate"), "annotate", "annotate");
    add(QT_TR_NOOP("Mass Properties"), "mass_properties", "mass");
    auto* meshInfo = add(QT_TR_NOOP("Mesh Info"), "mesh_info", "mesh_info");
    _meshActions.insert("mesh_info", meshInfo->defaultAction());
    auto* analysis = add(QT_TR_NOOP("Surface Analysis"), "surface_analysis", "analysis", true);
    _analysisMenu->setStyleSheet(FlyOutViewButton::menuStyleSheet());
    _analysisMenu->addAction(analysis->defaultAction());
    connect(_analysisMenu, &QMenu::triggered, analysis, &QToolButton::setDefaultAction);
    connect(_analysisMenu, &QMenu::triggered, this, &ToolsToolbar::refreshActiveTools);
    analysis->setMenu(_analysisMenu);
    analysis->setPopupMode(QToolButton::DelayedPopup);
    const char* modeNames[] = {QT_TR_NOOP("Curvature / Zebra Stripe"), QT_TR_NOOP("Wall Thickness / Draft Angle"), QT_TR_NOOP("Deviation")};
    const char* modeIcons[] = {"curvature_analysis", "wall_thickness", "deviation_analysis"};
    const char* modeCommands[] = {"curvature", "thickness", "deviation"};
    for (int i = 0; i < 3; ++i) {
        auto* action = _analysisMenu->addAction(QIcon(QStringLiteral(":/icons/res/") + QLatin1String(modeIcons[i]) + QStringLiteral(".png")), tr(modeNames[i]));
        const char* name = modeNames[i];
        connect(&LanguageManager::instance(), &LanguageManager::languageChanged, action, [action, name]() { action->setText(ToolsToolbar::tr(name)); });
        const QString command = QLatin1String(modeCommands[i]);
        connect(action, &QAction::triggered, this, [this, command]() {
            emit commandRequested(command);
            refreshActiveTools();
        });
    }
    separator();
    auto meshTool = [this, &add](const char* name, const char* command) {
        auto* button = add(name, command, command);
        _meshActions.insert(QLatin1String(command), button->defaultAction());
        return button;
    };
    meshTool(QT_TR_NOOP("Split by Connectivity"), "split_by_connectivity");
    auto* merge = meshTool(QT_TR_NOOP("Merge Selected"), "merge_selected");
    _mergeMenu = new QMenu(this);
    _mergeMenu->setToolTipsVisible(true);
    _mergeMenu->addAction(merge->defaultAction());
    auto* adjacency = _mergeMenu->addAction(QIcon(":/icons/res/merge_by_adjacency.png"), tr("Merge by Adjacency"));
    _meshActions.insert("merge_by_adjacency", adjacency);
    connect(adjacency, &QAction::triggered, this, [this] { emit commandRequested("merge_by_adjacency"); });
    connect(&LanguageManager::instance(), &LanguageManager::languageChanged, adjacency, [adjacency] {
        adjacency->setText(ToolsToolbar::tr("Merge by Adjacency"));
    });
    merge->setMenu(_mergeMenu);
    merge->setPopupMode(QToolButton::MenuButtonPopup);
    meshTool(QT_TR_NOOP("Mesh Union"), "mesh_union");
    separator();
    meshTool(QT_TR_NOOP("Group"), "group_meshes");
    meshTool(QT_TR_NOOP("Duplicate"), "duplicate_meshes");
    separator();
    add(QT_TR_NOOP("Shrink Wrap"), "shrink_wrap", "shrink");
    add(QT_TR_NOOP("Subdivide Surface"), "subdivide_surface", "subdivide");
    add(QT_TR_NOOP("Reconstruct Surface"), "reconstruct_surface", "reconstruct");
    add(QT_TR_NOOP("Repair Mesh"), "repair_mesh", "repair");
    add(QT_TR_NOOP("Fill Holes"), "fill_holes", "fill");
    add(QT_TR_NOOP("Generate UVs"), "generate_uvs", "uv");
    separator();
    add(QT_TR_NOOP("Export Report"), "export_report", "report");
    add(QT_TR_NOOP("Batch Render Views"), "batch_render_views", "batch");
    // Resolve style-dependent button metrics before freezing content width.
    _content->ensurePolished();
    _content->setFixedWidth(commands->sizeHint().width());
    connect(_left, &QToolButton::clicked, this, [this]() { _scroll->horizontalScrollBar()->setValue(_scroll->horizontalScrollBar()->value() - 110); });
    connect(_right, &QToolButton::clicked, this, [this]() { _scroll->horizontalScrollBar()->setValue(_scroll->horizontalScrollBar()->value() + 110); });
    connect(_scroll->horizontalScrollBar(), &QScrollBar::valueChanged, this, &ToolsToolbar::updateScrollButtons);
    connect(_scroll->horizontalScrollBar(), &QScrollBar::rangeChanged, this, &ToolsToolbar::updateScrollButtons);
    _left->setAutoRepeat(true);
    _right->setAutoRepeat(true);
    _left->setAccessibleName(tr("Scroll left"));
    _right->setAccessibleName(tr("Scroll right"));
    updateScrollButtons();
    setMeshToolAvailability({});
}
QSize ToolsToolbar::sizeHint() const { return QSize(_content->width() + 8, 64); }
void ToolsToolbar::trackToolWindow(const QString& command, QWidget* window)
{
    auto* button = _commandButtons.value(command);
    if (!button || !window || _toolWindows.value(button) == window) return;
    _toolWindows.insert(button, window);
    window->installEventFilter(this);
    connect(window, &QObject::destroyed, this, &ToolsToolbar::refreshActiveTools);
    refreshActiveTools();
}

void ToolsToolbar::refreshActiveTools()
{
    for (auto it = _toolWindows.cbegin(); it != _toolWindows.cend(); ++it) {
        auto* button = it.key();
        const bool active = it.value() && !it.value()->isHidden();
        const auto actions = button->menu() ? button->menu()->actions()
                                           : QList<QAction*>{button->defaultAction()};
        for (auto* action : actions) {
            action->setCheckable(true);
            action->setChecked(active && action == button->defaultAction());
        }
    }
}

bool ToolsToolbar::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Show || event->type() == QEvent::Hide)
        refreshActiveTools();
    return QWidget::eventFilter(watched, event);
}

bool ToolsToolbar::isFlyoutMenuVisible() const { return _analysisMenu->isVisible() || _mergeMenu->isVisible(); }
void ToolsToolbar::setMeshToolAvailability(const QMap<QString, QString>& disabledReasons)
{
    for (auto it = _meshActions.cbegin(); it != _meshActions.cend(); ++it) {
        const QString reason = disabledReasons.value(it.key(), tr("Select at least one mesh."));
        it.value()->setEnabled(reason.isEmpty());
        it.value()->setToolTip(it.value()->text() + (reason.isEmpty() ? QString() : "\n" + reason));
    }
}
void ToolsToolbar::resizeEvent(QResizeEvent* event) { QWidget::resizeEvent(event); updateScrollButtons(); }
void ToolsToolbar::updateScrollButtons()
{
    const bool overflow = _content->width() > width() - 8;
    _left->setVisible(overflow);
    _right->setVisible(overflow);
    _left->setEnabled(_scroll->horizontalScrollBar()->value() > 0);
    _right->setEnabled(_scroll->horizontalScrollBar()->value() < _scroll->horizontalScrollBar()->maximum());
}
