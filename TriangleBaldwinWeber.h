#ifndef TRIANGLEBALDWINWEBER_H
#define TRIANGLEBALDWINWEBER_H

#include "Triangle.h"

class TriangleBaldwinWeber : public Triangle
{
public:
    TriangleBaldwinWeber(const QVector3D& vertex1, const QVector3D& vertex2, const QVector3D& vertex3, QObject *parent = nullptr);
    virtual bool intersectsWithRay(const QVector3D &rayPos, const QVector3D &rayDir, QVector3D &outIntersectionPoint);

private:
    float _transformation[9];
    unsigned char _fixedColumn;
};

#endif // TRIANGLEBALDWINWEBER_H
