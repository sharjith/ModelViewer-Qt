#pragma once

#include "ModelViewerCommand.h"

#include <QSet>
#include <QUuid>

/**
 * @brief Undoable command for hiding/showing a batch of annotations at
 * once - the viewport context menu's Hide/Show, NOT the Annotation
 * dialog's own per-row checkbox (that stays a plain, non-undo toggle - same
 * split mesh visibility already has between VisibilityCommand and the
 * scene-tree checkbox, see VisibilityCommand.h). Mirrors
 * MeasurementVisibilityCommand's shape exactly - see that class's doc
 * comment for the full reasoning.
 */
class AnnotationVisibilityCommand : public ModelViewerCommand
{
public:
    AnnotationVisibilityCommand(ModelViewer* viewer,
        ViewportWidget* viewportWidget,
        const QSet<QUuid>& newVisibleIds,
        const QString& text = QObject::tr("Annotation Visibility"));

    void undo() override;
    void redo() override;

private:
    QSet<QUuid> _oldVisibleIds;  // Visible annotations before the operation
    QSet<QUuid> _newVisibleIds;  // Visible annotations after the operation

    void applyVisibility(const QSet<QUuid>& visibleIds);
};
