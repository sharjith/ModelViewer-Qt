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
}
