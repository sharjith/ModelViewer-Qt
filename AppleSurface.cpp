#include "AppleSurface.h"
#include "Point.h"

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>
#include <glm/glm.hpp>


AppleSurface::AppleSurface(QOpenGLShaderProgram* prog, GLfloat radius, GLuint nSlices, GLuint nStacks) :
	ParametricSurface(prog, nSlices, nStacks),
	_radius(radius)
{
	_name = "Apple Surface";
	buildMesh(nSlices, nStacks);
}


AppleSurface::~AppleSurface()
{
}

float AppleSurface::firstUParameter() const
{
	return 0.0;
}

float AppleSurface::lastUParameter() const
{
	return glm::two_pi<float>();
}

float AppleSurface::firstVParameter() const
{
	return -glm::pi<float>();
}

float AppleSurface::lastVParameter() const
{
	return glm::pi<float>();
}

Point AppleSurface::pointAtParameter(const float& u, const float& v)
{
	Point P;
	float x, y, z;

	//http://paulbourke.net/geometry/toroidal/
	// Apple Surface
	// Where 0 <= u <= 2 pi, -pi <= v <= pi
	x = _radius * (cos(u) * (4 + 3.8 * cos(v)));
	y = _radius * (sin(u) * (4 + 3.8 * cos(v)));
	z = _radius * ((cos(v) + sin(v) - 1) * (1 + sin(v)) * log(1 - glm::pi<float>() * v / 10) + 7.5 * sin(v));
		

	P.setParam(x, y, z);
	return P;
}
