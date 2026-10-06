#include "ModelViewer.h"

#include "AnalysisColorRamp.h"
#include "Plot3DAssembly.h"
#include "Plot3DAxisController.h"
#include "Plot3DMeshBuilder.h"
#include "Plot3DVoxelStyle.h"
#include "SceneGraph.h"
#include "SceneMesh.h"
#include "SceneNode.h"
#include "SimulationLegendWidget.h"
#include "ViewportWidget.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <QPointF>
#include <QPointer>

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
	controller.setReferencePlanesVisible(session->referencePlanes[0], session->referencePlanes[1], session->referencePlanes[2]);
	controller.setReferencePlaneOpacity(session->referencePlaneOpacity);
	Plot3DAxisLayout layout;
	QString error;
	if (controller.buildLayout(session->axes, minimum.data(), maximum.data(), layout, &error, session->title))
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
	if (session.title.trimmed().isEmpty())
		session.title = session.name;
	_plot3DSessions.push_back(std::move(session));
	markNonUndoDocumentModified();
	activatePlot3DSession(_plot3DSessions.back().meshUuid);
	emit plot3DSessionsChanged(true);
}

void ModelViewer::updatePlot3DSession(Plot3DSession session)
{
	Plot3DSession* stored = sessionFor(_plot3DSessions, session.meshUuid);
	if (!stored)
		return;
	*stored = std::move(session);
	applyAxes(this, stored);
	applyPlot3DColourState(stored->meshUuid, stored->colourMinimum, stored->colourMaximum,
		stored->colormap, stored->bands);
}

void ModelViewer::activatePlot3DSession(const QUuid& meshUuid)
{
	Plot3DSession* session = sessionFor(_plot3DSessions, meshUuid);
	if (!session)
		return;
	_activePlot3DMesh = meshUuid;
	applyAxes(this, session);
	refreshPlot3DLegend();
	if (_viewportWidget)
		_viewportWidget->updateView();
	emit plot3DSessionsChanged(false);
}

