#include "TriangleMesh.h"
#include "TriangleMollerTrumbore.h"
#include "TriangleBaldwinWeber.h"
#include "Point.h"

#include <algorithm>
#include <iostream>

TriangleMesh::TriangleMesh(QOpenGLShaderProgram* prog, const QString name) : Drawable(prog),
    _texture(0),
    _diffuseADSMap(0),
    _specularADSMap(0),
    _emissiveADSMap(0),
    _normalADSMap(0),
    _heightADSMap(0),
    _opacityADSMap(0),
    _hasTexture(false),
    _hasDiffuseADSMap(false),
    _hasSpecularADSMap(false),
    _hasEmissiveADSMap(false),
    _hasNormalADSMap(false),
    _hasHeightADSMap(false),
    _hasOpacityADSMap(false),
    _opacityADSMapInverted(false),
    _sMax(1),
    _tMax(1),
    _albedoPBRMap(0),
    _metallicPBRMap(0),
    _roughnessPBRMap(0),
    _normalPBRMap(0),
    _aoPBRMap(0),
    _opacityPBRMap(0),
    _hasAlbedoPBRMap(false),
    _hasMetallicPBRMap(false),
    _hasRoughnessPBRMap(false),
    _hasNormalPBRMap(false),
    _hasAOPBRMap(false),
    _hasHeightPBRMap(false),
    _heightPBRMapScale(0.05f),
    _hasOpacityPBRMap(false),
    _opacityPBRMapInverted(false)
{
    setAutoIncrName(name);
    _memorySize = 0;
    _transX = _transY = _transZ = 0.0f;
    _rotateX = _rotateY = _rotateZ = 0.0f;
    _scaleX = _scaleY = _scaleZ = 1.0f;
    _transformation.setToIdentity();

    _indexBuffer = QOpenGLBuffer(QOpenGLBuffer::IndexBuffer);
    _positionBuffer = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    _normalBuffer = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    _texCoordBuffer = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    _tangentBuf = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    _bitangentBuf = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);

    _indexBuffer.create();
    _positionBuffer.create();
    _normalBuffer.create();
    _texCoordBuffer.create();
    _tangentBuf.create();
    _bitangentBuf.create();

    _vertexArrayObject.create();

    if (!_texBuffer.load("textures/opengllogo.png"))
    { // Load first image from file
        qWarning("Could not read image file, using single-color instead.");
        QImage dummy(128, 128, static_cast<QImage::Format>(5));
        dummy.fill(Qt::white);
        _texBuffer = dummy;
    }
    _texImage = QGLWidget::convertToGLFormat(_texBuffer); // flipped 32bit RGBA

    glGenTextures(1, &_texture);
    //std::cout << "TriangleMesh::TriangleMesh : _texture = " << _texture << std::endl;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
}

void TriangleMesh::initBuffers(
        std::vector<unsigned int>* indices,
        std::vector<float>* points,
        std::vector<float>* normals,
        std::vector<float>* texCoords,
        std::vector<float>* tangents,
        std::vector<float>* bitangents)
{
    // Must have data for indices, points, and normals
    if (indices == nullptr || points == nullptr || normals == nullptr)
        return;

    _indices = *indices;
    _points = *points;
    _trsfpoints = _points;
    _normals = *normals;

    // build the triangles for selection
    buildTriangles();

    if (texCoords)
        _texCoords = *texCoords;
    if (tangents)
        _tangents = *tangents;
    if (bitangents)
        _bitangents = *bitangents;

    _memorySize = 0;
    _memorySize = (_points.size() + _normals.size() + _indices.size()) * sizeof(float);

    _nVerts = (unsigned int)indices->size();

    _buffers.push_back(_indexBuffer);
    _indexBuffer.bind();
    _indexBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    _indexBuffer.allocate(indices->data(), static_cast<int>(indices->size() * sizeof(unsigned int)));

    _buffers.push_back(_positionBuffer);
    _positionBuffer.bind();
    _positionBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    _positionBuffer.allocate(points->data(), static_cast<int>(points->size() * sizeof(float)));

    _buffers.push_back(_normalBuffer);
    _normalBuffer.bind();
    _normalBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    _normalBuffer.allocate(normals->data(), static_cast<int>(normals->size() * sizeof(float)));

    if (_texCoords.size())
    {
        _buffers.push_back(_texCoordBuffer);
        _texCoordBuffer.bind();
        _texCoordBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
        _texCoordBuffer.allocate(_texCoords.data(), static_cast<int>(_texCoords.size() * sizeof(float)));
        _memorySize += _texCoords.size() * sizeof(float);
    }

    if (_tangents.size())
    {
        _buffers.push_back(_tangentBuf);
        _tangentBuf.bind();
        _tangentBuf.setUsagePattern(QOpenGLBuffer::StaticDraw);
        _tangentBuf.allocate(_tangents.data(), static_cast<int>(_tangents.size() * sizeof(float)));
        _memorySize += _tangents.size() * sizeof(float);
    }

    if (_bitangents.size())
    {
        _buffers.push_back(_bitangentBuf);
        _bitangentBuf.bind();
        _bitangentBuf.setUsagePattern(QOpenGLBuffer::StaticDraw);
        _bitangentBuf.allocate(_bitangents.data(), static_cast<int>(_bitangents.size() * sizeof(float)));
        _memorySize += _bitangents.size() * sizeof(float);
    }

    _vertexArrayObject.bind();

    _indexBuffer.bind();

    // _position
    _positionBuffer.bind();
    //glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
    //glEnableVertexAttribArray(0);  // Vertex position
    _prog->enableAttributeArray("vertexPosition");
    _prog->setAttributeBuffer("vertexPosition", GL_FLOAT, 0, 3);

    // Normal
    _normalBuffer.bind();
    //glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, 0);
    //glEnableVertexAttribArray(1);  // Normal
    _prog->enableAttributeArray("vertexNormal");
    _prog->setAttributeBuffer("vertexNormal", GL_FLOAT, 0, 3);

    // Tex coords
    if (_texCoords.size())
    {
        _texCoordBuffer.bind();
        //glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, 0);
        //glEnableVertexAttribArray(2);  // Tex coord
        _prog->enableAttributeArray("texCoord2d");
        _prog->setAttributeBuffer("texCoord2d", GL_FLOAT, 0, 2);
    }

    if (_tangents.size())
    {
        _tangentBuf.bind();
        //glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 0, 0);
        //glEnableVertexAttribArray(3);  // Tangents
        _prog->enableAttributeArray("vertexTangent");
        _prog->setAttributeBuffer("vertexTangent", GL_FLOAT, 0, 3);
    }

    if (_bitangents.size())
    {
        _bitangentBuf.bind();
        _prog->enableAttributeArray("vertexBitangent");
        _prog->setAttributeBuffer("vertexBitangent", GL_FLOAT, 0, 3);
    }

    _vertexArrayObject.release();
}

