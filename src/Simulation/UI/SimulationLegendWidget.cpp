#include "SimulationLegendWidget.h"

#include "AnalysisColorRamp.h"

#include <QEvent>
#include <algorithm>
#include <cmath>
#include <QFontMetrics>
#include <QFont>
#include <QPainter>

namespace
{
	constexpr int kBarWidth = 280;
	constexpr int kBarHeight = 22;
	constexpr int kTitleHeight = 20;
	constexpr int kLabelHeight = 18;
	constexpr int kPadding = 6;
	constexpr int kMargin = 16;
	// Below the axis trihedron in the viewport's top-right corner; the bottom is taken by the reveal-on-hover
	// toolbar and the view cube.
	constexpr int kTopOffset = 64;
	// In compare mode the pane has no trihedron in its corner.
	constexpr int kPaneTopOffset = 12;
	// 0 = no backing box at all (like the navigation tree overlay). Raise (up to 255) for a tinted backing.
	constexpr int kBackgroundAlpha = 0;

	// White text with a thin dark outline, so it stays legible over both dark and light backgrounds now that
	// the legend has no backing box.
	void drawHaloText(QPainter& painter, const QRect& rect, int flags, const QString& text)
	{
		painter.setPen(QColor(0, 0, 0, 200));
		for (int dx = -1; dx <= 1; ++dx)
			for (int dy = -1; dy <= 1; ++dy)
				if (dx != 0 || dy != 0)
					painter.drawText(rect.translated(dx, dy), flags, text);
		painter.setPen(Qt::white);
		painter.drawText(rect, flags, text);
	}
}

SimulationLegendWidget::SimulationLegendWidget(QWidget* viewport)
	: QWidget(viewport)
{
	setAttribute(Qt::WA_TransparentForMouseEvents);
	// Translucent, like LassoOverlayWidget. (An opaque version was tried as a precaution while chasing the
	// "vanishing" legend; the real cause was the navigation panel stacking above it, see the class comment. If
	// the legend ever flickers over the GL view again, making paintEvent() fill an opaque colour is the
	// first thing to try.)
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_TranslucentBackground);
	if (viewport)
		viewport->installEventFilter(this);
	hide();
}

void SimulationLegendWidget::setLegend(const QString& title, float minValue, float maxValue, int colormap, int bands, const QString& toolTipText)
{
	_title = title;
	_minText = QString::number(minValue, 'g', 4);
	_maxText = QString::number(maxValue, 'g', 4);

	// The colour bar, one column per pixel, from the same colormap the shader uses.
	QPixmap bar(kBarWidth, kBarHeight);
	{
		const AnalysisColormap scheme = colormap == 1 ? AnalysisColormap::Diverging : AnalysisColormap::Sequential;
		QPainter painter(&bar);
		for (int x = 0; x < kBarWidth; ++x)
		{
			float t = static_cast<float>(x) / (kBarWidth - 1);
			if (bands >= 2) // same quantization as main_scene.frag: the colour at the centre of the band
			{
				const int band = std::min(static_cast<int>(std::floor(t * static_cast<float>(bands))), bands - 1);
				t = (static_cast<float>(band) + 0.5f) / static_cast<float>(bands);
			}
			painter.fillRect(x, 0, 1, kBarHeight, AnalysisColorRamp::colorForNormalized(t, scheme));
		}
	}
	_bar = bar;
	_hasContent = true;
	setToolTip(toolTipText);
	updateSize();
	reposition();
	refresh();
}

void SimulationLegendWidget::updateSize()
{
	const int heading = _heading.isEmpty() ? 0 : kTitleHeight;
	setFixedSize(kBarWidth + 2 * kPadding, heading + kTitleHeight + kBarHeight + kLabelHeight + 2 * kPadding);
}

void SimulationLegendWidget::setPane(std::function<QRect()> pane, const QString& heading)
{
	_pane = std::move(pane);
	_heading = heading;
	updateSize();
	reposition();
	update();
}

void SimulationLegendWidget::reposition()
{
	QWidget* viewport = parentWidget();
	if (!viewport)
		return;
	const QRect pane = _pane ? _pane() : QRect();
	if (pane.isValid() && !pane.isEmpty())
		move(pane.right() + 1 - width() - kMargin, pane.top() + kPaneTopOffset);
	else
		move(viewport->width() - width() - kMargin, kTopOffset);
}

void SimulationLegendWidget::refresh()
{
	const bool shouldShow = _hasContent && (!_alive || _alive());
	if (shouldShow != isVisible())
		setVisible(shouldShow);
	if (shouldShow)
	{
		raise();
		update();
	}
}

void SimulationLegendWidget::paintEvent(QPaintEvent*)
{
	if (!_hasContent)
		return;
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	if (kBackgroundAlpha > 0)
	{
		painter.setPen(Qt::NoPen);
		painter.setBrush(QColor(30, 30, 30, kBackgroundAlpha));
		painter.drawRoundedRect(rect(), 4, 4);
	}

	const int left = kPadding;
	int y = kPadding;
	if (!_heading.isEmpty())
	{
		QFont bold = painter.font();
		bold.setBold(true);
		painter.setFont(bold);
		drawHaloText(painter, QRect(left, y, kBarWidth, kTitleHeight), Qt::AlignLeft | Qt::AlignVCenter,
		             painter.fontMetrics().elidedText(_heading, Qt::ElideMiddle, kBarWidth));
		bold.setBold(false);
		painter.setFont(bold);
		y += kTitleHeight;
	}
	drawHaloText(painter, QRect(left, y, kBarWidth, kTitleHeight), Qt::AlignLeft | Qt::AlignVCenter,
	             painter.fontMetrics().elidedText(_title, Qt::ElideRight, kBarWidth));
	y += kTitleHeight;

	painter.drawPixmap(left, y, _bar);
	painter.setPen(QColor(0, 0, 0, 170));
	painter.setBrush(Qt::NoBrush);
	painter.drawRect(QRect(left, y, kBarWidth - 1, kBarHeight - 1));
	y += kBarHeight;

	drawHaloText(painter, QRect(left, y, kBarWidth / 2, kLabelHeight), Qt::AlignLeft | Qt::AlignVCenter, _minText);
	drawHaloText(painter, QRect(left + kBarWidth / 2, y, kBarWidth / 2, kLabelHeight), Qt::AlignRight | Qt::AlignVCenter, _maxText);
}

bool SimulationLegendWidget::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == parentWidget())
	{
		switch (event->type())
		{
		case QEvent::Resize:
			reposition();
			break;
		case QEvent::MouseMove:
		case QEvent::MouseButtonRelease:
		case QEvent::Wheel:
		case QEvent::Enter:
		case QEvent::Leave:
			refresh();
			break;
		default:
			break;
		}
	}
	return false; // never consume the viewport's events
}
