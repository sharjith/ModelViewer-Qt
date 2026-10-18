#pragma once

#include "IGpuContextResource.h"
#include "SimulationOverlays.h"

#include <QObject>
#include <QOpenGLFunctions_4_5_Core>
#include <QUuid>

#include <cstdint>
#include <functional>
#include <map>
#include <vector>

class Camera;
class RenderableMesh;
class SceneRenderController;

// ---------------------------------------------------------------------------
// SimulationStreamlineController
//
// Draws the streamlines of simulation results (see ResultStreamlines.h). Like SimulationSliceController it is a data cache plus a per-frame draw driven by
// ModelViewer: the lines are NOT scene meshes, so they take no part in selection, the scene tree, saving, export or path tracing. Each frame the vertices are
// moved by the result mesh's current transform; a result that is hidden (or, in compare mode, not the one of the pane being drawn) gets none.
// ---------------------------------------------------------------------------
class SimulationStreamlineController : public QObject, protected QOpenGLFunctions_4_5_Core, public IGpuContextResource
{
public:
	explicit SimulationStreamlineController(SceneRenderController& renderCtrl, QObject* parent = nullptr);

	void releaseGpuResources() override;
	void restoreGpuResources() override;

	void setLines(const QUuid& meshUuid, StreamlineDisplay lines);
	void clearLines(const QUuid& meshUuid);
	bool hasLines() const { return !_sets.empty(); }
	// What is shown for a result (empty when nothing): for saving it in a snapshot.
	StreamlineDisplay lines(const QUuid& meshUuid) const
	{
		const auto found = _sets.find(meshUuid);
		return found == _sets.end() ? StreamlineDisplay() : found->second;
	}
	// True when a result that is still there (and drawn now: `resolve` returns its mesh) has lines. Results that are gone are ignored, so a closed
	// result cannot leave the section caps switched off.
	bool hasLines(const std::function<const RenderableMesh*(const QUuid&)>& resolve) const
	{
		for (const auto& entry : _sets)
			if (resolve(entry.first))
				return true;
		return false;
	}

	using MeshResolver = std::function<const RenderableMesh*(const QUuid&)>;
	void drawOverlay(Camera* camera, const MeshResolver& resolve);

private:
	bool _glFunctionsInitialized = false;
	SceneRenderController& _renderCtrl;
	std::map<QUuid, StreamlineDisplay> _sets;
	unsigned int _vao = 0;
	unsigned int _vbo = 0;
};
