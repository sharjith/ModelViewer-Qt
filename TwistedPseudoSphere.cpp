#include "TwistedPseudoSphere.h"
#include "Point.h"

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>
#include <glm/glm.hpp>

#include <limits>

TwistedPseudoSphere::TwistedPseudoSphere(QOpenGLShaderProgram* prog, GLfloat radius, GLuint nSlices, GLuint nStacks) :
	ParametricSurface(prog, nSlices, nStacks),
	_radius(radius)
{
	_name = "Twisted Pseudo Sphere";
	buildMesh(nSlices, nStacks);
}


TwistedPseudoSphere::~TwistedPseudoSphere()
{
}

float TwistedPseudoSphere::firstUParameter() const
{
	return 0.0f;
}

float TwistedPseudoSphere::lastUParameter() const
{
	return 2 * glm::two_pi<float>();
}

float TwistedPseudoSphere::firstVParameter() const
{
	return 0.1f;
}

float TwistedPseudoSphere::lastVParameter() const
{
	return 1.0f;
}

#include <iostream>
Point TwistedPseudoSphere::pointAtParameter(const float& u, const float& v)
{
	Point P;
	float x, y, z;

	//http://paulbourke.net/geometry/toroidal/
	//Dini's Surface or Twisted Pseudo-sphere
	// Where 0 <= u, 0 < v
	
	float b = 6;
	x = _radius * cos(u) * sin(v);
	y = _radius * sin(u) * sin(v);
	z = _radius * (cos(v) + log(tan(v / 2))) + b * u + _radius/3;
	
	P.setParam(x, y, z);
	return P;
}
