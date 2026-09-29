#include "ModelViewer.h"

#include "AnalysisColorRamp.h"
#include "Plot3DAxisController.h"
#include "Plot3DMeshBuilder.h"
#include "SceneMesh.h"
#include "ViewportWidget.h"

#include <algorithm>

namespace
{
Plot3DSession* sessionFor(QVector<Plot3DSession>& sessions, const QUuid& uuid)
{
	for (Plot3DSession& session : sessions)
		if (session.meshUuid == uuid)
			return &session;
	return nullptr;
}

bool visiblePlotBounds(const ModelViewer* viewer, std::array<double, 3>& minimum, std::array<double, 3>& maximum)
{
	if (!viewer)
		return false;
	bool haveVisiblePlot = false;
	for (const Plot3DSession& candidate : viewer->plot3DSessions())
	{
		if (!candidate.visible)
			continue;
		if (!haveVisiblePlot)
		{
			minimum = candidate.dataMinimum;
			maximum = candidate.dataMaximum;
			haveVisiblePlot = true;
			continue;
		}
		for (int axis = 0; axis < 3; ++axis)
		{
			minimum[axis] = std::min(minimum[axis], candidate.dataMinimum[axis]);
			maximum[axis] = std::max(maximum[axis], candidate.dataMaximum[axis]);
		}
	}
	return haveVisiblePlot;
}

void applyAxes(ModelViewer* viewer, const Plot3DSession* session)
{
	ViewportWidget* viewport = viewer ? viewer->getViewportWidget() : nullptr;
	if (!viewport || !session)
		return;
	if (!session->axesVisible)
	{
		viewport->setPlot3DAxisVisible(false);
		return;
	}
	// One axis box represents the current Plot3D coordinate system.  It must
	// enclose every visible plot, not merely whichever plot is selected for
	// editing.  The active plot still owns the axis configuration below.
	std::array<double, 3> minimum{};
	std::array<double, 3> maximum{};
	if (!visiblePlotBounds(viewer, minimum, maximum))
	{
		viewport->setPlot3DAxisVisible(false);
		return;
	}
	Plot3DAxisController controller;
	Plot3DAxisLayout layout;
	QString error;
	if (controller.buildLayout(session->axes, minimum.data(), maximum.data(), layout, &error))
		viewport->setPlot3DAxisLayout(layout);
	else
		viewport->setPlot3DAxisVisible(false);
}
}

QVector<Plot3DSession> ModelViewer::plot3DSessions() const
{
	// Scene-tree visibility, undo/redo and the plot panel all use the same
	// authoritative set.  Return a presentation snapshot so the panel cannot
	// display a stale checkbox after visibility changed outside the panel.
	QVector<Plot3DSession> sessions;
	const QSet<QUuid> shown = getVisibleUuids();
	for (const Plot3DSession& stored : _plot3DSessions)
	{
		// Delete is undoable: its mesh stays in the recycle bin, but must not
		// remain selectable as an active plot until Undo restores it.
		if (!_viewportWidget || _viewportWidget->getIndexByUuid(stored.meshUuid) < 0)
			continue;
		Plot3DSession session = stored;
		session.visible = shown.contains(session.meshUuid);
		sessions.push_back(std::move(session));
	}
	return sessions;
}
QUuid ModelViewer::activePlot3DMeshUuid() const
{
	if (_viewportWidget && _viewportWidget->getIndexByUuid(_activePlot3DMesh) >= 0)
		return _activePlot3DMesh;
	for (const Plot3DSession& session : _plot3DSessions)
		if (_viewportWidget && _viewportWidget->getIndexByUuid(session.meshUuid) >= 0)
			return session.meshUuid;
	return {};
}

void ModelViewer::addPlot3DSession(Plot3DSession session)
{
	if (session.meshUuid.isNull())
		return;
	_plot3DSessions.push_back(std::move(session));
	markNonUndoDocumentModified();
	activatePlot3DSession(_plot3DSessions.back().meshUuid);
	emit plot3DSessionsChanged(true);
}

void ModelViewer::activatePlot3DSession(const QUuid& meshUuid)
{
	Plot3DSession* session = sessionFor(_plot3DSessions, meshUuid);
	if (!session)
		return;
	_activePlot3DMesh = meshUuid;
	applyAxes(this, session);
	if (_viewportWidget)
		_viewportWidget->updateView();
	emit plot3DSessionsChanged(false);
}

