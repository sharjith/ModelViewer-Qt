#include "Plot3DNotesCommand.h"

#include "ModelViewer.h"

Plot3DNotesCommand::Plot3DNotesCommand(ModelViewer* viewer, ViewportWidget* viewportWidget, const QUuid& plotMeshUuid,
	std::vector<Plot3DTextLabel> before, std::vector<Plot3DTextLabel> after, const QString& text)
	: ModelViewerCommand(viewer, viewportWidget, text)
	, _plot(plotMeshUuid)
	, _before(std::move(before))
	, _after(std::move(after))
{
}

void Plot3DNotesCommand::undo()
{
	if (_viewer)
		_viewer->applyPlot3DTextLabels(_plot, _before); // a plot deleted since is simply no longer there: nothing to restore
}

void Plot3DNotesCommand::redo()
{
	if (!_viewer)
		return;
	_viewer->applyPlot3DTextLabels(_plot, _after);
	_viewer->setDocumentModified(true);
}
