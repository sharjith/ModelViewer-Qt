#pragma once

#include <QDialog>
#include <QUuid>
#include <QVector>

#include <memory>

namespace Ui
{
	class FillHolesDialog;
}

class ModelViewer;
class SceneNode;
class QCloseEvent;
class QMdiSubWindow;
class QListWidgetItem;

// ---------------------------------------------------------------------------
// FillHolesDialog
//
// Non-modal "Fill Holes" dialog (Tools -> Fill Holes...) - mirrors RepairMeshDialog's
// non-modal/per-document/findChild-reuse pattern via ModelViewer::openFillHolesDialog(), but
// with a SECOND, auto-populated list on top of the usual working-mesh-list: every boundary
// loop SceneMesh::detectHoles() finds across the current mesh list, one checkable row each.
// A CGAL boundary cycle is ambiguous - it can be a genuine small defect gap, or a mesh's own
// real, intentional open edge (a single flat panel, a shell open at one end) - forcibly
// closing every one indiscriminately would break meshes meant to stay open. So unlike
// RepairMeshDialog's single Generate-repairs-everything action, this dialog is a picker:
// the holes list is repopulated (refreshHolesList()) whenever the mesh list changes, defaults
// every row to checked (the review itself is the safety mechanism, not a hard size/edge-count
// filter), and Generate only fills whatever is still checked per mesh.
//
// Selecting a row in the holes list highlights that loop (orange) in the viewport via
// ViewportWidget::setHighlightedHole() -> FillHolesController - see that class's doc comment.
// Unlike Mark Seams, there is no viewport click-to-toggle a hole's checked state in this first
// pass - the list's own checkboxes are the sole select mechanism.
//
// Each Generate click fills every mesh with at least one checked hole independently
// (SceneMesh::fillHoles(), one call per mesh) and pushes each result onto the undo stack
// immediately (ModelViewer::commitFillHoles(), reusing ShrinkWrapCommand like every other tool
// dialog here) - same "every result independently undoable without closing the dialog first"
// convention RepairMeshDialog/ShrinkWrapDialog follow. With "Replace previous result" checked
// (the default), Generate first undoably deletes the prior batch's results
// (ModelViewer::replaceToolResults()) before running.
// ---------------------------------------------------------------------------
class FillHolesDialog : public QDialog
{
	Q_OBJECT

public:
	explicit FillHolesDialog(ModelViewer* modelViewer, QWidget* parent = nullptr);
	~FillHolesDialog();

	// Adds whatever's currently selected in the tree to the working list (same logic the Add
	// Selected button runs) - public so ModelViewer::openFillHolesDialog() can seed the list
	// immediately with an existing tree selection when the dialog is (re)opened. Mirrors
	// RepairMeshDialog::addCurrentTreeSelection() exactly.
	void addCurrentTreeSelection();

protected:
	void closeEvent(QCloseEvent* event) override;
	// QDialog's own Escape handling calls reject(), which goes straight to done()/hide()
	// WITHOUT ever raising a QCloseEvent - same gotcha ShrinkWrapDialog::reject() documents.
	// This override exists only so saveSettings()/the viewport overlay teardown still run on
	// an Escape-closed dialog.
	void reject() override;

private slots:
	void onRemoveSelectedClicked();
	void onGenerateClicked();
	void onListSelectionChanged();
	void onHolesListSelectionChanged();
	void onHolesListItemChanged(QListWidgetItem* item);
	void onSelectAllHolesClicked();
	void onDeselectAllHolesClicked();

	// Hides/shows this dialog as its own document's MDI subwindow loses/gains focus - mirrors
	// RtRenderDialog's identical mechanism (see the constructor's connect() for why).
	void onActiveSubWindowChanged(QMdiSubWindow* activeSubWindow);

private:
	// Enables Generate only while at least one hole is checked, and Select/Deselect All only
	// while the holes list isn't empty - mirrors RepairMeshDialog::updateActionButtonsEnabled()'s
	// role but with a finer-grained condition than "mesh list non-empty" since Generate here
	// would otherwise be a silent no-op with nothing checked.
	void updateActionButtonsEnabled();

	// Re-detects holes for every mesh currently in meshList and repopulates holesList - called
	// whenever the mesh list changes (add/remove). Each row's Qt::UserRole holds the owning
	// mesh's QUuid, Qt::UserRole + 1 holds its DetectedHole::loopId (see SceneMesh.h) - together
	// they identify the hole for both the checked-set fillHoles() needs and the highlight
	// ViewportWidget::setHighlightedHole() needs. Also pushes the full flat hole list to
	// ViewportWidget::setDetectedHoles() for overlay rendering.
	void refreshHolesList();

	void loadSettings();
	void saveSettings();

	ModelViewer* _modelViewer; // not owned - dialog is a child of the ModelViewer's window
	std::unique_ptr<Ui::FillHolesDialog> ui;

	// Mesh UUID(s) from the most recent Generate click, already committed to the undo stack -
	// "Replace previous result" undoably deletes these (via ModelViewer::replaceToolResults())
	// right before running the next Generate, then this is overwritten with the new batch's
	// result UUIDs. Same convention as RepairMeshDialog::_lastResultMeshUuids.
	QVector<QUuid> _lastResultMeshUuids;

	// Next sequence number for naming ("Fill Holes 001", "Fill Holes 002", ...) - seeded in the
	// constructor from the highest-numbered "Fill Holes NNN" top-level node already in the
	// scene, then incremented once per successfully-filled mesh in a Generate batch. Same
	// convention as RepairMeshDialog::_nextRepairIndex.
	int _nextFillIndex = 1;
};
