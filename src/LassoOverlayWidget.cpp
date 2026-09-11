#include "LassoOverlayWidget.h"

#include <QPainter>
#include <QPen>
#include <QPaintEvent>

LassoOverlayWidget::LassoOverlayWidget(QWidget* parent)
	: QWidget(parent)
{
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_TranslucentBackground);
	hide();
}

void LassoOverlayWidget::setPoints(const QPolygon& points)
{
	_points = points;
	update();
}

void LassoOverlayWidget::paintEvent(QPaintEvent*)
{
	if (_points.size() < 2)
		return;

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);

	QPen pen(QColor(255, 200, 0));
	pen.setWidthF(1.5);
	pen.setStyle(Qt::DashLine);
	painter.setPen(pen);
	painter.setBrush(QColor(255, 200, 0, 40));

	if (_points.size() >= 3)
		painter.drawPolygon(_points);
	else
		painter.drawPolyline(_points);
}
