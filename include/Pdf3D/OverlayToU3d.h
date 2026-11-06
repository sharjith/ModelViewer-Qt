#pragma once

#include "U3dScene.h"

#include <QString>
#include <QStringList>
#include <QVector3D>

#include <vector>

// Turns the geometry of a measurement or an annotation (the overlay's coloured segments and triangles, see
// Measurement/OverlayGraphic.h; here as plain data so this library has no application dependency) into nodes of a U3dScene for the
// Save as 3D PDF export. A U3D line set always draws as a black hairline, so segments become thin crossed ribbons (flat, double
// sided meshes: one ribbon facing the view the graphic was made for, one across it) and the colours are vertex colours of one mesh; text, arrowheads and fills are the triangles as they are. The nodes are flagged `overlay` so they stay out of the parts
// list and the part count.
namespace Pdf3D
{

struct OverlayGraphicData
{
    QString             name;            // shown in the PDF's model tree
    QString             folder;          // group node the item is filed under in the model tree, e.g. "Measurements" (empty: the root); ASCII
    QVector3D           viewDirection;   // the view the graphic was made for (ribbon orientation, depth bias towards the viewer)
    QStringList         followsNodes;    // names of the part nodes (U3dNode::name) the item is attached to: it moves with them in a view
                                         // that moves them, and is hidden where they move differently
    std::vector<float>  lines;           // x y z r g b per vertex, two vertices per segment
    std::vector<float>  triangles;       // x y z r g b per vertex, three vertices per triangle
};

class OverlayToU3d
{
public:
    // Appends ONE node for `graphic` to `scene` (one mesh whose triangles carry their colours as vertex colours, so the model tree shows a
    // single entry per item, filed under the group node `graphic.folder`, which is created on first use) and returns how many nodes were added (0 for a graphic without geometry). `lineWidth` is the ribbon width in
    // world units.
    static int append(U3dScene& scene, const OverlayGraphicData& graphic, float lineWidth);
};

} // namespace Pdf3D
