#include "MeasurementGeometry.h"

#include <QVector2D>
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

// An arbitrary orthonormal in-plane basis (u, v) for the plane through
// `normal` - shared by circlePolyline() (placing point 0) and
// fitPitchCircle() (projecting points to 2D for the circle fit). Picking
// whichever world axis is least aligned with `normal` as the starting
// reference keeps the cross product well-conditioned.
void orthonormalBasis(const QVector3D& normal, QVector3D& outU, QVector3D& outV)
{
    const QVector3D n = normal.normalized();
    const QVector3D reference = (std::abs(QVector3D::dotProduct(n, QVector3D(0.0f, 1.0f, 0.0f))) < 0.9f)
        ? QVector3D(0.0f, 1.0f, 0.0f)
        : QVector3D(1.0f, 0.0f, 0.0f);
    outU = QVector3D::crossProduct(n, reference).normalized();
    outV = QVector3D::crossProduct(n, outU).normalized();
}

// Solve the 3x3 linear system [row0;row1;row2]*x = b via Cramer's rule (same
// small-hand-rolled-linear-solve spirit as circumcircle3Point's closed-form
// construction). Returns false if the matrix is singular - the threshold is
// scaled by the rows' own magnitude rather than a fixed epsilon, since the
// same code fits circles on both millimeter- and meter-scale models.
bool solve3x3(const QVector3D& row0, const QVector3D& row1, const QVector3D& row2,
    const QVector3D& b, QVector3D& outX)
{
    auto det3 = [](const QVector3D& r0, const QVector3D& r1, const QVector3D& r2) -> float {
        return r0.x() * (r1.y() * r2.z() - r1.z() * r2.y())
             - r0.y() * (r1.x() * r2.z() - r1.z() * r2.x())
             + r0.z() * (r1.x() * r2.y() - r1.y() * r2.x());
    };
    const float det = det3(row0, row1, row2);
    const float scale = std::max({ row0.length(), row1.length(), row2.length(), 1.0f });
    if (std::abs(det) < 1.0e-9f * scale * scale * scale)
        return false;

    outX = QVector3D(det3(b, row1, row2), det3(row0, b, row2), det3(row0, row1, b)) / det;
    return true;
}

