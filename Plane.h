#pragma once

#include "TriangleMesh.h"

class Plane : public TriangleMesh
{
public:
	Plane(QOpenGLShaderProgram* prog, QVector3D center, float xsize, float ysize, int xdivs, int ydivs, float zlevel = 0.0f, float smax = 1.0f, float tmax = 1.0f);
	void setPlane(QOpenGLShaderProgram* prog, QVector3D center, float xsize, float ysize, int xdivs, int ydivs, float zlevel = 0.0f, float smax = 1.0f, float tmax = 1.0f);

private:
	void buildMesh(QVector3D center, float xsize, float ysize, int xdivs, int ydivs, float zlevel, float smax, float tmax);
};
