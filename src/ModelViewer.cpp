#include "ADSMaterialSettingsPanel.h"
#include "AssImpModelLoader.h"
#include "ApplyPBRTexturesCommand.h"
#include "ApplyADSColorsCommand.h"
#include "ApplyADSTexturesCommand.h"
#include "DeleteMeshCommand.h"
#include "DuplicateCommand.h"
#include "GLWidget.h"
#include "LanguageManager.h"
#include "MainWindow.h"
#include "MeshProperties.h"
#include "ModelViewer.h"
#include "ModelViewerApplication.h"
#include "ObjectTransformPanel.h"
#include "PathUtils.h"
#include "SelectionCommand.h"
#include "TextureMappingPanel.h"
#include "TransformCommand.h"
#include "TriangleMesh.h"
#include "VisibilityCommand.h"
#include <assimp/Importer.hpp>
#include <QApplication>
#include <QColorDialog>
#include <QFileDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QToolTip>

QString ModelViewer::_lastOpenedDir;
QString ModelViewer::_lastSelectedFilter;

ModelViewer::ModelViewer(QWidget* parent) : QWidget(parent)
{
	setAttribute(Qt::WA_DeleteOnClose);

	_documentSaved = false;
	_documentModified = false;
	_runningFirstTime = true;

	_textureDirOpenedFirstTime = true;

	setupUi(this);

	// Initialize undo stack
	_undoStack = new QUndoStack(this);
	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
	int maxUndo = settings.value("spinBoxUndoLimit", 50).toInt(); // Keep last 50 operations as default
	_undoStack->setUndoLimit(maxUndo);

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
		
	QSurfaceFormat format = QSurfaceFormat::defaultFormat();
	format.setSwapInterval(0);
	_glWidget = new GLWidget(this, "glwidget");
	_glWidget->setAttribute(Qt::WA_DeleteOnClose);
	_glWidget->setFormat(format);
	_glWidget->setMouseTracking(true);
	// Put the GL widget inside the frame
	QVBoxLayout* flayout = new QVBoxLayout(glframe);
	flayout->setContentsMargins(0, 0, 0, 0);
	flayout->addWidget(_glWidget, 1);

	adsMaterialSettingsPanel->initialize(this, _glWidget, &_material);

	// Connect panel signals to ModelViewer slots	
	connect(adsMaterialSettingsPanel, &ADSMaterialSettingsPanel::applyColorToSelectionRequested,
		this, &ModelViewer::onADSColorsApplied);

	connect(adsMaterialSettingsPanel, &ADSMaterialSettingsPanel::opacitySliderReleased,
		this, &ModelViewer::onOpacitySliderReleased);

	connect(adsMaterialSettingsPanel, &ADSMaterialSettingsPanel::shininessSliderReleased,
		this, &ModelViewer::onShininessSliderReleased);
	
	// connect apply/clear buttons
	connect(adsMaterialSettingsPanel, &ADSMaterialSettingsPanel::applyTexturesRequested,
		this, &ModelViewer::onADSTexturesApplied);

	connect(adsMaterialSettingsPanel, &ADSMaterialSettingsPanel::clearTexturesRequested,
		this, &ModelViewer::clearADSTextures);

	connect(adsMaterialSettingsPanel, &ADSMaterialSettingsPanel::defaultMaterialsRequested,
		this, [this]() {
			updateControls();
		});

	connect(Ui_ModelViewer::adsMaterialSettingsPanel, &ADSMaterialSettingsPanel::detachRequested,
		this, &ModelViewer::detachADSMaterialPanel);


	connect(checkBoxAutoFitView, &QCheckBox::toggled, _glWidget, &GLWidget::setAutoFitViewOnUpdate);
	connect(checkBoxSelectionHighlight, &QCheckBox::toggled, _glWidget, &GLWidget::setSelectionHighlighting);
	connect(_glWidget, &GLWidget::singleSelectionDone, this, &ModelViewer::setListRow);
	connect(_glWidget, &GLWidget::sweepSelectionDone, this, &ModelViewer::setListRows);
	
	listWidgetModel->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(listWidgetModel, &ModelObjectList::customContextMenuRequested, this, &ModelViewer::showContextMenu);

	// For item editing
	connect(listWidgetModel->itemDelegate(), &QAbstractItemDelegate::closeEditor, this, &ModelViewer::itemEdited);


	connect(searchBox, &QLineEdit::textChanged, listWidgetModel, [&](const QString& text) {
		listWidgetModel->filterItems(text);

		// Optional: give visual feedback if no match
		bool anySelected = false;
		for (int i = 0; i < listWidgetModel->count(); ++i)
		{
			if (listWidgetModel->item(i)->isSelected())
			{
				anySelected = true;
				break;
			}
		}

		searchBox->setStyleSheet((anySelected || searchBox->text() == "") ? "" : "QLineEdit { border: 2px solid red; }");
		});

	connect(listWidgetModel, &ModelObjectList::selectionUpdated, this, [this]() {
		// If signals are blocked, it's programmatic (from setSelectionWithoutUndo)
		if (listWidgetModel->signalsBlocked())
		{
			// Just sync, no undo
			on_listWidgetModel_itemSelectionChanged();
			return;
		}

		// User action - create undo command
		std::vector<int> ids = getSelectedIDs();
		QSet<int> newSelection(ids.begin(), ids.end());
		setSelectionWithUndo(newSelection);
		});


	QShortcut* shortcut = new QShortcut(QKeySequence(Qt::Key_Delete), listWidgetModel);
	connect(shortcut, &QShortcut::activated, this, &ModelViewer::deleteSelectedItems);

	shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I), this);
	connect(shortcut, &QShortcut::activated, this, &ModelViewer::onFileImport);

	shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), this);
	connect(shortcut, &QShortcut::activated, this, &ModelViewer::onFileExport);


	connect(Ui_ModelViewer::objectTransformPanel, &ObjectTransformPanel::applyTransformationsRequested,
		this, &ModelViewer::setTransformation);

	connect(Ui_ModelViewer::objectTransformPanel, &ObjectTransformPanel::bakeTransformationsRequested,
		this, &ModelViewer::bakeTransformations);

	connect(Ui_ModelViewer::objectTransformPanel, &ObjectTransformPanel::resetTransformationsRequested,
		this, [this]() {
			resetTransformation();
			objectTransformPanel->resetAllValues();
		});

	connect(Ui_ModelViewer::objectTransformPanel, &ObjectTransformPanel::detachRequested,
		this, [this]() { detachTransformationsPanel(); });

	connect(buttonGroupLighting, &QButtonGroup::buttonToggled, this, &ModelViewer::lightingType_toggled);

	int indexADS = toolBox->indexOf(toolBox->findChild<QWidget*>("pageADSSettings"));
	int indexPBR = toolBox->indexOf(toolBox->findChild<QWidget*>("pageTextureMapping"));
	toolBox->setItemEnabled(indexADS, true);
	toolBox->setItemEnabled(indexPBR, false);
	toolBox->setCurrentIndex(0);

	// Shortcut to toggle lighting mode
	shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), this);
	connect(shortcut, &QShortcut::activated, this, [this] {
		auto* checked = qobject_cast<QRadioButton*>(buttonGroupLighting->checkedButton());
		if (!checked)
			return;

		// assuming exactly two buttons in the group
		const auto buttons = buttonGroupLighting->buttons();
		QAbstractButton* other =
			(buttons[0] == checked) ? buttons[1] : buttons[0];
		other->setChecked(true);   // or other->click();
		});


	MaterialLibraryWidget* libraryWidget =
		Ui_ModelViewer::predefinedMaterialsPanel->findChild<MaterialLibraryWidget*>("treeWidget");

	if (libraryWidget)
	{
		connect(libraryWidget, &MaterialLibraryWidget::materialSelected,
			this, &ModelViewer::onPredefinedMaterialSelected);
	}

	connect(Ui_ModelViewer::predefinedMaterialsPanel, &MaterialEditorPanel::materialApplied,
		this, &ModelViewer::onCustomMaterialApplied);

	connect(Ui_ModelViewer::predefinedMaterialsPanel, &MaterialEditorPanel::detachRequested,
		this, &ModelViewer::detachMaterialPanel);

	connect(textureMappingPanel, &TextureMappingPanel::detachRequested,
		this, &ModelViewer::detachTexturePanel);
	
	connect(Ui_ModelViewer::textureMappingPanel, &TextureMappingPanel::applyTexturesTriggered,
		this, [this](const GLMaterial& mat) { onTexturesApplied(&mat); });

	/*connect(Ui_ModelViewer::textureMappingPanel, &TextureMappingPanel::materialChanged,
		this, &ModelViewer::onTexturesApplied);*/

	connect(textureMappingPanel, &TextureMappingPanel::textureSamplerChanged,
		this, &ModelViewer::setTextureSamplersToSelectedItems);
	connect(textureMappingPanel, &TextureMappingPanel::textureCacheClearRequested,
		this, &ModelViewer::onTextureCacheCleared);
	QShortcut* detachShortcut = new QShortcut(
		QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T),  // Ctrl+Shift+T
		textureMappingPanel
	);
	connect(detachShortcut, &QShortcut::activated,
		this, &ModelViewer::detachTexturePanel);

	visualizationEnvironmentPanel->initialize(this, _glWidget);

	connect(_glWidget, QOverload<int>::of(&GLWidget::displayModeChanged),
		visualizationEnvironmentPanel, QOverload<int>::of(&VisualizationEnvironmentPanel::onDisplayModeChanged));

	connect(Ui_ModelViewer::visualizationEnvironmentPanel, &VisualizationEnvironmentPanel::detachRequested,
		this, &ModelViewer::detachEnvironmentPanel);

	_hasADSDiffuseTex = false;
	_hasADSSpecularTex = false;
	_hasADSEmissiveTex = false;
	_hasADSNormalTex = false;
	_hasADSHeightTex = false;
	_hasADSOpacityTex = false;
	_hasPBRAlbedoTex = false;
	_hasPBRMetallicTex = false;
	_hasPBRRoughnessTex = false;
	_hasPBRAOTex = false;
	_hasPBROpacTex = false;
	_hasPBRNormalTex = false;
	_hasPBRHeightTex = false;
	_heightPBRTexScale = 0.02f;

	_progressiveLoadingEnabled = QSettings(QCoreApplication::organizationName(), QCoreApplication::applicationName()).value("checkProgressiveLoading", true).toBool();

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
	if (_glWidget)
	{
		delete _glWidget;
	}
}

