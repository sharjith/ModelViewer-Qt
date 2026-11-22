#pragma once

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QWidget>

class QScreen;

// Eyedropper: a frameless full-screen overlay showing a snapshot of one screen. A magnifier follows the cursor; a left click
// picks the colour under it, Esc or a right click cancels. The overlay deletes itself when it is done.
class ScreenColorPicker : public QWidget
{
	Q_OBJECT

public:
	// parent: the dialog that asked. A modal dialog blocks input to other windows, but not to its own children.
	ScreenColorPicker(QScreen* screen, QWidget* parent);

	// Takes the snapshot and shows the overlay. Hide the calling dialog first (and let it repaint) so it is not in the picture.
	void start();

signals:
	void colorPicked(const QColor& color);
	void cancelled();

protected:
	void paintEvent(QPaintEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;

private:
	QPoint imagePos(const QPoint& widgetPos) const;
	QColor colorAt(const QPoint& widgetPos) const;

	QScreen* _screen = nullptr;
	QImage _shot;
	QPoint _cursor;
};
