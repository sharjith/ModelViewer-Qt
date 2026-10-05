// Playback of pathline plots over time: the trails are revealed up to the current moment with a moving head on each, driven by
// a second instance of the Simulation timeline widget. See Plot3DPathlineAnimation (ModelViewer.h).

#include "ModelViewer.h"

#include "AnalysisColorRamp.h"
#include "Plot3DPathlines.h"
#include "SceneMesh.h"
#include "SimulationTimelineWidget.h"
#include "ViewportWidget.h"

#include <QPointer>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace
{
	constexpr int kPathlineFrames = 200;
	constexpr double kHeadSize = 7.0;

	Plot3DSession* findPlotSession(QVector<Plot3DSession>& sessions, const QUuid& meshUuid)
	{
		for (Plot3DSession& session : sessions)
			if (session.meshUuid == meshUuid)
				return &session;
		return nullptr;
	}

	bool isPathlineSession(const Plot3DSession& session)
	{
		return session.generated.valid && (session.generated.sourceMode == 7 || session.generated.sourceMode == 8);
	}
}

void ModelViewer::setPlot3DPathlineAnimation(const QUuid& meshUuid, bool enabled)
{
	Plot3DSession* session = findPlotSession(_plot3DSessions, meshUuid);
	SceneMesh* mesh = _viewportWidget ? _viewportWidget->getMeshByUuid(meshUuid) : nullptr;
	if (!session || !mesh || !isPathlineSession(*session))
		return;
	if (!enabled)
	{
		if (_pathlineAnimation.mesh == meshUuid)
			endPathlineAnimation();
		else
			session->pathlineAnimation = false;
		emit plot3DSessionsChanged(false);
		return;
	}
	if (!_pathlineAnimation.mesh.isNull() && _pathlineAnimation.mesh != meshUuid)
		endPathlineAnimation(); // one plot at a time: there is one timeline
	if (!buildPathlineAnimationState(*session, *mesh))
		return;
	session->pathlineAnimation = true;
	updatePathlineTimeline();
	applyPathlineFrame();
	emit plot3DSessionsChanged(false);
}

void ModelViewer::refreshPlot3DPathlineAnimation(const QUuid& meshUuid)
{
	if (_pathlineAnimation.mesh != meshUuid)
		return;
	Plot3DSession* session = findPlotSession(_plot3DSessions, meshUuid);
	SceneMesh* mesh = _viewportWidget ? _viewportWidget->getMeshByUuid(meshUuid) : nullptr;
	if (!session || !mesh)
		return;
	const double ratio = _pathlineAnimation.frames > 1 ? static_cast<double>(_pathlineAnimation.frame) / (_pathlineAnimation.frames - 1) : 1.0;
	if (!buildPathlineAnimationState(*session, *mesh))
	{
		endPathlineAnimation();
		return;
	}
	_pathlineAnimation.frame = static_cast<int>(std::lround(ratio * (_pathlineAnimation.frames - 1)));
	updatePathlineTimeline();
	applyPathlineFrame();
}

bool ModelViewer::buildPathlineAnimationState(const Plot3DSession& session, SceneMesh& mesh)
{
	const std::vector<Vertex> vertices = mesh.vertices();
	if (vertices.size() < 2 || vertices.size() % 2 != 0 || session.values.size() != vertices.size())
		return false;

	Plot3DPathlineAnimation state;
	state.mesh = session.meshUuid;
	state.frames = kPathlineFrames;
	state.frame = state.frames - 1; // switching it on keeps showing the whole plot until Play is pressed
	state.times = session.values;
	state.positions.resize(vertices.size() * 3);
	for (std::size_t i = 0; i < vertices.size(); ++i)
	{
		state.positions[i * 3] = vertices[i].Position.x;
		state.positions[i * 3 + 1] = vertices[i].Position.y;
		state.positions[i * 3 + 2] = vertices[i].Position.z;
	}
	state.timeMinimum = *std::min_element(state.times.begin(), state.times.end());
	state.timeMaximum = *std::max_element(state.times.begin(), state.times.end());
	if (!(state.timeMaximum > state.timeMinimum))
		return false;

	state.trails = plot3DPathlineTrails(state.times);
	_pathlineAnimation = std::move(state);
	return true;
}

