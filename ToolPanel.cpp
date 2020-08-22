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
	_animation->setEasingCurve(QEasingCurve::InOutElastic);
}

bool ToolPanel::eventFilter(QObject* object, QEvent* event)
{
	if ((parent() == object) && (event->type() == QEvent::MouseMove))
	{
		QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
		if (!(mouseEvent->buttons() & Qt::LeftButton)
			&& !(mouseEvent->buttons() & Qt::RightButton)
			&& !(mouseEvent->buttons() & Qt::MiddleButton))
		{
			if (mouseEvent->pos().y() > static_cast<QWidget*>(parent())->height() - height())
			{
				if (isHidden() && (_animation->state() != _animation->Running))
				{
					_animation->setStartValue(QRect(6, 750, 0, sizeHint().height()));
					_animation->setEndValue(this->geometry());
					disconnect(_animation, SIGNAL(finished()), this, SLOT(hide()));
					_animation->start();
					show();
				}
			}
			else if (_animation->state() != _animation->Running)
			{
				if (!isHidden())
				{
					_animation->setEndValue(QRect(6, 750, 0, sizeHint().height()));
					_animation->setStartValue(this->geometry());
					connect(_animation, SIGNAL(finished()), this, SLOT(hide()));
					_animation->start();
					//hide();
				}
			}
		}
	}
	return QWidget::eventFilter(object, event);
}