void ModelViewer::applyPlot3DColourState(const QUuid& meshUuid, float minimum, float maximum, int colormap, int bands)
{
	Plot3DSession* session = sessionFor(_plot3DSessions, meshUuid);
	SceneMesh* mesh = _viewportWidget ? _viewportWidget->getMeshByUuid(meshUuid) : nullptr;
	if (!session || !mesh)
		return;
	// The path tracer reads the shown colours through plot3DBakedColors(), so a colour edit must rebuild its scene.
	struct PathTracerRefresh
	{
		ViewportWidget* viewport;
		~PathTracerRefresh() { viewport->notifyRayTracedSceneMutated(); }
	} pathTracerRefresh{ _viewportWidget };
	if (maximum <= minimum)
		maximum = minimum + 1.0f;
	session->colourMinimum = minimum;
	session->colourMaximum = maximum;
	session->colormap = colormap;
	session->bands = bands;
	if (session->primitive == Plot3DPrimitive::Quiver)
	{
		const std::vector<float> rgba = AnalysisColorRamp::mapToRGBA(
			session->values, session->valid, minimum, maximum, static_cast<AnalysisColormap>(colormap), bands);
		mesh->setAnalysisOverlayColors(rgba);
		mesh->setAnalysisOverlayBanding(0, colormap);
		std::vector<float> rgb;
		rgb.reserve(session->values.size() * 3);
		for (std::size_t i = 0; i + 3 < rgba.size(); i += 4)
			rgb.insert(rgb.end(), { rgba[i], rgba[i + 1], rgba[i + 2] });
		_viewportWidget->setSimulationGlyphColors(meshUuid, std::move(rgb), minimum, maximum);
		markNonUndoDocumentModified();
		refreshPlot3DLegend();
		emit plot3DSessionsChanged(false);
		return;
	}
	if (session->primitive == Plot3DPrimitive::Voxel)
	{
		// Voxel occupancy is already normalized to [0, 1]. Zero stays transparent and nonzero cells fade in with
		// their supplied occupancy; only the selected colour map is editable here.
		_viewportWidget->setSimulationVolumeTransferFunction(meshUuid, colormap, plot3DVoxelOpacity());
		markNonUndoDocumentModified();
		refreshPlot3DLegend();
		emit plot3DSessionsChanged(false);
		return;
	}
	if (session->values.empty())
		return;
	const AnalysisColormap ramp = static_cast<AnalysisColormap>(colormap);
	if (session->isFilledScatter)
	{
		// Transparent filled ribbons cannot use the analysis-overlay colour path: the transparent render pass loses
		// that overlay's RGB. Recolour their authored vertex data instead and retain the mesh's alpha-blended material.
		const std::vector<float> encoded = AnalysisColorRamp::mapToRGBA(
			session->values, session->valid, minimum, maximum, ramp, bands);
		std::vector<Vertex> vertices = mesh->vertices();
		if (encoded.size() == vertices.size() * 4)
		{
			for (std::size_t i = 0; i < vertices.size(); ++i)
				vertices[i].Color = glm::vec4(encoded[i * 4], encoded[i * 4 + 1], encoded[i * 4 + 2], 1.0f);
			_viewportWidget->makeCurrent();
			mesh->setMeshData(vertices, mesh->indices());
			_viewportWidget->doneCurrent();
		}
		_viewportWidget->updateView();
		markNonUndoDocumentModified();
		refreshPlot3DLegend();
		emit plot3DSessionsChanged(false);
		return;
	}
	const std::vector<float> encoded = bands >= 2
		? AnalysisColorRamp::mapToNormalizedScalarRGBA(session->values, session->valid, minimum, maximum)
		: AnalysisColorRamp::mapToRGBA(session->values, session->valid, minimum, maximum, ramp);
	mesh->setAnalysisOverlayColors(encoded);
	mesh->setAnalysisOverlayBanding(bands, colormap);
	if (!session->markerMeshUuid.isNull() && !session->markerValues.empty())
	{
		if (SceneMesh* markerMesh = _viewportWidget->getMeshByUuid(session->markerMeshUuid))
		{
			const std::vector<float> markerEncoded = bands >= 2
				? AnalysisColorRamp::mapToNormalizedScalarRGBA(session->markerValues, session->markerValid, minimum, maximum)
				: AnalysisColorRamp::mapToRGBA(session->markerValues, session->markerValid, minimum, maximum, ramp);
			markerMesh->setAnalysisOverlayColors(markerEncoded);
			markerMesh->setAnalysisOverlayBanding(bands, colormap);
		}
	}
	if (!session->contourOverlayMeshUuid.isNull() && !session->overlayValues.empty())
	{
		if (SceneMesh* overlayMesh = _viewportWidget->getMeshByUuid(session->contourOverlayMeshUuid))
		{
			overlayMesh->setAnalysisOverlayColors(bands >= 2
				? AnalysisColorRamp::mapToNormalizedScalarRGBA(session->overlayValues, session->overlayValid, minimum, maximum)
				: AnalysisColorRamp::mapToRGBA(session->overlayValues, session->overlayValid, minimum, maximum, ramp));
			overlayMesh->setAnalysisOverlayBanding(bands, colormap);
		}
	}
	_viewportWidget->updateView();
	markNonUndoDocumentModified();
	refreshPlot3DLegend();
	emit plot3DSessionsChanged(false);
}

