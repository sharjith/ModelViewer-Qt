#pragma once

#include "MeshGeometry.h"
#include "MeshVertex.h"

#include <QVector>

// ---------------------------------------------------------------------------
// DeformableGeometry
//
// Extends MeshGeometry with the additional CPU-side data required for
// skeletal skinning and morph-target (blend-shape) animation:
//
//   _jointIndices / _jointWeights   — per-vertex skinning data (flat float
//                                     arrays, 4 values per vertex)
//   _vertices / _baseVertices       — interleaved Vertex array used by the
//                                     AssImpMeshBuilder path; _baseVertices is
//                                     the unmodified snapshot used as the
//                                     morph-delta base, _vertices is the
//                                     working copy blended each frame
//   _morphTargets                   — per-target position/normal/tangent
//                                     deltas (static after load)
//   _defaultMorphWeights            — blend weights matching each target
//
// SceneMesh composes a DeformableGeometry and exposes the morph/skin
// query interface polymorphically.
//
// Introduced in Phase 13 (new hierarchy) of the mesh/render/runtime refactor.
// ---------------------------------------------------------------------------
class DeformableGeometry : public MeshGeometry
{
public:
    DeformableGeometry()  = default;
    ~DeformableGeometry() override = default;

    bool hasMorphTargets() const { return !_morphTargets.isEmpty(); }
    const QVector<MorphTargetData>& morphTargets()        const { return _morphTargets; }
    const QVector<float>&           defaultMorphWeights() const { return _defaultMorphWeights; }

    void setMorphTargets(const QVector<MorphTargetData>& targets,
                         const QVector<float>&           defaultWeights)
    {
        _morphTargets        = targets;
        _defaultMorphWeights = defaultWeights;
    }

protected:
    // ---- Skinning geometry (static after load) ----------------------------
    std::vector<float> _jointIndices;   // 4 floats per vertex
    std::vector<float> _jointWeights;   // 4 floats per vertex

    // ---- Interleaved CPU geometry (AssImpMeshBuilder path) ---------------
    // _vertices is the working copy modified by morph-weight blending.
    // _baseVertices is the unmodified snapshot used as the morph-delta base.
    std::vector<Vertex> _vertices;
    std::vector<Vertex> _baseVertices;

    // ---- Morph-target data (static after load) ----------------------------
    QVector<MorphTargetData> _morphTargets;
    QVector<float>           _defaultMorphWeights;
};
