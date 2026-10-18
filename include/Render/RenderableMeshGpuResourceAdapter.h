#pragma once

#include "IGpuContextResource.h"
#include <functional>

class RenderableMesh;
class QOpenGLShaderProgram;

// Wraps an existing RenderableMesh-derived object's inherited
// releaseContextBoundGpuResources()/restoreContextBoundGpuResources(prog)
// pair (see RenderableMesh.h) as an IGpuContextResource, for the scene-
// decoration objects (floor, skybox, axis cone, viewcube, light helpers,
// clipping planes) that already have this pair via inheritance but never
// used it - they were fully deleted and reconstructed on every context
// recreation instead.
//
// The shader is resolved lazily via shaderResolver, not stored eagerly:
// some of these objects (via ViewportWidget) register before
// createShaderPrograms() has ever run, so no valid QOpenGLShaderProgram*
// exists yet at registration time.
class RenderableMeshGpuResourceAdapter : public IGpuContextResource
{
public:
	RenderableMeshGpuResourceAdapter(RenderableMesh* mesh, std::function<QOpenGLShaderProgram*()> shaderResolver);

	void releaseGpuResources() override;
	void restoreGpuResources() override;

private:
	RenderableMesh* _mesh; // non-owning
	std::function<QOpenGLShaderProgram*()> _shaderResolver;
};
