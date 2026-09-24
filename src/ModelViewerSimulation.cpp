// ModelViewer's side of simulation results: file dialog, off-thread read, turning the boundary surface into an
// undoable scene node, and keeping that node's colouring and legend in step with the Simulation dock panel. Kept
// in its own translation unit (these are ModelViewer members, declared in ModelViewer.h) so ModelViewer.cpp does
// not grow further.
//
// Each opened result is a SimulationSession (dataset + boundary surface + result mesh + view state). The Simulation
// panel edits the ACTIVE session's SimulationViewState; applySimulationViewState() applies it through
// refreshSimulationDisplay(), which is also what runs when a result is opened.
//
// Rendering deliberately reuses the Surface Analysis overlay's GPU path (main_scene.frag with
// analysisOverlayBands >= 2: the interpolated normalized scalar is quantized per fragment, so colours are
// correct on coarse meshes and contour bands fall inside triangles) instead of a new shader - see
// docs/simulation_results_design.md section 6. Interim limits: no unit handling yet (the legend says so), node
// data only, one time step, and the coloured overlay is not persisted in MVF (the portable snapshot is a later
// slice).

#include "ModelViewer.h"

#include "AnalysisColorRamp.h"
#include "MainWindow.h"
#include "MeshVertex.h"
#include "SceneGraph.h"
#include "SceneMesh.h"
#include "ShaderProgram.h"
#include "ShrinkWrapCommand.h"
#include "SimulationLegendWidget.h"
#include "SimulationResultDisplay.h"
#include "ViewportWidget.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLocale>
#include <QMessageBox>
#include <QPointer>
#include <QSet>
#include <QSettings>
#include <QThread>

#include <cmath>
#include <memory>

namespace
{
	QString formatCount(std::size_t n)
	{
		return QLocale().toString(static_cast<qulonglong>(n));
	}
}

void ModelViewer::openSimulationResult()
{
	if (!_viewportWidget || !_sceneGraph || !_undoStack)
		return;
	if (_simulationLoadInFlight)
	{
		MainWindow::showStatusMessage(tr("A simulation result is already loading."), 3000);
		return;
	}

	QSettings settings;
	const QString lastDir = settings.value(QStringLiteral("simulation/lastDirectory")).toString();
	QStringList filters = supportedResultFileFilters();
	filters.prepend(tr("All simulation results (*.vtu *.vtk)"));
	const QString path = QFileDialog::getOpenFileName(this, tr("Open Simulation Result"), lastDir, filters.join(QStringLiteral(";;")));
	if (path.isEmpty())
		return;
	settings.setValue(QStringLiteral("simulation/lastDirectory"), QFileInfo(path).absolutePath());

	// Read and extract off the UI thread: a production-size result must not freeze the application.
	_simulationLoadInFlight = true;
	MainWindow::showProgressBar(false);
	QApplication::setOverrideCursor(Qt::WaitCursor);

	auto holder = std::make_shared<LoadedSimulationResult>();
	QThread* thread = QThread::create([holder, path]() { *holder = loadSimulationResult(path, nullptr); });
	QPointer<ModelViewer> self(this);
	// `thread` lives in the main thread, so this queued handler runs there. If the document was closed in the
	// meantime, `self` is null and the result is simply dropped.
	connect(thread, &QThread::finished, thread, [self, thread, holder, path]() {
		thread->deleteLater();
		QApplication::restoreOverrideCursor();
		MainWindow::hideProgressBar();
		if (!self)
			return;
		self->_simulationLoadInFlight = false;
		self->presentSimulationResult(path, *holder);
	});
	thread->start();
}

