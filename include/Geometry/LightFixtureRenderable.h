#pragma once

#include "RenderableMesh.h"
#include "LightFixtureGeometry.h"

// One part of a light gizmo fixture (see LightFixtureGeometry). Drawn by the light fixture shader, as triangles or, for the beam cone,
// as lines; it ignores the material pipeline of RenderableMesh::render() because a gizmo has no material.
class LightFixtureRenderable : public RenderableMesh
{
public:
    LightFixtureRenderable(QOpenGLShaderProgram* prog, LightFixtureGeometry::Part part);

    void render() override;
    RenderableMesh* clone() override;

private:
    LightFixtureGeometry::Part _part;
    GLenum _drawMode;
};
