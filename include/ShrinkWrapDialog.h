#pragma once

#include <QDialog>
#include <QUuid>
#include <QVector>

#include <memory>

namespace Ui
{
	class ShrinkWrapDialog;
}

class ModelViewer;
class SceneNode;
class QCloseEvent;
class QMdiSubWindow;

// ---------------------------------------------------------------------------
// ShrinkWrapDialog
//
// Non-modal "Shrink Wrap" dialog (Tools -> Shrink Wrap...) - mirrors
// MeasurementDialog/AnnotationDialog's non-modal, per-document,
// findChild-reuse-or-create pattern (see ModelViewer::openShrinkWrapDialog()),
// but owns its own working-mesh-list state directly rather than delegating
// to a Controller class - there's no interactive viewport-picking step here,
// just a user-curated list plus two numeric fields and a button. Widgets are
// defined in ui/ShrinkWrapDialog.ui (like RtRenderDialog/SettingsDialog),
// not built by hand in the constructor - so its size and layout can be
// tuned visually in Designer instead of by re-guessing pixel values in code.
//
// Each Generate click builds a new wrapped mesh under a new top-level
// SceneNode and pushes it onto the undo stack IMMEDIATELY (via
// ModelViewer::commitShrinkWrap()) - matching MeasurementDialog, where every
// placed measurement is independently undoable without closing the dialog
// first. This used to defer every result to a "live preview" only committed
// at close time; that made Undo a no-op for anything generated while the
// dialog stayed open (confirmed bug). With "Replace previous result" checked
// (the default), Generate first undoably DELETES the prior result (via
// ModelViewer::replaceToolResults(), a DeleteMeshCommand push) so there's
// still at most one live result at a time, just as two separate undo-stack
// entries instead of one invisible scratch overwrite; unchecked, previous
// results are left alone and each Generate just adds another.
// _lastResultMeshUuids only remembers the most recent result's mesh UUID so
// the next Replace-checked Generate knows what to delete - it holds no
// scene-graph pointers, unlike the old scratch-preview design.
// ---------------------------------------------------------------------------
class ShrinkWrapDialog : public QDialog
{
	Q_OBJECT

public:
	explicit ShrinkWrapDialog(ModelViewer* modelViewer, QWidget* parent = nullptr);
	~ShrinkWrapDialog();

	// Adds whatever's currently selected in the tree to the working list
	// (same logic the Add Selected button runs) - public so
	// ModelViewer::openShrinkWrapDialog() can seed the list immediately with
	// an existing tree selection when the dialog is (re)opened.
	void addCurrentTreeSelection();

protected:
	void closeEvent(QCloseEvent* event) override;
	// QDialog's own Escape handling calls reject(), which goes straight to
	// done()/hide() WITHOUT ever raising a QCloseEvent. Nothing here is left
	// uncommitted any more (every result is pushed immediately at Generate
	// time), so this override now only exists to make sure saveSettings()
	// still runs on an Escape-closed dialog, same as any other close path.
	void reject() override;

private slots:
	void onRemoveSelectedClicked();
	void onResetToleranceClicked();
	void onGenerateClicked();
	void onListSelectionChanged();

	// Hides/shows this dialog as its own document's MDI subwindow loses/gains focus - mirrors
	// RtRenderDialog's identical mechanism (see the constructor's connect() for why).
	void onActiveSubWindowChanged(QMdiSubWindow* activeSubWindow);

private:
	// Recomputes and overwrites both tolerance fields from the current
	// working list via SceneMesh::suggestShrinkWrapTolerance().
	void refreshSuggestedTolerance();

	// Enables Generate/Reset to Suggested only while the working list holds
	// at least one item - disabling upfront instead of letting the user
	// click either and get a status-label warning back. Called after every
	// change to the mesh list's contents (add, remove).
	void updateActionButtonsEnabled();

	void loadSettings();
	void saveSettings();

	ModelViewer* _modelViewer; // not owned - dialog is a child of the ModelViewer's window
	std::unique_ptr<Ui::ShrinkWrapDialog> ui;

	// Mesh UUID(s) from the most recent Generate click, already committed to
	// the undo stack - "Replace previous result" undoably deletes these (via
	// ModelViewer::replaceToolResults()) right before running the next
	// Generate, then this is overwritten with the new result's UUID. Always
	// 0 or 1 entries in practice (one combined result per click), kept as a
	// QVector to match SubdivisionDialog's identical mechanism.
	QVector<QUuid> _lastResultMeshUuids;

	// Next sequence number for naming ("Shrink Wrap 001", "Shrink Wrap 002",
	// ...) - seeded in the constructor from the highest-numbered
	// "Shrink Wrap NNN" top-level node already in the scene (so reopening
	// the dialog, or a second document, doesn't reuse a number already
	// committed earlier), then incremented after each successful Generate.
	int _nextWrapIndex = 1;
};
