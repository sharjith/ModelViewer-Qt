#include "ExplodedViewPanel.h"

#include "CapturedStepsTreeWidget.h"
#include "ExplodedViewSelectionEditor.h"
#include "ViewportWidget.h"
#include "GltfAnimationData.h"
#include "LanguageManager.h"
#include "ModelViewer.h"
#include "SceneGraph.h"
#include "SceneNode.h"
#include "SelectionManager.h"
#include "RenderableMesh.h"

#include <QAbstractItemModel>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>
#include <QTreeWidgetItem>
#include <QRegularExpression>
#include <QDebug>
#include <QtGlobal>
#include <algorithm>
// types.h (not matrix4x4.h alone) is needed for aiMatrix4x4t's inline
// constructor definitions (matrix4x4.h only declares them).
#include <assimp/types.h>
#include <functional>
#include <utility>

namespace
{
constexpr float kExplosionOffsetTolerance = 1.0e-4f;
constexpr float kRotationDotTolerance = 1.0e-4f;

struct LocalNodeTransform
{
    QVector3D translation;
    QQuaternion rotation;
    QVector3D scale = QVector3D(1.0f, 1.0f, 1.0f);
};

struct MeshCaptureMotion
{
    QUuid meshUuid;
    SceneNode* ownerNode = nullptr;
    QString sourceFile;
    QMatrix4x4 currentWorld;
};

QMatrix4x4 aiToQMatrix(const aiMatrix4x4& matrix)
{
    QMatrix4x4 out;
    out.setRow(0, QVector4D(matrix.a1, matrix.a2, matrix.a3, matrix.a4));
    out.setRow(1, QVector4D(matrix.b1, matrix.b2, matrix.b3, matrix.b4));
    out.setRow(2, QVector4D(matrix.c1, matrix.c2, matrix.c3, matrix.c4));
    out.setRow(3, QVector4D(matrix.d1, matrix.d2, matrix.d3, matrix.d4));
    return out;
}

aiMatrix4x4 qToAiMatrix(const QMatrix4x4& matrix)
{
    aiMatrix4x4 out;
    out.a1 = matrix(0, 0); out.a2 = matrix(0, 1); out.a3 = matrix(0, 2); out.a4 = matrix(0, 3);
    out.b1 = matrix(1, 0); out.b2 = matrix(1, 1); out.b3 = matrix(1, 2); out.b4 = matrix(1, 3);
    out.c1 = matrix(2, 0); out.c2 = matrix(2, 1); out.c3 = matrix(2, 2); out.c4 = matrix(2, 3);
    out.d1 = matrix(3, 0); out.d2 = matrix(3, 1); out.d3 = matrix(3, 2); out.d4 = matrix(3, 3);
    return out;
}

LocalNodeTransform decomposeLocalNodeTransform(const QMatrix4x4& matrix)
{
    aiVector3D scaling;
    aiQuaternion rotation;
    aiVector3D position;
    qToAiMatrix(matrix).Decompose(scaling, rotation, position);

    LocalNodeTransform result;
    result.translation = QVector3D(position.x, position.y, position.z);
    result.rotation = QQuaternion(rotation.w, rotation.x, rotation.y, rotation.z).normalized();
    result.scale = QVector3D(scaling.x, scaling.y, scaling.z);
    return result;
}

QMatrix4x4 composeLocalNodeTransform(const LocalNodeTransform& transform)
{
    QMatrix4x4 matrix;
    matrix.setToIdentity();
    matrix.translate(transform.translation);
    matrix.rotate(transform.rotation);
    matrix.scale(transform.scale);
    return matrix;
}

bool rotationsNearlyEqual(const QQuaternion& a, const QQuaternion& b)
{
    return std::abs(QQuaternion::dotProduct(a.normalized(), b.normalized())) >= (1.0f - kRotationDotTolerance);
}

bool localTransformsCompatible(const LocalNodeTransform& a, const LocalNodeTransform& b)
{
    return (a.translation - b.translation).lengthSquared() <= kExplosionOffsetTolerance
        && rotationsNearlyEqual(a.rotation, b.rotation);
}

QMatrix4x4 nodeWorldTransform(const SceneGraphWorldTransforms& worlds, const SceneNode* node)
{
    if (!node)
    {
        QMatrix4x4 identity;
        identity.setToIdentity();
        return identity;
    }

    return worlds.nodeWorldByUuid.value(node->nodeUuid, QMatrix4x4());
}

void collectSubtreeMeshUuids(SceneNode* node, QVector<QUuid>& out)
{
    if (!node)
        return;

    for (const QUuid& meshUuid : node->meshUuids)
        out.append(meshUuid);

    for (SceneNode* child : node->children)
        collectSubtreeMeshUuids(child, out);
}

QString sourceFileForNode(SceneNode* node)
{
    for (SceneNode* cur = node; cur; cur = cur->parent)
    {
        if (!cur->sourceFile.isEmpty())
            return cur->sourceFile;
    }
    return QString();
}

bool buildMeshCaptureMotion(ViewportWidget* viewportWidget,
                            SceneNode* ownerNode,
                            SceneMesh* mesh,
                            bool includeAutoPose,
                            bool includeManualPose,
                            MeshCaptureMotion& out)
{
    if (!viewportWidget || !ownerNode || !mesh)
        return false;

    const QVector3D worldOffset = mesh->explosionOffset();
    QMatrix4x4 visibleWorld = mesh->getTransformation() * mesh->getSceneRenderTransform();
    if (includeManualPose)
        visibleWorld = mesh->getExplodedViewTransformation() * visibleWorld;
    if (includeAutoPose && !worldOffset.isNull())
    {
        QMatrix4x4 offsetMatrix;
        offsetMatrix.setToIdentity();
        offsetMatrix.translate(worldOffset);
        visibleWorld = offsetMatrix * visibleWorld;
    }

    QMatrix4x4 currentWorld = visibleWorld;
    const QMatrix4x4 userTransform = mesh->getTransformation();
    if (!userTransform.isIdentity())
    {
        bool invertible = false;
        const QMatrix4x4 userTransformInv = userTransform.inverted(&invertible);
        if (invertible)
            currentWorld = userTransformInv * visibleWorld;
    }

    out.meshUuid = mesh->uuid();
    out.ownerNode = ownerNode;
    out.sourceFile = mesh->getSourceFile().trimmed();
    if (out.sourceFile.isEmpty())
        out.sourceFile = sourceFileForNode(ownerNode);
    out.currentWorld = currentWorld;
    return true;
}

bool inferCandidateLocalEndTransform(const SceneGraphWorldTransforms& worlds,
                                     const MeshCaptureMotion& motion,
                                     SceneNode* candidateNode,
                                     LocalNodeTransform& out)
{
    if (!motion.ownerNode || !candidateNode)
        return false;

    bool invertible = false;
    const QMatrix4x4 candidateWorldBase = nodeWorldTransform(worlds, candidateNode);
    const QMatrix4x4 candidateWorldBaseInv = candidateWorldBase.inverted(&invertible);
    if (!invertible)
        return false;

    const QMatrix4x4 ownerWorldBase = nodeWorldTransform(worlds, motion.ownerNode);
    const QMatrix4x4 candidateToOwnerBase = candidateWorldBaseInv * ownerWorldBase;
    const QMatrix4x4 ownerToCandidateBase = candidateToOwnerBase.inverted(&invertible);
    if (!invertible)
        return false;

    QMatrix4x4 candidateParentWorld;
    candidateParentWorld.setToIdentity();
    if (candidateNode->parent)
        candidateParentWorld = nodeWorldTransform(worlds, candidateNode->parent);

    const QMatrix4x4 candidateParentWorldInv = candidateParentWorld.inverted(&invertible);
    if (!invertible)
        return false;

    out = decomposeLocalNodeTransform(candidateParentWorldInv * motion.currentWorld * ownerToCandidateBase);
    return true;
}

bool inferMeshChildLocalEndTransform(const SceneGraphWorldTransforms& worlds,
                                     const MeshCaptureMotion& motion,
                                     LocalNodeTransform& out)
{
    if (!motion.ownerNode)
        return false;

    const QMatrix4x4 ownerWorldBase = nodeWorldTransform(worlds, motion.ownerNode);
    bool invertible = false;
    const QMatrix4x4 ownerWorldBaseInv = ownerWorldBase.inverted(&invertible);
    if (!invertible)
        return false;

    out = decomposeLocalNodeTransform(ownerWorldBaseInv * motion.currentWorld);
    return true;
}

QString formatPreviewTime(double seconds)
{
    const int totalMs = qMax(0, static_cast<int>(seconds * 1000.0));
    const int minutes = totalMs / 60000;
    const int secs = (totalMs / 1000) % 60;
    const int centiseconds = (totalMs / 10) % 100;
    return QStringLiteral("%1:%2.%3")
        .arg(minutes)
        .arg(secs, 2, 10, QLatin1Char('0'))
        .arg(centiseconds, 2, 10, QLatin1Char('0'));
}

void collectNodeBindings(SceneNode* node,
                         QVector<GltfAnimationNodeBinding>& out,
                         QHash<QUuid, int>& nodeIndexByUuid,
                         const QVector<int>& aiChildPath = {})
{
    if (!node)
        return;

    GltfAnimationNodeBinding binding;
    binding.nodeIndex = out.size();
    binding.nodeName = node->name;
    binding.hasAiChildPath = true;
    binding.aiChildPath = aiChildPath;
    out.append(binding);
    nodeIndexByUuid.insert(node->nodeUuid, binding.nodeIndex);

    for (int childIndex = 0; childIndex < node->children.size(); ++childIndex)
    {
        SceneNode* child = node->children[childIndex];
        QVector<int> childPath = aiChildPath;
        childPath.append(childIndex);
        collectNodeBindings(child, out, nodeIndexByUuid, childPath);
    }
}

SceneNode* findSceneNodeByAiChildPath(SceneNode* aiRootNode, const QVector<int>& aiChildPath)
{
    SceneNode* node = aiRootNode;
    for (int childIndex : aiChildPath)
    {
        if (!node || childIndex < 0 || childIndex >= node->children.size())
            return nullptr;
        node = node->children[childIndex];
    }
    return node;
}

ExplodedViewManager::Mode modeFromComboIndex(int index)
{
    switch (index)
    {
    case 1: return ExplodedViewManager::Mode::AxisX;
    case 2: return ExplodedViewManager::Mode::AxisY;
    case 3: return ExplodedViewManager::Mode::AxisZ;
    case 4: return ExplodedViewManager::Mode::Vector;
    default: return ExplodedViewManager::Mode::Auto;
    }
}

int comboIndexFromMode(ExplodedViewManager::Mode mode)
{
    switch (mode)
    {
    case ExplodedViewManager::Mode::AxisX: return 1;
    case ExplodedViewManager::Mode::AxisY: return 2;
    case ExplodedViewManager::Mode::AxisZ: return 3;
    case ExplodedViewManager::Mode::Vector: return 4;
    case ExplodedViewManager::Mode::Auto:
    default:
        return 0;
    }
}

QString modeToString(ExplodedViewManager::Mode mode)
{
    switch (mode)
    {
    case ExplodedViewManager::Mode::AxisX: return QStringLiteral("AxisX");
    case ExplodedViewManager::Mode::AxisY: return QStringLiteral("AxisY");
    case ExplodedViewManager::Mode::AxisZ: return QStringLiteral("AxisZ");
    case ExplodedViewManager::Mode::Vector: return QStringLiteral("Vector");
    case ExplodedViewManager::Mode::Auto:
    default:
        return QStringLiteral("Auto");
    }
}

ExplodedViewManager::Mode modeFromString(const QString& mode)
{
    if (mode == QLatin1String("AxisX"))
        return ExplodedViewManager::Mode::AxisX;
    if (mode == QLatin1String("AxisY"))
        return ExplodedViewManager::Mode::AxisY;
    if (mode == QLatin1String("AxisZ"))
        return ExplodedViewManager::Mode::AxisZ;
    if (mode == QLatin1String("Vector"))
        return ExplodedViewManager::Mode::Vector;
    return ExplodedViewManager::Mode::Auto;
}

QJsonArray vector3ToJson(const QVector3D& vector)
{
    return QJsonArray{vector.x(), vector.y(), vector.z()};
}

QVector3D vector3FromJson(const QJsonArray& array, const QVector3D& fallback = QVector3D())
{
    if (array.size() < 3)
        return fallback;
    return QVector3D(
        static_cast<float>(array[0].toDouble(fallback.x())),
        static_cast<float>(array[1].toDouble(fallback.y())),
        static_cast<float>(array[2].toDouble(fallback.z())));
}

QJsonArray quaternionToJson(const QQuaternion& quaternion)
{
    return QJsonArray{quaternion.scalar(), quaternion.x(), quaternion.y(), quaternion.z()};
}

QQuaternion quaternionFromJson(const QJsonArray& array,
                               const QQuaternion& fallback = QQuaternion(1.0f, 0.0f, 0.0f, 0.0f))
{
    if (array.size() < 4)
        return fallback;

    return QQuaternion(
        static_cast<float>(array[0].toDouble(fallback.scalar())),
        static_cast<float>(array[1].toDouble(fallback.x())),
        static_cast<float>(array[2].toDouble(fallback.y())),
        static_cast<float>(array[3].toDouble(fallback.z()))).normalized();
}

ExplodedViewPanel::AutoStrategy autoStrategyFromComboIndex(int index)
{
    switch (index)
    {
    case 1:
        return ExplodedViewPanel::AutoStrategy::Classic;
    case 0:
    default:
        return ExplodedViewPanel::AutoStrategy::AssemblyAware;
    }
}

int comboIndexFromAutoStrategy(ExplodedViewPanel::AutoStrategy strategy)
{
    switch (strategy)
    {
    case ExplodedViewPanel::AutoStrategy::Classic:
        return 1;
    case ExplodedViewPanel::AutoStrategy::AssemblyAware:
    default:
        return 0;
    }
}
}

// ---------------------------------------------------------------------------
// Parameter accessors
// ---------------------------------------------------------------------------
ExplodedViewManager::Mode ExplodedViewPanel::mode() const
{
    if (comboBoxMode)
        return modeFromComboIndex(comboBoxMode->currentIndex());
    if (const ExplodedViewPreset* preset = activePreset())
        return preset->mode;
    return ExplodedViewManager::Mode::Auto;
}

ExplodedViewPanel::AutoStrategy ExplodedViewPanel::autoStrategy() const
{
    if (comboBoxAutoStrategy)
        return autoStrategyFromComboIndex(comboBoxAutoStrategy->currentIndex());
    if (const ExplodedViewPreset* preset = activePreset())
        return preset->autoStrategy;
    return AutoStrategy::AssemblyAware;
}

QVector3D ExplodedViewPanel::userVector() const
{
    if (doubleSpinBoxVectorX && doubleSpinBoxVectorY && doubleSpinBoxVectorZ)
    {
        return QVector3D(
            static_cast<float>(doubleSpinBoxVectorX->value()),
            static_cast<float>(doubleSpinBoxVectorY->value()),
            static_cast<float>(doubleSpinBoxVectorZ->value()));
    }
    if (const ExplodedViewPreset* preset = activePreset())
        return preset->userVector;
    return QVector3D(1.0f, 0.0f, 0.0f);
}

float ExplodedViewPanel::factor() const
{
    if (sliderExplosion)
        return sliderExplosion->value() / 100.0f;
    if (const ExplodedViewPreset* preset = activePreset())
        return preset->factor;
    return 1.0f;
}

const QSet<QUuid>& ExplodedViewPanel::assemblyUuids() const
{
    if (const ExplodedViewPreset* preset = activePreset())
        return preset->assemblyUuids;
    return _emptyAssemblyUuids;
}

QUuid ExplodedViewPanel::anchorUuid() const
{
    if (const ExplodedViewPreset* preset = activePreset())
        return preset->anchorUuid;
    return QUuid();
}

ExplodedViewPanel::ExplodedViewPreset* ExplodedViewPanel::activePreset()
{
    if (_activePresetIndex < 0 || _activePresetIndex >= _presets.size())
        return nullptr;
    return &_presets[_activePresetIndex];
}

const ExplodedViewPanel::ExplodedViewPreset* ExplodedViewPanel::activePreset() const
{
    if (_activePresetIndex < 0 || _activePresetIndex >= _presets.size())
        return nullptr;
    return &_presets[_activePresetIndex];
}

ExplodedViewPanel::ExplodedViewPreset& ExplodedViewPanel::ensureActivePreset()
{
    if (_activePresetIndex < 0 || _activePresetIndex >= _presets.size())
    {
        initializeDefaultPreset();
        if (_activePresetIndex < 0 || _activePresetIndex >= _presets.size())
            _activePresetIndex = 0;
    }
    return _presets[_activePresetIndex];
}

QVector<ExplodedViewPanel::CapturedExplosionStep>& ExplodedViewPanel::activeCapturedSteps()
{
    return ensureActivePreset().capturedSteps;
}

const QVector<ExplodedViewPanel::CapturedExplosionStep>& ExplodedViewPanel::activeCapturedSteps() const
{
    static const QVector<CapturedExplosionStep> kEmptySteps;
    if (const ExplodedViewPreset* preset = activePreset())
        return preset->capturedSteps;
    return kEmptySteps;
}

QJsonArray ExplodedViewPanel::presetsToJson()
{
    syncActivePresetManualStateFromRuntime();

    QJsonArray presetsJson;
    for (const ExplodedViewPreset& preset : _presets)
    {
        QJsonObject presetObj;
        presetObj.insert(QStringLiteral("id"), preset.id.toString(QUuid::WithoutBraces));
        presetObj.insert(QStringLiteral("name"), preset.name);

        QJsonArray assemblyArray;
        for (const QUuid& uuid : preset.assemblyUuids)
            assemblyArray.append(uuid.toString(QUuid::WithoutBraces));
        presetObj.insert(QStringLiteral("assemblyUuids"), assemblyArray);
        presetObj.insert(QStringLiteral("anchorUuid"), preset.anchorUuid.toString(QUuid::WithoutBraces));
        presetObj.insert(QStringLiteral("mode"), modeToString(preset.mode));
        presetObj.insert(QStringLiteral("userVector"), vector3ToJson(preset.userVector));
        presetObj.insert(QStringLiteral("factor"), preset.factor);
        QJsonArray manualSelectionArray;
        for (const QUuid& uuid : preset.manualSelectionUuids)
            manualSelectionArray.append(uuid.toString(QUuid::WithoutBraces));
        presetObj.insert(QStringLiteral("manualSelectionUuids"), manualSelectionArray);
        QJsonArray manualStatesArray;
        for (auto stateIt = preset.uncapturedManualStates.cbegin();
             stateIt != preset.uncapturedManualStates.cend(); ++stateIt)
        {
            QJsonObject stateObj;
            stateObj.insert(QStringLiteral("meshUuid"), stateIt.key().toString(QUuid::WithoutBraces));
            stateObj.insert(QStringLiteral("translation"), vector3ToJson(stateIt.value().translation));
            stateObj.insert(QStringLiteral("rotation"), vector3ToJson(stateIt.value().rotation));
            stateObj.insert(QStringLiteral("scale"), vector3ToJson(stateIt.value().scale));
            stateObj.insert(QStringLiteral("rotationQuat"), quaternionToJson(stateIt.value().rotationQuat));
            stateObj.insert(QStringLiteral("hasExactRotation"), stateIt.value().hasExactRotation);
            manualStatesArray.append(stateObj);
        }
        presetObj.insert(QStringLiteral("uncapturedManualStates"), manualStatesArray);
        presetObj.insert(QStringLiteral("capturedStepCounter"), preset.capturedStepCounter);
        presetObj.insert(QStringLiteral("capturedGroupCounter"), preset.capturedGroupCounter);
        presetObj.insert(QStringLiteral("outputMode"), preset.outputMode);
        presetObj.insert(QStringLiteral("durationSeconds"), preset.durationSeconds);
        presetObj.insert(QStringLiteral("loopBack"), preset.loopBack);
        presetObj.insert(QStringLiteral("useCombinedPose"), preset.useCombinedPose);
        presetObj.insert(QStringLiteral("autoStrategy"), comboIndexFromAutoStrategy(preset.autoStrategy));

        const std::function<QJsonObject(const CapturedExplosionStep&)> serializeStep =
            [&](const CapturedExplosionStep& step) -> QJsonObject
        {
            QJsonObject stepObj;
            stepObj.insert(QStringLiteral("id"), step.id.toString(QUuid::WithoutBraces));
            stepObj.insert(QStringLiteral("name"), step.name);
            stepObj.insert(QStringLiteral("isGroup"), step.isGroup);

            QJsonArray tracksArray;
            for (const CapturedTransformTrack& track : step.tracks)
            {
                QJsonObject trackObj;
                trackObj.insert(QStringLiteral("targetKind"), static_cast<int>(track.targetKind));
                trackObj.insert(QStringLiteral("meshUuid"), track.meshUuid.toString(QUuid::WithoutBraces));
                trackObj.insert(QStringLiteral("ownerNodeUuid"), track.ownerNodeUuid.toString(QUuid::WithoutBraces));
                trackObj.insert(QStringLiteral("sourceFile"), track.sourceFile);
                trackObj.insert(QStringLiteral("targetNodeName"), track.targetNodeName);
                trackObj.insert(QStringLiteral("targetNodeIndex"), track.targetNodeIndex);
                trackObj.insert(QStringLiteral("startPosition"), vector3ToJson(track.startPosition));
                trackObj.insert(QStringLiteral("endPosition"), vector3ToJson(track.endPosition));
                trackObj.insert(QStringLiteral("startRotation"), quaternionToJson(track.startRotation));
                trackObj.insert(QStringLiteral("endRotation"), quaternionToJson(track.endRotation));
                tracksArray.append(trackObj);
            }
            stepObj.insert(QStringLiteral("tracks"), tracksArray);

            QJsonArray childArray;
            for (const CapturedExplosionStep& child : step.children)
                childArray.append(serializeStep(child));
            stepObj.insert(QStringLiteral("children"), childArray);
            return stepObj;
        };

        QJsonArray stepsArray;
        for (const CapturedExplosionStep& step : preset.capturedSteps)
            stepsArray.append(serializeStep(step));

        presetObj.insert(QStringLiteral("capturedSteps"), stepsArray);
        presetsJson.append(presetObj);
    }

    return presetsJson;
}

