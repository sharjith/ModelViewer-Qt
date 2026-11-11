#pragma once

#include <QString>
#include <QUuid>
#include <QVector>
#include <QVector3D>

#include <vector>

// ---------------------------------------------------------------------------
// OverlayGraphic
//
// The geometry the measurement and annotation overlays draw for ONE item (a measurement or an annotation), captured without any
// OpenGL work for exports (Save as 3D PDF): the segments and triangles the overlay would upload, in world space, as the overlay
// builds them: 6 floats per vertex (x, y, z, r, g, b), lines two vertices per segment, triangles three per triangle. The text of
// a label is part of it (filled glyph triangles with a dark halo), written in the plane the item's orientation setting gives it.
//
// Made by MeasurementController::captureGraphic() / AnnotationController::captureGraphic() with a scratch camera: orthographic,
// looking along the view the item was created from (offsetReferenceDir), so a label oriented for its creation view comes out that
// way whatever the live camera shows.
// ---------------------------------------------------------------------------
struct OverlayGraphic
{
    bool visibleInViewer = true;
	QUuid               id;
	bool                annotation = false;   // an annotation, else a measurement
	QString             name;                 // what the user sees for it (a measurement's summary, an annotation's text)
	QVector3D           viewDirection;        // the view the graphic was captured for
	QVector<QUuid>      meshUuids;            // the meshes the item is attached to (its anchors), so an export can move it with them
	std::vector<float>  lines;                // x y z r g b per vertex, two per segment
	std::vector<float>  triangles;            // x y z r g b per vertex, three per triangle
};
