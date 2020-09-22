#include <QApplication>
#include <MainWindow.h>
#include "ModelViewer.h"
#include "GLWidget.h"
#include "SphericalHarmonicsEditor.h"
#include "TriangleMesh.h"
#include "STLMesh.h"
#include "MeshProperties.h"

ModelViewer::ModelViewer(QWidget* parent) : QWidget(parent)
{
	_bFirstTime = true;
	_bDeletionInProgress = false;

	_lastOpenedDir = QApplication::applicationDirPath();
	_lastSelectedFilter = "All Models(*.dae *.xml *.blend *.bvh *.3ds *.ase *.obj *.ply *.dxf *.ifc "
		"*.nff *.smd *.vta *.mdl *.md2 *.md3 *.pk3 *.mdc *.md5mesh *.md5anim "
		"*.md5camera *.x *.q3o *.q3s *.raw *.ac *.stl *.dxf *.irrmesh *.xml "
		"*.irr *.off. *.ter *.mdl *.hmp *.mesh.xml *.skeleton.xml *.material "
		"*.ms3d *.lwo *.lws *.lxo *.csm *.ply *.cob *.scn *.xgl *.zgl)";
	_textureDirOpenedFirstTime = true;

	isometricView = new QAction(QIcon(":/new/prefix1/res/isometric.png"), "Isometric", this);
	isometricView->setObjectName(QString::fromUtf8("isometricView"));
	isometricView->setShortcut(QKeySequence(Qt::Key_Home));

	dimetricView = new QAction(QIcon(":/new/prefix1/res/dimetric.png"), "Dimetric", this);
	dimetricView->setObjectName(QString::fromUtf8("dimetricView"));
	dimetricView->setShortcut(QKeySequence(Qt::SHIFT + Qt::Key_End));

	trimetricView = new QAction(QIcon(":/new/prefix1/res/trimetric.png"), "Trimetric", this);
	trimetricView->setObjectName(QString::fromUtf8("trimetricView"));
	trimetricView->setShortcut(QKeySequence(Qt::Key_End));

	displayShaded = new QAction(QIcon(":/new/prefix1/res/shaded.png"), "Shaded", this);
	displayShaded->setObjectName(QString::fromUtf8("displayShaded"));
	displayShaded->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_S));

	displayWireframe = new QAction(QIcon(":/new/prefix1/res/wireframe.png"), "Wireframe", this);
	displayWireframe->setObjectName(QString::fromUtf8("displayWireframe"));
	displayWireframe->setShortcut(QKeySequence(Qt::CTRL + Qt::ALT + Qt::Key_W));

	displayWireShaded = new QAction(QIcon(":/new/prefix1/res/wireshaded.png"), "Wire Shaded", this);
	displayWireShaded->setObjectName(QString::fromUtf8("displayWireShaded"));
	displayWireShaded->setShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_W));

	displayRealShaded = new QAction(QIcon(":/new/prefix1/res/realshaded.png"), "Realistic", this);
	displayRealShaded->setObjectName(QString::fromUtf8("displayRealShaded"));
	displayRealShaded->setShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_R));

	setupUi(this);

	// View
	QMenu* axoMenu = new QMenu;
	axoMenu->addAction(isometricView);
	axoMenu->addAction(dimetricView);
	axoMenu->addAction(trimetricView);
	// add action to widget as well
	addAction(isometricView);
	addAction(dimetricView);
	addAction(trimetricView);

	toolButtonIsometricView->setMenu(axoMenu);
	toolButtonIsometricView->setDefaultAction(isometricView);
	QObject::connect(toolButtonIsometricView, SIGNAL(triggered(QAction*)),
		toolButtonIsometricView, SLOT(setDefaultAction(QAction*)));

	// Shading
	QMenu* dispMenu = new QMenu;
	dispMenu->addAction(displayRealShaded);
	dispMenu->addAction(displayShaded);
	dispMenu->addAction(displayWireframe);
	dispMenu->addAction(displayWireShaded);
	// add action to widget as well
	addAction(displayRealShaded);
	addAction(displayShaded);
	addAction(displayWireframe);
	addAction(displayWireShaded);

	toolButtonDisplayMode->setMenu(dispMenu);
	toolButtonDisplayMode->setDefaultAction(displayShaded);
	QObject::connect(toolButtonDisplayMode, SIGNAL(triggered(QAction*)),
		toolButtonDisplayMode, SLOT(setDefaultAction(QAction*)));

	setAttribute(Qt::WA_DeleteOnClose);

	QSurfaceFormat format = QSurfaceFormat::defaultFormat();
	format.setProfile(QSurfaceFormat::CoreProfile);
	format.setDepthBufferSize(24);
	format.setStencilBufferSize(8);
	format.setSamples(4);
	format.setSwapInterval(0);
	format.setStereo(true);
	_glWidget = new GLWidget(this, "glwidget");
	_glWidget->setAttribute(Qt::WA_DeleteOnClose);
	_glWidget->setFormat(format);
	_glWidget->setMouseTracking(true);
	// Put the GL widget inside the frame
	QVBoxLayout* flayout = new QVBoxLayout(glframe);
	flayout->addWidget(_glWidget, 1);
	_glWidget->installEventFilter(tabWidget);
	tabWidget->setParent(_glWidget);
	_glWidget->layout()->addWidget(tabWidget);
	tabWidget->setAutoHide(true);
	//connect(_glWidget, SIGNAL(displayListSet()), this, SLOT(updateDisplayList()));

	QObject::connect(_glWidget, SIGNAL(windowZoomEnded()), toolButtonWindowZoom, SLOT(toggle()));
	QObject::connect(_glWidget, SIGNAL(singleSelectionDone(int)), this, SLOT(setListRow(int)));
	QObject::connect(_glWidget, SIGNAL(sweepSelectionDone(QList<int>)), this, SLOT(setListRows(QList<int>)));

	listWidgetModel->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(listWidgetModel, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showContextMenu(QPoint)));
	QShortcut* shortcut = new QShortcut(QKeySequence(Qt::Key_Delete), listWidgetModel);
	connect(shortcut, SIGNAL(activated()), this, SLOT(deleteSelectedItems()));

	shortcut = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_O), this);
	connect(shortcut, SIGNAL(activated()), this, SLOT(on_toolButtonOpen_clicked()));

	// Views
	shortcut = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_T), this);
	connect(shortcut, SIGNAL(activated()), this, SLOT(on_toolButtonTopView_clicked()));
	shortcut = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_B), this);
	connect(shortcut, SIGNAL(activated()), this, SLOT(on_toolButtonBottomView_clicked()));
	shortcut = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_F), this);
	connect(shortcut, SIGNAL(activated()), this, SLOT(on_toolButtonFrontView_clicked()));
	shortcut = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_R), this);
	connect(shortcut, SIGNAL(activated()), this, SLOT(on_toolButtonBackView_clicked()));
	shortcut = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_L), this);
	connect(shortcut, SIGNAL(activated()), this, SLOT(on_toolButtonLeftView_clicked()));
	shortcut = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_J), this);
	connect(shortcut, SIGNAL(activated()), this, SLOT(on_toolButtonRightView_clicked()));
	shortcut = new QShortcut(QKeySequence(Qt::Key_F4), this);
	connect(shortcut, SIGNAL(activated()), this, SLOT(clickMultiViewButton()));

	connect(checkBoxLockLightCamera, SIGNAL(toggled(bool)), _glWidget, SLOT(lockLightAndCamera(bool)));
	connect(doubleSpinBoxRepeatS, SIGNAL(valueChanged(double)), _glWidget, SLOT(setFloorTexRepeatS(double)));
	connect(doubleSpinBoxRepeatT, SIGNAL(valueChanged(double)), _glWidget, SLOT(setFloorTexRepeatT(double)));
	connect(doubleSpinBoxSkyBoxFOV, SIGNAL(valueChanged(double)), _glWidget, SLOT(setSkyBoxFOV(double)));
	connect(doubleSpinBoxFloorOffset, SIGNAL(valueChanged(double)), _glWidget, SLOT(setFloorOffsetPercent(double)));
	connect(checkBoxSkyBoxHDRI, SIGNAL(toggled(bool)), _glWidget, SLOT(setSkyBoxTextureHDRI(bool)));

	connect(checkBoxHDRToneMapping, SIGNAL(toggled(bool)), _glWidget, SLOT(enableHDRToneMapping(bool)));
	connect(checkBoxGammaCorrection, SIGNAL(toggled(bool)), _glWidget, SLOT(enableGammaCorrection(bool)));
	connect(doubleSpinBoxScreenGamma, SIGNAL(valueChanged(double)), _glWidget, SLOT(setScreenGamma(double)));

	connect(buttonGroupLighting, SIGNAL(buttonToggled(int, bool)), this, SLOT(lightingType_toggled(int, bool)));
	toolBox->setItemEnabled(0, true);
	toolBox->setItemEnabled(1, false);
	toolBox->setItemEnabled(2, false);
	toolBox->setCurrentIndex(0);

	connect(sliderTransparency_2, SIGNAL(valueChanged(int)), this, SLOT(on_sliderTransparency_valueChanged(int)));
    connect(pushButtonDefaultMatlsPBR, SIGNAL(clicked()), this, SLOT(on_pushButtonDefaultMatls_clicked()));

	_opacity = 1.0f;
	//_ambiMat = { 0.2109375f, 0.125f, 0.05078125f, _opacity };
	//_diffMat = { 0.7109375f, 0.62890625f, 0.55078125f, _opacity };
	//_specMat = { 0.37890625f, 0.390625f, 0.3359375f, _opacity };
	_ambiMat = { 126 / 256.0f, 124 / 256.0f, 116 / 256.0f, _opacity };
	_diffMat = { 126 / 256.0f, 124 / 256.0f, 116 / 256.0f, _opacity };
	_specMat = { 140 / 256.0f, 140 / 256.0f, 130 / 256.0f, _opacity };
	_shine = fabs(128.0f * 0.05f);
	_emmiMat = { 0.0f, 0.0f, 0.0f, _opacity };
	_specRef = { 1.0f, 1.0f, 1.0f, 1.0f };
	//_shine = 128 * 0.2f;
	_metallic = false;
	_bHasTexture = false;
	setAlbedoFromADS(_metallic);
	_PBRMetallic = 1.0f;
	_PBRRoughness = 0.7f;
	_hasAlbedoTex = false;
	_hasMetallicTex = false;
	_hasRoughnessTex = false;
	_hasAOTex = false;
	_hasNormalTex = false;
	_hasHeightTex = false;

	updateControls();
}

