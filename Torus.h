#pragma once

#include "QuadMesh.h"

class Torus : public QuadMesh
{
public:
    Torus(QOpenGLShaderProgram* prog, float outerRadius, float innerRadius, unsigned int nsides, unsigned int nrings, unsigned int sMax = 1, unsigned int tMax = 1);
};
