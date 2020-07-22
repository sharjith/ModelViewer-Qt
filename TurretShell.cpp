#include "TurretShell.h"
#include "Point.h"

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>
#include <glm/glm.hpp>


TurretShell::TurretShell(QOpenGLShaderProgram* prog, GLfloat radius, GLuint nSlices, GLuint nStacks) :
	ParametricSurface(prog, nSlices, nStacks),
	_radius(radius)
{
	_name = "Turret Shell";
	buildMesh(nSlices, nStacks);
}


TurretShell::~TurretShell()
{
}

float TurretShell::firstUParameter() const
{
	return 0.0;
}

float TurretShell::lastUParameter() const
{
	return glm::two_pi<float>();
}

float TurretShell::firstVParameter() const
{
	return 0.0;
}

float TurretShell::lastVParameter() const
{
	return glm::two_pi<float>();
}

Point TurretShell::pointAtParameter(const float& u, const float& v)
{
	Point point;
	float x, y, z;

	// http://xahlee.info/SpecialPlaneCurves_dir/Seashell_dir/seashell_math_formulas.html
	float R = 1;    // radius of tube
	float N = 9.6f;  // number of turns
	float H = 5.0;  // height
	float P = 1.5;  // power
	float P1 = 1.1f; // another power
	float T = 0.8f;  // Triangleness of cross section
	float A = 0.1f;  // Angle of tilt of cross section (radians)
	float S = 1.5;  // Stretch
	
	auto W = [R, P1](auto u) {return pow((u / (2 * glm::pi<float>())*R), P1); };

	x = _radius * (W(u)*cos(N*u)*(1 + cos(v + A) + sin(2 * v + A)*T / 4));
	y = _radius * (W(u)*sin(N*u)*(1 + cos(v + A) + sin(2 * v + A)*T / 4));
	z = _radius * (S * W(u)*(sin(v + A) + cos(2 * v + A)*T / 4) + S * H* pow((u / (2 * glm::pi<float>())), P)) -(_radius*4.5);
	

	point.setParam(x, y, z);
	return point;
}
