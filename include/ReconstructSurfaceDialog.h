#pragma once

#include <QDialog>
#include <QUuid>
#include <QVector>

#include <memory>

namespace Ui
{
	class ReconstructSurfaceDialog;
}

class ModelViewer;
class SceneNode;
class QCloseEvent;
class QMdiSubWindow;

// ---------------------------------------------------------------------------
// ReconstructSurfaceDialog
//
// Non-modal "Reconstruct Surface" dialog (Tools -> Reconstruct Surface...) -
// mirrors ShrinkWrapDialog/SubdivisionDialog's structure and undo lifecycle
// exactly (see ShrinkWrapDialog's own doc comment for the full rationale).
// Combines the world-space POSITIONS of one or more selected meshes (their
// own faces, if any, are ignored - the natural input is a point-cloud mesh
// with none) into one new triangulated surface via CGAL's
// advancing_front_surface_reconstruction (see
// SceneMesh::reconstructSurfaceFromPoints()'s doc comment for why that
// algorithm was chosen over Poisson reconstruction). Same N-to-1 combine
// semantics as Shrink Wrap (not Subdivide's per-mesh 1-to-1 loop) - multiple
// selected point-cloud chunks (e.g. several scans of one object) reconstruct
// into a single result.
//
// Each Generate click pushes its result onto the undo stack IMMEDIATELY (via
// ModelViewer::commitReconstructSurface()) - matching MeasurementDialog/
// ShrinkWrapDialog/SubdivisionDialog: every result is independently undoable
// without closing the dialog first. With "Replace previous result" checked
// (the default), Generate first undoably DELETES the prior result (via
// ModelViewer::replaceToolResults(), already fully generic - no changes
// needed there). _lastResultMeshUuids only remembers the most recent
// result's mesh UUID so the next Replace-checked Generate knows what to
// delete - it holds no scene-graph pointers.
// ---------------------------------------------------------------------------
class ReconstructSurfaceDialog : public QDialog
{
	Q_OBJECT

public:
	explicit ReconstructSurfaceDialog(ModelViewer* modelViewer, QWidget* parent = nullptr);
	~ReconstructSurfaceDialog();

	// Adds whatever's currently selected in the tree to the working list
	// (same logic the Add Selected button runs) - public so
	// ModelViewer::openReconstructSurfaceDialog() can seed the list
	// immediately with an existing tree selection when the dialog is
	// (re)opened.
	void addCurrentTreeSelection();

protected:
	void closeEvent(QCloseEvent* event) override;
	// QDialog's own Escape handling calls reject(), which goes straight to
	// done()/hide() WITHOUT ever raising a QCloseEvent. Nothing here is left
	// uncommitted (every result is pushed immediately at Generate time), so
	// this override only exists to make sure saveSettings() still runs on an
	// Escape-closed dialog, same as any other close path.
	void reject() override;

private slots:
	void onRemoveSelectedClicked();
	void onResetToleranceClicked();
	void onSimplifyToggled(bool checked);
	void onGenerateClicked();
	void onListSelectionChanged();

	// Hides/shows this dialog as its own document's MDI subwindow loses/gains focus - mirrors
	// RtRenderDialog's identical mechanism (see the constructor's connect() for why).
	void onActiveSubWindowChanged(QMdiSubWindow* activeSubWindow);

private:
	// Recomputes and overwrites the target-spacing field from the current
	// working list via SceneMesh::suggestReconstructionSpacing().
	void refreshSuggestedSpacing();

	// Enables Generate/Reset to Suggested only while the working list holds
	// at least one item - disabling upfront instead of letting the user
	// click either and get a status-label warning back. Called after every
	// change to the mesh list's contents (add, remove).
	void updateActionButtonsEnabled();

	void loadSettings();
	void saveSettings();

	ModelViewer* _modelViewer; // not owned - dialog is a child of the ModelViewer's window
	std::unique_ptr<Ui::ReconstructSurfaceDialog> ui;

	// Mesh UUID(s) from the most recent Generate click, already committed to
	// the undo stack - "Replace previous result" undoably deletes these (via
	// ModelViewer::replaceToolResults()) right before running the next
	// Generate, then this is overwritten with the new result's UUID. Always
	// 0 or 1 entries in practice (one combined result per click), kept as a
	// QVector to match ShrinkWrapDialog/SubdivisionDialog's identical
	// mechanism.
	QVector<QUuid> _lastResultMeshUuids;

	// Next sequence number for naming ("Reconstruct Surface 001",
	// "Reconstruct Surface 002", ...) - seeded in the constructor from the
	// highest-numbered "Reconstruct Surface NNN" top-level node already in
	// the scene (so reopening the dialog, or a second document, doesn't
	// reuse a number already committed earlier), then incremented after
	// each successful Generate.
	int _nextReconstructIndex = 1;
};
