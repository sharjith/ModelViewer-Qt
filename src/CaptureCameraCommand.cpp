#include "CaptureCameraCommand.h"

#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "SceneGraph.h"

CaptureCameraCommand::CaptureCameraCommand(ModelViewer* viewer,
    ViewportWidget* viewportWidget,
    const QString& name,
    const QString& text)
    : ModelViewerCommand(viewer, viewportWidget, text)
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg || !_viewportWidget)
        return;

    const QString pseudoFile = capturedViewsSourceFileKey();

    _oldData = sg->gltfCameraDataForFile(pseudoFile);
    _oldData.sourceFile = pseudoFile;

    _newData = _oldData;
    _newData.cameras.append(_viewportWidget->captureCurrentCameraEntry(name));
}

void CaptureCameraCommand::undo()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg)
        return;

    if (_oldData.isEmpty())
        sg->clearGltfCameraData(capturedViewsSourceFileKey());
    else
        sg->setGltfCameraData(capturedViewsSourceFileKey(), _oldData);
}

void CaptureCameraCommand::redo()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg)
        return;

    sg->setGltfCameraData(capturedViewsSourceFileKey(), _newData);

    if (_viewer)
        _viewer->setDocumentModified(true);
}