ModelViewer::~ModelViewer()
{
	if (_glWidget)
	{
		delete _glWidget;
	}
}

void ModelViewer::setListRow(int index)
{
	if (index != -1)
	{
		QListWidgetItem* item = listWidgetModel->item(index);
		item->setSelected(!item->isSelected());
		listWidgetModel->setCurrentItem(item);
		if (toolBox->currentIndex() == 3)
			updateTransformationValues();
	}
	else
	{
		for (QListWidgetItem* item : listWidgetModel->selectedItems())
		{
			item->setSelected(false);
		}
		resetTransformationValues();
	}
}

void ModelViewer::setListRows(QList<int> indices)
{
	if (indices.count())
	{
		for (int index : indices)
		{
			QListWidgetItem* item = listWidgetModel->item(index);
			item->setSelected(!item->isSelected());
		}
	}
}

void ModelViewer::setTransformation()
{
	QApplication::setOverrideCursor(Qt::WaitCursor);
	if (listWidgetModel->count())
	{
		std::vector<int> ids;
		QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
		for (QListWidgetItem* i : items)
		{
			int rowId = listWidgetModel->row(i);
			ids.push_back(rowId);
		}

		QVector3D translate(doubleSpinBoxDX->value(), doubleSpinBoxDY->value(), doubleSpinBoxDZ->value());
		QVector3D rotate(doubleSpinBoxRX->value(), doubleSpinBoxRY->value(), doubleSpinBoxRZ->value());
		QVector3D scale(doubleSpinBoxSX->value(), doubleSpinBoxSY->value(), doubleSpinBoxSZ->value());
		_glWidget->setTransformation(ids, translate, rotate, scale);
	}
	QApplication::restoreOverrideCursor();
}

void ModelViewer::resetTransformation()
{
	QApplication::setOverrideCursor(Qt::WaitCursor);
	if (listWidgetModel->count())
	{
		std::vector<int> ids;
		QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
		for (QListWidgetItem* i : items)
		{
			int rowId = listWidgetModel->row(i);
			ids.push_back(rowId);
		}
		doubleSpinBoxDX->setValue(0.0f);
		doubleSpinBoxDY->setValue(0.0f);
		doubleSpinBoxDZ->setValue(0.0f);
		doubleSpinBoxRX->setValue(0.0f);
		doubleSpinBoxRY->setValue(0.0f);
		doubleSpinBoxRZ->setValue(0.0f);
		doubleSpinBoxSX->setValue(1.0f);
		doubleSpinBoxSY->setValue(1.0f);
		doubleSpinBoxSZ->setValue(1.0f);
		_glWidget->resetTransformation(ids);
	}
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
				doubleSpinBoxDX->setValue(trans.x());
				doubleSpinBoxDY->setValue(trans.y());
				doubleSpinBoxDZ->setValue(trans.z());

				QVector3D rot = mesh->getRotation();
				doubleSpinBoxRX->setValue(rot.x());
				doubleSpinBoxRY->setValue(rot.y());
				doubleSpinBoxRZ->setValue(rot.z());

				QVector3D scale = mesh->getScaling();
				doubleSpinBoxSX->setValue(scale.x());
				doubleSpinBoxSY->setValue(scale.y());
				doubleSpinBoxSZ->setValue(scale.z());
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
	doubleSpinBoxDX->setValue(0.0f);
	doubleSpinBoxDY->setValue(0.0f);
	doubleSpinBoxDZ->setValue(0.0f);

	doubleSpinBoxRX->setValue(0.0f);
	doubleSpinBoxRY->setValue(0.0f);
	doubleSpinBoxRZ->setValue(0.0f);

	doubleSpinBoxSX->setValue(1.0f);
	doubleSpinBoxSY->setValue(1.0f);
	doubleSpinBoxSZ->setValue(1.0f);
}

void ModelViewer::updateControls()
{
	bool oldState = blockSignals(true);
	QColor col;
	QString qss;
	QVector4D ambientLight = _glWidget->getAmbientLight();
	col.setRgbF(ambientLight.x(), ambientLight.y(), ambientLight.z());
	qss = QString("background-color: %1;color: %2").arg(col.name()).arg(col.lightness() < 75 ? QColor(Qt::white).name() : QColor(Qt::black).name());
	pushButtonLightAmbient->setStyleSheet(qss);

	QVector4D diffuseLight = _glWidget->getDiffuseLight();
	col.setRgbF(diffuseLight.x(), diffuseLight.y(), diffuseLight.z());
	qss = QString("background-color: %1;color: %2").arg(col.name()).arg(col.lightness() < 75 ? QColor(Qt::white).name() : QColor(Qt::black).name());
	pushButtonLightDiffuse->setStyleSheet(qss);

	QVector4D specularLight = _glWidget->getSpecularLight();
	col.setRgbF(specularLight.x(), specularLight.y(), specularLight.z());
	qss = QString("background-color: %1;color: %2").arg(col.name()).arg(col.lightness() < 75 ? QColor(Qt::white).name() : QColor(Qt::black).name());
	pushButtonLightSpecular->setStyleSheet(qss);
	// ADS Lighting
	if (radioButtonADSL->isChecked())
	{
		sliderShine->setValue((int)_shine);
		sliderTransparency->setValue((int)(1000 * _opacity));

		col.setRgbF(_ambiMat.x(), _ambiMat.y(), _ambiMat.z());
		qss = QString("background-color: %1;color: %2").arg(col.name()).arg(col.lightness() < 75 ? QColor(Qt::white).name() : QColor(Qt::black).name());
		pushButtonMaterialAmbient->setStyleSheet(qss);

		col.setRgbF(_diffMat.x(), _diffMat.y(), _diffMat.z());
		qss = QString("background-color: %1;color: %2").arg(col.name()).arg(col.lightness() < 75 ? QColor(Qt::white).name() : QColor(Qt::black).name());
		pushButtonMaterialDiffuse->setStyleSheet(qss);

		col.setRgbF(_specMat.x(), _specMat.y(), _specMat.z());
		qss = QString("background-color: %1;color: %2").arg(col.name()).arg(col.lightness() < 75 ? QColor(Qt::white).name() : QColor(Qt::black).name());
		pushButtonMaterialSpecular->setStyleSheet(qss);

		col.setRgbF(_emmiMat.x(), _emmiMat.y(), _emmiMat.z());
		qss = QString("background-color: %1;color: %2").arg(col.name()).arg(col.lightness() < 75 ? QColor(Qt::white).name() : QColor(Qt::black).name());
		pushButtonMaterialEmissive->setStyleSheet(qss);
	}
	// PBR Direct Lighting
	if (radioButtonDLPBR->isChecked())
	{
		col.setRgbF(_albedoColor.x(), _albedoColor.y(), _albedoColor.z());
		qss = QString("background-color: %1;color: %2").arg(col.name()).arg(col.lightness() < 75 ? QColor(Qt::white).name() : QColor(Qt::black).name());
		pushButtonAlbedoColor->setStyleSheet(qss);
		sliderMetallic->setValue((int)(_PBRMetallic * 1000));
		sliderRoughness->setValue((int)(_PBRRoughness * 1000));
		sliderTransparency_2->setValue((int)(_opacity * 1000));
	}
	blockSignals(oldState);
}

QString ModelViewer::getSupportedImagesFilter()
{
	QList<QByteArray> supportedFormats = QImageReader::supportedImageFormats();
	QList<QString> filters;
	QString filter("All Supported Images (");
	for (QByteArray ba : supportedFormats)
	{
		filter += QString("*.%1 ").arg(QString(ba));
		filters.push_back(QString("*.%1").arg(QString(ba)));
	}
	filter += ")";
	for (QString fil : filters)
	{
		filter += ";;" + fil;
	}
	return filter;
}

void ModelViewer::updateDisplayList()
{
	QApplication::setOverrideCursor(Qt::WaitCursor);
	listWidgetModel->clear();
	std::vector<TriangleMesh*> store = _glWidget->getMeshStore();
	std::vector<int> ids = _glWidget->getDisplayedObjectsIds();
	int id = 0;
	for (TriangleMesh* mesh : store)
	{
		QListWidgetItem* item = new QListWidgetItem(mesh->getName());
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable); // set checkable flag
		// AND initialize check state
		if (std::count(ids.begin(), ids.end(), id))
			item->setCheckState(Qt::Checked);
		else
			item->setCheckState(Qt::Unchecked);
		listWidgetModel->addItem(item);
		id++;
	}
	float range = _glWidget->getBoundingSphere().getRadius();
	sliderLightPosX->setRange(-range, range);
	sliderLightPosX->setSingleStep(range / 100);
	sliderLightPosY->setRange(-range, range);
	sliderLightPosY->setSingleStep(range / 100);
	sliderLightPosZ->setRange(-range, range);
	sliderLightPosZ->setSingleStep(range / 100);
	QApplication::restoreOverrideCursor();
}

void ModelViewer::showEvent(QShowEvent*)
{
	//showMaximized();
	if (_bFirstTime)
	{
		updateDisplayList();
		_bFirstTime = false;
	}
}

void ModelViewer::showContextMenu(const QPoint& pos)
{
	setFocus();
	if (listWidgetModel->selectedItems().count() != 0)
	{
		// Create menu and insert some actions
		QMenu myMenu;

		QList<QListWidgetItem*> selectedItems = listWidgetModel->selectedItems();
		if (selectedItems.count() <= 1 && selectedItems.at(0)->checkState() == Qt::Checked)
			myMenu.addAction("Center Screen", this, SLOT(centerScreen()));

		myMenu.addAction("Visualization Settings", this, SLOT(showVisualizationModelPage()));
		myMenu.addAction("Transformations", this, SLOT(showTransformationsPage()));
		myMenu.addAction("Hide", this, SLOT(hideSelectedItems()));
		myMenu.addAction("Show", this, SLOT(showSelectedItems()));
		myMenu.addAction("Show Only", this, SLOT(showOnlySelectedItems()));
		myMenu.addAction("Delete", this, SLOT(deleteSelectedItems()));

		if (selectedItems.count() <= 1 && selectedItems.at(0)->checkState() == Qt::Checked)
			myMenu.addAction("Mesh Info", this, SLOT(displaySelectedMeshInfo()));

		// Show context menu at handling position
		myMenu.exec(listWidgetModel->mapToGlobal(pos));
	}
}

void ModelViewer::centerScreen()
{
	QListWidgetItem* item = listWidgetModel->currentItem();
	int rowId = listWidgetModel->row(item);
	_glWidget->centerScreen(rowId);
}

