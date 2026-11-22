#include "ScreenColorPicker.h"

#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QScreen>

#include <algorithm>

namespace
{
constexpr int kSourceSize = 13; // pixels shown in the magnifier (odd, so one is in the middle)
constexpr int kZoom = 9;
}

ScreenColorPicker::ScreenColorPicker(QScreen* screen, QWidget* parent)
	: QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool), _screen(screen)
{
	setAttribute(Qt::WA_DeleteOnClose);
	setMouseTracking(true);
	setCursor(Qt::CrossCursor);
	setFocusPolicy(Qt::StrongFocus);
}

void ScreenColorPicker::start()
{
	if (!_screen)
		_screen = QGuiApplication::primaryScreen();
	if (!_screen)
	{
		emit cancelled();
		close();
		return;
	}
	_shot = _screen->grabWindow(0).toImage();
	setGeometry(_screen->geometry());
	_cursor = mapFromGlobal(QCursor::pos());
	show();
	raise();
	activateWindow();
	setFocus();
}

QPoint ScreenColorPicker::imagePos(const QPoint& widgetPos) const
{
	if (_shot.isNull() || width() <= 0 || height() <= 0)
		return QPoint();
	const int x = int(double(widgetPos.x()) * _shot.width() / width());
	const int y = int(double(widgetPos.y()) * _shot.height() / height());
	return QPoint(std::clamp(x, 0, _shot.width() - 1), std::clamp(y, 0, _shot.height() - 1));
}

QColor ScreenColorPicker::colorAt(const QPoint& widgetPos) const
{
	if (_shot.isNull())
		return QColor();
	const QPoint p = imagePos(widgetPos);
	return QColor(_shot.pixel(p));
}

void ScreenColorPicker::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	if (_shot.isNull())
		return;
	painter.drawImage(rect(), _shot);

	// Magnifier: a circle showing the pixels around the cursor, with the colour's hex name under it.
	const int diameter = kSourceSize * kZoom;
	int left = _cursor.x() + 24, top = _cursor.y() + 24;
	if (left + diameter > width())
		left = _cursor.x() - 24 - diameter;
	if (top + diameter + 28 > height())
		top = _cursor.y() - 24 - diameter - 28;
	const QRect target(left, top, diameter, diameter);
	const QPoint center = imagePos(_cursor);
	const QRect source(center.x() - kSourceSize / 2, center.y() - kSourceSize / 2, kSourceSize, kSourceSize);

	painter.save();
	QPainterPath circle;
	circle.addEllipse(target);
	painter.setClipPath(circle);
	painter.fillRect(target, Qt::black);
	painter.drawImage(target, _shot, source); // no smoothing: the pixels stay square
	painter.restore();

	const double cell = double(diameter) / kSourceSize;
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setPen(QPen(QColor(255, 255, 255), 2.0));
	painter.setBrush(Qt::NoBrush);
	painter.drawEllipse(target);
	painter.setPen(QPen(QColor(0, 0, 0), 1.0));
	painter.drawRect(QRectF(target.left() + cell * (kSourceSize / 2), target.top() + cell * (kSourceSize / 2), cell, cell));

	const QString text = colorAt(_cursor).name(QColor::HexRgb).toUpper();
	const QRect label(target.left(), target.bottom() + 4, diameter, 22);
	painter.setPen(Qt::NoPen);
	painter.setBrush(QColor(20, 20, 20, 220));
	painter.drawRoundedRect(label, 4, 4);
	painter.setPen(Qt::white);
	painter.drawText(label, Qt::AlignCenter, text);
}

void ScreenColorPicker::mouseMoveEvent(QMouseEvent* event)
{
	_cursor = event->position().toPoint();
	update();
}

void ScreenColorPicker::mousePressEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton)
	{
		const QColor color = colorAt(event->position().toPoint());
		if (color.isValid())
			emit colorPicked(color);
		else
			emit cancelled();
	}
	else
		emit cancelled();
	close();
}

void ScreenColorPicker::keyPressEvent(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Escape)
	{
		emit cancelled();
		close();
		return;
	}
	QWidget::keyPressEvent(event);
}
