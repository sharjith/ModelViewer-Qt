#include "AddMeasurementCommand.h"

#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "SceneGraph.h"

AddMeasurementCommand::AddMeasurementCommand(ModelViewer* viewer,
    ViewportWidget* viewportWidget,
    const Measurement& measurement,
    const QString& text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _measurement(measurement)
{
}

void AddMeasurementCommand::undo()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg)
        return;

    const int index = sg->measurementIndexById(_measurement.id);
    if (index >= 0)
        sg->removeMeasurementAt(index);
}

void AddMeasurementCommand::redo()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg)
        return;

    sg->addMeasurement(_measurement);

    if (_viewer)
        _viewer->setDocumentModified(true);
}
