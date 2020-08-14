#include "BowTie.h"
#include "Point.h"

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>
#include <glm/glm.hpp>

BowTie::BowTie(QOpenGLShaderProgram *prog, float radius, unsigned int nSlices, unsigned int nStacks) : ParametricSurface(prog, nSlices, nStacks),
																							 _radius(radius)
{
	_name = "Bow Tie";
	buildMesh();
}

BowTie::~BowTie()
{
}

float BowTie::firstUParameter() const
{
	return 0.0;
}

float BowTie::lastUParameter() const
{
	return glm::two_pi<float>();
}

float BowTie::firstVParameter() const
{
	return 0.0;
}

float BowTie::lastVParameter() const
{
	return glm::two_pi<float>();
}

Point BowTie::pointAtParameter(const float &u, const float &v)
{
	Point P;
	float x, y, z;

	//http://paulbourke.net/geometry/toroidal/
	// Bow Tie
	x = _radius * sin(u) / (sqrt(2) + cos(v));
	y = _radius * sin(u) / (sqrt(2) + sin(v));
	z = _radius * cos(u) / (1 + sqrt(2));

	P.setParam(x, y, z);
	return P;
}
