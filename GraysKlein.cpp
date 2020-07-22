#include "GraysKlein.h"
#include "Point.h"

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>
#include <glm/glm.hpp>


GraysKlein::GraysKlein(QOpenGLShaderProgram* prog, GLfloat radius, GLuint nSlices, GLuint nStacks) :
	ParametricSurface(prog, nSlices, nStacks),	
	_A(2),
	_M(1),
    _N(2),
    _radius(radius)
{
	_name = "Gray's Klein Bottle";
	buildMesh(nSlices, nStacks);
}


GraysKlein::~GraysKlein()
{
}

float GraysKlein::firstUParameter() const
{
	return 0.0;
}

float GraysKlein::lastUParameter() const
{
	return 4 * glm::pi<float>();
}

float GraysKlein::firstVParameter() const
{
	return 0.0;
}

float GraysKlein::lastVParameter() const
{
	return glm::two_pi<float>();
}

Point GraysKlein::pointAtParameter(const float& u, const float& v)
{
	Point P;
	float x, y, z;

	// Gray's Klein bottle
	//0 <= u <= 4 PI
	//0 <= v <= 2 PI
	//A = 2, N = 2, M = 1	
	x = _radius * (_A + cos(_N*u / 2.0) * sin(v) - sin(_N*u / 2.0) * sin(2 * v)) * cos(_M*u / 2.0);
	y = _radius * (_A + cos(_N*u / 2.0) * sin(v) - sin(_N*u / 2.0) * sin(2 * v)) * sin(_M*u / 2.0);
	z = _radius * sin(_N*u / 2.0) * sin(v) + cos(_N*u / 2.0) * sin(2 * v);

	P.setParam(x, y, z);
	return P;
}
