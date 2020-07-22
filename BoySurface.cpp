#include "BoySurface.h"
#include "Point.h"

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>
#include <glm/glm.hpp>


BoySurface::BoySurface(QOpenGLShaderProgram* prog, GLfloat radius, GLuint nSlices, GLuint nStacks) :
	ParametricSurface(prog, nSlices, nStacks), 
	_radius(radius)
{
	_name = "Boy's Surface";
	buildMesh(nSlices, nStacks);
}


BoySurface::~BoySurface()
{
}

float BoySurface::firstUParameter() const
{
	return 0.0;
}

float BoySurface::lastUParameter() const
{
	return glm::pi<float>();
}

float BoySurface::firstVParameter() const
{
	return 0.0;
}

float BoySurface::lastVParameter() const
{
	return glm::pi<float>();
}

Point BoySurface::pointAtParameter(const float& u, const float& v)
{
	Point P;
	float x, y, z;

	//Boy Surface
	// Where A = 2/3 and B = sqrt(2)
	// and 0 <= u, v <= pi
	float A = 2.0f / 3.0f;
	float B = sqrt(2.0f);
	x = _radius * (A * ((cos(u) *cos(2 * v) + B * sin(u)* cos(v))* cos(u)) / (B - sin(2 * u)* sin(3 * v)));
	y = _radius * (A * ((cos(u) *sin(2 * v) - B * sin(u) *sin(v)) *cos(u)) / (B - sin(2 * u) *sin(3 * v)));
	z = _radius * (B * (cos(u) *cos(u)) / (B - sin(2 * u) *sin(3 * v))) - _radius;

	P.setParam(x, y, z);
	return P;
}