QHash<QUuid, std::vector<float>> ModelViewer::plot3DBakedColors() const
{
	QHash<QUuid, std::vector<float>> colors;
	if (!_viewportWidget)
		return colors;
	for (const Plot3DSession& session : _plot3DSessions)
	{
		// Voxel has no coloured mesh; a filled scatter already carries its colours as authored vertex data.
		if (session.primitive == Plot3DPrimitive::Voxel || session.isFilledScatter || session.values.empty())
			continue;
		const float minimum = session.colourMinimum;
		const float maximum = session.colourMaximum > minimum ? session.colourMaximum : minimum + 1.0f;
		const AnalysisColormap ramp = static_cast<AnalysisColormap>(session.colormap);
		const auto bake = [&](const QUuid& meshUuid, const std::vector<float>& values, const std::vector<bool>& valid)
		{
			const SceneMesh* mesh = meshUuid.isNull() || values.empty() ? nullptr : _viewportWidget->getMeshByUuid(meshUuid);
			if (!mesh)
				return;
			std::vector<float> rgba = AnalysisColorRamp::mapToRGBA(values, valid, minimum, maximum, ramp, session.bands);
			if (rgba.size() == mesh->vertices().size() * 4)
				colors.insert(meshUuid, std::move(rgba));
		};
		bake(session.meshUuid, session.values, session.valid);
		bake(session.markerMeshUuid, session.markerValues, session.markerValid);
		bake(session.contourOverlayMeshUuid, session.overlayValues, session.overlayValid);
	}
	return colors;
}

void ModelViewer::setPlot3DAutomaticColourRange(const QUuid& meshUuid, bool automatic)
{
	if (Plot3DSession* session = sessionFor(_plot3DSessions, meshUuid))
		session->automaticColourRange = automatic;
}

void ModelViewer::setPlot3DGeneratedSpec(const QUuid& meshUuid, const Plot3DGeneratedSpec& spec)
{
	if (Plot3DSession* session = sessionFor(_plot3DSessions, meshUuid))
	{
		session->generated = spec;
		markNonUndoDocumentModified();
		emit plot3DSessionsChanged(false);
	}
}

void ModelViewer::setPlot3DTimeSeriesSource(const QUuid& meshUuid, const QString& csvText, const Plot3DCsvOptions& options, const Plot3DColumnMapping& mapping)
{
	if (Plot3DSession* session = sessionFor(_plot3DSessions, meshUuid))
	{
		session->editableCsv = true;
		session->csvSource = csvText;
		session->csvOptions = options;
		session->columnMapping = mapping;
		markNonUndoDocumentModified();
	}
}

bool ModelViewer::replacePlot3DMesh(const QUuid& meshUuid, const Plot3DMeshData& data, unsigned int primitiveMode)
{
	Plot3DSession* session = sessionFor(_plot3DSessions, meshUuid);
	SceneMesh* mesh = _viewportWidget ? _viewportWidget->getMeshByUuid(meshUuid) : nullptr;
	if (!session || !mesh || data.empty())
		return false;

	Plot3DMeshUpload upload = plot3DPrepareUpload(data); // vertices, per-vertex colour values and the bounds, as the commit step builds them

	_viewportWidget->makeCurrent();
	mesh->setPrimitiveMode(primitiveMode);
	mesh->setMeshData(upload.vertices, data.indices);
	_viewportWidget->doneCurrent();

	session->values = std::move(upload.values);
	session->valid = std::move(upload.valid);
	session->dataMinimum = upload.boundsMinimum;
	session->dataMaximum = upload.boundsMaximum;
	session->dataMinimumValue = upload.valueMinimum;
	session->dataMaximumValue = upload.valueMaximum;
	if (session->automaticColourRange)
	{
		session->colourMinimum = upload.valueMinimum;
		session->colourMaximum = upload.valueMaximum;
	}
	refreshPlot3DContourOverlay(meshUuid); // a Surface's contour lines are cut from this mesh
	refreshPlot3DPathlineAnimation(meshUuid); // a playing pathline plot follows its new trails
	applyPlot3DColourState(meshUuid, session->colourMinimum, session->colourMaximum, session->colormap, session->bands);
	refreshPlot3DAxes();
	_viewportWidget->updateView();
	markNonUndoDocumentModified();
	emit plot3DSessionsChanged(false);
	return true;
}

