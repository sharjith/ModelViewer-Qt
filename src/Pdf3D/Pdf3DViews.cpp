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

namespace
{

constexpr float kOrthoFovDegrees = 2.0f;
const char* const kPerspective30 = "<< /Type /3DProjection /Subtype /P /PS /Min /FOV 30 >>";

QByteArray buildView(const QString& name, const QVector3D& eye, const QVector3D& target, const QVector3D& up,
                     const QByteArray& projection, const QByteArray& nodes)
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

    const QByteArray n = PdfAppender::pdfTextString(name);
    return "<< /Type /3DView /XN " + n + " /IN " + n + " /MS /M /C2W [" + matrix + "] /CO " +
           PdfAppender::pdfNumber((target - eye).length()) +
           " /P " + projection +
           (nodes.isEmpty() ? QByteArray() : " /NA [" + nodes + "]") +
           " /BG << /Type /3DBG /Subtype /SC /C [1 1 1] >>"
           " /RM << /Type /3DRenderMode /Subtype /Solid >>"
           " /LS << /Type /3DLightingScheme /Subtype /CAD >> >>";
}

} // namespace

QByteArray makeView(const QString& name, const QVector3D& eye, const QVector3D& target, const QVector3D& up)
{
    return buildView(name, eye, target, up, kPerspective30, QByteArray());
}

QByteArray makeCameraView(const CameraViewSpec& spec, const QVector3D& sceneCenter, float sceneRadius)
{
    QVector3D dir = spec.direction.normalized();
    if (dir.isNull())
        dir = QVector3D(0.0f, 0.0f, -1.0f);
    const float minDistance = std::max(sceneRadius, 1.0e-6f) * 0.05f;
    const float distance = std::max(QVector3D::dotProduct(sceneCenter - spec.eye, dir), minDistance);
    QVector3D target = spec.eye + dir * distance;
    QVector3D eye = spec.eye;

    QByteArray projection;
    if (spec.orthographic)
    {
        // Acrobat's orthographic scale (/OS) convention is not verified, so an orthographic saved view is written as a
        // narrow-field perspective view: with a vertical field of kOrthoFovDegrees the eye is moved back along the view axis until
        // `orthoRange` world units span the smaller viewport dimension, the same extent the viewport shows. The depth distortion
        // of a 2 degree lens is negligible.
        const float range = std::max(spec.orthoRange, 1.0e-6f);
        const float halfFov = 0.5f * kOrthoFovDegrees * 3.14159265f / 180.0f;
        const float back = 0.5f * range / std::tan(halfFov);
        eye = target - dir * back;
        projection = "<< /Type /3DProjection /Subtype /P /PS /Min /FOV " + PdfAppender::pdfNumber(kOrthoFovDegrees) + " >>";
    }
    else
    {
        projection = "<< /Type /3DProjection /Subtype /P /PS /Min /FOV " + PdfAppender::pdfNumber(spec.fovDegrees) + " >>";
    }

    QByteArray nodes;
    for (const NodeVisibility& n : spec.nodes)
    {
        if (!nodes.isEmpty())
            nodes += ' ';
        nodes += "<< /Type /3DNode /N " + PdfAppender::pdfTextString(n.node) + " /V " + (n.visible ? "true" : "false");
        if (n.hasMatrix)
        {
            // 12 numbers: the 3x3 part as rows (images of the basis vectors, i.e. the columns of the Qt matrix), then the translation
            const float* m = n.matrix.constData();
            const int idx[12] = {0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14};
            nodes += " /M [";
            for (int i = 0; i < 12; ++i)
            {
                if (i)
                    nodes += ' ';
                nodes += PdfAppender::pdfNumber(m[idx[i]]);
            }
            nodes += "]";
        }
        nodes += " >>";
    }
    return buildView(spec.name, eye, target, spec.up, projection, nodes);
}

QList<QByteArray> standardViews(const QVector3D& center, float radius, UpAxis upAxis)
{
    // distance at which the bounding sphere fits the smaller viewport dimension (FOV 30), with a 5% margin
    const float distance = std::max(radius, 1.0e-6f) / std::sin(15.0f * 3.14159265f / 180.0f) * 1.05f;

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

QList<QByteArray> standardViewsFitted(const QVector3D& boundsMin, const QVector3D& boundsMax, float aspect, UpAxis upAxis)
{
    const QVector3D center = (boundsMin + boundsMax) * 0.5f;
    const float radius = 0.5f * (boundsMax - boundsMin).length();
    const float a = std::max(aspect, 1.0e-3f);
    const float tanMin = std::tan(15.0f * 3.14159265f / 180.0f);   // the view's field of view is 30 degrees on the smaller side
    const float tanX = (a >= 1.0f) ? tanMin * a : tanMin;
    const float tanY = (a >= 1.0f) ? tanMin : tanMin / a;

    QVector3D corners[8];
    for (int i = 0; i < 8; ++i)
        corners[i] = QVector3D((i & 1) ? boundsMax.x() : boundsMin.x(), (i & 2) ? boundsMax.y() : boundsMin.y(),
                               (i & 4) ? boundsMax.z() : boundsMin.z()) - center;

    QList<QByteArray> views;
    int count = 0;
    const ViewDef* defs = viewDefs(&count);
    for (int i = 0; i < count; ++i)
    {
        QVector3D dir = defs[i].direction.normalized();   // from the centre towards the eye
        QVector3D up = defs[i].up;
        if (upAxis == UpAxis::Y)
        {
            dir = toYUp(dir);
            up = toYUp(up);
        }
        // the same basis makeView() derives: forward = -dir, right = forward x up, true up = right x forward
        const QVector3D forward = -dir;
        QVector3D upN = up.normalized();
        if (qAbs(QVector3D::dotProduct(forward, upN)) > 0.999f)
            upN = (qAbs(forward.z()) < 0.9f) ? QVector3D(0.0f, 0.0f, 1.0f) : QVector3D(0.0f, 1.0f, 0.0f);
        const QVector3D right = QVector3D::crossProduct(forward, upN).normalized();
        const QVector3D trueUp = QVector3D::crossProduct(right, forward);

        // a point at lateral offset (x, y) and depth z (along forward, from the centre) is seen from distance d when
        // d + z >= max(|x| / tanX, |y| / tanY): solve for the smallest d over all corners
        float distance = 0.0f;
        for (const QVector3D& p : corners)
        {
            const float x = 1.05f * std::fabs(QVector3D::dotProduct(p, right));
            const float y = 1.05f * std::fabs(QVector3D::dotProduct(p, trueUp));
            const float z = QVector3D::dotProduct(p, forward);
            distance = std::max(distance, std::max(x / tanX, y / tanY) - z);
        }
        distance = std::max(distance, std::max(radius, 1.0e-6f) * 0.1f);
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
