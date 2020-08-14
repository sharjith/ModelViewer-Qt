#pragma once

#include <ParametricSurface.h>

class Point;
class Spring : public ParametricSurface
{
	friend class SpringEditor;
public:
	Spring(QOpenGLShaderProgram* prog, float sectionRadius, float coilRadius, float pitch, float turns, unsigned int nSlices, unsigned int nStacks);
	~Spring();

	virtual float firstUParameter() const;
	virtual float firstVParameter() const;
	virtual float lastUParameter() const ;
	virtual float lastVParameter() const ;
	virtual Point pointAtParameter(const float& u, const float& v);
	
private:
	float _sectionRadius;
	float _coilRadius;
	float _pitch;
	float _turns;
};

