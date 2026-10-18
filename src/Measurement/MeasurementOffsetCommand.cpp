#include "MeasurementOffsetCommand.h"

#include "ModelViewer.h"
#include "SceneGraph.h"

MeasurementOffsetCommand::MeasurementOffsetCommand(ModelViewer* viewer,
    ViewportWidget* viewportWidget,
    const QUuid& measurementId,
    float oldOffsetDistance,
    float newOffsetDistance,
    const QString& text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _measurementId(measurementId)
    , _oldOffsetDistance(oldOffsetDistance)
    , _newOffsetDistance(newOffsetDistance)
{
}

void MeasurementOffsetCommand::undo()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg)
        return;
    sg->setMeasurementOffsetDistance(_measurementId, _oldOffsetDistance);
}

void MeasurementOffsetCommand::redo()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg)
        return;
    sg->setMeasurementOffsetDistance(_measurementId, _newOffsetDistance);

    if (_viewer)
        _viewer->setDocumentModified(true);
}
