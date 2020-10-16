#pragma once

#include <ParametricSurface.h>
#include <Point.h>

class TopShell : public ParametricSurface
{
public:
	TopShell(QOpenGLShaderProgram* prog, Point center, float radius, unsigned int nSlices, unsigned int nStacks, unsigned int sMax = 1, unsigned int tMax = 1);
	~TopShell();

	virtual TriangleMesh* clone();

	virtual float firstUParameter() const;
	virtual float firstVParameter() const;
	virtual float lastUParameter() const;
	virtual float lastVParameter() const;
	virtual Point pointAtParameter(const float& u, const float& v);

private:
	float _radius;
	Point _center;
};
