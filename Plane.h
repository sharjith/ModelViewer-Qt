#pragma once

#include "TriangleMesh.h"

class Plane : public TriangleMesh
{
public:
	Plane(QOpenGLShaderProgram* prog, QVector3D center, float xsize, float ysize, int xdivs, int ydivs, float zlevel = 0.0f, float smax = 1.0f, float tmax = 1.0f);
	void setPlane(QOpenGLShaderProgram* prog, QVector3D center, float xsize, float ysize, int xdivs, int ydivs, float zlevel = 0.0f, float smax = 1.0f, float tmax = 1.0f);

	virtual TriangleMesh* clone();
private:
	void buildMesh(QVector3D center, float xsize, float ysize, int xdivs, int ydivs, float zlevel, float smax, float tmax);

protected:
	QVector3D	_center;
	float		_xSize;
	float		_ySize;
	int			_xDivs;
	int			_yDivs;
	float		_zLevel;
};
