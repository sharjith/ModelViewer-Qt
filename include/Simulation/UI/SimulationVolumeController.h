#pragma once

#include "IGpuContextResource.h"
#include "SimulationVolume.h"

#include <QOpenGLFunctions_4_5_Core>
#include <QObject>
#include <QPointF>
#include <QSize>
#include <QUuid>
#include <QVector>
#include <QVector3D>

#include <functional>
#include <map>
#include <vector>

class Camera;
class RenderableMesh;
class SceneRenderController;

struct SimulationVolumeClipState
{
	QVector3D axisEnabled;
	QVector3D axisThreshold;
	QVector3D axisSign;
	bool boxEnabled = false;
	bool boxKeepInside = false;
	QVector3D boxMinimum;
	QVector3D boxMaximum;
};

class SimulationVolumeController : public QObject, protected QOpenGLFunctions_4_5_Core, public IGpuContextResource
{
public:
	explicit SimulationVolumeController(SceneRenderController& renderCtrl, QObject* parent = nullptr);
	~SimulationVolumeController() override = default;

	void releaseGpuResources() override;
	void restoreGpuResources() override;
	void setVolume(const QUuid& meshUuid, VolumeGrid grid, int colormap, QVector<QPointF> opacity);
	void setTransferFunction(const QUuid& meshUuid, int colormap, QVector<QPointF> opacity);
	void clearVolume(const QUuid& meshUuid);
	// Retired textures count as pending work so the next paint can delete them while the GL context is current.
	bool hasVolumes() const { return !_entries.empty() || !_retiredTextures.empty(); }
	bool contains(const QUuid& meshUuid) const { return _entries.find(meshUuid) != _entries.end(); }

	using MeshResolver = std::function<const RenderableMesh*(const QUuid&)>;
	bool hasVisibleVolumes(const MeshResolver& resolve) const;
	void drawOverlay(Camera* camera, const QSize& framebufferSize, const SimulationVolumeClipState& clipping,
	                 const MeshResolver& resolve);

private:
	struct Entry
	{
		VolumeGrid grid;
		int colormap = 0;
		QVector<QPointF> opacity;
		unsigned int valueTexture = 0;
		unsigned int validityTexture = 0;
		unsigned int transferTexture = 0;
		bool dataDirty = true;
		bool transferDirty = true;
	};

	void deleteTextures(Entry& entry);
	void retireTextures(Entry& entry);
	void deleteRetiredTextures();
	void upload(Entry& entry);
	bool captureDepth(const QSize& framebufferSize);

	SceneRenderController& _renderCtrl;
	std::map<QUuid, Entry> _entries;
	bool _glFunctionsInitialized = false;
	unsigned int _depthTexture = 0;
	unsigned int _depthFramebuffer = 0;
	QSize _depthSize;
	std::vector<unsigned int> _retiredTextures;
};
