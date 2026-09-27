#pragma once

#include "RenderableMesh.h"
#include "Cone.h"

class ConeRenderable : public RenderableMesh
{
public:
    ConeRenderable(QOpenGLShaderProgram* prog,
                   float rad, float height, unsigned int sl, unsigned int st,
                   unsigned int sMax = 1, unsigned int tMax = 1);

    // Rebuild cone geometry and re-upload to the GPU.
    void setParameters(float rad, float height, unsigned int sl, unsigned int st,
                       unsigned int sMax = 1, unsigned int tMax = 1);

    RenderableMesh* clone() override;

private:
    Cone _cone;
};
