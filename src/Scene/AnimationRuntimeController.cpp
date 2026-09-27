#include "AnimationRuntimeController.h"
#include "AnimationUtils.h"
#include "SceneGraph.h"

#include <algorithm>
// types.h (not the individual component headers alone) is needed for
// aiMatrix4x4t/aiQuaterniont/aiVector3t's inline constructor definitions
// (the individual headers only declare them).
#include <assimp/types.h>

namespace
{
float uniformScaleOf(const QMatrix4x4& m)
{
    const float sx = QVector3D(m(0, 0), m(1, 0), m(2, 0)).length();
    const float sy = QVector3D(m(0, 1), m(1, 1), m(2, 1)).length();
    const float sz = QVector3D(m(0, 2), m(1, 2), m(2, 2)).length();
    return (sx + sy + sz) / 3.0f;
}
}

void AnimationRuntimeController::resetPlayback()
{
    _activeAnimationFile.clear();
    _activeAnimationClip = -1;
    _animationCurrentTimeSeconds = 0.0;
    _animationPlaying = false;
}

void AnimationRuntimeController::resumePlayback()
{
    _animationPlaying = true;
    _animationElapsed.restart();
    _animationTimer->start();
}

void AnimationRuntimeController::pausePlayback()
{
    _animationPlaying = false;
    _animationTimer->stop();
}

void AnimationRuntimeController::applyPlaybackSpeed(double speed)
{
    _animationPlaybackSpeed = std::clamp(speed, 0.25, 4.0);
    if (_animationPlaying)
        _animationElapsed.restart();
}

void AnimationRuntimeController::setActiveGltfCamera(const QString& file, int index)
{
    _activeGltfCameraFile  = file;
    _activeGltfCameraIndex = index;
}

void AnimationRuntimeController::setAnimatedLightTransform(const QString& sourceFile,
                                                            const std::vector<GPULight>& lights)
{
    _animatedLightTransformSourceFile = sourceFile;
    _animatedParsedLights = lights;
}

void AnimationRuntimeController::clearAnimatedLightTransform(const QString& sourceFile)
{
    if (_animatedLightTransformSourceFile != sourceFile)
        return;
    _animatedLightTransformSourceFile.clear();
    _animatedParsedLights.clear();
}

void AnimationRuntimeController::setAnimatedLightVisibility(const QString& sourceFile,
                                                             const QVector<bool>& mask)
{
    _animatedLightVisibilitySourceFile = sourceFile;
    _animatedLightVisibilityMask = mask;
}

void AnimationRuntimeController::clearAnimatedLightVisibility(const QString& sourceFile)
{
    if (_animatedLightVisibilitySourceFile != sourceFile)
        return;
    _animatedLightVisibilitySourceFile.clear();
    _animatedLightVisibilityMask.clear();
}

void AnimationRuntimeController::setAnimatedMeshVisibility(const QString& sourceFile,
                                                            const QSet<QUuid>& hiddenUuids)
{
    _animatedMeshVisibilitySourceFile = sourceFile;
    _animatedHiddenMeshUuids = hiddenUuids;
}

void AnimationRuntimeController::clearAnimatedMeshVisibility(const QString& sourceFile)
{
    if (_animatedMeshVisibilitySourceFile != sourceFile)
        return;
    _animatedMeshVisibilitySourceFile.clear();
    _animatedHiddenMeshUuids.clear();
}

void AnimationRuntimeController::clearAllAnimatedState()
{
    _animatedLightTransformSourceFile.clear();
    _animatedParsedLights.clear();
    _animatedLightVisibilitySourceFile.clear();
    _animatedLightVisibilityMask.clear();
    _animatedMeshVisibilitySourceFile.clear();
    _animatedHiddenMeshUuids.clear();
}

