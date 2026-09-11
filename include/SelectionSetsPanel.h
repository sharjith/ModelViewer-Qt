#pragma once

#include <QWidget>
#include <QUuid>
#include <QSet>

class QListWidget;
class QListWidgetItem;
class QPushButton;
class SceneGraph;

// ---------------------------------------------------------------------------
// SelectionSetsPanel ("Selections" dock tab)
//
// Lists the current document's named selection sets (SceneGraph::
// selectionSets(), see SelectionSetData.h) and lets the user save the
// current viewport selection under a name, recall one (single-click, same
// immediate-activation convention CamerasPanel already uses for its own
// list), or delete one. A single MainWindow-owned shared instance, rebound to
// whichever document is active - same lifecycle as CamerasPanel/
// MaterialVariantsPanel/AnimationsPanel in the same dock, but deliberately
// simpler: a flat QListWidget (sets have no hierarchy, unlike cameras'
// per-file grouping) with no detached-overlay mode (this panel only ever
// lives as a plain dock tab, never a floating viewport overlay).
//
// Two-way sync with the live viewport selection: whenever the document's
// selection changes (see syncActiveSet(), called from MainWindow's
// per-viewport ViewportWidget::selectionChanged connection), the row whose
// saved meshUuids exactly equal the CURRENT selection is highlighted as
// "active" - no row highlighted if the live selection doesn't match any
// saved set. Clicking that already-active row again deselects (emits
// selectionSetDeselectRequested() instead of a recall) - clicking any other
// row recalls it as usual.
// ---------------------------------------------------------------------------
class SelectionSetsPanel : public QWidget
{
	Q_OBJECT
public:
	explicit SelectionSetsPanel(QWidget* parent = nullptr);

	void setSceneGraph(SceneGraph* sg);

	// Rebuild the list from the current SceneGraph selection-set data,
	// re-applying whatever active-set highlight the last syncActiveSet()
	// call established (a save/delete elsewhere rebuilds this list's rows
	// out from under any existing highlight).
	void refresh();

	// Called whenever the document's live selection changes - highlights
	// the row (if any) whose saved meshUuids exactly equal
	// currentSelectionUuids, clearing the highlight if none match.
	void syncActiveSet(const QSet<QUuid>& currentSelectionUuids);

signals:
	// Emitted on single-click of a row that ISN'T already the active one -
	// immediate activation, same as CamerasPanel.
	void selectionSetRecallRequested(const QUuid& setId);
	// Emitted on single-click of the row that IS already active (toggling
	// it off) - clears the viewport selection instead of recalling.
	void selectionSetDeselectRequested();
	// Emitted from the bottom "Save Current Selection..." button, after
	// prompting for a name.
	void selectionSetSaveRequested(const QString& name);
	// Emitted from the bottom Delete button.
	void selectionSetDeleteRequested(const QUuid& setId);

private slots:
	void onItemClicked(QListWidgetItem* item);
	void onSaveButtonClicked();
	void onDeleteButtonClicked();
	void onSelectionChanged();

private:
	// Re-derives _activeSetId from _lastKnownSelectionUuids against the
	// CURRENT SceneGraph selection sets, then reflects it into _list's own
	// row highlight (programmatic - never emits selectionSetRecallRequested/
	// selectionSetDeselectRequested, only real clicks do that).
	void updateActiveHighlight();

	QListWidget* _list = nullptr;
	QPushButton* _saveButton = nullptr;
	QPushButton* _deleteButton = nullptr;
	SceneGraph* _sceneGraph = nullptr;

	// The set (if any) whose meshUuids currently exactly match the live
	// viewport selection - null when nothing matches. Compared against a
	// clicked row's id in onItemClicked() to decide recall vs. deselect.
	QUuid _activeSetId;
	// Cached from the last syncActiveSet() call so refresh() (triggered by
	// an unrelated save/delete elsewhere) can re-derive the highlight
	// without needing its own fresh selection query.
	QSet<QUuid> _lastKnownSelectionUuids;
};
