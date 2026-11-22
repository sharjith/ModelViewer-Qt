#pragma once

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QSize>
#include <QWidget>

class QPainter;

// Preview of the viewport background for the Background Color dialog. It draws the background exactly as background.frag does
// (same gradient formulas, so a diagonal looks the same here and in the viewport), a row of sample wireframe lines on top
// (what the wireframe shader would draw over that background), and a swatch at each end of the gradient: a click on a swatch
// asks for that colour, dragging the first swatch around the preview picks the gradient direction.
class BackgroundPreview : public QWidget
{
	Q_OBJECT

public:
	explicit BackgroundPreview(QWidget* parent = nullptr);

	// gradientStyle: 0 vertical, 1 horizontal, 2 top-left to bottom-right, 3 top-right to bottom-left (as in background.frag).
	void setState(const QColor& first, const QColor& second, int gradientStyle, bool gradient);
	// 0 = first colour, 1 = second colour: the swatch that gets the highlight ring.
	void setActiveColor(int which);

	// A gradient that spans mid-grey: the wireframe shader draws a halo under the lines there (see wireframe.frag).
	static bool isStrongGradient(const QColor& first, const QColor& second, bool gradient);
	// The background as the shader draws it, for a preview or an icon.
	static QImage renderBackground(const QSize& size, const QColor& first, const QColor& second, int gradientStyle, bool gradient);

	QSize sizeHint() const override { return QSize(300, 200); }
	QSize minimumSizeHint() const override { return QSize(220, 150); }

signals:
	void colorClicked(int which);
	void gradientStyleDragged(int gradientStyle);

protected:
	void paintEvent(QPaintEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;

private:
	QRect innerRect() const;
	QPointF handlePos(int which) const;
	int hitHandle(const QPoint& pos) const; // -1 = none
	double lumaAt(const QPointF& pos) const;
	void rebuildBackground();
	void drawSamples(QPainter& painter) const;
	void drawSwatch(QPainter& painter, int which) const;

	QColor _first = QColor(115, 115, 115);
	QColor _second = QColor(230, 230, 230);
	int _style = 0;
	bool _gradient = true;
	int _active = 0;
	QImage _background;

	int _pressed = -1;
	QPoint _pressPos;
	bool _dragging = false;
};