void ModelViewer::deleteSelectedItems()
{
	QApplication::setOverrideCursor(Qt::WaitCursor);
	QList<QListWidgetItem*> selectedItems = listWidgetModel->selectedItems();
	if (!selectedItems.isEmpty())
	{
		if (QMessageBox::question(this, "Confirmation", "Delete selection?") == QMessageBox::Yes)
		{
			_bDeletionInProgress = true;
			// If multiple selection is on, we need to erase all selected items
			for (QListWidgetItem* item : selectedItems)
			{
				item->setCheckState(Qt::Unchecked);
			}
			for (QListWidgetItem* item : selectedItems)
			{
				int rowId = listWidgetModel->row(item);

				// Remove the displayed object
				_glWidget->removeFromDisplay(rowId);

				// Get curent item on selected row
				QListWidgetItem* curItem = listWidgetModel->takeItem(rowId);
				// And remove it
				delete curItem;
			}
			if (listWidgetModel->count())
			{
				listWidgetModel->setCurrentRow(0);
				on_listWidgetModel_itemChanged(nullptr);
			}
			_glWidget->update();
			_bDeletionInProgress = false;
		}
	}
	QApplication::restoreOverrideCursor();
}

void ModelViewer::hideSelectedItems()
{
	QList<QListWidgetItem*> selectedItems = listWidgetModel->selectedItems();
	for (QListWidgetItem* item : selectedItems)
	{
		item->setCheckState(Qt::Unchecked);
		item->setSelected(false);
	}
}

void ModelViewer::showOnlySelectedItems()
{
	bool oldState = listWidgetModel->blockSignals(true);
	for (int i = 0; i < listWidgetModel->count(); i++)
	{
		QListWidgetItem* item = listWidgetModel->item(i);
		if (item->isSelected())
		{
			item->setCheckState(Qt::Checked);
		}
		else
		{
			item->setCheckState(Qt::Unchecked);
		}
	}
	listWidgetModel->blockSignals(oldState);
	on_listWidgetModel_itemChanged(nullptr);
}

void ModelViewer::showSelectedItems()
{
	QList<QListWidgetItem*> selectedItems = listWidgetModel->selectedItems();
	for (QListWidgetItem* item : selectedItems)
	{
		item->setCheckState(Qt::Checked);
	}
}

void ModelViewer::displaySelectedMeshInfo()
{
	QList<QListWidgetItem*> selectedItems = listWidgetModel->selectedItems();
	int rowId = listWidgetModel->row(selectedItems.at(0));
	std::vector<TriangleMesh*> meshes = _glWidget->getMeshStore();
	TriangleMesh* mesh = meshes.at(rowId);
	if (mesh)
	{
		QString name = QString("Mesh name: %1\n").arg(mesh->getName());
		QString points = QString("Points: %1\n").arg(mesh->getPoints().size() / 3);
		QString triangles = QString("Triangles: %1\n").arg(mesh->getIndices().size() / 2);
		unsigned long long rawmem = mesh->memorySize();
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
		QString meshSize = QString("Memory: %1 ").arg(mem) + units + "\n";
		QString meshProps;
		try
		{
			MeshProperties props(mesh);
			meshProps = QString("Mesh Volume: %1 \nSurface Area: %2\n").arg(props.volume()).arg(props.surfaceArea());
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception raised in ModelViewer::displaySelectedMeshInfo, Meshproperties" << ex.what() << std::endl;
		}
		QString info = name + points + triangles + meshSize + meshProps;
		QMessageBox::information(this, "Mesh Info", info);
	}
}

void ModelViewer::showVisualizationModelPage()
{
	if (radioButtonADSL->isChecked())
	{
		toolBox->setCurrentIndex(0);
	}
	if (radioButtonDLPBR->isChecked())
	{
		toolBox->setCurrentIndex(1);
	}
	if (radioButtonTXPBR->isChecked())
	{
		toolBox->setCurrentIndex(2);
	}
}

void ModelViewer::showPredefinedMaterialsPage()
{
	toolBox->setCurrentIndex(3);
}

void ModelViewer::showTransformationsPage()
{
	toolBox->setCurrentIndex(4);
}

void ModelViewer::showEnvironmentPage()
{
	toolBox->setCurrentIndex(5);
}

void ModelViewer::clickMultiViewButton()
{
	toolButtonMultiView->animateClick(0);
}

void ModelViewer::on_checkTexture_toggled(bool checked)
{
	_bHasTexture = checked;
	if (listWidgetModel->count())
	{
		std::vector<TriangleMesh*> meshes = _glWidget->getMeshStore();
		QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
		//for (QListWidgetItem* i : (items.isEmpty() ? listWidgetModel->findItems(QString("*"), Qt::MatchWrap | Qt::MatchWildcard) : items))
		if (!items.isEmpty())
		{
			for (QListWidgetItem* i : items)
			{
				int rowId = listWidgetModel->row(i);
				TriangleMesh* mesh = meshes.at(rowId);
				if (mesh)
				{
					mesh->enableTexture(_bHasTexture);
				}
			}
			_glWidget->updateView();
		}
	}
}

void ModelViewer::on_pushButtonTexture_clicked()
{
	QImage buf;
	QString filter = getSupportedImagesFilter();
	QString fileName = QFileDialog::getOpenFileName(
		this,
		"Choose an image for texture",
		_lastOpenedDir,
		filter);
	_lastOpenedDir = QFileInfo(fileName).path(); // store path for next time
	if (fileName != "")
	{
		if (!buf.load(fileName))
		{ // Load first image from file
			qWarning("Could not read image file, using single-color instead.");
			QImage dummy(128, 128, (QImage::Format)5);
			dummy.fill(1);
			buf = dummy;
		}

		if (listWidgetModel->count())
		{
			std::vector<int> ids;
			QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
			//for (QListWidgetItem* i : (items.isEmpty() ? listWidgetModel->findItems(QString("*"), Qt::MatchWrap | Qt::MatchWildcard) : items))
			if (!items.isEmpty())
			{
				for (QListWidgetItem* i : items)
				{
					int rowId = listWidgetModel->row(i);
					ids.push_back(rowId);
				}
				_glWidget->setTexture(ids, buf);
				_glWidget->updateView();
			}
		}
	}
}

void ModelViewer::on_pushButtonDefaultLights_clicked()
{
	_glWidget->setAmbientLight({ 0.0f, 0.0f, 0.0f, 1.0f });
	_glWidget->setDiffuseLight({ 1.0f, 1.0f, 1.0f, 1.0f });
	_glWidget->setSpecularLight({ 0.5f, 0.5f, 0.5f, 1.0f });

	sliderLightPosX->setValue(0);
	sliderLightPosY->setValue(0);
	sliderLightPosZ->setValue(0);

	_glWidget->updateView();
	updateControls();
}

void ModelViewer::on_pushButtonDefaultMatls_clicked()
{
	_opacity = 1.0;
	//_ambiMat = { 0.2109375f, 0.125f, 0.05078125f, _opacity };      // 54 32 13
	//_diffMat = { 0.7109375f, 0.62890625f, 0.55078125f, _opacity }; // 182 161 141
	//_specMat = { 0.37890625f, 0.390625f, 0.3359375f, _opacity };   // 97 100 86
	// 0.925f, 0.913f, 0.847f, 1.0f    
    setMaterialToSelectedItems(GLMaterial::DEFAULT_MAT());
	_glWidget->updateView();
	updateControls();
}

void ModelViewer::on_pushButtonApplyTransformations_clicked()
{
	QApplication::setOverrideCursor(Qt::WaitCursor);
	setTransformation();
	_glWidget->update();
	QApplication::restoreOverrideCursor();
}

void ModelViewer::on_pushButtonResetTransformations_clicked()
{
	QApplication::setOverrideCursor(Qt::WaitCursor);
	resetTransformation();
	_glWidget->update();
	QApplication::restoreOverrideCursor();
}

void ModelViewer::on_isometricView_triggered(bool /*checked*/)
{
	buttonGroupViews->setExclusive(false);
	for (auto b : buttonGroupViews->buttons())
	{
		b->setChecked(false);
	}
	buttonGroupViews->setExclusive(true);

	toolButtonTopView->setChecked(false);
	_glWidget->setViewMode(ViewMode::ISOMETRIC);
	_glWidget->updateView();

	toolButtonIsometricView->setDefaultAction(dynamic_cast<QAction*>(sender()));
}

void ModelViewer::on_dimetricView_triggered(bool /*checked*/)
{
	buttonGroupViews->setExclusive(false);
	for (auto b : buttonGroupViews->buttons())
	{
		b->setChecked(false);
	}
	buttonGroupViews->setExclusive(true);

	_glWidget->setViewMode(ViewMode::DIMETRIC);
	_glWidget->updateView();

	toolButtonIsometricView->setDefaultAction(dynamic_cast<QAction*>(sender()));
}

void ModelViewer::on_trimetricView_triggered(bool /*checked*/)
{
	buttonGroupViews->setExclusive(false);
	for (auto b : buttonGroupViews->buttons())
	{
		b->setChecked(false);
	}
	buttonGroupViews->setExclusive(true);

	_glWidget->setViewMode(ViewMode::TRIMETRIC);
	_glWidget->updateView();

	toolButtonIsometricView->setDefaultAction(dynamic_cast<QAction*>(sender()));
}

void ModelViewer::on_displayShaded_triggered(bool)
{
	checkBoxEnvMapping->setChecked(false);
	checkBoxShadowMapping->setChecked(false);
	checkBoxReflections->setChecked(false);
	checkBoxFloor->setChecked(false);
	_glWidget->setDisplayMode(DisplayMode::SHADED);
	_glWidget->updateView();
	displayShaded->setToolTip("Wireframe");
}

void ModelViewer::on_displayWireframe_triggered(bool)
{
	checkBoxEnvMapping->setChecked(false);
	checkBoxShadowMapping->setChecked(false);
	checkBoxReflections->setChecked(false);
	checkBoxFloor->setChecked(false);
	_glWidget->setDisplayMode(DisplayMode::WIREFRAME);
	_glWidget->updateView();
	displayShaded->setToolTip("Shaded");
}

void ModelViewer::on_displayWireShaded_triggered(bool)
{
	checkBoxEnvMapping->setChecked(false);
	checkBoxShadowMapping->setChecked(false);
	checkBoxReflections->setChecked(false);
	checkBoxFloor->setChecked(false);
	_glWidget->setDisplayMode(DisplayMode::WIRESHADED);
	_glWidget->updateView();
	displayShaded->setToolTip("Wire Shaded");
}

