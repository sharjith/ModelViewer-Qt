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
    SetMeshUVsCommand(ModelViewer* viewer,
        ViewportWidget* viewportWidget,
        const QUuid& meshUuid,
        std::vector<Vertex> beforeVertices,
        std::vector<unsigned int> beforeIndices,
        std::vector<Vertex> afterVertices,
        std::vector<unsigned int> afterIndices,
        const QString& text = QObject::tr("Generate UVs"));

    void undo() override;
    void redo() override;

private:
    void applySnapshot(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices);

    QUuid _meshUuid;
    std::vector<Vertex> _beforeVertices;
    std::vector<unsigned int> _beforeIndices;
    std::vector<Vertex> _afterVertices;
    std::vector<unsigned int> _afterIndices;
};
