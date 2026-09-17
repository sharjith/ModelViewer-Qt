#pragma once
#include <QWidget>
#include <QMap>
#include <QPointer>
class QAction;
class QScrollArea;
class QToolButton;
class QMenu;

class ToolsToolbar : public QWidget
{
    Q_OBJECT
public:
    explicit ToolsToolbar(QWidget* parent = nullptr);
    QSize sizeHint() const override;
    bool isFlyoutMenuVisible() const;
    void setMeshToolAvailability(const QMap<QString, QString>& disabledReasons);
    void trackToolWindow(const QString& command, QWidget* window);
signals:
    void commandRequested(const QString& command);
protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    void updateScrollButtons();
    void refreshActiveTools();
    QScrollArea* _scroll;
    QWidget* _content;
    QToolButton* _left;
    QToolButton* _right;
    QMenu* _analysisMenu;
    QMenu* _mergeMenu;
    QMap<QString, QAction*> _meshActions;
    QMap<QString, QToolButton*> _commandButtons;
    QMap<QToolButton*, QPointer<QWidget>> _toolWindows;
};