void ModelViewer::on_displayRealShaded_triggered(bool)
{
	checkBoxEnvMapping->setChecked(true);
	checkBoxShadowMapping->setChecked(true);
	checkBoxReflections->setChecked(true);
	checkBoxFloor->setChecked(true);
	_glWidget->setDisplayMode(DisplayMode::REALSHADED);
	_glWidget->updateView();
	displayShaded->setToolTip("Real Shaded");
}

void ModelViewer::on_toolButtonFitAll_clicked()
{
	_glWidget->fitAll();
	_glWidget->updateView();
}

void ModelViewer::on_toolButtonWindowZoom_clicked(bool checked)
{
	if (checked)
	{
		_glWidget->beginWindowZoom();
	}
}

void ModelViewer::on_toolButtonTopView_clicked()
{
	_glWidget->setMultiView(false);
	toolButtonMultiView->setChecked(false);
	_glWidget->setViewMode(ViewMode::TOP);
	_glWidget->updateView();
}

void ModelViewer::on_toolButtonBottomView_clicked()
{
	_glWidget->setMultiView(false);
	toolButtonMultiView->setChecked(false);
	_glWidget->setViewMode(ViewMode::BOTTOM);
	_glWidget->updateView();
}

void ModelViewer::on_toolButtonLeftView_clicked()
{
	_glWidget->setMultiView(false);
	toolButtonMultiView->setChecked(false);
	_glWidget->setViewMode(ViewMode::LEFT);
	_glWidget->updateView();
}

void ModelViewer::on_toolButtonRightView_clicked()
{
	_glWidget->setMultiView(false);
	toolButtonMultiView->setChecked(false);
	_glWidget->setViewMode(ViewMode::RIGHT);
	_glWidget->updateView();
}

void ModelViewer::on_toolButtonFrontView_clicked()
{
	_glWidget->setMultiView(false);
	toolButtonMultiView->setChecked(false);
	_glWidget->setViewMode(ViewMode::FRONT);
	_glWidget->updateView();
}

void ModelViewer::on_toolButtonBackView_clicked()
{
	_glWidget->setMultiView(false);
	toolButtonMultiView->setChecked(false);
	_glWidget->setViewMode(ViewMode::BACK);
	_glWidget->updateView();
}

void ModelViewer::on_toolButtonProjection_toggled(bool checked)
{
	_glWidget->setProjection(checked ? ViewProjection::PERSPECTIVE : ViewProjection::ORTHOGRAPHIC);
	toolButtonProjection->setToolTip(checked ? "Orthographic" : "Perspective");
}

void ModelViewer::on_toolButtonSectionView_toggled(bool checked)
{
	_glWidget->showClippingPlaneEditor(checked);
	tabWidget->setAutoHide(!checked);
}

void ModelViewer::on_toolButtonShowHideAxis_toggled(bool checked)
{
	_glWidget->showAxis(checked);
}

void ModelViewer::on_toolButtonMultiView_toggled(bool checked)
{
	_glWidget->setMultiView(checked);
	toolButtonIsometricView->animateClick(0);
	_glWidget->resizeView(glframe->width(), glframe->height());
	_glWidget->updateView();
}

void ModelViewer::on_pushButtonLightAmbient_clicked()
{
	QVector4D ambientLight = _glWidget->getAmbientLight();
	QColor c = QColorDialog::getColor(QColor::fromRgbF(ambientLight.x(), ambientLight.y(), ambientLight.z()), this, "Ambient Light Color");
	if (c.isValid())
	{
		_glWidget->setAmbientLight({ static_cast<float>(c.redF()),
									 static_cast<float>(c.greenF()),
									 static_cast<float>(c.blueF()),
									 static_cast<float>(c.alphaF()) });
		updateControls();
		_glWidget->updateView();
	}
}

void ModelViewer::on_pushButtonLightDiffuse_clicked()
{
	QVector4D diffuseLight = _glWidget->getDiffuseLight();
	QColor c = QColorDialog::getColor(QColor::fromRgbF(diffuseLight.x(), diffuseLight.y(), diffuseLight.z()), this, "Diffuse Light Color");
	if (c.isValid())
	{
		_glWidget->setDiffuseLight({ static_cast<float>(c.redF()),
									 static_cast<float>(c.greenF()),
									 static_cast<float>(c.blueF()),
									 static_cast<float>(c.alphaF()) });
		updateControls();
		_glWidget->updateView();
	}
}

void ModelViewer::on_pushButtonLightSpecular_clicked()
{
	QVector4D specularLight = _glWidget->getSpecularLight();
	QColor c = QColorDialog::getColor(QColor::fromRgbF(specularLight.x(), specularLight.y(), specularLight.z()), this, "Specular Light Color");
	if (c.isValid())
	{
		_glWidget->setSpecularLight({ static_cast<float>(c.redF()),
									  static_cast<float>(c.greenF()),
									  static_cast<float>(c.blueF()),
									  static_cast<float>(c.alphaF()) });
		updateControls();
		_glWidget->updateView();
	}
}

void ModelViewer::on_pushButtonMaterialAmbient_clicked()
{
	QColor c = QColorDialog::getColor(QColor::fromRgbF(_ambiMat.x(), _ambiMat.y(), _ambiMat.z()), this, "Ambient Material Color");
	if (c.isValid())
	{
		_ambiMat = {
			static_cast<float>(c.redF()),
			static_cast<float>(c.greenF()),
			static_cast<float>(c.blueF()),
			static_cast<float>(c.alphaF()) };

		GLMaterialProps mat = { _ambiMat,
								_diffMat,
								_specMat,
								{1.0f, 1.0f, 1.0f, 1.0f},
								_emmiMat,
								_shine,
								_opacity,
								_metallic,
								_PBRMetallic,
								_PBRRoughness,
								checkTexture->isChecked() };
		setMaterialProps(mat);

		updateControls();
		_glWidget->updateView();
	}
}

void ModelViewer::on_pushButtonMaterialDiffuse_clicked()
{
	QColor c = QColorDialog::getColor(QColor::fromRgbF(_diffMat.x(), _diffMat.y(), _diffMat.z()), this, "Diffuse Material Color");
	if (c.isValid())
	{
		_diffMat = {
			static_cast<float>(c.redF()),
			static_cast<float>(c.greenF()),
			static_cast<float>(c.blueF()),
			static_cast<float>(c.alphaF()) };

		GLMaterialProps mat = { _ambiMat,
								_diffMat,
								_specMat,
								{1.0f, 1.0f, 1.0f, 1.0f},
								_emmiMat,
								_shine,
								_opacity,
								_metallic,
								_PBRMetallic,
								_PBRRoughness,
								checkTexture->isChecked() };
		setMaterialProps(mat);

		updateControls();
		_glWidget->updateView();
	}
}

void ModelViewer::on_pushButtonMaterialSpecular_clicked()
{
	QColor c = QColorDialog::getColor(QColor::fromRgbF(_specMat.x(), _specMat.y(), _specMat.z()), this, "Specular Material Color");
	if (c.isValid())
	{
		_specMat = {
			static_cast<float>(c.redF()),
			static_cast<float>(c.greenF()),
			static_cast<float>(c.blueF()),
			static_cast<float>(c.alphaF()) };

		GLMaterialProps mat = { _ambiMat,
								_diffMat,
								_specMat,
								{1.0f, 1.0f, 1.0f, 1.0f},
								_emmiMat,
								_shine,
								_opacity,
								_metallic,
								_PBRMetallic,
								_PBRRoughness,
								checkTexture->isChecked() };
		setMaterialProps(mat);

		updateControls();
		_glWidget->updateView();
	}
}

void ModelViewer::on_pushButtonMaterialEmissive_clicked()
{
	QColor c = QColorDialog::getColor(QColor::fromRgbF(_emmiMat.x(), _emmiMat.y(), _emmiMat.z()), this, "Emissive Material Color");
	if (c.isValid())
	{
		_emmiMat = {
			static_cast<float>(c.redF()),
			static_cast<float>(c.greenF()),
			static_cast<float>(c.blueF()),
			static_cast<float>(c.alphaF()) };

		GLMaterialProps mat = { _ambiMat,
								_diffMat,
								_specMat,
								{1.0f, 1.0f, 1.0f, 1.0f},
								_emmiMat,
								_shine,
								_opacity,
								_metallic,
								_PBRMetallic,
								_PBRRoughness,
								checkTexture->isChecked() };
		setMaterialProps(mat);

		updateControls();
		_glWidget->updateView();
	}
}

void ModelViewer::on_sliderLightPosX_valueChanged(int)
{
	_glWidget->setLightPosition(QVector3D(static_cast<float>(sliderLightPosX->value()),
		static_cast<float>(sliderLightPosY->value()),
		static_cast<float>(sliderLightPosZ->value())));
	_glWidget->updateView();
}

void ModelViewer::on_sliderLightPosY_valueChanged(int)
{
	_glWidget->setLightPosition(QVector3D(static_cast<float>(sliderLightPosX->value()),
		static_cast<float>(sliderLightPosY->value()),
		static_cast<float>(sliderLightPosZ->value())));
	_glWidget->updateView();
}

void ModelViewer::on_sliderLightPosZ_valueChanged(int)
{
	_glWidget->setLightPosition(QVector3D(static_cast<float>(sliderLightPosX->value()),
		static_cast<float>(sliderLightPosY->value()),
		static_cast<float>(sliderLightPosZ->value())));
	_glWidget->updateView();
}

void ModelViewer::on_sliderTransparency_valueChanged(int value)
{
	_opacity = (float)value / 1000.0;
	_ambiMat[3] = _opacity;
	_diffMat[3] = _opacity;
	_specMat[3] = _opacity;

	GLMaterialProps mat = { _ambiMat,
							_diffMat,
							_specMat,
							{1.0f, 1.0f, 1.0f, 1.0f},
							{0.0f, 0.0f, 0.0f, 1.0f},
							_shine,
							_opacity,
							_metallic,
							_PBRMetallic,
							_PBRRoughness,
							checkTexture->isChecked() };
	setMaterialProps(mat);
	_glWidget->updateView();
}

void ModelViewer::on_sliderShine_valueChanged(int value)
{
	_shine = value;

	GLMaterialProps mat = { _ambiMat,
							_diffMat,
							_specMat,
							{1.0f, 1.0f, 1.0f, 1.0f},
							{0.0f, 0.0f, 0.0f, 1.0f},
							_shine,
							_opacity,
							_metallic,
							_PBRMetallic,
							_PBRRoughness,
							checkTexture->isChecked() };
	setMaterialProps(mat);

	_glWidget->updateView();
}

