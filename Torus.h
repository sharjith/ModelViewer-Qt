#pragma once

#include "GridMesh.h"

class Torus : public GridMesh
{
public:
	Torus(QOpenGLShaderProgram* prog, float outerRadius, float innerRadius, unsigned int nsides, unsigned int nrings, unsigned int sMax = 1, unsigned int tMax = 1);

	virtual TriangleMesh* clone();

protected:
	float _innerRadius;
	float _outerRadius;
};
