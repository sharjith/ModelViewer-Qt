#pragma once

#include "ModelViewerCommand.h"
#include "SceneNode.h"

#include <QSet>
#include <QUuid>

// ---------------------------------------------------------------------------
// ShrinkWrapCommand
//
// Undoable command for the Shrink Wrap operation: a new SceneNode (named
// "Shrink Wrap") holding one newly-created SceneMesh (CGAL alpha_wrap_3's
// output over the selected meshes' baked world-space geometry) is added to
// the tree. Unlike Merge/Group, the selected source meshes are left
// completely untouched - this is pure addition, not a replace/reorganize.
//
// Combines GroupMeshesCommand's "own a detached new SceneNode, attach/detach
// from its parent" pattern with DuplicateCommand's "own a new mesh sitting
// in the recycle bin while undone" pattern - the closest two precedents,
// since nothing existing already covers "add one brand-new node+mesh, undo
// removes only that."
//
// Undo  -> wrapped mesh moved to the recycle bin, wrap node detached from
//          its parent, original selection restored.
// Redo  -> wrap node re-attached at its original (parent, position); the
//          wrapped mesh restored from the recycle bin into it, selected.
// ---------------------------------------------------------------------------
class ShrinkWrapCommand : public ModelViewerCommand
{
public:
    // Called AFTER the wrap node has already been created, attached to
    // wrapParent at wrapPosition, and the wrapped mesh inserted into it -
    // mirrors GroupMeshesCommand/DuplicateCommand's "already happened,
    // command just replays it" convention.
    ShrinkWrapCommand(ModelViewer*       viewer,
                       ViewportWidget*    viewportWidget,
                       SceneNode*         wrapNode,
                       SceneNode*         wrapParent,
                       int                wrapPosition,
                       const QUuid&       wrappedMeshUuid,
                       const QSet<QUuid>& originalSelection,
                       const QString&     text = QObject::tr("Shrink Wrap"));
    ~ShrinkWrapCommand() override;

    void undo() override;
    void redo() override;

    int id() const override { return 17; } // next free id after GroupMeshesCommand's 16

    QSet<QUuid> getReferencedUuids() const;

private:
    SceneNode*  _wrapNode;
    SceneNode*  _wrapParent;
    int         _wrapPosition;
    QUuid       _wrappedMeshUuid;
    QSet<QUuid> _originalSelection;
    bool        _firstRedo;
    bool        _attached; // true = wrap node is live in the tree and owns the mesh
};
