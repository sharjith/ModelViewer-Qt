#pragma once

#include "ModelViewerCommand.h"
#include "GltfCameraData.h"

#include <QString>

/**
 * @brief Undoable command that captures the live camera pose as a new
 * named entry ("Capture View" in the Cameras tab).
 *
 * The entry is stored as a GltfCameraEntry under SceneGraph's synthetic
 * capturedViewsSourceFileKey() bucket, alongside real per-file glTF camera
 * data - not as a separate bookmark concept. This means activation,
 * deletion, MVF persistence, and glTF/GLB export all reuse the exact same
 * machinery already proven correct for authored glTF cameras (see
 * GltfCameraData.h for why). Capturing doesn't move the live camera, so
 * unlike CaptureVariantCommand this command only ever touches SceneGraph
 * state, never the viewport.
 */
class CaptureCameraCommand : public ModelViewerCommand
{
public:
    CaptureCameraCommand(ModelViewer* viewer,
        ViewportWidget* viewportWidget,
        const QString& name,
        const QString& text = QObject::tr("Add Camera View"));

    void undo() override;
    void redo() override;

private:
    GltfCameraData _oldData;
    GltfCameraData _newData;
};