void AnimationRuntimeController::rebuildCurrentRepositionedLights(
    const std::vector<GPULight>& originalLights,
    std::vector<GPULight>& repositionedLights,
    const QVector<LightOrigin>& lightFileIndexMap,
    const QHash<QString, QMatrix4x4>& userTransforms)
{
    if (originalLights.empty())
    {
        repositionedLights.clear();
        return;
    }

    const std::vector<GPULight>& sourceLights =
        (!_animatedLightTransformSourceFile.isEmpty() &&
         _animatedParsedLights.size() == originalLights.size())
        ? _animatedParsedLights
        : originalLights;
    repositionedLights = sourceLights;

    if (lightFileIndexMap.size() == static_cast<int>(repositionedLights.size()))
    {
        for (int i = 0; i < static_cast<int>(repositionedLights.size()); ++i)
        {
            const QString& file = lightFileIndexMap[i].file;
            const QMatrix4x4 transform = userTransforms.value(file, QMatrix4x4());
            if (transform.isIdentity())
                continue;

            auto& light = repositionedLights[i];

            const QVector3D pos = transform.map(
                QVector3D(light.position.x, light.position.y, light.position.z));
            light.position = glm::vec3(pos.x(), pos.y(), pos.z());

            const QVector3D dir = transform.mapVector(
                QVector3D(light.direction.x, light.direction.y, light.direction.z)).normalized();
            light.direction = glm::vec3(dir.x(), dir.y(), dir.z());

            const float s = uniformScaleOf(transform);
            if (light.range > 0.0f)
                light.range *= s;
            if (light.type != static_cast<int>(LightType::Directional))
                light.intensity *= (s * s);
        }
    }

    if (!_animatedLightVisibilitySourceFile.isEmpty() &&
        _animatedLightVisibilityMask.size() == static_cast<qsizetype>(repositionedLights.size()))
    {
        std::vector<GPULight> visibleLights;
        visibleLights.reserve(repositionedLights.size());
        for (int lightIndex = 0; lightIndex < static_cast<int>(repositionedLights.size()); ++lightIndex)
        {
            if (_animatedLightVisibilityMask[lightIndex])
                visibleLights.push_back(repositionedLights[lightIndex]);
        }
        repositionedLights = std::move(visibleLights);
    }
}

std::vector<GPULight> AnimationRuntimeController::rebuildAndBuildUploadLights(
    const std::function<QMatrix4x4(const QString&)>& userTransformResolver,
    const std::function<bool(const LightOrigin&)>& isLightEnabled)
{
    QHash<QString, QMatrix4x4> transformCache;
    if (userTransformResolver)
    {
        for (const LightOrigin& origin : _lightFileIndexMap)
        {
            if (!transformCache.contains(origin.file))
                transformCache.insert(origin.file, userTransformResolver(origin.file));
        }
    }

    rebuildCurrentRepositionedLights(_originalParsedLights, _currentRepositionedLights, _lightFileIndexMap, transformCache);
    return isLightEnabled
        ? buildUploadLights(isLightEnabled)
        : buildUploadLights();
}

std::vector<GPULight> AnimationRuntimeController::buildUploadLightsWithSceneGraph(
    const std::function<QMatrix4x4(const QString&)>& userTransformResolver,
    const SceneGraph* sg)
{
    if (!sg)
        return rebuildAndBuildUploadLights(userTransformResolver);

    return rebuildAndBuildUploadLights(
        userTransformResolver,
        [sg](const LightOrigin& origin) {
            const GltfLightData& ld = sg->lightDataForFile(origin.file);
            return origin.index < static_cast<int>(ld.lights.size()) &&
                   ld.lights[origin.index].enabled;
        });
}

std::vector<GPULight> AnimationRuntimeController::buildUploadLights() const
{
    return _currentRepositionedLights;
}

std::vector<GPULight> AnimationRuntimeController::buildUploadLights(
    const std::function<bool(const LightOrigin&)>& isLightEnabled) const
{
    if (!isLightEnabled ||
        _lightFileIndexMap.isEmpty() ||
        _lightFileIndexMap.size() != static_cast<int>(_currentRepositionedLights.size()))
    {
        return _currentRepositionedLights;
    }

    std::vector<GPULight> uploadLights;
    uploadLights.reserve(_currentRepositionedLights.size());
    for (int i = 0; i < static_cast<int>(_currentRepositionedLights.size()); ++i)
    {
        if (isLightEnabled(_lightFileIndexMap[i]))
            uploadLights.push_back(_currentRepositionedLights[i]);
    }
    return uploadLights;
}

