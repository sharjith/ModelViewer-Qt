#pragma once

#include "FastGlObjects.h"
#include <QHash>
#include <QByteArray>
#include <QImage>
#include <QMap>
#include <QVariant>

#include <vector>

// Forward declaration — only a pointer is stored, no full definition needed here.
class QOpenGLShaderProgram;

// One entry in the precomputed texture binding table built by
// SceneMesh::cacheTextureBindings().  Cached once per material change;
// consumed every frame by bindTexturesOptimized() without re-scanning _textures.
struct PrecomputedTexture
{
    unsigned int textureId;
    unsigned int textureUnit;
    int          uniformLocation;
    bool         isValid;
};

// GL resource container for a SceneMesh.
//
// Owns all per-mesh OpenGL state: vertex/index buffers, VAO, fallback texture,
// feature-edge and OCC-edge VAO/VBO pairs, the precomputed texture binding
// table, per-frame dirty flags, the uniform-location cache, and debug override
// maps.
//
// SceneMesh embeds one of these as _vizState and exposes each field through
// a reference alias (same zero-churn pattern used for Material& _material).
// All existing call sites in SceneMesh.cpp, FloorPlane.cpp, and ViewCubeMesh.cpp
// continue to compile unchanged.
//
// Intentionally free of geometry, material, transform, and import provenance.
// The draw methods and shader-program knowledge live in SceneMesh/RenderableMesh.
class MeshVizAdaptor
{
public:
    MeshVizAdaptor() = default;

    // MeshVizAdaptor is non-copyable and non-movable because it contains
    // QObject-derived members (FastGlVertexArray) and FastGlBuffer
    // objects whose ownership must not be transferred implicitly.
    MeshVizAdaptor(const MeshVizAdaptor&)            = delete;
    MeshVizAdaptor& operator=(const MeshVizAdaptor&) = delete;
    MeshVizAdaptor(MeshVizAdaptor&&)                 = delete;
    MeshVizAdaptor& operator=(MeshVizAdaptor&&)      = delete;

    // ---- Vertex / index buffers -----------------------------------------
    FastGlBuffer& indexBuffer()       { return _indexBuffer; }
    FastGlBuffer& positionBuffer()    { return _positionBuffer; }
    FastGlBuffer& normalBuffer()      { return _normalBuffer; }
    FastGlBuffer& colorBuffer()       { return _colorBuffer; }
    FastGlBuffer& texCoord0Buffer()   { return _texCoord0Buffer; }
    FastGlBuffer& texCoord1Buffer()   { return _texCoord1Buffer; }
    FastGlBuffer& texCoord2Buffer()   { return _texCoord2Buffer; }
    FastGlBuffer& texCoord3Buffer()   { return _texCoord3Buffer; }
    FastGlBuffer& tangentBuffer()     { return _tangentBuf; }
    FastGlBuffer& bitangentBuffer()   { return _bitangentBuf; }
    FastGlBuffer& jointIndexBuffer()  { return _jointIndexBuffer; }
    FastGlBuffer& jointWeightBuffer() { return _jointWeightBuffer; }
    FastGlBuffer& coordBuffer()       { return _coordBuf; }

    // ---- VAO & buffer ownership list ------------------------------------
    FastGlVertexArray& vao()          { return _vertexArrayObject; }
    std::vector<FastGlBuffer>& buffers()    { return _buffers; }

    // ---- Vertex count ---------------------------------------------------
    unsigned int& nVerts() { return _nVerts; }

    // ---- Fallback texture -----------------------------------------------
    // A 1×1 placeholder always bound on unit 0 so the shader never samples
    // an unbound unit.  Some render paths upload image data when dirty.
    unsigned int& fallbackTexture()       { return _fallbackTexture; }
    QImage& fallbackTextureImage()        { return _fallbackTextureImage; }
    QImage& fallbackTextureBuffer()       { return _fallbackTextureBuffer; }

    // ---- Per-frame dirty flags ------------------------------------------
    bool& textureBindingsDirty() { return _textureBindingsDirty; }
    bool& uniformsDirty()        { return _uniformsDirty; }