QUuid ExplodedViewPanel::activePresetId() const
{
    const ExplodedViewPreset* preset = activePreset();
    return preset ? preset->id : QUuid();
}

int ExplodedViewPanel::activeCapturedStepIndex() const
{
    return currentCapturedStepRow();
}

void ExplodedViewPanel::restorePresetsFromJson(const QJsonArray& presetsJson,
                                               const QUuid& activePresetId,
                                               int activeStepIndex)
{
    stopDraftPreview();
    cancelPickingMode();
    if (_viewportWidget)
        _viewportWidget->clearExplodedViewManualPlacement();
    clearManualPlacementSelection();

    _presets.clear();
    _activePresetIndex = -1;

    for (const QJsonValue& presetValue : presetsJson)
    {
        const QJsonObject presetObj = presetValue.toObject();
        ExplodedViewPreset preset;
        preset.id = QUuid(presetObj.value(QStringLiteral("id")).toString());
        if (preset.id.isNull())
            preset.id = QUuid::createUuid();
        preset.name = presetObj.value(QStringLiteral("name")).toString().trimmed();
        if (preset.name.isEmpty())
            preset.name = nextPresetName();

        for (const QJsonValue& uuidValue : presetObj.value(QStringLiteral("assemblyUuids")).toArray())
        {
            const QUuid uuid(uuidValue.toString());
            if (!uuid.isNull())
                preset.assemblyUuids.insert(uuid);
        }

        preset.anchorUuid = QUuid(presetObj.value(QStringLiteral("anchorUuid")).toString());
        preset.mode = modeFromString(presetObj.value(QStringLiteral("mode")).toString());
        preset.userVector = vector3FromJson(
            presetObj.value(QStringLiteral("userVector")).toArray(),
            QVector3D(1.0f, 0.0f, 0.0f));
        preset.factor = static_cast<float>(presetObj.value(QStringLiteral("factor")).toDouble(1.0));
        for (const QJsonValue& uuidValue : presetObj.value(QStringLiteral("manualSelectionUuids")).toArray())
        {
            const QUuid uuid(uuidValue.toString());
            if (!uuid.isNull())
                preset.manualSelectionUuids.append(uuid);
        }
        for (const QJsonValue& stateValue : presetObj.value(QStringLiteral("uncapturedManualStates")).toArray())
        {
            const QJsonObject stateObj = stateValue.toObject();
            const QUuid uuid(stateObj.value(QStringLiteral("meshUuid")).toString());
            if (uuid.isNull())
                continue;

            TransformState state;
            state.translation = vector3FromJson(stateObj.value(QStringLiteral("translation")).toArray());
            state.rotation = vector3FromJson(stateObj.value(QStringLiteral("rotation")).toArray());
            state.scale = vector3FromJson(
                stateObj.value(QStringLiteral("scale")).toArray(),
                QVector3D(1.0f, 1.0f, 1.0f));
            state.rotationQuat = quaternionFromJson(stateObj.value(QStringLiteral("rotationQuat")).toArray());
            state.hasExactRotation = stateObj.value(QStringLiteral("hasExactRotation")).toBool(false);
            preset.uncapturedManualStates.insert(uuid, state);
        }
        preset.capturedStepCounter = qMax(1, presetObj.value(QStringLiteral("capturedStepCounter")).toInt(1));
        preset.capturedGroupCounter = qMax(1, presetObj.value(QStringLiteral("capturedGroupCounter")).toInt(1));
        preset.outputMode = presetObj.value(QStringLiteral("outputMode")).toInt(0);
        preset.durationSeconds = presetObj.value(QStringLiteral("durationSeconds")).toDouble(3.0);
        preset.loopBack = presetObj.value(QStringLiteral("loopBack")).toBool(true);
        preset.useCombinedPose = presetObj.value(QStringLiteral("useCombinedPose")).toBool(true);
        preset.autoStrategy = autoStrategyFromComboIndex(
            presetObj.value(QStringLiteral("autoStrategy")).toInt(
                comboIndexFromAutoStrategy(AutoStrategy::AssemblyAware)));

        const std::function<CapturedExplosionStep(const QJsonObject&, int)> deserializeStep =
            [&](const QJsonObject& stepObj, int fallbackIndex) -> CapturedExplosionStep
        {
            CapturedExplosionStep step;
            step.id = QUuid(stepObj.value(QStringLiteral("id")).toString());
            if (step.id.isNull())
                step.id = QUuid::createUuid();
            step.name = stepObj.value(QStringLiteral("name")).toString().trimmed();
            if (step.name.isEmpty())
                step.name = tr("Step %1").arg(fallbackIndex);
            step.isGroup = stepObj.value(QStringLiteral("isGroup")).toBool(false);

            for (const QJsonValue& trackValue : stepObj.value(QStringLiteral("tracks")).toArray())
            {
                const QJsonObject trackObj = trackValue.toObject();
                CapturedTransformTrack track;
                track.targetKind = static_cast<GltfAnimationBindingTargetKind>(
                    trackObj.value(QStringLiteral("targetKind")).toInt(static_cast<int>(GltfAnimationBindingTargetKind::Node)));
                track.meshUuid = QUuid(trackObj.value(QStringLiteral("meshUuid")).toString());
                track.ownerNodeUuid = QUuid(trackObj.value(QStringLiteral("ownerNodeUuid")).toString());
                track.sourceFile = trackObj.value(QStringLiteral("sourceFile")).toString();
                track.targetNodeName = trackObj.value(QStringLiteral("targetNodeName")).toString();
                track.targetNodeIndex = trackObj.value(QStringLiteral("targetNodeIndex")).toInt(-1);
                track.startPosition = vector3FromJson(trackObj.value(QStringLiteral("startPosition")).toArray());
                track.endPosition = vector3FromJson(trackObj.value(QStringLiteral("endPosition")).toArray());
                track.startRotation = quaternionFromJson(trackObj.value(QStringLiteral("startRotation")).toArray());
                track.endRotation = quaternionFromJson(trackObj.value(QStringLiteral("endRotation")).toArray());

                if (track.ownerNodeUuid.isNull() || track.sourceFile.isEmpty())
                    continue;

                step.tracks.append(track);
            }

            int childIndex = 1;
            for (const QJsonValue& childValue : stepObj.value(QStringLiteral("children")).toArray())
            {
                const CapturedExplosionStep child = deserializeStep(childValue.toObject(), childIndex++);
                if (child.isGroup || !child.tracks.isEmpty())
                    step.children.append(child);
            }

            if (!step.children.isEmpty())
                step.isGroup = true;

            return step;
        };

        for (const QJsonValue& stepValue : presetObj.value(QStringLiteral("capturedSteps")).toArray())
        {
            CapturedExplosionStep step = deserializeStep(stepValue.toObject(), preset.capturedSteps.size() + 1);
            if (step.isGroup || !step.tracks.isEmpty())
                preset.capturedSteps.append(step);
        }

        normalizeCapturedGroups(preset.capturedSteps);

        int capturedStepCount = 0;
        int capturedGroupCount = 0;
        const std::function<void(const QVector<CapturedExplosionStep>&)> countSteps =
            [&](const QVector<CapturedExplosionStep>& steps)
        {
            for (const CapturedExplosionStep& step : steps)
            {
                if (step.isGroup)
                {
                    ++capturedGroupCount;
                    countSteps(step.children);
                }
                else
                {
                    ++capturedStepCount;
                }
            }
        };
        countSteps(preset.capturedSteps);
        if (preset.capturedStepCounter <= capturedStepCount)
            preset.capturedStepCounter = capturedStepCount + 1;
        if (preset.capturedGroupCounter <= capturedGroupCount)
            preset.capturedGroupCounter = capturedGroupCount + 1;

        _presets.append(preset);
    }

    if (_presets.isEmpty())
    {
        initializeDefaultPreset();
        return;
    }

    int resolvedIndex = 0;
    if (!activePresetId.isNull())
    {
        for (int index = 0; index < _presets.size(); ++index)
        {
            if (_presets[index].id == activePresetId)
            {
                resolvedIndex = index;
                break;
            }
        }
    }

    refreshPresetCombo();
    loadPresetIntoUi(resolvedIndex);

    if (listWidgetCapturedViews && !activeCapturedSteps().isEmpty())
    {
        const int maxStepIndex = static_cast<int>(activeCapturedSteps().size()) - 1;
        setCurrentCapturedStepRow(activeStepIndex >= 0 ? qBound(0, activeStepIndex, maxStepIndex) : 0);
    }
    else if (listWidgetCapturedViews)
        setCurrentCapturedStepRow(-1);

    updateManualPlacementUi();
    updateCaptureButton();
}

void ExplodedViewPanel::refreshPresetCombo()
{
    if (!comboBoxPreset)
        return;

    QSignalBlocker blocker(comboBoxPreset);
    comboBoxPreset->clear();
    for (const ExplodedViewPreset& preset : std::as_const(_presets))
        comboBoxPreset->addItem(preset.name, preset.id);

    if (_activePresetIndex >= 0 && _activePresetIndex < comboBoxPreset->count())
        comboBoxPreset->setCurrentIndex(_activePresetIndex);
}

void ExplodedViewPanel::syncActivePresetFromUi()
{
    if (_syncingPresetUi)
        return;

    ExplodedViewPreset& preset = ensureActivePreset();
    const ExplodedViewManager::Mode oldMode = preset.mode;
    const QVector3D oldUserVector = preset.userVector;
    const float oldFactor = preset.factor;
    const int oldOutputMode = preset.outputMode;
    const double oldDurationSeconds = preset.durationSeconds;
    const bool oldLoopBack = preset.loopBack;
    const bool oldUseCombinedPose = preset.useCombinedPose;
    const AutoStrategy oldAutoStrategy = preset.autoStrategy;
    preset.mode = modeFromComboIndex(comboBoxMode ? comboBoxMode->currentIndex() : 0);
    preset.autoStrategy = autoStrategyFromComboIndex(comboBoxAutoStrategy ? comboBoxAutoStrategy->currentIndex() : 0);
    preset.userVector = QVector3D(
        static_cast<float>(doubleSpinBoxVectorX ? doubleSpinBoxVectorX->value() : 1.0),
        static_cast<float>(doubleSpinBoxVectorY ? doubleSpinBoxVectorY->value() : 0.0),
        static_cast<float>(doubleSpinBoxVectorZ ? doubleSpinBoxVectorZ->value() : 0.0));
    preset.factor = sliderExplosion ? sliderExplosion->value() / 100.0f : 1.0f;
    preset.outputMode = comboBoxAnimationMode ? comboBoxAnimationMode->currentIndex() : 0;
    preset.durationSeconds = doubleSpinBoxAnimationDuration ? doubleSpinBoxAnimationDuration->value() : 3.0;
    preset.loopBack = checkBoxLoopBack && checkBoxLoopBack->isChecked();
    preset.useCombinedPose = !checkBoxCombinedPose || checkBoxCombinedPose->isChecked();

    const bool changed = preset.mode != oldMode
        || preset.userVector != oldUserVector
        || !qFuzzyCompare(preset.factor, oldFactor)
        || preset.outputMode != oldOutputMode
        || !qFuzzyCompare(1.0 + preset.durationSeconds, 1.0 + oldDurationSeconds)
        || preset.loopBack != oldLoopBack
        || preset.useCombinedPose != oldUseCombinedPose
        || preset.autoStrategy != oldAutoStrategy;
    if (changed)
        markDocumentModified();

    updatePresetDirtyIndicator();
}

QString ExplodedViewPanel::nextPresetName() const
{
    static const QRegularExpression kExplosionNamePattern(QStringLiteral("^Exploded View\\s+(\\d+)$"));

    QSet<int> usedNumbers;
    for (const ExplodedViewPreset& preset : _presets)
    {
        const QRegularExpressionMatch match = kExplosionNamePattern.match(preset.name);
        if (match.hasMatch())
            usedNumbers.insert(match.captured(1).toInt());
    }

    int candidate = 1;
    while (usedNumbers.contains(candidate))
        ++candidate;

    return tr("Exploded View %1").arg(candidate);
}

QString ExplodedViewPanel::nextDuplicatedPresetName(const QString& sourceName) const
{
    const QString baseName = sourceName.trimmed().isEmpty() ? tr("Exploded View") : sourceName.trimmed();
    const QString copyLabel = tr("%1 Copy").arg(baseName);

    QSet<QString> usedNames;
    for (const ExplodedViewPreset& preset : _presets)
        usedNames.insert(preset.name);

    if (!usedNames.contains(copyLabel))
        return copyLabel;

    int candidate = 2;
    while (usedNames.contains(tr("%1 Copy %2").arg(baseName).arg(candidate)))
        ++candidate;

    return tr("%1 Copy %2").arg(baseName).arg(candidate);
}

void ExplodedViewPanel::loadPresetIntoUi(int index)
{
    if (index < 0 || index >= _presets.size())
        return;

    QScopedValueRollback<bool> suspendSync(_suspendingManualPresetSync, true);

    stopDraftPreview();
    cancelPickingMode();
    syncActivePresetManualStateFromRuntime();
    if (_viewportWidget && (_viewportWidget->isExplodedViewManualPlacementActive()
        || _viewportWidget->hasExplodedViewManualPlacement()))
    {
        _viewportWidget->clearExplodedViewManualPlacement();
    }
    clearManualPlacementSelection();

    _activePresetIndex = index;
    const ExplodedViewPreset& preset = _presets[index];

    _syncingPresetUi = true;
    {
        QSignalBlocker b1(comboBoxPreset);
        QSignalBlocker b2(comboBoxMode);
        QSignalBlocker b3(sliderExplosion);
        QSignalBlocker b4(doubleSpinBoxVectorX);
        QSignalBlocker b5(doubleSpinBoxVectorY);
        QSignalBlocker b6(doubleSpinBoxVectorZ);
        QSignalBlocker b7(comboBoxAnimationMode);
        QSignalBlocker b8(doubleSpinBoxAnimationDuration);
        QSignalBlocker b9(checkBoxLoopBack);
        if (comboBoxPreset)
            comboBoxPreset->setCurrentIndex(index);
        if (comboBoxMode)
            comboBoxMode->setCurrentIndex(comboIndexFromMode(preset.mode));
        if (sliderExplosion)
            sliderExplosion->setValue(qRound(preset.factor * 100.0f));
        if (doubleSpinBoxVectorX)
            doubleSpinBoxVectorX->setValue(preset.userVector.x());
        if (doubleSpinBoxVectorY)
            doubleSpinBoxVectorY->setValue(preset.userVector.y());
        if (doubleSpinBoxVectorZ)
            doubleSpinBoxVectorZ->setValue(preset.userVector.z());
        if (comboBoxAnimationMode)
            comboBoxAnimationMode->setCurrentIndex(qBound(0, preset.outputMode, comboBoxAnimationMode->count() - 1));
        if (doubleSpinBoxAnimationDuration)
            doubleSpinBoxAnimationDuration->setValue(preset.durationSeconds);
        if (checkBoxLoopBack)
            checkBoxLoopBack->setChecked(preset.loopBack);
        if (checkBoxCombinedPose)
            checkBoxCombinedPose->setChecked(preset.useCombinedPose);
        if (comboBoxAutoStrategy)
            comboBoxAutoStrategy->setCurrentIndex(comboIndexFromAutoStrategy(preset.autoStrategy));
    }
    _syncingPresetUi = false;

    labelDistancePercent->setText(QString("%1%").arg(qRound(preset.factor * 100.0f)));
    if (frameVector)
        frameVector->setVisible((!radioButtonModeManual || !radioButtonModeManual->isChecked()) && comboBoxMode && comboBoxMode->currentIndex() == 4);

    QList<int> selectionIds;
    selectionIds.reserve(preset.assemblyUuids.size());
    if (_viewportWidget)
    {
        for (const QUuid& uuid : preset.assemblyUuids)
        {
            const int indexByUuid = _viewportWidget->getIndexByUuid(uuid);
            if (indexByUuid >= 0)
                selectionIds.append(indexByUuid);
        }
    }

    if (selectionIds.isEmpty())
    {
        lineEditAssembly->clear();
    }
    else
    {
        const QString selectionText = describeAssemblySelection(selectionIds);
        lineEditAssembly->setText(selectionText);
    }
    restorePresetManualStateIntoRuntime(preset);

    if (!preset.anchorUuid.isNull() && _viewportWidget)
    {
        if (_sceneGraph)
        {
            const SceneNode* anchorNode = _sceneGraph->findNodeForMesh(preset.anchorUuid);
            if (anchorNode)
                lineEditAnchor->setText(anchorNode->name);
            else
                lineEditAnchor->setText(preset.anchorUuid.toString(QUuid::WithoutBraces));
        }
        else
            lineEditAnchor->setText(preset.anchorUuid.toString(QUuid::WithoutBraces));
    }
    else
    {
        lineEditAnchor->clear();
    }

    updateCapturedViewsList();
    if (listWidgetCapturedViews && !preset.capturedSteps.isEmpty())
        setCurrentCapturedStepRow(0);
    _hasUncapturedAutoPose = false;
    updateCaptureButton();
    updateManualPlacementUi();
    updatePreviewControls();
    updatePresetDirtyIndicator();
    emit explosionParametersChanged();
}

void ExplodedViewPanel::initializeDefaultPreset()
{
    if (!_presets.isEmpty())
        return;

    ExplodedViewPreset preset;
    preset.id = QUuid::createUuid();
    preset.name = tr("Exploded View 1");
    preset.loopBack = checkBoxLoopBack ? checkBoxLoopBack->isChecked() : true;
    preset.durationSeconds = doubleSpinBoxAnimationDuration ? doubleSpinBoxAnimationDuration->value() : 3.0;
    preset.outputMode = comboBoxAnimationMode ? comboBoxAnimationMode->currentIndex() : 0;
    _presets.append(preset);
    _activePresetIndex = 0;
    refreshPresetCombo();
    loadPresetIntoUi(0);
}

ModelViewer* ExplodedViewPanel::owningModelViewer() const
{
    for (QWidget* widget = _viewportWidget; widget; widget = widget->parentWidget())
    {
        if (ModelViewer* viewer = qobject_cast<ModelViewer*>(widget))
            return viewer;
    }
    return nullptr;
}

void ExplodedViewPanel::markDocumentModified()
{
    if (ModelViewer* viewer = owningModelViewer())
        viewer->markNonUndoDocumentModified();
}

