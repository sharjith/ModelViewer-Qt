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
// docs/simulation_results_design.md section 6. Units: see ResultUnits.h (a guessed file unit only labels the
// numbers; "Show in" converts). Interim limits: node data only, one time step, and the coloured overlay is not persisted in MVF (the portable snapshot is a later
// slice).

#include "ModelViewer.h"

#include "AnalysisColorRamp.h"
#include "LengthUnits.h"
#include "MainWindow.h"
#include "MeshSurfaceAnchor.h"
#include "MeshVertex.h"
#include "DeleteMeshCommand.h"
#include "MvfSceneBuilder.h"
#include "ResultSnapshot.h"
#include "ResultUnits.h"
#include "SceneGraph.h"
#include "SceneMesh.h"
#include "ShaderProgram.h"
#include "ShrinkWrapCommand.h"
#include "SimulationGlyphs.h"
#include "SimulationLegendWidget.h"
#include "SimulationTimelineWidget.h"
#include "SimulationResultDisplay.h"
#include "ViewportWidget.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLocale>
#include <QMessageBox>
#include <QPointer>
#include <QSet>
#include <QSettings>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace
{
	QString formatCount(std::size_t n)
	{
		return QLocale().toString(static_cast<qulonglong>(n));
	}

	// The status-bar progress bar is application-wide, but each document loads on its own: count the loads in flight so
	// one document finishing does not hide the bar while another is still reading.
	int g_simulationLoadsInFlight = 0;
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
	QString allGlobs;
	for (const QString& extension : supportedResultExtensions())
		allGlobs += (allGlobs.isEmpty() ? QString() : QStringLiteral(" ")) + QStringLiteral("*.") + extension;
	filters.prepend(tr("All simulation results (%1)").arg(allGlobs));
	const QString path = QFileDialog::getOpenFileName(this, tr("Add Simulation Result"), lastDir, filters.join(QStringLiteral(";;")));
	if (path.isEmpty())
		return;
	settings.setValue(QStringLiteral("simulation/lastDirectory"), QFileInfo(path).absolutePath());
	openSimulationResultFile(path);
}

bool ModelViewer::openSimulationResultFile(const QString& path)
{
	if (!_viewportWidget || !_sceneGraph || !_undoStack)
		return false;
	if (_simulationLoadInFlight)
	{
		// A multi-file import starts several results: the rest wait their turn instead of being dropped.
		_pendingSimulationFiles << path;
		MainWindow::showStatusMessage(tr("%1 is queued: another simulation result is still loading.").arg(QFileInfo(path).fileName()), 4000);
		return true;
	}

	// Read and extract off the UI thread: a production-size result must not freeze the application.
	_simulationLoadInFlight = true;
	if (++g_simulationLoadsInFlight == 1)
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
		if (--g_simulationLoadsInFlight <= 0)
		{
			g_simulationLoadsInFlight = 0;
			MainWindow::hideProgressBar();
		}
		if (!self)
			return;
		self->_simulationLoadInFlight = false;
		self->presentSimulationResult(path, *holder);
		if (self)
			self->startNextPendingSimulationFile();
	});
	thread->start();
	return true;
}

void ModelViewer::startNextPendingSimulationFile()
{
	if (_simulationLoadInFlight || _pendingSimulationFiles.isEmpty() || !_viewportWidget)
		return;
	const QString next = _pendingSimulationFiles.takeFirst();
	openSimulationResultFile(next);
}

