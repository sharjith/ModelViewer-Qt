#pragma once

#include <QByteArray>
#include <QList>
#include <QSet>
#include <QString>
#include <QMatrix4x4>
#include <QStringList>
#include <QVector>
#include <QVector3D>

#include <optional>

// PDF 3D view dictionaries (/3DView) for the named views of a 3D PDF. Conventions verified in Adobe Acrobat (research doc
// 8.6, 8.14): /MS /M with /C2W = 12 numbers (rows: right, up, forward, then the eye; only up, forward and the eye are read), /CO
// = distance to the orbit centre, projection FOV 30 against the smaller viewport dimension.
namespace Pdf3D
{

enum class UpAxis { Y, Z };

// One /3DView dictionary looking from `eye` at `target` with the given up vector.
QByteArray makeView(const QString& name, const QVector3D& eye, const QVector3D& target, const QVector3D& up);

// Override for one named node in a view (the /NA array of a /3DView): its visibility and optionally its transform.
struct NodeVisibility
{
    QString    node;               // the U3D node name
    bool       visible = true;
    bool       hasMatrix = false;  // write /M: replaces the node's transform (used for exploded layouts)
    QMatrix4x4 matrix;             // local-to-world, Qt layout; nodes are direct children of the scene root
};

// An Acrobat 3D cross section (section view): one plane perpendicular to X, Y or Z at a world position, keeping one side, with
// the cut face capped. A view lists its planes (/SA); every view writes the list, empty when it has no section, because Acrobat
// keeps the previous view's section on otherwise.
struct SectionPlane
{
    int    axis = 2;               // 0 = X, 1 = Y, 2 = Z: the axis the plane is perpendicular to
    double position = 0.0;         // world coordinate on that axis (the PDF writes it relative to the scene centre)
    bool   keepPositive = false;   // keep the +axis side (y >= position), else the -axis side
};

// A view built from a saved camera (Cameras panel, authored glTF camera, scene state) instead of a standard direction.
// eye/direction/up are in the exported scene's space. For a scene state, `nodes` lists the visibility of every part.
struct CameraViewSpec
{
    QString                 name;
    QVector3D               eye;
    QVector3D               direction;        // looking direction (need not be normalised)
    QVector3D               up{0.0f, 1.0f, 0.0f};
    bool                    orthographic = false;
    float                   fovDegrees = 45.0f;   // vertical field of view (perspective)
    float                   orthoRange = 0.0f;    // world length shown across the smaller viewport dimension (orthographic)
    QVector<NodeVisibility> nodes;                // empty: the view leaves visibility alone
    QVector<SectionPlane>   sections;             // the cross sections of this view (empty: the view has none)
    bool                    showOverlays = true;  // show the measurement and annotation nodes in this view
    // The PDF render mode of the view: the /Subtype of its /3DRenderMode ("Solid", "SolidWireframe", "Wireframe", "HiddenWireframe",
    // "SolidOutline", ...). A scene state brings the display mode it was saved with.
    QByteArray              renderMode = "Solid";
    std::optional<QSet<int>> visibleOverlayItems; // absent: all included overlay items
};

// The /3DCrossSection dictionary for `plane` in a scene whose bounding box centre is `sceneCenter`. Verified in Acrobat (research doc,
// section views): the centre /C is relative to the scene's bounding box centre, and the rotation /O = [x y z] degrees with one angle
// null decides the plane and the kept side: Z: [0 0 null] keeps z <= position, [180 0 null] keeps z >= position; Y: [-90 0 null]
// keeps y <= position, [90 0 null] keeps y >= position; X: [0 90 null] keeps x <= position, [0 -90 null] keeps x >= position.
QByteArray makeCrossSection(const SectionPlane& plane, const QVector3D& sceneCenter);

// The /3DView dictionary for a CameraViewSpec. The orbit centre is the scene centre projected on the view axis (at least a
// small distance in front of the eye).
QByteArray makeCameraView(const CameraViewSpec& spec, const QVector3D& sceneCenter, float sceneRadius);

// The standard named views for a model whose bounding sphere is (center, radius): Isometric, Front, Back, Left, Right, Top,
// Bottom. The directions are defined for a Z-up model and mapped to Y-up when upAxis is Y ((x, y, z) -> (x, z, -y)).
// Front looks at the model from -Y (Z-up) or +Z (Y-up).
QList<QByteArray> standardViews(const QVector3D& center, float radius, UpAxis upAxis, const QByteArray& nodeOverrides = QByteArray());

// Same views, but each one is fitted to the model's bounding box instead of its bounding sphere: the distance is the smallest
// at which all eight box corners fit the 3D area (aspect = width / height; the field of view applies to the smaller side),
// with a 5% margin. A long, flat model then fills the view instead of floating in a sphere-sized frame.
QList<QByteArray> standardViewsFitted(const QVector3D& boundsMin, const QVector3D& boundsMax, float aspect, UpAxis upAxis,
                                      const QByteArray& nodeOverrides = QByteArray());

// The /NA entries (dictionaries separated by spaces, without the brackets) for `nodes`: visibility and, where set, the node transform.
// A view only changes the parts it lists and Acrobat keeps the previous view's changes for the others, so views that should show
// the default state must list those parts too (the exporter does this).
QByteArray makeNodeOverrides(const QVector<NodeVisibility>& nodes);

// Names of the views standardViews() writes, in order (for tests and the document).
QStringList standardViewNames();

} // namespace Pdf3D
