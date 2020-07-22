#include "SphericalHarmonic.h"
#include "Point.h"

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>
#include <glm/glm.hpp>


SphericalHarmonic::SphericalHarmonic(QOpenGLShaderProgram* prog, GLfloat radius, GLuint nSlices, GLuint nStacks) :
	ParametricSurface(prog, nSlices, nStacks),
	_radius(radius)
{
	_name = "Spherical Harmonics";

	_coeff1 = 0.0f;
	_coeff2 = 0.0f;
	_coeff3 = 1.0f;
	_coeff4 = 1.0f;
	_power1 = 3.0f;
	_power2 = 3.0f;
	_power3 = 3.0f;
	_power4 = 4.0f;

	buildMesh(nSlices, nStacks);
}


SphericalHarmonic::~SphericalHarmonic()
{
}

float SphericalHarmonic::firstUParameter() const
{
	return 0.0;
}

float SphericalHarmonic::lastUParameter() const
{
	return glm::two_pi<float>();
}

float SphericalHarmonic::firstVParameter() const
{
	return -glm::pi<float>();
}

float SphericalHarmonic::lastVParameter() const
{
	return glm::pi<float>();
}

Point SphericalHarmonic::pointAtParameter(const float& u, const float& v)
{
	Point P;
	float x, y, z;

	//http://paulbourke.net/geometry/toroidal/
		// Spherical Harmonics
	//int m[] = { 1, 3, 1, 3, 2, 3, 1, 4 };
	double r = 0.0;
	r += pow(sin(_coeff1 * v), _power1);
	r += pow(cos(_coeff2 * v), _power2);
	r += pow(sin(_coeff3 * u), _power3);
	r += pow(cos(_coeff4 * u), _power4);

	x = _radius * r * sin(v) * cos(u);
	y = _radius * r * cos(v);
	z = _radius * r * sin(v) * sin(u);

	P.setParam(x, y, z);
	return P;
}
