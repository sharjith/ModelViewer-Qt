#ifndef TRIANGLE_H
#define TRIANGLE_H

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

signals:

protected:
	QVector3D _vertex0;
	QVector3D _vertex1;
	QVector3D _vertex2;
};

#endif // TRIANGLE_H
