#include "AnnotationTextCommand.h"

#include "ModelViewer.h"
#include "SceneGraph.h"

AnnotationTextCommand::AnnotationTextCommand(ModelViewer* viewer,
    ViewportWidget* viewportWidget,
    const QUuid& annotationId,
    const QString& oldText,
    const QString& newText,
    const QString& text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _annotationId(annotationId)
    , _oldText(oldText)
    , _newText(newText)
{
}

void AnnotationTextCommand::undo()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg)
        return;
    sg->setAnnotationText(_annotationId, _oldText);
}

void AnnotationTextCommand::redo()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg)
        return;
    sg->setAnnotationText(_annotationId, _newText);

    if (_viewer)
        _viewer->setDocumentModified(true);
}
