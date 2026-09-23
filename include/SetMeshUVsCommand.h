#pragma once

#include "ModelViewerCommand.h"
#include "SceneMesh.h"

#include <QUuid>
#include <vector>

// ---------------------------------------------------------------------------
// SetMeshUVsCommand
//
// Undoable UV generation. UVGenerator's methods mutate a SceneMesh's vertex/
// index data in place (SceneMesh::setMeshData()) rather than creating a new
// result node the way Shrink Wrap/Subdivision do, so unlike ShrinkWrapCommand
// there's no node to attach/detach here - this instead snapshots the mesh's
// full vertex/index arrays both BEFORE and AFTER generation (captured by the
// caller, UVGenerationDialog::onGenerateClicked(), since the mutation has
// already happened by the time this command is constructed/pushed - same
// "already happened, command just replays it" convention as
// ShrinkWrapCommand/GroupMeshesCommand) and swaps between them on
// undo()/redo() via another setMeshData() call. Several UV methods explode
// vertex count up to 3x (one vertex per triangle-corner), so each entry
// holds a real, non-trivial copy of the mesh's data - an accepted, normal
// undo-stack memory cost, not a shortcut being taken.
// ---------------------------------------------------------------------------
class SetMeshUVsCommand : public ModelViewerCommand
{
public:
    // beforeSourceMeshIds/afterSourceMeshIds: the mesh's SceneMesh::
    // getSourceMeshIds() snapshotted by the caller at the same two moments
    // as the vertex/index arrays above (before the live mutation, and after
    // it) - setMeshData() itself only remaps an EXISTING id array through a
    // sourceVertexMap, it cannot reconstruct one it was never given, so
    // without these an undo/redo swap would silently drop provenance on a
    // mesh that had it before UV generation ever ran. Empty is the common
    // (never-merged) case and behaves exactly as before this parameter
    // existed.
    SetMeshUVsCommand(ModelViewer* viewer,
        ViewportWidget* viewportWidget,
        const QUuid& meshUuid,
        std::vector<Vertex> beforeVertices,
        std::vector<unsigned int> beforeIndices,
        std::vector<Vertex> afterVertices,
        std::vector<unsigned int> afterIndices,
        std::vector<quint64> beforeSourceMeshIds = {},
        std::vector<quint64> afterSourceMeshIds = {},
        const QString& text = QObject::tr("Generate UVs"));

    void undo() override;
    void redo() override;

private:
    void applySnapshot(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices,
        const std::vector<quint64>& sourceMeshIds);

    QUuid _meshUuid;
    std::vector<Vertex> _beforeVertices;
    std::vector<unsigned int> _beforeIndices;
    std::vector<Vertex> _afterVertices;
    std::vector<unsigned int> _afterIndices;
    std::vector<quint64> _beforeSourceMeshIds;
    std::vector<quint64> _afterSourceMeshIds;
};
