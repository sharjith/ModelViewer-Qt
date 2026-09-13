#pragma once

#include <QDialog>
#include <QVector>

#include "GltfCameraData.h"

#include <vector>

class QLabel;
class QListWidget;
class QComboBox;
class QSpinBox;
class QLineEdit;
class QPushButton;
class QProgressBar;
class ModelViewer;

// ---------------------------------------------------------------------------
// BatchRenderViewsDialog (Tools -> Batch Render Views...)
//
// Renders every captured camera view ("Capture View" in the Cameras tab) -
// or a checked subset - as a high-quality OFFLINE PATH-TRACED still image to
// disk, one file per view, in a single batch with progress and cancel.
// Distinct from Export Report (Tools -> Export Report...), which produces a
// single PDF from rasterized viewport screenshots of the same captured
// views - this produces individual full-quality path-traced image files.
//
// Modal, one-shot batch action - same reasoning as ReportExportDialog
// (there's no persistent tool state to keep in sync with the viewport here).
// Pure C++ widget construction (no .ui file), matching FilterByMaterialDialog/
// FilterByColorDialog's convention from this same session, rather than
// ReportExportDialog/RtRenderDialog's Designer-.ui convention - deliberate:
// a hand-written .ui XML file can't be visually verified without Designer,
// while plain C++ construction is exactly the same code either way and
// stays fully inspectable.
//
// Reuses, rather than duplicates: SceneGraph::gltfCameraDataForFile(
// capturedViewsSourceFileKey()) for the view list (same source
// ReportExportDialog reads), ViewportWidget::activateGltfCamera()/
// resetToSystemCamera() for per-view camera positioning and final restore
// (same functions, same "restore unconditionally when done" convention
// ReportExportDialog's own capture loop already establishes),
// ViewportWidget::renderRayTracedOffline()/cancelRayTracedOfflineRender()
// for the actual render (same blocking-call-with-progress-callback shape
// RtRenderDialog::onExportClicked() already uses for a single view), and
// the new RtImageExport::saveOfflineRender() (factored out of that same
// function) for the per-image save/tonemap.
// ---------------------------------------------------------------------------
class BatchRenderViewsDialog : public QDialog
{
	Q_OBJECT
public:
	explicit BatchRenderViewsDialog(ModelViewer* modelViewer, QWidget* parent = nullptr);

protected:
	// Escape (QDialog's default reject() path) during a render would
	// otherwise hide this dialog while onStartClicked()'s loop keeps running
	// underneath (renderRayTracedOffline()'s processEvents() pump delivers
	// the key event, but control doesn't return to that loop's own exec()
	// until the current C++ call stack unwinds) - redirect to Cancel instead
	// of letting that happen, same defensive-lifecycle spirit as
	// FilterByMaterialDialog/FilterByColorDialog's own reject() overrides
	// this session (different gotcha, same "don't let Escape silently skip
	// cleanup" reasoning).
	void reject() override;

private slots:
	void onResolutionPresetSelected(int index);
	void onExportResolutionChanged();
	void onBrowseFolderClicked();
	void onStartClicked();
	void onCancelClicked();

private:
	// One resolved (view index into the captured-views vector, output file
	// path) pair - built once, up front, before any rendering starts, so
	// filename sanitization/de-duplication never has to run mid-loop.
	struct Job
	{
		int viewIndex;
		QString path;
	};
	std::vector<Job> buildJobList(const QVector<GltfCameraEntry>& capturedViews) const;

	// Disables every OTHER interactive control, leaving only _cancelButton
	// enabled - same shape RtRenderDialog::onExportClicked() uses for its
	// own offline-render branch (see that function's doc comment for why:
	// never setEnabled(false) the whole dialog, since a disabled ancestor
	// makes an explicitly-enabled child inert too).
	void setRenderControlsEnabled(bool enabled);

	void populateResolutionPresets();
	void syncResolutionPresetFromSpinboxes();

	ModelViewer* _modelViewer; // not owned - dialog is a transient child of the ModelViewer document

	QLabel* _noViewsLabel = nullptr;
	QListWidget* _viewsList = nullptr;
	QComboBox* _resolutionPresetCombo = nullptr;
	QSpinBox* _widthSpin = nullptr;
	QSpinBox* _heightSpin = nullptr;
	QComboBox* _formatCombo = nullptr;
	QLineEdit* _outputFolderEdit = nullptr;
	QPushButton* _browseFolderButton = nullptr;
	QProgressBar* _overallProgressBar = nullptr;
	QProgressBar* _currentViewProgressBar = nullptr;
	QLabel* _statusLabel = nullptr;
	QPushButton* _startButton = nullptr;
	QPushButton* _cancelButton = nullptr;

	// Guards onExportResolutionChanged() against re-syncing the preset combo
	// back to itself while onResolutionPresetSelected() is the one driving
	// the spinbox values - same shape RtRenderDialog uses for the identical
	// preset/spinbox pairing.
	bool _updatingResolutionFromPreset = false;
	bool _renderInProgress = false;
	// Checked by onStartClicked()'s per-view loop between views (and passed
	// as renderRayTracedOffline()'s own cancel path stops the CURRENT view) -
	// set by onCancelClicked(), reset at the start of each Start click.
	bool _cancelRequested = false;
};
