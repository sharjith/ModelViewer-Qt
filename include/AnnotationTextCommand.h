#pragma once

#include "ModelViewerCommand.h"

#include <QUuid>
#include <QString>

/**
 * @brief Undoable command wrapping one edit of an annotation's text (see
 * Annotation::text - AnnotationDialog's details pane commits this once when
 * the text field loses focus, not per-keystroke).
 *
 * Unlike a dimension-line drag (MeasurementOffsetCommand/
 * MeasurementOffsetVectorCommand), there is no continuous live-preview
 * mutator being called before this command is pushed - the text field edits
 * its own local buffer, and this command is what actually commits the
 * change to SceneGraph, both on the initial redo() and on any later
 * undo()/redo().
 */
class AnnotationTextCommand : public ModelViewerCommand
{
public:
    AnnotationTextCommand(ModelViewer* viewer,
        ViewportWidget* viewportWidget,
        const QUuid& annotationId,
        const QString& oldText,
        const QString& newText,
        const QString& text = QObject::tr("Edit Annotation Text"));

    void undo() override;
    void redo() override;

private:
    QUuid   _annotationId;
    QString _oldText;
    QString _newText;
};
