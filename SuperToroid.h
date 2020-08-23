#pragma once

#include <ParametricSurface.h>

class Point;
class SuperToroid : public ParametricSurface
{
	friend class SuperToroidEditor;
public:
    SuperToroid(QOpenGLShaderProgram* prog, float outerRadius, float innerRadius, float sinPower, float cosPower, unsigned int nSlices, unsigned int nStacks, unsigned int sMax = 1, unsigned int tMax = 1);
	~SuperToroid();

	virtual float firstUParameter() const;
	virtual float firstVParameter() const;
	virtual float lastUParameter() const;
	virtual float lastVParameter() const;
	virtual Point pointAtParameter(const float& u, const float& v);

private:
	float _outerRadius;
	float _innerRadius;
	float _n1;
	float _n2;
};
