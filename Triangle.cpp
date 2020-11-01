#include "Triangle.h"

Triangle::Triangle(const QVector3D& vertex1, const QVector3D& vertex2, const QVector3D& vertex3, QObject *parent) : QObject(parent),
    _vertex1(vertex1),
    _vertex2(vertex2),
    _vertex3(vertex3)
{

}

Triangle::~Triangle()
{

}

void Triangle::setVertices(const QVector3D& vertex1, const QVector3D& vertex2, const QVector3D& vertex3)
{
    _vertex1 = vertex1;
    _vertex2 = vertex2;
    _vertex3 = vertex3;
}

void Triangle::vertices(QVector3D& vertex1, QVector3D& vertex2, QVector3D& vertex3) const
{
    vertex1 = _vertex1;
    vertex2 = _vertex2;
    vertex3 = _vertex3;
}

QList<QVector3D> Triangle::vertices() const
{
    return QList<QVector3D>{_vertex1, _vertex2, _vertex3};
}

QVector3D Triangle::normal() const
{
    return QVector3D::crossProduct(_vertex2 - _vertex1, _vertex3 - _vertex1);
}
