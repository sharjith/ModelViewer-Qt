#pragma once

#include "RenderableMesh.h"
#include "Cube.h"

class CubeRenderable : public RenderableMesh
{
public:
    explicit CubeRenderable(QOpenGLShaderProgram* prog, float size = 1.0f);

    void setSize(float size);

    RenderableMesh* clone() override;

private:
    Cube _cube;
};
