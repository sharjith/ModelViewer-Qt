#include "PurgeRedundantNodesCommand.h"
#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "SceneGraph.h"
#include "SceneMesh.h"

PurgeRedundantNodesCommand::PurgeRedundantNodesCommand(ModelViewer*                    viewer,
                                                         ViewportWidget*                 viewportWidget,
                                                         const QVector<PromotionEntry>&  promotions,
                                                         const QSet<QUuid>&              originalSelection,
                                                         const QString&                  text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _promotions(promotions)
    , _originalSelection(originalSelection)
    , _firstRedo(true)
    , _applied(true)
{
}

PurgeRedundantNodesCommand::~PurgeRedundantNodesCommand()
{
    // If destroyed while applied, every eliminated node is still detached and owned by this command - free them
    // (mirrors GroupMeshesCommand's detached-node cleanup). Once undone (_applied == false) they are live in the
    // SceneGraph again - nothing to do.
    if (_applied)
    {
        for (const PromotionEntry& entry : _promotions)
        {
            if (entry.eliminatedNode)
                SceneGraph::deleteDetachedSubtree(entry.eliminatedNode);
        }
    }
}

void PurgeRedundantNodesCommand::undo()
{
    if (!_viewer || !_viewportWidget)
        return;

    SceneGraph* sg = _viewer->sceneGraph();

    // Reverse of the recorded (bottom-up) order: undo the OUTERMOST collapse first, putting each eliminated node
    // back exactly where (and with the mesh it originally held) before the next-inner undo runs.
    for (auto it = _promotions.rbegin(); it != _promotions.rend(); ++it)
    {
        const PromotionEntry& entry = *it;
        sg->insertChildNode(entry.survivorNode, entry.eliminatedNode, entry.eliminatedPosition);

        int pos = 0;
        sg->removeMeshUuid(entry.meshUuid, pos);
        sg->restoreMeshUuid(entry.eliminatedNode, entry.meshUuid, 0);

        if (SceneMesh* mesh = _viewportWidget->getMeshByUuid(entry.meshUuid))
            mesh->setName(entry.meshNameBefore);
    }

    _applied = false;

    _viewportWidget->updateView();
    _viewer->updateDisplayList();
    _viewer->setSelectionWithoutUndo(_originalSelection);
}

void PurgeRedundantNodesCommand::redo()
{
    if (!_viewer || !_viewportWidget)
        return;

    QSet<QUuid> promotedSet;
    for (const PromotionEntry& entry : _promotions)
        promotedSet.insert(entry.meshUuid);

    if (_firstRedo)
    {
        // Every promotion already happened at push time; just select the promoted meshes.
        _firstRedo = false;
        _applied = true;
        _viewer->setSelectionWithoutUndo(promotedSet);
        return;
    }

    SceneGraph* sg = _viewer->sceneGraph();

    // Forward (recorded, bottom-up) order: re-collapse the innermost wrapper first, exactly as originally applied.
    for (const PromotionEntry& entry : _promotions)
    {
        int pos = 0;
        sg->removeMeshUuid(entry.meshUuid, pos);
        sg->restoreMeshUuid(entry.survivorNode, entry.meshUuid, entry.survivorNode->meshUuids.size());

        int outPosition = 0;
        sg->removeChildNode(entry.survivorNode, entry.eliminatedNode, outPosition);

        if (SceneMesh* mesh = _viewportWidget->getMeshByUuid(entry.meshUuid))
            mesh->setName(entry.meshNameAfter);
    }

    _applied = true;

    _viewportWidget->updateView();
    _viewer->updateDisplayList();
    _viewer->setSelectionWithoutUndo(promotedSet);
}

QSet<QUuid> PurgeRedundantNodesCommand::getReferencedUuids() const
{
    QSet<QUuid> result;
    for (const PromotionEntry& entry : _promotions)
        result.insert(entry.meshUuid);
    return result;
}
