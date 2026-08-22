#include "DeleteAnnotationCommand.h"

#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "SceneGraph.h"

DeleteAnnotationCommand::DeleteAnnotationCommand(ModelViewer* viewer,
    ViewportWidget* viewportWidget,
    const QUuid& annotationId,
    const QString& text)
    : ModelViewerCommand(viewer, viewportWidget, text)
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg)
        return;

    _removedIndex = sg->annotationIndexById(annotationId);
    if (_removedIndex >= 0)
        _removedAnnotation = sg->annotations().at(_removedIndex);
}

void DeleteAnnotationCommand::undo()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg || _removedIndex < 0)
        return;

    sg->insertAnnotationAt(_removedIndex, _removedAnnotation);
}

void DeleteAnnotationCommand::redo()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg || _removedIndex < 0)
        return;

    // Re-find by id rather than trusting _removedIndex is still valid -
    // other annotations could have been added/removed between undo/redo
    // cycles (e.g. a subsequent Add pushed after this Delete, then this
    // Delete gets redone after being undone).
    const int index = sg->annotationIndexById(_removedAnnotation.id);
    if (index >= 0)
        sg->removeAnnotationAt(index);

    if (_viewer)
        _viewer->setDocumentModified(true);
}