QHash<int, bool> AnimationRuntimeController::buildEffectiveNodeVisibility(
    const RuntimeAnimationFileState& runtime,
    const QHash<int, bool>& nodeVisibilityOverrides) const
{
    QHash<int, bool> effectiveCache;
    std::function<bool(int)> evalVisible = [&](int nodeIndex) -> bool
    {
        if (effectiveCache.contains(nodeIndex))
            return effectiveCache.value(nodeIndex);
        if (nodeIndex < 0 || nodeIndex >= runtime.data.nodeVisibilityStates.size())
            return true;

        const GltfAnimationNodeVisibilityState& nodeState = runtime.data.nodeVisibilityStates[nodeIndex];
        const bool localVisible = nodeVisibilityOverrides.value(nodeIndex, nodeState.defaultVisible);
        const bool effectiveVisible = localVisible &&
            (nodeState.parentNodeIndex < 0 || evalVisible(nodeState.parentNodeIndex));
        effectiveCache.insert(nodeIndex, effectiveVisible);
        return effectiveVisible;
    };

    for (const GltfAnimationNodeVisibilityState& nodeState : runtime.data.nodeVisibilityStates)
        evalVisible(nodeState.nodeIndex);

    return effectiveCache;
}

QVector<bool> AnimationRuntimeController::buildAnimatedLightVisibilityMask(
    const RuntimeAnimationFileState& runtime,
    const QHash<int, bool>& effectiveNodeVisibility) const
{
    QVector<bool> visibleByParsedLight(runtime.data.lightBindings.size(), true);
    for (const GltfAnimationLightBinding& binding : runtime.data.lightBindings)
    {
        if (binding.parsedLightIndex >= 0 && binding.parsedLightIndex < visibleByParsedLight.size())
            visibleByParsedLight[binding.parsedLightIndex] =
                effectiveNodeVisibility.value(binding.nodeIndex, true);
    }
    return visibleByParsedLight;
}

QSet<QUuid> AnimationRuntimeController::collectHiddenAnimatedMeshUuids(
    const RuntimeAnimationFileState& runtime,
    const QHash<int, bool>& effectiveNodeVisibility,
    const SceneNode* fileNode) const
{
    QSet<QUuid> hiddenMeshUuids;
    if (!fileNode)
        return hiddenMeshUuids;

    std::function<void(const SceneNode*)> collectHidden = [&](const SceneNode* node)
    {
        if (!node)
            return;

        const int nodeIndex = runtime.nodeIndexByUuid.value(node->nodeUuid, -1);
        const bool visible = nodeIndex < 0 ? true : effectiveNodeVisibility.value(nodeIndex, true);
        if (!visible)
        {
            for (const QUuid& uuid : node->meshUuids)
                hiddenMeshUuids.insert(uuid);
        }

        for (SceneNode* child : node->children)
            collectHidden(child);
    };

    for (SceneNode* child : fileNode->children)
        collectHidden(child);

    return hiddenMeshUuids;
}

