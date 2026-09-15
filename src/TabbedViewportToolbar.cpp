#include "TabbedViewportToolbar.h"
#include "ViewToolbar.h"
#include "ToolsToolbar.h"
#include "LanguageManager.h"
#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QHBoxLayout>
#include <QPropertyAnimation>
#include <QStackedWidget>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>

TabbedViewportToolbar::TabbedViewportToolbar(QWidget* viewport) : QWidget(viewport), _viewport(viewport)
{
    setObjectName(QStringLiteral("tabbedViewportToolbar"));
    setAttribute(Qt::WA_StyledBackground);
    // Paint the translucent backing once, at the shared container level.
    setAutoFillBackground(false);
    setStyleSheet(QStringLiteral(
        "QToolTip { color: #ffffff; background-color: #2a82da; border: 1px solid white; }"
        "QWidget#tabbedViewportToolbar { background: rgba(255, 255, 255, 100); border: 1px solid rgba(100, 100, 100, 160); border-radius: 5px; }"
        "QStackedWidget#toolbarPages { background: transparent; border: none; }"
        "QTabBar#toolbarTabs { background: transparent; }"
        "QTabBar#toolbarTabs::tab { color: #1f4e79; background: rgba(215, 235, 250, 220); border: 1px solid rgba(42, 130, 218, 140); border-radius: 4px; padding: 3px 22px; font-size: 12px; }"
        "QTabBar#toolbarTabs::tab:hover { background: #c2e1f7; }"
        "QTabBar#toolbarTabs::tab:selected { color: #ffffff; background: #2a82da; border-color: #ffffff; }"
        "QToolButton#toolbarPin { background: transparent; border: 1px solid transparent; border-radius: 4px; }"
        "QToolButton#toolbarPin:hover { background: rgba(0, 120, 215, 50); }"
        "QToolButton#toolbarPin:checked { background: rgba(0, 120, 215, 70); border-color: rgba(0, 120, 215, 150); }"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(3, 2, 3, 2);
    layout->setSpacing(0);
    auto* tabsRow = new QHBoxLayout;
    _tabs = new QTabBar(this);
    _tabs->setObjectName(QStringLiteral("toolbarTabs"));
    _tabs->setAutoFillBackground(false);
    _tabs->setExpanding(false);
    _tabs->setDrawBase(false);
    _tabs->addTab(tr("Standard"));
    _tabs->addTab(tr("Tools"));
    _pinButton = new QToolButton(this);
    _pinButton->setObjectName(QStringLiteral("toolbarPin"));
    _pinButton->setCheckable(true);
    _pinButton->setAutoRaise(true);
    _pinButton->setFocusPolicy(Qt::TabFocus);
    _pinButton->setIconSize(QSize(18, 18));
    _pinButton->setFixedSize(26, 26);
    // Balance the pin's width so the tabs remain centred in the toolbar.
    tabsRow->addSpacing(26);
    tabsRow->addStretch();
    tabsRow->addWidget(_tabs);
    tabsRow->addStretch();
    tabsRow->addWidget(_pinButton);
    layout->addLayout(tabsRow);
    _pages = new QStackedWidget(this);
    _pages->setObjectName(QStringLiteral("toolbarPages"));
    _pages->setAutoFillBackground(false);
    _standard = new ViewToolbar(viewport, _pages);
    _tools = new ToolsToolbar(_pages);
    _pages->addWidget(_standard);
    _pages->addWidget(_tools);
    layout->addWidget(_pages);
    _animation = new QPropertyAnimation(this, "pos", this);
    _animation->setDuration(220);
    _animation->setEasingCurve(QEasingCurve::OutCubic);
    _hideTimer = new QTimer(this);
    _hideTimer->setSingleShot(true);
    _hideTimer->setInterval(2000);
    connect(_pinButton, &QToolButton::toggled, this, [](bool pinned) {
        setPinnedPreference(pinned);
    });
    connect(_hideTimer, &QTimer::timeout, this, &TabbedViewportToolbar::tryHide);
    connect(_tabs, &QTabBar::currentChanged, this, [this](int index) {
        _standard->stopScrolling();
        _pages->setCurrentIndex(index);
        reveal();
    });
    connect(_tools, &ToolsToolbar::commandRequested, this, &TabbedViewportToolbar::commandRequested);
    connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
        _tabs->setTabText(0, tr("Standard"));
        _tabs->setTabText(1, tr("Tools"));
        updatePinButton();
        reposition();
    });
    installEventFilter(this);
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
        if (now && (now == this || isAncestorOf(now))) reveal();
        else if (!_pinned && !_hideTimer->isActive()) _hideTimer->start();
    });
    reposition();
    applyPinned(QSettings().value(QStringLiteral("ViewportToolbar/pinned"), false).toBool());
}
void TabbedViewportToolbar::reposition()
{
    _animation->stop();
    _tabs->ensurePolished();
    const int desired = std::max(_standard->sizeHint().width(), _tools->sizeHint().width()) + 6;
    const int w = std::max(1, std::min(desired, _viewport->width() - 20));
    const QMargins margins = layout()->contentsMargins();
    const int pageHeight = std::max(_standard->sizeHint().height(), _tools->sizeHint().height());
    const int h = pageHeight + std::max(_tabs->sizeHint().height(), _pinButton->height())
        + margins.top() + margins.bottom();
    setFixedSize(w, h);
    move((_viewport->width() - w) / 2, _revealed ? _viewport->height() - h - 10 : _viewport->height() + 2);
}
void TabbedViewportToolbar::reveal()
{
    _hideTimer->stop();
    raise();
    if (_revealed) return;
    _revealed = true;
    _animation->stop();
    _animation->setStartValue(pos());
    _animation->setEndValue(QPoint(x(), _viewport->height() - height() - 10));
    _animation->start();
}
bool TabbedViewportToolbar::isInteracting() const
{
    QWidget* focus = QApplication::focusWidget();
    return underMouse() || (focus && (focus == this || isAncestorOf(focus)))
        || _standard->isFlyoutMenuVisible() || _tools->isFlyoutMenuVisible();
}
void TabbedViewportToolbar::tryHide()
{
    if (_pinned) {
        _hideTimer->stop();
        return;
    }
    const QPoint p = _viewport->mapFromGlobal(QCursor::pos());
    if (isInteracting() || (p.y() >= _viewport->height() - 30 && p.y() <= _viewport->height() && p.x() >= x() && p.x() <= x() + width())) {
        _hideTimer->start();
        return;
    }
    if (!_revealed) return;
    _revealed = false;
    _standard->stopScrolling();
    _animation->stop();
    _animation->setStartValue(pos());
    _animation->setEndValue(QPoint(x(), _viewport->height() + 2));
    _animation->start();
}
void TabbedViewportToolbar::trackPointer(const QPoint& p)
{
    if (_pinned || (p.y() >= _viewport->height() - 30 && p.x() >= x() && p.x() <= x() + width()) || isInteracting()) reveal();
    else if (!_hideTimer->isActive()) _hideTimer->start();
}
bool TabbedViewportToolbar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == this && event->type() == QEvent::Enter) reveal();
    if (watched == this && event->type() == QEvent::Leave && !_pinned) _hideTimer->start();
    return QWidget::eventFilter(watched, event);
}

void TabbedViewportToolbar::applyPinned(bool pinned)
{
    const bool changed = _pinned != pinned;
    _pinned = pinned;
    const QSignalBlocker blocker(_pinButton);
    _pinButton->setChecked(pinned);
    updatePinButton();
    if (pinned) reveal();
    else _hideTimer->start();
    if (changed) emit pinnedChanged(pinned);
}

void TabbedViewportToolbar::setPinnedPreference(bool pinned)
{
    QSettings().setValue(QStringLiteral("ViewportToolbar/pinned"), pinned);
    for (QWidget* widget : QApplication::allWidgets()) {
        if (auto* toolbar = qobject_cast<TabbedViewportToolbar*>(widget))
            toolbar->applyPinned(pinned);
    }
}

void TabbedViewportToolbar::updatePinButton()
{
    _pinButton->setIcon(QIcon(_pinned ? QStringLiteral(":/icons/res/pin.png")
                                   : QStringLiteral(":/icons/res/unpin.png")));
    const QString text = _pinned ? tr("Allow toolbar to hide automatically") : tr("Keep toolbar visible");
    _pinButton->setToolTip(text);
    _pinButton->setAccessibleName(text);
}
