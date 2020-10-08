#pragma once

#include <vector>
#include "TriangleMesh.h"

class GridMesh : public TriangleMesh
{
public:
	GridMesh(QOpenGLShaderProgram* prog, const QString name, unsigned int slices, unsigned int stacks);
    virtual ~GridMesh();
	virtual bool intersectsWithRay(const QVector3D& rayPos, const QVector3D& rayDir, QVector3D& outIntersectionPoint);

protected:
	unsigned int _slices;
	unsigned int _stacks;
};
