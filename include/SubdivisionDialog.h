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
// mirrors ShrinkWrapDialog's structure and preview/undo lifecycle exactly
// (see that class's own doc comment for the full rationale), with one
// semantic difference: Shrink Wrap combines N selected meshes into ONE
// wrapped result (a real N-to-1 combine), while subdivision is
// topology-preserving per-mesh refinement, so each Generate click produces
// one independent subdivided result PER mesh in the list, not one merged
// blob. PreviewEntry/_previews/discardAllPreviews()/closeEvent() are
// unchanged in spirit from ShrinkWrapDialog - they already operate over a
// list of preview entries regardless of how many a single Generate produced.
//
// As with ShrinkWrapDialog, previews are scratch state (built directly via
// SceneGraph/ViewportWidget calls, bypassing the undo stack) until the
// dialog closes, at which point every still-live preview becomes its own
// real, undoable command (pushed via ModelViewer::commitSubdivision(), which
// reuses ShrinkWrapCommand - see that method's doc comment for why no new
// command class was needed).
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

private slots:
	void onRemoveSelectedClicked();
	void onGenerateClicked();
	void onListSelectionChanged();

private:
	// One not-yet-committed Generate result.
	struct PreviewEntry
	{
		SceneNode* node     = nullptr;
		SceneNode* parent   = nullptr;
		int        position = 0;
		QUuid      meshUuid;
	};

	// Enables Generate only while the working list holds at least one item.
	// Called after every change to the mesh list's contents (add, remove).
	void updateActionButtonsEnabled();

	// Tears down every live preview WITHOUT going through the undo stack -
	// none of them were ever pushed there. Called before a "replace"
	// Generate. Deliberately NOT called from the destructor - see
	// ShrinkWrapDialog::discardAllPreviews()'s doc comment for why.
	void discardAllPreviews();

	void loadSettings();
	void saveSettings();

	ModelViewer* _modelViewer; // not owned - dialog is a child of the ModelViewer's window
	std::unique_ptr<Ui::SubdivisionDialog> ui;

	QVector<PreviewEntry> _previews;
};