void ModelViewer::retranslateUI()
{
	// Dynamically created	
}

void ModelViewer::deselectAll()
{
	bool oldState = listWidgetModel->blockSignals(true);
	QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
	for (QListWidgetItem* item : items)
	{
		item->setSelected(false);
	}
	resetTransformationValues();
	listWidgetModel->blockSignals(oldState);
	on_listWidgetModel_itemSelectionChanged();
}

void ModelViewer::setListRow(int index)
{
	if (index == -1)
		return;

	std::vector<TriangleMesh*> meshes = _glWidget->getMeshStore();
	TriangleMesh* mesh = meshes.at(index);

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
	if (toolBox->currentIndex() == 4)
	{
		if (newSelection.size() == 1)
			updateTransformationValues();
		else
			resetTransformationValues();
	}
}

void ModelViewer::setListRows(QList<int> indices)
{
	if (indices.isEmpty())
		return;

	// Build selection set from indices
	QSet<int> newSelection;
	for (int index : indices)
		newSelection.insert(index);

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
		QUuid uuid = _glWidget->getUuidByIndex(id);
		if (!uuid.isNull())
			uuids.append(uuid);
	}

	// Create and push transform command
	// redo() will be called automatically and will apply the transformation
	_undoStack->push(new TransformCommand(
		this, _glWidget, uuids, translate, rotate, scale
	));

	// Update UI (transformation already applied by command's redo())
	float range = _glWidget->getBoundingSphere().getRadius() * 4.0f;
	float offset = _glWidget->getFloorSize() * 1.25f;
	visualizationEnvironmentPanel->updateLightPositionRanges(range, offset);
	_glWidget->update();

	QApplication::restoreOverrideCursor();
}

void ModelViewer::bakeTransformations()
{
	if (!checkForActiveSelection())
		return;

	// Updated warning dialog
	QMessageBox::StandardButton reply = QMessageBox::question(
		this,
		tr("Bake Transformations"),
		tr("This operation will permanently apply transformations to mesh vertices.\n"
			"Transform undo/redo for these meshes will no longer work.\n"
			"Other undo operations (selection, visibility, etc.) will still work.\n\n"
			"Do you want to proceed?"),
		QMessageBox::Yes | QMessageBox::No
	);

	if (reply != QMessageBox::Yes)
		return;

	QApplication::setOverrideCursor(Qt::WaitCursor);

	// Get selected mesh IDs and UUIDs
	std::vector<int> ids = getSelectedIDs();
	QVector<QUuid> bakedUuids;
	for (int id : ids)
	{
		QUuid uuid = _glWidget->getUuidByIndex(id);
		if (!uuid.isNull())
			bakedUuids.append(uuid);
	}

	// Bake the transformations
	_glWidget->bakeTransformation(ids);

	// Mark all TransformCommands affecting these meshes as obsolete
	for (int i = 0; i < _undoStack->count(); ++i)
	{
		const TransformCommand* cmd =
			dynamic_cast<const TransformCommand*>(_undoStack->command(i));
		if (cmd && cmd->affectsAnyUuid(bakedUuids))
		{
			// Mark each baked mesh in the command
			for (const QUuid& uuid : bakedUuids)
			{
				cmd->markMeshBaked(uuid);
			}
		}
	}

	// Panel values already reset (you handled this!)

	QMessageBox::information(
		this,
		tr("Action Complete"),
		tr("Baked the applied transformations into the mesh vertices")
	);

	QApplication::restoreOverrideCursor();
	_glWidget->update();
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
		QUuid uuid = _glWidget->getUuidByIndex(id);
		if (!uuid.isNull())
			uuids.append(uuid);
	}

	// Reset is transformation to identity values
	QVector3D identity_trans(0, 0, 0);
	QVector3D identity_rot(0, 0, 0);
	QVector3D identity_scale(1, 1, 1);

	// Create and push transform command with identity values
	_undoStack->push(new TransformCommand(
		this, _glWidget, uuids,
		identity_trans, identity_rot, identity_scale,
		tr("Reset Transform")  // Different text for reset
	));

	// Reset panel values
	objectTransformPanel->resetAllValues();

	// Update UI
	float range = _glWidget->getBoundingSphere().getRadius() * 4.0f;
	float offset = _glWidget->getFloorSize() * 1.25f;
	visualizationEnvironmentPanel->updateLightPositionRanges(range, offset);
	_glWidget->update();

	QApplication::restoreOverrideCursor();
}

