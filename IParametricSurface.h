#pragma once

#include <QVector3D>

class Point;
class IParametricSurface
{
public:

	virtual float firstUParameter() const = 0;
	virtual float firstVParameter() const = 0;
	virtual float lastUParameter() const = 0;
	virtual float lastVParameter() const = 0;
	virtual Point pointAtParameter(const float& u, const float& v) = 0;
	virtual QVector3D normalAtParameter(const float& u, const float& v) = 0;
};
