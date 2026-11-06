#include "Pdf3DViews.h"

#include "PdfAppender.h"

#include <QStringList>

#include <algorithm>
#include <cmath>

namespace Pdf3D
{
namespace
{

struct ViewDef
{
    const char* name;
    QVector3D   direction;   // from the model centre towards the eye, Z-up frame
    QVector3D   up;          // Z-up frame
};

const ViewDef* viewDefs(int* count)
{
    static const ViewDef defs[] = {
        {"Isometric", QVector3D(1.0f, -1.0f, 0.8f), QVector3D(0.0f, 0.0f, 1.0f)},
        {"Front",     QVector3D(0.0f, -1.0f, 0.0f), QVector3D(0.0f, 0.0f, 1.0f)},
        {"Back",      QVector3D(0.0f, 1.0f, 0.0f),  QVector3D(0.0f, 0.0f, 1.0f)},
        {"Left",      QVector3D(-1.0f, 0.0f, 0.0f), QVector3D(0.0f, 0.0f, 1.0f)},
        {"Right",     QVector3D(1.0f, 0.0f, 0.0f),  QVector3D(0.0f, 0.0f, 1.0f)},
        {"Top",       QVector3D(0.0f, 0.0f, 1.0f),  QVector3D(0.0f, 1.0f, 0.0f)},
        {"Bottom",    QVector3D(0.0f, 0.0f, -1.0f), QVector3D(0.0f, -1.0f, 0.0f)},
    };
    *count = static_cast<int>(sizeof(defs) / sizeof(defs[0]));
    return defs;
}

// Z-up -> Y-up: (x, y, z) -> (x, z, -y)
QVector3D toYUp(const QVector3D& v)
{
    return QVector3D(v.x(), v.z(), -v.y());
}

} // namespace

QByteArray makeView(const QString& name, const QVector3D& eye, const QVector3D& target, const QVector3D& up)
{
    const QVector3D forward = (target - eye).normalized();
    QVector3D upN = up.normalized();
    // up must not be parallel to forward
    if (qAbs(QVector3D::dotProduct(forward, upN)) > 0.999f)
        upN = (qAbs(forward.z()) < 0.9f) ? QVector3D(0.0f, 0.0f, 1.0f) : QVector3D(0.0f, 1.0f, 0.0f);
    const QVector3D right = QVector3D::crossProduct(forward, upN).normalized();
    const QVector3D trueUp = QVector3D::crossProduct(right, forward);

    // Rows as written by commercial 3D PDF producers: a right-handed set (right' = up x forward), then the eye. Acrobat reads
    // only up, forward and the eye (research doc 8.14).
    const QVector3D rightRow = QVector3D::crossProduct(trueUp, forward);
    const float c2w[12] = {rightRow.x(), rightRow.y(), rightRow.z(), trueUp.x(), trueUp.y(), trueUp.z(),
                           forward.x(), forward.y(), forward.z(), eye.x(), eye.y(), eye.z()};
    QByteArray matrix;
    for (int i = 0; i < 12; ++i)
    {
        if (i)
            matrix += ' ';
        matrix += PdfAppender::pdfNumber(c2w[i]);
    }

    const QByteArray n = PdfAppender::pdfString(name);
    return "<< /Type /3DView /XN " + n + " /IN " + n + " /MS /M /C2W [" + matrix + "] /CO " +
           PdfAppender::pdfNumber((target - eye).length()) +
           " /P << /Type /3DProjection /Subtype /P /PS /Min /FOV 30 >>"
           " /BG << /Type /3DBG /Subtype /SC /C [1 1 1] >>"
           " /RM << /Type /3DRenderMode /Subtype /Solid >>"
           " /LS << /Type /3DLightingScheme /Subtype /CAD >> >>";
}

QList<QByteArray> standardViews(const QVector3D& center, float radius, UpAxis upAxis)
{
    // distance at which the bounding sphere fits the smaller viewport dimension (FOV 30), with a 25% margin
    const float distance = std::max(radius, 1.0e-6f) / std::sin(15.0f * 3.14159265f / 180.0f) * 1.25f;

    QList<QByteArray> views;
    int count = 0;
    const ViewDef* defs = viewDefs(&count);
    for (int i = 0; i < count; ++i)
    {
        QVector3D dir = defs[i].direction.normalized();
        QVector3D up = defs[i].up;
        if (upAxis == UpAxis::Y)
        {
            dir = toYUp(dir);
            up = toYUp(up);
        }
        views.append(makeView(QString::fromLatin1(defs[i].name), center + dir * distance, center, up));
    }
    return views;
}

QStringList standardViewNames()
{
    QStringList names;
    int count = 0;
    const ViewDef* defs = viewDefs(&count);
    for (int i = 0; i < count; ++i)
        names << QString::fromLatin1(defs[i].name);
    return names;
}

} // namespace Pdf3D