void ModelViewer::updateTransformationValues()
{
	try
	{
		QList<QListWidgetItem*> selected = listWidgetModel->selectedItems();
		if (selected.count() > 0)
		{
			QListWidgetItem* item = selected.at(0);
			std::vector<TriangleMesh*> meshStore = _glWidget->getMeshStore();
			TriangleMesh* mesh = meshStore.at(listWidgetModel->row(item));
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
		std::cout << "Exception raised in ModelViewer::on_toolBox_currentChanged\n" << ex.what() << std::endl;
	}
}

void ModelViewer::resetTransformationValues()
{
	objectTransformPanel->resetAllValues();
}

void ModelViewer::updateControls()
{
	visualizationEnvironmentPanel->updateButtonStyles();
	// ADS Lighting
	if (radioButtonADSL->isChecked())
	{
		adsMaterialSettingsPanel->updateMaterialButtonStyles();
		adsMaterialSettingsPanel->updateMaterialPropertySliders();
	}
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

void ModelViewer::detachADSMaterialPanel()
{
	if (!adsMaterialSettingsPanel || !toolBox) return;
	if (_detachedADSMaterialDialog)
	{
		_detachedADSMaterialDialog->raise();
		_detachedADSMaterialDialog->activateWindow();
		return;
	}
	// Find and remove from toolbox
	_adsMaterialPageIndex = toolBox->indexOf(adsMaterialSettingsPanel->parentWidget());
	if (_adsMaterialPageIndex >= 0)
	{
		_adsMaterialPageLabel = toolBox->itemText(_adsMaterialPageIndex);
		toolBox->removeItem(_adsMaterialPageIndex);
	}
	// Create floating dialog
	_detachedADSMaterialDialog = new QDialog(this);
	_detachedADSMaterialDialog->setWindowTitle("ADS Material Settings");
	_detachedADSMaterialDialog->setWindowFlags(Qt::Window | Qt::Tool);
	QVBoxLayout* layout = new QVBoxLayout(_detachedADSMaterialDialog);
	layout->setContentsMargins(6, 6, 6, 6);
	_adsMaterialOriginalParent = adsMaterialSettingsPanel->parentWidget();
	adsMaterialSettingsPanel->setParent(_detachedADSMaterialDialog);
	layout->addWidget(adsMaterialSettingsPanel);
	// Position and show...
	QScreen* screen = QGuiApplication::primaryScreen();
	QRect screenGeom = screen->availableGeometry();
	QRect myGeometry = this->frameGeometry();
	int x = myGeometry.right() + 20;
	int y = myGeometry.top();
	if (x + 420 > screenGeom.right()) x = screenGeom.right() - adsMaterialSettingsPanel->width() - 40;  // Fit within screen;
	if (y + 700 > screenGeom.bottom()) y = screenGeom.bottom() - adsMaterialSettingsPanel->height();
	if (x < screenGeom.left()) x = screenGeom.left();
	if (y < screenGeom.top()) y = screenGeom.top();
	_detachedADSMaterialDialog->move(x, y);
	_detachedADSMaterialDialog->resize(420, std::min(adsMaterialSettingsPanel->height(), static_cast<int>(height() * 0.95)));
	_detachedADSMaterialDialog->show();
	adsMaterialSettingsPanel->setDetached(true);
	connect(_detachedADSMaterialDialog, &QDialog::finished,
		this, &ModelViewer::reattachADSMaterialPanel);
}

void ModelViewer::reattachADSMaterialPanel()
{
	if (!_detachedADSMaterialDialog || !toolBox) return;
	_detachedADSMaterialDialog->disconnect();
	_detachedADSMaterialDialog->deleteLater();
	_detachedADSMaterialDialog = nullptr;
	if (adsMaterialSettingsPanel && _adsMaterialOriginalParent && _adsMaterialPageIndex >= 0)
	{
		// Re-insert page into toolbox
		toolBox->insertItem(_adsMaterialPageIndex, _adsMaterialOriginalParent, _adsMaterialPageLabel);
		adsMaterialSettingsPanel->setParent(_adsMaterialOriginalParent);
		QGridLayout* gridLayout = qobject_cast<QGridLayout*>(_adsMaterialOriginalParent->layout());
		if (gridLayout)
		{
			gridLayout->addWidget(adsMaterialSettingsPanel, 0, 0);
			gridLayout->setColumnStretch(0, 1);
			gridLayout->setRowStretch(0, 1);
		}
		adsMaterialSettingsPanel->show();
		_adsMaterialOriginalParent->show();
		adsMaterialSettingsPanel->setDetached(false);
		bool shouldActivate = radioButtonADSL->isChecked();
		if (shouldActivate)
		{
			toolBox->setCurrentIndex(_adsMaterialPageIndex);
			toolBox->setItemEnabled(_adsMaterialPageIndex, true); // Enable only if ADS is selected
		}
	}
}

void ModelViewer::detachTexturePanel()
{
	if (!textureMappingPanel || !toolBox) return;

	if (_detachedTextureDialog)
	{
		_detachedTextureDialog->raise();
		_detachedTextureDialog->activateWindow();
		return;
	}

	// Find and remove from toolbox
	_texturePageIndex = toolBox->indexOf(textureMappingPanel->parentWidget());
	if (_texturePageIndex >= 0)
	{
		_texturePageLabel = toolBox->itemText(_texturePageIndex);
		toolBox->removeItem(_texturePageIndex);
	}

	// Create floating dialog
	_detachedTextureDialog = new QDialog(this);
	_detachedTextureDialog->setWindowTitle("PBR Texture Settings");
	_detachedTextureDialog->setWindowFlags(Qt::Window | Qt::Tool);

	QVBoxLayout* layout = new QVBoxLayout(_detachedTextureDialog);
	layout->setContentsMargins(6, 6, 6, 6);

	_textureOriginalParent = textureMappingPanel->parentWidget();
	textureMappingPanel->setParent(_detachedTextureDialog);
	layout->addWidget(textureMappingPanel);

	// Position and show...
	QScreen* screen = QGuiApplication::primaryScreen();
	QRect screenGeom = screen->availableGeometry();
	QRect myGeometry = this->frameGeometry();
	int x = myGeometry.right() + 20;
	int y = myGeometry.top();
	if (x + 420 > screenGeom.right()) x = screenGeom.right() - textureMappingPanel->width() - 40; // Fit within screen;
	if (y + 700 > screenGeom.bottom()) y = screenGeom.bottom() - textureMappingPanel->height();
	if (x < screenGeom.left()) x = screenGeom.left();
	if (y < screenGeom.top()) y = screenGeom.top();

	_detachedTextureDialog->move(x, y);
	_detachedTextureDialog->resize(420, std::min(textureMappingPanel->height(), static_cast<int>(height() * 0.95)));
	_detachedTextureDialog->show();
	textureMappingPanel->setDetached(true);

	connect(_detachedTextureDialog, &QDialog::finished,
		this, &ModelViewer::reattachTexturePanel);
}

void ModelViewer::reattachTexturePanel()
{
	if (!_detachedTextureDialog || !toolBox) return;

	_detachedTextureDialog->disconnect();
	_detachedTextureDialog->deleteLater();
	_detachedTextureDialog = nullptr;

	if (textureMappingPanel && _textureOriginalParent && _texturePageIndex >= 0)
	{
		// Re-insert page into toolbox
		toolBox->insertItem(_texturePageIndex, _textureOriginalParent, _texturePageLabel);

		textureMappingPanel->setParent(_textureOriginalParent);
		QGridLayout* gridLayout = qobject_cast<QGridLayout*>(_textureOriginalParent->layout());
		if (gridLayout)
		{
			gridLayout->addWidget(textureMappingPanel, 0, 0);
			gridLayout->setColumnStretch(0, 1);
			gridLayout->setRowStretch(0, 1);
		}

		textureMappingPanel->show();
		_textureOriginalParent->show();
		textureMappingPanel->setDetached(false);

		bool shouldActivate = radioButtonTXPBR->isChecked();
		if (shouldActivate)
		{
			toolBox->setCurrentIndex(_texturePageIndex);
			toolBox->setItemEnabled(_texturePageIndex, true); // Enable only if PBR is selected
		}
	}
}

void ModelViewer::detachMaterialPanel()
{
	if (!predefinedMaterialsPanel || !toolBox) return;

	if (_detachedMaterialDialog)
	{
		_detachedMaterialDialog->raise();
		_detachedMaterialDialog->activateWindow();
		return;
	}

	// Find and remove from toolbox
	_materialPageIndex = toolBox->indexOf(predefinedMaterialsPanel->parentWidget());
	if (_materialPageIndex >= 0)
	{
		_materialPageLabel = toolBox->itemText(_materialPageIndex);
		toolBox->removeItem(_materialPageIndex);
	}

	// Create floating dialog
	_detachedMaterialDialog = new QDialog(this);
	_detachedMaterialDialog->setWindowTitle("Predefined Materials");
	_detachedMaterialDialog->setWindowFlags(Qt::Window | Qt::Tool);

	QVBoxLayout* layout = new QVBoxLayout(_detachedMaterialDialog);
	layout->setContentsMargins(6, 6, 6, 6);

	_materialOriginalParent = predefinedMaterialsPanel->parentWidget();
	predefinedMaterialsPanel->setParent(_detachedMaterialDialog);
	layout->addWidget(predefinedMaterialsPanel);

	// Position and show...
	QScreen* screen = QGuiApplication::primaryScreen();
	QRect screenGeom = screen->availableGeometry();
	QRect myGeometry = this->frameGeometry();
	int x = myGeometry.right() + 20;
	int y = myGeometry.top();
	if (x + 420 > screenGeom.right()) x = screenGeom.right() - predefinedMaterialsPanel->width() - 40;  // Fit within screen;
	if (y + 700 > screenGeom.bottom()) y = screenGeom.bottom() - predefinedMaterialsPanel->height();
	if (x < screenGeom.left()) x = screenGeom.left();
	if (y < screenGeom.top()) y = screenGeom.top();

	_detachedMaterialDialog->move(x, y);
	_detachedMaterialDialog->resize(420, std::min(predefinedMaterialsPanel->height(), static_cast<int>(height() * 0.95)));
	_detachedMaterialDialog->show();
	predefinedMaterialsPanel->setDetached(true);

	connect(_detachedMaterialDialog, &QDialog::finished,
		this, &ModelViewer::reattachMaterialPanel);
}

void ModelViewer::reattachMaterialPanel()
{
	if (!_detachedMaterialDialog || !toolBox) return;

	_detachedMaterialDialog->disconnect();
	_detachedMaterialDialog->deleteLater();
	_detachedMaterialDialog = nullptr;

	if (predefinedMaterialsPanel && _materialOriginalParent && _materialPageIndex >= 0)
	{
		// Re-insert page into toolbox
		toolBox->insertItem(_materialPageIndex, _materialOriginalParent, _materialPageLabel);

		predefinedMaterialsPanel->setParent(_materialOriginalParent);
		QGridLayout* gridLayout = qobject_cast<QGridLayout*>(_materialOriginalParent->layout());
		if (gridLayout)
		{
			gridLayout->addWidget(predefinedMaterialsPanel, 0, 0);
			gridLayout->setColumnStretch(0, 1);
			gridLayout->setRowStretch(0, 1);
		}

		predefinedMaterialsPanel->show();
		_materialOriginalParent->show();
		predefinedMaterialsPanel->setDetached(false);
		toolBox->setCurrentIndex(_materialPageIndex);
	}
}

void ModelViewer::detachTransformationsPanel()
{
	if (objectTransformPanel->isDetached() || !toolBox)	return;

	if(_detachedTransformationsDialog)
	{
		_detachedTransformationsDialog->raise();
		_detachedTransformationsDialog->activateWindow();
		return;
	}
	// Find and remove from toolbox
	_transformationsPageIndex = toolBox->indexOf(objectTransformPanel->parentWidget());
	if (_transformationsPageIndex >= 0)
	{
		_transformationsPageLabel = toolBox->itemText(_transformationsPageIndex);
		toolBox->removeItem(_transformationsPageIndex);
	}
	// Create floating dialog
	_detachedTransformationsDialog = new QDialog(this);
	_detachedTransformationsDialog->setWindowTitle("Object Transformations");
	_detachedTransformationsDialog->setWindowFlags(Qt::Window | Qt::Tool);
	QVBoxLayout* layout = new QVBoxLayout(_detachedTransformationsDialog);
	layout->setContentsMargins(6, 6, 6, 6);
	_transformationsOriginalParent = objectTransformPanel->parentWidget();
	objectTransformPanel->setParent(_detachedTransformationsDialog);
	layout->addWidget(objectTransformPanel);
	// Position and show...
	QScreen* screen = QGuiApplication::primaryScreen();
	QRect screenGeom = screen->availableGeometry();
	QRect myGeometry = this->frameGeometry();
	int x = myGeometry.right() + 20;
	int y = myGeometry.top();
	if (x + 420 > screenGeom.right()) x = screenGeom.right() - objectTransformPanel->width() - 40;  // Fit within screen;
	if (y + 700 > screenGeom.bottom()) y = screenGeom.bottom() - objectTransformPanel->height();
	if (x < screenGeom.left()) x = screenGeom.left();
	if (y < screenGeom.top()) y = screenGeom.top();
	_detachedTransformationsDialog->move(x, y);
	_detachedTransformationsDialog->resize(420, std::min(objectTransformPanel->height(), static_cast<int>(height() * 0.95)));
	_detachedTransformationsDialog->show();
	objectTransformPanel->setDetached(true);
	connect(_detachedTransformationsDialog, &QDialog::finished,
		this, &ModelViewer::reattachTransformationsPanel);
}

void ModelViewer::reattachTransformationsPanel()
{
	if (!_detachedTransformationsDialog || !toolBox) return;
	_detachedTransformationsDialog->disconnect();
	_detachedTransformationsDialog->deleteLater();
	_detachedTransformationsDialog = nullptr;
	if (objectTransformPanel && _transformationsOriginalParent && _transformationsPageIndex >= 0)
	{
		// Re-insert page into toolbox
		toolBox->insertItem(_transformationsPageIndex, _transformationsOriginalParent, _transformationsPageLabel);
		objectTransformPanel->setParent(_transformationsOriginalParent);
		QGridLayout* gridLayout = qobject_cast<QGridLayout*>(_transformationsOriginalParent->layout());
		if (gridLayout)
		{
			gridLayout->addWidget(objectTransformPanel, 0, 0);
			gridLayout->setColumnStretch(0, 1);
			gridLayout->setRowStretch(0, 1);
		}
		objectTransformPanel->show();
		_transformationsOriginalParent->show();
		objectTransformPanel->setDetached(false);
		bool shouldActivate = listWidgetModel->selectedItems().count() >= 1;
		if (shouldActivate)
		{
			toolBox->setCurrentIndex(_transformationsPageIndex);
			toolBox->setItemEnabled(_transformationsPageIndex, true); // Enable only if at least one object is selected
		}
	}
}

void ModelViewer::detachEnvironmentPanel()
{
	if (!visualizationEnvironmentPanel || !toolBox) return;

	if (_detachedEnvironmentDialog)
	{
		_detachedEnvironmentDialog->raise();
		_detachedEnvironmentDialog->activateWindow();
		return;
	}

	// Find and remove from toolbox
	_environmentPageIndex = toolBox->indexOf(visualizationEnvironmentPanel->parentWidget());
	if (_environmentPageIndex >= 0)
	{
		_environmentPageLabel = toolBox->itemText(_environmentPageIndex);
		toolBox->removeItem(_environmentPageIndex);
	}

	// Create floating dialog
	_detachedEnvironmentDialog = new QDialog(this);
	_detachedEnvironmentDialog->setWindowTitle("Visualization Environment Settings");
	_detachedEnvironmentDialog->setWindowFlags(Qt::Window | Qt::Tool);

	QVBoxLayout* layout = new QVBoxLayout(_detachedEnvironmentDialog);
	layout->setContentsMargins(6, 6, 6, 6);

	_environmentOriginalParent = visualizationEnvironmentPanel->parentWidget();
	visualizationEnvironmentPanel->setParent(_detachedEnvironmentDialog);
	layout->addWidget(visualizationEnvironmentPanel);

	// Position and show...
	QScreen* screen = QGuiApplication::primaryScreen();
	QRect screenGeom = screen->availableGeometry();
	QRect myGeometry = this->frameGeometry();
	int x = myGeometry.right() + 20;
	int y = myGeometry.top();
	if (x + 420 > screenGeom.right()) x = screenGeom.right() - visualizationEnvironmentPanel->width() - 40;  // Fit within screen;
	if (y + 700 > screenGeom.bottom()) y = screenGeom.bottom() - visualizationEnvironmentPanel->height();
	if (x < screenGeom.left()) x = screenGeom.left();
	if (y < screenGeom.top()) y = screenGeom.top();

	_detachedEnvironmentDialog->move(x, y);
	_detachedEnvironmentDialog->resize(420, std::min(visualizationEnvironmentPanel->height(), static_cast<int>(height() * 0.95)));
	_detachedEnvironmentDialog->show();
	visualizationEnvironmentPanel->setDetached(true);

	connect(_detachedEnvironmentDialog, &QDialog::finished,
		this, &ModelViewer::reattachEnvironmentPanel);
}

void ModelViewer::reattachEnvironmentPanel()
{
	if (!_detachedEnvironmentDialog || !toolBox) return;
	_detachedEnvironmentDialog->disconnect();
	_detachedEnvironmentDialog->deleteLater();
	_detachedEnvironmentDialog = nullptr;
	if (visualizationEnvironmentPanel && _environmentOriginalParent && _environmentPageIndex >= 0)
	{
		// Re-insert page into toolbox
		toolBox->insertItem(_environmentPageIndex, _environmentOriginalParent, _environmentPageLabel);
		visualizationEnvironmentPanel->setParent(_environmentOriginalParent);
		QGridLayout* gridLayout = qobject_cast<QGridLayout*>(_environmentOriginalParent->layout());
		if (gridLayout)
		{
			gridLayout->addWidget(visualizationEnvironmentPanel, 0, 0);
			gridLayout->setColumnStretch(0, 1);
			gridLayout->setRowStretch(0, 1);
		}
		visualizationEnvironmentPanel->show();
		_environmentOriginalParent->show();
		visualizationEnvironmentPanel->setDetached(false);					
		toolBox->setCurrentIndex(_environmentPageIndex);
		toolBox->setItemEnabled(_environmentPageIndex, true);
	}
}

void ModelViewer::setupUndoStackMonitoring()
{
	// Connect to stack changes
	connect(_undoStack, &QUndoStack::indexChanged,
		this, &ModelViewer::onUndoStackChanged);

	// Initialize cache
	_lastStackCount = 0;
	_cachedReferencedUuids.clear();
}

void ModelViewer::onUndoStackChanged()
{
	if (!_undoStack || !_glWidget)
		return;

	int currentCount = _undoStack->count();

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

				// If it's a DeleteCommand, add its UUIDs to cache
				if (const auto* delCmd = dynamic_cast<const DeleteMeshCommand*>(cmd))
				{
					_cachedReferencedUuids.unite(delCmd->getReferencedUuids());
				}
			}
		}

		_lastStackCount = currentCount;
	}
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
			_glWidget->permanentlyDeleteFromBin(uuid);
		}

		qDebug() << "Cleaned up" << orphaned.size()
			<< "orphaned mesh(es) from recycle bin";
	}

	// Update cache
	_cachedReferencedUuids = currentlyReferenced;
}

