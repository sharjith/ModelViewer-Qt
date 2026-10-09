#include "SimulationChartWidget.h"

#include "PathUtils.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QCursor>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStandardPaths>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
	constexpr int kMargin = 44;    // left/bottom room for axis labels and ticks
	constexpr int kTopMargin = 28; // room for the title
	constexpr int kRightMargin = 16;
	constexpr int kSecondaryMargin = 62; // the right-hand axis: tick labels and its rotated name

	// A "nice" tick step (1/2/5 x a power of ten) covering `range` in about `targetTicks` steps.
	double niceStep(double range, int targetTicks)
	{
		if (!(range > 0.0))
			return 1.0;
		const double raw = range / std::max(1, targetTicks);
		const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
		const double normalized = raw / magnitude;
		const double step = normalized < 1.5 ? 1.0 : (normalized < 3.5 ? 2.0 : (normalized < 7.5 ? 5.0 : 10.0));
		return step * magnitude;
	}

	QString formatNumber(double v)
	{
		if (!std::isfinite(v))
			return QStringLiteral("-");
		return QString::number(v, std::fabs(v) >= 1.0e5 || (std::fabs(v) < 1.0e-3 && v != 0.0) ? 'e' : 'g', 4);
	}

	QString withUnit(const QString& label, const QString& unit) { return unit.isEmpty() ? label : label + QStringLiteral(" (%1)").arg(unit); }

	const QColor kMainColour(70, 130, 200);
	const QColor kExtraColours[] = { QColor(230, 140, 40), QColor(60, 160, 90), QColor(150, 90, 180), QColor(200, 60, 60) };

	// The finite span of the y values of a curve widened into [lo, hi]; false when it has none.
	bool widenBy(const ChartSeries& s, double& lo, double& hi)
	{
		bool any = false;
		for (float v : s.y)
			if (std::isfinite(v))
			{
				lo = std::min(lo, static_cast<double>(v));
				hi = std::max(hi, static_cast<double>(v));
				any = true;
			}
		return any;
	}

	// A magnifier cursor with a plus (zooming in) or a minus (out), drawn on the fly: a white outline under a dark stroke reads on any background.
	QCursor zoomCursor(bool zoomIn)
	{
		QPixmap pixmap(32, 32);
		pixmap.fill(Qt::transparent);
		QPainter p(&pixmap);
		p.setRenderHint(QPainter::Antialiasing, true);
		auto stroke = [&](const QColor& colour, double width) {
			p.setPen(QPen(colour, width, Qt::SolidLine, Qt::RoundCap));
			p.setBrush(Qt::NoBrush);
			p.drawEllipse(QPointF(13, 13), 8.0, 8.0);
			p.drawLine(QPointF(19, 19), QPointF(27, 27));
			p.drawLine(QPointF(9, 13), QPointF(17, 13));
			if (zoomIn)
				p.drawLine(QPointF(13, 9), QPointF(13, 17));
		};
		stroke(Qt::white, 4.5);
		stroke(QColor(30, 30, 30), 2.0);
		p.end();
		return QCursor(pixmap, 13, 13);
	}

	// A degenerate range gets some width, then a little headroom so the extremes are not drawn exactly on the frame.
	void padRange(double& lo, double& hi)
	{
		if (!(hi > lo))
		{
			lo -= 0.5;
			hi += 0.5;
		}
		const double pad = (hi - lo) * 0.08;
		lo -= pad;
		hi += pad;
	}
}

SimulationChartWidget::SimulationChartWidget(QWidget* parent) : QWidget(parent, Qt::Window)
{
	// Qt::Window (not the default embedded-child behaviour a QWidget gets from a plain parent) makes this a genuine
	// top-level window, but one OWNED by `parent` - clicking the main window (or another document) no longer buries
	// it, the same way the app's QDialog-based floating tools (RtRenderDialog, ShrinkWrapDialog, ...) already behave
	// by being parented to their document. Pass the document (ModelViewer) as parent, not nullptr.
	setMouseTracking(true);
	setAttribute(Qt::WA_DeleteOnClose);
	setMinimumSize(420, 300);
	resize(560, 380);
	_zoomCursorTimer.setSingleShot(true);
	_zoomCursorTimer.setInterval(700);
	connect(&_zoomCursorTimer, &QTimer::timeout, this, [this]() {
		if (!_panning)
			unsetCursor();
	});
}