ExplodedViewPanel::ExplodedViewPanel(ViewportWidget* parent)
    : QWidget(parent)
    , _viewportWidget(parent)
{
    setupUi(this);

    // Without this, switching languages live in Settings left every string
    // in this panel showing whatever language was active when the app
    // started, until the next restart re-ran setupUi() from scratch - see
    // ClippingPlanesEditor's identical connection for the established
    // pattern this app uses instead of overriding changeEvent().
    connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
        retranslateUi(this);
        });

    _draftPreviewTimer = new QTimer(this);
    _draftPreviewTimer->setInterval(16);
    frameVector->setVisible(false);
    _draftPreviewLoopPlayback = true;
    checkBoxPreviewLoop->setChecked(_draftPreviewLoopPlayback);
    _assemblySelectIdleIcon = pushButtonSelectAssembly->icon();
    _assemblySelectCommitIcon = QIcon(":/icons/res/checkmark.png");
    if (_assemblySelectCommitIcon.isNull())
        _assemblySelectCommitIcon = style()->standardIcon(QStyle::SP_DialogApplyButton);
    updateAssemblyPickButtonVisual(false);
    if (pushButtonPresetNew)
        pushButtonPresetNew->setEnabled(true);
    if (pushButtonPresetDuplicate)
        pushButtonPresetDuplicate->setEnabled(true);
    if (pushButtonPresetActions)
        pushButtonPresetActions->setEnabled(true);

    auto emitParamChanged = [this]() { emit explosionParametersChanged(); };
    connect(doubleSpinBoxVectorX, qOverload<double>(&QDoubleSpinBox::valueChanged), this, emitParamChanged);
    connect(doubleSpinBoxVectorY, qOverload<double>(&QDoubleSpinBox::valueChanged), this, emitParamChanged);
    connect(doubleSpinBoxVectorZ, qOverload<double>(&QDoubleSpinBox::valueChanged), this, emitParamChanged);
    connect(_draftPreviewTimer, &QTimer::timeout, this, &ExplodedViewPanel::onDraftPreviewTick);
    if (_viewportWidget)
    {
        connect(_viewportWidget, &ViewportWidget::selectionChanged, this, [this](const QList<int>&) {
            updateCaptureButton();
        });
        connect(_viewportWidget, &ViewportWidget::explodedViewManualPlacementChanged, this, [this]() {
            if (_suspendingManualPresetSync)
                return;
            const bool changed = syncActivePresetManualStateFromRuntime();
            if (changed)
                markDocumentModified();
            updatePresetDirtyIndicator();
        });
    }
    if (ModelViewer* viewer = owningModelViewer())
    {
        connect(viewer, &ModelViewer::documentModifiedChanged, this, [this](bool) {
            updatePresetDirtyIndicator();
        });
    }

    lineEditAssembly->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(lineEditAssembly, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (lineEditAssembly->text().isEmpty())
            return;
        QMenu menu(this);
        applyPopupMenuStyle(menu);
        connect(menu.addAction(tr("Edit Selection...")), &QAction::triggered, this, [this]() {
            showExplodedViewSelectionEditor();
        });
        menu.addSeparator();
        connect(menu.addAction(tr("Clear Selection")), &QAction::triggered, this, [this]() {
            cancelPickingMode();
            clearAssemblySelection();
            {
                QSignalBlocker b1(pushButtonSelectAssembly);
                QSignalBlocker b2(pushButtonSelectAnchor);
                pushButtonSelectAssembly->setChecked(false);
                pushButtonSelectAnchor->setChecked(false);
            }
            updateAssemblyPickButtonVisual(false);
            updateCaptureButton();
            markDocumentModified();
            emit explosionParametersChanged();
            emit selectionClearRequested();
        });
        menu.exec(lineEditAssembly->mapToGlobal(pos));
    });

    if (lineEditManualSelection)
    {
        lineEditManualSelection->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(lineEditManualSelection, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
            if (lineEditManualSelection->text().isEmpty())
                return;
            QMenu menu(this);
            applyPopupMenuStyle(menu);
            connect(menu.addAction(tr("Clear Selection")), &QAction::triggered, this, [this]() {
                stopDraftPreview();
                if (_viewportWidget && (_viewportWidget->isExplodedViewManualPlacementActive()
                    || _viewportWidget->hasExplodedViewManualPlacement()
                    || _viewportWidget->hasExplodedViewManualTransformChanges()))
                {
                    _viewportWidget->clearExplodedViewManualPlacement();
                }
                clearManualPlacementSelection();
                syncActivePresetManualStateFromRuntime();
                updateManualPlacementUi();
                updateCaptureButton();
                updatePreviewControls();
                markDocumentModified();
                emit selectionClearRequested();
            });
            menu.exec(lineEditManualSelection->mapToGlobal(pos));
        });
    }

    lineEditAnchor->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(lineEditAnchor, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (lineEditAnchor->text().isEmpty())
            return;
        QMenu menu(this);
        applyPopupMenuStyle(menu);
        connect(menu.addAction(tr("Clear Anchor")), &QAction::triggered, this, [this]() {
            lineEditAnchor->clear();
            ensureActivePreset().anchorUuid = QUuid();
            markDocumentModified();
            pushButtonSelectAnchor->setChecked(false);
            emit explosionParametersChanged();
        });
        menu.exec(lineEditAnchor->mapToGlobal(pos));
    });

    if (listWidgetCapturedViews)
    {
        listWidgetCapturedViews->setSelectionMode(QAbstractItemView::ExtendedSelection);
        listWidgetCapturedViews->setContextMenuPolicy(Qt::CustomContextMenu);
        listWidgetCapturedViews->setDragEnabled(false);
        listWidgetCapturedViews->setAcceptDrops(false);
        listWidgetCapturedViews->setDropIndicatorShown(false);
        listWidgetCapturedViews->setDragDropMode(QAbstractItemView::NoDragDrop);
        listWidgetCapturedViews->setDefaultDropAction(Qt::IgnoreAction);
        listWidgetCapturedViews->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
        connect(listWidgetCapturedViews, &QTreeWidget::currentItemChanged,
                this, [this](QTreeWidgetItem*, QTreeWidgetItem*) {
            stopDraftPreview();
            updateCaptureButton();
            updatePreviewControls();
        });
        connect(listWidgetCapturedViews, &QTreeWidget::itemChanged,
                this, &ExplodedViewPanel::onCapturedViewItemChanged);
        connect(listWidgetCapturedViews, &QWidget::customContextMenuRequested,
                this, &ExplodedViewPanel::onCapturedViewsContextMenuRequested);
    }

    connect(radioButtonModeAuto, &QRadioButton::toggled, this, [this](bool) {
        updateAuthoringModeUi();
    });
    connect(radioButtonModeManual, &QRadioButton::toggled, this, [this](bool) {
        updateAuthoringModeUi();
    });
    connect(pushButtonPreviewPlayPause, &QPushButton::clicked,
            this, &ExplodedViewPanel::onPreviewPlayPauseClicked);
    connect(pushButtonPreviewStop, &QPushButton::clicked,
            this, &ExplodedViewPanel::onPreviewStopClicked);
    connect(checkBoxPreviewLoop, &QCheckBox::toggled,
            this, &ExplodedViewPanel::onPreviewLoopToggled);
    connect(sliderPreviewTimeline, &QSlider::sliderPressed,
            this, &ExplodedViewPanel::onPreviewSliderPressed);
    connect(sliderPreviewTimeline, &QSlider::sliderReleased,
            this, &ExplodedViewPanel::onPreviewSliderReleased);
    connect(sliderPreviewTimeline, &QSlider::valueChanged,
            this, &ExplodedViewPanel::onPreviewSliderValueChanged);
    connect(comboBoxAnimationMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        stopDraftPreview();
        updatePreviewControls();
    });
    connect(doubleSpinBoxAnimationDuration, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        stopDraftPreview();
        updatePreviewControls();
    });
    connect(checkBoxLoopBack, &QCheckBox::toggled, this, [this](bool) {
        syncActivePresetFromUi();
        stopDraftPreview();
        updatePreviewControls();
    });
    if (comboBoxPreset)
    {
        connect(comboBoxPreset, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
            if (_syncingPresetUi)
                return;
            loadPresetIntoUi(index);
        });
    }
    connect(comboBoxMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        _hasUncapturedAutoPose = true;
        syncActivePresetFromUi();
    });
    connect(sliderExplosion, &QSlider::valueChanged, this, [this](int) {
        _hasUncapturedAutoPose = true;
        syncActivePresetFromUi();
    });
    connect(doubleSpinBoxVectorX, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        _hasUncapturedAutoPose = true;
        syncActivePresetFromUi();
    });
    connect(doubleSpinBoxVectorY, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        _hasUncapturedAutoPose = true;
        syncActivePresetFromUi();
    });
    connect(doubleSpinBoxVectorZ, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        _hasUncapturedAutoPose = true;
        syncActivePresetFromUi();
    });
    connect(comboBoxAnimationMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        syncActivePresetFromUi();
    });
    connect(doubleSpinBoxAnimationDuration, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        syncActivePresetFromUi();
    });
    connect(checkBoxCombinedPose, &QCheckBox::toggled, this, [this](bool) {
        syncActivePresetFromUi();
        updateCaptureButton();
    });
    if (comboBoxAutoStrategy)
    {
        connect(comboBoxAutoStrategy, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
            _hasUncapturedAutoPose = true;
            syncActivePresetFromUi();
            emit explosionParametersChanged();
        });
    }
    auto applyManualTranslationEditors = [this](double) {
        if (_syncingManualPlacementEditors || !_viewportWidget)
            return;
        _viewportWidget->setExplodedViewManualPlacementTranslationDelta(QVector3D(
            static_cast<float>(doubleSpinBoxManualDX->value()),
            static_cast<float>(doubleSpinBoxManualDY->value()),
            static_cast<float>(doubleSpinBoxManualDZ->value())));
        updateCaptureButton();
    };
    auto applyManualRotationEditors = [this](double) {
        if (_syncingManualPlacementEditors || !_viewportWidget)
            return;
        _viewportWidget->setExplodedViewManualPlacementRotationDelta(QVector3D(
            static_cast<float>(doubleSpinBoxManualRX->value()),
            static_cast<float>(doubleSpinBoxManualRY->value()),
            static_cast<float>(doubleSpinBoxManualRZ->value())));
        updateCaptureButton();
    };
    connect(doubleSpinBoxManualDX, qOverload<double>(&QDoubleSpinBox::valueChanged), this, applyManualTranslationEditors);
    connect(doubleSpinBoxManualDY, qOverload<double>(&QDoubleSpinBox::valueChanged), this, applyManualTranslationEditors);
    connect(doubleSpinBoxManualDZ, qOverload<double>(&QDoubleSpinBox::valueChanged), this, applyManualTranslationEditors);
    connect(doubleSpinBoxManualRX, qOverload<double>(&QDoubleSpinBox::valueChanged), this, applyManualRotationEditors);
    connect(doubleSpinBoxManualRY, qOverload<double>(&QDoubleSpinBox::valueChanged), this, applyManualRotationEditors);
    connect(doubleSpinBoxManualRZ, qOverload<double>(&QDoubleSpinBox::valueChanged), this, applyManualRotationEditors);
    connect(_viewportWidget, &ViewportWidget::explodedViewManualPlacementChanged,
            this, &ExplodedViewPanel::updateManualPlacementEditors);

    initializeDefaultPreset();
    updateAuthoringModeUi();
    updatePreviewControls();
    updateCaptureButton();
    updatePresetDirtyIndicator();
}

void ExplodedViewPanel::setSceneGraph(SceneGraph* sg)
{
    stopDraftPreview();
    _sceneGraph = sg;
    updatePreviewControls();
}

void ExplodedViewPanel::applyContrastTheme(const QColor& textColor)
{
    const QString panelStyle = QString("color: rgb(%1, %2, %3);")
        .arg(textColor.red())
        .arg(textColor.green())
        .arg(textColor.blue());
    setStyleSheet(panelStyle);

    const QString blackTextStyle = QStringLiteral("color: rgb(0, 0, 0);");
    const QString translucentInputStyle = QStringLiteral("background-color: rgba(255, 255, 255, 10%); color: rgb(0, 0, 0);");
    const QString translucentFrameStyle = QStringLiteral("background-color: rgba(255, 255, 255, 5%); color: rgb(0, 0, 0);");
    const QString radioIndicatorStyle = QString(
        "QRadioButton { color: rgb(%1, %2, %3); }"
        "QRadioButton::indicator {"
        " width: 13px;"
        " height: 13px;"
        " border-radius: 6.5px;"
        " border: 1px solid rgba(%1, %2, %3, 220);"
        " background-color: transparent;"
        "}"
        "QRadioButton::indicator:checked {"
        " border: 1px solid rgba(%1, %2, %3, 220);"
        " background-color: qradialgradient("
        "   cx:0.5, cy:0.5, radius:0.5, fx:0.5, fy:0.5,"
        "   stop:0 rgb(%1, %2, %3),"
        "   stop:0.6 rgb(%1, %2, %3),"
        "   stop:0.65 transparent,"
        "   stop:1 transparent);"
        "}"
        "QRadioButton::indicator:unchecked:hover,"
        "QRadioButton::indicator:checked:hover {"
        " border: 1px solid rgb(%1, %2, %3);"
        "}")
        .arg(textColor.red())
        .arg(textColor.green())
        .arg(textColor.blue());

    if (comboBoxPreset) comboBoxPreset->setStyleSheet(translucentInputStyle);
    if (pushButtonPresetNew) pushButtonPresetNew->setStyleSheet(translucentInputStyle);
    if (pushButtonPresetDuplicate) pushButtonPresetDuplicate->setStyleSheet(translucentInputStyle);
    if (pushButtonPresetActions) pushButtonPresetActions->setStyleSheet(translucentInputStyle);

    if (lineEditAssembly) lineEditAssembly->setStyleSheet(translucentInputStyle);
    if (lineEditAnchor) lineEditAnchor->setStyleSheet(translucentInputStyle);
    if (comboBoxMode) comboBoxMode->setStyleSheet(translucentInputStyle);
    if (pushButtonSelectAssembly) pushButtonSelectAssembly->setStyleSheet(translucentInputStyle);
    if (pushButtonSelectAnchor) pushButtonSelectAnchor->setStyleSheet(translucentInputStyle);
    if (pushButtonSelectVector) pushButtonSelectVector->setStyleSheet(translucentInputStyle);

    if (frameVector) frameVector->setStyleSheet(translucentFrameStyle);
    if (doubleSpinBoxVectorX) doubleSpinBoxVectorX->setStyleSheet(translucentInputStyle);
    if (doubleSpinBoxVectorY) doubleSpinBoxVectorY->setStyleSheet(translucentInputStyle);
    if (doubleSpinBoxVectorZ) doubleSpinBoxVectorZ->setStyleSheet(translucentInputStyle);

    if (frameManualPlacement) frameManualPlacement->setStyleSheet(translucentFrameStyle);
    if (lineEditManualSelection) lineEditManualSelection->setStyleSheet(translucentInputStyle);
    if (pushButtonStartManualPlacement) pushButtonStartManualPlacement->setStyleSheet(translucentInputStyle);
    if (pushButtonFinishManualPlacement) pushButtonFinishManualPlacement->setStyleSheet(translucentInputStyle);
    if (pushButtonClearManualPlacement) pushButtonClearManualPlacement->setStyleSheet(translucentInputStyle);
    if (doubleSpinBoxManualDX) doubleSpinBoxManualDX->setStyleSheet(translucentInputStyle);
    if (doubleSpinBoxManualDY) doubleSpinBoxManualDY->setStyleSheet(translucentInputStyle);
    if (doubleSpinBoxManualDZ) doubleSpinBoxManualDZ->setStyleSheet(translucentInputStyle);
    if (doubleSpinBoxManualRX) doubleSpinBoxManualRX->setStyleSheet(translucentInputStyle);
    if (doubleSpinBoxManualRY) doubleSpinBoxManualRY->setStyleSheet(translucentInputStyle);
    if (doubleSpinBoxManualRZ) doubleSpinBoxManualRZ->setStyleSheet(translucentInputStyle);

    if (doubleSpinBoxAnimationDuration) doubleSpinBoxAnimationDuration->setStyleSheet(translucentInputStyle);
    if (comboBoxAnimationMode) comboBoxAnimationMode->setStyleSheet(translucentInputStyle);

    if (pushButtonCaptureView) pushButtonCaptureView->setStyleSheet(translucentInputStyle);
    if (pushButtonReplaceCapture) pushButtonReplaceCapture->setStyleSheet(translucentInputStyle);
    if (pushButtonRemoveCapture) pushButtonRemoveCapture->setStyleSheet(translucentInputStyle);
    if (listWidgetCapturedViews) listWidgetCapturedViews->setStyleSheet(translucentInputStyle);

    if (pushButtonPreviewPlayPause) pushButtonPreviewPlayPause->setStyleSheet(translucentInputStyle);
    if (pushButtonPreviewStop) pushButtonPreviewStop->setStyleSheet(translucentInputStyle);

    if (pushButtonCapture) pushButtonCapture->setStyleSheet(translucentInputStyle);
    if (pushButtonReset) pushButtonReset->setStyleSheet(translucentInputStyle);

    if (radioButtonModeAuto) radioButtonModeAuto->setStyleSheet(radioIndicatorStyle);
    if (radioButtonModeManual) radioButtonModeManual->setStyleSheet(radioIndicatorStyle);
    if (checkBoxLoopBack) checkBoxLoopBack->setStyleSheet(blackTextStyle);
    if (checkBoxPreviewLoop) checkBoxPreviewLoop->setStyleSheet(blackTextStyle);
}

void ExplodedViewPanel::applyBackgroundTheme(const QColor& topColor, const QColor& bottomColor)
{
    const QColor averageBackgroundColor(
        (topColor.red() + bottomColor.red()) / 2,
        (topColor.green() + bottomColor.green()) / 2,
        (topColor.blue() + bottomColor.blue()) / 2,
        (topColor.alpha() + bottomColor.alpha()) / 2);
    const QColor contrastColor = (averageBackgroundColor.lightnessF() < 0.5)
        ? QColor(255, 255, 255)
        : QColor(0, 0, 0);

    const bool darkBackground = averageBackgroundColor.lightnessF() < 0.5;
    const QColor menuBg = darkBackground ? QColor(34, 34, 34, 255) : QColor(248, 248, 248, 255);
    const QColor menuText = darkBackground ? QColor(255, 255, 255) : QColor(0, 0, 0);
    const QColor menuBorder = darkBackground ? QColor(90, 90, 90, 255) : QColor(170, 170, 170, 255);
    const QColor menuHover = darkBackground ? QColor(70, 70, 70, 255) : QColor(222, 222, 222, 255);
    _popupMenuStyleSheet = QString(
        "QMenu {"
        " background-color: rgba(%1, %2, %3, %4);"
        " color: rgb(%5, %6, %7);"
        " border: 1px solid rgba(%8, %9, %10, %11);"
        " padding: 4px;"
        "}"
        "QMenu::item {"
        " background-color: transparent;"
        " color: rgb(%5, %6, %7);"
        " padding: 6px 24px 6px 12px;"
        "}"
        "QMenu::item:selected {"
        " background-color: rgba(%12, %13, %14, %15);"
        " color: rgb(%5, %6, %7);"
        "}"
        "QMenu::separator {"
        " height: 1px;"
        " background: rgba(%8, %9, %10, %11);"
        " margin: 4px 8px;"
        "}")
        .arg(menuBg.red()).arg(menuBg.green()).arg(menuBg.blue()).arg(menuBg.alpha())
        .arg(menuText.red()).arg(menuText.green()).arg(menuText.blue())
        .arg(menuBorder.red()).arg(menuBorder.green()).arg(menuBorder.blue()).arg(menuBorder.alpha())
        .arg(menuHover.red()).arg(menuHover.green()).arg(menuHover.blue()).arg(menuHover.alpha());

    applyContrastTheme(contrastColor);
}

void ExplodedViewPanel::applyPopupMenuStyle(QMenu& menu) const
{
    if (!_popupMenuStyleSheet.isEmpty())
        menu.setStyleSheet(_popupMenuStyleSheet);
}

void ExplodedViewPanel::deactivateInteractiveState()
{
    stopDraftPreview();
    cancelPickingMode();
    {
        QSignalBlocker b1(pushButtonSelectAssembly);
        QSignalBlocker b2(pushButtonSelectAnchor);
        pushButtonSelectAssembly->setChecked(false);
        pushButtonSelectAnchor->setChecked(false);
    }
    updateAssemblyPickButtonVisual(false);
    updateCaptureButton();
}

void ExplodedViewPanel::captureCurrentSelection()
{
    if (!_viewportWidget)
        return;
    const QList<int> ids = _viewportWidget->getSelectionManager()->getSelectedIds();
    if (!ids.isEmpty())
        applyAssemblySelection(ids);
}

// ---------------------------------------------------------------------------
// Picking mode
// ---------------------------------------------------------------------------
void ExplodedViewPanel::on_pushButtonSelectAssembly_toggled(bool checked)
{
    if (checked) {
        {
            QSignalBlocker b(pushButtonSelectAnchor);
            pushButtonSelectAnchor->setChecked(false);
        }
        cancelPickingMode();

        _pickingTarget = PickingTarget::Assembly;
        lineEditAssembly->setPlaceholderText(tr("Add meshes, then click again to confirm..."));
        updateAssemblyPickButtonVisual(true);

        _pickingConn = connect(_viewportWidget, &ViewportWidget::selectionChanged,
                               this, &ExplodedViewPanel::onPickingSelectionChanged);
    } else {
        const bool committingSelection = (_pickingTarget == PickingTarget::Assembly);
        cancelPickingMode();
        lineEditAssembly->setPlaceholderText(tr("Select assembly or meshes..."));
        updateAssemblyPickButtonVisual(false);

        if (committingSelection)
        {
            mergeAssemblySelection(_viewportWidget->getSelectionManager()->getSelectedIds());
            emit selectionClearRequested();
            updateCaptureButton();
            if (_reopenAssemblyEditDialogAfterPick)
            {
                _reopenAssemblyEditDialogAfterPick = false;
                _assemblyEditPickActive = false;
                reopenExplodedViewSelectionEditor();
            }
        }
    }
}

