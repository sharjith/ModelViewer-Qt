#pragma once
#include <QWidget>
#include <QMap>
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
signals:
    void commandRequested(const QString& command);
protected:
    void resizeEvent(QResizeEvent* event) override;
private:
    void updateScrollButtons();
    QScrollArea* _scroll;
    QWidget* _content;
    QToolButton* _left;
    QToolButton* _right;
    QMenu* _analysisMenu;
    QMenu* _mergeMenu;
    QMap<QString, QAction*> _meshActions;
};