void SimulationChartWidget::setSeries(const ChartSeries& series)
{
	_series = series;
	_histogram = false;
	_hoverIndex = -1;
	_zoomed = false;
	setWindowTitle(series.title.isEmpty() ? tr("Chart") : series.title);
	update();
}

void SimulationChartWidget::setHistogram(const QString& title, const QString& xLabel, const QString& xUnit,
                                         const std::vector<float>& edges, const std::vector<std::size_t>& counts)
{
	_histogram = true;
	_histTitle = title;
	_histXLabel = xLabel;
	_histXUnit = xUnit;
	_histEdges = edges;
	_histCounts = counts;
	_hoverIndex = -1;
	_zoomed = false;
	setWindowTitle(title.isEmpty() ? tr("Distribution") : title);
	update();
}

bool SimulationChartWidget::hasSecondaryAxis() const
{
	if (_histogram)
		return false;
	return std::any_of(_extraCurves.begin(), _extraCurves.end(), [this](const ChartSeries& c) { return chartNeedsSecondaryAxis(_series, c); });
}

QRectF SimulationChartWidget::plotRect() const
{
	const int right = hasSecondaryAxis() ? kSecondaryMargin : kRightMargin;
	return QRectF(kMargin, kTopMargin, std::max(1, width() - kMargin - right), std::max(1, height() - kTopMargin - kMargin));
}

SimulationChartWidget::Axes SimulationChartWidget::computeAxes() const
{
	if (_zoomed)
	{
		Axes zoomed = _view;
		zoomed.valid = true;
		zoomed.hasY2 = hasSecondaryAxis();
		return zoomed;
	}
	Axes a;
	if (_histogram)
	{
		if (_histCounts.empty())
			return a;
		a.xMin = _histEdges.empty() ? 0.0 : _histEdges.front();
		a.xMax = _histEdges.empty() ? 1.0 : _histEdges.back();
		a.yMin = 0.0;
		a.yMax = static_cast<double>(*std::max_element(_histCounts.begin(), _histCounts.end()));
		if (!(a.xMax > a.xMin))
			a.xMax = a.xMin + 1.0;
		padRange(a.yMin, a.yMax);
		a.valid = true;
		return a;
	}
	if (_series.empty())
		return a;
	a.xMin = _series.x.front();
	a.xMax = _series.x.back();
	double lo = std::numeric_limits<double>::max(), hi = std::numeric_limits<double>::lowest();
	double lo2 = lo, hi2 = hi;
	bool any = widenBy(_series, lo, hi), any2 = false;
	for (const ChartSeries& extra : _extraCurves) // the axes span every curve
	{
		a.xMin = std::min(a.xMin, extra.x.front());
		a.xMax = std::max(a.xMax, extra.x.back());
		if (chartNeedsSecondaryAxis(_series, extra))
			any2 = widenBy(extra, lo2, hi2) || any2;
		else
			any = widenBy(extra, lo, hi) || any;
	}
	if (!any && !any2) // every sample is NaN (the line never touched the mesh)
		return a;
	if (!any)
	{
		lo = 0.0;
		hi = 1.0;
	}
	padRange(lo, hi);
	a.yMin = lo;
	a.yMax = hi;
	if (any2)
	{
		padRange(lo2, hi2);
		a.y2Min = lo2;
		a.y2Max = hi2;
	}
	a.hasY2 = hasSecondaryAxis();
	if (!(a.xMax > a.xMin))
		a.xMax = a.xMin + 1.0;
	a.valid = true;
	return a;
}

