#pragma once

#include "ModelViewerCommand.h"
#include "AnnotationData.h"

#include <QUuid>

/**
 * @brief Undoable command that removes one Annotation from SceneGraph's
 * document-level list, identified by id (not index - the index an
 * annotation sits at can shift as others are added/removed, but its id is
 * stable). Captures the full Annotation and its position at construction
 * time so undo can restore it at the same spot rather than just appending
 * it back at the end.
 */
class DeleteAnnotationCommand : public ModelViewerCommand
{
public:
    DeleteAnnotationCommand(ModelViewer* viewer,
        ViewportWidget* viewportWidget,
        const QUuid& annotationId,
        const QString& text = QObject::tr("Delete Annotation"));

    void undo() override;
    void redo() override;

private:
    Annotation _removedAnnotation;
    int        _removedIndex = -1;
};
