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
// The dialog maintains zero or more "live previews": each Generate click
// builds a new wrapped mesh under a new top-level SceneNode. With "Replace
// previous result" checked (the default), Generate tears down every prior
// preview first, so there's always at most one; unchecked, previous previews
// are left alone and each Generate just adds another. None of this touches
// the undo stack - previews are scratch state until the dialog closes, at
// which point every still-live preview becomes its own real, undoable
// ShrinkWrapCommand (pushed via ModelViewer::commitShrinkWrap()). Closing
// without ever generating anything is a no-op.
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
	// done()/hide() WITHOUT ever raising a QCloseEvent - closeEvent()'s
	// commit-live-previews logic never ran for an Escape-closed dialog
	// otherwise (same gap found and fixed in MeasurementDialog::reject()),
	// leaving any live preview mesh orphaned in the scene: never committed
	// to the undo stack, never discarded. Runs the same commit before
	// deferring to the base implementation.
	void reject() override;

private slots:
	void onRemoveSelectedClicked();
	void onResetToleranceClicked();
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

	// Recomputes and overwrites both tolerance fields from the current
	// working list via SceneMesh::suggestShrinkWrapTolerance().
	void refreshSuggestedTolerance();

	// Enables Generate/Reset to Suggested only while the working list holds
	// at least one item - disabling upfront instead of letting the user
	// click either and get a status-label warning back. Called after every
	// change to the mesh list's contents (add, remove).
	void updateActionButtonsEnabled();

	// Tears down every live preview WITHOUT going through
	// ShrinkWrapCommand/the undo stack - none of them were ever pushed
	// there. Called before a "replace" Generate. Deliberately NOT called
	// from the destructor: if this dialog is ever destroyed without
	// closeEvent() running (e.g. the parent ModelViewer/document is being
	// torn down), _modelViewer's own members may already be gone by the
	// time a QObject child's destructor runs - any still-live previews just
	// get freed along with the rest of that document's scene graph instead.
	void discardAllPreviews();

	// Commits every live preview into a real, undoable ShrinkWrapCommand -
	// the shared body of closeEvent() and reject() (see reject()'s doc
	// comment for why both need to run it independently).
	void commitLivePreviews();

	void loadSettings();
	void saveSettings();

	ModelViewer* _modelViewer; // not owned - dialog is a child of the ModelViewer's window
	std::unique_ptr<Ui::ShrinkWrapDialog> ui;

	QVector<PreviewEntry> _previews;

	// Next sequence number for naming ("Shrink Wrap 001", "Shrink Wrap 002",
	// ...) - seeded in the constructor from the highest-numbered
	// "Shrink Wrap NNN" top-level node already in the scene (so reopening
	// the dialog, or a second document, doesn't reuse a number already
	// committed earlier), then incremented after each successful Generate.
	int _nextWrapIndex = 1;
};