void TriangleMesh::buildTriangles()
{
    if (_triangles.size())
    {
        for (Triangle* t : _triangles)
            delete t;
        _triangles.clear();
    }
    try {
        size_t offset = 3; // each index points to 3 floats
        for (size_t i = 0; i < _indices.size();)
        {
            // Vertex 1
            QVector3D v1(_trsfpoints.at(offset * _indices.at(i) + 0), // x coordinate
                _trsfpoints.at(offset * _indices.at(i) + 1),          // y coordinate
                _trsfpoints.at(offset * _indices.at(i) + 2));         // z coordinate
            i++;

            // Vertex 2
            QVector3D v2(_trsfpoints.at(offset * _indices.at(i) + 0), // x coordinate
                _trsfpoints.at(offset * _indices.at(i) + 1),          // y coordinate
                _trsfpoints.at(offset * _indices.at(i) + 2));         // z coordinate
            i++;

            // Vertex 3
            QVector3D v3(_trsfpoints[offset * _indices.at(i) + 0], // x coordinate
                _trsfpoints.at(offset * _indices.at(i) + 1),          // y coordinate
                _trsfpoints.at(offset * _indices.at(i) + 2));         // z coordinate
            i++;

            _triangles.push_back(new TriangleMollerTrumbore(v1, v2, v3, this));
        }
    }
    catch (const std::exception& ex) {
        std::cout << "Exception raised in TriangleMesh::buildTriangles\n" << ex.what() << std::endl;
    }
}

void TriangleMesh::setProg(QOpenGLShaderProgram* prog)
{
    _prog = prog;

    _vertexArrayObject.bind();

    //_indexBuffer.bind();

    // _position
    _positionBuffer.bind();
    //glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
    //glEnableVertexAttribArray(0);  // Vertex position
    _prog->enableAttributeArray("vertexPosition");
    _prog->setAttributeBuffer("vertexPosition", GL_FLOAT, 0, 3);

    // Normal
    _normalBuffer.bind();
    //glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, 0);
    //glEnableVertexAttribArray(1);  // Normal
    _prog->enableAttributeArray("vertexNormal");
    _prog->setAttributeBuffer("vertexNormal", GL_FLOAT, 0, 3);

    if (_texCoords.size())
    {
        _texCoordBuffer.bind();
        //glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, 0);
        //glEnableVertexAttribArray(2);  // Tex coord
        _prog->enableAttributeArray("texCoord2d");
        _prog->setAttributeBuffer("texCoord2d", GL_FLOAT, 0, 2);
    }

    if (_tangents.size())
    {
        _tangentBuf.bind();
        //glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 0, 0);
        //glEnableVertexAttribArray(3);  // Tangents
        _prog->enableAttributeArray("vertexTangent");
        _prog->setAttributeBuffer("vertexTangent", GL_FLOAT, 0, 3);
    }

    if (_bitangents.size())
    {
        _bitangentBuf.bind();
        _prog->enableAttributeArray("vertexBitangent");
        _prog->setAttributeBuffer("vertexBitangent", GL_FLOAT, 0, 3);
    }

    _vertexArrayObject.release();
}

