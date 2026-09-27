#include "RenderableMeshGpuResourceAdapter.h"
#include "RenderableMesh.h"
#include <utility>

RenderableMeshGpuResourceAdapter::RenderableMeshGpuResourceAdapter(RenderableMesh* mesh, std::function<QOpenGLShaderProgram*()> shaderResolver)
	: _mesh(mesh), _shaderResolver(std::move(shaderResolver))
{
}

void RenderableMeshGpuResourceAdapter::releaseGpuResources()
{
	_mesh->releaseContextBoundGpuResources();
}

void RenderableMeshGpuResourceAdapter::restoreGpuResources()
{
	_mesh->restoreContextBoundGpuResources(_shaderResolver());
}
