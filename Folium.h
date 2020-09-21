#pragma once

#include <ParametricSurface.h>

class Point;
class Folium : public ParametricSurface
{
public:
	Folium(QOpenGLShaderProgram* prog, float radius, unsigned int nSlices, unsigned int nStacks, unsigned int sMax = 1, unsigned int tMax = 1);
	~Folium();

	virtual float firstUParameter() const;
	virtual float firstVParameter() const;
	virtual float lastUParameter() const;
	virtual float lastVParameter() const;
	virtual Point pointAtParameter(const float& u, const float& v);

private:
	float _radius;
};
