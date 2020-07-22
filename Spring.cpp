#include "Spring.h"
#include "Point.h"

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>
#include <glm/glm.hpp>


Spring::Spring(QOpenGLShaderProgram* prog, GLfloat sectionRadius, GLfloat coilRadius, GLfloat pitch, GLfloat turns, GLuint nSlices, GLuint nStacks) :
	ParametricSurface(prog, nSlices, nStacks),
	_sectionRadius(sectionRadius),
	_coilRadius(coilRadius),
	_pitch(pitch),
	_turns(turns)
{
	_name = "Spring";
	buildMesh(nSlices, nStacks);
}


Spring::~Spring()
{
}

float Spring::firstUParameter() const
{
	return 0.0;
}

float Spring::lastUParameter() const
{
	return _turns * glm::two_pi<float>();
}

float Spring::firstVParameter() const
{
	return 0.0;
}

float Spring::lastVParameter() const
{
	return glm::two_pi<float>();
}

Point Spring::pointAtParameter(const float& u, const float& v)
{
	Point P;
	float x, y, z;

	//http://paulbourke.net/geometry/spiral/
	// Spring

	float h = (1 / glm::pi<float>()) / _sectionRadius * _pitch;
	
	x = (_coilRadius + _sectionRadius * cos(v)) * cos(u);
	y = (_coilRadius + _sectionRadius * cos(v)) * sin(u);
	z = _sectionRadius * (sin(v) + u * h);

	P.setParam(x, y, z);
	return P;
}
