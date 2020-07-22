#pragma once

#include <ParametricSurface.h>
#include <Point.h>

class TopShell : public ParametricSurface
{
public:
	TopShell(QOpenGLShaderProgram* prog, Point center, GLfloat radius, GLuint nSlices, GLuint nStacks);
	~TopShell();

	virtual float firstUParameter() const;
	virtual float firstVParameter() const;
	virtual float lastUParameter() const ;
	virtual float lastVParameter() const ;
	virtual Point pointAtParameter(const float& u, const float& v);
	
private:
	GLfloat _radius;
	Point _center;
};

