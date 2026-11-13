#include "LightFixtureRenderable.h"

LightFixtureRenderable::LightFixtureRenderable(QOpenGLShaderProgram* prog, LightFixtureGeometry::Part part)
    : RenderableMesh(prog, QStringLiteral("LightFixture")),
      _part(part),
      _drawMode(GL_TRIANGLES)
{
    LightFixtureGeometry geometry(part);
    _drawMode = geometry.isLines() ? GL_LINES : GL_TRIANGLES;
    setPrimitiveMode(_drawMode);   // lines have no picking triangles
    uploadGeometry(geometry);
}

void LightFixtureRenderable::render()
{
    if (!_vertexArrayObject.isCreated())
        return;
    ensureResident();

    const QMatrix4x4 modelMatrix = currentGlobalModelMatrix() * combinedRenderTransform();
    _prog->setUniformValue("modelMatrix", modelMatrix);

    _vertexArrayObject.bind();
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _indexBuffer.bufferId());
    glDrawElements(_drawMode, _nVerts, GL_UNSIGNED_INT, 0);
    _vertexArrayObject.release();
}

RenderableMesh* LightFixtureRenderable::clone()
{
    return new LightFixtureRenderable(_prog, _part);
}
