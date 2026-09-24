#pragma once

#include "ModelViewerCommand.h"
#include "SceneNode.h"

#include <QSet>
#include <QString>
#include <QUuid>
#include <QVector>

// ---------------------------------------------------------------------------
// PurgeRedundantNodesCommand
//
// Undoable command for the "purge" operation: a sub-assembly node that exists
// solely to wrap a single mesh - one child, no meshes of its own, and that
// child has no children and exactly one mesh - is redundant clutter (typical
// of a STEP/IGES import's own assembly structure). Promoting the mesh into
// the wrapper collapses it away, one level at a time; run bottom-up, this
// naturally collapses a whole CHAIN of such wrappers in one pass, not just
// the innermost one. No geometry is touched, no SceneMesh is created or
// destroyed - purely scene-tree reorganization, the hierarchy-purging sibling
// to GroupMeshesCommand (which does the opposite: wraps meshes INTO a new
// node).
//
// Each promotion keeps the SURVIVING (absorbing) node's own identity - it is
// the node that already sat one level up, not a newly-created one - and
// takes on the ELIMINATED child's one mesh, renamed to the survivor's own
// name (a sub-assembly's name is usually more meaningful than a generic
// single-part child's). The eliminated node itself is removed from the tree.
//
// Ownership while undone mirrors GroupMeshesCommand/PasteCommand: each
// eliminated SceneNode* is owned by this command until redo() re-eliminates
// it (or the command is destroyed, which frees any still-eliminated ones).
// ---------------------------------------------------------------------------
class PurgeRedundantNodesCommand : public ModelViewerCommand
{
public:
    // One collapsed wrapper: survivorNode keeps its identity and gains the mesh; eliminatedNode (survivorNode's
    // former only child) is removed from the tree.
    struct PromotionEntry
    {
        QUuid      meshUuid;
        SceneNode* survivorNode        = nullptr; // absorbs the mesh; stays attached to the tree throughout
        SceneNode* eliminatedNode      = nullptr; // was survivorNode's only child; detached, command-owned while eliminated
        int        eliminatedPosition  = 0;       // index eliminatedNode held in survivorNode->children
        QString    meshNameBefore;                // the mesh's own name, before this promotion
        QString    meshNameAfter;                 // survivorNode's name at the moment of promotion
    };

    // Called AFTER every promotion in `promotions` has already happened (recorded bottom-up: innermost collapse
    // first) - mirrors GroupMeshesCommand/DuplicateCommand's "already happened, command just replays it" convention.
    PurgeRedundantNodesCommand(ModelViewer*                      viewer,
                                ViewportWidget*                   viewportWidget,
                                const QVector<PromotionEntry>&    promotions,
                                const QSet<QUuid>&                originalSelection,
                                const QString&                    text = QObject::tr("Purge Redundant Nodes"));
    ~PurgeRedundantNodesCommand() override;

    void undo() override;
    void redo() override;

    int id() const override { return 22; } // next free id after DeleteSceneStateCommand's 21

    // For the cleanup system - every mesh this command renamed (never recycle-binned, just relocated/renamed).
    QSet<QUuid> getReferencedUuids() const;

private:
    QVector<PromotionEntry> _promotions; // recorded order (bottom-up); undo() replays in reverse
    QSet<QUuid>              _originalSelection;
    bool                      _firstRedo;
    bool                      _applied; // true = promotions are currently in effect (eliminated nodes are detached, command-owned)
};
