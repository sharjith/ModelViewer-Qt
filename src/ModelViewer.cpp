#include "FloatingPanelDialog.h"
#include "AddMeasurementCommand.h"
#include "AddAnnotationCommand.h"
#include "MeasurementVisibilityCommand.h"
#include "AnnotationVisibilityCommand.h"
#include "AssImpModelLoader.h"
#include "CaptureCameraCommand.h"
#include "CaptureVariantCommand.h"
#include "DeleteMeasurementCommand.h"
#include "DeleteAnnotationCommand.h"
#include "AnnotationTextCommand.h"
#include "SetDefaultVariantCommand.h"
#include "CutCommand.h"
#include "DeleteMeshCommand.h"
#include "MetadataDeleteCommand.h"
#include "DuplicateCommand.h"
#include "SplitByConnectivityCommand.h"
#include "MergeByAdjacencyCommand.h"
#include "GroupMeshesCommand.h"
#include "ShrinkWrapCommand.h"
#include "ExplodedViewPanel.h"
#include "PasteCommand.h"
#include "RenameMeshCommand.h"
#include "ViewportWidget.h"
#include "MeasurementDialog.h"
#include "AnnotationDialog.h"
#include "ShrinkWrapDialog.h"
#include "SurfaceAnalysisDialog.h"
#include "MassPropertiesDialog.h"
#include "ReportExportDialog.h"
#include "BatchRenderViewsDialog.h"
#include "SubdivisionDialog.h"
#include "ReconstructSurfaceDialog.h"
#include "RepairMeshDialog.h"
#include "FillHolesDialog.h"
#include "LanguageManager.h"
#include "MainWindow.h"
#include "MaterialPreviewWidget.h"
#include "MeshProperties.h"
#include "ModelViewer.h"
#include "LengthUnits.h"
#include "ImportUnitsDialog.h"
#include "ModelViewerApplication.h"
#include "MvfDocument.h"
#include "MvfFormat.h"
#include "MvfMeshPreparationWorker.h"
#include "MvfSceneBuilder.h"
#include "SceneTreeWidget.h"
#include "ObjectTransformPanel.h"
#include "MaterialPropertiesPanel.h"
#include "MaterialLibraryWidget.h"
#include "PathUtils.h"
#include "SelectionCommand.h"
#include "TransformCommand.h"
#include "RenderableMesh.h"
#include "VisibilityCommand.h"
#include "MaterialGrouping.h"
#include "MeshColorUtils.h"
#include "FilterByMaterialDialog.h"
#include "FilterByColorDialog.h"
#include "FilterByBoundingBoxDialog.h"
#include "SaveSelectionSetCommand.h"
#include "DeleteSelectionSetCommand.h"
#include "SaveSceneStateCommand.h"
#include "DeleteSceneStateCommand.h"
#include <assimp/Importer.hpp>
#include <algorithm>
#include <functional>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QDataStream>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QLineEdit>
#include <QMdiSubWindow>
#include <QMenu>
#include <QAbstractButton>
#include <QMessageBox>
#include <QPushButton>
#include <QPainter>
#include <QProxyStyle>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QStyleOptionButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTabBar>
#include <QTabWidget>
#include <QScrollArea>
#include <QtMath>
#include <cmath>
#include <limits>

QString ModelViewer::_lastOpenedDir;
QString ModelViewer::_lastSelectedFilter;
QList<ClipboardEntry> ModelViewer::s_clipboard;
QPointer<ModelViewer> ModelViewer::s_clipboardSourceViewer;
quint64                ModelViewer::s_clipboardGeneration = 0;

namespace
{
QMatrix4x4 buildWorldRotationDeltaMatrix(const QVector3D& rotation)
{
	QMatrix4x4 matrix;
	matrix.setToIdentity();
	matrix.rotate(rotation.x(), QVector3D(1.0f, 0.0f, 0.0f));
	matrix.rotate(rotation.y(), QVector3D(0.0f, 1.0f, 0.0f));
	matrix.rotate(rotation.z(), QVector3D(0.0f, 0.0f, 1.0f));
	return matrix;
}

QVector3D canonicalizeEulerFromRotationMatrix(const QMatrix4x4& rotationOnly)
{
	auto normalizeDegrees180 = [](float degrees) {
		float normalized = std::fmod(degrees + 180.0f, 360.0f);
		if (normalized < 0.0f)
			normalized += 360.0f;
		normalized -= 180.0f;
		if (std::abs(normalized) < 1.0e-4f)
			return 0.0f;
		if (std::abs(normalized - 180.0f) < 1.0e-4f || std::abs(normalized + 180.0f) < 1.0e-4f)
			return 180.0f;
		return normalized;
	};
	auto canonicalizeEuler = [&](const QVector3D& euler) {
		const QVector3D primary(
			normalizeDegrees180(euler.x()),
			normalizeDegrees180(euler.y()),
			normalizeDegrees180(euler.z()));
		const QVector3D alternate(
			normalizeDegrees180(euler.x() + 180.0f),
			normalizeDegrees180(180.0f - euler.y()),
			normalizeDegrees180(euler.z() + 180.0f));
		const float primaryScore = std::abs(primary.x()) + std::abs(primary.y()) + std::abs(primary.z());
		const float alternateScore = std::abs(alternate.x()) + std::abs(alternate.y()) + std::abs(alternate.z());
		return (alternateScore + 1.0e-4f < primaryScore) ? alternate : primary;
	};

	const float m00 = rotationOnly(0, 0);
	const float m01 = rotationOnly(0, 1);
	const float m02 = rotationOnly(0, 2);
	const float m10 = rotationOnly(1, 0);
	const float m11 = rotationOnly(1, 1);
	const float m12 = rotationOnly(1, 2);
	const float m22 = rotationOnly(2, 2);
	const float yRadians = std::asin(std::clamp(m02, -1.0f, 1.0f));
	const float cosY = std::cos(yRadians);

	float xRadians = 0.0f;
	float zRadians = 0.0f;
	if (std::abs(cosY) > 1.0e-6f)
	{
		xRadians = std::atan2(-m12, m22);
		zRadians = std::atan2(-m01, m00);
	}
	else
	{
		xRadians = std::atan2(m10, m11);
		zRadians = 0.0f;
	}

	return canonicalizeEuler(QVector3D(
		qRadiansToDegrees(xRadians),
		qRadiansToDegrees(yRadians),
		qRadiansToDegrees(zRadians)));
}

QVector3D computeSelectionCog(ViewportWidget* widget, const std::vector<int>& ids)
{
	QVector3D center(0.0f, 0.0f, 0.0f);
	int count = 0;
	for (int id : ids)
	{
		SceneMesh* mesh = widget ? widget->getMeshByIndex(id) : nullptr;
		if (!mesh)
			continue;

		center += mesh->getStableTransformCenter();
		++count;
	}

	if (count > 0)
		center /= static_cast<float>(count);

	return center;
}
}

ModelViewer::ModelViewer(QWidget* parent) : QWidget(parent)
{
	setAttribute(Qt::WA_DeleteOnClose);

	_documentSaved = false;
	setDocumentModified(false);
	_runningFirstTime = true;

	_textureDirOpenedFirstTime = true;

	setupUi(this);

	// Scene graph — owns the node hierarchy mirroring the loaded aiScene tree.
	_sceneGraph = new SceneGraph(this);

	// Initialize undo stack
	_undoStack = new QUndoStack(this);
	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
	int maxUndo = settings.value("spinBoxUndoLimit", 50).toInt(); // Keep last 50 operations as default
	_undoStack->setUndoLimit(maxUndo);

	// Seed the default HDRI/LDRI skybox indices from the configured Settings presets
	// (if any). Presets are matched by folder name rather than index, since the
	// scanned folder list (and therefore index order) can change if presets are
	// added/removed.
	{
		const QString envmapRoot = PathUtils::getDataDirectory() + "/textures/envmap/skyboxes";

		const QString hdriPresetName = settings.value("comboBoxDefaultSkyboxHDRI", QString()).toString();
		if (!hdriPresetName.isEmpty())
		{
			QDir hdriDir(envmapRoot + "/HDRI");
			const int idx = hdriDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).indexOf(hdriPresetName);
			if (idx >= 0)
				_skyBoxHDRIIndex = idx;
		}

		const QString ldriPresetName = settings.value("comboBoxDefaultSkyboxLDRI", QString()).toString();
		if (!ldriPresetName.isEmpty())
		{
			QDir ldriDir(envmapRoot + "/LDRI");
			const int idx = ldriDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).indexOf(ldriPresetName);
			if (idx >= 0)
				_skyBoxLDRIIndex = idx;
		}
	}

	// Detect when undo becomes unavailable
	connect(_undoStack, &QUndoStack::canUndoChanged,
		this, [this](bool canUndo) {
			if (_lastCanUndo && !canUndo)  // Transition: true -> false
			{
				MainWindow::showStatusMessage("Nothing to undo", 2000);
			}
			_lastCanUndo = canUndo;
		});

	// Detect when redo becomes unavailable  
	connect(_undoStack, &QUndoStack::canRedoChanged,
		this, [this](bool canRedo) {
			if (_lastCanRedo && !canRedo)  // Transition: true -> false
			{
				MainWindow::showStatusMessage("Nothing to redo", 2000);
			}
			_lastCanRedo = canRedo;
		});

	setupUndoStackMonitoring();

	setAttribute(Qt::WA_DeleteOnClose);
		
	int values[] = { 0, 2, 4, 8, 16, 32 };
	int samples = values[settings.value("msaaComboBox", 4).toInt()];
	bool vsyncEnabled = settings.value("vsyncCheckBox", true).toBool();

	QSurfaceFormat format;
	format.setVersion(4, 5); // OpenGL version 4.5
	format.setProfile(QSurfaceFormat::CoreProfile);
	format.setDepthBufferSize(24);
	format.setStencilBufferSize(8);
	format.setAlphaBufferSize(0); // see ModelViewerApplication.cpp for why (Wayland compositor bleed-through)
	format.setSwapInterval(vsyncEnabled ? 1 : 0);
	format.setStereo(true);
	format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
	format.setRenderableType(QSurfaceFormat::OpenGL);
	format.setSamples(samples); // Set MSAA samples
	_viewportWidget = new ViewportWidget(this, "viewportWidget");
    connect(_viewportWidget, &ViewportWidget::toolCommandRequested, this, &ModelViewer::executeToolCommand);
    connect(_viewportWidget, &ViewportWidget::selectionChanged, this, &ModelViewer::updateMeshTools, Qt::QueuedConnection);
    connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, &ModelViewer::updateMeshTools, Qt::QueuedConnection);
	_viewportWidget->setAttribute(Qt::WA_DeleteOnClose);
	_viewportWidget->setFormat(format);
	_viewportWidget->setMouseTracking(true);
	// Put the GL widget inside the frame
	QVBoxLayout* flayout = new QVBoxLayout(glFrame);
	flayout->setContentsMargins(0, 0, 0, 0);
	flayout->addWidget(_viewportWidget, 1);

	connect(_viewportWidget, &ViewportWidget::singleSelectionDone, this, &ModelViewer::setListRow);
	connect(_viewportWidget, &ViewportWidget::sweepSelectionDone, this, &ModelViewer::setListRows);
	connect(_viewportWidget, &ViewportWidget::eyedropperMaterialSampled, this, &ModelViewer::onEyedropperMaterialSampled);
	connect(_viewportWidget, &ViewportWidget::eyedropperStrokeFinished, this, &ModelViewer::applyEyedropperStroke);
	connect(_viewportWidget, &ViewportWidget::zoomAndPanSet, this, [this]() {
		if (_treeRebuildPending)
			rebuildTreeFromCurrentState();
	});

	treeWidgetModel->setSceneGraph(_sceneGraph);
	treeWidgetModel->setViewportWidget(_viewportWidget);

	// SceneTreeWidget::rebuild() only enqueues work and starts a batching
	// timer (see processRebuildBatch()) - topLevelItemCount() is still 0
	// (or stale) immediately after calling it, so the search box/label
	// visibility toggle has to wait for the tree to actually finish
	// populating, not run right after rebuild() returns.
	connect(treeWidgetModel, &SceneTreeWidget::rebuildComplete, this, [this]() {
		const bool hasItems = treeWidgetModel->topLevelItemCount() > 0;
		label_23->setVisible(hasItems);
		searchBox->setVisible(hasItems);
	});

	// rebuild() only enqueues work and starts a batching timer - the actual
	// selection restore happens later, in finalizeRebuild(), entirely under
	// blockSignals(true) (so selectionUpdated/handleTreeWidgetSelectionChanged
	// never fire for it - only rebuildComplete() does, once signals are
	// unblocked again). Calling updateMeshTools() synchronously right after
	// treeWidgetModel->rebuild() returns (as rebuildTreeFromCurrentState()
	// used to) would see the tree still empty/mid-reconstruction and disable
	// every mesh tool regardless of the selection about to be restored.
	connect(treeWidgetModel, &SceneTreeWidget::rebuildComplete, this, &ModelViewer::updateMeshTools);

	// Exploded View Panel — created inside ViewportWidget; wire SceneGraph + selection clearing here.
	{
		ExplodedViewPanel* evPanel = _viewportWidget->getExplodedViewPanel();
		evPanel->setSceneGraph(_sceneGraph);
		connect(evPanel, &ExplodedViewPanel::selectionClearRequested,
		        this,    &ModelViewer::deselectAll);
	}

	// Texture Debug Panel — created once per viewer, shown on demand via
	// Visualization → Texture Debugger (visible only when the setting is enabled).
	{
		_textureDebugPanel = new TextureDebugPanel(this);
		_textureDebugPanel->setViewportWidget(_viewportWidget);
		_textureDebugPanel->setModelViewer(this);

		connect(_viewportWidget,          &ViewportWidget::selectionChanged,
		        _textureDebugPanel, &TextureDebugPanel::onSelectionChanged);
		connect(_viewportWidget,          &ViewportWidget::textureReadbackReady,
		        _textureDebugPanel, &TextureDebugPanel::onTextureReadbackReady);
		connect(_textureDebugPanel, &TextureDebugPanel::requestPBRMode,
		        this, [this]() { onRenderingModeSelected("PBR"); });
	}

	connect(_sceneGraph, &SceneGraph::structureChanged,
	        this, &ModelViewer::validateCutClipboard);
	connect(_sceneGraph, &SceneGraph::structureChanged,
	        this, &ModelViewer::validateVariantData);
	connect(_sceneGraph, &SceneGraph::structureChanged,
	        this, &ModelViewer::validateAnimationData);
	connect(_sceneGraph, &SceneGraph::structureChanged,
	        this, &ModelViewer::validateCameraData);
	connect(_sceneGraph, &SceneGraph::structureChanged,
	        this, &ModelViewer::validateLightData);
	treeWidgetModel->installEventFilter(this);
	treeWidgetModel->viewport()->installEventFilter(this);

	treeWidgetModel->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(treeWidgetModel, &SceneTreeWidget::customContextMenuRequested, this, &ModelViewer::showContextMenu);

	// Rename via tree widget's internal delegate handling
	connect(treeWidgetModel, &SceneTreeWidget::meshRenamed,
	        this, &ModelViewer::handleTreeWidgetMeshRenamed);

	QTimer* searchTimer = new QTimer(this);
	searchTimer->setSingleShot(true);
	searchTimer->setInterval(500);

	connect(searchBox, &QLineEdit::textEdited, this, [searchTimer](const QString&) {
		searchTimer->start();
		});

	connect(searchTimer, &QTimer::timeout, this, [this]() {
		treeWidgetModel->filterItems(searchBox->text());

		// Visual feedback if no match
		bool anySelected = treeWidgetModel->hasMeshSelection();
		searchBox->setStyleSheet((anySelected || searchBox->text().isEmpty()) ? "" : "QLineEdit { border: 2px solid red; }");
		});

	connect(treeWidgetModel, &SceneTreeWidget::selectionUpdated,
	        this, &ModelViewer::handleTreeWidgetSelectionChanged);

	connect(treeWidgetModel, &SceneTreeWidget::meshVisibilityChanged,
	        this, &ModelViewer::handleTreeWidgetVisibilityChanged);

	// Delete/Ctrl+Shift+I/E/A/P/R used to be registered here as one QShortcut
	// per document. Even after scoping them to
	// Qt::WidgetWithChildrenShortcut, multiple simultaneously-open documents
	// still reported them as ambiguous (unlike the ViewToolbar Home
	// shortcut's parent/child inversion, which that context fix genuinely
	// resolved). Moved to single MainWindow-owned instances dispatching via
	// activeMdiChild() instead - see MainWindow's constructor - which
	// sidesteps the whole class of bug by construction: with only one
	// instance total, there is nothing for it to be ambiguous with
	// regardless of how many documents are open or how they're arranged.

	// predefinedMaterialsPanel/objectTransformPanel/tabWidgetVizAttribs/
	// visualizationEnvironmentPanel are MainWindow's single shared instances
	// now (used to be built per-document by Ui_ModelViewer::setupUi()) -
	// their signal wiring and rebind-to-this-document calls live in
	// MainWindow's constructor/rebindSharedPanelsTo() instead of here.
	predefinedMaterialsPanel = MainWindow::mainWindow()->materialPropertiesPanel();
	objectTransformPanel = MainWindow::mainWindow()->objectTransformPanel();
	tabWidgetVizAttribs = MainWindow::mainWindow()->propertiesTabWidget();
	visualizationEnvironmentPanel = MainWindow::mainWindow()->visualizationEnvironmentPanel();

	// Connect ViewToolbar rendering mode selection
	connect(_viewportWidget->getViewToolbar(), &ViewToolbar::renderingModeSelected,
		this, &ModelViewer::onRenderingModeSelected);

	// Connect ViewToolbar navigation selection
	connect(_viewportWidget->getViewToolbar(), &ViewToolbar::rotateViewRequested,
		_viewportWidget, [this]() { _viewportWidget->setRotationActive(true); });

	connect(_viewportWidget->getViewToolbar(), &ViewToolbar::panViewRequested,
		_viewportWidget, [this]() { _viewportWidget->setPanningActive(true); });

	connect(_viewportWidget->getViewToolbar(), &ViewToolbar::zoomViewRequested,
		_viewportWidget, [this]() { _viewportWidget->setZoomingActive(true); });

	modelNavigationWidget->setProperty("transparentOverlaySurface", true);
	label_23->setProperty("transparentOverlayText", true);
	labelMeshCount->setProperty("transparentOverlayText", true);
	attachNavigationOverlay();

	_hasPBRAlbedoTex = false;	
	_hasPBRMetallicTex = false;
	_hasPBRRoughnessTex = false;
	_hasPBRAOTex = false;
	_hasPBROpacTex = false;
	_hasPBRNormalTex = false;
	_hasPBRHeightTex = false;
	_heightPBRTexScale = 0.02f;

	_progressiveLoadingEnabled = QSettings(QCoreApplication::organizationName(), QCoreApplication::applicationName()).value("checkProgressiveLoading", true).toBool();
	_animateProgressiveFitEnabled = QSettings(QCoreApplication::organizationName(), QCoreApplication::applicationName()).value("checkAnimateProgressiveFit", true).toBool();

	updateControls();

	connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
		retranslateUi(this);
		retranslateUI();  // if needed
		});
}

ModelViewer::~ModelViewer()
{
	if (_undoStack)
	{
		disconnect(_undoStack, nullptr, nullptr, nullptr);  // Prevent callbacks
		_undoStack->clear();
	}
	if (_viewportWidget)
	{
		delete _viewportWidget;
	}
}

void ModelViewer::retranslateUI()
{
	// Dynamically created	
}

void ModelViewer::close()
{
	// QMdiSubWindow's documented auto-close cascade only reliably runs
	// top-down (closing the subwindow closes its embedded widget) - closing
	// the embedded widget directly does not reliably tear down the
	// QMdiSubWindow/tab itself, leaving an empty tab behind. Redirecting to
	// the subwindow's own close() fixes that at the source.
	if (QMdiSubWindow* subWin = qobject_cast<QMdiSubWindow*>(parentWidget()))
	{
		subWin->close(); // Closes the MDI tab/subwindow container
	}
	else
	{
		// Not yet parented to a QMdiSubWindow (or no longer is) - fall back
		// to the real QWidget::close(). Plain this->close() here would
		// recurse into THIS function forever: ModelViewer::close() isn't
		// virtual and isn't overriding anything (QWidget::close() isn't
		// virtual either), so declaring it just hides the base class name -
		// this->close() would resolve back to ModelViewer::close(), not
		// QWidget::close(), without the explicit qualification below.
		QWidget::close();
	}
}

void ModelViewer::deselectAll()
{
	treeWidgetModel->clearMeshSelection();
	resetTransformationValues();
	handleTreeWidgetSelectionChanged();
}

void ModelViewer::deselectAllWithUndo()
{
	// Only push a command if there is something to deselect, so that
	// pressing Esc on an already-empty selection does not pollute the undo stack.
	if (hasSelection())
		setSelectionWithUndo(QSet<int>{});
}

void ModelViewer::setListRow(int index)
{
	if (index == -1)
	{
		// Viewport empty-space click (or toggle-deselect): clear selection with undo.
		// Guard against empty undo entries when nothing is selected.
		if (hasSelection())
			setSelectionWithUndo(QSet<int>{});
		return;
	}

	std::vector<SceneMesh*> meshes = _viewportWidget->getMeshStore();
	SceneMesh* mesh = meshes.at(index);

	// Build new selection set
	QSet<int> newSelection;
	std::vector<int> currentIDs = getSelectedIDs();
	newSelection = QSet<int>(currentIDs.begin(), currentIDs.end());

	if (mesh->isSelected())
	{
		// Toggle off
		newSelection.remove(index);
	}
	else
	{		
		newSelection.insert(index);
	}

	// Apply selection with undo support
	setSelectionWithUndo(newSelection);

	// Update transformation panel if needed
	if (tabWidgetVizAttribs->currentIndex() == 1)
	{
		if (newSelection.size() == 1)
			updateTransformationValues();
		else
			resetTransformationValues();
	}
}

void ModelViewer::setListRows(QList<int> indices)
{
	// Build selection set from indices
	QSet<int> newSelection;
	for (int index : indices)
		newSelection.insert(index);

	// An empty result is meaningful for Replace and Subtract gestures: it
	// clears the previous selection. Avoid pushing an undo command only when
	// the authoritative tree selection already matches the gesture result.
	const std::vector<int> currentIds = getSelectedIDs();
	const QSet<int> currentSelection(currentIds.cbegin(), currentIds.cend());
	if (newSelection == currentSelection)
		return;

	// Apply selection with undo support
	setSelectionWithUndo(newSelection);
}

void ModelViewer::setTransformation()
{
	if (!checkForActiveSelection())
		return;

	QApplication::setOverrideCursor(Qt::WaitCursor);

	// Get transformation values from panel
	QVector3D translate = objectTransformPanel->getTranslation();
	QVector3D rotate = objectTransformPanel->getRotation();
	QVector3D scale = objectTransformPanel->getScale();

	// Get UUIDs of selected meshes
	QVector<QUuid> uuids;
	std::vector<int> ids = getSelectedIDs();
	for (int id : ids)
	{
		QUuid uuid = _viewportWidget->getUuidByIndex(id);
		if (!uuid.isNull())
			uuids.append(uuid);
	}

	if (ids.size() > 1)
	{
		QMap<QUuid, TransformState> oldStates;
		QMap<QUuid, TransformState> newStates;
		const QVector3D pivot = computeSelectionCog(_viewportWidget, ids);
		const QMatrix4x4 rotationDelta = buildWorldRotationDeltaMatrix(rotate);
		const bool hasTranslationDelta = translate.lengthSquared() > 1.0e-8f;
		const bool hasRotationDelta = rotate.lengthSquared() > 1.0e-8f;
		const bool hasScaleDelta =
			std::abs(scale.x() - 1.0f) > 1.0e-8f ||
			std::abs(scale.y() - 1.0f) > 1.0e-8f ||
			std::abs(scale.z() - 1.0f) > 1.0e-8f;

		for (int id : ids)
		{
			SceneMesh* mesh = _viewportWidget->getMeshByIndex(id);
			if (!mesh)
				continue;

			const QUuid uuid = _viewportWidget->getUuidByIndex(id);
			if (uuid.isNull())
				continue;

			const QVector3D startTranslation = mesh->getTranslation();
			const QVector3D startRotation = mesh->getRotation();
			const QVector3D startScale = mesh->getScaling();
			const QQuaternion startQuat = mesh->getRotationQuaternion();
			oldStates.insert(uuid, TransformState(startTranslation, startRotation, startScale, startQuat));

			QVector3D newTranslation = startTranslation;
			QVector3D newScale = QVector3D(
				startScale.x() * scale.x(),
				startScale.y() * scale.y(),
				startScale.z() * scale.z());
			QQuaternion newQuat = startQuat;
			QVector3D displayRotation = startRotation;

			QVector3D offset = startTranslation - pivot;
			if (hasScaleDelta)
			{
				offset = QVector3D(offset.x() * scale.x(), offset.y() * scale.y(), offset.z() * scale.z());
			}
			if (hasRotationDelta)
			{
				const QQuaternion deltaQuat =
					QQuaternion::fromRotationMatrix(rotationDelta.toGenericMatrix<3, 3>()).normalized();
				offset = rotationDelta.map(offset);
				newQuat = (deltaQuat * startQuat).normalized();
				QMatrix4x4 displayRotationMatrix;
				displayRotationMatrix.setToIdentity();
				displayRotationMatrix.rotate(newQuat);
				displayRotation = canonicalizeEulerFromRotationMatrix(displayRotationMatrix);
			}
			newTranslation = pivot + offset + translate;

			newStates.insert(uuid, TransformState(newTranslation, displayRotation, newScale, newQuat));
		}

		if (!oldStates.isEmpty() && (hasTranslationDelta || hasRotationDelta || hasScaleDelta))
		{
			_undoStack->push(new TransformCommand(
				this, _viewportWidget, oldStates, newStates, tr("Transform Selection"), false));
		}

		objectTransformPanel->setTranslationValues(QVector3D(0.0f, 0.0f, 0.0f));
		objectTransformPanel->setRotationValues(QVector3D(0.0f, 0.0f, 0.0f));
		objectTransformPanel->setScaleValues(QVector3D(1.0f, 1.0f, 1.0f));
		_viewportWidget->update();
		QApplication::restoreOverrideCursor();
		return;
	}

	// Create and push transform command
	// redo() will be called automatically and will apply the transformation
	_undoStack->push(new TransformCommand(
		this, _viewportWidget, uuids, translate, rotate, scale
	));

	// Update UI (transformation already applied by command's redo())
	_viewportWidget->update();

	QApplication::restoreOverrideCursor();
}

void ModelViewer::resetTransformation()
{
	if (!checkForActiveSelection())
		return;

	QApplication::setOverrideCursor(Qt::WaitCursor);

	// Get UUIDs of selected meshes
	QVector<QUuid> uuids;
	std::vector<int> ids = getSelectedIDs();
	for (int id : ids)
	{
		QUuid uuid = _viewportWidget->getUuidByIndex(id);
		if (!uuid.isNull())
			uuids.append(uuid);
	}

	// Reset is transformation to identity values
	QVector3D identity_trans(0, 0, 0);
	QVector3D identity_rot(0, 0, 0);
	QVector3D identity_scale(1, 1, 1);

	// Create and push transform command with identity values
	_undoStack->push(new TransformCommand(
		this, _viewportWidget, uuids,
		identity_trans, identity_rot, identity_scale,
		tr("Reset Transform")  // Different text for reset
	));

	// Reset panel values
	objectTransformPanel->resetAllValues();

	// Update UI
	_viewportWidget->update();

	QApplication::restoreOverrideCursor();
}

void ModelViewer::syncLightPositionUiToScene()
{
	if (!_viewportWidget || !visualizationEnvironmentPanel)
		return;

	float range = _viewportWidget->getBoundingSphere().getRadius() * 4.0f;
	float offset = _viewportWidget->getFloorSize() * 1.25f;
	visualizationEnvironmentPanel->updateLightPositionRanges(range, offset);
}

void ModelViewer::updateTransformationValues()
{
	try
	{
		QList<QUuid> selected = treeWidgetModel->selectedMeshUuids();
		if (!selected.isEmpty())
		{
			if (selected.size() > 1)
			{
				objectTransformPanel->setTranslationValues(QVector3D(0.0f, 0.0f, 0.0f));
				objectTransformPanel->setRotationValues(QVector3D(0.0f, 0.0f, 0.0f));
				objectTransformPanel->setScaleValues(QVector3D(1.0f, 1.0f, 1.0f));
				return;
			}

			SceneMesh* mesh = _viewportWidget->getMeshByUuid(selected.at(0));
			if (mesh)
			{
				QVector3D trans = mesh->getTranslation();
				QVector3D rot = mesh->getRotation();
				QVector3D scale = mesh->getScaling();
				objectTransformPanel->setTranslationValues(trans);
				objectTransformPanel->setRotationValues(rot);
				objectTransformPanel->setScaleValues(scale);
			}
		}
	}
	catch (const std::exception& ex)
	{
		std::cout << "Exception raised in ModelViewer::on_tabWidgetVizAttribs_currentChanged\n" << ex.what() << std::endl;
	}
}

void ModelViewer::resetTransformationValues()
{
	objectTransformPanel->resetAllValues();
}

void ModelViewer::updateControls()
{
	visualizationEnvironmentPanel->updateButtonStyles();
	// ADS Lighting mode (computed from PBR properties)
}

QString ModelViewer::getSupportedQtImagesFilter()
{
	QList<QByteArray> supportedFormats = QImageReader::supportedImageFormats();
	QList<QString> filters;
	QString filter("All Supported Images (");
	for (const QByteArray& ba : supportedFormats)
	{
		filter += QString("*.%1 ").arg(QString(ba));
		filters.push_back(QString("*.%1").arg(QString(ba)));
	}
	filter += ")";
	for (const QString& fil : filters)
	{
		filter += ";;" + fil;
	}
	return filter;
}

void ModelViewer::attachNavigationOverlay()
{
	// Permanent now, not a toggle - the docked/detached choice and its
	// tab-removal/reattach-button bookkeeping are gone. Called once from the
	// constructor; the navigation tree lives as a transparent CAD-style
	// overlay glued to this document's own viewport for its entire lifetime.
	if (!modelNavigationWidget || _navigationOverlay)
		return;

	modelNavigationWidget->setAttribute(Qt::WA_NoSystemBackground, true);
	modelNavigationWidget->setAutoFillBackground(false);
	modelNavigationWidget->setProperty("detachedOverlayMode", true);
	treeWidgetModel->setDetachedOverlayMode(true);

	// A freshly created document always starts with an empty tree (loadFile()
	// runs after this, not before) - hidden until the SceneTreeWidget::
	// rebuildComplete() handler below finds actual content, which is the
	// sole source of truth for this from here on (covers loading, deleting
	// down to nothing, etc.).
	label_23->setVisible(false);
	searchBox->setVisible(false);

	// Collapsed state is shared across documents (persisted, not per-
	// instance) - a new document should open already collapsed if the user
	// left it that way on another one, and that should survive a restart
	// too, same as the other QSettings-backed UI state in this app.
	{
		QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
		_navigationCollapsed = settings.value(QStringLiteral("NavigationPanelCollapsed"), false).toBool();
	}

	// The overlay is an absolutely-positioned floating child of
	// _viewportWidget (see attachOverlayPanel() below), not a normal
	// side-by-side grid column - so the collapse button has to be wrapped
	// INSIDE the same content widget that gets attached as the overlay,
	// glued to modelNavigationWidget's own left edge, rather than living
	// in this document's outer gridLayout (which attachOverlayPanel()
	// reparents modelNavigationWidget away from entirely).
	_navCollapseButton = new QToolButton();
	_navCollapseButton->setObjectName(QStringLiteral("navCollapseButton"));
	_navCollapseButton->setMinimumSize(14, 0);
	_navCollapseButton->setMaximumSize(14, QWIDGETSIZE_MAX);
	_navCollapseButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
	_navCollapseButton->setAutoRaise(true);
	_navCollapseButton->setText(_navigationCollapsed ? QStringLiteral("▶") : QStringLiteral("◀"));
	_navCollapseButton->setToolTip(_navigationCollapsed
		? tr("Expand the model navigation panel")
		: tr("Collapse the model navigation panel"));

	// Right-edge counterpart to _navCollapseButton above - an invisible drag
	// strip the user can pull to resize the panel, matching the established
	// event-filter pattern in VisualizationEnvironmentPanel's punctual-lights
	// tree handle, just horizontal instead of vertical - but with no visible
	// line even on hover, only the resize cursor, per explicit user request.
	_navResizeHandle = new QFrame();
	_navResizeHandle->setObjectName(QStringLiteral("navResizeHandle"));
	_navResizeHandle->setFrameShape(QFrame::NoFrame);
	_navResizeHandle->setCursor(Qt::SizeHorCursor);
	_navResizeHandle->setFixedWidth(6);
	_navResizeHandle->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
	_navResizeHandle->setToolTip(tr("Drag to resize the navigation panel"));
	_navResizeHandle->installEventFilter(this);

	auto* navComposite = new QWidget();
	navComposite->setObjectName(QStringLiteral("navComposite"));
	// Transparent like modelNavigationWidget itself, so the overlay
	// wrapper's own translucent panel styling (applyOverlayPanelStyle())
	// shows through instead of this composite's plain default background
	// covering it.
	navComposite->setAttribute(Qt::WA_NoSystemBackground, true);
	navComposite->setAutoFillBackground(false);
	auto* navCompositeLayout = new QHBoxLayout(navComposite);
	navCompositeLayout->setContentsMargins(0, 0, 0, 0);
	navCompositeLayout->setSpacing(0);
	navCompositeLayout->addWidget(_navCollapseButton);
	navCompositeLayout->addWidget(modelNavigationWidget, 1);
	navCompositeLayout->addWidget(_navResizeHandle);

	// Apply the persisted collapsed state loaded above.
	modelNavigationWidget->setVisible(!_navigationCollapsed);
	_navResizeHandle->setVisible(!_navigationCollapsed);

	// Whole panel (not just its contents) hides on collapse - the button
	// stays behind (it's a sibling in navComposite, not a child of
	// modelNavigationWidget) so it's still clickable to re-expand. The
	// resize handle hides too - there's nothing to resize while collapsed.
	// updateNavigationOverlayGeometry() shrinks the overlay's own width to
	// match so the collapsed state doesn't leave 400+px of dead space.
	// Persisted (not just applied to this document) so every other open or
	// subsequently-opened document starts collapsed/expanded the same way.
	connect(_navCollapseButton, &QToolButton::clicked, this, [this]() {
		_navigationCollapsed = !_navigationCollapsed;
		modelNavigationWidget->setVisible(!_navigationCollapsed);
		_navResizeHandle->setVisible(!_navigationCollapsed);
		_navCollapseButton->setText(_navigationCollapsed ? QStringLiteral("▶") : QStringLiteral("◀"));
		_navCollapseButton->setToolTip(_navigationCollapsed
			? tr("Expand the model navigation panel")
			: tr("Collapse the model navigation panel"));
		updateNavigationOverlayGeometry();

		QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
		settings.setValue(QStringLiteral("NavigationPanelCollapsed"), _navigationCollapsed);
	});

	_navigationOverlay = _viewportWidget->attachOverlayPanel(
		navComposite,
		QRect(0, 0, _navigationOverlayWidth, std::max(120, _viewportWidget->height())),
		Qt::AlignTop | Qt::AlignLeft,
		"navigationOverlayPanel");

	if (_navigationOverlay)
	{
		_viewportWidget->refreshDetachedNavigationOverlayTheme();
		updateNavigationOverlayGeometry();
		_navigationOverlay->show();
		// The navigation overlay and tabbed toolbar are viewport siblings.
		// Raise the outer container so both pages and the pin remain above
		// the overlay immediately, including when the toolbar is pinned.
		_viewportWidget->raiseViewportToolbar();
		QMetaObject::invokeMethod(this, [this]()
		{
			if (_navigationOverlay && _viewportWidget)
				_viewportWidget->refreshDetachedNavigationOverlayTheme();
		}, Qt::QueuedConnection);
	}

	// setVisible(), not show() - must still respect a persisted collapsed
	// state instead of unconditionally forcing this on.
	modelNavigationWidget->setVisible(!_navigationCollapsed);
}

void ModelViewer::updateNavigationOverlayGeometry()
{
	if (!_navigationOverlay || !_viewportWidget)
		return;

	// Now that the checkbox row and mesh-count text have both moved out of
	// this overlay (into MainWindow's shared Document tab), there's no
	// longer anything needing headroom above the search box/tree - flush
	// against the viewport's top-left edge for a seamless look.
	const int overlayTop = 0;
	const int overlayLeft = 0;
	// Just enough for the collapse button itself once collapsed - matches
	// its own locked 14px width plus the overlay wrapper's own 6px margins
	// (see attachOverlayPanel()).
	const int overlayCollapsedWidth = 26;
	// Full height, no bottom margin - deliberately overlaps the bottom-docked
	// ViewToolbar's hover-reveal strip (see ViewportWidget::mouseMoveEvent())
	// wherever the two intersect, so the toolbar won't auto-reveal while the
	// mouse is over the navigation panel. Accepted as a reasonable trade-off:
	// the toolbar's reveal area outside the panel's width is unaffected.
	_navigationOverlay->setGeometry(
		overlayLeft,
		overlayTop,
		_navigationCollapsed ? overlayCollapsedWidth : _navigationOverlayWidth,
		std::max(120, _viewportWidget->height() - overlayTop));
}

void ModelViewer::showTextureDebugPanel()
{
	if (!_textureDebugPanel)
		return;
	_textureDebugPanel->show();
	_textureDebugPanel->raise();
	_textureDebugPanel->activateWindow();
	// Trigger an immediate readback if there is already a selection.
	_textureDebugPanel->refresh();
}

