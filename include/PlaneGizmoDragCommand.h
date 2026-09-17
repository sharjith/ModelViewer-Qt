#pragma once

#include "ModelViewerCommand.h"

#include <functional>

// Undoable command wrapping one completed plane-gizmo drag - the Clipping
// Planes editor's 3 axis coefficients and Filter by Bounding Box's 6 face
// limits all reduce to "set one float value, with undo restoring the old
// one", so this is a single reusable command (a setter closure supplied by
// the caller) rather than one bespoke class per one of those 9 targets - same
// shape as MeasurementOffsetCommand (include/MeasurementOffsetCommand.h),
// generalized with std::function since there's no single shared ID-based
// setter to call directly the way that command has SceneGraph::
// setMeasurementOffsetDistance().
//
// The drag itself directly mutates the value every mouse-move frame (via
// PlaneGizmo::onDragged) for live preview, with no undo-stack involvement -
// this command is pushed exactly once, from PlaneGizmo::onDragFinished,
// capturing the value from before the drag started (onDragStarted) and the
// final value it settled on.
//
// Deliberately NOT pushed for direct spin-box edits (typing a value) - only
// a completed gizmo drag creates an undo step. See PlaneGizmo.h's own
// onDragStarted/onDragFinished doc comments.
class PlaneGizmoDragCommand : public ModelViewerCommand
{
public:
    PlaneGizmoDragCommand(ModelViewer* viewer,
        ViewportWidget* viewportWidget,
        std::function<void(float)> setter,
        float oldValue,
        float newValue,
        const QString& text);

    void undo() override;
    void redo() override;

    // Clipping-plane coefficients and bounding-box filter limits are
    // transient view/selection-filter state, not part of the saved document
    // model (unlike a measurement's offset) - dragging a gizmo shouldn't
    // mark the document as having unsaved changes.
    bool affectsDocument() const override { return false; }

private:
    std::function<void(float)> _setter;
    float _oldValue;
    float _newValue;
};