void TriangleMesh::setupTextures()
{
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _texImage.width(), _texImage.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, _texImage.bits());
    glGenerateMipmap(GL_TEXTURE_2D);

    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_2D, _diffuseADSMap);
    glActiveTexture(GL_TEXTURE11);
    glBindTexture(GL_TEXTURE_2D, _specularADSMap);
    glActiveTexture(GL_TEXTURE12);
    glBindTexture(GL_TEXTURE_2D, _emissiveADSMap);
    glActiveTexture(GL_TEXTURE13);
    glBindTexture(GL_TEXTURE_2D, _normalADSMap);
    glActiveTexture(GL_TEXTURE14);
    glBindTexture(GL_TEXTURE_2D, _heightADSMap);
    glActiveTexture(GL_TEXTURE15);
    glBindTexture(GL_TEXTURE_2D, _opacityADSMap);

    glActiveTexture(GL_TEXTURE20);
    glBindTexture(GL_TEXTURE_2D, _albedoPBRMap);
    glActiveTexture(GL_TEXTURE21);
    glBindTexture(GL_TEXTURE_2D, _normalPBRMap);
    glActiveTexture(GL_TEXTURE22);
    glBindTexture(GL_TEXTURE_2D, _metallicPBRMap);
    glActiveTexture(GL_TEXTURE23);
    glBindTexture(GL_TEXTURE_2D, _roughnessPBRMap);
    glActiveTexture(GL_TEXTURE24);
    glBindTexture(GL_TEXTURE_2D, _aoPBRMap);
    glActiveTexture(GL_TEXTURE25);
    glBindTexture(GL_TEXTURE_2D, _heightPBRMap);
    glActiveTexture(GL_TEXTURE26);
    glBindTexture(GL_TEXTURE_2D, _opacityPBRMap);
}

void TriangleMesh::setupUniforms()
{
    _prog->bind();
    _prog->setUniformValue("texEnabled", _hasTexture);
    _prog->setUniformValue("texUnit", 0);
    _prog->setUniformValue("material.ambient", _material.ambient());
    _prog->setUniformValue("material.diffuse", _material.diffuse());
    _prog->setUniformValue("material.specular", _material.specular());
    _prog->setUniformValue("material.emission", _material.emissive());
    _prog->setUniformValue("material.shininess", _material.shininess());
    _prog->setUniformValue("material.metallic", _material.metallic());
    _prog->setUniformValue("opacity", _material.opacity());
    // ADS light texture maps
    _prog->setUniformValue("hasDiffuseTexture", _hasDiffuseADSMap);
    _prog->setUniformValue("hasSpecularTexture", _hasSpecularADSMap);
    _prog->setUniformValue("hasEmissiveTexture", _hasEmissiveADSMap);
    _prog->setUniformValue("hasNormalTexture", _hasNormalADSMap);
    _prog->setUniformValue("hasHeightTexture", _hasHeightADSMap);
    _prog->setUniformValue("hasOpacityTexture", _hasOpacityADSMap);
    _prog->setUniformValue("opacityTextureInverted", _opacityADSMapInverted);

    _prog->setUniformValue("texture_diffuse", 10);
    _prog->setUniformValue("texture_specular", 11);
    _prog->setUniformValue("texture_emissive", 12);
    _prog->setUniformValue("texture_normal", 13);
    _prog->setUniformValue("texture_height", 14);
    _prog->setUniformValue("texture_opacity", 15);
    // PBR Direct Lighting
    _prog->setUniformValue("pbrLighting.albedo", _material.albedoColor());
    _prog->setUniformValue("pbrLighting.metallic", _material.metalness());
    _prog->setUniformValue("pbrLighting.roughness", _material.roughness());
    _prog->setUniformValue("pbrLighting.ambientOcclusion", 1.0f);
    // PBR Texture Maps
    _prog->setUniformValue("albedoMap", 20);
    _prog->setUniformValue("normalMap", 21);
    _prog->setUniformValue("metallicMap", 22);
    _prog->setUniformValue("roughnessMap", 23);
    _prog->setUniformValue("aoMap", 24);
    _prog->setUniformValue("heightMap", 25);
    _prog->setUniformValue("opacityMap", 26);
    _prog->setUniformValue("heightScale", _heightPBRMapScale);
    _prog->setUniformValue("hasAlbedoMap", _hasAlbedoPBRMap);
    _prog->setUniformValue("hasMetallicMap", _hasMetallicPBRMap);
    _prog->setUniformValue("hasRoughnessMap", _hasRoughnessPBRMap);
    _prog->setUniformValue("hasNormalMap", _hasNormalPBRMap);
    _prog->setUniformValue("hasAOMap", _hasAOPBRMap);
    _prog->setUniformValue("hasOpacityMap", _hasOpacityPBRMap);
    _prog->setUniformValue("opacityMapInverted", _opacityPBRMapInverted);
    _prog->setUniformValue("hasHeightMap", _hasHeightPBRMap);

    _prog->setUniformValue("selected", _selected);
}

void TriangleMesh::enableOpacityADSMap(bool enable)
{
    _hasOpacityADSMap = enable;
}

void TriangleMesh::invertOpacityADSMap(bool invert)
{
    _opacityADSMapInverted = invert;
}

void TriangleMesh::setOpacityADSMap(unsigned int opacityTex)
{
    glDeleteTextures(1, &_opacityADSMap);
    _opacityADSMap = opacityTex;
    _hasOpacityADSMap = true;
}

void TriangleMesh::enableHeightADSMap(bool enable)
{
    _hasHeightADSMap = enable;
}

void TriangleMesh::setHeightADSMap(unsigned int heightTex)
{
    glDeleteTextures(1, &_heightADSMap);
    _heightADSMap = heightTex;
    _hasHeightADSMap = true;
}

