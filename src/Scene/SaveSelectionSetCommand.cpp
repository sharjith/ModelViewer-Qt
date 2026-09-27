#include "SaveSelectionSetCommand.h"
#include "ModelViewer.h"
#include "SceneGraph.h"

SaveSelectionSetCommand::SaveSelectionSetCommand(ModelViewer* viewer,
	ViewportWidget* viewportWidget,
	const SelectionSet& newSet,
	const QString& text)
	: ModelViewerCommand(viewer, viewportWidget, text)
	, _set(newSet)
{
}

void SaveSelectionSetCommand::redo()
{
	if (_viewer && _viewer->sceneGraph())
		_viewer->sceneGraph()->addSelectionSet(_set);
}

void SaveSelectionSetCommand::undo()
{
	if (_viewer && _viewer->sceneGraph())
		_viewer->sceneGraph()->removeSelectionSetById(_set.id);
}