QSet<QUuid> ModelViewer::scanStackForReferencedUuids()
{
	QSet<QUuid> referenced;

	// Scan all commands in the undo stack
	int count = _undoStack->count();
	for (int i = 0; i < count; ++i)
	{
		const QUndoCommand* cmd = _undoStack->command(i);

		// Check if it's a DeleteCommand
		if (const auto* delCmd = dynamic_cast<const DeleteMeshCommand*>(cmd))
		{
			referenced.unite(delCmd->getReferencedUuids());
		}

		// Optional: Could also check other command types if needed
		// For example, MaterialCommand might want to prevent deletion
		// of meshes it references, but this is probably not necessary
	}

	return referenced;
}

void ModelViewer::updateDisplayList()
{
	_glWidget->setTransmissionEnabled(false);
	if (_glWidget->getMeshStore().empty())
	{
		listWidgetModel->clear();
		return;
	}
	QApplication::setOverrideCursor(Qt::WaitCursor);
	listWidgetModel->clear();
	std::vector<TriangleMesh*> store = _glWidget->getMeshStore();
	std::vector<int> ids = _glWidget->getDisplayedObjectsIds();
	int id = 0;
	QListWidgetItem* item = nullptr;
	bool oldState = listWidgetModel->blockSignals(true);
	for (TriangleMesh* mesh : store)
	{
		if (mesh->getMaterial().hasTransmission())
			_glWidget->setTransmissionEnabled(true);
		item = new QListWidgetItem(mesh->getName());
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable); // set checkable flag
		// AND initialize check state
		if (std::count(ids.begin(), ids.end(), id))
			item->setCheckState(Qt::Checked);
		else
			item->setCheckState(Qt::Unchecked);
		listWidgetModel->addItem(item);
		id++;
	}

	QApplication::restoreOverrideCursor();

	listWidgetModel->blockSignals(oldState);
	on_listWidgetModel_itemChanged(item);
}

void ModelViewer::updateSelectionStatusMessage()
{
	int count = listWidgetModel->selectedItems().count();
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
	if (listWidgetModel->count())
	{
		bool oldState = listWidgetModel->blockSignals(true);
		for (int i = 0; i < listWidgetModel->count(); i++)
		{
			QListWidgetItem* item = listWidgetModel->item(i);
			if (item->checkState() == (_glWidget->isVisibleSwapped() ? Qt::Unchecked : Qt::Checked))
			{
				item->setSelected(true);
			}
		}
		listWidgetModel->blockSignals(oldState);
		on_listWidgetModel_itemSelectionChanged();
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
				bool success = _glWidget->loadAssImpModel(fileName, method, errMsg, _progressiveLoadingEnabled);
				if (!success)
				{
					QMessageBox::critical(this, tr("Error"), tr("Failed to load model: ") + fileName + "\n" + errMsg);
					continue;
				}
			}

			updateDisplayList();

			listWidgetModel->setCurrentRow(listWidgetModel->count() - 1);
			listWidgetModel->currentItem()->setCheckState(Qt::Checked);

			updateDisplayList();
		}
	}
	QApplication::restoreOverrideCursor();
}

void ModelViewer::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
}

void ModelViewer::mouseMoveEvent(QMouseEvent* event)
{
	QWidget::mouseMoveEvent(event);
}

void ModelViewer::closeEvent(QCloseEvent* event)
{
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

	// Clean up floating dialog if it exists
	if (_detachedTextureDialog)
	{
		_detachedTextureDialog->close();
		_detachedTextureDialog = nullptr;
	}

	event->accept();
}

