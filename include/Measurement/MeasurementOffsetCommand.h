#pragma once

#include "ModelViewerCommand.h"

#include <QUuid>

/**
 * @brief Undoable command wrapping one drag-to-reposition edit of a
 * measurement's dimension-line offset (see Measurement::offsetDistance's
 * doc comment and ViewportWidget's dimension-line-drag interaction).
 *
 * The drag itself directly mutates the offset every mouse-move (via
 * SceneGraph::setMeasurementOffsetDistance()) for live preview, with no
 * undo-stack involvement - exactly like TransformCommand's gizmo-drag
 * pattern. This command is pushed exactly once, on mouse-release, capturing
 * the offset value from before the drag started and the final value it
 * settled on; redo()/undo() both just call the same direct mutator.
 */
class MeasurementOffsetCommand : public ModelViewerCommand
{
public:
    MeasurementOffsetCommand(ModelViewer* viewer,
        ViewportWidget* viewportWidget,
        const QUuid& measurementId,
        float oldOffsetDistance,
        float newOffsetDistance,
        const QString& text = QObject::tr("Reposition Dimension Line"));

    void undo() override;
    void redo() override;

private:
    QUuid _measurementId;
    float _oldOffsetDistance;
    float _newOffsetDistance;
};