void SimulationChartWidget::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.fillRect(rect(), palette().window());

	const QRectF plot = plotRect();
	const QColor axisColor = palette().windowText().color();
	const QColor gridColor = QColor(axisColor.red(), axisColor.green(), axisColor.blue(), 40);
	painter.setPen(axisColor);
	painter.drawText(QRectF(0, 4, width(), kTopMargin - 4), Qt::AlignHCenter, _histogram ? _histTitle : _series.title);

	if (_histogram ? _histCounts.empty() : _series.empty())
	{
		painter.drawText(plot, Qt::AlignCenter, tr("No data"));
		return;
	}
	const Axes axes = computeAxes();
	if (!axes.valid)
	{
		painter.drawText(plot, Qt::AlignCenter, tr("The sampled line/point had no data"));
		return;
	}
	const double xMin = axes.xMin, xMax = axes.xMax, yMin = axes.yMin, yMax = axes.yMax;

	auto toX = [&](double x) { return plot.left() + (x - xMin) / (xMax - xMin) * plot.width(); };
	auto toY = [&](double y) { return plot.bottom() - (y - yMin) / (yMax - yMin) * plot.height(); };
	auto toY2 = [&](double y) { return plot.bottom() - (y - axes.y2Min) / (axes.y2Max - axes.y2Min) * plot.height(); };

	// Gridlines and tick labels.
	painter.setPen(gridColor);
	const double yStep = niceStep(yMax - yMin, 6);
	for (double y = std::ceil(yMin / yStep) * yStep; y <= yMax; y += yStep)
	{
		const double py = toY(y);
		painter.drawLine(QPointF(plot.left(), py), QPointF(plot.right(), py));
	}
	const double xStep = niceStep(xMax - xMin, 6);
	for (double x = std::ceil(xMin / xStep) * xStep; x <= xMax; x += xStep)
	{
		const double px = toX(x);
		painter.drawLine(QPointF(px, plot.top()), QPointF(px, plot.bottom()));
	}
	painter.setPen(axisColor);
	for (double y = std::ceil(yMin / yStep) * yStep; y <= yMax; y += yStep)
		painter.drawText(QRectF(0, toY(y) - 8, kMargin - 6, 16), Qt::AlignRight | Qt::AlignVCenter, formatNumber(y));
	for (double x = std::ceil(xMin / xStep) * xStep; x <= xMax; x += xStep)
		painter.drawText(QRectF(toX(x) - 30, plot.bottom() + 2, 60, 16), Qt::AlignHCenter, formatNumber(x));
	painter.drawRect(plot);

	const QString xAxisLabel = _histogram ? withUnit(_histXLabel, _histXUnit) : withUnit(_series.xLabel, _series.xUnit);
	const QString yAxisLabel = _histogram ? tr("Count") : withUnit(_series.yLabel, _series.yUnit);
	painter.drawText(QRectF(0, height() - 18, width(), 16), Qt::AlignHCenter, xAxisLabel);
	painter.save();
	painter.translate(12, height() / 2.0);
	painter.rotate(-90);
	painter.drawText(QRectF(-height() / 2.0, -14, static_cast<double>(height()), 16), Qt::AlignHCenter, yAxisLabel);
	painter.restore();

	// The second y axis, on the right, in the colour of the first curve that uses it.
	if (axes.hasY2)
	{
		std::size_t first = 0;
		for (std::size_t c = 0; c < _extraCurves.size(); ++c)
			if (chartNeedsSecondaryAxis(_series, _extraCurves[c]))
			{
				first = c;
				break;
			}
		const QColor colour = kExtraColours[first % 4];
		const double step2 = niceStep(axes.y2Max - axes.y2Min, 6);
		painter.setPen(colour);
		for (double y = std::ceil(axes.y2Min / step2) * step2; y <= axes.y2Max; y += step2)
			painter.drawText(QRectF(plot.right() + 4, toY2(y) - 8, 46, 16), Qt::AlignLeft | Qt::AlignVCenter, formatNumber(y));
		painter.save();
		painter.translate(width() - 6, height() / 2.0);
		painter.rotate(90);
		painter.drawText(QRectF(-height() / 2.0, -14, static_cast<double>(height()), 16), Qt::AlignHCenter,
			withUnit(_extraCurves[first].yLabel.isEmpty() ? _extraCurves[first].title : _extraCurves[first].yLabel, _extraCurves[first].yUnit));
		painter.restore();
	}

	if (_histogram)
	{
		painter.setBrush(QColor(70, 130, 200, 200));
		painter.setPen(QColor(40, 90, 150));
		for (std::size_t i = 0; i < _histCounts.size() && i + 1 < _histEdges.size(); ++i)
		{
			const QRectF bar(toX(_histEdges[i]), toY(static_cast<double>(_histCounts[i])), toX(_histEdges[i + 1]) - toX(_histEdges[i]),
			                 toY(0.0) - toY(static_cast<double>(_histCounts[i])));
			painter.drawRect(bar.normalized());
		}
	}
	else
	{
		// The polyline, broken at a NaN (the sample left the mesh) instead of joining across the gap; a sample with no finite neighbour has no line to
		// show it (a single-step result gives one sample): a dot instead. Clipped to the plot, since a zoomed curve runs past it.
		auto drawCurve = [&](const ChartSeries& s, const QColor& colour, const auto& yOf) {
			painter.save();
			painter.setClipRect(plot.adjusted(-1, -1, 1, 1));
			painter.setPen(QPen(colour, 2));
			painter.setBrush(Qt::NoBrush);
			QPainterPath path;
			bool open = false;
			for (std::size_t i = 0; i < s.x.size() && i < s.y.size(); ++i)
			{
				if (!std::isfinite(s.y[i]))
				{
					open = false;
					continue;
				}
				const QPointF p(toX(s.x[i]), yOf(static_cast<double>(s.y[i])));
				if (!open)
				{
					path.moveTo(p);
					open = true;
				}
				else
					path.lineTo(p);
			}
			painter.drawPath(path);
			painter.setPen(Qt::NoPen);
			painter.setBrush(colour);
			for (std::size_t i = 0; i < s.x.size() && i < s.y.size(); ++i)
			{
				const bool before = i > 0 && std::isfinite(s.y[i - 1]), after = i + 1 < s.y.size() && std::isfinite(s.y[i + 1]);
				if (std::isfinite(s.y[i]) && !before && !after)
					painter.drawEllipse(QPointF(toX(s.x[i]), yOf(static_cast<double>(s.y[i]))), 4.0, 4.0);
			}
			painter.restore();
		};
		drawCurve(_series, kMainColour, toY);
		for (std::size_t c = 0; c < _extraCurves.size(); ++c)
		{
			if (chartNeedsSecondaryAxis(_series, _extraCurves[c]))
				drawCurve(_extraCurves[c], kExtraColours[c % 4], toY2);
			else
				drawCurve(_extraCurves[c], kExtraColours[c % 4], toY);
		}
		if (!_extraCurves.empty())
		{
			double legendY = plot.top() + 6.0;
			auto legendEntry = [&](const QColor& colour, const QString& title) {
				painter.setPen(QPen(colour, 2));
				painter.drawLine(QPointF(plot.right() - 150, legendY + 6), QPointF(plot.right() - 130, legendY + 6));
				painter.setPen(axisColor);
				painter.drawText(QRectF(plot.right() - 126, legendY - 2, 124, 16), Qt::AlignLeft | Qt::AlignVCenter,
					painter.fontMetrics().elidedText(title, Qt::ElideRight, 120));
				legendY += 16.0;
			};
			legendEntry(kMainColour, _series.title);
			for (std::size_t c = 0; c < _extraCurves.size(); ++c)
				legendEntry(kExtraColours[c % 4], chartNeedsSecondaryAxis(_series, _extraCurves[c]) ? tr("%1 (right axis)").arg(_extraCurves[c].title) : _extraCurves[c].title);
		}
	}

	// The cursor: where the result being shown is on this axis (the current time step), drawn over the curves.
	if (!_histogram && _hasCursor && _cursorX >= xMin && _cursorX <= xMax)
	{
		const double cx = toX(_cursorX);
		painter.setPen(QPen(QColor(230, 120, 20), 2));
		painter.drawLine(QPointF(cx, plot.top()), QPointF(cx, plot.bottom()));
		painter.setBrush(QColor(230, 120, 20));
		const QPointF top(cx, plot.top());
		const QPointF triangle[3] = { top, QPointF(cx - 5, plot.top() - 8), QPointF(cx + 5, plot.top() - 8) };
		painter.drawPolygon(triangle, 3);
	}

	// Hover crosshair + readout.
	if (_hoverIndex >= 0)
	{
		if (_histogram && static_cast<std::size_t>(_hoverIndex) < _histCounts.size())
		{
			const double lo = _histEdges[static_cast<std::size_t>(_hoverIndex)], hi = _histEdges[static_cast<std::size_t>(_hoverIndex) + 1];
			const QString text = QStringLiteral("[%1, %2): %3").arg(formatNumber(lo), formatNumber(hi)).arg(_histCounts[static_cast<std::size_t>(_hoverIndex)]);
			painter.setPen(axisColor);
			painter.drawText(QRectF(plot.left(), plot.top() - 2, plot.width(), 16), Qt::AlignLeft, text);
		}
		else if (!_histogram && static_cast<std::size_t>(_hoverIndex) < _series.x.size())
		{
			const double x = _series.x[static_cast<std::size_t>(_hoverIndex)];
			const float y = _series.y[static_cast<std::size_t>(_hoverIndex)];
			painter.setPen(QPen(gridColor, 1, Qt::DashLine));
			painter.drawLine(QPointF(toX(x), plot.top()), QPointF(toX(x), plot.bottom()));
			if (std::isfinite(y))
			{
				painter.setPen(QColor(200, 60, 60));
				painter.setBrush(QColor(200, 60, 60));
				painter.drawEllipse(QPointF(toX(x), toY(static_cast<double>(y))), 3.5, 3.5);
			}
			painter.setPen(axisColor);
			const QString text = std::isfinite(y) ? QStringLiteral("%1: %2   %3: %4").arg(_series.xLabel, formatNumber(x), _series.yLabel, formatNumber(y))
			                                      : QStringLiteral("%1: %2   (no data)").arg(_series.xLabel, formatNumber(x));
			painter.drawText(QRectF(plot.left(), plot.top() - 2, plot.width(), 16), Qt::AlignLeft, text);
		}
	}
}

