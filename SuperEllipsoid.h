#pragma once

#include <ParametricSurface.h>

class Point;
class SuperEllipsoid : public ParametricSurface
{
	friend class SuperEllipsoidEditor;
public:
	SuperEllipsoid(QOpenGLShaderProgram* prog, GLfloat radius, GLfloat scaleX, GLfloat scaleY, GLfloat scaleZ, GLfloat sinPower, GLfloat cosPower, GLuint nSlices, GLuint nStacks);
	~SuperEllipsoid();

	virtual float firstUParameter() const;
	virtual float firstVParameter() const;
	virtual float lastUParameter() const ;
	virtual float lastVParameter() const ;
	virtual Point pointAtParameter(const float& u, const float& v);
	
private:
	GLfloat _radius;
	GLfloat _scaleX;
	GLfloat _scaleY;
	GLfloat _scaleZ;
	GLfloat _n1;
	GLfloat _n2;
};

