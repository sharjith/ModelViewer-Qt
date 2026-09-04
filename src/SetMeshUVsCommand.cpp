#include "SetMeshUVsCommand.h"

#include "ModelViewer.h"
#include "ViewportWidget.h"

SetMeshUVsCommand::SetMeshUVsCommand(ModelViewer* viewer,
    ViewportWidget* viewportWidget,
    const QUuid& meshUuid,
    std::vector<Vertex> beforeVertices,
    std::vector<unsigned int> beforeIndices,
    std::vector<Vertex> afterVertices,
    std::vector<unsigned int> afterIndices,
    const QString& text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _meshUuid(meshUuid)
    , _beforeVertices(std::move(beforeVertices))
    , _beforeIndices(std::move(beforeIndices))
    , _afterVertices(std::move(afterVertices))
    , _afterIndices(std::move(afterIndices))
{
}

void SetMeshUVsCommand::applySnapshot(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices)
{
    if (!_viewportWidget)
        return;
    SceneMesh* mesh = _viewportWidget->getMeshByUuid(_meshUuid);
    if (!mesh)
        return;

    // Same makeCurrent()/doneCurrent()/repaint pairing ViewportWidget::generateUVsForMeshes()
    // already established as required - setMeshData() re-uploads to the GPU internally, and
    // without releasing/forcing a repaint afterward the new UVs only became visible once
    // something else (an orbit, a resize) happened to force a redraw.
    _viewportWidget->makeCurrent();
    mesh->setMeshData(vertices, indices);
    _viewportWidget->doneCurrent();
    _viewportWidget->updateView();

    if (_viewer)
        _viewer->updateDisplayList();
}

void SetMeshUVsCommand::undo()
{
    applySnapshot(_beforeVertices, _beforeIndices);
}

void SetMeshUVsCommand::redo()
{
    applySnapshot(_afterVertices, _afterIndices);
}
