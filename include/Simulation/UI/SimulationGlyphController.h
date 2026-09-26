#pragma once

#include "IGpuContextResource.h"
#include "SimulationGlyphs.h"

#include <QObject>
#include <QOpenGLFunctions_4_5_Core>
#include <QUuid>

#include <functional>
#include <map>

class Camera;
class RenderableMesh;
class SceneRenderController;

// ---------------------------------------------------------------------------
// SimulationGlyphController
//
// Draws the vector-field arrows of simulation results (see SimulationGlyphs.h). A pure data cache + per-frame draw, driven
// by ModelViewer (setGlyphs()/clearGlyphs()), like FillHolesController: the arrows are NOT scene meshes, so they take no
// part in selection, the scene tree, saving, export or path tracing. Each frame the arrow bases are read from the result
// mesh's current transformed vertices, so the arrows follow a deformed or moved result; a result that is hidden (or, in
// compare mode, not the one of the pane being drawn) gets none, as `resolve` returns null for it.
// ---------------------------------------------------------------------------
class SimulationGlyphController : public QObject, protected QOpenGLFunctions_4_5_Core, public IGpuContextResource
{
public:
	explicit SimulationGlyphController(SceneRenderController& renderCtrl, QObject* parent = nullptr);

	void releaseGpuResources() override;
	void restoreGpuResources() override;

	void setGlyphs(const QUuid& meshUuid, GlyphSet glyphs);
	void clearGlyphs(const QUuid& meshUuid);
	bool hasGlyphs() const { return !_sets.empty(); }

	// The mesh a result's arrows are drawn on, or null when it is gone or must not be drawn now.
	using MeshResolver = std::function<const RenderableMesh*(const QUuid&)>;
	void drawOverlay(Camera* camera, const MeshResolver& resolve);

private:
	bool _glFunctionsInitialized = false;
	SceneRenderController& _renderCtrl;
	std::map<QUuid, GlyphSet> _sets;
	unsigned int _vao = 0;
	unsigned int _vbo = 0;
};
