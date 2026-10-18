#include "DeleteSceneStateCommand.h"
#include "ModelViewer.h"
#include "SceneGraph.h"

DeleteSceneStateCommand::DeleteSceneStateCommand(ModelViewer* viewer,
	ViewportWidget* viewportWidget,
	const QUuid& stateId,
	const QString& text)
	: ModelViewerCommand(viewer, viewportWidget, text)
{
	if (_viewer && _viewer->sceneGraph())
	{
		_removedIndex = _viewer->sceneGraph()->sceneStateIndexById(stateId);
		if (_removedIndex >= 0)
			_removedState = _viewer->sceneGraph()->sceneStates().at(_removedIndex);
	}
}

void DeleteSceneStateCommand::redo()
{
	if (_viewer && _viewer->sceneGraph())
		_viewer->sceneGraph()->removeSceneStateById(_removedState.id);
}

void DeleteSceneStateCommand::undo()
{
	if (_viewer && _viewer->sceneGraph() && _removedIndex >= 0)
		_viewer->sceneGraph()->insertSceneStateAt(_removedIndex, _removedState);
}
