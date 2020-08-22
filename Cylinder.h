#pragma once

#include "QuadMesh.h"

class Cylinder : public QuadMesh
{
public:
	Cylinder(QOpenGLShaderProgram* prog, float rad, float height, unsigned int sl, unsigned int st);
};
