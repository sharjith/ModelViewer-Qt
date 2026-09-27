#include "SaveSceneStateCommand.h"
#include "ModelViewer.h"
#include "SceneGraph.h"

SaveSceneStateCommand::SaveSceneStateCommand(ModelViewer* viewer,
	ViewportWidget* viewportWidget,
	const SceneState& newState,
	const QString& text)
	: ModelViewerCommand(viewer, viewportWidget, text)
	, _state(newState)
{
}

void SaveSceneStateCommand::redo()
{
	if (_viewer && _viewer->sceneGraph())
		_viewer->sceneGraph()->addSceneState(_state);
}

void SaveSceneStateCommand::undo()
{
	if (_viewer && _viewer->sceneGraph())
		_viewer->sceneGraph()->removeSceneStateById(_state.id);
}
