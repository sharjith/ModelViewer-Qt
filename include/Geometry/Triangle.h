#pragma once


#include <QObject>
#include <QVector3D>
#include <QList>

class Triangle : public QObject
{
	Q_OBJECT
public:
	Triangle(const QVector3D& vertex1, const QVector3D& vertex2, const QVector3D& vertex3, QObject* parent = nullptr);

	virtual ~Triangle();

	void setVertices(const QVector3D& vertex1, const QVector3D& vertex2, const QVector3D& vertex3);

	void vertices(QVector3D& vertex1, QVector3D& vertex2, QVector3D& vertex3) const;
	QList<QVector3D> vertices() const;

	QVector3D normal() const;

	virtual bool intersectsWithRay(const QVector3D& rayPos, const QVector3D& rayDir, QVector3D& outIntersectionPoint) = 0;

	// Barycentric weights (u,v,w; sum to 1) of `point` with respect to this
	// triangle's vertex0/vertex1/vertex2, in that order. Assumes `point`
	// lies on (or very near) the triangle's plane - e.g. a hit point
	// already returned by intersectsWithRay(). Non-virtual and generic
	// (re-derives from the known vertices + point rather than reusing the
	// per-algorithm intersection math), so every Triangle subclass gets it
	// for free without touching their intersectsWithRay() implementations.
	void computeBarycentric(const QVector3D& point, float& outU, float& outV, float& outW) const;

signals:

protected:
	QVector3D _vertex0;
	QVector3D _vertex1;
	QVector3D _vertex2;
};