void ModelViewer::applyPlot3DAppearance(const QUuid& meshUuid, float lineWidth, float markerSize, float arrowScale)
{
	Plot3DSession* session = sessionFor(_plot3DSessions, meshUuid);
	SceneMesh* mesh = _viewportWidget ? _viewportWidget->getMeshByUuid(meshUuid) : nullptr;
	if (!session || !mesh || !_viewportWidget)
		return;
	lineWidth = std::clamp(lineWidth, 0.5f, 10.0f);
	markerSize = std::clamp(markerSize, 1.0f, 20.0f);
	arrowScale = std::clamp(arrowScale, 0.25f, 4.0f);
	if (session->lineWidth == lineWidth && session->markerSize == markerSize && session->arrowScale == arrowScale)
		return;
	session->lineWidth = lineWidth;
	session->markerSize = markerSize;
	session->arrowScale = arrowScale;
	mesh->setPrimitiveLineWidth(lineWidth);
	mesh->setPrimitivePointSize(markerSize);
	if (!session->markerMeshUuid.isNull())
		if (SceneMesh* markerMesh = _viewportWidget->getMeshByUuid(session->markerMeshUuid))
			markerMesh->setPrimitivePointSize(markerSize);
	if (!session->contourOverlayMeshUuid.isNull())
		if (SceneMesh* overlayMesh = _viewportWidget->getMeshByUuid(session->contourOverlayMeshUuid))
			overlayMesh->setPrimitiveLineWidth(lineWidth);
	_viewportWidget->setSimulationGlyphScale(meshUuid, arrowScale);
	_viewportWidget->updateView();
	markNonUndoDocumentModified();
	emit plot3DSessionsChanged(false);
}

void ModelViewer::applyPlot3DBarAppearance(const QUuid& meshUuid, float widthScale, float depthScale)
{
	Plot3DSession* session = sessionFor(_plot3DSessions, meshUuid);
	SceneMesh* mesh = _viewportWidget ? _viewportWidget->getMeshByUuid(meshUuid) : nullptr;
	if (!session || !mesh || !_viewportWidget || session->primitive != Plot3DPrimitive::Bar || session->barSource.bars.empty())
		return;

	widthScale = std::clamp(widthScale, 0.1f, 3.0f);
	depthScale = std::clamp(depthScale, 0.1f, 3.0f);
	if (session->barWidthScale == widthScale && session->barDepthScale == depthScale)
		return;

	Plot3DBarData styled = session->barSource;
	for (Plot3DBar& bar : styled.bars)
	{
		bar.width *= widthScale;
		bar.depth *= depthScale;
	}
	Plot3DMeshData data;
	QString error;
	if (!buildPlot3DBarMesh(styled, data, &error))
		return;

	std::vector<Vertex> vertices(data.vertexCount());
	std::vector<float> values(data.vertexCount());
	std::vector<bool> valid(data.vertexCount(), true);
	for (std::size_t i = 0; i < data.vertexCount(); ++i)
	{
		Vertex& vertex = vertices[i];
		vertex.Color = glm::vec4(1.0f);
		vertex.Position = glm::vec3(data.positions[i * 3], data.positions[i * 3 + 1], data.positions[i * 3 + 2]);
		vertex.Normal = glm::vec3(data.normals[i * 3], data.normals[i * 3 + 1], data.normals[i * 3 + 2]);
		vertex.Tangent = glm::vec3(0.0f);
		vertex.Bitangent = glm::vec3(0.0f);
		for (glm::vec2& uv : vertex.TexCoords)
			uv = glm::vec2(0.0f);
		values[i] = static_cast<float>(data.values[i]);
	}

	_viewportWidget->makeCurrent();
	mesh->setMeshData(vertices, data.indices);
	_viewportWidget->doneCurrent();
	session->barWidthScale = widthScale;
	session->barDepthScale = depthScale;
	session->values = std::move(values);
	session->valid = std::move(valid);

	Plot3DDataset boundsDataset;
	boundsDataset.primitive = Plot3DPrimitive::Bar;
	boundsDataset.content = std::move(styled);
	double minimum[3]{}, maximum[3]{};
	if (plot3DDataBounds(boundsDataset, minimum, maximum))
	{
		std::copy(minimum, minimum + 3, session->dataMinimum.begin());
		std::copy(maximum, maximum + 3, session->dataMaximum.begin());
	}
	if (meshUuid == _activePlot3DMesh)
		applyAxes(this, session);

	// Rebuilding uploads fresh vertices, so restore the active colour mapping to the new vertex buffer.
	applyPlot3DColourState(meshUuid, session->colourMinimum, session->colourMaximum, session->colormap, session->bands);
}

