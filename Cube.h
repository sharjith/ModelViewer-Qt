#pragma once

#include "Drawable.h"
#include "QuadMesh.h"

class Cube : public QuadMesh
{
public:
    Cube(QOpenGLShaderProgram *prog, float size = 1.0f);
};
