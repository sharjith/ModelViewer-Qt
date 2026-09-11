#pragma once

#include <QDialog>

#include <vector>

class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QPushButton;
class ModelViewer;
class QMdiSubWindow;
class QShowEvent;

// "Selection -> Filter by Material..." - lets the user pick one or more
// material identities present in the current scene (extended/multi-
// selection list - Ctrl/Shift-click, same convention as any other Qt list)
// and live-previews the viewport selection as the UNION of every mesh
// sharing any of them, so Show Only/Hide can act on the combined result
// immediately - e.g. isolating several related paint/trim variants of the
// same part in one pass instead of repeating single-material cycles. One
// row per distinct current-material identity from
// groupIndicesByCurrentMaterial() (see MaterialGrouping.h) - hundreds of
// patches sharing one material collapse into a single row.
//
// Non-modal, per-document singleton (see ModelViewer::filterSelectionByMaterial()'s
// findChild-reuse-or-create pattern, mirroring ShrinkWrapDialog/MeasurementDialog),
// hiding/showing itself in lockstep with its own document's MDI subwindow
// activation so a background document's dialog never stays visible or
// live-previews against the wrong scene. Every selection change immediately
// pushes the matching mesh set as the real (undoable) viewport selection via
// ModelViewer::setSelectionWithUndo() - consecutive pushes from this same
// dialog instance collapse into one undo step (see SelectionCommand's
// mergeSource mechanism), so tweaking through several rows before deciding
// doesn't flood the undo stack. There's no OK/Cancel - Show Only and Hide
// (calling the existing ModelViewer::showOnlySelectedItems()/
// hideSelectedItems(), unchanged) act on whatever the live preview currently
// has selected, and closing the dialog (titlebar X or Escape) just stops
// previewing - there's nothing left to commit at that point.
class FilterByMaterialDialog : public QDialog
{
	Q_OBJECT
public:
	explicit FilterByMaterialDialog(ModelViewer* modelViewer, QWidget* parent = nullptr);

protected:
	void showEvent(QShowEvent* event) override;

private slots:
	void onRowChanged();
	void onFilterTextChanged(const QString& text);
	void onItemDoubleClicked(QListWidgetItem* item);
	void onShowOnlyClicked();
	void onHideClicked();

	// Hides/shows this dialog as its own document's MDI subwindow loses/gains focus - mirrors
	// ShrinkWrapDialog's identical mechanism.
	void onActiveSubWindowChanged(QMdiSubWindow* activeSubWindow);

private:
	// Rebuilds _groups and the list rows from the CURRENT mesh store - called
	// on construction and every time the dialog is (re)shown, since the
	// scene's materials can change while it was hidden behind another
	// document tab.
	void rebuildGroups();

	// Pushes the UNION of every selected row's mesh group as the live
	// viewport selection (tagged with this dialog's mergeSource so
	// consecutive pushes coalesce into one undo step) - no-ops if it already
	// matches the live selection.
	void applyLiveSelection();

	// Union of the mesh groups for every currently-selected row (dedup'd -
	// see the caller).
	std::vector<int> selectedGroups() const;

	ModelViewer* _modelViewer; // not owned - dialog is a child of the ModelViewer's window
	std::vector<std::vector<int>> _groups; // same order as _list's rows

	QLineEdit* _searchBox = nullptr;
	QListWidget* _list = nullptr;
	QPushButton* _showOnlyButton = nullptr;
	QPushButton* _hideButton = nullptr;
};
