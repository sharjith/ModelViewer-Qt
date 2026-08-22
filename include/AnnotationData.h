#pragma once

#include "MeasurementData.h"

#include <QUuid>
#include <QString>
#include <QVector3D>

// ---------------------------------------------------------------------------
// AnnotationData
//
// A saved Annotation is a free-text note anchored to a point on a mesh's
// surface, with a leader line pointing from that anchor to a draggable
// label position - the CAD-viewer equivalent of a sticky note. Document-
// level, not per-file (same reasoning as Measurement - see
// MeasurementData.h's own top comment), MVF-session-only for v1 (glTF has
// no native concept of an annotation, same as a measurement).
//
// Reuses MeasurementAnchorRef for the anchor rather than a duplicate
// AnnotationAnchorRef struct - it's already a general mesh-relative anchor
// format (triangle+barycentric, optional vertex snap), the "Measurement"
// in its name predates Annotation's existence. edgeIndex is unused here
// (v1 scope: a note anchors to a surface point, not an edge).
// ---------------------------------------------------------------------------
struct Annotation
{
    QUuid                 id;
    QString               text;              // multi-line, freeform
    MeasurementAnchorRef  anchor;

    // Show/hide toggle from the Annotation dialog's results list checkbox -
    // not undoable, same convention as Measurement::visible.
    bool                  visible = true;

    // Same two-field offset convention as Measurement::offsetReferenceDir/
    // offsetVector (see those fields' doc comments in MeasurementData.h for
    // the full reasoning) - offsetReferenceDir is captured once from the
    // camera's view direction at creation time so an undragged leader has a
    // stable default direction that doesn't swim as the user orbits;
    // leaderOffset holds the full dragged world-space offset once the user
    // repositions the label, at which point offsetReferenceDir stops being
    // consulted for this annotation.
    QVector3D             offsetReferenceDir;
    QVector3D             leaderOffset;
};
