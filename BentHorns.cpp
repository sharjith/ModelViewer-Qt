#include "BentHorns.h"
#include "Point.h"

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>
#include <glm/glm.hpp>

BentHorns::BentHorns(QOpenGLShaderProgram* prog, float radius, unsigned int nSlices, unsigned int nStacks, unsigned int sMax, unsigned int tMax) :
	ParametricSurface(prog, nSlices, nStacks, sMax, tMax),
	_radius(radius)
{
	setAutoIncrName("Bent Horns");
	buildMesh();
}

BentHorns::~BentHorns()
{
}

TriangleMesh* BentHorns::clone()
{
	return new BentHorns(_prog, _radius, _slices, _stacks, _sMax, _tMax);
}

float BentHorns::firstUParameter() const
{
	return -glm::pi<float>();
}

float BentHorns::lastUParameter() const
{
	return glm::pi<float>();
}

float BentHorns::firstVParameter() const
{
	return -glm::two_pi<float>();
}

float BentHorns::lastVParameter() const
{
	return glm::two_pi<float>();
}

Point BentHorns::pointAtParameter(const float& u, const float& v)
{
	Point P;
	float x, y, z;

	//http://paulbourke.net/geometry/toroidal/
	// Bent Horns
	// Where  -pi <= u <= pi	  - 2pi <= v <= 2pi
	x = _radius * (2.0f + cos(u)) * (v / 3.0f - sin(v));
	y = _radius * (2.0f + cos(u - 2.0f * glm::pi<float>() / 3.0f)) * (cos(v) - 1.0f);
	z = _radius * (2.0f + cos(u + 2.0f * glm::pi<float>() / 3.0f)) * (cos(v) - 1.0f) + _radius * 2.0f;

	P.setParam(x, y, z);
	return P;
}