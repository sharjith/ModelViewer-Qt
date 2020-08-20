#include "QuadMesh.h"


QuadMesh::QuadMesh(QOpenGLShaderProgram* prog, const QString name) : TriangleMesh(prog, name)
{

}

void QuadMesh::render()
{
    if (!_vertexArrayObject.isCreated())
        return;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _texture);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, _texImage.width(), _texImage.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, _texImage.bits());    
    glGenerateMipmap(GL_TEXTURE_2D);

    _prog->bind();
    _prog->setUniformValue("texUnit", 0);
    _prog->setUniformValue("material.emission", _emmissiveMaterial.toVector3D());
    _prog->setUniformValue("material.ambient", _ambientMaterial.toVector3D());
    _prog->setUniformValue("material.diffuse", _diffuseMaterial.toVector3D());
    _prog->setUniformValue("material.specular", _specularMaterial.toVector3D());
    _prog->setUniformValue("material.shininess", _shininess);
    _prog->setUniformValue("texEnabled", _bHasTexture);
    //_prog->setUniformValue("reflectionMapEnabled", false);
    _prog->setUniformValue("alpha", _opacity);
    _prog->setUniformValue("selected", _selected);

    _vertexArrayObject.bind();
    glDrawElements(GL_QUADS, _nVerts, GL_UNSIGNED_INT, 0);
    _vertexArrayObject.release();

    _prog->release();
}

QuadMesh::~QuadMesh()
{
}

bool QuadMesh::intersectsWithRay(const QVector3D& rayPos, const QVector3D& rayDir, QVector3D& outIntersectionPoint)
{
    bool intersects = false;
    for (size_t i = 0; i < _trsfpoints.size() - 9; i += 12)
    {
        QVector3D v0(_trsfpoints[i + 0], _trsfpoints[i + 1], _trsfpoints[i + 2]);
        QVector3D v1(_trsfpoints[i + 3], _trsfpoints[i + 4], _trsfpoints[i + 5]);
        QVector3D v2(_trsfpoints[i + 6], _trsfpoints[i + 7], _trsfpoints[i + 8]);

        intersects = rayIntersectsTriangle(rayPos, rayDir, v0, v1, v2, outIntersectionPoint);
        if (intersects)
            break;

        QVector3D v3(_trsfpoints[i + 0], _trsfpoints[i + 1], _trsfpoints[i + 2]);
        QVector3D v4(_trsfpoints[i + 3], _trsfpoints[i + 4], _trsfpoints[i + 5]);
        QVector3D v5(_trsfpoints[i + 9], _trsfpoints[i + 10], _trsfpoints[i + 11]);

        intersects = rayIntersectsTriangle(rayPos, rayDir, v3, v4, v5, outIntersectionPoint);
        if (intersects)
            break;
    }

    return intersects;
}

