#include "SphereRenderable.h"

SphereRenderable::SphereRenderable(QOpenGLShaderProgram* prog,
                                   float rad, unsigned int sl, unsigned int st,
                                   unsigned int sMax, unsigned int tMax)
    : RenderableMesh(prog, "Sphere"), _sphere(rad, sl, st, sMax, tMax)
{
    uploadGeometry(_sphere);
}

RenderableMesh* SphereRenderable::clone()
{
    return new SphereRenderable(_prog,
                                _sphere.radius(),
                                _sphere.slices(), _sphere.stacks(),
                                static_cast<unsigned int>(_sphere.sMax()),
                                static_cast<unsigned int>(_sphere.tMax()));
}
