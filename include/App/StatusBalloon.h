#pragma once

#include <QFrame>

class QLabel;
class QTimer;

// A small, non-modal pop-up that sits just above the status bar and explains something in a few wrapped lines - for messages too long for the
// status bar's single clipped line. It never takes focus, hides itself after a while, and a click on it dismisses it. Colours are set outright from the
// application palette (with a contrast check), so it is readable in every theme.
class StatusBalloon : public QFrame
{
	Q_OBJECT
public:
	explicit StatusBalloon(QWidget* anchorWindow);

	// Shows `text` (wrapped to a fixed width) at the bottom-left of the anchor window, above its status bar, for `timeoutMs` (0 = until clicked).
	void showText(const QString& text, int timeoutMs);

protected:
	void mousePressEvent(QMouseEvent* event) override;

private:
	void applyColours();
	QWidget* _anchor = nullptr;
	QLabel* _label = nullptr;
	QTimer* _timer = nullptr;
};
