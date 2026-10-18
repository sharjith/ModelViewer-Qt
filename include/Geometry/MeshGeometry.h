#pragma once

#include <vector>

// ---------------------------------------------------------------------------
// MeshGeometry
//
// Pure CPU-side geometry asset.  No GL headers, no Qt, no QObject dependency.
// Owns the flat float arrays that describe a triangle mesh on the CPU side:
// positions, normals, texcoords, tangents, bitangents, per-vertex colours,
// and the index buffer.
//
// Subclasses generate specific shapes (Plane, GridMesh, Cone, Sphere, …) or
// add deformable data (DeformableGeometry).
//
// RenderableMesh composes a MeshGeometry* and is responsible for uploading
// these arrays to the GPU and issuing draw calls.
//
// Introduced in Phase 13 (new hierarchy) of the mesh/render/runtime refactor.
// ---------------------------------------------------------------------------
class MeshGeometry
{
public:
    MeshGeometry()  = default;
    virtual ~MeshGeometry() = default;

    // Non-copyable by default; subclasses may provide clone() if needed.
    MeshGeometry(const MeshGeometry&)            = delete;
    MeshGeometry& operator=(const MeshGeometry&) = delete;

    // Primitive topology — mirrors GL constants (GL_TRIANGLES=4, GL_LINES=1,
    // GL_POINTS=0, GL_LINE_STRIP=3, GL_TRIANGLE_STRIP=5).
    // Stored as unsigned int to avoid a GL header dependency here.
    unsigned int primitiveMode() const { return _primitiveMode; }
    void setPrimitiveMode(unsigned int mode) { _primitiveMode = mode; }

    float sMax() const { return _sMax; }
    float tMax() const { return _tMax; }
    bool  hasVertexColors() const { return _hasVertexColors; }

    // Geometry data accessors — used by RenderableMesh::uploadGeometry()
    const std::vector<unsigned int>& indices()    const { return _indices; }
    const std::vector<float>&        points()     const { return _points; }
    const std::vector<float>&        normals()    const { return _normals; }
    const std::vector<float>&        colors()     const { return _colors; }
    const std::vector<float>&        tangents()   const { return _tangents; }
    const std::vector<float>&        bitangents() const { return _bitangents; }
    const std::vector<float>&        texCoords()  const { return _texCoords; }

protected:
    // GL_TRIANGLES = 4; default matches historical SceneMesh behaviour.
    unsigned int _primitiveMode = 4;

    float _sMax = 1.0f;
    float _tMax = 1.0f;
    bool  _hasVertexColors = false;

    std::vector<unsigned int> _indices;
    std::vector<float>        _points;
    std::vector<float>        _normals;
    std::vector<float>        _colors;
    std::vector<float>        _tangents;
    std::vector<float>        _bitangents;
    std::vector<float>        _texCoords;
};
