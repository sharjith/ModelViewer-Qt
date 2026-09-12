#pragma once

#include <QDialog>
#include <QVector3D>
#include <QVector>

class QLabel;
class QPushButton;
class QToolButton;
class QSlider;
class QListWidget;
class QListWidgetItem;
class ModelViewer;
class QMdiSubWindow;
class QShowEvent;
class QCloseEvent;

// "Selection -> Filter by Color..." - lets the user build a small list of
// target colors (an "Add" button opens QColorDialog and appends a row; each
// row has its own inline remove button) and live-previews the viewport
// selection as every mesh in the scene whose representative color (see
// MeshColorUtils.h - material albedo, or averaged per-vertex color for
// Point Set Reconstruction meshes) falls within one shared tolerance of ANY
// listed color, so Show Only/Hide can act on the combined result
// immediately.
//
// Unlike FilterByMaterialDialog's list, this one is NOT selectable/
// clickable - every color in the list is always part of the active filter,
// there's no separate "which rows are currently chosen" state. "Armed" is
// simply "the list is non-empty": opening with a prior viewport selection
// seeds the list with that selection's distinct representative colors (real
// user intent, so it's immediately live); opening with no prior selection
// starts with an empty list, nothing live, Show Only/Hide disabled until
// the user adds at least one color. This deliberately has no default target
// color at all (the old single-swatch design defaulted to white, which
// looked like a confirmed choice and silently matched/missed meshes before
// the user had chosen anything).
//
// Two more ways to get a color into the list, both added after the initial
// manual-picking-only design proved fragile in practice (a color guessed or
// screen-sampled via QColorDialog rarely lands close enough to a mesh's real
// stored color - see meshRepresentativeColor() in MeshColorUtils.h, which
// reflects raw unlit material/vertex data, not the lit/shaded on-screen
// pixel QColorDialog's own eyedropper samples):
// - A "pick from mesh" toggle button arms ViewportWidget::setColorPickArmed(),
//   which reads a clicked mesh's exact meshRepresentativeColor() - always a
//   distance-0 match, unlike eyeballing a color.
// - An "Auto-Detect" button scans the whole scene and adds every distinct
//   color found (deduped - see dedupedColors() in MeshColorUtils.h), so the
//   user can start from everything and prune the unwanted rows via each
//   row's own remove button, instead of building the list up one guess at a
//   time.
//
// Non-modal, per-document singleton, MDI-activation-synced, live-preview
// undo-merging - same shape as FilterByMaterialDialog; see that class's
// header doc comment for the full rationale.
class FilterByColorDialog : public QDialog
{
	Q_OBJECT
public:
	// initialColors: distinct representative colors sampled from the
	// viewport selection at open time (empty if nothing was selected) - see
	// ModelViewer::filterSelectionByColor()'s doc comment for how these are
	// derived/deduped.
	explicit FilterByColorDialog(ModelViewer* modelViewer,
	                              const QVector<QVector3D>& initialColors,
	                              QWidget* parent = nullptr);

protected:
	void showEvent(QShowEvent* event) override;
	// Refreshes matches/live selection when this dialog's window regains
	// OS-level activation (e.g. the user clicks back onto it after using an
	// unrelated command like Show All elsewhere) - see
	// FilterByMaterialDialog.h's identical override for the confirmed gap
	// this closes.
	void changeEvent(QEvent* event) override;
	// Disarms the viewport's color-pick tool and saves window geometry (see
	// saveSettings()'s doc comment) on close - there'd be no dialog left to
	// receive colorPicked() otherwise, and a silently-armed eyedropper
	// cursor with nothing visibly requesting it would be a confusing
	// dangling state.
	void closeEvent(QCloseEvent* event) override;
	// QDialog's own Escape handling calls reject(), which goes straight to
	// done()/hide() WITHOUT ever raising a QCloseEvent (same gotcha
	// ShrinkWrapDialog's identical override documents) - this override
	// exists purely to make sure the color-pick disarm and saveSettings()
	// still run on an Escape-closed dialog, same as any other close path.
	void reject() override;

private slots:
	void onAddColorClicked();
	void onAutoDetectClicked();
	void onPickFromMeshToggled(bool checked);
	void onColorPicked(const QVector3D& color);
	// External sync only (see ViewportWidget::colorPickArmedChanged()'s doc
	// comment) - this dialog's own onPickFromMeshToggled() already updates
	// _pickFromMeshButton's checked state for its OWN toggle, so this only
	// matters when something ELSE (another armed tool's mutual-exclusion
	// clearing) disarms color-pick out from under this dialog.
	void onColorPickArmedChanged(bool armed);
	void onToleranceChanged(int sliderValue);
	void onShowOnlyClicked();
	void onHideClicked();
	void onActiveSubWindowChanged(QMdiSubWindow* activeSubWindow);

private:
	// Removes _colors[index] and rebuilds the list - called from each row's
	// own inline remove button (there's no row-selection to remove "the
	// selected row" from, by design - see this class's doc comment).
	void removeColorAt(int index);

	// Rebuilds the list's row widgets from _colors (full teardown/rebuild -
	// the list is always small, a handful of colors in practice), each
	// labeled with its OWN independent match count (not the union - lets
	// the user see at a glance which listed colors are actually matching
	// anything, e.g. a color that's a slightly-off guess at a shaded part's
	// true representative color will show "(0 meshes)" instead of silently
	// contributing nothing to the aggregate below), then recomputes the
	// aggregate/live selection. Called on construction and after every
	// add/remove/tolerance change (the per-row counts depend on tolerance
	// too, so a tolerance change needs the same full rebuild, not just
	// updateMatches()).
	void rebuildColorList();

	// Recomputes matches against a FRESH mesh store (the scene may have
	// changed while this dialog was open/hidden), updates the match count
	// label and button enablement, and - only while _colors is non-empty -
	// pushes the result as the live viewport selection (undo-merged, see
	// FilterByMaterialDialog.cpp's applyLiveSelection() for the identical
	// mechanism). An empty _colors list is "not armed," matching
	// FilterByMaterialDialog's "nothing selected until the user picks a
	// row" behavior.
	void updateMatches();

	// Window geometry persistence, via QSettings - same shape as
	// ShrinkWrapDialog::loadSettings()/saveSettings(). loadSettings() is
	// called once from the constructor; saveSettings() from every close
	// path (closeEvent() and reject(), see their doc comments above).
	void loadSettings();
	void saveSettings();

	ModelViewer* _modelViewer; // not owned - dialog is a child of the ModelViewer's window
	QVector<QVector3D> _colors;
	float _tolerance = 0.05f; // Euclidean distance in linear RGB, shared across every listed color

	QListWidget* _list = nullptr;
	QPushButton* _addButton = nullptr;
	QToolButton* _pickFromMeshButton = nullptr;
	QPushButton* _autoDetectButton = nullptr;
	QSlider* _toleranceSlider = nullptr;
	QLabel* _toleranceValueLabel = nullptr; // live numeric readout beside the slider
	QLabel* _matchCountLabel = nullptr;
	QPushButton* _showOnlyButton = nullptr;
	QPushButton* _hideButton = nullptr;
};
