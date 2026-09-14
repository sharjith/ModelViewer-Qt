#pragma once
#include <QWidget>
class ViewToolbar;
class ToolsToolbar;
class QTabBar;
class QStackedWidget;
class QPropertyAnimation;
class QTimer;
class QToolButton;

class TabbedViewportToolbar : public QWidget
{
    Q_OBJECT
public:
    explicit TabbedViewportToolbar(QWidget* viewport);
    ViewToolbar* viewToolbar() const { return _standard; }
    void reposition();
    void trackPointer(const QPoint& viewportPosition);
signals:
    void commandRequested(const QString& command);
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    void reveal();
    void tryHide();
    bool isInteracting() const;
    void applyPinned(bool pinned);
    void updatePinButton();
    QWidget* _viewport;
    ViewToolbar* _standard;
    ToolsToolbar* _tools;
    QTabBar* _tabs;
    QStackedWidget* _pages;
    QPropertyAnimation* _animation;
    QTimer* _hideTimer;
    QToolButton* _pinButton;
    bool _pinned = false;
    bool _revealed = true;
};
