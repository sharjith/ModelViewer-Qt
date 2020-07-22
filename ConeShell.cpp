#include "ConeShell.h"
#include "Point.h"

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>
#include <glm/glm.hpp>


ConeShell::ConeShell(QOpenGLShaderProgram* prog, GLfloat radius, GLuint nSlices, GLuint nStacks) :
	ParametricSurface(prog, nSlices, nStacks),
	_radius(radius)
{
	_name = "Cone Sea Shell";
	buildMesh(nSlices, nStacks);
}


ConeShell::~ConeShell()
{
}

float ConeShell::firstUParameter() const
{
	return 0.0;
}

float ConeShell::lastUParameter() const
{
	return glm::two_pi<float>();
}

float ConeShell::firstVParameter() const
{
	return 0.0;
}

float ConeShell::lastVParameter() const
{
	return glm::two_pi<float>();
}

Point ConeShell::pointAtParameter(const float& u, const float& v)
{
	Point P;
	float x, y, z;

	
	// http://xahlee.info/SpecialPlaneCurves_dir/Seashell_dir/seashell_math_formulas.html
	// Cone Shell
	
	float R = 1;    // radius of tube
	float N = 4.6f;  // number of turns
	float H = 0.5f;  // height
	float p = 2;    // power
	auto W = [&R](auto u) { return  u / (2 * glm::pi<float>())*R; };
	x = _radius * (W(u)*cos(N*u)*(1 + cos(v)));
	y = _radius * (W(u)*sin(N*u)*(1 + cos(v)));
	z = _radius * (W(u)*sin(v)*1.25 + H * pow((u / (2 * glm::pi<float>())), p) + W(u)*cos(v)*1.25) - _radius / 2;

	P.setParam(x, y, z);
	return P;
}