bool AnimationRuntimeController::buildAnimatedLightTransformState(
    const QString& sourceFile,
    const RuntimeAnimationFileState& runtime,
    const AnimationSampleResult& result,
    const SceneNode* fileNode,
    std::vector<GPULight>& outAnimatedLights) const
{
    if (!fileNode || !(runtime.data.hasNodeAnimations || runtime.data.hasSkinning))
        return false;

    int fileLightOffset = 0;
    int fileLightCount = static_cast<int>(_originalParsedLights.size());
    if (!_lightFileIndexMap.isEmpty())
    {
        int foundOffset = -1;
        int foundCount = 0;
        for (int k = 0; k < _lightFileIndexMap.size(); ++k)
        {
            if (_lightFileIndexMap[k].file == sourceFile)
            {
                if (foundCount == 0)
                    foundOffset = k;
                ++foundCount;
            }
        }
        if (foundOffset >= 0)
        {
            fileLightOffset = foundOffset;
            fileLightCount = foundCount;
        }
    }

    if (runtime.data.lightBindings.isEmpty() ||
        fileLightCount != static_cast<int>(runtime.data.lightBindings.size()))
    {
        return false;
    }

    const QMatrix4x4 importCorrection = AnimationRuntimeController::aiToQMatrix(fileNode->localTransform);
    std::vector<GPULight> animatedLights = _originalParsedLights;
    bool resolvedAnyAnimatedLightTransform = false;

    for (const GltfAnimationLightBinding& binding : runtime.data.lightBindings)
    {
        const int globalIndex = fileLightOffset + binding.parsedLightIndex;
        if (binding.parsedLightIndex < 0 ||
            globalIndex >= static_cast<int>(animatedLights.size()))
        {
            continue;
        }

        QMatrix4x4 modelSpaceWorld;
        bool hasWorldTransform = false;
        if (binding.nodeIndex >= 0 && runtime.nodeUuidByIndex.contains(binding.nodeIndex))
        {
            const QUuid nodeUuid = AnimationRuntimeController::resolveRuntimeNodeUuid(runtime, binding.nodeIndex);
            if (result.worldTransforms.contains(nodeUuid))
            {
                modelSpaceWorld = result.worldTransforms.value(nodeUuid);
                hasWorldTransform = true;
            }
        }
        else if (!binding.nodeName.isEmpty())
        {
            const QUuid nodeUuid = AnimationRuntimeController::resolveRuntimeNodeUuid(runtime, -1, binding.nodeName);
            if (!nodeUuid.isNull() && result.worldTransforms.contains(nodeUuid))
            {
                modelSpaceWorld = result.worldTransforms.value(nodeUuid);
                hasWorldTransform = true;
            }
        }

        if (!hasWorldTransform)
            continue;

        const QMatrix4x4 worldSpace = importCorrection * modelSpaceWorld;
        GPULight& light = animatedLights[globalIndex];
        light.position = glm::vec3(worldSpace(0, 3), worldSpace(1, 3), worldSpace(2, 3));

        const QVector3D localDir(0.0f, 0.0f, -1.0f);
        const QVector3D worldDir = (worldSpace.mapVector(localDir)).normalized();
        light.direction = glm::vec3(worldDir.x(), worldDir.y(), worldDir.z());
        resolvedAnyAnimatedLightTransform = true;
    }

    if (!resolvedAnyAnimatedLightTransform)
        return false;

    outAnimatedLights = std::move(animatedLights);
    return true;
}

