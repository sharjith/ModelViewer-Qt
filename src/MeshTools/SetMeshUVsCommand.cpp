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
    std::vector<quint64> beforeSourceMeshIds,
    std::vector<quint64> afterSourceMeshIds,
    const QString& text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _meshUuid(meshUuid)
    , _beforeVertices(std::move(beforeVertices))
    , _beforeIndices(std::move(beforeIndices))
    , _afterVertices(std::move(afterVertices))
    , _afterIndices(std::move(afterIndices))
    , _beforeSourceMeshIds(std::move(beforeSourceMeshIds))
    , _afterSourceMeshIds(std::move(afterSourceMeshIds))
{
}

void SetMeshUVsCommand::applySnapshot(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices,
    const std::vector<quint64>& sourceMeshIds)
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
    // No sourceVertexMap here: this is a straight swap to a PREVIOUSLY-
    // CAPTURED, already-consistent vertex/id pair (not a fresh remap the way
    // UV generation's own live mutation is), so setMeshData() would have no
    // way to remap anyway - restore sourceMeshIds directly afterward instead
    // (empty is a no-op, matching setPrecomputedSourceMeshIds()'s own guard).
    mesh->setMeshData(vertices, indices);
    if (!sourceMeshIds.empty())
        mesh->setPrecomputedSourceMeshIds(sourceMeshIds);
    _viewportWidget->doneCurrent();
    _viewportWidget->updateView();

    if (_viewer)
        _viewer->updateDisplayList();
}

void SetMeshUVsCommand::undo()
{
    applySnapshot(_beforeVertices, _beforeIndices, _beforeSourceMeshIds);
}

void SetMeshUVsCommand::redo()
{
    applySnapshot(_afterVertices, _afterIndices, _afterSourceMeshIds);
}
