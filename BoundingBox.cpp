// BoundingBox.cpp: implementation of the CBoundingBox class.
//
//////////////////////////////////////////////////////////////////////

#include "BoundingBox.h"
#include "Point.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

BoundingBox::BoundingBox()
{

}

BoundingBox::BoundingBox(const double& xMin, const double& xMax,
						   const double& yMin, const double& yMax, 
                           const double& zMin, const double& zMax) : _xMax(xMax), _xMin(xMin),
                           _yMax(yMax), _yMin(yMin),
                           _zMax(zMax), _zMin(zMin)
{
}

BoundingBox::~BoundingBox()
{

}

Point BoundingBox::center() const
{
    Point P;
    P.setX((xMax() + xMin())/2);
    P.setY((yMax() + yMin())/2);
    P.setZ((zMax() + zMin())/2);
	return P;
}

double BoundingBox::BoundingRadius() const
{
	double rad;
    Point pMax(xMax(), yMax(), zMax());
    Point cen = center();
    rad = pMax.distance(cen);
	return rad;
}

void BoundingBox::setLimits(const double& xMin, const double& xMax,
						   const double& yMin, const double& yMax, 
						   const double& zMin, const double& zMax)
{
    _xMax = xMax;
    _xMin = xMin;
    _yMax = yMax;
    _yMin = yMin;
    _zMax = zMax;
    _zMin = zMin;
}

void BoundingBox::getLimits(double& xMin, double& xMax,
						   double& yMin, double& yMax,
						   double& zMin, double& zMax)
{
    xMax = _xMax;
    xMin = _xMin;
    yMax = _yMax;
    yMin = _yMin;
    zMax = _zMax;
    zMin = _zMin;
}

bool BoundingBox::contains(const Point &P) const
{
	bool bx = false, by = false, bz = false;
    bx = (P.getX() <= xMax() && P.getX() >= xMin());
    by = (P.getY() <= yMax() && P.getY() >= yMin());
    bz = (P.getZ() <= zMax() && P.getZ() >= zMin());

	return (bx && by &&bz);
}

void BoundingBox::addBox(const BoundingBox& B)
{
    if(B.xMax() > xMax())
        _xMax = B.xMax();
    if(B.xMin() < xMin())
        _xMin = B.xMin();

    if(B.yMax() > yMax())
        _yMax = B.yMax();
    if(B.yMin() < yMin())
        _yMin = B.yMin();

    if(B.zMax() > zMax())
        _zMax = B.zMax();
    if(B.zMin() < zMin())
        _zMin = B.zMin();
}
