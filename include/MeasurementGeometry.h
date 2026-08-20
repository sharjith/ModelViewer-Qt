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
}
