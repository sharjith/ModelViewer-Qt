#include "Figure8KleinBottle.h"
#include "Point.h"

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>
#include <glm/glm.hpp>


Figure8KleinBottle::Figure8KleinBottle(QOpenGLShaderProgram* prog, GLfloat radius, GLuint nSlices, GLuint nStacks) :
	ParametricSurface(prog, nSlices, nStacks),
	_radius(radius)
{
	_name = "Figure 8 Klein Bottle";
	buildMesh(nSlices, nStacks);
}


Figure8KleinBottle::~Figure8KleinBottle()
{
}

float Figure8KleinBottle::firstUParameter() const
{
	return 0.0;
}

float Figure8KleinBottle::lastUParameter() const
{
	return glm::two_pi<float>();
}

float Figure8KleinBottle::firstVParameter() const
{
	return 0.0;
}

float Figure8KleinBottle::lastVParameter() const
{
	return glm::two_pi<float>();
}

Point Figure8KleinBottle::pointAtParameter(const float& u, const float& v)
{
	Point P;
	float x, y, z;

	//http://paulbourke.net/geometry/toroidal/	
	// Figure-8 Klein bottle
	// Where u = 0 - 2PI and v = 0 - 2PI
	x = _radius * (2 + cos(v / 2)* sin(u) - sin(v / 2)* sin(2 * u))* cos(v);
	y = _radius * (2 + cos(v / 2)* sin(u) - sin(v / 2)* sin(2 * u))* sin(v);
	z = _radius * (sin(v / 2)* sin(u) + cos(v / 2) *sin(2 * u));


	P.setParam(x, y, z);
	return P;
}
