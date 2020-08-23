#pragma once

#include <ParametricSurface.h>

class Point;
class SuperEllipsoid : public ParametricSurface
{
	friend class SuperEllipsoidEditor;
public:
    SuperEllipsoid(QOpenGLShaderProgram* prog, float radius, float scaleX, float scaleY, float scaleZ, float sinPower, float cosPower, unsigned int nSlices, unsigned int nStacks, unsigned int sMax = 1, unsigned int tMax = 1);
	~SuperEllipsoid();

	virtual float firstUParameter() const;
	virtual float firstVParameter() const;
	virtual float lastUParameter() const;
	virtual float lastVParameter() const;
	virtual Point pointAtParameter(const float& u, const float& v);

private:
	float _radius;
	float _scaleX;
	float _scaleY;
	float _scaleZ;
	float _n1;
	float _n2;
};
