#pragma once

#include "ModelViewerCommand.h"

#include <QUuid>
#include <QVector3D>

/**
 * @brief Undoable command wrapping one drag-to-reposition edit of a LINEAR
 * dimension's full offset vector (pivot + extend) - the vector-typed
 * sibling of MeasurementOffsetCommand (which handles the ANGLE dimension's
 * scalar arc-radius drag instead). See Measurement::offsetVector's doc
 * comment and ViewportWidget's dimension-line-drag interaction.
 *
 * Same "direct mutator during drag, one command pushed on release" pattern
 * as MeasurementOffsetCommand - see that class's doc comment for the full
 * reasoning.
 */
class MeasurementOffsetVectorCommand : public ModelViewerCommand
{
public:
    MeasurementOffsetVectorCommand(ModelViewer* viewer,
        ViewportWidget* viewportWidget,
        const QUuid& measurementId,
        const QVector3D& oldOffsetVector,
        const QVector3D& newOffsetVector,
        const QString& text = QObject::tr("Reposition Dimension Line"));

    void undo() override;
    void redo() override;

private:
    QUuid _measurementId;
    QVector3D _oldOffsetVector;
    QVector3D _newOffsetVector;
};
