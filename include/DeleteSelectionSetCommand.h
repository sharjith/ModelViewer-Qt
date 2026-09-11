#pragma once

#include "ModelViewerCommand.h"
#include "SelectionSetData.h"

#include <QUuid>

// Undoable "Delete Selection Set". Captures the full SelectionSet (and its
// current display position) from the viewer's SceneGraph at construction
// time - same "capture old state before the change" convention
// VisibilityCommand uses - so undo can restore it exactly via
// SceneGraph::insertSelectionSetAt().
class DeleteSelectionSetCommand : public ModelViewerCommand
{
public:
	DeleteSelectionSetCommand(ModelViewer* viewer,
		ViewportWidget* viewportWidget,
		const QUuid& setId,
		const QString& text = QObject::tr("Delete Selection Set"));

	void undo() override;
	void redo() override;

	int id() const override { return 19; }

private:
	SelectionSet _removedSet;
	int _removedIndex = -1;
};
