#include "ApplyMaterialCommand.h"
#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "RenderableMesh.h"

ApplyMaterialCommand::ApplyMaterialCommand(ModelViewer* viewer,
    ViewportWidget* viewportWidget,
    const QVector<QUuid>& meshUuids,
    const Material& newMaterial,
    const QString& materialName,
    const QString& text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _materialName(materialName)
{
    // Capture old materials before applying new
    for (const QUuid& uuid : meshUuids)
    {
        SceneMesh* mesh = _viewportWidget->getMeshByUuid(uuid);
        if (mesh)
        {
            // Store complete material state for undo
            _oldMaterials[uuid] = mesh->getMaterial();

            // Store new material state for redo
            _newMaterials[uuid] = newMaterial;
        }
    }

    // Update command text with material name if provided
    if (!_materialName.isEmpty())
    {
        setText(QString("Set Material: %1").arg(_materialName));
    }
}

void ApplyMaterialCommand::undo()
{
    if (!_viewer || !_viewportWidget)
        return;

    applyMaterials(_oldMaterials);
}

void ApplyMaterialCommand::redo()
{
    if (!_viewer || !_viewportWidget)
        return;

    applyMaterials(_newMaterials);
}

void ApplyMaterialCommand::applyMaterials(const QMap<QUuid, Material>& materials)
{
    // Ensure the correct GL context before any GPU-backed material updates.
    if (!_viewportWidget)
    {
        qWarning() << "ApplyMaterialCommand::applyMaterials - ViewportWidget is null!";
        return;
    }

    _viewportWidget->makeCurrent();

    QSet<QString> affectedAnimatedSourceFiles;

    for (auto it = materials.begin(); it != materials.end(); ++it)
    {
        const QUuid& uuid = it.key();
        const Material& mat = it.value();

        int index = _viewportWidget->getIndexByUuid(uuid);
        if (index < 0)
        {
            // Mesh was permanently deleted, skip.
            continue;
        }

        SceneMesh* mesh = _viewportWidget->getMeshByIndex(index);
        if (!mesh)
            continue;

        // Resolve texture paths to GPU texture IDs and sampler state, then
        // make the resolved Material the authoritative mesh state.
        Material resolved = ViewportWidget::resolveMaterialTextures(_viewportWidget, mat);
        resolved.setIsGLTFMaterial(true);

        mesh->setMaterial(resolved);
        mesh->setTextureMaps(resolved);
        mesh->invertOpacityADSMap(resolved.isOpacityMapInverted());
        mesh->invertOpacityPBRMap(resolved.isOpacityMapInverted());

        if (!mesh->getSourceFile().isEmpty())
            affectedAnimatedSourceFiles.insert(mesh->getSourceFile());

        if (mat.hasTransmission() || mat.diffuseTransmissionFactor() > 0.0f)
            _viewportWidget->setTransmissionEnabled(true);
    }

    for (const QString& sourceFile : affectedAnimatedSourceFiles)
        _viewportWidget->refreshAnimationMaterialState(sourceFile);

    _viewportWidget->updateView();
    _viewportWidget->update();
}

QSet<QUuid> ApplyMaterialCommand::getReferencedUuids() const
{
    QSet<QUuid> uuids;

    for (auto it = _oldMaterials.begin(); it != _oldMaterials.end(); ++it)
    {
        uuids.insert(it.key());
    }

    return uuids;
}
