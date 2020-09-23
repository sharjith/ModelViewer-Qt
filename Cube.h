#pragma once

#include "Drawable.h"
#include "QuadMesh.h"

class Cube : public QuadMesh
{
public:
	Cube(QOpenGLShaderProgram* prog, float size = 1.0f);
    void setSize(const float& size);
	bool intersectsWithRay(const QVector3D& rayPos, const QVector3D& rayDir, QVector3D& outIntersectionPoint);
};
