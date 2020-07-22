#include "KleinBottle.h"
#include "Point.h"

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>
#include <glm/glm.hpp>


KleinBottle::KleinBottle(QOpenGLShaderProgram* prog, GLfloat radius, GLuint nSlices, GLuint nStacks) :
	ParametricSurface(prog, nSlices, nStacks),
	_radius(radius)
{
	_name = "Klein Bottle";
	buildMesh(nSlices, nStacks);
}


KleinBottle::~KleinBottle()
{
}

float KleinBottle::firstUParameter() const
{
	return 0.0;
}

float KleinBottle::lastUParameter() const
{
	return glm::two_pi<float>();
}

float KleinBottle::firstVParameter() const
{
	return 0.0;
}

float KleinBottle::lastVParameter() const
{
	return glm::two_pi<float>();
}

Point KleinBottle::pointAtParameter(const float& u, const float& v)
{
	Point P;
	float x, y, z;

	//http://paulbourke.net/geometry/toroidal/
	// Klein Bottle
	// Where u = 0 - 2PI and v = 0 - 2PI
	float r = 4 * (1 - cos(u) / 2);
	if(u >= 0 && u < glm::pi<float>())
		x = -_radius / 6 * (6 * cos(u)*(1 + sin(u)) + r * cos(u)*cos(v));
	else
		x = -_radius / 6 * (6 * cos(u)*(1 + sin(u)) + r * cos(v + glm::pi<float>()));

	if (u >= 0 && u < glm::pi<float>())
		z = -_radius / 6 * (16 * sin(u) + r * sin(u)*cos(v));
	else
		z = -_radius / 6 * (16 * sin(u));

	y = -_radius / 6 * (r * sin(v));
	

	P.setParam(x, y, z);
	return P;
}
