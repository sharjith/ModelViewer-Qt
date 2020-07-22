#pragma once

#include <ParametricSurface.h>

class Point;
class BowTie : public ParametricSurface
{
public:
	BowTie(QOpenGLShaderProgram *prog, GLfloat radius, GLuint nSlices, GLuint nStacks);
	~BowTie();

	virtual float firstUParameter() const;
	virtual float firstVParameter() const;
	virtual float lastUParameter() const;
	virtual float lastVParameter() const;
	virtual Point pointAtParameter(const float &u, const float &v);

private:
	GLfloat _radius;
};
