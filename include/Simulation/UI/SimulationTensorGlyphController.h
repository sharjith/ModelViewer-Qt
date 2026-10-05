#pragma once

#include "IGpuContextResource.h"
#include "SimulationGlyphs.h"

#include <QObject>
#include <QMatrix4x4>
#include <QOpenGLFunctions_4_5_Core>
#include <QUuid>

#include <functional>
#include <map>
#include <vector>

class Camera;
class RenderableMesh;
class SceneRenderController;

// Draws the tensor-field ellipsoids of simulation results (see SimulationGlyphs.h, TensorGlyphSet). Sibling of
// SimulationGlyphController (vector arrows): same pure data-cache-plus-per-frame-draw shape, driven by ModelViewer
// (setTensorGlyphs()/clearTensorGlyphs()) - not scene meshes, so no selection/scene-tree/save/export/path-tracing
// participation, and each frame the ellipsoid centres are read from the result mesh's current transformed vertices
// so they follow a deformed or moved result.
class SimulationTensorGlyphController : public QObject, protected QOpenGLFunctions_4_5_Core, public IGpuContextResource
{
public:
	explicit SimulationTensorGlyphController(SceneRenderController& renderCtrl, QObject* parent = nullptr);

	void releaseGpuResources() override;
	void restoreGpuResources() override;

	void setTensorGlyphs(const QUuid& meshUuid, TensorGlyphSet glyphs);
	void clearTensorGlyphs(const QUuid& meshUuid);
	bool hasTensorGlyphs() const { return !_sets.empty(); }

	using MeshResolver = std::function<const RenderableMesh*(const QUuid&)>;
	void drawOverlay(Camera* camera, const MeshResolver& resolve);

private:
	struct MeshCacheKey
	{
		QUuid uuid;
		const RenderableMesh* mesh = nullptr;
		quint64 geometryRevision = 0;
		QMatrix4x4 transform;
	};

	// One tessellated ellipsoid in _cachedTriangles (kVerticesPerGlyph vertices each, in order) and the set it belongs to.
	struct GlyphRange
	{
		QVector3D centre;
		std::size_t setIndex = 0;
	};
	// Where one result's glyphs sit: the box of its glyph centres, used to size the glyphs against the camera distance.
	struct SetExtent
	{
		QVector3D centre;
		float diameter = 0.0f;
	};

	bool _glFunctionsInitialized = false;
	SceneRenderController& _renderCtrl;
	std::map<QUuid, TensorGlyphSet> _sets;
	std::vector<MeshCacheKey> _cachedMeshes;
	std::vector<float> _cachedTriangles;
	std::vector<GlyphRange> _cachedGlyphs;
	std::vector<SetExtent> _cachedExtents;
	bool _cacheValid = false;
	bool _bufferDirty = true;
	unsigned int _vao = 0;
	unsigned int _vbo = 0;
};
