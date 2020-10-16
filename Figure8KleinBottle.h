#pragma once
#include "ParametricSurface.h"
class Figure8KleinBottle :
	public ParametricSurface
{
public:
	Figure8KleinBottle(QOpenGLShaderProgram* prog, float radius, unsigned int nSlices, unsigned int nStacks, unsigned int sMax = 1, unsigned int tMax = 1);
	~Figure8KleinBottle();

	virtual TriangleMesh* clone();

	virtual float firstUParameter() const;
	virtual float firstVParameter() const;
	virtual float lastUParameter() const;
	virtual float lastVParameter() const;
	virtual Point pointAtParameter(const float& u, const float& v);

private:
	float _radius;
};
