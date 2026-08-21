#pragma once

#include <QDialog>
#include <QElapsedTimer>
#include <QString>
#include <cstdint>
#include <memory>

namespace Ui
{
	class RtRenderDialog;
}

class ModelViewer;
class QTimer;
class QCloseEvent;
class QMdiSubWindow;

// ---------------------------------------------------------------------------
// RtRenderDialog
//
// Non-modal settings/progress/export dialog for the CPU path tracer, opened
// from the Visualization menu (see MainWindow::on_actionRayTracing_triggered()).
// Non-modal deliberately - unlike SettingsDialog, the user is expected to
// watch the main viewport update live while adjusting settings and pressing
// Render, not fill out a form and close it.
//
// Does not own or duplicate any ray-tracing state - every control here is a
// thin read/write onto the target ModelViewer's ViewportWidget (see
// ViewportWidget::setRayTracingMaxSamples()/rayTracingProgress()/
// requestRayTracedRenderNow()) and ModelViewer::onRenderingModeSelected()
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
class RtRenderDialog : public QDialog
{
	Q_OBJECT

public:
	explicit RtRenderDialog(ModelViewer* modelViewer, QWidget* parent = nullptr);
	~RtRenderDialog();

protected:
	void closeEvent(QCloseEvent* event) override;

private slots:
	void onMaxSamplesChanged(int value);
	void onMaxBouncesChanged(int value);
	void onDenoiserToggled(bool checked);
	void onDenoiserDeviceChanged(int index);
	void onRenderEngineChanged(int index);
	void onFireflyClampChanged(double value);
	void onMaxTransmissionBouncesChanged(int value);
	void onRussianRouletteDepthChanged(int value);
	void onMaxShadowRayHitsChanged(int value);
	void onMaxVolumeScatterBouncesChanged(int value);
	void onEnvImportanceSamplingToggled(bool checked);
	void onShadowsToggled(bool checked);
	void onSelfShadowsToggled(bool checked);
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
	// Self-contained (queries the viewport itself, no parameters) so it can
	// be called freely from both the periodic progress poll (onProgressTimer())
	// and immediately on a resolution-spinbox edit (onExportResolutionChanged())
	// - Export must become available the instant an export resolution larger
	// than the viewport is entered, not wait for the next poll tick.
	void updateButtonsForState();
	// "M:SS" formatting shared by onProgressTimer()'s live/frozen elapsed
	// display and onExportClicked()'s offline-render progress callback -
	// the latter can't rely on onProgressTimer() at all since it stops that
	// timer for the whole blocking export (see onExportClicked()'s own doc
	// comment on why).
	static QString formatElapsedTime(qint64 elapsedMs);
	void loadSettings(); // QSettings "raytracing/*" - geometry + last-used values, loaded into the viewport before the UI reads them
	void saveSettings(); // called from closeEvent()
	void updateResolutionWarning(); // shows/hides labelResolutionWarning + adjustSize() on change, mirrors labelOrthoThinWallWarning's pattern
	void populateResolutionPresets();
	void syncResolutionPresetFromSpinboxes(); // selects the matching preset (or "Custom") after a manual width/height edit

	// Enables/disables spinBoxMaxShadowRayHits (and its label) based on
	// whether the CPU (Embree) engine is the one actually active right now -
	// see CpuPathTracer::Settings::maxShadowRayHits' doc comment for why this
	// setting has no effect at all on the OptiX (GPU) engine. Called from the
	// constructor and whenever comboBoxRenderEngine changes.
	void updateMaxShadowRayHitsEnabled();

	// Populates the Diagnostics tab from ViewportWidget::rayTracingDiagnostics().
	// Called from onProgressTimer() but gated there on the Diagnostics tab
	// actually being the visible one right now - every field is cheap to
	// read (see rayTracingDiagnostics()'s own doc comment), but there's no
	// reason to touch even that while nobody can see the result, and
	// samples/sec here does its own arithmetic on top. effectiveElapsedMs is
	// onProgressTimer()'s already-frozen elapsed time (see _frozenElapsedMs's
	// doc comment), NOT ViewportWidget::rayTracingElapsedMs()/diag.elapsedMs
	// directly - the latter is a live, never-reset session clock that keeps
	// ticking after Stop is pressed, which previously made Render Time/
	// Samples-per-sec/MRays-per-sec keep climbing even once rendering had
	// actually stopped.
	void refreshDiagnostics(qint64 effectiveElapsedMs);

	ModelViewer* _modelViewer; // not owned - dialog is a child of the ModelViewer's window
	QTimer* _progressTimer;
	std::unique_ptr<Ui::RtRenderDialog> ui;

	// RtRayTracingSession::stop() (what disarming ultimately calls)
	// deliberately leaves its last published sample count in place rather
	// than clearing it - so a camera-interrupted render can keep showing its
	// last frame until a fresh start() resets it on settle. That means this
	// dialog's own Stop button (which only stops, never restarts) would
	// otherwise show a frozen non-zero count instead of returning to Idle.
	// Tracked locally rather than changing RtRayTracingSession's semantics
	// (which other callers - camera interaction - correctly rely on).
	// Cleared the moment real progress is observed again (via Render, or
	// the camera settling and restarting outside this dialog entirely).
	bool _stoppedByUser = false;

	// Set for the duration of onExportClicked()'s blocking offline-render
	// branch (needsOfflineRender==true) - repurposes pushButtonStop (label
	// swapped to "Cancel" for the duration) to call ViewportWidget::
	// cancelRayTracedOfflineRender() instead of its normal onStopClicked()
	// interactive-session-stop behavior, since a blocking offline render has
	// no running RtRayTracingSession/RtOptixRayTracingSession to stop in
	// the first place. See onExportClicked()'s own doc comment for the rest
	// of the cancellation mechanism (why the whole dialog can't just be
	// setEnabled(false) the way it used to be).
	bool _offlineRenderInProgress = false;

	// Elapsed-time display below the progress bar. The LIVE value is read
	// straight from ViewportWidget::rayTracingElapsedMs() every poll - that
	// clock is authoritative (restarted at the one place a session actually
	// begins, regardless of what triggered it: this dialog's Render button, a
	// keyboard shortcut, or ViewportWidget auto-restarting on its own after
	// the camera settles), so this dialog no longer needs to guess when a
	// render cycle began by watching for its own rising edge. An earlier
	// version kept its own independent QElapsedTimer that only started once
	// THIS dialog observed a running session for the first time - reopening
	// the dialog after a session was already started via keyboard shortcut
	// (with no dialog open to observe the rising edge) showed elapsed time
	// counting up from 0 instead of the session's real age.
	// _frozenElapsedMs is continuously updated to the live elapsed value
	// while a session is running (see onProgressTimer()), so it's always
	// ready to serve as "the render's final duration" the instant running
	// stops - once stopped, that last captured value is what's displayed/
	// reported (Diagnostics tab included) instead of the display continuing
	// to imply the render is still going, or ViewportWidget's own live,
	// never-reset session clock inflating rates like samples/sec toward zero
	// forever after Stop.
	qint64 _frozenElapsedMs = -1; // -1 = never started (no session observed yet this dialog's lifetime)

	// Guards against onResolutionPresetSelected() -> spinbox setValue() ->
	// onExportResolutionChanged() -> syncResolutionPresetFromSpinboxes()
	// feeding back into the combo box mid-update (which would be harmless
	// in this exact case since it'd just re-select the same preset it came
	// from, but is fragile to rely on - explicit is safer than implicit).
	bool _updatingResolutionFromPreset = false;
};
