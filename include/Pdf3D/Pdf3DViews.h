#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVector3D>

// PDF 3D view dictionaries (/3DView) for the named views of a 3D PDF. Conventions verified in Adobe Acrobat (research doc
// 8.6, 8.14): /MS /M with /C2W = 12 numbers (rows: right, up, forward, then the eye; only up, forward and the eye are read), /CO
// = distance to the orbit centre, projection FOV 30 against the smaller viewport dimension.
namespace Pdf3D
{

enum class UpAxis { Y, Z };

// One /3DView dictionary looking from `eye` at `target` with the given up vector.
QByteArray makeView(const QString& name, const QVector3D& eye, const QVector3D& target, const QVector3D& up);

// Visibility override for one named node in a view (the /NA array of a /3DView).
struct NodeVisibility
{
    QString node;            // the U3D node name
    bool    visible = true;
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
};

// The /3DView dictionary for a CameraViewSpec. The orbit centre is the scene centre projected on the view axis (at least a
// small distance in front of the eye).
QByteArray makeCameraView(const CameraViewSpec& spec, const QVector3D& sceneCenter, float sceneRadius);

// The standard named views for a model whose bounding sphere is (center, radius): Isometric, Front, Back, Left, Right, Top,
// Bottom. The directions are defined for a Z-up model and mapped to Y-up when upAxis is Y ((x, y, z) -> (x, z, -y)).
// Front looks at the model from -Y (Z-up) or +Z (Y-up).
QList<QByteArray> standardViews(const QVector3D& center, float radius, UpAxis upAxis);

// Names of the views standardViews() writes, in order (for tests and the document).
QStringList standardViewNames();

} // namespace Pdf3D