void ExplodedViewPanel::on_pushButtonSelectAnchor_toggled(bool checked)
{
    if (checked) {
        {
            QSignalBlocker b(pushButtonSelectAssembly);
            pushButtonSelectAssembly->setChecked(false);
        }
        cancelPickingMode();
        updateAssemblyPickButtonVisual(false);

        _pickingTarget = PickingTarget::Anchor;
        lineEditAnchor->setPlaceholderText(tr("Click mesh or node in scene..."));

        _pickingConn = connect(_viewportWidget, &ViewportWidget::selectionChanged,
                               this, &ExplodedViewPanel::onPickingSelectionChanged);
    } else {
        cancelPickingMode();
        lineEditAnchor->setPlaceholderText(tr("Select anchor mesh (optional)..."));
    }
}

void ExplodedViewPanel::onPickingSelectionChanged(const QList<int>& ids)
{
    if (_pickingTarget == PickingTarget::Assembly)
    {
        updateAssemblySelectionDisplay(ids);
        updateCaptureButton();
        return;
    }

    if (ids.isEmpty())
        return;

    disconnect(_pickingConn);

    if (_pickingTarget == PickingTarget::Anchor)
    {
        applyAnchorSelection(ids);
        pushButtonSelectAnchor->setChecked(false);
        lineEditAnchor->setPlaceholderText(tr("Select anchor mesh (optional)..."));
    }

    _pickingTarget = PickingTarget::None;
    emit selectionClearRequested();
    updateCaptureButton();
}

void ExplodedViewPanel::cancelPickingMode()
{
    if (_pickingTarget != PickingTarget::None) {
        disconnect(_pickingConn);
        _pickingTarget = PickingTarget::None;
    }
    if (!_reopenAssemblyEditDialogAfterPick)
        _assemblyEditPickActive = false;
}

// ---------------------------------------------------------------------------
// Selection helpers
// ---------------------------------------------------------------------------
void ExplodedViewPanel::applyAssemblySelection(const QList<int>& ids)
{
    if (ids.isEmpty())
    {
        clearAssemblySelection();
        _hasUncapturedAutoPose = false;
        markDocumentModified();
        emit explosionParametersChanged();
        return;
    }

    ExplodedViewPreset& preset = ensureActivePreset();
    preset.assemblyUuids.clear();
    for (int id : ids)
        preset.assemblyUuids.insert(_viewportWidget->getUuidByIndex(id));

    updateAssemblySelectionDisplay(ids);

    if (!preset.anchorUuid.isNull() && !preset.assemblyUuids.contains(preset.anchorUuid)) {
        lineEditAnchor->clear();
        preset.anchorUuid = QUuid();
    }

    _hasUncapturedAutoPose = true;
    markDocumentModified();
    emit explosionParametersChanged();
}

void ExplodedViewPanel::mergeAssemblySelection(const QList<int>& ids)
{
    if (ids.isEmpty() || !_viewportWidget)
        return;

    if (_assemblyEditPickActive)
    {
        for (int id : ids)
        {
            const QUuid uuid = _viewportWidget->getUuidByIndex(id);
            if (!uuid.isNull() && !_assemblyEditWorkingUuids.contains(uuid))
                _assemblyEditWorkingUuids.append(uuid);
        }
        return;
    }

    QSet<QUuid> mergedUuids = ensureActivePreset().assemblyUuids;
    for (int id : ids)
    {
        const QUuid uuid = _viewportWidget->getUuidByIndex(id);
        if (!uuid.isNull())
            mergedUuids.insert(uuid);
    }

    applyAssemblyEntries(QVector<QUuid>(mergedUuids.begin(), mergedUuids.end()));
}

void ExplodedViewPanel::updateAssemblySelectionDisplay(const QList<int>& ids)
{
    if (ids.isEmpty())
    {
        lineEditAssembly->clear();
        return;
    }

    const QString selectionText = describeAssemblySelection(ids);
    lineEditAssembly->setText(selectionText);
}

void ExplodedViewPanel::applyManualPlacementSelection(const QList<int>& ids)
{
    QVector<QUuid> selectionUuids;
    selectionUuids.reserve(ids.size());
    for (int id : ids)
    {
        const QUuid uuid = _viewportWidget ? _viewportWidget->getUuidByIndex(id) : QUuid();
        if (!uuid.isNull() && !selectionUuids.contains(uuid))
            selectionUuids.append(uuid);
    }
    applyManualPlacementEntries(selectionUuids);
}

void ExplodedViewPanel::applyManualPlacementEntries(const QVector<QUuid>& selectionUuids)
{
    _manualPlacementSelectionUuids.clear();
    for (const QUuid& uuid : selectionUuids)
    {
        if (uuid.isNull() || _manualPlacementSelectionUuids.contains(uuid))
            continue;
        if (_viewportWidget && _viewportWidget->getIndexByUuid(uuid) < 0)
            continue;
        if (!_manualPlacementSelectionUuids.contains(uuid))
            _manualPlacementSelectionUuids.append(uuid);
    }
    updateManualPlacementSelectionDisplay();
    if (syncActivePresetManualStateFromRuntime())
        markDocumentModified();
}

void ExplodedViewPanel::updateManualPlacementSelectionDisplay()
{
    if (!lineEditManualSelection)
        return;

    QList<int> ids;
    ids.reserve(_manualPlacementSelectionUuids.size());
    if (_viewportWidget)
    {
        for (const QUuid& uuid : std::as_const(_manualPlacementSelectionUuids))
        {
            const int index = _viewportWidget->getIndexByUuid(uuid);
            if (index >= 0)
                ids.append(index);
        }
    }

    if (ids.isEmpty())
    {
        lineEditManualSelection->clear();
        return;
    }

    lineEditManualSelection->setText(describeAssemblySelection(ids));
}

void ExplodedViewPanel::clearManualPlacementSelection()
{
    _manualPlacementSelectionUuids.clear();
    if (lineEditManualSelection)
        lineEditManualSelection->clear();
}

bool ExplodedViewPanel::syncActivePresetManualStateFromRuntime()
{
    ExplodedViewPreset* preset = activePreset();
    if (!preset)
        return false;

    const auto statesEqual = [](const QMap<QUuid, TransformState>& a,
                                const QMap<QUuid, TransformState>& b) {
        if (a.size() != b.size())
            return false;
        for (auto it = a.cbegin(); it != a.cend(); ++it)
        {
            auto jt = b.find(it.key());
            if (jt == b.end())
                return false;
            const TransformState& lhs = it.value();
            const TransformState& rhs = jt.value();
            if (lhs.translation != rhs.translation
                || lhs.rotation != rhs.rotation
                || lhs.scale != rhs.scale
                || lhs.rotationQuat != rhs.rotationQuat
                || lhs.hasExactRotation != rhs.hasExactRotation)
            {
                return false;
            }
        }
        return true;
    };

    const QVector<QUuid> oldSelection = preset->manualSelectionUuids;
    const QMap<QUuid, TransformState> oldStates = preset->uncapturedManualStates;

    preset->manualSelectionUuids = _manualPlacementSelectionUuids;
    preset->uncapturedManualStates = _viewportWidget ? _viewportWidget->explodedViewManualStates()
                                               : QMap<QUuid, TransformState>{};

    _hasUncapturedManualPose = !preset->uncapturedManualStates.isEmpty();
    return preset->manualSelectionUuids != oldSelection
        || !statesEqual(preset->uncapturedManualStates, oldStates);
}

void ExplodedViewPanel::restorePresetManualStateIntoRuntime(const ExplodedViewPreset& preset)
{
    _manualPlacementSelectionUuids.clear();
    for (const QUuid& uuid : preset.manualSelectionUuids)
    {
        if (!uuid.isNull())
            _manualPlacementSelectionUuids.append(uuid);
    }
    updateManualPlacementSelectionDisplay();

    if (_viewportWidget)
        _viewportWidget->restoreExplodedViewManualStates(preset.uncapturedManualStates);

    _hasUncapturedManualPose = !preset.uncapturedManualStates.isEmpty();
}

void ExplodedViewPanel::applyAnchorSelection(const QList<int>& ids)
{
    const QUuid uuid = _viewportWidget->getUuidByIndex(ids.first());

    ExplodedViewPreset& preset = ensureActivePreset();
    if (!preset.assemblyUuids.isEmpty() && !preset.assemblyUuids.contains(uuid)) {
        lineEditAnchor->setPlaceholderText(tr("Anchor must be within the assembly"));
        return;
    }

    preset.anchorUuid = uuid;

    QString name;
    if (_sceneGraph) {
        const SceneNode* node = _sceneGraph->findNodeForMesh(uuid);
        name = node ? node->name : tr("Mesh");
    } else {
        name = tr("Mesh");
    }
    lineEditAnchor->setText(name);
    markDocumentModified();
    emit explosionParametersChanged();
}

QVector<QUuid> ExplodedViewPanel::orderedAssemblyUuids() const
{
    QVector<QUuid> ordered;
    if (const ExplodedViewPreset* preset = activePreset())
    {
        ordered.reserve(preset->assemblyUuids.size());
        for (const QUuid& uuid : preset->assemblyUuids)
            ordered.append(uuid);
    }

    std::sort(ordered.begin(), ordered.end(), [this](const QUuid& a, const QUuid& b) {
        return displayLabelForMeshUuid(a).localeAwareCompare(displayLabelForMeshUuid(b)) < 0;
    });
    return ordered;
}

QString ExplodedViewPanel::displayLabelForMeshUuid(const QUuid& uuid) const
{
    if (uuid.isNull())
        return tr("Unknown");

    if (_sceneGraph)
    {
        if (const SceneNode* node = _sceneGraph->findNodeForMesh(uuid))
        {
            if (!node->name.trimmed().isEmpty())
                return node->name;
        }
    }

    if (_viewportWidget)
    {
        if (SceneMesh* mesh = _viewportWidget->getMeshByUuid(uuid))
        {
            const QString meshName = mesh->getName();
            if (!meshName.trimmed().isEmpty())
                return meshName;
        }
    }

    return uuid.toString(QUuid::WithoutBraces);
}

void ExplodedViewPanel::showExplodedViewSelectionEditor()
{
    _assemblyEditWorkingUuids = orderedAssemblyUuids();
    reopenExplodedViewSelectionEditor();
}

void ExplodedViewPanel::reopenExplodedViewSelectionEditor()
{
    if (_explodedViewSelectionEditor)
    {
        _explodedViewSelectionEditor->raise();
        _explodedViewSelectionEditor->activateWindow();
        return;
    }

    _explodedViewSelectionEditor = new ExplodedViewSelectionEditor(window());
    _explodedViewSelectionEditor->setAttribute(Qt::WA_DeleteOnClose);
    _explodedViewSelectionEditor->setModal(false);
    _explodedViewSelectionEditor->setWindowModality(Qt::NonModal);

    QVector<ExplodedViewSelectionEditor::Entry> entries;
    entries.reserve(_assemblyEditWorkingUuids.size());
    for (const QUuid& uuid : std::as_const(_assemblyEditWorkingUuids))
        entries.append({uuid, displayLabelForMeshUuid(uuid)});
    _explodedViewSelectionEditor->setEntries(entries);

    connect(_explodedViewSelectionEditor, &ExplodedViewSelectionEditor::previewEntryRequested,
            this, &ExplodedViewPanel::previewAssemblyEntry);
    connect(_explodedViewSelectionEditor, &QDialog::finished,
            this, &ExplodedViewPanel::onExplodedViewSelectionEditorFinished);
    connect(_explodedViewSelectionEditor, &QObject::destroyed,
            this, [this]() { _explodedViewSelectionEditor = nullptr; });

    _explodedViewSelectionEditor->show();
    _explodedViewSelectionEditor->raise();
    _explodedViewSelectionEditor->activateWindow();
}

void ExplodedViewPanel::onExplodedViewSelectionEditorFinished(int result)
{
    ExplodedViewSelectionEditor* editor = _explodedViewSelectionEditor;
    if (!editor)
    {
        clearAssemblyPreviewSelection();
        emit selectionClearRequested();
        return;
    }

    const QVector<ExplodedViewSelectionEditor::Entry> updatedEntries = editor->entries();
    clearAssemblyPreviewSelection();

    if (result == QDialog::Accepted)
    {
        QVector<QUuid> updatedUuids;
        updatedUuids.reserve(updatedEntries.size());
        for (const ExplodedViewSelectionEditor::Entry& entry : updatedEntries)
            updatedUuids.append(entry.uuid);
        applyAssemblyEntries(updatedUuids);
        emit selectionClearRequested();
        editor->deleteLater();
        return;
    }

    if (result == ExplodedViewSelectionEditor::AddMoreResult)
    {
        _assemblyEditWorkingUuids.clear();
        for (const ExplodedViewSelectionEditor::Entry& entry : updatedEntries)
            _assemblyEditWorkingUuids.append(entry.uuid);

        _reopenAssemblyEditDialogAfterPick = true;
        _assemblyEditPickActive = true;
        if (pushButtonSelectAssembly && !pushButtonSelectAssembly->isChecked())
            pushButtonSelectAssembly->setChecked(true);
        editor->deleteLater();
        return;
    }

    emit selectionClearRequested();
    editor->deleteLater();
}

void ExplodedViewPanel::applyAssemblyEntries(const QVector<QUuid>& assemblyUuids)
{
    if (!_viewportWidget)
        return;

    ExplodedViewPreset& preset = ensureActivePreset();
    preset.assemblyUuids.clear();
    for (const QUuid& uuid : assemblyUuids)
    {
        if (!uuid.isNull())
            preset.assemblyUuids.insert(uuid);
    }

    if (!preset.anchorUuid.isNull() && !preset.assemblyUuids.contains(preset.anchorUuid))
    {
        preset.anchorUuid = QUuid();
        if (lineEditAnchor)
            lineEditAnchor->clear();
    }

    QList<int> ids;
    ids.reserve(assemblyUuids.size());
    for (const QUuid& uuid : assemblyUuids)
    {
        const int index = _viewportWidget->getIndexByUuid(uuid);
        if (index >= 0)
            ids.append(index);
    }

    updateAssemblySelectionDisplay(ids);
    updateCaptureButton();
    _hasUncapturedAutoPose = !preset.assemblyUuids.isEmpty();
    markDocumentModified();
    emit explosionParametersChanged();
}

void ExplodedViewPanel::previewAssemblyEntry(const QUuid& uuid)
{
    if (uuid.isNull())
        return;

    if (ModelViewer* viewer = owningModelViewer())
    {
        viewer->setSelectionWithoutUndo(QSet<QUuid>{uuid});
        return;
    }

    clearAssemblyPreviewSelection();
    if (_viewportWidget)
    {
        const int index = _viewportWidget->getIndexByUuid(uuid);
        if (index >= 0)
        {
            _viewportWidget->getSelectionManager()->syncSelectedIds(QList<int>{index});
            _viewportWidget->syncMeshSelectionVisualState();
            emit _viewportWidget->selectionChanged(QList<int>{index});
            _viewportWidget->update();
        }
    }
}

void ExplodedViewPanel::clearAssemblyPreviewSelection()
{
    emit selectionClearRequested();
}

QString ExplodedViewPanel::describeAssemblySelection(const QList<int>& ids) const
{
    if (!_sceneGraph || ids.isEmpty())
        return tr("%1 meshes").arg(ids.size());

    QSet<const SceneNode*> owningNodes;
    for (int id : ids) {
        const QUuid uuid = _viewportWidget->getUuidByIndex(id);
        const SceneNode* node = _sceneGraph->findNodeForMesh(uuid);
        if (node)
            owningNodes.insert(node);
    }

    if (owningNodes.isEmpty())
        return tr("%1 meshes").arg(ids.size());

    if (owningNodes.size() == 1)
        return (*owningNodes.begin())->name;

    const SceneNode* commonParent = nullptr;
    bool allSameParent = true;
    for (const SceneNode* n : owningNodes) {
        if (!commonParent) {
            commonParent = n->parent;
        } else if (n->parent != commonParent) {
            allSameParent = false;
            break;
        }
    }

    if (allSameParent && commonParent && !commonParent->isSynthetic)
        return commonParent->name;

    return tr("%1 meshes").arg(ids.size());
}

// ---------------------------------------------------------------------------
// Capture / create animation
// ---------------------------------------------------------------------------
void ExplodedViewPanel::on_pushButtonCaptureView_clicked()
{
    stopDraftPreview();
    if (captureCurrentExplosionStep())
    {
        updateCaptureButton();
        updatePreviewControls();
    }
}

void ExplodedViewPanel::on_pushButtonReplaceCapture_clicked()
{
    stopDraftPreview();
    if (replaceCapturedExplosionStep(currentCapturedStepRow()))
    {
        updateCaptureButton();
        updatePreviewControls();
    }
}

void ExplodedViewPanel::on_pushButtonRemoveCapture_clicked()
{
    stopDraftPreview();
    if (removeCapturedStepById(currentCapturedStepId()))
    {
        updateCapturedViewsList();
        updateCaptureButton();
        updatePreviewControls();
    }
}

void ExplodedViewPanel::on_pushButtonMoveCaptureUp_clicked()
{
    stopDraftPreview();
    const QUuid stepId = currentCapturedStepId();
    if (!stepId.isNull() && moveCapturedStep(stepId, -1))
    {
        updateCapturedViewsList();
        updateCaptureButton();
        updatePreviewControls();
    }
}

void ExplodedViewPanel::on_pushButtonMoveCaptureDown_clicked()
{
    stopDraftPreview();
    const QUuid stepId = currentCapturedStepId();
    if (!stepId.isNull() && moveCapturedStep(stepId, 1))
    {
        updateCapturedViewsList();
        updateCaptureButton();
        updatePreviewControls();
    }
}

void ExplodedViewPanel::on_pushButtonCapture_clicked()
{
    stopDraftPreview();
    createAnimationsFromCapturedSteps();
}

bool ExplodedViewPanel::captureCurrentExplosionStep()
{
    CapturedExplosionStep step;
    if (!buildCurrentCapturedExplosionStep(step))
        return false;

    ExplodedViewPreset& preset = ensureActivePreset();
    step.id = QUuid::createUuid();
    step.name = tr("Step %1").arg(preset.capturedStepCounter++);
    preset.capturedSteps.append(step);
    const bool useCombinedPose = !checkBoxCombinedPose || checkBoxCombinedPose->isChecked();
    const bool manualMode = radioButtonModeManual && radioButtonModeManual->isChecked();
    if (useCombinedPose || !manualMode)
        _hasUncapturedAutoPose = false;
    if (useCombinedPose || manualMode)
        _hasUncapturedManualPose = false;
    updateCapturedViewsList();
    if (listWidgetCapturedViews)
        setCurrentCapturedStepRow(preset.capturedSteps.size() - 1);
    markDocumentModified();
    return true;
}

bool ExplodedViewPanel::replaceCapturedExplosionStep(int row)
{
    Q_UNUSED(row);
    QVector<CapturedExplosionStep>& capturedSteps = activeCapturedSteps();
    const QUuid stepId = currentCapturedStepId();
    CapturedExplosionStep* existing = findCapturedStepById(capturedSteps, stepId);
    if (!existing || existing->isGroup)
        return false;

    CapturedExplosionStep replacement;
    if (!buildCurrentCapturedExplosionStep(replacement))
        return false;

    replacement.id = existing->id;
    replacement.name = existing->name;
    *existing = replacement;
    const bool useCombinedPose = !checkBoxCombinedPose || checkBoxCombinedPose->isChecked();
    const bool manualMode = radioButtonModeManual && radioButtonModeManual->isChecked();
    if (useCombinedPose || !manualMode)
        _hasUncapturedAutoPose = false;
    if (useCombinedPose || manualMode)
        _hasUncapturedManualPose = false;
    updateCapturedViewsList();
    if (!stepId.isNull() && listWidgetCapturedViews)
    {
        const QList<QTreeWidgetItem*> matches = listWidgetCapturedViews->findItems(QStringLiteral("*"),
            Qt::MatchWildcard | Qt::MatchRecursive, 0);
        for (QTreeWidgetItem* item : matches)
        {
            if (item && item->data(0, Qt::UserRole).toUuid() == stepId)
            {
                listWidgetCapturedViews->setCurrentItem(item);
                break;
            }
        }
    }
    markDocumentModified();
    return true;
}