void ModelViewer::applyVariant(const QString& sourceFile, int variantIndex)
{
	if (!_viewportWidget || !_sceneGraph)
		return;

	const std::vector<SceneMesh*>& meshes = _viewportWidget->getMeshStore();
	for (SceneMesh* mesh : meshes)
	{
		if (!mesh || mesh->getSourceFile() != sourceFile)
			continue;
		if (!mesh->hasVariants())
			continue;

		const Material* mat = mesh->materialForVariant(variantIndex);
		if (mat)
		{
			// Resolve texture paths → GPU IDs before applying.
			// The prebuilt variant Materials carry paths but texture IDs
			// are 0 until resolveMaterialTextures uploads them, matching
			// what setTexturesToObjects does for the regular material path.
			const Material resolved = ViewportWidget::resolveMaterialTextures(_viewportWidget, *mat);
			mesh->setMaterial(resolved);
			mesh->setTextureMaps(resolved);
			mesh->invertOpacityADSMap(resolved.isOpacityMapInverted());
			mesh->invertOpacityPBRMap(resolved.isOpacityMapInverted());
			mesh->setActiveVariantIndex(variantIndex);
		}
	}

	_sceneGraph->setActiveVariant(sourceFile, variantIndex);
	_viewportWidget->refreshAnimationMaterialState(sourceFile);
	_viewportWidget->update();
	_viewportWidget->notifyRayTracedSceneMutated();
}

void ModelViewer::deleteVariant(const QString& sourceFile, int variantIndex)
{
	if (!_sceneGraph || !_viewportWidget || !_undoStack || sourceFile.isEmpty())
		return;
	_undoStack->push(new MetadataDeleteCommand(
		this, _viewportWidget, MetadataDeleteCommand::Kind::Variant,
		sourceFile, variantIndex, tr("Delete Variant")));
}

void ModelViewer::deleteAnimationClip(const QString& sourceFile, int clipIndex)
{
	if (!_sceneGraph || !_viewportWidget || !_undoStack || sourceFile.isEmpty())
		return;
	_undoStack->push(new MetadataDeleteCommand(
		this, _viewportWidget, MetadataDeleteCommand::Kind::Animation,
		sourceFile, clipIndex, tr("Delete Animation")));
}

void ModelViewer::deleteGltfCamera(const QString& sourceFile, int cameraIndex)
{
	if (!_sceneGraph || !_viewportWidget || !_undoStack || sourceFile.isEmpty())
		return;
	_undoStack->push(new MetadataDeleteCommand(
		this, _viewportWidget, MetadataDeleteCommand::Kind::Camera,
		sourceFile, cameraIndex, tr("Delete Camera")));
}

void ModelViewer::captureVariant(const QString& sourceFile, const QString& variantName)
{
	if (!_sceneGraph || !_viewportWidget || !_undoStack || sourceFile.isEmpty() || variantName.isEmpty())
		return;
	_undoStack->push(new CaptureVariantCommand(this, _viewportWidget, sourceFile, variantName));
}

void ModelViewer::setVariantAsDefault(const QString& sourceFile, int variantIndex)
{
	if (!_sceneGraph || !_viewportWidget || !_undoStack || sourceFile.isEmpty() || variantIndex < 0)
		return;
	_undoStack->push(new SetDefaultVariantCommand(this, _viewportWidget, sourceFile, variantIndex));
}

void ModelViewer::captureCameraView(const QString& name)
{
	if (!_sceneGraph || !_viewportWidget || !_undoStack || name.isEmpty())
		return;
	_undoStack->push(new CaptureCameraCommand(this, _viewportWidget, name));
}

void ModelViewer::addMeasurement(const Measurement& measurement)
{
	if (!_sceneGraph || !_viewportWidget || !_undoStack || measurement.anchors.isEmpty())
		return;
	_undoStack->push(new AddMeasurementCommand(this, _viewportWidget, measurement));
}

void ModelViewer::deleteMeasurement(const QUuid& measurementId)
{
	if (!_sceneGraph || !_viewportWidget || !_undoStack || measurementId.isNull())
		return;
	_undoStack->push(new DeleteMeasurementCommand(this, _viewportWidget, measurementId));
}

void ModelViewer::deleteSelectedMeasurements()
{
	if (!_viewportWidget)
		return;

	const QSet<QUuid> selected = _viewportWidget->selectedMeasurementIds();
	if (selected.isEmpty())
		return;

	const bool multiple = selected.size() > 1;
	if (multiple && _undoStack)
		_undoStack->beginMacro(tr("Delete %1 Measurements").arg(selected.size()));

	for (const QUuid& id : selected)
		deleteMeasurement(id);

	if (multiple && _undoStack)
		_undoStack->endMacro();

	_viewportWidget->setSelectedMeasurementIds({});
}

void ModelViewer::addAnnotation(const Annotation& annotation)
{
	if (!_sceneGraph || !_viewportWidget || !_undoStack || !annotation.anchor.isValid())
		return;
	_undoStack->push(new AddAnnotationCommand(this, _viewportWidget, annotation));
}

void ModelViewer::deleteAnnotation(const QUuid& annotationId)
{
	if (!_sceneGraph || !_viewportWidget || !_undoStack || annotationId.isNull())
		return;
	_undoStack->push(new DeleteAnnotationCommand(this, _viewportWidget, annotationId));
}

void ModelViewer::deleteSelectedAnnotations()
{
	if (!_viewportWidget)
		return;

	const QSet<QUuid> selected = _viewportWidget->selectedAnnotationIds();
	if (selected.isEmpty())
		return;

	const bool multiple = selected.size() > 1;
	if (multiple && _undoStack)
		_undoStack->beginMacro(tr("Delete %1 Annotations").arg(selected.size()));

	for (const QUuid& id : selected)
		deleteAnnotation(id);

	if (multiple && _undoStack)
		_undoStack->endMacro();

	_viewportWidget->setSelectedAnnotationIds({});
}

void ModelViewer::setAnnotationText(const QUuid& annotationId, const QString& text)
{
	if (!_sceneGraph || !_viewportWidget || !_undoStack || annotationId.isNull())
		return;

	const int index = _sceneGraph->annotationIndexById(annotationId);
	if (index < 0)
		return;

	const QString oldText = _sceneGraph->annotations().at(index).text;
	if (oldText == text)
		return;

	_undoStack->push(new AnnotationTextCommand(this, _viewportWidget, annotationId, oldText, text));
}

void ModelViewer::openMeasurementDialog(const QUuid& selectId)
{
	// Measure and Annotate arm mutually exclusive viewport tools (see
	// ViewportWidget::setMeasurementTool()/setAnnotationToolArmed()) - close
	// Annotate's dialog outright rather than leaving it open showing a tool
	// that's no longer actually armed underneath it. close() runs its own
	// closeEvent() (disarms, saves geometry) and WA_DeleteOnClose then frees
	// it, same as the user closing it themselves.
	if (AnnotationDialog* other = findChild<AnnotationDialog*>(QString(), Qt::FindDirectChildrenOnly))
		other->close();

	MeasurementDialog* dialog = findChild<MeasurementDialog*>(QString(), Qt::FindDirectChildrenOnly);
	if (!dialog)
	{
		dialog = new MeasurementDialog(this, this);
		dialog->setAttribute(Qt::WA_DeleteOnClose);
	}
	dialog->show();
	dialog->raise();
	dialog->activateWindow();

	if (!selectId.isNull() && _viewportWidget)
		_viewportWidget->setSelectedMeasurementIds({ selectId });
}

void ModelViewer::openAnnotationDialog(const QUuid& selectId)
{
	// See openMeasurementDialog()'s matching comment - same mutual
	// exclusivity, other direction.
	if (MeasurementDialog* other = findChild<MeasurementDialog*>(QString(), Qt::FindDirectChildrenOnly))
		other->close();

	AnnotationDialog* dialog = findChild<AnnotationDialog*>(QString(), Qt::FindDirectChildrenOnly);
	if (!dialog)
	{
		dialog = new AnnotationDialog(this, this);
		dialog->setAttribute(Qt::WA_DeleteOnClose);
	}
	dialog->show();
	dialog->raise();
	dialog->activateWindow();

	if (!selectId.isNull() && _viewportWidget)
		_viewportWidget->setSelectedAnnotationIds({ selectId });
}

QSet<QUuid> ModelViewer::getVisibleMeasurementUuids() const
{
	QSet<QUuid> ids;
	if (_sceneGraph)
	{
		for (const Measurement& m : _sceneGraph->measurements())
			if (m.visible)
				ids.insert(m.id);
	}
	return ids;
}

void ModelViewer::setMeasurementVisibilityWithUndo(const QSet<QUuid>& newVisibleIds, const QString& commandText)
{
	if (!_sceneGraph || !_viewportWidget || !_undoStack)
		return;
	// Note: push() automatically calls redo() on the command
	_undoStack->push(new MeasurementVisibilityCommand(this, _viewportWidget, newVisibleIds, commandText));
}

void ModelViewer::setMeasurementVisibilityWithoutUndo(const QSet<QUuid>& visibleIds)
{
	if (!_sceneGraph)
		return;
	for (const Measurement& m : _sceneGraph->measurements())
		_sceneGraph->setMeasurementVisible(m.id, visibleIds.contains(m.id));

	// Measurement visibility mutates SceneGraph directly, never through
	// ViewportWidget::setDisplayList() - without this, bounds went stale
	// and "Auto Fit View On Hide/Show" silently never applied here.
	if (_viewportWidget)
		_viewportWidget->refreshMeasurementAnnotationBounds();
}

void ModelViewer::hideSelectedMeasurements()
{
	if (!_viewportWidget)
		return;
	const QSet<QUuid> selected = _viewportWidget->selectedMeasurementIds();
	if (selected.isEmpty())
		return;
	QSet<QUuid> newVisible = getVisibleMeasurementUuids();
	newVisible -= selected;
	setMeasurementVisibilityWithUndo(newVisible, tr("Hide Measurement"));
}

void ModelViewer::showSelectedMeasurements()
{
	if (!_viewportWidget)
		return;
	const QSet<QUuid> selected = _viewportWidget->selectedMeasurementIds();
	if (selected.isEmpty())
		return;
	QSet<QUuid> newVisible = getVisibleMeasurementUuids();
	newVisible += selected;
	setMeasurementVisibilityWithUndo(newVisible, tr("Show Measurement"));
}

QSet<QUuid> ModelViewer::getVisibleAnnotationUuids() const
{
	QSet<QUuid> ids;
	if (_sceneGraph)
	{
		for (const Annotation& a : _sceneGraph->annotations())
			if (a.visible)
				ids.insert(a.id);
	}
	return ids;
}

void ModelViewer::setAnnotationVisibilityWithUndo(const QSet<QUuid>& newVisibleIds, const QString& commandText)
{
	if (!_sceneGraph || !_viewportWidget || !_undoStack)
		return;
	_undoStack->push(new AnnotationVisibilityCommand(this, _viewportWidget, newVisibleIds, commandText));
}

void ModelViewer::setAnnotationVisibilityWithoutUndo(const QSet<QUuid>& visibleIds)
{
	if (!_sceneGraph)
		return;
	for (const Annotation& a : _sceneGraph->annotations())
		_sceneGraph->setAnnotationVisible(a.id, visibleIds.contains(a.id));

	// Same reasoning as setMeasurementVisibilityWithoutUndo() above.
	if (_viewportWidget)
		_viewportWidget->refreshMeasurementAnnotationBounds();
}

void ModelViewer::hideSelectedAnnotations()
{
	if (!_viewportWidget)
		return;
	const QSet<QUuid> selected = _viewportWidget->selectedAnnotationIds();
	if (selected.isEmpty())
		return;
	QSet<QUuid> newVisible = getVisibleAnnotationUuids();
	newVisible -= selected;
	setAnnotationVisibilityWithUndo(newVisible, tr("Hide Annotation"));
}

void ModelViewer::showSelectedAnnotations()
{
	if (!_viewportWidget)
		return;
	const QSet<QUuid> selected = _viewportWidget->selectedAnnotationIds();
	if (selected.isEmpty())
		return;
	QSet<QUuid> newVisible = getVisibleAnnotationUuids();
	newVisible += selected;
	setAnnotationVisibilityWithUndo(newVisible, tr("Show Annotation"));
}

void ModelViewer::setupUndoStackMonitoring()
{
	// Connect to stack changes
	connect(_undoStack, &QUndoStack::indexChanged,
		this, &ModelViewer::onUndoStackChanged);

	// Initialize cache
	_lastUndoIndex = _undoStack ? _undoStack->index() : 0;
	_savedUndoIndex = _lastUndoIndex;
	_lastStackCount = 0;
	_cachedReferencedUuids.clear();
}

void ModelViewer::onUndoStackChanged()
{
	if (!_undoStack || !_viewportWidget)
		return;

	// Any undo/redo/push (material edits, transforms, visibility, light
	// edits, delete/paste, ...) can change what ray-traced mode should be
	// showing - unlike raw camera interaction, none of this already flows
	// through mousePressEvent()/wheelEvent()/etc., so it needs its own nudge
	// back to the live raster feed + settle countdown.
	_viewportWidget->notifyRayTracedSceneMutated();

	const int currentIndex = _undoStack->index();
	int currentCount = _undoStack->count();

	int changedCommandIndex = -1;
	if (currentIndex > _lastUndoIndex)
		changedCommandIndex = currentIndex - 1;
	else if (currentIndex < _lastUndoIndex)
		changedCommandIndex = _lastUndoIndex - 1;

	if (changedCommandIndex >= 0 && changedCommandIndex < currentCount)
	{
		if (undoCommandAffectsDocument(_undoStack->command(changedCommandIndex)))
		{
			_documentSaved = false;
		}
	}

	setDocumentModified(_nonUndoDocumentDirty || hasUnsavedUndoDocumentChanges());

	// Only cleanup when stack size changes (commands added/purged)
	// Not on every undo/redo (which just changes index)
	if (currentCount != _lastStackCount)
	{
		// Check if commands were purged (count decreased)
		// or if this is the first operation (count increased from 0)
		bool shouldCleanup = (currentCount < _lastStackCount) ||
			(_lastStackCount == 0 && currentCount > 0);

		if (shouldCleanup)
		{
			cleanupOrphanedMeshes();
		}
		else
		{
			// Count increased - command was added
			// Update cache incrementally instead of full scan

			// Get the newly added command (at current index - 1)
			int newCmdIndex = _undoStack->index() - 1;
			if (newCmdIndex >= 0 && newCmdIndex < _undoStack->count())
			{
				const QUndoCommand* cmd = _undoStack->command(newCmdIndex);

				if (const auto* delCmd = dynamic_cast<const DeleteMeshCommand*>(cmd))
					_cachedReferencedUuids.unite(delCmd->getReferencedUuids());
				else if (const auto* dupCmd = dynamic_cast<const DuplicateCommand*>(cmd))
					_cachedReferencedUuids.unite(dupCmd->getReferencedUuids());
				else if (const auto* pasteCmd = dynamic_cast<const PasteCommand*>(cmd))
					_cachedReferencedUuids.unite(pasteCmd->getReferencedUuids());
			}
		}

		_lastStackCount = currentCount;
	}

	_lastUndoIndex = currentIndex;
}

bool ModelViewer::undoCommandAffectsDocument(const QUndoCommand* command) const
{
	const auto* modelCommand = dynamic_cast<const ModelViewerCommand*>(command);
	return modelCommand && modelCommand->affectsDocument();
}

bool ModelViewer::hasUnsavedUndoDocumentChanges() const
{
	if (!_undoStack)
		return false;

	const int currentIndex = _undoStack->index();
	const int commandCount = _undoStack->count();
	if (currentIndex == _savedUndoIndex)
		return false;
	if (_savedUndoIndex < 0 || _savedUndoIndex > commandCount)
		return true;

	const int rangeBegin = std::min(currentIndex, _savedUndoIndex);
	const int rangeEnd = std::max(currentIndex, _savedUndoIndex);
	for (int index = rangeBegin; index < rangeEnd; ++index)
	{
		if (undoCommandAffectsDocument(_undoStack->command(index)))
			return true;
	}
	return false;
}

void ModelViewer::cleanupOrphanedMeshes()
{
	// Get current set of referenced UUIDs by scanning stack
	QSet<QUuid> currentlyReferenced = scanStackForReferencedUuids();

	// Find UUIDs that were in cache but no longer referenced
	QSet<QUuid> orphaned = _cachedReferencedUuids - currentlyReferenced;

	if (!orphaned.isEmpty())
	{
		// Permanently delete orphaned meshes from recycle bin
		for (const QUuid& uuid : orphaned)
		{
			_viewportWidget->permanentlyDeleteFromBin(uuid);
		}

		qDebug() << "Cleaned up" << orphaned.size()
			<< "orphaned mesh(es) from recycle bin";
	}

	// Update cache
	_cachedReferencedUuids = currentlyReferenced;
}

bool ModelViewer::saveMaterialsBeforeClose()
{
	// Get the material panel
	MaterialPropertiesPanel* materialPanel = predefinedMaterialsPanel;
	if (!materialPanel)
	{
		qWarning() << "Material panel not available for saving unsaved materials";
		return false;
	}

	// Get all unsaved material keys
	QSet<QString> unsavedKeys = materialPanel->getUnsavedMaterialKeys();
	if (unsavedKeys.isEmpty())
	{
		return true;  // Nothing to save
	}

	int savedCount = 0;
	int failedCount = 0;

	// Block signals during batch save to prevent "select a mesh" dialogs during tree refresh
	materialPanel->beginSaveUnsavedMaterials();

	// Save each unsaved material to the library
	for (const QString& key : unsavedKeys)
	{
		// Get cached material with metadata
		auto cachedIt = _materialCache.find(key);
		if (cachedIt == _materialCache.end())
		{
			qWarning() << "Material key not found in cache:" << key;
			failedCount++;
			continue;
		}

		const CachedMaterial& cached = cachedIt.value();
		QString groupLabel = cached.group;
		QString materialName = cached.name;
		const Material& material = cached.material;

		// Save to library using existing infrastructure
		// Pass nullptr as parent to suppress "Overwrite?" dialog during closeEvent
		// User already chose "Save All", so we auto-confirm overwrites
		QString errorMsg;
		bool success = MaterialLibraryWidget::saveUserMaterialToUserLocation(
			groupLabel,
			key,
			materialName,
			material,
			nullptr,  // No parent = no confirmation dialog
			&errorMsg
		);

		if (success)
		{
			// Remove from unsaved set
			materialPanel->removeMaterialFromUnsaved(key);
			_ownedUnsavedMaterials.remove(key);
			savedCount++;
		}
		else
		{
			failedCount++;
			qWarning() << "FAILED to save material:" << materialName;
			qWarning() << "  Key:" << key;
			qWarning() << "  Group:" << groupLabel;
			qWarning() << "  Error:" << errorMsg;
		}
	}

	// Unblock signals and refresh tree BEFORE showing dialogs
	// This prevents the tree refresh from triggering material selection signals
	// which would cause "Please select a mesh" warnings
	materialPanel->endSaveUnsavedMaterials();

	// Show result to user
	if (failedCount > 0)
	{
		QString msg = QString(tr("Saved %1 of %2 material(s). %3 failed to save."))
			.arg(savedCount)
			.arg(savedCount + failedCount)
			.arg(failedCount);
		QMessageBox::warning(this, tr("Save Materials - Partial Success"), msg);
		return false;  // Some materials failed to save
	}
	else if (savedCount > 0)
	{
		QString msg = QString(tr("Successfully saved %1 material(s) to library.")).arg(savedCount);
		QMessageBox::information(this, tr("Materials Saved"), msg);
		return true;  // All saved successfully
	}

	return true;  // Nothing needed saving
}

void ModelViewer::cleanupUnsavedMaterialsFromLibrary()
{
	// Remove ONLY unsaved materials CREATED BY THIS MDI from shared library when it closes
	// This allows other MDIs' unsaved materials to remain visible

	if (_ownedUnsavedMaterials.isEmpty())
		return;

	// Remove from shared material map - only owned materials
	auto& sharedMap = const_cast<QMap<QString, std::function<Material()>>&>(
		MaterialLibraryWidget::sharedMaterialMap());

	for (const QString& key : _ownedUnsavedMaterials)
	{
		sharedMap.remove(key);
	}

	// Remove from shared groups - only owned materials
	auto& mutableGroups = const_cast<QVector<QPair<QString, QVector<QPair<QString, QString>>>>&>(
		MaterialLibraryWidget::sharedGroups());

	for (auto& groupPair : mutableGroups)
	{
		// Remove only unsaved materials owned by this MDI
		auto& materials = groupPair.second;
		materials.erase(
			std::remove_if(materials.begin(), materials.end(),
				[this](const QPair<QString, QString>& item) {
					return _ownedUnsavedMaterials.contains(item.second);
				}),
			materials.end()
		);
	}

}

QSet<QUuid> ModelViewer::scanStackForReferencedUuids()
{
	QSet<QUuid> referenced;

	// Scan all commands in the undo stack
	int count = _undoStack->count();
	for (int i = 0; i < count; ++i)
	{
		const QUndoCommand* cmd = _undoStack->command(i);

		if (const auto* delCmd = dynamic_cast<const DeleteMeshCommand*>(cmd))
			referenced.unite(delCmd->getReferencedUuids());
		else if (const auto* dupCmd = dynamic_cast<const DuplicateCommand*>(cmd))
			referenced.unite(dupCmd->getReferencedUuids());
		else if (const auto* pasteCmd = dynamic_cast<const PasteCommand*>(cmd))
			referenced.unite(pasteCmd->getReferencedUuids());
		// CutCommand::getReferencedUuids() returns {} — nothing goes to the bin
	}

	return referenced;
}

void ModelViewer::updateDisplayList()
{
	_viewportWidget->setTransmissionEnabled(false);
	for (SceneMesh* mesh : _viewportWidget->getMeshStore())
	{
		const Material& mat = mesh->getMaterial();
		if (mat.hasTransmission() || mat.diffuseTransmissionFactor() > 0.0f)
		{
			_viewportWidget->setTransmissionEnabled(true);
			break;
		}
	}

	const bool shouldAutoFit = _viewportWidget->autoFitViewOnUpdate();
	_viewportWidget->setAutoFitViewOnUpdate(false);

	_visibleMeshUuids = collectVisibleUuidsFromDisplayList();
	if (_progressiveLoadingEnabled)
	{
		const QList<QUuid> pendingSceneUuids = _viewportWidget->getPendingSceneUuids();
		for (const QUuid& uuid : pendingSceneUuids)
		{
			if (!uuid.isNull())
				_visibleMeshUuids.insert(uuid);
		}
	}
	applyVisibleMeshState(false);

	++_treeRebuildGeneration;
	_treeRebuildPending = false;
	rebuildTreeFromCurrentState();
	_viewportWidget->setAutoFitViewOnUpdate(shouldAutoFit);

	// Start the fit animation immediately using the bounding sphere already
	// computed by setDisplayList() above — no need to wait for the tree
	// rebuild.  Skip when the mesh store is empty (e.g. the initial show
	// before any file is loaded) to avoid fitting an empty / sentinel sphere.
	if (shouldAutoFit &&
		!_viewportWidget->getMeshStore().empty() &&
		_viewportWidget->cameraMode() == Camera::CameraMode::Orbit)
	{
		if (!_viewportWidget->isGltfCameraActive())
		{
			if (!_viewportWidget->getPendingSceneUuids().isEmpty() && !_animateProgressiveFitEnabled)
				_viewportWidget->fitAllImmediate();
			else
				_viewportWidget->fitAll();
		}
	}

	
}

void ModelViewer::updateSelectionStatusMessage()
{
	int count = static_cast<int>(treeWidgetModel->selectedMeshUuids().count());
	if (count)
	{
		QString noun = count > 1 ? tr("objects") : tr("object");
		MainWindow::showStatusMessage(QString(tr("Selected %1 %2")).arg(count).arg(noun));
	}
	else
		MainWindow::showStatusMessage(tr("No selection"), 2000);
}

void ModelViewer::showEvent(QShowEvent*)
{
	//showMaximized();
	if (_runningFirstTime)
	{
		updateDisplayList();
		_runningFirstTime = false;
	}
}

bool ModelViewer::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == _navResizeHandle)
	{
		auto* me = static_cast<QMouseEvent*>(event);
		switch (event->type())
		{
		case QEvent::MouseButtonPress:
			if (me->button() == Qt::LeftButton)
			{
				_navResizeDragStartX = me->globalPosition().x();
				_navResizeDragStartWidth = _navigationOverlayWidth;
				return true;
			}
			break;

		case QEvent::MouseMove:
			if ((me->buttons() & Qt::LeftButton) && _viewportWidget)
			{
				constexpr int kMinWidth = 200;
				const int maxWidth = qMax(kMinWidth, _viewportWidget->width() / 2);
				const int delta = static_cast<int>(me->globalPosition().x() - _navResizeDragStartX);
				_navigationOverlayWidth = qBound(kMinWidth, _navResizeDragStartWidth + delta, maxWidth);
				updateNavigationOverlayGeometry();
				return true;
			}
			break;

		case QEvent::MouseButtonRelease:
			return true;

		default:
			break;
		}
	}

	if (_treeRebuildPending &&
		(watched == treeWidgetModel || watched == treeWidgetModel->viewport()))
	{
		switch (event->type())
		{
		case QEvent::Enter:
		case QEvent::FocusIn:
		case QEvent::MouseButtonPress:
		case QEvent::MouseButtonDblClick:
		case QEvent::ContextMenu:
		case QEvent::KeyPress:
			rebuildTreeFromCurrentState();
			break;
		default:
			break;
		}
	}

	if (_treeVisibilityDirty &&
		(watched == treeWidgetModel || watched == treeWidgetModel->viewport()))
	{
		switch (event->type())
		{
		case QEvent::Enter:
		case QEvent::FocusIn:
		case QEvent::MouseButtonPress:
		case QEvent::MouseButtonDblClick:
		case QEvent::ContextMenu:
		case QEvent::KeyPress:
			syncTreeVisibilityFromModel();
			break;
		default:
			break;
		}
	}

	return QWidget::eventFilter(watched, event);
}

void ModelViewer::keyPressEvent(QKeyEvent* event)
{
	if (event->modifiers() == Qt::ControlModifier)
	{
		if (event->key() == Qt::Key_A)
		{
			selectAll();
		}
	}
	else if (event->modifiers() == Qt::AltModifier)
	{
		if (event->key() == Qt::Key_A)
			hideAllItems();
		if (event->key() == Qt::Key_C)
			centerScreen();
	}
	else if (event->modifiers() == Qt::ShiftModifier)
	{
		if (event->key() == Qt::Key_A)
			showAllItems();
	}
	else
	{
	}

	QWidget::keyPressEvent(event);
}

void ModelViewer::selectAll()
{
	if (treeWidgetModel->meshCount() > 0)
	{
		QSet<QUuid> toSelect;
		if (_viewportWidget->isVisibleSwapped())
		{
			// Visible-swapped mode: select the "hidden" meshes (unchecked)
			const QSet<QUuid> visible = getVisibleUuids();
			const auto& store = _viewportWidget->getMeshStore();
			for (size_t i = 0; i < store.size(); ++i)
			{
				QUuid uuid = _viewportWidget->getUuidByIndex(static_cast<int>(i));
				if (!visible.contains(uuid)) toSelect.insert(uuid);
			}
		}
		else
		{
			// Normal mode: select all visible (checked) meshes
			toSelect = getVisibleUuids();
		}
		treeWidgetModel->setSelectionByUuids(toSelect);
		handleTreeWidgetSelectionChanged();
	}
}

void ModelViewer::dragEnterEvent(QDragEnterEvent* event)
{
	if (event->mimeData()->hasUrls())
	{
		event->acceptProposedAction();
	}
}

void ModelViewer::dropEvent(QDropEvent* event)
{
	QStringList supportedExtensions = ModelViewerApplication::supportedImportExtensions();
	QApplication::setOverrideCursor(Qt::WaitCursor);
	foreach(const QUrl & url, event->mimeData()->urls())
	{
		QString fileName = url.toLocalFile();
		_lastOpenedDir = QFileInfo(fileName).path(); // store path for next time
		QFileInfo fi(fileName);
		QString extn = fi.suffix();
		if (!supportedExtensions[0].contains(extn, Qt::CaseInsensitive)
			&& extn != "mvf")
		{
			QMessageBox::critical(this, tr("Error"), url.toString() + tr("\nUnsupported file format: ") + extn);
		}
		else
		{
			if (extn == "mvf")
				loadFromFile(fileName);
			else
			{
				UVMethod method;
				QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
				bool remember = settings.value("RememberUVMethod", false).toBool();
				if (remember)
				{
					int value = settings.value("UVMethod", static_cast<int>(UVMethod::None)).toInt();
					method = static_cast<UVMethod>(value);
				}
				else
					method = askUserForUVMethod(this).method;
				QString errMsg;
				_progressiveLoadingEnabled = settings.value("checkProgressiveLoading", true).toBool();
				_animateProgressiveFitEnabled = settings.value("checkAnimateProgressiveFit", true).toBool();
				bool success = _viewportWidget->loadAssImpModel(fileName, method, errMsg, _progressiveLoadingEnabled);
				if (!success)
				{
					if (errMsg == "Model loading cancelled by user.")
					{
						continue;
					}
					QMessageBox::critical(this, tr("Error"), tr("Failed to load model: ") + fileName + "\n" + errMsg);
					continue;
				}
			}

			updateDisplayList();
		}
	}
	QApplication::restoreOverrideCursor();
}

void ModelViewer::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	updateNavigationOverlayGeometry();
}

void ModelViewer::mouseMoveEvent(QMouseEvent* event)
{
	QWidget::mouseMoveEvent(event);
}


