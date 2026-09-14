#pragma once
#include <QWidget>
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
};