QSet<QUuid> ExplodedViewPanel::currentCaptureMeshUuids() const
{
    const bool useCombinedPose = !checkBoxCombinedPose || checkBoxCombinedPose->isChecked();
    const bool manualMode = radioButtonModeManual && radioButtonModeManual->isChecked();

    QSet<QUuid> captureUuids;
    if (useCombinedPose || !manualMode)
        captureUuids.unite(assemblyUuids());
    if (_viewportWidget && (useCombinedPose || manualMode))
        captureUuids.unite(_viewportWidget->explodedViewManualPlacementUuids());
    return captureUuids;
}

bool ExplodedViewPanel::buildCurrentCapturedExplosionStep(CapturedExplosionStep& step) const
{
    const QSet<QUuid> captureUuids = currentCaptureMeshUuids();
    if (!_viewportWidget || !_sceneGraph || captureUuids.isEmpty())
        return false;

    const bool useCombinedPose = !checkBoxCombinedPose || checkBoxCombinedPose->isChecked();
    const bool manualMode = radioButtonModeManual && radioButtonModeManual->isChecked();
    const bool includeAutoPose = useCombinedPose || !manualMode;
    const bool includeManualPose = useCombinedPose || manualMode;

    step.children.clear();
    step.isGroup = false;
    step.tracks.clear();
    const SceneGraphWorldTransforms worlds = _sceneGraph->evaluateWorldTransforms();
    QHash<QUuid, MeshCaptureMotion> contributingMotionsByMeshUuid;
    QHash<QUuid, QVector<MeshCaptureMotion>> selectedMotionsByOwnerNode;

    for (const QUuid& meshUuid : captureUuids)
    {
        SceneMesh* mesh = _viewportWidget->getMeshByUuid(meshUuid);
        SceneNode* ownerNode = _sceneGraph->findNodeForMesh(meshUuid);
        if (!mesh || !ownerNode)
            continue;

        const QVector3D worldOffset = mesh->explosionOffset();
        const TransformState meshState(
            mesh->getExplodedViewTranslation(),
            mesh->getExplodedViewRotation(),
            mesh->getExplodedViewScaling(),
            mesh->getExplodedViewRotationQuaternion());
        const bool hasManualTranslation = meshState.translation.lengthSquared() > 1.0e-8f;
        const QQuaternion identityRotation(1.0f, 0.0f, 0.0f, 0.0f);
        const bool hasManualRotation = meshState.hasExactRotation
            ? !rotationsNearlyEqual(meshState.rotationQuat, identityRotation)
            : meshState.rotation.lengthSquared() > 1.0e-8f;
        const bool contributesAuto = includeAutoPose && worldOffset.lengthSquared() >= 1.0e-8f;
        const bool contributesManual = includeManualPose && (hasManualTranslation || hasManualRotation);
        if (!contributesAuto && !contributesManual)
            continue;

        MeshCaptureMotion motion;
        if (!buildMeshCaptureMotion(_viewportWidget, ownerNode, mesh, includeAutoPose, includeManualPose, motion))
            continue;
        contributingMotionsByMeshUuid.insert(meshUuid, motion);
        selectedMotionsByOwnerNode[ownerNode->nodeUuid].append(motion);
    }

    for (auto it = selectedMotionsByOwnerNode.cbegin(); it != selectedMotionsByOwnerNode.cend(); ++it)
    {
        const QVector<MeshCaptureMotion>& selectedMotions = it.value();
        if (selectedMotions.isEmpty())
            continue;

        SceneNode* ownerNode = selectedMotions.front().ownerNode;
        if (!ownerNode)
            continue;

        QVector<SceneNode*> candidateNodes;
        for (SceneNode* candidate = ownerNode; candidate && !candidate->isSynthetic; candidate = candidate->parent)
            candidateNodes.append(candidate);

        bool resolved = false;
        QString failureReason;

        for (SceneNode* candidateNode : std::as_const(candidateNodes))
        {
            QVector<QUuid> subtreeMeshUuids;
            collectSubtreeMeshUuids(candidateNode, subtreeMeshUuids);
            QVector<MeshCaptureMotion> subtreeMotions;
            subtreeMotions.reserve(subtreeMeshUuids.size());
            for (const QUuid& subtreeMeshUuid : std::as_const(subtreeMeshUuids))
            {
                auto cachedIt = contributingMotionsByMeshUuid.find(subtreeMeshUuid);
                if (cachedIt != contributingMotionsByMeshUuid.end())
                {
                    subtreeMotions.append(cachedIt.value());
                    continue;
                }

                SceneMesh* subtreeMesh = _viewportWidget->getMeshByUuid(subtreeMeshUuid);
                SceneNode* subtreeOwnerNode = _sceneGraph->findNodeForMesh(subtreeMeshUuid);
                MeshCaptureMotion subtreeMotion;
                if (buildMeshCaptureMotion(_viewportWidget,
                                           subtreeOwnerNode,
                                           subtreeMesh,
                                           includeAutoPose,
                                           includeManualPose,
                                           subtreeMotion))
                {
                    subtreeMotions.append(subtreeMotion);
                }
            }
            if (subtreeMotions.isEmpty())
            {
                failureReason = tr("no captured mesh motions are available in the candidate subtree");
                continue;
            }

            LocalNodeTransform referenceTransform;
            if (!inferCandidateLocalEndTransform(worlds, subtreeMotions.front(), candidateNode, referenceTransform))
            {
                failureReason = tr("the candidate transform basis could not be inverted");
                continue;
            }

            bool transformsCompatible = true;
            for (const MeshCaptureMotion& motion : std::as_const(subtreeMotions))
            {
                LocalNodeTransform candidateEndTransform;
                const bool inferred =
                    inferCandidateLocalEndTransform(worlds, motion, candidateNode, candidateEndTransform);
                if (!inferred || !localTransformsCompatible(referenceTransform, candidateEndTransform))
                {
                    transformsCompatible = false;
                    break;
                }
            }

            if (!transformsCompatible)
            {
                failureReason = tr("captured meshes in the candidate subtree do not resolve to one rigid node transform");
                continue;
            }

            CapturedTransformTrack track;
            track.targetKind = GltfAnimationBindingTargetKind::Node;
            track.meshUuid = selectedMotions.front().meshUuid;
            track.ownerNodeUuid = candidateNode->nodeUuid;
            track.sourceFile = selectedMotions.front().sourceFile;
            track.targetNodeName = candidateNode->name;

            const LocalNodeTransform startTransform =
                decomposeLocalNodeTransform(aiToQMatrix(candidateNode->localTransform));
            track.startPosition = startTransform.translation;
            track.endPosition = referenceTransform.translation;
            track.startRotation = startTransform.rotation;
            track.endRotation = referenceTransform.rotation;
            step.tracks.append(track);
            resolved = true;
            break;
        }

        if (!resolved)
        {
            bool emittedMeshTracks = false;
            for (const MeshCaptureMotion& motion : selectedMotions)
            {
                LocalNodeTransform meshEndTransform;
                if (!inferMeshChildLocalEndTransform(worlds, motion, meshEndTransform))
                    continue;

                SceneMesh* mesh = _viewportWidget->getMeshByUuid(motion.meshUuid);
                CapturedTransformTrack track;
                track.targetKind = GltfAnimationBindingTargetKind::Mesh;
                track.meshUuid = motion.meshUuid;
                track.ownerNodeUuid = ownerNode->nodeUuid;
                track.sourceFile = motion.sourceFile;
                track.targetNodeName = mesh ? mesh->getName() : ownerNode->name;
                track.targetNodeIndex = -1;
                track.startPosition = QVector3D(0.0f, 0.0f, 0.0f);
                track.endPosition = meshEndTransform.translation;
                track.startRotation = QQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
                track.endRotation = meshEndTransform.rotation;
                step.tracks.append(track);
                emittedMeshTracks = true;
            }

            if (!emittedMeshTracks)
            {
                qWarning() << "[ExplodedView] Skipping animation capture for node"
                           << ownerNode->name
                           << "because no rigid animatable node target could represent the current exploded mesh state:"
                           << failureReason;
            }
        }
    }

    return !step.tracks.isEmpty();
}

QString ExplodedViewPanel::nextCapturedGroupName() const
{
    const ExplodedViewPreset* preset = activePreset();
    const int candidate = preset ? qMax(1, preset->capturedGroupCounter) : 1;
    return tr("Group %1").arg(candidate);
}

void ExplodedViewPanel::collectCapturedLeafTracks(const CapturedExplosionStep& step,
                                                  QVector<CapturedTransformTrack>& out) const
{
    if (step.isGroup)
    {
        for (const CapturedExplosionStep& child : step.children)
            collectCapturedLeafTracks(child, out);
        return;
    }

    out += step.tracks;
}

QVector<ExplodedViewPanel::CapturedTransformTrack> ExplodedViewPanel::resolvedTracksForStep(
    const CapturedExplosionStep& step) const
{
    QVector<ExplodedViewPanel::CapturedTransformTrack> tracks;
    collectCapturedLeafTracks(step, tracks);
    return tracks;
}

QVector<ExplodedViewPanel::CapturedExplosionStep> ExplodedViewPanel::resolvedTopLevelCapturedEntries() const
{
    QVector<CapturedExplosionStep> resolved;
    const QVector<CapturedExplosionStep>& topLevel = activeCapturedSteps();
    resolved.reserve(topLevel.size());
    for (const CapturedExplosionStep& step : topLevel)
    {
        CapturedExplosionStep entry = step;
        if (step.isGroup)
            entry.tracks = resolvedTracksForStep(step);
        resolved.append(entry);
    }
    return resolved;
}

ExplodedViewPanel::CapturedExplosionStep* ExplodedViewPanel::findCapturedStepById(
    QVector<CapturedExplosionStep>& steps, const QUuid& stepId)
{
    for (CapturedExplosionStep& step : steps)
    {
        if (step.id == stepId)
            return &step;
        if (step.isGroup)
        {
            if (CapturedExplosionStep* match = findCapturedStepById(step.children, stepId))
                return match;
        }
    }
    return nullptr;
}

const ExplodedViewPanel::CapturedExplosionStep* ExplodedViewPanel::findCapturedStepById(
    const QVector<CapturedExplosionStep>& steps, const QUuid& stepId) const
{
    for (const CapturedExplosionStep& step : steps)
    {
        if (step.id == stepId)
            return &step;
        if (step.isGroup)
        {
            if (const CapturedExplosionStep* match = findCapturedStepById(step.children, stepId))
                return match;
        }
    }
    return nullptr;
}

void ExplodedViewPanel::normalizeCapturedGroups(QVector<CapturedExplosionStep>& steps)
{
    for (int index = steps.size() - 1; index >= 0; --index)
    {
        CapturedExplosionStep& step = steps[index];
        if (step.isGroup)
        {
            normalizeCapturedGroups(step.children);
            if (step.children.isEmpty())
            {
                steps.removeAt(index);
                continue;
            }
            if (step.children.size() == 1)
            {
                CapturedExplosionStep child = step.children.front();
                steps[index] = child;
                continue;
            }
        }
    }

    refreshCapturedGroupTracks(steps);
}

void ExplodedViewPanel::refreshCapturedGroupTracks(QVector<CapturedExplosionStep>& steps)
{
    for (CapturedExplosionStep& step : steps)
    {
        if (!step.isGroup)
            continue;

        refreshCapturedGroupTracks(step.children);
        step.tracks = resolvedTracksForStep(step);
    }
}

bool ExplodedViewPanel::removeCapturedStepById(QVector<CapturedExplosionStep>& steps, const QUuid& stepId)
{
    for (int index = 0; index < steps.size(); ++index)
    {
        CapturedExplosionStep& step = steps[index];
        if (step.id == stepId)
        {
            steps.removeAt(index);
            return true;
        }

        if (step.isGroup && removeCapturedStepById(step.children, stepId))
            return true;
    }
    return false;
}

bool ExplodedViewPanel::removeCapturedStepById(const QUuid& stepId)
{
    if (stepId.isNull())
        return false;

    QVector<CapturedExplosionStep>& steps = activeCapturedSteps();
    if (!removeCapturedStepById(steps, stepId))
        return false;

    normalizeCapturedGroups(steps);
    markDocumentModified();
    return true;
}

bool ExplodedViewPanel::ungroupCapturedStep(QVector<CapturedExplosionStep>& steps, const QUuid& groupId)
{
    for (int index = 0; index < steps.size(); ++index)
    {
        CapturedExplosionStep& step = steps[index];
        if (step.id == groupId && step.isGroup)
        {
            QVector<CapturedExplosionStep> children = step.children;
            steps.removeAt(index);
            for (int childIndex = 0; childIndex < children.size(); ++childIndex)
                steps.insert(index + childIndex, children[childIndex]);
            return true;
        }

        if (step.isGroup && ungroupCapturedStep(step.children, groupId))
            return true;
    }
    return false;
}

bool ExplodedViewPanel::ungroupCapturedStep(const QUuid& groupId)
{
    if (groupId.isNull())
        return false;

    QVector<CapturedExplosionStep>& steps = activeCapturedSteps();
    if (!ungroupCapturedStep(steps, groupId))
        return false;

    normalizeCapturedGroups(steps);
    markDocumentModified();
    return true;
}

QUuid ExplodedViewPanel::currentCapturedStepId() const
{
    if (!listWidgetCapturedViews)
        return QUuid();

    QTreeWidgetItem* current = listWidgetCapturedViews->currentItem();
    return current ? current->data(0, Qt::UserRole).toUuid() : QUuid();
}

QUuid ExplodedViewPanel::currentTopLevelCapturedStepId() const
{
    if (!listWidgetCapturedViews)
        return QUuid();

    QTreeWidgetItem* current = listWidgetCapturedViews->currentItem();
    if (!current)
        return QUuid();

    while (current->parent())
        current = current->parent();
    return current->data(0, Qt::UserRole).toUuid();
}

ExplodedViewPanel::CapturedExplosionStep* ExplodedViewPanel::currentCapturedStep()
{
    const QUuid stepId = currentCapturedStepId();
    return stepId.isNull() ? nullptr : findCapturedStepById(activeCapturedSteps(), stepId);
}

const ExplodedViewPanel::CapturedExplosionStep* ExplodedViewPanel::currentCapturedStep() const
{
    const QUuid stepId = currentCapturedStepId();
    return stepId.isNull() ? nullptr : findCapturedStepById(activeCapturedSteps(), stepId);
}

ExplodedViewPanel::CapturedExplosionStep* ExplodedViewPanel::currentTopLevelCapturedStep()
{
    const QUuid stepId = currentTopLevelCapturedStepId();
    return stepId.isNull() ? nullptr : findCapturedStepById(activeCapturedSteps(), stepId);
}

const ExplodedViewPanel::CapturedExplosionStep* ExplodedViewPanel::currentTopLevelCapturedStep() const
{
    const QUuid stepId = currentTopLevelCapturedStepId();
    return stepId.isNull() ? nullptr : findCapturedStepById(activeCapturedSteps(), stepId);
}

bool ExplodedViewPanel::groupSelectedCaptures()
{
    if (!listWidgetCapturedViews)
        return false;

    QList<QTreeWidgetItem*> selectedItems = listWidgetCapturedViews->selectedItems();
    if (selectedItems.size() < 2)
        return false;

    QVector<QUuid> selectedIds;
    selectedIds.reserve(selectedItems.size());
    for (QTreeWidgetItem* item : selectedItems)
    {
        if (!item || item->parent() != nullptr)
            return false;

        const QUuid stepId = item->data(0, Qt::UserRole).toUuid();
        const CapturedExplosionStep* step = findCapturedStepById(activeCapturedSteps(), stepId);
        if (!step || step->isGroup)
            return false;

        selectedIds.append(stepId);
    }

    QVector<CapturedExplosionStep>& steps = activeCapturedSteps();
    QVector<CapturedExplosionStep> groupedChildren;
    int insertIndex = steps.size();
    for (int index = 0; index < steps.size(); )
    {
        if (selectedIds.contains(steps[index].id))
        {
            if (insertIndex == steps.size())
                insertIndex = index;
            groupedChildren.append(steps[index]);
            steps.removeAt(index);
            continue;
        }
        ++index;
    }

    if (groupedChildren.size() < 2)
        return false;

    ExplodedViewPreset& preset = ensureActivePreset();
    CapturedExplosionStep group;
    group.id = QUuid::createUuid();
    group.name = nextCapturedGroupName();
    group.isGroup = true;
    group.children = groupedChildren;
    group.tracks = resolvedTracksForStep(group);
    steps.insert(insertIndex, group);
    preset.capturedGroupCounter = qMax(preset.capturedGroupCounter, 1) + 1;
    markDocumentModified();
    return true;
}

