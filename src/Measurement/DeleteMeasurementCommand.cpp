#include "DeleteMeasurementCommand.h"

#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "SceneGraph.h"

DeleteMeasurementCommand::DeleteMeasurementCommand(ModelViewer* viewer,
    ViewportWidget* viewportWidget,
    const QUuid& measurementId,
    const QString& text)
    : ModelViewerCommand(viewer, viewportWidget, text)
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg)
        return;

    _removedIndex = sg->measurementIndexById(measurementId);
    if (_removedIndex >= 0)
        _removedMeasurement = sg->measurements().at(_removedIndex);
}

void DeleteMeasurementCommand::undo()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg || _removedIndex < 0)
        return;

    sg->insertMeasurementAt(_removedIndex, _removedMeasurement);
}

void DeleteMeasurementCommand::redo()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg || _removedIndex < 0)
        return;

    // Re-find by id rather than trusting _removedIndex is still valid -
    // other measurements could have been added/removed between undo/redo
    // cycles (e.g. a subsequent Add pushed after this Delete, then this
    // Delete gets redone after being undone).
    const int index = sg->measurementIndexById(_removedMeasurement.id);
    if (index >= 0)
        sg->removeMeasurementAt(index);

    if (_viewer)
        _viewer->setDocumentModified(true);
}
