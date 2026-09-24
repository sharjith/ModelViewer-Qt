#pragma once

#include <QPoint>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <utility>

class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;
class QToolButton;

// Playback controls for a multi-step simulation result, drawn as a small overlay child of the viewport (top
// centre): previous / play-pause / next, a step slider, the current step's description, Loop and a speed choice.
// See docs/simulation_results_design.md section 9.
//
// Deviation from that design note: the note put the timeline at the viewport's BOTTOM edge, but the reveal-on-hover
// toolbar lives there, so it sits at the top centre instead (the legend is top-right, the navigation tree top-left).
//
// It only reports what the user did (signals); ModelViewer owns the timer and the current step and pushes state
// back through the setters, none of which emit. Like the legend it (a) is raised by ModelViewer::revealNavigation()
// after the navigation panel raises itself (that panel re-raises on nearly every mouse move and would otherwise
// bury it), and (b) shows only while the result it belongs to is displayed (setAliveCheck()).
class SimulationTimelineWidget : public QWidget
{
	Q_OBJECT
public:
	explicit SimulationTimelineWidget(QWidget* viewport);

	// `describe(i)` gives the text of step i (e.g. "Mode 3 - 73971 Hz"). Call again when the result changes.
	void setSteps(int count, const std::function<QString(int)>& describe);
	void setCurrentStep(int step);
	void setPlaying(bool playing);
	void setLoop(bool loop);
	void setSpeed(double speed); // 0.5, 1, 2 or 4

	// The timeline is shown only while this returns true (default: never, until set).
	void setAliveCheck(std::function<bool()> alive) { _alive = std::move(alive); refresh(); }
	void refresh();
	void reposition();

signals:
	void stepRequested(int step);
	void playRequested(bool play);
	void loopChanged(bool loop);
	void speedChanged(double speed);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void updateText();
	void savePlacement() const;
	void loadPlacement();
	void resetPosition(); // back to the default top-centre spot
	void updateGrip();

	QToolButton* _prevButton = nullptr;
	QToolButton* _playButton = nullptr;
	QToolButton* _nextButton = nullptr;
	QSlider* _slider = nullptr;
	QLabel* _label = nullptr;
	QCheckBox* _loopCheck = nullptr;
	QLabel* _grip = nullptr;         // drag handle (left edge); double-click resets the position
	QToolButton* _pinButton = nullptr; // pinned = position locked
	QComboBox* _speedCombo = nullptr;

	QStringList _texts;
	int _count = 0;
	bool _playing = false;
	std::function<bool()> _alive;

	// Placement: by default top centre; once dragged, the centre's x as a fraction of the viewport width and the top
	// y in pixels, so a resize keeps it in place. Pinned locks it against accidental drags. Both persist (QSettings).
	bool _customPos = false;
	double _fracX = 0.5;
	int _topY = 12;
	bool _pinned = false;
	bool _dragging = false;
	QPoint _dragOffset;
};
