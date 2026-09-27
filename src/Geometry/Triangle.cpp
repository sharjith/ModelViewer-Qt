#include "Triangle.h"

#include <cmath>

Triangle::Triangle(const QVector3D& vertex1, const QVector3D& vertex2, const QVector3D& vertex3, QObject* parent) : QObject(parent),
_vertex0(vertex1),
_vertex1(vertex2),
_vertex2(vertex3)
{
}

Triangle::~Triangle()
{
}

void Triangle::setVertices(const QVector3D& vertex1, const QVector3D& vertex2, const QVector3D& vertex3)
{
	_vertex0 = vertex1;
	_vertex1 = vertex2;
	_vertex2 = vertex3;
}

void Triangle::vertices(QVector3D& vertex1, QVector3D& vertex2, QVector3D& vertex3) const
{
	vertex1 = _vertex0;
	vertex2 = _vertex1;
	vertex3 = _vertex2;
}

QList<QVector3D> Triangle::vertices() const
{
	return QList<QVector3D>{_vertex0, _vertex1, _vertex2};
}

QVector3D Triangle::normal() const
{
	return QVector3D::crossProduct(_vertex1 - _vertex0, _vertex2 - _vertex0);
}

void Triangle::computeBarycentric(const QVector3D& point, float& outU, float& outV, float& outW) const
{
	const QVector3D e0 = _vertex1 - _vertex0;
	const QVector3D e1 = _vertex2 - _vertex0;
	const QVector3D e2 = point - _vertex0;

	const float d00 = QVector3D::dotProduct(e0, e0);
	const float d01 = QVector3D::dotProduct(e0, e1);
	const float d11 = QVector3D::dotProduct(e1, e1);
	const float d20 = QVector3D::dotProduct(e2, e0);
	const float d21 = QVector3D::dotProduct(e2, e1);
	const float denom = d00 * d11 - d01 * d01;

	if (std::abs(denom) < 1.0e-12f)
	{
		// Degenerate (near-zero-area) triangle - fall back to vertex0.
		outU = 1.0f;
		outV = 0.0f;
		outW = 0.0f;
		return;
	}

	const float v = (d11 * d20 - d01 * d21) / denom;
	const float w = (d00 * d21 - d01 * d20) / denom;
	outU = 1.0f - v - w;
	outV = v;
	outW = w;
}