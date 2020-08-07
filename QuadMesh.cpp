#include "QuadMesh.h"


QuadMesh::QuadMesh(QOpenGLShaderProgram* prog, const QString name) : TriangleMesh(prog, name)
{

}

void QuadMesh::render()
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
    _prog->setUniformValue("material.emission", _emmissiveMaterial.toVector3D());
    _prog->setUniformValue("material.ambient", _ambientMaterial.toVector3D());
    _prog->setUniformValue("material.diffuse", _diffuseMaterial.toVector3D());
    _prog->setUniformValue("material.specular", _specularMaterial.toVector3D());
    _prog->setUniformValue("material.shininess", _shininess);
    _prog->setUniformValue("b_texEnabled", _bHasTexture);
    _prog->setUniformValue("f_alpha", _opacity);
    _prog->setUniformValue("selected", _selected);

    _vertexArrayObject.bind();
    glDrawElements(GL_QUADS, _nVerts, GL_UNSIGNED_INT, 0);
    _vertexArrayObject.release();

    _prog->release();
}

QuadMesh::~QuadMesh()
{
}

