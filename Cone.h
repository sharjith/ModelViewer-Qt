#pragma once

#include "GridMesh.h"

class Cone : public GridMesh
{
public:
	Cone(QOpenGLShaderProgram* prog, float rad, float height, unsigned int sl, unsigned int st, unsigned int sMax = 1, unsigned int tMax = 1);

protected:
	void computeBounds();
};
