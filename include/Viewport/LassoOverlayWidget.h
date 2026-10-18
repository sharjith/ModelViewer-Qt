#pragma once

#include <QWidget>
#include <QPolygon>

// Simple transparent child widget drawing the live Lasso-select drag path as a
// polyline/polygon outline - same "plain QWidget floating on top of the GL
// surface" mechanism QRubberBand already uses for the existing rectangle
// multi-select (ViewportWidget's _rubberBand member), just with an arbitrary
// polygon instead of QRubberBand's rectangle-only shape (which can't draw one).
class LassoOverlayWidget : public QWidget
{
	Q_OBJECT
public:
	explicit LassoOverlayWidget(QWidget* parent);

	// Replaces the drag path and repaints. An empty/short polygon draws nothing.
	void setPoints(const QPolygon& points);

	// Live preview of which combine mode releasing now would apply - checked
	// continuously during the drag using the same current-modifier and
	// press-time-latch rules as mouseReleaseEvent(). Red for Subtract, orange
	// otherwise (Replace/Add look identical here - orange already meant
	// "select" for both before this feature existed).
	void setSubtractMode(bool subtract);

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	QPolygon _points;
	bool _subtractMode = false;
};