void TriangleMesh::enableNormalADSMap(bool enable)
{
    _hasNormalADSMap = enable;
}

void TriangleMesh::setNormalADSMap(unsigned int normalTex)
{
    glDeleteTextures(1, &_normalADSMap);
    _normalADSMap = normalTex;
    _hasNormalADSMap = true;
}

void TriangleMesh::enableSpecularADSMap(bool enable)
{
    _hasSpecularADSMap = enable;
}

void TriangleMesh::setSpecularADSMap(unsigned int specularTex)
{
    glDeleteTextures(1, &_specularADSMap);
    _specularADSMap = specularTex;
    _hasSpecularADSMap = true;
}

void TriangleMesh::enableEmissiveADSMap(bool enable)
{
    _hasEmissiveADSMap = enable;
}

void TriangleMesh::setEmissiveADSMap(unsigned int emissiveTex)
{
    glDeleteTextures(1, &_emissiveADSMap);
    _emissiveADSMap = emissiveTex;
    _hasEmissiveADSMap = true;
}

void TriangleMesh::enableDiffuseADSMap(bool enable)
{
    _hasDiffuseADSMap = enable;
}

void TriangleMesh::setDiffuseADSMap(unsigned int diffuseTex)
{
    glDeleteTextures(1, &_diffuseADSMap);
    _diffuseADSMap = diffuseTex;
    _hasDiffuseADSMap = true;
}

void TriangleMesh::clearDiffuseADSMap()
{
    glDeleteTextures(1, &_diffuseADSMap);
    _diffuseADSMap = 0;
}

void TriangleMesh::clearSpecularADSMap()
{
    glDeleteTextures(1, &_specularADSMap);
    _specularADSMap = 0;
}

void TriangleMesh::clearEmissiveADSMap()
{
    glDeleteTextures(1, &_emissiveADSMap);
    _emissiveADSMap = 0;
}

void TriangleMesh::clearNormalADSMap()
{
    glDeleteTextures(1, &_normalADSMap);
    _normalADSMap = 0;
}

void TriangleMesh::clearHeightADSMap()
{
    glDeleteTextures(1, &_heightADSMap);
    _heightADSMap = 0;
}

void TriangleMesh::clearOpacityADSMap()
{
    glDeleteTextures(1, &_opacityADSMap);
    _opacityADSMap = 0;
}

void TriangleMesh::clearAllADSMaps()
{
    glDeleteTextures(1, &_diffuseADSMap);
    _diffuseADSMap = 0;
    glDeleteTextures(1, &_specularADSMap);
    _specularADSMap = 0;
    glDeleteTextures(1, &_emissiveADSMap);
    _emissiveADSMap = 0;
    glDeleteTextures(1, &_normalADSMap);
    _normalADSMap = 0;
    glDeleteTextures(1, &_heightADSMap);
    _heightADSMap = 0;
}

GLMaterial TriangleMesh::getMaterial() const
{
    return _material;
}

void TriangleMesh::setMaterial(const GLMaterial& material)
{
    _material = material;
}

void TriangleMesh::render()
{
    if (!_vertexArrayObject.isCreated())
        return;

    setupTextures();

    setupUniforms();

    if (_material.opacity() < 1.0f || _hasOpacityADSMap || _hasOpacityPBRMap)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_LINE_SMOOTH);
        glEnable(GL_POLYGON_SMOOTH);
    }
    else
    {
        glDisable(GL_BLEND);
    }

    // Handle lighting normal for negative scaling
    if ((_scaleX < 0 && _scaleY > 0 && _scaleZ > 0) ||
            (_scaleX > 0 && _scaleY < 0 && _scaleZ > 0) ||
            (_scaleX > 0 && _scaleY > 0 && _scaleZ < 0) ||
            (_scaleX < 0 && _scaleY < 0 && _scaleZ < 0))
    {
        glFrontFace(GL_CW);
    }
    else
    {
        glFrontFace(GL_CCW);
    }
    _vertexArrayObject.bind();
    glDrawElements(GL_TRIANGLES, _nVerts, GL_UNSIGNED_INT, 0);
    _vertexArrayObject.release();
    _prog->release();

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
}

void TriangleMesh::deleteTextures()
{
    //std::cout << "TriangleMesh::deleteTextures : _texture = " << _texture << std::endl;

    glDeleteTextures(1, &_texture);
    glDeleteTextures(1, &_diffuseADSMap);
    glDeleteTextures(1, &_specularADSMap);
    glDeleteTextures(1, &_emissiveADSMap);
    glDeleteTextures(1, &_normalADSMap);
    glDeleteTextures(1, &_heightADSMap);
    glDeleteTextures(1, &_opacityADSMap);
    glDeleteTextures(1, &_albedoPBRMap);
    glDeleteTextures(1, &_metallicPBRMap);
    glDeleteTextures(1, &_roughnessPBRMap);
    glDeleteTextures(1, &_normalPBRMap);
    glDeleteTextures(1, &_aoPBRMap);
    glDeleteTextures(1, &_heightPBRMap);
    glDeleteTextures(1, &_opacityPBRMap);
}