// The smallest-eigenvalue eigenvector of a symmetric 3x3 matrix (given by
// its 6 distinct entries), for fitPitchCircle()'s best-fit plane normal.
// Closed-form/non-iterative (the standard trigonometric solution for real
// symmetric 3x3 eigenvalues, sometimes attributed to O.K. Smith 1961),
// deliberately NOT power iteration - power iteration's convergence rate
// depends on the ratio between the matrix's two largest eigenvalues after
// the spectral shift, which is close to 1 (i.e. pathologically slow to
// converge in any fixed iteration count) for exactly the input this
// function needs to handle well: 3+ points clustered along a shallow arc
// (e.g. a handful of ADJACENT holes picked out of a much larger bolt
// pattern) have a covariance matrix with one much larger in-plane spread
// than the other, which is what made an earlier power-iteration version of
// this function return wildly wrong (hugely oversized, mis-oriented)
// circles on exactly that input.
QVector3D smallestEigenvectorSymmetric3x3(float mxx, float myy, float mzz, float mxy, float mxz, float myz)
{
    const float offDiagSq = mxy * mxy + mxz * mxz + myz * myz;
    float eig3;  // the smallest eigenvalue
    if (offDiagSq < 1.0e-20f)
    {
        // Already diagonal - no rotation needed, the smallest entry names
        // its own axis directly.
        eig3 = std::min({ mxx, myy, mzz });
    }
    else
    {
        const float q = (mxx + myy + mzz) / 3.0f;
        const float p2 = (mxx - q) * (mxx - q) + (myy - q) * (myy - q) + (mzz - q) * (mzz - q) + 2.0f * offDiagSq;
        const float p = std::sqrt(p2 / 6.0f);
        const float invP = 1.0f / p;
        // B = (M - q*I) / p
        const float bxx = (mxx - q) * invP, byy = (myy - q) * invP, bzz = (mzz - q) * invP;
        const float bxy = mxy * invP, bxz = mxz * invP, byz = myz * invP;
        const float detB = bxx * (byy * bzz - byz * byz) - bxy * (bxy * bzz - byz * bxz) + bxz * (bxy * byz - byy * bxz);
        // In exact arithmetic detB/2 is in [-1, 1] for a symmetric matrix -
        // clamp against floating-point drift pushing it just outside.
        const float r = std::clamp(detB * 0.5f, -1.0f, 1.0f);
        const float phi = std::acos(r) / 3.0f;
        // The three eigenvalues are q + 2p*cos(phi + 2k*pi/3), k=0,1,2, in
        // DESCENDING order for k=0,1,2 - only the smallest (k=2) is needed.
        eig3 = q + 2.0f * p * std::cos(phi + kTwoPi / 3.0f);
    }

    // Null-space direction of (M - eig3*I): the cross product of any two of
    // its rows is exactly (not just approximately) orthogonal to the row
    // space, because eig3 is the PRECISE eigenvalue - not an approximation
    // to bootstrap further, unlike a covariance-matrix-row heuristic
    // applied to M directly would be. Tries all 3 row pairs and keeps
    // whichever cross product has the largest magnitude, for numerical
    // stability (avoids a near-parallel pair).
    const QVector3D row0(mxx - eig3, mxy, mxz);
    const QVector3D row1(mxy, myy - eig3, myz);
    const QVector3D row2(mxz, myz, mzz - eig3);

    QVector3D best = QVector3D::crossProduct(row0, row1);
    QVector3D candidate = QVector3D::crossProduct(row0, row2);
    if (candidate.lengthSquared() > best.lengthSquared())
        best = candidate;
    candidate = QVector3D::crossProduct(row1, row2);
    if (candidate.lengthSquared() > best.lengthSquared())
        best = candidate;

    if (best.lengthSquared() < 1.0e-20f)
        return QVector3D(0.0f, 0.0f, 1.0f);  // degenerate (coincident points) - arbitrary axis; the caller's Kasa singularity check rejects this input anyway

    return best.normalized();
}
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

    QVector3D u, v;
    orthonormalBasis(normal, u, v);

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

QVector3D closestPointOnLine(const QVector3D& point, const QVector3D& linePoint, const QVector3D& lineDir)
{
    const QVector3D dirN = lineDir.normalized();
    return linePoint + dirN * QVector3D::dotProduct(point - linePoint, dirN);
}

float pointToLineDistance(const QVector3D& point, const QVector3D& linePoint, const QVector3D& lineDir)
{
    return point.distanceToPoint(closestPointOnLine(point, linePoint, lineDir));
}

EdgeToEdgeResult compareLines(const QVector3D& p1, const QVector3D& d1,
    const QVector3D& p2, const QVector3D& d2, float parallelToleranceDegrees)
{
    EdgeToEdgeResult result;

    const QVector3D d1n = d1.normalized();
    const QVector3D d2n = d2.normalized();
    // abs() before acos: two edges are equally "parallel" whether their
    // picked directions point the same way or opposite ways - only the
    // lines' relative orientation matters, same reasoning as compareFaces().
    const float dot = std::clamp(std::abs(QVector3D::dotProduct(d1n, d2n)), 0.0f, 1.0f);
    const float angleBetweenDirs = std::acos(dot) * kRadToDeg;  // already in [0, 90]

    if (angleBetweenDirs < parallelToleranceDegrees)
    {
        result.isParallel = true;
        result.distance = pointToLineDistance(p2, p1, d1n);
    }
    else
    {
        result.isParallel = false;
        result.angleDegrees = angleBetweenDirs;
    }
    return result;
}

