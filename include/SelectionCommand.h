#pragma once

#include "ModelViewerCommand.h"
#include <QSet>

/**
 * @brief Undoable command for mesh selection changes
 *
 * Captures the old and new selection states and can restore either.
 * Supports both single selection and multi-selection operations.
 */
class SelectionCommand : public ModelViewerCommand
{
public:
    /**
     * @brief Construct a selection command
     * @param viewer The ModelViewer instance
     * @param glWidget The ViewportWidget instance
     * @param newSelection The new selection set (mesh IDs)
     * @param text Description (default: "Select")
     * @param mergeSource Opaque tag identifying the UI session that produced
     *        this change (e.g. a live-filter dialog's `this` pointer). Only
     *        commands sharing the same non-null mergeSource ever merge - see
     *        mergeWith(). Defaults to null, meaning "never merges," which is
     *        what every plain click/lasso/sweep selection already wants.
     */
    SelectionCommand(ModelViewer* viewer,
        ViewportWidget* viewportWidget,
        const QSet<int>& newSelection,
        const QString& text = QObject::tr("Select"),
        const void* mergeSource = nullptr);

    void undo() override;
    void redo() override;
    bool affectsDocument() const override { return false; }

    /**
     * @brief Command ID for merging
     * @return Unique ID for SelectionCommand (1)
     */
    int id() const override { return 1; }

    /**
     * @brief Merge with another command
     * @param other The command to potentially merge with
     * @return true if merged, false otherwise
     *
     * Only merges when both commands carry the same non-null mergeSource -
     * a plain click/lasso/sweep selection (mergeSource == nullptr) never
     * merges with anything, preserving today's one-undo-step-per-click
     * behavior. A live-filter dialog tags every selection push it makes with
     * its own `this` pointer so a whole tweak-the-slider session collapses
     * into one undo step, without affecting selection changes made anywhere
     * else.
     */
    bool mergeWith(const QUndoCommand* other) override;

private:
    QSet<int> _oldSelection;  // Selection state before command
    QSet<int> _newSelection;  // Selection state after command
    const void* _mergeSource = nullptr;

    /**
     * @brief Apply a selection set
     * @param selection The set of mesh IDs to select
     */
    void applySelection(const QSet<int>& selection);
};

