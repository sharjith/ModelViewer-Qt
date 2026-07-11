#pragma once

#include <QDialog>
#include <QElapsedTimer>
#include <cstdint>
#include <memory>

namespace Ui
{
	class PathTracingDialog;
}

class ModelViewer;
class QTimer;
class QCloseEvent;
class QMdiSubWindow;

// ---------------------------------------------------------------------------
// PathTracingDialog
//
// Non-modal settings/progress/export dialog for the CPU path tracer, opened
// from the Tools menu (see MainWindow::on_actionPathTracing_triggered()).
// Non-modal deliberately - unlike SettingsDialog, the user is expected to
// watch the main viewport update live while adjusting settings and pressing
// Render, not fill out a form and close it.
//
// Does not own or duplicate any path-tracing state - every control here is a
// thin read/write onto the target ModelViewer's ViewportWidget (see
// ViewportWidget::setPathTracingMaxSamples()/pathTracingProgress()/
// requestPathTracedRenderNow()) and ModelViewer::onRenderingModeSelected()
// (the same entry point the toolbar's render-mode menu uses, so this
// dialog's Render/Stop buttons and the toolbar's mode indicator never fall
// out of sync with each other).
//
// One instance per document: parented to its ModelViewer (not MainWindow),
// so it's destroyed when that document closes rather than outliving every
// MDI document, and hidden/shown as that document's MDI subwindow loses/
// gains focus (onActiveSubWindowChanged()) - this app uses QMdiArea::
// SubWindowView, not tabbed, so switching documents doesn't hide/show
// widgets on its own, and a dialog left visible while controlling a
// different (inactive) document would be ambiguous about which one it's
// actually acting on.
// ---------------------------------------------------------------------------
class PathTracingDialog : public QDialog
{
	Q_OBJECT

public:
	explicit PathTracingDialog(ModelViewer* modelViewer, QWidget* parent = nullptr);
	~PathTracingDialog();

protected:
	void closeEvent(QCloseEvent* event) override;

private slots:
	void onMaxSamplesChanged(int value);
	void onMaxBouncesChanged(int value);
	void onDenoiserToggled(bool checked);
	void onDenoiserDeviceChanged(int index);
	void onFireflyClampChanged(double value);
	void onMaxTransmissionBouncesChanged(int value);
	void onRussianRouletteDepthChanged(int value);
	void onEnvImportanceSamplingToggled(bool checked);
	void onRenderClicked();
	void onStopClicked();
	void onExportClicked();
	void onRestoreDefaultsClicked();
	void onProgressTimer();
	void onActiveSubWindowChanged(QMdiSubWindow* activeSubWindow);
	void onExportResolutionChanged();
	void onMatchViewportClicked();
	void onResolutionPresetSelected(int index);

private:
	void updateButtonsForState(bool running, uint32_t currentSamples);
	void loadSettings(); // QSettings "pathtracing/*" - geometry + last-used values, loaded into the viewport before the UI reads them
	void saveSettings(); // called from closeEvent()
	void updateResolutionWarning(); // shows/hides labelResolutionWarning + adjustSize() on change, mirrors labelOrthoThinWallWarning's pattern
	void populateResolutionPresets();
	void syncResolutionPresetFromSpinboxes(); // selects the matching preset (or "Custom") after a manual width/height edit

	ModelViewer* _modelViewer; // not owned - dialog is a child of the ModelViewer's window
	QTimer* _progressTimer;
	std::unique_ptr<Ui::PathTracingDialog> ui;

	// RtPathTracingSession::stop() (what disarming ultimately calls)
	// deliberately leaves its last published sample count in place rather
	// than clearing it - so a camera-interrupted render can keep showing its
	// last frame until a fresh start() resets it on settle. That means this
	// dialog's own Stop button (which only stops, never restarts) would
	// otherwise show a frozen non-zero count instead of returning to Idle.
	// Tracked locally rather than changing RtPathTracingSession's semantics
	// (which other callers - camera interaction - correctly rely on).
	// Cleared the moment real progress is observed again (via Render, or
	// the camera settling and restarting outside this dialog entirely).
	bool _stoppedByUser = false;

	// Elapsed-time display below the progress bar. Restarted fresh at the
	// start of every render cycle - not just an explicit Render click, but
	// also whenever the camera settles and ViewportWidget auto-restarts a
	// session on its own (see onProgressTimer()'s rising-edge detection via
	// _wasSessionRunningLastPoll) - ticks live while running, then freezes
	// at whatever it last read once the session stops being active
	// (converged or Stop pressed) rather than continuing to run in the
	// background - a frozen "how long did that take" readout is more useful
	// than a clock that keeps advancing after the render is actually done.
	// An earlier version only ever restarted this on the Render button
	// click, so every camera-triggered auto-restart kept accumulating on
	// top of the very first render instead of timing its own cycle.
	QElapsedTimer _renderTimer;
	qint64 _frozenElapsedMs = -1; // -1 = not frozen (currently running, or never started)
	bool _wasSessionRunningLastPoll = false; // see onProgressTimer()'s rising-edge restart

	// Guards against onResolutionPresetSelected() -> spinbox setValue() ->
	// onExportResolutionChanged() -> syncResolutionPresetFromSpinboxes()
	// feeding back into the combo box mid-update (which would be harmless
	// in this exact case since it'd just re-select the same preset it came
	// from, but is fragile to rely on - explicit is safer than implicit).
	bool _updatingResolutionFromPreset = false;
};
