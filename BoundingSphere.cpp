#include "BoundingSphere.h"
#include <cmath>
#include <QDebug>

BoundingSphere::BoundingSphere(const float& cx,
	const float& cy,
	const float& cz,
	const float& rad) : _center(cx, cy, cz), _radius(rad)
{
}

BoundingSphere::~BoundingSphere(void)
{
}

void BoundingSphere::setRadius(const float& rad)
{
	_radius = rad;
}

void BoundingSphere::setCenter(const float& cx, const float& cy, const float& cz)
{
	_center.setX(cx);
	_center.setY(cy);
	_center.setZ(cz);
}

void BoundingSphere::setCenter(const QVector3D& cen)
{
	_center = cen;
}

void BoundingSphere::addSphere(const BoundingSphere& other)
{
	if (qFuzzyCompare(_center, other._center) && qFuzzyCompare(_radius, other._radius)) // same sphere
		return;

	if (_center == QVector3D(0, 0, 0) && _radius == 0)
	{
		_center = other.getCenter();
		_radius = other.getRadius();
	}
	else
	{
		float smallerRadius = _radius < other._radius ? _radius : other._radius;
		float largerRadius = _radius > other._radius ? _radius : other._radius;
		if (_center.distanceToPoint(other._center) <= largerRadius - smallerRadius) // one sphere inside other
		{
			if (_radius == smallerRadius) // this sphere is smaller
			{
				//qDebug() << "This sphere is smaller";
				// make other sphere the bounding sphere
				_center = other._center;
				_radius = other._radius;
			}
			else // other sphere is already inside this one, do nothing
				return;
		}
		else // intersecting or touching or spaced
		{
			//qDebug() << "Intersecting/Touching/Spaced";
			QVector3D toThisEnd = (_center - other._center).normalized();
			QVector3D toOtherEnd = (other._center - _center).normalized();

			QVector3D thisEndPoint = _center + toThisEnd * _radius;
			QVector3D otherEndPoint = other._center + toOtherEnd * other._radius;

			_radius = thisEndPoint.distanceToPoint(otherEndPoint) / 2;
			_center = thisEndPoint + toOtherEnd * _radius;
		}
	}
}

bool BoundingSphere::intersectsWithRay(const QVector3D& rayPos, const QVector3D& rayDir)
{
	// https://www.scratchapixel.com/lessons/3d-basic-rendering/minimal-ray-tracer-rendering-simple-shapes/ray-sphere-intersection
	
	float t0, t1; // solutions for t if the ray intersects 
	QVector3D oc = rayPos - _center;
	float a = QVector3D::dotProduct(rayDir, rayDir);
	float b = 2.0 * QVector3D::dotProduct(oc, rayDir);
	float c = QVector3D::dotProduct(oc, oc) - _radius * _radius;
	if (!solveQuadratic(a, b, c, t0, t1)) 
		return false;

	// ray touching the sphere
	if (fabs(t0) - fabs(t1) <= 0.00000001) // floating point equality check
		return true;

	if (t0 > t1) std::swap(t0, t1);

	if (t0 < 0) 
	{
		t0 = t1; // if t0 is negative, let's use t1 instead 
		if (t0 < 0) 
			return false; // both t0 and t1 are negative 
	}
	
	return true;
}

bool BoundingSphere::solveQuadratic(const float& a, const float& b, const float& c, float& x0, float& x1)
{
	float discr = b * b - 4.0f * a * c;
	if (discr < 0) return false;
	else if (discr == 0) x0 = x1 = -0.5f * b / a;
	else 
	{
		float q = (b > 0) ?
			-0.5f * (b + sqrt(discr)) :
			-0.5f * (b - sqrt(discr));
		x0 = q / a;
		x1 = c / q;
	}
	if (x0 > x1) std::swap(x0, x1);

	return true;
}
