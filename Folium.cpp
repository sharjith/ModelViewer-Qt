#include "Folium.h"
#include "Point.h"

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>
#include <glm/glm.hpp>


Folium::Folium(QOpenGLShaderProgram* prog, GLfloat radius, GLuint nSlices, GLuint nStacks) :
	ParametricSurface(prog, nSlices, nStacks),
	_radius(radius)
{
	_name = "Folium";
	buildMesh(nSlices, nStacks);
}


Folium::~Folium()
{
}

float Folium::firstUParameter() const
{
	return -glm::pi<float>();
}

float Folium::lastUParameter() const
{
	return glm::pi<float>();
}

float Folium::firstVParameter() const
{
	return -glm::pi<float>();
}

float Folium::lastVParameter() const
{
	return glm::pi<float>();
}

Point Folium::pointAtParameter(const float& u, const float& v)
{
	Point P;
	float x, y, z;

	
	// Folium
	// Where -pi <= u <= pi -pi <= v <= pi
	x = _radius * (cos(u) * (2 * v / glm::pi<float>() - tanh(v)));
	y = _radius * (cos(u + 2 * glm::pi<float>() / 3) / cosh(v));
	z = _radius * (cos(u - 2 * glm::pi<float>() / 3) / cosh(v));

	
	P.setParam(x, y, z);
	return P;
}
