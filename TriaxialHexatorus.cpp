#include "TriaxialHexatorus.h"
#include "Point.h"

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>
#include <glm/glm.hpp>

TriaxialHexatorus::TriaxialHexatorus(QOpenGLShaderProgram* prog, float radius, unsigned int nSlices, unsigned int nStacks, unsigned int sMax, unsigned int tMax) :
	ParametricSurface(prog, nSlices, nStacks, sMax, tMax),
	_radius(radius)
{
	_name = "Triaxial Hexatorus";
	buildMesh();
}

TriaxialHexatorus::~TriaxialHexatorus()
{
}

float TriaxialHexatorus::firstUParameter() const
{
	return -glm::pi<float>();
}

float TriaxialHexatorus::lastUParameter() const
{
	return glm::pi<float>();
}

float TriaxialHexatorus::firstVParameter() const
{
	return -glm::pi<float>();;
}

float TriaxialHexatorus::lastVParameter() const
{
	return glm::pi<float>();
}

Point TriaxialHexatorus::pointAtParameter(const float& u, const float& v)
{
	Point P;
	float x, y, z;

	//http://paulbourke.net/geometry/toroidal/
	/*Triaxial Hexatorus
	Where
	-PI <= u <= PI and -PI <= v <= PI
	*/
	x = _radius * sin(u) / (sqrt(2) + cos(v));
	y = _radius * sin(u + 2 * glm::pi<float>() / 3) / (sqrt(2) + cos(v + 2 * glm::pi<float>() / 3));
	z = _radius * cos(u - 2 * glm::pi<float>() / 3) / (sqrt(2) + cos(v - 2 * glm::pi<float>() / 3));

	P.setParam(x, y, z);
	return P;
}
