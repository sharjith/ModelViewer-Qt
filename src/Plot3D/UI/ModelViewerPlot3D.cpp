#include "ModelViewer.h"

#include "AnalysisColorRamp.h"
#include "Plot3DAxisController.h"
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
	QVector<Plot3DSession> sessions = _plot3DSessions;
	const QSet<QUuid> shown = getVisibleUuids();
	for (Plot3DSession& session : sessions)
		session.visible = shown.contains(session.meshUuid);
	return sessions;
}
QUuid ModelViewer::activePlot3DMeshUuid() const { return _activePlot3DMesh; }

void ModelViewer::addPlot3DSession(Plot3DSession session)
{
	if (session.meshUuid.isNull())
		return;
	_plot3DSessions.push_back(std::move(session));
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
	Plot3DSession* session = sessionFor(_plot3DSessions, _activePlot3DMesh);
	if (session)
		applyAxes(this, session);
}