namespace
{
	// Rebuilds a Contour plot's iso-lines for new level and projection settings and restores its colour mapping.
	void rebuildPlot3DContour(ModelViewer* viewer, ViewportWidget* viewport, Plot3DSession& session, SceneMesh* mesh, int levels, bool projected)
	{
		Plot3DMeshData data;
		QString error;
		if (!buildPlot3DContourMesh(session.contourSource, data, levels, &error, projected))
			return;
		Plot3DMeshUpload upload = plot3DPrepareUpload(data, false);
		viewport->makeCurrent();
		mesh->setMeshData(upload.vertices, {});
		session.contourLevels = levels;
		session.contourProjected = projected;
		session.values = std::move(upload.values);
		session.valid = std::move(upload.valid);
		viewer->applyPlot3DColourState(session.meshUuid, session.colourMinimum, session.colourMaximum, session.colormap, session.bands);
		viewport->doneCurrent();
		viewport->updateView();
		emit viewer->plot3DSessionsChanged(false);
	}
}

void ModelViewer::setPlot3DContourLevels(const QUuid& meshUuid, int levels)
{
	Plot3DSession* session = sessionFor(_plot3DSessions, meshUuid);
	SceneMesh* mesh = _viewportWidget ? _viewportWidget->getMeshByUuid(meshUuid) : nullptr;
	if (!session || !mesh || session->primitive != Plot3DPrimitive::Contour || levels == session->contourLevels)
		return;
	rebuildPlot3DContour(this, _viewportWidget, *session, mesh, levels, session->contourProjected);
}

void ModelViewer::setPlot3DContourProjected(const QUuid& meshUuid, bool projected)
{
	Plot3DSession* session = sessionFor(_plot3DSessions, meshUuid);
	SceneMesh* mesh = _viewportWidget ? _viewportWidget->getMeshByUuid(meshUuid) : nullptr;
	if (!session || !mesh || session->primitive != Plot3DPrimitive::Contour || projected == session->contourProjected)
		return;
	rebuildPlot3DContour(this, _viewportWidget, *session, mesh, session->contourLevels, projected);
}

void ModelViewer::setPlot3DContourOverlay(const QUuid& meshUuid, int mode, int levels)
{
	Plot3DSession* session = sessionFor(_plot3DSessions, meshUuid);
	if (!session || !_viewportWidget || session->primitive != Plot3DPrimitive::Surface)
		return;
	mode = std::clamp(mode, 0, 2);
	levels = std::clamp(levels, 1, 40);
	if (mode == session->contourOverlayMode && levels == session->contourOverlayLevels)
		return;
	session->contourOverlayMode = mode;
	session->contourOverlayLevels = levels;
	refreshPlot3DContourOverlay(meshUuid);
	markNonUndoDocumentModified();
	emit plot3DSessionsChanged(false);
}