void SimulationChartWidget::mouseMoveEvent(QMouseEvent* event)
{
	const QRectF plot = plotRect();
	const QPointF pos = event->position();
	if (_panning)
	{
		// Middle-button drag: the data follows the pointer.
		Axes axes = computeAxes();
		if (axes.valid)
		{
			const QPointF delta = pos - _panLast;
			const double dx = -delta.x() / std::max(1.0, plot.width()), dy = delta.y() / std::max(1.0, plot.height());
			_view = axes;
			_view.xMin += dx * (axes.xMax - axes.xMin);
			_view.xMax += dx * (axes.xMax - axes.xMin);
			_view.yMin += dy * (axes.yMax - axes.yMin);
			_view.yMax += dy * (axes.yMax - axes.yMin);
			_view.y2Min += dy * (axes.y2Max - axes.y2Min);
			_view.y2Max += dy * (axes.y2Max - axes.y2Min);
			_zoomed = true;
			_panLast = pos;
			update();
		}
		return;
	}
	if ((event->buttons() & Qt::LeftButton) && plot.contains(pos))
		seekTo(pos); // dragging scrubs the cursor
	if (!plot.contains(pos))
	{
		if (_hoverIndex != -1)
		{
			_hoverIndex = -1;
			update();
		}
		return;
	}
	const Axes axes = computeAxes();
	const std::size_t count = _histogram ? _histCounts.size() : _series.x.size();
	if (count == 0 || !axes.valid)
		return;
	// The sample (or bin) under the pointer, by its x value, so a zoomed chart reads the right one.
	const double x = axes.xMin + std::clamp((pos.x() - plot.left()) / std::max(1.0, plot.width()), 0.0, 1.0) * (axes.xMax - axes.xMin);
	int index = 0;
	if (_histogram)
	{
		const auto upper = std::upper_bound(_histEdges.begin(), _histEdges.end(), static_cast<float>(x));
		index = std::clamp(static_cast<int>(upper - _histEdges.begin()) - 1, 0, static_cast<int>(count) - 1);
	}
	else
	{
		const auto upper = std::lower_bound(_series.x.begin(), _series.x.end(), x);
		index = static_cast<int>(upper - _series.x.begin());
		if (index >= static_cast<int>(count))
			index = static_cast<int>(count) - 1;
		else if (index > 0 && std::fabs(_series.x[static_cast<std::size_t>(index) - 1] - x) <= std::fabs(_series.x[static_cast<std::size_t>(index)] - x))
			--index;
	}
	if (index != _hoverIndex)
	{
		_hoverIndex = index;
		update();
	}
}

