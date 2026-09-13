#pragma once

#include <QWidget>
#include <QUuid>

class QListWidget;
class QListWidgetItem;
class QPushButton;
class SceneGraph;

// ---------------------------------------------------------------------------
// SceneStatesPanel ("States" dock tab)
//
// Lists the current document's named scene states (SceneGraph::sceneStates(),
// see SceneStateData.h) and lets the user save the current camera view +
// mesh visibility + selection together under a name, recall one (single-
// click, same immediate-activation convention CamerasPanel/SelectionSetsPanel
// already use), or delete one. A single MainWindow-owned shared instance,
// rebound to whichever document is active - same lifecycle as
// SelectionSetsPanel in the same dock, and structurally its closest
// relative: a flat QListWidget with Save/Delete buttons, no hierarchy.
//
// Deliberately simpler than SelectionSetsPanel in one respect: no "active
// state" row highlighting. SelectionSetsPanel's exact-set-equality highlight
// is already borderline for a single mesh-UUID set; a scene state would
// additionally need float-tolerant equality against live camera position/
// direction/up/FOV, which drifts from ordinary orbit/pan navigation and
// would flicker on/off unpredictably even when nothing meaningful has
// changed since a recall - and no float-tolerant-equality helper exists
// anywhere in this codebase to reuse. So: every click recalls, there's no
// toggle-off-on-reclick behavior either (there's no reliable "this one is
// currently active" to toggle away from).
// ---------------------------------------------------------------------------
class SceneStatesPanel : public QWidget
{
	Q_OBJECT
public:
	explicit SceneStatesPanel(QWidget* parent = nullptr);

	void setSceneGraph(SceneGraph* sg);

	// Rebuild the list from the current SceneGraph scene-state data.
	void refresh();

signals:
	// Emitted on single-click of any row - always an immediate recall (see
	// this class's doc comment for why there's no toggle-off case here,
	// unlike SelectionSetsPanel).
	void sceneStateRecallRequested(const QUuid& stateId);
	// Emitted from the bottom "Save Current State..." button, after
	// prompting for a name.
	void sceneStateSaveRequested(const QString& name);
	// Emitted from the bottom Delete button.
	void sceneStateDeleteRequested(const QUuid& stateId);

private slots:
	void onItemClicked(QListWidgetItem* item);
	void onSaveButtonClicked();
	void onDeleteButtonClicked();
	void onSelectionChanged();

private:
	QListWidget* _list = nullptr;
	QPushButton* _saveButton = nullptr;
	QPushButton* _deleteButton = nullptr;
	SceneGraph* _sceneGraph = nullptr;
};