void ModelViewer::presentSimulationResult(const QString& path, LoadedSimulationResult& result)
{
	if (!result.ok())
	{
		QMessageBox::warning(this, tr("Open Simulation Result"),
			tr("Could not open '%1':\n\n%2").arg(QDir::toNativeSeparators(path), result.error));
		return;
	}
	const std::size_t triangleCount = result.surface.triangleCount();
	if (triangleCount == 0)
	{
		QMessageBox::information(this, tr("Open Simulation Result"),
			tr("'%1' was read (%2 nodes, %3 cells) but contains nothing that can be displayed yet.\n\n%4")
				.arg(QFileInfo(path).fileName(), formatCount(result.dataset->nodeCount()), formatCount(result.dataset->cellCount()),
				     result.warnings.join(QLatin1Char('\n'))));
		return;
	}

	ViewportWidget* viewport = _viewportWidget;
	viewport->makeCurrent();

	// ---- Geometry: the boundary surface as an ordinary SceneMesh ---------------------------------------------
	const ResultBoundarySurface& surface = result.surface;
	const std::vector<float> normals = computeSmoothVertexNormals(surface);
	std::vector<Vertex> vertices(surface.vertexCount());
	for (std::size_t i = 0; i < vertices.size(); ++i)
	{
		Vertex v{};
		v.Color = glm::vec4(1.0f);
		v.Position = glm::vec3(surface.positions[i * 3], surface.positions[i * 3 + 1], surface.positions[i * 3 + 2]);
		v.Normal = glm::vec3(normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]);
		v.Tangent = glm::vec3(0.0f);
		v.Bitangent = glm::vec3(0.0f);
		for (glm::vec2& uv : v.TexCoords)
			uv = glm::vec2(0.0f);
		vertices[i] = v;
	}

	const QString baseName = QFileInfo(path).completeBaseName();
	const QString meshName = viewport->generateUniqueMeshName(baseName);
	// skipOptimization = true: the analysis overlay is indexed by vertex, and the mesh optimiser would reorder
	// vertices (see SceneMesh::optimizeMesh()), so the per-vertex scalars would land on the wrong vertices.
	SceneMesh* mesh = new SceneMesh(viewport->getShader(), meshName, vertices, surface.triangles, {}, Material(), true);
	viewport->addToDisplay(mesh);
	const QUuid meshUuid = mesh->uuid();

	// ---- Scene node ------------------------------------------------------------------------------------------
	const QSet<QUuid> originalSelection = getSelectedUuids();
	SceneNode* node = new SceneNode();
	node->nodeUuid = QUuid::createUuid();
	node->name = baseName;
	SceneNode* parent = _sceneGraph->root();
	const int position = parent->children.size();
	_sceneGraph->insertChildNode(parent, node, position);
	_sceneGraph->restoreMeshUuid(node, meshUuid, 0);

	viewport->doneCurrent();
	viewport->updateView();
	updateDisplayList();

	// ---- Session: what the Simulation panel edits ------------------------------------------------------------
	DisplayScalar scalar;
	SimulationSession session;
	session.meshUuid = meshUuid;
	session.state = defaultViewState(*result.dataset, &scalar);
	session.dataset = result.dataset;
	session.surface = std::make_shared<ResultBoundarySurface>(std::move(result.surface)); // `surface` is invalid from here
	session.filePath = path;
	session.warnings = result.warnings;
	const int fieldIndex = session.state.fieldIndex;
	_simulationSessions.push_back(std::move(session));
	_activeSimulationMesh = meshUuid;
	connectSimulationHooks();
	refreshSimulationDisplay(_simulationSessions.back()); // colours + legend

	// One undoable step, reusing the "add one node + one mesh" command Shrink Wrap/Repair Mesh use.
	_undoStack->push(new ShrinkWrapCommand(this, viewport, node, parent, position, meshUuid, originalSelection,
	                                       tr("Open Simulation Result")));
	viewport->fitAll();

	QString message = tr("%1: %2 nodes, %3 cells, %4 boundary triangles").arg(
		QFileInfo(path).fileName(), formatCount(result.dataset->nodeCount()), formatCount(result.dataset->cellCount()),
		formatCount(triangleCount));
	if (fieldIndex >= 0 && scalar.valid())
		message += tr(" - showing %1 (%2 to %3)").arg(scalar.label).arg(scalar.minValue).arg(scalar.maxValue);
	else
		message += tr(" - no node field to colour by");
	if (!result.warnings.isEmpty())
		message += QStringLiteral(" - ") + result.warnings.first();
	MainWindow::showStatusMessage(message, 12000);

	emit simulationSessionChanged(true);
}

// ---------------------------------------------------------------------------------------------------------------
// Sessions
// ---------------------------------------------------------------------------------------------------------------

SimulationSession* ModelViewer::findSimulationSession(const QUuid& meshUuid)
{
	for (SimulationSession& s : _simulationSessions)
		if (s.meshUuid == meshUuid)
			return &s;
	return nullptr;
}

