#include "MetadataDeleteCommand.h"

#include "ViewportWidget.h"
#include "ModelViewer.h"
#include "SceneGraph.h"
#include "RenderableMesh.h"

#include <algorithm>

namespace
{
QVector<GltfVariantMapping> remapVariantMappings(const QVector<GltfVariantMapping>& mappings,
                                                 int removedVariantIndex)
{
    QVector<GltfVariantMapping> remapped;
    remapped.reserve(mappings.size());

    for (GltfVariantMapping mapping : mappings)
    {
        QVector<int> remappedIndices;
        remappedIndices.reserve(mapping.variantIndices.size());
        for (int idx : mapping.variantIndices)
        {
            if (idx == removedVariantIndex)
                continue;
            remappedIndices.append(idx > removedVariantIndex ? idx - 1 : idx);
        }

        if (!remappedIndices.isEmpty())
        {
            mapping.variantIndices = remappedIndices;
            remapped.append(mapping);
        }
    }

    return remapped;
}
}

MetadataDeleteCommand::MetadataDeleteCommand(ModelViewer* viewer,
                                             ViewportWidget* viewportWidget,
                                             Kind kind,
                                             const QString& sourceFile,
                                             int index,
                                             const QString& text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _kind(kind)
    , _sourceFile(sourceFile)
    , _index(index)
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg || !_viewportWidget || _sourceFile.isEmpty())
        return;

    if (_kind == Kind::Animation)
    {
        _oldAnimationData = sg->animationDataForFile(_sourceFile);
        _oldSceneGraphActiveClip = sg->activeAnimationClipForFile(_sourceFile);
        _oldAnimationState.file = _viewportWidget->activeAnimationFile();
        _oldAnimationState.clipIndex = _viewportWidget->activeAnimationClip();
        _oldAnimationState.timeSeconds = _viewportWidget->currentAnimationTimeSeconds();
        _oldAnimationState.playing = _viewportWidget->isAnimationPlaying();
    }
    else if (_kind == Kind::Camera)
    {
        _oldCameraData = sg->gltfCameraDataForFile(_sourceFile);
        _oldCameraState.file = _viewportWidget->activeGltfCameraFile();
        _oldCameraState.cameraIndex = _viewportWidget->activeGltfCameraIndex();
    }
    else if (_kind == Kind::Variant)
    {
        _oldVariantData = sg->variantDataForFile(_sourceFile);
        _oldActiveVariant = sg->activeVariantForFile(_sourceFile);
        for (SceneMesh* mesh : _viewportWidget->getMeshStore())
        {
            if (!mesh || mesh->getSourceFile() != _sourceFile)
                continue;
            _oldVariantMappingsByMesh.insert(mesh->uuid(), mesh->variantMappings());
        }
    }
}

void MetadataDeleteCommand::undo()
{
    switch (_kind)
    {
    case Kind::Animation:
        undoAnimationDelete();
        break;
    case Kind::Camera:
        undoCameraDelete();
        break;
    case Kind::Variant:
        undoVariantDelete();
        break;
    }
}

void MetadataDeleteCommand::redo()
{
    switch (_kind)
    {
    case Kind::Animation:
        redoAnimationDelete();
        break;
    case Kind::Camera:
        redoCameraDelete();
        break;
    case Kind::Variant:
        redoVariantDelete();
        break;
    }

    if (_viewer)
        _viewer->setDocumentModified(true);
}

void MetadataDeleteCommand::redoAnimationDelete()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg || !_viewportWidget || _sourceFile.isEmpty())
        return;

    if (_index < 0)
    {
        sg->clearAnimationData(_sourceFile);
        _viewportWidget->clearAnimationRuntimeForFile(_sourceFile);
        return;
    }

    GltfAnimationData data = sg->animationDataForFile(_sourceFile);
    if (_index >= data.clips.size())
        return;

    const QString activeFile = _viewportWidget->activeAnimationFile();
    const int activeClip = _viewportWidget->activeAnimationClip();
    const int previousSceneGraphActive = sg->activeAnimationClipForFile(_sourceFile);

    data.clips.removeAt(_index);
    if (data.clips.isEmpty())
    {
        sg->clearAnimationData(_sourceFile);
        _viewportWidget->clearAnimationRuntimeForFile(_sourceFile);
        return;
    }

    sg->setAnimationData(_sourceFile, data);

    int nextClipIndex = previousSceneGraphActive;
    if (_sourceFile == activeFile && activeClip >= 0)
        nextClipIndex = activeClip > _index ? activeClip - 1 : qMin(activeClip, data.clips.size() - 1);
    else if (previousSceneGraphActive > _index)
        nextClipIndex = previousSceneGraphActive - 1;
    else
        nextClipIndex = qMin(previousSceneGraphActive, data.clips.size() - 1);

    nextClipIndex = qBound(0, nextClipIndex, data.clips.size() - 1);
    sg->setActiveAnimationClip(_sourceFile, nextClipIndex);

    if (_sourceFile == activeFile)
    {
        _viewportWidget->setAnimationPlaying(false);
        _viewportWidget->setActiveAnimation(_sourceFile, nextClipIndex);
    }
}

