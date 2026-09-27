#pragma once

#include "AssemblyRelationGraph.h"
#include "RenderableMesh.h"

class SceneMesh;
#include "TransformCommand.h"

#include <QMap>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QSet>
#include <QUuid>
#include <QVector3D>

class ExplodedViewManager;

// ---------------------------------------------------------------------------
// ExplodedViewRuntimeController
//
// Groups all exploded-view runtime state that was previously scattered
// through ViewportWidget's private section.  ViewportWidget embeds one instance and
// accesses state through this API.
//
// Introduced in Phase 9 of the mesh/render/runtime separation refactor.
// De-aliased in the controller ownership cleanup pass.
//
// Note: _explodedViewManager is created by ViewportWidget with itself as Qt parent
// (required for ownership).  ExplodedViewRuntimeController stores the pointer
// only — ownership stays with ViewportWidget.
// ---------------------------------------------------------------------------
class ExplodedViewRuntimeController
{
public:
    // ---- Manager -----------------------------------------------------------
    ExplodedViewManager* explodedViewManager() const    { return _explodedViewManager; }
    void setExplodedViewManager(ExplodedViewManager* m) { _explodedViewManager = m; }

    // ---- Hints cache -------------------------------------------------------
    bool cachedHintsValid() const                        { return _cachedHintsValid; }
    const QSet<QUuid>& cachedHintsAssemblyUuids() const  { return _cachedHintsAssemblyUuids; }
    const QUuid& cachedHintsAnchorUuid() const           { return _cachedHintsAnchorUuid; }
    AssemblyRelationGraph::AutoPlacementHints& cachedAutoHints() { return _cachedAutoHints; }

    void setHintsCache(const QSet<QUuid>& assemblyUuids,
                       const QUuid& anchorUuid,
                       const AssemblyRelationGraph::AutoPlacementHints& hints);
    void invalidateHintsCache() { _cachedHintsValid = false; }

    // ---- Manual placement session ------------------------------------------
    bool isManualPlacementActive() const         { return _explodedViewManualPlacementActive; }
    void setManualPlacementActive(bool active)   { _explodedViewManualPlacementActive = active; }

    bool isManualPlacementSuppressed() const         { return _explodedViewManualPlacementSuppressed; }
    void setManualPlacementSuppressed(bool s)        { _explodedViewManualPlacementSuppressed = s; }

    QMap<QUuid, TransformState>& manualOriginalStates()       { return _explodedViewManualOriginalStates; }
    const QMap<QUuid, TransformState>& manualOriginalStates() const { return _explodedViewManualOriginalStates; }

    QMap<QUuid, TransformState>& manualHiddenStates()       { return _explodedViewManualHiddenStates; }
    const QMap<QUuid, TransformState>& manualHiddenStates() const { return _explodedViewManualHiddenStates; }

    QSet<QUuid>& manualPlacementSessionUuids()       { return _explodedViewManualPlacementSessionUuids; }
    const QSet<QUuid>& manualPlacementSessionUuids() const { return _explodedViewManualPlacementSessionUuids; }

    QMap<QUuid, TransformState>& manualSessionStartStates()       { return _explodedViewManualSessionStartStates; }
    const QMap<QUuid, TransformState>& manualSessionStartStates() const { return _explodedViewManualSessionStartStates; }

    QMap<QUuid, QMatrix4x4>& manualSessionStartMatrices()       { return _explodedViewManualSessionStartMatrices; }
    const QMap<QUuid, QMatrix4x4>& manualSessionStartMatrices() const { return _explodedViewManualSessionStartMatrices; }

    // ---- Session transform deltas ------------------------------------------
    const QVector3D& manualSessionStartPivot() const        { return _explodedViewManualSessionStartPivot; }
    void setManualSessionStartPivot(const QVector3D& v)     { _explodedViewManualSessionStartPivot = v; }

    const QVector3D& manualSessionTranslationDelta() const       { return _explodedViewManualSessionTranslationDelta; }
    void setManualSessionTranslationDelta(const QVector3D& v)    { _explodedViewManualSessionTranslationDelta = v; }

    const QQuaternion& manualSessionRotationQuat() const         { return _explodedViewManualSessionRotationQuat; }
    void setManualSessionRotationQuat(const QQuaternion& q)      { _explodedViewManualSessionRotationQuat = q; }

    const QVector3D& manualSessionRotationEuler() const          { return _explodedViewManualSessionRotationEuler; }
    void setManualSessionRotationEuler(const QVector3D& v)       { _explodedViewManualSessionRotationEuler = v; }

    const QVector3D& manualDragStartTranslationDelta() const     { return _explodedViewManualDragStartTranslationDelta; }
    void setManualDragStartTranslationDelta(const QVector3D& v)  { _explodedViewManualDragStartTranslationDelta = v; }

    const QQuaternion& manualDragStartRotationQuat() const       { return _explodedViewManualDragStartRotationQuat; }
    void setManualDragStartRotationQuat(const QQuaternion& q)    { _explodedViewManualDragStartRotationQuat = q; }

    const QVector3D& manualDragStartRotationEuler() const        { return _explodedViewManualDragStartRotationEuler; }
    void setManualDragStartRotationEuler(const QVector3D& v)     { _explodedViewManualDragStartRotationEuler = v; }

    // ---- Semantic helpers --------------------------------------------------
    // Resets all session-scoped transform state (called at session start/end).
    void resetSessionTransforms();

    // Clears the full manual placement session (active flag + all session state).
    void clearManualPlacement();

    // ---- Static transform helpers ------------------------------------------
    static bool         transformStatesNearlyEqual(const TransformState& a, const TransformState& b);
    static TransformState explodedViewTransformState(const SceneMesh* mesh);
    static QMatrix4x4   explodedViewTransformMatrix(const SceneMesh* mesh);
    static void         applyExplodedViewTransformState(SceneMesh* mesh,
                                                         const TransformState& state,
                                                         bool fast);

private:
    // Manager
    ExplodedViewManager* _explodedViewManager = nullptr;

    // Hints cache
    QSet<QUuid>                               _cachedHintsAssemblyUuids;
    QUuid                                     _cachedHintsAnchorUuid;
    AssemblyRelationGraph::AutoPlacementHints _cachedAutoHints;
    bool                                      _cachedHintsValid = false;

    // Manual placement session
    bool                        _explodedViewManualPlacementActive      = false;
    QMap<QUuid, TransformState> _explodedViewManualOriginalStates;
    QMap<QUuid, TransformState> _explodedViewManualHiddenStates;
    bool                        _explodedViewManualPlacementSuppressed   = false;
    QSet<QUuid>                 _explodedViewManualPlacementSessionUuids;
    QMap<QUuid, TransformState> _explodedViewManualSessionStartStates;
    QMap<QUuid, QMatrix4x4>    _explodedViewManualSessionStartMatrices;

    // Session transform deltas
    QVector3D   _explodedViewManualSessionStartPivot         = QVector3D(0.0f, 0.0f, 0.0f);
    QVector3D   _explodedViewManualSessionTranslationDelta   = QVector3D(0.0f, 0.0f, 0.0f);
    QQuaternion _explodedViewManualSessionRotationQuat       = QQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
    QVector3D   _explodedViewManualSessionRotationEuler      = QVector3D(0.0f, 0.0f, 0.0f);
    QVector3D   _explodedViewManualDragStartTranslationDelta = QVector3D(0.0f, 0.0f, 0.0f);
    QQuaternion _explodedViewManualDragStartRotationQuat     = QQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
    QVector3D   _explodedViewManualDragStartRotationEuler    = QVector3D(0.0f, 0.0f, 0.0f);
};
