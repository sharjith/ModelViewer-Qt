#include "RenameMeshCommand.h"
#include "ViewportWidget.h"
#include "SceneTreeWidget.h"
#include "RenderableMesh.h"

// ---------------------------------------------------------------------------

RenameMeshCommand::RenameMeshCommand(ModelViewer*     viewer,
                                     ViewportWidget*        viewportWidget,
                                     SceneTreeWidget* treeWidget,
                                     const QUuid&     uuid,
                                     const QString&   oldName,
                                     const QString&   newName,
                                     const QString&   text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _treeWidget(treeWidget)
    , _uuid(uuid)
    , _oldName(oldName)
    , _newName(newName)
{
}

void RenameMeshCommand::redo()
{
    applyName(_newName);
}

void RenameMeshCommand::undo()
{
    applyName(_oldName);
}

void RenameMeshCommand::applyName(const QString& name)
{
    SceneMesh* mesh = _viewportWidget->getMeshByUuid(_uuid);
    if (!mesh) return;

    mesh->setName(name);

    if (_treeWidget)
        _treeWidget->updateMeshName(_uuid, name);
}
