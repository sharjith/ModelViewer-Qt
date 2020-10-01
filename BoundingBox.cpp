// BoundingBox.cpp: implementation of the CBoundingBox class.
//
//////////////////////////////////////////////////////////////////////

#include "BoundingBox.h"
#include "Point.h"
#include <QVector3D>
#include <QList>

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

BoundingBox::BoundingBox()
{
	_xMax = 0.0f;
	_xMin = 0.0f;
	_yMax = 0.0f;
	_yMin = 0.0f;
	_zMax = 0.0f;
	_zMin = 0.0f;
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
	P.setX((xMax() + xMin()) / 2);
	P.setY((yMax() + yMin()) / 2);
	P.setZ((zMax() + zMin()) / 2);
	return P;
}

double BoundingBox::boundingRadius() const
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

bool BoundingBox::contains(const Point& P) const
{
	bool bx = false, by = false, bz = false;
	bx = (P.getX() <= xMax() && P.getX() >= xMin());
	by = (P.getY() <= yMax() && P.getY() >= yMin());
	bz = (P.getZ() <= zMax() && P.getZ() >= zMin());

	return (bx && by && bz);
}

void BoundingBox::addBox(const BoundingBox& B)
{
	if (B.xMax() > xMax())
		_xMax = B.xMax();
	if (B.xMin() < xMin())
		_xMin = B.xMin();

	if (B.yMax() > yMax())
		_yMax = B.yMax();
	if (B.yMin() < yMin())
		_yMin = B.yMin();

	if (B.zMax() > zMax())
		_zMax = B.zMax();
	if (B.zMin() < zMin())
		_zMin = B.zMin();
}

QRect BoundingBox::project(const QMatrix4x4& modelView, const QMatrix4x4& projection, const QRect& viewport, const QRect& window)
{
	// Bottom Face
	QVector3D leftFrontBottom(_xMin, _yMin, _zMin);
	QVector3D rightFrontBottom(_xMax, _yMin, _zMin);
	QVector3D leftRearBottom(_xMin, _yMax, _zMin);
	QVector3D rightRearBottom(_xMax, _yMax, _zMin);
	// Top Face
	QVector3D leftFrontTop(_xMin, _yMin, _zMax);
	QVector3D rightFrontTop(_xMax, _yMin, _zMax);
	QVector3D leftRearTop(_xMin, _yMax, _zMax);
	QVector3D rightRearTop(_xMax, _yMax, _zMax);

	QVector3D v1 = leftFrontBottom.project(modelView, projection, viewport);
	QVector3D v2 = rightFrontBottom.project(modelView, projection, viewport);
	QVector3D v3 = leftRearBottom.project(modelView, projection, viewport);
	QVector3D v4 = rightRearBottom.project(modelView, projection, viewport);

	QVector3D v5 = leftFrontTop.project(modelView, projection, viewport);
	QVector3D v6 = rightFrontTop.project(modelView, projection, viewport);
	QVector3D v7 = leftRearTop.project(modelView, projection, viewport);
	QVector3D v8 = rightRearTop.project(modelView, projection, viewport);

	QList<float> xVals = { v1.x(), v2.x(), v3.x(), v4.x(), v5.x(), v6.x(), v7.x(), v8.x() };
	QList<float> yVals = { v1.y(), v2.y(), v3.y(), v4.y(), v5.y(), v6.y(), v7.y(), v8.y() };

	std::sort(xVals.begin(), xVals.end(), std::less<float>());
	std::sort(yVals.begin(), yVals.end(), std::less<float>());

	QRect rect(xVals.first(), (window.height() - yVals.last()), (xVals.last() - xVals.first()), (yVals.last() - yVals.first()));

	return rect;
}