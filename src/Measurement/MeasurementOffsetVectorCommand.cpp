#include "MeasurementOffsetVectorCommand.h"

#include "ModelViewer.h"
#include "SceneGraph.h"

MeasurementOffsetVectorCommand::MeasurementOffsetVectorCommand(ModelViewer* viewer,
    ViewportWidget* viewportWidget,
    const QUuid& measurementId,
    const QVector3D& oldOffsetVector,
    const QVector3D& newOffsetVector,
    const QString& text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _measurementId(measurementId)
    , _oldOffsetVector(oldOffsetVector)
    , _newOffsetVector(newOffsetVector)
{
}

void MeasurementOffsetVectorCommand::undo()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg)
        return;
    sg->setMeasurementOffsetVector(_measurementId, _oldOffsetVector);
}

void MeasurementOffsetVectorCommand::redo()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg)
        return;
    sg->setMeasurementOffsetVector(_measurementId, _newOffsetVector);

    if (_viewer)
        _viewer->setDocumentModified(true);
}
