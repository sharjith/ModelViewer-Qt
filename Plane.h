#pragma once

#include "TriangleMesh.h"

class Plane : public TriangleMesh
{
public:
    Plane(QOpenGLShaderProgram* prog, float xsize, float zsize, int xdivs, int zdivs, float smax = 1.0f, float tmax = 1.0f);
};
