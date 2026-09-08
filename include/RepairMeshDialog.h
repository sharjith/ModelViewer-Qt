#pragma once

#include <QDialog>
#include <QUuid>
#include <QVector>

#include <memory>

namespace Ui
{
	class RepairMeshDialog;
}

class ModelViewer;
class SceneNode;
class QCloseEvent;
class QMdiSubWindow;

// ---------------------------------------------------------------------------
// RepairMeshDialog
//
// Non-modal "Repair Mesh" dialog (Tools -> Repair Mesh...) - mirrors
// ShrinkWrapDialog exactly (same non-modal, per-document, findChild-reuse-or-create
// pattern via ModelViewer::openRepairMeshDialog(), same user-curated working-mesh-list
// state, same MDI-subwindow-visibility mechanism), minus the alpha/offset tolerance
// fields - repair (see MeshRepair.h) has no tunable parameter in its current,
// defect-cleanup-only scope (never fills holes, never forces a mesh closed - an open
// panel stays open).
//
// Each Generate click repairs every mesh in the working list independently
// (SceneMesh::repairMesh(), one call per mesh) and pushes each successfully-repaired
// result onto the undo stack IMMEDIATELY (via ModelViewer::commitRepairMesh(), reusing
// the exact same ShrinkWrapCommand class ShrinkWrapDialog/SubdivisionDialog/
// ReconstructSurfaceDialog already do, just with its own `text`) - same "every result
// independently undoable without closing the dialog first" convention as those
// siblings. A mesh that repairMesh() reports as already valid (MeshRepairReport::
// wasAlreadyValid) is skipped entirely - no result node is created for it, avoiding a
// redundant duplicate. With "Replace previous result" checked (the default), Generate
// first undoably deletes the prior batch's results (ModelViewer::replaceToolResults(),
// already fully generic - not Shrink-Wrap-specific despite the name) before running.
// ---------------------------------------------------------------------------
class RepairMeshDialog : public QDialog
{
	Q_OBJECT

public:
	explicit RepairMeshDialog(ModelViewer* modelViewer, QWidget* parent = nullptr);
	~RepairMeshDialog();

	// Adds whatever's currently selected in the tree to the working list (same logic the Add
	// Selected button runs) - public so ModelViewer::openRepairMeshDialog() can seed the list
	// immediately with an existing tree selection when the dialog is (re)opened. Mirrors
	// ShrinkWrapDialog::addCurrentTreeSelection() exactly.
	void addCurrentTreeSelection();

protected:
	void closeEvent(QCloseEvent* event) override;
	// QDialog's own Escape handling calls reject(), which goes straight to done()/hide()
	// WITHOUT ever raising a QCloseEvent - same gotcha ShrinkWrapDialog::reject() documents.
	// This override exists only so saveSettings() still runs on an Escape-closed dialog.
	void reject() override;

private slots:
	void onRemoveSelectedClicked();
	void onGenerateClicked();
	void onListSelectionChanged();

	// Hides/shows this dialog as its own document's MDI subwindow loses/gains focus - mirrors
	// RtRenderDialog's identical mechanism (see the constructor's connect() for why).
	void onActiveSubWindowChanged(QMdiSubWindow* activeSubWindow);

private:
	// Enables Generate only while the working list holds at least one item - mirrors
	// ShrinkWrapDialog::updateActionButtonsEnabled(). Called after every change to the mesh
	// list's contents (add, remove).
	void updateActionButtonsEnabled();

	void loadSettings();
	void saveSettings();

	ModelViewer* _modelViewer; // not owned - dialog is a child of the ModelViewer's window
	std::unique_ptr<Ui::RepairMeshDialog> ui;

	// Mesh UUID(s) from the most recent Generate click, already committed to the undo stack -
	// "Replace previous result" undoably deletes these (via ModelViewer::replaceToolResults())
	// right before running the next Generate, then this is overwritten with the new batch's
	// result UUIDs. Unlike ShrinkWrapDialog (always 0 or 1 - one combined result per click),
	// this can hold one entry per source mesh that actually needed repair.
	QVector<QUuid> _lastResultMeshUuids;

	// Next sequence number for naming ("Repair Mesh 001", "Repair Mesh 002", ...) - seeded in
	// the constructor from the highest-numbered "Repair Mesh NNN" top-level node already in the
	// scene, then incremented once per successfully-repaired mesh in a Generate batch. Same
	// convention as ShrinkWrapDialog::_nextWrapIndex.
	int _nextRepairIndex = 1;
};