AnimationRuntimeController::AnimationSampleResult AnimationRuntimeController::sampleClip(
    const RuntimeAnimationFileState& runtime,
    const GltfAnimationClip& clip,
    double timeSeconds,
    const SceneGraph* sg) const
{
    AnimationSampleResult result;
    result.nodeTransforms = runtime.defaultNodeTransformsByUuid;
    result.morphWeights = runtime.defaultNodeMorphWeightsByUuid;
    result.animatedMaterials = runtime.defaultMeshMaterials;

    for (const GltfAnimationNodeVisibilityState& nodeState : runtime.data.nodeVisibilityStates)
        result.nodeVisibility.insert(nodeState.nodeIndex, nodeState.defaultVisible);

    const auto resolveChannelNodeUuid = [&](const GltfAnimationChannel& channel) -> QUuid
    {
        return AnimationRuntimeController::resolveRuntimeNodeUuid(
            runtime,
            channel.targetNodeIndex,
            channel.targetNodeName);
    };

    QHash<QUuid, bool> nodeAffectsShadowCache;
    std::function<bool(const QUuid&)> nodeAffectsShadow = [&](const QUuid& nodeUuid) -> bool
    {
        if (nodeUuid.isNull() || !sg)
            return false;
        if (nodeAffectsShadowCache.contains(nodeUuid))
            return nodeAffectsShadowCache.value(nodeUuid);

        const SceneNode* node = sg->findNodeByUuid(nodeUuid);
        if (!node)
        {
            nodeAffectsShadowCache.insert(nodeUuid, false);
            return false;
        }

        std::function<bool(const SceneNode*)> hasMeshInSubtree = [&](const SceneNode* current) -> bool
        {
            if (!current)
                return false;
            if (!current->meshUuids.isEmpty())
                return true;
            for (const SceneNode* child : current->children)
            {
                if (hasMeshInSubtree(child))
                    return true;
            }
            return false;
        };

        const bool affects = hasMeshInSubtree(node);
        nodeAffectsShadowCache.insert(nodeUuid, affects);
        return affects;
    };

    for (const GltfAnimationChannel& channel : clip.channels)
    {
        if (channel.targetPath == GltfAnimationTargetPath::Pointer)
        {
            if (channel.pointerTargetKind == GltfAnimationPointerTargetKind::MaterialTextureTransform)
            {
                if (channel.targetMaterialIndex < 0)
                    continue;

                const QList<QUuid> affectedMeshes = runtime.meshUuidsByMaterialIndex.values(channel.targetMaterialIndex);
                for (const QUuid& meshUuid : affectedMeshes)
                {
                    auto materialIt = result.animatedMaterials.find(meshUuid);
                    if (materialIt == result.animatedMaterials.end())
                        continue;

                    if (channel.pointerProperty == GltfAnimationPointerProperty::BaseColorFactor)
                    {
                        const QVector4D vec4Value = AnimationUtils::sampleVec4Keys(
                            channel.vec4Keys,
                            timeSeconds,
                            QVector4D(materialIt.value().albedoColor(), materialIt.value().opacity()));
                        AnimationRuntimeController::applyMaterialFactorPointerValue(
                            materialIt.value(),
                            channel.pointerProperty,
                            vec4Value);
                        continue;
                    }

                    const QVector2D vec2Value = channel.pointerProperty == GltfAnimationPointerProperty::Rotation
                        ? QVector2D()
                        : AnimationUtils::sampleVec2Keys(channel.vec2Keys, timeSeconds, QVector2D());
                    const float scalarValue = channel.pointerProperty == GltfAnimationPointerProperty::Rotation
                        ? AnimationUtils::sampleFloatKeys(channel.floatKeys, timeSeconds, 0.0f)
                        : 0.0f;
                    AnimationRuntimeController::applyTexturePointerValue(
                        materialIt.value(),
                        channel.textureTarget,
                        channel.pointerProperty,
                        vec2Value,
                        scalarValue);
                }
            }
            else if (channel.pointerTargetKind == GltfAnimationPointerTargetKind::NodeVisibility)
            {
                if (channel.targetNodeIndex >= 0)
                {
                    const QUuid resolvedNodeUuid = AnimationRuntimeController::resolveRuntimeNodeUuid(
                        runtime,
                        channel.targetNodeIndex);
                    result.affectsShadowCasters =
                        result.affectsShadowCasters || nodeAffectsShadow(resolvedNodeUuid);
                    result.nodeVisibility.insert(
                        channel.targetNodeIndex,
                        AnimationUtils::sampleBoolKeys(
                            channel.boolKeys,
                            timeSeconds,
                            result.nodeVisibility.value(channel.targetNodeIndex, true)));
                }
            }
            continue;
        }

        if (channel.targetKind == GltfAnimationBindingTargetKind::Mesh)
        {
            if (channel.targetMeshUuid.isNull())
                continue;

            if (channel.targetPath == GltfAnimationTargetPath::Translation ||
                channel.targetPath == GltfAnimationTargetPath::Rotation ||
                channel.targetPath == GltfAnimationTargetPath::Scale ||
                channel.targetPath == GltfAnimationTargetPath::Weights)
            {
                result.affectsShadowCasters = true;
            }

            RuntimeNodeTransform meshTransform = result.meshTransforms.value(channel.targetMeshUuid);
            if (meshTransform.rotation.isNull())
                meshTransform.rotation = QQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
            switch (channel.targetPath)
            {
            case GltfAnimationTargetPath::Translation:
                meshTransform.translation = AnimationUtils::sampleVec3Keys(
                    channel.vec3Keys, timeSeconds, meshTransform.translation);
                break;
            case GltfAnimationTargetPath::Rotation:
                meshTransform.rotation = AnimationUtils::sampleQuatKeys(
                    channel.quatKeys, timeSeconds, meshTransform.rotation);
                break;
            case GltfAnimationTargetPath::Scale:
                meshTransform.scale = AnimationUtils::sampleVec3Keys(
                    channel.vec3Keys,
                    timeSeconds,
                    meshTransform.scale.isNull() ? QVector3D(1.0f, 1.0f, 1.0f) : meshTransform.scale);
                break;
            case GltfAnimationTargetPath::Weights:
            case GltfAnimationTargetPath::Pointer:
                continue;
            }
            if (meshTransform.scale.isNull())
                meshTransform.scale = QVector3D(1.0f, 1.0f, 1.0f);
            result.meshTransforms.insert(channel.targetMeshUuid, meshTransform);
            continue;
        }

        const QUuid resolvedNodeUuid = resolveChannelNodeUuid(channel);
        if (resolvedNodeUuid.isNull())
            continue;

        if ((channel.targetPath == GltfAnimationTargetPath::Translation ||
             channel.targetPath == GltfAnimationTargetPath::Rotation ||
             channel.targetPath == GltfAnimationTargetPath::Scale ||
             channel.targetPath == GltfAnimationTargetPath::Weights) &&
            (runtime.data.hasSkinning || nodeAffectsShadow(resolvedNodeUuid)))
        {
            result.affectsShadowCasters = true;
        }

        RuntimeNodeTransform node = result.nodeTransforms.value(resolvedNodeUuid);
        switch (channel.targetPath)
        {
        case GltfAnimationTargetPath::Translation:
            node.translation = AnimationUtils::sampleVec3Keys(channel.vec3Keys, timeSeconds, node.translation);
            break;
        case GltfAnimationTargetPath::Rotation:
            node.rotation = AnimationUtils::sampleQuatKeys(channel.quatKeys, timeSeconds, node.rotation);
            break;
        case GltfAnimationTargetPath::Scale:
            node.scale = AnimationUtils::sampleVec3Keys(channel.vec3Keys, timeSeconds, node.scale);
            break;
        case GltfAnimationTargetPath::Weights:
            result.morphWeights.insert(
                resolvedNodeUuid,
                AnimationUtils::sampleWeightKeys(
                    channel.weightKeys,
                    timeSeconds,
                    result.morphWeights.value(resolvedNodeUuid)));
            continue;
        case GltfAnimationTargetPath::Pointer:
            continue;
        }
        result.nodeTransforms.insert(resolvedNodeUuid, node);
    }

    return result;
}

