#pragma once

#include "GltfLightData.h"
#include "ModelViewerCommand.h"

/**
 * @brief Undoable edit of one property group of a user light (name, position, aim, colour, intensity, range, cone angles).
 *
 * Consecutive edits of the same field on the same light merge into one undo step, so stepping a spin box or typing several values in a
 * row does not flood the undo stack.
 */
class EditUserLightCommand : public ModelViewerCommand
{
public:
    // Which part of the light an edit changes; only edits of the same field of the same light merge.
    enum class Field { Name, Position, Direction, Color, Intensity, Range, Cone, Enabled };

    EditUserLightCommand(ModelViewer*            viewer,
                         ViewportWidget*         viewportWidget,
                         int                     index,
                         Field                   field,
                         const GltfLightEntry&   oldEntry,
                         const GltfLightEntry&   newEntry,
                         const QString&          text);

    void undo() override;
    void redo() override;

    int id() const override { return 61; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    void apply(const GltfLightEntry& entry);

    int            _index;
    Field          _field;
    GltfLightEntry _oldEntry;
    GltfLightEntry _newEntry;
};
