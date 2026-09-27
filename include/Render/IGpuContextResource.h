#pragma once

#include <QCoreApplication>

// Implemented by anything owning GL objects that must be torn down before
// the QOpenGLContext they belong to is destroyed, and rebuilt once the new
// one is current (see ViewportWidget::initializeGL()'s context-recreation
// handling). Register instances with GpuResourceRegistry rather than
// hand-adding calls to ViewportWidget::releaseGLSceneResources()/
// initializeGL() directly.
class IGpuContextResource
{
public:
	virtual ~IGpuContextResource() = default;

	// Called with the dying context still current, right before Qt destroys
	// it. Must not leave any GL handle non-zero unless it's still valid
	// under the surviving (shared) context - see contextsAreShared() below.
	virtual void releaseGpuResources() = 0;

	// Called with the new context current. Must fully restore whatever
	// releaseGpuResources() tore down.
	virtual void restoreGpuResources() = 0;

	// Centralizes the AA_ShareOpenGLContexts check every release/restore
	// implementation needs: when contexts are shared, shareable GL object
	// types (buffers, textures, shader programs) survive context
	// recreation and must not be deleted - only non-shareable types (VAOs,
	// FBOs, renderbuffers) do. Callers still own the decision of WHICH of
	// their own handles are which type; this only centralizes the flag
	// itself, not the per-type logic built on top of it.
	static bool contextsAreShared()
	{
		return QCoreApplication::testAttribute(Qt::AA_ShareOpenGLContexts);
	}
};
