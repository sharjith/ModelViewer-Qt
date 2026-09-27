#pragma once

#include "RenderableMesh.h"
#include <QVector>

class ViewCubeMesh : public RenderableMesh
{
public:
    struct RegionHit
    {
        int regionId = -1;
        QVector3D outwardNormal;
    };

    ViewCubeMesh(QOpenGLShaderProgram* prog, float size = 1.0f, float chamferRatio = 0.24f);

    RenderableMesh* clone() override;

    void setParameters(float size, float chamferRatio = 0.24f);
    void renderRegion(int regionId);
    bool pickRegion(const QVector3D& rayOriginWorld,
                    const QVector3D& rayDirWorld,
                    const QMatrix4x4& modelMatrix,
                    RegionHit& outHit) const;
    int regionCount() const { return _regionNormals.size(); }
    QVector3D regionNormal(int regionId) const;
    bool isPrimaryFaceRegion(int regionId) const;

    float size() const { return _size; }
    float chamferRatio() const { return _chamferRatio; }

private:
    struct TriangleRegion
    {
        QVector3D a;
        QVector3D b;
        QVector3D c;
        int regionId = -1;
        QVector3D outwardNormal;
        unsigned int elementOffset = 0;
    };

    float _size = 1.0f;
    float _chamferRatio = 0.24f;
    QVector<QVector3D> _regionNormals;
    QVector<TriangleRegion> _triangleRegions;
};
