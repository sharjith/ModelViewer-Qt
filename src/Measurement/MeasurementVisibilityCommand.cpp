#include "MeasurementVisibilityCommand.h"

#include "ModelViewer.h"
#include "ViewportWidget.h"

MeasurementVisibilityCommand::MeasurementVisibilityCommand(ModelViewer* viewer,
    ViewportWidget* viewportWidget,
    const QSet<QUuid>& newVisibleIds,
    const QString& text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _newVisibleIds(newVisibleIds)
{
    // Capture the current visibility state before the change
    _oldVisibleIds = _viewer->getVisibleMeasurementUuids();
}

void MeasurementVisibilityCommand::undo()
{
    applyVisibility(_oldVisibleIds);
}

void MeasurementVisibilityCommand::redo()
{
    applyVisibility(_newVisibleIds);
}

void MeasurementVisibilityCommand::applyVisibility(const QSet<QUuid>& visibleIds)
{
    if (!_viewer || !_viewportWidget)
        return;

    // Use the non-undo version to prevent recursion
    _viewer->setMeasurementVisibilityWithoutUndo(visibleIds);
}
