#pragma once

#include <ParametricSurface.h>

class Point;
class TriaxialTritorus : public ParametricSurface
{
public:
	TriaxialTritorus(QOpenGLShaderProgram* prog, GLfloat radius, GLuint nSlices, GLuint nStacks);
	~TriaxialTritorus();

	virtual float firstUParameter() const;
	virtual float firstVParameter() const;
	virtual float lastUParameter() const ;
	virtual float lastVParameter() const ;
	virtual Point pointAtParameter(const float& u, const float& v);
	
private:
	GLfloat _radius;
};

