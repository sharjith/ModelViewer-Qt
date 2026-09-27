#pragma once

#include "ModelViewerCommand.h"
#include "SceneNode.h"

#include <QSet>
#include <QUuid>
#include <QVector>

// ---------------------------------------------------------------------------
// DuplicateCommand
//
// Undoable command for duplicating one or more mesh leaves in the tree.
// Each duplicate is inserted immediately after its original in the same
// SceneNode so the tree hierarchy is preserved.
//
// Undo  → duplicates moved to recycle bin, original selection restored.
// Redo  → duplicates restored from recycle bin and re-inserted in the tree.
// ---------------------------------------------------------------------------
class DuplicateCommand : public ModelViewerCommand
{
public:
    // Per-duplicate record: the new UUID, the node it was inserted into, and
    // the position within that node's meshUuids list.
    struct DuplicateEntry
    {
        QUuid      uuid;
        SceneNode* ownerNode = nullptr;
        int        position  = 0;
    };

    // Called AFTER duplication has already occurred.
    // entries        — one record per created clone (uuid + tree placement).
    // originalSelection — UUIDs selected before duplication (for undo restore).
    DuplicateCommand(ModelViewer*                    viewer,
                     ViewportWidget*                 viewportWidget,
                     const QVector<DuplicateEntry>&  entries,
                     const QSet<QUuid>&              originalSelection,
                     const QString&                  text = QObject::tr("Duplicate"));
    ~DuplicateCommand() override;

    void undo() override;
    void redo() override;

    int id() const override { return 7; }

    QSet<QUuid> getReferencedUuids() const;

private:
    QVector<DuplicateEntry> _entries;
    QSet<QUuid>             _originalSelection;
    bool                    _firstRedo;
    bool                    _inserted; // true when dupes are in the scene
};
