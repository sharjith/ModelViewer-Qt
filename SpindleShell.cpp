#include "SpindleShell.h"
#include "Point.h"

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>
#include <glm/glm.hpp>

SpindleShell::SpindleShell(QOpenGLShaderProgram* prog, float radius, unsigned int nSlices, unsigned int nStacks, unsigned int sMax, unsigned int tMax) :
	ParametricSurface(prog, nSlices, nStacks, sMax, tMax),
	_radius(radius)
{
	setName("Spindle Sea Shell");
	buildMesh();
}

SpindleShell::~SpindleShell()
{
}

TriangleMesh* SpindleShell::clone()
{
	return new SpindleShell(_prog, _radius, _slices, _stacks, _sMax, _tMax);
}

float SpindleShell::firstUParameter() const
{
	return 0.0;
}

float SpindleShell::lastUParameter() const
{
	return glm::two_pi<float>();
}

float SpindleShell::firstVParameter() const
{
	return 0.0;
}

float SpindleShell::lastVParameter() const
{
	return glm::two_pi<float>();
}

Point SpindleShell::pointAtParameter(const float& u, const float& v)
{
	Point P;
	float x, y, z;

	// http://xahlee.info/SpecialPlaneCurves_dir/Seashell_dir/seashell_math_formulas.html
	// Spindle shell
	float R = 1;    // radius of tube
	float N = 3.6f;  // number of turns
	float H = 2.5f;  // height
	float p = 1.4f;  // power
	float L = 4;    // Controls spike length
	float K = 9;    // Controls spike sharpness
	auto W = [R](auto u) { return u / pow(((2 * glm::pi<float>()) * R), 0.9); };
	x = _radius * (W(u) * cos(N * u) * (1 + cos(v)));
	y = _radius * (W(u) * sin(N * u) * (1 + cos(v)));
	z = _radius * (W(u) * (sin(v) + L * pow((sin(v / 2)), K) + H * pow((u / (2 * glm::pi<float>()) * R), p))) - _radius * 3.8;

	P.setParam(x, y, z);
	return P;
}