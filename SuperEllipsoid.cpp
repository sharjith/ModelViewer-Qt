#include "SuperEllipsoid.h"
#include "Point.h"

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>
#include <glm/glm.hpp>


SuperEllipsoid::SuperEllipsoid(QOpenGLShaderProgram* prog, GLfloat radius, GLfloat scaleX, GLfloat scaleY, GLfloat scaleZ, GLfloat n1, GLfloat n2, GLuint nSlices, GLuint nStacks) :
	ParametricSurface(prog, nSlices, nStacks),
	_radius(radius),
	_scaleX(scaleX),
	_scaleY(scaleY),
	_scaleZ(scaleZ),
	_n1(n1),
	_n2(n2)
{
	_name = "Super Ellipsoid";
	buildMesh(nSlices, nStacks);
}


SuperEllipsoid::~SuperEllipsoid()
{
}

float SuperEllipsoid::firstUParameter() const
{
	return -glm::pi<float>()/2;
}

float SuperEllipsoid::lastUParameter() const
{
	return glm::pi<float>()/2;
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

    /*
	auto power = [](double f, double p)->double
	{
		int sign;
		double absf;

		sign = (f < 0 ? -1 : 1);
		absf = (f < 0 ? -f : f);

		if (absf < 0.00001)
			return(0.0);
		else
			return(sign * pow(absf, p));
	};

    x = _radius * power(cos(u), _n1) * power(cos(v), _n2);
	y = _radius * power(cos(u), _n1) * power(sin(v), _n2);
	z = _radius * power(sin(u), _n1);*/

	

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

	x = _radius * _scaleX * auxC(u, _n1) * auxC(v, _n2);
	y = _radius * _scaleY * auxC(u, _n1) * auxS(v, _n2);
	z = _radius * _scaleZ * auxS(u, _n1);

	P.setParam(x, y, z);
	return P;
}
