#pragma once

#include <QDialog>
#include <QRect>

#include <vector>

class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QPushButton;
class QCheckBox;
class QGroupBox;
class QLabel;
class QTimer;
class ModelViewer;
class QMdiSubWindow;
class QShowEvent;
class QCloseEvent;
class QHideEvent;

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
	// Refreshes the live selection when this dialog's window regains OS-level
	// activation (e.g. the user clicks back onto it after using an unrelated
	// command like Show All elsewhere) - confirmed real gap: Hide clears the
	// viewport selection, and a subsequent Show All doesn't touch selection
	// either, so without this the dialog's still-valid filter criteria never
	// re-asserted themselves until the user touched the dialog's own
	// controls again. Scoped to this dialog only - no changes to the shared
	// visibility-command code path.
	void changeEvent(QEvent* event) override;
	// Saves window geometry - see saveSettings()'s doc comment.
	void closeEvent(QCloseEvent* event) override;
	// QDialog's own Escape handling calls reject(), which goes straight to
	// done()/hide() WITHOUT ever raising a QCloseEvent (same gotcha
	// ShrinkWrapDialog's identical override documents) - this override
	// exists purely to make sure saveSettings() still runs on an
	// Escape-closed dialog, same as any other close path.
	void reject() override;
	// Hides the hover-preview popup (a separate top-level window, so hiding
	// this dialog doesn't automatically hide it too) on every path that
	// hides the dialog - subwindow deactivation, close, minimize, etc.
	void hideEvent(QHideEvent* event) override;

	// Tracks mouse movement over _list's viewport for the hover-preview
	// popup - see the .cpp's doc comment on why this needs raw mouse-move
	// tracking instead of just QListWidget::itemEntered().
	bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
	void onRowChanged();
	void onFilterTextChanged(const QString& text);
	void onItemDoubleClicked(QListWidgetItem* item);
	void onShowOnlyClicked();
	void onHideClicked();
	// Fires once _hoverTimer's long-hover delay elapses over an icon -
	// see eventFilter()'s doc comment.
	void showHoverPreview();
	// Right-click context menu ("Edit Material...") - see the .cpp for why
	// this is the only entry so far.
	void onListContextMenuRequested(const QPoint& pos);

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

	// Updates _materialsGroup's title with the material count - "Materials
	// in Scene (N)", or "Materials in Scene (M of N)" once the search box
	// has hidden some rows. Called after rebuildGroups() and after every
	// search-text change.
	void updateGroupBoxTitle();

	// Approximate icon rect for a row, in _list's viewport coordinates - the
	// hover preview only arms over this sub-region, not the row's full width
	// (which includes the text label). Qt doesn't expose the icon's exact
	// drawn rect publicly, so this reconstructs it as an iconSize()-square
	// box left-aligned within the row - close enough for an "is the cursor
	// roughly over the swatch" check, not meant to be pixel-exact.
	QRect iconRectForItem(QListWidgetItem* item) const;

	// Window geometry persistence, via QSettings - same shape as
	// ShrinkWrapDialog::loadSettings()/saveSettings(). loadSettings() is
	// called once from the constructor; saveSettings() from every close
	// path (closeEvent() and reject(), see their doc comments above).
	void loadSettings();
	void saveSettings();

	ModelViewer* _modelViewer; // not owned - dialog is a child of the ModelViewer's window
	std::vector<std::vector<int>> _groups; // same order as _list's rows

	QGroupBox* _materialsGroup = nullptr;
	QLineEdit* _searchBox = nullptr;
	QCheckBox* _sortByCountCheck = nullptr;
	QListWidget* _list = nullptr;
	QPushButton* _showOnlyButton = nullptr;
	QPushButton* _hideButton = nullptr;

	// Lazily created floating popup for showHoverPreview()'s bigger swatch
	// preview - parented to `this` so it's destroyed along with the dialog
	// despite being a separate top-level (Qt::ToolTip) window.
	QLabel* _hoverPreview = nullptr;
	// Single-shot; (re)started whenever the cursor enters a new row's icon
	// rect, stopped whenever it leaves one - showHoverPreview() only actually
	// shows the popup if it fires, i.e. the cursor dwelled there.
	QTimer* _hoverTimer = nullptr;
	// The item the cursor is currently over the icon of (armed for
	// _hoverTimer, or already showing) - nullptr whenever the cursor isn't
	// over any row's icon. Not owned; just an identity check against
	// eventFilter()'s itemAt() result.
	QListWidgetItem* _hoverArmedItem = nullptr;
};
