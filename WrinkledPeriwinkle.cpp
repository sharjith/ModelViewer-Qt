#include "WrinkledPeriwinkle.h"
#include "Point.h"

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>
#include <glm/glm.hpp>


WrinkledPeriwinkle::WrinkledPeriwinkle(QOpenGLShaderProgram* prog, GLfloat radius, GLuint nSlices, GLuint nStacks) :
	ParametricSurface(prog, nSlices, nStacks),
	_radius(radius)
{
	_name = "Wrinkled Periwinkle";
	buildMesh(nSlices, nStacks);
}


WrinkledPeriwinkle::~WrinkledPeriwinkle()
{
}

float WrinkledPeriwinkle::firstUParameter() const
{
	return 0.0;
}

float WrinkledPeriwinkle::lastUParameter() const
{
	return glm::two_pi<float>();
}

float WrinkledPeriwinkle::firstVParameter() const
{
	return 0.0;
}

float WrinkledPeriwinkle::lastVParameter() const
{
	return glm::two_pi<float>();
}

Point WrinkledPeriwinkle::pointAtParameter(const float& u, const float& v)
{
	Point P;
	float x, y, z;

	// http://xahlee.info/SpecialPlaneCurves_dir/Seashell_dir/seashell_math_formulas.html
	// Wrinkled periwinkle
	float R = 1;    // radius of tube
	float N = 4.6f;  // number of turns
	float H = 2.5f;  // height
	float F = 80;   // wave frequency
	float A = 0.2f;  // wave amplitude
	float p = 1.9f;  // power
	auto W = [R](auto u) {return u / (2 * glm::pi<float>())*R; };
	x = _radius * W(u)*cos(N*u)*(1 + cos(v) + cos(F*u)*A);
	y = _radius * W(u)*sin(N*u)*(1 + cos(v) + cos(F*u)*A);
	z = _radius * W(u)*sin(v) + H * pow((u / (2 * glm::pi<float>())), p);

	P.setParam(x, y, z);
	return P;
}
