#pragma once

#include <QPointF>
#include <QVector>
#include <QWidget>

// Compact opacity-curve editor for volume rendering. X is normalized field value and Y is opacity. Double-click
// adds a point, right-click removes an interior point, and dragging edits a point (endpoints stay at X=0/1).
class SimulationTransferFunctionWidget : public QWidget
{
	Q_OBJECT
public:
	explicit SimulationTransferFunctionWidget(QWidget* parent = nullptr);
	QVector<QPointF> points() const { return _points; }
	void setPoints(QVector<QPointF> points);
	void setColormap(int colormap);

signals:
	void pointsChanged();

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent*) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
	QRectF plotRect() const;
	QPointF toWidget(const QPointF& point) const;
	QPointF fromWidget(const QPointF& point) const;
	int pointAt(const QPointF& position) const;
	void normalize();

	QVector<QPointF> _points;
	int _colormap = 0;
	int _drag = -1;
};
