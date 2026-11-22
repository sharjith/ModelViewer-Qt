#include "BackgroundPreview.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>

namespace
{
constexpr int kMargin = 20;       // space around the gradient ends for the swatches
constexpr double kSwatchRadius = 9.0;
constexpr double kHitRadius = 13.0;

double lumaOf(const QColor& c)
{
	return 0.2126 * c.redF() + 0.7152 * c.greenF() + 0.0722 * c.blueF();
}

double smoothStep(double edge0, double edge1, double x)
{
	const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
	return t * t * (3.0 - 2.0 * t);
}

// The weight of the first (top) colour at a position in the preview: the formulas of background.frag. u runs left to right,
// v bottom to top (the shader's v_uv).
double firstWeight(int style, double u, double v)
{
	switch (style)
	{
	case 1:  return 1.0 - u;                           // horizontal: the first colour at the left
	case 2:  return 1.0 - (u + (1.0 - v)) * 0.5;       // top-left to bottom-right
	case 3:  return 1.0 - ((1.0 - u) + (1.0 - v)) * 0.5; // top-right to bottom-left
	default: return v;                                 // vertical: the first colour at the top
	}
}

// What wireframe.frag does to a line colour over a background of luminance bgLum (the push to one side for the whole screen).
// Kept in step with that shader by hand; it only drives this preview.
QColor pushedLine(const QColor& line, double bgLum, bool darker)
{
	const double lineLum = lumaOf(line);
	const double gap = std::abs(lineLum - bgLum);
	const bool onPushedSide = darker ? lineLum <= bgLum : lineLum >= bgLum;
	const double weight = onPushedSide ? 1.0 - smoothStep(0.12, 0.28, gap) : 1.0 - smoothStep(0.0, 0.01, gap);
	if (weight <= 0.0)
		return line;
	const double target = darker ? std::max(bgLum - 0.38, 0.04) : std::min(bgLum + 0.38, 0.96);
	double r = line.redF(), g = line.greenF(), b = line.blueF();
	double ar, ag, ab;
	if (target < lineLum)
	{
		const double scale = target / std::max(lineLum, 0.001);
		ar = r * scale; ag = g * scale; ab = b * scale;
	}
	else
	{
		const double delta = target - lineLum;
		ar = std::clamp(r + delta, 0.0, 1.0); ag = std::clamp(g + delta, 0.0, 1.0); ab = std::clamp(b + delta, 0.0, 1.0);
	}
	return QColor::fromRgbF(float(r + (ar - r) * weight), float(g + (ag - g) * weight), float(b + (ab - b) * weight));
}
}

BackgroundPreview::BackgroundPreview(QWidget* parent) : QWidget(parent)
{
	setMouseTracking(true);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

bool BackgroundPreview::isStrongGradient(const QColor& first, const QColor& second, bool gradient)
{
	if (!gradient)
		return false;
	const double a = lumaOf(first), b = lumaOf(second);
	return std::min(a, b) < 0.35 && std::max(a, b) > 0.65;
}

QImage BackgroundPreview::renderBackground(const QSize& size, const QColor& first, const QColor& second, int gradientStyle, bool gradient)
{
	if (size.isEmpty())
		return QImage();
	QImage image(size, QImage::Format_RGB32);
	const int w = size.width(), h = size.height();
	for (int y = 0; y < h; ++y)
	{
		QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
		const double v = 1.0 - (y + 0.5) / h;
		for (int x = 0; x < w; ++x)
		{
			const double a = gradient ? firstWeight(gradientStyle, (x + 0.5) / w, v) : 1.0;
			line[x] = qRgb(int(std::lround(first.red() * a + second.red() * (1.0 - a))),
				int(std::lround(first.green() * a + second.green() * (1.0 - a))),
				int(std::lround(first.blue() * a + second.blue() * (1.0 - a))));
		}
	}
	return image;
}

void BackgroundPreview::setState(const QColor& first, const QColor& second, int gradientStyle, bool gradient)
{
	_first = first;
	_second = second;
	_style = std::clamp(gradientStyle, 0, 3);
	_gradient = gradient;
	rebuildBackground();
	update();
}

void BackgroundPreview::setActiveColor(int which)
{
	if (_active == which)
		return;
	_active = which;
	update();
}

void BackgroundPreview::rebuildBackground()
{
	_background = renderBackground(size(), _first, _second, _style, _gradient);
}

void BackgroundPreview::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	rebuildBackground();
}

