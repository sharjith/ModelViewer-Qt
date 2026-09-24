// ModelViewer's side of loading a simulation result: file dialog, off-thread read, and turning the boundary
// surface into an undoable scene node coloured by a scalar field. Kept in its own translation unit (these are
// ModelViewer members, declared in ModelViewer.h) so ModelViewer.cpp does not grow further.
//
// Rendering deliberately reuses the Surface Analysis overlay's GPU path (main_scene.frag with
// analysisOverlayBands >= 2: the interpolated normalized scalar is quantized per fragment, so colours are
// correct on coarse meshes and contour bands fall inside triangles) instead of a new shader - see
// docs/simulation_results_design.md section 6. Interim limits of this first viewport slice: one default
// field, no field/range/colormap controls yet (the Simulation dock tab), no unit handling yet (the legend says
// so), and the coloured overlay is not persisted in MVF (the portable snapshot is a later slice).

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
	// 256 levels: visually continuous, while still quantized per fragment by the shared shader path.
	constexpr int kSmoothBands = 256;

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
	const ResultBoundarySurface& surface = result.surface;
	if (surface.triangleCount() == 0)
	{
		QMessageBox::information(this, tr("Open Simulation Result"),
			tr("'%1' was read (%2 nodes, %3 cells) but contains nothing that can be displayed yet.\n\n%4")
				.arg(QFileInfo(path).fileName(), formatCount(result.dataset->nodeCount()), formatCount(result.dataset->cellCount()),
				     result.warnings.join(QLatin1Char('\n'))));
		return;
	}

	DisplayScalar scalar;
	const bool haveScalar = chooseDefaultDisplayScalar(*result.dataset, scalar);

	ViewportWidget* viewport = _viewportWidget;
	viewport->makeCurrent();

	// ---- Geometry: the boundary surface as an ordinary SceneMesh ---------------------------------------------
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
	// vertices (see SceneMesh::optimizeMesh()), so the per-vertex scalars below would land on the wrong vertices.
	SceneMesh* mesh = new SceneMesh(viewport->getShader(), meshName, vertices, surface.triangles, {}, Material(), true);
	viewport->addToDisplay(mesh);
	const QUuid meshUuid = mesh->uuid();

	// ---- Colour: scalar in R, validity in A, banded per fragment by the shared shader ------------------------
	if (haveScalar)
	{
		const std::vector<float> vertexValues = boundaryVertexValues(surface, scalar.nodeValues);
		std::vector<bool> valid(vertexValues.size());
		for (std::size_t i = 0; i < vertexValues.size(); ++i)
			valid[i] = std::isfinite(vertexValues[i]);
		mesh->setAnalysisOverlayColors(
			AnalysisColorRamp::mapToNormalizedScalarRGBA(vertexValues, valid, scalar.minValue, scalar.maxValue));
		mesh->setAnalysisOverlayBanding(kSmoothBands, static_cast<int>(AnalysisColormap::Sequential));
	}

	// ---- Scene node + undo -----------------------------------------------------------------------------------
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
	// One undoable step, reusing the "add one node + one mesh" command Shrink Wrap/Repair Mesh use.
	_undoStack->push(new ShrinkWrapCommand(this, viewport, node, parent, position, meshUuid, originalSelection,
	                                       tr("Open Simulation Result")));
	viewport->fitAll();

	// ---- Legend ----------------------------------------------------------------------------------------------
	if (haveScalar)
	{
		if (!_simulationLegend)
			_simulationLegend = new SimulationLegendWidget(viewport);
		// No units yet (design section 7): say so instead of implying a unit.
		_simulationLegend->setLegend(tr("%1  [unit not specified]").arg(scalar.label), scalar.minValue, scalar.maxValue,
		                             tr("%1\n%2").arg(QDir::toNativeSeparators(path), result.warnings.join(QLatin1Char('\n'))));
		// Shown only while this result's mesh is still displayed (it disappears with Undo, returns with Redo).
		QPointer<ViewportWidget> viewportGuard(viewport);
		_simulationLegend->setAliveCheck([viewportGuard, meshUuid]() { return viewportGuard && viewportGuard->getMeshByUuid(meshUuid); });
	}
	else if (_simulationLegend)
		_simulationLegend->setAliveCheck([]() { return false; });

	QString message = tr("%1: %2 nodes, %3 cells, %4 boundary triangles").arg(
		QFileInfo(path).fileName(), formatCount(result.dataset->nodeCount()), formatCount(result.dataset->cellCount()),
		formatCount(surface.triangleCount()));
	if (haveScalar)
		message += tr(" - showing %1 (%2 to %3)").arg(scalar.label).arg(scalar.minValue).arg(scalar.maxValue);
	else
		message += tr(" - no node field to colour by");
	if (!result.warnings.isEmpty())
		message += QStringLiteral(" - ") + result.warnings.first();
	MainWindow::showStatusMessage(message, 12000);
}
