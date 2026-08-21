#pragma once

#include <QDialog>
#include <QSet>
#include "MeasurementData.h"

class ModelViewer;
class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QCloseEvent;
class QMdiSubWindow;

// ---------------------------------------------------------------------------
// MeasurementDialog
//
// Non-modal "Measure" dialog (Tools -> Measure...) - replaces the old pair of
// checkable Tools-menu actions (Measure Point / Measure Distance) with a
// single combo box covering every MeasurementTool, so adding future tools
// (arc-radius now, Face/Edge/Edge-radius later - see MeasurementData.h) never
// needs another menu action. Mirrors RtRenderDialog's non-modal, per-document
// pattern: parented to the target ModelViewer (not MainWindow) so it closes
// with its document, WA_DeleteOnClose, reused/raised via findChild() rather
// than stacking a new instance per click (see MainWindow.cpp's
// actionMeasure handler).
//
// Owns no measurement state itself - every control is a thin read/write onto
// the target ModelViewer's ViewportWidget (setMeasurementTool()/
// measurementTool()/selectedMeasurementIds()/measurementSummaryText()) and
// SceneGraph (measurements()/measurementsChanged), the same "thin front-end"
// relationship RtRenderDialog has with ray tracing.
// ---------------------------------------------------------------------------
class MeasurementDialog : public QDialog
{
	Q_OBJECT

public:
	explicit MeasurementDialog(ModelViewer* modelViewer, QWidget* parent = nullptr);
	~MeasurementDialog();

protected:
	void closeEvent(QCloseEvent* event) override;

private slots:
	void onToolComboChanged(int index);
	void onResultsSelectionChanged();
	void onDeleteClicked();
	void onFinishClicked();
	void onMeasurementProgressChanged(int picked, int required);
	void onMeasurementToolChangedExternally(MeasurementTool tool);
	void onMeasurementsChanged();
	// Viewport -> dialog direction of selection sync (a measurement was
	// clicked directly in the 3D view) - distinct from onResultsSelectionChanged()
	// (dialog -> viewport), which reads the list's own selection rather than
	// this signal's ids, so the two can't just share a slot.
	void onViewportSelectionChanged(const QSet<QUuid>& ids);
	// Fires for both the checkbox (show/hide) and, harmlessly, any other
	// per-item change QListWidget might report - only the check state is
	// acted on (see setMeasurementVisible()'s not-undoable, plain-display-
	// setting convention, same as mesh-visibility checkboxes elsewhere).
	void onResultItemChanged(QListWidgetItem* item);
	// Hides/shows this dialog as its own document's MDI subwindow loses/gains
	// focus - same reasoning as RtRenderDialog::onActiveSubWindowChanged():
	// SubWindowView mode doesn't hide/show widgets on document switch by
	// itself, and a measurement tool armed for one document's viewport would
	// otherwise look like it applies to whichever document is now active.
	void onActiveSubWindowChanged(QMdiSubWindow* activeSubWindow);

private:
	void refreshResultsList();
	// Selects `tool` in the combo without re-triggering onToolComboChanged()'s
	// own setMeasurementTool() call - needed when the sync is going the OTHER
	// way (viewport -> dialog, e.g. Escape cancelling the tool), since the
	// combo's normal currentIndexChanged path always assumes the dialog is
	// the one driving the change.
	void setComboToolSilently(MeasurementTool tool);

	// Window position/size, under QSettings key "measurement/geometry" -
	// same saveGeometry()/restoreGeometry() pattern as RtRenderDialog's
	// loadSettings()/saveSettings(). loadSettings() runs at the end of the
	// constructor (after every widget exists, since restoreGeometry() can
	// resize them); saveSettings() runs from closeEvent().
	void loadSettings();
	void saveSettings();

	ModelViewer*  _modelViewer; // not owned - dialog is a child of the ModelViewer's window
	QComboBox*    _toolCombo;
	QLabel*       _statusLabel;
	// Completes a variable-anchor-count tool's in-progress pick sequence
	// (currently only Pitch Circle) with whatever's been picked so far -
	// see ViewportWidget::finishVariableLengthMeasurement(). Visible only
	// while such a tool is armed, enabled only once its minimum anchor
	// count is reached; the Enter/Return key does the same thing directly
	// in the viewport, this is just the discoverable/clickable form of it.
	QPushButton*  _finishButton;
	QListWidget*  _resultsList;
	QPushButton*  _deleteButton;

	// True while this dialog is programmatically updating _resultsList's
	// selection or check-state from a full refreshResultsList() rebuild or a
	// setSelectedMeasurementIds() sync - guards both onResultsSelectionChanged()
	// (which would otherwise redundantly call setSelectedMeasurementIds()
	// again) and onResultItemChanged() (which would otherwise call
	// setMeasurementVisible() while just reflecting the model, not changing
	// it) - same one-flag reentrancy guard pattern as RtRenderDialog's
	// _updatingResolutionFromPreset.
	bool _updatingSelectionFromViewport = false;
};
