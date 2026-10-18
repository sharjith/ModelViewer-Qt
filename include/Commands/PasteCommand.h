#pragma once

#include "ModelViewerCommand.h"
#include "SceneClipboard.h"
#include "SceneNode.h"

#include <QList>
#include <QSet>
#include <QUuid>

// ---------------------------------------------------------------------------
// PasteCommand
//
// Undoable command for the Paste operation.  Handles two flavours:
//
//   Copy-paste (isCut == false):
//     Cloned meshes/subtrees are inserted at the destination.  On undo they
//     are moved to the recycle bin; on redo they are restored from it.
//     PasteCommand owns detached Subtree SceneNode* roots while undone.
//
//   Cut-paste (isCut == true):
//     Items are moved within the scene (no cloning, no recycle bin).  On
//     undo they are moved back to their original location and the cut marks
//     are re-applied; on redo the move is re-executed and marks are cleared.
//     SceneNode* ownership never leaves the SceneGraph.
// ---------------------------------------------------------------------------
class PasteCommand : public ModelViewerCommand
{
public:
    struct PastedItem
    {
        enum Type { Mesh, Subtree };
        Type type  = Mesh;
        bool isCut = false;  // true = move (no recycle-bin lifecycle)

        // --- Copy/Cut Mesh: destination ---
        QUuid      meshUuid;
        SceneNode* ownerNode    = nullptr;
        int        meshPosition = 0;

        // --- Cut Mesh: source ---
        SceneNode* srcOwnerNode    = nullptr;
        int        srcMeshPosition = 0;

        // --- Copy/Cut Subtree: destination ---
        SceneNode*   subtreeRoot      = nullptr;
        SceneNode*   subtreeParent    = nullptr;
        int          childPosition    = 0;
        QList<QUuid> subtreeMeshUuids;   // all UUIDs in subtree (for selection / bin)

        // --- Cut Subtree: source ---
        SceneNode* srcSubtreeParent = nullptr;
        int        srcChildPosition = 0;
    };

    // cutEntries: the clipboard snapshot at paste time, stored so that
    // undo can restore both the visual marks and the clipboard state.
    // Pass an empty list for copy-paste.
    PasteCommand(ModelViewer*                 viewer,
                 ViewportWidget*                    viewportWidget,
                 const QList<PastedItem>&     items,
                 const QSet<QUuid>&           originalSelection,
                 const QList<ClipboardEntry>& cutEntries = {},
                 const QString&               text = QObject::tr("Paste"));

    ~PasteCommand() override;

    void undo() override;
    void redo() override;

    int id() const override { return 8; }

    QSet<QUuid> getReferencedUuids() const;

private:
    QList<PastedItem>     _items;
    QSet<QUuid>           _originalSelection;
    QList<ClipboardEntry> _cutEntries;    // non-empty iff this was a cut-paste
    QSet<QUuid>           _cutMeshUuids;  // derived from cut items (for mark re-apply)
    QSet<QUuid>           _cutNodeUuids;
    bool                  _firstRedo;
    bool                  _inserted;     // true when copy-paste items are in the scene
    // Captured at construction (ModelViewer::currentClipboardGeneration()).
    // Threaded through to clearCutMarks()/reapplyCutMarks() (only relevant
    // when _cutEntries is non-empty) so undo/redo is a no-op on the
    // clipboard if a different document has since taken it over.
    quint64               _generation;

    static void freeSubtree(SceneNode* root);
};
