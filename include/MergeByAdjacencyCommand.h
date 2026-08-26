#pragma once

#include "ModelViewerCommand.h"
#include "SceneNode.h"

#include <QSet>
#include <QUuid>
#include <QVector>

// ---------------------------------------------------------------------------
// MergeByAdjacencyCommand
//
// Undoable command for replacing several separate meshes that were found to
// be spatially touching (see ModelViewer::mergeSelectedMeshesByAdjacency())
// with one combined mesh (see SceneMesh::mergeMeshes()). The inverse
// counterpart to SplitByConnectivityCommand: N sources (each possibly under
// its OWN SceneNode, unlike Split's fragments which always share the single
// original's node) collapse into 1 merged mesh under a chosen target node
// (the first source's own node). Mirrors both DeleteMeshCommand's recycle-
// bin mechanics and DuplicateCommand's "already happened, command just
// replays it" convention, same as SplitByConnectivityCommand.
//
// Undo  -> merged mesh moved to recycle bin, each source restored from the
//          recycle bin at its own original node/position, original
//          selection restored.
// Redo  -> every source moved to recycle bin, merged mesh restored from the
//          recycle bin and re-inserted, merged mesh selected.
// ---------------------------------------------------------------------------
class MergeByAdjacencyCommand : public ModelViewerCommand
{
public:
    // Per-source record: its UUID and the node/position it held just before
    // being removed - so undo can put it back exactly there.
    struct SourceEntry
    {
        QUuid      uuid;
        SceneNode* ownerNode = nullptr;
        int        position  = 0;
    };

    // Called AFTER the merge has already occurred (sources removed from the
    // tree/store, merged mesh constructed and inserted).
    MergeByAdjacencyCommand(ModelViewer*                  viewer,
                             ViewportWidget*               viewportWidget,
                             const QVector<SourceEntry>&  sources,
                             const QUuid&                  mergedUuid,
                             SceneNode*                    targetNode,
                             int                            mergedPosition,
                             const QSet<QUuid>&             originalSelection,
                             const QString&                text = QObject::tr("Merge by Adjacency"));
    ~MergeByAdjacencyCommand() override;

    void undo() override;
    void redo() override;

    int id() const override { return 15; }

    // For cleanup system - covers the merged mesh (owned by this command
    // while merged) and every source (owned by this command while undone).
    QSet<QUuid> getReferencedUuids() const;

private:
    QVector<SourceEntry>  _sources;
    QUuid                  _mergedUuid;
    SceneNode*             _targetNode;
    int                    _mergedPosition;
    QSet<QUuid>            _originalSelection;
    bool                   _firstRedo;
    bool                   _merged; // true = merged mesh lives in the scene, sources in the recycle bin
};