void ModelViewer::applyPathlineFrame()
{
	Plot3DPathlineAnimation& a = _pathlineAnimation;
	SceneMesh* mesh = _viewportWidget && !a.mesh.isNull() ? _viewportWidget->getMeshByUuid(a.mesh) : nullptr;
	Plot3DSession* session = findPlotSession(_plot3DSessions, a.mesh);
	if (!mesh || !session)
		return;

	const double fraction = a.frames > 1 ? static_cast<double>(a.frame) / (a.frames - 1) : 1.0;
	const double now = a.timeMinimum + (a.timeMaximum - a.timeMinimum) * fraction;
	constexpr double eps = 1.0e-9;

	std::vector<int> firsts, counts;
	std::vector<float> heads; // position(3) + colour(3)
	// The colour of a head: the plot's colour map at the current time, so it matches the trail it is drawing.
	const std::vector<float> rgba = AnalysisColorRamp::mapToRGBA({ static_cast<float>(now) }, { true }, session->colourMinimum, session->colourMaximum,
		static_cast<AnalysisColormap>(session->colormap), session->bands);
	for (const Plot3DPathlineTrail& trail : a.trails)
	{
		const int base = trail.firstVertex / 2; // index of the trail's first segment
		// The segments that have fully elapsed (end time <= now) are drawn.
		const int elapsed = plot3DElapsedSegments(a.times, trail, now);
		if (elapsed > 0)
		{
			firsts.push_back(trail.firstVertex);
			counts.push_back(2 * elapsed);
		}
		// The head sits inside the segment being traversed; a trail that ended before now has no head.
		float head[3];
		bool haveHead = false;
		if (elapsed < trail.segments)
		{
			const std::size_t j = static_cast<std::size_t>(2 * (base + elapsed));
			const double ta = a.times[j], tb = a.times[j + 1];
			const double u = tb > ta ? std::clamp((now - ta) / (tb - ta), 0.0, 1.0) : 0.0;
			for (int c = 0; c < 3; ++c)
				head[c] = static_cast<float>(a.positions[j * 3 + static_cast<std::size_t>(c)] * (1.0 - u) + a.positions[(j + 1) * 3 + static_cast<std::size_t>(c)] * u);
			haveHead = true;
		}
		else if (std::abs(a.times[static_cast<std::size_t>(2 * (base + trail.segments - 1) + 1)] - now) <= eps)
		{
			const std::size_t last = static_cast<std::size_t>(2 * (base + trail.segments - 1) + 1);
			for (int c = 0; c < 3; ++c)
				head[c] = a.positions[last * 3 + static_cast<std::size_t>(c)];
			haveHead = true;
		}
		if (haveHead)
			heads.insert(heads.end(), { head[0], head[1], head[2], rgba[0], rgba[1], rgba[2] });
	}
	mesh->setDrawRanges(std::move(firsts), std::move(counts));
	_viewportWidget->setPlot3DPointOverlay(a.mesh, std::move(heads), static_cast<float>(kHeadSize));
	_viewportWidget->updateView();
}

void ModelViewer::setPathlineFrame(int frame, bool fromPlayback)
{
	if (_pathlineAnimation.mesh.isNull())
		return;
	_pathlineAnimation.frame = std::clamp(frame, 0, _pathlineAnimation.frames - 1);
	applyPathlineFrame();
	if (_pathlineTimeline)
		_pathlineTimeline->setCurrentStep(_pathlineAnimation.frame);
	(void)fromPlayback;
}

void ModelViewer::setPathlinePlaying(bool playing)
{
	if (playing == _pathlinePlaying)
		return;
	if (playing)
	{
		if (_pathlineAnimation.mesh.isNull())
			return;
		if (_pathlineAnimation.frame >= _pathlineAnimation.frames - 1)
			setPathlineFrame(0, true); // from the end, Play starts over
		if (!_pathlineTimer)
		{
			_pathlineTimer = new QTimer(this);
			connect(_pathlineTimer, &QTimer::timeout, this, &ModelViewer::advancePathlineFrame);
		}
		_pathlinePlaying = true;
		_pathlineTimer->start(std::max(15, static_cast<int>(40.0 / _pathlineSpeed)));
	}
	else
	{
		_pathlinePlaying = false;
		if (_pathlineTimer)
			_pathlineTimer->stop();
	}
	if (_pathlineTimeline)
		_pathlineTimeline->setPlaying(_pathlinePlaying);
}

