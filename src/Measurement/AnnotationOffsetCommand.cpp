#include "AnnotationOffsetCommand.h"

#include "ModelViewer.h"
#include "SceneGraph.h"

AnnotationOffsetCommand::AnnotationOffsetCommand(ModelViewer* viewer,
    ViewportWidget* viewportWidget,
    const QUuid& annotationId,
    const QVector3D& oldLeaderOffset,
    const QVector3D& newLeaderOffset,
    const QString& text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _annotationId(annotationId)
    , _oldLeaderOffset(oldLeaderOffset)
    , _newLeaderOffset(newLeaderOffset)
{
}

void AnnotationOffsetCommand::undo()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg)
        return;
    sg->setAnnotationLeaderOffset(_annotationId, _oldLeaderOffset);
}

void AnnotationOffsetCommand::redo()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg)
        return;
    sg->setAnnotationLeaderOffset(_annotationId, _newLeaderOffset);

    if (_viewer)
        _viewer->setDocumentModified(true);
}
