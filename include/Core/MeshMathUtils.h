#pragma once

#include <QMatrix4x4>
#include <QQuaternion>
#include <QVector3D>

#include <cmath>

// ---------------------------------------------------------------------------
// MeshMathUtils
//
// Pure math helpers for Euler angle decomposition and mesh rotation
// conventions. Header-only; all functions are inline.
// ---------------------------------------------------------------------------
namespace MeshMathUtils
{

inline float normalizeDegrees180(float degrees)
{
    float n = std::fmod(degrees + 180.0f, 360.0f);
    if (n < 0.0f) n += 360.0f;
    n -= 180.0f;
    if (std::abs(n) < 1.0e-4f) return 0.0f;
    if (std::abs(n - 180.0f) < 1.0e-4f || std::abs(n + 180.0f) < 1.0e-4f) return 180.0f;
    return n;
}

inline QVector3D normalizeEulerDegrees(const QVector3D& r)
{
    return QVector3D(normalizeDegrees180(r.x()),
                     normalizeDegrees180(r.y()),
                     normalizeDegrees180(r.z()));
}

inline float eulerCanonicalScore(const QVector3D& r)
{
    return std::abs(r.x()) + std::abs(r.y()) + std::abs(r.z());
}

inline QVector3D canonicalizeMeshEuler(const QVector3D& r)
{
    const QVector3D primary   = normalizeEulerDegrees(r);
    const QVector3D alternate = normalizeEulerDegrees(
        QVector3D(r.x() + 180.0f, 180.0f - r.y(), r.z() + 180.0f));
    return (eulerCanonicalScore(alternate) + 1.0e-4f < eulerCanonicalScore(primary))
               ? alternate : primary;
}

inline QMatrix4x4 buildMeshRotationMatrix(const QVector3D& rotation)
{
    QMatrix4x4 m;
    m.setToIdentity();
    m.rotate(rotation.x(), QVector3D(1.0f, 0.0f, 0.0f));
    m.rotate(rotation.y(), QVector3D(0.0f, 1.0f, 0.0f));
    m.rotate(rotation.z(), QVector3D(0.0f, 0.0f, 1.0f));
    return m;
}

inline QQuaternion quaternionFromMeshEuler(const QVector3D& rotation)
{
    return QQuaternion::fromRotationMatrix(
        buildMeshRotationMatrix(rotation).toGenericMatrix<3, 3>()).normalized();
}

inline QVector3D rotationMatrixToMeshEuler(const QMatrix4x4& matrix)
{
    const float m00 = matrix(0, 0), m01 = matrix(0, 1), m02 = matrix(0, 2);
    const float m10 = matrix(1, 0), m11 = matrix(1, 1), m12 = matrix(1, 2);
    const float m22 = matrix(2, 2);

    const float yRad  = std::asin(std::clamp(m02, -1.0f, 1.0f));
    const float cosY  = std::cos(yRad);
    const float xRad  = std::abs(cosY) > 1.0e-6f ? std::atan2(-m12, m22) : std::atan2(m10, m11);
    const float zRad  = std::abs(cosY) > 1.0e-6f ? std::atan2(-m01, m00) : 0.0f;

    return canonicalizeMeshEuler(QVector3D(
        qRadiansToDegrees(xRad),
        qRadiansToDegrees(yRad),
        qRadiansToDegrees(zRad)));
}

inline QVector3D extractMeshRotationFromMatrix(const QMatrix4x4& matrix)
{
    QVector3D c0(matrix(0,0), matrix(1,0), matrix(2,0));
    QVector3D c1(matrix(0,1), matrix(1,1), matrix(2,1));
    QVector3D c2(matrix(0,2), matrix(1,2), matrix(2,2));

    const float sx = c0.length(), sy = c1.length(), sz = c2.length();
    if (sx > 1.0e-8f) c0 /= sx;
    if (sy > 1.0e-8f) c1 /= sy;
    if (sz > 1.0e-8f) c2 /= sz;

    QMatrix4x4 rot;
    rot.setToIdentity();
    rot(0,0) = c0.x(); rot(1,0) = c0.y(); rot(2,0) = c0.z();
    rot(0,1) = c1.x(); rot(1,1) = c1.y(); rot(2,1) = c1.z();
    rot(0,2) = c2.x(); rot(1,2) = c2.y(); rot(2,2) = c2.z();
    return rotationMatrixToMeshEuler(rot);
}

} // namespace MeshMathUtils
