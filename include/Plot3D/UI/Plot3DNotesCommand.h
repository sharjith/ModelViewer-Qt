#pragma once

#include "ModelViewerCommand.h"
#include "Plot3DSession.h"

#include <QUuid>

#include <vector>

// Undoable change of one plot's text notes (placing, moving, editing or deleting a note, or an edit in the notes table): the plot's whole list of
// notes before and after. The notes are small, so a before/after copy is the simplest thing that is always right, whatever the edit was.
class Plot3DNotesCommand : public ModelViewerCommand
{
public:
	Plot3DNotesCommand(ModelViewer* viewer, ViewportWidget* viewportWidget, const QUuid& plotMeshUuid,
		std::vector<Plot3DTextLabel> before, std::vector<Plot3DTextLabel> after, const QString& text);

	void undo() override;
	void redo() override;

private:
	QUuid _plot;
	std::vector<Plot3DTextLabel> _before;
	std::vector<Plot3DTextLabel> _after;
};
