#pragma once

#include "ModelViewerCommand.h"
#include "SceneNode.h"

#include <QSet>
#include <QUuid>
#include <QVector>

// ---------------------------------------------------------------------------
// GroupMeshesCommand
//
// Undoable command for the organizational "Group" operation: creates a new
// (empty at construction) SceneNode and moves the selected meshes' UUIDs
// into it - no geometry is touched, no SceneMesh is created/destroyed, this
// is pure scene-tree reorganization. The inverse of ungrouping, and the
// hierarchy-manipulation sibling to SplitByConnectivityCommand/
// MergeByAdjacencyCommand (which combine/split MESH GEOMETRY instead).
//
// SceneNode* ownership never leaves the SceneGraph while grouped; while
// undone, this command owns the detached group node (mirrors PasteCommand's
// cut-subtree/copy-subtree ownership split) and frees it if destroyed in
// that state.
//
// Undo  -> every mesh moved back to its original (node, position); the now-
//          empty group node detached from its parent, original selection
//          restored.
// Redo  -> group node re-attached at its original (parent, position); every
//          mesh moved back into it, group's meshes selected.
// ---------------------------------------------------------------------------
class GroupMeshesCommand : public ModelViewerCommand
{
public:
    // Per-mesh record: its UUID and the (node, position) it held just
    // before being moved into the group - so undo can put it back exactly
    // there.
    struct MeshEntry
    {
        QUuid      uuid;
        SceneNode* originalOwnerNode = nullptr;
        int        originalPosition  = 0;
    };

    // Called AFTER the group has already been created (group node built,
    // inserted into the tree, every mesh already moved into it) - mirrors
    // DuplicateCommand/SplitByConnectivityCommand/MergeByAdjacencyCommand's
    // "already happened, command just replays it" convention.
    GroupMeshesCommand(ModelViewer*               viewer,
                        ViewportWidget*            viewportWidget,
                        SceneNode*                 groupNode,
                        SceneNode*                 groupParent,
                        int                        groupPosition,
                        const QVector<MeshEntry>&  meshEntries,
                        const QSet<QUuid>&         originalSelection,
                        const QString&             text = QObject::tr("Group"));
    ~GroupMeshesCommand() override;

    void undo() override;
    void redo() override;

    int id() const override { return 16; }

    // For cleanup system - every mesh moved by this command (never
    // recycle-binned, just relocated, but still worth tracking for
    // consistency with the other structural commands).
    QSet<QUuid> getReferencedUuids() const;

private:
    SceneNode*          _groupNode;
    SceneNode*          _groupParent;
    int                 _groupPosition;
    QVector<MeshEntry>  _meshEntries;
    QSet<QUuid>         _originalSelection;
    bool                _firstRedo;
    bool                _grouped; // true = group node is attached and owns the meshes
};