EdgeToFaceResult compareEdgeToFace(const QVector3D& edgePoint, const QVector3D& edgeDir,
    const QVector3D& facePoint, const QVector3D& faceNormal, float parallelToleranceDegrees)
{
    EdgeToFaceResult result;

    const QVector3D dN = edgeDir.normalized();
    const QVector3D nN = faceNormal.normalized();
    // dot = cos(angle between the edge direction and the face NORMAL).
    // The angle between the edge and the face PLANE is the complement of
    // that: 90 - acos(dot) degrees, which is the same as asin(dot) degrees
    // (acos(x) + asin(x) == 90 degrees for x in [0,1]) - 0 when the edge is
    // perpendicular to the normal (lying flat against/parallel to the
    // plane), 90 when the edge IS the normal direction (perpendicular to
    // the plane).
    const float dot = std::clamp(std::abs(QVector3D::dotProduct(dN, nN)), 0.0f, 1.0f);
    const float angleToPlaneDeg = std::asin(dot) * kRadToDeg;

    if (angleToPlaneDeg < parallelToleranceDegrees)
    {
        result.isParallel = true;
        result.distance = pointToPlaneDistance(edgePoint, facePoint, nN);
    }
    else
    {
        result.isParallel = false;
        result.angleDegrees = angleToPlaneDeg;
    }
    return result;
}

PitchCircleResult fitPitchCircle(const QVector<QVector3D>& points)
{
    PitchCircleResult result;
    const int n = points.size();
    if (n < 3)
        return result;

    // --- Best-fit plane: the smallest-eigenvalue eigenvector of the
    // points' covariance matrix, via the closed-form 3x3 symmetric
    // eigensolve (smallestEigenvectorSymmetric3x3()) - order-independent
    // (correct regardless of click order), and exact rather than iterative
    // (unlike a Newell's-method approach, which only recovers the right
    // plane when points are already fed in boundary-walk order, or power
    // iteration, whose convergence rate degrades badly for exactly the
    // input this needs to handle well - see that function's doc comment). ---
    QVector3D centroid;
    for (const QVector3D& p : points)
        centroid += p;
    centroid /= static_cast<float>(n);

    float mxx = 0.0f, myy = 0.0f, mzz = 0.0f, mxy = 0.0f, mxz = 0.0f, myz = 0.0f;
    for (const QVector3D& p : points)
    {
        const QVector3D d = p - centroid;
        mxx += d.x() * d.x(); myy += d.y() * d.y(); mzz += d.z() * d.z();
        mxy += d.x() * d.y(); mxz += d.x() * d.z(); myz += d.y() * d.z();
    }
    const QVector3D eigenVec = smallestEigenvectorSymmetric3x3(mxx, myy, mzz, mxy, mxz, myz);

    QVector3D u, v;
    orthonormalBasis(eigenVec, u, v);

    // --- Project to 2D and fit a circle via the Kasa algebraic
    // least-squares method (x^2+y^2+Dx+Ey+F=0, solved via the 3x3 normal
    // equations) - appropriate here since Kasa's known bias only shows up
    // fitting a narrow arc slice, and a pitch circle's points are spread
    // around the full 360 degrees by nature. ---
    QVector<QVector2D> pts2D;
    pts2D.reserve(n);
    for (const QVector3D& p : points)
    {
        const QVector3D d = p - centroid;
        pts2D.append(QVector2D(QVector3D::dotProduct(d, u), QVector3D::dotProduct(d, v)));
    }

    float sx = 0.0f, sy = 0.0f, sxx = 0.0f, syy = 0.0f, sxy = 0.0f, sxz = 0.0f, syz = 0.0f, sz = 0.0f;
    for (const QVector2D& p : pts2D)
    {
        const float x = p.x(), y = p.y();
        const float r2 = x * x + y * y;
        sx += x; sy += y; sxx += x * x; syy += y * y; sxy += x * y;
        sxz += x * r2; syz += y * r2; sz += r2;
    }
    const float nf = static_cast<float>(n);

    QVector3D def;  // Kasa's (D, E, F) coefficients
    if (!solve3x3(QVector3D(sxx, sxy, sx), QVector3D(sxy, syy, sy), QVector3D(sx, sy, nf),
        QVector3D(-sxz, -syz, -sz), def))
        return result;  // singular - collinear (or coincident) points, no well-defined circle

    const float radiusSq = def.x() * def.x() * 0.25f + def.y() * def.y() * 0.25f - def.z();
    if (radiusSq <= 0.0f)
        return result;

    const QVector2D center2D(-def.x() * 0.5f, -def.y() * 0.5f);

    result.center = centroid + u * center2D.x() + v * center2D.y();
    result.normal = eigenVec;
    result.radius = std::sqrt(radiusSq);

    // --- Angle-sort + gaps (wrap-around included, sums to exactly 360 by
    // construction). ---
    QVector<float> angles;
    angles.reserve(n);
    for (const QVector2D& p : pts2D)
    {
        float theta = std::atan2(p.y() - center2D.y(), p.x() - center2D.x()) * kRadToDeg;
        if (theta < 0.0f)
            theta += 360.0f;
        angles.append(theta);
    }

    // Which rotational sense (ascending vs descending angle) the rest of
    // this function walks in is otherwise arbitrary (tied to whichever way
    // orthonormalBasis() happened to orient u/v) - pick it to match how the
    // points were actually clicked instead, using the short way around from
    // the first pick to the second as the signal: if THAT way is the
    // descending direction, flip every angle (360-theta) so "ascending" -
    // the only direction the sort/rotate/gap code below ever walks - now
    // means what was originally "descending". A flip preserves every
    // angular relationship (gaps, adjacency) exactly, just mirrored, so
    // the rest of the algorithm needs no separate reversed-case logic.
    {
        const float shortWayForward = std::fmod(angles[1] - angles[0] + 360.0f, 360.0f);
        if (shortWayForward > 180.0f)
        {
            for (float& a : angles)
            {
                a = 360.0f - a;
                if (a >= 360.0f)
                    a = 0.0f;
            }
        }
    }

    QVector<int> order(n);
    for (int i = 0; i < n; ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(), [&angles](int a, int b) { return angles[a] < angles[b]; });

    // Rotate so input index 0 - the FIRST point the caller picked - leads
    // the list. Purely a reporting convention: it doesn't change which
    // gaps exist or their values, only where the list "starts" and which
    // gap is reported first, so the result reads as "the gap right after
    // your first click" instead of an arbitrary point tied to whichever
    // direction orthonormalBasis() happened to place at angle zero.
    const int firstPickPos = order.indexOf(0);
    if (firstPickPos > 0)
        std::rotate(order.begin(), order.begin() + firstPickPos, order.end());

    QVector<float> gaps;
    gaps.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        const float a0 = angles[order[i]];
        const float a1 = angles[order[(i + 1) % n]];
        // `order` was sorted ascending then rotated (see firstPickPos
        // above) - a rotation of a sorted cyclic sequence has exactly one
        // "descent" pair left (a1 strictly less than a0), which is the one
        // true wrap-around pair regardless of where the rotation put it,
        // so this is keyed off that directly rather than a fixed array
        // position. Strict less-than (not <=) so a genuine zero-length gap
        // from two picks landing at the exact same angle isn't mistaken
        // for a wrap.
        const float gap = (a1 < a0) ? (a1 - a0 + 360.0f) : (a1 - a0);
        gaps.append(gap);
    }

    result.angleSortedIndices = order;
    result.gapAnglesDegrees = gaps;
    result.valid = true;
    return result;
}

