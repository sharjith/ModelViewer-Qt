#include "SelectionCommand.h"
#include "ModelViewer.h"
#include "ViewportWidget.h"

SelectionCommand::SelectionCommand(ModelViewer* viewer,
    ViewportWidget* viewportWidget,
    const QSet<int>& newSelection,
    const QString& text,
    const void* mergeSource)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _newSelection(newSelection)
    , _mergeSource(mergeSource)
{
    // Capture the current selection state before the change
    std::vector<int> currentIDs = _viewer->getSelectedIDs();
    _oldSelection = QSet<int>(currentIDs.begin(), currentIDs.end());
}

void SelectionCommand::undo()
{
    applySelection(_oldSelection);
}

void SelectionCommand::redo()
{
    applySelection(_newSelection);
}

void SelectionCommand::applySelection(const QSet<int>& selection)
{
    if (!_viewer || !_viewportWidget)
        return;

    // Use the non-undo version to prevent recursion
    _viewer->setSelectionWithoutUndo(selection);
}

bool SelectionCommand::mergeWith(const QUndoCommand* other)
{
    // Enabled for the Filter by Material/Color dialogs' live preview
    // (FilterByMaterialDialog/FilterByColorDialog): each criteria tweak
    // pushes a real SelectionCommand rather than bypassing undo, but
    // QUndoStack::push() only ever tries to merge against the command
    // already on top of the stack, so this only ever coalesces an
    // unbroken run of consecutive selection changes - any other command
    // pushed in between (e.g. a VisibilityCommand from Show Only/Hide)
    // breaks the chain and the next selection change starts a fresh
    // entry. Net effect: an entire live-filtering session collapses into
    // one undo step back to whatever was selected before it started.
    //
    // Gated on _mergeSource (see header) so this ONLY ever fires between
    // two pushes from the same live-filter dialog instance - a plain
    // click/lasso/sweep selection always carries mergeSource == nullptr
    // and never merges with anything, so today's one-undo-step-per-click
    // behavior elsewhere in the app is unaffected.
    if (other->id() != id())
        return false;

    const SelectionCommand* otherCmd = static_cast<const SelectionCommand*>(other);
    if (!_mergeSource || _mergeSource != otherCmd->_mergeSource)
        return false;

    _newSelection = otherCmd->_newSelection;
    return true;
}
