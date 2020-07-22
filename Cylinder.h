#pragma once

#include "QuadMesh.h"

class Cylinder : public QuadMesh
{
public:
    Cylinder(QOpenGLShaderProgram *prog, float rad, float height, GLuint sl, GLuint st);
};
