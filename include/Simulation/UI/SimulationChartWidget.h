#pragma once

#include "SimulationCharts.h"

#include <QWidget>

// A standalone (non-modal, floating) window showing one XY chart: "plot over line" or "plot over time" from
// SimulationCharts.h, or a field-distribution histogram (setHistogram()). Hand-drawn with QPainter (axes,
// gridlines, the polyline or bars, a hover crosshair with a readout), matching the house style of
// SimulationLegendWidget/SimulationTimelineWidget rather than a charting library - see docs/source_reorg_plan.md's
// module notes and the review that chose this for the simulation panel's other small widgets.
class SimulationChartWidget : public QWidget
{
public:
	explicit SimulationChartWidget(QWidget* parent = nullptr);

	// Shows a line chart (a "plot over line"/"plot over time" result). Replaces any histogram shown before.
	void setSeries(const ChartSeries& series);

	// Shows a histogram: `edges` has binCount+1 entries (edges[i]..edges[i+1] is bin i's range), `counts` has
	// binCount entries. Replaces any line series shown before.
	void setHistogram(const QString& title, const QString& xLabel, const QString& xUnit, const std::vector<float>& edges,
	                  const std::vector<std::size_t>& counts);

protected:
	void paintEvent(QPaintEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	// The plot area inside the widget, after room for axis labels/ticks.
	QRectF plotRect() const;

	ChartSeries _series;
	bool _histogram = false;
	std::vector<float> _histEdges;
	std::vector<std::size_t> _histCounts;
	QString _histTitle, _histXLabel, _histXUnit;

	int _hoverIndex = -1; // index into _series.x/.y (line mode) or into _histCounts (histogram mode), -1 = no hover
};