void ModelViewer::setCurrentFile(const QString& fileName)
{
	_currentFile = fileName;
	_documentSaved = true;
	_documentModified = false;
	setWindowTitle(tr("%1").arg(QFileInfo(_currentFile).fileName()));
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
	_documentModified = modified;
	if (modified)
	{
		setWindowTitle(tr("%1*").arg(QFileInfo(_currentFile).fileName()));
	}
	else
	{
		setWindowTitle(tr("%1").arg(QFileInfo(_currentFile).fileName()));
	}
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
		_documentModified = false;
		MainWindow::showStatusMessage(tr("File saved"), 2000);
		setWindowTitle(tr("%1").arg(QFileInfo(_currentFile).fileName()));
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
	if (listWidgetModel->selectedItems().count() != 0)
	{
		// Create menu and insert some actions
		QMenu myMenu;

		myMenu.addAction(tr("Center Screen"), this, &ModelViewer::centerScreen);
		myMenu.addAction(tr("Visualization Settings"), this, &ModelViewer::showVisualizationModelPage);
		myMenu.addAction(tr("Transformations"), this, &ModelViewer::showTransformationsPage);
		myMenu.addAction(tr("Hide"), this, &ModelViewer::hideSelectedItems);
		myMenu.addAction(tr("Show"), this, &ModelViewer::showSelectedItems);
		myMenu.addAction(tr("Show Only"), this, &ModelViewer::showOnlySelectedItems);
		myMenu.addAction(tr("Duplicate"), this, &ModelViewer::duplicateSelectedItems);
		myMenu.addAction(tr("Delete"), this, &ModelViewer::deleteSelectedItems);
		myMenu.addAction(tr("Mesh Info"), this, &ModelViewer::displaySelectedMeshInfo);

		// Show context menu at handling position
		myMenu.exec(listWidgetModel->mapToGlobal(pos));
	}
}

void ModelViewer::centerScreen()
{
	std::vector<int> selectedIDs = getSelectedIDs();
	_glWidget->centerScreen(selectedIDs);
}

void ModelViewer::duplicateSelectedItems()
{
	QList<QListWidgetItem*> selectedItems = listWidgetModel->selectedItems();
	if (selectedItems.isEmpty())
		return;

	QApplication::setOverrideCursor(Qt::WaitCursor);

	std::vector<int> ids = getSelectedIDs();

	// CAPTURE ORIGINAL SELECTION FIRST (before updateDisplayList)
	QSet<QUuid> originalSelection = getSelectedUuids();

	QVector<QUuid> duplicatedUuids = _glWidget->duplicateObjects(ids);

	updateDisplayList();  // May clear selection, but we already saved it

	// PASS original selection to command
	_undoStack->push(new DuplicateCommand(
		this, _glWidget, duplicatedUuids, originalSelection
	));

	QApplication::restoreOverrideCursor();
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
		QUuid uuid = _glWidget->getUuidByIndex(index);
		if (!uuid.isNull())
			uuidsToDelete.append(uuid);
	}

	if (uuidsToDelete.isEmpty())
		return;

	// Push delete command (will move to recycle bin)
	_undoStack->push(new DeleteMeshCommand(this, _glWidget, uuidsToDelete));

	// Update UI
	updateControls();
}

#include "UVGenerationDialog.h"
void ModelViewer::generateUVsForSelectedItems()
{
	std::vector<int> selected = getSelectedIDs();
	if (selected.size() != 0)
	{
		QString error;
		UVGenerationDialog dialog(this);
		if (dialog.exec() == QDialog::Accepted)
		{
			// User clicked OK - get the selected method and config
			UVMethod method = dialog.getSelectedMethod();
			UVConfig config = dialog.getUVConfig();

			bool success = _glWidget->generateUVsForMeshes(selected, method, config, error);
			if (success)
			{
				MainWindow::showStatusMessage(QString("UVs generated using %1 method")
					.arg(dialog.getMethodName(method)));
			}
			else
			{
				QMessageBox::critical(this, "Error", "Failed to generate UVs.\n" + error);
			}
		}
	}
}

void ModelViewer::hideAllItems()
{
	// Hide all meshes (empty visibility set)
	QSet<QUuid> newVisible;  // Empty set

	// Apply visibility with undo support
	setVisibilityWithUndo(newVisible, tr("Hide All"));

	// Turn off swap visible if it was on
	if (_glWidget->isVisibleSwapped())
		_glWidget->swapVisible(false);
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
		QUuid uuid = _glWidget->getUuidByIndex(id);
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
		QUuid uuid = _glWidget->getUuidByIndex(id);
		if (!uuid.isNull())
			newVisible.insert(uuid);
	}

	// Apply visibility with undo support
	setVisibilityWithUndo(newVisible, tr("Show Only"));

	// Turn off swap visible if it was on
	if (_glWidget->isVisibleSwapped())
		_glWidget->swapVisible(false);
}

void ModelViewer::showAllItems()
{
	// Get all mesh UUIDs
	QSet<QUuid> newVisible;
	int meshCount = static_cast<int>(_glWidget->getMeshStore().size());

	for (int i = 0; i < meshCount; ++i)
	{
		QUuid uuid = _glWidget->getUuidByIndex(i);
		if (!uuid.isNull())
			newVisible.insert(uuid);
	}

	// Apply visibility with undo support
	setVisibilityWithUndo(newVisible, tr("Show All"));

	// Turn off swap visible if it was on
	if (_glWidget->isVisibleSwapped())
		_glWidget->swapVisible(false);
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
		QUuid uuid = _glWidget->getUuidByIndex(id);
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
	return !listWidgetModel->selectedItems().isEmpty();
}

std::vector<int> ModelViewer::getSelectedIDs() const
{
	std::vector<int> ids;
	QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
	for (QListWidgetItem* i : items)
	{
		int rowId = listWidgetModel->row(i);
		ids.push_back(rowId);
	}
	return ids;
}

QSet<QUuid> ModelViewer::getSelectedUuids() const
{
	std::vector<int> selectedIds = getSelectedIDs();
	QSet<QUuid> selectedUuids;

	for (int id : selectedIds)
	{
		QUuid uuid = _glWidget->getUuidByIndex(id);
		if (!uuid.isNull())
			selectedUuids.insert(uuid);
	}

	return selectedUuids;
}

void ModelViewer::displaySelectedMeshInfo()
{
	std::vector<int> selected = getSelectedIDs();
	if (selected.size() != 0)
	{
		std::vector<TriangleMesh*> meshes = _glWidget->getMeshStore();
		QString name;
		size_t points = 0, triangles = 0;
		unsigned long long rawmem = 0;
		float surfArea = 0, volume = 0;
		QVector3D centerOfMass;
		float weight = 0, density = 0;
		TriangleMesh* mesh = nullptr;
		BoundingBox bbox;
		size_t selectionCount = selected.size();
		if (selectionCount > 1)
			name = QString("%1 Meshes\n").arg(selectionCount);
		else
			name = meshes.at(selected[0])->getName() + "\n";
		int meshCount = 0;
		for (int id : selected)
		{
			mesh = meshes.at(id);
			points += mesh->getPoints().size() / 3;
			triangles += mesh->getIndices().size() / 3;
			rawmem += mesh->memorySize();
			try
			{
				MeshProperties props(mesh);
				surfArea += props.surfaceArea();
				volume += props.volume();
				centerOfMass += props.centerOfMass() * props.weight();
				weight += props.weight();
				density = props.density();
				if (meshCount == 0)
					bbox = props.boundingBox();
				else
					bbox.addBox(props.boundingBox());
			}
			catch (const std::exception& ex)
			{
				std::cout << "Exception raised in ModelViewer::displaySelectedMeshInfo, Meshproperties" << ex.what() << std::endl;
			}
			meshCount++;
		}
		centerOfMass /= weight;

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
		QString meshSize = QString(tr("Memory: %1 ")).arg(mem) + units + "\n";
		QString meshProps;

		meshProps = QString(tr("Mesh Volume: %1mm^3\nSurface Area: %2mm^2\nDensity: %3kg/m^3\nWeight: %4kg\n")).arg(volume).arg(surfArea)
			.arg(density).arg(weight);

		meshProps += QString(tr("Mesh Center of Mass: X%1, Y%2, Z%3\n")).arg(centerOfMass.x()).arg(centerOfMass.y()).arg(centerOfMass.z());

		meshProps += QString(tr("Bounding Limits:\n\tXMin %1  XMax %2\n\tYMin %3  YMax %4\n\tZMin %5  ZMax %6\n"))
			.arg(bbox.xMin()).arg(bbox.xMax()).arg(bbox.yMin()).arg(bbox.yMax()).arg(bbox.zMin()).arg(bbox.zMax());

		meshProps += QString(tr("Bounding Size:\n\tX %1\n\tY %2\n\tZ %3"))
			.arg(fabs(bbox.xMax() - bbox.xMin())).arg(fabs(bbox.yMax() - bbox.yMin())).arg(fabs(bbox.zMax() - bbox.zMin()));

		QString info = name + strpoints + strtriangles + meshSize + meshProps;
		QMessageBox::information(this, tr("Mesh Info"), info);
	}
}

void ModelViewer::showVisualizationModelPage()
{
	if (radioButtonADSL->isChecked())
	{
		toolBox->setCurrentIndex(0);
	}
	if (radioButtonTXPBR->isChecked())
	{
		toolBox->setCurrentIndex(1);
	}
}

void ModelViewer::showPredefinedMaterialsPage()
{
	toolBox->setCurrentIndex(2);
}

void ModelViewer::showTransformationsPage()
{
	toolBox->setCurrentIndex(3);
	updateTransformationValues();
}

void ModelViewer::showEnvironmentPage()
{
	toolBox->setCurrentIndex(4);
}

void ModelViewer::applyADSColors()
{
	setMaterialToSelectedItems(_material);
	_glWidget->updateView();
	updateControls();
}