void ModelViewer::closeEvent(QCloseEvent* event)
{
	// Check for unsaved materials first
	MaterialPropertiesPanel* materialPanel = predefinedMaterialsPanel;
	QSet<QString> unsavedKeys = materialPanel ? materialPanel->getUnsavedMaterialKeys() : QSet<QString>();

	if (!unsavedKeys.isEmpty())
	{
		int count = unsavedKeys.size();
		QString msg = QString(tr("You have %1 unsaved material(s). Do you want to save them?")).arg(count);

		QMessageBox::StandardButton reply = QMessageBox::question(
			this,
			tr("Unsaved Materials"),
			msg,
			QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

		if (reply == QMessageBox::Cancel)
		{
			event->ignore();
			return;
		}
		else if (reply == QMessageBox::Yes)
		{
			// Save all unsaved materials to library
			if (!saveMaterialsBeforeClose())
			{
				// Ask user if they want to close anyway
				int closeAnyway = QMessageBox::question(this,
					tr("Save Failed"),
					tr("Failed to save some materials. Close anyway?"),
					QMessageBox::Yes | QMessageBox::No,
					QMessageBox::No);

				if (closeAnyway != QMessageBox::Yes)
				{
					event->ignore();
					return;
				}
			}
		}
		// If No, just proceed with closing (materials will be discarded)
	}

	// Check for unsaved document changes
	if (_documentModified)
	{
		auto ret = QMessageBox::question(this, tr("Unsaved Changes"),
			tr("The document has unsaved changes. Do you want to save before closing?"),
			QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
			QMessageBox::Yes);
		if (ret == QMessageBox::Yes)
		{
			if (!save())
			{
				event->ignore();
				return;
			}
		}
		else if (ret == QMessageBox::Cancel)
		{
			event->ignore();
			return;
		}
	}

	// Clean up unsaved materials from shared library
	// Unsaved materials are MDI-scoped and should not appear in other MDIs
	cleanupUnsavedMaterialsFromLibrary();

	event->accept();
}

void ModelViewer::setCurrentFile(const QString& fileName)
{
	_currentFile = fileName;
	_documentSaved = true;
	_nonUndoDocumentDirty = false;
	_savedUndoIndex = _undoStack ? _undoStack->index() : 0;
	setDocumentModified(false);
}

QString ModelViewer::currentFile() const
{
	return _currentFile;
}

void ModelViewer::importModel()
{
	onFileImport();
}

void ModelViewer::exportModel()
{
	onFileExport();
}

void ModelViewer::setDocumentModified(bool modified)
{
	const bool changed = (_documentModified != modified);
	_documentModified = modified;
	const QString baseTitle = _currentFile.isEmpty()
		? windowTitle().remove(QLatin1Char('*'))
		: QFileInfo(_currentFile).fileName();
	if (modified)
		setWindowTitle(tr("%1*").arg(baseTitle));
	else
		setWindowTitle(baseTitle);
	if (changed)
		emit documentModifiedChanged(_documentModified);
}

void ModelViewer::markNonUndoDocumentModified()
{
	_nonUndoDocumentDirty = true;
	_documentSaved = false;
	setDocumentModified(true);
}

bool ModelViewer::save()
{
	// If current file's extension is not .mvf, call saveAs
	// this way, user cannot accidentally overwrite non-mvf files
	QString ext = QFileInfo(_currentFile).suffix();
	if (ext.toLower() != "mvf")
	{
		return saveAs();
	}

	if (_currentFile.isEmpty())
	{
		return saveAs();
	}

	if (saveToFile(_currentFile))
	{
		_documentSaved = true;
		_nonUndoDocumentDirty = false;
		_savedUndoIndex = _undoStack ? _undoStack->index() : 0;
		setDocumentModified(false);
		MainWindow::showStatusMessage(tr("File saved"), 2000);
		return true;
	}
	else
	{
		QMessageBox::critical(this, tr("Error"), tr("Failed to save file: %1").arg(_currentFile));
		return false;
	}
}

bool ModelViewer::saveAs()
{
	// Set the filter for .mvf files
	QString filter = tr("Model Viewer Files (*.mvf)");
	QString fileName = QFileDialog::getSaveFileName(this, tr("Save Model"), currentFile(), filter);

	if (fileName.isEmpty())
		return false;

	// Ensure the file has the .mvf extension
	if (!fileName.endsWith(".mvf", Qt::CaseInsensitive))
		fileName += ".mvf";

	_currentFile = fileName;
	return save();
}

void ModelViewer::setDocumentSaved(bool saved)
{
	_documentSaved = saved;
	if (saved)
	{
		setWindowTitle(tr("%1").arg(QFileInfo(_currentFile).fileName()));
	}
	else
	{
		setWindowTitle(tr("%1*").arg(QFileInfo(_currentFile).fileName()));
	}
}

QString ModelViewer::getLastOpenedDir()
{
	return _lastOpenedDir;
}

void ModelViewer::setLastOpenedDir(const QString& lastOpenedDir)
{
	_lastOpenedDir = lastOpenedDir;
}

QString ModelViewer::getLastSelectedFilter()
{
	return _lastSelectedFilter;
}

void ModelViewer::setLastSelectedFilter(const QString& lastSelectedFilter)
{
	_lastSelectedFilter = lastSelectedFilter;
}

void ModelViewer::showContextMenu(const QPoint& pos)
{
	setFocus();

	const bool clickedAssembly = treeWidgetModel->isAssemblyAt(pos);

	// Run ensureAssemblySelectionAt once to determine whether there are any
	// mesh descendants (used to decide which menu sections to show).
	if (clickedAssembly)
		treeWidgetModel->ensureAssemblySelectionAt(pos);

	// NOT hasMeshSelection(): on a first right-click of a previously
	// unselected assembly, ensureAssemblySelectionAt() above selects only
	// the assembly item itself (under blocked signals) - hasMeshSelection()
	// only checks whether a SELECTED item is itself a leaf, so it would read
	// false here even though the assembly has mesh descendants, hiding the
	// entire mesh-operations section (Duplicate, Delete, ...) on that first
	// click. selectedMeshUuids() correctly expands an assembly selection to
	// its leaf descendants via collectLeaves(), matching what the menu
	// actions below actually operate on (expandThen() re-expands the same
	// way right before running).
	const bool hasMeshes = !treeWidgetModel->selectedMeshUuids().isEmpty();

	if (!hasMeshes && !clickedAssembly) return;

	const SceneNode* assemblyNode = clickedAssembly
	    ? treeWidgetModel->nodeAt(pos)
	    : nullptr;

	// A single-mesh assembly is treated as a redundant wrapper around that
	// one mesh, not a real grouping - same rule ToolsToolbar's
	// meshToolDisabledReasons() already uses to decide when Duplicate is
	// available (see its own comment there).
	const bool singleMeshAssembly = clickedAssembly && assemblyNode
	    && _sceneGraph->collectMeshUuids(assemblyNode).size() == 1;

	// Visual feedback: narrow the highlight to just the right-clicked node.
	// Save the full selection so we can restore it if the user dismisses.
	const QSet<QUuid> savedSelection = getSelectedUuids();
	if (clickedAssembly)
		treeWidgetModel->highlightSingleItemAt(pos);

	// Flag set by any action that intentionally changes selection state.
	// If the menu is dismissed without an action, savedSelection is restored.
	bool actionTaken = false;

	// For assembly right-clicks each mesh-op action must re-expand the
	// selection to the full subtree right before it runs, because
	// highlightSingleItemAt narrowed it to a single item.
	// We collect the UUIDs directly instead of relying on ensureAssemblySelectionAt,
	// which is a no-op when the assembly item is already selected.
	auto expandThen = [&](auto fn) {
		return [this, clickedAssembly, assemblyNode, &actionTaken, fn]() mutable {
			actionTaken = true;
			if (clickedAssembly && assemblyNode)
			{
				const QList<QUuid> uuids = _sceneGraph->collectMeshUuids(assemblyNode);
				setSelectionWithoutUndo(QSet<QUuid>(uuids.begin(), uuids.end()));
			}
			fn();
		};
	};

	QMenu myMenu;

	// ---- Expand / Collapse (assembly only) ---------------------------------
	if (clickedAssembly && treeWidgetModel->hasChildrenAt(pos))
	{
		myMenu.addAction(QIcon(QPixmap(":/icons/res/expand.png")),
		    tr("Expand/Collapse to 1st Level"), treeWidgetModel,
		    [this, pos]() { treeWidgetModel->expandOneLevelAt(pos); });

		myMenu.addAction(QIcon(QPixmap(":/icons/res/expandall.png")),
		    tr("Expand All Children"), treeWidgetModel,
		    [this, pos]() { treeWidgetModel->expandSubtreeAt(pos); });

		myMenu.addSeparator();

		myMenu.addAction(QIcon(QPixmap(":/icons/res/collapse.png")),
		    tr("Collapse All Children"), treeWidgetModel,
		    [this, pos]() { treeWidgetModel->collapseAllBelowAt(pos); });

		myMenu.addSeparator();
	}

	// ---- Select Parent -------------------------------------------------------
	// Grouped with Expand/Collapse above as one contiguous "navigate the
	// hierarchy" cluster (look downward into the subtree, then upward to the
	// container) rather than leading the whole menu - it's reached for less
	// often than Expand/Collapse on a freshly right-clicked item, so it
	// doesn't need top billing, just a stable home next to the other
	// no-side-effect navigation actions.
	//
	// Uses the clicked tree item's own QTreeWidgetItem::parent(), NOT the
	// SceneGraph's structural SceneNode::parent - a childless "pure mesh
	// holder" SceneNode has its meshes flattened directly into its
	// grandparent's tree item (see buildSubtree()'s leaf-optimization,
	// SceneTreeWidget.cpp), so a mesh's true SceneNode owner frequently has
	// no tree item of its own at all (findNodeForMesh()'s uuid then matches
	// nothing in the tree - confirmed dead end). Asking the tree for its own
	// parent item instead always matches what's actually displayed, for
	// both a clicked mesh leaf and a clicked assembly alike.
	{
		QTreeWidgetItem* clickedItem = treeWidgetModel->itemAt(pos);
		QTreeWidgetItem* parentItem = clickedItem ? clickedItem->parent() : nullptr;
		const QUuid parentUuid = parentItem
			? parentItem->data(0, SceneTreeWidget::NodeUuidRole).value<QUuid>()
			: QUuid();

		if (!parentUuid.isNull())
		{
			myMenu.addAction(QIcon(":/icons/res/select_parent.png"), tr("Select Parent"), this, [this, parentUuid, &actionTaken]() {
				actionTaken = true;
				treeWidgetModel->selectNodeByUuid(parentUuid);
			});
			myMenu.addSeparator();
		}
	}

	// ---- Copy / Cut --------------------------------------------------------
	myMenu.addAction(QIcon(":/icons/res/copy.png"), tr("Copy"), this, [this, &actionTaken]() {
		actionTaken = true;
		copySelectedItems();
	});

	myMenu.addAction(QIcon(":/icons/res/cut.png"), tr("Cut"), this, [this, &actionTaken]() {
		actionTaken = true;
		cutSelectedItems();
	});

	// ---- Paste (assembly target only, clipboard must be non-empty) ---------
	if (clickedAssembly && assemblyNode && !s_clipboard.isEmpty())
	{
		myMenu.addAction(QIcon(":/icons/res/paste.png"), tr("Paste"), this,
		    [this, assemblyNode, &actionTaken]() {
		        actionTaken = true;
		        pasteIntoSelectedNode(assemblyNode);
		    });
	}

	// ---- Import Units (synthetic file node only) ---------------------------
	// SceneNode::importUnit lives on the synthetic per-import file node, not
	// on an assembly/leaf mesh within it - see LengthUnits.h's own doc
	// comment for the resolution order this sets the first link of. Reached
	// here (not from Mass Properties/Surface Analysis themselves) because a
	// unit is a property of one imported FILE, not of whatever multi-mesh
	// selection happens to be open in one of those dialogs at the time.
	if (clickedAssembly && assemblyNode && assemblyNode->isSynthetic)
	{
		// Deliberately does NOT set actionTaken - this action never touches
		// the mesh selection (unlike the expandThen()-wrapped actions below,
		// or Select Parent above, which both deliberately establish a new
		// real selection). Leaving actionTaken false lets the "no action
		// taken" path at the bottom of this function restore the real
		// pre-click selection (savedSelection), undoing
		// highlightSingleItemAt()'s purely-visual, signal-blocked narrowing
		// above - without this, the tree/viewport are left in that
		// transient single-item state with no real selection underneath it,
		// so a later empty-viewport click has nothing valid to clear.
		myMenu.addAction(QIcon(":/icons/res/import_units.png"), tr("Import Units..."), this, [this, nodeUuid = assemblyNode->nodeUuid]() {
			if (SceneNode* fileNode = _sceneGraph->findNodeByUuid(nodeUuid))
				showImportUnitsDialog(fileNode);
		});
		myMenu.addSeparator();
	}

	// ---- Mesh operations ---------------------------------------------------
	if (hasMeshes)
	{
		myMenu.addSeparator();
		myMenu.addAction(QIcon(":/icons/res/center_screen.png"), tr("Center Screen"), this, expandThen([this]() { centerScreen(); }));
		myMenu.addAction(QIcon(":/icons/res/transformations.png"), tr("Transformations"), this, expandThen([this]() { showTransformationsPage(); }));
		myMenu.addAction(QIcon(":/icons/res/material.png"), tr("Edit Material"), this, expandThen([this]() { editMeshMaterial(); }));
		myMenu.addSeparator();
		myMenu.addAction(QIcon(":/icons/res/hide.png"), tr("Hide"), this, expandThen([this]() { hideSelectedItems(); }));
		myMenu.addAction(QIcon(":/icons/res/show.png"), tr("Show"), this, expandThen([this]() { showSelectedItems(); }));
		myMenu.addAction(QIcon(":/icons/res/show_only.png"), tr("Show Only"), this, expandThen([this]() { showOnlySelectedItems(); }));
		myMenu.addSeparator();
		// Duplicate is deliberately a leaf-only shortcut (see c5686ad's commit
		// message): it clones each mesh flatly back into its own existing
		// parent, with no new mirrored assembly subtree - Copy+Paste is the
		// hierarchy-preserving path for assembly-level duplication. Split/
		// Merge have no such concern (neither reproduces assembly structure -
		// they just insert/combine specific meshes wherever they already
		// live), so unlike Duplicate they're available for assembly clicks too.
		// A single-mesh assembly is the one exception: it's a redundant
		// wrapper around exactly one mesh, so Duplicate sees through it the
		// same way the ToolsToolbar's Duplicate button already does - only a
		// genuine multi-mesh assembly still hides this entry.
		if (!clickedAssembly || singleMeshAssembly)
			myMenu.addAction(QIcon(":/icons/res/duplicate_meshes.png"), tr("Duplicate"), this, expandThen([this]() { duplicateSelectedItems(); }));
		myMenu.addAction(QIcon(":/icons/res/split_by_connectivity.png"), tr("Split by Connectivity"), this, expandThen([this]() { splitSelectedMeshesByConnectivity(); }));
		myMenu.addAction(QIcon(":/icons/res/merge_by_adjacency.png"), tr("Merge by Adjacency"), this, expandThen([this]() { mergeSelectedMeshesByAdjacency(); }));
		myMenu.addAction(QIcon(":/icons/res/merge_selected.png"), tr("Merge Selected"), this, expandThen([this]() { mergeSelectedMeshes(); }));
		myMenu.addAction(QIcon(":/icons/res/mesh_union.png"), tr("Mesh Union"), this, expandThen([this]() { unionSelectedMeshes(); }));
		myMenu.addAction(QIcon(":/icons/res/group_meshes.png"), tr("Group"), this, expandThen([this]() { groupSelectedMeshes(); }));
		myMenu.addAction(QIcon(":/icons/res/delete.png"), tr("Delete"), this, expandThen([this]() { deleteSelectedItems(); }));
		myMenu.addSeparator();
		myMenu.addAction(QIcon(":/icons/res/mesh_info.png"), tr("Mesh Info"), this, expandThen([this]() { displaySelectedMeshInfo(); }));
	}

	myMenu.exec(treeWidgetModel->mapMenuToGlobal(pos));

	// If no action was taken (menu dismissed), restore the previous selection.
	if (!actionTaken)
		setSelectionWithoutUndo(savedSelection);
}

void ModelViewer::centerScreen()
{
	std::vector<int> selectedIDs = getSelectedIDs();
	_viewportWidget->centerScreen(selectedIDs);
}

void ModelViewer::copySelectedItems()
{
	// Starting a new Copy always resets the clipboard outright (same as any
	// OS clipboard - a new Copy replaces whatever was there before,
	// regardless of which document it came from).
	s_clipboard.clear();
	s_clipboardSourceViewer = this;
	++s_clipboardGeneration;

	// Collect selected assemblies (deduplicated against each other below)
	QList<const SceneNode*> assemblies = treeWidgetModel->selectedAssemblyNodes();
	QList<QUuid> leafUuids = treeWidgetModel->selectedMeshUuids();

	// Build set of all mesh UUIDs covered by selected assemblies so we can
	// skip leaf entries that are already inside a copied subtree.
	QSet<QUuid> coveredByAssembly;
	for (const SceneNode* node : assemblies)
	{
		for (const QUuid& uuid : _sceneGraph->collectMeshUuids(node))
			coveredByAssembly.insert(uuid);
	}

	// Also skip assemblies that are descendants of another selected assembly.
	// Build a set of all assembly node UUIDs for quick ancestor lookup.
	QSet<QUuid> selectedAssemblyUuids;
	for (const SceneNode* node : assemblies)
		selectedAssemblyUuids.insert(node->nodeUuid);

	auto hasSelectedAncestor = [&](const SceneNode* node) -> bool {
		for (const SceneNode* p = node->parent; p; p = p->parent)
			if (selectedAssemblyUuids.contains(p->nodeUuid))
				return true;
		return false;
	};

	// Add top-level assembly entries
	for (const SceneNode* node : assemblies)
	{
		if (hasSelectedAncestor(node))
			continue;  // covered by a higher selected assembly

		ClipboardEntry entry;
		entry.isLeaf       = false;
		entry.assemblyRoot = snapshotSceneNode(node);
		s_clipboard.append(entry);
	}

	// Add standalone leaf entries (not covered by any selected assembly)
	for (const QUuid& uuid : leafUuids)
	{
		if (coveredByAssembly.contains(uuid))
			continue;

		ClipboardEntry entry;
		entry.isLeaf   = true;
		entry.leafUuid = uuid;
		s_clipboard.append(entry);
	}
}

void ModelViewer::cutSelectedItems()
{
	// Same deduplication logic as copySelectedItems, but entries are
	// tagged isCut=true and carry source location UUIDs.
	s_clipboard.clear();
	s_clipboardSourceViewer = this;
	++s_clipboardGeneration;

	QList<const SceneNode*> assemblies = treeWidgetModel->selectedAssemblyNodes();
	QList<QUuid>            leafUuids  = treeWidgetModel->selectedMeshUuids();

	QSet<QUuid> coveredByAssembly;
	for (const SceneNode* node : assemblies)
		for (const QUuid& uuid : _sceneGraph->collectMeshUuids(node))
			coveredByAssembly.insert(uuid);

	QSet<QUuid> selectedAssemblyUuids;
	for (const SceneNode* node : assemblies)
		selectedAssemblyUuids.insert(node->nodeUuid);

	auto hasSelectedAncestor = [&](const SceneNode* node) -> bool {
		for (const SceneNode* p = node->parent; p; p = p->parent)
			if (selectedAssemblyUuids.contains(p->nodeUuid))
				return true;
		return false;
	};

	QSet<QUuid> cutMeshUuids;
	QSet<QUuid> cutNodeUuids;

	for (const SceneNode* node : assemblies)
	{
		if (hasSelectedAncestor(node))
			continue;

		ClipboardEntry entry;
		entry.isLeaf             = false;
		entry.isCut              = true;
		entry.cutNodeUuid        = node->nodeUuid;
		entry.cutSourceNodeUuid  = node->parent ? node->parent->nodeUuid : QUuid();
		entry.cutSourcePosition  = node->parent
		    ? node->parent->children.indexOf(const_cast<SceneNode*>(node))
		    : 0;
		s_clipboard.append(entry);

		cutNodeUuids.insert(node->nodeUuid);
		for (const QUuid& uuid : _sceneGraph->collectMeshUuids(node))
			cutMeshUuids.insert(uuid);
	}

	for (const QUuid& uuid : leafUuids)
	{
		if (coveredByAssembly.contains(uuid))
			continue;

		SceneNode* owner = _sceneGraph->findNodeForMesh(uuid);

		ClipboardEntry entry;
		entry.isLeaf            = true;
		entry.isCut             = true;
		entry.leafUuid          = uuid;
		entry.cutSourceNodeUuid = owner ? owner->nodeUuid : QUuid();
		entry.cutSourcePosition = owner ? owner->meshUuids.indexOf(uuid) : 0;
		s_clipboard.append(entry);

		cutMeshUuids.insert(uuid);
	}

	if (s_clipboard.isEmpty())
		return;

	treeWidgetModel->markAsCut(cutMeshUuids, cutNodeUuids);
	_undoStack->push(new CutCommand(this, _viewportWidget, s_clipboard,
	                                cutMeshUuids, cutNodeUuids));
}

void ModelViewer::clearCutMarks(quint64 generation)
{
	// No-op if some other document has since taken over the clipboard
	// (Copy/Cut bumps s_clipboardGeneration) - an Undo/Redo reaching this
	// call from deep in a stale command must not clobber whichever
	// document's clipboard is actually current now.
	if (generation != s_clipboardGeneration)
		return;

	s_clipboard.clear();
	treeWidgetModel->clearCutMarks();
}

void ModelViewer::reapplyCutMarks(quint64 generation,
                                  const QList<ClipboardEntry>& entries,
                                  const QSet<QUuid>&           meshUuids,
                                  const QSet<QUuid>&           nodeUuids)
{
	if (generation != s_clipboardGeneration)
		return;

	s_clipboard = entries;
	treeWidgetModel->markAsCut(meshUuids, nodeUuids);
}

void ModelViewer::validateCutClipboard()
{
	// The clipboard may currently belong to a different document (Copy/Cut
	// ran there, not here) - only that document's own structural changes
	// are allowed to invalidate it. Without this, editing ANY document
	// would wipe out a cut pending in some other, unrelated document.
	if (s_clipboardSourceViewer.data() != this)
		return;

	if (s_clipboard.isEmpty())
		return;

	// Only validate cut-mode clipboard entries.
	bool anyCut = false;
	for (const ClipboardEntry& e : s_clipboard)
		if (e.isCut) { anyCut = true; break; }

	if (!anyCut)
		return;

	for (const ClipboardEntry& entry : s_clipboard)
	{
		if (!entry.isCut)
			continue;

		if (entry.isLeaf)
		{
			if (!_sceneGraph->findNodeForMesh(entry.leafUuid))
			{
				invalidateCutClipboard();
				return;
			}
		}
		else
		{
			if (!_sceneGraph->findNodeByUuid(entry.cutNodeUuid))
			{
				invalidateCutClipboard();
				return;
			}
		}
	}
}

void ModelViewer::validateVariantData()
{
	if (!_sceneGraph || !_viewportWidget)
		return;

	const QStringList files = _sceneGraph->filesWithVariants();
	if (files.isEmpty())
		return;

	const std::vector<SceneMesh*> meshes = _viewportWidget->getMeshStore();

	for (const QString& sourceFile : files)
	{
		// A mesh counts as live only if it matches the source file AND is
		// still registered in the SceneGraph. Meshes removed by a delete
		// command are unregistered from the graph immediately but may linger
		// in the store until the deferred cleanup pass runs.
		const bool hasLiveMesh = std::any_of(meshes.begin(), meshes.end(),
			[&](SceneMesh* m)
			{
				return m
				    && m->getSourceFile() == sourceFile
				    && _sceneGraph->findNodeForMesh(m->uuid()) != nullptr;
			});

		if (!hasLiveMesh)
			_sceneGraph->clearVariantData(sourceFile);
	}
}

void ModelViewer::validateAnimationData()
{
	if (!_sceneGraph || !_viewportWidget)
		return;

	const QStringList files = _sceneGraph->filesWithAnimations();
	if (files.isEmpty())
		return;

	const std::vector<SceneMesh*> meshes = _viewportWidget->getMeshStore();
	for (const QString& sourceFile : files)
	{
		const bool hasLiveMesh = std::any_of(meshes.begin(), meshes.end(),
			[&](SceneMesh* mesh)
			{
				return mesh
					&& mesh->getSourceFile() == sourceFile
					&& _sceneGraph->findNodeForMesh(mesh->uuid()) != nullptr;
			});

		if (!hasLiveMesh)
		{
			_sceneGraph->clearAnimationData(sourceFile);
			// Also drop ViewportWidget's cached runtime (default transforms, UUID
			// lookup tables) for this file and stop playback if it was active.
			_viewportWidget->clearAnimationRuntimeForFile(sourceFile);
		}
	}
}

void ModelViewer::validateCameraData()
{
	if (!_sceneGraph || !_viewportWidget)
		return;

	const QStringList files = _sceneGraph->filesWithGltfCameras();
	const std::vector<SceneMesh*>& meshes = _viewportWidget->getMeshStore();

	for (const QString& sourceFile : files)
	{
		const bool hasLiveMesh = std::any_of(meshes.cbegin(), meshes.cend(),
			[&](SceneMesh* mesh)
			{
				return mesh
					&& mesh->getSourceFile() == sourceFile
					&& _sceneGraph->findNodeForMesh(mesh->uuid()) != nullptr;
			});

		if (!hasLiveMesh)
		{
			// If the active glTF camera belongs to this file, revert to system camera.
			if (_viewportWidget->activeGltfCameraFile() == sourceFile)
				_viewportWidget->resetToSystemCamera();

			_sceneGraph->clearGltfCameraData(sourceFile);
		}
	}
}

void ModelViewer::validateLightData()
{
	if (!_sceneGraph || !_viewportWidget)
		return;

	const QStringList files = _sceneGraph->filesWithLights();
	const std::vector<SceneMesh*>& meshes = _viewportWidget->getMeshStore();

	for (const QString& sourceFile : files)
	{
		const bool hasLiveMesh = std::any_of(meshes.cbegin(), meshes.cend(),
			[&](SceneMesh* mesh)
			{
				return mesh
					&& mesh->getSourceFile() == sourceFile
					&& _sceneGraph->findNodeForMesh(mesh->uuid()) != nullptr;
			});

		if (!hasLiveMesh)
			_sceneGraph->clearLightData(sourceFile);
	}
}

void ModelViewer::invalidateCutClipboard()
{
	s_clipboard.clear();
	treeWidgetModel->clearCutMarks();
}

ClipboardNode ModelViewer::snapshotSceneNode(const SceneNode* n)
{
	ClipboardNode cn;
	cn.name           = n->name;
	cn.localTransform = n->localTransform;
	cn.meshUuids      = n->meshUuids;
	for (const SceneNode* child : n->children)
		cn.children.append(snapshotSceneNode(child));
	return cn;
}

SceneNode* ModelViewer::cloneClipboardSubtree(const ClipboardNode& cn, SceneNode* parent,
                                              ViewportWidget* srcVp, bool resolveTextures,
                                              QList<QUuid>& allUuids)
{
	SceneNode* node      = new SceneNode();
	node->nodeUuid       = QUuid::createUuid();
	node->name           = cn.name;
	node->localTransform = cn.localTransform;
	node->parent         = parent;

	for (const QUuid& srcUuid : cn.meshUuids)
	{
		SceneMesh* original = srcVp->getMeshByUuid(srcUuid);
		if (!original) continue;

		SceneMesh* clone = original->clone();
		clone->setName(_viewportWidget->generateUniqueMeshName(original->getName()));
		_viewportWidget->addToDisplay(clone);
		if (resolveTextures)
		{
			const Material resolved = ViewportWidget::resolveMaterialTextures(_viewportWidget, clone->getMaterial());
			clone->setTextureMaps(resolved);
		}

		node->meshUuids.append(clone->uuid());
		allUuids.append(clone->uuid());
	}

	for (const ClipboardNode& childCn : cn.children)
		node->children.append(cloneClipboardSubtree(childCn, node, srcVp, resolveTextures, allUuids));

	return node;
}

void ModelViewer::pasteIntoSelectedNode(const SceneNode* targetNode)
{
	if (s_clipboard.isEmpty() || !targetNode)
		return;

	// true when the clipboard's UUIDs resolve against a DIFFERENT document
	// than this one (Copy/Cut ran there, Paste is running here).
	const bool crossDoc = s_clipboardSourceViewer && s_clipboardSourceViewer.data() != this;

	QApplication::setOverrideCursor(Qt::WaitCursor);

	const bool isCutPaste = s_clipboard.first().isCut;
	const QSet<QUuid> originalSelection = getSelectedUuids();
	QList<PasteCommand::PastedItem> items;

	// Cast away const — targetNode is owned by SceneGraph and will remain
	// valid for the lifetime of this command.
	SceneNode* target = const_cast<SceneNode*>(targetNode);

	if (isCutPaste && crossDoc)
	{
		// Cross-document cut+paste is structurally different enough (source
		// and destination are two different SceneGraphs/undo stacks) that it
		// doesn't fit this function's same-document move logic below - see
		// performCrossDocumentCutPaste()'s own doc comment.
		performCrossDocumentCutPaste(target);
		QApplication::restoreOverrideCursor();
		return;
	}

	if (isCutPaste)
	{
		// ---- Cut-paste: move items within the scene (no cloning) -----------

		// Validate all sources before touching anything.
		for (const ClipboardEntry& entry : s_clipboard)
		{
			if (entry.isLeaf)
			{
				if (!_sceneGraph->findNodeForMesh(entry.leafUuid))
				{
					invalidateCutClipboard();
					QApplication::restoreOverrideCursor();
					return;
				}
			}
			else
			{
				if (!_sceneGraph->findNodeByUuid(entry.cutNodeUuid))
				{
					invalidateCutClipboard();
					QApplication::restoreOverrideCursor();
					return;
				}
			}
		}

		// Snapshot and clear clipboard before executing moves so that
		// structureChanged signals fired mid-move don't trigger validateCutClipboard
		// with a stale clipboard while items are temporarily un-registered.
		const QList<ClipboardEntry> cutEntries = s_clipboard;
		s_clipboard.clear();

		for (const ClipboardEntry& entry : cutEntries)
		{
			if (entry.isLeaf)
			{
				int srcPos = 0;
				SceneNode* srcOwner = _sceneGraph->removeMeshUuid(entry.leafUuid, srcPos);
				const int dstPos = target->meshUuids.size();
				_sceneGraph->restoreMeshUuid(target, entry.leafUuid, dstPos);

				PasteCommand::PastedItem item;
				item.type            = PasteCommand::PastedItem::Mesh;
				item.isCut           = true;
				item.meshUuid        = entry.leafUuid;
				item.ownerNode       = target;
				item.meshPosition    = dstPos;
				item.srcOwnerNode    = srcOwner;
				item.srcMeshPosition = srcPos;
				items.append(item);
			}
			else
			{
				SceneNode* subtree  = _sceneGraph->findNodeByUuid(entry.cutNodeUuid);
				SceneNode* srcParent = subtree->parent; // capture before removeChildNode clears it
				int srcPos = 0;
				_sceneGraph->removeChildNode(srcParent, subtree, srcPos);
				const int dstPos = target->children.size();
				_sceneGraph->insertChildNode(target, subtree, dstPos);

				PasteCommand::PastedItem item;
				item.type               = PasteCommand::PastedItem::Subtree;
				item.isCut              = true;
				item.subtreeRoot        = subtree;
				item.subtreeParent      = target;
				item.childPosition      = dstPos;
				item.subtreeMeshUuids   = _sceneGraph->collectMeshUuids(subtree);
				item.srcSubtreeParent   = srcParent;
				item.srcChildPosition   = srcPos;
				items.append(item);
			}
		}

		if (!items.isEmpty())
		{
			_viewportWidget->updateView();
			updateDisplayList();
			_undoStack->push(new PasteCommand(this, _viewportWidget, items,
			                                  originalSelection, cutEntries));
			// Clear marks AFTER the command is pushed (command holds its own copy).
			clearCutMarks(s_clipboardGeneration);
		}
	}
	else
	{
		// ---- Copy-paste: clone meshes and insert as new items --------------
		// srcVp resolves the ORIGINAL mesh being cloned FROM - the source
		// document's viewport for a cross-document paste, otherwise just
		// this document's own (same as before this existed). Everything
		// else (clone(), addToDisplay(), _sceneGraph inserts, the
		// PasteCommand push) stays targeted at THIS document/target
		// unchanged - GL contexts are shared app-wide (Qt::AA_ShareOpenGLContexts),
		// so a mesh cloned from another document's context is already usable
		// here with no extra work beyond the texture re-resolve below.
		ViewportWidget* srcVp = crossDoc ? s_clipboardSourceViewer->getViewportWidget()
		                                 : _viewportWidget;

		// clone() and addToDisplay() both require a current GL context.
		_viewportWidget->makeCurrent();

		for (const ClipboardEntry& entry : s_clipboard)
		{
			if (entry.isLeaf)
			{
				SceneMesh* original = srcVp->getMeshByUuid(entry.leafUuid);
				if (!original) continue;

				SceneMesh* clone = original->clone();
				clone->setName(_viewportWidget->generateUniqueMeshName(original->getName()));
				_viewportWidget->addToDisplay(clone);
				if (crossDoc)
				{
					const Material resolved = ViewportWidget::resolveMaterialTextures(_viewportWidget, clone->getMaterial());
					clone->setTextureMaps(resolved);
				}

				const QUuid newUuid  = clone->uuid();
				const int insertPos  = target->meshUuids.size();
				_sceneGraph->restoreMeshUuid(target, newUuid, insertPos);

				PasteCommand::PastedItem item;
				item.type         = PasteCommand::PastedItem::Mesh;
				item.meshUuid     = newUuid;
				item.ownerNode    = target;
				item.meshPosition = insertPos;
				items.append(item);
			}
			else
			{
				QList<QUuid> allUuids;
				SceneNode* clonedRoot = cloneClipboardSubtree(entry.assemblyRoot, target, srcVp, crossDoc, allUuids);
				const int childPos    = target->children.size();
				_sceneGraph->insertChildNode(target, clonedRoot, childPos);

				PasteCommand::PastedItem item;
				item.type             = PasteCommand::PastedItem::Subtree;
				item.subtreeRoot      = clonedRoot;
				item.subtreeParent    = target;
				item.childPosition    = childPos;
				item.subtreeMeshUuids = allUuids;
				items.append(item);
			}
		}

		_viewportWidget->doneCurrent();

		if (!items.isEmpty())
		{
			updateDisplayList();
			_undoStack->push(new PasteCommand(this, _viewportWidget, items,
			                                  originalSelection));
		}
	}

	QApplication::restoreOverrideCursor();
}

void ModelViewer::performCrossDocumentCutPaste(SceneNode* target)
{
	// Snapshot before any early return clears it - s_clipboard/
	// s_clipboardSourceViewer are cleared unconditionally before we return,
	// same as invalidateCutClipboard() does for the same-document case.
	ModelViewer* src = s_clipboardSourceViewer.data();
	if (!src)
	{
		s_clipboard.clear();
		MainWindow::showStatusMessage(tr("Paste failed: the document this was cut from has since been closed."), 4000);
		return;
	}

	ViewportWidget* srcVp = src->getViewportWidget();
	const QSet<QUuid> originalSelection = getSelectedUuids();
	QList<PasteCommand::PastedItem> items;
	QVector<QUuid> srcUuids;   // leaf UUIDs actually removed from the source
	bool anyAssemblyFellBackToCopy = false;

	_viewportWidget->makeCurrent();

	for (const ClipboardEntry& entry : s_clipboard)
	{
		if (entry.isLeaf)
		{
			SceneMesh* original = srcVp->getMeshByUuid(entry.leafUuid);
			if (!original)
				continue;

			SceneMesh* clone = original->clone();
			clone->setName(_viewportWidget->generateUniqueMeshName(original->getName()));
			_viewportWidget->addToDisplay(clone);

			const Material resolved = ViewportWidget::resolveMaterialTextures(_viewportWidget, clone->getMaterial());
			clone->setTextureMaps(resolved);

			const QUuid newUuid = clone->uuid();
			const int insertPos = target->meshUuids.size();
			_sceneGraph->restoreMeshUuid(target, newUuid, insertPos);

			PasteCommand::PastedItem item;
			item.type         = PasteCommand::PastedItem::Mesh;
			item.meshUuid     = newUuid;
			item.ownerNode    = target;
			item.meshPosition = insertPos;
			items.append(item);

			srcUuids.append(entry.leafUuid);
		}
		else
		{
			// Nothing in SceneGraph can detach/clean up an emptied-out
			// ASSEMBLY node's wrapper the way detachEmptyFileNode() does
			// for synthetic file nodes, so this can't be genuinely MOVED
			// yet - fall back to copy semantics instead of rejecting the
			// whole paste: clone it into the target and leave the source
			// assembly exactly as it was (not cut, still fully intact).
			//
			// entry.assemblyRoot is NOT usable here - cutSelectedItems()
			// only ever populates cutNodeUuid/cutSourceNodeUuid/
			// cutSourcePosition for an assembly entry (everything the
			// same-document move-by-pointer path needs), never
			// assemblyRoot (only copySelectedItems() populates that).
			// Using it directly clones an empty, default-constructed
			// ClipboardNode - an empty node with nothing in it silently
			// gets inserted, which looks exactly like "paste did nothing".
			// Build the snapshot on demand instead, from the still-live
			// source node (cut items stay in the scene, just grayed out,
			// until paste actually completes).
			SceneNode* liveSourceNode = src->sceneGraph()->findNodeByUuid(entry.cutNodeUuid);
			if (!liveSourceNode)
				continue;

			anyAssemblyFellBackToCopy = true;

			QList<QUuid> allUuids;
			SceneNode* clonedRoot = cloneClipboardSubtree(snapshotSceneNode(liveSourceNode), target, srcVp, true, allUuids);
			const int childPos    = target->children.size();
			_sceneGraph->insertChildNode(target, clonedRoot, childPos);

			PasteCommand::PastedItem item;
			item.type             = PasteCommand::PastedItem::Subtree;
			item.subtreeRoot      = clonedRoot;
			item.subtreeParent    = target;
			item.childPosition    = childPos;
			item.subtreeMeshUuids = allUuids;
			items.append(item);
		}
	}

	_viewportWidget->doneCurrent();

	if (!items.isEmpty())
	{
		updateDisplayList();
		// Two independent commands, one per document's own QUndoStack -
		// there is no cross-stack undo mechanism in this codebase, so a
		// cross-document move can only ever be two separately-undoable
		// halves. Text on both makes that explicit to the user. If nothing
		// leaf-level was actually cut (assembly-only selection), skip the
		// source-side delete entirely - there's nothing to remove there.
		_undoStack->push(new PasteCommand(this, _viewportWidget, items,
		                                  originalSelection, {},
		                                  tr("Paste (moved from another document)")));
		if (!srcUuids.isEmpty())
		{
			src->getUndoStack()->push(new DeleteMeshCommand(src, srcVp, srcUuids,
			                                  tr("Cut (moved to another document)")));
		}
	}

	// clearCutMarks() clears s_clipboard itself (guarded by generation match,
	// which still holds - nothing else has touched the clipboard since).
	src->clearCutMarks(s_clipboardGeneration);
	s_clipboardSourceViewer = nullptr;

	if (anyAssemblyFellBackToCopy)
	{
		// A status message rather than a modal QMessageBox - this function
		// runs from inside a context menu action's triggered handler (that
		// QMenu's own exec() is still unwinding), and popping a modal
		// dialog there - even deferred via QTimer::singleShot(0) - was
		// observed to interfere with the paste that had just completed.
		MainWindow::showStatusMessage(
			tr("Pasted group as a copy - moving a whole group into a different document isn't supported yet, so the original was left in place."),
			5000);
	}
}

void ModelViewer::duplicateSelectedItems()
{
	const QList<QUuid> selectedUuids = treeWidgetModel->selectedMeshUuids();
	if (selectedUuids.isEmpty())
		return;

	QApplication::setOverrideCursor(Qt::WaitCursor);

	const QSet<QUuid> originalSelection(selectedUuids.begin(), selectedUuids.end());

	QVector<DuplicateCommand::DuplicateEntry> entries;

	_viewportWidget->makeCurrent();
	for (const QUuid& srcUuid : selectedUuids)
	{
		SceneNode* ownerNode = _sceneGraph->findNodeForMesh(srcUuid);
		if (!ownerNode)
			continue;

		SceneMesh* original = _viewportWidget->getMeshByUuid(srcUuid);
		if (!original)
			continue;

		SceneMesh* clone = original->clone();
		clone->setName(_viewportWidget->generateUniqueMeshName(original->getName()));
		_viewportWidget->addToDisplay(clone);

		const QUuid newUuid  = clone->uuid();
		const int insertPos  = ownerNode->meshUuids.size();
		_sceneGraph->restoreMeshUuid(ownerNode, newUuid, insertPos);

		DuplicateCommand::DuplicateEntry e;
		e.uuid      = newUuid;
		e.ownerNode = ownerNode;
		e.position  = insertPos;
		entries.append(e);
	}
	_viewportWidget->doneCurrent();

	if (!entries.isEmpty())
	{
		updateDisplayList();
		_undoStack->push(new DuplicateCommand(
		    this, _viewportWidget, entries, originalSelection));
	}

	QApplication::restoreOverrideCursor();
}

void ModelViewer::splitSelectedMeshesByConnectivity()
{
	const QList<QUuid> selectedUuids = treeWidgetModel->selectedMeshUuids();
	if (selectedUuids.isEmpty())
		return;

	QApplication::setOverrideCursor(Qt::WaitCursor);

	const QSet<QUuid> originalSelection(selectedUuids.begin(), selectedUuids.end());

	QVector<SplitByConnectivityCommand*> commands;
	int fragmentCount = 0;
	int alreadyConnectedCount = 0;

	_viewportWidget->makeCurrent();
	for (const QUuid& srcUuid : selectedUuids)
	{
		SceneNode* ownerNode = _sceneGraph->findNodeForMesh(srcUuid);
		if (!ownerNode)
			continue;

		SceneMesh* original = _viewportWidget->getMeshByUuid(srcUuid);
		if (!original)
			continue;

		const std::vector<std::vector<int>> groups = original->findConnectedTriangleGroups();
		if (groups.size() <= 1)
		{
			++alreadyConnectedCount;
			continue;
		}

		// Build every fragment before touching the original's tree/store
		// placement, while its vertex/index data is still trivially readable.
		QVector<QUuid> fragmentUuids;
		fragmentUuids.reserve(static_cast<int>(groups.size()));
		for (size_t i = 0; i < groups.size(); ++i)
		{
			const QString fragmentName = _viewportWidget->generateUniqueMeshName(
				QString("%1_%2").arg(original->getName()).arg(i + 1));
			SceneMesh* fragment = original->extractFragment(groups[i], fragmentName);
			_viewportWidget->addToDisplay(fragment);
			fragmentUuids.append(fragment->uuid());
		}

		// Remove the original from the tree and recycle-bin it - capture its
		// tree position first so undo can put it back exactly there.
		int originalPosition = 0;
		_sceneGraph->removeMeshUuid(srcUuid, originalPosition);
		const int originalIndex = _viewportWidget->getIndexByUuid(srcUuid);
		if (originalIndex >= 0)
			_viewportWidget->moveToRecycleBin(srcUuid, originalIndex);

		// Insert the fragments into the tree, as new siblings under the same node.
		QVector<SplitByConnectivityCommand::FragmentEntry> entries;
		entries.reserve(fragmentUuids.size());
		for (const QUuid& fragUuid : fragmentUuids)
		{
			const int insertPos = ownerNode->meshUuids.size();
			_sceneGraph->restoreMeshUuid(ownerNode, fragUuid, insertPos);

			SplitByConnectivityCommand::FragmentEntry e;
			e.uuid     = fragUuid;
			e.position = insertPos;
			entries.append(e);
		}

		commands.append(new SplitByConnectivityCommand(
			this, _viewportWidget, srcUuid, originalPosition, ownerNode, entries, originalSelection));

		fragmentCount += static_cast<int>(groups.size());
	}
	_viewportWidget->doneCurrent();

	if (!commands.isEmpty())
	{
		updateDisplayList();
		if (commands.size() == 1)
		{
			_undoStack->push(commands.first());
		}
		else
		{
			_undoStack->beginMacro(tr("Split by Connectivity"));
			for (SplitByConnectivityCommand* cmd : commands)
				_undoStack->push(cmd);
			_undoStack->endMacro();
		}
	}

	QApplication::restoreOverrideCursor();

	if (commands.isEmpty() && alreadyConnectedCount > 0)
	{
		QMessageBox::information(this, tr("Split by Connectivity"),
			alreadyConnectedCount == 1
				? tr("The selected mesh is already a single connected piece - nothing to split.")
				: tr("All %1 selected meshes are already single connected pieces - nothing to split.").arg(alreadyConnectedCount));
	}
	else if (alreadyConnectedCount > 0)
	{
		MainWindow::showStatusMessage(tr("Split %1 mesh(es) into %2 piece(s); %3 mesh(es) were already a single connected piece.")
			.arg(commands.size()).arg(fragmentCount).arg(alreadyConnectedCount));
	}
	else if (!commands.isEmpty())
	{
		MainWindow::showStatusMessage(tr("Split %1 mesh(es) into %2 piece(s).").arg(commands.size()).arg(fragmentCount));
	}
}

namespace
{
	// Union-Find (path-compressed find, union by nothing-fancy) over a small
	// set of indices - the user's selection, never large enough to need
	// union-by-rank.
	int ufFind(std::vector<int>& parent, int x)
	{
		while (parent[x] != x)
		{
			parent[x] = parent[parent[x]];
			x = parent[x];
		}
		return x;
	}

	void ufUnite(std::vector<int>& parent, int a, int b)
	{
		a = ufFind(parent, a);
		b = ufFind(parent, b);
		if (a != b)
			parent[a] = b;
	}

	// Quantized world-space vertex-position keys for one mesh, one entry per
	// vertex - used to detect adjacency ACROSS separate mesh objects (unlike
	// SceneMesh::buildPositionWeldMap(), which welds vertices WITHIN one
	// mesh). Same quantize-then-FNV-1a-hash tolerance (eps = 1e-4) that
	// function uses, applied to getTrsfPoints() (world-space, not local) so
	// two separately-authored pieces meant to share a boundary vertex are
	// detected despite living under different transforms.
	std::unordered_set<int64_t> meshWorldPositionKeys(SceneMesh* mesh)
	{
		std::unordered_set<int64_t> keys;
		if (!mesh)
			return keys;

		const std::vector<float>& pts = mesh->getTrsfPoints();
		const size_t nVerts = pts.size() / 3;
		keys.reserve(nVerts);

		constexpr float kEps = 1e-4f;
		auto quantize = [](float v) -> int64_t { return static_cast<int64_t>(std::llround(v / kEps)); };

		for (size_t i = 0; i < nVerts; ++i)
		{
			const int64_t qx = quantize(pts[i * 3]);
			const int64_t qy = quantize(pts[i * 3 + 1]);
			const int64_t qz = quantize(pts[i * 3 + 2]);
			uint64_t h = 1469598103934665603ull;  // FNV-1a offset basis
			for (int64_t v : { qx, qy, qz })
			{
				h ^= static_cast<uint64_t>(v);
				h *= 1099511628211ull;  // FNV-1a prime
			}
			keys.insert(static_cast<int64_t>(h));
		}
		return keys;
	}

	bool meshesShareAVertex(const std::unordered_set<int64_t>& a, const std::unordered_set<int64_t>& b)
	{
		const std::unordered_set<int64_t>& smaller = a.size() <= b.size() ? a : b;
		const std::unordered_set<int64_t>& larger  = a.size() <= b.size() ? b : a;
		for (int64_t key : smaller)
		{
			if (larger.count(key))
				return true;
		}
		return false;
	}

	// groupIndicesByMaterial() used to live here - promoted to include/MaterialGrouping.h +
	// src/MaterialGrouping.cpp so Filter by Material and the material eyedropper can call it
	// too, not just this file's merge/union paths.
}

void ModelViewer::mergeSelectedMeshesByAdjacency()
{
	const QList<QUuid> selectedUuids = treeWidgetModel->selectedMeshUuids();
	if (selectedUuids.isEmpty())
		return;

	if (selectedUuids.size() < 2)
		return;

	const QSet<QUuid> originalSelection(selectedUuids.begin(), selectedUuids.end());

	QApplication::setOverrideCursor(Qt::WaitCursor);

	QVector<SceneMesh*> meshes;
	QVector<SceneNode*> ownerNodes;
	QVector<QUuid> uuids;
	for (const QUuid& u : selectedUuids)
	{
		SceneNode* node = _sceneGraph->findNodeForMesh(u);
		SceneMesh* mesh = _viewportWidget->getMeshByUuid(u);
		if (!node || !mesh)
			continue;
		meshes.append(mesh);
		ownerNodes.append(node);
		uuids.append(u);
	}

	const int n = meshes.size();
	if (n < 2)
	{
		QApplication::restoreOverrideCursor();
		return;
	}

	QVector<std::unordered_set<int64_t>> keySets(n);
	for (int i = 0; i < n; ++i)
		keySets[i] = meshWorldPositionKeys(meshes[i]);

	std::vector<int> parent(n);
	std::iota(parent.begin(), parent.end(), 0);
	for (int i = 0; i < n; ++i)
	{
		for (int j = i + 1; j < n; ++j)
		{
			if (meshesShareAVertex(keySets[i], keySets[j]))
				ufUnite(parent, i, j);
		}
	}

	std::unordered_map<int, std::vector<int>> clusters;
	for (int i = 0; i < n; ++i)
		clusters[ufFind(parent, i)].push_back(i);

	// Classify each touching (size > 1) cluster before touching the scene:
	// compatible clusters (same source file, tracked original material
	// index, and primitive mode across every member - an untracked/-1 index
	// never counts as "matching", the safe, conservative default) merge
	// unconditionally; mismatched ones need the user's say-so below, since
	// merging them means cascading one mesh's material onto the others.
	std::vector<std::vector<int>> compatibleGroups;
	std::vector<std::vector<int>> mismatchedGroups;
	for (auto& [root, members] : clusters)
	{
		if (members.size() <= 1)
			continue;

		SceneMesh* refMesh = meshes[members[0]];
		bool compatible = refMesh->getOriginalMaterialIndex() >= 0;
		for (size_t k = 1; compatible && k < members.size(); ++k)
		{
			SceneMesh* m = meshes[members[k]];
			compatible = (m->getPrimitiveMode() == refMesh->getPrimitiveMode()) &&
			             (m->getOriginalMaterialIndex() == refMesh->getOriginalMaterialIndex()) &&
			             (m->getSourceFile() == refMesh->getSourceFile());
		}

		if (compatible)
			compatibleGroups.push_back(members);
		else
			mismatchedGroups.push_back(members);
	}

	// Ask once, up front, how the user wants every mismatched (touching-but-multi-material)
	// cluster handled - only bother asking if there's actually one to decide about. Three-way
	// choice instead of the old Yes/No: Mesh Union/Merge is a legitimate real-boolean-op
	// workflow that sometimes genuinely wants one blob regardless of material (Cascade), so
	// that path stays available - Keep Materials Separate is the new, additional option that
	// never loses material info, splitting each mismatched cluster into one merge per material
	// instead of cascading the first mesh's material onto the whole cluster.
	enum class MismatchedClusterChoice { Skip, CascadeMaterial, KeepSeparate };
	MismatchedClusterChoice mismatchedChoice = MismatchedClusterChoice::Skip;
	if (!mismatchedGroups.empty())
	{
		QApplication::restoreOverrideCursor();
		QMessageBox box(this);
		box.setWindowTitle(tr("Merge by Adjacency"));
		box.setText(mismatchedGroups.size() == 1
			? tr("One touching group of selected meshes has different materials.")
			: tr("%1 touching groups of selected meshes have different materials.").arg(mismatchedGroups.size()));
		box.setInformativeText(tr("\"Keep Materials Separate\" splits each group into one merge per "
		                          "material instead of merging everything in it into one, using the "
		                          "first mesh's material."));
		QAbstractButton* keepSeparateButton = box.addButton(tr("Keep Materials Separate"), QMessageBox::ActionRole);
		QAbstractButton* cascadeButton = box.addButton(tr("Merge Anyway"), QMessageBox::ActionRole);
		box.addButton(QMessageBox::Cancel);
		box.setDefaultButton(static_cast<QPushButton*>(keepSeparateButton));
		box.exec();

		if (box.clickedButton() == keepSeparateButton)
			mismatchedChoice = MismatchedClusterChoice::KeepSeparate;
		else if (box.clickedButton() == cascadeButton)
			mismatchedChoice = MismatchedClusterChoice::CascadeMaterial;
		QApplication::setOverrideCursor(Qt::WaitCursor);
	}

	std::vector<std::vector<int>> groupsToMerge = compatibleGroups;
	int cascadedClusterCount = 0;
	int separatedClusterCount = 0;
	int separatedResultGroupCount = 0;
	int separatedSingletonCount = 0;
	for (const std::vector<int>& members : mismatchedGroups)
	{
		if (mismatchedChoice == MismatchedClusterChoice::CascadeMaterial)
		{
			groupsToMerge.push_back(members);
			++cascadedClusterCount;
		}
		else if (mismatchedChoice == MismatchedClusterChoice::KeepSeparate)
		{
			++separatedClusterCount;
			for (const std::vector<int>& subGroup : groupIndicesByMaterial(meshes, members))
			{
				if (subGroup.size() >= 2)
				{
					groupsToMerge.push_back(subGroup);
					++separatedResultGroupCount;
				}
				else
				{
					++separatedSingletonCount;
				}
			}
		}
	}
	const int skippedClusterCount = static_cast<int>(mismatchedGroups.size())
		- cascadedClusterCount - separatedClusterCount;

	QVector<MergeByAdjacencyCommand*> commands;
	int meshesMergedCount = 0;

	_viewportWidget->makeCurrent();
	for (const std::vector<int>& members : groupsToMerge)
	{
		SceneMesh* refMesh = meshes[members[0]];
		QVector<SceneMesh*> clusterMeshes;
		clusterMeshes.reserve(static_cast<int>(members.size()));
		for (int idx : members)
			clusterMeshes.append(meshes[idx]);

		const QString mergedName = _viewportWidget->generateUniqueMeshName(refMesh->getName() + "_Merged");
		SceneMesh* merged = SceneMesh::mergeMeshes(clusterMeshes, mergedName);
		_viewportWidget->addToDisplay(merged);
		const QUuid mergedUuid = merged->uuid();

		// Remove each source from its own tree position and recycle-bin it,
		// capturing its (node, position) for undo.
		QVector<MergeByAdjacencyCommand::SourceEntry> sourceEntries;
		sourceEntries.reserve(static_cast<int>(members.size()));
		for (int idx : members)
		{
			const QUuid& u = uuids[idx];
			int pos = 0;
			_sceneGraph->removeMeshUuid(u, pos);
			const int meshIndex = _viewportWidget->getIndexByUuid(u);
			if (meshIndex >= 0)
				_viewportWidget->moveToRecycleBin(u, meshIndex);

			MergeByAdjacencyCommand::SourceEntry e;
			e.uuid      = u;
			e.ownerNode = ownerNodes[idx];
			e.position  = pos;
			sourceEntries.append(e);
		}

		// Insert the merged mesh under the first source's own node.
		SceneNode* targetNode = ownerNodes[members[0]];
		const int mergedPosition = targetNode->meshUuids.size();
		_sceneGraph->restoreMeshUuid(targetNode, mergedUuid, mergedPosition);

		commands.append(new MergeByAdjacencyCommand(
			this, _viewportWidget, sourceEntries, mergedUuid, targetNode, mergedPosition, originalSelection));

		meshesMergedCount += static_cast<int>(members.size());
	}
	_viewportWidget->doneCurrent();

	if (!commands.isEmpty())
	{
		updateDisplayList();
		if (commands.size() == 1)
		{
			_undoStack->push(commands.first());
		}
		else
		{
			_undoStack->beginMacro(tr("Merge by Adjacency"));
			for (MergeByAdjacencyCommand* cmd : commands)
				_undoStack->push(cmd);
			_undoStack->endMacro();
		}
	}

	QApplication::restoreOverrideCursor();

	if (commands.isEmpty() && (skippedClusterCount > 0 || separatedClusterCount > 0))
	{
		QMessageBox::information(this, tr("Merge by Adjacency"),
			tr("Found %1 touching group(s) of selected meshes with different materials, left unmerged.")
				.arg(skippedClusterCount + separatedClusterCount));
	}
	else if (commands.isEmpty())
	{
		QMessageBox::information(this, tr("Merge by Adjacency"),
			tr("None of the selected meshes are touching - nothing to merge."));
	}
	else
	{
		QString summary = tr("Merged %1 group(s) (%2 meshes total) into %1 mesh(es).")
			.arg(commands.size()).arg(meshesMergedCount);
		QStringList details;
		if (cascadedClusterCount > 0)
			details << tr("%1 mixed-material group(s) used a cascaded material").arg(cascadedClusterCount);
		if (separatedClusterCount > 0)
		{
			details << tr("%1 mixed-material group(s) split into %2 per-material merge(s)%3")
				.arg(separatedClusterCount).arg(separatedResultGroupCount)
				.arg(separatedSingletonCount > 0
					? tr(" (%1 mesh(es) left unmerged - unique material within their group)").arg(separatedSingletonCount)
					: QString());
		}
		if (skippedClusterCount > 0)
			details << tr("%1 mixed-material group(s) left unmerged").arg(skippedClusterCount);
		if (!details.isEmpty())
			summary += tr(" (%1)").arg(details.join(tr("; ")));
		MainWindow::showStatusMessage(summary);
	}
}

// Shared body for mergeSelectedMeshes()/unionSelectedMeshes() - identical in
// every respect except which SceneMesh combining function is used and the
// action's display name (undo text, dialog titles). combineFn is type-
// erased (rather than a plain function pointer, as before
// SceneMesh::booleanUnionMeshes() gained its outUsedRealUnion parameter)
// so unionSelectedMeshes() can wrap that extra parameter in a closure while
// mergeSelectedMeshes() keeps calling the plain 2-argument mergeMeshes() -
// the optional outDetail string lets a combineFn append extra context to
// the final status message (currently only used by unionSelectedMeshes(),
// to say when the real union wasn't possible and plain concatenation ran
// instead - see that function below).
void ModelViewer::combineSelectedMeshes(
	const std::function<SceneMesh*(const QVector<SceneMesh*>&, const QString&, QString* outDetail)>& combineFn,
	const QString& actionName)
{
	const QList<QUuid> selectedUuids = treeWidgetModel->selectedMeshUuids();
	if (selectedUuids.isEmpty())
		return;

	const QSet<QUuid> originalSelection(selectedUuids.begin(), selectedUuids.end());

	QVector<SceneMesh*> meshes;
	QVector<SceneNode*> ownerNodes;
	QVector<QUuid> uuids;
	for (const QUuid& u : selectedUuids)
	{
		SceneNode* node = _sceneGraph->findNodeForMesh(u);
		SceneMesh* mesh = _viewportWidget->getMeshByUuid(u);
		if (!node || !mesh)
			continue;
		meshes.append(mesh);
		ownerNodes.append(node);
		uuids.append(u);
	}

	if (meshes.size() < 2)
		return;

	QApplication::setOverrideCursor(Qt::WaitCursor);

	// Same material-compatibility gate as mergeSelectedMeshesByAdjacency(),
	// applied once to the whole selection instead of per-cluster - see that
	// method's doc comment for why an untracked/-1 material index never
	// counts as "matching".
	SceneMesh* refMesh = meshes.first();
	bool compatible = refMesh->getOriginalMaterialIndex() >= 0;
	for (int i = 1; compatible && i < meshes.size(); ++i)
	{
		SceneMesh* m = meshes[i];
		compatible = (m->getPrimitiveMode() == refMesh->getPrimitiveMode()) &&
		             (m->getOriginalMaterialIndex() == refMesh->getOriginalMaterialIndex()) &&
		             (m->getSourceFile() == refMesh->getSourceFile());
	}

	// Same three-way choice as mergeSelectedMeshesByAdjacency() (see its own doc comment for why
	// "cascade the first mesh's material" stays available rather than being replaced outright -
	// Union in particular is a real boolean op, not just concatenation, and sometimes genuinely
	// wants one blob regardless of material). Only asked when the whole selection doesn't
	// already share one material; groupsToCombine is a single whole-selection group in every
	// other case, matching today's existing single-result behavior exactly.
	std::vector<int> allIndices(meshes.size());
	std::iota(allIndices.begin(), allIndices.end(), 0);
	std::vector<std::vector<int>> groupsToCombine{ allIndices };
	int separatedSingletonCount = 0;

	if (!compatible)
	{
		QApplication::restoreOverrideCursor();
		QMessageBox box(this);
		box.setWindowTitle(actionName);
		box.setText(tr("The selected meshes have different materials."));
		box.setInformativeText(tr("\"Keep Materials Separate\" combines each material's own meshes "
		                          "into its own result instead of combining everything into one, "
		                          "using the first mesh's material."));
		QAbstractButton* keepSeparateButton = box.addButton(tr("Keep Materials Separate"), QMessageBox::ActionRole);
		QAbstractButton* combineButton = box.addButton(actionName + tr(" Anyway"), QMessageBox::ActionRole);
		box.addButton(QMessageBox::Cancel);
		box.setDefaultButton(static_cast<QPushButton*>(keepSeparateButton));
		box.exec();

		if (box.clickedButton() == keepSeparateButton)
		{
			groupsToCombine.clear();
			for (const std::vector<int>& group : groupIndicesByMaterial(meshes, allIndices))
			{
				if (group.size() >= 2)
					groupsToCombine.push_back(group);
				else
					++separatedSingletonCount;
			}
		}
		else if (box.clickedButton() != combineButton)
		{
			QMessageBox::information(this, actionName,
				tr("The selected meshes have different materials - nothing merged."));
			return;
		}
		// else: combineButton clicked - groupsToCombine stays the single whole-selection group,
		// same as today's existing "Yes, merge anyway" behavior.
		QApplication::setOverrideCursor(Qt::WaitCursor);
	}

	_viewportWidget->makeCurrent();

	QVector<MergeByAdjacencyCommand*> commands;
	int meshesCombinedCount = 0;
	int groupsUsedFallback = 0;
	for (const std::vector<int>& group : groupsToCombine)
	{
		SceneMesh* groupRefMesh = meshes[group[0]];
		QVector<SceneMesh*> groupMeshes;
		groupMeshes.reserve(static_cast<int>(group.size()));
		for (int idx : group)
			groupMeshes.append(meshes[idx]);

		const QString mergedName = _viewportWidget->generateUniqueMeshName(groupRefMesh->getName() + "_Merged");
		QString detail;
		SceneMesh* merged = combineFn(groupMeshes, mergedName, &detail);
		if (!detail.isEmpty())
			++groupsUsedFallback;
		_viewportWidget->addToDisplay(merged);
		const QUuid mergedUuid = merged->uuid();

		// Remove each source from its own tree position and recycle-bin it, capturing its
		// (node, position) for undo - same bookkeeping mergeSelectedMeshesByAdjacency() uses
		// per-cluster.
		QVector<MergeByAdjacencyCommand::SourceEntry> sourceEntries;
		sourceEntries.reserve(static_cast<int>(group.size()));
		for (int idx : group)
		{
			const QUuid& u = uuids[idx];
			int pos = 0;
			_sceneGraph->removeMeshUuid(u, pos);
			const int meshIndex = _viewportWidget->getIndexByUuid(u);
			if (meshIndex >= 0)
				_viewportWidget->moveToRecycleBin(u, meshIndex);

			MergeByAdjacencyCommand::SourceEntry e;
			e.uuid      = u;
			e.ownerNode = ownerNodes[idx];
			e.position  = pos;
			sourceEntries.append(e);
		}

		// Insert the combined mesh under the group's first source's own node.
		SceneNode* targetNode = ownerNodes[group[0]];
		const int mergedPosition = targetNode->meshUuids.size();
		_sceneGraph->restoreMeshUuid(targetNode, mergedUuid, mergedPosition);

		commands.append(new MergeByAdjacencyCommand(
			this, _viewportWidget, sourceEntries, mergedUuid, targetNode, mergedPosition, originalSelection,
			actionName));

		meshesCombinedCount += static_cast<int>(group.size());
	}
	_viewportWidget->doneCurrent();

	if (!commands.isEmpty())
	{
		updateDisplayList();
		if (commands.size() == 1)
		{
			_undoStack->push(commands.first());
		}
		else
		{
			_undoStack->beginMacro(actionName);
			for (MergeByAdjacencyCommand* cmd : commands)
				_undoStack->push(cmd);
			_undoStack->endMacro();
		}
	}

	QApplication::restoreOverrideCursor();

	if (commands.size() == 1 && separatedSingletonCount == 0)
	{
		// Exact wording as before this change, for the common (already-compatible or
		// merge-anyway) single-result case - no behavior/message change there.
		MainWindow::showStatusMessage(tr("Combined %1 selected meshes into 1.").arg(meshes.size())
			+ (groupsUsedFallback > 0 ? tr(" (geometry couldn't be unioned - used plain concatenation instead)") : QString()));
	}
	else
	{
		QString summary = tr("Combined %1 of %2 selected mesh(es) into %3 result(s), grouped by material.")
			.arg(meshesCombinedCount).arg(meshes.size()).arg(commands.size());
		QStringList details;
		if (groupsUsedFallback > 0)
			details << tr("%1 group(s) couldn't be unioned - used plain concatenation instead").arg(groupsUsedFallback);
		if (separatedSingletonCount > 0)
			details << tr("%1 mesh(es) left uncombined - unique material within the selection").arg(separatedSingletonCount);
		if (!details.isEmpty())
			summary += tr(" (%1)").arg(details.join(tr("; ")));
		MainWindow::showStatusMessage(summary);
	}
}

void ModelViewer::mergeSelectedMeshes()
{
	combineSelectedMeshes(
		[](const QVector<SceneMesh*>& meshes, const QString& name, QString*) {
			return SceneMesh::mergeMeshes(meshes, name);
		},
		tr("Merge Selected"));
}

void ModelViewer::unionSelectedMeshes()
{
	combineSelectedMeshes(
		[](const QVector<SceneMesh*>& meshes, const QString& name, QString* outDetail) {
			bool usedRealUnion = false;
			SceneMesh* result = SceneMesh::booleanUnionMeshes(meshes, name, &usedRealUnion);
			// Mesh Union's own fallback-to-concatenation behavior is
			// intentionally silent by design (never worse than plain Merge
			// Selected, whatever the input geometry) - but the user should
			// still be able to tell which actually happened, rather than
			// both paths reporting an identical "Combined" message.
			if (outDetail && !usedRealUnion)
				*outDetail = tr(" (geometry couldn't be unioned - used plain concatenation instead)");
			return result;
		},
		tr("Mesh Union"));
}

void ModelViewer::groupSelectedMeshes()
{
	const QList<QUuid> selectedUuids = treeWidgetModel->selectedMeshUuids();
	if (selectedUuids.isEmpty())
		return;

	const QSet<QUuid> originalSelection(selectedUuids.begin(), selectedUuids.end());

	QVector<SceneNode*> ownerNodes;
	QVector<QUuid> uuids;
	for (const QUuid& u : selectedUuids)
	{
		SceneNode* node = _sceneGraph->findNodeForMesh(u);
		if (!node)
			continue;
		ownerNodes.append(node);
		uuids.append(u);
	}

	if (uuids.isEmpty())
		return;

	QApplication::setOverrideCursor(Qt::WaitCursor);

	// Capture each mesh's original (node, position) before moving anything,
	// so undo can put every mesh back exactly where it came from.
	QVector<GroupMeshesCommand::MeshEntry> meshEntries;
	meshEntries.reserve(uuids.size());
	for (int i = 0; i < uuids.size(); ++i)
	{
		GroupMeshesCommand::MeshEntry e;
		e.uuid              = uuids[i];
		e.originalOwnerNode = ownerNodes[i];
		e.originalPosition  = ownerNodes[i]->meshUuids.indexOf(uuids[i]);
		meshEntries.append(e);
	}

	// The new group becomes a new CHILD of the first selected mesh's own
	// owner node - i.e. nested one level under wherever these meshes
	// currently live as direct entries, not a sibling of that owner. That
	// owner keeps whatever other meshes weren't selected; the group only
	// takes the ones the user actually picked.
	SceneNode* groupParent = ownerNodes.first();

	SceneNode* groupNode = new SceneNode();
	groupNode->nodeUuid = QUuid::createUuid();
	groupNode->name = tr("Group");

	const int groupPosition = groupParent->children.size();
	_sceneGraph->insertChildNode(groupParent, groupNode, groupPosition);

	for (const GroupMeshesCommand::MeshEntry& e : meshEntries)
	{
		int pos = 0;
		_sceneGraph->removeMeshUuid(e.uuid, pos);
		_sceneGraph->restoreMeshUuid(groupNode, e.uuid, groupNode->meshUuids.size());
	}

	updateDisplayList();
	_undoStack->push(new GroupMeshesCommand(
		this, _viewportWidget, groupNode, groupParent, groupPosition, meshEntries, originalSelection));

	QApplication::restoreOverrideCursor();

	MainWindow::showStatusMessage(tr("Grouped %1 mesh(es).").arg(meshEntries.size()));
}

void ModelViewer::openShrinkWrapDialog()
{
	ShrinkWrapDialog* dialog = findChild<ShrinkWrapDialog*>(QString(), Qt::FindDirectChildrenOnly);
	if (!dialog)
	{
		dialog = new ShrinkWrapDialog(this, this);
		dialog->setAttribute(Qt::WA_DeleteOnClose);
	}
	// Whatever's already tree-selected when the menu is clicked goes
	// straight into the working list - same effect whether this is a fresh
	// dialog or the menu was clicked again while it was already open with a
	// new tree selection made since.
	dialog->addCurrentTreeSelection();
	dialog->show();
	dialog->raise();
	dialog->activateWindow();
}

void ModelViewer::openSurfaceAnalysisDialog(const QString& mode)
{
	SurfaceAnalysisDialog* dialog = findChild<SurfaceAnalysisDialog*>(QString(), Qt::FindDirectChildrenOnly);
	if (!dialog)
	{
		dialog = new SurfaceAnalysisDialog(this, this);
		dialog->setAttribute(Qt::WA_DeleteOnClose);
	}
	// No tree-selection seeding - see this function's declaration comment
	// in ModelViewer.h for why (acts on the live viewport selection at
	// Apply-click time, not a fixed working list).
    if (!mode.isEmpty()) dialog->selectMode(mode);
	dialog->show();
	dialog->raise();
	dialog->activateWindow();
}

void ModelViewer::commitShrinkWrap(SceneNode* wrapNode, SceneNode* wrapParent, int wrapPosition,
                                    const QUuid& wrappedMeshUuid, const QSet<QUuid>& originalSelection)
{
	if (!_sceneGraph || !_viewportWidget || !_undoStack || !wrapNode || !wrapParent)
		return;
	_undoStack->push(new ShrinkWrapCommand(
		this, _viewportWidget, wrapNode, wrapParent, wrapPosition, wrappedMeshUuid, originalSelection));
}

void ModelViewer::openSubdivisionDialog()
{
	SubdivisionDialog* dialog = findChild<SubdivisionDialog*>(QString(), Qt::FindDirectChildrenOnly);
	if (!dialog)
	{
		dialog = new SubdivisionDialog(this, this);
		dialog->setAttribute(Qt::WA_DeleteOnClose);
	}
	// Same seed-with-current-tree-selection convention as
	// openShrinkWrapDialog() above.
	dialog->addCurrentTreeSelection();
	dialog->show();
	dialog->raise();
	dialog->activateWindow();
}

void ModelViewer::commitSubdivision(SceneNode* node, SceneNode* parent, int position,
                                     const QUuid& meshUuid, const QSet<QUuid>& originalSelection)
{
	if (!_sceneGraph || !_viewportWidget || !_undoStack || !node || !parent)
		return;
	_undoStack->push(new ShrinkWrapCommand(
		this, _viewportWidget, node, parent, position, meshUuid, originalSelection, tr("Subdivide")));
}

void ModelViewer::openReconstructSurfaceDialog()
{
	ReconstructSurfaceDialog* dialog = findChild<ReconstructSurfaceDialog*>(QString(), Qt::FindDirectChildrenOnly);
	if (!dialog)
	{
		dialog = new ReconstructSurfaceDialog(this, this);
		dialog->setAttribute(Qt::WA_DeleteOnClose);
	}
	// Same seed-with-current-tree-selection convention as
	// openShrinkWrapDialog()/openSubdivisionDialog() above.
	dialog->addCurrentTreeSelection();
	dialog->show();
	dialog->raise();
	dialog->activateWindow();
}

void ModelViewer::commitReconstructSurface(SceneNode* node, SceneNode* parent, int position,
                                            const QUuid& meshUuid, const QSet<QUuid>& originalSelection)
{
	if (!_sceneGraph || !_viewportWidget || !_undoStack || !node || !parent)
		return;
	_undoStack->push(new ShrinkWrapCommand(
		this, _viewportWidget, node, parent, position, meshUuid, originalSelection, tr("Reconstruct Surface")));
}

void ModelViewer::openRepairMeshDialog()
{
	RepairMeshDialog* dialog = findChild<RepairMeshDialog*>(QString(), Qt::FindDirectChildrenOnly);
	if (!dialog)
	{
		dialog = new RepairMeshDialog(this, this);
		dialog->setAttribute(Qt::WA_DeleteOnClose);
	}
	// Same seed-with-current-tree-selection convention as
	// openShrinkWrapDialog()/openSubdivisionDialog() above.
	dialog->addCurrentTreeSelection();
	dialog->show();
	dialog->raise();
	dialog->activateWindow();
}

void ModelViewer::commitRepairMesh(const QVector<RepairMeshResult>& results, const QSet<QUuid>& originalSelection)
{
	if (!_sceneGraph || !_viewportWidget || !_undoStack || results.isEmpty())
		return;

	// One Ctrl+Z should undo an entire multi-mesh Generate click as a single step - same
	// beginMacro()/endMacro() wrapping commitUVGeneration() uses for the same "one user action,
	// several underlying commands" reason. Skipped entirely for the common single-mesh case so
	// the undo-stack entry shows that command's own text directly instead of a redundant
	// one-item macro.
	const bool macro = results.size() > 1;
	if (macro)
		_undoStack->beginMacro(tr("Repair Mesh (%1)").arg(results.size()));

	for (const RepairMeshResult& result : results)
	{
		if (!result.node || !result.parent)
			continue;
		_undoStack->push(new ShrinkWrapCommand(
			this, _viewportWidget, result.node, result.parent, result.position,
			result.meshUuid, originalSelection, tr("Repair Mesh")));
	}

	if (macro)
		_undoStack->endMacro();
}

void ModelViewer::openFillHolesDialog()
{
	FillHolesDialog* dialog = findChild<FillHolesDialog*>(QString(), Qt::FindDirectChildrenOnly);
	if (!dialog)
	{
		dialog = new FillHolesDialog(this, this);
		dialog->setAttribute(Qt::WA_DeleteOnClose);
	}
	// Same seed-with-current-tree-selection convention as
	// openRepairMeshDialog()/openShrinkWrapDialog() above.
	dialog->addCurrentTreeSelection();
	dialog->show();
	dialog->raise();
	dialog->activateWindow();
}

void ModelViewer::commitFillHoles(const QVector<FillHolesResult>& results, const QSet<QUuid>& originalSelection)
{
	if (!_sceneGraph || !_viewportWidget || !_undoStack || results.isEmpty())
		return;

	// Same one-Ctrl+Z-undoes-the-whole-batch macro convention as commitRepairMesh() above.
	const bool macro = results.size() > 1;
	if (macro)
		_undoStack->beginMacro(tr("Fill Holes (%1)").arg(results.size()));

	for (const FillHolesResult& result : results)
	{
		if (!result.node || !result.parent)
			continue;
		_undoStack->push(new ShrinkWrapCommand(
			this, _viewportWidget, result.node, result.parent, result.position,
			result.meshUuid, originalSelection, tr("Fill Holes")));
	}

	if (macro)
		_undoStack->endMacro();
}

void ModelViewer::replaceToolResults(const QVector<QUuid>& meshUuids, const QString& text)
{
	if (!_undoStack || !_viewportWidget || meshUuids.isEmpty())
		return;
	_undoStack->push(new DeleteMeshCommand(this, _viewportWidget, meshUuids, text));
}

void ModelViewer::deleteSelectedItems()
{
	if (!checkForActiveSelection())
		return;

	QMessageBox::StandardButton reply = QMessageBox::question(
		this,
		tr("Delete"),
		tr("Delete selected item(s)?"),
		QMessageBox::Yes | QMessageBox::No
	);

	if (reply != QMessageBox::Yes)
		return;

	// Get UUIDs of selected meshes
	std::vector<int> indices = getSelectedIDs();
	QVector<QUuid> uuidsToDelete;

	for (int index : indices)
	{
		QUuid uuid = _viewportWidget->getUuidByIndex(index);
		if (!uuid.isNull())
			uuidsToDelete.append(uuid);
	}

	if (uuidsToDelete.isEmpty())
		return;

	// Push delete command (will move to recycle bin)
	_undoStack->push(new DeleteMeshCommand(this, _viewportWidget, uuidsToDelete));

	// Update UI
	updateControls();
}

#include "UVGenerationDialog.h"
void ModelViewer::openUVGenerationDialog()
{
	UVGenerationDialog* dialog = findChild<UVGenerationDialog*>(QString(), Qt::FindDirectChildrenOnly);
	if (!dialog)
	{
		dialog = new UVGenerationDialog(this, this);
		dialog->setAttribute(Qt::WA_DeleteOnClose);
	}
	// Whatever's already tree-selected when the menu is clicked goes
	// straight into the working list - same effect whether this is a fresh
	// dialog or the menu was clicked again while it was already open with a
	// new tree selection made since. Mirrors openShrinkWrapDialog() exactly.
	dialog->addCurrentTreeSelection();
	dialog->show();
	dialog->raise();
	dialog->activateWindow();
}

void ModelViewer::commitUVGeneration(QVector<QUndoCommand*> commands, const QString& methodName)
{
	if (!_undoStack || commands.isEmpty())
	{
		qDeleteAll(commands);
		return;
	}

	// One Ctrl+Z should undo an entire multi-mesh Generate click as a single
	// step - same beginMacro()/endMacro() wrapping hideAllItems() uses for
	// the same "one user action, several underlying commands" reason. Skip
	// the macro entirely for the common single-mesh case so the undo-stack
	// entry shows that command's own text directly instead of a redundant
	// one-item macro.
	const bool macro = commands.size() > 1;
	if (macro)
		_undoStack->beginMacro(tr("Generate UVs (%1)").arg(methodName));

	for (QUndoCommand* command : commands)
		_undoStack->push(command);

	if (macro)
		_undoStack->endMacro();
}

void ModelViewer::hideAllItems()
{
	// Hide All is one coherent action across every content type - meshes,
	// measurements, and annotations together, not just meshes - wrapped in
	// an undo macro so a single Ctrl+Z reverses all of it at once. Without
	// this, hiding all meshes left measurements/annotations visible (their
	// own, entirely separate visibility path never got touched), which read
	// as broken/disconnected rather than "Hide All" actually meaning all.
	if (_undoStack)
		_undoStack->beginMacro(tr("Hide All"));

	// Hide all meshes (empty visibility set)
	QSet<QUuid> newVisible;  // Empty set
	setVisibilityWithUndo(newVisible, tr("Hide All"));

	// Only push a measurement/annotation command if there's actually
	// something to hide - avoids polluting the undo stack with a no-op
	// entry for the common case of a document with no measurements/
	// annotations at all.
	if (!getVisibleMeasurementUuids().isEmpty())
		setMeasurementVisibilityWithUndo(QSet<QUuid>(), tr("Hide All Measurements"));
	if (!getVisibleAnnotationUuids().isEmpty())
		setAnnotationVisibilityWithUndo(QSet<QUuid>(), tr("Hide All Annotations"));

	if (_undoStack)
		_undoStack->endMacro();

	// Turn off swap visible if it was on
	if (_viewportWidget->isVisibleSwapped())
		_viewportWidget->swapVisible(false);
}

void ModelViewer::hideSelectedItems()
{
	if (!checkForActiveSelection())
		return;

	// Get current visibility
	QSet<QUuid> currentlyVisible = getVisibleUuids();

	// Get UUIDs of selected items to hide
	std::vector<int> selectedIds = getSelectedIDs();
	QSet<QUuid> toHide;
	for (int id : selectedIds)
	{
		QUuid uuid = _viewportWidget->getUuidByIndex(id);
		if (!uuid.isNull())
			toHide.insert(uuid);
	}

	// Calculate new visibility (remove selected from visible set)
	QSet<QUuid> newVisible = currentlyVisible - toHide;

	// Apply visibility with undo support
	setVisibilityWithUndo(newVisible, tr("Hide"));

	// Clear selection (preserve existing behavior)
	deselectAll();
}

void ModelViewer::showOnlySelectedItems()
{
	if (!checkForActiveSelection())
		return;

	// Get UUIDs of selected items - these will be the ONLY visible ones
	std::vector<int> selectedIds = getSelectedIDs();
	QSet<QUuid> newVisible;
	for (int id : selectedIds)
	{
		QUuid uuid = _viewportWidget->getUuidByIndex(id);
		if (!uuid.isNull())
			newVisible.insert(uuid);
	}

	// Apply visibility with undo support
	setVisibilityWithUndo(newVisible, tr("Show Only"));

	// Turn off swap visible if it was on
	if (_viewportWidget->isVisibleSwapped())
		_viewportWidget->swapVisible(false);
}

void ModelViewer::filterSelectionByMaterial()
{
	if (_viewportWidget->getMeshStore().empty())
		return;

	// Mutually exclusive with Filter by Color and Filter by Bounding Box -
	// all three dialogs live-push their own idea of the "current filter
	// selection" independently, so having more than one open at once means
	// whichever one you touch last silently wins, with no indication the
	// others' criteria are still armed. Closing it (not just hiding it)
	// goes through its normal closeEvent()/saveSettings() and self-deletes
	// via WA_DeleteOnClose.
	if (auto* other = findChild<FilterByColorDialog*>(QString(), Qt::FindDirectChildrenOnly))
		other->close();
	if (auto* other = findChild<FilterByBoundingBoxDialog*>(QString(), Qt::FindDirectChildrenOnly))
		other->close();

	// Non-modal, per-document singleton - same findChild-reuse-or-create
	// pattern as ModelViewer::openShrinkWrapDialog(). The dialog live-
	// previews the selection as its material choice changes and applies
	// Show Only/Hide directly, so there's nothing to read back here.
	auto* dialog = findChild<FilterByMaterialDialog*>(QString(), Qt::FindDirectChildrenOnly);
	if (!dialog)
	{
		dialog = new FilterByMaterialDialog(this, this);
		dialog->setAttribute(Qt::WA_DeleteOnClose);
	}
	dialog->show();
	dialog->raise();
	dialog->activateWindow();
}

void ModelViewer::filterSelectionByColor()
{
	std::vector<SceneMesh*> meshStore = _viewportWidget->getMeshStore();
	if (meshStore.empty())
		return;

	// Mutually exclusive with Filter by Material and Filter by Bounding Box -
	// see the matching comment in filterSelectionByMaterial() for why.
	if (auto* other = findChild<FilterByMaterialDialog*>(QString(), Qt::FindDirectChildrenOnly))
		other->close();
	if (auto* other = findChild<FilterByBoundingBoxDialog*>(QString(), Qt::FindDirectChildrenOnly))
		other->close();

	auto* dialog = findChild<FilterByColorDialog*>(QString(), Qt::FindDirectChildrenOnly);
	if (!dialog)
	{
		// Seed the target color list from the DISTINCT representative
		// colors of the current selection - real prior intent (the user
		// already selected these meshes), so the dialog opens already
		// live, matching every mesh close to any of them. Deduped within a
		// small epsilon so near-identical colors (e.g. minor shading
		// variance across a multi-mesh selection) don't spam the list with
		// near-duplicate rows. Empty if nothing was selected - the dialog
		// then starts with an empty list, nothing live, until the user adds
		// a color themselves (see FilterByColorDialog.h's doc comment for
		// why there's no arbitrary default color any more). Only done for a
		// fresh dialog - reopening an already-open one keeps whatever color
		// list the user already built.
		QVector<QVector3D> selectionColors;
		const std::vector<int> currentSelection = getSelectedIDs();
		for (int id : currentSelection)
		{
			if (id >= 0 && id < static_cast<int>(meshStore.size()))
				selectionColors.push_back(meshRepresentativeColor(meshStore[id]));
		}
		const QVector<QVector3D> initialColors = dedupedColors({}, selectionColors);

		dialog = new FilterByColorDialog(this, initialColors, this);
		dialog->setAttribute(Qt::WA_DeleteOnClose);
	}
	dialog->show();
	dialog->raise();
	dialog->activateWindow();
}

void ModelViewer::filterSelectionByBoundingBox()
{
	std::vector<SceneMesh*> meshStore = _viewportWidget->getMeshStore();
	if (meshStore.empty())
		return;

	// Mutually exclusive with Filter by Material and Filter by Color - see
	// the matching comment in filterSelectionByMaterial() for why.
	if (auto* other = findChild<FilterByMaterialDialog*>(QString(), Qt::FindDirectChildrenOnly))
		other->close();
	if (auto* other = findChild<FilterByColorDialog*>(QString(), Qt::FindDirectChildrenOnly))
		other->close();

	auto* dialog = findChild<FilterByBoundingBoxDialog*>(QString(), Qt::FindDirectChildrenOnly);
	if (!dialog)
	{
		// Seed the six limits from the current selection's combined bounds
		// (real prior intent, same reasoning as filterSelectionByColor()'s
		// own seeding) - or, if nothing is selected, from the whole scene's
		// combined bounds, so the dialog never opens with a degenerate
		// all-zero box that would silently match nothing. Only done for a
		// fresh dialog - reopening an already-open one keeps whatever limits
		// the user already set (re-seed via its own "Use Current Selection's
		// Bounds" button instead).
		const std::vector<int> currentSelection = getSelectedIDs();
		BoundingBox initialBounds;
		bool any = false;
		auto includeMesh = [&](SceneMesh* mesh) {
			if (!mesh)
				return;
			if (!any)
			{
				initialBounds = mesh->getBoundingBox();
				any = true;
			}
			else
			{
				initialBounds.addBox(mesh->getBoundingBox());
			}
		};
		if (!currentSelection.empty())
		{
			for (int id : currentSelection)
				if (id >= 0 && id < static_cast<int>(meshStore.size()))
					includeMesh(meshStore[id]);
		}
		else
		{
			for (SceneMesh* mesh : meshStore)
				includeMesh(mesh);
		}

		dialog = new FilterByBoundingBoxDialog(this, initialBounds, this);
		dialog->setAttribute(Qt::WA_DeleteOnClose);
	}
	dialog->show();
	dialog->raise();
	dialog->activateWindow();
}

void ModelViewer::saveCurrentSelectionAsSet(const QString& name)
{
	if (!_sceneGraph || name.trimmed().isEmpty())
		return;

	std::vector<int> selectedIds = getSelectedIDs();
	if (selectedIds.empty())
		return;

	QSet<QUuid> uuids;
	for (int id : selectedIds)
	{
		QUuid uuid = _viewportWidget->getUuidByIndex(id);
		if (!uuid.isNull())
			uuids.insert(uuid);
	}
	if (uuids.isEmpty())
		return;

	SelectionSet set;
	set.id = QUuid::createUuid();
	set.name = name.trimmed();
	set.meshUuids = uuids;

	_undoStack->push(new SaveSelectionSetCommand(this, _viewportWidget, set));
}

void ModelViewer::recallSelectionSet(const QUuid& setId)
{
	if (!_sceneGraph)
		return;

	const int index = _sceneGraph->selectionSetIndexById(setId);
	if (index < 0)
		return;

	const SelectionSet& set = _sceneGraph->selectionSets().at(index);

	QSet<int> ids;
	QSet<QUuid> resolvedUuids;
	for (const QUuid& uuid : set.meshUuids)
	{
		const int meshIndex = _viewportWidget->getIndexByUuid(uuid);
		if (meshIndex >= 0)
		{
			ids.insert(meshIndex);
			resolvedUuids.insert(uuid);
		}
	}
	if (ids.isEmpty())
		return;

	// Reveal any of the set's own members that are currently hidden - a
	// saved selection is a "jump back to this" bookmark, and leaving a
	// hidden member hidden would silently fail to select part of the set
	// with no visual feedback (same "show, not show only" behavior as
	// showSelectedItems() - never touches visibility of anything OUTSIDE
	// the set). Both the reveal and the selection change land in one undo
	// macro so a single Ctrl+Z reverses both together, same convention as
	// Merge by Adjacency/the material-grouped Union.
	const QSet<QUuid> currentlyVisible = getVisibleUuids();
	const bool hasHiddenMembers = !(resolvedUuids - currentlyVisible).isEmpty();

	if (hasHiddenMembers)
	{
		_undoStack->beginMacro(tr("Recall Selection Set"));
		setVisibilityWithUndo(currentlyVisible | resolvedUuids, tr("Show"));
		setSelectionWithUndo(ids);
		_undoStack->endMacro();
	}
	else
	{
		setSelectionWithUndo(ids);
	}
}

void ModelViewer::deleteSelectionSet(const QUuid& setId)
{
	if (!_sceneGraph)
		return;
	if (_sceneGraph->selectionSetIndexById(setId) < 0)
		return;

	_undoStack->push(new DeleteSelectionSetCommand(this, _viewportWidget, setId));
}

void ModelViewer::saveCurrentSceneState(const QString& name)
{
	if (!_sceneGraph || !_viewportWidget || name.trimmed().isEmpty())
		return;

	SceneState state;
	state.id = QUuid::createUuid();
	state.name = name.trimmed();
	state.camera = _viewportWidget->captureCurrentCameraEntry(state.name);
	state.visibleMeshUuids = getVisibleUuids();
	state.selectedMeshUuids = getSelectedUuids();

	// Presentation state - see SceneStateData.h for why this now mirrors the
	// full "viewerState" document-defaults field set verbatim.
	state.displayMode = static_cast<int>(_viewportWidget->getDisplayMode());
	// RAY_TRACED is tracked separately from SceneRenderController's own
	// RenderingMode (see RenderEnums.h's doc comment on RenderingMode) - the
	// "armed" check has to come first since getRenderingMode() itself never
	// returns RAY_TRACED.
	state.renderingMode = _viewportWidget->isRayTracedRenderingModeArmed()
		? QStringLiteral("RayTraced")
		: (_viewportWidget->getRenderingMode() == RenderingMode::ADS_BLINN_PHONG
			? QStringLiteral("ADS")
			: QStringLiteral("PBR"));
	state.groundMode = static_cast<int>(_viewportWidget->groundMode());
	state.skyBoxShown = _viewportWidget->isSkyBoxShown();
	state.skyBoxHDRIEnabled = _viewportWidget->isSkyBoxHDRIEnabled();
	state.skyBoxFolderPath = _viewportWidget->getCurrentSkyboxFolder();
	state.skyBoxBlurPercent = _viewportWidget->getSkyBoxBlurPercent();
	state.skyBoxFOV = _viewportWidget->getSkyBoxFOV();
	state.skyBoxZRotationDegrees = _viewportWidget->getSkyBoxZRotationDegrees();
	state.floorTextureShown = _viewportWidget->isFloorTextureShown();
	state.floorTexturePath = _viewportWidget->getFloorTexturePath();
	state.floorTexRepeatS = _viewportWidget->getFloorTexRepeatS();
	state.floorTexRepeatT = _viewportWidget->getFloorTexRepeatT();
	state.floorOffsetPercent = _viewportWidget->getFloorOffsetPercent();
	state.shadowQuality = static_cast<int>(_viewportWidget->getShadowQuality());
	state.reflectionsEnabled = _viewportWidget->areReflectionsEnabled();
	state.shadowsEnabled = _viewportWidget->areShadowsEnabled();
	state.selfShadowsEnabled = _viewportWidget->areSelfShadowsEnabled();
	state.shadowCatcherDarkness = _viewportWidget->shadowCatcherDarkness();
	state.shadowCatcherBaseColor = _viewportWidget->shadowCatcherBaseColor();
	state.shadowCatcherMetalness = _viewportWidget->shadowCatcherMetalness();
	state.shadowCatcherRoughness = _viewportWidget->shadowCatcherRoughness();
	state.environmentEnabled = _viewportWidget->isEnvironmentMapEnabled();
	state.iblEnabled = _viewportWidget->isIBLEnabled();
	state.envMapExposureStops = std::log2(std::max(_viewportWidget->getEnvMapExposure(), 1.0e-6f));
	state.iblExposureStops = std::log2(std::max(_viewportWidget->getIBLExposure(), 1.0e-6f));
	state.defaultLightsEnabled = _viewportWidget->areDefaultLightsEnabled();
	state.punctualLightsEnabled = _viewportWidget->arePunctualLightsEnabled();
	state.showLights = _viewportWidget->areLightsShown();
	state.defaultLightColor = _viewportWidget->getDefaultLightColor();
	state.defaultLightOffset = _viewportWidget->getLightOffset();
	state.hdrToneMapping = _viewportWidget->getHdrToneMapping();
	state.hdrToneMappingMode = static_cast<int>(_viewportWidget->getHDRToneMappingMode());
	state.gammaCorrection = _viewportWidget->getGammaCorrection();
	state.screenGamma = _viewportWidget->getScreenGamma();
	state.bgTopColor = _viewportWidget->getBgTopColor();
	state.bgBotColor = _viewportWidget->getBgBotColor();

	_undoStack->push(new SaveSceneStateCommand(this, _viewportWidget, state));
}

void ModelViewer::recallSceneState(const QUuid& stateId)
{
	if (!_sceneGraph || !_viewportWidget)
		return;

	const int index = _sceneGraph->sceneStateIndexById(stateId);
	if (index < 0)
		return;

	const SceneState& state = _sceneGraph->sceneStates().at(index);

	// Camera restore is immediate and NOT undoable - matches this app's
	// existing convention that no camera activation is ever on the undo
	// stack (see activateCameraEntry()'s own doc comment). Applied here
	// FIRST so a Ray-Traced state's requestRayTracedRenderNow() call below
	// already sees the right pose for its first snapshot - but this is NOT
	// the final word on camera position; see the second activateCameraEntry()
	// call after the visibility/selection restore below for why.
	_viewportWidget->activateCameraEntry(state.camera);

	// Presentation state - also immediate/not undoable, same convention as
	// camera above (no display/rendering-mode change anywhere in this app
	// is on the undo stack either).
	//
	// Order matters a lot here, confirmed the hard way (a real, reported
	// bug: recalling a Ray-Traced state with GroundMode::InfinitePlane
	// showed Floor instead). setDisplayMode() unconditionally emits
	// displayModeChanged(), which VisualizationEnvironmentPanel::
	// onDisplayModeChanged() reacts to by re-asserting the ground-mode
	// DEFAULT for whatever rendering mode is CURRENTLY active (Floor for
	// realistic shading, None for ADS) - "unconditionally re-asserted on
	// every mode switch," per that function's own doc comment. Calling
	// setDisplayMode() AFTER onRenderingModeSelected("RayTraced") - the
	// original order here - fired that reset a second time with nothing
	// left to correct it back to InfinitePlane afterward, clobbering the
	// value onRenderingModeSelected() had just correctly set via its own
	// applyRayTracedGroundDefaultsOnce() call.
	//
	// Fixed by restoring in the same relative order a normal user
	// interaction would naturally produce: display mode first (whatever
	// stale ground-mode side effect it causes is harmless, since it's about
	// to be overridden anyway), THEN the rendering-mode switch itself
	// (routed through onRenderingModeSelected() rather than ViewportWidget::
	// setRenderingMode() directly, so the toolbar's active-mode indicator
	// stays in sync - same entry point RtRenderDialog::onRenderClicked()
	// uses; its own internal sequence re-asserts that mode's OWN canonical
	// ground-mode default as its last word), THEN an EXPLICIT setGroundMode()
	// to the state's actual saved value - not just relying on the mode's
	// default, since the user can freely override ground mode after a mode
	// switch and it sticks (see SceneStateData.h's own doc comment) - BEFORE
	// starting the ray-traced render itself, so its first snapshot already
	// reflects the correct ground mode instead of needing an extra rebuild
	// a moment later (same reasoning applyRayTracedGroundDefaultsOnce()'s
	// own doc comment gives for its call-before-arm ordering). Skybox/
	// background restore last, same as before - onRenderingModeSelected()
	// also has skybox/HDRI side effects, and the state's own saved values
	// need to win over those defaults too.
	_viewportWidget->setDisplayMode(static_cast<DisplayMode>(state.displayMode));
	onRenderingModeSelected(state.renderingMode);
	_viewportWidget->setGroundMode(static_cast<GroundMode>(state.groundMode));

	// Everything below is restored for the SAME reason groundMode is set
	// explicitly above: onRenderingModeSelected()'s onDisplayModeChanged()
	// side effect unconditionally re-asserts its own realism-driven defaults
	// for floor/shadows/reflections/env-map/default-lights on every mode
	// switch (see that function's own doc comment), so anything the saved
	// state actually wants has to be applied AFTER the mode switch, as an
	// explicit override - not before, where it would just get clobbered the
	// same way groundMode originally was. All of it (skybox included) also
	// runs BEFORE requestRayTracedRenderNow() at the bottom, so a Ray-Traced
	// state's first interactive snapshot already reflects the full restored
	// look instead of needing a second rebuild moments later.
	_viewportWidget->showSkyBox(state.skyBoxShown);
	_viewportWidget->setSkyBoxTextureHDRI(state.skyBoxHDRIEnabled);
	// Which skybox/HDRI preset (or custom folder) is loaded isn't implied by
	// the HDRI/LDRI toggle above - setSkyBoxTextureHDRI() only picks which
	// preset SET is active, not which member of it. Restore the actual
	// folder explicitly (see SceneStateData.h's skyBoxFolderPath doc comment
	// for why this is a path, not an index), then resync
	// VisualizationEnvironmentPanel's own combo/index bookkeeping to match -
	// via syncSkyBoxSelectionSilently(), not reloadSkyBoxPresets(), since the
	// latter would call setSkyBoxTextureFolder() again itself and reload the
	// texture a second time for no reason.
	if (!state.skyBoxFolderPath.isEmpty())
	{
		_viewportWidget->setSkyBoxTextureFolder(state.skyBoxFolderPath);
		visualizationEnvironmentPanel->syncSkyBoxSelectionSilently();
	}
	_viewportWidget->setSkyBoxBlurPercent(state.skyBoxBlurPercent);
	_viewportWidget->setSkyBoxFOV(state.skyBoxFOV);
	// Applies to the viewport AND syncs the panel's preset combo + fine
	// slider - see its own doc comment (same reason viewerState's load block
	// uses this instead of a raw setSkyBoxZRotationDegrees() call).
	visualizationEnvironmentPanel->restoreSkyBoxRotationDegrees(static_cast<float>(state.skyBoxZRotationDegrees));

	_viewportWidget->showFloorTexture(state.floorTextureShown);
	if (!state.floorTexturePath.isEmpty())
		_viewportWidget->setFloorTextureFromPath(state.floorTexturePath);
	_viewportWidget->setFloorTexRepeatS(state.floorTexRepeatS);
	_viewportWidget->setFloorTexRepeatT(state.floorTexRepeatT);
	_viewportWidget->setFloorOffsetPercent(state.floorOffsetPercent);
	_viewportWidget->setShadowQuality(static_cast<AdaptiveShadowMapper::QualityLevel>(state.shadowQuality));
	_viewportWidget->showReflections(state.reflectionsEnabled);
	_viewportWidget->showShadows(state.shadowsEnabled);
	_viewportWidget->showSelfShadows(state.selfShadowsEnabled);
	_viewportWidget->setShadowCatcherDarkness(state.shadowCatcherDarkness);
	_viewportWidget->setShadowCatcherBaseColor(state.shadowCatcherBaseColor);
	_viewportWidget->setShadowCatcherMetalness(state.shadowCatcherMetalness);
	_viewportWidget->setShadowCatcherRoughness(state.shadowCatcherRoughness);

	_viewportWidget->showEnvironment(state.environmentEnabled);
	_viewportWidget->useIBL(state.iblEnabled);
	_viewportWidget->setEnvMapExposure(state.envMapExposureStops);
	_viewportWidget->setIBLExposure(state.iblExposureStops);

	_viewportWidget->useDefaultLights(state.defaultLightsEnabled);
	_viewportWidget->usePunctualLights(state.punctualLightsEnabled);
	_viewportWidget->showLights(state.showLights);
	_viewportWidget->setDefaultLightColor(state.defaultLightColor);
	// Applies to the viewport AND syncs the panel's X/Y/Z sliders - see
	// viewerState's load block, which uses the same call for the same reason.
	visualizationEnvironmentPanel->restoreDefaultLightOffset(state.defaultLightOffset);

	_viewportWidget->enableHDRToneMapping(state.hdrToneMapping);
	_viewportWidget->setHDRToneMappingMode(static_cast<HDRToneMapMode>(state.hdrToneMappingMode));
	_viewportWidget->enableGammaCorrection(state.gammaCorrection);
	_viewportWidget->setScreenGamma(state.screenGamma);

	_viewportWidget->setBgTopColor(state.bgTopColor);
	_viewportWidget->setBgBotColor(state.bgBotColor);

	if (state.renderingMode == QStringLiteral("RayTraced"))
		_viewportWidget->requestRayTracedRenderNow();

	// Resolve stored selection UUIDs to live indices, dropping any mesh
	// deleted since the state was saved.
	QSet<int> selectedIds;
	for (const QUuid& uuid : state.selectedMeshUuids)
	{
		const int meshIndex = _viewportWidget->getIndexByUuid(uuid);
		if (meshIndex >= 0)
			selectedIds.insert(meshIndex);
	}

	// Visibility + selection land in one undo macro so a single Ctrl+Z
	// reverses both together, same beginMacro()/endMacro() shape
	// recallSelectionSet() uses above. Sets the EXACT saved visibility set,
	// not unioned with what's currently visible - a scene state is a full
	// configuration snapshot, not an "also reveal these" bookmark.
	//
	// setVisibilityWithUndo() below rebuilds the display list, which - via
	// ViewportWidget::setDisplayList() - can itself move the camera we just
	// restored above: it calls fitAll() whenever Auto Fit View is on (real,
	// reported bug: a saved zoomed/panned framing got silently replaced by a
	// fresh fit-to-scene on recall), and unconditionally repositions a Fly/
	// FirstPerson-mode camera via positionGameplayCameraForScene() regardless
	// of Auto Fit. Suppressing Auto Fit for the duration (same save/disable/
	// restore pattern already used around display-list rebuilds elsewhere in
	// this file, e.g. handleActiveDocumentChanged()) stops the first trigger,
	// but not the gameplay-camera one - so the camera is explicitly
	// reactivated again below, AFTER the visibility/selection restore, as the
	// true final step. That second call is what actually guarantees the
	// saved framing is what's on screen when recall finishes, regardless of
	// which (if any) of setDisplayList()'s internal repositioning paths fired.
	const bool shouldAutoFit = _viewportWidget->autoFitViewOnUpdate();
	_viewportWidget->setAutoFitViewOnUpdate(false);

	_undoStack->beginMacro(tr("Recall Scene State"));
	setVisibilityWithUndo(state.visibleMeshUuids, tr("Show"));
	setSelectionWithUndo(selectedIds);
	_undoStack->endMacro();

	_viewportWidget->setAutoFitViewOnUpdate(shouldAutoFit);
	_viewportWidget->activateCameraEntry(state.camera);
}

void ModelViewer::deleteSceneState(const QUuid& stateId)
{
	if (!_sceneGraph)
		return;
	if (_sceneGraph->sceneStateIndexById(stateId) < 0)
		return;

	_undoStack->push(new DeleteSceneStateCommand(this, _viewportWidget, stateId));
}

void ModelViewer::showAllItems()
{
	// Show All is one coherent action across every content type - see
	// hideAllItems()'s identical reasoning.
	if (_undoStack)
		_undoStack->beginMacro(tr("Show All"));

	// Get all mesh UUIDs
	QSet<QUuid> newVisible;
	int meshCount = static_cast<int>(_viewportWidget->getMeshStore().size());

	for (int i = 0; i < meshCount; ++i)
	{
		QUuid uuid = _viewportWidget->getUuidByIndex(i);
		if (!uuid.isNull())
			newVisible.insert(uuid);
	}

	// Apply visibility with undo support
	setVisibilityWithUndo(newVisible, tr("Show All"));

	// Only push a measurement/annotation command if there's actually a
	// change to make - same no-op-avoidance reasoning as hideAllItems().
	if (_sceneGraph)
	{
		QSet<QUuid> allMeasurementIds;
		for (const Measurement& m : _sceneGraph->measurements())
			allMeasurementIds.insert(m.id);
		if (allMeasurementIds != getVisibleMeasurementUuids())
			setMeasurementVisibilityWithUndo(allMeasurementIds, tr("Show All Measurements"));

		QSet<QUuid> allAnnotationIds;
		for (const Annotation& a : _sceneGraph->annotations())
			allAnnotationIds.insert(a.id);
		if (allAnnotationIds != getVisibleAnnotationUuids())
			setAnnotationVisibilityWithUndo(allAnnotationIds, tr("Show All Annotations"));
	}

	if (_undoStack)
		_undoStack->endMacro();

	// Turn off swap visible if it was on
	if (_viewportWidget->isVisibleSwapped())
		_viewportWidget->swapVisible(false);
}

void ModelViewer::showSelectedItems()
{
	if (!checkForActiveSelection())
		return;

	// Get current visibility
	QSet<QUuid> currentlyVisible = getVisibleUuids();

	// Get UUIDs of selected items to show
	std::vector<int> selectedIds = getSelectedIDs();
	QSet<QUuid> toShow;
	for (int id : selectedIds)
	{
		QUuid uuid = _viewportWidget->getUuidByIndex(id);
		if (!uuid.isNull())
			toShow.insert(uuid);
	}

	// Calculate new visibility (add selected to visible set)
	QSet<QUuid> newVisible = currentlyVisible | toShow;

	// Apply visibility with undo support
	setVisibilityWithUndo(newVisible, tr("Show"));

	// Clear selection (preserve existing behavior)
	deselectAll();
}

bool ModelViewer::checkForActiveSelection()
{
	if (!hasSelection())
	{
		QMessageBox::information(this, tr("Selection Required"), tr("Please select an object first"));
		return false;
	}
	return true;
}

bool ModelViewer::hasSelection() const
{
	return treeWidgetModel->hasMeshSelection();
}

std::vector<int> ModelViewer::getSelectedIDs() const
{
	return treeWidgetModel->getSelectedIndices();
}

QSet<QUuid> ModelViewer::getSelectedUuids() const
{
	std::vector<int> selectedIds = getSelectedIDs();
	QSet<QUuid> selectedUuids;

	for (int id : selectedIds)
	{
		QUuid uuid = _viewportWidget->getUuidByIndex(id);
		if (!uuid.isNull())
			selectedUuids.insert(uuid);
	}

	return selectedUuids;
}

void ModelViewer::displaySelectedMeshInfo()
{
	// Trimmed to mesh statistics only (points/triangles/memory) - a quick
	// glance stat from the tree's context menu. Mass properties (volume,
	// surface area, mass, center of mass, bounding box) moved to the
	// purpose-built Tools -> Mass Properties... dialog (MassPropertiesDialog),
	// which has room for the "N/A + reason" nuance those values need (an
	// open mesh has no valid volume, a material with no assigned density has
	// no valid mass, etc.) that a single QMessageBox string dump doesn't.
	std::vector<int> selected = getSelectedIDs();
	if (selected.size() != 0)
	{
		std::vector<SceneMesh*> meshes = _viewportWidget->getMeshStore();
		QString name;
		size_t points = 0, triangles = 0;
		unsigned long long rawmem = 0;
		SceneMesh* mesh = nullptr;
		size_t selectionCount = selected.size();
		if (selectionCount > 1)
			name = QString("%1 Meshes\n").arg(selectionCount);
		else
			name = meshes.at(selected[0])->getName() + "\n";
		for (int id : selected)
		{
			mesh = meshes.at(id);
			points += mesh->getPoints().size() / 3;
			triangles += mesh->getIndices().size() / 3;
			rawmem += mesh->memorySize();
		}

		QString strpoints = QString(tr("Points: %1\n")).arg(points);
		QString strtriangles = QString(tr("Triangles: %1\n")).arg(triangles);
		unsigned long long mem = 0;
		QString units;
		if (rawmem < 1024)
		{
			mem = rawmem;
			units = "bytes";
		}
		else if (rawmem < (1024 * 1024))
		{
			mem = rawmem / 1024;
			units = "kb";
		}
		else if (rawmem < (1024 * 1024 * 1024))
		{
			mem = rawmem / (1024 * 1024);
			units = "mb";
		}
		else
		{
			mem = rawmem / (1024 * 1024 * 1024);
			units = "gb";
		}
		QString meshSize = QString(tr("Memory: %1 ")).arg(mem) + units;

		QString info = name + strpoints + strtriangles + meshSize;
		QMessageBox::information(this, tr("Mesh Info"), info);
	}
}

void ModelViewer::showVisualizationModelPage()
{
	MainWindow::mainWindow()->showEnvironmentDockPage();
}

void ModelViewer::showPredefinedMaterialsPage()
{
	MainWindow::mainWindow()->showMaterialsPropertiesPage();
}

void ModelViewer::showTransformationsPage()
{
	MainWindow::mainWindow()->showTransformationsPropertiesPage();
}

void ModelViewer::showEnvironmentPage()
{
	MainWindow::mainWindow()->showEnvironmentDockPage();
}

void ModelViewer::handleTreeWidgetVisibilityChanged()
{
	_visibleMeshUuids = treeWidgetModel->getVisibleUuids();
	applyVisibleMeshState(false);
}

void ModelViewer::handleTreeWidgetSelectionChanged()
{
	const std::vector<int> selectedVec = treeWidgetModel->getSelectedIndices();
	const QList<int> selectedIds(selectedVec.begin(), selectedVec.end());

	// Tree-initiated selection: sync SelectionManager silently (no signal) to avoid
	// re-entering this handler via the SelectionManager::selectionChanged → singleSelectionDone
	// → setListRow loop. Visual state is applied separately.
	_viewportWidget->getSelectionManager()->syncSelectedIds(selectedIds);
	_viewportWidget->syncMeshSelectionVisualState();

	_viewportWidget->update();
	updateSelectionStatusMessage();

	// Notify panels connected to ViewportWidget::selectionChanged (e.g. TextureDebugPanel).
	emit _viewportWidget->selectionChanged(selectedIds);

	if (selectedVec.empty())
	{
		if (_viewportWidget->isExplodedViewManualPlacementActive())
			_viewportWidget->showTransformGizmoForSelection(true);
		else
			_viewportWidget->showTransformGizmoForSelection(false);
	}
	else if (tabWidgetVizAttribs->currentIndex() == 1) // Transformations tab
	{
		_viewportWidget->showTransformGizmoForSelection(true);
	}
}

void ModelViewer::handleTreeWidgetMeshRenamed(const QUuid& uuid, const QString& newName)
{
	SceneMesh* mesh = _viewportWidget->getMeshByUuid(uuid);
	if (!mesh) return;

	// Capture old name before any mutation so the command can restore it.
	const QString oldName   = mesh->getName();
	const QString finalName = computeUniqueName(mesh, newName);

	// Nothing to do if the resolved name matches the current one.
	if (finalName == oldName) return;

	_undoStack->push(new RenameMeshCommand(
	    this, _viewportWidget, treeWidgetModel,
	    uuid, oldName, finalName,
	    tr("Rename \"%1\" to \"%2\"").arg(oldName, finalName)));
}

void ModelViewer::checkAndRenameModel(SceneMesh* mesh, const QString& name)
{
	bool duplicate = false;
	QString finalName = name;
	int dupCnt = 1;
	std::vector<SceneMesh*> meshes = _viewportWidget->getMeshStore();
	do
	{
		for (SceneMesh* msh : meshes)
		{
			if (msh->getName() == finalName)
			{
				duplicate = true;
				finalName = QString("%1_%2").arg(name).arg(dupCnt);
				dupCnt++;
				break;
			}
			else
				duplicate = false;
		}
	} while (duplicate);
	mesh->setName(finalName);
	updateDisplayList();
}

QString ModelViewer::computeUniqueName(SceneMesh* exclude, const QString& name) const
{
	// Return a version of 'name' that does not collide with any existing mesh
	// name, skipping 'exclude' (the mesh being renamed) so it doesn't conflict
	// with itself.  Appends _1, _2, … until a free slot is found.
	bool    duplicate = false;
	QString finalName = name;
	int     dupCnt    = 1;
	const std::vector<SceneMesh*> meshes = _viewportWidget->getMeshStore();
	do
	{
		duplicate = false;
		for (SceneMesh* msh : meshes)
		{
			if (msh == exclude) continue;
			if (msh->getName() == finalName)
			{
				duplicate = true;
				finalName = QString("%1_%2").arg(name).arg(dupCnt++);
				break;
			}
		}
	} while (duplicate);
	return finalName;
}

void ModelViewer::onFileImport()
{
	QFileDialog fileDialog(this, tr("Import Model File"), _lastOpenedDir);
	fileDialog.setFileMode(QFileDialog::ExistingFiles);
	QStringList supportedExtensions = ModelViewerApplication::supportedImportExtensions();
	fileDialog.setNameFilters(supportedExtensions);

	if (supportedExtensions.contains(_lastSelectedFilter))
	{
		fileDialog.selectNameFilter(_lastSelectedFilter);
	}

	// Run dialog
	QStringList fileNames;
	if (fileDialog.exec())
	{
		fileNames = fileDialog.selectedFiles();
		_lastSelectedFilter = fileDialog.selectedNameFilter();
		_lastOpenedDir = QFileInfo(fileNames.first()).absolutePath();
	}

	importFiles(fileNames);
}

void ModelViewer::importFiles(QStringList& fileNames)
{
	// Load selected files
	if (!fileNames.isEmpty())
	{
		qDebug() << "ModelViewer::importFiles - importing into viewer=" << (void*)this
		         << "title=" << windowTitle()
		         << "parentWidget=" << (parentWidget() ? parentWidget()->metaObject()->className() : "null")
		         << "files=" << fileNames;
		QApplication::setOverrideCursor(Qt::WaitCursor);
		for (const QString& fileName : std::as_const(fileNames))
		{
			loadFile(fileName);
		}
		markNonUndoDocumentModified();

		// New SceneMesh objects were just allocated - see
		// MeasurementController::clearGeometryCaches()'s doc comment for why
		// this is worth doing here specifically (not a hot path, negligible
		// cost, removes even the narrow residual risk of a pointer-keyed
		// cache entry surviving past the mesh it was computed for).
		_viewportWidget->clearMeasurementGeometryCaches();

		QApplication::restoreOverrideCursor();
		MainWindow::mainWindow()->activateWindow();
		QApplication::alert(MainWindow::mainWindow());

		qDebug() << "ModelViewer::importFiles - import complete for viewer=" << (void*)this
		         << "parentWidget=" << (parentWidget() ? parentWidget()->metaObject()->className() : "null");
	}
}


#include "AssImpMeshExporter.h"
#include "SceneMesh.h"
#include "SceneUtils.h"
#include "SceneGraphExporter.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
void ModelViewer::onFileExport()
{
	if (!_sceneGraph || !_sceneGraph->root() || !_viewportWidget)
		return;

	// --- Build the format filter list (shared for all file dialogs) ---------------
	Assimp::Exporter assimpExporter;
	QStringList filters;
	QStringList allExtensions;
	QMap<QString, QString> filterToExtension;

	for (unsigned int i = 0; i < assimpExporter.GetExportFormatCount(); ++i)
	{
		const aiExportFormatDesc* desc = assimpExporter.GetExportFormatDescription(i);
		QString ext  = QString::fromUtf8(desc->fileExtension);
		QString descStr = QString::fromUtf8(desc->description);
		QString filter  = QString("%1 (*.%2)").arg(descStr).arg(ext);
		filters.append(filter);
		allExtensions.append("*." + ext);
		filterToExtension[filter] = ext;
	}
	QString allSupportedFilter = QString("All Supported Files (%1)").arg(allExtensions.join(' '));
	filters.prepend(allSupportedFilter);
	filterToExtension[allSupportedFilter] = "";
	// ------------------------------------------------------------------------------

	// --- Scene selection: if multiple files are loaded, ask which one to export ---
	// The dialog is shown BEFORE the filename dialog so the user picks a single scene
	// first, then provides exactly one output filename for it.
	QString selectedSourceFile;   // empty = no filter (single scene loaded)
	const QList<SceneNode*>& fileNodes = _sceneGraph->root()->children;

	if (fileNodes.size() > 1)
	{
		QDialog selDlg(this);
		selDlg.setWindowTitle(tr("Select Scene to Export"));
		selDlg.setMinimumWidth(440);

		QVBoxLayout* vlay = new QVBoxLayout(&selDlg);
		vlay->addWidget(new QLabel(
			tr("Multiple scenes are loaded. Select one to export:"), &selDlg));

		QListWidget* list = new QListWidget(&selDlg);
		list->setSelectionMode(QAbstractItemView::SingleSelection);
		for (const SceneNode* fileNode : fileNodes)
		{
			QListWidgetItem* item = new QListWidgetItem(fileNode->name, list);
			item->setData(Qt::UserRole, fileNode->sourceFile);
		}
		list->setCurrentRow(0);   // select first by default
		vlay->addWidget(list);

		QDialogButtonBox* bbox = new QDialogButtonBox(
			QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &selDlg);
		connect(bbox, &QDialogButtonBox::accepted, &selDlg, &QDialog::accept);
		connect(bbox, &QDialogButtonBox::rejected, &selDlg, &QDialog::reject);
		vlay->addWidget(bbox);

		if (selDlg.exec() != QDialog::Accepted)
			return;

		QListWidgetItem* sel = list->currentItem();
		if (!sel)
			return;

		selectedSourceFile = sel->data(Qt::UserRole).toString();
	}
	// ------------------------------------------------------------------------------

	// --- Filename dialog ----------------------------------------------------------
	QString selectedFilter;
	QString fileName = QFileDialog::getSaveFileName(
		this, tr("Export Model"), _lastOpenedDir,
		filters.join(";;"), &selectedFilter);

	if (fileName.isEmpty())
		return;

	QString extToAppend = filterToExtension[selectedFilter];
	if (!extToAppend.isEmpty() && !fileName.endsWith("." + extToAppend, Qt::CaseInsensitive))
		fileName += "." + extToAppend;
	// ------------------------------------------------------------------------------

	// --- Build export scene and mesh list ----------------------------------------
	QStringList allowedSourceFiles;
	if (!selectedSourceFile.isEmpty())
		allowedSourceFiles << selectedSourceFile;

	QSettings exportModeSettings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
	const bool exportSelectedOnly = !exportModeSettings.value("radioButtonExportScene", true).toBool();

	QSet<QUuid> selectedMeshUuidSet;
	if (exportSelectedOnly)
	{
		const QList<QUuid> selected = treeWidgetModel->selectedMeshUuids();
		if (selected.isEmpty())
		{
			QMessageBox::warning(this, tr("Nothing Selected"),
				tr("Select one or more meshes in the scene tree before exporting selected meshes."));
			return;
		}
		selectedMeshUuidSet = QSet<QUuid>(selected.begin(), selected.end());
	}

	// Collected as a side effect of the resolver, in the exact order buildExportScene()
	// embeds meshes into copyScene, so triMeshes always stays in 1:1 correspondence with
	// copyScene->mNumMeshes. That correspondence is required for
	// AssImpMeshExporter::exportScene() to take the hierarchy-aware (transform-preserving)
	// path instead of silently falling back to its flat, sceneIndex-based pruning path,
	// which assumes mesh-array positions match the ORIGINAL import scene and would
	// mis-associate meshes/materials for a freshly rebuilt, selection-filtered export scene.
	std::vector<SceneMesh*> triMeshes;
	auto resolver = [this, exportSelectedOnly, &selectedMeshUuidSet, &triMeshes](const QUuid& uuid) -> SceneMesh* {
		if (exportSelectedOnly && !selectedMeshUuidSet.contains(uuid))
			return nullptr;
		SceneMesh* mesh = _viewportWidget->getMeshByUuid(uuid);
		if (mesh)
			triMeshes.push_back(mesh);
		return mesh;
	};

	const QString exportExt = QFileInfo(fileName).suffix().toLower();
	const bool flattenTransforms = (exportExt == "obj" || exportExt == "ply" || exportExt == "stl");

	QMap<QString, unsigned int> animMatRemap; // "origMatIdx@sourceFile" → export material index
	aiScene* copyScene = SceneGraphExporter::buildExportScene(
		_sceneGraph, resolver, flattenTransforms, allowedSourceFiles, &animMatRemap);

	if (!copyScene)
	{
		QMessageBox::critical(this, tr("Error"), tr("Failed to build export scene."));
		return;
	}

	if (exportSelectedOnly && copyScene->mNumMeshes == 0)
	{
		delete copyScene;
		QMessageBox::warning(this, tr("Nothing Selected"),
			tr("None of the selected items are exportable meshes."));
		return;
	}
	// ------------------------------------------------------------------------------

	// The autoOrient+autoScale correction is factored out inside buildExportScene()
	// via the importCorrection stored on each fileNode.

	// Collect punctual lights per exported file from their PARSED positions
	// and un-bake each file's importCorrection (the autoOrient/autoScale
	// transform applied at load).  The geometry exporter factors the exact
	// same per-fileNode correction out of the node tree, so lights stay
	// aligned with the geometry.  Using the per-file correction — instead of
	// ViewportWidget's global scene transform, which reflects only the LAST Assimp
	// load — also keeps MVF-restored sessions correct, where that global
	// transform is identity while the parsed light positions still carry the
	// baked-in correction.
	std::vector<GPULight> lights;
	{
		// Only the exported file's lights — other loaded models' lights must
		// not leak into the output.
		QStringList lightFiles;
		if (selectedSourceFile.isEmpty())
			lightFiles = _sceneGraph->filesWithLights();
		else if (_sceneGraph->filesWithLights().contains(selectedSourceFile))
			lightFiles << selectedSourceFile;

		for (const QString& file : lightFiles)
		{
			glm::mat4 inverseCorrection(1.0f);
			if (const SceneNode* fileNode = _sceneGraph->findFileNode(file))
			{
				if (!fileNode->importCorrection.IsIdentity())
					inverseCorrection = glm::inverse(
						SceneUtils::aiMatrixToGlm(fileNode->importCorrection));
			}

			const glm::vec3 invScale(
				glm::length(glm::vec3(inverseCorrection[0])),
				glm::length(glm::vec3(inverseCorrection[1])),
				glm::length(glm::vec3(inverseCorrection[2])));
			const float avgScale = (invScale.x + invScale.y + invScale.z) / 3.0f;

			const GltfLightData ld = _sceneGraph->lightDataForFile(file);
			for (const GltfLightEntry& entry : ld.lights)
			{
				GPULight light = entry.gpuLight;
				light.position = glm::vec3(
					inverseCorrection * glm::vec4(light.position, 1.0f));
				light.direction = glm::normalize(glm::vec3(
					inverseCorrection * glm::vec4(light.direction, 0.0f)));
				light.range *= avgScale;
				lights.push_back(light);
			}
		}
	}

	const bool formatSupportsTextures = (exportExt != "ply" && exportExt != "stl");
	AssImpMeshExporter::ExportSettings expSettings;
	expSettings.outputDirectory  = QFileInfo(fileName).absolutePath();
	expSettings.copyTextures     = formatSupportsTextures;
	expSettings.useRelativePaths = true;
	expSettings.deduplicateTextures = true;
	expSettings.verbose = true;
	expSettings.lights  = lights;

	// Collect glTF cameras for all exported files.
	{
		const QStringList camFiles = _sceneGraph->filesWithGltfCameras();
		for (const QString& file : camFiles)
		{
			// Only include cameras from files that are part of this export.
			if (!selectedSourceFile.isEmpty() && file != selectedSourceFile)
				continue;
			const GltfCameraData camData = _sceneGraph->gltfCameraDataForFile(file);
			for (const GltfCameraEntry& cam : camData.cameras)
				expSettings.cameras.append(cam);
		}
	}

	// Note: user-captured views ("Capture View" in the Cameras tab) are
	// already included by the loop above - they're stored as regular
	// GltfCameraEntry data under SceneGraph's synthetic
	// capturedViewsSourceFileKey() bucket, which filesWithGltfCameras()
	// naturally picks up like any other file. See GltfCameraData.h.

	// Collect variant names so KHR_materials_variants is preserved on glTF export.
	{
		// Use variant data from the selected source file if known, else first available.
		QString variantFile = selectedSourceFile.isEmpty()
		                      ? (_sceneGraph->filesWithVariants().isEmpty()
		                         ? QString() : _sceneGraph->filesWithVariants().first())
		                      : selectedSourceFile;
		if (!variantFile.isEmpty())
		{
			GltfVariantData vd = _sceneGraph->variantDataForFile(variantFile);
			expSettings.variantNames = vd.variantNames;
		}
	}

	// Collect glTF animation data for Pointer-channel injection (KHR_animation_pointer).
	// Pointer channels (material texture-transform, node visibility) are stored in
	// GltfAnimationData but cannot be expressed as Assimp aiAnimation channels.
	// We pass them to GltfPostProcessor via ExportSettings so they are re-injected
	// into the output glTF/GLB after Assimp writes the file.
	{
		const QStringList animFiles = _sceneGraph->filesWithAnimations();
		for (const QString& file : animFiles)
		{
			if (!selectedSourceFile.isEmpty() && file != selectedSourceFile)
				continue;
			const GltfAnimationData animData = _sceneGraph->animationDataForFile(file);
			if (!animData.isEmpty() && (animData.hasPointerAnimations || animData.hasMorphAnimations))
				expSettings.animationDataList.append(animData);
		}

		// Remap pointer-animation targetMaterialIndex values to the actual indices used
		// in the exported aiScene.  The original material indices stored in
		// GltfAnimationChannel::targetMaterialIndex were assigned by the ORIGINAL glTF
		// loader and may not match the export-scene order (which depends on the DFS
		// traversal order of the scene graph).  Without this remap the injected
		// KHR_animation_pointer paths reference the wrong material on re-import.
		if (!animMatRemap.isEmpty())
		{
			for (GltfAnimationData& animData : expSettings.animationDataList)
			{
				for (GltfAnimationClip& clip : animData.clips)
				{
					for (GltfAnimationChannel& ch : clip.channels)
					{
						if (ch.targetPath != GltfAnimationTargetPath::Pointer)
							continue;
						if (ch.targetMaterialIndex < 0)
							continue;

						const QString remapKey =
							QString::number(ch.targetMaterialIndex)
							+ QLatin1Char('@')
							+ animData.sourceFile;
						const auto it = animMatRemap.constFind(remapKey);
						if (it != animMatRemap.constEnd())
						{
							const int newIdx = static_cast<int>(it.value());
							if (newIdx != ch.targetMaterialIndex)
							{
								qDebug() << "[EXPORT-ANIM-REMAP] sourceFile=" << animData.sourceFile
								         << "material" << ch.targetMaterialIndex << "->" << newIdx;
								ch.targetMaterialIndex = newIdx;
							}
						}
					}
				}
			}
		}
	}

	// Both "Export Whole Scene" and "Export Selected Meshes" use the same
	// hierarchy-aware path — copyScene/triMeshes were already built above with the
	// selection filter (if any) applied via the resolver, preserving node transforms
	// and tree structure for both modes.
	AssImpMeshExporter meshExporter(this);
	aiReturn res = meshExporter.exportScene(copyScene, triMeshes, fileName.toStdString(), expSettings);
	qDebug() << "Exporting scene result:" << res;
	delete copyScene;

	if (res == aiReturn_SUCCESS)
		QMessageBox::information(this, tr("Information"), tr("Exported %1").arg(QFileInfo(fileName).fileName()));
	else
		QMessageBox::critical(this, tr("Error"), tr("Export failed!"));
}


bool ModelViewer::loadFile(const QString& fileName)
{
	_lastOpenedDir = QFileInfo(fileName).path(); // store path for next time

	QString errMsg;
	bool success = false;
	const QString suffix = QFileInfo(fileName).suffix().toLower();
	const bool isNativeSession = (suffix == "mvf");
	if (isNativeSession)
	{
		// Load native ModelViewer session file
		success = loadFromFile(fileName);
	}
	else
	{
		UVMethod method;
		QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
		bool remember = settings.value("RememberUVMethod", false).toBool();
		if (remember)
		{
			int value = settings.value("UVMethod", static_cast<int>(UVMethod::None)).toInt();
			method = static_cast<UVMethod>(value);
		}
		else
			method = askUserForUVMethod(this).method;

		_progressiveLoadingEnabled = settings.value("checkProgressiveLoading", true).toBool();
		_animateProgressiveFitEnabled = settings.value("checkAnimateProgressiveFit", true).toBool();
		success = _viewportWidget->loadAssImpModel(fileName, method, errMsg, _progressiveLoadingEnabled);
	}

	if (success && !_viewportWidget->getMeshStore().empty())
	{
		if (!isNativeSession)
		{
			updateDisplayList();
			markNonUndoDocumentModified();
		}

		treeWidgetModel->scrollToTop();

		MainWindow::showStatusMessage(tr("File loaded"), 2000);

		return success;
	}
	else
	{
		if (errMsg == "Model loading cancelled by user.")
		{
			return false;
		}

		QApplication::restoreOverrideCursor();
		QMessageBox::critical(this, tr("Error"), QString(tr("Failed to load model %1")).arg(fileName) + "\n" + errMsg);
		QApplication::setOverrideCursor(Qt::WaitCursor);

		return false;
	}


	return false;
}

bool ModelViewer::loadFromFile(const QString& fileName)
{
	// -- Show progress bar (no cancel button for MVF) ------------------
	QString displayFileName = QFileInfo(fileName).fileName();
	MainWindow::showStatusMessage(tr("Reading file: ") + displayFileName);
	MainWindow::showProgressBar(false);
	MainWindow::setProgressValue(0);

	// Ensure the GL context / shader is ready before the worker starts.
	_viewportWidget->makeCurrent();
	if (!_viewportWidget->getShader())
	{
		_viewportWidget->update();
		QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
		_viewportWidget->makeCurrent();
	}
	if (!_viewportWidget->getShader())
	{
		MainWindow::hideProgressBar();
		return false;
	}

	// ---------------------------------------------------------------
	// Single worker thread mirrors the AssImp pattern:
	//   • File I/O, JSON parse, vertex assembly run on the worker.
	//   • Each mesh's GL upload is dispatched to the main thread via
	//     BlockingQueuedConnection, so the main event loop stays
	//     fully alive between uploads — exactly like
	//     AssImpModelLoader::meshBatchReady → onMeshBatchReady.
	// ---------------------------------------------------------------
	struct LoadResult
	{
		Mvf::Document document;
		std::vector<GPULight> lights;
		QVector<GltfVariantData> variantDataByFile;
		QHash<QString, int> activeVariantByFile;
		QVector<GltfAnimationData> animationDataByFile;
		QHash<QString, int> activeAnimationByFile;
		QVector<GltfCameraData> cameraDataByFile;
		QJsonArray    explodedViews;
		QString       activeExplodedViewId;
		int           activeExplodedViewStepIndex = -1;
		QString       activeGltfCameraFile;
		int           activeGltfCameraIndex = -1;
		QJsonObject   viewerState;
		QVector<Measurement> measurements;
		QVector<Annotation> annotations;
		QVector<SelectionSet> selectionSets;
		QVector<SceneState> sceneStates;
		bool          ok       = false;
		bool          badMagic = false;
	};

	LoadResult result;
	QEventLoop waitLoop;
	QThread    workerThread;

	auto* worker = new QObject();
	worker->moveToThread(&workerThread);

	connect(&workerThread, &QThread::started, worker,
		[&result, &fileName, &waitLoop, this, &displayFileName, progressiveMode = _progressiveLoadingEnabled]()
	{
		// --- Phase 1: File I/O ----------------------------------------
		QFile file(fileName);
		if (!file.open(QIODevice::ReadOnly))
		{
			waitLoop.quit();
			return;
		}
		QDataStream in(&file);

		quint32 magic = 0;
		in >> magic;
		if (in.status() != QDataStream::Ok)       { waitLoop.quit(); return; }
		if (magic != Mvf::Magic)                   { result.badMagic = true; waitLoop.quit(); return; }

		file.seek(0);
		in.device()->seek(0);

		Mvf::Header header;
		if (!Mvf::readHeader(in, header) || !Mvf::isSupportedHeader(header))
		{
			waitLoop.quit();
			return;
		}

		QByteArray jsonPayload, geomChunk, imgChunk;
		while (in.status() == QDataStream::Ok && !in.atEnd())
		{
			Mvf::ChunkHeader chunkHeader;
			if (!Mvf::readChunkHeader(in, chunkHeader))
				break;
			const QByteArray chunkPayload = Mvf::readChunkPayload(in, chunkHeader);
			switch (chunkHeader.type)
			{
			case Mvf::ChunkType::Json:     jsonPayload = chunkPayload; break;
			case Mvf::ChunkType::Geometry: geomChunk   = chunkPayload; break;
			case Mvf::ChunkType::Images:   imgChunk    = chunkPayload; break;
			default: break;
			}
		}
		if (jsonPayload.isEmpty()) { waitLoop.quit(); return; }

		// --- Phase 2: JSON parse + vertex/material assembly (CPU) ------
		QMetaObject::invokeMethod(_viewportWidget, [&displayFileName]() {
			MainWindow::showStatusMessage(
				QObject::tr("Preparing meshes: ") + displayFileName);
			MainWindow::setProgressValue(10);
		}, Qt::BlockingQueuedConnection);

		result.document = Mvf::fromJsonBytes(jsonPayload);
		const QJsonObject session = result.document.mvfSession;

		auto jsonArrayToVec3 = [](const QJsonArray& arr, const QVector3D& fallback = QVector3D()) {
			if (arr.size() < 3)
				return fallback;
			return QVector3D(
				static_cast<float>(arr[0].toDouble()),
				static_cast<float>(arr[1].toDouble()),
				static_cast<float>(arr[2].toDouble()));
		};

		auto jsonArrayToGlmVec3 = [&](const QJsonArray& arr, const glm::vec3& fallback = glm::vec3(0.0f)) {
			const QVector3D vec = jsonArrayToVec3(
				arr, QVector3D(fallback.x, fallback.y, fallback.z));
			return glm::vec3(vec.x(), vec.y(), vec.z());
		};

		for (const QJsonValue& lightValue : session[QStringLiteral("lights")].toArray())
		{
			const QJsonObject lightObj = lightValue.toObject();
			GPULight light{};
			light.type = lightObj[QStringLiteral("type")].toInt(static_cast<int>(LightType::Point));
			light.range = static_cast<float>(lightObj[QStringLiteral("range")].toDouble(0.0));
			light.intensity = static_cast<float>(lightObj[QStringLiteral("intensity")].toDouble(1.0));
			light.direction = jsonArrayToGlmVec3(lightObj[QStringLiteral("direction")].toArray());
			light.color = jsonArrayToGlmVec3(lightObj[QStringLiteral("color")].toArray(), glm::vec3(1.0f));
			light.position = jsonArrayToGlmVec3(lightObj[QStringLiteral("position")].toArray());
			light.innerConeCos = static_cast<float>(lightObj[QStringLiteral("innerConeCos")].toDouble(0.0));
			light.outerConeCos = static_cast<float>(lightObj[QStringLiteral("outerConeCos")].toDouble(0.0));
			result.lights.push_back(light);
		}

		for (const QJsonValue& fileValue : session[QStringLiteral("variantFiles")].toArray())
		{
			const QJsonObject fileObj = fileValue.toObject();
			const QString sourceFile = fileObj[QStringLiteral("sourceFile")].toString();
			if (sourceFile.isEmpty())
				continue;

			GltfVariantData variantData;
			variantData.sourceFile = sourceFile;

			for (const QJsonValue& nameValue : fileObj[QStringLiteral("variantNames")].toArray())
				variantData.variantNames.append(nameValue.toString());

			for (const QJsonValue& mappingValue : fileObj[QStringLiteral("meshVariantMappings")].toArray())
			{
				const QJsonObject mappingObj = mappingValue.toObject();
				const int sceneIndex = mappingObj[QStringLiteral("sceneIndex")].toInt(-1);
				if (sceneIndex < 0)
					continue;

				QVector<GltfVariantMapping> mappings;
				for (const QJsonValue& variantMappingValue : mappingObj[QStringLiteral("variantMappings")].toArray())
				{
					const QJsonObject variantMappingObj = variantMappingValue.toObject();
					GltfVariantMapping mapping;
					mapping.materialIndex = variantMappingObj[QStringLiteral("materialIndex")].toInt(-1);
					for (const QJsonValue& variantIndexValue : variantMappingObj[QStringLiteral("variantIndices")].toArray())
						mapping.variantIndices.append(variantIndexValue.toInt(-1));
					mappings.append(mapping);
				}
				variantData.meshVariantMappings.insert(sceneIndex, mappings);
			}

			result.variantDataByFile.append(variantData);
			result.activeVariantByFile.insert(
				sourceFile,
				fileObj[QStringLiteral("activeVariant")].toInt(-1));
		}

		for (const QJsonValue& fileValue : session[QStringLiteral("cameraFiles")].toArray())
		{
			const QJsonObject fileObj = fileValue.toObject();
			const QString sourceFile = fileObj[QStringLiteral("sourceFile")].toString();
			if (sourceFile.isEmpty())
				continue;

			GltfCameraData cameraData;
			cameraData.sourceFile = sourceFile;
			const QString cameraSourceSuffix = QFileInfo(sourceFile).suffix().toLower();
			const bool isDirectGltfCameraSource =
				(cameraSourceSuffix == QLatin1String("gltf") || cameraSourceSuffix == QLatin1String("glb"));

			for (const QJsonValue& cameraValue : fileObj[QStringLiteral("cameras")].toArray())
			{
				const QJsonObject cameraObj = cameraValue.toObject();
				GltfCameraEntry camera;
				camera.name = cameraObj[QStringLiteral("name")].toString();
				camera.nodeName = cameraObj[QStringLiteral("nodeName")].toString();
				camera.nodeIndex = cameraObj[QStringLiteral("nodeIndex")].toInt(-1);
				camera.hasAiChildPath = cameraObj[QStringLiteral("hasAiChildPath")].toBool(false);
				for (const QJsonValue& pathValue : cameraObj[QStringLiteral("aiChildPath")].toArray())
					camera.aiChildPath.append(pathValue.toInt(-1));
				camera.type = cameraObj[QStringLiteral("type")].toString() == QLatin1String("orthographic")
					? GltfCameraType::Orthographic
					: GltfCameraType::Perspective;
				camera.fovYRadians = static_cast<float>(cameraObj[QStringLiteral("fovYRadians")].toDouble(camera.fovYRadians));
				camera.zNear = static_cast<float>(cameraObj[QStringLiteral("zNear")].toDouble(camera.zNear));
				camera.zFar = static_cast<float>(cameraObj[QStringLiteral("zFar")].toDouble(camera.zFar));
				camera.xMag = static_cast<float>(cameraObj[QStringLiteral("xMag")].toDouble(camera.xMag));
				camera.yMag = static_cast<float>(cameraObj[QStringLiteral("yMag")].toDouble(camera.yMag));
				camera.worldPosition = jsonArrayToVec3(cameraObj[QStringLiteral("worldPosition")].toArray());
				camera.worldDirection = jsonArrayToVec3(
					cameraObj[QStringLiteral("worldDirection")].toArray(), QVector3D(0.0f, 0.0f, -1.0f));
				camera.worldUp = jsonArrayToVec3(
					cameraObj[QStringLiteral("worldUp")].toArray(), QVector3D(0.0f, 1.0f, 0.0f));
				camera.needsModelTransformCompensation =
					cameraObj[QStringLiteral("needsModelTransformCompensation")].toBool(false);
				if (isDirectGltfCameraSource)
					camera.needsModelTransformCompensation = false;
				camera.capturedViewRange = static_cast<float>(
					cameraObj[QStringLiteral("capturedViewRange")].toDouble(-1.0));

				// needsNewNode isn't itself persisted - every camera in the
				// captured-views bucket is synthetic by construction (no
				// authored glTF node backs it), so re-derive the flag from
				// which bucket it's loading into rather than needing a new
				// JSON field. Without this, a captured view would still
				// export after an MVF round-trip, but as an orphaned camera
				// definition unwired to any node (writeGltfCameras() would
				// try - and fail - to name-match it like an authored one).
				camera.needsNewNode = (sourceFile == capturedViewsSourceFileKey());

				cameraData.cameras.append(camera);
			}

			result.cameraDataByFile.append(cameraData);
		}

		result.activeGltfCameraFile = session[QStringLiteral("activeGltfCameraFile")].toString();
		result.activeGltfCameraIndex = session[QStringLiteral("activeGltfCameraIndex")].toInt(-1);
		result.explodedViews = session[QStringLiteral("explodedViews")].toArray();
		result.activeExplodedViewId = session[QStringLiteral("activeExplodedViewId")].toString();
		result.activeExplodedViewStepIndex = session[QStringLiteral("activeExplodedViewStepIndex")].toInt(-1);
		result.viewerState = session[QStringLiteral("viewerState")].toObject();

		auto jsonArrayToVec3Measurement = [](const QJsonArray& arr) {
			if (arr.size() < 3)
				return QVector3D();
			return QVector3D(static_cast<float>(arr[0].toDouble()),
			                  static_cast<float>(arr[1].toDouble()),
			                  static_cast<float>(arr[2].toDouble()));
		};
		const QJsonArray measurementsArr = session[QStringLiteral("measurements")].toArray();
		result.measurements.reserve(measurementsArr.size());
		for (const QJsonValue& measurementVal : measurementsArr)
		{
			const QJsonObject measurementObj = measurementVal.toObject();

			Measurement m;
			m.id = QUuid(measurementObj[QStringLiteral("id")].toString());
			m.name = measurementObj[QStringLiteral("name")].toString();
			m.type = static_cast<MeasurementType>(measurementObj[QStringLiteral("type")].toInt(
				static_cast<int>(MeasurementType::Point)));
			m.visible = measurementObj[QStringLiteral("visible")].toBool(true);
			m.offsetReferenceDir = jsonArrayToVec3Measurement(measurementObj[QStringLiteral("offsetReferenceDir")].toArray());
			m.offsetDistance = static_cast<float>(measurementObj[QStringLiteral("offsetDistance")].toDouble());
			m.offsetVector = jsonArrayToVec3Measurement(measurementObj[QStringLiteral("offsetVector")].toArray());

			const QJsonArray anchorsArr = measurementObj[QStringLiteral("anchors")].toArray();
			m.anchors.reserve(anchorsArr.size());
			for (const QJsonValue& anchorVal : anchorsArr)
			{
				const QJsonObject anchorObj = anchorVal.toObject();

				MeasurementAnchorRef ref;
				ref.meshUuid = QUuid(anchorObj[QStringLiteral("meshUuid")].toString());
				ref.triangleIndex = anchorObj[QStringLiteral("triangleIndex")].toInt(-1);
				ref.barycentric = jsonArrayToVec3Measurement(anchorObj[QStringLiteral("barycentric")].toArray());
				ref.snappedVertexIndex = anchorObj[QStringLiteral("snappedVertexIndex")].toInt(-1);
				ref.edgeIndex = anchorObj[QStringLiteral("edgeIndex")].toInt(-1);
				m.anchors.append(ref);
			}

			if (!m.id.isNull() && !m.anchors.isEmpty())
				result.measurements.append(m);
		}

		const QJsonArray annotationsArr = session[QStringLiteral("annotations")].toArray();
		result.annotations.reserve(annotationsArr.size());
		for (const QJsonValue& annotationVal : annotationsArr)
		{
			const QJsonObject annotationObj = annotationVal.toObject();

			Annotation a;
			a.id = QUuid(annotationObj[QStringLiteral("id")].toString());
			a.text = annotationObj[QStringLiteral("text")].toString();
			a.visible = annotationObj[QStringLiteral("visible")].toBool(true);
			a.offsetReferenceDir = jsonArrayToVec3Measurement(annotationObj[QStringLiteral("offsetReferenceDir")].toArray());
			a.leaderOffset = jsonArrayToVec3Measurement(annotationObj[QStringLiteral("leaderOffset")].toArray());

			const QJsonObject anchorObj = annotationObj[QStringLiteral("anchor")].toObject();
			a.anchor.meshUuid = QUuid(anchorObj[QStringLiteral("meshUuid")].toString());
			a.anchor.triangleIndex = anchorObj[QStringLiteral("triangleIndex")].toInt(-1);
			a.anchor.barycentric = jsonArrayToVec3Measurement(anchorObj[QStringLiteral("barycentric")].toArray());
			a.anchor.snappedVertexIndex = anchorObj[QStringLiteral("snappedVertexIndex")].toInt(-1);
			a.anchor.edgeIndex = anchorObj[QStringLiteral("edgeIndex")].toInt(-1);

			if (!a.id.isNull() && a.anchor.isValid())
				result.annotations.append(a);
		}

		const QJsonArray selectionSetsArr = session[QStringLiteral("selectionSets")].toArray();
		result.selectionSets.reserve(selectionSetsArr.size());
		for (const QJsonValue& setVal : selectionSetsArr)
		{
			const QJsonObject setObj = setVal.toObject();

			SelectionSet s;
			s.id = QUuid(setObj[QStringLiteral("id")].toString());
			s.name = setObj[QStringLiteral("name")].toString();
			for (const QJsonValue& uv : setObj[QStringLiteral("meshUuids")].toArray())
				s.meshUuids.insert(QUuid(uv.toString()));

			if (!s.id.isNull())
				result.selectionSets.append(s);
		}

		const QJsonArray sceneStatesArr = session[QStringLiteral("sceneStates")].toArray();
		result.sceneStates.reserve(sceneStatesArr.size());
		for (const QJsonValue& stateVal : sceneStatesArr)
		{
			const QJsonObject stateObj = stateVal.toObject();

			SceneState st;
			st.id = QUuid(stateObj[QStringLiteral("id")].toString());
			st.name = stateObj[QStringLiteral("name")].toString();

			const QJsonObject cameraObj = stateObj[QStringLiteral("camera")].toObject();
			GltfCameraEntry& cam = st.camera;
			cam.name = cameraObj[QStringLiteral("name")].toString();
			cam.type = cameraObj[QStringLiteral("type")].toString() == QLatin1String("orthographic")
				? GltfCameraType::Orthographic
				: GltfCameraType::Perspective;
			cam.fovYRadians = static_cast<float>(cameraObj[QStringLiteral("fovYRadians")].toDouble(cam.fovYRadians));
			cam.zNear = static_cast<float>(cameraObj[QStringLiteral("zNear")].toDouble(cam.zNear));
			cam.zFar = static_cast<float>(cameraObj[QStringLiteral("zFar")].toDouble(cam.zFar));
			cam.xMag = static_cast<float>(cameraObj[QStringLiteral("xMag")].toDouble(cam.xMag));
			cam.yMag = static_cast<float>(cameraObj[QStringLiteral("yMag")].toDouble(cam.yMag));
			cam.worldPosition = jsonArrayToVec3(cameraObj[QStringLiteral("worldPosition")].toArray());
			cam.worldDirection = jsonArrayToVec3(
				cameraObj[QStringLiteral("worldDirection")].toArray(), QVector3D(0.0f, 0.0f, -1.0f));
			cam.worldUp = jsonArrayToVec3(
				cameraObj[QStringLiteral("worldUp")].toArray(), QVector3D(0.0f, 1.0f, 0.0f));
			cam.needsModelTransformCompensation = false; // always false for a live-captured snapshot - see captureCurrentCameraEntry()
			cam.needsNewNode = true; // synthetic, same as any captured view - see GltfCameraData.h
			cam.capturedViewRange = static_cast<float>(
				cameraObj[QStringLiteral("capturedViewRange")].toDouble(-1.0));

			for (const QJsonValue& uv : stateObj[QStringLiteral("visibleMeshUuids")].toArray())
				st.visibleMeshUuids.insert(QUuid(uv.toString()));
			for (const QJsonValue& uv : stateObj[QStringLiteral("selectedMeshUuids")].toArray())
				st.selectedMeshUuids.insert(QUuid(uv.toString()));

			// Presentation state (see SceneStateData.h) - local jsonToColor
			// copy since the shared one below is defined later in this same
			// function, out of scope here (same per-block duplication
			// convention the vec3ToJson/jsonArrayToVec3 lambdas already use
			// throughout this function).
			st.displayMode = stateObj[QStringLiteral("displayMode")].toInt(st.displayMode);
			st.renderingMode = stateObj[QStringLiteral("renderingMode")].toString(st.renderingMode);
			st.groundMode = stateObj[QStringLiteral("groundMode")].toInt(st.groundMode);
			st.skyBoxShown = stateObj[QStringLiteral("skyBoxShown")].toBool(st.skyBoxShown);
			st.skyBoxHDRIEnabled = stateObj[QStringLiteral("skyBoxHDRIEnabled")].toBool(st.skyBoxHDRIEnabled);
			st.skyBoxFolderPath = stateObj[QStringLiteral("skyBoxFolderPath")].toString(st.skyBoxFolderPath);
			st.skyBoxBlurPercent = stateObj[QStringLiteral("skyBoxBlurPercent")].toInt(st.skyBoxBlurPercent);
			st.skyBoxFOV = stateObj[QStringLiteral("skyBoxFOV")].toDouble(st.skyBoxFOV);
			st.skyBoxZRotationDegrees = stateObj[QStringLiteral("skyBoxZRotationDegrees")].toDouble(st.skyBoxZRotationDegrees);

			st.floorTextureShown = stateObj[QStringLiteral("floorTextureShown")].toBool(st.floorTextureShown);
			st.floorTexturePath = stateObj[QStringLiteral("floorTexturePath")].toString(st.floorTexturePath);
			st.floorTexRepeatS = stateObj[QStringLiteral("floorTexRepeatS")].toDouble(st.floorTexRepeatS);
			st.floorTexRepeatT = stateObj[QStringLiteral("floorTexRepeatT")].toDouble(st.floorTexRepeatT);
			st.floorOffsetPercent = stateObj[QStringLiteral("floorOffsetPercent")].toDouble(st.floorOffsetPercent);
			st.shadowQuality = stateObj[QStringLiteral("shadowQuality")].toInt(st.shadowQuality);
			st.reflectionsEnabled = stateObj[QStringLiteral("reflectionsEnabled")].toBool(st.reflectionsEnabled);
			st.shadowsEnabled = stateObj[QStringLiteral("shadowsEnabled")].toBool(st.shadowsEnabled);
			st.selfShadowsEnabled = stateObj[QStringLiteral("selfShadowsEnabled")].toBool(st.selfShadowsEnabled);
			st.shadowCatcherDarkness = static_cast<float>(stateObj[QStringLiteral("shadowCatcherDarkness")].toDouble(st.shadowCatcherDarkness));
			st.shadowCatcherMetalness = static_cast<float>(stateObj[QStringLiteral("shadowCatcherMetalness")].toDouble(st.shadowCatcherMetalness));
			st.shadowCatcherRoughness = static_cast<float>(stateObj[QStringLiteral("shadowCatcherRoughness")].toDouble(st.shadowCatcherRoughness));

			st.environmentEnabled = stateObj[QStringLiteral("environmentEnabled")].toBool(st.environmentEnabled);
			st.iblEnabled = stateObj[QStringLiteral("iblEnabled")].toBool(st.iblEnabled);
			st.envMapExposureStops = stateObj[QStringLiteral("envMapExposureStops")].toDouble(st.envMapExposureStops);
			st.iblExposureStops = stateObj[QStringLiteral("iblExposureStops")].toDouble(st.iblExposureStops);

			st.defaultLightsEnabled = stateObj[QStringLiteral("defaultLightsEnabled")].toBool(st.defaultLightsEnabled);
			st.punctualLightsEnabled = stateObj[QStringLiteral("punctualLightsEnabled")].toBool(st.punctualLightsEnabled);
			st.showLights = stateObj[QStringLiteral("showLights")].toBool(st.showLights);

			st.hdrToneMapping = stateObj[QStringLiteral("hdrToneMapping")].toBool(st.hdrToneMapping);
			st.hdrToneMappingMode = stateObj[QStringLiteral("hdrToneMappingMode")].toInt(st.hdrToneMappingMode);
			st.gammaCorrection = stateObj[QStringLiteral("gammaCorrection")].toBool(st.gammaCorrection);
			st.screenGamma = stateObj[QStringLiteral("screenGamma")].toDouble(st.screenGamma);

			auto stateJsonToColor = [](const QJsonArray& arr, const QColor& fallback) {
				if (arr.size() < 4)
					return fallback;
				return QColor(arr[0].toInt(fallback.red()),
				              arr[1].toInt(fallback.green()),
				              arr[2].toInt(fallback.blue()),
				              arr[3].toInt(fallback.alpha()));
			};
			st.bgTopColor = stateJsonToColor(stateObj[QStringLiteral("bgTopColor")].toArray(), st.bgTopColor);
			st.bgBotColor = stateJsonToColor(stateObj[QStringLiteral("bgBotColor")].toArray(), st.bgBotColor);

			const QJsonArray stateCatcherColorArr = stateObj[QStringLiteral("shadowCatcherBaseColor")].toArray();
			if (stateCatcherColorArr.size() >= 3)
			{
				st.shadowCatcherBaseColor = QVector3D(
					static_cast<float>(stateCatcherColorArr[0].toDouble()),
					static_cast<float>(stateCatcherColorArr[1].toDouble()),
					static_cast<float>(stateCatcherColorArr[2].toDouble()));
			}

			const QJsonArray stateLightColorArr = stateObj[QStringLiteral("defaultLightColor")].toArray();
			if (stateLightColorArr.size() == 4)
			{
				st.defaultLightColor = QVector4D(
					static_cast<float>(stateLightColorArr[0].toDouble(1.0)),
					static_cast<float>(stateLightColorArr[1].toDouble(1.0)),
					static_cast<float>(stateLightColorArr[2].toDouble(1.0)),
					static_cast<float>(stateLightColorArr[3].toDouble(1.0)));
			}

			const QJsonArray stateLightOffsetArr = stateObj[QStringLiteral("defaultLightOffset")].toArray();
			if (stateLightOffsetArr.size() == 3)
			{
				st.defaultLightOffset = QVector3D(
					static_cast<float>(stateLightOffsetArr[0].toDouble(0.0)),
					static_cast<float>(stateLightOffsetArr[1].toDouble(0.0)),
					static_cast<float>(stateLightOffsetArr[2].toDouble(0.0)));
			}

			if (!st.id.isNull())
				result.sceneStates.append(st);
		}

		auto jsonArrayToQuat = [](const QJsonArray& arr, const QQuaternion& fallback = QQuaternion()) {
			if (arr.size() < 4)
				return fallback;
			return QQuaternion(
				static_cast<float>(arr[0].toDouble()),
				static_cast<float>(arr[1].toDouble()),
				static_cast<float>(arr[2].toDouble()),
				static_cast<float>(arr[3].toDouble()));
		};

		auto jsonToAiMatrix = [](const QJsonArray& mat) {
			aiMatrix4x4 m;
			if (mat.size() == 16)
			{
				m.a1 = static_cast<float>(mat[0].toDouble());  m.a2 = static_cast<float>(mat[1].toDouble());
				m.a3 = static_cast<float>(mat[2].toDouble());  m.a4 = static_cast<float>(mat[3].toDouble());
				m.b1 = static_cast<float>(mat[4].toDouble());  m.b2 = static_cast<float>(mat[5].toDouble());
				m.b3 = static_cast<float>(mat[6].toDouble());  m.b4 = static_cast<float>(mat[7].toDouble());
				m.c1 = static_cast<float>(mat[8].toDouble());  m.c2 = static_cast<float>(mat[9].toDouble());
				m.c3 = static_cast<float>(mat[10].toDouble()); m.c4 = static_cast<float>(mat[11].toDouble());
				m.d1 = static_cast<float>(mat[12].toDouble()); m.d2 = static_cast<float>(mat[13].toDouble());
				m.d3 = static_cast<float>(mat[14].toDouble()); m.d4 = static_cast<float>(mat[15].toDouble());
			}
			return m;
		};

		for (const QJsonValue& fileValue : session[QStringLiteral("animationFiles")].toArray())
		{
			const QJsonObject fileObj = fileValue.toObject();
			const QString sourceFile = fileObj[QStringLiteral("sourceFile")].toString();
			if (sourceFile.isEmpty())
				continue;

			GltfAnimationData animationData;
			animationData.sourceFile = sourceFile;
			animationData.hasNodeAnimations = fileObj[QStringLiteral("hasNodeAnimations")].toBool(false);
			animationData.hasSkinning = fileObj[QStringLiteral("hasSkinning")].toBool(false);
			animationData.hasMorphAnimations = fileObj[QStringLiteral("hasMorphAnimations")].toBool(false);
			animationData.hasPointerAnimations = fileObj[QStringLiteral("hasPointerAnimations")].toBool(false);
			animationData.rootInverseTransform = jsonToAiMatrix(
				fileObj[QStringLiteral("rootInverseTransform")].toArray());

			for (const QJsonValue& bindingValue : fileObj[QStringLiteral("nodeBindings")].toArray())
			{
				const QJsonObject bindingObj = bindingValue.toObject();
				GltfAnimationNodeBinding binding;
				binding.nodeIndex = bindingObj[QStringLiteral("nodeIndex")].toInt(-1);
				binding.nodeName = bindingObj[QStringLiteral("nodeName")].toString();
				binding.hasAiChildPath = bindingObj[QStringLiteral("hasAiChildPath")].toBool(false);
				for (const QJsonValue& pathValue : bindingObj[QStringLiteral("aiChildPath")].toArray())
					binding.aiChildPath.append(pathValue.toInt(-1));
				animationData.nodeBindings.append(binding);
			}

			for (const QJsonValue& stateValue : fileObj[QStringLiteral("nodeVisibilityStates")].toArray())
			{
				const QJsonObject stateObj = stateValue.toObject();
				GltfAnimationNodeVisibilityState state;
				state.nodeIndex = stateObj[QStringLiteral("nodeIndex")].toInt(-1);
				state.parentNodeIndex = stateObj[QStringLiteral("parentNodeIndex")].toInt(-1);
				state.nodeName = stateObj[QStringLiteral("nodeName")].toString();
				state.defaultVisible = stateObj[QStringLiteral("defaultVisible")].toBool(true);
				animationData.nodeVisibilityStates.append(state);
			}

			for (const QJsonValue& bindingValue : fileObj[QStringLiteral("lightBindings")].toArray())
			{
				const QJsonObject bindingObj = bindingValue.toObject();
				GltfAnimationLightBinding binding;
				binding.parsedLightIndex = bindingObj[QStringLiteral("parsedLightIndex")].toInt(-1);
				binding.lightDefinitionIndex = bindingObj[QStringLiteral("lightDefinitionIndex")].toInt(-1);
				binding.nodeIndex = bindingObj[QStringLiteral("nodeIndex")].toInt(-1);
				binding.nodeName = bindingObj[QStringLiteral("nodeName")].toString();
				animationData.lightBindings.append(binding);
			}

			for (const QJsonValue& clipValue : fileObj[QStringLiteral("clips")].toArray())
			{
				const QJsonObject clipObj = clipValue.toObject();
				GltfAnimationClip clip;
				clip.name = clipObj[QStringLiteral("name")].toString();
				clip.durationSeconds = clipObj[QStringLiteral("durationSeconds")].toDouble(0.0);
				clip.hasNodeTransforms = clipObj[QStringLiteral("hasNodeTransforms")].toBool(false);
				clip.hasSkinning = clipObj[QStringLiteral("hasSkinning")].toBool(false);
				clip.hasMorphAnimations = clipObj[QStringLiteral("hasMorphAnimations")].toBool(false);
				clip.hasPointerAnimations = clipObj[QStringLiteral("hasPointerAnimations")].toBool(false);

				for (const QJsonValue& channelValue : clipObj[QStringLiteral("channels")].toArray())
				{
					const QJsonObject channelObj = channelValue.toObject();
					GltfAnimationChannel channel;
					channel.targetKind = static_cast<GltfAnimationBindingTargetKind>(
						channelObj[QStringLiteral("targetKind")].toInt(static_cast<int>(GltfAnimationBindingTargetKind::Node)));
					channel.targetNodeName = channelObj[QStringLiteral("targetNodeName")].toString();
					channel.targetNodeIndex = channelObj[QStringLiteral("targetNodeIndex")].toInt(-1);
					channel.targetMeshUuid = QUuid(channelObj[QStringLiteral("targetMeshUuid")].toString());
					channel.targetPath = static_cast<GltfAnimationTargetPath>(
						channelObj[QStringLiteral("targetPath")].toInt(static_cast<int>(GltfAnimationTargetPath::Translation)));
					channel.targetPointer = channelObj[QStringLiteral("targetPointer")].toString();
					channel.pointerTargetKind = static_cast<GltfAnimationPointerTargetKind>(
						channelObj[QStringLiteral("pointerTargetKind")].toInt(static_cast<int>(GltfAnimationPointerTargetKind::None)));
					channel.targetMaterialIndex = channelObj[QStringLiteral("targetMaterialIndex")].toInt(-1);
					channel.textureTarget = static_cast<GltfAnimationTextureTarget>(
						channelObj[QStringLiteral("textureTarget")].toInt(static_cast<int>(GltfAnimationTextureTarget::Unknown)));
					channel.pointerProperty = static_cast<GltfAnimationPointerProperty>(
						channelObj[QStringLiteral("pointerProperty")].toInt(static_cast<int>(GltfAnimationPointerProperty::None)));

					for (const QJsonValue& keyValue : channelObj[QStringLiteral("vec3Keys")].toArray())
					{
						const QJsonObject keyObj = keyValue.toObject();
						channel.vec3Keys.append(GltfAnimationVec3Key{
							keyObj[QStringLiteral("timeSeconds")].toDouble(0.0),
							jsonArrayToVec3(keyObj[QStringLiteral("value")].toArray())
						});
					}

					for (const QJsonValue& keyValue : channelObj[QStringLiteral("vec4Keys")].toArray())
					{
						const QJsonObject keyObj = keyValue.toObject();
						const QJsonArray value = keyObj[QStringLiteral("value")].toArray();
						channel.vec4Keys.append(GltfAnimationVec4Key{
							keyObj[QStringLiteral("timeSeconds")].toDouble(0.0),
							value.size() >= 4 ? QVector4D(
								static_cast<float>(value[0].toDouble()),
								static_cast<float>(value[1].toDouble()),
								static_cast<float>(value[2].toDouble()),
								static_cast<float>(value[3].toDouble())) : QVector4D()
						});
					}

					for (const QJsonValue& keyValue : channelObj[QStringLiteral("quatKeys")].toArray())
					{
						const QJsonObject keyObj = keyValue.toObject();
						channel.quatKeys.append(GltfAnimationQuatKey{
							keyObj[QStringLiteral("timeSeconds")].toDouble(0.0),
							jsonArrayToQuat(keyObj[QStringLiteral("value")].toArray())
						});
					}

					for (const QJsonValue& keyValue : channelObj[QStringLiteral("vec2Keys")].toArray())
					{
						const QJsonObject keyObj = keyValue.toObject();
						const QJsonArray value = keyObj[QStringLiteral("value")].toArray();
						channel.vec2Keys.append(GltfAnimationVec2Key{
							keyObj[QStringLiteral("timeSeconds")].toDouble(0.0),
							value.size() >= 2 ? QVector2D(
								static_cast<float>(value[0].toDouble()),
								static_cast<float>(value[1].toDouble())) : QVector2D()
						});
					}

					for (const QJsonValue& keyValue : channelObj[QStringLiteral("floatKeys")].toArray())
					{
						const QJsonObject keyObj = keyValue.toObject();
						channel.floatKeys.append(GltfAnimationFloatKey{
							keyObj[QStringLiteral("timeSeconds")].toDouble(0.0),
							static_cast<float>(keyObj[QStringLiteral("value")].toDouble(0.0))
						});
					}

					for (const QJsonValue& keyValue : channelObj[QStringLiteral("boolKeys")].toArray())
					{
						const QJsonObject keyObj = keyValue.toObject();
						channel.boolKeys.append(GltfAnimationBoolKey{
							keyObj[QStringLiteral("timeSeconds")].toDouble(0.0),
							keyObj[QStringLiteral("value")].toBool(false)
						});
					}

					for (const QJsonValue& keyValue : channelObj[QStringLiteral("weightKeys")].toArray())
					{
						const QJsonObject keyObj = keyValue.toObject();
						QVector<float> weights;
						for (const QJsonValue& weightValue : keyObj[QStringLiteral("values")].toArray())
							weights.append(static_cast<float>(weightValue.toDouble(0.0)));
						channel.weightKeys.append(GltfAnimationWeightsKey{
							keyObj[QStringLiteral("timeSeconds")].toDouble(0.0),
							weights
						});
					}

					clip.channels.append(channel);
				}

				animationData.clips.append(clip);
			}

			result.animationDataByFile.append(animationData);
			result.activeAnimationByFile.insert(
				sourceFile,
				fileObj[QStringLiteral("activeClip")].toInt(animationData.clips.isEmpty() ? -1 : 0));
		}

		QVector<PreparedMvfMesh> prepared =
			MvfMeshPreparationWorker::prepare(result.document, geomChunk, imgChunk);

		// Extract mesh UUIDs and visibility
		QList<QUuid> allMeshUuids;
		for (const auto& pm : prepared)
			allMeshUuids.append(pm.uuid);

		QSet<QUuid> visibleUuids;
		const QJsonArray visArr = result.document.mvfSession[QStringLiteral("visibleMeshUuids")].toArray();
		for (const QJsonValue& v : visArr)
		{
			const QUuid uuid = QUuid::fromString(v.toString());
			if (!uuid.isNull())
				visibleUuids.insert(uuid);
		}
		if (visibleUuids.isEmpty())
		{
			for (const QUuid& uuid : allMeshUuids)
				visibleUuids.insert(uuid);
		}

		if (!result.viewerState.isEmpty())
		{
			QMetaObject::invokeMethod(_viewportWidget, [this, viewerState = result.viewerState]() {
				_viewportWidget->setCameraUpAxisZUp(
					viewerState[QStringLiteral("cameraUpAxisZUp")].toBool(_viewportWidget->isCameraUpAxisZUp()));
				_viewportWidget->setProjection(static_cast<ViewProjection>(
					viewerState[QStringLiteral("projection")].toInt(static_cast<int>(_viewportWidget->projection()))));

				const int savedCameraMode =
					viewerState[QStringLiteral("cameraMode")].toInt(static_cast<int>(_viewportWidget->cameraMode()));
				switch (savedCameraMode)
				{
				case static_cast<int>(Camera::CameraMode::Fly):
					_viewportWidget->setCameraMode(Camera::CameraMode::Fly);
					break;
				case static_cast<int>(Camera::CameraMode::FirstPerson):
					_viewportWidget->setCameraMode(Camera::CameraMode::FirstPerson);
					break;
				case static_cast<int>(Camera::CameraMode::Orbit):
				default:
					_viewportWidget->setCameraMode(Camera::CameraMode::Orbit);
					break;
				}
			}, Qt::BlockingQueuedConnection);
		}

		QHash<QUuid, QMatrix4x4> preparedMeshWorldByUuid;
		const int mvfSceneIndex = result.document.scene;
		const QJsonArray sceneRootNodes =
			(mvfSceneIndex >= 0 && mvfSceneIndex < result.document.scenes.size())
				? result.document.scenes[mvfSceneIndex][QStringLiteral("nodes")].toArray()
				: QJsonArray{};
		QMetaObject::invokeMethod(this, [this, &result, &sceneRootNodes, &preparedMeshWorldByUuid]() {
			if (!_sceneGraph)
				return;
			_sceneGraph->rebuildFromMvf(result.document.nodes, sceneRootNodes);
			preparedMeshWorldByUuid = _sceneGraph->evaluateWorldTransforms().meshWorldByUuid;
		}, Qt::BlockingQueuedConnection);
		for (PreparedMvfMesh& pm : prepared)
		{
			const auto it = preparedMeshWorldByUuid.constFind(pm.uuid);
			if (it != preparedMeshWorldByUuid.constEnd())
			{
				pm.sceneRenderTransform = it.value();
				pm.hasSceneRenderTransform = true;
			}
		}

		// --- Phase 3: GL upload — dispatched one mesh at a time --------
		//     BlockingQueuedConnection blocks the worker while the main
		//     thread uploads, then returns control to waitLoop.exec()
		//     which processes ALL events (paint, timers, user input)
		//     before the next mesh is dispatched.

		// Clear old meshes and set visibility before uploading
		QMetaObject::invokeMethod(_viewportWidget, [this, &visibleUuids]() {
			_viewportWidget->clearMeshStore();
			_visibleMeshUuids = visibleUuids;
		}, Qt::BlockingQueuedConnection);

		const int totalMeshes = prepared.size();
		for (int i = 0; i < totalMeshes; ++i)
		{
			QMetaObject::invokeMethod(_viewportWidget,
				[this, &prepared, i, totalMeshes, &displayFileName, progressiveMode]()
			{
				const PreparedMvfMesh& pm = prepared[i];
				_viewportWidget->uploadOneMvfMesh(pm);

				const int pct = 15 + (i + 1) * 75 / totalMeshes;
				MainWindow::setProgressValue(pct);
				MainWindow::showStatusMessage(
					tr("Loading mesh %1 / %2").arg(i + 1).arg(totalMeshes));

				// In progressive mode, update display every 20 meshes (matching AssImp's
				// batchSize) so user sees meshes appearing as they load. In non-progressive
				// mode, defer until Phase 3.5 so all meshes appear together after loading completes.
				if (progressiveMode && ((i + 1) % 20 == 0 || (i + 1) == totalMeshes))
					updateDisplayList();
			}, Qt::BlockingQueuedConnection);
		}

		// --- Phase 3.5: Finalize session (still in event loop) ---
		//     Update display to show all pending meshes (either progressively
		//     during Phase 3, or all at once if non-progressive).
		QMetaObject::invokeMethod(this,
			[this, &result, &visibleUuids, &fileName]()
		{
			// Wrap flat MVF light list into a GltfLightData with unnamed entries.
			// Source file is not tracked per-light in the MVF format yet, so the
			// panel won't show these; they still render correctly via the parsed light baseline.
			{
				GltfLightData ld;
				for (const GPULight& gl : result.lights)
				{
					GltfLightEntry e;
					e.gpuLight = gl;
					e.enabled  = true;
					ld.lights.append(e);
				}
				_viewportWidget->setParsedLights(ld);
			}

			// Ensure all mesh UUIDs in _pendingSceneUuids are marked visible
			// In progressive mode, this was already called during Phase 3.
			// In non-progressive mode, this is the first call, so all meshes appear together.
			updateDisplayList();

			// Apply visibility
			_visibleMeshUuids = visibleUuids;
			const bool shouldAutoFit = _viewportWidget->autoFitViewOnUpdate();
			_viewportWidget->setAutoFitViewOnUpdate(false);
			_viewportWidget->setDisplayList(visibleIndicesFromState());
			_viewportWidget->setAutoFitViewOnUpdate(shouldAutoFit);
			updateVisibilityUiFromState();

			// Restore selection
			const QJsonArray selArr = result.document.mvfSession[QStringLiteral("selectedMeshUuids")].toArray();
			QSet<QUuid> selectedUuids;
			for (const QJsonValue& v : selArr)
				selectedUuids.insert(QUuid::fromString(v.toString()));
			setSelectionWithoutUndo(selectedUuids);

			// Clear undo/redo history
			if (_undoStack)
				_undoStack->clear();
			_cachedReferencedUuids.clear();
			_lastUndoIndex = 0;
			_savedUndoIndex = 0;
			_lastStackCount = 0;

			_currentFile = fileName;
			_documentSaved = true;
			_nonUndoDocumentDirty = false;
			setDocumentModified(false);

			MainWindow::setProgressValue(100);
		}, Qt::BlockingQueuedConnection);

		result.ok = true;
		waitLoop.quit();
	});

	workerThread.start();
	waitLoop.exec();          // main event loop fully alive the ENTIRE time

	workerThread.quit();
	workerThread.wait();
	delete worker;

	if (result.badMagic)
	{
		MainWindow::hideProgressBar();
		QMessageBox::critical(this, tr("Error"),
			tr("Unrecognized file format: %1").arg(fileName));
		return false;
	}
	if (!result.ok)
	{
		MainWindow::hideProgressBar();
		return false;
	}

	_viewportWidget->updateView();

	// --- Phase 4: Build tree structure (after event loop exits) ---
	//     All meshes are loaded and visible. Build the tree now.
	//     The logger can still output asynchronously in the background.
	//     For MVF files, reconstruct the original hierarchy from the saved node structure.
	const int sceneIndex = result.document.scene;
	const QJsonArray sceneRootNodes =
		result.document.scenes[sceneIndex][QStringLiteral("nodes")].toArray();
	_sceneGraph->rebuildFromMvf(result.document.nodes, sceneRootNodes);
	// Restore punctual light data.  New MVF files store a per-file structure
	// ("punctualLightsByFile") with display names, enabled flags, and the
	// user's repositioned positions.  Older files fall back to the flat
	// "lights" array under a synthetic "__mvf__" key so they still render.
	{
		const QJsonArray perFileArr =
			result.document.mvfSession[QStringLiteral("punctualLightsByFile")].toArray();

		if (!perFileArr.isEmpty())
		{
			// New format: restore per-file GltfLightData so PunctualLightsPanel
			// can show names and per-light checkboxes.
			auto jsonToVec3 = [](const QJsonArray& a) -> glm::vec3 {
				return glm::vec3(
					static_cast<float>(a[0].toDouble()),
					static_cast<float>(a[1].toDouble()),
					static_cast<float>(a[2].toDouble()));
			};

			for (const QJsonValue& fileVal : perFileArr)
			{
				const QJsonObject fileObj = fileVal.toObject();
				const QString sourceFile  = fileObj[QStringLiteral("sourceFile")].toString();
				if (sourceFile.isEmpty())
					continue;

				GltfLightData ld;
				ld.sourceFile = sourceFile;

				const QJsonArray lightsArr = fileObj[QStringLiteral("lights")].toArray();
				for (const QJsonValue& lightVal : lightsArr)
				{
					const QJsonObject lightObj = lightVal.toObject();
					const QJsonObject gpuObj   = lightObj[QStringLiteral("gpuLight")].toObject();

					GltfLightEntry entry;
					entry.name    = lightObj[QStringLiteral("name")].toString();
					entry.enabled = lightObj[QStringLiteral("enabled")].toBool(true);

					entry.gpuLight.type         = gpuObj[QStringLiteral("type")].toInt();
					entry.gpuLight.range        = static_cast<float>(gpuObj[QStringLiteral("range")].toDouble());
					entry.gpuLight.intensity    = static_cast<float>(gpuObj[QStringLiteral("intensity")].toDouble());
					entry.gpuLight.innerConeCos = static_cast<float>(gpuObj[QStringLiteral("innerConeCos")].toDouble());
					entry.gpuLight.outerConeCos = static_cast<float>(gpuObj[QStringLiteral("outerConeCos")].toDouble());
					entry.gpuLight.color     = jsonToVec3(gpuObj[QStringLiteral("color")].toArray());
					entry.gpuLight.position  = jsonToVec3(gpuObj[QStringLiteral("position")].toArray());
					entry.gpuLight.direction = jsonToVec3(gpuObj[QStringLiteral("direction")].toArray());

					ld.lights.append(entry);
				}

				_sceneGraph->setLightData(sourceFile, ld);
			}
		}
		else if (!result.lights.empty())
		{
			// Legacy flat list: store under a synthetic key.  Lights render correctly
			// but PunctualLightsPanel won't show per-file names or checkboxes.
			GltfLightData ld;
			ld.sourceFile = QStringLiteral("__mvf__");
			for (const GPULight& gl : result.lights)
			{
				GltfLightEntry e;
				e.gpuLight = gl;
				e.enabled  = true;
				ld.lights.append(e);
			}
			_sceneGraph->setLightData(ld.sourceFile, ld);
		}
	}

	for (const GltfVariantData& variantData : result.variantDataByFile)
	{
		if (variantData.sourceFile.isEmpty())
			continue;

		_sceneGraph->setVariantData(variantData.sourceFile, variantData);
		const int activeVariant = result.activeVariantByFile.value(variantData.sourceFile, -1);
		_sceneGraph->setActiveVariant(variantData.sourceFile, activeVariant);
		if (activeVariant >= 0)
			applyVariant(variantData.sourceFile, activeVariant);
	}

	for (const GltfAnimationData& animationData : result.animationDataByFile)
	{
		if (animationData.sourceFile.isEmpty())
			continue;

		_sceneGraph->setAnimationData(animationData.sourceFile, animationData);
		_sceneGraph->setActiveAnimationClip(
			animationData.sourceFile,
			result.activeAnimationByFile.value(animationData.sourceFile, -1));
	}

	for (const GltfCameraData& cameraData : result.cameraDataByFile)
	{
		if (!cameraData.sourceFile.isEmpty())
			_sceneGraph->setGltfCameraData(cameraData.sourceFile, cameraData);
	}

	// Not undoable (SceneGraph::addMeasurement(), not ModelViewer::
	// addMeasurement()) - a fresh load shouldn't seed the undo stack with
	// "create" entries for measurements that already existed in the file.
	for (const Measurement& measurement : result.measurements)
		_sceneGraph->addMeasurement(measurement);

	// Same non-undoable reasoning as the measurements loop above.
	for (const Annotation& annotation : result.annotations)
		_sceneGraph->addAnnotation(annotation);

	// Same non-undoable reasoning as the measurements loop above.
	for (const SelectionSet& set : result.selectionSets)
		_sceneGraph->addSelectionSet(set);

	// Same non-undoable reasoning as the measurements loop above.
	for (const SceneState& state : result.sceneStates)
		_sceneGraph->addSceneState(state);

	for (SceneNode* fileNode : _sceneGraph->root()->children)
	{
		if (fileNode && fileNode->isSynthetic && !fileNode->sourceFile.isEmpty())
			_viewportWidget->syncRuntimeNodeTransforms(fileNode->sourceFile);
	}

	for (const GltfAnimationData& animationData : result.animationDataByFile)
	{
		if (animationData.sourceFile.isEmpty())
			continue;

		const int activeClip = result.activeAnimationByFile.value(animationData.sourceFile, -1);
		if (activeClip >= 0 && activeClip < animationData.clips.size())
			_viewportWidget->setActiveAnimation(animationData.sourceFile, activeClip);
	}

	if (ExplodedViewPanel* explodedViewPanel = _viewportWidget ? _viewportWidget->getExplodedViewPanel() : nullptr)
	{
		explodedViewPanel->restorePresetsFromJson(
			result.explodedViews,
			QUuid(result.activeExplodedViewId),
			result.activeExplodedViewStepIndex);
	}

	// Refit from the authoritative restored scene-graph state. During MVF load
	// the initial Phase 3.5 display list is built before rebuildFromMvf() and
	// syncRuntimeNodeTransforms(), so preserved-node-transform assets can have
	// incorrect bounds/camera framing until we recompute after the hierarchy is
	// restored.
	const bool shouldAutoFit = _viewportWidget->autoFitViewOnUpdate();
	_viewportWidget->setAutoFitViewOnUpdate(shouldAutoFit);
	_viewportWidget->setDisplayList(visibleIndicesFromState());
	_viewportWidget->setAutoFitViewOnUpdate(shouldAutoFit);

	auto jsonToColor = [](const QJsonArray& arr, const QColor& fallback = QColor()) {
		if (arr.size() < 4)
			return fallback;
		return QColor(arr[0].toInt(fallback.red()),
		              arr[1].toInt(fallback.green()),
		              arr[2].toInt(fallback.blue()),
		              arr[3].toInt(fallback.alpha()));
	};

	if (!result.viewerState.isEmpty())
	{
		const QJsonObject& viewerState = result.viewerState;
		// Older MVF files simply lack this key, correctly yielding Unknown -
		// resolveEffectiveImportUnit() then falls through to the hardcoded
		// Millimeter default, same as today's behavior for every such file.
		_defaultImportUnit = lengthUnitFromString(
			viewerState[QStringLiteral("defaultImportUnit")].toString(), LengthUnit::Unknown);
		_viewportWidget->setCameraUpAxisZUp(
			viewerState[QStringLiteral("cameraUpAxisZUp")].toBool(_viewportWidget->isCameraUpAxisZUp()));
		_viewportWidget->setProjection(static_cast<ViewProjection>(
			viewerState[QStringLiteral("projection")].toInt(static_cast<int>(_viewportWidget->projection()))));
		const int savedCameraMode =
			viewerState[QStringLiteral("cameraMode")].toInt(static_cast<int>(_viewportWidget->cameraMode()));
		{
			// Migration: old REALSHADED=5, old FLATSHADED=6 (pre-refactor enum values)
			const int savedMode = viewerState[QStringLiteral("displayMode")].toInt(
				static_cast<int>(_viewportWidget->getDisplayMode()));
			if (savedMode == 5)
			{
				_viewportWidget->setDisplayMode(DisplayMode::SHADED);
				_viewportWidget->setRealismEnabled(true);
			}
			else if (savedMode == 6)
			{
				_viewportWidget->setDisplayMode(DisplayMode::SHADED);
				_viewportWidget->setShadingNormalMode(ShadingNormalMode::FLAT);
			}
			else
			{
				_viewportWidget->setDisplayMode(static_cast<DisplayMode>(savedMode));
			}
			// New sessions store realismEnabled explicitly; old sessions rely on migration above.
			if (viewerState.contains(QStringLiteral("realismEnabled")))
				_viewportWidget->setRealismEnabled(viewerState[QStringLiteral("realismEnabled")].toBool());
		}
		_viewportWidget->setRenderingMode(static_cast<RenderingMode>(
			viewerState[QStringLiteral("renderingMode")].toInt(static_cast<int>(_viewportWidget->getRenderingMode()))));
		{
			GroundMode loadedGroundMode = static_cast<GroundMode>(
				viewerState[QStringLiteral("groundMode")].toInt(static_cast<int>(_viewportWidget->groundMode())));
			// Migration: documents saved before GroundMode::InfinitePlane
			// existed persisted "shadow catcher" as a separate bool layered
			// on top of GroundMode::Floor (Visualization panel's old
			// checkBoxShadowCatcher) instead of its own ground mode - fold
			// that combination into the new dedicated mode so old documents
			// keep opening with the same effective look.
			if (loadedGroundMode == GroundMode::Floor
				&& viewerState[QStringLiteral("shadowCatcherEnabled")].toBool(false))
				loadedGroundMode = GroundMode::InfinitePlane;
			_viewportWidget->setGroundMode(loadedGroundMode);
		}
		_viewportWidget->showFloorTexture(
			viewerState[QStringLiteral("floorTextureShown")].toBool(_viewportWidget->isFloorTextureShown()));
		// floorTexturePath/floorTexRepeatS/T/floorOffsetPercent/shadowQuality
		// were a genuine pre-existing gap in this block - floorTextureShown
		// only ever persisted WHETHER a floor texture is shown, never which
		// image file, its UV repeat, the floor's vertical offset, or the
		// shadow quality preset, so a document reload silently lost all four
		// even though the panel/viewport have always supported changing them.
		const QString floorTexPath =
			viewerState[QStringLiteral("floorTexturePath")].toString(_viewportWidget->getFloorTexturePath());
		if (!floorTexPath.isEmpty())
			_viewportWidget->setFloorTextureFromPath(floorTexPath);
		_viewportWidget->setFloorTexRepeatS(
			viewerState[QStringLiteral("floorTexRepeatS")].toDouble(static_cast<double>(_viewportWidget->getFloorTexRepeatS())));
		_viewportWidget->setFloorTexRepeatT(
			viewerState[QStringLiteral("floorTexRepeatT")].toDouble(static_cast<double>(_viewportWidget->getFloorTexRepeatT())));
		_viewportWidget->setFloorOffsetPercent(
			viewerState[QStringLiteral("floorOffsetPercent")].toDouble(static_cast<double>(_viewportWidget->getFloorOffsetPercent())));
		_viewportWidget->setShadowQuality(static_cast<AdaptiveShadowMapper::QualityLevel>(
			viewerState[QStringLiteral("shadowQuality")].toInt(static_cast<int>(_viewportWidget->getShadowQuality()))));
		_viewportWidget->showReflections(
			viewerState[QStringLiteral("reflectionsEnabled")].toBool(_viewportWidget->areReflectionsEnabled()));
		_viewportWidget->setShadowCatcherDarkness(static_cast<float>(
			viewerState[QStringLiteral("shadowCatcherDarkness")].toDouble(static_cast<double>(_viewportWidget->shadowCatcherDarkness()))));
		{
			const QJsonArray catcherColorArr = viewerState[QStringLiteral("shadowCatcherBaseColor")].toArray();
			QVector3D catcherColor = _viewportWidget->shadowCatcherBaseColor();
			if (catcherColorArr.size() >= 3)
			{
				catcherColor = QVector3D(
					static_cast<float>(catcherColorArr[0].toDouble()),
					static_cast<float>(catcherColorArr[1].toDouble()),
					static_cast<float>(catcherColorArr[2].toDouble()));
			}
			_viewportWidget->setShadowCatcherBaseColor(catcherColor);
		}
		_viewportWidget->setShadowCatcherMetalness(static_cast<float>(
			viewerState[QStringLiteral("shadowCatcherMetalness")].toDouble(static_cast<double>(_viewportWidget->shadowCatcherMetalness()))));
		_viewportWidget->setShadowCatcherRoughness(static_cast<float>(
			viewerState[QStringLiteral("shadowCatcherRoughness")].toDouble(static_cast<double>(_viewportWidget->shadowCatcherRoughness()))));
		_viewportWidget->showShadows(
			viewerState[QStringLiteral("shadowsEnabled")].toBool(_viewportWidget->areShadowsEnabled()));
		_viewportWidget->showSelfShadows(
			viewerState[QStringLiteral("selfShadowsEnabled")].toBool(_viewportWidget->areSelfShadowsEnabled()));
		_viewportWidget->showEnvironment(
			viewerState[QStringLiteral("environmentEnabled")].toBool(_viewportWidget->isEnvironmentMapEnabled()));
		_viewportWidget->useIBL(
			viewerState[QStringLiteral("iblEnabled")].toBool(_viewportWidget->isIBLEnabled()));
		_viewportWidget->showSkyBox(
			viewerState[QStringLiteral("skyBoxEnabled")].toBool(_viewportWidget->isSkyBoxShown()));
		_viewportWidget->setSkyBoxTextureHDRI(
			viewerState[QStringLiteral("skyBoxHDRIEnabled")].toBool(_viewportWidget->isSkyBoxHDRIEnabled()));
		_viewportWidget->setSkyBoxBlurPercent(
			viewerState[QStringLiteral("skyBoxBlurPercent")].toInt(_viewportWidget->getSkyBoxBlurPercent()));
		_viewportWidget->setSkyBoxFOV(
			viewerState[QStringLiteral("skyBoxFOV")].toDouble(_viewportWidget->getSkyBoxFOV()));
		_viewportWidget->useDefaultLights(
			viewerState[QStringLiteral("defaultLightsEnabled")].toBool(_viewportWidget->areDefaultLightsEnabled()));
		_viewportWidget->usePunctualLights(
			viewerState[QStringLiteral("punctualLightsEnabled")].toBool(_viewportWidget->arePunctualLightsEnabled()));
		_viewportWidget->showLights(
			viewerState[QStringLiteral("showLights")].toBool(_viewportWidget->areLightsShown()));
		_viewportWidget->enableHDRToneMapping(
			viewerState[QStringLiteral("hdrToneMapping")].toBool(_viewportWidget->getHdrToneMapping()));
		_viewportWidget->setHDRToneMappingMode(static_cast<HDRToneMapMode>(
			viewerState[QStringLiteral("hdrToneMappingMode")].toInt(static_cast<int>(_viewportWidget->getHDRToneMappingMode()))));
		_viewportWidget->enableGammaCorrection(
			viewerState[QStringLiteral("gammaCorrection")].toBool(_viewportWidget->getGammaCorrection()));
		_viewportWidget->setScreenGamma(
			viewerState[QStringLiteral("screenGamma")].toDouble(_viewportWidget->getScreenGamma()));
		_viewportWidget->setEnvMapExposure(
			viewerState[QStringLiteral("envMapExposureStops")].toDouble(std::log2(std::max(_viewportWidget->getEnvMapExposure(), 1.0e-6f))));
		_viewportWidget->setIBLExposure(
			viewerState[QStringLiteral("iblExposureStops")].toDouble(std::log2(std::max(_viewportWidget->getIBLExposure(), 1.0e-6f))));

		const QJsonArray defaultLightColor = viewerState[QStringLiteral("defaultLightColor")].toArray();
		if (defaultLightColor.size() == 4)
		{
			_viewportWidget->setDefaultLightColor(QVector4D(
				static_cast<float>(defaultLightColor[0].toDouble(1.0)),
				static_cast<float>(defaultLightColor[1].toDouble(1.0)),
				static_cast<float>(defaultLightColor[2].toDouble(1.0)),
				static_cast<float>(defaultLightColor[3].toDouble(1.0))));
		}

		const QJsonArray lightOffset = viewerState[QStringLiteral("defaultLightOffset")].toArray();
		if (lightOffset.size() == 3)
		{
			const QVector3D off(
				static_cast<float>(lightOffset[0].toDouble(0.0)),
				static_cast<float>(lightOffset[1].toDouble(0.0)),
				static_cast<float>(lightOffset[2].toDouble(0.0)));
			_viewportWidget->setLightOffset(off);
			// Also push the value back into the panel sliders — updateLightPositionRanges
			// (called earlier via setDisplayList) resets them to defaults, so we need
			// an explicit override here to keep the UI in sync with the restored state.
			visualizationEnvironmentPanel->restoreDefaultLightOffset(off);
		}

		const QString skyboxFolder =
			viewerState[QStringLiteral("skyBoxFolder")].toString(_viewportWidget->getCurrentSkyboxFolder());
		if (!skyboxFolder.isEmpty() && skyboxFolder != _viewportWidget->getCurrentSkyboxFolder())
			_viewportWidget->setSkyBoxTextureFolder(skyboxFolder);

		const double skyBoxZRotation = viewerState[QStringLiteral("skyBoxZRotationDegrees")]
			.toDouble(_viewportWidget->getSkyBoxZRotationDegrees());
		// Decomposes the saved angle back into the panel's preset combo +
		// fine offset slider (now that the rotation control is a preset PLUS
		// a +/-45 degree slider, not just the 4 fixed presets alone) and
		// applies it - see VisualizationEnvironmentPanel::restoreSkyBoxRotationDegrees()'s
		// doc comment. This also fixes a pre-existing gap where this call
		// site applied the rotation directly to the viewport without ever
		// updating comboBoxSkyBoxRotation's displayed selection, leaving the
		// panel showing a stale/default preset after a viewerState load.
		visualizationEnvironmentPanel->restoreSkyBoxRotationDegrees(static_cast<float>(skyBoxZRotation));

		const QJsonArray bgTop = viewerState[QStringLiteral("bgTopColor")].toArray();
		if (bgTop.size() == 4)
			_viewportWidget->setBgTopColor(jsonToColor(bgTop, _viewportWidget->getBgTopColor()));

		const QJsonArray bgBot = viewerState[QStringLiteral("bgBotColor")].toArray();
		if (bgBot.size() == 4)
			_viewportWidget->setBgBotColor(jsonToColor(bgBot, _viewportWidget->getBgBotColor()));

		switch (savedCameraMode)
		{
		case static_cast<int>(Camera::CameraMode::Fly):
			_viewportWidget->setCameraMode(Camera::CameraMode::Fly);
			break;
		case static_cast<int>(Camera::CameraMode::FirstPerson):
			_viewportWidget->setCameraMode(Camera::CameraMode::FirstPerson);
			break;
		case static_cast<int>(Camera::CameraMode::Orbit):
		default:
			_viewportWidget->setCameraMode(Camera::CameraMode::Orbit);
			break;
		}
	}

	if (!result.activeGltfCameraFile.isEmpty() && result.activeGltfCameraIndex >= 0)
		_viewportWidget->activateGltfCamera(result.activeGltfCameraFile, result.activeGltfCameraIndex);

	MainWindow::hideProgressBar();
	return true;
}

Mvf::MVFPackage ModelViewer::buildMVFPackage() const
{
	QSet<QUuid> selectedSet;
	for (const QUuid& uuid : treeWidgetModel->selectedMeshUuids())
		selectedSet.insert(uuid);

	QVector<GltfCameraData> cameraDataByFile;
	const QStringList cameraFiles = _sceneGraph->filesWithGltfCameras();
	cameraDataByFile.reserve(cameraFiles.size());
	for (const QString& sourceFile : cameraFiles)
	{
		const GltfCameraData cameraData = _sceneGraph->gltfCameraDataForFile(sourceFile);
		if (cameraData.isEmpty())
			continue;

		cameraDataByFile.append(
			_viewportWidget ? _viewportWidget->cameraDataForMvfSave(cameraData) : cameraData);
	}

	Mvf::MVFPackage package = Mvf::buildMVFPackage(*_sceneGraph,
	                                               _viewportWidget ? _viewportWidget->getMeshStore() : std::vector<SceneMesh*>(),
	                                               _visibleMeshUuids,
	                                               selectedSet,
	                                               cameraDataByFile);

	if (_viewportWidget)
	{
		if (ExplodedViewPanel* explodedViewPanel = _viewportWidget->getExplodedViewPanel())
		{
			package.document.mvfSession.insert(
				QStringLiteral("explodedViews"),
				explodedViewPanel->presetsToJson());
			package.document.mvfSession.insert(
				QStringLiteral("activeExplodedViewId"),
				explodedViewPanel->activePresetId().toString(QUuid::WithoutBraces));
			package.document.mvfSession.insert(
				QStringLiteral("activeExplodedViewStepIndex"),
				explodedViewPanel->activeCapturedStepIndex());
		}
	}

	if (_viewportWidget && _viewportWidget->isGltfCameraActive())
	{
		package.document.mvfSession.insert(
			QStringLiteral("activeGltfCameraFile"),
			_viewportWidget->activeGltfCameraFile());
		package.document.mvfSession.insert(
			QStringLiteral("activeGltfCameraIndex"),
			_viewportWidget->activeGltfCameraIndex());
	}

	// ---- Measurements ----
	// Document-level (see MeasurementData.h), same as the exploded-view
	// presets above - v1 scope is MVF-session-only (glTF has no native
	// concept of a measurement, unlike a camera), so this is the only
	// place they're persisted at all.
	if (_sceneGraph && !_sceneGraph->measurements().isEmpty())
	{
		auto vec3ToJson = [](const QVector3D& v) {
			return QJsonArray{ static_cast<double>(v.x()), static_cast<double>(v.y()), static_cast<double>(v.z()) };
		};

		QJsonArray measurementsJson;
		for (const Measurement& m : _sceneGraph->measurements())
		{
			QJsonArray anchorsJson;
			for (const MeasurementAnchorRef& ref : m.anchors)
			{
				QJsonObject anchorObj;
				anchorObj.insert(QStringLiteral("meshUuid"), ref.meshUuid.toString(QUuid::WithoutBraces));
				anchorObj.insert(QStringLiteral("triangleIndex"), ref.triangleIndex);
				anchorObj.insert(QStringLiteral("barycentric"), vec3ToJson(ref.barycentric));
				anchorObj.insert(QStringLiteral("snappedVertexIndex"), ref.snappedVertexIndex);
				anchorObj.insert(QStringLiteral("edgeIndex"), ref.edgeIndex);
				anchorsJson.append(anchorObj);
			}

			QJsonObject measurementObj;
			measurementObj.insert(QStringLiteral("id"), m.id.toString(QUuid::WithoutBraces));
			measurementObj.insert(QStringLiteral("name"), m.name);
			measurementObj.insert(QStringLiteral("type"), static_cast<int>(m.type));
			measurementObj.insert(QStringLiteral("visible"), m.visible);
			measurementObj.insert(QStringLiteral("offsetReferenceDir"), vec3ToJson(m.offsetReferenceDir));
			measurementObj.insert(QStringLiteral("offsetDistance"), static_cast<double>(m.offsetDistance));
			measurementObj.insert(QStringLiteral("offsetVector"), vec3ToJson(m.offsetVector));
			measurementObj.insert(QStringLiteral("anchors"), anchorsJson);
			measurementsJson.append(measurementObj);
		}
		package.document.mvfSession.insert(QStringLiteral("measurements"), measurementsJson);
	}

	// ---- Annotations ----
	// Document-level (see AnnotationData.h), same MVF-session-only v1 scope
	// as Measurements above - mirrors that block exactly, just with a single
	// anchor object (not an array) and text/leaderOffset instead of
	// anchors/offsetDistance/offsetVector.
	if (_sceneGraph && !_sceneGraph->annotations().isEmpty())
	{
		auto vec3ToJson = [](const QVector3D& v) {
			return QJsonArray{ static_cast<double>(v.x()), static_cast<double>(v.y()), static_cast<double>(v.z()) };
		};

		QJsonArray annotationsJson;
		for (const Annotation& a : _sceneGraph->annotations())
		{
			QJsonObject anchorObj;
			anchorObj.insert(QStringLiteral("meshUuid"), a.anchor.meshUuid.toString(QUuid::WithoutBraces));
			anchorObj.insert(QStringLiteral("triangleIndex"), a.anchor.triangleIndex);
			anchorObj.insert(QStringLiteral("barycentric"), vec3ToJson(a.anchor.barycentric));
			anchorObj.insert(QStringLiteral("snappedVertexIndex"), a.anchor.snappedVertexIndex);
			anchorObj.insert(QStringLiteral("edgeIndex"), a.anchor.edgeIndex);

			QJsonObject annotationObj;
			annotationObj.insert(QStringLiteral("id"), a.id.toString(QUuid::WithoutBraces));
			annotationObj.insert(QStringLiteral("text"), a.text);
			annotationObj.insert(QStringLiteral("anchor"), anchorObj);
			annotationObj.insert(QStringLiteral("visible"), a.visible);
			annotationObj.insert(QStringLiteral("offsetReferenceDir"), vec3ToJson(a.offsetReferenceDir));
			annotationObj.insert(QStringLiteral("leaderOffset"), vec3ToJson(a.leaderOffset));
			annotationsJson.append(annotationObj);
		}
		package.document.mvfSession.insert(QStringLiteral("annotations"), annotationsJson);
	}

	// ---- Named Selection Sets ----
	// Document-level (see SelectionSetData.h), same MVF-session-only v1
	// scope as Measurements/Annotations above - a selection set has no
	// glTF-native concept either.
	if (_sceneGraph && !_sceneGraph->selectionSets().isEmpty())
	{
		QJsonArray setsJson;
		for (const SelectionSet& s : _sceneGraph->selectionSets())
		{
			QJsonArray uuidsArr;
			for (const QUuid& u : s.meshUuids)
				uuidsArr.append(u.toString(QUuid::WithoutBraces));

			QJsonObject setObj;
			setObj.insert(QStringLiteral("id"), s.id.toString(QUuid::WithoutBraces));
			setObj.insert(QStringLiteral("name"), s.name);
			setObj.insert(QStringLiteral("meshUuids"), uuidsArr);
			setsJson.append(setObj);
		}
		package.document.mvfSession.insert(QStringLiteral("selectionSets"), setsJson);
	}

	// ---- Named Scene States ----
	// Document-level (see SceneStateData.h), same MVF-session-only v1 scope
	// as Measurements/Annotations/Selection Sets above. A scene state's
	// camera field is its OWN private snapshot (not one of the per-file glTF
	// cameras cameraDataByFile collects below), so it needs its own small
	// field-by-field encoding here rather than reusing that collection.
	if (_sceneGraph && !_sceneGraph->sceneStates().isEmpty())
	{
		auto vec3ToJson = [](const QVector3D& v) {
			return QJsonArray{ static_cast<double>(v.x()), static_cast<double>(v.y()), static_cast<double>(v.z()) };
		};

		QJsonArray statesJson;
		for (const SceneState& s : _sceneGraph->sceneStates())
		{
			const GltfCameraEntry& cam = s.camera;
			QJsonObject cameraObj;
			cameraObj.insert(QStringLiteral("name"), cam.name);
			cameraObj.insert(QStringLiteral("type"),
				cam.type == GltfCameraType::Orthographic ? QStringLiteral("orthographic") : QStringLiteral("perspective"));
			cameraObj.insert(QStringLiteral("fovYRadians"), static_cast<double>(cam.fovYRadians));
			cameraObj.insert(QStringLiteral("zNear"), static_cast<double>(cam.zNear));
			cameraObj.insert(QStringLiteral("zFar"), static_cast<double>(cam.zFar));
			cameraObj.insert(QStringLiteral("xMag"), static_cast<double>(cam.xMag));
			cameraObj.insert(QStringLiteral("yMag"), static_cast<double>(cam.yMag));
			cameraObj.insert(QStringLiteral("worldPosition"), vec3ToJson(cam.worldPosition));
			cameraObj.insert(QStringLiteral("worldDirection"), vec3ToJson(cam.worldDirection));
			cameraObj.insert(QStringLiteral("worldUp"), vec3ToJson(cam.worldUp));
			cameraObj.insert(QStringLiteral("capturedViewRange"), static_cast<double>(cam.capturedViewRange));

			QJsonArray visArr;
			for (const QUuid& u : s.visibleMeshUuids)
				visArr.append(u.toString(QUuid::WithoutBraces));
			QJsonArray selArr;
			for (const QUuid& u : s.selectedMeshUuids)
				selArr.append(u.toString(QUuid::WithoutBraces));

			// Local colorToJson copy - the shared one below is defined later
			// in this same function, out of scope here.
			auto stateColorToJson = [](const QColor& color) {
				return QJsonArray{ color.red(), color.green(), color.blue(), color.alpha() };
			};

			QJsonObject stateObj;
			stateObj.insert(QStringLiteral("id"), s.id.toString(QUuid::WithoutBraces));
			stateObj.insert(QStringLiteral("name"), s.name);
			stateObj.insert(QStringLiteral("camera"), cameraObj);
			stateObj.insert(QStringLiteral("visibleMeshUuids"), visArr);
			stateObj.insert(QStringLiteral("selectedMeshUuids"), selArr);
			// Presentation state (see SceneStateData.h).
			stateObj.insert(QStringLiteral("displayMode"), s.displayMode);
			stateObj.insert(QStringLiteral("renderingMode"), s.renderingMode);
			stateObj.insert(QStringLiteral("groundMode"), s.groundMode);
			stateObj.insert(QStringLiteral("skyBoxShown"), s.skyBoxShown);
			stateObj.insert(QStringLiteral("skyBoxHDRIEnabled"), s.skyBoxHDRIEnabled);
			stateObj.insert(QStringLiteral("skyBoxFolderPath"), s.skyBoxFolderPath);
			stateObj.insert(QStringLiteral("skyBoxBlurPercent"), s.skyBoxBlurPercent);
			stateObj.insert(QStringLiteral("skyBoxFOV"), s.skyBoxFOV);
			stateObj.insert(QStringLiteral("skyBoxZRotationDegrees"), s.skyBoxZRotationDegrees);

			stateObj.insert(QStringLiteral("floorTextureShown"), s.floorTextureShown);
			stateObj.insert(QStringLiteral("floorTexturePath"), s.floorTexturePath);
			stateObj.insert(QStringLiteral("floorTexRepeatS"), s.floorTexRepeatS);
			stateObj.insert(QStringLiteral("floorTexRepeatT"), s.floorTexRepeatT);
			stateObj.insert(QStringLiteral("floorOffsetPercent"), s.floorOffsetPercent);
			stateObj.insert(QStringLiteral("shadowQuality"), s.shadowQuality);
			stateObj.insert(QStringLiteral("reflectionsEnabled"), s.reflectionsEnabled);
			stateObj.insert(QStringLiteral("shadowsEnabled"), s.shadowsEnabled);
			stateObj.insert(QStringLiteral("selfShadowsEnabled"), s.selfShadowsEnabled);
			stateObj.insert(QStringLiteral("shadowCatcherDarkness"), static_cast<double>(s.shadowCatcherDarkness));
			stateObj.insert(QStringLiteral("shadowCatcherBaseColor"), QJsonArray{
				s.shadowCatcherBaseColor.x(), s.shadowCatcherBaseColor.y(), s.shadowCatcherBaseColor.z()});
			stateObj.insert(QStringLiteral("shadowCatcherMetalness"), static_cast<double>(s.shadowCatcherMetalness));
			stateObj.insert(QStringLiteral("shadowCatcherRoughness"), static_cast<double>(s.shadowCatcherRoughness));

			stateObj.insert(QStringLiteral("environmentEnabled"), s.environmentEnabled);
			stateObj.insert(QStringLiteral("iblEnabled"), s.iblEnabled);
			stateObj.insert(QStringLiteral("envMapExposureStops"), s.envMapExposureStops);
			stateObj.insert(QStringLiteral("iblExposureStops"), s.iblExposureStops);

			stateObj.insert(QStringLiteral("defaultLightsEnabled"), s.defaultLightsEnabled);
			stateObj.insert(QStringLiteral("punctualLightsEnabled"), s.punctualLightsEnabled);
			stateObj.insert(QStringLiteral("showLights"), s.showLights);
			stateObj.insert(QStringLiteral("defaultLightColor"), QJsonArray{
				s.defaultLightColor.x(), s.defaultLightColor.y(), s.defaultLightColor.z(), s.defaultLightColor.w()});
			stateObj.insert(QStringLiteral("defaultLightOffset"), QJsonArray{
				s.defaultLightOffset.x(), s.defaultLightOffset.y(), s.defaultLightOffset.z()});

			stateObj.insert(QStringLiteral("hdrToneMapping"), s.hdrToneMapping);
			stateObj.insert(QStringLiteral("hdrToneMappingMode"), s.hdrToneMappingMode);
			stateObj.insert(QStringLiteral("gammaCorrection"), s.gammaCorrection);
			stateObj.insert(QStringLiteral("screenGamma"), s.screenGamma);

			stateObj.insert(QStringLiteral("bgTopColor"), stateColorToJson(s.bgTopColor));
			stateObj.insert(QStringLiteral("bgBotColor"), stateColorToJson(s.bgBotColor));
			statesJson.append(stateObj);
		}
		package.document.mvfSession.insert(QStringLiteral("sceneStates"), statesJson);
	}

	// Note: user-captured views ("Capture View" in the Cameras tab) need no
	// separate save block - they're regular GltfCameraData under
	// SceneGraph's synthetic capturedViewsSourceFileKey() bucket, so the
	// cameraDataByFile collection above (built from filesWithGltfCameras())
	// already includes and saves them like any other file's cameras.

	// Whole block guarded, not each call individually (confirmed via
	// clang-analyzer: fixing the getMeshStore() null-deref above let its
	// path-sensitive analysis reach this far and flag every one of these
	// as the same unguarded-_viewportWidget bug) - viewerState only reads
	// _viewportWidget and writes into local/package objects, no other
	// side effects, so there's nothing meaningful to save here at all
	// without a viewport, matching the explodedViews/activeGltfCamera
	// blocks just above.
	if (_viewportWidget)
	{
		auto colorToJson = [](const QColor& color) {
			return QJsonArray{ color.red(), color.green(), color.blue(), color.alpha() };
		};

		QJsonObject viewerState;
		viewerState.insert(QStringLiteral("cameraUpAxisZUp"), _viewportWidget->isCameraUpAxisZUp());
		viewerState.insert(QStringLiteral("projection"), static_cast<int>(_viewportWidget->projection()));
		viewerState.insert(QStringLiteral("cameraMode"), static_cast<int>(_viewportWidget->cameraMode()));
		viewerState.insert(QStringLiteral("displayMode"), static_cast<int>(_viewportWidget->getDisplayMode()));
		viewerState.insert(QStringLiteral("realismEnabled"), _viewportWidget->isRealismEnabled());
		viewerState.insert(QStringLiteral("renderingMode"), static_cast<int>(_viewportWidget->getRenderingMode()));
		viewerState.insert(QStringLiteral("groundMode"), static_cast<int>(_viewportWidget->groundMode()));
		viewerState.insert(QStringLiteral("floorTextureShown"), _viewportWidget->isFloorTextureShown());
		viewerState.insert(QStringLiteral("floorTexturePath"), _viewportWidget->getFloorTexturePath());
		viewerState.insert(QStringLiteral("floorTexRepeatS"), _viewportWidget->getFloorTexRepeatS());
		viewerState.insert(QStringLiteral("floorTexRepeatT"), _viewportWidget->getFloorTexRepeatT());
		viewerState.insert(QStringLiteral("floorOffsetPercent"), _viewportWidget->getFloorOffsetPercent());
		viewerState.insert(QStringLiteral("shadowQuality"), static_cast<int>(_viewportWidget->getShadowQuality()));
		viewerState.insert(QStringLiteral("reflectionsEnabled"), _viewportWidget->areReflectionsEnabled());
		viewerState.insert(QStringLiteral("shadowCatcherEnabled"), _viewportWidget->isShadowCatcherEnabled());
		viewerState.insert(QStringLiteral("shadowCatcherDarkness"), static_cast<double>(_viewportWidget->shadowCatcherDarkness()));
		{
			const QVector3D catcherColor = _viewportWidget->shadowCatcherBaseColor();
			viewerState.insert(QStringLiteral("shadowCatcherBaseColor"),
				QJsonArray{ static_cast<double>(catcherColor.x()), static_cast<double>(catcherColor.y()), static_cast<double>(catcherColor.z()) });
		}
		viewerState.insert(QStringLiteral("shadowCatcherMetalness"), static_cast<double>(_viewportWidget->shadowCatcherMetalness()));
		viewerState.insert(QStringLiteral("shadowCatcherRoughness"), static_cast<double>(_viewportWidget->shadowCatcherRoughness()));
		viewerState.insert(QStringLiteral("shadowsEnabled"), _viewportWidget->areShadowsEnabled());
		viewerState.insert(QStringLiteral("selfShadowsEnabled"), _viewportWidget->areSelfShadowsEnabled());
		viewerState.insert(QStringLiteral("environmentEnabled"), _viewportWidget->isEnvironmentMapEnabled());
		viewerState.insert(QStringLiteral("iblEnabled"), _viewportWidget->isIBLEnabled());
		viewerState.insert(QStringLiteral("skyBoxEnabled"), _viewportWidget->isSkyBoxShown());
		viewerState.insert(QStringLiteral("skyBoxHDRIEnabled"), _viewportWidget->isSkyBoxHDRIEnabled());
		viewerState.insert(QStringLiteral("skyBoxBlurPercent"), _viewportWidget->getSkyBoxBlurPercent());
		viewerState.insert(QStringLiteral("skyBoxFOV"), _viewportWidget->getSkyBoxFOV());
		viewerState.insert(QStringLiteral("skyBoxZRotationDegrees"), _viewportWidget->getSkyBoxZRotationDegrees());
		viewerState.insert(QStringLiteral("skyBoxFolder"), _viewportWidget->getCurrentSkyboxFolder());
		viewerState.insert(QStringLiteral("defaultLightsEnabled"), _viewportWidget->areDefaultLightsEnabled());
		viewerState.insert(QStringLiteral("punctualLightsEnabled"), _viewportWidget->arePunctualLightsEnabled());
		viewerState.insert(QStringLiteral("showLights"), _viewportWidget->areLightsShown());
		viewerState.insert(QStringLiteral("hdrToneMapping"), _viewportWidget->getHdrToneMapping());
		viewerState.insert(QStringLiteral("hdrToneMappingMode"), static_cast<int>(_viewportWidget->getHDRToneMappingMode()));
		viewerState.insert(QStringLiteral("gammaCorrection"), _viewportWidget->getGammaCorrection());
		viewerState.insert(QStringLiteral("screenGamma"), _viewportWidget->getScreenGamma());
		viewerState.insert(QStringLiteral("envMapExposureStops"), std::log2(std::max(_viewportWidget->getEnvMapExposure(), 1.0e-6f)));
		viewerState.insert(QStringLiteral("iblExposureStops"), std::log2(std::max(_viewportWidget->getIBLExposure(), 1.0e-6f)));
		viewerState.insert(QStringLiteral("defaultLightColor"), QJsonArray{
			_viewportWidget->getDefaultLightColor().x(),
			_viewportWidget->getDefaultLightColor().y(),
			_viewportWidget->getDefaultLightColor().z(),
			_viewportWidget->getDefaultLightColor().w()});
		const QVector3D lightOffset = _viewportWidget->getLightOffset();
		viewerState.insert(QStringLiteral("defaultLightOffset"), QJsonArray{
			lightOffset.x(), lightOffset.y(), lightOffset.z()});
		viewerState.insert(QStringLiteral("bgTopColor"), colorToJson(_viewportWidget->getBgTopColor()));
		viewerState.insert(QStringLiteral("bgBotColor"), colorToJson(_viewportWidget->getBgBotColor()));
		// Only-write-if-non-default, same convention SceneNode::importUnit
		// uses on the per-node side - an old MVF reader/file simply lacks
		// this key, correctly falling through to the hardcoded Millimeter
		// default (see resolveEffectiveImportUnit()'s own doc comment).
		if (_defaultImportUnit != LengthUnit::Unknown)
			viewerState.insert(QStringLiteral("defaultImportUnit"), lengthUnitToString(_defaultImportUnit));
		package.document.mvfSession.insert(QStringLiteral("viewerState"), viewerState);
	}

	// ---- Per-file punctual light data ----
	// Save the ORIGINAL parsed positions together with each light's display
	// name and enabled flag, grouped by source file.  Mesh user TRS is saved
	// separately (meshTrs in primitive extras); on load the light positions
	// are re-derived from parsed positions + restored mesh TRS by
	// updatePunctualLights(), so baking the transform here would apply it
	// twice.  The flat "lights" key written by older versions is superseded.
	if (_viewportWidget && _sceneGraph)
	{
		const QStringList lightFiles = _sceneGraph->filesWithLights();

		if (!lightFiles.isEmpty())
		{
			// Helper to serialise one GPULight
			auto gpuLightToJson = [](const GPULight& gl) -> QJsonObject {
				return QJsonObject{
					{QStringLiteral("type"),         gl.type},
					{QStringLiteral("range"),        static_cast<double>(gl.range)},
					{QStringLiteral("intensity"),    static_cast<double>(gl.intensity)},
					{QStringLiteral("innerConeCos"), static_cast<double>(gl.innerConeCos)},
					{QStringLiteral("outerConeCos"), static_cast<double>(gl.outerConeCos)},
					{QStringLiteral("color"),        QJsonArray{gl.color.x, gl.color.y, gl.color.z}},
					{QStringLiteral("position"),     QJsonArray{gl.position.x, gl.position.y, gl.position.z}},
					{QStringLiteral("direction"),    QJsonArray{gl.direction.x, gl.direction.y, gl.direction.z}},
				};
			};

			QJsonArray fileArray;
			for (const QString& sourceFile : lightFiles)
			{
				const GltfLightData& ld = _sceneGraph->lightDataForFile(sourceFile);
				if (ld.isEmpty())
					continue;

				QJsonObject fileObj;
				fileObj.insert(QStringLiteral("sourceFile"), sourceFile);

				QJsonArray lightsArr;
				for (int li = 0; li < ld.lights.size(); ++li)
				{
					const GltfLightEntry& entry = ld.lights[li];

					QJsonObject lightObj;
					lightObj.insert(QStringLiteral("name"),    entry.name);
					lightObj.insert(QStringLiteral("enabled"), entry.enabled);
					lightObj.insert(QStringLiteral("gpuLight"), gpuLightToJson(entry.gpuLight));
					lightsArr.append(lightObj);
				}

				fileObj.insert(QStringLiteral("lights"), lightsArr);
				fileArray.append(fileObj);
			}

			if (!fileArray.isEmpty())
				package.document.mvfSession.insert(
					QStringLiteral("punctualLightsByFile"), fileArray);
		}
	}

	return package;
}

bool ModelViewer::saveToFile(const QString& fileName)
{
	// Flush any unsaved material-panel changes to the mesh before building the MVF package.
	// The panel works on a copy of the mesh material; changes become visible in the viewport
	// via texture-cache warming but the mesh's stored material is only updated when the user
	// explicitly clicks Apply.  Silently committing the current panel state here ensures that
	// "save without clicking Apply" still captures the intended textures/properties.
	if (!_currentEditingMeshUuid.isNull())
	{
		const Material* panelMat = predefinedMaterialsPanel->material();
		if (panelMat && _viewportWidget)
		{
			SceneMesh* mesh = _viewportWidget->getMeshByUuid(_currentEditingMeshUuid);
			if (mesh)
			{
				_viewportWidget->makeCurrent();
				Material resolved = ViewportWidget::resolveMaterialTextures(_viewportWidget, *panelMat);
				resolved.setIsGLTFMaterial(true);
				mesh->setMaterial(resolved);
				mesh->setTextureMaps(resolved);
			}
		}
	}

	const Mvf::MVFPackage package = buildMVFPackage();
	const QByteArray jsonPayload = Mvf::toJsonBytes(package.document);
	const QByteArray& geometryPayload = package.geometryChunk;
	const QByteArray& imagePayload = package.imageChunk;

	Mvf::Header header;
	header.fileLength = static_cast<quint32>(
		sizeof(quint32) * 4 +
		sizeof(quint32) * 2 + jsonPayload.size() +
		sizeof(quint32) * 2 + geometryPayload.size() +
		sizeof(quint32) * 2 + imagePayload.size());

	QFile file(fileName);
	if (!file.open(QIODevice::WriteOnly))
		return false;

	QDataStream out(&file);
	if (!Mvf::writeHeader(out, header))
		return false;
	if (!Mvf::writeChunk(out, Mvf::ChunkType::Json, jsonPayload))
		return false;
	if (!Mvf::writeChunk(out, Mvf::ChunkType::Geometry, geometryPayload))
		return false;
	if (!Mvf::writeChunk(out, Mvf::ChunkType::Images, imagePayload))
		return false;

	return out.status() == QDataStream::Ok;
}

void ModelViewer::setMaterialToSelectedItems(const Material& mat)
{
	_material = mat;
	std::vector<int> ids = getSelectedIDs();
	_viewportWidget->setMaterialToObjects(ids, mat);
	_viewportWidget->updateView();
	updateControls();
}

void ModelViewer::setTexturesToSelectedItems(const Material& mat)
{
	if (checkForActiveSelection())
	{
		_material = mat;
		std::vector<int> ids = getSelectedIDs();
		_viewportWidget->setTexturesToObjects(ids, mat);
		_viewportWidget->updateView();
	}
}

void ModelViewer::setTextureSamplersToSelectedItems(const Material* material, Material::TextureType type)
{
	if (!_viewportWidget) return;
	_viewportWidget->synchronizeTextureCache(material, type);
}

void ModelViewer::switchToRealisticRendering()
{
	_viewportWidget->setRealismEnabled(true);
}

void ModelViewer::onDisplayModeChanged(int mode)
{
	visualizationEnvironmentPanel->onDisplayModeChanged(mode);
}

void ModelViewer::onRenderingModeSelected(const QString& mode)
{
	if (mode == "ADS")
	{
		_viewportWidget->disarmRayTracedRenderingMode();
		_viewportWidget->setRenderingMode(RenderingMode::ADS_BLINN_PHONG);
		visualizationEnvironmentPanel->setPBRLightingMode(false);

		// Explicitly switch Realistic rendering off - ADS is the one mode
		// that must NOT inherit whatever Floor/InfinitePlane +
		// shadows/reflections/env-map state PBR or Ray-Traced left behind.
		// This fires onDisplayModeChanged() with realShaded=false, which
		// (via its existing realShaded-gated defaults) forces GroundMode::
		// None and turns shadows/reflections/env-map/default-lights off, all
		// in one pass - restoreDefaultLightsForAds() right after then
		// overrides just the lights back on for ADS specifically (see its
		// own doc comment for why ADS needs lights but not the rest of
		// realism).
		_viewportWidget->setRealismEnabled(false);
		visualizationEnvironmentPanel->restoreDefaultLightsForAds();
	}
	else if (mode == "PBR")
	{
		_viewportWidget->disarmRayTracedRenderingMode();
		_viewportWidget->setRenderingMode(RenderingMode::PHYSICALLY_BASED_RENDERING);
		visualizationEnvironmentPanel->setPBRLightingMode(true);
		_viewportWidget->setSkyBoxTextureHDRI(true);
		switchToRealisticRendering();
	}
	else if (mode == "RayTraced")
	{
		// Ray-traced mode shows the same PBR raster feed while interacting
		// (identical setup to selecting PBR outright) and layers in a
		// progressively-converging ray-traced image once the camera settles
		// - see ViewportWidget::armRayTracedRenderingMode().
		_viewportWidget->setRenderingMode(RenderingMode::PHYSICALLY_BASED_RENDERING);
		visualizationEnvironmentPanel->setPBRLightingMode(true);
		_viewportWidget->setSkyBoxTextureHDRI(true);
		switchToRealisticRendering();

		// Mirrors onDisplayModeChanged()'s own mode-defining default (Floor +
		// default lights on, unconditionally re-asserted on every switch into
		// realistic shading) but for Ray-Traced mode specifically:
		// GroundMode::InfinitePlane (the shadow-catcher floor) instead of
		// Floor, plus default lights off - a shadow-catcher floor lit only by
		// the flat default headlight looks wrong compared to real
		// environment/skybox lighting. Called BEFORE armRayTracedRenderingMode()
		// (not after) so the FIRST interactive PT session/snapshot it builds
		// already reflects GroundMode::InfinitePlane - calling this after arm()
		// meant the first snapshot got built with whatever groundMode was
		// active before (Floor/None), and setGroundMode()'s own
		// notifyRayTracedSceneMutated() call then had to tear that
		// freshly-armed session down and rebuild it a moment later just to
		// pick up the shadow-catcher floor, instead of getting it right the
		// first time. radioButtonGroundInfinitePlane's own
		// isRayTracedRenderingModeArmed()-gated enablement is still correct
		// by the time this function returns - the unconditional
		// updateControlDependencies() call at the end of this whole function
		// re-evaluates it once armRayTracedRenderingMode() below has actually
		// run.
		visualizationEnvironmentPanel->applyRayTracedGroundDefaultsOnce();

		_viewportWidget->armRayTracedRenderingMode();
	}
	// Update toolbar button to reflect the new rendering mode
	_viewportWidget->getViewToolbar()->updateRenderingModeButton(mode);
	updateControls();
	// Explicit call (not just relying on ViewportWidget::renderingModeChanged) -
	// the "RayTraced" branch above calls armRayTracedRenderingMode() AFTER
	// setRenderingMode(), so a signal fired from setRenderingMode() would
	// re-evaluate radioButtonGroundInfinitePlane's isRayTracedRenderingModeArmed()
	// gate one step too early or on the previous state. This call happens
	// after every branch's arm/disarm has already run, so it's always correct.
	visualizationEnvironmentPanel->updateControlDependencies();
	_viewportWidget->update();
}

void ModelViewer::onTextureCacheCleared()
{
	if (_viewportWidget)
	{
		_viewportWidget->clearTextureCache();
	}
}

void ModelViewer::onPredefinedMaterialSelected(const Material& mat)
{
	// Material application is now handled by MaterialPropertiesPanel.
	Q_UNUSED(mat);
}

void ModelViewer::onCustomMaterialApplied(const Material& mat)
{
	if (!checkForActiveSelection())
		return;

	QApplication::setOverrideCursor(Qt::WaitCursor);

	QVector<QUuid> uuids;
	std::vector<int> ids = getSelectedIDs();
	for (int id : ids)
	{
		QUuid uuid = _viewportWidget->getUuidByIndex(id);
		if (!uuid.isNull())
			uuids.append(uuid);
	}

	QString materialName = "Custom Material";

	_undoStack->push(new ApplyMaterialCommand(
		this, _viewportWidget, uuids, mat, materialName
	));

	QApplication::restoreOverrideCursor();
}

void ModelViewer::applyMeshMaterial(const QUuid& meshUuid, const Material& material)
{
	Q_UNUSED(meshUuid);

	std::vector<int> selectedIds = getSelectedIDs();
	QVector<QUuid> selectedUuids;
	for (int id : selectedIds)
	{
		QUuid uuid = _viewportWidget->getUuidByIndex(id);
		if (!uuid.isNull())
			selectedUuids.append(uuid);
	}

	QString materialName = "Mesh Material";
	_undoStack->push(new ApplyMaterialCommand(
		this, _viewportWidget, selectedUuids, material, materialName));

	_currentEditingMeshUuid = QUuid();

	QApplication::restoreOverrideCursor();
}

void ModelViewer::setEyedropperArmed(bool armed)
{
	_viewportWidget->setEyedropperArmed(armed);
}

void ModelViewer::onEyedropperMaterialSampled(const Material& material, const QString& sourceMeshName)
{
	// Unlike editMeshMaterial()'s createUnsavedMaterialFromMesh() flow, this
	// is deliberately volatile/in-memory only - no "Mesh Materials" tree
	// entry, no unsaved-material bookkeeping. The eyedropper is a quick
	// sample-and-apply gesture, not an invitation to start editing/saving a
	// library entry.
	predefinedMaterialsPanel->bindEyedropperSample(material, sourceMeshName);
	showPredefinedMaterialsPage();
}

void ModelViewer::applyEyedropperStroke(const QVector<QUuid>& targetUuids, const Material& material)
{
	if (targetUuids.isEmpty())
		return;

	// ApplyMaterialCommand already batch-applies a QVector<QUuid> in one
	// command, so this gives one undo entry per brush stroke with no new
	// command class needed - same as applyMeshMaterial() above.
	_undoStack->push(new ApplyMaterialCommand(
		this, _viewportWidget, targetUuids, material, material.name(), tr("Apply Material (Eyedropper)")));
}

void ModelViewer::replaceMaterial(const QVector<QUuid>& meshUuids, const Material& newMaterial)
{
	if (meshUuids.isEmpty())
		return;

	// Same ApplyMaterialCommand batching as applyEyedropperStroke() above -
	// every listed mesh gets the new material in one undo step. Called from
	// FilterByMaterialDialog's "Replace With..." context menu action.
	_undoStack->push(new ApplyMaterialCommand(
		this, _viewportWidget, meshUuids, newMaterial, newMaterial.name(), tr("Replace Material")));
}

void ModelViewer::onTexturesApplied(const Material* mat)
{
	Q_UNUSED(mat);
}

UVDialogResult ModelViewer::askUserForUVMethod(QWidget* parent)
{
	UVDialogResult result;

	UVPromptDialog dialog(parent);

	if (dialog.exec() == QDialog::Accepted)
	{
		UVPromptDialog::Choice choice = dialog.selectedChoice();
		if (choice == UVPromptDialog::Choice::Planar)
		{
			result.method = UVMethod::Planar;
		}
		else if (choice == UVPromptDialog::Choice::Cylindrical)
		{
			result.method = UVMethod::Cylindrical;
		}
		else if (choice == UVPromptDialog::Choice::Spherical)
		{
			result.method = UVMethod::Spherical;
		}
		else if (choice == UVPromptDialog::Choice::Angular)
		{
			result.method = UVMethod::AngleBased;
		}
		else if (choice == UVPromptDialog::Choice::Hybrid)
		{
			result.method = UVMethod::Hybrid;
		}
		else if (choice == UVPromptDialog::Choice::Smart)
		{
			result.method = UVMethod::AngleBasedSmartUV;
		}
		else if (choice == UVPromptDialog::Choice::SmartProject)
		{
			result.method = UVMethod::SmartProject;
		}
		else
		{
			result.method = UVMethod::None; // Skip UV generation			
		}
	}
	else
	{
		result.method = UVMethod::None; // User cancelled	
	}

	if (dialog.rememberChoiceChecked())
	{
		QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
		settings.setValue("RememberUVMethod", true);
		settings.setValue("UVMethod", static_cast<int>(result.method));
	}

	return result;
}

bool ModelViewer::hasUndo() const
{
	return _undoStack && _undoStack->canUndo();
}

bool ModelViewer::hasRedo() const
{
	return _undoStack && _undoStack->canRedo();
}

void ModelViewer::undo()
{
	if (_undoStack)
		_undoStack->undo();
}

void ModelViewer::redo()
{
	if (_undoStack)
		_undoStack->redo();
}

void ModelViewer::setSelectionWithUndo(const QSet<int>& newSelection, const void* mergeSource)
{
	// Create and push the undo command.
	// Note: push() automatically calls redo() on the command.
	const QString label = newSelection.isEmpty() ? tr("Deselect") : tr("Select");
	_undoStack->push(new SelectionCommand(this, _viewportWidget, newSelection, label, mergeSource));
}

void ModelViewer::setSelectionWithoutUndo(const QSet<int>& selection)
{
	treeWidgetModel->setSelectionByIndices(selection);
	handleTreeWidgetSelectionChanged();
}

void ModelViewer::setSelectionWithoutUndo(const QSet<QUuid>& uuids)
{
	treeWidgetModel->setSelectionByUuids(uuids);
	handleTreeWidgetSelectionChanged();
}

QSet<QUuid> ModelViewer::getVisibleUuids() const
{
	return _visibleMeshUuids;
}

void ModelViewer::setVisibilityWithUndo(const QSet<QUuid>& newVisibleUuids,
	const QString& commandText)
{
	// Create and push the undo command
	// Note: push() automatically calls redo() on the command
	_undoStack->push(new VisibilityCommand(this, _viewportWidget,
		newVisibleUuids, commandText));
}

void ModelViewer::setVisibilityWithoutUndo(const QSet<QUuid>& visibleUuids)
{
	QSet<QUuid> changedUuids = _visibleMeshUuids - visibleUuids;
	changedUuids.unite(visibleUuids - _visibleMeshUuids);
	_visibleMeshUuids = visibleUuids;
	applyVisibleMeshState(true, true, changedUuids);
}

QSet<QUuid> ModelViewer::collectVisibleUuidsFromDisplayList() const
{
	QSet<QUuid> visibleUuids;
	for (int id : _viewportWidget->getDisplayedObjectsIds())
	{
		QUuid uuid = _viewportWidget->getUuidByIndex(id);
		if (!uuid.isNull())
			visibleUuids.insert(uuid);
	}

	if (visibleUuids.isEmpty() &&
	    !_viewportWidget->isVisibleSwapped() &&
	    _viewportWidget->getHiddenObjectsIds().empty())
	{
		const auto& store = _viewportWidget->getMeshStore();
		for (size_t i = 0; i < store.size(); ++i)
		{
			QUuid uuid = _viewportWidget->getUuidByIndex(static_cast<int>(i));
			if (!uuid.isNull())
				visibleUuids.insert(uuid);
		}
	}

	return visibleUuids;
}

std::vector<int> ModelViewer::visibleIndicesFromState() const
{
	std::vector<int> ids;
	const auto& store = _viewportWidget->getMeshStore();
	ids.reserve(store.size());

	for (size_t i = 0; i < store.size(); ++i)
	{
		QUuid uuid = _viewportWidget->getUuidByIndex(static_cast<int>(i));
		if (_visibleMeshUuids.contains(uuid))
			ids.push_back(static_cast<int>(i));
	}

	return ids;
}

void ModelViewer::updateVisibilityUiFromState()
{
	float range = _viewportWidget->getBoundingSphere().getRadius() * 4.0f;
	float offset = _viewportWidget->getFloorSize() * 1.25f;
	visualizationEnvironmentPanel->updateLightPositionRanges(range, offset);

	const int count = static_cast<int>(_viewportWidget->currentVisibleObjectIds().size());
	labelMeshCount->setText(count > 0 ? tr("No of Meshes: %1").arg(count) : QString());
	emit visibleMeshCountChanged(count);
}

void ModelViewer::applyVisibleMeshState(bool syncTree,
                                        bool deferTreeSync,
                                        const QSet<QUuid>& changedUuids)
{
	if (!_viewportWidget)
		return;

	_viewportWidget->setDisplayList(visibleIndicesFromState());
	updateVisibilityUiFromState();

	if (syncTree)
	{
		constexpr int kTargetedTreeSyncThreshold = 128;
		const bool useTargetedSync =
			!changedUuids.isEmpty() &&
			changedUuids.size() <= kTargetedTreeSyncThreshold;

		if (useTargetedSync)
		{
			++_treeVisibilitySyncGeneration; // invalidate any pending full sync
			_treeVisibilityDirty = false;
			treeWidgetModel->setVisibilityDelta(changedUuids, _visibleMeshUuids);
		}
		else if (deferTreeSync)
		{
			_treeVisibilityDirty = true;
			scheduleTreeVisibilitySync();
		}
		else
		{
			_treeVisibilityDirty = false;
			syncTreeVisibilityFromModel();
		}
	}
}

void ModelViewer::scheduleTreeRebuild(int delayMs)
{
	const int generation = ++_treeRebuildGeneration;
	_treeRebuildPending = true;
	QPointer<ModelViewer> self(this);
	QTimer::singleShot(delayMs, this, [self, generation]() {
		if (!self)
			return;
		if (self->_treeRebuildGeneration != generation)
			return;
		self->rebuildTreeFromCurrentState();
	});
}

void ModelViewer::rebuildTreeFromCurrentState()
{
	_treeRebuildPending = false;
	treeWidgetModel->rebuild();
	syncTreeVisibilityFromModel();
	// updateMeshTools() deliberately NOT called here - rebuild() only starts
	// an async batched reconstruction, so the tree/selection aren't restored
	// yet at this point. It's connected to SceneTreeWidget::rebuildComplete
	// instead (see the constructor), which fires once the restore genuinely
	// finishes.
}

void ModelViewer::scheduleTreeVisibilitySync(int delayMs)
{
	const int generation = ++_treeVisibilitySyncGeneration;
	const QSet<QUuid> snapshot = _visibleMeshUuids;
	QPointer<ModelViewer> self(this);
	QTimer::singleShot(delayMs, this, [self, generation, snapshot]() {
		if (!self)
			return;
		if (self->_treeVisibilitySyncGeneration != generation)
			return;
		if (self->_visibleMeshUuids != snapshot)
			return;
		self->syncTreeVisibilityFromModel();
		});
}

void ModelViewer::syncTreeVisibilityFromModel()
{
	_treeVisibilityDirty = false;
	treeWidgetModel->setVisibilityByUuids(_visibleMeshUuids);
}

void ModelViewer::editMeshMaterial()
{
	// Check for active selection (shows "Please select an object first" if none)
	if (!checkForActiveSelection())
		return;

	// Get selected mesh IDs
	std::vector<int> selectedIds = getSelectedIDs();
	if (selectedIds.empty())
		return;

	// Edit FIRST selected mesh (but Apply will apply to ALL selected)
	int firstMeshId = selectedIds[0];
	QUuid meshUuid = _viewportWidget->getUuidByIndex(firstMeshId);
	if (meshUuid.isNull())
		return;

	// Get the mesh and its material
	SceneMesh* mesh = _viewportWidget->getMeshByUuid(meshUuid);
	if (!mesh)
		return;

	// Capture mesh UUID for later (when Apply is clicked)
	_currentEditingMeshUuid = meshUuid;

	// Get mesh's current material
	Material meshMaterial = mesh->getMaterial();
	QString meshName = mesh->getName();

	// Create unsaved material from mesh's material
	predefinedMaterialsPanel->createUnsavedMaterialFromMesh(meshName, meshMaterial);

	// Set the editing mesh UUID in the panel
	predefinedMaterialsPanel->setEditingMeshUuid(meshUuid);

	// Show material properties panel
	showPredefinedMaterialsPage();

	// Status feedback
	if (selectedIds.size() > 1) {
		MainWindow::showStatusMessage(
			QString(tr("Editing material of %1 (Apply will affect all %2 selected meshes)"))
			.arg(meshName).arg(selectedIds.size()));
	} else {
		MainWindow::showStatusMessage(tr("Editing material of %1").arg(meshName));
	}
}

void ModelViewer::showImportUnitsDialog(SceneNode* fileNode)
{
	if (!fileNode)
		return;

	ImportUnitsDialog dialog(this, fileNode, this);
	dialog.exec();
}

void ModelViewer::executeToolCommand(const QString& command)
{
    if (executeMeshToolCommand(command)) return;
    // Both the menu and this document's toolbar use the same entry point.
    if (command == QLatin1String("measure")) openMeasurementDialog();
    else if (command == QLatin1String("annotate")) openAnnotationDialog();
    else if (command == QLatin1String("analysis")) openSurfaceAnalysisDialog();
    else if (command == QLatin1String("curvature") || command == QLatin1String("thickness") || command == QLatin1String("deviation")) openSurfaceAnalysisDialog(command);
    else if (command == QLatin1String("shrink")) openShrinkWrapDialog();
    else if (command == QLatin1String("subdivide")) openSubdivisionDialog();
    else if (command == QLatin1String("reconstruct")) openReconstructSurfaceDialog();
    else if (command == QLatin1String("repair")) openRepairMeshDialog();
    else if (command == QLatin1String("fill")) openFillHolesDialog();
    else if (command == QLatin1String("uv")) openUVGenerationDialog();
    else if (command == QLatin1String("mass")) { MassPropertiesDialog dialog(this, this); dialog.exec(); }
    else if (command == QLatin1String("report")) { ReportExportDialog dialog(this, this); dialog.exec(); }
    else if (command == QLatin1String("batch")) { BatchRenderViewsDialog dialog(this, this); dialog.exec(); }
}
