#pragma once

#include "GridMesh.h"

class Cylinder : public GridMesh
{
public:
	Cylinder(QOpenGLShaderProgram* prog, float rad, float height, unsigned int sl, unsigned int st, unsigned int sMax = 1, unsigned int tMax = 1);
	virtual TriangleMesh* clone();
protected:
	void computeBounds();

private:
	float _radius;
	float _height;
};