void ModelViewer::presentSimulationResult(const QString& path, LoadedSimulationResult& result)
{
	if (!result.ok())
	{
		QMessageBox::warning(this, tr("Open Simulation Result"),
			tr("Could not open '%1':\n\n%2").arg(QDir::toNativeSeparators(path), result.error));
		closeEmptyResultDocument();
		return;
	}
	const std::size_t triangleCount = result.surface.triangleCount();
	if (triangleCount == 0)
	{
		QMessageBox::information(this, tr("Open Simulation Result"),
			tr("'%1' was read (%2 nodes, %3 cells) but contains nothing that can be displayed yet.\n\n%4")
				.arg(QFileInfo(path).fileName(), formatCount(result.dataset->nodeCount()), formatCount(result.dataset->cellCount()),
				     result.warnings.join(QLatin1Char('\n'))));
		closeEmptyResultDocument();
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
	// The result file's own length unit (readers that know it - OpenFOAM, CGNS - set it) becomes the node's unit, which Mass
	// Properties and Surface Analysis read; a file that does not state one leaves it unset (the document default / mm).
	node->importUnit = lengthUnitFromString(result.dataset->lengthUnit, LengthUnit::Unknown);
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
	session.extentsValid = surfaceExtents(*session.surface, session.extents[0], session.extents[1], session.extents[2]);
	session.displacementField = findDisplacementField(*session.dataset);
	session.modal = isModalResult(*session.dataset);
	if (session.displacementField >= 0)
	{
		session.autoDeformScale = autoDeformScale(*session.dataset, *session.surface, session.displacementField);
		session.state.deformScale = session.autoDeformScale;
	}
	const int fieldIndex = session.state.fieldIndex;
	_simulationSessions.push_back(std::move(session));
	_activeSimulationMesh = meshUuid;
	connectSimulationHooks();
	refreshSimulationDisplay(_simulationSessions.back()); // colours + legend

	// A document that File > Open created just for this file (still empty and untouched, also after the read) takes it as
	// its content, not as an undoable edit, and stays unmodified. Anything else - an import into a document with unsaved
	// changes, or one that was edited while the read ran - must keep its state: the result is one undoable step.
	const bool freshDocument = _closeOnSimulationLoadFailure && !_documentModified && _undoStack->count() == 0;
	_closeOnSimulationLoadFailure = false;
	if (freshDocument)
		setDocumentModified(false);
	else
	{
		// Added to a document that already has content: one undoable step, reusing the "add one node + one mesh"
		// command Shrink Wrap/Repair Mesh use.
		_undoStack->push(new ShrinkWrapCommand(this, viewport, node, parent, position, meshUuid, originalSelection,
		                                       tr("Open Simulation Result")));
	}
	viewport->fitAll();

	QString message = tr("%1: %2 nodes, %3 cells, %4 boundary triangles").arg(
		QFileInfo(path).fileName(), formatCount(result.dataset->nodeCount()), formatCount(result.dataset->cellCount()),
		formatCount(triangleCount));
	if (fieldIndex >= 0 && scalar.valid())
		message += tr(" - showing %1 (%2 to %3)").arg(scalar.label).arg(scalar.minValue).arg(scalar.maxValue);
	else
		message += tr(" - no field to colour by");
	if (!result.warnings.isEmpty())
		message += QStringLiteral(" - ") + result.warnings.first();
	MainWindow::showStatusMessage(message, 12000);

	emit simulationSessionChanged(true);
}

// Called when reading a result failed: a document that File > Open created only for that file is still empty, so
// close it instead of leaving a blank document behind. Does nothing for a document that already had content.
void ModelViewer::closeEmptyResultDocument()
{
	if (!_closeOnSimulationLoadFailure)
		return;
	_closeOnSimulationLoadFailure = false;
	if (_simulationSessions.empty() && _viewportWidget && _viewportWidget->getMeshStore().empty() && !_documentModified
	    && _undoStack && _undoStack->count() == 0)
	{
		_pendingSimulationFiles.clear();
		setDocumentModified(false); // no unsaved-changes prompt for a document that never had content
		close();
	}
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

QVector<SimulationResultItem> ModelViewer::simulationResults() const
{
	QVector<SimulationResultItem> items;
	if (!_viewportWidget)
		return items;
	for (const SimulationSession& s : _simulationSessions)
	{
		SceneMesh* mesh = _viewportWidget->getMeshByUuid(s.meshUuid);
		if (!mesh)
			continue; // deleted (Undo brings it back)
		SimulationResultItem item;
		item.meshUuid = s.meshUuid;
		item.name = mesh->getName();
		item.visible = _visibleMeshUuids.contains(s.meshUuid);
		items.append(item);
	}
	return items;
}

QUuid ModelViewer::activeSimulationMeshUuid() const
{
	const SimulationSession* active = activeSimulationSession();
	return active ? active->meshUuid : QUuid();
}

void ModelViewer::activateSimulationResult(const QUuid& meshUuid)
{
	SimulationSession* session = findSimulationSession(meshUuid);
	if (!session || !_viewportWidget || !_viewportWidget->getMeshByUuid(meshUuid))
		return;
	setSelectionWithoutUndo({ meshUuid }); // the selection hook below normally switches the session ...
	if (_activeSimulationMesh != meshUuid)  // ... this covers a selection that did not change anything
	{
		_activeSimulationMesh = meshUuid;
		refreshSimulationDisplay(*session);
		emit simulationSessionChanged(false);
	}
}

void ModelViewer::setSimulationResultVisible(const QUuid& meshUuid, bool visible)
{
	if (!_viewportWidget || !findSimulationSession(meshUuid) || !_viewportWidget->getMeshByUuid(meshUuid))
		return;
	QSet<QUuid> shown = getVisibleUuids();
	if (shown.contains(meshUuid) == visible)
		return;
	if (visible)
		shown.insert(meshUuid);
	else
		shown.remove(meshUuid);
	setVisibilityWithUndo(shown, visible ? tr("Show Simulation Result") : tr("Hide Simulation Result"));
	// The legend and timeline belong to the visible active result; markers and the mesh follow the visibility.
	if (_simulationLegend)
		_simulationLegend->refresh();
	if (_simulationTimeline)
		_simulationTimeline->refresh();
	emit simulationSessionChanged(false);
}

void ModelViewer::closeSimulationResult(const QUuid& meshUuid)
{
	if (!_viewportWidget || !_undoStack || !findSimulationSession(meshUuid) || !_viewportWidget->getMeshByUuid(meshUuid))
		return;
	// The session stays in the list (it is a few references): Undo restores the mesh and the result with it. The mesh
	// leaving the viewport is what removes it from the panel, legend, timeline and markers.
	_undoStack->push(new DeleteMeshCommand(this, _viewportWidget, QVector<QUuid>{ meshUuid }));
	updateControls();
	// Another result may now be the one to show: its legend, markers and timeline take over.
	if (SimulationSession* next = activeSimulationSessionMutable())
	{
		_activeSimulationMesh = next->meshUuid;
		refreshSimulationDisplay(*next);
	}
	else
		pushSimulationMarkers();
	if (_simulationLegend)
		_simulationLegend->refresh();
	if (_simulationTimeline)
		_simulationTimeline->refresh();
	emit simulationSessionChanged(false);
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
	// The timeline (multi-step results) follows whichever session is active.
	connect(this, &ModelViewer::simulationSessionChanged, this, [this](bool) {
		checkSimulationCompare();
		updateSimulationTimeline();
	});
}

void ModelViewer::applySimulationViewState(const SimulationViewState& state)
{
	SimulationSession* session = activeSimulationSessionMutable();
	if (!session)
		return;
	const int keepStep = session->state.step; // the timeline owns the step, the panel does not
	session->state = state;
	session->state.step = keepStep;
	refreshSimulationDisplay(*session);
	emit simulationSessionChanged(false); // lets the panel show e.g. the recomputed automatic range
}

void ModelViewer::applySimulationLengthUnit(const QString& unitText)
{
	SimulationSession* session = activeSimulationSessionMutable();
	if (!session || !session->dataset)
		return;
	const LengthUnit unit = lengthUnitFromString(unitText, LengthUnit::Unknown);
	session->dataset->lengthUnit = unit == LengthUnit::Unknown ? QString() : lengthUnitToString(unit);
	if (SceneNode* node = _sceneGraph ? _sceneGraph->findNodeForMesh(session->meshUuid) : nullptr)
	{
		node->importUnit = unit;
		node->importUnitUserOverridden = unit != LengthUnit::Unknown;
	}
	markNonUndoDocumentModified();
	notifyImportUnitsChanged();
	emit simulationSessionChanged(false);
}

void ModelViewer::applySimulationUnits(int fieldIndex, const QString& kindId, const QString& fileUnit, const QString& displayUnit)
{
	SimulationSession* session = activeSimulationSessionMutable();
	if (!session || !session->dataset)
		return;
	ResultDataset& dataset = *session->dataset;
	if (fieldIndex < 0 || static_cast<std::size_t>(fieldIndex) >= dataset.fields.size())
		return;

	const ResultField before = dataset.fields[static_cast<std::size_t>(fieldIndex)];
	const QString oldDisplay = before.displayUnit.isEmpty() ? before.fileUnit : before.displayUnit;
	if (!setFieldUnits(dataset, fieldIndex, kindId, fileUnit, displayUnit))
	{
		emit simulationSessionChanged(false); // invalid combination: let the panel show what is really set
		return;
	}
	const ResultField& after = dataset.fields[static_cast<std::size_t>(fieldIndex)];
	const QString newDisplay = after.displayUnit.isEmpty() ? after.fileUnit : after.displayUnit;

	if (session->state.customRange)
	{
		const bool numbersChanged = before.quantityKind != after.quantityKind || before.fileUnit != after.fileUnit;
		const UnitConversion follow = unitConversion(after.quantityKind, oldDisplay, newDisplay);
		if (!numbersChanged && follow.valid)
		{
			// Same numbers, shown in another unit: the range the user set is the same physical range.
			session->state.rangeMin = follow.apply(session->state.rangeMin);
			session->state.rangeMax = follow.apply(session->state.rangeMax);
			if (session->state.rangeMin > session->state.rangeMax)
				std::swap(session->state.rangeMin, session->state.rangeMax);
		}
		else
			session->state.customRange = false; // what the numbers mean changed: a range typed for the old ones no longer applies
	}
	refreshSimulationDisplay(*session);
	emit simulationSessionChanged(false);
}

// The value under the cursor for the result mesh the anchor is on: "von Mises: 1.2345e+07 MPa  (node 42)".
QString ModelViewer::simulationProbeText(const MeshSurfaceAnchor& anchor, QColor& color) const
{
	if (!anchor.isValid())
		return QString();
	const SimulationSession* session = nullptr;
	for (const SimulationSession& s : _simulationSessions)
		if (s.meshUuid == anchor.meshUuid)
			session = &s;
	if (!session || !session->dataset || !session->surface || !session->shownScalar.valid())
		return QString();
	const ProbeSample sample = sampleSurfaceScalar(*session->dataset, *session->surface, session->shownScalar,
		static_cast<std::size_t>(anchor.triangleIndex), anchor.barycentric.x(), anchor.barycentric.y(), anchor.barycentric.z(),
		session->shownLo, session->shownHi);
	if (!sample.valid)
		return tr("%1: no value").arg(session->shownScalar.label);

	// White or black, whichever reads against the colour the point is painted with.
	const QColor painted = AnalysisColorRamp::colorForNormalized(sample.normalized, static_cast<AnalysisColormap>(session->state.colormap));
	color = painted.lightness() < 128 ? Qt::white : Qt::black;

	QString text = QStringLiteral("%1: %2").arg(session->shownScalar.label, QLocale().toString(static_cast<double>(sample.value), 'g', 5));
	if (!session->shownScalar.unit.isEmpty())
		text += QLatin1Char(' ') + session->shownScalar.unit;
	return text + (sample.cell ? tr("  (cell %1)").arg(sample.nodeId) : tr("  (node %1)").arg(sample.nodeId));
}

// ---------------------------------------------------------------------------------------------------------------
// Saving into .mvf (docs/simulation_mvf_persistence_design.md, S2)
// ---------------------------------------------------------------------------------------------------------------

namespace
{
	SnapshotOptions snapshotOptionsFor(const SimulationSession& session, bool allFields)
	{
		SnapshotOptions options;
		options.content = allFields ? SnapshotOptions::Content::AllFields : SnapshotOptions::Content::ShownAndDisplacement;
		options.shownField = session.state.fieldIndex;
		return options;
	}

	void padTo4(QByteArray& buffer)
	{
		while (buffer.size() % 4 != 0)
			buffer.append(char(0));
	}
}

// The shown colours per mesh vertex (RGBA), written as COLOR_0 so other glTF viewers show the result.
QHash<QUuid, std::vector<float>> ModelViewer::simulationBakedColors() const
{
	QHash<QUuid, std::vector<float>> colors;
	if (!_viewportWidget)
		return colors;
	for (const SimulationSession& session : _simulationSessions)
	{
		if (!session.surface || !session.shownScalar.valid() || !_viewportWidget->getMeshByUuid(session.meshUuid))
			continue;
		const std::vector<float> values = surfaceVertexValues(*session.surface, session.shownScalar); // cell data: averaged per vertex
		const float span = session.shownHi > session.shownLo ? session.shownHi - session.shownLo : 1.0f;
		const int bands = session.state.bands >= 2 ? session.state.bands : 0;
		std::vector<float> rgba(values.size() * 4);
		for (std::size_t i = 0; i < values.size(); ++i)
		{
			QColor color;
			if (!std::isfinite(values[i]))
				color = AnalysisColorRamp::invalidSampleColor();
			else
			{
				float t = std::clamp((values[i] - session.shownLo) / span, 0.0f, 1.0f);
				if (bands > 0)
					t = std::min(1.0f, (std::floor(t * static_cast<float>(bands)) + 0.5f) / static_cast<float>(bands)); // contour band centre
				color = AnalysisColorRamp::colorForNormalized(t, static_cast<AnalysisColormap>(session.state.colormap));
			}
			rgba[i * 4] = static_cast<float>(color.redF());
			rgba[i * 4 + 1] = static_cast<float>(color.greenF());
			rgba[i * 4 + 2] = static_cast<float>(color.blueF());
			rgba[i * 4 + 3] = 1.0f;
		}
		colors.insert(session.meshUuid, std::move(rgba));
	}
	return colors;
}

void ModelViewer::appendSimulationSnapshots(Mvf::MVFPackage& package) const
{
	_simulationSaveNotes.clear();
	if (_simulationSessions.empty() || !_viewportWidget || _simulationSaveContent == SimulationSaveContent::GeometryOnly)
		return;

	QJsonArray results;
	for (const SimulationSession& session : _simulationSessions)
	{
		if (!session.dataset || !session.surface || !_viewportWidget->getMeshByUuid(session.meshUuid))
			continue;
		ResultSnapshot snapshot;
		QString error;
		if (!encodeResultSnapshot(*session.dataset, *session.surface, session.state,
		                          snapshotOptionsFor(session, _simulationSaveContent == SimulationSaveContent::AllFields), snapshot, &error))
		{
			_simulationSaveNotes << tr("A simulation result was saved without its data (%1).").arg(error);
			continue;
		}
		_simulationSaveNotes << snapshot.notes;

		// The blobs go into the GEOM buffer, each as its own bufferView (glTF-style indirection, like the OCC edges).
		QJsonArray views;
		for (std::size_t i = 0; i < snapshot.blobs.size(); ++i)
		{
			padTo4(package.geometryChunk);
			const qint64 offset = package.geometryChunk.size();
			package.geometryChunk.append(snapshot.blobs[i]);
			QJsonObject view;
			view.insert(QStringLiteral("buffer"), 0);
			view.insert(QStringLiteral("byteOffset"), offset);
			view.insert(QStringLiteral("byteLength"), static_cast<qint64>(snapshot.blobs[i].size()));
			view.insert(QStringLiteral("name"), QStringLiteral("SIM_%1_%2").arg(session.meshUuid.toString(QUuid::WithoutBraces)).arg(i));
			views.append(package.document.bufferViews.size());
			package.document.bufferViews.append(view);
		}
		QJsonObject entry;
		entry.insert(QStringLiteral("meshUuid"), session.meshUuid.toString(QUuid::WithoutBraces));
		entry.insert(QStringLiteral("content"), _simulationSaveContent == SimulationSaveContent::AllFields ? QStringLiteral("all") : QStringLiteral("shown"));
		entry.insert(QStringLiteral("snapshot"), snapshot.json);
		entry.insert(QStringLiteral("blobViews"), views);
		results.append(entry);
	}
	if (!results.isEmpty())
		package.document.mvfSession.insert(QStringLiteral("simulationResults"), results);
}

// Asks what to store, once per session, on the first save of a document that has results. False = cancelled.
bool ModelViewer::promptSimulationSaveOptions()
{
	if (_simulationSavePrompted || _simulationSessions.empty() || !_viewportWidget)
		return true;
	std::uint64_t shown = 0, all = 0;
	int results = 0;
	for (const SimulationSession& session : _simulationSessions)
	{
		if (!session.dataset || !session.surface || !_viewportWidget->getMeshByUuid(session.meshUuid))
			continue;
		++results;
		shown += estimateSnapshotSize(*session.dataset, *session.surface, snapshotOptionsFor(session, false)).storedBytes;
		all += estimateSnapshotSize(*session.dataset, *session.surface, snapshotOptionsFor(session, true)).storedBytes;
	}
	if (results == 0)
		return true;

	const auto sizeText = [](std::uint64_t bytes) { return QLocale().formattedDataSize(static_cast<qint64>(bytes)); };
	QDialog dialog(this);
	dialog.setWindowTitle(tr("Save Simulation Results"));
	auto* layout = new QVBoxLayout(&dialog);
	auto* intro = new QLabel(tr("This document contains %n simulation result(s). Choose what to store in the .mvf file, so it can "
	                            "be reopened without the original result file:", nullptr, results), &dialog);
	intro->setWordWrap(true);
	layout->addWidget(intro);
	auto* shownRadio = new QRadioButton(tr("Shown field and displacement, all time steps  (about %1)  - recommended").arg(sizeText(shown)), &dialog);
	auto* allRadio = new QRadioButton(tr("All fields, all time steps  (about %1)").arg(sizeText(all)), &dialog);
	auto* geometryRadio = new QRadioButton(tr("Geometry only  (the surface with the colours as displayed; no result data)"), &dialog);
	shownRadio->setChecked(true);
	layout->addWidget(shownRadio);
	layout->addWidget(allRadio);
	layout->addWidget(geometryRadio);
	auto* note = new QLabel(tr("Sizes are estimates: the data is compressed without loss. Only the visible surface is stored, not the "
	                           "volume. At most 100 time steps are stored per result (evenly spaced). You are asked once per session."), &dialog);
	note->setWordWrap(true);
	layout->addWidget(note);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);

	if (dialog.exec() != QDialog::Accepted)
		return false;
	_simulationSaveContent = geometryRadio->isChecked() ? SimulationSaveContent::GeometryOnly
		: (allRadio->isChecked() ? SimulationSaveContent::AllFields : SimulationSaveContent::ShownAndDisplacement);
	_simulationSavePrompted = true;
	return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Loading from .mvf (docs/simulation_mvf_persistence_design.md, S3)
// ---------------------------------------------------------------------------------------------------------------

void ModelViewer::restoreSimulationSessions(QVector<PendingSimulationRestore>& restores)
{
	if (restores.isEmpty() || !_viewportWidget)
		return;

	QStringList failures;
	for (PendingSimulationRestore& pending : restores)
	{
		SceneMesh* mesh = _viewportWidget->getMeshByUuid(pending.meshUuid);
		if (!mesh)
			continue;
		if (!pending.decoded.dataset)
		{
			failures << tr("%1: %2").arg(mesh->getName(), pending.error.isEmpty() ? tr("the stored result data is unusable") : pending.error);
			continue;
		}

		// The surface of a snapshot is the identity: vertex i is node i, triangle t is cell t.
		auto surface = std::make_shared<ResultBoundarySurface>();
		surface->positions = pending.decoded.restPositions;
		surface->vertexNode.resize(surface->vertexCount());
		for (std::size_t v = 0; v < surface->vertexNode.size(); ++v)
			surface->vertexNode[v] = static_cast<std::uint32_t>(v);
		const std::vector<unsigned int> indices = mesh->indices();
		surface->triangles.assign(indices.begin(), indices.end());
		surface->triangleCell.resize(surface->triangleCount());
		for (std::size_t t = 0; t < surface->triangleCell.size(); ++t)
			surface->triangleCell[t] = static_cast<std::uint32_t>(t);
		surface->triangleFace.assign(surface->triangleCount(), ResultBoundarySurface::kNoFace);

		// Back to the rest shape with plain vertex colours: the saved mesh carries the deformed pose (if deformation
		// was on) and the baked COLOR_0 written for other viewers; the app draws the result with its own overlay.
		{
			const std::vector<float> normals = computeSmoothVertexNormals(*surface);
			std::vector<Vertex> vertices = mesh->vertices();
			if (vertices.size() * 3 == surface->positions.size())
			{
				for (std::size_t i = 0; i < vertices.size(); ++i)
				{
					vertices[i].Position = glm::vec3(surface->positions[i * 3], surface->positions[i * 3 + 1], surface->positions[i * 3 + 2]);
					vertices[i].Normal = glm::vec3(normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]);
					vertices[i].Color = glm::vec4(1.0f);
				}
				_viewportWidget->makeCurrent();
				mesh->setMeshData(vertices, indices);
				_viewportWidget->doneCurrent();
			}
		}

		// A file saved before results carried a unit on their node: take the result's own.
		if (SceneNode* node = _sceneGraph ? _sceneGraph->findNodeForMesh(pending.meshUuid) : nullptr)
			if (node->importUnit == LengthUnit::Unknown && pending.decoded.dataset)
				node->importUnit = lengthUnitFromString(pending.decoded.dataset->lengthUnit, LengthUnit::Unknown);

		SimulationSession session;
		session.meshUuid = pending.meshUuid;
		session.dataset = pending.decoded.dataset;
		session.surface = surface;
		session.extentsValid = surfaceExtents(*session.surface, session.extents[0], session.extents[1], session.extents[2]);
		session.filePath = pending.decoded.sourcePath;
		session.warnings = pending.decoded.warnings;
		session.warnings << tr("Restored from a saved snapshot of the visible surface; the original result file is not needed.");
		session.state = pending.decoded.state;
		if (session.state.fieldIndex < 0)
			session.state.fieldIndex = defaultViewState(*session.dataset).fieldIndex;
		session.displacementField = findDisplacementField(*session.dataset);
		session.modal = isModalResult(*session.dataset);
		session.autoDeformScale = session.displacementField >= 0 ? autoDeformScale(*session.dataset, *session.surface, session.displacementField) : 1.0;
		if (session.displacementField < 0)
			session.state.deform = false;
		session.deformApplied = false; // the mesh is at its rest shape; refresh applies the saved deformation

		_simulationSessions.push_back(std::move(session));
		_activeSimulationMesh = pending.meshUuid;
		connectSimulationHooks();
		refreshSimulationDisplay(_simulationSessions.back());
	}

	if (!_simulationSessions.empty())
		emit simulationSessionChanged(false); // panel, legend and timeline follow the restored result

	if (!failures.isEmpty())
		QMessageBox::warning(this, tr("Simulation Results"),
			tr("The simulation result data could not be restored for:\n\n%1\n\nThe surface is shown with the colours it had when it was saved.")
				.arg(failures.join(QLatin1Char('\n'))));
}

// ---------------------------------------------------------------------------------------------------------------
// Compare mode
// ---------------------------------------------------------------------------------------------------------------

void ModelViewer::startSimulationCompare(const QUuid& otherMeshUuid, bool stacked, bool sharedRange)
{
	SimulationSession* first = activeSimulationSessionMutable();
	SimulationSession* second = findSimulationSession(otherMeshUuid);
	if (!first || !second || first == second || !_viewportWidget || !_viewportWidget->getMeshByUuid(first->meshUuid)
	    || !_viewportWidget->getMeshByUuid(second->meshUuid))
		return;

	// Both results must be shown for the panes to have something to draw.
	QSet<QUuid> shown = getVisibleUuids();
	if (!shown.contains(first->meshUuid) || !shown.contains(second->meshUuid))
	{
		shown.insert(first->meshUuid);
		shown.insert(second->meshUuid);
		setVisibilityWithoutUndo(shown);
	}
	if (_simulationPlaying)
		setSimulationPlaying(false);

	_simulationCompareActive = true;
	_simulationCompareStacked = stacked;
	_simulationCompareSharedRange = sharedRange;
	_simulationCompareMeshes = { first->meshUuid, second->meshUuid };
	_viewportWidget->setCompareLinkCameras(QSettings().value(QStringLiteral("Simulation/compareLinkCameras"), false).toBool());
	_viewportWidget->setCompareResults(_simulationCompareMeshes, stacked ? CompareArrangement::Stacked : CompareArrangement::SideBySide);
	if (_simulationLegend)
		_simulationLegend->setAliveCheck([]() { return false; }); // each pane has its own legend now
	syncComparePartnerStep(*first);
	refreshComparePair();
	_viewportWidget->fitAll(); // fitted to a pane now, not the whole window
	emit simulationSessionChanged(false);
}

void ModelViewer::setSimulationCompareOptions(bool stacked, bool sharedRange)
{
	if (!_simulationCompareActive || !_viewportWidget)
		return;
	_simulationCompareStacked = stacked;
	_simulationCompareSharedRange = sharedRange;
	_viewportWidget->setCompareLinkCameras(QSettings().value(QStringLiteral("Simulation/compareLinkCameras"), false).toBool());
	_viewportWidget->setCompareResults(_simulationCompareMeshes, stacked ? CompareArrangement::Stacked : CompareArrangement::SideBySide);
	// Turning the shared range off must let each result go back to its own range.
	refreshComparePair();
	emit simulationSessionChanged(false);
}

void ModelViewer::stopSimulationCompare()
{
	if (!_simulationCompareActive)
		return;
	_simulationCompareActive = false;
	_simulationCompareSharedRange = false;
	_simulationCompareMeshes.clear();
	if (_viewportWidget)
	{
		_viewportWidget->clearCompare();
		_viewportWidget->fitAll(); // back to the whole window
	}
	for (const QPointer<SimulationLegendWidget>& legend : std::as_const(_compareLegends))
		if (legend)
			legend->deleteLater();
	_compareLegends.clear();
	// Every result back to its own colour range, and the single legend returns for the active one.
	for (SimulationSession& session : _simulationSessions)
		if (_viewportWidget && _viewportWidget->getMeshByUuid(session.meshUuid) && session.shownScalar.valid())
			refreshSimulationDisplay(session);
	emit simulationSessionChanged(false);
}

void ModelViewer::syncComparePartnerStep(const SimulationSession& driver)
{
	if (!driver.dataset || driver.dataset->stepCount() < 1)
		return;
	const int driverLast = static_cast<int>(driver.dataset->stepCount()) - 1;
	for (const QUuid& id : std::as_const(_simulationCompareMeshes))
	{
		if (id == driver.meshUuid)
			continue;
		SimulationSession* partner = findSimulationSession(id);
		if (!partner || !partner->dataset || partner->dataset->stepCount() < 2)
			continue; // a single-step result has nothing to follow
		const int partnerLast = static_cast<int>(partner->dataset->stepCount()) - 1;
		const int mapped = driverLast > 0
			? static_cast<int>(std::lround(static_cast<double>(driver.state.step) * partnerLast / driverLast)) : 0;
		partner->state.step = std::clamp(mapped, 0, partnerLast);
	}
}

void ModelViewer::refreshComparePair()
{
	if (_simulationCompareMeshes.size() < 2)
		return;
	SimulationSession* a = findSimulationSession(_simulationCompareMeshes[0]);
	SimulationSession* b = findSimulationSession(_simulationCompareMeshes[1]);
	if (!a || !b)
		return;
	_refreshingComparePartner = true; // this function does the pairing itself; a refresh must not chase its partner
	refreshSimulationDisplay(*a);
	refreshSimulationDisplay(*b);
	if (_simulationCompareSharedRange)
	{
		// The first pass gave each result its own new range; now each takes the union with the other's up-to-date one.
		refreshSimulationDisplay(*a);
		refreshSimulationDisplay(*b);
	}
	_refreshingComparePartner = false;
}

void ModelViewer::toggleSimulationCompare()
{
	if (_simulationCompareActive)
	{
		stopSimulationCompare();
		return;
	}
	const SimulationSession* active = activeSimulationSession();
	const QVector<SimulationResultItem> all = simulationResults();
	QVector<SimulationResultItem> others;
	QString activeName;
	for (const SimulationResultItem& item : all)
	{
		if (active && item.meshUuid == active->meshUuid)
			activeName = item.name;
		else
			others.append(item);
	}
	if (!active || others.isEmpty())
	{
		QMessageBox::information(this, tr("Compare Results"),
			tr("Comparing needs two simulation results in this document. Add another with Simulation > Add Result to This "
			   "Document, or open one with File > Open."));
		return;
	}
	QUuid partner = others.first().meshUuid;
	if (others.size() > 1)
	{
		QStringList names;
		for (const SimulationResultItem& item : std::as_const(others))
			names << item.name;
		bool ok = false;
		const QString chosen = QInputDialog::getItem(this, tr("Compare Results"),
			tr("Compare \"%1\" with:").arg(activeName), names, 0, false, &ok);
		if (!ok)
			return;
		partner = others[std::max(0, static_cast<int>(names.indexOf(chosen)))].meshUuid;
	}
	startSimulationCompare(partner, _simulationCompareStacked, _simulationCompareSharedRange);
}

// The viewport's min/max labels: the active result's, or in compare mode both compared results' (one per pane).
void ModelViewer::pushSimulationMarkers()
{
	if (!_viewportWidget)
		return;
	QVector<QUuid> shown;
	if (_simulationCompareActive)
		shown = _simulationCompareMeshes;
	else if (const SimulationSession* active = activeSimulationSession())
		shown.append(active->meshUuid);
	QVector<ViewportWidget::VertexMarker> markers;
	for (const QUuid& id : std::as_const(shown))
	{
		const SimulationSession* session = const_cast<ModelViewer*>(this)->findSimulationSession(id);
		if (!session)
			continue;
		for (const SimulationMarker& m : session->markers)
		{
			ViewportWidget::VertexMarker marker;
			marker.meshUuid = id;
			marker.vertex = m.vertex;
			marker.localNormal = QVector3D(m.normal[0], m.normal[1], m.normal[2]);
			marker.text = m.text;
			marker.color = m.lightText ? QColor(Qt::white) : QColor(Qt::black);
			markers.append(marker);
		}
	}
	_viewportWidget->setVertexMarkers(markers);
}

// Compare mode ends by itself when either result is hidden, closed, or removed by Undo.
void ModelViewer::checkSimulationCompare()
{
	if (!_simulationCompareActive)
		return;
	for (const QUuid& id : std::as_const(_simulationCompareMeshes))
		if (!_viewportWidget || !_viewportWidget->getMeshByUuid(id) || !_visibleMeshUuids.contains(id))
		{
			stopSimulationCompare();
			MainWindow::showStatusMessage(tr("Compare ended: one of the compared results is no longer shown."), 5000);
			return;
		}
}

// ---------------------------------------------------------------------------------------------------------------
// Time steps and playback
// ---------------------------------------------------------------------------------------------------------------

void ModelViewer::setSimulationStep(int step, bool fromPlayback)
{
	SimulationSession* session = activeSimulationSessionMutable();
	if (!session || !session->dataset)
		return;
	const int last = static_cast<int>(session->dataset->stepCount()) - 1;
	step = std::clamp(step, 0, std::max(0, last));
	if (step == session->state.step)
		return;
	session->state.step = step;
	if (_simulationCompareActive && _simulationCompareMeshes.contains(session->meshUuid))
	{
		// Compare mode: the partner steps along with it, so both panes show the same moment.
		syncComparePartnerStep(*session);
		refreshComparePair();
	}
	else
		refreshSimulationDisplay(*session);
	if (_simulationTimeline)
		_simulationTimeline->setCurrentStep(step);
	if (!fromPlayback)
		emit simulationSessionChanged(false); // the panel's per-step range display follows
}

void ModelViewer::setSimulationPlaying(bool playing)
{
	if (playing == _simulationPlaying)
		return;
	if (playing)
	{
		SimulationSession* session = activeSimulationSessionMutable();
		if (!session || !session->dataset || session->dataset->stepCount() < 2)
			return;
		if (!_simulationPlayTimer)
		{
			_simulationPlayTimer = new QTimer(this);
			connect(_simulationPlayTimer, &QTimer::timeout, this, &ModelViewer::advanceSimulationStep);
		}
		_simulationPlayingMesh = session->meshUuid;
		_simulationPlaying = true;
		_simulationPlayTimer->start(std::max(15, static_cast<int>(500.0 / _simulationSpeed)));
	}
	else
	{
		_simulationPlaying = false;
		_simulationPlayingMesh = QUuid();
		if (_simulationPlayTimer)
			_simulationPlayTimer->stop();
	}
	if (_simulationTimeline)
		_simulationTimeline->setPlaying(_simulationPlaying);
	if (!playing)
		emit simulationSessionChanged(false); // sync the panel now that the step is no longer moving under it
}

void ModelViewer::advanceSimulationStep()
{
	SimulationSession* session = activeSimulationSessionMutable();
	if (!session || !session->dataset || session->dataset->stepCount() < 2 || session->meshUuid != _simulationPlayingMesh)
	{
		setSimulationPlaying(false); // the result went away or another one became active
		return;
	}
	int next = session->state.step + 1;
	if (next >= static_cast<int>(session->dataset->stepCount()))
	{
		if (!_simulationLoop)
		{
			setSimulationPlaying(false);
			return;
		}
		next = 0;
	}
	setSimulationStep(next, true);
}

// Shows the timeline while the active result has more than one step, hides it (and stops playback) otherwise.
void ModelViewer::updateSimulationTimeline()
{
	SimulationSession* session = activeSimulationSessionMutable();
	const bool multiStep = session && session->dataset && session->dataset->stepCount() > 1;
	if (!multiStep)
	{
		if (_simulationPlaying)
			setSimulationPlaying(false);
		if (_simulationTimeline)
			_simulationTimeline->setAliveCheck([]() { return false; });
		return;
	}
	if (session->meshUuid != _simulationPlayingMesh && _simulationPlaying)
		setSimulationPlaying(false);

	if (!_simulationTimeline)
	{
		_simulationTimeline = new SimulationTimelineWidget(_viewportWidget);
		connect(_simulationTimeline, &SimulationTimelineWidget::stepRequested, this, [this](int step) { setSimulationStep(step, false); });
		connect(_simulationTimeline, &SimulationTimelineWidget::playRequested, this, [this](bool play) { setSimulationPlaying(play); });
		connect(_simulationTimeline, &SimulationTimelineWidget::loopChanged, this, [this](bool loop) { _simulationLoop = loop; });
		connect(_simulationTimeline, &SimulationTimelineWidget::speedChanged, this, [this](double speed) {
			_simulationSpeed = speed;
			if (_simulationPlaying && _simulationPlayTimer)
				_simulationPlayTimer->setInterval(std::max(15, static_cast<int>(500.0 / _simulationSpeed)));
		});
	}
	const std::shared_ptr<ResultDataset> dataset = session->dataset;
	_simulationTimeline->setSteps(static_cast<int>(dataset->stepCount()), [dataset](int i) { return stepDescription(*dataset, i); });
	_simulationTimeline->setCurrentStep(session->state.step);
	_simulationTimeline->setLoop(_simulationLoop);
	_simulationTimeline->setSpeed(_simulationSpeed);
	_simulationTimeline->setPlaying(_simulationPlaying);
	QPointer<ViewportWidget> viewportGuard(_viewportWidget);
	QPointer<ModelViewer> self(this);
	const QUuid meshUuid = session->meshUuid;
	_simulationTimeline->setAliveCheck([viewportGuard, self, meshUuid]() {
		return viewportGuard && viewportGuard->getMeshByUuid(meshUuid) && self && self->_visibleMeshUuids.contains(meshUuid);
	});
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
	const int stepCount = static_cast<int>(session.dataset->stepCount());
	session.state.step = std::clamp(session.state.step, 0, std::max(0, stepCount - 1));

	// ---- Deformed shape: rest positions + scale * displacement(step). The vertices are only re-uploaded when the
	// shown geometry actually changes (a recolour alone leaves them alone). Done before colouring because the
	// re-upload rebuilds the mesh buffers; the overlay colours are applied again below.
	{
		const bool wantDeform = session.state.deform && session.displacementField >= 0;
		const int wantStep = wantDeform ? session.state.step : 0;
		const double wantScale = wantDeform ? session.state.deformScale : 1.0; // the user's factor; see effectiveScale
		const bool changed = wantDeform
			? (!session.deformApplied || session.deformAppliedStep != wantStep || session.deformAppliedScale != wantScale)
			: session.deformApplied;
		if (changed)
		{
			std::vector<float> positions;
			// A modal result is shown with each mode normalised to a tenth of the model size, times the user's factor.
			const double effectiveScale = session.modal
				? wantScale * modalDisplayFactor(*session.dataset, *session.surface, session.displacementField, wantStep)
				: wantScale;
			const bool deformed = wantDeform
				&& buildDeformedPositions(*session.dataset, *session.surface, session.displacementField, wantStep, effectiveScale, positions);
			if (!deformed)
				positions = session.surface->positions; // rest shape (also when this step has no displacement data)
			const std::vector<float> normals = computeSmoothVertexNormals(positions, session.surface->triangles);
			std::vector<Vertex> vertices = mesh->vertices();
			if (vertices.size() * 3 == positions.size())
			{
				for (std::size_t i = 0; i < vertices.size(); ++i)
				{
					vertices[i].Position = glm::vec3(positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]);
					vertices[i].Normal = glm::vec3(normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]);
				}
				_viewportWidget->makeCurrent();
				mesh->setMeshData(vertices, session.surface->triangles);
				_viewportWidget->doneCurrent();
			}
			session.deformApplied = deformed;
			session.deformAppliedStep = wantStep;
			session.deformAppliedScale = wantScale;
		}
	}
	DisplayScalar scalar;
	float lo = 0.0f, hi = 1.0f;
	bool haveScalar = session.state.fieldIndex >= 0
		&& buildDisplayScalar(*session.dataset, session.state.fieldIndex, session.state.component, scalar, session.state.step);
	if (haveScalar)
	{
		// Automatic range over ALL steps (a fixed colour scale, so animation frames stay comparable), cached so
		// playback does not rescan every step per frame; otherwise the range of the step shown / the custom one.
		if (!session.state.customRange && session.state.allStepsRange && stepCount > 1
		    && cachedAllStepsRange(session, session.state.fieldIndex, session.state.component, lo, hi))
		{
			if (!(hi > lo))
				hi = lo + std::max(1.0e-6f, std::fabs(lo) * 1.0e-6f);
		}
		else
			haveScalar = resolveViewRange(scalar, session.state, lo, hi);
	}

	const bool comparing = _simulationCompareActive && _simulationCompareMeshes.contains(session.meshUuid);
	if (!haveScalar)
	{
		session.ownRangeValid = false;
		session.shownScalar = DisplayScalar();
		session.markers.clear();
		pushSimulationMarkers();
		mesh->clearAnalysisOverlay(); // CPU-only, no GL context needed
		updateSimulationGlyphs(session, false, 0.0f, 1.0f); // arrows do not need a scalar to colour the surface by
		if (isActive && _simulationLegend)
			_simulationLegend->setAliveCheck([]() { return false; });
		_viewportWidget->update();
		return;
	}

	// Compare mode with "same colour range": this result's own range widened to include its partner's, so equal colours
	// mean equal values. Only when both show the same unit (otherwise the numbers are not comparable).
	session.ownLo = lo;
	session.ownHi = hi;
	session.ownRangeValid = true;
	SimulationSession* partner = nullptr;
	if (comparing)
		for (const QUuid& id : std::as_const(_simulationCompareMeshes))
			if (id != session.meshUuid)
				partner = findSimulationSession(id);
	if (comparing && _simulationCompareSharedRange && partner && partner->ownRangeValid && partner->shownScalar.valid()
	    && partner->shownScalar.unit == scalar.unit)
	{
		lo = std::min(lo, partner->ownLo);
		hi = std::max(hi, partner->ownHi);
	}

	// Values on the surface: one per vertex for node data (interpolated smoothly), one per triangle for cell data
	// (constant over the cell, so drawn flat through the per-face overlay).
	const std::vector<float> surfaceValues = scalar.cellData ? boundaryFaceValues(*session.surface, scalar.nodeValues)
	                                                         : boundaryVertexValues(*session.surface, scalar.nodeValues);
	std::vector<bool> valid(surfaceValues.size());
	for (std::size_t i = 0; i < surfaceValues.size(); ++i)
		valid[i] = std::isfinite(surfaceValues[i]);

	_viewportWidget->makeCurrent();
	const std::vector<float> encoded = AnalysisColorRamp::mapToNormalizedScalarRGBA(surfaceValues, valid, lo, hi);
	if (scalar.cellData)
		mesh->setAnalysisOverlayFlatColors(encoded);
	else
		mesh->setAnalysisOverlayColors(encoded);
	mesh->setAnalysisOverlayBanding(simulationShaderBands(session.state), session.state.colormap);
	_viewportWidget->doneCurrent();

	if ((isActive && !_simulationCompareActive) || comparing)
	{
		// Compare mode: one legend per compared result, in its own pane; otherwise the single legend of the active one.
		SimulationLegendWidget* legend = nullptr;
		if (comparing)
		{
			QPointer<SimulationLegendWidget>& slot = _compareLegends[session.meshUuid];
			if (!slot)
				slot = new SimulationLegendWidget(_viewportWidget);
			legend = slot;
			QPointer<ViewportWidget> paneGuard(_viewportWidget);
			const QUuid paneMesh = session.meshUuid;
			legend->setPane([paneGuard, paneMesh]() {
				return paneGuard ? paneGuard->comparePaneRect(paneGuard->comparePaneOfMesh(paneMesh)) : QRect();
			}, session.dataset->stepCount() > 1 ? tr("%1 - %2").arg(mesh->getName(), stepDescription(*session.dataset, session.state.step))
			                                     : mesh->getName());
		}
		else
		{
			if (!_simulationLegend)
				_simulationLegend = new SimulationLegendWidget(_viewportWidget);
			legend = _simulationLegend;
			legend->setPane({}, QString());
		}
		// Unit in brackets: the display unit, flagged when it is only a guess, or an honest "not specified".
		const QString unitText = scalar.unit.isEmpty() ? tr("unit not specified")
			: (scalar.unitAssumed ? tr("%1, assumed").arg(scalar.unit) : scalar.unit);
		legend->setLegend(tr("%1  [%2]").arg(scalar.label, unitText), lo, hi, session.state.colormap,
		                             session.state.bands,
		                             tr("%1\n%2").arg(QDir::toNativeSeparators(session.filePath), session.warnings.join(QLatin1Char('\n'))));
		// Shown only while this result's mesh is still displayed (it disappears with Undo, returns with Redo).
		QPointer<ViewportWidget> viewportGuard(_viewportWidget);
		QPointer<ModelViewer> self(this);
		const QUuid meshUuid = session.meshUuid;
		legend->setAliveCheck([viewportGuard, self, meshUuid]() {
			return viewportGuard && viewportGuard->getMeshByUuid(meshUuid) && self && self->_visibleMeshUuids.contains(meshUuid);
		});
	}
	// ---- Min/max markers of this result. Each result keeps its own; pushSimulationMarkers() shows the active one's, or in
	// compare mode both compared results' (each in its own pane).
	{
		std::vector<SimulationMarker> markers;
		std::size_t minVertex = 0, maxVertex = 0;
		if (session.state.markExtrema && findScalarExtrema(surfaceValues, minVertex, maxVertex))
		{
			const std::vector<Vertex> current = mesh->vertices(); // once, for both markers' normals
			// For cell data the extreme is a triangle: the marker goes on that triangle's first vertex.
			const auto makeMarker = [&](std::size_t index, const QString& name, float normalized) {
				const std::size_t vertex = scalar.cellData ? session.surface->triangles[index * 3] : index;
				SimulationMarker marker;
				marker.vertex = static_cast<int>(vertex);
				if (vertex < current.size())
				{
					marker.normal[0] = current[vertex].Normal.x;
					marker.normal[1] = current[vertex].Normal.y;
					marker.normal[2] = current[vertex].Normal.z;
				}
				marker.text = QStringLiteral("%1 %2").arg(name, QLocale().toString(static_cast<double>(surfaceValues[index]), 'g', 5));
				if (!scalar.unit.isEmpty())
					marker.text += QLatin1Char(' ') + scalar.unit;
				const QColor painted = AnalysisColorRamp::colorForNormalized(normalized, static_cast<AnalysisColormap>(session.state.colormap));
				marker.lightText = painted.lightness() < 128;
				return marker;
			};
			const auto normalize = [&](float v) { return hi > lo ? std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f; };
			markers.push_back(makeMarker(minVertex, tr("Min"), normalize(surfaceValues[minVertex])));
			if (maxVertex != minVertex)
				markers.push_back(makeMarker(maxVertex, tr("Max"), normalize(surfaceValues[maxVertex])));
		}
		session.markers = std::move(markers);
		pushSimulationMarkers();
	}
	updateSimulationGlyphs(session, true, lo, hi);
	session.shownLo = lo;
	session.shownHi = hi;
	session.shownScalar = std::move(scalar); // last use of `scalar`: the hover probe reads it
	_viewportWidget->update();

	// A shared colour range depends on both results: when this one changed, its partner recolours with the new union.
	if (comparing && _simulationCompareSharedRange && partner && !_refreshingComparePartner)
	{
		_refreshingComparePartner = true;
		refreshSimulationDisplay(*partner);
		_refreshingComparePartner = false;
	}
}

