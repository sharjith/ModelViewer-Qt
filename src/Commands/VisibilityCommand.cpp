#include "VisibilityCommand.h"
#include "ModelViewer.h"
#include "ViewportWidget.h"

VisibilityCommand::VisibilityCommand(ModelViewer* viewer,
    ViewportWidget* viewportWidget,
    const QSet<QUuid>& newVisibleUuids,
    const QString& text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _newVisibleUuids(newVisibleUuids)
{
    // Capture the current visibility state before the change
    _oldVisibleUuids = _viewer->getVisibleUuids();
}

void VisibilityCommand::undo()
{
    applyVisibility(_oldVisibleUuids);
}

void VisibilityCommand::redo()
{
    applyVisibility(_newVisibleUuids);
}

void VisibilityCommand::applyVisibility(const QSet<QUuid>& visibleUuids)
{
    if (!_viewer || !_viewportWidget)
        return;

    // Use the non-undo version to prevent recursion
    _viewer->setVisibilityWithoutUndo(visibleUuids);
}