void SimulationChartWidget::addCurve(const ChartSeries& series)
{
	if (series.empty())
		return;
	_extraCurves.push_back(series);
	update();
}

void SimulationChartWidget::clearCurves()
{
	_extraCurves.clear();
	update();
}

void SimulationChartWidget::setCursorX(double x)
{
	if (_hasCursor && _cursorX == x)
		return;
	_hasCursor = true;
	_cursorX = x;
	update();
}

void SimulationChartWidget::clearCursor()
{
	_hasCursor = false;
	update();
}

void SimulationChartWidget::resetZoom()
{
	if (!_zoomed)
		return;
	_zoomed = false;
	update();
}

// The x value under a point of the plot area, for seeking (the same mapping paintEvent() draws with: the visible span).
void SimulationChartWidget::seekTo(const QPointF& pos)
{
	if (!_seekable || _histogram || _series.empty())
		return;
	const QRectF plot = plotRect();
	const Axes axes = computeAxes();
	if (!axes.valid || !(axes.xMax > axes.xMin))
		return;
	const double fraction = std::clamp((pos.x() - plot.left()) / std::max(1.0, plot.width()), 0.0, 1.0);
	emit seekRequested(axes.xMin + fraction * (axes.xMax - axes.xMin));
}

void SimulationChartWidget::zoomAt(const QPointF& pos, double factor, bool zoomX, bool zoomY)
{
	const QRectF plot = plotRect();
	Axes axes = computeAxes();
	if (!axes.valid)
		return;
	const double ax = std::clamp((pos.x() - plot.left()) / std::max(1.0, plot.width()), 0.0, 1.0);
	const double ay = std::clamp((plot.bottom() - pos.y()) / std::max(1.0, plot.height()), 0.0, 1.0);
	_view = axes;
	if (zoomX)
		chartZoomRange(axes.xMin, axes.xMax, ax, factor, _view.xMin, _view.xMax);
	if (zoomY)
	{
		chartZoomRange(axes.yMin, axes.yMax, ay, factor, _view.yMin, _view.yMax);
		chartZoomRange(axes.y2Min, axes.y2Max, ay, factor, _view.y2Min, _view.y2Max);
	}
	_zoomed = true;
	update();
}

