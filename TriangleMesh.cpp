#include "TriangleMesh.h"
#include <algorithm>
#include <iostream>

TriangleMesh::TriangleMesh(QOpenGLShaderProgram *prog, const QString name) : Drawable(prog), _name(name),
    _opacity(1.0f),
    _bHasTexture(false)    
{
    _transformation.setToIdentity();

    _indexBuffer = QOpenGLBuffer(QOpenGLBuffer::IndexBuffer);
    _positionBuffer = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    _normalBuffer = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    _texCoordBuffer = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    _tangentBuf = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);

    _indexBuffer.create();
    _positionBuffer.create();
    _normalBuffer.create();
    _texCoordBuffer.create();
    _tangentBuf.create();

    _vertexArrayObject.create();

    _ambientMaterial = {0.2109375f, 0.125f, 0.05078125f, 1.0f};
    _diffuseMaterial = {0.7109375f, 0.62890625f, 0.55078125f, 1.0f};
    _specularMaterial = {0.37890625f, 0.390625f, 0.3359375f, 1.0f};
    _shininess = fabs(128.0f * 0.2f);


    if (!_texBuffer.load("textures/opengllogo.png"))
    { // Load first image from file
        qWarning("Could not read image file, using single-color instead.");
        QImage dummy(128, 128, static_cast<QImage::Format>(5));
        dummy.fill(Qt::white);
        _texBuffer = dummy;
    }
    _texImage = QGLWidget::convertToGLFormat(_texBuffer); // flipped 32bit RGBA

    glGenTextures(1, &_texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
}

void TriangleMesh::initBuffers(
        std::vector<GLuint> *indices,
        std::vector<GLfloat> *points,
        std::vector<GLfloat> *normals,
        std::vector<GLfloat> *texCoords,
        std::vector<GLfloat> *tangents)
{
    // Must have data for indices, points, and normals
    if (indices == nullptr || points == nullptr || normals == nullptr)
        return;

    _indices = *indices;
    _points = *points;
    _trsfpoints = _points;
    _normals = *normals;

    _nVerts = (GLuint)indices->size();

    _buffers.push_back(_indexBuffer);
    _indexBuffer.bind();
    _indexBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    _indexBuffer.allocate(indices->data(), static_cast<int>(indices->size() * sizeof(GLuint)));

    _buffers.push_back(_positionBuffer);
    _positionBuffer.bind();
    _positionBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    _positionBuffer.allocate(points->data(), static_cast<int>(points->size() * sizeof(GLfloat)));

    _buffers.push_back(_normalBuffer);
    _normalBuffer.bind();
    _normalBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    _normalBuffer.allocate(normals->data(), static_cast<int>(normals->size() * sizeof(GLfloat)));

    if (texCoords != nullptr)
    {
        _buffers.push_back(_texCoordBuffer);
        _texCoordBuffer.bind();
        _texCoordBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
        _texCoordBuffer.allocate(texCoords->data(), static_cast<int>(texCoords->size() * sizeof(GLfloat)));
    }

    if (tangents != nullptr)
    {
        _buffers.push_back(_tangentBuf);
        _tangentBuf.bind();
        _tangentBuf.setUsagePattern(QOpenGLBuffer::StaticDraw);
        _tangentBuf.allocate(tangents->data(), static_cast<int>(tangents->size() * sizeof(GLfloat)));
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
    if (texCoords != nullptr)
    {
        _texCoordBuffer.bind();
        //glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, 0);
        //glEnableVertexAttribArray(2);  // Tex coord
        _prog->enableAttributeArray("texCoord2d");
        _prog->setAttributeBuffer("texCoord2d", GL_FLOAT, 0, 2);
    }

    if (tangents != nullptr)
    {
        _tangentBuf.bind();
        //glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 0, 0);
        //glEnableVertexAttribArray(3);  // Tangents
        _prog->enableAttributeArray("tangentCoord");
        _prog->setAttributeBuffer("tangentCoord", GL_FLOAT, 0, 4);
    }

    _vertexArrayObject.release();
}

void TriangleMesh::setProg(QOpenGLShaderProgram *prog)
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


    _texCoordBuffer.bind();
    //glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, 0);
    //glEnableVertexAttribArray(2);  // Tex coord
    _prog->enableAttributeArray("texCoord2d");
    _prog->setAttributeBuffer("texCoord2d", GL_FLOAT, 0, 2);

    _tangentBuf.bind();
    //glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 0, 0);
    //glEnableVertexAttribArray(3);  // Tangents
    _prog->enableAttributeArray("tangentCoord");
    _prog->setAttributeBuffer("tangentCoord", GL_FLOAT, 0, 4);


    _vertexArrayObject.release();
}

