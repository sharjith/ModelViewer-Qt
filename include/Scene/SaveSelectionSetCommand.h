#pragma once

#include "ModelViewerCommand.h"
#include "SelectionSetData.h"

// Undoable "Save Selection Set" - same simple before/after shape as
// VisibilityCommand (a save is a single atomic state change, not a
// structural operation that already happened before the command was built).
class SaveSelectionSetCommand : public ModelViewerCommand
{
public:
	SaveSelectionSetCommand(ModelViewer* viewer,
		ViewportWidget* viewportWidget,
		const SelectionSet& newSet,
		const QString& text = QObject::tr("Save Selection Set"));

	void undo() override;
	void redo() override;

	int id() const override { return 18; }

private:
	SelectionSet _set;
};
