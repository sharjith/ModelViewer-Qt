#pragma once

#include <QMatrix4x4>
#include <QVector>

// ---------------------------------------------------------------------------
// MeshAnimationState
//
// Runtime animation state for one mesh instance — the parts that change
// every frame (or on every weight/pose update) rather than at import time.
//
// Introduced in Phase 6 of the mesh/render/runtime separation refactor.
//
// Fields:
//   _jointPalette        — per-frame GPU-ready joint transform array for
//                          skinned meshes.  Set by AnimationRuntimeController
//                          and read by SceneMesh::setupUniformsOptimized().
//                          Moved here from MeshImportAdaptor, which retains
//                          the static joint *definitions* (_skinJoints).
//
//   _currentMorphWeights — active blend-shape weights for this frame.
//                          Moved here from SceneMesh where it was a raw
//                          private field.  SceneMesh aliases it by reference
//                          so all existing call sites compile unchanged.
// ---------------------------------------------------------------------------
class MeshAnimationState
{
public:
    MeshAnimationState() = default;

    // ---- Joint palette (skinning) -------------------------------------------
    void setJointPalette(const QVector<QMatrix4x4>& palette) { _jointPalette = palette; }
    const QVector<QMatrix4x4>& jointPalette() const          { return _jointPalette; }

    // ---- Current morph weights (blend shapes) --------------------------------
    void                  setCurrentMorphWeights(const QVector<float>& w) { _currentMorphWeights = w; }
    const QVector<float>& currentMorphWeights()  const { return _currentMorphWeights; }
    QVector<float>&       currentMorphWeights()        { return _currentMorphWeights; }

private:
    QVector<QMatrix4x4> _jointPalette;
    QVector<float>      _currentMorphWeights;
};
