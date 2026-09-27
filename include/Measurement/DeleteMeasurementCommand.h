#pragma once

#include "ModelViewerCommand.h"
#include "MeasurementData.h"

#include <QUuid>

/**
 * @brief Undoable command that removes one Measurement from SceneGraph's
 * document-level list, identified by id (not index - the index a
 * measurement sits at can shift as others are added/removed, but its id is
 * stable). Captures the full Measurement and its position at construction
 * time so undo can restore it at the same spot rather than just appending
 * it back at the end.
 */
class DeleteMeasurementCommand : public ModelViewerCommand
{
public:
    DeleteMeasurementCommand(ModelViewer* viewer,
        ViewportWidget* viewportWidget,
        const QUuid& measurementId,
        const QString& text = QObject::tr("Delete Measurement"));

    void undo() override;
    void redo() override;

private:
    Measurement _removedMeasurement;
    int         _removedIndex = -1;
};
