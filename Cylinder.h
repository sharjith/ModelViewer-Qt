#pragma once

#include "QuadMesh.h"

class Cylinder : public QuadMesh
{
public:
	Cylinder(QOpenGLShaderProgram* prog, float rad, float height, unsigned int sl, unsigned int st, unsigned int sMax = 1, unsigned int tMax = 1);
};
