#pragma once

#include "GridMesh.h"

class Cylinder : public GridMesh
{
public:
	Cylinder(QOpenGLShaderProgram* prog, float rad, float height, unsigned int sl, unsigned int st, unsigned int sMax = 1, unsigned int tMax = 1);
};
