#pragma once

#include "ModelViewerCommand.h"

#include <QSet>
#include <QUuid>

/**
 * @brief Undoable command for hiding/showing a batch of measurements at
 * once - the viewport context menu's Hide/Show, NOT the Measurement
 * dialog's own per-row checkbox (that stays a plain, non-undo toggle - same
 * split mesh visibility already has between VisibilityCommand and the
 * scene-tree checkbox, see VisibilityCommand.h). Mirrors VisibilityCommand's
 * exact shape: stores full old/new sets of visible measurement ids (not
 * deltas), captures "old" automatically in the constructor from the
 * document's current state, and both undo()/redo() funnel through one
 * direct-mutator helper (ModelViewer::setMeasurementVisibilityWithoutUndo())
 * to avoid recursive undo-stack pushes.
 */
class MeasurementVisibilityCommand : public ModelViewerCommand
{
public:
    MeasurementVisibilityCommand(ModelViewer* viewer,
        ViewportWidget* viewportWidget,
        const QSet<QUuid>& newVisibleIds,
        const QString& text = QObject::tr("Measurement Visibility"));

    void undo() override;
    void redo() override;

private:
    QSet<QUuid> _oldVisibleIds;  // Visible measurements before the operation
    QSet<QUuid> _newVisibleIds;  // Visible measurements after the operation

    void applyVisibility(const QSet<QUuid>& visibleIds);
};