void ModelViewer::advancePathlineFrame()
{
	if (_pathlineAnimation.mesh.isNull() || !_visibleMeshUuids.contains(_pathlineAnimation.mesh))
	{
		setPathlinePlaying(false); // the plot went away or is hidden
		return;
	}
	int next = _pathlineAnimation.frame + 1;
	if (next >= _pathlineAnimation.frames)
	{
		if (!_pathlineLoop)
		{
			setPathlinePlaying(false);
			return;
		}
		next = 0;
	}
	setPathlineFrame(next, true);
}

void ModelViewer::updatePathlineTimeline()
{
	if (_pathlineAnimation.mesh.isNull() || !_viewportWidget)
	{
		if (_pathlineTimeline)
			_pathlineTimeline->setAliveCheck([]() { return false; });
		return;
	}
	if (!_pathlineTimeline)
	{
		_pathlineTimeline = new SimulationTimelineWidget(_viewportWidget);
		connect(_pathlineTimeline, &SimulationTimelineWidget::stepRequested, this, [this](int frame) { setPathlineFrame(frame, false); });
		connect(_pathlineTimeline, &SimulationTimelineWidget::playRequested, this, [this](bool play) { setPathlinePlaying(play); });
		connect(_pathlineTimeline, &SimulationTimelineWidget::loopChanged, this, [this](bool loop) { _pathlineLoop = loop; });
		connect(_pathlineTimeline, &SimulationTimelineWidget::speedChanged, this, [this](double speed) {
			_pathlineSpeed = speed;
			if (_pathlinePlaying && _pathlineTimer)
				_pathlineTimer->setInterval(std::max(15, static_cast<int>(40.0 / _pathlineSpeed)));
		});
	}
	const double t0 = _pathlineAnimation.timeMinimum, t1 = _pathlineAnimation.timeMaximum;
	const int frames = _pathlineAnimation.frames;
	_pathlineTimeline->setSteps(frames, [t0, t1, frames](int i) {
		return tr("t = %1").arg(t0 + (t1 - t0) * i / std::max(1, frames - 1), 0, 'g', 5);
	});
	_pathlineTimeline->setCurrentStep(_pathlineAnimation.frame);
	_pathlineTimeline->setLoop(_pathlineLoop);
	_pathlineTimeline->setSpeed(_pathlineSpeed);
	_pathlineTimeline->setPlaying(_pathlinePlaying);
	QPointer<ViewportWidget> viewport(_viewportWidget);
	QPointer<ModelViewer> self(this);
	const QUuid meshUuid = _pathlineAnimation.mesh;
	_pathlineTimeline->setAliveCheck([viewport, self, meshUuid]() {
		return viewport && viewport->getMeshByUuid(meshUuid) && self && self->_visibleMeshUuids.contains(meshUuid) && self->_pathlineAnimation.mesh == meshUuid;
	});
}

void ModelViewer::endPathlineAnimation(bool touchMesh)
{
	setPathlinePlaying(false);
	const QUuid meshUuid = _pathlineAnimation.mesh;
	if (Plot3DSession* session = findPlotSession(_plot3DSessions, meshUuid))
		session->pathlineAnimation = false;
	if (touchMesh && _viewportWidget && !meshUuid.isNull())
	{
		if (SceneMesh* mesh = _viewportWidget->getMeshByUuid(meshUuid))
			mesh->clearDrawRanges(); // the whole plot again
		_viewportWidget->clearPlot3DPointOverlay(meshUuid);
		_viewportWidget->updateView();
	}
	_pathlineAnimation = Plot3DPathlineAnimation();
	if (_pathlineTimeline)
		_pathlineTimeline->setAliveCheck([]() { return false; });
}