void ModelViewer::applyPlot3DColourState(const QUuid& meshUuid, float minimum, float maximum, int colormap, int bands)
{
	Plot3DSession* session = sessionFor(_plot3DSessions, meshUuid);
	SceneMesh* mesh = _viewportWidget ? _viewportWidget->getMeshByUuid(meshUuid) : nullptr;
	if (!session || !mesh || session->values.empty())
		return;
	if (maximum <= minimum)
		maximum = minimum + 1.0f;
	session->colourMinimum = minimum;
	session->colourMaximum = maximum;
	session->colormap = colormap;
	session->bands = bands;
	const AnalysisColormap ramp = static_cast<AnalysisColormap>(colormap);
	const std::vector<float> encoded = bands >= 2
		? AnalysisColorRamp::mapToNormalizedScalarRGBA(session->values, session->valid, minimum, maximum)
		: AnalysisColorRamp::mapToRGBA(session->values, session->valid, minimum, maximum, ramp);
	mesh->setAnalysisOverlayColors(encoded);
	mesh->setAnalysisOverlayBanding(bands, colormap);
	_viewportWidget->updateView();
	emit plot3DSessionsChanged(false);
}

void ModelViewer::setPlot3DContourLevels(const QUuid& meshUuid, int levels)
{
	Plot3DSession* session = sessionFor(_plot3DSessions, meshUuid);
	SceneMesh* mesh = _viewportWidget ? _viewportWidget->getMeshByUuid(meshUuid) : nullptr;
	if (!session || !mesh || session->primitive != Plot3DPrimitive::Contour || levels == session->contourLevels)
		return;

	Plot3DMeshData data;
	QString error;
	if (!buildPlot3DContourMesh(session->contourSource, data, levels, &error))
		return;
	std::vector<Vertex> vertices(data.vertexCount());
	std::vector<float> values(data.vertexCount());
	std::vector<bool> valid(data.vertexCount(), true);
	for (std::size_t i = 0; i < data.vertexCount(); ++i)
	{
		Vertex& vertex = vertices[i];
		vertex.Color = glm::vec4(1.0f);
		vertex.Position = glm::vec3(data.positions[i * 3], data.positions[i * 3 + 1], data.positions[i * 3 + 2]);
		vertex.Normal = glm::vec3(0.0f, 0.0f, 1.0f);
		vertex.Tangent = glm::vec3(0.0f); vertex.Bitangent = glm::vec3(0.0f);
		for (glm::vec2& uv : vertex.TexCoords) uv = glm::vec2(0.0f);
		values[i] = static_cast<float>(data.values[i]);
	}
	_viewportWidget->makeCurrent();
	mesh->setMeshData(vertices, {});
	session->contourLevels = levels;
	session->values = std::move(values);
	session->valid = std::move(valid);
	applyPlot3DColourState(meshUuid, session->colourMinimum, session->colourMaximum, session->colormap, session->bands);
	_viewportWidget->doneCurrent();
	_viewportWidget->updateView();
	emit plot3DSessionsChanged(false);
}

void ModelViewer::setPlot3DSessionAxesVisible(const QUuid& meshUuid, bool visible)
{
	Plot3DSession* session = sessionFor(_plot3DSessions, meshUuid);
	if (!session)
		return;
	session->axesVisible = visible;
	if (meshUuid == _activePlot3DMesh)
		applyAxes(this, session);
	emit plot3DSessionsChanged(false);
}

void ModelViewer::applyPlot3DAxisConfig(const QUuid& meshUuid, const std::array<Plot3DAxisConfig, 3>& axes)
{
	Plot3DSession* session = sessionFor(_plot3DSessions, meshUuid);
	if (!session)
		return;
	// Validate before committing the edit.  In particular, a Log10 axis with
	// non-positive bounds has no drawable layout; retaining that invalid state
	// would make the panel say it applied settings that the viewport cannot show.
	Plot3DAxisController controller;
	Plot3DAxisLayout layout;
	QString error;
	std::array<double, 3> minimum{};
	std::array<double, 3> maximum{};
	if (!visiblePlotBounds(this, minimum, maximum)
		|| !controller.buildLayout(axes, minimum.data(), maximum.data(), layout, &error))
		return;
	session->axes = axes;
	if (meshUuid == _activePlot3DMesh)
		applyAxes(this, session);
	emit plot3DSessionsChanged(false);
}

void ModelViewer::refreshPlot3DAxes()
{
	const QUuid active = activePlot3DMeshUuid();
	Plot3DSession* session = sessionFor(_plot3DSessions, active);
	if (session)
		applyAxes(this, session);
	else if (_viewportWidget)
		_viewportWidget->clearPlot3DAxisLayout();
}
