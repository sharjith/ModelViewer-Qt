#include "FileToolbar.h"
#include "MainWindow.h"
#include "LanguageManager.h"
#include "FlyOutViewButton.h"
#include <QAction>
#include <QFrame>
#include <QHBoxLayout>
#include <QMenu>
#include <QShowEvent>
#include <QToolButton>

FileToolbar::FileToolbar(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(64);
    setAutoFillBackground(false);
    setStyleSheet(QStringLiteral("QToolButton { background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 5px; }"
        "QToolButton:hover { background: rgba(0, 120, 215, 50); border-color: #0078D7; }"
        "QToolButton:pressed { background: rgba(0, 120, 215, 100); border-color: #005A9E; }"
        "QToolButton:disabled { background: transparent; }"
        "QToolButton::menu-indicator { image: none; }"));
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(2, 2, 2, 2);
    row->setSpacing(2);
    populate();

    connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
        if (_recentButton)
        {
            _recentButton->setToolTip(tr("Recent Files"));
            _recentButton->setAccessibleName(tr("Recent Files"));
        }
        if (_recentEmptyAction)
            _recentEmptyAction->setText(tr("No recent files"));
        emit contentSizeChanged();
    });
}

// The buttons are bound to the main window's actions. A document (and its toolbar) can be created before the main window has made them,
// so an empty toolbar fills itself when it is first shown.
void FileToolbar::populate()
{
    if (_populated)
        return;
    MainWindow* window = MainWindow::mainWindow();
    if (!window || !window->findChild<QAction*>(QStringLiteral("actionOpen")))
        return;
    _populated = true;
    auto* row = static_cast<QHBoxLayout*>(layout());

    addActionButton(QStringLiteral("actionNew"));
    addActionButton(QStringLiteral("actionOpen"));

    // Recent files: the File menu's own submenu, opened from a button.
    QMenu* recentMenu = window->recentFilesMenu();
    if (recentMenu)
    {
        _recentButton = new FlyOutViewButton(this);   // the small arrow the grouped command buttons carry
        _recentButton->setIcon(QIcon(QStringLiteral(":/icons/res/recent_files.png")));
        _recentButton->setIconSize(QSize(40, 40));
        _recentButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
        _recentButton->setPopupMode(QToolButton::InstantPopup);
        // A menu of its own in the style of the fly-out menus, holding the very same actions as the File menu's Recent submenu (the
        // actions are shared, so the list, its numbering, Shift-click and Clear work as there). The shared menu itself keeps the menu bar's
        // look.
        _recentMenu = new QMenu(this);
        _recentMenu->setStyleSheet(FlyOutViewButton::menuStyleSheet());
        _recentEmptyAction = _recentMenu->addAction(tr("No recent files"));
        _recentEmptyAction->setEnabled(false);
        _recentMenu->addActions(recentMenu->actions());
        // the File menu refreshes its list when it is about to show; this one forwards that, then shows the placeholder when nothing is listed
        connect(_recentMenu, &QMenu::aboutToShow, recentMenu, &QMenu::aboutToShow);
        connect(_recentMenu, &QMenu::aboutToShow, this, [this, recentMenu]() {
            bool any = false;
            for (const QAction* act : recentMenu->actions())
                any = any || (act->isVisible() && !act->isSeparator());
            _recentEmptyAction->setVisible(!any);
        });
        _recentButton->setMenu(_recentMenu);
        _recentButton->setToolTip(tr("Recent Files"));
        _recentButton->setAccessibleName(tr("Recent Files"));
        row->addWidget(_recentButton);
    }

    addSeparator();
    addActionButton(QStringLiteral("actionSave"));
    addActionButton(QStringLiteral("actionSave_As"));
    addSeparator();
    addActionButton(QStringLiteral("actionImport"));
    addActionButton(QStringLiteral("actionExport"));
    addActionButton(QStringLiteral("actionExport3DPdf"));
    addActionButton(QStringLiteral("actionPrint"));
    addSeparator();
    addActionButton(QStringLiteral("actionUndo"));
    addActionButton(QStringLiteral("actionRedo"));
    row->addStretch();
    emit contentSizeChanged();
}

void FileToolbar::showEvent(QShowEvent* event)
{
    populate();
    QWidget::showEvent(event);
}

QToolButton* FileToolbar::addActionButton(const QString& actionName)
{
    MainWindow* window = MainWindow::mainWindow();
    QAction* action = window ? window->findChild<QAction*>(actionName) : nullptr;
    if (!action)
        return nullptr;
    auto* button = new QToolButton(this);
    button->setDefaultAction(action);   // icon, tooltip, enabled state and click come from the menu's action
    button->setIconSize(QSize(40, 40));
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    static_cast<QHBoxLayout*>(layout())->addWidget(button);
    return button;
}

void FileToolbar::addSeparator()
{
    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::VLine);
    line->setFrameShadow(QFrame::Plain);
    line->setStyleSheet(QStringLiteral("color: rgba(100, 100, 100, 120);"));
    static_cast<QHBoxLayout*>(layout())->addWidget(line);
}

bool FileToolbar::isMenuVisible() const
{
    return _recentMenu && _recentMenu->isVisible();
}
