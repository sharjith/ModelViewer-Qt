#include "PasteCommand.h"
#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "SceneGraph.h"

PasteCommand::PasteCommand(ModelViewer*                 viewer,
                           ViewportWidget*                    viewportWidget,
                           const QList<PastedItem>&     items,
                           const QSet<QUuid>&           originalSelection,
                           const QList<ClipboardEntry>& cutEntries,
                           const QString&               text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _items(items)
    , _originalSelection(originalSelection)
    , _cutEntries(cutEntries)
    , _firstRedo(true)
    , _inserted(true)
    , _generation(ModelViewer::currentClipboardGeneration())
{
    // Derive cut-mark sets from cut items so undo can re-apply them.
    for (const PastedItem& item : _items)
    {
        if (!item.isCut)
            continue;
        if (item.type == PastedItem::Mesh)
        {
            _cutMeshUuids.insert(item.meshUuid);
        }
        else // Subtree
        {
            _cutMeshUuids += QSet<QUuid>(item.subtreeMeshUuids.begin(),
                                         item.subtreeMeshUuids.end());
            if (item.subtreeRoot)
                _cutNodeUuids.insert(item.subtreeRoot->nodeUuid);
        }
    }
}

PasteCommand::~PasteCommand()
{
    // Only copy-paste Subtree items whose SceneNode* we own (when undone)
    // need cleanup.  Cut items always live in the scene — nothing to free.
    if (!_inserted)
    {
        for (const PastedItem& item : _items)
        {
            if (item.isCut)
                continue;

            if (item.type == PastedItem::Subtree)
            {
                if (_viewportWidget)
                {
                    for (const QUuid& uuid : item.subtreeMeshUuids)
                        _viewportWidget->permanentlyDeleteFromBin(uuid);
                }
                freeSubtree(item.subtreeRoot);
            }
            else // copy Mesh
            {
                if (_viewportWidget)
                    _viewportWidget->permanentlyDeleteFromBin(item.meshUuid);
            }
        }
    }
}

void PasteCommand::undo()
{
    if (!_viewer || !_viewportWidget)
        return;

    SceneGraph* sg = _viewer->sceneGraph();

    // Process in reverse insertion order to keep positions valid.
    for (int i = _items.size() - 1; i >= 0; --i)
    {
        const PastedItem& item = _items[i];

        if (item.isCut)
        {
            // Move the item back to its original (source) location.
            if (item.type == PastedItem::Mesh)
            {
                int pos = 0;
                sg->removeMeshUuid(item.meshUuid, pos);
                sg->restoreMeshUuid(item.srcOwnerNode, item.meshUuid,
                                    item.srcMeshPosition);
            }
            else // Subtree
            {
                int pos = 0;
                sg->removeChildNode(item.subtreeParent, item.subtreeRoot, pos);
                sg->insertChildNode(item.srcSubtreeParent, item.subtreeRoot,
                                    item.srcChildPosition);
            }
        }
        else
        {
            // Copy-paste: move to recycle bin and remove from scene.
            if (item.type == PastedItem::Mesh)
            {
                int idx = _viewportWidget->getIndexByUuid(item.meshUuid);
                if (idx >= 0)
                    _viewportWidget->moveToRecycleBin(item.meshUuid, idx);

                int pos = 0;
                sg->removeMeshUuid(item.meshUuid, pos);
            }
            else // Subtree
            {
                for (const QUuid& uuid : item.subtreeMeshUuids)
                {
                    int idx = _viewportWidget->getIndexByUuid(uuid);
                    if (idx >= 0)
                        _viewportWidget->moveToRecycleBin(uuid, idx);
                }

                int pos = 0;
                sg->removeChildNode(item.subtreeParent, item.subtreeRoot, pos);
            }
        }
    }

    _inserted = false;

    // Re-apply cut marks so items appear grayed while CutCommand is still
    // on the undo stack above this command.
    if (!_cutEntries.isEmpty())
        _viewer->reapplyCutMarks(_generation, _cutEntries, _cutMeshUuids, _cutNodeUuids);

    _viewportWidget->updateView();
    _viewer->updateDisplayList();
    _viewer->setSelectionWithoutUndo(_originalSelection);
}

void PasteCommand::redo()
{
    if (!_viewer || !_viewportWidget)
        return;

    if (_firstRedo)
    {
        // Items already in the scene from the initial paste — just select.
        _firstRedo = false;
        _inserted  = true;
        QSet<QUuid> pastedSet;
        for (const PastedItem& item : _items)
        {
            if (item.type == PastedItem::Mesh)
                pastedSet.insert(item.meshUuid);
            else
                pastedSet += QSet<QUuid>(item.subtreeMeshUuids.begin(),
                                         item.subtreeMeshUuids.end());
        }
        _viewer->setSelectionWithoutUndo(pastedSet);
        return;
    }

    SceneGraph* sg = _viewer->sceneGraph();

    for (const PastedItem& item : _items)
    {
        if (item.isCut)
        {
            // Re-execute the move (source → destination).
            if (item.type == PastedItem::Mesh)
            {
                int pos = 0;
                sg->removeMeshUuid(item.meshUuid, pos);
                sg->restoreMeshUuid(item.ownerNode, item.meshUuid,
                                    item.meshPosition);
            }
            else // Subtree
            {
                int pos = 0;
                sg->removeChildNode(item.srcSubtreeParent, item.subtreeRoot, pos);
                sg->insertChildNode(item.subtreeParent, item.subtreeRoot,
                                    item.childPosition);
            }
        }
        else
        {
            // Copy-paste: restore from recycle bin.
            if (item.type == PastedItem::Mesh)
            {
                _viewportWidget->restoreFromRecycleBin(item.meshUuid);
                sg->restoreMeshUuid(item.ownerNode, item.meshUuid, item.meshPosition);
            }
            else // Subtree
            {
                for (const QUuid& uuid : item.subtreeMeshUuids)
                    _viewportWidget->restoreFromRecycleBin(uuid);

                sg->insertChildNode(item.subtreeParent, item.subtreeRoot,
                                    item.childPosition);
            }
        }
    }

    _inserted = true;

    // Clear cut marks — items are now at their destination.
    if (!_cutEntries.isEmpty())
        _viewer->clearCutMarks(_generation);

    _viewportWidget->updateView();
    _viewer->updateDisplayList();

    QSet<QUuid> pastedSet;
    for (const PastedItem& item : _items)
    {
        if (item.type == PastedItem::Mesh)
            pastedSet.insert(item.meshUuid);
        else
            pastedSet += QSet<QUuid>(item.subtreeMeshUuids.begin(),
                                     item.subtreeMeshUuids.end());
    }
    _viewer->setSelectionWithoutUndo(pastedSet);
}

QSet<QUuid> PasteCommand::getReferencedUuids() const
{
    // Only copy-paste items go to the recycle bin — cut items never do.
    QSet<QUuid> result;
    for (const PastedItem& item : _items)
    {
        if (item.isCut)
            continue;
        if (item.type == PastedItem::Mesh)
            result.insert(item.meshUuid);
        else
            result += QSet<QUuid>(item.subtreeMeshUuids.begin(),
                                   item.subtreeMeshUuids.end());
    }
    return result;
}

void PasteCommand::freeSubtree(SceneNode* root)
{
    if (!root)
        return;
    for (SceneNode* child : root->children)
        freeSubtree(child);
    delete root;
}
