#include "MeasurementOffsetCommand.h"

#include "ModelViewer.h"
#include "SceneGraph.h"

MeasurementOffsetCommand::MeasurementOffsetCommand(ModelViewer* viewer,
    ViewportWidget* viewportWidget,
    const QUuid& measurementId,
    float oldOffsetDistance,
    float newOffsetDistance,
    bool oldReflexAngle,
    bool newReflexAngle,
    const QString& text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _measurementId(measurementId)
    , _oldOffsetDistance(oldOffsetDistance)
    , _newOffsetDistance(newOffsetDistance)
    , _oldReflexAngle(oldReflexAngle)
    , _newReflexAngle(newReflexAngle)
{
}

void MeasurementOffsetCommand::undo()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg)
        return;
    sg->setMeasurementOffsetDistance(_measurementId, _oldOffsetDistance);
    sg->setMeasurementReflexAngle(_measurementId, _oldReflexAngle);
}

void MeasurementOffsetCommand::redo()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg)
        return;
    sg->setMeasurementOffsetDistance(_measurementId, _newOffsetDistance);
    sg->setMeasurementReflexAngle(_measurementId, _newReflexAngle);

    if (_viewer)
        _viewer->setDocumentModified(true);
}
