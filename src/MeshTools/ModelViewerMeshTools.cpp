#include "ModelViewer.h"
#include "ToolsToolbar.h"

QMap<QString, QString> ModelViewer::meshToolDisabledReasons() const
{
    // Resolve assemblies and deduplicate descendants without changing selection.
    const auto selected = treeWidgetModel->selectedMeshUuids();
    int count = 0;
    GLenum primitive = GL_TRIANGLES;
    bool samePrimitive = true;
    bool triangles = true;
    bool invalid = false;
    for (const QUuid& uuid : selected) {
        SceneMesh* mesh = _viewportWidget->getMeshByUuid(uuid);
        if (!mesh || !_sceneGraph->findNodeForMesh(uuid)) { invalid = true; continue; }
        if (count == 0) primitive = mesh->getPrimitiveMode();
        samePrimitive &= primitive == mesh->getPrimitiveMode();
        triangles &= mesh->getPrimitiveMode() == GL_TRIANGLES && !mesh->indices().empty();
        ++count;
    }
    const QString one = invalid ? tr("Some selected meshes are not available yet.")
        : count == 0 ? tr("Select at least one mesh.") : QString();
    const QString two = !one.isEmpty() ? one : count < 2 ? tr("Select at least two meshes.") : QString();
    const QString merge = !two.isEmpty() ? two : !samePrimitive
        ? tr("Select meshes with the same primitive type.") : QString();
    const QString split = !one.isEmpty() ? one : !triangles
        ? tr("Select only triangle meshes\nwith indexed geometry.") : QString();
    const QString booleanUnion = !two.isEmpty() ? two : !triangles
        ? tr("Select only triangle meshes\nwith indexed geometry.") : QString();
    // explicitlySelectedAssemblyNodes(), NOT selectedAssemblyNodes(): the
    // tree auto-selects a parent whenever all its direct children are
    // selected (see SceneTreeWidget::onItemSelectionChanged()), so
    // selecting every mesh leaf in a multi-mesh assembly would otherwise
    // read identically to explicitly selecting the assembly itself and
    // wrongly disable Duplicate, even though duplicateSelectedItems()
    // already handles that exact leaf set correctly via selectedMeshUuids().
    bool containsMultiMeshAssembly = false;
    for (const SceneNode* node : treeWidgetModel->explicitlySelectedAssemblyNodes()) {
        if (_sceneGraph->collectMeshUuids(node).size() > 1) {
            containsMultiMeshAssembly = true;
            break;
        }
    }
    const QString duplicate = !one.isEmpty() ? one : containsMultiMeshAssembly
        ? tr("Select mesh leaves or single-mesh assemblies.\nUse Copy and Paste for a multi-mesh assembly.") : QString();
    return {{"split_by_connectivity", split}, {"merge_selected", merge},
        {"merge_by_adjacency", merge}, {"mesh_union", booleanUnion},
        {"group_meshes", one}, {"duplicate_meshes", duplicate}, {"mesh_info", one}};
}

void ModelViewer::updateMeshTools()
{
    _viewportWidget->getToolsToolbar()->setMeshToolAvailability(meshToolDisabledReasons());
}

bool ModelViewer::executeMeshToolCommand(const QString& command)
{
    static const QSet<QString> meshCommands = {"split_by_connectivity", "merge_selected",
        "merge_by_adjacency", "mesh_union", "group_meshes", "duplicate_meshes", "mesh_info"};
    if (!meshCommands.contains(command)) return false;
    const auto reasons = meshToolDisabledReasons();
    const auto found = reasons.constFind(command);
    if (found == reasons.cend()) return false;
    // Revalidate at invocation; a queued visual refresh may not have run yet.
    if (!found.value().isEmpty()) { updateMeshTools(); return true; }
    if (command == "split_by_connectivity") splitSelectedMeshesByConnectivity();
    else if (command == "merge_selected") mergeSelectedMeshes();
    else if (command == "merge_by_adjacency") mergeSelectedMeshesByAdjacency();
    else if (command == "mesh_union") unionSelectedMeshes();
    else if (command == "group_meshes") groupSelectedMeshes();
    else if (command == "duplicate_meshes") duplicateSelectedItems();
    else if (command == "mesh_info") displaySelectedMeshInfo();
    updateMeshTools();
    return true;
}
