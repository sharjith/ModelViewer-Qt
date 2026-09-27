#pragma once

#include "GltfAnimationData.h"

#include <QRandomGenerator>
#include <QString>
#include <QtGlobal>
#include <QVector>
#include <atomic>
#include <unordered_map>
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

// Analytic axis for one topological B-Rep FACE (Cylindrical/Conical
// Diameter measurement tool) - mirrors BRepToAssimpConverter::OccFaceAxis/
// AssImpMeshData::PrecomputedFaceAxis, same CPU-copy-hop convention as
// OccEdgeCircleInfo above. isCylinder/isCone are mutually exclusive, both
// false for any other surface type; originX/Y/Z and axisX/Y/Z are in the
// mesh's local/model space - a caller must apply the mesh's current world
// transform. No stored radius -
// see OccFaceAxis's doc comment for why.
struct OccFaceAxisInfo
{
    bool   isCylinder = false;
    bool   isCone = false;
    double originX = 0.0, originY = 0.0, originZ = 0.0;
    double axisX = 0.0, axisY = 0.0, axisZ = 1.0;
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

    // ---- OCC B-Rep per-face axis CPU data (import provenance) --------------
    // Same retention convention as setOccEdgeData() above (clone(), MVF
    // serialization, and Cylindrical/Conical Diameter picking). SPARSE
    // parallel arrays: triangleIndices[i]/faceIndices[i] is one (triangle,
    // face) association, only for triangles actually on a cylindrical/
    // conical face (most CAD triangles aren't, so this is far smaller than
    // one entry per mesh triangle). Deliberately NOT a per-topological-
    // face contiguous range table - SceneMesh::optimizeMesh() reorders
    // triangles for GPU cache locality, so a face's triangles are NOT
    // contiguous in the mesh's final index buffer; these indices are
    // always into THIS mesh's own CURRENT triangle order (re-derived via
    // SceneMesh::remapOccFaceTriangleIndicesByPosition() whenever a new
    // SceneMesh instance is built from the same source data - import,
    // clone(), or MVF reload - since each may reorder differently).
    void setOccFaceData(const std::vector<int>& triangleIndices,
                        const std::vector<int>& faceIndices,
                        const std::vector<OccFaceAxisInfo>& axes = {})
        { _occFaceTriangleIndices = triangleIndices; _occFaceIndexPerTriangle = faceIndices; _occFaceAxes = axes; }
    const std::vector<int>& occFaceTriangleIndices() const { return _occFaceTriangleIndices; }
    const std::vector<int>& occFaceIndexPerTriangle() const { return _occFaceIndexPerTriangle; }
    const std::vector<OccFaceAxisInfo>& occFaceAxes() const { return _occFaceAxes; }
    bool hasOccFaces() const { return !_occFaceTriangleIndices.empty(); }

    // ---- Source-mesh provenance (Curvature Analysis cross-body advisory) ---
    // One id per CURRENT vertex (same order as SceneMesh::vertices()), tying
    // it back to which originally-distinct SceneMesh it came from - see
    // project memory project_curvature_edge_welding_provenance_design.md.
    // EMPTY (the default for every freshly imported, cloned-from-a-never-
    // merged-source, or Split-by-Connectivity-fragment mesh) means "this
    // whole mesh is one implicit body" - callers must treat an empty array
    // the same as every entry reading the same value, NOT as "no data"
    // requiring a separate branch (see sourceMeshIdForVertex() below).
    // Populated ONLY by SceneMesh::mergeMeshes() ("Merge Selected", and by
    // extension booleanUnionMeshes()'s fallback to it - a true CGAL boolean
    // union never needs this, since it genuinely fuses its inputs into one
    // continuous solid with no leftover body boundary). Kept in lockstep
    // with SceneMesh::optimizeMesh()'s vertex-fetch reorder (same `remap`
    // permutation applied to both), NOT re-derived by position matching -
    // unlike occFaceTriangleIndices/occFaceIndexPerTriangle above, which are
    // triangle-indexed and need SceneMesh::remapOccFaceTriangleIndicesByPosition()
    // because they're rarely rebuilt in lockstep with a reorder; this one
    // always is, since it is only ever set once, at construction time,
    // before optimizeMesh() runs.
    void setSourceMeshIds(std::vector<quint64> ids) { _sourceMeshIds = std::move(ids); }
    const std::vector<quint64>& sourceMeshIds() const { return _sourceMeshIds; }
    bool hasSourceMeshIds() const { return !_sourceMeshIds.empty(); }

