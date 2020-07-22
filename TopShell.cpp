#include "TopShell.h"
#include "Point.h"

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>
#include <glm/glm.hpp>


TopShell::TopShell(QOpenGLShaderProgram* prog, Point center, GLfloat radius, GLuint nSlices, GLuint nStacks) :
	ParametricSurface(prog, nSlices, nStacks),
	_radius(radius),
	_center(center)
{
	_name = "Top Sea Shell";
	buildMesh(nSlices, nStacks);
}


TopShell::~TopShell()
{
}

float TopShell::firstUParameter() const
{
	return 0.0;
}

float TopShell::lastUParameter() const
{
	return glm::two_pi<float>();
}

float TopShell::firstVParameter() const
{
	return 0.0;
}

float TopShell::lastVParameter() const
{
	return glm::two_pi<float>();
}

Point TopShell::pointAtParameter(const float& u, const float& v)
{
	Point P;
	float x, y, z;

	// http://xahlee.info/SpecialPlaneCurves_dir/Seashell_dir/seashell_math_formulas.html
	// Top
	float R = 1;    // radius of tube
	float N = 7.6f;  // number of turns
	float H = 2.5f;  // height
	float p = 1.3f;  // power
	auto W = [R](auto u) {return u / (2 * glm::pi<float>())*R; };
	x = _center.getX() + (_radius * (W(u)*cos(N*u)*(1 + cos(v))));
	y = _center.getY() + (_radius * (W(u)*sin(N*u)*(1 + cos(v))));
	z = _center.getZ() + (_radius * (W(u)*sin(v) + H * pow((u / (2 * glm::pi<float>())), p)) - _radius * 1.75);

	P.setParam(x, y, z);	
	return P;
}
