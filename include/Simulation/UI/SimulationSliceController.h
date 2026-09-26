#pragma once

#include "IGpuContextResource.h"

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

// A coloured triangle set cut out of a result's volume (a data-coloured section, an iso-surface), ready to draw. Positions are in the result
// mesh's own frame (the dataset's coordinates), colours are final RGB.
struct SliceDisplay
{
	std::vector<float> positions;         // 3 per vertex
	std::vector<float> colors;            // 3 per vertex
	std::vector<std::uint32_t> triangles; // 3 vertex indices per triangle
	bool lit = false;                     // headlight shading by each triangle's normal (an iso-surface); off = the colours as they are (a data section)
};

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
	/// True when some slice is a lit iso-surface (as opposed to a flat section fill): those lie inside the solid.
	bool hasIsoSurfaces() const
	{
		for (const auto& entry : _sets)
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
