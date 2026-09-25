#pragma once

#include <QPixmap>
#include <QRect>
#include <QString>
#include <QWidget>

#include <functional>

// Colour-bar legend for a simulation result, drawn as a small overlay child of the viewport (top-right, below
// the axis trihedron). Transparent to mouse events; paints itself with QPainter: no backing box (see
// kBackgroundAlpha in the .cpp), so the title and min/max labels are drawn with a thin dark outline to stay
// legible over any background.
//
// Stacking order: the navigation panel re-raises itself on nearly every mouse move over the viewport
// (ModelViewer::revealNavigation()), which buried an earlier version of this legend underneath it - it looked
// like it vanished on every mouse move. ModelViewer::revealNavigation() therefore raises this legend right
// after each of those raises (just before it re-raises the toolbar). The widget repositions itself when the
// viewport resizes, and hides itself while the result it describes is no longer displayed (e.g. after
// Undo) - see setAliveCheck().
class SimulationLegendWidget : public QWidget
{
public:
	explicit SimulationLegendWidget(QWidget* viewport);

	// Sets the title, value range and colour scheme shown, builds the bar, and shows the legend. `colormap` is an
	// AnalysisColormap value (0 sequential, 1 diverging); `bands` >= 2 draws the bar as that many flat bands, the
	// same quantization the shader applies, 0 draws it smooth.
	void setLegend(const QString& title, float minValue, float maxValue, int colormap, int bands, const QString& toolTipText);

	// Compare mode: the legend sits in the top-right corner of its own pane instead of the viewport's, and names its
	// result in a heading line above the title. `pane` gives the pane's current rectangle (widget coordinates; an
	// empty rectangle = the viewport's own corner); pass an empty function and heading to go back to the normal layout.
	void setPane(std::function<QRect()> pane, const QString& heading);

	// Called on viewport mouse events; the legend is shown only while this returns true (default: always).
	void setAliveCheck(std::function<bool()> alive) { _alive = std::move(alive); refresh(); }

	// Re-evaluates visibility, then raises and repaints.
	void refresh();
	void reposition();

protected:
	void paintEvent(QPaintEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void updateSize();

	QPixmap _bar;
	QString _title, _minText, _maxText, _heading;
	std::function<QRect()> _pane;
	bool _hasContent = false;
	std::function<bool()> _alive;
};