TriangleMesh::~TriangleMesh()
{
    deleteBuffers();
    deleteTextures();
    for (Triangle* t : _triangles)
        delete t;
}

void TriangleMesh::deleteBuffers()
{
    if (_buffers.size() > 0)
    {
        for (QOpenGLBuffer& buff : _buffers)
        {
            buff.destroy();
        }
        _buffers.clear();
    }

    if (_vertexArrayObject.isCreated())
    {
        _vertexArrayObject.destroy();
    }
}

void TriangleMesh::computeBounds()
{
    // Ritter's algorithm
    std::vector<QVector3D> aPoints;
    for (size_t i = 0; i < _trsfpoints.size(); i += 3)
    {
        aPoints.push_back(QVector3D(_trsfpoints[i], _trsfpoints[i + 1], _trsfpoints[i + 2]));
    }
    QVector3D xmin, xmax, ymin, ymax, zmin, zmax;
    xmin = ymin = zmin = QVector3D(1, 1, 1) * INFINITY;
    xmax = ymax = zmax = QVector3D(1, 1, 1) * -INFINITY;
    for (auto p : aPoints)
    {
        if (p.x() < xmin.x())
            xmin = p;
        if (p.x() > xmax.x())
            xmax = p;
        if (p.y() < ymin.y())
            ymin = p;
        if (p.y() > ymax.y())
            ymax = p;
        if (p.z() < zmin.z())
            zmin = p;
        if (p.z() > zmax.z())
            zmax = p;
    }
    auto xSpan = (xmax - xmin).lengthSquared();
    auto ySpan = (ymax - ymin).lengthSquared();
    auto zSpan = (zmax - zmin).lengthSquared();
    auto dia1 = xmin;
    auto dia2 = xmax;
    auto maxSpan = xSpan;
    if (ySpan > maxSpan)
    {
        maxSpan = ySpan;
        dia1 = ymin;
        dia2 = ymax;
    }
    if (zSpan > maxSpan)
    {
        dia1 = zmin;
        dia2 = zmax;
    }
    auto center = (dia1 + dia2) * 0.5f;
    auto sqRad = (dia2 - center).lengthSquared();
    auto radius = sqrt(sqRad);
    for (auto p : aPoints)
    {
        float d = (p - center).lengthSquared();
        if (d > sqRad)
        {
            auto r = sqrt(d);
            radius = (radius + r) * 0.5f;
            sqRad = radius * radius;
            auto offset = r - radius;
            center = (radius * center + offset * p) / r;
        }
    }

    _boundingSphere.setCenter(center);
    _boundingSphere.setRadius(radius);

    QList<float> xVals, yVals, zVals;
    for (size_t i = 0; i < _trsfpoints.size(); i += 3)
    {
        xVals.push_back(_trsfpoints.at(i));
        yVals.push_back(_trsfpoints.at(i + 1));
        zVals.push_back(_trsfpoints.at(i + 2));
    }
    std::sort(xVals.begin(), xVals.end(), std::less<float>());
    std::sort(yVals.begin(), yVals.end(), std::less<float>());
    std::sort(zVals.begin(), zVals.end(), std::less<float>());
    _boundingBox.setLimits(xVals.first(), xVals.last(),
                           yVals.first(), yVals.last(),
                           zVals.first(), zVals.last());
    Point cen = _boundingBox.center();
}

float TriangleMesh::getHighestXValue() const
{
    return _boundingBox.xMax();
}

float TriangleMesh::getLowestXValue() const
{
    return _boundingBox.xMin();
}

float TriangleMesh::getHighestYValue() const
{
    return _boundingBox.yMax();
}

float TriangleMesh::getLowestYValue() const
{
    return _boundingBox.yMin();
}

float TriangleMesh::getHighestZValue() const
{
    return _boundingBox.zMax();
}

float TriangleMesh::getLowestZValue() const
{
    return _boundingBox.zMin();
}

QRect TriangleMesh::projectedRect(const QMatrix4x4& modelView, const QMatrix4x4& projection, const QRect& viewport, const QRect& window) const
{
    QList<float> xVals;
    QList<float> yVals;
    for (size_t i = 0; i < _trsfpoints.size(); i += 3)
    {
        QVector3D point(_trsfpoints.at(i + 0), _trsfpoints.at(i + 1), _trsfpoints.at(i + 2));
        QVector3D projPoint = point.project(modelView, projection, viewport);
        xVals.push_back(projPoint.x());
        yVals.push_back(projPoint.y());
    }
    std::sort(xVals.begin(), xVals.end(), std::less<float>());
    std::sort(yVals.begin(), yVals.end(), std::less<float>());
    QRect rect(xVals.first(), (window.height() - yVals.last()), (xVals.last() - xVals.first()), (yVals.last() - yVals.first()));

    return rect;
}

std::vector<float> TriangleMesh::getNormals() const
{
    return _normals;
}

std::vector<float> TriangleMesh::getTexCoords() const
{
    return _texCoords;
}

std::vector<float> TriangleMesh::getTrsfPoints() const
{
    return _trsfpoints;
}

