#pragma once

#include "QuadMesh.h"

class Sphere : public QuadMesh
{
public:
    Sphere(QOpenGLShaderProgram* prog, float rad, GLuint sl, GLuint st);
};
