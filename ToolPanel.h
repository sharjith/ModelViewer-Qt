#ifndef TOOLPANEL_H
#define TOOLPANEL_H

#include <QTabWidget>
#include <QEvent>

class QPropertyAnimation;
class ToolPanel : public QTabWidget
{
	Q_OBJECT
public:
	explicit ToolPanel(QWidget* parent = nullptr);

	bool autoHide() const;
	void setAutoHide(bool autoHide);

protected:
	bool eventFilter(QObject* object, QEvent* event);

private:
	QPropertyAnimation* _animation;
	bool _autoHide;

signals:
};

#endif // TOOLPANEL_H
