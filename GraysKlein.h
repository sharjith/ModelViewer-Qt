#pragma once

#include <ParametricSurface.h>

class Point;
class GraysKlein : public ParametricSurface
{
public:
	GraysKlein(QOpenGLShaderProgram* prog, GLfloat radius, GLuint nSlices, GLuint nStacks);
	~GraysKlein();

	virtual float firstUParameter() const;
	virtual float firstVParameter() const;
	virtual float lastUParameter() const ;
	virtual float lastVParameter() const ;
	virtual Point pointAtParameter(const float& u, const float& v);

	GLfloat _A;
	GLfloat _M;
	GLfloat _N;
	
private:
	GLfloat _radius;
};

