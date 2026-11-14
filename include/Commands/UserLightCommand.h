#pragma once

#include "GltfLightData.h"
#include "ModelViewerCommand.h"

/**
 * @brief Undoable add or delete of a user light (a light that belongs to the scene, not to a model file).
 *
 * The light is kept by value, so undo of a delete restores it exactly, including its name and enabled flag, at its original position
 * in the list. The scene graph signals the GPU light upload; ViewportWidget::userLightsChanged() does the rest.
 */
class UserLightCommand : public ModelViewerCommand
{
public:
    enum class Kind { Add, Remove };

    UserLightCommand(ModelViewer*            viewer,
                     ViewportWidget*         viewportWidget,
                     Kind                    kind,
                     int                     index,
                     const GltfLightEntry&   entry,
                     const QString&          text);

    void undo() override;
    void redo() override;

private:
    void insertLight();
    void removeLight();

    Kind           _kind;
    int            _index;
    GltfLightEntry _entry;
};
