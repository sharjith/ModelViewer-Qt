#include "ModelViewerCommand.h"
#include "ModelViewer.h"
#include "ViewportWidget.h"

ModelViewerCommand::ModelViewerCommand(ModelViewer* viewer,
    ViewportWidget* viewportWidget,
    const QString& text)
    : QUndoCommand(text)
    , _viewer(viewer)
    , _viewportWidget(viewportWidget)
{
}