void TriangleMesh::resetTransformations()
{
    _transX = _transY = _transZ = 0.0f;
    _rotateX = _rotateY = _rotateZ = 0.0f;
    _scaleX = _scaleY = _scaleZ = 1.0f;

    _transformation.setToIdentity();

    _trsfpoints.clear();
    _trsfnormals.clear();

    _trsfpoints = _points;
    _trsfnormals = _normals;

    _prog->bind();
    _positionBuffer.bind();
    _positionBuffer.allocate(_points.data(), static_cast<int>(_points.size() * sizeof(float)));
    _prog->enableAttributeArray("vertexPosition");
    _prog->setAttributeBuffer("vertexPosition", GL_FLOAT, 0, 3);

    _normalBuffer.bind();
    _normalBuffer.allocate(_normals.data(), static_cast<int>(_normals.size() * sizeof(float)));
    _prog->enableAttributeArray("vertexNormal");
    _prog->setAttributeBuffer("vertexNormal", GL_FLOAT, 0, 3);

    computeBounds();
}

std::vector<unsigned int> TriangleMesh::getIndices() const
{
    return _indices;
}

std::vector<float> TriangleMesh::getPoints() const
{
    return _points;
}

QVector3D TriangleMesh::getTranslation() const
{
    return QVector3D(_transX, _transY, _transZ);
}

void TriangleMesh::setTranslation(const QVector3D& trans)
{
    _transX = trans.x() - _transX;
    _transY = trans.y() - _transY;
    _transZ = trans.z() - _transZ;
    _transformation.translate(_transX, _transY, _transZ);
    setupTransformation();
    _transX = trans.x();
    _transY = trans.y();
    _transZ = trans.z();
}

QVector3D TriangleMesh::getRotation() const
{
    return QVector3D(_rotateX, _rotateY, _rotateZ);
}

void TriangleMesh::setRotation(const QVector3D& rota)
{
    _rotateX = rota.x() - _rotateX;
    _rotateY = rota.y() - _rotateY;
    _rotateZ = rota.z() - _rotateZ;
    _transformation.rotate(_rotateX, QVector3D(1.0f, 0.0f, 0.0f));
    _transformation.rotate(_rotateY, QVector3D(0.0f, 1.0f, 0.0f));
    _transformation.rotate(_rotateZ, QVector3D(0.0f, 0.0f, 1.0f));
    setupTransformation();
    _rotateX = rota.x();
    _rotateY = rota.y();
    _rotateZ = rota.z();
}

QVector3D TriangleMesh::getScaling() const
{
    return QVector3D(_scaleX, _scaleY, _scaleZ);
}

void TriangleMesh::setScaling(const QVector3D& scale)
{
    _scaleX = scale.x() / _scaleX;
    _scaleY = scale.y() / _scaleY;
    _scaleZ = scale.z() / _scaleZ;
    _transformation.scale(_scaleX, _scaleY, _scaleZ);
    setupTransformation();
    _scaleX = scale.x();
    _scaleY = scale.y();
    _scaleZ = scale.z();
}

QMatrix4x4 TriangleMesh::getTransformation() const
{
    return _transformation;
}

void TriangleMesh::setupTransformation()
{
    _prog->bind();
    _trsfpoints.clear();
    _trsfnormals.clear();

    // transform points
    for (size_t i = 0; i < _points.size(); i += 3)
    {
        QVector3D p(_points[i + 0], _points[i + 1], _points[i + 2]);
        QVector3D tp = _transformation * p;
        _trsfpoints.push_back(tp.x());
        _trsfpoints.push_back(tp.y());
        _trsfpoints.push_back(tp.z());
    }
    _positionBuffer.bind();
    _positionBuffer.allocate(_trsfpoints.data(), static_cast<int>(_trsfpoints.size() * sizeof(float)));
    _prog->enableAttributeArray("vertexPosition");
    _prog->setAttributeBuffer("vertexPosition", GL_FLOAT, 0, 3);

    // transform normals
    for (size_t i = 0; i < _normals.size(); i += 3)
    {
        QVector3D n(_normals[i + 0], _normals[i + 1], _normals[i + 2]);
        QMatrix4x4 rotMat = _transformation;
        // use only the rotations
        rotMat.setColumn(3, QVector4D(0, 0, 0, 1));
        QVector3D tn = rotMat * n;
        _trsfnormals.push_back(tn.x());
        _trsfnormals.push_back(tn.y());
        _trsfnormals.push_back(tn.z());
    }
    _normalBuffer.bind();
    _normalBuffer.allocate(_trsfnormals.data(), static_cast<int>(_trsfnormals.size() * sizeof(float)));
    _prog->enableAttributeArray("vertexNormal");
    _prog->setAttributeBuffer("vertexNormal", GL_FLOAT, 0, 3);

    buildTriangles();
    computeBounds();
}

void TriangleMesh::setTexureImage(const QImage& texImage)
{
    _texImage = texImage;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _texImage.width(), _texImage.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, _texImage.bits());
}

bool TriangleMesh::hasTexture() const
{
    return _hasTexture;
}

void TriangleMesh::enableTexture(const bool& bHasTexture)
{
    _hasTexture = bHasTexture;
}

float TriangleMesh::shininess() const
{
    return _material.shininess();
}

void TriangleMesh::setShininess(const float& shine)
{
    _material.setShininess(shine);
}

float TriangleMesh::opacity() const
{
    return _material.opacity();
}

