#include "SetDefaultVariantCommand.h"

#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "SceneGraph.h"
#include "RenderableMesh.h"

#include <algorithm>

SetDefaultVariantCommand::SetDefaultVariantCommand(ModelViewer* viewer,
    ViewportWidget* viewportWidget,
    const QString& sourceFile,
    int variantIndex,
    const QString& text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _sourceFile(sourceFile)
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg || !_viewportWidget || sourceFile.isEmpty() || variantIndex < 0)
        return;

    _oldActiveVariant = sg->activeVariantForFile(sourceFile);
    _oldVariantData = sg->variantDataForFile(sourceFile);
    _newVariantData = _oldVariantData;
    _newVariantData.sourceFile = sourceFile;

    for (SceneMesh* mesh : _viewportWidget->getMeshStore())
    {
        if (!mesh || mesh->getSourceFile() != sourceFile || !mesh->hasVariants())
            continue;

        const Material* variantMat = mesh->materialForVariant(variantIndex);
        const int defaultKey = mesh->getOriginalMaterialIndex();
        if (!variantMat || defaultKey < 0)
            continue;

        const QUuid uuid = mesh->uuid();
        _oldAllVariantMaterialsByMesh.insert(uuid, mesh->allVariantMaterials());
        _oldMappingsByMesh.insert(uuid, mesh->variantMappings());

        QMap<int, Material> newMats = mesh->allVariantMaterials();
        QVector<GltfVariantMapping> newMappings = mesh->variantMappings();

        // De-alias: any OTHER variant mapping currently pointing at
        // defaultKey shares that slot with Default and needs its own
        // independent copy before we overwrite it below. Skip the mapping
        // for the variant being promoted itself - it's about to become
        // identical to Default anyway, no aliasing concern there.
        bool mappingsChanged = false;
        for (GltfVariantMapping& vm : newMappings)
        {
            if (vm.materialIndex != defaultKey || vm.variantIndices.contains(variantIndex))
                continue;

            int freshKey = defaultKey;
            for (auto it = newMats.constBegin(); it != newMats.constEnd(); ++it)
                freshKey = std::max(freshKey, it.key());
            freshKey += 1;

            newMats.insert(freshKey, newMats.value(defaultKey));
            vm.materialIndex = freshKey;
            mappingsChanged = true;
        }

        newMats[defaultKey] = *variantMat;

        _newAllVariantMaterialsByMesh.insert(uuid, newMats);
        _newMappingsByMesh.insert(uuid, newMappings);

        if (mappingsChanged && mesh->getSceneIndex() >= 0)
            _newVariantData.meshVariantMappings.insert(mesh->getSceneIndex(), newMappings);
    }
}

void SetDefaultVariantCommand::undo()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg || !_viewportWidget)
        return;

    for (auto it = _oldAllVariantMaterialsByMesh.cbegin(); it != _oldAllVariantMaterialsByMesh.cend(); ++it)
    {
        if (SceneMesh* mesh = _viewportWidget->getMeshByUuid(it.key()))
            mesh->setAllVariantMaterials(it.value());
    }
    for (auto it = _oldMappingsByMesh.cbegin(); it != _oldMappingsByMesh.cend(); ++it)
    {
        if (SceneMesh* mesh = _viewportWidget->getMeshByUuid(it.key()))
            mesh->setVariantMappings(it.value());
    }

    sg->setVariantData(_sourceFile, _oldVariantData);

    // Re-apply whatever variant is currently active so the viewport
    // reflects the restored materials immediately.
    _viewer->applyVariant(_sourceFile, _oldActiveVariant);
}

void SetDefaultVariantCommand::redo()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg || !_viewportWidget)
        return;

    for (auto it = _newAllVariantMaterialsByMesh.cbegin(); it != _newAllVariantMaterialsByMesh.cend(); ++it)
    {
        if (SceneMesh* mesh = _viewportWidget->getMeshByUuid(it.key()))
            mesh->setAllVariantMaterials(it.value());
    }
    for (auto it = _newMappingsByMesh.cbegin(); it != _newMappingsByMesh.cend(); ++it)
    {
        if (SceneMesh* mesh = _viewportWidget->getMeshByUuid(it.key()))
            mesh->setVariantMappings(it.value());
    }

    sg->setVariantData(_sourceFile, _newVariantData);

    // setVariantData() always resets the active variant to -1 (Default) -
    // restore whatever was actually active before this command ran.
    _viewer->applyVariant(_sourceFile, _oldActiveVariant);

    _viewer->setDocumentModified(true);
}