// ---------------------------------------------------------------------------
// Light population
// ---------------------------------------------------------------------------

void AnimationRuntimeController::setParsedLightsFromSingleFile(const GltfLightData& lightData)
{
    _originalParsedLights.clear();
    _lightFileIndexMap.clear();
    _currentRepositionedLights.clear();
    for (const GltfLightEntry& entry : lightData.lights)
        _originalParsedLights.push_back(entry.gpuLight);
    // lightFileIndexMap intentionally not built here — onSceneLightDataChanged
    // (→ rebuildParsedLightsFromSceneGraph) is the authoritative rebuilder once
    // the file is registered in SceneGraph.
}

void AnimationRuntimeController::rebuildParsedLightsFromSceneGraph(const SceneGraph* sg)
{
    _originalParsedLights.clear();
    _lightFileIndexMap.clear();
    _currentRepositionedLights.clear();
    if (!sg)
        return;
    const QStringList files = sg->filesWithLights();
    for (const QString& file : files)
    {
        const GltfLightData& ld = sg->lightDataForFile(file);
        for (int i = 0; i < static_cast<int>(ld.lights.size()); ++i)
        {
            _originalParsedLights.push_back(ld.lights[i].gpuLight);
            _lightFileIndexMap.append({file, i});
        }
    }
}

void AnimationRuntimeController::clearParsedLights()
{
    _originalParsedLights.clear();
    _lightFileIndexMap.clear();
    _currentRepositionedLights.clear();
}