void ModelViewer::refreshPlot3DContourOverlay(const QUuid& meshUuid)
{
	Plot3DSession* session = sessionFor(_plot3DSessions, meshUuid);
	SceneMesh* surface = _viewportWidget ? _viewportWidget->getMeshByUuid(meshUuid) : nullptr;
	if (!session || !surface || session->primitive != Plot3DPrimitive::Surface)
		return;
	SceneMesh* overlay = session->contourOverlayMeshUuid.isNull() ? nullptr : _viewportWidget->getMeshByUuid(session->contourOverlayMeshUuid);

	Plot3DMeshData lines;
	bool haveLines = false;
	if (session->contourOverlayMode != 0)
	{
		const std::vector<Vertex> surfaceVertices = surface->vertices();
		std::vector<float> positions(surfaceVertices.size() * 3);
		float lowZ = std::numeric_limits<float>::max(), highZ = std::numeric_limits<float>::lowest();
		for (std::size_t i = 0; i < surfaceVertices.size(); ++i)
		{
			positions[i * 3] = surfaceVertices[i].Position.x;
			positions[i * 3 + 1] = surfaceVertices[i].Position.y;
			positions[i * 3 + 2] = surfaceVertices[i].Position.z;
			lowZ = std::min(lowZ, positions[i * 3 + 2]);
			highZ = std::max(highZ, positions[i * 3 + 2]);
		}
		// Lines lying ON the surface would z-fight with it; lift them a hair along Z (0.3 % of the surface's height).
		const float lift = session->contourOverlayMode == 1 && highZ > lowZ ? (highZ - lowZ) * 0.003f : 0.0f;
		const std::vector<float>* values = session->values.size() == surfaceVertices.size() ? &session->values : nullptr;
		const std::vector<unsigned int> surfaceIndices = surface->indices();
		haveLines = buildPlot3DContourLines(positions, surfaceIndices, values, lines, session->contourOverlayLevels, nullptr,
			session->contourOverlayMode == 2, lift);
		if (haveLines && lift > 0.0f)
		{
			// The lifted copy is hidden by the surface when it is seen from below. A second copy lowered by the same amount sits
			// just outside the underside, so the lines read from either face (the hidden copy is simply behind the surface).
			Plot3DMeshData lower;
			if (buildPlot3DContourLines(positions, surfaceIndices, values, lower, session->contourOverlayLevels, nullptr, false, -lift))
			{
				lines.positions.insert(lines.positions.end(), lower.positions.begin(), lower.positions.end());
				lines.normals.insert(lines.normals.end(), lower.normals.begin(), lower.normals.end());
				lines.values.insert(lines.values.end(), lower.values.begin(), lower.values.end());
			}
		}
	}

	if (!haveLines)
	{
		// Off, or a surface with no iso-lines to show (a flat one): no companion mesh.
		session->overlayValues.clear();
		session->overlayValid.clear();
		if (overlay)
		{
			int position = 0;
			_sceneGraph->removeMeshUuid(session->contourOverlayMeshUuid, position);
			const int index = _viewportWidget->getIndexByUuid(session->contourOverlayMeshUuid);
			session->contourOverlayMeshUuid = QUuid();
			if (index >= 0)
				_viewportWidget->removeFromDisplay(index);
			updateDisplayList();
			_viewportWidget->updateView();
		}
		return;
	}

	// Lines lying on the surface are drawn in a fixed dark colour: coloured by the surface's own value they would match the
	// very colour beneath them and vanish. Lines on the base plane keep the colour map.
	const bool onSurface = session->contourOverlayMode == 1;
	Plot3DMeshUpload upload = plot3DPrepareUpload(lines, false);
	if (onSurface)
		for (Vertex& vertex : upload.vertices)
			vertex.Color = glm::vec4(0.06f, 0.06f, 0.06f, 1.0f);
	_viewportWidget->makeCurrent();
	bool created = false;
	if (overlay)
	{
		overlay->setMeshData(upload.vertices, {});
	}
	else if (SceneNode* node = _sceneGraph->findNodeForMesh(meshUuid))
	{
		// skipOptimization = true: the colour overlay below is indexed by vertex (see Plot3DPanel::buildPlot()).
		overlay = new SceneMesh(_viewportWidget->getShader(), surface->getName() + tr(" Contours"), upload.vertices, {}, {}, Material(), true, GL_LINES);
		_viewportWidget->addToDisplay(overlay);
		session->contourOverlayMeshUuid = overlay->uuid();
		_sceneGraph->restoreMeshUuid(node, session->contourOverlayMeshUuid, node->meshUuids.size());
		created = true;
	}
	if (!overlay)
	{
		_viewportWidget->doneCurrent();
		return;
	}
	overlay->setPrimitiveLineWidth(session->lineWidth);
	if (onSurface)
	{
		overlay->clearAnalysisOverlay(); // a previous base-plane colouring must not override the dark lines
		session->overlayValues.clear();
		session->overlayValid.clear();
	}
	else
	{
		session->overlayValues = std::move(upload.values);
		session->overlayValid = std::move(upload.valid);
	}
	applyPlot3DColourState(meshUuid, session->colourMinimum, session->colourMaximum, session->colormap, session->bands);
	_viewportWidget->doneCurrent();
	if (created)
		updateDisplayList();
	_viewportWidget->updateView();
}

