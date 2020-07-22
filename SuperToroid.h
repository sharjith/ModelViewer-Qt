#pragma once

#include <ParametricSurface.h>

class Point;
class SuperToroid : public ParametricSurface
{
	friend class SuperToroidEditor;
public:
	SuperToroid(QOpenGLShaderProgram* prog, GLfloat outerRadius, GLfloat innerRadius, GLfloat sinPower, GLfloat cosPower, GLuint nSlices, GLuint nStacks);
	~SuperToroid();

	virtual float firstUParameter() const;
	virtual float firstVParameter() const;
	virtual float lastUParameter() const ;
	virtual float lastVParameter() const ;
	virtual Point pointAtParameter(const float& u, const float& v);
	
private:
	GLfloat _outerRadius;
	GLfloat _innerRadius;
	GLfloat _n1;
	GLfloat _n2;
};

