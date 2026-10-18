#include "GroupMeshesCommand.h"
#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "SceneGraph.h"

GroupMeshesCommand::GroupMeshesCommand(ModelViewer*               viewer,
                                        ViewportWidget*            viewportWidget,
                                        SceneNode*                 groupNode,
                                        SceneNode*                 groupParent,
                                        int                        groupPosition,
                                        const QVector<MeshEntry>&  meshEntries,
                                        const QSet<QUuid>&         originalSelection,
                                        const QString&             text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _groupNode(groupNode)
    , _groupParent(groupParent)
    , _groupPosition(groupPosition)
    , _meshEntries(meshEntries)
    , _originalSelection(originalSelection)
    , _firstRedo(true)
    , _grouped(true)
{
}

GroupMeshesCommand::~GroupMeshesCommand()
{
    // If destroyed while undone, the group node is detached and owned by
    // this command - free it (mirrors PasteCommand's detached-subtree
    // cleanup). While grouped, it's live in the SceneGraph - nothing to do.
    if (!_grouped && _groupNode)
        SceneGraph::deleteDetachedSubtree(_groupNode);
}

void GroupMeshesCommand::undo()
{
    if (!_viewer || !_viewportWidget)
        return;

    SceneGraph* sg = _viewer->sceneGraph();

    for (const MeshEntry& e : _meshEntries)
    {
        int pos = 0;
        sg->removeMeshUuid(e.uuid, pos);
        sg->restoreMeshUuid(e.originalOwnerNode, e.uuid, e.originalPosition);
    }

    int outPosition = 0;
    sg->removeChildNode(_groupParent, _groupNode, outPosition);

    _grouped = false;

    _viewportWidget->updateView();
    _viewer->updateDisplayList();
    _viewer->setSelectionWithoutUndo(_originalSelection);
}

void GroupMeshesCommand::redo()
{
    if (!_viewer || !_viewportWidget)
        return;

    QSet<QUuid> groupedSet;
    for (const MeshEntry& e : _meshEntries)
        groupedSet.insert(e.uuid);

    if (_firstRedo)
    {
        // The group already happened at push time; just select its meshes.
        _firstRedo = false;
        _grouped = true;
        _viewer->setSelectionWithoutUndo(groupedSet);
        return;
    }

    SceneGraph* sg = _viewer->sceneGraph();

    sg->insertChildNode(_groupParent, _groupNode, _groupPosition);

    // The group node is freshly re-attached (and empty - undo() pulled
    // every mesh back out), so appending in _meshEntries' own order
    // reconstructs the same layout every time without needing a separate
    // per-entry "position within group" field.
    for (const MeshEntry& e : _meshEntries)
    {
        int pos = 0;
        sg->removeMeshUuid(e.uuid, pos);
        sg->restoreMeshUuid(_groupNode, e.uuid, _groupNode->meshUuids.size());
    }

    _grouped = true;

    _viewportWidget->updateView();
    _viewer->updateDisplayList();
    _viewer->setSelectionWithoutUndo(groupedSet);
}

QSet<QUuid> GroupMeshesCommand::getReferencedUuids() const
{
    QSet<QUuid> result;
    for (const MeshEntry& e : _meshEntries)
        result.insert(e.uuid);
    return result;
}
