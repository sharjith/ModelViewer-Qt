#pragma once

#include "SimulationCharts.h"

#include <QWidget>

#include <vector>

// A standalone (non-modal, floating) window showing one XY chart: "plot over line" or "plot over time" from
// SimulationCharts.h, or a field-distribution histogram (setHistogram()). Hand-drawn with QPainter (axes,
// gridlines, the polyline or bars, a hover crosshair with a readout), matching the house style of
// SimulationLegendWidget/SimulationTimelineWidget rather than a charting library - see docs/source_reorg_plan.md's
// module notes and the review that chose this for the simulation panel's other small widgets.
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

signals:
	void seekRequested(double x);

protected:
	void paintEvent(QPaintEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void leaveEvent(QEvent* event) override;
	void contextMenuEvent(QContextMenuEvent* event) override;

private:
	// The plot area inside the widget, after room for axis labels/ticks.
	QRectF plotRect() const;

	ChartSeries _series;
	std::vector<ChartSeries> _extraCurves; // drawn after the main one, each in its own colour
	bool _hasCursor = false;
	double _cursorX = 0.0;
	bool _seekable = false;
	void seekTo(const QPointF& pos);
	bool _histogram = false;
	std::vector<float> _histEdges;
	std::vector<std::size_t> _histCounts;
	QString _histTitle, _histXLabel, _histXUnit;

	int _hoverIndex = -1; // index into _series.x/.y (line mode) or into _histCounts (histogram mode), -1 = no hover
};
