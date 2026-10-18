#pragma once

struct AssImpMeshData;
class SceneMesh;
class ViewportWidget;
class QOpenGLShaderProgram;

namespace AssImpMeshBuilder
{
    // Convert one AssImpMeshData record (produced by AssImpModelLoader) into a
    // fully-initialized SceneMesh ready for insertion into the scene mesh store.
    // Requires an active GL context (call from the GL thread).
    SceneMesh* build(const AssImpMeshData& data, QOpenGLShaderProgram* shader, ViewportWidget* viewportWidget);
}
