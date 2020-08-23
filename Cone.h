#pragma once

#include "QuadMesh.h"

class Cone : public QuadMesh
{
public:
    Cone(QOpenGLShaderProgram* prog, float rad, float height, unsigned int sl, unsigned int st, unsigned int sMax = 1, unsigned int tMax = 1);
};
