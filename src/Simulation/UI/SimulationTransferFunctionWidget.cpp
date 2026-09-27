#include "SimulationTransferFunctionWidget.h"

#include "AnalysisColorRamp.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QLineF>

#include <algorithm>

SimulationTransferFunctionWidget::SimulationTransferFunctionWidget(QWidget* parent)
	: QWidget(parent)
	, _points{ QPointF(0.0, 0.0), QPointF(0.35, 0.02), QPointF(0.7, 0.08), QPointF(1.0, 0.25) }
{
	setMinimumHeight(92);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setToolTip(tr("Opacity from low values (left) to high values (right).\n"
	              "Drag a point; double-click to add; right-click an interior point to remove."));
}

void SimulationTransferFunctionWidget::setPoints(QVector<QPointF> points)
{
	_points = std::move(points);
	normalize();
	update();
}

void SimulationTransferFunctionWidget::setColormap(int colormap)
{
	if (_colormap == colormap)
		return;
	_colormap = colormap;
	update();
}

QRectF SimulationTransferFunctionWidget::plotRect() const { return rect().adjusted(8, 8, -8, -8); }

QPointF SimulationTransferFunctionWidget::toWidget(const QPointF& p) const
{
	const QRectF r = plotRect();
	return QPointF(r.left() + p.x() * r.width(), r.bottom() - p.y() * r.height());
}

QPointF SimulationTransferFunctionWidget::fromWidget(const QPointF& p) const
{
	const QRectF r = plotRect();
	return QPointF(std::clamp((p.x() - r.left()) / r.width(), 0.0, 1.0),
	               std::clamp((r.bottom() - p.y()) / r.height(), 0.0, 1.0));
}

int SimulationTransferFunctionWidget::pointAt(const QPointF& position) const
{
	for (int i = 0; i < _points.size(); ++i)
		if (QLineF(position, toWidget(_points[i])).length() <= 7.0)
			return i;
	return -1;
}

void SimulationTransferFunctionWidget::normalize()
{
	if (_points.size() < 2)
		_points = { QPointF(0.0, 0.0), QPointF(1.0, 0.25) };
	for (QPointF& p : _points)
		p = QPointF(std::clamp(p.x(), 0.0, 1.0), std::clamp(p.y(), 0.0, 1.0));
	std::sort(_points.begin(), _points.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
	_points.front().setX(0.0);
	_points.back().setX(1.0);
}

void SimulationTransferFunctionWidget::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	const QRectF r = plotRect();
	QLinearGradient gradient(r.topLeft(), r.topRight());
	const AnalysisColormap map = static_cast<AnalysisColormap>(_colormap);
	for (int i = 0; i <= 16; ++i)
		gradient.setColorAt(i / 16.0, AnalysisColorRamp::colorForNormalized(i / 16.0f, map));
	painter.fillRect(r, gradient);
	painter.fillRect(r, QColor(0, 0, 0, 85));
	painter.setPen(QPen(palette().color(QPalette::Mid), 1));
	painter.drawRect(r);

	QPainterPath path;
	path.moveTo(toWidget(_points.front()));
	for (int i = 1; i < _points.size(); ++i)
		path.lineTo(toWidget(_points[i]));
	painter.setPen(QPen(Qt::white, 2));
	painter.drawPath(path);
	for (int i = 0; i < _points.size(); ++i)
	{
		painter.setBrush(i == _drag ? QColor(80, 170, 255) : Qt::white);
		painter.setPen(QPen(Qt::black, 1));
		painter.drawEllipse(toWidget(_points[i]), 4.5, 4.5);
	}
}

void SimulationTransferFunctionWidget::mousePressEvent(QMouseEvent* event)
{
	const int hit = pointAt(event->position());
	if (event->button() == Qt::RightButton && hit > 0 && hit + 1 < _points.size())
	{
		_points.removeAt(hit);
		emit pointsChanged();
		update();
		return;
	}
	if (event->button() == Qt::LeftButton)
		_drag = hit;
	update();
}

void SimulationTransferFunctionWidget::mouseMoveEvent(QMouseEvent* event)
{
	if (_drag < 0 || _drag >= _points.size())
		return;
	QPointF point = fromWidget(event->position());
	if (_drag == 0)
		point.setX(0.0);
	else if (_drag + 1 == _points.size())
		point.setX(1.0);
	else
		point.setX(std::clamp(point.x(), _points[_drag - 1].x() + 0.005, _points[_drag + 1].x() - 0.005));
	_points[_drag] = point;
	emit pointsChanged();
	update();
}

void SimulationTransferFunctionWidget::mouseReleaseEvent(QMouseEvent*)
{
	_drag = -1;
	update();
}

void SimulationTransferFunctionWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
	if (!plotRect().contains(event->position()))
		return;
	_points.push_back(fromWidget(event->position()));
	normalize();
	emit pointsChanged();
	update();
}
