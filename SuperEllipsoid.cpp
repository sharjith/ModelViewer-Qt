#include "SuperEllipsoid.h"
#include "Point.h"

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>
#include <glm/glm.hpp>

SuperEllipsoid::SuperEllipsoid(QOpenGLShaderProgram* prog, float radius, float scaleX, float scaleY, float scaleZ, float n1, float n2, unsigned int nSlices, unsigned int nStacks, unsigned int sMax, unsigned int tMax) :
	ParametricSurface(prog, nSlices, nStacks, sMax, tMax),
	_radius(radius),
	_scaleX(scaleX),
	_scaleY(scaleY),
	_scaleZ(scaleZ),
	_n1(n1),
	_n2(n2)
{
	setAutoIncrName("Super Ellipsoid");
	buildMesh();
}

SuperEllipsoid::~SuperEllipsoid()
{
}

TriangleMesh* SuperEllipsoid::clone()
{
	return new SuperEllipsoid(_prog, _radius, _scaleX, _scaleY, _scaleZ, _n1, _n2, _slices, _stacks, _sMax, _tMax);
}

float SuperEllipsoid::firstUParameter() const
{
	return -glm::pi<float>() / 2;
}

float SuperEllipsoid::lastUParameter() const
{
	return glm::pi<float>() / 2;
}

float SuperEllipsoid::firstVParameter() const
{
	return -glm::pi<float>();
}

float SuperEllipsoid::lastVParameter() const
{
	return glm::pi<float>();
}

Point SuperEllipsoid::pointAtParameter(const float& u, const float& v)
{
	Point P;
	float x, y, z;

	//http://paulbourke.net/geometry/spherical/
	// Super ellipsoid
	// x = r * cos^n1(u) * cos^n2(v)
	// y = r * cos^n1(u) * sin^n2(v)
	// z = r * sin^n1(u)
	// Where u = -2PI - 2PI and v = -PI - PI

	auto sign = [](float f)->float
	{
		if (f == 0)
			return 0;
		else if (f < 0)
			return -1.0f;
		else
			return 1.0f;
	};

	auto auxC = [sign](float w, float m)->float
	{
		return sign(cos(w)) * pow(fabs(cos(w)), m);
	};

	auto auxS = [sign](float w, float m)->float
	{
		return sign(sin(w)) * pow(fabs(sin(w)), m);
	};

	x = _radius * _scaleY * auxC(u, _n1) * auxS(v, _n2);
	y = _radius * _scaleX * auxC(u, _n1) * auxC(v, _n2);
	z = _radius * _scaleZ * auxS(u, _n1);

	P.setParam(x, y, z);
	return P;
}