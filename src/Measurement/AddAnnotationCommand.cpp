#include "AddAnnotationCommand.h"

#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "SceneGraph.h"

AddAnnotationCommand::AddAnnotationCommand(ModelViewer* viewer,
    ViewportWidget* viewportWidget,
    const Annotation& annotation,
    const QString& text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _annotation(annotation)
{
}

void AddAnnotationCommand::undo()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg)
        return;

    const int index = sg->annotationIndexById(_annotation.id);
    if (index >= 0)
        sg->removeAnnotationAt(index);
}

void AddAnnotationCommand::redo()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg)
        return;

    sg->addAnnotation(_annotation);

    if (_viewer)
        _viewer->setDocumentModified(true);
}