SimulationSession* ModelViewer::activeSimulationSessionMutable()
{
	if (!_viewportWidget)
		return nullptr;
	// The active one, if its mesh is still displayed (Undo of the open removes it).
	SimulationSession* active = findSimulationSession(_activeSimulationMesh);
	if (active && _viewportWidget->getMeshByUuid(active->meshUuid))
		return active;
	// Otherwise the most recently opened result that is still displayed.
	for (auto it = _simulationSessions.rbegin(); it != _simulationSessions.rend(); ++it)
		if (_viewportWidget->getMeshByUuid(it->meshUuid))
			return &*it;
	return nullptr;
}

const SimulationSession* ModelViewer::activeSimulationSession() const
{
	return const_cast<ModelViewer*>(this)->activeSimulationSessionMutable();
}

void ModelViewer::connectSimulationHooks()
{
	if (_simulationHooksConnected)
		return;
	_simulationHooksConnected = true;
	// Selecting a result mesh makes its session the one the panel shows.
	connect(_viewportWidget, &ViewportWidget::selectionChanged, this, [this](const QList<int>&) {
		const QSet<QUuid> selected = getSelectedUuids();
		for (const SimulationSession& s : _simulationSessions)
		{
			if (!selected.contains(s.meshUuid) || s.meshUuid == _activeSimulationMesh)
				continue;
			_activeSimulationMesh = s.meshUuid;
			if (SimulationSession* session = findSimulationSession(s.meshUuid))
				refreshSimulationDisplay(*session);
			emit simulationSessionChanged(false);
			return;
		}
	});
	// Undo/Redo of an open adds or removes a result mesh, which changes what the panel and legend should show.
	connect(_undoStack, &QUndoStack::indexChanged, this, [this](int) { emit simulationSessionChanged(false); });
}

void ModelViewer::applySimulationViewState(const SimulationViewState& state)
{
	SimulationSession* session = activeSimulationSessionMutable();
	if (!session)
		return;
	session->state = state;
	refreshSimulationDisplay(*session);
	emit simulationSessionChanged(false); // lets the panel show e.g. the recomputed automatic range
}

// Recolours the session's mesh and updates the legend from session.state.
void ModelViewer::refreshSimulationDisplay(SimulationSession& session)
{
	if (!_viewportWidget || !session.dataset || !session.surface)
		return;
	SceneMesh* mesh = _viewportWidget->getMeshByUuid(session.meshUuid);
	if (!mesh)
		return;

	const bool isActive = session.meshUuid == _activeSimulationMesh;
	DisplayScalar scalar;
	float lo = 0.0f, hi = 1.0f;
	const bool haveScalar = session.state.fieldIndex >= 0
		&& buildDisplayScalar(*session.dataset, session.state.fieldIndex, session.state.component, scalar)
		&& resolveViewRange(scalar, session.state, lo, hi);

	if (!haveScalar)
	{
		mesh->clearAnalysisOverlay(); // CPU-only, no GL context needed
		if (isActive && _simulationLegend)
			_simulationLegend->setAliveCheck([]() { return false; });
		_viewportWidget->update();
		return;
	}

	const std::vector<float> vertexValues = boundaryVertexValues(*session.surface, scalar.nodeValues);
	std::vector<bool> valid(vertexValues.size());
	for (std::size_t i = 0; i < vertexValues.size(); ++i)
		valid[i] = std::isfinite(vertexValues[i]);

	_viewportWidget->makeCurrent();
	mesh->setAnalysisOverlayColors(AnalysisColorRamp::mapToNormalizedScalarRGBA(vertexValues, valid, lo, hi));
	mesh->setAnalysisOverlayBanding(simulationShaderBands(session.state), session.state.colormap);
	_viewportWidget->doneCurrent();

	if (isActive)
	{
		if (!_simulationLegend)
			_simulationLegend = new SimulationLegendWidget(_viewportWidget);
		// No units yet (design section 7): say so instead of implying a unit.
		_simulationLegend->setLegend(tr("%1  [unit not specified]").arg(scalar.label), lo, hi, session.state.colormap,
		                             session.state.bands,
		                             tr("%1\n%2").arg(QDir::toNativeSeparators(session.filePath), session.warnings.join(QLatin1Char('\n'))));
		// Shown only while this result's mesh is still displayed (it disappears with Undo, returns with Redo).
		QPointer<ViewportWidget> viewportGuard(_viewportWidget);
		const QUuid meshUuid = session.meshUuid;
		_simulationLegend->setAliveCheck([viewportGuard, meshUuid]() { return viewportGuard && viewportGuard->getMeshByUuid(meshUuid); });
	}
	_viewportWidget->update();
}
