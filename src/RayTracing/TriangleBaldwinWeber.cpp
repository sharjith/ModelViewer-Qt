#include "TriangleBaldwinWeber.h"
#include <iostream>
#include <cmath>

TriangleBaldwinWeber::TriangleBaldwinWeber(const QVector3D& vertex1, const QVector3D& vertex2, const QVector3D& vertex3, QObject* parent) :
	Triangle(vertex1, vertex2, vertex3, parent),
	_transformation{ 0,0,0,0,0,0,0,0,0 },
	_fixedColumn(0)
{
	// Calculate edges and normal
	QVector3D edge1, edge2;
	edge1 = _vertex1 - _vertex0;
	edge2 = _vertex2 - _vertex0;
	QVector3D normal = QVector3D::crossProduct(edge1, edge2);

	// Build transformation from global to barycentric coordinates.
	float x1, x2;
	float num = QVector3D::dotProduct(_vertex0, normal);                 // Element (3,4) of each transformation matrix

	if (fabs(normal.x()) > fabs(normal.y()) && fabs(normal.x()) > fabs(normal.z()))
	{
		x1 = _vertex1.y() * _vertex0.z() - _vertex1.z() * _vertex0.y();
		x2 = _vertex2.y() * _vertex0.z() - _vertex2.z() * _vertex1.y();

		//Do matrix set up here for when a = 1, b = c = 0 formula
		_fixedColumn = 1;

		_transformation[0] = edge2.z() / normal.x();
		_transformation[1] = -edge2.y() / normal.x();
		_transformation[2] = x2 / normal.x();

		_transformation[3] = -edge1.z() / normal.x();
		_transformation[4] = edge1.y() / normal.x();
		_transformation[5] = -x1 / normal.x();

		_transformation[6] = normal.y() / normal.x();
		_transformation[7] = normal.z() / normal.x();
		_transformation[8] = -num / normal.x();
	}
	else if (fabs(normal.y()) > fabs(normal.z()))
	{
		x1 = _vertex1.z() * _vertex0.x() - _vertex1.x() * _vertex0.z();
		x2 = _vertex2.z() * _vertex0.x() - _vertex2.x() * _vertex0.z();

		// b = 1 case
		_fixedColumn = 2;

		_transformation[0] = -edge2.z() / normal.y();
		_transformation[1] = edge2.x() / normal.y();
		_transformation[2] = x2 / normal.y();

		_transformation[3] = edge1.z() / normal.y();
		_transformation[4] = -edge1.x() / normal.y();
		_transformation[5] = -x1 / normal.y();

		_transformation[6] = normal.x() / normal.y();
		_transformation[7] = normal.z() / normal.y();
		_transformation[8] = -num / normal.y();
	}
	else if (fabs(normal.z()) > 0.0f)
	{
		x1 = _vertex1.x() * _vertex0.y() - _vertex1.y() * _vertex0.x();
		x2 = _vertex2.x() * _vertex0.y() - _vertex2.y() * _vertex0.x();

		// c = 1 case
		_fixedColumn = 3;

		_transformation[0] = edge2.y() / normal.z();
		_transformation[1] = -edge2.x() / normal.z();
		_transformation[2] = x2 / normal.z();

		_transformation[3] = -edge1.y() / normal.z();
		_transformation[4] = edge1.x() / normal.z();
		_transformation[5] = -x1 / normal.z();

		_transformation[6] = normal.x() / normal.z();
		_transformation[7] = normal.y() / normal.z();
		_transformation[8] = -num / normal.z();
	}
	else
	{
		std::cout << "TriangleBaldwinWeber::TriangleBaldwinWeber Error: Building precomputed-transformation triangle from degenerate source " << std::endl;
	}
}

constexpr float tFar = std::numeric_limits<float>::max();
constexpr float tNear = std::numeric_limits<float>::min();
bool TriangleBaldwinWeber::intersectsWithRay(const QVector3D& rayPos, const QVector3D& rayDir, QVector3D& outIntersectionPoint)
{
	// Get barycentric z components of ray origin and direction for calculation of t value
	float xg, yg;                       // The barycentric coordinates

	if (_fixedColumn == 1)
	{
		const float transS = rayPos.x() + _transformation[6] * rayPos.y() + _transformation[7] * rayPos.z() + _transformation[8];
		const float transD = rayDir.x() + _transformation[6] * rayDir.y() + _transformation[7] * rayDir.z();
		const float ta = -transS / transD;

		if (ta <= tNear || ta >= tFar)
			return false;

		const float wr[3] = { rayPos.x() + ta * rayDir.x(), rayPos.y() + ta * rayDir.y(), rayPos.z() + ta * rayDir.z() };
		outIntersectionPoint = { wr[0], wr[1], wr[2] };

		xg = _transformation[0] * wr[1] + _transformation[1] * wr[2] + _transformation[2];
		yg = _transformation[3] * wr[1] + _transformation[4] * wr[2] + _transformation[5];
	}
	else if (_fixedColumn == 2)
	{
		const float transS = _transformation[6] * rayPos.x() + rayPos.y() + _transformation[7] * rayPos.z() + _transformation[8];
		const float transD = _transformation[6] * rayDir.x() + rayDir.y() + _transformation[7] * rayDir.z();
		const float ta = -transS / transD;

		if (ta <= tNear || ta >= tFar)
			return false;

		const float wr[3] = { rayPos.x() + ta * rayDir.x(), rayPos.y() + ta * rayDir.y(), rayPos.z() + ta * rayDir.z() };
		outIntersectionPoint = { wr[0], wr[1], wr[2] };

		xg = _transformation[0] * wr[0] + _transformation[1] * wr[2] + _transformation[2];
		yg = _transformation[3] * wr[0] + _transformation[4] * wr[2] + _transformation[5];
	}
	else if (_fixedColumn == 3)
	{
		const float transS = _transformation[6] * rayPos.x() + _transformation[7] * rayPos.y() + rayPos.z() + _transformation[8];
		const float transD = _transformation[6] * rayDir.x() + _transformation[7] * rayDir.y() + rayDir.z();
		const float ta = -transS / transD;

		if (ta <= tNear || ta >= tFar)
			return false;

		const float wr[3] = { rayPos.x() + ta * rayDir.x(), rayPos.y() + ta * rayDir.y(), rayPos.z() + ta * rayDir.z() };
		outIntersectionPoint = { wr[0], wr[1], wr[2] };

		xg = _transformation[0] * wr[0] + _transformation[1] * wr[1] + _transformation[2];
		yg = _transformation[3] * wr[0] + _transformation[4] * wr[1] + _transformation[5];
	}
	else
	{
		// Invalid fixed-column code, treat ray as missing triangle
		return false;
	}

	// Final check of barycentric coordinates to see if intersection is inside or outside triangle
	if (xg >= 0.0f && yg >= 0.0f && yg + xg < 1.0f)
	{
		return true;
	}

	return false;
}