QRect BackgroundPreview::innerRect() const
{
	return rect().adjusted(kMargin, kMargin, -kMargin, -kMargin);
}

QPointF BackgroundPreview::handlePos(int which) const
{
	const QRect r = innerRect();
	const QPointF c = r.center();
	if (!_gradient)
		return c; // solid: the one swatch in the middle
	QPointF first, second;
	switch (_style)
	{
	case 1:  first = QPointF(r.left(), c.y());   second = QPointF(r.right(), c.y()); break;
	case 2:  first = QPointF(r.left(), r.top()); second = QPointF(r.right(), r.bottom()); break;
	case 3:  first = QPointF(r.right(), r.top()); second = QPointF(r.left(), r.bottom()); break;
	default: first = QPointF(c.x(), r.top());    second = QPointF(c.x(), r.bottom()); break;
	}
	return which == 0 ? first : second;
}

int BackgroundPreview::hitHandle(const QPoint& pos) const
{
	const int count = _gradient ? 2 : 1;
	int best = -1;
	double bestDistance = kHitRadius;
	for (int i = 0; i < count; ++i)
	{
		const QPointF d = QPointF(pos) - handlePos(i);
		const double distance = std::hypot(d.x(), d.y());
		if (distance <= bestDistance)
		{
			bestDistance = distance;
			best = i;
		}
	}
	return best;
}

double BackgroundPreview::lumaAt(const QPointF& pos) const
{
	if (_background.isNull())
		return 0.5;
	const int x = std::clamp(int(pos.x()), 0, _background.width() - 1);
	const int y = std::clamp(int(pos.y()), 0, _background.height() - 1);
	const QRgb p = _background.pixel(x, y);
	return (0.2126 * qRed(p) + 0.7152 * qGreen(p) + 0.0722 * qBlue(p)) / 255.0;
}

void BackgroundPreview::drawSamples(QPainter& painter) const
{
	// Tall outlined boxes in a few typical line colours: dark grey, mid grey, light grey, blue, yellow, red. Each crosses the whole
	// gradient, so the colour shifts along a line show where the wireframe shader pushes it (and where it adds a halo).
	static const QColor kLines[] = {
		QColor::fromRgbF(0.14f, 0.14f, 0.14f), QColor::fromRgbF(0.50f, 0.50f, 0.50f), QColor::fromRgbF(0.86f, 0.86f, 0.86f),
		QColor::fromRgbF(0.17f, 0.39f, 0.64f), QColor::fromRgbF(0.75f, 0.70f, 0.10f), QColor::fromRgbF(0.62f, 0.06f, 0.06f) };
	const bool strong = isStrongGradient(_first, _second, _gradient);
	const double averageLuma = _gradient ? (lumaOf(_first) + lumaOf(_second)) * 0.5 : lumaOf(_first);
	const bool darker = averageLuma > 0.4; // one side for the whole screen, as in the shader

	const QRect r = innerRect();
	const int count = int(sizeof(kLines) / sizeof(kLines[0]));
	painter.setRenderHint(QPainter::Antialiasing, true);

	const auto segmentLine = [&](const QPointF& a, const QPointF& b, const QColor& line)
	{
		const double length = std::hypot(b.x() - a.x(), b.y() - a.y());
		const int steps = std::max(1, int(std::ceil(length / 3.0)));
		for (int i = 0; i < steps; ++i)
		{
			const QPointF p0 = a + (b - a) * (double(i) / steps);
			const QPointF p1 = a + (b - a) * (double(i + 1) / steps);
			const double bgLum = lumaAt((p0 + p1) * 0.5);
			if (strong)
			{
				// The halo: a wider underlay of the opposite luminance, then the line in its own colour.
				const QColor halo = lumaOf(line) > 0.5 ? QColor(0, 0, 0, 166) : QColor(255, 255, 255, 166);
				painter.setPen(QPen(halo, 3.4, Qt::SolidLine, Qt::FlatCap));
				painter.drawLine(p0, p1);
				painter.setPen(QPen(line, 1.3, Qt::SolidLine, Qt::FlatCap));
			}
			else
				painter.setPen(QPen(pushedLine(line, bgLum, darker), 1.3, Qt::SolidLine, Qt::FlatCap));
			painter.drawLine(p0, p1);
		}
	};

	for (int i = 0; i < count; ++i)
	{
		const double cx = r.left() + (i + 0.5) * r.width() / count;
		const double x0 = cx - 8.0, x1 = cx + 8.0;
		const double y0 = r.top() + 16.0, y1 = r.bottom() - 16.0;
		segmentLine(QPointF(x0, y0), QPointF(x1, y0), kLines[i]);
		segmentLine(QPointF(x1, y0), QPointF(x1, y1), kLines[i]);
		segmentLine(QPointF(x1, y1), QPointF(x0, y1), kLines[i]);
		segmentLine(QPointF(x0, y1), QPointF(x0, y0), kLines[i]);
	}
}