bool ExplodedViewPanel::createAnimationsFromCapturedSteps()
{
    const QVector<CapturedExplosionStep> capturedSteps = resolvedTopLevelCapturedEntries();
    if (!_sceneGraph || capturedSteps.isEmpty())
        return false;

    const ExplodedViewPreset& preset = ensureActivePreset();
    enum class OutputMode { ParallelSingle, SequentialSingle, Separate };
    const OutputMode mode = (preset.outputMode == 1)
        ? OutputMode::SequentialSingle
        : (preset.outputMode == 2)
            ? OutputMode::Separate
            : OutputMode::ParallelSingle;

    const double durationSeconds = preset.durationSeconds;
    const bool loopBack = preset.loopBack;

    // Build one node-index map per source file. If a file has no persisted node
    // bindings yet, synthesize them from the current SceneGraph hierarchy.
    QHash<QString, GltfAnimationData> animationDataByFile;
    QHash<QString, QHash<QUuid, int>> nodeIndexByUuidByFile;

    QSet<QString> involvedFiles;
    for (const CapturedExplosionStep& step : capturedSteps)
        for (const CapturedTransformTrack& track : step.tracks)
            if (!track.sourceFile.isEmpty())
                involvedFiles.insert(track.sourceFile);

    for (const QString& sourceFile : involvedFiles)
    {
        GltfAnimationData data = _sceneGraph->animationDataForFile(sourceFile);
        if (data.sourceFile.isEmpty())
            data.sourceFile = sourceFile;

        QHash<QUuid, int> nodeIndexByUuid;
        if (data.nodeBindings.isEmpty())
        {
            SceneNode* fileNode = _sceneGraph->findFileNode(sourceFile);
            if (fileNode)
            {
                if (fileNode->isSynthetic && !fileNode->children.isEmpty())
                {
                    SceneNode* aiRootNode = fileNode->children.first();
                    collectNodeBindings(aiRootNode, data.nodeBindings, nodeIndexByUuid);
                }
                else
                {
                    collectNodeBindings(fileNode, data.nodeBindings, nodeIndexByUuid);
                }
            }
        }
        else
        {
            SceneNode* fileNode = _sceneGraph->findFileNode(sourceFile);
            if (fileNode)
            {
                SceneNode* aiRootNode = fileNode->children.isEmpty() ? nullptr : fileNode->children.first();
                for (const GltfAnimationNodeBinding& binding : std::as_const(data.nodeBindings))
                {
                    SceneNode* targetNode = nullptr;
                    if (binding.hasAiChildPath)
                        targetNode = findSceneNodeByAiChildPath(aiRootNode, binding.aiChildPath);

                    if (!targetNode && !binding.nodeName.isEmpty())
                    {
                        std::function<SceneNode*(SceneNode*)> findByName = [&](SceneNode* node) -> SceneNode*
                        {
                            if (!node)
                                return nullptr;
                            if (node->name == binding.nodeName)
                                return node;
                            for (SceneNode* child : node->children)
                            {
                                if (SceneNode* match = findByName(child))
                                    return match;
                            }
                            return nullptr;
                        };

                        for (SceneNode* child : fileNode->children)
                        {
                            targetNode = findByName(child);
                            if (targetNode)
                                break;
                        }
                    }

                    if (targetNode && binding.nodeIndex >= 0 && !nodeIndexByUuid.contains(targetNode->nodeUuid))
                        nodeIndexByUuid.insert(targetNode->nodeUuid, binding.nodeIndex);
                }
            }
        }

        animationDataByFile.insert(sourceFile, data);
        nodeIndexByUuidByFile.insert(sourceFile, nodeIndexByUuid);
    }

    auto makeChannel = [&](const CapturedTransformTrack& track,
                           int nodeIndex,
                           const QVector<GltfAnimationVec3Key>& keys) {
        GltfAnimationChannel channel;
        channel.targetKind = track.targetKind;
        channel.targetPath = GltfAnimationTargetPath::Translation;
        channel.targetNodeName = track.targetNodeName;
        channel.targetNodeIndex = nodeIndex;
        channel.targetMeshUuid = track.meshUuid;
        channel.vec3Keys = keys;
        return channel;
    };

    auto makeRotationChannel = [&](const CapturedTransformTrack& track,
                                   int nodeIndex,
                                   const QVector<GltfAnimationQuatKey>& keys) {
        GltfAnimationChannel channel;
        channel.targetKind = track.targetKind;
        channel.targetPath = GltfAnimationTargetPath::Rotation;
        channel.targetNodeName = track.targetNodeName;
        channel.targetNodeIndex = nodeIndex;
        channel.targetMeshUuid = track.meshUuid;
        channel.quatKeys = keys;
        return channel;
    };

    const auto targetUuidForTrack = [](const CapturedTransformTrack& track) {
        return track.targetKind == GltfAnimationBindingTargetKind::Mesh ? track.meshUuid : track.ownerNodeUuid;
    };

    auto makeForwardKeys = [&](const QVector3D& startPosition,
                               const QVector3D& endPosition) {
        QVector<GltfAnimationVec3Key> keys;
        keys.append({0.0, startPosition});
        keys.append({durationSeconds, endPosition});
        if (loopBack)
            keys.append({durationSeconds * 2.0, startPosition});
        return keys;
    };

    auto makeForwardRotationKeys = [&](const QQuaternion& startRotation,
                                       const QQuaternion& endRotation) {
        QVector<GltfAnimationQuatKey> keys;
        keys.append({0.0, startRotation});
        keys.append({durationSeconds, endRotation});
        if (loopBack)
            keys.append({durationSeconds * 2.0, startRotation});
        return keys;
    };

    const auto makeClipName = [&](const QString& suffix = QString()) {
        const QString base = preset.name.trimmed().isEmpty()
            ? tr("Exploded View %1").arg(_createdAnimationCounter++)
            : preset.name.trimmed();
        return suffix.isEmpty() ? base : QStringLiteral("%1 - %2").arg(base, suffix);
    };

    if (mode == OutputMode::ParallelSingle)
    {
        QHash<QString, QHash<QUuid, CapturedTransformTrack>> finalTracksByFile;
        for (const CapturedExplosionStep& step : capturedSteps)
        {
            for (const CapturedTransformTrack& track : step.tracks)
            {
                if (!track.sourceFile.isEmpty())
                    finalTracksByFile[track.sourceFile].insert(targetUuidForTrack(track), track);
            }
        }

        QString firstActivatedFile;
        int firstActivatedClip = -1;

        for (auto fileIt = finalTracksByFile.cbegin(); fileIt != finalTracksByFile.cend(); ++fileIt)
        {
            GltfAnimationData data = animationDataByFile.value(fileIt.key());
            GltfAnimationClip clip;
            clip.name = makeClipName(tr("Parallel"));
            clip.durationSeconds = loopBack ? durationSeconds * 2.0 : durationSeconds;
            clip.hasNodeTransforms = true;

            const QHash<QUuid, int> nodeIndexByUuid = nodeIndexByUuidByFile.value(fileIt.key());
            for (const CapturedTransformTrack& track : fileIt.value())
            {
                const int nodeIndex = track.targetKind == GltfAnimationBindingTargetKind::Node
                    ? nodeIndexByUuid.value(track.ownerNodeUuid, track.targetNodeIndex)
                    : -1;
                clip.channels.append(makeChannel(track,
                    nodeIndex,
                    makeForwardKeys(track.startPosition, track.endPosition)));
                if (!rotationsNearlyEqual(track.startRotation, track.endRotation))
                {
                    clip.channels.append(makeRotationChannel(track,
                        nodeIndex,
                        makeForwardRotationKeys(track.startRotation, track.endRotation)));
                }
            }

            if (clip.channels.isEmpty())
                continue;

            data.clips.append(clip);
            data.hasNodeAnimations = true;
            _sceneGraph->setAnimationData(fileIt.key(), data);
            _sceneGraph->setActiveAnimationClip(fileIt.key(), data.clips.size() - 1);

            if (firstActivatedFile.isEmpty())
            {
                firstActivatedFile = fileIt.key();
                firstActivatedClip = data.clips.size() - 1;
            }
        }

        if (!firstActivatedFile.isEmpty())
            _viewportWidget->setActiveAnimation(firstActivatedFile, firstActivatedClip);
        return true;
    }

    if (mode == OutputMode::Separate)
    {
        QString firstActivatedFile;
        int firstActivatedClip = -1;

        for (const CapturedExplosionStep& step : capturedSteps)
        {
            QHash<QString, QHash<QUuid, CapturedTransformTrack>> stepTracksByFile;
            for (const CapturedTransformTrack& track : step.tracks)
            {
                if (!track.sourceFile.isEmpty())
                    stepTracksByFile[track.sourceFile].insert(targetUuidForTrack(track), track);
            }

            for (auto fileIt = stepTracksByFile.cbegin(); fileIt != stepTracksByFile.cend(); ++fileIt)
            {
                GltfAnimationData data = _sceneGraph->animationDataForFile(fileIt.key());
                if (data.sourceFile.isEmpty())
                    data.sourceFile = fileIt.key();
                if (data.nodeBindings.isEmpty())
                    data = animationDataByFile.value(fileIt.key(), data);

                GltfAnimationClip clip;
                clip.name = makeClipName(step.name);
                clip.durationSeconds = loopBack ? durationSeconds * 2.0 : durationSeconds;
                clip.hasNodeTransforms = true;

                const QHash<QUuid, int> nodeIndexByUuid = nodeIndexByUuidByFile.value(fileIt.key());
                for (const CapturedTransformTrack& track : fileIt.value())
                {
                    const int nodeIndex = track.targetKind == GltfAnimationBindingTargetKind::Node
                        ? nodeIndexByUuid.value(track.ownerNodeUuid, track.targetNodeIndex)
                        : -1;
                    clip.channels.append(makeChannel(track,
                        nodeIndex,
                        makeForwardKeys(track.startPosition, track.endPosition)));
                    if (!rotationsNearlyEqual(track.startRotation, track.endRotation))
                    {
                        clip.channels.append(makeRotationChannel(track,
                            nodeIndex,
                            makeForwardRotationKeys(track.startRotation, track.endRotation)));
                    }
                }

                if (clip.channels.isEmpty())
                    continue;

                data.clips.append(clip);
                data.hasNodeAnimations = true;
                _sceneGraph->setAnimationData(fileIt.key(), data);
                _sceneGraph->setActiveAnimationClip(fileIt.key(), data.clips.size() - 1);

                if (firstActivatedFile.isEmpty())
                {
                    firstActivatedFile = fileIt.key();
                    firstActivatedClip = data.clips.size() - 1;
                }
            }
        }

        if (!firstActivatedFile.isEmpty())
            _viewportWidget->setActiveAnimation(firstActivatedFile, firstActivatedClip);
        return true;
    }

    // Sequential single animation: sample the cumulative state after each step.
    QString firstActivatedFile;
    int firstActivatedClip = -1;

    for (const QString& sourceFile : involvedFiles)
    {
        struct NodeState
        {
            CapturedTransformTrack info;
            QVector3D base;
            QVector3D current;
            QQuaternion baseRotation;
            QQuaternion currentRotation;
            QVector<GltfAnimationVec3Key> keys;
            QVector<GltfAnimationQuatKey> rotationKeys;
        };

        QHash<QUuid, NodeState> states;
        for (const CapturedExplosionStep& step : capturedSteps)
        {
            for (const CapturedTransformTrack& track : step.tracks)
            {
                if (track.sourceFile != sourceFile)
                    continue;

                const QUuid targetUuid = targetUuidForTrack(track);
                auto it = states.find(targetUuid);
                if (it == states.end())
                {
                    NodeState state;
                    state.info = track;
                    state.base = track.startPosition;
                    state.current = track.startPosition;
                    state.baseRotation = track.startRotation;
                    state.currentRotation = track.startRotation;
                    states.insert(targetUuid, state);
                }
                else
                {
                    it->info = track;
                }
            }
        }

        if (states.isEmpty())
            continue;

        for (auto it = states.begin(); it != states.end(); ++it)
        {
            it->keys.append({0.0, it->base});
            it->rotationKeys.append({0.0, it->baseRotation});
        }

        const qsizetype stepCount = std::max<qsizetype>(1, capturedSteps.size());
        const double segmentDuration = durationSeconds / static_cast<double>(stepCount);
        QVector<QHash<QUuid, QVector3D>> forwardSnapshots;
        QVector<QHash<QUuid, QQuaternion>> forwardRotationSnapshots;
        forwardSnapshots.reserve(capturedSteps.size() + 1);
        forwardRotationSnapshots.reserve(capturedSteps.size() + 1);

        QHash<QUuid, QVector3D> initialSnapshot;
        QHash<QUuid, QQuaternion> initialRotationSnapshot;
        for (auto it = states.cbegin(); it != states.cend(); ++it)
        {
            initialSnapshot.insert(it.key(), it->base);
            initialRotationSnapshot.insert(it.key(), it->baseRotation);
        }
        forwardSnapshots.append(initialSnapshot);
        forwardRotationSnapshots.append(initialRotationSnapshot);

        for (int stepIndex = 0; stepIndex < capturedSteps.size(); ++stepIndex)
        {
            const CapturedExplosionStep& step = capturedSteps[stepIndex];
            for (const CapturedTransformTrack& track : step.tracks)
            {
                if (track.sourceFile != sourceFile)
                    continue;

                auto it = states.find(targetUuidForTrack(track));
                if (it != states.end())
                {
                    it->info = track;
                    it->current = track.endPosition;
                    it->currentRotation = track.endRotation;
                }
            }

            const double t = segmentDuration * static_cast<double>(stepIndex + 1);
            for (auto it = states.begin(); it != states.end(); ++it) {
                it->keys.append({t, it->current});
                it->rotationKeys.append({t, it->currentRotation});
            }

            QHash<QUuid, QVector3D> snapshot;
            QHash<QUuid, QQuaternion> rotationSnapshot;
            for (auto it = states.cbegin(); it != states.cend(); ++it)
            {
                snapshot.insert(it.key(), it->current);
                rotationSnapshot.insert(it.key(), it->currentRotation);
            }
            forwardSnapshots.append(snapshot);
            forwardRotationSnapshots.append(rotationSnapshot);
        }

        if (loopBack)
        {
            for (qsizetype reverseStep = stepCount - 1; reverseStep >= 0; --reverseStep)
            {
                const double t = durationSeconds + segmentDuration * static_cast<double>(stepCount - reverseStep);
                const QHash<QUuid, QVector3D>& snapshot = forwardSnapshots[reverseStep];
                const QHash<QUuid, QQuaternion>& rotationSnapshot = forwardRotationSnapshots[reverseStep];
                for (auto it = states.begin(); it != states.end(); ++it)
                {
                    const QVector3D position = snapshot.value(it.key(), it->base);
                    it->keys.append({t, position});
                    const QQuaternion rotation = rotationSnapshot.value(it.key(), it->baseRotation);
                    it->rotationKeys.append({t, rotation});
                }
            }
        }

        GltfAnimationData data = _sceneGraph->animationDataForFile(sourceFile);
        if (data.sourceFile.isEmpty())
            data.sourceFile = sourceFile;
        if (data.nodeBindings.isEmpty())
            data = animationDataByFile.value(sourceFile, data);

        GltfAnimationClip clip;
        clip.name = makeClipName(tr("Sequential"));
        clip.durationSeconds = loopBack ? durationSeconds * 2.0 : durationSeconds;
        clip.hasNodeTransforms = true;

        const QHash<QUuid, int> nodeIndexByUuid = nodeIndexByUuidByFile.value(sourceFile);
        for (auto it = states.cbegin(); it != states.cend(); ++it)
        {
            const int nodeIndex = it->info.targetKind == GltfAnimationBindingTargetKind::Node
                ? nodeIndexByUuid.value(it->info.ownerNodeUuid, it->info.targetNodeIndex)
                : -1;
            clip.channels.append(makeChannel(it->info, nodeIndex, it->keys));
            bool hasRotationAnimation = false;
            for (const GltfAnimationQuatKey& key : it->rotationKeys)
            {
                if (!rotationsNearlyEqual(it->baseRotation, key.value))
                {
                    hasRotationAnimation = true;
                    break;
                }
            }
            if (hasRotationAnimation)
                clip.channels.append(makeRotationChannel(it->info, nodeIndex, it->rotationKeys));
        }

        if (clip.channels.isEmpty())
            continue;

        data.clips.append(clip);
        data.hasNodeAnimations = true;
        _sceneGraph->setAnimationData(sourceFile, data);
        _sceneGraph->setActiveAnimationClip(sourceFile, data.clips.size() - 1);

        if (firstActivatedFile.isEmpty())
        {
            firstActivatedFile = sourceFile;
            firstActivatedClip = data.clips.size() - 1;
        }
    }

    if (!firstActivatedFile.isEmpty())
        _viewportWidget->setActiveAnimation(firstActivatedFile, firstActivatedClip);
    return !firstActivatedFile.isEmpty();
}

// ---------------------------------------------------------------------------
// UI state
// ---------------------------------------------------------------------------
void ExplodedViewPanel::on_comboBoxMode_currentIndexChanged(int index)
{
    const bool autoMode = !radioButtonModeManual || !radioButtonModeManual->isChecked();
    frameVector->setVisible(autoMode && index == 4);
    syncActivePresetFromUi();
    emit explosionParametersChanged();
}

void ExplodedViewPanel::on_sliderExplosion_valueChanged(int value)
{
    labelDistancePercent->setText(QString("%1%").arg(value));
    syncActivePresetFromUi();
    updateCaptureButton();
    emit explosionParametersChanged();
}