void ModelViewer::setAlbedoFromADS(const bool metallic)
{
	QVector3D col;
	if (metallic)
		col = _ambiMat.toVector3D() + _diffMat.toVector3D();
	else
		col = _ambiMat.toVector3D() + _diffMat.toVector3D();
	_albedoColor.setX(clamp(col.x(), 0.0f, 1.0f));
	_albedoColor.setY(clamp(col.y(), 0.0f, 1.0f));
    _albedoColor.setZ(clamp(col.z(), 0.0f, 1.0f));
}

void ModelViewer::on_pushButtonBrass_clicked()
{
	if (listWidgetModel->selectedItems().isEmpty())
	{
		QMessageBox::information(this, "Material Selection", "Please select an object first");
		return;
	}
    setMaterialToSelectedItems(GLMaterial::BRASS());
}

void ModelViewer::on_pushButtonBronze_clicked()
{
	if (listWidgetModel->selectedItems().isEmpty())
	{
		QMessageBox::information(this, "Material Selection", "Please select an object first");
		return;
    }
    setMaterialToSelectedItems(GLMaterial::BRONZE());
}

void ModelViewer::on_pushButtonCopper_clicked()
{
	if (listWidgetModel->selectedItems().isEmpty())
	{
		QMessageBox::information(this, "Material Selection", "Please select an object first");
		return;
    }
    setMaterialToSelectedItems(GLMaterial::COPPER());
}

void ModelViewer::on_pushButtonGold_clicked()
{
	if (listWidgetModel->selectedItems().isEmpty())
	{
		QMessageBox::information(this, "Material Selection", "Please select an object first");
		return;
    }
    setMaterialToSelectedItems(GLMaterial::GOLD());
}

void ModelViewer::on_pushButtonSilver_clicked()
{
	if (listWidgetModel->selectedItems().isEmpty())
	{
		QMessageBox::information(this, "Material Selection", "Please select an object first");
		return;
    }
    setMaterialToSelectedItems(GLMaterial::SILVER());
}

void ModelViewer::on_pushButtonChrome_clicked()
{
	if (listWidgetModel->selectedItems().isEmpty())
	{
		QMessageBox::information(this, "Material Selection", "Please select an object first");
		return;
    }
    setMaterialToSelectedItems(GLMaterial::CHROME());
}

void ModelViewer::on_pushButtonRuby_clicked()
{
	if (listWidgetModel->selectedItems().isEmpty())
	{
		QMessageBox::information(this, "Material Selection", "Please select an object first");
		return;
    }
    setMaterialToSelectedItems(GLMaterial::RUBY());
}

void ModelViewer::on_pushButtonEmerald_clicked()
{
	if (listWidgetModel->selectedItems().isEmpty())
	{
		QMessageBox::information(this, "Material Selection", "Please select an object first");
		return;
    }
    setMaterialToSelectedItems(GLMaterial::EMERALD());
}

void ModelViewer::on_pushButtonTurquoise_clicked()
{
	if (listWidgetModel->selectedItems().isEmpty())
	{
		QMessageBox::information(this, "Material Selection", "Please select an object first");
		return;
    }
    setMaterialToSelectedItems(GLMaterial::TURQUOISE());
}

void ModelViewer::on_pushButtonJade_clicked()
{
	if (listWidgetModel->selectedItems().isEmpty())
	{
		QMessageBox::information(this, "Material Selection", "Please select an object first");
		return;
    }
    setMaterialToSelectedItems(GLMaterial::JADE());
}

void ModelViewer::on_pushButtonObsidian_clicked()
{
	if (listWidgetModel->selectedItems().isEmpty())
	{
		QMessageBox::information(this, "Material Selection", "Please select an object first");
		return;
    }
    setMaterialToSelectedItems(GLMaterial::OBSIDIAN());
}

void ModelViewer::on_pushButtonPearl_clicked()
{
	if (listWidgetModel->selectedItems().isEmpty())
	{
		QMessageBox::information(this, "Material Selection", "Please select an object first");
		return;
    }
    setMaterialToSelectedItems(GLMaterial::PEARL());
}

void ModelViewer::on_pushButtonBlackPlastic_clicked()
{
	if (listWidgetModel->selectedItems().isEmpty())
	{
		QMessageBox::information(this, "Material Selection", "Please select an object first");
		return;
    }
    setMaterialToSelectedItems(GLMaterial::BLACK_PLASTIC());
}

void ModelViewer::on_pushButtonCyanPlastic_clicked()
{
	if (listWidgetModel->selectedItems().isEmpty())
	{
		QMessageBox::information(this, "Material Selection", "Please select an object first");
		return;
    }
    setMaterialToSelectedItems(GLMaterial::CYAN_PLASTIC());
}

void ModelViewer::on_pushButtonGreenPlastic_clicked()
{
	if (listWidgetModel->selectedItems().isEmpty())
	{
		QMessageBox::information(this, "Material Selection", "Please select an object first");
		return;
    }
    setMaterialToSelectedItems(GLMaterial::GREEN_PLASTIC());
}

void ModelViewer::on_pushButtonRedPlastic_clicked()
{
	if (listWidgetModel->selectedItems().isEmpty())
	{
		QMessageBox::information(this, "Material Selection", "Please select an object first");
		return;
    }
    setMaterialToSelectedItems(GLMaterial::RED_PLASTIC());
}

void ModelViewer::on_pushButtonWhitePlastic_clicked()
{
	if (listWidgetModel->selectedItems().isEmpty())
	{
		QMessageBox::information(this, "Material Selection", "Please select an object first");
		return;
    }
    setMaterialToSelectedItems(GLMaterial::WHITE_PLASTIC());
}

void ModelViewer::on_pushButtonYellowPlastic_clicked()
{
	if (listWidgetModel->selectedItems().isEmpty())
	{
		QMessageBox::information(this, "Material Selection", "Please select an object first");
		return;
    }
    setMaterialToSelectedItems(GLMaterial::YELLOW_PLASTIC());
}

void ModelViewer::on_pushButtonBlackRubber_clicked()
{
	if (listWidgetModel->selectedItems().isEmpty())
	{
		QMessageBox::information(this, "Material Selection", "Please select an object first");
		return;
    }
    setMaterialToSelectedItems(GLMaterial::BLACK_RUBBER());
}

void ModelViewer::on_pushButtonCyanRubber_clicked()
{
	if (listWidgetModel->selectedItems().isEmpty())
	{
		QMessageBox::information(this, "Material Selection", "Please select an object first");
		return;
    }
    setMaterialToSelectedItems(GLMaterial::CYAN_RUBBER());
}

void ModelViewer::on_pushButtonGreenRubber_clicked()
{
	if (listWidgetModel->selectedItems().isEmpty())
	{
		QMessageBox::information(this, "Material Selection", "Please select an object first");
		return;
    }
    setMaterialToSelectedItems(GLMaterial::GREEN_RUBBER());
}

void ModelViewer::on_pushButtonRedRubber_clicked()
{
	if (listWidgetModel->selectedItems().isEmpty())
	{
		QMessageBox::information(this, "Material Selection", "Please select an object first");
		return;
    }
    setMaterialToSelectedItems(GLMaterial::RED_RUBBER());
}

void ModelViewer::on_pushButtonWhiteRubber_clicked()
{
	if (listWidgetModel->selectedItems().isEmpty())
	{
		QMessageBox::information(this, "Material Selection", "Please select an object first");
		return;
    }
    setMaterialToSelectedItems(GLMaterial::WHITE_RUBBER());
}

void ModelViewer::on_pushButtonYellowRubber_clicked()
{
	if (listWidgetModel->selectedItems().isEmpty())
	{
		QMessageBox::information(this, "Material Selection", "Please select an object first");
		return;
    }
    setMaterialToSelectedItems(GLMaterial::YELLOW_RUBBER());
}

void ModelViewer::on_listWidgetModel_itemChanged(QListWidgetItem*)
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
		_glWidget->setDisplayList(ids);
	}
}

void ModelViewer::on_listWidgetModel_itemSelectionChanged()
{
	if (!_bDeletionInProgress) // check to avoid unnecessary selection triggering in view
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
	}
}

void ModelViewer::on_listWidgetModel_itemDoubleClicked(QListWidgetItem* item)
{
	item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
}

