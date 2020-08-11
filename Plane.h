#pragma once

#include "TriangleMesh.h"

class Plane : public TriangleMesh
{
public:
    Plane(QOpenGLShaderProgram* prog, float xsize, float ysize, int xdivs, int zdivs, float zlevel = 0.0f, float smax = 1.0f, float tmax = 1.0f);
    void setPlane(QOpenGLShaderProgram* prog, float xsize, float ysize, int xdivs, int zdivs, float zlevel = 0.0f, float smax = 1.0f, float tmax = 1.0f);
};