void ExplodedViewPanel::on_pushButtonReset_clicked()
{
    const ExplodedViewPreset* preset = activePreset();
    const bool hasSelection = preset && (!preset->assemblyUuids.isEmpty() || !preset->anchorUuid.isNull());
    const bool hasNonDefaultExplodeState = comboBoxMode
        && sliderExplosion
        && doubleSpinBoxVectorX
        && doubleSpinBoxVectorY
        && doubleSpinBoxVectorZ
        && comboBoxAutoStrategy
        && checkBoxLoopBack
        && comboBoxAnimationMode
        && doubleSpinBoxAnimationDuration
        && (comboBoxMode->currentIndex() != 0
            || sliderExplosion->value() != 100
            || !qFuzzyCompare(doubleSpinBoxVectorX->value(), 1.0)
            || !qFuzzyCompare(doubleSpinBoxVectorY->value(), 0.0)
            || !qFuzzyCompare(doubleSpinBoxVectorZ->value(), 0.0)
            || comboBoxAutoStrategy->currentIndex() != comboIndexFromAutoStrategy(AutoStrategy::AssemblyAware)
            || comboBoxAnimationMode->currentIndex() != 0
            || !qFuzzyCompare(doubleSpinBoxAnimationDuration->value(), 3.0)
            || !checkBoxLoopBack->isChecked());
    const bool hasCapturedSteps = preset && !preset->capturedSteps.isEmpty();
    const bool hasManualPlacement = _viewportWidget
        && (_viewportWidget->isExplodedViewManualPlacementActive()
            || _viewportWidget->hasExplodedViewManualPlacement()
            || _viewportWidget->hasExplodedViewManualTransformChanges());

    if (hasSelection || hasNonDefaultExplodeState || hasCapturedSteps || hasManualPlacement)
    {
        const auto response = QMessageBox::warning(
            window(),
            tr("Reset Exploded View"),
            tr("This will clear the current exploded-view selection, settings, manual placement, and captured steps for the active preset. Continue?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (response != QMessageBox::Yes)
            return;
    }

    stopDraftPreview();
    cancelPickingMode();
    if (_viewportWidget)
        _viewportWidget->clearExplodedViewManualPlacement();
    clearManualPlacementSelection();
    _hasUncapturedAutoPose = false;
    _hasUncapturedManualPose = false;

    QSignalBlocker b1(comboBoxMode);
    QSignalBlocker b2(sliderExplosion);
    QSignalBlocker b3(doubleSpinBoxVectorX);
    QSignalBlocker b4(doubleSpinBoxVectorY);
    QSignalBlocker b5(doubleSpinBoxVectorZ);
    QSignalBlocker b6(checkBoxCombinedPose);
    QSignalBlocker b7(comboBoxAutoStrategy);

    clearAssemblySelection();

    {
        QSignalBlocker b6(pushButtonSelectAssembly);
        QSignalBlocker b7(pushButtonSelectAnchor);
        pushButtonSelectAssembly->setChecked(false);
        pushButtonSelectAnchor->setChecked(false);
    }
    updateAssemblyPickButtonVisual(false);
    lineEditAssembly->setPlaceholderText(tr("Select assembly or meshes..."));
    lineEditAnchor->setPlaceholderText(tr("Select anchor mesh (optional)..."));

    comboBoxMode->setCurrentIndex(0);
    frameVector->setVisible(false);

    sliderExplosion->setValue(100);
    labelDistancePercent->setText("100%");

    doubleSpinBoxVectorX->setValue(1.0);
    doubleSpinBoxVectorY->setValue(0.0);
    doubleSpinBoxVectorZ->setValue(0.0);
    ExplodedViewPreset& active = ensureActivePreset();
    active.mode = ExplodedViewManager::Mode::Auto;
    active.userVector = QVector3D(1.0f, 0.0f, 0.0f);
    active.factor = 1.0f;
    active.manualSelectionUuids.clear();
    active.uncapturedManualStates.clear();
    active.capturedSteps.clear();
    active.capturedStepCounter = 1;
    active.capturedGroupCounter = 1;
    active.outputMode = 0;
    active.durationSeconds = 3.0;
    active.loopBack = true;
    active.useCombinedPose = true;
    active.autoStrategy = AutoStrategy::AssemblyAware;
    comboBoxAnimationMode->setCurrentIndex(active.outputMode);
    doubleSpinBoxAnimationDuration->setValue(active.durationSeconds);
    checkBoxLoopBack->setChecked(active.loopBack);
    if (checkBoxCombinedPose)
        checkBoxCombinedPose->setChecked(active.useCombinedPose);
    if (comboBoxAutoStrategy)
        comboBoxAutoStrategy->setCurrentIndex(comboIndexFromAutoStrategy(active.autoStrategy));
    if (listWidgetCapturedViews)
        listWidgetCapturedViews->clear();

    updateCaptureButton();
    updateManualPlacementUi();
    updatePreviewControls();
    markDocumentModified();
    emit explosionParametersChanged();
    emit selectionClearRequested();
}

void ExplodedViewPanel::on_pushButtonPresetNew_clicked()
{
    stopDraftPreview();
    cancelPickingMode();
    syncActivePresetManualStateFromRuntime();
    if (_viewportWidget)
        _viewportWidget->clearExplodedViewManualPlacement();
    clearManualPlacementSelection();

    ExplodedViewPreset preset;
    preset.id = QUuid::createUuid();
    preset.name = nextPresetName();
    preset.mode = ExplodedViewManager::Mode::Auto;
    preset.userVector = QVector3D(1.0f, 0.0f, 0.0f);
    preset.factor = 1.0f;
    preset.outputMode = 0;
    preset.durationSeconds = 3.0;
    preset.loopBack = true;
    preset.autoStrategy = AutoStrategy::AssemblyAware;

    _presets.append(preset);
    _activePresetIndex = _presets.size() - 1;
    refreshPresetCombo();
    loadPresetIntoUi(_activePresetIndex);
    updateManualPlacementUi();
    markDocumentModified();
    emit selectionClearRequested();
}

void ExplodedViewPanel::on_pushButtonPresetDuplicate_clicked()
{
    const ExplodedViewPreset* sourcePreset = activePreset();
    if (!sourcePreset)
        return;

    stopDraftPreview();
    cancelPickingMode();
    syncActivePresetManualStateFromRuntime();
    if (_viewportWidget)
        _viewportWidget->clearExplodedViewManualPlacement();
    clearManualPlacementSelection();

    ExplodedViewPreset duplicate = *sourcePreset;
    duplicate.id = QUuid::createUuid();
    duplicate.name = nextDuplicatedPresetName(sourcePreset->name);

    _presets.append(duplicate);
    _activePresetIndex = _presets.size() - 1;
    refreshPresetCombo();
    loadPresetIntoUi(_activePresetIndex);
    updateManualPlacementUi();
    markDocumentModified();
    emit selectionClearRequested();
}

void ExplodedViewPanel::on_pushButtonPresetActions_clicked()
{
    if (!activePreset())
        return;

    QMenu menu(this);
    applyPopupMenuStyle(menu);
    QAction* renameAction = menu.addAction(tr("Rename Preset"));
    menu.addSeparator();
    QAction* deleteAction = menu.addAction(tr("Delete Preset"));
    QAction* chosenAction = menu.exec(pushButtonPresetActions
        ? pushButtonPresetActions->mapToGlobal(QPoint(0, pushButtonPresetActions->height()))
        : QCursor::pos());
    if (!chosenAction)
        return;

    if (chosenAction == renameAction)
    {
        ExplodedViewPreset* preset = activePreset();
        if (!preset)
            return;

        bool ok = false;
        const QString renamed = QInputDialog::getText(
            window(),
            tr("Rename Exploded View Preset"),
            tr("Preset name"),
            QLineEdit::Normal,
            preset->name,
            &ok).trimmed();
        if (!ok || renamed.isEmpty() || renamed == preset->name)
            return;

        preset->name = renamed;
        refreshPresetCombo();
        if (comboBoxPreset && _activePresetIndex >= 0 && _activePresetIndex < comboBoxPreset->count())
            comboBoxPreset->setCurrentIndex(_activePresetIndex);
        markDocumentModified();
        return;
    }

    if (chosenAction != deleteAction)
        return;

    const ExplodedViewPreset* preset = activePreset();
    if (!preset)
        return;

    const auto response = QMessageBox::warning(
        window(),
        tr("Delete Exploded View Preset"),
        tr("Delete preset \"%1\" and its captured steps?").arg(preset->name),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (response != QMessageBox::Yes)
        return;

    stopDraftPreview();
    cancelPickingMode();
    syncActivePresetManualStateFromRuntime();
    if (_viewportWidget)
        _viewportWidget->clearExplodedViewManualPlacement();

    const int removeIndex = _activePresetIndex;
    if (removeIndex < 0 || removeIndex >= _presets.size())
        return;

    _presets.removeAt(removeIndex);

    if (_presets.isEmpty())
    {
        _activePresetIndex = -1;
        initializeDefaultPreset();
    }
    else
    {
        _activePresetIndex = std::min<int>(removeIndex, _presets.size() - 1);
        refreshPresetCombo();
        loadPresetIntoUi(_activePresetIndex);
    }

    updateManualPlacementUi();
    markDocumentModified();
    emit selectionClearRequested();
}

void ExplodedViewPanel::updateCaptureButton()
{
    const bool hasAssembly = !assemblyUuids().isEmpty();
    const bool hasManualCaptureSet = _viewportWidget && _viewportWidget->hasExplodedViewManualTransformChanges();
    const bool hasAutoCaptureSet = hasAssembly && sliderExplosion->value() >= 10;
    const bool useCombinedPose = !checkBoxCombinedPose || checkBoxCombinedPose->isChecked();
    const bool manualMode = radioButtonModeManual && radioButtonModeManual->isChecked();
    const bool canCaptureExplosion = useCombinedPose
        ? (hasAutoCaptureSet || hasManualCaptureSet)
        : (manualMode ? hasManualCaptureSet : hasAutoCaptureSet);
    const CapturedExplosionStep* selectedStep = currentCapturedStep();

    if (pushButtonCaptureView)
        pushButtonCaptureView->setEnabled(canCaptureExplosion);
    if (pushButtonReplaceCapture)
        pushButtonReplaceCapture->setEnabled(canCaptureExplosion
            && selectedStep
            && !selectedStep->isGroup);
    if (pushButtonCapture)
        pushButtonCapture->setEnabled(!activeCapturedSteps().isEmpty());
    if (pushButtonRemoveCapture)
        pushButtonRemoveCapture->setEnabled(listWidgetCapturedViews
            && currentCapturedStep());
    updateCaptureMoveButtons();
    updatePresetDirtyIndicator();
}

void ExplodedViewPanel::updatePresetDirtyIndicator()
{
    if (!labelPresetDirty)
        return;

    const bool hasUncapturedPose = _hasUncapturedAutoPose || _hasUncapturedManualPose;
    const bool documentDirty = owningModelViewer() && owningModelViewer()->documentModified();

    if (hasUncapturedPose)
    {
        labelPresetDirty->setText(tr("Uncaptured"));
        labelPresetDirty->setToolTip(tr("There are staged auto or manual placements that\nhave not been captured into a step yet."));
        labelPresetDirty->setVisible(true);
        return;
    }

    if (documentDirty)
    {
        labelPresetDirty->setText(tr("Unsaved"));
        labelPresetDirty->setToolTip(tr("The exploded-view presets or captures have changed and\nneed to be saved with the document."));
        labelPresetDirty->setVisible(true);
        return;
    }

    labelPresetDirty->clear();
    labelPresetDirty->setToolTip(QString());
    labelPresetDirty->setVisible(false);
}

void ExplodedViewPanel::updateAuthoringModeUi()
{
    stopDraftPreview();
    const bool autoMode = !radioButtonModeManual || !radioButtonModeManual->isChecked();

    if (!autoMode)
    {
        cancelPickingMode();
        {
            QSignalBlocker b1(pushButtonSelectAssembly);
            QSignalBlocker b2(pushButtonSelectAnchor);
            pushButtonSelectAssembly->setChecked(false);
            pushButtonSelectAnchor->setChecked(false);
        }
        updateAssemblyPickButtonVisual(false);
    }
    else if (_viewportWidget && _viewportWidget->isExplodedViewManualPlacementActive())
    {
        _viewportWidget->finishExplodedViewManualPlacement();
    }

    if (stackedWidgetAuthoring)
        stackedWidgetAuthoring->setCurrentWidget(autoMode ? pageAutoExplode : pageManualPlacement);

    if (frameVector)
        frameVector->setVisible(autoMode && comboBoxMode && comboBoxMode->currentIndex() == 4);
    if (labelModeAutoStrategy)
        labelModeAutoStrategy->setVisible(autoMode);
    if (comboBoxAutoStrategy)
        comboBoxAutoStrategy->setVisible(autoMode);

    updateManualPlacementUi();
    updateCaptureButton();
}

void ExplodedViewPanel::updateManualPlacementUi()
{
    if (!pushButtonStartManualPlacement || !_viewportWidget)
        return;

    const bool manualMode = radioButtonModeManual && radioButtonModeManual->isChecked();
    const bool active = _viewportWidget->isExplodedViewManualPlacementActive();
    const bool hasPlacement = _viewportWidget->hasExplodedViewManualPlacement();

    pushButtonStartManualPlacement->setEnabled(manualMode && !active);
    pushButtonFinishManualPlacement->setEnabled(manualMode && active);
    pushButtonClearManualPlacement->setEnabled(manualMode && hasPlacement);

    if (labelManualPlacementStatus)
    {
        if (active)
            labelManualPlacementStatus->setText(tr("Manual placement is active. Translate or rotate the selected meshes to refine the staged exploded pose."));
        else if (hasPlacement)
            labelManualPlacementStatus->setText(tr("Manual placement is staged. You can resume placement for another selection or capture the current exploded pose."));
        else
            labelManualPlacementStatus->setText(tr("Manual placement will reuse the transform gizmo to stage exploded poses without changing the real model transform."));
    }

    updateManualPlacementEditors();
}

void ExplodedViewPanel::updateManualPlacementEditors()
{
    const bool enabled = _viewportWidget && _viewportWidget->isExplodedViewManualPlacementActive();
    _syncingManualPlacementEditors = true;

    const QVector3D translation = enabled
        ? _viewportWidget->explodedViewManualPlacementTranslationDelta()
        : QVector3D(0.0f, 0.0f, 0.0f);
    const QVector3D rotation = enabled
        ? _viewportWidget->explodedViewManualPlacementRotationDelta()
        : QVector3D(0.0f, 0.0f, 0.0f);

    if (doubleSpinBoxManualDX)
    {
        doubleSpinBoxManualDX->setEnabled(enabled);
        doubleSpinBoxManualDX->setValue(translation.x());
    }
    if (doubleSpinBoxManualDY)
    {
        doubleSpinBoxManualDY->setEnabled(enabled);
        doubleSpinBoxManualDY->setValue(translation.y());
    }
    if (doubleSpinBoxManualDZ)
    {
        doubleSpinBoxManualDZ->setEnabled(enabled);
        doubleSpinBoxManualDZ->setValue(translation.z());
    }
    if (doubleSpinBoxManualRX)
    {
        doubleSpinBoxManualRX->setEnabled(enabled);
        doubleSpinBoxManualRX->setValue(rotation.x());
    }
    if (doubleSpinBoxManualRY)
    {
        doubleSpinBoxManualRY->setEnabled(enabled);
        doubleSpinBoxManualRY->setValue(rotation.y());
    }
    if (doubleSpinBoxManualRZ)
    {
        doubleSpinBoxManualRZ->setEnabled(enabled);
        doubleSpinBoxManualRZ->setValue(rotation.z());
    }

    _syncingManualPlacementEditors = false;
    updateCaptureButton();
}

double ExplodedViewPanel::currentDraftPreviewDuration() const
{
    const ExplodedViewPreset* preset = activePreset();
    if (!preset || preset->capturedSteps.isEmpty())
        return 0.0;

    const double durationSeconds = preset->durationSeconds;
    const bool loopBack = preset->loopBack;

    return loopBack ? durationSeconds * 2.0 : durationSeconds;
}

bool ExplodedViewPanel::ensureDraftPreviewSession()
{
    const QVector<CapturedExplosionStep> capturedSteps = resolvedTopLevelCapturedEntries();
    if (_draftPreviewActive || !_viewportWidget || !_sceneGraph || capturedSteps.isEmpty())
        return _draftPreviewActive;

    _draftPreviewFiles.clear();
    for (const CapturedExplosionStep& step : capturedSteps)
    {
        for (const CapturedTransformTrack& track : step.tracks)
        {
            if (!track.sourceFile.isEmpty())
                _draftPreviewFiles.insert(track.sourceFile);
        }
    }

    if (_draftPreviewFiles.isEmpty())
        return false;

    _draftPreviewMeshStates.clear();
    for (const QString& sourceFile : std::as_const(_draftPreviewFiles))
    {
        SceneNode* fileNode = _sceneGraph->findFileNode(sourceFile);
        if (!fileNode)
            continue;

        const QList<QUuid> meshUuids = _sceneGraph->collectMeshUuids(fileNode);
        for (const QUuid& meshUuid : meshUuids)
        {
            SceneMesh* mesh = _viewportWidget->getMeshByUuid(meshUuid);
            if (!mesh || _draftPreviewMeshStates.contains(meshUuid))
                continue;

            PreviewMeshState state;
            state.sceneRenderTransform = mesh->getSceneRenderTransform();
            state.explosionOffset = mesh->explosionOffset();
            state.translation = mesh->getTranslation();
            state.rotation = mesh->getRotation();
            state.scale = mesh->getScaling();
            state.rotationQuat = mesh->getRotationQuaternion();
            state.hasExactRotation = true;
            state.explodedViewTranslation = mesh->getExplodedViewTranslation();
            state.explodedViewRotation = mesh->getExplodedViewRotation();
            state.explodedViewScale = mesh->getExplodedViewScaling();
            state.explodedViewRotationQuat = mesh->getExplodedViewRotationQuaternion();
            state.hasExactExplodedViewRotation = true;
            _draftPreviewMeshStates.insert(meshUuid, state);
        }
    }

    _draftPreviewSuspendedAnimation = {};
    if (!_viewportWidget->activeAnimationFile().isEmpty() && _viewportWidget->activeAnimationClip() >= 0)
    {
        _draftPreviewSuspendedAnimation.valid = true;
        _draftPreviewSuspendedAnimation.sourceFile = _viewportWidget->activeAnimationFile();
        _draftPreviewSuspendedAnimation.clipIndex = _viewportWidget->activeAnimationClip();
        _draftPreviewSuspendedAnimation.timeSeconds = _viewportWidget->currentAnimationTimeSeconds();
        _draftPreviewSuspendedAnimation.wasPlaying = _viewportWidget->isAnimationPlaying();
        _viewportWidget->setAnimationPlaying(false);
        _viewportWidget->seekAnimation(0.0);
    }

    for (auto it = _draftPreviewMeshStates.begin(); it != _draftPreviewMeshStates.end(); ++it)
    {
        if (SceneMesh* mesh = _viewportWidget->getMeshByUuid(it.key()))
        {
            mesh->setExplosionOffset(QVector3D());
            mesh->resetExplodedViewTransformations();
        }
    }

    _draftPreviewCurrentTime = 0.0;
    _draftPreviewActive = true;
    _draftPreviewPlaying = false;
    applyDraftPreviewPose(0.0);
    return true;
}

void ExplodedViewPanel::stopDraftPreview(bool restoreScene)
{
    if (_draftPreviewTimer)
        _draftPreviewTimer->stop();
    _draftPreviewPlaying = false;

    if (!_draftPreviewActive)
    {
        updatePreviewControls();
        return;
    }

    if (restoreScene && _viewportWidget)
    {
        for (auto it = _draftPreviewMeshStates.cbegin(); it != _draftPreviewMeshStates.cend(); ++it)
        {
            SceneMesh* mesh = _viewportWidget->getMeshByUuid(it.key());
            if (!mesh)
                continue;

            const PreviewMeshState& state = it.value();
            mesh->setSceneRenderTransform(state.sceneRenderTransform);
            mesh->setExplosionOffset(state.explosionOffset);
            mesh->setTranslation(state.translation);
            if (state.hasExactRotation)
                mesh->setRotationQuaternion(state.rotationQuat, state.rotation);
            else
                mesh->setRotation(state.rotation);
            mesh->setScaling(state.scale);
            mesh->setExplodedViewTranslation(state.explodedViewTranslation);
            if (state.hasExactExplodedViewRotation)
                mesh->setExplodedViewRotationQuaternion(
                    state.explodedViewRotationQuat, state.explodedViewRotation);
            else
                mesh->setExplodedViewRotation(state.explodedViewRotation);
            mesh->setExplodedViewScaling(state.explodedViewScale);
        }

        if (_draftPreviewSuspendedAnimation.valid)
        {
            _viewportWidget->setActiveAnimation(_draftPreviewSuspendedAnimation.sourceFile,
                                          _draftPreviewSuspendedAnimation.clipIndex);
            _viewportWidget->seekAnimation(_draftPreviewSuspendedAnimation.timeSeconds);
            _viewportWidget->setAnimationPlaying(_draftPreviewSuspendedAnimation.wasPlaying);
        }

        _viewportWidget->update();
    }

    _draftPreviewActive = false;
    _draftPreviewCurrentTime = 0.0;
    _draftPreviewFiles.clear();
    _draftPreviewMeshStates.clear();
    _draftPreviewSuspendedAnimation = {};
    updatePreviewControls();
}

void ExplodedViewPanel::applyDraftPreviewPose(double timeSeconds)
{
    if (!_draftPreviewActive || !_viewportWidget || !_sceneGraph)
        return;

    const ExplodedViewPreset& preset = ensureActivePreset();
    const QVector<CapturedExplosionStep> capturedSteps = resolvedTopLevelCapturedEntries();
    const double durationSeconds = currentDraftPreviewDuration();
    if (durationSeconds <= 0.0)
        return;

    const double clampedTime = qBound(0.0, timeSeconds, durationSeconds);
    const double forwardDuration = preset.durationSeconds;
    const bool loopBack = preset.loopBack;

    enum class OutputMode { ParallelSingle, SequentialSingle, SeparateClips };
    const OutputMode mode = (preset.outputMode == 1)
        ? OutputMode::SequentialSingle
        : (preset.outputMode == 2)
            ? OutputMode::SeparateClips
            : OutputMode::ParallelSingle;

    struct SampledNodeState
    {
        QVector3D translation;
        QQuaternion rotation;
        QVector3D scale = QVector3D(1.0f, 1.0f, 1.0f);
    };

    QHash<QUuid, SampledNodeState> sampledByNode;
    QHash<QUuid, SampledNodeState> sampledByMesh;

    if (mode == OutputMode::ParallelSingle)
    {
        QHash<QUuid, CapturedTransformTrack> finalTracksByNode;
        for (const CapturedExplosionStep& step : capturedSteps)
        {
            for (const CapturedTransformTrack& track : step.tracks)
            {
                const QUuid targetUuid = track.targetKind == GltfAnimationBindingTargetKind::Mesh
                    ? track.meshUuid
                    : track.ownerNodeUuid;
                finalTracksByNode.insert(targetUuid, track);
            }
        }

        double localTime = clampedTime;
        bool reversing = false;
        if (loopBack && localTime > forwardDuration)
        {
            localTime -= forwardDuration;
            reversing = true;
        }
        const double segmentDuration = qMax(1.0e-6, forwardDuration);
        const float t = static_cast<float>(qBound(0.0, localTime / segmentDuration, 1.0));

        for (auto it = finalTracksByNode.cbegin(); it != finalTracksByNode.cend(); ++it)
        {
            const CapturedTransformTrack& track = it.value();
            SampledNodeState state;
            state.scale = QVector3D(1.0f, 1.0f, 1.0f);
            if (!reversing)
            {
                state.translation = track.startPosition * (1.0f - t) + track.endPosition * t;
                state.rotation = QQuaternion::slerp(track.startRotation, track.endRotation, t).normalized();
            }
            else
            {
                state.translation = track.endPosition * (1.0f - t) + track.startPosition * t;
                state.rotation = QQuaternion::slerp(track.endRotation, track.startRotation, t).normalized();
            }
            if (track.targetKind == GltfAnimationBindingTargetKind::Mesh)
                sampledByMesh.insert(track.meshUuid, state);
            else
            {
                const SceneNode* node = _sceneGraph->findNodeByUuid(track.ownerNodeUuid);
                if (!node)
                    continue;
                const LocalNodeTransform base = decomposeLocalNodeTransform(aiToQMatrix(node->localTransform));
                state.scale = base.scale;
                sampledByNode.insert(track.ownerNodeUuid, state);
            }
        }
    }
    else if (mode == OutputMode::SequentialSingle)
    {
        struct NodeState
        {
            GltfAnimationBindingTargetKind targetKind = GltfAnimationBindingTargetKind::Node;
            QVector3D basePosition;
            QVector3D currentPosition;
            QQuaternion baseRotation;
            QQuaternion currentRotation;
            QVector3D scale = QVector3D(1.0f, 1.0f, 1.0f);
        };

        QHash<QUuid, NodeState> states;
        for (const CapturedExplosionStep& step : capturedSteps)
        {
            for (const CapturedTransformTrack& track : step.tracks)
            {
                const QUuid targetUuid = track.targetKind == GltfAnimationBindingTargetKind::Mesh
                    ? track.meshUuid
                    : track.ownerNodeUuid;
                if (states.contains(targetUuid))
                    continue;

                NodeState state;
                state.targetKind = track.targetKind;
                state.basePosition = track.startPosition;
                state.currentPosition = track.startPosition;
                state.baseRotation = track.startRotation;
                state.currentRotation = track.startRotation;
                state.scale = QVector3D(1.0f, 1.0f, 1.0f);
                if (track.targetKind == GltfAnimationBindingTargetKind::Node)
                {
                    const SceneNode* node = _sceneGraph->findNodeByUuid(track.ownerNodeUuid);
                    if (!node)
                        continue;
                    const LocalNodeTransform base = decomposeLocalNodeTransform(aiToQMatrix(node->localTransform));
                    state.scale = base.scale;
                }
                states.insert(targetUuid, state);
            }
        }

        const qsizetype stepCount = std::max<qsizetype>(1, capturedSteps.size());
        const double segmentDuration = forwardDuration / static_cast<double>(stepCount);

        QVector<QHash<QUuid, QVector3D>> positionSnapshots;
        QVector<QHash<QUuid, QQuaternion>> rotationSnapshots;
        positionSnapshots.reserve(capturedSteps.size() + 1);
        rotationSnapshots.reserve(capturedSteps.size() + 1);

        QHash<QUuid, QVector3D> initialPositions;
        QHash<QUuid, QQuaternion> initialRotations;
        for (auto it = states.cbegin(); it != states.cend(); ++it)
        {
            initialPositions.insert(it.key(), it->basePosition);
            initialRotations.insert(it.key(), it->baseRotation);
        }
        positionSnapshots.append(initialPositions);
        rotationSnapshots.append(initialRotations);

        for (const CapturedExplosionStep& step : capturedSteps)
        {
            for (const CapturedTransformTrack& track : step.tracks)
            {
                const QUuid targetUuid = track.targetKind == GltfAnimationBindingTargetKind::Mesh
                    ? track.meshUuid
                    : track.ownerNodeUuid;
                auto it = states.find(targetUuid);
                if (it == states.end())
                    continue;
                it->currentPosition = track.endPosition;
                it->currentRotation = track.endRotation;
            }

            QHash<QUuid, QVector3D> snapshotPositions;
            QHash<QUuid, QQuaternion> snapshotRotations;
            for (auto it = states.cbegin(); it != states.cend(); ++it)
            {
                snapshotPositions.insert(it.key(), it->currentPosition);
                snapshotRotations.insert(it.key(), it->currentRotation);
            }
            positionSnapshots.append(snapshotPositions);
            rotationSnapshots.append(snapshotRotations);
        }

        auto sampleSequential = [&](double sampleTime) {
            const double bounded = qBound(0.0, sampleTime, forwardDuration);
            int segmentIndex = segmentDuration > 1.0e-6
                ? qMin<int>(static_cast<int>(bounded / segmentDuration), static_cast<int>(stepCount) - 1)
                : 0;
            if (bounded >= forwardDuration)
                segmentIndex = static_cast<int>(stepCount) - 1;

            const double segmentStart = segmentDuration * static_cast<double>(segmentIndex);
            const double segmentEnd = segmentDuration * static_cast<double>(segmentIndex + 1);
            const float t = segmentEnd > segmentStart
                ? static_cast<float>((bounded - segmentStart) / (segmentEnd - segmentStart))
                : 1.0f;

            const QHash<QUuid, QVector3D>& fromPositions = positionSnapshots[segmentIndex];
            const QHash<QUuid, QVector3D>& toPositions = positionSnapshots[segmentIndex + 1];
            const QHash<QUuid, QQuaternion>& fromRotations = rotationSnapshots[segmentIndex];
            const QHash<QUuid, QQuaternion>& toRotations = rotationSnapshots[segmentIndex + 1];

            for (auto it = states.cbegin(); it != states.cend(); ++it)
            {
                SampledNodeState state;
                state.scale = it->scale;
                const QVector3D fromPosition = fromPositions.value(it.key(), it->basePosition);
                const QVector3D toPosition = toPositions.value(it.key(), fromPosition);
                const QQuaternion fromRotation = fromRotations.value(it.key(), it->baseRotation);
                const QQuaternion toRotation = toRotations.value(it.key(), fromRotation);
                state.translation = fromPosition * (1.0f - t) + toPosition * t;
                state.rotation = QQuaternion::slerp(fromRotation, toRotation, t).normalized();
                if (it->targetKind == GltfAnimationBindingTargetKind::Mesh)
                    sampledByMesh.insert(it.key(), state);
                else
                    sampledByNode.insert(it.key(), state);
            }
        };

        if (loopBack && clampedTime > forwardDuration)
            sampleSequential(forwardDuration - (clampedTime - forwardDuration));
        else
            sampleSequential(clampedTime);
    }
    else
    {
        const int selectedRow = currentCapturedStepRow();
        const int stepIndex = (selectedRow >= 0 && selectedRow < capturedSteps.size()) ? selectedRow : 0;
        if (stepIndex < capturedSteps.size())
        {
            const CapturedExplosionStep& previewEntry = capturedSteps[stepIndex];
            double localTime = clampedTime;
            bool reversing = false;
            if (loopBack && localTime > forwardDuration)
            {
                localTime -= forwardDuration;
                reversing = true;
            }
            const float t = static_cast<float>(qBound(0.0, localTime / qMax(1.0e-6, forwardDuration), 1.0));
            for (const CapturedTransformTrack& track : previewEntry.tracks)
            {
                SampledNodeState state;
                state.scale = QVector3D(1.0f, 1.0f, 1.0f);
                if (!reversing)
                {
                    state.translation = track.startPosition * (1.0f - t) + track.endPosition * t;
                    state.rotation = QQuaternion::slerp(track.startRotation, track.endRotation, t).normalized();
                }
                else
                {
                    state.translation = track.endPosition * (1.0f - t) + track.startPosition * t;
                    state.rotation = QQuaternion::slerp(track.endRotation, track.startRotation, t).normalized();
                }
                if (track.targetKind == GltfAnimationBindingTargetKind::Mesh)
                    sampledByMesh.insert(track.meshUuid, state);
                else
                {
                    const SceneNode* node = _sceneGraph->findNodeByUuid(track.ownerNodeUuid);
                    if (!node)
                        continue;
                    const LocalNodeTransform base = decomposeLocalNodeTransform(aiToQMatrix(node->localTransform));
                    state.scale = base.scale;
                    sampledByNode.insert(track.ownerNodeUuid, state);
                }
            }
        }
    }

    for (const QString& sourceFile : std::as_const(_draftPreviewFiles))
    {
        SceneNode* fileNode = _sceneGraph->findFileNode(sourceFile);
        if (!fileNode)
            continue;

        std::function<void(SceneNode*, const QMatrix4x4&)> applyNode =
            [&](SceneNode* node, const QMatrix4x4& parentWorld)
        {
            if (!node)
                return;

            QMatrix4x4 localMatrix = aiToQMatrix(node->localTransform);
            if (sampledByNode.contains(node->nodeUuid))
            {
                const SampledNodeState& sampledState = sampledByNode.value(node->nodeUuid);
                LocalNodeTransform localTransform;
                localTransform.translation = sampledState.translation;
                localTransform.rotation = sampledState.rotation;
                localTransform.scale = sampledState.scale;
                localMatrix = composeLocalNodeTransform(localTransform);
            }

            const QMatrix4x4 world = parentWorld * localMatrix;
            for (const QUuid& meshUuid : node->meshUuids)
            {
                if (SceneMesh* mesh = _viewportWidget->getMeshByUuid(meshUuid))
                {
                    mesh->setExplosionOffset(QVector3D());
                    if (sampledByMesh.contains(meshUuid))
                    {
                        LocalNodeTransform meshLocal;
                        meshLocal.translation = sampledByMesh.value(meshUuid).translation;
                        meshLocal.rotation = sampledByMesh.value(meshUuid).rotation;
                        meshLocal.scale = sampledByMesh.value(meshUuid).scale;
                        mesh->setSceneRenderTransformFast(world * composeLocalNodeTransform(meshLocal));
                    }
                    else
                    {
                        mesh->setSceneRenderTransformFast(world);
                    }
                }
            }

            for (SceneNode* child : node->children)
                applyNode(child, world);
        };

        for (SceneNode* child : fileNode->children)
            applyNode(child, QMatrix4x4());
    }

    _draftPreviewCurrentTime = clampedTime;
    _viewportWidget->update();
}

void ExplodedViewPanel::updatePreviewControls()
{
    if (!pushButtonPreviewPlayPause || !pushButtonPreviewStop || !checkBoxPreviewLoop
        || !sliderPreviewTimeline || !labelPreviewTime || !_viewportWidget)
        return;

    _syncingPreviewControls = true;

    const double durationSeconds = currentDraftPreviewDuration();
    const bool manualPlacementActive = _viewportWidget->isExplodedViewManualPlacementActive();
    const bool enabled = !activeCapturedSteps().isEmpty() && durationSeconds > 0.0 && !manualPlacementActive;

    pushButtonPreviewPlayPause->setEnabled(enabled);
    pushButtonPreviewPlayPause->setText(_draftPreviewPlaying ? tr("Pause") : tr("Play"));
    pushButtonPreviewStop->setEnabled(enabled && (_draftPreviewActive || _draftPreviewCurrentTime > 0.0));
    checkBoxPreviewLoop->setEnabled(enabled);
    checkBoxPreviewLoop->setChecked(_draftPreviewLoopPlayback);
    sliderPreviewTimeline->setEnabled(enabled);

    if (!_previewScrubbing)
    {
        const int sliderValue = enabled
            ? qRound((_draftPreviewCurrentTime / durationSeconds) * sliderPreviewTimeline->maximum())
            : 0;
        QSignalBlocker blocker(sliderPreviewTimeline);
        sliderPreviewTimeline->setValue(qBound(0, sliderValue, sliderPreviewTimeline->maximum()));
    }

    labelPreviewTime->setText(QStringLiteral("%1 / %2")
        .arg(formatPreviewTime(_draftPreviewCurrentTime))
        .arg(formatPreviewTime(durationSeconds)));

    _syncingPreviewControls = false;
}

void ExplodedViewPanel::onPreviewPlayPauseClicked()
{
    if (activeCapturedSteps().isEmpty())
        return;

    if (_draftPreviewPlaying)
    {
        _draftPreviewPlaying = false;
        if (_draftPreviewTimer)
            _draftPreviewTimer->stop();
        updatePreviewControls();
        return;
    }

    if (!ensureDraftPreviewSession())
        return;

    if (_draftPreviewCurrentTime >= currentDraftPreviewDuration())
        applyDraftPreviewPose(0.0);

    _draftPreviewPlaying = true;
    _draftPreviewElapsed.restart();
    if (_draftPreviewTimer)
        _draftPreviewTimer->start();
    updatePreviewControls();
}

void ExplodedViewPanel::onPreviewStopClicked()
{
    stopDraftPreview(true);
}

void ExplodedViewPanel::onPreviewLoopToggled(bool checked)
{
    if (_syncingPreviewControls)
        return;

    _draftPreviewLoopPlayback = checked;
}

void ExplodedViewPanel::onPreviewSliderPressed()
{
    _previewScrubbing = true;
}

void ExplodedViewPanel::onPreviewSliderReleased()
{
    _previewScrubbing = false;
    onPreviewSliderValueChanged(sliderPreviewTimeline ? sliderPreviewTimeline->value() : 0);
}

void ExplodedViewPanel::onPreviewSliderValueChanged(int value)
{
    if (_syncingPreviewControls || !_viewportWidget || !sliderPreviewTimeline)
        return;

    const double durationSeconds = currentDraftPreviewDuration();
    if (durationSeconds <= 0.0)
        return;

    const double timeSeconds = (static_cast<double>(value) / static_cast<double>(sliderPreviewTimeline->maximum())) * durationSeconds;
    labelPreviewTime->setText(QStringLiteral("%1 / %2")
        .arg(formatPreviewTime(timeSeconds))
        .arg(formatPreviewTime(durationSeconds)));

    if (!ensureDraftPreviewSession())
        return;

    applyDraftPreviewPose(timeSeconds);
    if (_draftPreviewPlaying)
    {
        _draftPreviewPlaying = false;
        if (_draftPreviewTimer)
            _draftPreviewTimer->stop();
    }
    updatePreviewControls();
}

void ExplodedViewPanel::onDraftPreviewTick()
{
    if (!_draftPreviewPlaying)
        return;

    const double durationSeconds = currentDraftPreviewDuration();
    if (durationSeconds <= 0.0)
    {
        stopDraftPreview();
        return;
    }

    const double deltaSeconds = _draftPreviewElapsed.isValid()
        ? static_cast<double>(_draftPreviewElapsed.restart()) / 1000.0
        : 0.016;
    double nextTime = _draftPreviewCurrentTime + deltaSeconds;
    if (nextTime >= durationSeconds)
    {
        if (_draftPreviewLoopPlayback)
            nextTime = std::fmod(nextTime, durationSeconds);
        else
        {
            applyDraftPreviewPose(durationSeconds);
            _draftPreviewPlaying = false;
            if (_draftPreviewTimer)
                _draftPreviewTimer->stop();
            updatePreviewControls();
            return;
        }
    }

    applyDraftPreviewPose(nextTime);
    updatePreviewControls();
}

void ExplodedViewPanel::updateAssemblyPickButtonVisual(bool awaitingCommit)
{
    pushButtonSelectAssembly->setIcon(awaitingCommit ? _assemblySelectCommitIcon
                                                     : _assemblySelectIdleIcon);
    pushButtonSelectAssembly->setToolTip(awaitingCommit
        ? tr("Adjust scene or tree selection, then click again to confirm")
        : tr("Pick meshes from scene or tree"));
}

void ExplodedViewPanel::clearAssemblySelection()
{
    lineEditAssembly->clear();
    lineEditAnchor->clear();
    ExplodedViewPreset& preset = ensureActivePreset();
    preset.assemblyUuids.clear();
    preset.anchorUuid = QUuid();
    _hasUncapturedAutoPose = false;
}

void ExplodedViewPanel::on_pushButtonStartManualPlacement_clicked()
{
    stopDraftPreview();
    if (!_viewportWidget)
        return;

    if (_manualPlacementSelectionUuids.isEmpty())
    {
        const QList<int> selectedIds = _viewportWidget->getSelectionManager()->getSelectedIds();
        if (selectedIds.isEmpty())
        {
            QMessageBox::information(
                window(),
                tr("Start Placement"),
                tr("Select one or more meshes in the tree or viewport before starting manual placement."));
            return;
        }

        applyManualPlacementSelection(selectedIds);
    }

    if (_viewportWidget->beginExplodedViewManualPlacement(_manualPlacementSelectionUuids))
    {
        updateManualPlacementUi();
        updateCaptureButton();
        updatePreviewControls();
    }
}

void ExplodedViewPanel::on_pushButtonFinishManualPlacement_clicked()
{
    stopDraftPreview();
    if (_viewportWidget)
    {
        _viewportWidget->finishExplodedViewManualPlacement();
        clearManualPlacementSelection();
        syncActivePresetManualStateFromRuntime();
        updateManualPlacementUi();
        updateCaptureButton();
        updatePreviewControls();
        markDocumentModified();
    }
}

void ExplodedViewPanel::on_pushButtonClearManualPlacement_clicked()
{
    stopDraftPreview();
    if (_viewportWidget)
    {
        _viewportWidget->clearExplodedViewManualPlacement();
        clearManualPlacementSelection();
        if (ExplodedViewPreset* preset = activePreset())
            preset->uncapturedManualStates.clear();
        syncActivePresetManualStateFromRuntime();
        _hasUncapturedManualPose = false;
        updateManualPlacementUi();
        updateCaptureButton();
        updatePreviewControls();
        markDocumentModified();
        emit explosionParametersChanged();
    }
}

void ExplodedViewPanel::updateCapturedViewsList()
{
    if (!listWidgetCapturedViews)
        return;

    const QUuid selectedId = currentCapturedStepId();
    _syncingCapturedViewsList = true;
    listWidgetCapturedViews->clear();
    const std::function<void(QTreeWidgetItem*, const CapturedExplosionStep&)> addItem =
        [&](QTreeWidgetItem* parentItem, const CapturedExplosionStep& step)
    {
        QTreeWidgetItem* item = parentItem
            ? new QTreeWidgetItem(parentItem)
            : new QTreeWidgetItem(listWidgetCapturedViews);
        item->setText(0, step.name);
        item->setData(0, Qt::UserRole, step.id);
        item->setData(0, Qt::UserRole + 1, step.isGroup);
        const QVector<CapturedTransformTrack> tracks = step.isGroup ? resolvedTracksForStep(step) : step.tracks;
        item->setToolTip(0, step.isGroup
            ? tr("%1 capture%2, %3 mesh%4")
                .arg(step.children.size())
                .arg(step.children.size() == 1 ? QString() : QStringLiteral("s"))
                .arg(tracks.size())
                .arg(tracks.size() == 1 ? QString() : QStringLiteral("es"))
            : QStringLiteral("%1 mesh%2")
                .arg(tracks.size())
                .arg(tracks.size() == 1 ? QString() : QStringLiteral("es")));
        Qt::ItemFlags flags = item->flags() | Qt::ItemIsEditable;
        flags &= ~Qt::ItemIsDragEnabled;
        flags &= ~Qt::ItemIsDropEnabled;
        item->setFlags(flags);
        if (step.isGroup)
            item->setExpanded(true);

        if (step.isGroup)
        {
            for (const CapturedExplosionStep& child : step.children)
                addItem(item, child);
        }
    };

    for (const CapturedExplosionStep& step : activeCapturedSteps())
        addItem(nullptr, step);
    _syncingCapturedViewsList = false;

    if (!selectedId.isNull())
    {
        const QList<QTreeWidgetItem*> matches = listWidgetCapturedViews->findItems(
            QStringLiteral("*"), Qt::MatchWildcard | Qt::MatchRecursive, 0);
        for (QTreeWidgetItem* item : matches)
        {
            if (item && item->data(0, Qt::UserRole).toUuid() == selectedId)
            {
                listWidgetCapturedViews->clearSelection();
                listWidgetCapturedViews->setCurrentItem(
                    item,
                    0,
                    QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Current);
                listWidgetCapturedViews->scrollToItem(item);
                break;
            }
        }
    }

    updateCaptureMoveButtons();
}

void ExplodedViewPanel::updateCaptureMoveButtons()
{
    bool canMoveUp = false;
    bool canMoveDown = false;
    if (listWidgetCapturedViews)
    {
        QTreeWidgetItem* current = listWidgetCapturedViews->currentItem();
        const QList<QTreeWidgetItem*> selectedItems = listWidgetCapturedViews->selectedItems();
        if (current && selectedItems.size() <= 1)
        {
            const int siblingCount = current->parent()
                ? current->parent()->childCount()
                : listWidgetCapturedViews->topLevelItemCount();
            const int row = current->parent()
                ? current->parent()->indexOfChild(current)
                : listWidgetCapturedViews->indexOfTopLevelItem(current);
            canMoveUp = row > 0;
            canMoveDown = row >= 0 && row < siblingCount - 1;
        }
    }

    if (pushButtonMoveCaptureUp)
        pushButtonMoveCaptureUp->setEnabled(canMoveUp);
    if (pushButtonMoveCaptureDown)
        pushButtonMoveCaptureDown->setEnabled(canMoveDown);
}

bool ExplodedViewPanel::moveCapturedStep(const QUuid& stepId, int direction)
{
    if (stepId.isNull() || direction == 0)
        return false;

    QVector<CapturedExplosionStep>& steps = activeCapturedSteps();
    if (!moveCapturedStepInList(steps, stepId, direction))
        return false;

    normalizeCapturedGroups(steps);
    markDocumentModified();
    return true;
}

bool ExplodedViewPanel::moveCapturedStepInList(QVector<CapturedExplosionStep>& steps, const QUuid& stepId, int direction)
{
    for (int index = 0; index < steps.size(); ++index)
    {
        if (steps[index].id == stepId)
        {
            const int targetIndex = index + direction;
            if (targetIndex < 0 || targetIndex >= steps.size())
                return false;

            steps.swapItemsAt(index, targetIndex);
            return true;
        }

        if (steps[index].isGroup && moveCapturedStepInList(steps[index].children, stepId, direction))
            return true;
    }

    return false;
}

int ExplodedViewPanel::currentCapturedStepRow() const
{
    if (!listWidgetCapturedViews)
        return -1;

    QTreeWidgetItem* current = listWidgetCapturedViews->currentItem();
    if (!current)
        return -1;

    while (current->parent())
        current = current->parent();
    return listWidgetCapturedViews->indexOfTopLevelItem(current);
}

void ExplodedViewPanel::setCurrentCapturedStepRow(int row)
{
    if (!listWidgetCapturedViews)
        return;

    if (row < 0 || row >= listWidgetCapturedViews->topLevelItemCount())
    {
        listWidgetCapturedViews->setCurrentItem(nullptr);
        return;
    }

    listWidgetCapturedViews->setCurrentItem(listWidgetCapturedViews->topLevelItem(row));
}

void ExplodedViewPanel::onCapturedViewItemChanged(QTreeWidgetItem* item, int column)
{
    if (_syncingCapturedViewsList || !item || column != 0)
        return;

    const QUuid stepId = item->data(0, Qt::UserRole).toUuid();
    const QString trimmedName = item->text(0).trimmed();
    CapturedExplosionStep* step = findCapturedStepById(activeCapturedSteps(), stepId);
    if (!step)
        return;

    const QString resolvedName = trimmedName.isEmpty()
        ? (step->isGroup ? tr("Group") : tr("Step"))
        : trimmedName;
    step->name = resolvedName;
    if (item->text(0) != resolvedName)
    {
        _syncingCapturedViewsList = true;
        item->setText(0, resolvedName);
        _syncingCapturedViewsList = false;
    }
    markDocumentModified();
}

void ExplodedViewPanel::onCapturedViewsContextMenuRequested(const QPoint& pos)
{
    if (!listWidgetCapturedViews)
        return;

    QTreeWidgetItem* item = listWidgetCapturedViews->itemAt(pos);
    if (item && !item->isSelected())
        listWidgetCapturedViews->setCurrentItem(item);

    QMenu menu(this);
    applyPopupMenuStyle(menu);

    const QList<QTreeWidgetItem*> selectedItems = listWidgetCapturedViews->selectedItems();
    bool allTopLevelLeaf = selectedItems.size() >= 2;
    for (QTreeWidgetItem* selected : selectedItems)
    {
        if (!selected || selected->parent() != nullptr || selected->data(0, Qt::UserRole + 1).toBool())
        {
            allTopLevelLeaf = false;
            break;
        }
    }

    QAction* groupAction = nullptr;
    if (allTopLevelLeaf)
        groupAction = menu.addAction(tr("Group Selected"));

    QAction* ungroupAction = nullptr;
    QAction* removeAction = nullptr;
    if (selectedItems.size() == 1)
    {
        QTreeWidgetItem* selected = selectedItems.front();
        const bool isGroup = selected && selected->data(0, Qt::UserRole + 1).toBool();
        if (isGroup)
            ungroupAction = menu.addAction(tr("Ungroup"));
        removeAction = menu.addAction(tr("Remove"));
    }

    QAction* chosen = menu.exec(listWidgetCapturedViews->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == groupAction)
    {
        stopDraftPreview();
        if (groupSelectedCaptures())
        {
            updateCapturedViewsList();
            updateCaptureButton();
            updatePreviewControls();
        }
        return;
    }

    if (chosen == ungroupAction)
    {
        stopDraftPreview();
        if (ungroupCapturedStep(currentCapturedStepId()))
        {
            updateCapturedViewsList();
            updateCaptureButton();
            updatePreviewControls();
        }
        return;
    }

    if (chosen == removeAction)
    {
        on_pushButtonRemoveCapture_clicked();
    }
}