void ModelViewer::updateSimulationGlyphs(SimulationSession& session, bool haveSurfaceRange, float surfaceLo, float surfaceHi)
{
	session.glyphInfo.clear();
	if (!_viewportWidget || !session.dataset || !session.surface)
		return;
	const ResultDataset& dataset = *session.dataset;
	const SimulationViewState& state = session.state;
	int fieldIndex = state.glyphField;
	if (fieldIndex < 0 || static_cast<std::size_t>(fieldIndex) >= dataset.fields.size()
	    || !isGlyphField(dataset.fields[static_cast<std::size_t>(fieldIndex)]))
		fieldIndex = chooseDefaultGlyphField(dataset);
	if (!state.glyphs || fieldIndex < 0)
	{
		_viewportWidget->clearSimulationGlyphs(session.meshUuid);
		return;
	}
	const ResultField& field = dataset.fields[static_cast<std::size_t>(fieldIndex)];
	const bool cellField = field.association == ResultFieldAssociation::Cell;
	const std::size_t wanted = static_cast<std::size_t>(std::clamp(state.glyphCount, 20, 50000));

	// The sampled sites are kept between refreshes: they depend only on the field's association and the arrow count.
	if (session.glyphSites.empty() || session.glyphSitesCell != cellField || session.glyphSitesCount != static_cast<int>(wanted))
	{
		session.glyphSites = selectSurfaceGlyphSites(*session.surface, cellField, wanted);
		session.glyphSitesCell = cellField;
		session.glyphSitesCount = static_cast<int>(wanted);
		session.glyphSitesField = fieldIndex;
	}
	if (session.surfaceDiagonal < 0.0)
		session.surfaceDiagonal = surfaceDiagonal(*session.surface);

	// The largest magnitude over all steps sets the arrow length and the colour range, so animation frames stay comparable.
	float referenceMax = 0.0f, ownLo = 0.0f, ownHi = 0.0f;
	bool haveAllSteps = false;
	if (dataset.stepCount() > 1 && cachedAllStepsRange(dataset, session.glyphRangeCache, fieldIndex, -1, ownLo, ownHi))
	{
		referenceMax = ownHi;
		haveAllSteps = true;
	}
	GlyphOptions options;
	options.target = wanted;
	options.scale = state.glyphScale;
	options.scaleByMagnitude = state.glyphScaleByMagnitude;
	GlyphSet set;
	if (!buildGlyphSet(dataset, *session.surface, fieldIndex, state.step, session.glyphSites, session.surfaceDiagonal, options, referenceMax, set))
	{
		_viewportWidget->clearSimulationGlyphs(session.meshUuid);
		session.glyphInfo = tr("No arrows at this step: '%1' has no vector data here.").arg(field.name);
		return;
	}
	if (!haveAllSteps)
	{
		ownLo = set.fieldMin;
		ownHi = set.fieldMax;
	}

	// The arrows show the surface's own field: use its colour range, so equal colours mean equal values in the legend too.
	const bool likeSurface = haveSurfaceRange && state.fieldIndex == fieldIndex && state.component == -1;
	float lo = likeSurface ? surfaceLo : ownLo, hi = likeSurface ? surfaceHi : ownHi;
	if (!(hi > lo))
		hi = lo + std::max(1.0e-6f, std::fabs(lo) * 1.0e-6f);
	const AnalysisColormap colormap = static_cast<AnalysisColormap>(state.colormap);
	set.colors.reserve(set.count() * 3);
	for (float value : set.values)
	{
		const QColor c = AnalysisColorRamp::colorForNormalized(std::clamp((value - lo) / (hi - lo), 0.0f, 1.0f), colormap);
		set.colors.insert(set.colors.end(), { static_cast<float>(c.redF()), static_cast<float>(c.greenF()), static_cast<float>(c.blueF()) });
	}
	if (likeSurface)
		session.glyphInfo = tr("Arrows: %1, coloured as in the legend.").arg(field.name);
	else
		session.glyphInfo = tr("Arrows: %1, coloured by magnitude from %2 to %3%4.")
			.arg(field.name).arg(lo, 0, 'g', 4).arg(hi, 0, 'g', 4).arg(set.unit.isEmpty() ? QString() : QStringLiteral(" ") + set.unit);
	_viewportWidget->setSimulationGlyphs(session.meshUuid, std::move(set));
}
