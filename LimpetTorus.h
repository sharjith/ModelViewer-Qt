#pragma once

#include <ParametricSurface.h>

class Point;
class LimpetTorus : public ParametricSurface
{
public:
	LimpetTorus(QOpenGLShaderProgram* prog, float radius, unsigned int nSlices, unsigned int nStacks);
	~LimpetTorus();

	virtual float firstUParameter() const;
	virtual float firstVParameter() const;
	virtual float lastUParameter() const;
	virtual float lastVParameter() const;
	virtual Point pointAtParameter(const float& u, const float& v);

private:
	float _radius;
};