    // ---- Uniform location cache -----------------------------------------
    // Accessed from const SceneMesh methods via the reference alias;
    // references are transparent to const so no mutable annotation is needed.
    QHash<QByteArray, int>& uniformLocationCache()  { return _uniformLocationCache; }
    QOpenGLShaderProgram*&  vaoConfiguredProgram()  { return _vaoConfiguredProgram; }

    // ---- Debug override maps --------------------------------------------
    QMap<int, unsigned int>&    debugTextureOverrides()  { return _debugTextureOverrides; }
    QMap<QString, QVariant>&    debugUniformOverrides()  { return _debugUniformOverrides; }

    // ---- Feature-edge VAO/VBO (heuristic silhouette edges) --------------
    FastGlBuffer&              featureEdgeIndexBuffer() { return _featureEdgeIndexBuffer; }
    FastGlVertexArray&   featureEdgeVAO()         { return _featureEdgeVAO; }
    int&                        featureEdgeCount()       { return _featureEdgeCount; }

    // ---- OCC B-Rep edge VAO/VBO (STEP/IGES/BREP exact topology) ---------
    FastGlBuffer&              occEdgeVertexBuffer()    { return _occEdgeVertexBuffer; }
    FastGlVertexArray&   occEdgeVAO()             { return _occEdgeVAO; }
    int&                        occEdgeCount()           { return _occEdgeCount; }

    // ---- Precomputed texture binding table ------------------------------
    // Built once by cacheTextureBindings(); consumed by bindTexturesOptimized().
    std::vector<PrecomputedTexture>& textureBindings()   { return _textureBindings; }

    // ---- Uniform-state signature cache ----------------------------------
    // Accessed from const methods (uniformStateSignature(), getRenderMaterialSortKey())
    // so the fields are mutable and the accessors are const-qualified, matching
    // the same pattern used for _uniformLocationCache above.
    bool&    uniformStateSignatureDirty()    const { return _uniformStateSignatureDirty; }
    quint64& cachedUniformStateSignature()   const { return _cachedUniformStateSignature; }

private:
    FastGlBuffer _indexBuffer;
    FastGlBuffer _positionBuffer;
    FastGlBuffer _normalBuffer;
    FastGlBuffer _colorBuffer;
    FastGlBuffer _texCoord0Buffer;
    FastGlBuffer _texCoord1Buffer;
    FastGlBuffer _texCoord2Buffer;
    FastGlBuffer _texCoord3Buffer;
    FastGlBuffer _tangentBuf;
    FastGlBuffer _bitangentBuf;
    FastGlBuffer _jointIndexBuffer;
    FastGlBuffer _jointWeightBuffer;
    FastGlBuffer _coordBuf;

    FastGlVertexArray   _vertexArrayObject;
    std::vector<FastGlBuffer> _buffers;
    unsigned int               _nVerts = 0;

    QImage       _fallbackTextureImage;
    QImage       _fallbackTextureBuffer;
    unsigned int _fallbackTexture = 0;

    bool _textureBindingsDirty = true;
    bool _uniformsDirty        = true;

    QHash<QByteArray, int>  _uniformLocationCache;
    QOpenGLShaderProgram*   _vaoConfiguredProgram = nullptr;

    QMap<int, unsigned int>  _debugTextureOverrides;
    QMap<QString, QVariant>  _debugUniformOverrides;

    FastGlBuffer            _featureEdgeIndexBuffer { FastGlBuffer::IndexBuffer };
    FastGlVertexArray _featureEdgeVAO;
    int                      _featureEdgeCount = 0;

    FastGlBuffer            _occEdgeVertexBuffer { FastGlBuffer::VertexBuffer };
    FastGlVertexArray _occEdgeVAO;
    int                      _occEdgeCount = 0;

    std::vector<PrecomputedTexture> _textureBindings;

    mutable bool    _uniformStateSignatureDirty  = true;
    mutable quint64 _cachedUniformStateSignature = 0;
};
