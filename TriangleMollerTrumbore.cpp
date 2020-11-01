#include "TriangleMollerTrumbore.h"

TriangleMollerTrumbore::TriangleMollerTrumbore(const QVector3D &vertex1, const QVector3D &vertex2, const QVector3D &vertex3, QObject *parent) :
    Triangle(vertex1, vertex2, vertex3, parent)
{

}

TriangleMollerTrumbore::~TriangleMollerTrumbore()
{

}

bool TriangleMollerTrumbore::intersectsWithRay(const QVector3D &rayPos, const QVector3D &rayDir, QVector3D &outIntersectionPoint)
{
    // Möller–Trumbore intersection algorithm
    // https://en.wikipedia.org/wiki/M%C3%B6ller%E2%80%93Trumbore_intersection_algorithm
    const float EPSILON = 0.0000001f;
    QVector3D edge1, edge2, h, s, q;
    float a, f, u, v;
    edge1 = _vertex2 - _vertex1;
    edge2 = _vertex3 - _vertex1;
    h = QVector3D::crossProduct(rayDir, edge2);
    a = QVector3D::dotProduct(edge1, h);
    if (a > -EPSILON && a < EPSILON)
        return false;    // This ray is parallel to this triangle.
    f = 1.0f / a;
    s = rayPos - _vertex1;
    u = f * QVector3D::dotProduct(s, h);
    if (u < 0.0f || u > 1.0f)
        return false;
    q = QVector3D::crossProduct(s, edge1);
    v = f * QVector3D::dotProduct(rayDir, q);
    if (v < 0.0f || u + v > 1.0f)
        return false;
    // At this stage we can compute t to find out where the intersection point is on the line.
    float t = f * QVector3D::dotProduct(edge2, q);
    if (t > EPSILON) // ray intersection
    {
        outIntersectionPoint = rayPos + rayDir * t;
        return true;
    }
    else // This means that there is a line intersection but not a ray intersection.
        return false;
}
