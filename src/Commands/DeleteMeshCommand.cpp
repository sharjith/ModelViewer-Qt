#include "DeleteMeshCommand.h"
#include "ViewportWidget.h"
#include "ModelViewer.h"
#include "SceneGraph.h"
#include "RenderableMesh.h"

#include <algorithm>
#include <climits>

DeleteMeshCommand::DeleteMeshCommand(ModelViewer* viewer,
    ViewportWidget* viewportWidget,
    const QVector<QUuid>& meshUuids,
    const QString& text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _meshUuids(meshUuids)
{
    // Store original indices for each mesh
    for (const QUuid& uuid : meshUuids)
    {
        int index = _viewportWidget->getIndexByUuid(uuid);
        if (index >= 0)
            _originalIndices[uuid] = index;
    }
}

DeleteMeshCommand::~DeleteMeshCommand()
{
    // If the command is destroyed while in the redone state (e.g. the undo
    // stack is cleared after a permanent delete), the detached file-node
    // subtrees are no longer reachable from the SceneGraph — free them here.
    for (const DetachedFileNode& detached : _detachedFileNodes)
    {
        if (detached.node && detached.node->parent == nullptr)
            SceneGraph::deleteDetachedSubtree(detached.node);
    }
}

void DeleteMeshCommand::undo()
{
    SceneGraph* sg = _viewer->sceneGraph();

    // Reattach file-level nodes that were pruned in redo() BEFORE restoring
    // mesh UUIDs, so each restoreMeshUuid() targets a node that is back in
    // the live tree.
    if (sg)
    {
        for (auto it = _detachedFileNodes.cbegin(); it != _detachedFileNodes.cend(); ++it)
            sg->reattachFileNode(it.value().node, it.value().position);
        _detachedFileNodes.clear();
    }

    // Restore in ascending original-index order so each insertion lands at
    // its recorded position — this reconstructs the exact pre-deletion
    // _meshStore order, which the export path depends on.
    QVector<QUuid> orderedUuids = _meshUuids;
    std::sort(orderedUuids.begin(), orderedUuids.end(),
        [this](const QUuid& a, const QUuid& b)
        {
            return _originalIndices.value(a, INT_MAX) < _originalIndices.value(b, INT_MAX);
        });

    for (const QUuid& uuid : orderedUuids)
    {
        // Restore mesh in the renderer's store
        if (_viewportWidget->isInRecycleBin(uuid))
        {
            _viewportWidget->restoreFromRecycleBin(uuid);
        }
        else
        {
            qWarning() << "DeleteMeshCommand::undo - Mesh not in bin:" << uuid;
        }

        // Restore UUID in the SceneGraph at its original node and position
        if (sg)
        {
            const SceneRemovalRecord& rec = _sceneRecords.value(uuid);
            sg->restoreMeshUuid(rec.node, uuid, rec.position);
        }
    }

    // Re-register the per-file glTF data for any files that had all their
    // meshes deleted.  The validate*Data() handlers cleared it during redo();
    // we put it back now that the meshes are live again.
    if (sg)
    {
        for (auto it = _savedLightData.cbegin(); it != _savedLightData.cend(); ++it)
            sg->setLightData(it.key(), it.value());

        for (auto it = _savedCameraData.cbegin(); it != _savedCameraData.cend(); ++it)
            sg->setGltfCameraData(it.key(), it.value());

        for (auto it = _savedVariantData.cbegin(); it != _savedVariantData.cend(); ++it)
        {
            sg->setVariantData(it.key(), it.value());
            sg->setActiveVariant(it.key(), _savedActiveVariant.value(it.key(), -1));
        }

        for (auto it = _savedAnimationData.cbegin(); it != _savedAnimationData.cend(); ++it)
        {
            const QString& file = it.key();
            sg->setAnimationData(file, it.value());
            // Rebuild ViewportWidget's runtime tables (cleared together with the
            // SceneGraph data during redo) and re-select the previously
            // active clip so the Animations panel comes back functional.
            _viewportWidget->syncRuntimeNodeTransforms(file);
            const int activeClip = _savedActiveClip.value(file, -1);
            if (activeClip >= 0 && activeClip < it.value().clips.size())
                _viewportWidget->setActiveAnimation(file, activeClip);
        }
    }

    _viewportWidget->updateView();
    _viewer->updateDisplayList();
}

void DeleteMeshCommand::redo()
{
    SceneGraph* sg = _viewer->sceneGraph();

    // Collect unique source files for the meshes being deleted.
    QSet<QString> candidateFiles;
    for (const QUuid& uuid : _meshUuids)
    {
        if (SceneMesh* m = _viewportWidget->getMeshByUuid(uuid))
            candidateFiles.insert(m->getSourceFile());
    }

    // Determine which of those files will lose ALL their meshes — i.e. no
    // live mesh of that file remains outside the deletion set.
    QSet<QString> filesLosingAllMeshes;
    {
        const QSet<QUuid> deletingSet(_meshUuids.begin(), _meshUuids.end());
        const std::vector<SceneMesh*>& store = _viewportWidget->getMeshStore();
        for (const QString& file : candidateFiles)
        {
            bool allDeleted = true;
            for (const SceneMesh* mesh : store)
            {
                if (mesh && mesh->getSourceFile() == file &&
                    !deletingSet.contains(mesh->uuid()))
                {
                    allDeleted = false;
                    break;
                }
            }
            if (allDeleted)
                filesLosingAllMeshes.insert(file);
        }
    }

    // Snapshot the per-file glTF data for any file that is about to lose ALL
    // its meshes.  This must happen before removeMeshUuid() because
    // structureChanged → validate*Data() → clear*Data() fires synchronously
    // inside that call and would destroy the data we need to save.
    _savedLightData.clear();
    _savedAnimationData.clear();
    _savedCameraData.clear();
    _savedVariantData.clear();
    _savedActiveClip.clear();
    _savedActiveVariant.clear();
    if (sg)
    {
        for (const QString& file : filesLosingAllMeshes)
        {
            const GltfLightData& ld = sg->lightDataForFile(file);
            if (!ld.isEmpty())
                _savedLightData[file] = ld;

            const GltfAnimationData ad = sg->animationDataForFile(file);
            if (!ad.isEmpty() || ad.hasSkinning)
            {
                _savedAnimationData[file] = ad;
                _savedActiveClip[file] = sg->activeAnimationClipForFile(file);
            }

            const GltfCameraData cd = sg->gltfCameraDataForFile(file);
            if (!cd.isEmpty())
                _savedCameraData[file] = cd;

            const GltfVariantData vd = sg->variantDataForFile(file);
            if (!vd.isEmpty())
            {
                _savedVariantData[file] = vd;
                _savedActiveVariant[file] = sg->activeVariantForFile(file);
            }
        }
    }

    for (const QUuid& uuid : _meshUuids)
    {
        // Remove UUID from SceneGraph and record where it was for undo
        if (sg)
        {
            SceneRemovalRecord rec;
            rec.node = sg->removeMeshUuid(uuid, rec.position);
            _sceneRecords[uuid] = rec;
        }

        // Move mesh to recycle bin in the renderer's store
        int originalIndex = _originalIndices.value(uuid, -1);
        _viewportWidget->moveToRecycleBin(uuid, originalIndex);
    }

    // Prune file-level nodes whose subtree lost its last mesh.  A stale empty
    // file node would shadow a freshly imported instance of the same file in
    // findFileNode(), breaking node-transform sync on re-import.
    if (sg)
    {
        for (const QString& file : filesLosingAllMeshes)
        {
            DetachedFileNode detached;
            detached.node = sg->detachEmptyFileNode(file, detached.position);
            if (detached.node)
                _detachedFileNodes[file] = detached;
        }
    }

    _viewportWidget->updateView();
    _viewer->updateDisplayList();
}
