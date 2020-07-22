#pragma once

#include "QuadMesh.h"

class Torus : public QuadMesh
{
public:
    Torus(QOpenGLShaderProgram* prog, GLfloat outerRadius, GLfloat innerRadius, GLuint nsides, GLuint nrings);
};
