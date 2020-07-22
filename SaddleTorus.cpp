#include "SaddleTorus.h"
#include "Point.h"

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>
#include <glm/glm.hpp>


SaddleTorus::SaddleTorus(QOpenGLShaderProgram* prog, GLfloat radius, GLuint nSlices, GLuint nStacks) :
	ParametricSurface(prog, nSlices, nStacks),
	_radius(radius)
{
	_name = "Saddle Torus";
	buildMesh(nSlices, nStacks);
}


SaddleTorus::~SaddleTorus()
{
}

float SaddleTorus::firstUParameter() const
{
	return 0.0;
}

float SaddleTorus::lastUParameter() const
{
	return glm::two_pi<float>();
}

float SaddleTorus::firstVParameter() const
{
	return 0.0;
}

float SaddleTorus::lastVParameter() const
{
	return glm::two_pi<float>();
}

Point SaddleTorus::pointAtParameter(const float& u, const float& v)
{
	Point P;
	float x, y, z;

	
	//Saddle torus
	// Where F(s) = 1 - cos2(s) - cos2(s + 2 PI / 3)
	//	0 <= u <= 2 PI, 0 <= v <= 2 PI
	auto sign = [](auto a) { if (a == 0) return 0;  else if (a < 0) return -1; else return 1; };
	auto F = [](auto s) {return 1 - pow(cos(s), 2) - pow(cos(s + 2 * glm::pi<float>() / 3), 2); };
	x = _radius * (2 + cos(u))* cos(v);
	y = _radius * (2 + cos(u + 2 * glm::pi<float>() / 3)) * cos(v + 2 * glm::pi<float>() / 3);
	z = _radius * (2 + sign(F(u)) * sqrt(abs(F(u)))) * sign(F(v)) * sqrt(abs(F(v)));

	P.setParam(x, y, z);
	return P;
}
