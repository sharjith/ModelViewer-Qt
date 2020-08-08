#pragma once

#include <vector>
#include "TriangleMesh.h"

class QuadMesh : public TriangleMesh 
{

public:
    QuadMesh(QOpenGLShaderProgram* prog, const QString name);
    virtual ~QuadMesh();
	virtual void render();
    virtual bool intersectsWithRay(const QVector3D& rayPos, const QVector3D& rayDir, QVector3D& outIntersectionPoint);
};
