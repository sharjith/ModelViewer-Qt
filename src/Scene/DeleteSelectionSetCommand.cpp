#include "DeleteSelectionSetCommand.h"
#include "ModelViewer.h"
#include "SceneGraph.h"

DeleteSelectionSetCommand::DeleteSelectionSetCommand(ModelViewer* viewer,
	ViewportWidget* viewportWidget,
	const QUuid& setId,
	const QString& text)
	: ModelViewerCommand(viewer, viewportWidget, text)
{
	if (_viewer && _viewer->sceneGraph())
	{
		_removedIndex = _viewer->sceneGraph()->selectionSetIndexById(setId);
		if (_removedIndex >= 0)
			_removedSet = _viewer->sceneGraph()->selectionSets().at(_removedIndex);
	}
}

void DeleteSelectionSetCommand::redo()
{
	if (_viewer && _viewer->sceneGraph())
		_viewer->sceneGraph()->removeSelectionSetById(_removedSet.id);
}

void DeleteSelectionSetCommand::undo()
{
	if (_viewer && _viewer->sceneGraph() && _removedIndex >= 0)
		_viewer->sceneGraph()->insertSelectionSetAt(_removedIndex, _removedSet);
}
