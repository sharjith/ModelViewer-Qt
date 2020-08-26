#include "ToolPanel.h"
#include <QPropertyAnimation>
#include <QMouseEvent>
#include <QDebug>

ToolPanel::ToolPanel(QWidget* parent)
	: QTabWidget(parent)
	, _animation(new QPropertyAnimation(this, "geometry"))
{
	parent->setMouseTracking(true);
	parent->installEventFilter(this);
	_animation->setDuration(250);
	_animation->setEasingCurve(QEasingCurve::InOutBounce);
}

bool ToolPanel::eventFilter(QObject* object, QEvent* event)
{
	if ((parent() == object) && (event->type() == QEvent::MouseMove))
	{
		setFocus();
		QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
		if (!(mouseEvent->buttons() == Qt::LeftButton)
			&& !(mouseEvent->buttons() == Qt::RightButton)
			&& !(mouseEvent->buttons() == Qt::MiddleButton))
		{
			if (mouseEvent->pos().y() > static_cast<QWidget*>(parent())->height() - sizeHint().height())
			{
				if (isHidden() && (_animation->state() != _animation->Running))
				{
					QRect startVal = QRect(geometry().x(), geometry().y() + sizeHint().height(), sizeHint().width(), 0);
					_animation->setStartValue(startVal);
					_animation->setEndValue(geometry());
					disconnect(_animation, SIGNAL(finished()), this, SLOT(hide()));
					_animation->start();
					show();
				}
			}
			else if (_animation->state() != _animation->Running)
			{
				if (!isHidden())
				{
					QRect endVal = QRect(geometry().x(), geometry().y() + sizeHint().height(), sizeHint().width(), 0);
					_animation->setEndValue(endVal);
					_animation->setStartValue(geometry());
					connect(_animation, SIGNAL(finished()), this, SLOT(hide()));
					_animation->start();
					//hide();
				}
			}
		}
	}
	return QWidget::eventFilter(object, event);
}