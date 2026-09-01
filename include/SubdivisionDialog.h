#pragma once

#include <QDialog>
#include <QUuid>
#include <QVector>

#include <memory>

namespace Ui
{
	class SubdivisionDialog;
}

class ModelViewer;
class SceneNode;
class QCloseEvent;

// ---------------------------------------------------------------------------
// SubdivisionDialog
//
// Non-modal "Subdivide Surface" dialog (Tools -> Subdivide Surface...) -
// mirrors ShrinkWrapDialog's structure exactly (see that class's own doc
// comment for the full rationale), with one semantic difference: Shrink Wrap
// combines N selected meshes into ONE wrapped result (a real N-to-1 combine),
// while subdivision is topology-preserving per-mesh refinement, so each
// Generate click produces one independent subdivided result PER mesh in the
// list, not one merged blob.
//
// Every Generate result is pushed onto the undo stack IMMEDIATELY (via
// ModelViewer::commitSubdivision(), which reuses ShrinkWrapCommand - see that
// method's doc comment for why no new command class was needed) - matching
// MeasurementDialog, where every placed measurement is independently
// undoable without closing the dialog first. This dialog used to defer every
// result to a "live preview" only committed at close time; that made Undo a
// no-op for anything generated while the dialog stayed open (confirmed bug).
// _lastResultMeshUuids only remembers the most recent batch's mesh UUIDs, so
// "Replace previous result" knows what to undoably delete before the next
// Generate - it holds no scene-graph pointers, since deletion goes through
// ModelViewer::replaceToolResults() (a DeleteMeshCommand push) rather than
// direct SceneGraph manipulation.
// ---------------------------------------------------------------------------
class SubdivisionDialog : public QDialog
{
	Q_OBJECT

public:
	explicit SubdivisionDialog(ModelViewer* modelViewer, QWidget* parent = nullptr);
	~SubdivisionDialog();

	// Adds whatever's currently selected in the tree to the working list
	// (same logic the Add Selected button runs) - public so
	// ModelViewer::openSubdivisionDialog() can seed the list immediately
	// with an existing tree selection when the dialog is (re)opened.
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
	void onGenerateClicked();
	void onListSelectionChanged();

private:
	// Enables Generate only while the working list holds at least one item.
	// Called after every change to the mesh list's contents (add, remove).
	void updateActionButtonsEnabled();

	void loadSettings();
	void saveSettings();

	ModelViewer* _modelViewer; // not owned - dialog is a child of the ModelViewer's window
	std::unique_ptr<Ui::SubdivisionDialog> ui;

	// Mesh UUIDs from the most recent Generate click, already committed to
	// the undo stack - "Replace previous result" undoably deletes these
	// (via ModelViewer::replaceToolResults()) right before running the next
	// Generate, then this is overwritten with the new batch's UUIDs.
	QVector<QUuid> _lastResultMeshUuids;
};
