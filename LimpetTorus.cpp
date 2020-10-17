#include "LimpetTorus.h"
#include "Point.h"

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>
#include <glm/glm.hpp>

LimpetTorus::LimpetTorus(QOpenGLShaderProgram* prog, float radius, unsigned int nSlices, unsigned int nStacks, unsigned int sMax, unsigned int tMax) :
	ParametricSurface(prog, nSlices, nStacks, sMax, tMax),
	_radius(radius)
{
	setAutoIncrName("Limpet Torus");
	buildMesh();
}

LimpetTorus::~LimpetTorus()
{
}

TriangleMesh* LimpetTorus::clone()
{
	return new LimpetTorus(_prog, _radius, _slices, _stacks, _sMax, _tMax);
}

float LimpetTorus::firstUParameter() const
{
	return 0.0;
}

float LimpetTorus::lastUParameter() const
{
	return glm::two_pi<float>();
}

float LimpetTorus::firstVParameter() const
{
	return 0.0;
}

float LimpetTorus::lastVParameter() const
{
	return glm::two_pi<float>();
}

Point LimpetTorus::pointAtParameter(const float& u, const float& v)
{
	Point P;
	float x, y, z;

	//http://paulbourke.net/geometry/toroidal/
	// Limpet Torus
	x = _radius * sin(u) / (sqrt(2) + sin(v));
	y = _radius * cos(u) / (sqrt(2) + sin(v));
	z = _radius * 1 / (sqrt(2) + cos(v)) - _radius;

	P.setParam(x, y, z);
	return P;
}