    // Mints a fresh, process-lifetime-unique id (never 0 - that value is
    // reserved as the "no tag / uniform" sentinel an empty sourceMeshIds()
    // implicitly means). Thread-safe; called from mergeMeshes() (a live
    // merge) and remapLocalGroupsToFreshIds() (an MVF reload) - kept atomic
    // defensively since mesh construction is not guaranteed single-threaded
    // everywhere in this codebase.
    //
    // Seeded once per process from QRandomGenerator rather than a fixed 1,
    // so two ids minted in the SAME process (e.g. a live merge right after
    // an MVF file was reloaded, each independently calling this) can never
    // collide with each other via coincidentally retracing the same 1, 2,
    // 3, ... sequence - same reasoning as QUuid. (MVF itself never persists
    // or reloads a raw id value at all - see remapLocalGroupsToFreshIds()'s
    // doc comment for why - so this is a same-process guarantee only, not a
    // cross-session one; a random seed still costs nothing to keep.)
    static quint64 nextSourceMeshId()
    {
        static std::atomic<quint64> counter { []() -> quint64
        {
            const quint64 seed = QRandomGenerator::global()->generate64();
            return seed == 0 ? 1 : seed; // 0 is the reserved "no tag" sentinel
        }() };
        return counter.fetch_add(1, std::memory_order_relaxed);
    }

    // MVF read-side counterpart to the write-side remap
    // MvfSceneBuilder.cpp's mesh-export loop performs on sourceMeshIds()
    // before serializing it: a raw id from nextSourceMeshId() is a random
    // 64-bit value, and a standard JSON number only carries 53 bits of
    // integer precision (IEEE-754 double), so writing it directly would
    // silently collapse two originally-distinct ids that happen to round to
    // the same double - exactly defeating the point. The file therefore
    // never carries a raw id at all: only small, sequential per-mesh LOCAL
    // group numbers (0, 1, 2, ... in order of first appearance), which are
    // exact in a JSON double. This function is the inverse - given such a
    // small-number array read back from a file, it mints one FRESH,
    // process-local id per distinct group number and returns the expanded
    // array, so equality/inequality among the file's original groups is
    // preserved even though none of the actual numeric values survive the
    // round trip (they were never meaningful outside a same-process
    // comparison anyway - see nextSourceMeshId()'s own doc comment).
    // Returns an empty vector unchanged (nothing to remap).
    static std::vector<quint64> remapLocalGroupsToFreshIds(const std::vector<quint64>& localGroups)
    {
        if (localGroups.empty())
            return {};
        std::unordered_map<quint64, quint64> freshIdByGroup;
        freshIdByGroup.reserve(localGroups.size());
        std::vector<quint64> result;
        result.reserve(localGroups.size());
        for (quint64 group : localGroups)
        {
            auto it = freshIdByGroup.find(group);
            if (it == freshIdByGroup.end())
                it = freshIdByGroup.emplace(group, nextSourceMeshId()).first;
            result.push_back(it->second);
        }
        return result;
    }

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
    std::vector<int>              _occFaceTriangleIndices;
    std::vector<int>              _occFaceIndexPerTriangle;
    std::vector<OccFaceAxisInfo>  _occFaceAxes;
    std::vector<quint64>          _sourceMeshIds;
};
