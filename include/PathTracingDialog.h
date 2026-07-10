#pragma once

#include <QDialog>
#include <cstdint>
#include <memory>

namespace Ui
{
	class PathTracingDialog;
}

class ModelViewer;
class QTimer;
class QCloseEvent;

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
	void onFireflyClampChanged(double value);
	void onMaxTransmissionBouncesChanged(int value);
	void onRussianRouletteDepthChanged(int value);
	void onEnvImportanceSamplingToggled(bool checked);
	void onRenderClicked();
	void onStopClicked();
	void onExportClicked();
	void onRestoreDefaultsClicked();
	void onProgressTimer();

private:
	void updateButtonsForState(bool running, uint32_t currentSamples);
	void loadSettings(); // QSettings "pathtracing/*" - geometry + last-used values, loaded into the viewport before the UI reads them
	void saveSettings(); // called from closeEvent()

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
};
