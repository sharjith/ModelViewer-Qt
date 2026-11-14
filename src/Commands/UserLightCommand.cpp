#include "UserLightCommand.h"

#include "ModelViewer.h"
#include "SceneGraph.h"
#include "ViewportWidget.h"

UserLightCommand::UserLightCommand(ModelViewer*          viewer,
                                   ViewportWidget*       viewportWidget,
                                   Kind                  kind,
                                   int                   index,
                                   const GltfLightEntry& entry,
                                   const QString&        text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _kind(kind)
    , _index(index)
    , _entry(entry)
{
}

void UserLightCommand::redo()
{
    if (_kind == Kind::Add)
        insertLight();
    else
        removeLight();
}

void UserLightCommand::undo()
{
    if (_kind == Kind::Add)
        removeLight();
    else
        insertLight();
}

void UserLightCommand::insertLight()
{
    if (SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr)
        sg->insertUserLight(_index, _entry);
    if (_viewportWidget)
        _viewportWidget->userLightsChanged();
}

void UserLightCommand::removeLight()
{
    if (SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr)
        sg->removeUserLight(_index);
    if (_viewportWidget)
        _viewportWidget->userLightsChanged();
}
