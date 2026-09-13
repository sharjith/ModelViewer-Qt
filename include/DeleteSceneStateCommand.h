#pragma once

#include "ModelViewerCommand.h"
#include "SceneStateData.h"

#include <QUuid>

// Undoable "Delete Scene State". Captures the full SceneState (and its
// current display position) from the viewer's SceneGraph at construction
// time - same "capture old state before the change" convention
// DeleteSelectionSetCommand uses - so undo can restore it exactly via
// SceneGraph::insertSceneStateAt().
class DeleteSceneStateCommand : public ModelViewerCommand
{
public:
	DeleteSceneStateCommand(ModelViewer* viewer,
		ViewportWidget* viewportWidget,
		const QUuid& stateId,
		const QString& text = QObject::tr("Delete Scene State"));

	void undo() override;
	void redo() override;

	int id() const override { return 21; }

private:
	SceneState _removedState;
	int _removedIndex = -1;
};