std::vector<GPULight> AnimationRuntimeController::buildGizmoLights(const SceneGraph* sg) const
{
    if (_currentRepositionedLights.empty())
        return {};

    const bool hasEnabledMap = sg
        && !_lightFileIndexMap.isEmpty()
        && static_cast<int>(_currentRepositionedLights.size()) == _lightFileIndexMap.size();

    if (!hasEnabledMap)
        return _currentRepositionedLights;

    std::vector<GPULight> result;
    result.reserve(_currentRepositionedLights.size());
    for (int i = 0; i < static_cast<int>(_currentRepositionedLights.size()); ++i)
    {
        const LightOrigin& origin = _lightFileIndexMap[i];
        const GltfLightData& ld = sg->lightDataForFile(origin.file);
        if (origin.index < static_cast<int>(ld.lights.size()) && ld.lights[origin.index].enabled)
            result.push_back(_currentRepositionedLights[i]);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

QMatrix4x4 AnimationRuntimeController::aiToQMatrix(const aiMatrix4x4& m)
{
    QMatrix4x4 out;
    out.setRow(0, QVector4D(m.a1, m.a2, m.a3, m.a4));
    out.setRow(1, QVector4D(m.b1, m.b2, m.b3, m.b4));
    out.setRow(2, QVector4D(m.c1, m.c2, m.c3, m.c4));
    out.setRow(3, QVector4D(m.d1, m.d2, m.d3, m.d4));
    return out;
}

AnimationRuntimeController::RuntimeNodeTransform
AnimationRuntimeController::decomposeNodeTransform(const aiMatrix4x4& matrix)
{
    aiVector3D   scaling;
    aiQuaternion rotation;
    aiVector3D   position;
    matrix.Decompose(scaling, rotation, position);

    RuntimeNodeTransform result;
    result.translation = QVector3D(position.x, position.y, position.z);
    result.rotation    = QQuaternion(rotation.w, rotation.x, rotation.y, rotation.z).normalized();
    result.scale       = QVector3D(scaling.x, scaling.y, scaling.z);
    return result;
}

QMatrix4x4 AnimationRuntimeController::composeNodeTransform(const RuntimeNodeTransform& tr)
{
    QMatrix4x4 m;
    m.translate(tr.translation);
    m.rotate(tr.rotation);
    m.scale(tr.scale);
    return m;
}

QUuid AnimationRuntimeController::resolveRuntimeNodeUuid(const RuntimeAnimationFileState& runtime,
                                                           int targetNodeIndex,
                                                           const QString& fallbackNodeName)
{
    if (targetNodeIndex >= 0 && runtime.nodeUuidByIndex.contains(targetNodeIndex))
        return runtime.nodeUuidByIndex.value(targetNodeIndex);

    if (!fallbackNodeName.isEmpty())
        return runtime.nodeUuidByName.value(fallbackNodeName, QUuid());

    return QUuid();
}

void AnimationRuntimeController::applyTexturePointerValue(Material& material,
    GltfAnimationTextureTarget textureTarget,
    GltfAnimationPointerProperty property,
    const QVector2D& vec2Value,
    float scalarValue)
{
    auto applyVec2 = [&](auto setOffset, auto setScale) {
        switch (property)
        {
        case GltfAnimationPointerProperty::Offset: (material.*setOffset)(vec2Value); break;
        case GltfAnimationPointerProperty::Scale:  (material.*setScale)(vec2Value);  break;
        default: break;
        }
    };
    auto applyRotation = [&](auto setRotation) {
        if (property == GltfAnimationPointerProperty::Rotation)
            (material.*setRotation)(scalarValue);
    };

    switch (textureTarget)
    {
    case GltfAnimationTextureTarget::Albedo:
        applyVec2(&Material::setAlbedoTexOffset, &Material::setAlbedoTexScale);
        applyRotation(&Material::setAlbedoTexRotation); break;
    case GltfAnimationTextureTarget::Metallic:
        applyVec2(&Material::setMetallicTexOffset, &Material::setMetallicTexScale);
        applyRotation(&Material::setMetallicTexRotation); break;
    case GltfAnimationTextureTarget::Roughness:
        applyVec2(&Material::setRoughnessTexOffset, &Material::setRoughnessTexScale);
        applyRotation(&Material::setRoughnessTexRotation); break;
    case GltfAnimationTextureTarget::MetallicRoughness:
        applyVec2(&Material::setMetallicTexOffset, &Material::setMetallicTexScale);
        applyRotation(&Material::setMetallicTexRotation);
        applyVec2(&Material::setRoughnessTexOffset, &Material::setRoughnessTexScale);
        applyRotation(&Material::setRoughnessTexRotation); break;
    case GltfAnimationTextureTarget::Normal:
        applyVec2(&Material::setNormalTexOffset, &Material::setNormalTexScale);
        applyRotation(&Material::setNormalTexRotation); break;
    case GltfAnimationTextureTarget::Occlusion:
        applyVec2(&Material::setOcclusionTexOffset, &Material::setOcclusionTexScale);
        applyRotation(&Material::setOcclusionTexRotation); break;
    case GltfAnimationTextureTarget::Emissive:
        applyVec2(&Material::setEmissiveTexOffset, &Material::setEmissiveTexScale);
        applyRotation(&Material::setEmissiveTexRotation); break;
    case GltfAnimationTextureTarget::Transmission:
        applyVec2(&Material::setTransmissionTexOffset, &Material::setTransmissionTexScale);
        applyRotation(&Material::setTransmissionTexRotation); break;
    case GltfAnimationTextureTarget::Thickness:
        applyVec2(&Material::setThicknessTexOffset, &Material::setThicknessTexScale);
        applyRotation(&Material::setThicknessTexRotation); break;
    case GltfAnimationTextureTarget::IOR:
        applyVec2(&Material::setIORTexOffset, &Material::setIORTexScale);
        applyRotation(&Material::setIORTexRotation); break;
    case GltfAnimationTextureTarget::SheenColor:
        applyVec2(&Material::setSheenColorTexOffset, &Material::setSheenColorTexScale);
        applyRotation(&Material::setSheenColorTexRotation); break;
    case GltfAnimationTextureTarget::SheenRoughness:
        applyVec2(&Material::setSheenRoughnessTexOffset, &Material::setSheenRoughnessTexScale);
        applyRotation(&Material::setSheenRoughnessTexRotation); break;
    case GltfAnimationTextureTarget::Clearcoat:
        applyVec2(&Material::setClearcoatColorTexOffset, &Material::setClearcoatColorTexScale);
        applyRotation(&Material::setClearcoatColorTexRotation); break;
    case GltfAnimationTextureTarget::ClearcoatRoughness:
        applyVec2(&Material::setClearcoatRoughnessTexOffset, &Material::setClearcoatRoughnessTexScale);
        applyRotation(&Material::setClearcoatRoughnessTexRotation); break;
    case GltfAnimationTextureTarget::ClearcoatNormal:
        applyVec2(&Material::setClearcoatNormalTexOffset, &Material::setClearcoatNormalTexScale);
        applyRotation(&Material::setClearcoatNormalTexRotation); break;
    case GltfAnimationTextureTarget::Iridescence:
        applyVec2(&Material::setIridescenceTexOffset, &Material::setIridescenceTexScale);
        applyRotation(&Material::setIridescenceTexRotation); break;
    case GltfAnimationTextureTarget::IridescenceThickness:
        applyVec2(&Material::setIridescenceThicknessTexOffset, &Material::setIridescenceThicknessTexScale);
        applyRotation(&Material::setIridescenceThicknessTexRotation); break;
    case GltfAnimationTextureTarget::SpecularFactor:
        applyVec2(&Material::setSpecularFactorTexOffset, &Material::setSpecularFactorTexScale);
        applyRotation(&Material::setSpecularFactorTexRotation); break;
    case GltfAnimationTextureTarget::SpecularColor:
        applyVec2(&Material::setSpecularColorTexOffset, &Material::setSpecularColorTexScale);
        applyRotation(&Material::setSpecularColorTexRotation); break;
    case GltfAnimationTextureTarget::Anisotropy:
        applyVec2(&Material::setAnisotropyTexOffset, &Material::setAnisotropyTexScale);
        applyRotation(&Material::setAnisotropyTexRotation); break;
    case GltfAnimationTextureTarget::DiffuseTransmission:
        applyVec2(&Material::setDiffuseTransmissionTexOffset, &Material::setDiffuseTransmissionTexScale);
        applyRotation(&Material::setDiffuseTransmissionTexRotation); break;
    case GltfAnimationTextureTarget::DiffuseTransmissionColor:
        applyVec2(&Material::setDiffuseTransmissionColorTexOffset, &Material::setDiffuseTransmissionColorTexScale);
        applyRotation(&Material::setDiffuseTransmissionColorTexRotation); break;
    case GltfAnimationTextureTarget::Diffuse:
        applyVec2(&Material::setDiffuseTexOffset, &Material::setDiffuseTexScale);
        applyRotation(&Material::setDiffuseTexRotation); break;
    case GltfAnimationTextureTarget::SpecularGlossiness:
        applyVec2(&Material::setSpecularGlossinessTexOffset, &Material::setSpecularGlossinessTexScale);
        applyRotation(&Material::setSpecularGlossinessTexRotation); break;
    default: break;
    }
}

void AnimationRuntimeController::applyMaterialFactorPointerValue(Material& material,
    GltfAnimationPointerProperty property,
    const QVector4D& vec4Value)
{
    if (property != GltfAnimationPointerProperty::BaseColorFactor)
        return;
    const QVector3D color(vec4Value.x(), vec4Value.y(), vec4Value.z());
    material.setAlbedoColor(color);
    material.setDiffuse(color);
    material.setOpacity(vec4Value.w());
}
