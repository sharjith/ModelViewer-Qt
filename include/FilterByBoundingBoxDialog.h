#pragma once

#include <QDialog>
#include "BoundingBox.h"

class QLabel;
class QPushButton;
class QGroupBox;
class QDoubleSpinBox;
class QRadioButton;
class ModelViewer;
class QMdiSubWindow;
class QShowEvent;
class QCloseEvent;

// "Selection -> Filter by Bounding Box..." - lets the user type world-space
// min/max X/Y/Z limits and live-previews the viewport selection as every
// mesh whose world-space bounding box (SceneMesh::getBoundingBox(), already
// post-transform - see MeshInstanceState::fastUpdateWorldBounds()) matches,
// either fully enclosed ("Fully Inside") or any overlap ("Any Overlap"), so
// Show Only/Hide can act on the result immediately. This exists because the
// rubber-band/window-zoom approach to isolating a spatial region is
// inherently imprecise (camera framing and screen-space depth overlap get in
// the way) - a numeric filter sidesteps both entirely.
//
// Deliberately axis-aligned only (world X/Y/Z), not a rotated/oriented box -
// an oriented box would need real per-mesh geometry for a correct "fully
// inside" test (re-bounding a rotated AABB from just its own 8 cached
// corners overestimates the mesh's true extent) and a UI where the numbers
// stop meaning "world X between A and B", both real costs for a case that
// hasn't come up in practice.
//
// The Y/Z row whose axis is the app's current "up" (see
// ViewportWidget::isCameraUpAxisZUp()) is labeled with a "(Height)" suffix -
// read once at construction, matching FilterByColorDialog/
// FilterByMaterialDialog's own "read scene/view state once, no live
// viewport-signal subscription" convention for dialog criteria, rather than
// being the one filter dialog that behaves differently.
//
// Same non-modal, per-document singleton, MDI-activation-synced, live-preview
// undo-merging shape as FilterByColorDialog - see that class's header doc
// comment for the full rationale; this dialog reuses the identical
// mechanism, just with six numeric limits instead of a color list.
class FilterByBoundingBoxDialog : public QDialog
{
	Q_OBJECT
public:
	// initialBounds: the box to seed the six fields with at open time - see
	// ModelViewer::filterSelectionByBoundingBox()'s doc comment for how this
	// is derived (current selection's combined bounds if any, else the whole
	// scene's).
	explicit FilterByBoundingBoxDialog(ModelViewer* modelViewer,
	                                    const BoundingBox& initialBounds,
	                                    QWidget* parent = nullptr);

protected:
	void showEvent(QShowEvent* event) override;
	// Refreshes matches/live selection when this dialog's window regains
	// OS-level activation - same gap FilterByColorDialog's identical
	// override closes (see its own doc comment).
	void changeEvent(QEvent* event) override;
	void closeEvent(QCloseEvent* event) override;
	// QDialog's own Escape handling calls reject(), which never raises a
	// QCloseEvent - this override exists purely so saveSettings() still runs
	// on an Escape-closed dialog, same as FilterByColorDialog's identical
	// override.
	void reject() override;

private slots:
	void onLimitsChanged();
	void onContainmentModeChanged();
	// Re-seeds the six fields from the current selection's combined bounds
	// (or does nothing if the selection is empty) - a re-triggerable version
	// of the same seeding this dialog's constructor already does once, for
	// when the user's selection changes while the dialog stays open.
	void onUseSelectionBoundsClicked();
	void onShowOnlyClicked();
	void onHideClicked();
	void onActiveSubWindowChanged(QMdiSubWindow* activeSubWindow);
	// Same suppress-the-push-while-rebuilding mechanism as
	// FilterByColorDialog::onUndoStackIndexChanged() - see that method's own
	// doc comment for why an undo/redo-triggered rebuild must never push a
	// new selection command.
	void onUndoStackIndexChanged();

private:
	// Recomputes matches against a fresh mesh store, updates the match-count
	// label and button enablement, and pushes the result as the live
	// viewport selection (undo-merged) - same mechanism as
	// FilterByColorDialog::updateMatches(), unconditional here (there's no
	// "empty criteria" analog for six always-present numeric fields the way
	// an empty color list means "not armed").
	void updateMatches();

	void loadSettings();
	void saveSettings();

	ModelViewer* _modelViewer; // not owned - dialog is a child of the ModelViewer's window

	QDoubleSpinBox* _xMinSpin = nullptr;
	QDoubleSpinBox* _xMaxSpin = nullptr;
	QDoubleSpinBox* _yMinSpin = nullptr;
	QDoubleSpinBox* _yMaxSpin = nullptr;
	QDoubleSpinBox* _zMinSpin = nullptr;
	QDoubleSpinBox* _zMaxSpin = nullptr;
	QLabel* _yRowLabel = nullptr; // "Y" or "Y (Height)" - fixed at construction, see class doc comment
	QLabel* _zRowLabel = nullptr; // "Z" or "Z (Height)" - fixed at construction, see class doc comment

	QRadioButton* _anyOverlapRadio = nullptr;  // "Crossing"-style - BoundingBox::intersects()
	QRadioButton* _fullyInsideRadio = nullptr; // "Window"-style - BoundingBox::contains()

	QLabel* _matchCountLabel = nullptr;
	QPushButton* _useSelectionBoundsButton = nullptr;
	QPushButton* _showOnlyButton = nullptr;
	QPushButton* _hideButton = nullptr;

	// Set for the duration of an undo/redo-triggered rebuild - see
	// onUndoStackIndexChanged()'s doc comment. Same confirmed-real-bug
	// prevention as FilterByColorDialog's identical flag.
	bool _suppressLiveSelectionPush = false;
};
