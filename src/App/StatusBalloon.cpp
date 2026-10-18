#include "StatusBalloon.h"

#include <QApplication>
#include <QColor>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPalette>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace
{
	double relativeLuminance(const QColor& colour)
	{
		auto channel = [](double c) { return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
		return 0.2126 * channel(colour.redF()) + 0.7152 * channel(colour.greenF()) + 0.0722 * channel(colour.blueF());
	}

	double contrastRatio(const QColor& a, const QColor& b)
	{
		const double la = relativeLuminance(a), lb = relativeLuminance(b);
		return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
	}
}

StatusBalloon::StatusBalloon(QWidget* anchorWindow)
	: QFrame(anchorWindow, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus)
	, _anchor(anchorWindow)
{
	setAttribute(Qt::WA_ShowWithoutActivating);
	setObjectName(QStringLiteral("statusBalloon"));

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

// The colours are stated outright (the application theme styles tool-tip windows and labels, and the tooltip palette can come out black on
// black): a panel a little off the window colour, with the window text colour, and a check that the two are readable together - when they
// are not, a fixed light-on-dark or dark-on-light pair is used instead.
void StatusBalloon::applyColours()
{
	const QPalette appPalette = QApplication::palette();
	const QColor window = appPalette.color(QPalette::Window);
	const bool dark = window.lightnessF() < 0.5;
	QColor background = dark ? window.lighter(135) : window.darker(104);
	QColor text = appPalette.color(QPalette::WindowText);
	if (contrastRatio(background, text) < 4.5)
	{
		background = dark ? QColor(52, 56, 64) : QColor(255, 255, 232);
		text = dark ? QColor(235, 238, 242) : QColor(30, 30, 30);
	}
	const QColor border = dark ? QColor(120, 128, 140) : QColor(140, 140, 140);
	// A style sheet on the balloon itself overrides whatever the application's one says about frames and labels.
	setStyleSheet(QStringLiteral("QFrame#statusBalloon { background-color: %1; border: 1px solid %2; border-radius: 4px; }"
	                             "QFrame#statusBalloon QLabel { color: %3; background: transparent; }")
		.arg(background.name(), border.name(), text.name()));
}

void StatusBalloon::showText(const QString& text, int timeoutMs)
{
	applyColours(); // the theme may have changed since the last time
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
