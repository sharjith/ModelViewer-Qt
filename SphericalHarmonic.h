#pragma once

#include <ParametricSurface.h>

class Point;
class SphericalHarmonic : public ParametricSurface
{
	friend class SphericalHarmonicsEditor;
public:
	SphericalHarmonic(QOpenGLShaderProgram* prog, float radius, unsigned int nSlices, unsigned int nStacks, unsigned int sMax = 1, unsigned int tMax = 1);
	~SphericalHarmonic();

	virtual float firstUParameter() const;
	virtual float firstVParameter() const;
	virtual float lastUParameter() const;
	virtual float lastVParameter() const;
	virtual Point pointAtParameter(const float& u, const float& v);

private:
	float _radius;
	float _coeff1;
	float _coeff2;
	float _coeff3;
	float _coeff4;
	float _power1;
	float _power2;
	float _power3;
	float _power4;
};
