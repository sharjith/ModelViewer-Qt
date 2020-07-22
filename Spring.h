#pragma once

#include <ParametricSurface.h>

class Point;
class Spring : public ParametricSurface
{
	friend class SpringEditor;
public:
	Spring(QOpenGLShaderProgram* prog, GLfloat sectionRadius, GLfloat coilRadius, GLfloat pitch, GLfloat turns, GLuint nSlices, GLuint nStacks);
	~Spring();

	virtual float firstUParameter() const;
	virtual float firstVParameter() const;
	virtual float lastUParameter() const ;
	virtual float lastVParameter() const ;
	virtual Point pointAtParameter(const float& u, const float& v);
	
private:
	GLfloat _sectionRadius;
	GLfloat _coilRadius;
	GLfloat _pitch;
	GLfloat _turns;
};

