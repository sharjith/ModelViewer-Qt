#pragma once

#include <ParametricSurface.h>

class Point;
class SphericalHarmonic : public ParametricSurface
{
	friend class SphericalHarmonicsEditor;
public:
	SphericalHarmonic(QOpenGLShaderProgram* prog, GLfloat radius, GLuint nSlices, GLuint nStacks);
	~SphericalHarmonic();

	virtual float firstUParameter() const;
	virtual float firstVParameter() const;
	virtual float lastUParameter() const ;
	virtual float lastVParameter() const ;
	virtual Point pointAtParameter(const float& u, const float& v);
	
private:
	GLfloat _radius;
	GLfloat _coeff1;
	GLfloat _coeff2;
	GLfloat _coeff3;
	GLfloat _coeff4;
	GLfloat _power1;
	GLfloat _power2;
	GLfloat _power3;
	GLfloat _power4;
};

