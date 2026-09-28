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

void applyAxes(ViewportWidget* viewport, const Plot3DSession* session)
{
	if (!viewport || !session)
		return;
	if (!session->axesVisible)
	{
		viewport->setPlot3DAxisVisible(false);
		return;
	}
	Plot3DAxisController controller;
	Plot3DAxisLayout layout;
	QString error;
	if (controller.buildLayout(session->axes, session->dataMinimum.data(), session->dataMaximum.data(), layout, &error))
		viewport->setPlot3DAxisLayout(layout);
}
}

QVector<Plot3DSession> ModelViewer::plot3DSessions() const { return _plot3DSessions; }
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
	applyAxes(_viewportWidget, session);
	if (_viewportWidget)
		_viewportWidget->updateView();
	emit plot3DSessionsChanged(false);
}

void ModelViewer::setPlot3DSessionVisible(const QUuid& meshUuid, bool visible)
{
	Plot3DSession* session = sessionFor(_plot3DSessions, meshUuid);
	if (!session || session->visible == visible)
		return;
	session->visible = visible;
	QSet<QUuid> shown = getVisibleUuids();
	if (visible) shown.insert(meshUuid); else shown.remove(meshUuid);
	setVisibilityWithoutUndo(shown);
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
		applyAxes(_viewportWidget, session);
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
	if (!controller.buildLayout(axes, session->dataMinimum.data(), session->dataMaximum.data(), layout, &error))
		return;
	session->axes = axes;
	if (meshUuid == _activePlot3DMesh)
		_viewportWidget->setPlot3DAxisLayout(layout);
	emit plot3DSessionsChanged(false);
}
