#pragma once
#include "ParametricSurface.h"
class Figure8KleinBottle :
	public ParametricSurface
{
public:
	Figure8KleinBottle(QOpenGLShaderProgram* prog, GLfloat radius, GLuint nSlices, GLuint nStacks);
	~Figure8KleinBottle();

	virtual float firstUParameter() const;
	virtual float firstVParameter() const;
	virtual float lastUParameter() const;
	virtual float lastVParameter() const;
	virtual Point pointAtParameter(const float& u, const float& v);

private:
	GLfloat _radius;
};