void ModelViewer::setPlot3DSectionProbe(const QUuid& meshUuid, bool enabled)
{
	Plot3DSession* session = sessionFor(_plot3DSessions, meshUuid);
	if (!session || !_viewportWidget || session->primitive != Plot3DPrimitive::Surface || session->sectionProbe == enabled)
		return;
	session->sectionProbe = enabled;
	_viewportWidget->setPlot3DSectionProbeEnabled(meshUuid, enabled);
	emit plot3DSessionsChanged(false);
}

void ModelViewer::setPlot3DSessionAxesVisible(const QUuid& meshUuid, bool visible)
{
	Plot3DSession* session = sessionFor(_plot3DSessions, meshUuid);
	if (!session || session->axesVisible == visible)
		return;
	session->axesVisible = visible;
	if (meshUuid == _activePlot3DMesh)
		applyAxes(this, session);
	markNonUndoDocumentModified();
	emit plot3DSessionsChanged(false);
}

void ModelViewer::applyPlot3DReferencePlanes(const QUuid& meshUuid, const std::array<bool, 3>& visible, float opacity)
{
	Plot3DSession* session = sessionFor(_plot3DSessions, meshUuid);
	if (!session)
		return;
	opacity = std::clamp(opacity, 0.0f, 0.35f);
	if (session->referencePlanes == visible && session->referencePlaneOpacity == opacity)
		return;
	session->referencePlanes = visible;
	session->referencePlaneOpacity = opacity;
	if (meshUuid == _activePlot3DMesh)
		applyAxes(this, session);
	markNonUndoDocumentModified();
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
		|| !controller.buildLayout(axes, minimum.data(), maximum.data(), layout, &error, session->title))
		return;
	session->axes = axes;
	if (meshUuid == _activePlot3DMesh)
		applyAxes(this, session);
	markNonUndoDocumentModified();
	emit plot3DSessionsChanged(false);
}

void ModelViewer::setPlot3DAxisTitle(const QUuid& meshUuid, const QString& title)
{
	Plot3DSession* session = sessionFor(_plot3DSessions, meshUuid);
	if (!session || session->title == title.trimmed())
		return;
	session->title = title.trimmed();
	if (meshUuid == _activePlot3DMesh)
		applyAxes(this, session);
	markNonUndoDocumentModified();
	emit plot3DSessionsChanged(false);
}

void ModelViewer::setPlot3DPreview(const QVector<QUuid>& meshUuids, const Plot3DAxisLayout& axes)
{
	clearPlot3DPreview();
	if (!_viewportWidget || !_sceneGraph || meshUuids.isEmpty())
		return;

	// Normal viewport passes cull via SceneRuntime's scene-node hierarchy,
	// rather than drawing every raw mesh-store entry. The preview therefore
	// needs a temporary owner node to reach those passes. It is never added to
	// the navigation tree or given a Plot3D session, undo record, or save entry.
	SceneNode* previewNode = new SceneNode();
	previewNode->nodeUuid = QUuid::createUuid();
	previewNode->name = tr("Plot3D Preview");
	for (const QUuid& meshUuid : meshUuids)
		_sceneGraph->restoreMeshUuid(previewNode, meshUuid, previewNode->meshUuids.size());
	_sceneGraph->insertChildNode(_sceneGraph->root(), previewNode, _sceneGraph->root()->children.size());

	_plot3DPreviewMeshes = meshUuids;
	for (const QUuid& meshUuid : _plot3DPreviewMeshes)
		_visibleMeshUuids.insert(meshUuid);
	// Deliberately avoid updateDisplayList(): it rebuilds the navigation tree
	// and would turn this transient object into apparent document content.
	_viewportWidget->setDisplayList(visibleIndicesFromState());
	_viewportWidget->setPlot3DAxisLayout(axes);
	_viewportWidget->updateView();
}