void ModelViewer::on_listWidgetModel_itemChanged(QListWidgetItem* item)
{
	if (listWidgetModel->count())
	{
		std::vector<int> ids;
		for (int i = 0; i < listWidgetModel->count(); i++)
		{
			QListWidgetItem* item = listWidgetModel->item(i);
			if (item->checkState() == Qt::Checked)
			{
				int rowId = listWidgetModel->row(item);
				ids.push_back(rowId);
			}
		}

		listWidgetModel->scrollToItem(item, QAbstractItemView::PositionAtCenter);

		// Update the tristate checkbox
		checkBoxSelectAll->blockSignals(true);
		if (ids.size() == 0)
			checkBoxSelectAll->setCheckState(Qt::Unchecked);
		else if (ids.size() == static_cast<size_t>(listWidgetModel->count()))
			checkBoxSelectAll->setCheckState(Qt::Checked);
		else
			checkBoxSelectAll->setCheckState(Qt::PartiallyChecked);
		checkBoxSelectAll->blockSignals(false);

		_glWidget->setDisplayList(ids);
		float range = _glWidget->getBoundingSphere().getRadius() * 4.0f;
		float offset = _glWidget->getFloorSize() * 1.25f;
		visualizationEnvironmentPanel->updateLightPositionRanges(range, offset);
	}
}

void ModelViewer::on_listWidgetModel_itemSelectionChanged()
{
	for (int i = 0; i < listWidgetModel->count(); i++)
	{
		QListWidgetItem* item = listWidgetModel->item(i);
		int rowId = listWidgetModel->row(item);
		if (item->isSelected())
			_glWidget->select(rowId);
		else
			_glWidget->deselect(rowId);
	}
	_glWidget->update();
	adsMaterialSettingsPanel->setSelectionState(hasSelection());
	updateSelectionStatusMessage();
}

void ModelViewer::itemEdited(QWidget* widget, QAbstractItemDelegate::EndEditHint /*hint*/)
{
	const QString path = reinterpret_cast<QLineEdit*>(widget)->text();
	int rowId = listWidgetModel->currentRow();
	std::vector<TriangleMesh*> meshes = _glWidget->getMeshStore();
	TriangleMesh* mesh = meshes.at(rowId);
	if (mesh->getName() != path)
		checkAndRenameModel(mesh, path);
}

GLMaterial ModelViewer::buildADSMaterialFromPanel() const
{
    GLMaterial mat;
    
    ADSMaterialSettingsPanel* adsPanel = 
        qobject_cast<ADSMaterialSettingsPanel*>(Ui_ModelViewer::adsMaterialSettingsPanel);
    
    // Get texture paths from panel
    QString diffusePath = adsPanel->getDiffuseTexturePath();
    QString specularPath = adsPanel->getSpecularTexturePath();
    QString normalPath = adsPanel->getNormalTexturePath();
    QString emissivePath = adsPanel->getEmissiveTexturePath();
    QString heightPath = adsPanel->getHeightTexturePath();
    QString opacityPath = adsPanel->getOpacityTexturePath();
    bool opacityInverted = adsPanel->isOpacityTextureInverted();
    
    // Set textures
    if (!diffusePath.isEmpty())
    {
        GLMaterial::Texture& tex = mat.texture(GLMaterial::TextureType::Diffuse);
        tex.path = diffusePath.toStdString();
    }
    
    if (!specularPath.isEmpty())
    {
        GLMaterial::Texture& tex = mat.texture(GLMaterial::TextureType::SpecularGlossiness);
        tex.path = specularPath.toStdString();
    }
    
    if (!normalPath.isEmpty())
    {
        GLMaterial::Texture& tex = mat.texture(GLMaterial::TextureType::Normal);
        tex.path = normalPath.toStdString();
    }
    
    if (!emissivePath.isEmpty())
    {
        GLMaterial::Texture& tex = mat.texture(GLMaterial::TextureType::Emissive);
        tex.path = emissivePath.toStdString();
    }
    
    if (!heightPath.isEmpty())
    {
        GLMaterial::Texture& tex = mat.texture(GLMaterial::TextureType::Height);
        tex.path = heightPath.toStdString();
    }
    
    if (!opacityPath.isEmpty())
    {
        GLMaterial::Texture& tex = mat.texture(GLMaterial::TextureType::Opacity);
        tex.path = opacityPath.toStdString();
    }
    
    mat.setInvertOpacityMap(opacityInverted);
    
    return mat;
}

void ModelViewer::checkAndRenameModel(TriangleMesh* mesh, const QString& name)
{
	bool duplicate = false;
	QString finalName = name;
	int dupCnt = 1;
	std::vector<TriangleMesh*> meshes = _glWidget->getMeshStore();
	do
	{
		for (TriangleMesh* msh : meshes)
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
		QApplication::setOverrideCursor(Qt::WaitCursor);
		for (const QString& fileName : std::as_const(fileNames))
		{
			loadFile(fileName);
		}
		_documentModified = true;
		_documentSaved = false;

		QApplication::restoreOverrideCursor();
		MainWindow::mainWindow()->activateWindow();
		QApplication::alert(MainWindow::mainWindow());
	}
}


#include "AssImpMeshExporter.h"
#include <AssImpMesh.h>
#include "SceneUtils.h"
void ModelViewer::onFileExport()
{
	Assimp::Exporter exporter;
	QStringList filters;
	QStringList allExtensions;
	QMap<QString, QString> filterToExtension; // Map filter -> extension

	// Build filters and track extensions
	for (unsigned int i = 0; i < exporter.GetExportFormatCount(); ++i)
	{
		const aiExportFormatDesc* desc = exporter.GetExportFormatDescription(i);
		QString ext = QString::fromUtf8(desc->fileExtension);
		QString descStr = QString::fromUtf8(desc->description);
		QString filter = QString("%1 (*.%2)").arg(descStr).arg(ext);

		filters.append(filter);
		allExtensions.append("*." + ext);
		filterToExtension[filter] = ext;
	}

	// All Supported Files filter
	QString allSupportedFilter = QString("All Supported Files (%1)").arg(allExtensions.join(' '));
	filters.prepend(allSupportedFilter);

	// Map the "All Supported Files" to empty extension (no default append)
	filterToExtension[allSupportedFilter] = "";

	QString selectedFilter;
	QString fileName = QFileDialog::getSaveFileName(this, tr("Export Model"), _lastOpenedDir, filters.join(";;"), &selectedFilter);

	if (!fileName.isEmpty())
	{
		QString extToAppend = filterToExtension[selectedFilter];

		// Append extension only if not present already
		if (!extToAppend.isEmpty() && !fileName.endsWith("." + extToAppend, Qt::CaseInsensitive))
		{
			fileName += "." + extToAppend;
		}

		// Export
		AssImpMeshExporter exporter(this);
		std::vector<TriangleMesh*> triMeshes = _glWidget->getMeshStore();
		std::vector<AssImpMesh*> assImpMeshes;
		for (TriangleMesh* triMesh : triMeshes)
			assImpMeshes.push_back(dynamic_cast<AssImpMesh*>(triMesh));

		// Check the user settings
		QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
		bool exportScene = settings.value("radioButtonExportScene", true).toBool();
				
		// Export a copy of the the original aiScene
		aiScene* copyScene = SceneUtils::deepCopyScene(_glWidget->getAssImpScene());

		// Apply inverse global transform to meshes before export, since we have applied 
		// auto scaling and rotation based on the up vector of the model and user settings.
		glm::mat4 transform = _glWidget->getGlobalSceneTransform();
		// Invert the transform
		glm::mat4 inverseTransform = glm::inverse(transform);
		// Apply to the root node of the scene, which will affect all meshes
		aiNode* node = copyScene->mRootNode;
		aiMatrix4x4 aiTransform = SceneUtils::glmToAiMatrix(inverseTransform);
		node->mTransformation = aiTransform * node->mTransformation;

		// Apply inverse transforms to the punctual lights as well
		std::vector<GPULight> lights = _glWidget->getParsedLights();
		for (GPULight& light : lights)
		{
			// Transform position (with translation)
			glm::vec4 transformedPos = inverseTransform * glm::vec4(light.position, 1.0f);
			light.position = glm::vec3(transformedPos);

			// Transform direction (no translation)
			glm::vec4 transformedDir = inverseTransform * glm::vec4(light.direction, 0.0f);
			light.direction = glm::normalize(glm::vec3(transformedDir));

			// Extract scale from transform matrix
			glm::vec3 scale(
				glm::length(glm::vec3(inverseTransform[0])),
				glm::length(glm::vec3(inverseTransform[1])),
				glm::length(glm::vec3(inverseTransform[2]))
			);
			float avgScale = (scale.x + scale.y + scale.z) / 3.0f;
			light.range *= avgScale;
		}

		// Export the meshes loaded in the scene
		AssImpMeshExporter::ExportSettings expSettings;
		expSettings.outputDirectory = QFileInfo(fileName).absolutePath();
		expSettings.copyTextures = true;
		expSettings.useRelativePaths = true;
		expSettings.deduplicateTextures = true;
		expSettings.verbose = true;		
		expSettings.lights = lights;

		aiReturn res = aiReturn_FAILURE;
		if (exportScene)
		{			
			res = exporter.exportScene(copyScene, triMeshes, fileName.toStdString(), expSettings);
			qDebug() << "Exporting scene result:" << res;
			delete copyScene;
		}
		else
		{			
			res = exporter.exportMeshes(copyScene, triMeshes, fileName, expSettings);
			qDebug() << "Exporting meshes result:" << res;
		}

		if (res == aiReturn_SUCCESS)
			QMessageBox::information(this, tr("Information"), tr("Exported %1").arg(QFileInfo(fileName).fileName()));
		else
			QMessageBox::critical(this, tr("Error"), tr("Export failed!"));
	}
}


