#include "MeasurementGeometry.h"

#include <algorithm>
#include <cmath>

namespace MeasurementGeometry
{

namespace
{
// M_PI isn't guaranteed available (MSVC needs _USE_MATH_DEFINES before
// <cmath>) - defined locally to avoid that portability trap, same reasoning
// as Camera.h's own PI macro.
constexpr float kTwoPi = 6.283185307179586f;
constexpr float kRadToDeg = 57.29577951308232f; // 180 / pi
}

bool circumcircle3Point(const QVector3D& a, const QVector3D& b, const QVector3D& c,
    QVector3D& outCenter, QVector3D& outNormal, float& outRadius)
{
    // Standard 3D triangle-circumcenter construction: express the center as
    // A + t, where t is a combination of the two edge vectors (ab, ac)
    // orthogonal to their cross product, solved via the identity
    //   t = ((|ac|^2 (ab x ac) x ab) + (|ab|^2 ac x (ab x ac))) / (2 |ab x ac|^2)
    const QVector3D ab = b - a;
    const QVector3D ac = c - a;
    const QVector3D abXac = QVector3D::crossProduct(ab, ac);
    const float abXacLenSq = abXac.lengthSquared();

    // Near-zero cross product means the 3 points are (nearly) collinear -
    // no unique circle.
    if (abXacLenSq < 1.0e-9f)
        return false;

    const QVector3D t =
        (QVector3D::crossProduct(abXac, ab) * ac.lengthSquared() +
         QVector3D::crossProduct(ac, abXac) * ab.lengthSquared())
        / (2.0f * abXacLenSq);

    outCenter = a + t;
    outRadius = t.length();
    outNormal = abXac.normalized();
    return true;
}

bool circleFromCenterAndTwoPoints(const QVector3D& center, const QVector3D& p1, const QVector3D& p2,
    QVector3D& outNormal, float& outRadius)
{
    const QVector3D v1 = p1 - center;
    const QVector3D v2 = p2 - center;
    const QVector3D cross = QVector3D::crossProduct(v1, v2);
    if (cross.lengthSquared() < 1.0e-9f)
        return false;  // center/p1/p2 collinear - no well-defined plane

    outNormal = cross.normalized();
    outRadius = (v1.length() + v2.length()) * 0.5f;
    return true;
}

QVector<QVector3D> circlePolyline(const QVector3D& center, const QVector3D& normal, float radius, int segments)
{
    QVector<QVector3D> points;
    if (segments < 3 || radius <= 0.0f)
        return points;

    const QVector3D n = normal.normalized();
    // Any vector not parallel to n works as a starting reference for the
    // in-plane basis; picking whichever world axis is least aligned with n
    // keeps the cross product well-conditioned.
    const QVector3D reference = (std::abs(QVector3D::dotProduct(n, QVector3D(0.0f, 1.0f, 0.0f))) < 0.9f)
        ? QVector3D(0.0f, 1.0f, 0.0f)
        : QVector3D(1.0f, 0.0f, 0.0f);
    const QVector3D u = QVector3D::crossProduct(n, reference).normalized();
    const QVector3D v = QVector3D::crossProduct(n, u).normalized();

    points.reserve(segments);
    for (int i = 0; i < segments; ++i)
    {
        const float theta = (kTwoPi * static_cast<float>(i)) / static_cast<float>(segments);
        points.append(center + (u * std::cos(theta) + v * std::sin(theta)) * radius);
    }
    return points;
}

FaceToFaceResult compareFaces(const QVector3D& p1, const QVector3D& n1,
    const QVector3D& p2, const QVector3D& n2, float parallelToleranceDegrees)
{
    FaceToFaceResult result;

    const QVector3D n1n = n1.normalized();
    const QVector3D n2n = n2.normalized();
    // abs() before acos: two planes are equally "parallel" whether their
    // picked normals point the same way or opposite ways - only the planes'
    // relative orientation matters, not which side each triangle winds to.
    const float dot = std::clamp(std::abs(QVector3D::dotProduct(n1n, n2n)), 0.0f, 1.0f);
    const float angleBetweenNormals = std::acos(dot) * kRadToDeg;  // already in [0, 90]

    if (angleBetweenNormals < parallelToleranceDegrees)
    {
        result.isParallel = true;
        result.distance = std::abs(QVector3D::dotProduct(p2 - p1, n1n));
    }
    else
    {
        result.isParallel = false;
        result.angleDegrees = angleBetweenNormals;
    }
    return result;
}

float pointToPlaneDistance(const QVector3D& point, const QVector3D& planePoint, const QVector3D& planeNormal)
{
    return std::abs(QVector3D::dotProduct(point - planePoint, planeNormal.normalized()));
}

}
