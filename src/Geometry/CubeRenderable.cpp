#include "CubeRenderable.h"

CubeRenderable::CubeRenderable(QOpenGLShaderProgram* prog, float size)
    : RenderableMesh(prog, "Cube"), _cube(size)
{
    uploadGeometry(_cube);
}

void CubeRenderable::setSize(float size)
{
    _cube.setSize(size);
    uploadGeometry(_cube);
}

RenderableMesh* CubeRenderable::clone()
{
    return new CubeRenderable(_prog, _cube.size());
}