void MetadataDeleteCommand::undoAnimationDelete()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg || !_viewportWidget || _sourceFile.isEmpty())
        return;

    sg->setAnimationData(_sourceFile, _oldAnimationData);
    if (_oldSceneGraphActiveClip >= 0 && _oldSceneGraphActiveClip < _oldAnimationData.clips.size())
        sg->setActiveAnimationClip(_sourceFile, _oldSceneGraphActiveClip);

    _viewportWidget->syncRuntimeNodeTransforms(_sourceFile);

    if (!_oldAnimationState.file.isEmpty() && _oldAnimationState.clipIndex >= 0)
    {
        _viewportWidget->setActiveAnimation(_oldAnimationState.file, _oldAnimationState.clipIndex);
        _viewportWidget->seekAnimation(_oldAnimationState.timeSeconds);
        _viewportWidget->setAnimationPlaying(_oldAnimationState.playing);
    }
}

void MetadataDeleteCommand::redoCameraDelete()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg || !_viewportWidget || _sourceFile.isEmpty())
        return;

    if (_index < 0)
    {
        if (_viewportWidget->activeGltfCameraFile() == _sourceFile)
            _viewportWidget->resetToSystemCamera();
        sg->clearGltfCameraData(_sourceFile);
        return;
    }

    GltfCameraData data = sg->gltfCameraDataForFile(_sourceFile);
    if (_index >= data.cameras.size())
        return;

    const QString activeFile = _viewportWidget->activeGltfCameraFile();
    const int activeIndex = _viewportWidget->activeGltfCameraIndex();

    data.cameras.removeAt(_index);
    if (data.cameras.isEmpty())
    {
        if (activeFile == _sourceFile)
            _viewportWidget->resetToSystemCamera();
        sg->clearGltfCameraData(_sourceFile);
        return;
    }

    sg->setGltfCameraData(_sourceFile, data);
    if (activeFile == _sourceFile)
    {
        if (activeIndex == _index)
            _viewportWidget->resetToSystemCamera();
        else if (activeIndex > _index)
            _viewportWidget->activateGltfCamera(_sourceFile, activeIndex - 1);
    }
}

void MetadataDeleteCommand::undoCameraDelete()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg || !_viewportWidget || _sourceFile.isEmpty())
        return;

    sg->setGltfCameraData(_sourceFile, _oldCameraData);
    if (!_oldCameraState.file.isEmpty() && _oldCameraState.cameraIndex >= 0)
        _viewportWidget->activateGltfCamera(_oldCameraState.file, _oldCameraState.cameraIndex);
    else
        _viewportWidget->resetToSystemCamera();
}

void MetadataDeleteCommand::redoVariantDelete()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg || !_viewportWidget || _sourceFile.isEmpty())
        return;

    if (_index < 0)
    {
        _viewer->applyVariant(_sourceFile, -1);
        for (SceneMesh* mesh : _viewportWidget->getMeshStore())
        {
            if (mesh && mesh->getSourceFile() == _sourceFile)
                mesh->setVariantMappings({});
        }
        sg->clearVariantData(_sourceFile);
        _viewportWidget->update();
        return;
    }

    GltfVariantData data = sg->variantDataForFile(_sourceFile);
    if (_index >= data.variantNames.size())
        return;

    data.variantNames.removeAt(_index);
    for (auto it = data.meshVariantMappings.begin(); it != data.meshVariantMappings.end(); ++it)
        it.value() = remapVariantMappings(it.value(), _index);

    for (SceneMesh* mesh : _viewportWidget->getMeshStore())
    {
        if (!mesh || mesh->getSourceFile() != _sourceFile)
            continue;

        if (mesh->getSceneIndex() < 0)
        {
            mesh->setVariantMappings({});
            continue;
        }

        mesh->setVariantMappings(data.meshVariantMappings.value(mesh->getSceneIndex()));
    }

    if (data.variantNames.isEmpty())
    {
        _viewer->applyVariant(_sourceFile, -1);
        for (SceneMesh* mesh : _viewportWidget->getMeshStore())
        {
            if (mesh && mesh->getSourceFile() == _sourceFile)
                mesh->setVariantMappings({});
        }
        sg->clearVariantData(_sourceFile);
        _viewportWidget->update();
        return;
    }

    const int activeVariant = sg->activeVariantForFile(_sourceFile);
    int nextVariant = activeVariant;
    if (activeVariant == _index)
        nextVariant = -1;
    else if (activeVariant > _index)
        nextVariant = activeVariant - 1;

    sg->setVariantData(_sourceFile, data);
    _viewer->applyVariant(_sourceFile, nextVariant);
}

void MetadataDeleteCommand::undoVariantDelete()
{
    SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr;
    if (!sg || !_viewportWidget || _sourceFile.isEmpty())
        return;

    for (auto it = _oldVariantMappingsByMesh.cbegin(); it != _oldVariantMappingsByMesh.cend(); ++it)
    {
        if (SceneMesh* mesh = _viewportWidget->getMeshByUuid(it.key()))
            mesh->setVariantMappings(it.value());
    }

    sg->setVariantData(_sourceFile, _oldVariantData);
    _viewer->applyVariant(_sourceFile, _oldActiveVariant);
}