void ModelViewer::on_toolButtonOpen_clicked()
{
	TriangleMesh* mesh = nullptr;

	QString supportedExtensions = "All Models(*.dae *.xml *.blend *.bvh *.3ds *.ase *.obj *.ply *.dxf *.ifc "
		"*.nff *.smd *.vta *.mdl *.md2 *.md3 *.pk3 *.mdc *.md5mesh *.md5anim "
		"*.md5camera *.x *.q3o *.q3s *.raw *.ac *.stl *.dxf *.irrmesh *.xml "
		"*.irr *.off. *.ter *.mdl *.hmp *.mesh.xml *.skeleton.xml *.material "
		"*.ms3d *.lwo *.lws *.lxo *.csm *.ply *.cob *.scn *.xgl *.zgl);;"
		"Collada ( *.dae;*.xml );;" "Blender ( *.blend );;" "Biovision BVH ( *.bvh );;"
		"3D Studio Max 3DS ( *.3ds );;" "3D Studio Max ASE ( *.ase );;" "Wavefront Object ( *.obj );;"
		"Stanford Polygon Library ( *.ply );;" "AutoCAD DXF ( *.dxf );;"
		"IFC-STEP, Industry Foundation Classes ( *.ifc );;" "Neutral File Format ( *.nff );;"
		"Sense8 WorldToolkit ( *.nff );;" "Valve Model ( *.smd,*.vta );;"
		"Quake I ( *.mdl );;" "Quake II ( *.md2 );;" "Quake III ( *.md3 );;" "Quake 3 BSP ( *.pk3 );;"
		"RtCW ( *.mdc );;" "Doom 3 ( *.md5mesh;*.md5anim;*.md5camera );;" "DirectX X ( *.x );;" "Quick3D ( *.q3o;q3s );;"
		"Raw Triangles ( .raw );;" "AC3D ( *.ac );;" "Stereolithography ( *.stl );;" "Autodesk DXF ( *.dxf );;"
		"Irrlicht Mesh ( *.irrmesh;*.xml );;" "Irrlicht Scene ( *.irr;*.xml );;" "Object File Format ( *.off );;"
		"Terragen Terrain ( *.ter );;" "3D GameStudio Model ( *.mdl );;" "3D GameStudio Terrain ( *.hmp );;"
		"Ogre (*.mesh.xml, *.skeleton.xml, *.material);;" "Milkshape 3D ( *.ms3d );;" "LightWave Model ( *.lwo );;"
		"LightWave Scene ( *.lws );;" "Modo Model ( *.lxo );;" "CharacterStudio Motion ( *.csm );;"
		"Stanford Ply ( *.ply );;" "TrueSpace ( *.cob, *.scn );;" "XGL ( *.xgl, *.zgl );;";

	QFileDialog fileDialog(this, tr("Open Model File"), _lastOpenedDir, supportedExtensions);
	fileDialog.setFileMode(QFileDialog::ExistingFile);
	fileDialog.selectNameFilter(_lastSelectedFilter);
	QString fileName;
	if (fileDialog.exec())
	{
		fileName = fileDialog.selectedFiles()[0];
		_lastSelectedFilter = fileDialog.selectedNameFilter();
	}

	if (fileName != "")
	{
		QApplication::setOverrideCursor(Qt::WaitCursor);
		_lastOpenedDir = QFileInfo(fileName).path(); // store path for next time
		QFileInfo fi(fileName);
		if (fi.suffix().toLower() == "stl")
		{
			mesh = _glWidget->loadSTLMesh(fileName);
			if (mesh)
			{
				if (!static_cast<STLMesh*>(mesh)->loaded())
				{
					delete mesh;
					mesh = nullptr;
				}
			}
		}
		else
		{
			mesh = _glWidget->loadAssImpMesh(fileName);
		}
		if (mesh)
		{
			updateDisplayList();

			listWidgetModel->setCurrentRow(listWidgetModel->count() - 1);
			listWidgetModel->currentItem()->setCheckState(Qt::Checked);

			updateDisplayList();
		}
		QApplication::restoreOverrideCursor();
		MainWindow::mainWindow()->activateWindow();
		QApplication::alert(MainWindow::mainWindow());
	}

	/*
	if (fileName != "")
	{
		QApplication::setOverrideCursor(Qt::WaitCursor);
		QFileInfo fi(fileName);
		if (fi.suffix().toLower() == "stl")
		{
			mesh = _glWidget->loadSTLMesh(fileName);
			if (mesh)
			{
				if (!static_cast<STLMesh*>(mesh)->loaded())
				{
					delete mesh;
					mesh = nullptr;
				}
			}
		}
		if (fi.suffix().toLower() == "obj")
			mesh = _glWidget->loadOBJMesh(fileName);
		_lastOpenedDir = QFileInfo(fileName).path(); // store path for next time

		if (mesh)
		{
			updateDisplayList();

			listWidgetModel->setCurrentRow(listWidgetModel->count() - 1);
			listWidgetModel->currentItem()->setCheckState(Qt::Checked);

			updateDisplayList();
		}
		else
		{
			QMessageBox::critical(this, "Error", "Model load unsuccessful!");
		}
		QApplication::restoreOverrideCursor();
		MainWindow::mainWindow()->activateWindow();
		QApplication::alert(MainWindow::mainWindow());
	}
	*/
}

void ModelViewer::setMaterialProps(const GLMaterialProps& mat)
{
	if (listWidgetModel->count())
	{
		std::vector<int> ids;
		QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
		//for (QListWidgetItem* i : (items.isEmpty() ? listWidgetModel->findItems(QString("*"), Qt::MatchWrap | Qt::MatchWildcard) : items))
		if (!items.isEmpty())
		{
			for (QListWidgetItem* i : items)
			{
				int rowId = listWidgetModel->row(i);
				ids.push_back(rowId);
			}
			_glWidget->setMaterialProps(ids, mat);
			_glWidget->setPBRAlbedoColor(ids, QColor(_albedoColor.x() * 255, _albedoColor.y() * 255, _albedoColor.z() * 255));
			_glWidget->updateView();
			updateControls();
		}
    }
}

void ModelViewer::setMaterialToSelectedItems(const GLMaterial &mat)
{
    if (listWidgetModel->count())
    {
        std::vector<int> ids;
        QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
        if (!items.isEmpty())
        {
            for (QListWidgetItem* i : items)
            {
                int rowId = listWidgetModel->row(i);
                ids.push_back(rowId);
            }
            _glWidget->setMaterialToObjects(ids, mat);
            _glWidget->updateView();
            _ambiMat = mat.ambient();
            _diffMat = mat.diffuse();
            _specMat = mat.specular();
            _shine = mat.shininess();
            _metallic = mat.metallic();
            _albedoColor = mat.albedoColor();
            _PBRMetallic = mat.metalness();
            _PBRRoughness = mat.roughness();
            updateControls();
        }
    }
}

void ModelViewer::on_toolButtonVertexNormal_clicked(bool checked)
{
	_glWidget->setShowVertexNormals(checked);
	_glWidget->update();
}

void ModelViewer::on_toolButtonFaceNormal_clicked(bool checked)
{
	_glWidget->setShowFaceNormals(checked);
	_glWidget->update();
}

void ModelViewer::on_checkBoxSelectAll_toggled(bool checked)
{
	if (listWidgetModel->count())
	{
		bool oldState = listWidgetModel->blockSignals(true);
		for (int i = 0; i < listWidgetModel->count(); i++)
		{
			QListWidgetItem* item = listWidgetModel->item(i);
			item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
		}
		listWidgetModel->blockSignals(oldState);
		on_listWidgetModel_itemChanged(nullptr);
	}
}

void ModelViewer::on_checkBoxShadowMapping_toggled(bool checked)
{
	_glWidget->showShadows(checked);
	_glWidget->update();
}

void ModelViewer::on_checkBoxEnvMapping_toggled(bool checked)
{
	_glWidget->showEnvironment(checked);
	_glWidget->update();
}

void ModelViewer::on_checkBoxSkyBox_toggled(bool checked)
{
	_glWidget->showSkyBox(checked);
	_glWidget->update();
}

void ModelViewer::on_checkBoxReflections_toggled(bool checked)
{
	_glWidget->showReflections(checked);
	_glWidget->update();
}

void ModelViewer::on_checkBoxFloor_toggled(bool checked)
{
	_glWidget->showFloor(checked);
	_glWidget->update();
}

void ModelViewer::on_checkBoxFloorTexture_toggled(bool checked)
{
	_glWidget->showFloorTexture(checked);
	_glWidget->update();
}

void ModelViewer::on_pushButtonFloorTexture_clicked()
{
	QString appPath = QCoreApplication::applicationDirPath();
	QImage buf;
	QString filter = getSupportedImagesFilter();
	QString fileName = QFileDialog::getOpenFileName(
		this,
		"Choose an image for texture",
		appPath + "/textures/envmap/floor",
		filter);
	_lastOpenedDir = QFileInfo(fileName).path(); // store path for next time
	if (fileName != "")
	{
		if (!buf.load(fileName))
		{ // Load first image from file
			qWarning("Could not read image file, using single-color instead.");
			QImage dummy(128, 128, (QImage::Format)5);
			dummy.fill(1);
			buf = dummy;
		}
		_glWidget->setFloorTexture(buf);
		_glWidget->update();
	}
}

void ModelViewer::on_toolBox_currentChanged(int index)
{
	if (index == 3) // Transformations page
	{
		updateTransformationValues();
	}
}

void ModelViewer::on_toolButtonRotateView_clicked()
{
	_glWidget->setRotationActive(true);
}

void ModelViewer::on_toolButtonPanView_clicked()
{
	_glWidget->setPanningActive(true);
}

void ModelViewer::on_toolButtonZoomView_clicked()
{
	_glWidget->setZoomingActive(true);
}

void ModelViewer::on_pushButtonSkyBoxTex_clicked()
{
	QString appPath = QCoreApplication::applicationDirPath();
	QString dir = QFileDialog::getExistingDirectory(this, tr("Select Skybox Texture Folder"),
		appPath + "/textures/envmap/skyboxes",
		QFileDialog::ShowDirsOnly
		| QFileDialog::DontResolveSymlinks);
	if (dir != "")
	{
		_lastOpenedDir = dir;
		_glWidget->setSkyBoxTextureFolder(_lastOpenedDir);
	}
}

void ModelViewer::switchToRealisticRendering()
{
	if (toolButtonDisplayMode->defaultAction() != displayRealShaded)
	{
		QToolTip::showText(groupBoxVisModel->mapToGlobal(groupBoxVisModel->pos()), "Switching to Realistic Display Mode", this);
		displayRealShaded->trigger();
		toolButtonDisplayMode->setDefaultAction(displayRealShaded);
	}
}

void ModelViewer::lightingType_toggled(int, bool)
{
	if (radioButtonADSL->isChecked())
	{
		toolBox->setItemEnabled(0, true);
		toolBox->setItemEnabled(1, false);
		toolBox->setItemEnabled(2, false);
		toolBox->setCurrentIndex(0);
		_glWidget->setRenderingMode(RenderingMode::ADS_PHONG);
	}
	if (radioButtonDLPBR->isChecked())
	{
		toolBox->setItemEnabled(0, false);
		toolBox->setItemEnabled(1, true);
		toolBox->setItemEnabled(2, false);
		toolBox->setCurrentIndex(1);
		_glWidget->setRenderingMode(RenderingMode::PBR_DIRECT_LIGHTING);
		switchToRealisticRendering();
	}
	if (radioButtonTXPBR->isChecked())
	{
		toolBox->setItemEnabled(0, false);
		toolBox->setItemEnabled(1, false);
		toolBox->setItemEnabled(2, true);
		toolBox->setCurrentIndex(2);
		_glWidget->setRenderingMode(RenderingMode::PBR_TEXTURED_LIGHTING);
		switchToRealisticRendering();
	}
	updateControls();
	_glWidget->update();
}

void ModelViewer::on_pushButtonAlbedoColor_clicked()
{
	QApplication::setOverrideCursor(Qt::WaitCursor);
	if (listWidgetModel->count())
	{
		QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
		if (!items.isEmpty())
		{
			QColor c = QColorDialog::getColor(QColor::fromRgbF(_albedoColor.x(), _albedoColor.y(), _albedoColor.z()), this, "Albedo Color");
			if (c.isValid())
			{
				_albedoColor = { c.red() / 255.0f, c.green() / 255.0f, c.blue() / 255.0f };
				if (_PBRMetallic >= 0.5)
					_specMat = QVector4D(_albedoColor, _opacity);
				else
					_diffMat = QVector4D(_albedoColor, _opacity);

				std::vector<int> ids;

				for (QListWidgetItem* i : items)
				{
					int rowId = listWidgetModel->row(i);
					ids.push_back(rowId);
				}
				_glWidget->setPBRAlbedoColor(ids, c);
				_glWidget->updateView();
				updateControls();
			}
		}
		else
			QMessageBox::information(this, "Albedo Color", "Please select an object first");
	}
	QApplication::restoreOverrideCursor();
}

