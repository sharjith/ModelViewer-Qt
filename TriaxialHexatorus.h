#pragma once

#include <ParametricSurface.h>

class Point;
class TriaxialHexatorus : public ParametricSurface
{
public:
	TriaxialHexatorus(QOpenGLShaderProgram* prog, float radius, unsigned int nSlices, unsigned int nStacks);
	~TriaxialHexatorus();

	virtual float firstUParameter() const;
	virtual float firstVParameter() const;
	virtual float lastUParameter() const ;
	virtual float lastVParameter() const ;
	virtual Point pointAtParameter(const float& u, const float& v);
	
private:
	float _radius;
};

