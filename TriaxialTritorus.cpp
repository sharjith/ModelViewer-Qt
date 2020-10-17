#include "TriaxialTritorus.h"
#include "Point.h"

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>
#include <glm/glm.hpp>

TriaxialTritorus::TriaxialTritorus(QOpenGLShaderProgram* prog, float radius, unsigned int nSlices, unsigned int nStacks, unsigned int sMax, unsigned int tMax) :
	ParametricSurface(prog, nSlices, nStacks, sMax, tMax),
	_radius(radius)
{
	setName("Triaxial Tritorus");
	buildMesh();
}

TriaxialTritorus::~TriaxialTritorus()
{
}

TriangleMesh* TriaxialTritorus::clone()
{
	return new TriaxialTritorus(_prog, _radius, _slices, _stacks, _sMax, _tMax);
}

float TriaxialTritorus::firstUParameter() const
{
	return -glm::pi<float>();
}

float TriaxialTritorus::lastUParameter() const
{
	return glm::pi<float>();
}

float TriaxialTritorus::firstVParameter() const
{
	return -glm::pi<float>();;
}

float TriaxialTritorus::lastVParameter() const
{
	return glm::pi<float>();
}

Point TriaxialTritorus::pointAtParameter(const float& u, const float& v)
{
	Point P;
	float x, y, z;

	//http://paulbourke.net/geometry/toroidal/

	/*The Triaxial Tritorus is defined parametrically as
	Where
	-PI <= u <= PI and -PI <= v <= PI
	*/
	x = _radius * sin(u) * (1 + cos(v));
	y = _radius * sin(u + 2 * glm::pi<float>() / 3) * (1 + cos(v + 2 * glm::pi<float>() / 3));
	z = _radius * sin(u + 4 * glm::pi<float>() / 3) * (1 + cos(v + 4 * glm::pi<float>() / 3));

	P.setParam(x, y, z);
	return P;
}