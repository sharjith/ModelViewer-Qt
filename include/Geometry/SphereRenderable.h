#pragma once

#include "RenderableMesh.h"
#include "Sphere.h"

class SphereRenderable : public RenderableMesh
{
public:
    SphereRenderable(QOpenGLShaderProgram* prog,
                     float rad, unsigned int sl, unsigned int st,
                     unsigned int sMax = 1, unsigned int tMax = 1);

    RenderableMesh* clone() override;

private:
    Sphere _sphere;
};
