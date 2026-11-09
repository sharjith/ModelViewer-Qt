#pragma once
#include <QWidget>

class QToolButton;
class QMenu;
class QAction;
class QShowEvent;

// The "File" tab of the viewport's tabbed toolbar: the commands of the File and Edit menus a user reaches for most (New, Open, Recent,
// Save, Save As, Import, Export, Save as 3D PDF, Undo, Redo). Each button is bound to the main window's own QAction, so the icon, tooltip,
// enabled state and behaviour are the menu's, and nothing is duplicated here.
class FileToolbar : public QWidget
{
    Q_OBJECT
public:
    explicit FileToolbar(QWidget* parent = nullptr);
    // True while the Recent Files menu is open (the toolbar must not hide under it).
    bool isMenuVisible() const;
signals:
    // The size of the buttons changed (a language switch): the container resizes.
    void contentSizeChanged();
protected:
    void showEvent(QShowEvent* event) override;
private:
    void populate();
    bool _populated = false;
    QToolButton* addActionButton(const QString& actionName);
    void addSeparator();
    QToolButton* _recentButton = nullptr;
    QMenu* _recentMenu = nullptr;
    QAction* _recentEmptyAction = nullptr;
};