bool ModelViewer::loadFile(const QString& fileName)
{
	_lastOpenedDir = QFileInfo(fileName).path(); // store path for next time

	QString errMsg;
	bool success = false;
	if (QFileInfo(fileName).suffix().toLower() == "mvf")
	{
		// Load MVF file
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
		success = _glWidget->loadAssImpModel(fileName, method, errMsg, _progressiveLoadingEnabled);
	}

	if (success && !_glWidget->getMeshStore().empty())
	{
		updateDisplayList();

		listWidgetModel->scrollToTop();

		_documentModified = true;

		MainWindow::showStatusMessage(tr("File loaded"), 2000);

		return success;
	}
	else
	{
		QApplication::restoreOverrideCursor();
		QMessageBox::critical(this, tr("Error"), QString(tr("Failed to load model %1")).arg(fileName) + "\n" + errMsg);
		QApplication::setOverrideCursor(Qt::WaitCursor);

		return false;
	}


	return false;
}

#include <QFile>
#include <QDataStream>

bool ModelViewer::saveToFile(const QString& fileName)
{
	QFile file(fileName);
	if (!file.open(QIODevice::WriteOnly))
		return false;
	QDataStream out(&file);
	_glWidget->serializeScene(out);
	return true;
}

bool ModelViewer::loadFromFile(const QString& fileName)
{
	QFile file(fileName);
	if (!file.open(QIODevice::ReadOnly))
		return false;
	QDataStream in(&file);
	_glWidget->deserializeScene(in);
	setWindowTitle(QFileInfo(fileName).fileName());
	return true;
}

void ModelViewer::setMaterialToSelectedItems(const GLMaterial& mat)
{
	_material = mat;
	std::vector<int> ids = getSelectedIDs();
	_glWidget->setMaterialToObjects(ids, mat);
	_glWidget->updateView();
	updateControls();
}

void ModelViewer::setTexturesToSelectedItems(const GLMaterial& mat)
{
	if (checkForActiveSelection())
	{
		_material = mat;
		std::vector<int> ids = getSelectedIDs();
		_glWidget->setTexturesToObjects(ids, mat);
		_glWidget->updateView();
	}
}

void ModelViewer::setTextureSamplersToSelectedItems(const GLMaterial* material, GLMaterial::TextureType type)
{
	if (!_glWidget) return;
	_glWidget->synchronizeTextureCache(material, type);
}

void ModelViewer::on_checkBoxSelectAll_stateChanged(int arg1)
{
	if (arg1 != Qt::PartiallyChecked)
	{
		if (listWidgetModel->count())
		{
			bool oldState = listWidgetModel->blockSignals(true);
			for (int i = 0; i < listWidgetModel->count(); i++)
			{
				QListWidgetItem* item = listWidgetModel->item(i);
				item->setCheckState(checkBoxSelectAll->checkState());
			}
			listWidgetModel->blockSignals(oldState);
			on_listWidgetModel_itemChanged(nullptr);
		}
	}
	else
	{
		checkBoxSelectAll->setCheckState(Qt::Checked);
	}
}

void ModelViewer::on_toolBox_currentChanged(int index)
{
	if (index == 3) // Transformations page
	{
		updateTransformationValues();
	}
}

void ModelViewer::switchToRealisticRendering()
{
	if (_glWidget->getDisplayMode() == DisplayMode::REALSHADED)
		return;
	QToolTip::showText(groupBoxVisModel->mapToGlobal(groupBoxVisModel->pos()), "Switching to Realistic Display Mode", this);
	_glWidget->setDisplayMode(DisplayMode::REALSHADED);
}

void ModelViewer::lightingType_toggled(QAbstractButton*, bool)
{
	int indexADS = toolBox->indexOf(toolBox->findChild<QWidget*>("pageADSSettings"));
	int indexPBR = toolBox->indexOf(toolBox->findChild<QWidget*>("pageTextureMapping"));

	if (radioButtonADSL->isChecked())
	{
		toolBox->setItemEnabled(indexADS, true);
		toolBox->setItemEnabled(indexPBR, false);
		toolBox->setCurrentIndex(indexADS);
		_glWidget->setRenderingMode(RenderingMode::ADS_BLINN_PHONG);
		visualizationEnvironmentPanel->setPBRLightingMode(false);
	}
	if (radioButtonTXPBR->isChecked())
	{
		toolBox->setItemEnabled(indexADS, false);
		toolBox->setItemEnabled(indexPBR, true);
		toolBox->setCurrentIndex(indexPBR);
		_glWidget->setRenderingMode(RenderingMode::PHYSICALLY_BASED_RENDERING);
		visualizationEnvironmentPanel->setPBRLightingMode(true);
		_glWidget->setSkyBoxTextureHDRI(true);
		switchToRealisticRendering();
	}
	updateControls();
	_glWidget->update();
}

void ModelViewer::onDisplayModeChanged(int mode)
{	
	visualizationEnvironmentPanel->onDisplayModeChanged(mode);	
}

void ModelViewer::onTextureCacheCleared()
{
	if (_glWidget)
	{
		_glWidget->clearTextureCache();
	}
}

void ModelViewer::on_toolButtonClearOpacityTex_clicked()
{
	if (checkForActiveSelection())
	{
		std::vector<int> ids = getSelectedIDs();
		QApplication::setOverrideCursor(Qt::WaitCursor);
		_glWidget->clearADSOpacityTexMap(ids);
		_glWidget->updateView();
		QApplication::restoreOverrideCursor();
	}
}

void ModelViewer::applyADSTextures()
{
	bool allOK = true;
	if (!_hasADSDiffuseTex || (_hasADSDiffuseTex && _diffuseADSTexture == ""))
	{
		QMessageBox::critical(this, tr("ADS Texture Missing"), tr("Diffuse map texture not set"));
		allOK = false;
	}
	else if (_hasADSSpecularTex && _specularADSTexture == "")
	{
		QMessageBox::critical(this, tr("ADS Texture Missing"), tr("Specular map texture not set"));
		allOK = false;
	}
	else if (_hasADSNormalTex && _normalADSTexture == "")
	{
		QMessageBox::critical(this, tr("ADS Texture Missing"), tr("Normal map texture not set"));
		allOK = false;
	}
	else if (_hasADSHeightTex && _heightADSTexture == "")
	{
		QMessageBox::critical(this, tr("ADS Texture Missing"), tr("Height map texture not set"));
		allOK = false;
	}

	if (allOK)
	{
		if (checkForActiveSelection())
		{
			QApplication::setOverrideCursor(Qt::WaitCursor);

			std::vector<int> ids = getSelectedIDs();
			_glWidget->enableADSDiffuseTexMap(ids, _hasADSDiffuseTex);
			if (_hasADSDiffuseTex)
			{
				_glWidget->setADSDiffuseTexMap(ids, _diffuseADSTexture);
			}
			_glWidget->enableADSSpecularTexMap(ids, _hasADSSpecularTex);
			if (_hasADSSpecularTex)
			{
				_glWidget->setADSSpecularTexMap(ids, _specularADSTexture);
			}
			_glWidget->enableADSNormalTexMap(ids, _hasADSNormalTex);
			if (_hasADSNormalTex)
			{
				_glWidget->setADSNormalTexMap(ids, _normalADSTexture);
			}
			_glWidget->enableADSHeightTexMap(ids, _hasADSHeightTex);
			if (_hasADSHeightTex)
			{
				_glWidget->setADSHeightTexMap(ids, _heightADSTexture);
			}
			_glWidget->enableADSOpacityTexMap(ids, _hasADSOpacityTex);
			if (_hasADSOpacityTex)
			{
				_glWidget->setADSOpacityTexMap(ids, _opacityADSTexture);
			}
			_glWidget->updateView();
			QApplication::restoreOverrideCursor();
		}
	}
}

void ModelViewer::clearADSTextures()
{
	if (checkForActiveSelection())
	{
		std::vector<int> ids = getSelectedIDs();
		QApplication::setOverrideCursor(Qt::WaitCursor);
		_glWidget->clearADSTexMaps(ids);
		_glWidget->updateView();
		QApplication::restoreOverrideCursor();
	}
}

void ModelViewer::onPredefinedMaterialSelected(const GLMaterial& mat)
{	
	if (!checkForActiveSelection())
	{
		qDebug() << "No active selection, returning";
		return;
	}

	QApplication::setOverrideCursor(Qt::WaitCursor);

	QVector<QUuid> uuids;
	std::vector<int> ids = getSelectedIDs();
	for (int id : ids)
	{
		QUuid uuid = _glWidget->getUuidByIndex(id);
		if (!uuid.isNull())
			uuids.append(uuid);
	}

	QString materialName;
	MaterialEditorPanel* panel = Ui_ModelViewer::predefinedMaterialsPanel;
	if (panel)
	{
		MaterialLibraryWidget* tree = panel->findChild<MaterialLibraryWidget*>("treeWidget");
		if (tree && !tree->selectedItems().isEmpty())
		{
			materialName = tree->selectedItems().first()->text(0);
		}
	}
		
	_undoStack->push(new ApplyMaterialCommand(
		this, _glWidget, uuids, mat, materialName
	));

	QApplication::restoreOverrideCursor();
}

