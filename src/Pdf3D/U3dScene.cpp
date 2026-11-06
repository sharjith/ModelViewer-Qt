#include "U3dScene.h"

#include <cfloat>

namespace Pdf3D
{

bool U3dScene::computeBounds(QVector3D* outMin, QVector3D* outMax) const
{
    QVector3D lo(FLT_MAX, FLT_MAX, FLT_MAX);
    QVector3D hi(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    bool any = false;

    for (const U3dNode& node : nodes)
    {
        if (node.meshIndex < 0 || node.meshIndex >= meshes.size())
            continue;
        const QVector<float>& p = meshes[node.meshIndex].positions;
        for (int i = 0; i + 2 < p.size(); i += 3)
        {
            const QVector3D w = node.world.map(QVector3D(p[i], p[i + 1], p[i + 2]));
            lo.setX(qMin(lo.x(), w.x())); lo.setY(qMin(lo.y(), w.y())); lo.setZ(qMin(lo.z(), w.z()));
            hi.setX(qMax(hi.x(), w.x())); hi.setY(qMax(hi.y(), w.y())); hi.setZ(qMax(hi.z(), w.z()));
            any = true;
        }
    }

    if (any)
    {
        if (outMin) *outMin = lo;
        if (outMax) *outMax = hi;
    }
    return any;
}

} // namespace Pdf3D
