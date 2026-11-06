#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QVector3D>

// PDF 3D view dictionaries (/3DView) for the named views of a 3D PDF. Conventions verified in Adobe Acrobat (research doc
// 8.6, 8.14): /MS /M with /C2W = 12 numbers (rows: right, up, forward, then the eye; only up, forward and the eye are read), /CO
// = distance to the orbit centre, projection FOV 30 against the smaller viewport dimension.
namespace Pdf3D
{

enum class UpAxis { Y, Z };

// One /3DView dictionary looking from `eye` at `target` with the given up vector.
QByteArray makeView(const QString& name, const QVector3D& eye, const QVector3D& target, const QVector3D& up);

// The standard named views for a model whose bounding sphere is (center, radius): Isometric, Front, Back, Left, Right, Top,
// Bottom. The directions are defined for a Z-up model and mapped to Y-up when upAxis is Y ((x, y, z) -> (x, z, -y)).
// Front looks at the model from -Y (Z-up) or +Z (Y-up).
QList<QByteArray> standardViews(const QVector3D& center, float radius, UpAxis upAxis);

// Names of the views standardViews() writes, in order (for tests and the document).
QStringList standardViewNames();

} // namespace Pdf3D
