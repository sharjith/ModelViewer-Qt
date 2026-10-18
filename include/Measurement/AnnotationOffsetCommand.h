#pragma once

#include "ModelViewerCommand.h"

#include <QUuid>
#include <QVector3D>

/**
 * @brief Undoable command wrapping one drag-to-reposition edit of a note's
 * leader-line offset (see Annotation::leaderOffset's doc comment and
 * AnnotationController's leader-line-drag interaction). The vector-typed
 * sibling of MeasurementOffsetVectorCommand.
 *
 * The drag itself directly mutates the offset every mouse-move (via
 * SceneGraph::setAnnotationLeaderOffset()) for live preview, with no
 * undo-stack involvement - exactly like MeasurementOffsetVectorCommand's
 * pattern. This command is pushed exactly once, on mouse-release, capturing
 * the offset value from before the drag started and the final value it
 * settled on; redo()/undo() both just call the same direct mutator.
 */
class AnnotationOffsetCommand : public ModelViewerCommand
{
public:
    AnnotationOffsetCommand(ModelViewer* viewer,
        ViewportWidget* viewportWidget,
        const QUuid& annotationId,
        const QVector3D& oldLeaderOffset,
        const QVector3D& newLeaderOffset,
        const QString& text = QObject::tr("Reposition Annotation"));

    void undo() override;
    void redo() override;

private:
    QUuid     _annotationId;
    QVector3D _oldLeaderOffset;
    QVector3D _newLeaderOffset;
};
