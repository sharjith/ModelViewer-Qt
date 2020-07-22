#include "LimpetTorus.h"
#include "Point.h"

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>
#include <glm/glm.hpp>


LimpetTorus::LimpetTorus(QOpenGLShaderProgram* prog, GLfloat radius, GLuint nSlices, GLuint nStacks) :
	ParametricSurface(prog, nSlices, nStacks),
	_radius(radius)
{
	_name = "Limpet Torus";
	buildMesh(nSlices, nStacks);
}


LimpetTorus::~LimpetTorus()
{
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
	y = _radius * sin(u) / (sqrt(2) + sin(v));
	x = _radius * cos(u) / (sqrt(2) + sin(v));
	z = _radius * 1 / (sqrt(2) + cos(v)) - _radius;

	P.setParam(x, y, z);
	return P;
}
