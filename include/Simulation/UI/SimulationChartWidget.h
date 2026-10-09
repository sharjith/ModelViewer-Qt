#pragma once

#include "SimulationCharts.h"

#include <QPointF>
#include <QTimer>
#include <QWidget>

#include <vector>

// A standalone (non-modal, floating) window showing one XY chart: "plot over line" or "plot over time" from
// SimulationCharts.h, or a field-distribution histogram (setHistogram()). Hand-drawn with QPainter (axes,
// gridlines, the polyline or bars, a hover crosshair with a readout), matching the house style of
// SimulationLegendWidget/SimulationTimelineWidget rather than a charting library - see docs/source_reorg_plan.md's
// module notes and the review that chose this for the simulation panel's other small widgets.
//
// A line chart can be zoomed (mouse wheel around the pointer; Shift = x only, Ctrl = y only), panned (middle-button drag) and reset (double-click, or
// the right-click menu), exported (PNG image, CSV data), and an added curve whose unit differs from the main curve's is drawn against a second y axis on
// the right.
class SimulationChartWidget : public QWidget
{
	Q_OBJECT
public:
	explicit SimulationChartWidget(QWidget* parent = nullptr);

	// Shows a line chart (a "plot over line"/"plot over time" result). Replaces any histogram shown before.
	void setSeries(const ChartSeries& series);

	// Shows a histogram: `edges` has binCount+1 entries (edges[i]..edges[i+1] is bin i's range), `counts` has
	// binCount entries. Replaces any line series shown before.
	// A second (third, ...) curve drawn on the same axes in its own colour with a legend - test data, a frequency-response curve. The axes
	// span every curve. The right-click menu adds one from a CSV file and clears them.
	void addCurve(const ChartSeries& series);
	void clearCurves();

	// A vertical cursor at an x value (e.g. the current time step of the result the chart belongs to). Shown only after setCursorX().
	void setCursorX(double x);
	void clearCursor();
	// When seekable, pressing or dragging inside the plot area asks for the x value under the pointer (seekRequested), so the chart can drive
	// the result's time step.
	void setSeekable(bool seekable) { _seekable = seekable; }

	void setHistogram(const QString& title, const QString& xLabel, const QString& xUnit, const std::vector<float>& edges,
	                  const std::vector<std::size_t>& counts);

	// Back to the automatic axes (what the whole data spans).
	void resetZoom();
	bool isZoomed() const { return _zoomed; }

signals:
	void seekRequested(double x);
	void addPointRequested(); // right-click > "Add a point from the model": the owner arms a pick and adds that point's history here

protected:
	void paintEvent(QPaintEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void leaveEvent(QEvent* event) override;
	void contextMenuEvent(QContextMenuEvent* event) override;

private:
	// What the axes show: the automatic span of the data (padded), or the zoomed view. y2 is the right-hand axis, present when a curve needs it.
	struct Axes
	{
		bool valid = false; // false: nothing finite to show
		double xMin = 0, xMax = 1, yMin = 0, yMax = 1, y2Min = 0, y2Max = 1;
		bool hasY2 = false;
	};
	Axes computeAxes() const;
	bool hasSecondaryAxis() const;
	// The plot area inside the widget, after room for axis labels/ticks.
	QRectF plotRect() const;
	void seekTo(const QPointF& pos);
	void zoomAt(const QPointF& pos, double factor, bool zoomX, bool zoomY);
	void saveImage();
	void exportCsv();

	ChartSeries _series;
	std::vector<ChartSeries> _extraCurves; // drawn after the main one, each in its own colour
	bool _hasCursor = false;
	double _cursorX = 0.0;
	bool _seekable = false;
	bool _histogram = false;
	std::vector<float> _histEdges;
	std::vector<std::size_t> _histCounts;
	QString _histTitle, _histXLabel, _histXUnit;

	int _hoverIndex = -1; // index into _series.x/.y (line mode) or into _histCounts (histogram mode), -1 = no hover

	bool _zoomed = false;
	Axes _view;           // the zoomed axes, valid while _zoomed
	bool _panning = false;
	QTimer _zoomCursorTimer; // the magnifier cursor of a wheel zoom goes back to the normal one a moment after the last notch
	QPointF _panLast;
};
