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
// SimulationSliceController
//
// Draws the cut surfaces of simulation results (see ResultSlice.h): a data cache + per-frame draw driven by ModelViewer, like
// SimulationGlyphController. They are overlays, not scene meshes - no selection, no scene tree, no saving. A result that is hidden (or, in compare
// mode, not the one of the pane being drawn) gets none, as `resolve` returns null for it. Sections lie exactly on the Clipping Planes, where
// the closed skin's own cap is drawn too, so they are pulled toward the viewer by a depth offset.
// ---------------------------------------------------------------------------
class SimulationSliceController : public QObject, protected QOpenGLFunctions_4_5_Core, public IGpuContextResource
{
public:
	explicit SimulationSliceController(SceneRenderController& renderCtrl, QObject* parent = nullptr);

	void releaseGpuResources() override;
	void restoreGpuResources() override;

	void setSlices(const QUuid& meshUuid, std::vector<SliceDisplay> slices);
	void clearSlices(const QUuid& meshUuid);
	bool hasSlices() const { return !_sets.empty(); }
	// What is shown for a result (empty when nothing): for saving it in a snapshot.
	std::vector<SliceDisplay> slices(const QUuid& meshUuid) const
	{
		const auto found = _sets.find(meshUuid);
		return found == _sets.end() ? std::vector<SliceDisplay>() : found->second;
	}
	/// True when a result that is still there (and drawn now: `resolve` returns its mesh) has a lit iso-surface, as opposed to a flat section fill:
	/// those lie inside the solid. Results that are gone are ignored, so a closed result cannot leave the section caps switched off.
	bool hasIsoSurfaces(const std::function<const RenderableMesh*(const QUuid&)>& resolve) const
	{
		for (const auto& entry : _sets)
			if (resolve(entry.first))
				for (const SliceDisplay& slice : entry.second)
					if (slice.lit)
						return true;
		return false;
	}

	using MeshResolver = std::function<const RenderableMesh*(const QUuid&)>;
	void drawOverlay(Camera* camera, const MeshResolver& resolve);

private:
	bool _glFunctionsInitialized = false;
	SceneRenderController& _renderCtrl;
	std::map<QUuid, std::vector<SliceDisplay>> _sets;
	unsigned int _vao = 0;
	unsigned int _vbo = 0;
};