void SimulationChartWidget::wheelEvent(QWheelEvent* event)
{
	if (_histogram || _series.empty() || !plotRect().contains(event->position()))
	{
		event->ignore();
		return;
	}
	const double steps = event->angleDelta().y() / 120.0;
	if (steps == 0.0)
		return;
	const bool shift = event->modifiers() & Qt::ShiftModifier, ctrl = event->modifiers() & Qt::ControlModifier;
	zoomAt(event->position(), std::pow(0.85, steps), !ctrl || shift, !shift || ctrl);
	if (!_panning)
	{
		setCursor(zoomCursor(steps > 0));
		_zoomCursorTimer.start();
	}
	event->accept();
}

void SimulationChartWidget::mousePressEvent(QMouseEvent* event)
{
	if (event->button() == Qt::MiddleButton && plotRect().contains(event->position()) && !_histogram)
	{
		_panning = true;
		_panLast = event->position();
		setCursor(Qt::ClosedHandCursor);
		return;
	}
	if (event->button() == Qt::LeftButton && plotRect().contains(event->position()))
		seekTo(event->position());
}

void SimulationChartWidget::mouseReleaseEvent(QMouseEvent* event)
{
	if (event->button() == Qt::MiddleButton && _panning)
	{
		_panning = false;
		unsetCursor();
	}
}

void SimulationChartWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton && plotRect().contains(event->position()))
		resetZoom();
}

void SimulationChartWidget::saveImage()
{
	const QString path = QFileDialog::getSaveFileName(this, tr("Save Chart Image"),
		QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + QStringLiteral("/chart.png"), tr("PNG image (*.png)"));
	if (path.isEmpty())
		return;
	const int hover = _hoverIndex;
	_hoverIndex = -1; // no hover readout in the picture
	const QPixmap picture = grab();
	_hoverIndex = hover;
	if (!picture.save(path, "PNG"))
		QMessageBox::warning(this, tr("Save Chart Image"), tr("The image could not be saved."));
}

void SimulationChartWidget::exportCsv()
{
	const QString path = QFileDialog::getSaveFileName(this, tr("Export Chart Data"),
		QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/chart.csv"), tr("CSV files (*.csv)"));
	if (path.isEmpty())
		return;
	QString text;
	if (_histogram)
	{
		text = QStringLiteral("bin_from,bin_to,count\n");
		for (std::size_t i = 0; i < _histCounts.size() && i + 1 < _histEdges.size(); ++i)
			text += QStringLiteral("%1,%2,%3\n").arg(static_cast<double>(_histEdges[i]), 0, 'g', 9).arg(static_cast<double>(_histEdges[i + 1]), 0, 'g', 9).arg(_histCounts[i]);
	}
	else
		text = chartToCsv(_series, _extraCurves);
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || file.write(text.toUtf8()) < 0)
		QMessageBox::warning(this, tr("Export Chart Data"), tr("The file could not be written."));
}

void SimulationChartWidget::contextMenuEvent(QContextMenuEvent* event)
{
	QMenu menu(this);
	menu.setToolTipsVisible(true);
	QAction* addPoint = nullptr;
	QAction* add = nullptr;
	QAction* clear = nullptr;
	QAction* reset = nullptr;
	if (!_histogram)
	{
		if (_seekable) // a point-history chart: more points of the same result can be added to compare them
			addPoint = menu.addAction(tr("Add a point from the model"));
		add = menu.addAction(tr("Add curve from CSV..."));
		clear = menu.addAction(tr("Remove added curves"));
		clear->setEnabled(!_extraCurves.empty());
		menu.addSeparator();
		reset = menu.addAction(tr("Reset zoom"));
		reset->setToolTip(tr("Mouse wheel zooms (Shift: x only, Ctrl: y only), middle-button drag pans, double-click resets."));
		reset->setEnabled(_zoomed);
		menu.addSeparator();
	}
	QAction* image = menu.addAction(tr("Save image..."));
	QAction* data = menu.addAction(tr("Export data (CSV)..."));
	QAction* chosen = menu.exec(event->globalPos());
	if (!chosen)
		return;
	if (chosen == addPoint)
		emit addPointRequested();
	else if (chosen == clear)
		clearCurves();
	else if (chosen == reset)
		resetZoom();
	else if (chosen == image)
		saveImage();
	else if (chosen == data)
		exportCsv();
	else if (chosen == add)
	{
		const QString path = QFileDialog::getOpenFileName(this, tr("Add Curve"), PathUtils::getDataDirectory() + QStringLiteral("/sample-models/Simulation"),
			tr("CSV files (*.csv *.txt);;All files (*)"));
		if (path.isEmpty())
			return;
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		{
			QMessageBox::warning(this, tr("Add Curve"), tr("The file could not be opened."));
			return;
		}
		ChartSeries curve;
		QString error;
		if (!parseChartCurveCsv(QString::fromUtf8(file.readAll()), QFileInfo(path).completeBaseName(), curve, &error))
		{
			QMessageBox::warning(this, tr("Add Curve"), error);
			return;
		}
		addCurve(curve);
	}
}

void SimulationChartWidget::leaveEvent(QEvent*)
{
	if (_hoverIndex != -1)
	{
		_hoverIndex = -1;
		update();
	}
}
