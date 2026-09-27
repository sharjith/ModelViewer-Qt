#pragma once

#include "ModelViewerCommand.h"
#include "SceneStateData.h"

// Undoable "Save Scene State" - same simple before/after shape as
// SaveSelectionSetCommand (a save is a single atomic state change, not a
// structural operation that already happened before the command was built).
class SaveSceneStateCommand : public ModelViewerCommand
{
public:
	SaveSceneStateCommand(ModelViewer* viewer,
		ViewportWidget* viewportWidget,
		const SceneState& newState,
		const QString& text = QObject::tr("Save Scene State"));

	void undo() override;
	void redo() override;

	int id() const override { return 20; }

private:
	SceneState _state;
};
