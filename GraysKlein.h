#pragma once

#include <ParametricSurface.h>

class Point;
class GraysKlein : public ParametricSurface
{
public:
	GraysKlein(QOpenGLShaderProgram* prog, float radius, unsigned int nSlices, unsigned int nStacks);
	~GraysKlein();

	virtual float firstUParameter() const;
	virtual float firstVParameter() const;
	virtual float lastUParameter() const ;
	virtual float lastVParameter() const ;
	virtual Point pointAtParameter(const float& u, const float& v);

	float _A;
	float _M;
	float _N;
	
private:
	float _radius;
};

