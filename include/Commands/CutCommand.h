#pragma once

#include "ModelViewerCommand.h"
#include "SceneClipboard.h"

#include <QList>
#include <QSet>
#include <QUuid>

// ---------------------------------------------------------------------------
// CutCommand
//
// Marks selected items as "cut" — grays them in the tree and populates the
// clipboard.  Does NOT remove items from the scene; that happens inside
// PasteCommand when the user pastes.
//
// undo  → clears cut marks and the clipboard (items look normal again).
// redo  → re-applies cut marks and restores the clipboard.
//
// Because nothing is moved to the recycle bin, getReferencedUuids() always
// returns an empty set and the cleanup system ignores this command.
// ---------------------------------------------------------------------------
class CutCommand : public ModelViewerCommand
{
public:
    CutCommand(ModelViewer*                 viewer,
               ViewportWidget*                    viewportWidget,
               const QList<ClipboardEntry>& entries,
               const QSet<QUuid>&           cutMeshUuids,
               const QSet<QUuid>&           cutNodeUuids,
               const QString&               text = QObject::tr("Cut"));

    void undo() override;
    void redo() override;
    bool affectsDocument() const override { return false; }

    int id() const override { return 9; }

    QSet<QUuid> getReferencedUuids() const { return {}; }

private:
    QList<ClipboardEntry> _entries;
    QSet<QUuid>           _cutMeshUuids;
    QSet<QUuid>           _cutNodeUuids;
    bool                  _firstRedo;
    // Captured at construction (ModelViewer::currentClipboardGeneration()).
    // Threaded through to clearCutMarks()/reapplyCutMarks() so this command
    // is a no-op if a different document has since taken over the shared
    // clipboard - see ModelViewer::s_clipboardGeneration's doc comment.
    quint64               _generation;
};
