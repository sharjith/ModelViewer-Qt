#pragma once

#include "GltfAnimationData.h"

#include <QString>
#include <QVector>
#include <vector>

// Analytic circle for one topological B-Rep edge (Edge Radius measurement
// tool) - mirrors BRepToAssimpConverter::OccEdgeCircle/AssImpMeshData::
// PrecomputedEdgeCircle, carried as plain data through this same CPU-copy
// hop (see MeshImportAdaptor's class doc comment: retained for clone(),
// MVF serialization, and edge picking). isCircle is false for any other
// curve type (line, spline, ...); centerX/Y/Z and axisX/Y/Z are in the
// mesh's local/model space, same frame as occEdgeSegments() - a caller
// must apply the mesh's current world transform, same convention as
// MeshSurfaceAnchor::worldPosition.
struct OccEdgeCircleInfo
{
    bool   isCircle = false;
    double centerX = 0.0, centerY = 0.0, centerZ = 0.0;
    double axisX = 0.0, axisY = 0.0, axisZ = 1.0;
    double radius = 0.0;
};

// Import provenance for a mesh — source file, scene/material indices, skin joint
// definitions, and per-joint runtime palette.
//
// Intentionally free of GL resources, transform state, and material data.
// Owns the CPU-side data that ties a mesh back to its origin asset; all other
// systems (animation, export, variant switching) query this to correlate a
// live SceneMesh with its source scene entry.
class MeshImportAdaptor
{
public:
    MeshImportAdaptor() = default;

    // ---- Scene / material indices ----------------------------------------
    // Original index into aiScene::mMeshes[] at load time.
    // -1 for meshes not originating from an Assimp scene (parametric shapes).
    void setSceneIndex(int idx)           { _sceneIndex = idx; }
    int  sceneIndex() const               { return _sceneIndex; }

    // Original aiMesh::mMaterialIndex at import time.
    // Used during export to assign the correct material without name matching.
    void setOriginalMaterialIndex(int idx) { _originalMaterialIndex = idx; }
    int  originalMaterialIndex() const    { return _originalMaterialIndex; }

    // ---- Source file / node tracking ------------------------------------
    void    setSourceFile(const QString& path) { _sourceFile = path; }
    QString sourceFile() const                 { return _sourceFile; }

    void    setSourceNodeName(const QString& name) { _sourceNodeName = name; }
    QString sourceNodeName() const                 { return _sourceNodeName; }

    // ---- Skinning — joint definitions (import-time, static) -------------
    // Runtime joint palette lives in MeshAnimationState (Phase 6).
    void setSkinJoints(const QVector<GltfSkinJoint>& joints) { _skinJoints = joints; }
    const QVector<GltfSkinJoint>& skinJoints() const         { return _skinJoints; }
    bool hasSkinning() const                                 { return !_skinJoints.isEmpty(); }

    // ---- Mesh optimization flag ---------------------------------------------
    // When true, meshopt vertex-cache / overdraw / vertex-fetch passes are
    // skipped.  Set for meshes that arrive pre-optimized (e.g. clones).
    void setSkipOptimization(bool skip) { _skipOptimization = skip; }
    bool skipOptimization() const       { return _skipOptimization; }

    // ---- OCC B-Rep edge CPU data (import provenance) ------------------------
    // CPU copies retained for clone(), MVF serialization, and edge picking.
    // The corresponding GL resources (vertex buffer, VAO) live in SceneMesh/MeshVizAdaptor.
    void setOccEdgeData(const std::vector<float>& segments,
                        const std::vector<int>&   boundaries,
                        const std::vector<OccEdgeCircleInfo>& circles = {},
                        double vertexTolerance = 0.0)
        { _occEdgeSegments = segments; _occEdgeBoundaries = boundaries; _occEdgeCircles = circles;
          _occEdgeVertexTolerance = vertexTolerance; }
    const std::vector<float>& occEdgeSegments()   const { return _occEdgeSegments; }
    const std::vector<int>&   occEdgeBoundaries() const { return _occEdgeBoundaries; }
    const std::vector<OccEdgeCircleInfo>& occEdgeCircles() const { return _occEdgeCircles; }
    // Mesh-wide max BRep vertex tolerance - see OccEdgeCircleInfo's doc
    // comment section for how this CPU-copy hop works; used by the Chain
    // Length measurement tool (ViewportWidget::measurementChainEdgeConnects())
    // to decide whether two picked edges are actually connected.
    double occEdgeVertexTolerance() const { return _occEdgeVertexTolerance; }
    bool hasOccEdges() const { return !_occEdgeSegments.empty(); }

private:
    bool    _skipOptimization      = false;
    int     _sceneIndex           = -1;
    int     _originalMaterialIndex = -1;
    QString _sourceFile;
    QString _sourceNodeName;
    QVector<GltfSkinJoint>  _skinJoints;
    // _jointPalette → MeshAnimationState (Phase 6)
    std::vector<float>      _occEdgeSegments;
    std::vector<int>        _occEdgeBoundaries;
    std::vector<OccEdgeCircleInfo> _occEdgeCircles;
    double                   _occEdgeVertexTolerance = 0.0;
};