ConcentricityResult compareCircles(const QVector3D& center1, const QVector3D& axis1,
    const QVector3D& center2, const QVector3D& axis2)
{
    ConcentricityResult result;
    result.centerOffset = (center2 - center1).length();

    const QVector3D a1 = axis1.normalized();
    const QVector3D a2 = axis2.normalized();
    // abs() before acos: an axis direction is arbitrary in sign (nothing
    // about a circular edge picks which way its axis "points"), same
    // reasoning as compareFaces()/compareLines() treating a flipped
    // normal/direction as no less parallel.
    const float dot = std::clamp(std::abs(QVector3D::dotProduct(a1, a2)), 0.0f, 1.0f);
    result.axisAngleDegrees = std::acos(dot) * kRadToDeg;
    return result;
}

float angleBetweenRays(const QVector3D& vertex, const QVector3D& p1, const QVector3D& p2)
{
    const QVector3D v1 = (p1 - vertex).normalized();
    const QVector3D v2 = (p2 - vertex).normalized();
    // NOT abs()'d, unlike compareFaces()/compareLines()/compareCircles() -
    // see this function's doc comment for why the sign matters here.
    const float dot = std::clamp(QVector3D::dotProduct(v1, v2), -1.0f, 1.0f);
    return std::acos(dot) * kRadToDeg;
}

}