void TriangleMesh::render()
{
    if (!_vertexArrayObject.isCreated())
        return;


    glTexImage2D(GL_TEXTURE_2D, 0, 3, _texImage.width(), _texImage.height(), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, _texImage.bits());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);


    _prog->bind();
    _prog->setUniformValue("texUnit", 0);
    _prog->setUniformValue("material.emission", _emmissiveMaterial.toVector3D());
    _prog->setUniformValue("material.ambient", _ambientMaterial.toVector3D());
    _prog->setUniformValue("material.diffuse", _diffuseMaterial.toVector3D());
    _prog->setUniformValue("material.specular", _specularMaterial.toVector3D());
    _prog->setUniformValue("material.shininess", _shininess);
    _prog->setUniformValue("texEnabled", _bHasTexture);
    _prog->setUniformValue("alpha", _opacity);
    _prog->setUniformValue("selected", _selected);


    _vertexArrayObject.bind();
    glDrawElements(GL_TRIANGLES, _nVerts, GL_UNSIGNED_INT, 0);
    _vertexArrayObject.release();
    _prog->release();
}

TriangleMesh::~TriangleMesh()
{
    deleteBuffers();
}

void TriangleMesh::deleteBuffers()
{
    if (_buffers.size() > 0)
    {
        for (QOpenGLBuffer &buff : _buffers)
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

void TriangleMesh::computeBoundingSphere(std::vector<GLfloat> points)
{
    /*
    float minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0, maxZ = 0;
    for (int i = 0; i < points.size(); i += 3)
    {
        // X
        if (points[i] > maxX)
            maxX = points[i];
        if (points[i] < minX)
            minX = points[i];
        // Y
        if (points[i+1] > maxY)
            maxY = points[i+1];
        if (points[i+1] < minY)
            minY = points[i+1];
        // Z
        if (points[i+2] > maxZ)
            maxZ = points[i+2];
        if (points[i+2] < minZ)
            minZ = points[i+2];
    }

    QVector3D sphereCenter = QVector3D(minX + (maxX - minX) / 2, minY + (maxY - minY) / 2, minZ + (maxZ - minZ) / 2);
    float sphereRadius = std::max((maxX - minX) / 2, std::max((maxY - minY) / 2, (maxZ - minZ) / 2));

    _boundingSphere.setCenter(sphereCenter);
    _boundingSphere.setRadius(sphereRadius);
    */

    // Ritter's algorithm
    std::vector<QVector3D> aPoints;
    for (ulong i = 0; i < points.size(); i += 3)
    {
        aPoints.push_back(QVector3D(points[i], points[i + 1], points[i + 2]));
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
}

float TriangleMesh::getLowestZValue() const
{
    float lowestZ = std::numeric_limits<float>::max();
    for (size_t i = 2; i < _trsfpoints.size(); i+=3)
    {
        float z = _trsfpoints[i];
        if (z < lowestZ)
            lowestZ = z;
    }
    return lowestZ;
}

std::vector<GLfloat> TriangleMesh::getNormals() const
{
    return _normals;
}

std::vector<GLfloat> TriangleMesh::getPoints() const
{
    return _points;
}

QMatrix4x4 TriangleMesh::getTransformation() const
{
    return _transformation;
}

void TriangleMesh::setTransformation(const QMatrix4x4 &transformation)
{
    _prog->bind();
    _transformation = transformation;
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
    _positionBuffer.allocate(_trsfpoints.data(), static_cast<int>(_trsfpoints.size() * sizeof(GLfloat)));
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
    _normalBuffer.allocate(_trsfnormals.data(), static_cast<int>(_trsfnormals.size() * sizeof(GLfloat)));
    _prog->enableAttributeArray("vertexNormal");
    _prog->setAttributeBuffer("vertexNormal", GL_FLOAT, 0, 3);

    computeBoundingSphere(_trsfpoints);
}

void TriangleMesh::setTexureImage(const QImage &texImage)
{
    _texImage = texImage;
}

GLboolean TriangleMesh::hasTexture() const
{
    return _bHasTexture;
}

void TriangleMesh::enableTexture(const GLboolean &bHasTexture)
{
    _bHasTexture = bHasTexture;
}

GLfloat TriangleMesh::shininess() const
{
    return _shininess;
}

void TriangleMesh::setShininess(const GLfloat &shine)
{
    _shininess = shine;
}

GLfloat TriangleMesh::opacity() const
{
    return _opacity;
}

void TriangleMesh::setOpacity(const GLfloat &opacity)
{
    _opacity = opacity;
}

QVector4D TriangleMesh::specularReflectivity() const
{
    return _specularReflectivity;
}

void TriangleMesh::setSpecularReflectivity(const QVector4D &specularReflectivity)
{
    _specularReflectivity = specularReflectivity;
}

QVector4D TriangleMesh::emmissiveMaterial() const
{
    return _emmissiveMaterial;
}

void TriangleMesh::setEmmissiveMaterial(const QVector4D &emmissiveMaterial)
{
    _emmissiveMaterial = emmissiveMaterial;
}

QVector4D TriangleMesh::specularMaterial() const
{
    return _specularMaterial;
}

void TriangleMesh::setSpecularMaterial(const QVector4D &specularMaterial)
{
    _specularMaterial = specularMaterial;
}

QVector4D TriangleMesh::diffuseMaterial() const
{
    return _diffuseMaterial;
}

void TriangleMesh::setDiffuseMaterial(const QVector4D &diffuseMaterial)
{
    _diffuseMaterial = diffuseMaterial;
}

QVector4D TriangleMesh::ambientMaterial() const
{
    return _ambientMaterial;
}

void TriangleMesh::setAmbientMaterial(const QVector4D &ambientMaterial)
{
    _ambientMaterial = ambientMaterial;
}

QOpenGLVertexArrayObject &TriangleMesh::getVAO()
{
    return _vertexArrayObject;
}

bool TriangleMesh::intersectsWithRay(const QVector3D& rayPos, const QVector3D& rayDir, QVector3D& outIntersectionPoint)
{
    bool intersects = false;
    for (size_t i = 0; i < _trsfpoints.size() - 9; i += 9)
    {
        QVector3D v0(_trsfpoints[i + 0], _trsfpoints[i + 1], _trsfpoints[i + 2]);
        QVector3D v1(_trsfpoints[i + 3], _trsfpoints[i + 4], _trsfpoints[i + 5]);
        QVector3D v2(_trsfpoints[i + 6], _trsfpoints[i + 7], _trsfpoints[i + 8]);

        intersects = rayIntersectsTriangle(rayPos, rayDir, v0, v1, v2, outIntersectionPoint);
        if(intersects)
            break;
    }

    return intersects;
}

// Möller–Trumbore intersection algorithm
bool TriangleMesh::rayIntersectsTriangle(const QVector3D& rayOrigin,
                                         const QVector3D& rayVector,
                                         const QVector3D& vertex0,
                                         const QVector3D& vertex1,
                                         const QVector3D& vertex2,
                                         QVector3D& outIntersectionPoint)
{
    const float EPSILON = 0.0000001f;
    QVector3D edge1, edge2, h, s, q;
    float a,f,u,v;
    edge1 = vertex1 - vertex0;
    edge2 = vertex2 - vertex0;
    h = QVector3D::crossProduct(rayVector, edge2);
    a = QVector3D::dotProduct(edge1, h);
    if (a > -EPSILON && a < EPSILON)
        return false;    // This ray is parallel to this triangle.
    f = 1.0f/a;
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
        /*qDebug() << "Ray Origin: " << rayOrigin;
        qDebug() << "Ray Vector: " << rayVector;
        qDebug() << "Intersection at: " << outIntersectionPoint;
        qDebug() << "Vertices:\n" << vertex0 << vertex1 << vertex2;*/
        return true;
    }
    else // This means that there is a line intersection but not a ray intersection.
        return false;
}