void ModelViewer::on_sliderMetallic_valueChanged(int value)
{
	_PBRMetallic = value / 1000.0f;
	if (listWidgetModel->count())
	{
		std::vector<int> ids;
		QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
		if (!items.isEmpty())
		{
			for (QListWidgetItem* i : items)
			{
				int rowId = listWidgetModel->row(i);
				ids.push_back(rowId);
			}
			_glWidget->setPBRMetallic(ids, _PBRMetallic);
			_glWidget->updateView();
		}
	}
}

void ModelViewer::on_sliderRoughness_valueChanged(int value)
{
	_PBRRoughness = value / 1000.0f;
	if (listWidgetModel->count())
	{
		std::vector<int> ids;
		QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
		if (!items.isEmpty())
		{
			for (QListWidgetItem* i : items)
			{
				int rowId = listWidgetModel->row(i);
				ids.push_back(rowId);
			}
			_glWidget->setPBRRoughness(ids, _PBRRoughness);
			_glWidget->updateView();
		}
	}
}

void ModelViewer::on_checkBoxAlbedoMap_toggled(bool checked)
{
	if (listWidgetModel->count())
	{
		_hasAlbedoTex = checked;
		std::vector<int> ids;
		QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
		if (!items.isEmpty())
		{
			for (QListWidgetItem* i : items)
			{
				int rowId = listWidgetModel->row(i);
				ids.push_back(rowId);
			}
			_glWidget->enableAlbedoTexture(ids, checked);
			_glWidget->updateView();
		}
		else
			QMessageBox::information(this, "Albedo Map", "Please select an object first");
	}
}

void ModelViewer::on_pushButtonAlbedoMap_clicked()
{
	if (listWidgetModel->count())
	{
		std::vector<int> ids;
		QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
		if (!items.isEmpty())
		{
			QString appPath = QCoreApplication::applicationDirPath();
			QString dirPath = appPath + "/textures/materials";
			QString filter = getSupportedImagesFilter();
			QString fileName = QFileDialog::getOpenFileName(
				this,
				"Choose an image for Albedo map texture",
				_textureDirOpenedFirstTime ? dirPath : _lastOpenedDir,
				filter);
			_lastOpenedDir = QFileInfo(fileName).path(); // store path for next time
			if (fileName != "")
			{
				_textureDirOpenedFirstTime = false;
				QPixmap img; img.load(fileName);
				if (!img.isNull())
				{
					QApplication::setOverrideCursor(Qt::WaitCursor);
					_PBRAlbedoTexture = fileName;
					labelAlbedoMap->setPixmap(img);
					for (QListWidgetItem* i : items)
					{
						int rowId = listWidgetModel->row(i);
						ids.push_back(rowId);
					}
                    _glWidget->enableAlbedoTexture(ids, _hasAlbedoTex);
					_glWidget->setAlbedoTexture(ids, fileName);
					_glWidget->updateView();
					QApplication::restoreOverrideCursor();
				}
			}
		}
		else
			QMessageBox::information(this, "Albedo Map", "Please select an object first");
	}
}

void ModelViewer::on_checkBoxMetallicMap_toggled(bool checked)
{
	if (listWidgetModel->count())
	{
		_hasMetallicTex = checked;
		std::vector<int> ids;
		QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
		if (!items.isEmpty())
		{
			for (QListWidgetItem* i : items)
			{
				int rowId = listWidgetModel->row(i);
				ids.push_back(rowId);
			}
			_glWidget->enableMetallicTexture(ids, checked);
			_glWidget->updateView();
		}
		else
			QMessageBox::information(this, "Metallic Map", "Please select an object first");
	}
}

void ModelViewer::on_pushButtonMetallicMap_clicked()
{
	if (listWidgetModel->count())
	{
		std::vector<int> ids;
		QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
		if (!items.isEmpty())
		{
			QString appPath = QCoreApplication::applicationDirPath();
			QString dirPath = appPath + "/textures/materials";
			QString filter = getSupportedImagesFilter();
			QString fileName = QFileDialog::getOpenFileName(
				this,
				"Choose an image for Metallic map texture",
				_textureDirOpenedFirstTime ? dirPath : _lastOpenedDir,
				filter);
			_lastOpenedDir = QFileInfo(fileName).path(); // store path for next time
			if (fileName != "")
			{
				_PBRMetallicTexture = fileName;
				_textureDirOpenedFirstTime = false;
				QPixmap img; img.load(fileName);
				if (!img.isNull())
				{
					QApplication::setOverrideCursor(Qt::WaitCursor);
					labelMetallicMap->setPixmap(img);
					for (QListWidgetItem* i : items)
					{
						int rowId = listWidgetModel->row(i);
						ids.push_back(rowId);
					}
                    _glWidget->enableMetallicTexture(ids, _hasMetallicTex);
					_glWidget->setMetallicTexture(ids, fileName);
					_glWidget->updateView();
					QApplication::restoreOverrideCursor();
				}
			}
		}
		else
			QMessageBox::information(this, "Metallic Map", "Please select an object first");
	}
}

void ModelViewer::on_checkBoxRoughnessMap_toggled(bool checked)
{
	if (listWidgetModel->count())
	{
		_hasRoughnessTex = checked;
		std::vector<int> ids;
		QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
		if (!items.isEmpty())
		{
			for (QListWidgetItem* i : items)
			{
				int rowId = listWidgetModel->row(i);
				ids.push_back(rowId);
			}
			_glWidget->enableRoughnessTexture(ids, checked);
			_glWidget->updateView();
		}
		else
			QMessageBox::information(this, "Roughness Map", "Please select an object first");
	}
}

void ModelViewer::on_pushButtonRoughnessMap_clicked()
{
	if (listWidgetModel->count())
	{
		std::vector<int> ids;
		QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
		if (!items.isEmpty())
		{
			QString appPath = QCoreApplication::applicationDirPath();
			QString dirPath = appPath + "/textures/materials";
			QString filter = getSupportedImagesFilter();
			QString fileName = QFileDialog::getOpenFileName(
				this,
				"Choose an image for Roughness map texture",
				_textureDirOpenedFirstTime ? dirPath : _lastOpenedDir,
				filter);
			_lastOpenedDir = QFileInfo(fileName).path(); // store path for next time
			if (fileName != "")
			{
				_PBRRoughnessTexture = fileName;
				_textureDirOpenedFirstTime = false;
				QPixmap img; img.load(fileName);
				if (!img.isNull())
				{
					QApplication::setOverrideCursor(Qt::WaitCursor);
					labelRoughnessMap->setPixmap(img);
					for (QListWidgetItem* i : items)
					{
						int rowId = listWidgetModel->row(i);
						ids.push_back(rowId);
					}
                    _glWidget->enableRoughnessTexture(ids, _hasRoughnessTex);
					_glWidget->setRoughnessTexture(ids, fileName);
					_glWidget->updateView();
					QApplication::restoreOverrideCursor();
				}
			}
		}
		else
			QMessageBox::information(this, "Roughness Map", "Please select an object first");
	}
}

void ModelViewer::on_checkBoxNormalMap_toggled(bool checked)
{
	if (listWidgetModel->count())
	{
		_hasNormalTex = checked;
		std::vector<int> ids;
		QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
		if (!items.isEmpty())
		{
			for (QListWidgetItem* i : items)
			{
				int rowId = listWidgetModel->row(i);
				ids.push_back(rowId);
			}
			_glWidget->enableNormalTexture(ids, checked);
			_glWidget->updateView();
		}
		else
			QMessageBox::information(this, "Normal Map", "Please select an object first");
	}
}

void ModelViewer::on_pushButtonNormalMap_clicked()
{
	if (listWidgetModel->count())
	{
		std::vector<int> ids;
		QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
		if (!items.isEmpty())
		{
			QString appPath = QCoreApplication::applicationDirPath();
			QString dirPath = appPath + "/textures/materials";
			QString filter = getSupportedImagesFilter();
			QString fileName = QFileDialog::getOpenFileName(
				this,
				"Choose an image for Normal map texture",
				_textureDirOpenedFirstTime ? dirPath : _lastOpenedDir,
				filter);
			_lastOpenedDir = QFileInfo(fileName).path(); // store path for next time
			if (fileName != "")
			{
				_PBRNormalTexture = fileName;
				_textureDirOpenedFirstTime = false;
				QPixmap img; img.load(fileName);
				if (!img.isNull())
				{
					QApplication::setOverrideCursor(Qt::WaitCursor);
					labelNormalMap->setPixmap(img);
					for (QListWidgetItem* i : items)
					{
						int rowId = listWidgetModel->row(i);
						ids.push_back(rowId);
					}
                    _glWidget->enableNormalTexture(ids, _hasNormalTex);
					_glWidget->setNormalTexture(ids, fileName);
					_glWidget->updateView();
				}
				QApplication::restoreOverrideCursor();
			}
		}
		else
			QMessageBox::information(this, "Normal Map", "Please select an object first");
	}
}

void ModelViewer::on_checkBoxAOMap_toggled(bool checked)
{
	if (listWidgetModel->count())
	{
		_hasAOTex = checked;
		std::vector<int> ids;
		QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
		if (!items.isEmpty())
		{
			for (QListWidgetItem* i : items)
			{
				int rowId = listWidgetModel->row(i);
				ids.push_back(rowId);
			}
			_glWidget->enableAOTexture(ids, checked);
			_glWidget->updateView();
		}
		else
			QMessageBox::information(this, "AO Map", "Please select an object first");
	}
}

void ModelViewer::on_pushButtonAOMap_clicked()
{
	if (listWidgetModel->count())
	{
		std::vector<int> ids;
		QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
		if (!items.isEmpty())
		{
			QString appPath = QCoreApplication::applicationDirPath();
			QString dirPath = appPath + "/textures/materials";
			QString filter = getSupportedImagesFilter();
			QString fileName = QFileDialog::getOpenFileName(
				this,
				"Choose an image for AO map texture",
				_textureDirOpenedFirstTime ? dirPath : _lastOpenedDir,
				filter);
			_lastOpenedDir = QFileInfo(fileName).path(); // store path for next time
			if (fileName != "")
			{
				_PBRAOTexture = fileName;
				_textureDirOpenedFirstTime = false;
				QPixmap img; img.load(fileName);
				if (!img.isNull())
				{
					QApplication::setOverrideCursor(Qt::WaitCursor);
					labelAOMap->setPixmap(img);
					for (QListWidgetItem* i : items)
					{
						int rowId = listWidgetModel->row(i);
						ids.push_back(rowId);
					}
                    _glWidget->enableAOTexture(ids, _hasAOTex);
					_glWidget->setAOTexture(ids, fileName);
					_glWidget->updateView();
					QApplication::restoreOverrideCursor();
				}
			}
		}
		else
			QMessageBox::information(this, "AO Map", "Please select an object first");
	}
}