void ModelViewer::clearPlot3DPreview()
{
	if (!_viewportWidget)
	{
		_plot3DPreviewMeshes.clear();
		return;
	}
	for (const QUuid& meshUuid : _plot3DPreviewMeshes)
	{
		if (_sceneGraph)
		{
			// Preview owner nodes contain only preview meshes. Detach their node
			// before deleting the proxy, so the graph never retains a dead UUID.
			if (SceneNode* node = _sceneGraph->findNodeForMesh(meshUuid))
			{
				if (SceneNode* parent = node->parent)
				{
					int position = -1;
					_sceneGraph->removeChildNode(parent, node, position);
					SceneGraph::deleteDetachedSubtree(node);
				}
			}
		}
		// A preview may later be a glyph or volume plot as well as an ordinary
		// mesh. Clearing every optional renderer attachment is harmless when it
		// was never installed and prevents an overlay outliving its proxy mesh.
		_viewportWidget->clearSimulationGlyphs(meshUuid);
		_viewportWidget->clearSimulationTensorGlyphs(meshUuid);
		_viewportWidget->clearSimulationVolume(meshUuid);
		_visibleMeshUuids.remove(meshUuid);
		const int index = _viewportWidget->getIndexByUuid(meshUuid);
		if (index >= 0)
			_viewportWidget->removeFromDisplay(index);
	}
	_plot3DPreviewMeshes.clear();
	_viewportWidget->setDisplayList(visibleIndicesFromState());
	refreshPlot3DAxes(); // restore the active committed plot's shared axes, if any
	_viewportWidget->updateView();
}

void ModelViewer::refreshPlot3DAxes()
{
	const QUuid active = activePlot3DMeshUuid();
	Plot3DSession* session = sessionFor(_plot3DSessions, active);
	if (session)
		applyAxes(this, session);
	else if (_viewportWidget)
		_viewportWidget->clearPlot3DAxisLayout();
	refreshPlot3DLegend();
}

void ModelViewer::refreshPlot3DLegend()
{
	if (!_viewportWidget)
		return;
	Plot3DSession* session = sessionFor(_plot3DSessions, activePlot3DMeshUuid());
	const QSet<QUuid> shown = getVisibleUuids();
	if (!session || !shown.contains(session->meshUuid))
	{
		if (_plot3DLegend)
			_plot3DLegend->setAliveCheck([]() { return false; });
		return;
	}
	if (!_plot3DLegend)
		_plot3DLegend = new SimulationLegendWidget(_viewportWidget);

	QPointer<ModelViewer> self(this);
	QPointer<ViewportWidget> viewport(_viewportWidget);
	// The heading names the plot; with a Simulation legend showing too, the two stack (this one below the other's real bottom edge,
	// not a fixed offset) so they never paint over one another. The plot legend returns to the normal top-right slot without it.
	_plot3DLegend->setPane([self, viewport]() {
		if (!self || !viewport || !self->_simulationLegend || !self->_simulationLegend->isVisible())
			return QRect();
		const int top = self->_simulationLegend->geometry().bottom() + 8;
		return QRect(0, top, viewport->width(), std::max(1, viewport->height() - top));
	}, session->name);
	const QUuid meshUuid = session->meshUuid;
	_plot3DLegend->setAliveCheck([self, meshUuid]() {
		if (!self || self->activePlot3DMeshUuid() != meshUuid)
			return false;
		return self->getVisibleUuids().contains(meshUuid);
	});
	// The title says what the colours mean; the plot's name is the heading above it.
	const QString label = session->primitive == Plot3DPrimitive::Voxel
		? tr("Occupancy")
		: (session->generated.valid && (session->generated.sourceMode == 7 || session->generated.sourceMode == 8))
			? tr("Time")
			: (session->primitive == Plot3DPrimitive::Quiver ? tr("Vector magnitude") : tr("Value"));
	_plot3DLegend->setLegend(label, session->colourMinimum, session->colourMaximum, session->colormap,
		session->bands, tr("Colour range for the active 3D plot."));
}
