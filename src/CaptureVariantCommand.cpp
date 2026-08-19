#include "CaptureVariantCommand.h"

#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "SceneGraph.h"
#include "RenderableMesh.h"

#include <algorithm>

CaptureVariantCommand::CaptureVariantCommand(ModelViewer* viewer,
    ViewportWidget* viewportWidget,
    const QString& sourceFile,
    const QString& variantName,
    const QString& text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _sourceFile(sourceFile)
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg || !_viewportWidget || sourceFile.isEmpty())
        return;

    _oldVariantData  = sg->variantDataForFile(sourceFile);
    _oldActiveVariant = sg->activeVariantForFile(sourceFile);

    _newVariantData = _oldVariantData;
    _newVariantData.sourceFile = sourceFile;
    _newVariantIndex = _newVariantData.variantNames.size();
    _newVariantData.variantNames.append(variantName);

    for (SceneMesh* mesh : _viewportWidget->getMeshStore())
    {
        if (!mesh || mesh->getSourceFile() != sourceFile)
            continue;

        const QUuid uuid = mesh->uuid();
        _oldMappingsByMesh.insert(uuid, mesh->variantMappings());
        _oldAllVariantMaterialsByMesh.insert(uuid, mesh->allVariantMaterials());

        QMap<int, Material> newMats = mesh->allVariantMaterials();

        // Meshes that never had variant data yet have no "Default" entry
        // keyed by their original material index - synthesize one from the
        // current material so switching back to Default after this capture
        // keeps showing today's look instead of resolving to nothing.
        const int defaultKey = mesh->getOriginalMaterialIndex();
        if (defaultKey >= 0 && !newMats.contains(defaultKey))
            newMats.insert(defaultKey, mesh->getMaterial());

        int newKey = defaultKey;
        for (auto it = newMats.constBegin(); it != newMats.constEnd(); ++it)
            newKey = std::max(newKey, it.key());
        newKey += 1;

        newMats.insert(newKey, mesh->getMaterial());

        QVector<GltfVariantMapping> newMappings = mesh->variantMappings();
        GltfVariantMapping mapping;
        mapping.materialIndex = newKey;
        mapping.variantIndices.append(_newVariantIndex);
        newMappings.append(mapping);

        _newMappingsByMesh.insert(uuid, newMappings);
        _newAllVariantMaterialsByMesh.insert(uuid, newMats);

        if (mesh->getSceneIndex() >= 0)
            _newVariantData.meshVariantMappings.insert(mesh->getSceneIndex(), newMappings);
    }
}

void CaptureVariantCommand::undo()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg || !_viewportWidget)
        return;

    for (auto it = _oldMappingsByMesh.cbegin(); it != _oldMappingsByMesh.cend(); ++it)
    {
        if (SceneMesh* mesh = _viewportWidget->getMeshByUuid(it.key()))
        {
            mesh->setVariantMappings(it.value());
            mesh->setAllVariantMaterials(_oldAllVariantMaterialsByMesh.value(it.key()));
        }
    }

    if (_oldVariantData.isEmpty())
        sg->clearVariantData(_sourceFile);
    else
        sg->setVariantData(_sourceFile, _oldVariantData);

    _viewer->applyVariant(_sourceFile, _oldActiveVariant);
}

void CaptureVariantCommand::redo()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg || !_viewportWidget)
        return;

    for (auto it = _newMappingsByMesh.cbegin(); it != _newMappingsByMesh.cend(); ++it)
    {
        if (SceneMesh* mesh = _viewportWidget->getMeshByUuid(it.key()))
        {
            mesh->setVariantMappings(it.value());
            mesh->setAllVariantMaterials(_newAllVariantMaterialsByMesh.value(it.key()));
        }
    }

    sg->setVariantData(_sourceFile, _newVariantData);
    _viewer->applyVariant(_sourceFile, _newVariantIndex);

    if (_viewer)
        _viewer->setDocumentModified(true);
}

QSet<QUuid> CaptureVariantCommand::getReferencedUuids() const
{
    QSet<QUuid> uuids;
    for (auto it = _newMappingsByMesh.cbegin(); it != _newMappingsByMesh.cend(); ++it)
        uuids.insert(it.key());
    return uuids;
}
