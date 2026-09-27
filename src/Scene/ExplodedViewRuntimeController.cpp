#include "ExplodedViewRuntimeController.h"
#include "SceneMesh.h"

#include <cmath>

void ExplodedViewRuntimeController::setHintsCache(
    const QSet<QUuid>& assemblyUuids,
    const QUuid& anchorUuid,
    const AssemblyRelationGraph::AutoPlacementHints& hints)
{
    _cachedAutoHints          = hints;
    _cachedHintsAssemblyUuids = assemblyUuids;
    _cachedHintsAnchorUuid    = anchorUuid;
    _cachedHintsValid         = true;
}

void ExplodedViewRuntimeController::resetSessionTransforms()
{
    _explodedViewManualPlacementSessionUuids.clear();
    _explodedViewManualSessionStartStates.clear();
    _explodedViewManualSessionStartMatrices.clear();
    _explodedViewManualSessionStartPivot         = QVector3D(0.0f, 0.0f, 0.0f);
    _explodedViewManualSessionTranslationDelta   = QVector3D(0.0f, 0.0f, 0.0f);
    _explodedViewManualSessionRotationQuat       = QQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
    _explodedViewManualSessionRotationEuler      = QVector3D(0.0f, 0.0f, 0.0f);
    _explodedViewManualDragStartTranslationDelta = QVector3D(0.0f, 0.0f, 0.0f);
    _explodedViewManualDragStartRotationQuat     = QQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
    _explodedViewManualDragStartRotationEuler    = QVector3D(0.0f, 0.0f, 0.0f);
}

void ExplodedViewRuntimeController::clearManualPlacement()
{
    _explodedViewManualPlacementActive = false;
    resetSessionTransforms();
    _explodedViewManualHiddenStates.clear();
    _explodedViewManualPlacementSuppressed = false;
    _explodedViewManualOriginalStates.clear();
}

// ---------------------------------------------------------------------------
// Static transform helpers
// ---------------------------------------------------------------------------

static constexpr float kExplodedManualTransformTolerance = 1.0e-8f;

bool ExplodedViewRuntimeController::transformStatesNearlyEqual(const TransformState& a,
                                                                const TransformState& b)
{
    if ((a.translation - b.translation).lengthSquared() > kExplodedManualTransformTolerance)
        return false;
    if ((a.scale - b.scale).lengthSquared() > kExplodedManualTransformTolerance)
        return false;

    if (a.hasExactRotation || b.hasExactRotation)
    {
        const QQuaternion identity(1.0f, 0.0f, 0.0f, 0.0f);
        const QQuaternion aq = a.hasExactRotation ? a.rotationQuat.normalized() : identity;
        const QQuaternion bq = b.hasExactRotation ? b.rotationQuat.normalized() : identity;
        return std::abs(QQuaternion::dotProduct(aq, bq)) >= (1.0f - 1.0e-4f);
    }

    return (a.rotation - b.rotation).lengthSquared() <= kExplodedManualTransformTolerance;
}

TransformState ExplodedViewRuntimeController::explodedViewTransformState(const SceneMesh* mesh)
{
    if (!mesh)
        return {};
    return TransformState(
        mesh->getExplodedViewTranslation(),
        mesh->getExplodedViewRotation(),
        mesh->getExplodedViewScaling(),
        mesh->getExplodedViewRotationQuaternion());
}

QMatrix4x4 ExplodedViewRuntimeController::explodedViewTransformMatrix(const SceneMesh* mesh)
{
    return mesh ? mesh->getExplodedViewTransformation() : QMatrix4x4();
}

void ExplodedViewRuntimeController::applyExplodedViewTransformState(SceneMesh* mesh,
                                                                      const TransformState& state,
                                                                      bool fast)
{
    if (!mesh)
        return;

    if (fast)
    {
        mesh->setExplodedViewTranslationFast(state.translation);
        if (state.hasExactRotation)
            mesh->setExplodedViewRotationQuaternionFast(state.rotationQuat, state.rotation);
        else
            mesh->setExplodedViewRotationFast(state.rotation);
        mesh->setExplodedViewScalingFast(state.scale);
        return;
    }

    mesh->setExplodedViewTranslation(state.translation);
    if (state.hasExactRotation)
        mesh->setExplodedViewRotationQuaternion(state.rotationQuat, state.rotation);
    else
        mesh->setExplodedViewRotation(state.rotation);
    mesh->setExplodedViewScaling(state.scale);
}