void BackgroundPreview::drawSwatch(QPainter& painter, int which) const
{
	const QPointF c = handlePos(which);
	const QColor color = which == 0 ? _first : _second;
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setPen(QPen(QColor(0, 0, 0, 170), 3.0));
	painter.setBrush(Qt::NoBrush);
	painter.drawEllipse(c, kSwatchRadius + 1.0, kSwatchRadius + 1.0);
	painter.setPen(QPen(which == _active ? palette().color(QPalette::Highlight) : QColor(255, 255, 255), which == _active ? 2.6 : 1.6));
	painter.setBrush(color);
	painter.drawEllipse(c, kSwatchRadius, kSwatchRadius);
}

void BackgroundPreview::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	if (!_background.isNull())
		painter.drawImage(0, 0, _background);
	drawSamples(painter);
	drawSwatch(painter, 0);
	if (_gradient)
		drawSwatch(painter, 1);
	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.setPen(QColor(0, 0, 0, 120));
	painter.setBrush(Qt::NoBrush);
	painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

void BackgroundPreview::mousePressEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton)
		return;
	_pressed = hitHandle(event->position().toPoint());
	_pressPos = event->position().toPoint();
	_dragging = false;
}

void BackgroundPreview::mouseMoveEvent(QMouseEvent* event)
{
	if (_pressed < 0)
	{
		setCursor(hitHandle(event->position().toPoint()) >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
		return;
	}
	// Dragging the first swatch picks the gradient direction: its angle around the middle snaps to the four directions the
	// background shader draws (the first colour at the top, the top-left, the left or the top-right).
	if (_pressed != 0 || !_gradient)
		return;
	if (!_dragging && (event->position().toPoint() - _pressPos).manhattanLength() < 5)
		return;
	_dragging = true;
	const QPointF v = QPointF(event->position().toPoint()) - QPointF(innerRect().center());
	if (std::hypot(v.x(), v.y()) < 10.0)
		return;
	double angle = std::atan2(-v.y(), v.x()) * 180.0 / 3.14159265358979323846; // 0 = right, 90 = up
	if (angle < 0.0)
		angle += 180.0; // the opposite half maps onto the same direction: the second colour follows on the far side
	static const int kStyleForStep[5] = { 1, 3, 0, 2, 1 }; // 0, 45, 90, 135, 180 degrees
	const int style = kStyleForStep[std::clamp(int(std::lround(angle / 45.0)), 0, 4)];
	if (style != _style)
		emit gradientStyleDragged(style);
}

void BackgroundPreview::mouseReleaseEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton)
		return;
	const int pressed = _pressed;
	const bool dragged = _dragging;
	_pressed = -1;
	_dragging = false;
	if (pressed >= 0 && !dragged && hitHandle(event->position().toPoint()) == pressed)
		emit colorClicked(pressed);
}
