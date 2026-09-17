#include "PlaneGizmoDragCommand.h"

PlaneGizmoDragCommand::PlaneGizmoDragCommand(ModelViewer* viewer,
    ViewportWidget* viewportWidget,
    std::function<void(float)> setter,
    float oldValue,
    float newValue,
    const QString& text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _setter(std::move(setter))
    , _oldValue(oldValue)
    , _newValue(newValue)
{
}

void PlaneGizmoDragCommand::undo()
{
    if (_setter)
        _setter(_oldValue);
}

void PlaneGizmoDragCommand::redo()
{
    if (_setter)
        _setter(_newValue);
}
