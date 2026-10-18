#pragma once

#include "ModelViewerCommand.h"
#include "SceneNode.h"

#include <QSet>
#include <QUuid>
#include <QVector>

// ---------------------------------------------------------------------------
// SplitByConnectivityCommand
//
// Undoable command for replacing one mesh (that turned out to contain
// several spatially-disconnected triangle islands - see SceneMesh::
// findConnectedTriangleGroups()) with N separate mesh fragments, one per
// island - all living as sibling leaves under the same SceneNode the
// original mesh belonged to. Mirrors DeleteMeshCommand's recycle-bin
// mechanics for the removed original and DuplicateCommand's "already
// happened, command just replays it" convention for the new fragments
// (constructed and inserted into the scene BEFORE this command is pushed -
// see ModelViewer::splitSelectedMeshesByConnectivity()).
//
// Undo  -> fragments moved to recycle bin, original restored from recycle
//          bin at its original tree position, original selection restored.
// Redo  -> original moved to recycle bin, fragments restored from recycle
//          bin and re-inserted, fragments selected.
// ---------------------------------------------------------------------------
class SplitByConnectivityCommand : public ModelViewerCommand
{
public:
    // Per-fragment record: the new mesh's UUID and the position it holds
    // (holds again, on every redo) within ownerNode->meshUuids.
    struct FragmentEntry
    {
        QUuid uuid;
        int   position = 0;
    };

    // Called AFTER the split has already occurred (original removed from the
    // tree/store, fragments constructed and inserted). originalPosition is
    // where the original mesh's UUID sat in ownerNode->meshUuids just before
    // it was removed, so undo can put it back exactly there.
    SplitByConnectivityCommand(ModelViewer*                   viewer,
                                ViewportWidget*                viewportWidget,
                                const QUuid&                   originalUuid,
                                int                             originalPosition,
                                SceneNode*                      ownerNode,
                                const QVector<FragmentEntry>&  fragments,
                                const QSet<QUuid>&              originalSelection,
                                const QString&                 text = QObject::tr("Split by Connectivity"));
    ~SplitByConnectivityCommand() override;

    void undo() override;
    void redo() override;

    int id() const override { return 14; }

    // For cleanup system - covers both the original (owned by this command
    // while split) and every fragment (owned by this command while undone).
    QSet<QUuid> getReferencedUuids() const;

private:
    QUuid                   _originalUuid;
    int                     _originalPosition;
    SceneNode*              _ownerNode;
    QVector<FragmentEntry>  _fragments;
    QSet<QUuid>             _originalSelection;
    bool                    _firstRedo;
    bool                    _split; // true = fragments live in the scene, original in the recycle bin
};
