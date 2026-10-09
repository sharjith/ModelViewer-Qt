#include "StatusBalloon.h"

#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPalette>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

StatusBalloon::StatusBalloon(QWidget* anchorWindow)
	: QFrame(anchorWindow, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus)
	, _anchor(anchorWindow)
{
	setAttribute(Qt::WA_ShowWithoutActivating);
	setFrameShape(QFrame::StyledPanel);
	setAutoFillBackground(true);
	QPalette colours = palette();
	colours.setColor(QPalette::Window, colours.color(QPalette::ToolTipBase));
	colours.setColor(QPalette::WindowText, colours.color(QPalette::ToolTipText));
	setPalette(colours);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(12, 8, 12, 8);
	_label = new QLabel(this);
	_label->setWordWrap(true);
	_label->setTextFormat(Qt::PlainText);
	_label->setMinimumWidth(360);
	_label->setMaximumWidth(460);
	layout->addWidget(_label);

	_timer = new QTimer(this);
	_timer->setSingleShot(true);
	connect(_timer, &QTimer::timeout, this, &QWidget::hide);
	hide();
}

void StatusBalloon::showText(const QString& text, int timeoutMs)
{
	_label->setText(text);
	_label->adjustSize();
	adjustSize();
	// Bottom-left of the window, just above the status bar (or the window's bottom edge without one).
	QPoint bottomLeft;
	if (auto* window = qobject_cast<QMainWindow*>(_anchor); window && window->statusBar())
		bottomLeft = window->statusBar()->mapToGlobal(QPoint(0, 0));
	else if (_anchor)
		bottomLeft = _anchor->mapToGlobal(QPoint(0, _anchor->height()));
	move(bottomLeft.x() + 12, bottomLeft.y() - height() - 8);
	show();
	raise();
	if (timeoutMs > 0)
		_timer->start(timeoutMs);
	else
		_timer->stop();
}

void StatusBalloon::mousePressEvent(QMouseEvent* event)
{
	hide();
	event->accept();
}