void ModelViewer::on_checkBoxHeightMap_toggled(bool checked)
{
	if (listWidgetModel->count())
	{
		_hasHeightTex = checked;
		std::vector<int> ids;
		QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
		if (!items.isEmpty())
		{
			for (QListWidgetItem* i : items)
			{
				int rowId = listWidgetModel->row(i);
				ids.push_back(rowId);
			}
			_glWidget->enableHeightTexture(ids, checked);
			_glWidget->updateView();
		}
		else
			QMessageBox::information(this, "Height Map", "Please select an object first");
	}
}

void ModelViewer::on_pushButtonHeightMap_clicked()
{
	if (listWidgetModel->count())
	{
		std::vector<int> ids;
		QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
		if (!items.isEmpty())
		{
			QString appPath = QCoreApplication::applicationDirPath();
			QString dirPath = appPath + "/textures/materials";
			QString filter = getSupportedImagesFilter();
			QString fileName = QFileDialog::getOpenFileName(
				this,
				"Choose an image for Height map texture",
				_textureDirOpenedFirstTime ? dirPath : _lastOpenedDir,
				filter);
			_lastOpenedDir = QFileInfo(fileName).path(); // store path for next time
			if (fileName != "")
			{
				_PBRHeightTexture = fileName;
				_textureDirOpenedFirstTime = false;
				QPixmap img; img.load(fileName);
				if (!img.isNull())
				{
					QApplication::setOverrideCursor(Qt::WaitCursor);
					labelHeightMap->setPixmap(img);
					for (QListWidgetItem* i : items)
					{
						int rowId = listWidgetModel->row(i);
						ids.push_back(rowId);
					}
                    _glWidget->enableHeightTexture(ids, _hasHeightTex);
					_glWidget->setHeightTexture(ids, fileName);
					_glWidget->updateView();
					QApplication::restoreOverrideCursor();
				}
			}
		}
		else
			QMessageBox::information(this, "Height Map", "Please select an object first");
	}
}

void ModelViewer::on_doubleSpinBoxHeightScale_valueChanged(double val)
{
	if (listWidgetModel->count())
	{
		_heightScale = val;
		std::vector<int> ids;
		QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
		if (!items.isEmpty())
		{
			for (QListWidgetItem* i : items)
			{
				int rowId = listWidgetModel->row(i);
				ids.push_back(rowId);
			}
			_glWidget->setHeightScale(ids, static_cast<float>(val));
			_glWidget->updateView();
		}
		else
			QMessageBox::information(this, "Height Scale", "Please select an object first");
	}
}

void ModelViewer::on_pushButtonApplyPBRTexture_clicked()
{
	bool allOK = true;
	if (_PBRAlbedoTexture == "")
	{
		QMessageBox::critical(this, "PBR Texture Missing", "Albedo map texture not set");
		allOK = false;
	}
	if (_PBRMetallicTexture == "")
	{
		QMessageBox::critical(this, "PBR Texture Missing", "Metallic map texture not set");
		allOK = false;
	}
	if (_PBRRoughnessTexture == "")
	{
		QMessageBox::critical(this, "PBR Texture Missing", "Roughness map texture not set");
		allOK = false;
	}
	if (_hasNormalTex && _PBRNormalTexture == "")
	{
		QMessageBox::critical(this, "PBR Texture Missing", "Normal map texture not set");
		allOK = false;
	}
	if (_hasAOTex && _PBRAOTexture == "")
	{
		QMessageBox::critical(this, "PBR Texture Missing", "AO map texture not set");
		allOK = false;
	}
	if (_hasHeightTex && _PBRHeightTexture == "")
	{
		QMessageBox::critical(this, "PBR Texture Missing", "Height map texture not set");
		allOK = false;
	}
	if (allOK)
	{
		if (listWidgetModel->count())
		{
			std::vector<int> ids;
			QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
			if (!items.isEmpty())
			{
				QApplication::setOverrideCursor(Qt::WaitCursor);
				for (QListWidgetItem* i : items)
				{
					int rowId = listWidgetModel->row(i);
					ids.push_back(rowId);
				}

				_glWidget->enableAlbedoTexture(ids, _hasAlbedoTex);
				if (_hasAlbedoTex)
				{
					_glWidget->setAlbedoTexture(ids, _PBRAlbedoTexture);
				}
				_glWidget->enableMetallicTexture(ids, _hasMetallicTex);
				if (_hasMetallicTex)
				{
					_glWidget->setMetallicTexture(ids, _PBRMetallicTexture);
				}
				_glWidget->enableRoughnessTexture(ids, _hasRoughnessTex);
				if (_hasRoughnessTex)
				{
					_glWidget->setRoughnessTexture(ids, _PBRRoughnessTexture);
				}
				_glWidget->enableNormalTexture(ids, _hasNormalTex);
				if (_hasNormalTex)
				{
					_glWidget->setNormalTexture(ids, _PBRNormalTexture);
				}
				_glWidget->enableAOTexture(ids, _hasAOTex);
				if (_hasAOTex)
				{
					_glWidget->setAOTexture(ids, _PBRAOTexture);
				}
				_glWidget->enableHeightTexture(ids, _hasHeightTex);
				if (_hasHeightTex)
				{
					_glWidget->setHeightTexture(ids, _PBRHeightTexture);
					_glWidget->setHeightScale(ids, static_cast<float>(_heightScale));
				}
				_glWidget->updateView();
				QApplication::restoreOverrideCursor();
			}
			else
				QMessageBox::information(this, "PBR Textures", "Please select an object first");
		}
	}
}

void ModelViewer::on_pushButtonClearPBRTextures_clicked()
{
	if (listWidgetModel->count())
	{
		std::vector<int> ids;
		QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
		if (!items.isEmpty())
		{
			QApplication::setOverrideCursor(Qt::WaitCursor);
			for (QListWidgetItem* i : items)
			{
				int rowId = listWidgetModel->row(i);
				ids.push_back(rowId);
			}
			_glWidget->clearPBRTextures(ids);
			_glWidget->updateView();
			QApplication::restoreOverrideCursor();
		}
		else
			QMessageBox::information(this, "Clear PBR Textures", "Please select an object first");
	}
}

void ModelViewer::on_toolButtonClearAlbedo_clicked()
{
	if (listWidgetModel->count())
	{
		std::vector<int> ids;
		QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
		if (!items.isEmpty())
		{
			QApplication::setOverrideCursor(Qt::WaitCursor);
			for (QListWidgetItem* i : items)
			{
				int rowId = listWidgetModel->row(i);
				ids.push_back(rowId);
			}
			_glWidget->clearAlbedoTexture(ids);
			_glWidget->updateView();
			QApplication::restoreOverrideCursor();
		}
		else
			QMessageBox::information(this, "Clear Albedo", "Please select an object first");
	}
}

void ModelViewer::on_toolButtonClearMetallic_clicked()
{
	if (listWidgetModel->count())
	{
		std::vector<int> ids;
		QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
		if (!items.isEmpty())
		{
			QApplication::setOverrideCursor(Qt::WaitCursor);
			for (QListWidgetItem* i : items)
			{
				int rowId = listWidgetModel->row(i);
				ids.push_back(rowId);
			}
			_glWidget->clearMetallicTexture(ids);
			_glWidget->updateView();
			QApplication::restoreOverrideCursor();
		}
		else
			QMessageBox::information(this, "Clear Metallic", "Please select an object first");
	}
}

void ModelViewer::on_toolButtonClearRoughness_clicked()
{
	if (listWidgetModel->count())
	{
		std::vector<int> ids;
		QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
		if (!items.isEmpty())
		{
			QApplication::setOverrideCursor(Qt::WaitCursor);
			for (QListWidgetItem* i : items)
			{
				int rowId = listWidgetModel->row(i);
				ids.push_back(rowId);
			}
			_glWidget->clearRoughnessTexture(ids);
			_glWidget->updateView();
			QApplication::restoreOverrideCursor();
		}
		else
			QMessageBox::information(this, "Clear Roughness", "Please select an object first");
	}
}

void ModelViewer::on_toolButtonClearNormal_clicked()
{
	if (listWidgetModel->count())
	{
		std::vector<int> ids;
		QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
		if (!items.isEmpty())
		{
			QApplication::setOverrideCursor(Qt::WaitCursor);
			for (QListWidgetItem* i : items)
			{
				int rowId = listWidgetModel->row(i);
				ids.push_back(rowId);
			}
			_glWidget->clearNormalTexture(ids);
			_glWidget->updateView();
			QApplication::restoreOverrideCursor();
		}
		else
			QMessageBox::information(this, "Clear Normal", "Please select an object first");
	}
}

void ModelViewer::on_toolButtonClearAO_clicked()
{
	if (listWidgetModel->count())
	{
		std::vector<int> ids;
		QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
		if (!items.isEmpty())
		{
			QApplication::setOverrideCursor(Qt::WaitCursor);
			for (QListWidgetItem* i : items)
			{
				int rowId = listWidgetModel->row(i);
				ids.push_back(rowId);
			}
			_glWidget->clearAOTexture(ids);
			_glWidget->updateView();
			QApplication::restoreOverrideCursor();
		}
		else
			QMessageBox::information(this, "Clear AO", "Please select an object first");
	}
}

void ModelViewer::on_toolButtonClearHeight_clicked()
{
	if (listWidgetModel->count())
	{
		std::vector<int> ids;
		QList<QListWidgetItem*> items = listWidgetModel->selectedItems();
		if (!items.isEmpty())
		{
			QApplication::setOverrideCursor(Qt::WaitCursor);
			for (QListWidgetItem* i : items)
			{
				int rowId = listWidgetModel->row(i);
				ids.push_back(rowId);
			}
			_glWidget->clearHeightTexture(ids);
			_glWidget->updateView();
			QApplication::restoreOverrideCursor();
		}
		else
			QMessageBox::information(this, "Clear Height", "Please select an object first");
	}
}
