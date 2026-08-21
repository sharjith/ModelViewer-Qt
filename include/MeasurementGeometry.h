#pragma once

#include <QVector3D>
#include <QVector>

// ---------------------------------------------------------------------------
// MeasurementGeometry
//
// Pure geometry helpers for the arc-radius measurement tools. Kept separate
// from ViewportWidget so the math is independently reusable (e.g. by the
// Measurement dialog's results list, which needs the same numbers
// ViewportWidget's overlay renders, without needing a Camera/GL context).
// ---------------------------------------------------------------------------
namespace MeasurementGeometry
{
    // The unique circle passing through 3 non-collinear points in 3D
    // ("3-Point Arc Radius"). Returns false for degenerate (collinear, or
    // coincident) input, in which case outputs are left untouched.
    bool circumcircle3Point(const QVector3D& a, const QVector3D& b, const QVector3D& c,
        QVector3D& outCenter, QVector3D& outNormal, float& outRadius);

    // A circle from an explicit center plus 2 points assumed to lie on it
    // ("Center + 2-Point Arc Radius") - radius is the average of the two
    // center-to-point distances, tolerant of the two picks not being
    // perfectly equidistant (e.g. slightly different vertex snaps). Returns
    // false if center/p1/p2 are collinear (no well-defined plane/normal).
    bool circleFromCenterAndTwoPoints(const QVector3D& center, const QVector3D& p1, const QVector3D& p2,
        QVector3D& outNormal, float& outRadius);

    // `segments` points evenly spaced around the circle (center, normal,
    // radius), for outline rendering. The basis used to place point 0 is
    // arbitrary (not tied to any of the original picked points) - callers
    // that need the outline to visually pass near a specific picked point
    // should not rely on point ordering here, only on the circle's shape.
    QVector<QVector3D> circlePolyline(const QVector3D& center, const QVector3D& normal, float radius, int segments = 48);

    // Result of comparing two face planes ("Face to Face") - exactly one of
    // distance/angleDegrees is meaningful, indicated by isParallel. Planes
    // within parallelToleranceDegrees of each other (by the ACUTE angle
    // between their normals - sign/winding-independent, so a flipped normal
    // on an otherwise-parallel face still reads as parallel) report a
    // perpendicular distance instead of a near-zero angle, since that's
    // almost always what's actually wanted for two flat faces.
    struct FaceToFaceResult
    {
        bool  isParallel = false;
        float distance = 0.0f;      // meaningful only if isParallel
        float angleDegrees = 0.0f;  // meaningful only if !isParallel, range [0, 90]
    };
    FaceToFaceResult compareFaces(const QVector3D& p1, const QVector3D& n1,
        const QVector3D& p2, const QVector3D& n2, float parallelToleranceDegrees = 5.0f);

    // Perpendicular distance from `point` to the plane through `planePoint`
    // with (not-necessarily-normalized) normal `planeNormal` ("Point to Face").
    float pointToPlaneDistance(const QVector3D& point, const QVector3D& planePoint, const QVector3D& planeNormal);

    // Perpendicular distance from `point` to the INFINITE line through
    // `linePoint` with (not-necessarily-normalized) direction `lineDir`
    // ("Edge to Vertex") - not clamped to a finite segment, same convention
    // as pointToPlaneDistance() treating a face as an infinite plane rather
    // than stopping at the picked triangle's edges.
    float pointToLineDistance(const QVector3D& point, const QVector3D& linePoint, const QVector3D& lineDir);

    // The point on the INFINITE line through `linePoint`/`lineDir` closest
    // to `point` - the perpendicular foot, used for rendering the Edge to
    // Vertex dimension's connector.
    QVector3D closestPointOnLine(const QVector3D& point, const QVector3D& linePoint, const QVector3D& lineDir);

    // Result of comparing two edge directions ("Edge to Edge") - the line
    // analog of FaceToFaceResult, same "exactly one field meaningful"
    // convention and same sign/winding-independent parallel test. Distance
    // for the parallel case is the perpendicular distance between the two
    // (infinite) lines; the non-parallel (including skew) case reports only
    // the angle between them, same as FaceToFace's non-parallel case
    // reporting only an angle rather than also attempting a minimum-
    // distance-between-skew-lines figure.
    struct EdgeToEdgeResult
    {
        bool  isParallel = false;
        float distance = 0.0f;      // meaningful only if isParallel
        float angleDegrees = 0.0f;  // meaningful only if !isParallel, range [0, 90]
    };
    EdgeToEdgeResult compareLines(const QVector3D& p1, const QVector3D& d1,
        const QVector3D& p2, const QVector3D& d2, float parallelToleranceDegrees = 5.0f);

    // Result of comparing an edge direction against a face plane ("Edge to
    // Face") - same "exactly one field meaningful" convention. Note this is
    // a LINE-vs-PLANE comparison, not line-vs-line/plane-vs-plane like the
    // other compare*() helpers, so "parallel" means the edge lies within a
    // plane parallel to the face (direction perpendicular to the face
    // normal) - the opposite alignment condition from compareFaces()/
    // compareLines(), which both test for near-zero angle between two
    // normals/directions of the SAME kind.
    struct EdgeToFaceResult
    {
        bool  isParallel = false;
        float distance = 0.0f;      // meaningful only if isParallel
        float angleDegrees = 0.0f;  // meaningful only if !isParallel - angle between the edge and the face plane, range [0, 90]
    };
    EdgeToFaceResult compareEdgeToFace(const QVector3D& edgePoint, const QVector3D& edgeDir,
        const QVector3D& facePoint, const QVector3D& faceNormal, float parallelToleranceDegrees = 5.0f);

    // Best-fit circle through N (>= 3) points that aren't necessarily exactly
    // coplanar/concyclic ("Pitch Circle") - unlike circumcircle3Point()'s
    // exact 3-point solve, this least-squares fits a plane through the
    // points first (order-independent - correct regardless of the order
    // they were picked in), then a 2D circle within that plane. Also reports
    // each input point's angular position sorted around the circle, and the
    // gaps between angularly-consecutive points (wrapping, sums to exactly
    // 360 by construction) - the actual "is this bolt pattern uniformly
    // spaced" answer a pitch circle measurement exists to give. The fit
    // itself is pick-order-independent, but the REPORTED list is rotated to
    // start at points[0] (the first point the caller passed in) - purely a
    // display convention, so "gap 1" reads as "the gap right after your
    // first pick" instead of landing at an arbitrary point tied to the
    // internal plane basis.
    struct PitchCircleResult
    {
        bool  valid = false;
        QVector3D center;
        QVector3D normal;
        float radius = 0.0f;              // PCD = 2 * radius
        QVector<int> angleSortedIndices;  // input indices, angle-sorted, rotated to start at index 0
        QVector<float> gapAnglesDegrees;  // one gap per input point, in angleSortedIndices order
    };
    PitchCircleResult fitPitchCircle(const QVector<QVector3D>& points);
}
