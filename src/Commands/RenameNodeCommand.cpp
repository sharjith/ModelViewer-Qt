#include "RenameNodeCommand.h"
#include "ModelViewer.h"
#include "SceneGraph.h"
#include "SceneTreeWidget.h"

RenameNodeCommand::RenameNodeCommand(ModelViewer*     viewer,
                                     ViewportWidget*  viewportWidget,
                                     SceneTreeWidget* treeWidget,
                                     const QUuid&     nodeUuid,
                                     const QString&   oldName,
                                     const QString&   newName,
                                     const QString&   text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _treeWidget(treeWidget)
    , _nodeUuid(nodeUuid)
    , _oldName(oldName)
    , _newName(newName)
{
}

void RenameNodeCommand::redo()
{
    applyName(_newName);
}

void RenameNodeCommand::undo()
{
    applyName(_oldName);
}

void RenameNodeCommand::applyName(const QString& name)
{
    if (SceneGraph* sg = _viewer->sceneGraph())
    {
        if (SceneNode* node = sg->findNodeByUuid(_nodeUuid))
            node->name = name;
    }

    if (_treeWidget)
        _treeWidget->updateNodeName(_nodeUuid, name);
}
