#pragma once

#include "U3dScene.h"

#include <QString>
#include <QVector3D>

#include <vector>

// Turns the geometry of a measurement or an annotation (the overlay's coloured segments and triangles, see
// Measurement/OverlayGraphic.h; here as plain data so this library has no application dependency) into nodes of a U3dScene for the
// Save as 3D PDF export. A U3D line set always draws as a black hairline, so segments become thin crossed ribbons (flat, double
// sided meshes: one ribbon facing the view the graphic was made for, one across it) and every colour becomes its own flat-shaded
// material; text, arrowheads and fills are the triangles as they are. The nodes are flagged `overlay` so they stay out of the parts
// list and the part count.
namespace Pdf3D
{

struct OverlayGraphicData
{
    QString             name;            // shown in the PDF's model tree
    QVector3D           viewDirection;   // the view the graphic was made for (ribbon orientation, depth bias towards the viewer)
    std::vector<float>  lines;           // x y z r g b per vertex, two vertices per segment
    std::vector<float>  triangles;       // x y z r g b per vertex, three vertices per triangle
};

class OverlayToU3d
{
public:
    // Appends one node per colour of `graphic` to `scene` (one mesh each) and returns how many were added. `lineWidth` is the ribbon
    // width in world units.
    static int append(U3dScene& scene, const OverlayGraphicData& graphic, float lineWidth);
};

} // namespace Pdf3D
