#include "BoundingSphere.h"
#include <cmath>
#include <QDebug>

BoundingSphere::BoundingSphere(const float &cx,
                               const float &cy,
                               const float &cz,
                               const float &rad) : _center(cx, cy, cz), _radius(rad)
{
}

BoundingSphere::~BoundingSphere(void)
{
}

void BoundingSphere::setRadius(const float &rad)
{
    _radius = rad;
}

void BoundingSphere::setCenter(const float &cx, const float &cy, const float &cz)
{
    _center.setX(cx);
    _center.setY(cy);
    _center.setZ(cz);
}

void BoundingSphere::setCenter(const QVector3D &cen)
{
    _center = cen;
}

void BoundingSphere::addSphere(const BoundingSphere &other)
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
