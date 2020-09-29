#pragma once

#include "GridMesh.h"

class Sphere : public GridMesh
{
public:
	Sphere(QOpenGLShaderProgram* prog, float rad, unsigned int sl, unsigned int st, unsigned int sMax = 1, unsigned int tMax = 1);
};