void TriangleMesh::setOpacity(const float& opacity)
{
    _material.setOpacity(opacity);
}

QVector3D TriangleMesh::emmissiveMaterial() const
{
    return _material.emissive();
}

void TriangleMesh::setEmmissiveMaterial(const QVector3D& emissive)
{
    _material.setEmissive(emissive);
}

QVector3D TriangleMesh::specularMaterial() const
{
    return _material.specular();
}

void TriangleMesh::setSpecularMaterial(const QVector3D& specular)
{
    _material.setSpecular(specular);
}

QVector3D TriangleMesh::diffuseMaterial() const
{
    return _material.diffuse();
}

void TriangleMesh::setDiffuseMaterial(const QVector3D& diffuse)
{
    _material.setDiffuse(diffuse);
}

QVector3D TriangleMesh::ambientMaterial() const
{
    return _material.ambient();
}

void TriangleMesh::setAmbientMaterial(const QVector3D& ambient)
{
    _material.setAmbient(ambient);
}

bool TriangleMesh::isMetallic() const
{
    return _material.metallic();
}

void TriangleMesh::setMetallic(bool metallic)
{
    _material.setMetallic(metallic);
    _material.setMetalness(metallic ? 1.0f : 0.0f);
}

void TriangleMesh::setPBRAlbedoColor(const float& r, const float& g, const float& b)
{
    _material.setAlbedoColor(QVector3D(r, g, b));
}

void TriangleMesh::setPBRMetallic(const float& val)
{
    _material.setMetalness(val);
}

void TriangleMesh::setPBRRoughness(const float& val)
{
    _material.setRoughness(val);
}

QOpenGLVertexArrayObject& TriangleMesh::getVAO()
{
    return _vertexArrayObject;
}

bool TriangleMesh::intersectsWithRay(const QVector3D& rayPos, const QVector3D& rayDir, QVector3D& outIntersectionPoint)
{
    bool intersects = false;
    if (_triangles.size())
    {
        for (Triangle* t : _triangles)
        {
            intersects = t->intersectsWithRay(rayPos, rayDir, outIntersectionPoint);
            if (intersects)
                break;
        }
    }
    /*if (_trsfpoints.size() == 0)
    {
        return intersects;
    }
    try {
        size_t offset = 3; // each index points to 3 floats
        for (size_t i = 0; i < _indices.size() - offset;)
        {
            // Vertex 1
            QVector3D v1(_trsfpoints.at(offset * _indices.at(i) + 0), // x coordinate
                         _trsfpoints.at(offset * _indices.at(i) + 1),          // y coordinate
                         _trsfpoints.at(offset * _indices.at(i) + 2));         // z coordinate
            i++;

            // Vertex 2
            QVector3D v2(_trsfpoints.at(offset * _indices.at(i) + 0), // x coordinate
                         _trsfpoints.at(offset * _indices.at(i) + 1),          // y coordinate
                         _trsfpoints.at(offset * _indices.at(i) + 2));         // z coordinate
            i++;

            // Vertex 3
            QVector3D v3(_trsfpoints[offset * _indices.at(i) + 0], // x coordinate
                    _trsfpoints.at(offset * _indices.at(i) + 1),          // y coordinate
                    _trsfpoints.at(offset * _indices.at(i) + 2));         // z coordinate
            i++;
            intersects = rayIntersectsTriangle(rayPos, rayDir, v1, v2, v3, outIntersectionPoint);
            if (intersects)
                break;
        }
    }
    catch (const std::exception& ex) {
        std::cout << "Exception raised in TriangleMesh::intersectsWithRay\n" << ex.what() << std::endl;
    }*/

    return intersects;
}

bool TriangleMesh::rayIntersectsTriangle(const QVector3D& rayOrigin,
                                         const QVector3D& rayVector,
                                         const QVector3D& vertex0,
                                         const QVector3D& vertex1,
                                         const QVector3D& vertex2,
                                         QVector3D& outIntersectionPoint)
{
    // Möller–Trumbore intersection algorithm
    // https://en.wikipedia.org/wiki/M%C3%B6ller%E2%80%93Trumbore_intersection_algorithm
    const float EPSILON = 0.0000001f;
    QVector3D edge1, edge2, h, s, q;
    float a, f, u, v;
    edge1 = vertex1 - vertex0;
    edge2 = vertex2 - vertex0;
    h = QVector3D::crossProduct(rayVector, edge2);
    a = QVector3D::dotProduct(edge1, h);
    if (a > -EPSILON && a < EPSILON)
        return false;    // This ray is parallel to this triangle.
    f = 1.0f / a;
    s = rayOrigin - vertex0;
    u = f * QVector3D::dotProduct(s, h);
    if (u < 0.0f || u > 1.0f)
        return false;
    q = QVector3D::crossProduct(s, edge1);
    v = f * QVector3D::dotProduct(rayVector, q);
    if (v < 0.0f || u + v > 1.0f)
        return false;
    // At this stage we can compute t to find out where the intersection point is on the line.
    float t = f * QVector3D::dotProduct(edge2, q);
    if (t > EPSILON) // ray intersection
    {
        outIntersectionPoint = rayOrigin + rayVector * t;
        return true;
    }
    else // This means that there is a line intersection but not a ray intersection.
        return false;
}