void ModelViewer::onCustomMaterialApplied(const GLMaterial& mat)
{
	if (!checkForActiveSelection())
	{
		qDebug() << "No active selection, returning";
		return;
	}

	QApplication::setOverrideCursor(Qt::WaitCursor);

	QVector<QUuid> uuids;
	std::vector<int> ids = getSelectedIDs();
	for (int id : ids)
	{
		QUuid uuid = _glWidget->getUuidByIndex(id);
		if (!uuid.isNull())
			uuids.append(uuid);
	}

	QString materialName = "Custom Material";

	_undoStack->push(new ApplyMaterialCommand(
		this, _glWidget, uuids, mat, materialName
	));

	QApplication::restoreOverrideCursor();
}

void ModelViewer::onTexturesApplied(const GLMaterial* mat)
{
	if (!mat || !checkForActiveSelection())
		return;

	QApplication::setOverrideCursor(Qt::WaitCursor);

	QVector<QUuid> uuids;
	std::vector<int> ids = getSelectedIDs();
	for (int id : ids)
	{
		QUuid uuid = _glWidget->getUuidByIndex(id);
		if (!uuid.isNull())
			uuids.append(uuid);
	}

	_undoStack->push(new ApplyPBRTexturesCommand(
		this, _glWidget, uuids, *mat  // Dereference pointer
	));

	QApplication::restoreOverrideCursor();
}

void ModelViewer::onADSColorsApplied()
{
	if (!checkForActiveSelection())
		return;

	QApplication::setOverrideCursor(Qt::WaitCursor);

	// Get current color values from panel (single source of truth!)
	ADSMaterialSettingsPanel* adsPanel =
		qobject_cast<ADSMaterialSettingsPanel*>(Ui_ModelViewer::adsMaterialSettingsPanel);

	QVector3D ambient = adsPanel->getAmbientColor();
	QVector3D diffuse = adsPanel->getDiffuseColor();
	QVector3D specular = adsPanel->getSpecularColor();
	QVector3D emissive = adsPanel->getEmissiveColor();
	float opacity = adsPanel->getOpacity();
	int shininess = adsPanel->getShininess();

	// Get UUIDs of selected meshes
	QVector<QUuid> uuids;
	std::vector<int> ids = getSelectedIDs();
	for (int id : ids)
	{
		QUuid uuid = _glWidget->getUuidByIndex(id);
		if (!uuid.isNull())
			uuids.append(uuid);
	}

	// Create and push command
	_undoStack->push(new ApplyADSColorsCommand(
		this, _glWidget, adsPanel, uuids,
		ambient, diffuse, specular, emissive, opacity, shininess
	));

	QApplication::restoreOverrideCursor();
}

void ModelViewer::onOpacitySliderReleased()
{
	if (!checkForActiveSelection())
		return;

	QApplication::setOverrideCursor(Qt::WaitCursor);

	// Get CURRENT state from panel (including new opacity!)
	ADSMaterialSettingsPanel* adsPanel =
		qobject_cast<ADSMaterialSettingsPanel*>(Ui_ModelViewer::adsMaterialSettingsPanel);

	QVector3D ambient = adsPanel->getAmbientColor();
	QVector3D diffuse = adsPanel->getDiffuseColor();
	QVector3D specular = adsPanel->getSpecularColor();
	QVector3D emissive = adsPanel->getEmissiveColor();
	float opacity = adsPanel->getOpacity(); 
	int shininess = adsPanel->getShininess();

	// Get UUIDs
	QVector<QUuid> uuids;
	std::vector<int> ids = getSelectedIDs();
	for (int id : ids)
	{
		QUuid uuid = _glWidget->getUuidByIndex(id);
		if (!uuid.isNull())
			uuids.append(uuid);
	}

	// Reuse ApplyADSColorsCommand!
	_undoStack->push(new ApplyADSColorsCommand(
		this, _glWidget, adsPanel, uuids,
		ambient, diffuse, specular, emissive, opacity, shininess
	));

	QApplication::restoreOverrideCursor();
}

void ModelViewer::onShininessSliderReleased()
{
	if (!checkForActiveSelection())
		return;

	QApplication::setOverrideCursor(Qt::WaitCursor);

	// Get CURRENT state from panel (including new shininess!)
	ADSMaterialSettingsPanel* adsPanel =
		qobject_cast<ADSMaterialSettingsPanel*>(Ui_ModelViewer::adsMaterialSettingsPanel);

	QVector3D ambient = adsPanel->getAmbientColor();
	QVector3D diffuse = adsPanel->getDiffuseColor();
	QVector3D specular = adsPanel->getSpecularColor();
	QVector3D emissive = adsPanel->getEmissiveColor();
	float opacity = adsPanel->getOpacity();
	int shininess = adsPanel->getShininess();

	// Get UUIDs
	QVector<QUuid> uuids;
	std::vector<int> ids = getSelectedIDs();
	for (int id : ids)
	{
		QUuid uuid = _glWidget->getUuidByIndex(id);
		if (!uuid.isNull())
			uuids.append(uuid);
	}

	// Reuse ApplyADSColorsCommand!
	_undoStack->push(new ApplyADSColorsCommand(
		this, _glWidget, adsPanel, uuids,
		ambient, diffuse, specular, emissive, opacity, shininess
	));

	QApplication::restoreOverrideCursor();
}

void ModelViewer::onADSTexturesApplied()
{
	if (!checkForActiveSelection())
		return;

	// Get texture paths from panel (using getters approach)
	ADSMaterialSettingsPanel* adsPanel =
		qobject_cast<ADSMaterialSettingsPanel*>(Ui_ModelViewer::adsMaterialSettingsPanel);

	QString diffusePath = adsPanel->getDiffuseTexturePath();
	QString specularPath = adsPanel->getSpecularTexturePath();
	QString normalPath = adsPanel->getNormalTexturePath();
	QString emissivePath = adsPanel->getEmissiveTexturePath();
	QString heightPath = adsPanel->getHeightTexturePath();
	QString opacityPath = adsPanel->getOpacityTexturePath();
	bool opacityInverted = adsPanel->isOpacityTextureInverted();

	// Check if at least diffuse texture is set
	if (diffusePath.isEmpty())
	{
		QMessageBox::warning(this, tr("No Textures"),
			tr("Please select at least a diffuse texture."));
		return;
	}

	QApplication::setOverrideCursor(Qt::WaitCursor);

	// Get UUIDs of selected meshes
	QVector<QUuid> uuids;
	std::vector<int> ids = getSelectedIDs();
	for (int id : ids)
	{
		QUuid uuid = _glWidget->getUuidByIndex(id);
		if (!uuid.isNull())
			uuids.append(uuid);
	}

	// Create and push command with PATHS, not GLMaterial
	_undoStack->push(new ApplyADSTexturesCommand(
		this, _glWidget, uuids,
		diffusePath, specularPath, normalPath,
		emissivePath, heightPath, opacityPath,
		opacityInverted
	));

	QApplication::restoreOverrideCursor();
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

void ModelViewer::setSelectionWithUndo(const QSet<int>& newSelection)
{
	// Create and push the undo command
	// Note: push() automatically calls redo() on the command
	_undoStack->push(new SelectionCommand(this, _glWidget, newSelection));
}

void ModelViewer::setSelectionWithoutUndo(const QSet<int>& selection)
{
	// Block signals to prevent triggering selection changed handlers
	bool oldState = listWidgetModel->blockSignals(true);

	// Clear current selection
	listWidgetModel->clearSelection();

	// Apply new selection
	for (int id : selection)
	{
		if (id >= 0 && id < listWidgetModel->count())
		{
			QListWidgetItem* item = listWidgetModel->item(id);
			if (item)
				item->setSelected(true);
		}
	}

	// Restore signals
	listWidgetModel->blockSignals(oldState);

	// Manually sync to GLWidget
	on_listWidgetModel_itemSelectionChanged();
}

void ModelViewer::setSelectionWithoutUndo(const QSet<QUuid>& uuids)
{
	// Convert UUIDs to indices
	QSet<int> indices;
	for (const QUuid& uuid : uuids)
	{
		int index = _glWidget->getIndexByUuid(uuid);
		if (index >= 0)
			indices.insert(index);
	}

	// Use existing implementation
	setSelectionWithoutUndo(indices);
}

QSet<QUuid> ModelViewer::getVisibleUuids() const
{
	std::vector<int> displayedIds = _glWidget->getDisplayedObjectsIds();
	QSet<QUuid> visibleUuids;

	for (int id : displayedIds)
	{
		QUuid uuid = _glWidget->getUuidByIndex(id);
		if (!uuid.isNull())
			visibleUuids.insert(uuid);
	}

	return visibleUuids;
}

void ModelViewer::setVisibilityWithUndo(const QSet<QUuid>& newVisibleUuids,
	const QString& commandText)
{
	// Create and push the undo command
	// Note: push() automatically calls redo() on the command
	_undoStack->push(new VisibilityCommand(this, _glWidget,
		newVisibleUuids, commandText));
}

void ModelViewer::setVisibilityWithoutUndo(const QSet<QUuid>& visibleUuids)
{
	// Block signals to prevent triggering itemChanged handlers
	bool oldState = listWidgetModel->blockSignals(true);

	// Update check states for all items based on visibility set
	for (int i = 0; i < listWidgetModel->count(); ++i)
	{
		QUuid uuid = _glWidget->getUuidByIndex(i);
		QListWidgetItem* item = listWidgetModel->item(i);

		if (visibleUuids.contains(uuid))
		{
			item->setCheckState(Qt::Checked);
		}
		else
		{
			item->setCheckState(Qt::Unchecked);
		}
	}

	// Restore signals
	listWidgetModel->blockSignals(oldState);

	// Manually trigger visibility update
	// This will update _displayedObjectsIds and _hiddenObjectsIds
	on_listWidgetModel_itemChanged(nullptr);
}
