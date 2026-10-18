#pragma once

#include "ModelViewerCommand.h"
#include "MeasurementData.h"

/**
 * @brief Undoable command that adds one Measurement (Point or Distance) to
 * SceneGraph's document-level list, pushed when a "Measure" tool click
 * completes a measurement (see ViewportWidget::handleMeasurementClick()).
 */
class AddMeasurementCommand : public ModelViewerCommand
{
public:
    AddMeasurementCommand(ModelViewer* viewer,
        ViewportWidget* viewportWidget,
        const Measurement& measurement,
        const QString& text = QObject::tr("Add Measurement"));

    void undo() override;
    void redo() override;

private:
    Measurement _measurement;
};
