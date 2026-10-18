#pragma once

#include "GltfAnimationData.h"
#include "GltfCameraData.h"
#include "GltfLightData.h"
#include "GltfVariantData.h"
#include "ModelViewerCommand.h"
#include "SceneNode.h"
#include <QHash>
#include <QVector>
#include <QMap>
#include <QUuid>
#include <QSet>
#include <QString>

class ModelViewer;
class ViewportWidget;

/**
 * @brief Undoable command for deleting meshes
 *
 * Captures the UUIDs of deleted meshes and their original positions
 * in the model list to allow restoration on undo.
 */
class DeleteMeshCommand : public ModelViewerCommand
{
public:
    DeleteMeshCommand(ModelViewer* viewer,
        ViewportWidget* viewportWidget,
        const QVector<QUuid>& meshUuids,
        const QString& text = QObject::tr("Delete"));

    ~DeleteMeshCommand() override;

    void undo() override;
    void redo() override;

    int id() const override { return 5; }

    // For cleanup system
    QSet<QUuid> getReferencedUuids() const
    {
        return QSet<QUuid>(_meshUuids.begin(), _meshUuids.end());
    }

private:
    QVector<QUuid>   _meshUuids;
    QMap<QUuid, int> _originalIndices;  // Store original positions in _meshStore

    // SceneGraph removal records — populated during the first redo() call so
    // that undo() can put each UUID back in exactly the right node and position.
    struct SceneRemovalRecord
    {
        SceneNode* node     = nullptr;
        int        position = -1;
    };
    QMap<QUuid, SceneRemovalRecord> _sceneRecords;

    // Snapshots of the per-file glTF data for files that will lose ALL their
    // meshes in this deletion.  The validate*Data() handlers fire on
    // structureChanged and clear these from the SceneGraph synchronously
    // inside redo(); we capture them before any removal and put them back in
    // undo() so delete+undo round-trips do not lose animations, cameras,
    // variants, or lights.
    QHash<QString, GltfLightData>     _savedLightData;
    QHash<QString, GltfAnimationData> _savedAnimationData;
    QHash<QString, GltfCameraData>    _savedCameraData;
    QHash<QString, GltfVariantData>   _savedVariantData;
    QHash<QString, int>               _savedActiveClip;
    QHash<QString, int>               _savedActiveVariant;

    // File-level nodes detached from the SceneGraph because their subtree
    // lost its last mesh in this deletion.  Held alive here so undo can
    // reattach them at their original root position.  If the command dies
    // while still in the redone state, the destructor frees the subtrees.
    struct DetachedFileNode
    {
        SceneNode* node     = nullptr;
        int        position = -1;
    };
    QHash<QString, DetachedFileNode> _detachedFileNodes;
};
