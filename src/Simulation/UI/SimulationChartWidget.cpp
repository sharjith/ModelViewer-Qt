#include "SimulationChartWidget.h"

#include "PathUtils.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
	constexpr int kMargin = 44;    // left/bottom room for axis labels and ticks
	constexpr int kTopMargin = 28; // room for the title
	constexpr int kRightMargin = 16;

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
}

void SimulationChartWidget::setSeries(const ChartSeries& series)
{
	_series = series;
	_histogram = false;
	_hoverIndex = -1;
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
	setWindowTitle(title.isEmpty() ? tr("Distribution") : title);
	update();
}

QRectF SimulationChartWidget::plotRect() const
{
	return QRectF(kMargin, kTopMargin, std::max(1, width() - kMargin - kRightMargin), std::max(1, height() - kTopMargin - kMargin));
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

	double xMin, xMax, yMin, yMax;
	if (_histogram)
	{
		xMin = _histEdges.empty() ? 0.0 : _histEdges.front();
		xMax = _histEdges.empty() ? 1.0 : _histEdges.back();
		yMin = 0.0;
		yMax = static_cast<double>(*std::max_element(_histCounts.begin(), _histCounts.end()));
	}
	else
	{
		xMin = _series.x.front();
		xMax = _series.x.back();
		yMin = std::numeric_limits<double>::max();
		yMax = std::numeric_limits<double>::lowest();
		for (float v : _series.y)
			if (std::isfinite(v))
			{
				yMin = std::min(yMin, static_cast<double>(v));
				yMax = std::max(yMax, static_cast<double>(v));
			}
		for (const ChartSeries& extra : _extraCurves) // the axes span every curve
		{
			xMin = std::min(xMin, extra.x.front());
			xMax = std::max(xMax, extra.x.back());
			for (float v : extra.y)
				if (std::isfinite(v))
				{
					yMin = std::min(yMin, static_cast<double>(v));
					yMax = std::max(yMax, static_cast<double>(v));
				}
		}
		if (yMin > yMax) // every sample is NaN (the line never touched the mesh)
		{
			painter.drawText(plot, Qt::AlignCenter, tr("The sampled line/point had no data"));
			return;
		}
	}
	if (!(yMax > yMin))
	{
		yMin -= 0.5;
		yMax += 0.5;
	}
	if (!(xMax > xMin))
		xMax = xMin + 1.0;
	// A little headroom so the extremes are not drawn exactly on the frame.
	const double yPad = (yMax - yMin) * 0.08;
	yMin -= yPad;
	yMax += yPad;

	auto toX = [&](double x) { return plot.left() + (x - xMin) / (xMax - xMin) * plot.width(); };
	auto toY = [&](double y) { return plot.bottom() - (y - yMin) / (yMax - yMin) * plot.height(); };

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

	const QString xAxisLabel = _histogram ? (_histXUnit.isEmpty() ? _histXLabel : _histXLabel + QStringLiteral(" (%1)").arg(_histXUnit))
	                                      : (_series.xUnit.isEmpty() ? _series.xLabel : _series.xLabel + QStringLiteral(" (%1)").arg(_series.xUnit));
	const QString yAxisLabel = _histogram ? tr("Count")
	                                      : (_series.yUnit.isEmpty() ? _series.yLabel : _series.yLabel + QStringLiteral(" (%1)").arg(_series.yUnit));
	painter.drawText(QRectF(0, height() - 18, width(), 16), Qt::AlignHCenter, xAxisLabel);
	painter.save();
	painter.translate(12, height() / 2.0);
	painter.rotate(-90);
	painter.drawText(QRectF(-height() / 2.0, -14, static_cast<double>(height()), 16), Qt::AlignHCenter, yAxisLabel);
	painter.restore();

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
		// The polyline, broken at a NaN (the sample left the mesh) instead of joining across the gap.
		painter.setPen(QPen(QColor(70, 130, 200), 2));
		QPainterPath path;
		bool open = false;
		for (std::size_t i = 0; i < _series.x.size(); ++i)
		{
			if (!std::isfinite(_series.y[i]))
			{
				open = false;
				continue;
			}
			const QPointF p(toX(_series.x[i]), toY(static_cast<double>(_series.y[i])));
			if (!open)
			{
				path.moveTo(p);
				open = true;
			}
			else
				path.lineTo(p);
		}
		painter.drawPath(path);

		// The added curves, each in its own colour, and a legend once there is more than one curve.
		static const QColor kExtraColours[] = { QColor(230, 140, 40), QColor(60, 160, 90), QColor(150, 90, 180), QColor(200, 60, 60) };
		for (std::size_t c = 0; c < _extraCurves.size(); ++c)
		{
			const ChartSeries& extra = _extraCurves[c];
			painter.setPen(QPen(kExtraColours[c % 4], 2));
			QPainterPath extraPath;
			bool extraOpen = false;
			for (std::size_t i = 0; i < extra.x.size(); ++i)
			{
				if (!std::isfinite(extra.y[i]))
				{
					extraOpen = false;
					continue;
				}
				const QPointF p(toX(extra.x[i]), toY(static_cast<double>(extra.y[i])));
				if (!extraOpen)
				{
					extraPath.moveTo(p);
					extraOpen = true;
				}
				else
					extraPath.lineTo(p);
			}
			painter.drawPath(extraPath);
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
			legendEntry(QColor(70, 130, 200), _series.title);
			for (std::size_t c = 0; c < _extraCurves.size(); ++c)
				legendEntry(kExtraColours[c % 4], _extraCurves[c].title);
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
	const std::size_t count = _histogram ? _histCounts.size() : _series.x.size();
	if (count == 0)
		return;
	const double fraction = std::clamp((pos.x() - plot.left()) / std::max(1.0, plot.width()), 0.0, 1.0);
	const int index = std::clamp(static_cast<int>(fraction * static_cast<double>(count)), 0, static_cast<int>(count) - 1);
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

// The x value under a point of the plot area, for seeking (the same mapping paintEvent() draws with: every curve's span).
void SimulationChartWidget::seekTo(const QPointF& pos)
{
	if (!_seekable || _histogram || _series.empty())
		return;
	const QRectF plot = plotRect();
	double xMin = _series.x.front(), xMax = _series.x.back();
	for (const ChartSeries& extra : _extraCurves)
	{
		xMin = std::min(xMin, extra.x.front());
		xMax = std::max(xMax, extra.x.back());
	}
	if (!(xMax > xMin))
		return;
	const double fraction = std::clamp((pos.x() - plot.left()) / std::max(1.0, plot.width()), 0.0, 1.0);
	emit seekRequested(xMin + fraction * (xMax - xMin));
}

void SimulationChartWidget::mousePressEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton && plotRect().contains(event->position()))
		seekTo(event->position());
}

void SimulationChartWidget::contextMenuEvent(QContextMenuEvent* event)
{
	if (_histogram)
		return;
	QMenu menu(this);
	QAction* add = menu.addAction(tr("Add curve from CSV..."));
	QAction* clear = menu.addAction(tr("Remove added curves"));
	clear->setEnabled(!_extraCurves.empty());
	QAction* chosen = menu.exec(event->globalPos());
	if (chosen == clear)
		clearCurves();
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
