#pragma once

#include "ModelViewerCommand.h"
#include "AnnotationData.h"

/**
 * @brief Undoable command that adds one Annotation to SceneGraph's
 * document-level list, pushed when placing a note completes (see
 * AnnotationController::handleAnnotationClick()).
 */
class AddAnnotationCommand : public ModelViewerCommand
{
public:
    AddAnnotationCommand(ModelViewer* viewer,
        ViewportWidget* viewportWidget,
        const Annotation& annotation,
        const QString& text = QObject::tr("Add Annotation"));

    void undo() override;
    void redo() override;

private:
    Annotation _annotation;
};
