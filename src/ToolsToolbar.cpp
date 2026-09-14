#include "ToolsToolbar.h"
#include "LanguageManager.h"
#include <QAction>
#include <QFrame>
#include <QHBoxLayout>
#include <QMenu>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>

ToolsToolbar::ToolsToolbar(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(76);
    setAutoFillBackground(false);
    setStyleSheet(QStringLiteral("QToolButton { background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 5px; }"
        "QToolButton:hover { background: rgba(0, 120, 215, 50); border-color: #0078D7; }"
        "QToolButton:pressed { background: rgba(0, 120, 215, 100); border-color: #005A9E; }"));
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(2, 2, 2, 2);
    row->setSpacing(2);
    _left = new QToolButton(this);
    _left->setArrowType(Qt::LeftArrow);
    _right = new QToolButton(this);
    _right->setArrowType(Qt::RightArrow);
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
    auto add = [this, commands](const char* text, const char* icon, const char* command) {
        auto* action = new QAction(QIcon(QStringLiteral(":/icons/res/") + QLatin1String(icon) + QStringLiteral(".png")), tr(text), this);
        auto translate = [action, text]() { action->setText(ToolsToolbar::tr(text)); action->setToolTip(ToolsToolbar::tr(text)); };
        connect(&LanguageManager::instance(), &LanguageManager::languageChanged, action, translate);
        translate();
        connect(action, &QAction::triggered, this, [this, command]() { emit commandRequested(QLatin1String(command)); });
        auto* button = new QToolButton(_content);
        button->setDefaultAction(action);
        button->setAutoRaise(true);
        button->setIconSize(QSize(48, 48));
        commands->addWidget(button);
        return button;
    };
    auto separator = [commands]() { auto* line = new QFrame; line->setFrameShape(QFrame::VLine); commands->addWidget(line); };
    add(QT_TR_NOOP("Measure"), "measure", "measure");
    add(QT_TR_NOOP("Annotate"), "annotate", "annotate");
    add(QT_TR_NOOP("Mass Properties"), "mass_properties", "mass");
    auto* analysis = add(QT_TR_NOOP("Surface Analysis"), "surface_analysis", "analysis");
    analysis->setMenu(_analysisMenu);
    analysis->setPopupMode(QToolButton::MenuButtonPopup);
    const char* modeNames[] = {QT_TR_NOOP("Curvature / Zebra Stripe"), QT_TR_NOOP("Wall Thickness / Draft Angle"), QT_TR_NOOP("Deviation")};
    const char* modeIcons[] = {"curvature_analysis", "wall_thickness", "deviation_analysis"};
    const char* modeCommands[] = {"curvature", "thickness", "deviation"};
    for (int i = 0; i < 3; ++i) {
        auto* action = _analysisMenu->addAction(QIcon(QStringLiteral(":/icons/res/") + QLatin1String(modeIcons[i]) + QStringLiteral(".png")), tr(modeNames[i]));
        const char* name = modeNames[i];
        connect(&LanguageManager::instance(), &LanguageManager::languageChanged, action, [action, name]() { action->setText(ToolsToolbar::tr(name)); });
        const QString command = QLatin1String(modeCommands[i]);
        connect(action, &QAction::triggered, this, [this, command]() { emit commandRequested(command); });
    }
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
    connect(_left, &QToolButton::clicked, this, [this]() { _scroll->horizontalScrollBar()->setValue(_scroll->horizontalScrollBar()->value() - 140); });
    connect(_right, &QToolButton::clicked, this, [this]() { _scroll->horizontalScrollBar()->setValue(_scroll->horizontalScrollBar()->value() + 140); });
    connect(_scroll->horizontalScrollBar(), &QScrollBar::valueChanged, this, &ToolsToolbar::updateScrollButtons);
    connect(_scroll->horizontalScrollBar(), &QScrollBar::rangeChanged, this, &ToolsToolbar::updateScrollButtons);
    _left->setAutoRepeat(true);
    _right->setAutoRepeat(true);
    _left->setAccessibleName(tr("Scroll left"));
    _right->setAccessibleName(tr("Scroll right"));
    updateScrollButtons();
}
QSize ToolsToolbar::sizeHint() const { return QSize(_content->width() + 8, 76); }
bool ToolsToolbar::isFlyoutMenuVisible() const { return _analysisMenu->isVisible(); }
void ToolsToolbar::resizeEvent(QResizeEvent* event) { QWidget::resizeEvent(event); updateScrollButtons(); }
void ToolsToolbar::updateScrollButtons()
{
    const bool overflow = _content->width() > width() - 8;
    _left->setVisible(overflow);
    _right->setVisible(overflow);
    _left->setEnabled(_scroll->horizontalScrollBar()->value() > 0);
    _right->setEnabled(_scroll->horizontalScrollBar()->value() < _scroll->horizontalScrollBar()->maximum());
}
