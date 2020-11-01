#ifndef TRIANGLEMOLLERTRUMBORE_H
#define TRIANGLEMOLLERTRUMBORE_H

#include "Triangle.h"

class TriangleMollerTrumbore : public Triangle
{
public:
    TriangleMollerTrumbore(const QVector3D& vertex1, const QVector3D& vertex2, const QVector3D& vertex3, QObject *parent = nullptr);
    ~TriangleMollerTrumbore();

    virtual bool intersectsWithRay(const QVector3D& rayPos, const QVector3D& rayDir, QVector3D& outIntersectionPoint);
};

#endif // TRIANGLEMOLLERTRUMBORE_H