bool TriangleMesh::hasAlbedoPBRMap() const
{
    return _hasAlbedoPBRMap;
}

void TriangleMesh::enableAlbedoPBRMap(bool hasAlbedoMap)
{
    _hasAlbedoPBRMap = hasAlbedoMap;
}

bool TriangleMesh::hasMetallicPBRMap() const
{
    return _hasMetallicPBRMap;
}

void TriangleMesh::enableMetallicPBRMap(bool hasMetallicMap)
{
    _hasMetallicPBRMap = hasMetallicMap;
}

bool TriangleMesh::hasRoughnessPBRMap() const
{
    return _hasRoughnessPBRMap;
}

void TriangleMesh::enableRoughnessPBRMap(bool hasRoughnessMap)
{
    _hasRoughnessPBRMap = hasRoughnessMap;
}

bool TriangleMesh::hasHeightPBRMap() const
{
    return _hasHeightPBRMap;
}

void TriangleMesh::enableHeightPBRMap(bool hasHeightMap)
{
    _hasHeightPBRMap = hasHeightMap;
}

bool TriangleMesh::hasAOPBRMap() const
{
    return _hasAOPBRMap;
}

void TriangleMesh::enableAOPBRMap(bool hasAOMap)
{
    _hasAOPBRMap = hasAOMap;
}

bool TriangleMesh::hasNormalPBRMap() const
{
    return _hasNormalPBRMap;
}

void TriangleMesh::enableNormalPBRMap(bool hasNormalMap)
{
    _hasNormalPBRMap = hasNormalMap;
}

bool TriangleMesh::hasOpacityPBRMap() const
{
    return _hasOpacityPBRMap;
}

void TriangleMesh::enableOpacityPBRMap(bool hasOpacityMap)
{
    _hasOpacityPBRMap = hasOpacityMap;
}

void TriangleMesh::setAlbedoPBRMap(unsigned int albedoMap)
{
    glDeleteTextures(1, &_albedoPBRMap);
    _albedoPBRMap = albedoMap;
}

void TriangleMesh::setMetallicPBRMap(unsigned int metallicMap)
{
    glDeleteTextures(1, &_metallicPBRMap);
    _metallicPBRMap = metallicMap;
}

void TriangleMesh::setRoughnessPBRMap(unsigned int roughnessMap)
{
    glDeleteTextures(1, &_roughnessPBRMap);
    _roughnessPBRMap = roughnessMap;
}

void TriangleMesh::setNormalPBRMap(unsigned int normalMap)
{
    glDeleteTextures(1, &_normalPBRMap);
    _normalPBRMap = normalMap;
}

void TriangleMesh::setAOPBRMap(unsigned int aoMap)
{
    glDeleteTextures(1, &_aoPBRMap);
    _aoPBRMap = aoMap;
}

void TriangleMesh::setHeightPBRMap(unsigned int heightMap)
{
    glDeleteTextures(1, &_heightPBRMap);
    _heightPBRMap = heightMap;
}

float TriangleMesh::getHeightPBRMapScale() const
{
    return _heightPBRMapScale;
}

void TriangleMesh::setHeightPBRMapScale(float heightScale)
{
    _heightPBRMapScale = heightScale;
}

void TriangleMesh::setOpacityPBRMap(unsigned int opacityMap)
{
    glDeleteTextures(1, &_opacityPBRMap);
    _opacityPBRMap = opacityMap;
}

void TriangleMesh::invertOpacityPBRMap(bool invert)
{
    _opacityPBRMapInverted = invert;
}

void TriangleMesh::clearAlbedoPBRMap()
{
    glDeleteTextures(1, &_albedoPBRMap);
    _albedoPBRMap = 0;
}

void TriangleMesh::clearMetallicPBRMap()
{
    glDeleteTextures(1, &_metallicPBRMap);
    _metallicPBRMap = 0;
}

void TriangleMesh::clearRoughnessPBRMap()
{
    glDeleteTextures(1, &_roughnessPBRMap);
    _roughnessPBRMap = 0;
}

void TriangleMesh::clearNormalPBRMap()
{
    glDeleteTextures(1, &_normalPBRMap);
    _normalPBRMap = 0;
}

void TriangleMesh::clearAOPBRMap()
{
    glDeleteTextures(1, &_aoPBRMap);
    _aoPBRMap = 0;
}

void TriangleMesh::clearHeightPBRMap()
{
    glDeleteTextures(1, &_heightPBRMap);
    _heightPBRMap = 0;
}

void TriangleMesh::clearOpacityPBRMap()
{
    glDeleteTextures(1, &_opacityPBRMap);
    _opacityPBRMap = 0;
}

void TriangleMesh::clearAllPBRMaps()
{
    glDeleteTextures(1, &_albedoPBRMap);
    _albedoPBRMap = 0;
    glDeleteTextures(1, &_metallicPBRMap);
    _metallicPBRMap = 0;
    glDeleteTextures(1, &_roughnessPBRMap);
    _roughnessPBRMap = 0;
    glDeleteTextures(1, &_normalPBRMap);
    _normalPBRMap = 0;
    glDeleteTextures(1, &_aoPBRMap);
    _aoPBRMap = 0;
    glDeleteTextures(1, &_heightPBRMap);
    _heightPBRMap = 0;
}
