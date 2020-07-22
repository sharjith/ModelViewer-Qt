#pragma once

#include <ParametricSurface.h>

class Point;
class VerrillMinimal : public ParametricSurface
{
public:
	VerrillMinimal(QOpenGLShaderProgram* prog, GLfloat radius, GLuint nSlices, GLuint nStacks);
	~VerrillMinimal();

	virtual float firstUParameter() const;
	virtual float firstVParameter() const;
	virtual float lastUParameter() const ;
	virtual float lastVParameter() const ;
	virtual Point pointAtParameter(const float& u, const float& v);
	
private:
	GLfloat _radius;
};

