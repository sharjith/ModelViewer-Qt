#include "AnnotationVisibilityCommand.h"

#include "ModelViewer.h"
#include "ViewportWidget.h"

AnnotationVisibilityCommand::AnnotationVisibilityCommand(ModelViewer* viewer,
    ViewportWidget* viewportWidget,
    const QSet<QUuid>& newVisibleIds,
    const QString& text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _newVisibleIds(newVisibleIds)
{
    // Capture the current visibility state before the change
    _oldVisibleIds = _viewer->getVisibleAnnotationUuids();
}

void AnnotationVisibilityCommand::undo()
{
    applyVisibility(_oldVisibleIds);
}

void AnnotationVisibilityCommand::redo()
{
    applyVisibility(_newVisibleIds);
}

void AnnotationVisibilityCommand::applyVisibility(const QSet<QUuid>& visibleIds)
{
    if (!_viewer || !_viewportWidget)
        return;

    // Use the non-undo version to prevent recursion
    _viewer->setAnnotationVisibilityWithoutUndo(visibleIds);
}
