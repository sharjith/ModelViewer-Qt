#include "Horn.h"
#include "Point.h"

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>
#include <glm/glm.hpp>

Horn::Horn(QOpenGLShaderProgram* prog, float radius, unsigned int nSlices, unsigned int nStacks, unsigned int sMax, unsigned int tMax) :
	ParametricSurface(prog, nSlices, nStacks, sMax, tMax),
	_radius(radius)
{
	_name = "Horn";
	buildMesh();
}

Horn::~Horn()
{
}

float Horn::firstUParameter() const
{
	return 0.0;
}

float Horn::lastUParameter() const
{
	return 1.0f;
}

float Horn::firstVParameter() const
{
	return 0.0;
}

float Horn::lastVParameter() const
{
	return glm::two_pi<float>();
}

Point Horn::pointAtParameter(const float& u, const float& v)
{
	Point P;
	float x, y, z;

	//http://paulbourke.net/geometry/spiral/
	/*Horn
	Where 0 <= u <= 1, 0 <= v <= 2pi
	*/
	x = _radius * (2 + u * cos(v)) * sin(2 * glm::pi<float>() * u);
	y = _radius * (2 + u * cos(v)) * cos(2 * glm::pi<float>() * u) + 2 * u;
	z = _radius * u * sin(v);

	P.setParam(x, y, z);
	return P;
}
