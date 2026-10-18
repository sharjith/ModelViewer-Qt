#include "ConeRenderable.h"

ConeRenderable::ConeRenderable(QOpenGLShaderProgram* prog,
                               float rad, float height,
                               unsigned int sl, unsigned int st,
                               unsigned int sMax, unsigned int tMax)
    : RenderableMesh(prog, "Cone"),
      _cone(rad, height, sl, st, sMax, tMax)
{
    uploadGeometry(_cone);
}

void ConeRenderable::setParameters(float rad, float height,
                                   unsigned int sl, unsigned int st,
                                   unsigned int sMax, unsigned int tMax)
{
    _cone.setParameters(rad, height, sl, st, sMax, tMax);
    uploadGeometry(_cone);
}

RenderableMesh* ConeRenderable::clone()
{
    return new ConeRenderable(_prog,
                              _cone.radius(), _cone.height(),
                              _cone.slices(), _cone.stacks(),
                              static_cast<unsigned int>(_cone.sMax()),
                              static_cast<unsigned int>(_cone.tMax()));
}
