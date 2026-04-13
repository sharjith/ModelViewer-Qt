
#include "ClippingPlanesEditor.h"
#include "Cone.h"
#include "Cube.h"
#include "GLWidget.h"
#include "SelectionManager.h"
#include "LanguageManager.h"
#include "MainWindow.h"
#include "ModelViewer.h"
#include "SceneTreeWidget.h"
#include "ModelViewerApplication.h"
#include "PathUtils.h"
#include "Plane.h"
#include "Point.h"
#include "Sphere.h"
#include "stb_image.h"
#include "TextRenderer.h"
#include "Utils.h"
#include <algorithm>
#include <iostream>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QStyleFactory>
#include <QThread>


constexpr auto MAX_MODEL_SIZE_BYTES = 52428800; // bytes

GLWidget::GLWidget(QWidget* parent, const char* /*name*/) : QOpenGLWidget(parent),
_bgShader(nullptr),
_bgSplitShader(nullptr),
_fgShader(nullptr),
_axisShader(nullptr),
_vertexNormalShader(nullptr),
_faceNormalShader(nullptr),
_shadowMappingShader(nullptr),
_skyBoxShader(nullptr),
_irradianceShader(nullptr),
_prefilterShader(nullptr),
_brdfShader(nullptr),
_lightCubeShader(nullptr),
_clippingPlaneShader(nullptr),
_clippedMeshShader(nullptr),
_selectionShader(nullptr),
_debugShader(nullptr),
_textShader(nullptr),
_textRenderer(nullptr),
_axisTextRenderer(nullptr),
_clippingPlanesEditor(nullptr),
_clippingPlaneXY(nullptr),
_clippingPlaneYZ(nullptr),
_clippingPlaneZX(nullptr),
_floorPlane(nullptr),
_skyBox(nullptr),
_axisCone(nullptr),
_lightCube(nullptr),
_assimpModelLoader(nullptr)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);  // Enable mouseMoveEvent for hover highlighting

    _viewer = static_cast<ModelViewer*>(parent);


	// Setup the view toolbar
	_viewToolbar = new ViewToolbar(this);
	_viewToolbar->reposition(width(), height());

	connect(_viewToolbar, &ViewToolbar::zoomViewRequested, this, [this]() {
		setZoomingActive(true);
		});

	connect(_viewToolbar, &ViewToolbar::panViewRequested, this, [this]() {
		setPanningActive(true);
		});

	connect(_viewToolbar, &ViewToolbar::rotateViewRequested, this, [this]() {
		setRotationActive(true);
		});

	connect(_viewToolbar, &ViewToolbar::cameraModeSelected, this, [this](const QString& type) {
		if (type == "Orbit") setCameraMode(GLCamera::CameraMode::Orbit);
		else if (type == "Fly") setCameraMode(GLCamera::CameraMode::Fly);
		else if (type == "First Person") setCameraMode(GLCamera::CameraMode::FirstPerson);
		});

	 connect(_viewToolbar, &ViewToolbar::viewSelected, this, [this](const QString& view) {
        if (view == "Top") setViewMode(ViewMode::TOP);
        else if (view == "Front") setViewMode(ViewMode::FRONT);
        else if (view == "Left") setViewMode(ViewMode::LEFT);
        else if (view == "Bottom") setViewMode(ViewMode::BOTTOM);
        else if (view == "Rear") setViewMode(ViewMode::BACK);
        else if (view == "Right") setViewMode(ViewMode::RIGHT);
    });

	 connect(_viewToolbar, &ViewToolbar::axonometricSelected, this, [this](const QString& type) {
		 if (type == "Isometric") setViewMode(ViewMode::ISOMETRIC);
		 else if (type == "Dimetric") setViewMode(ViewMode::DIMETRIC);
		 else if (type == "Trimetric") setViewMode(ViewMode::TRIMETRIC);
		 });

	 connect(_viewToolbar, &ViewToolbar::displayModeSelected, this, [this](const QString& type) {
		 if (type == "Realistic") setDisplayMode(DisplayMode::REALSHADED);
		 else if (type == "Shaded") setDisplayMode(DisplayMode::SHADED);
		 else if (type == "Wireframe") setDisplayMode(DisplayMode::WIREFRAME);
		 else if (type == "WireShaded") setDisplayMode(DisplayMode::WIRESHADED);		 
		 });
	 connect(this, &GLWidget::displayModeChanged, _viewer, &ModelViewer::onDisplayModeChanged);

	 connect(_viewToolbar, &ViewToolbar::fitToViewRequested, this, &GLWidget::fitAll);
	 
	 connect(_viewToolbar, &ViewToolbar::windowZoomRequested, this, &GLWidget::beginWindowZoom);

	 connect(_viewToolbar, &ViewToolbar::projectionToggled, this, [this](bool ortho) {
		 setProjection(ortho ? ViewProjection::ORTHOGRAPHIC : ViewProjection::PERSPECTIVE);
		 update();
		 });

	 connect(_viewToolbar, &ViewToolbar::multiViewToggled, this, [this](bool enabled) {
		 setMultiView(enabled);
		 if (enabled)
			 setViewMode(ViewMode::ISOMETRIC);
		 fitAll();
		 update();
		 });

	 connect(_viewToolbar, &ViewToolbar::sectionViewToggled, this, [this](bool enabled) {
		 showClippingPlaneEditor(enabled);
		 });

	 connect(_viewToolbar, &ViewToolbar::swapVisibleToggled, this, [this](bool enabled) {
		 swapVisible(enabled);
		 });

	 connect(_viewToolbar, &ViewToolbar::axisDisplayToggled, this, [this](bool enabled) {
		 showAxis(enabled);
		 });

	 connect(this, &GLWidget::visibleSwapped, _viewToolbar, &ViewToolbar::setSwapVisibleChecked);

	loadBgColorSettings();

	_quadVAO = 0;

	_viewBoundingSphereDia = 200.0f;
	_viewRange = _viewBoundingSphereDia;
	_FOV = 45.0f;
	_currentViewRange = 1.0f;
	_viewMode = ViewMode::ISOMETRIC;
	_projection = ViewProjection::ORTHOGRAPHIC;
	_previousProjection = GLCamera::ProjectionType::ORTHOGRAPHIC;

	_autoFitViewOnUpdate = true;
	_selectionHighlighting = true;

	_primaryCamera = new GLCamera(width(), height(), _viewRange, _FOV);
	_primaryCamera->setView(GLCamera::ViewProjection::SE_ISOMETRIC_VIEW);

	_orthoViewsCamera = new GLCamera(width(), height(), _viewRange, _FOV);
	_orthoViewsCamera->setView(GLCamera::ViewProjection::SE_ISOMETRIC_VIEW);

	_currentRotation = QQuaternion::fromRotationMatrix(_primaryCamera->getViewMatrix().toGenericMatrix<3, 3>());
	_currentTranslation = _primaryCamera->getPosition();
	_currentViewRange = _viewRange;

	// Create SelectionManager (dependency injection of camera reference)
	_selectionManager = new SelectionManager(
		this,
		_primaryCamera,
		_meshStore,
		_displayedObjectsIds,
		_hiddenObjectsIds,
		_visibleSwapped,
		this);  // Parent for Qt memory management

	// Connect SelectionManager signals to GLWidget update
	connect(_selectionManager, &SelectionManager::hoverChanged,
			this, [this](int) { update(); });
	connect(_selectionManager, &SelectionManager::selectionChanged,
			this, [this](const QList<int>& selectedIds) {
				// Click select APPENDS to selection (multi-select by default)
				// Add the selected mesh(es) if not already there
				for (int id : selectedIds) {
					if (!_selectedIDs.contains(id)) {
						_selectedIDs.append(id);
					}
				}
				// Emit singleSelectionDone only for actual single clicks
				if (selectedIds.count() == 1) {
					emit singleSelectionDone(selectedIds.first());
				}
				update();
			});

	// Load hover highlight mode from saved settings
	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
	int modeIndex = settings.value("comboBoxHoverHighlightMode", static_cast<int>(HoverHighlightMode::Disabled)).toInt();
	if (modeIndex >= 0 && modeIndex <= 2) {
		_selectionManager->setHoverHighlightMode(static_cast<HoverHighlightMode>(modeIndex));
	}

	_slerpStep = 0.0f;
	_slerpFrac = 0.05f;

	_modelNum = 6;

	_ambientLight = { 0.0f, 0.0f, 0.0f, 1.0f };
	_diffuseLight = { 1.0f, 1.0f, 1.0f, 1.0f };
	_specularLight = { 0.5f, 0.5f, 0.5f, 1.0f };

	_lightPosition = { 25.0f, 25.0f, 50.0f };
	_lightOffsetX = 0.0f;
	_lightOffsetY = 0.0f;
	_lightOffsetZ = 0.0f;

	_displayMode = DisplayMode::SHADED;
	_renderingMode = RenderingMode::ADS_BLINN_PHONG;

	_multiViewActive = false;

	_showAxis = true;

	_windowZoomActive = false;

    _rubberBand = new QRubberBand(QRubberBand::Rectangle, this);
    _rubberBand->setStyle(QStyleFactory::create("Fusion"));


	_viewZooming = false;
	_viewPanning = false;
	_viewRotating = false;

	_modelName = "Model";

	_clipYZEnabled = false;
	_clipZXEnabled = false;
	_clipXYEnabled = false;

	_clipXFlipped = false;
	_clipYFlipped = false;
	_clipZFlipped = false;
	_clippingPlaneXY = nullptr;
	_clippingPlaneYZ = nullptr;
	_clippingPlaneZX = nullptr;

	_cappingEnabled = false;
	_cappingTexture = 0;

	_showVertexNormals = false;
	_showFaceNormals = false;

	_envMapEnabled = false;
	_shadowsEnabled = false;
	_selfShadowsEnabled = false;
	_reflectionsEnabled = false;
	_floorSize = 10.0f;
	_floorSizeFactor = 5.0f;
	_floorDisplayed = false;
	_floorTextureDisplayed = true;
	_floorTexRepeatS = _floorTexRepeatT = 1;
	_floorOffsetPercent = 0.0f;

	// Floor texture
	if (!_texBuffer.load(PathUtils::getDataDirectory() + "/" + "textures/envmap/floor/Grey-White-Checkered-Squares1800x1800.jpg"))
	{ // Load first image from file
		qWarning("GLWidget::loadFloor - Could not read image file, using single-color instead.");
		QImage dummy(128, 128, QImage::Format_ARGB32);
		dummy.fill(Qt::white);
		_floorTexImage = dummy;
	}
	else
	{
		_floorTexImage = convertToGLFormat(_texBuffer);
	}

	_skyBoxEnabled = false;
	_skyBoxBlurred = false;
	_skyBoxFOV = 45.0f;
	_skyBoxTextureHDRI = false;
	_gammaCorrection = false;
	_screenGamma = 2.2f;
	_hdrToneMapping = false;
	_envMapExposure = 1.0f;
	_iblExposure = 1.0f;
	_toneMappingMode = HDRToneMapMode::KhronosPbrNeutral;

	_lowResEnabled = false;	
	_showLights = false;
	_useDefaultLights = true;
	_usePunctualLights = true;
	_useIBL = true;

	_shadowWidth = 1024 * 4;
	_shadowHeight = 1024 * 4;

	_environmentMap = 0;
	_shadowMap = 0;
	_shadowMapFBO = 0;
	_irradianceMap = 0;
	_prefilterMap = 0;
	_brdfLUTTexture = 0;

	_selectionFBO = 0;
	_selectionRBO = 0;
	_selectionDBO = 0;

	_quadVBO = 0;

	_rubberBandRadius = 1.0f;
	_rubberBandZoomRatio = 0.5f;

	_scaleFrac = 1.0f;

	_clipXCoeff = 0.0f;
	_clipYCoeff = 0.0f;
	_clipZCoeff = 0.0f;

	_clipDX = 0.0f;
	_clipDY = 0.0f;
	_clipDZ = 0.0f;

	_xTran = 0.0f;
	_yTran = 0.0f;
	_zTran = 0.0f;

	_xRot = 0.0f;
	_yRot = 0.0f;
	_zRot = 0.0f;

	_xScale = 1.0f;
	_yScale = 1.0f;
	_zScale = 1.0f;

	_displayedObjectsMemSize = 0;
	_visibleSwapped = false;

	_keyboardNavTimer = new QTimer(this);
	connect(_keyboardNavTimer, &QTimer::timeout, this, &GLWidget::performKeyboardNav);
	_keyboardNavTimer->start(15);

	_animateViewTimer = new QTimer(this);
	_animateViewTimer->setTimerType(Qt::PreciseTimer);
	connect(_animateViewTimer, &QTimer::timeout, this, &GLWidget::animateViewChange);
	connect(this, &GLWidget::rotationsSet, this, &GLWidget::stopAnimations);

	_animateFitAllTimer = new QTimer(this);
	_animateFitAllTimer->setTimerType(Qt::PreciseTimer);
	connect(_animateFitAllTimer, &QTimer::timeout, this, &GLWidget::animateFitAll);
	connect(this, &GLWidget::zoomAndPanSet, this, &GLWidget::stopAnimations);

	_animateWindowZoomTimer = new QTimer(this);
	_animateWindowZoomTimer->setTimerType(Qt::PreciseTimer);
	connect(_animateWindowZoomTimer, &QTimer::timeout, this, &GLWidget::animateWindowZoom);
	connect(this, &GLWidget::zoomAndPanSet, this, &GLWidget::stopAnimations);

	_animateCenterScreenTimer = new QTimer(this);
	_animateCenterScreenTimer->setTimerType(Qt::PreciseTimer);
	connect(_animateCenterScreenTimer, &QTimer::timeout, this, &GLWidget::animateCenterScreen);
	connect(this, &GLWidget::zoomAndPanSet, this, &GLWidget::stopAnimations);

	_inertiaTimer = new QTimer(this);
	_inertiaTimer->setInterval(16); // ~60 FPS
	connect(_inertiaTimer, &QTimer::timeout, this, &GLWidget::onInertiaTimer);

	_editorLayout = new QVBoxLayout(this);
	_upperLayout = new QFormLayout();
	_upperLayout->setFormAlignment(Qt::AlignTop | Qt::AlignLeft);
	_upperLayout->setRowWrapPolicy(QFormLayout::DontWrapRows);
	_upperLayout->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
	_editorLayout->addItem(_upperLayout);

	_editorLayout->addStretch(height());

	_lowerLayout = new QFormLayout();
	_editorLayout->addItem(_lowerLayout);
	_lowerLayout->setFormAlignment(Qt::AlignBottom | Qt::AlignRight);
	_lowerLayout->setRowWrapPolicy(QFormLayout::DontWrapRows);
	_lowerLayout->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);

	int toolbarHeight = _viewToolbar->height();
	_lowerLayout->setContentsMargins(0, 0, 0, toolbarHeight);

	_clippingPlanesEditor = new ClippingPlanesEditor(this);
	_lowerLayout->addWidget(_clippingPlanesEditor);
	_clippingPlanesEditor->applyContrastTheme((_bgBotColor.lightnessF() < 0.5)
		? QColor(255, 255, 255)
		: QColor(0, 0, 0));
	_clippingPlanesEditor->hide();

	//_displayedObjectsIds.push_back(0);

	setContextMenuPolicy(Qt::CustomContextMenu);
	connect(this, &GLWidget::customContextMenuRequested, this, &GLWidget::showContextMenu);

	_selectRect = new QRubberBand(QRubberBand::Rectangle, this);

	retranslateUI();

	connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {		
		retranslateUI();  // if needed
		});
}

GLWidget::~GLWidget()
{
	_viewToolbar = nullptr;

	if (_textRenderer)
		delete _textRenderer;
	if (_axisTextRenderer)
		delete _axisTextRenderer;

	for (auto a : _meshStore)
	{
		delete a;
	}
	if (_primaryCamera)
		delete _primaryCamera;
	if (_orthoViewsCamera)
		delete _orthoViewsCamera;

	if (_globalScene)
	{
		SceneUtils::deleteScene(_globalScene);
		_globalScene = nullptr;
	}

	if (_assimpModelLoader)
		delete _assimpModelLoader;

	// ===== CRITICAL: Ensure context is current before GL calls =====
	if (context() && context()->isValid())
	{
		makeCurrent();

		cleanUpShaders();

		// Delete textures
		glDeleteTextures(1, &_environmentMap);
		glDeleteTextures(1, &_shadowMap);
		glDeleteTextures(1, &_irradianceMap);
		glDeleteTextures(1, &_prefilterMap);
		glDeleteTextures(1, &_brdfLUTTexture);
		glDeleteTextures(1, &_cappingTexture);

		// Delete framebuffers and renderbuffers
		if (_skyboxFBO != 0)
			glDeleteFramebuffers(1, &_skyboxFBO);
		if (_shadowMapFBO != 0)
			glDeleteFramebuffers(1, &_shadowMapFBO);
		if (_selectionFBO != 0)
			glDeleteFramebuffers(1, &_selectionFBO);

		glDeleteRenderbuffers(1, &_skyboxDepthBuffer);
		if (_selectionRBO != 0)
			glDeleteRenderbuffers(1, &_selectionRBO);
		if (_selectionDBO != 0)
			glDeleteRenderbuffers(1, &_selectionDBO);

		// Destroy Qt wrapper types
		_axisVBO.destroy();
		_axisVAO.destroy();
		_bgSplitVBO.destroy();
		_bgSplitVAO.destroy();
		_bgVAO.destroy();

		// Delete fullscreen quad
		if (_fsTriVAO != 0)
		{
			glDeleteBuffers(1, &_fsTriVBO);
			glDeleteVertexArrays(1, &_fsTriVAO);
			_fsTriVAO = 0;
			_fsTriVBO = 0;
		}

		// Delete quad mesh
		if (_quadVAO != 0)
		{
			glDeleteBuffers(1, &_quadVBO);
			glDeleteVertexArrays(1, &_quadVAO);
			_quadVAO = 0;
			_quadVBO = 0;
		}

		// Delete conversion cube
		if (_conversionCubeVAO != 0)
		{
			glDeleteBuffers(1, &_conversionCubeVBO);
			glDeleteVertexArrays(1, &_conversionCubeVAO);
			_conversionCubeVAO = 0;
			_conversionCubeVBO = 0;
		}

		// Delete scene objects
		if (_clippingPlaneXY)
			delete _clippingPlaneXY;
		if (_clippingPlaneYZ)
			delete _clippingPlaneYZ;
		if (_clippingPlaneZX)
			delete _clippingPlaneZX;
		if (_floorPlane)
			delete _floorPlane;
		if (_axisCone)
			delete _axisCone;
		if (_skyBox)
			delete _skyBox;
		if (_lightCube)
			delete _lightCube;

		if (glLights)
		{
			glLights->cleanup();
		}

		cleanupTransmissionBuffer();

		doneCurrent();  // Release context

		qInfo() << "GLWidget::~GLWidget - OpenGL resources cleaned up successfully.";
	}
	else
	{
		qWarning() << "GLWidget::~GLWidget - No valid OpenGL context for cleanup.";
	}
}

void GLWidget::retranslateUI()
{
	// Axis labels
	_labelAxisX = tr("X");
	_labelAxisY = tr("Y");
	_labelAxisZ = tr("Z");

	// View labels
	_labelTop = tr("Top");
	_labelFront = tr("Front");
	_labelLeft = tr("Left");
	_labelIsometric = tr("Isometric");
	_labelDimetric = tr("Dimetric");
	_labelTrimetric = tr("Trimetric");

	// Mesh count label
	_labelNumMeshes = tr("No of Meshes: %1");
}

void GLWidget::cleanUpShaders()
{	
}

void GLWidget::moveToRecycleBin(const QUuid& uuid, int originalIndex)
{
	// Find mesh in _meshStore
	TriangleMesh* mesh = nullptr;
	int index = -1;

	for (size_t i = 0; i < _meshStore.size(); ++i)
	{
		if (_meshStore[i]->uuid() == uuid)
		{
			mesh = _meshStore[i];
			index = static_cast<int>(i);
			break;
		}
	}

	if (!mesh)
	{
		qWarning() << "GLWidget::moveToRecycleBin - Mesh not found:" << uuid;
		return;
	}

	// Remove from _meshStore (but don't delete)
	_meshStore.erase(_meshStore.begin() + index);

	// Also remove from displayed/hidden lists
	auto it = std::find(_displayedObjectsIds.begin(),
		_displayedObjectsIds.end(), index);
	if (it != _displayedObjectsIds.end())
		_displayedObjectsIds.erase(it);

	it = std::find(_hiddenObjectsIds.begin(),
		_hiddenObjectsIds.end(), index);
	if (it != _hiddenObjectsIds.end())
		_hiddenObjectsIds.erase(it);

	// Adjust indices in displayed/hidden lists
	// All indices > removed index need to be decremented
	for (int& id : _displayedObjectsIds)
	{
		if (id > index)
			id--;
	}
	for (int& id : _hiddenObjectsIds)
	{
		if (id > index)
			id--;
	}

	// Add to recycle bin
	RecycleBinEntry entry;
	entry.mesh = mesh;
	entry.originalIndex = originalIndex;
	entry.deletedAt = QDateTime::currentDateTime();

	_recycleBin[uuid] = entry;

	qDebug() << "Moved mesh to recycle bin:" << mesh->getName()
		<< "uuid:" << uuid;
}

bool GLWidget::restoreFromRecycleBin(const QUuid& uuid)
{
	if (!_recycleBin.contains(uuid))
	{
		qWarning() << "GLWidget::restoreFromRecycleBin - Mesh not in bin:" << uuid;
		return false;
	}

	RecycleBinEntry entry = _recycleBin.take(uuid);
	TriangleMesh* mesh = entry.mesh;

	// Restore to end of _meshStore (simplest approach)
	// Could use entry.originalIndex for smarter restoration
	_meshStore.push_back(mesh);
	int newIndex = static_cast<int>(_meshStore.size() - 1);

	// Add to displayed objects
	_displayedObjectsIds.push_back(newIndex);

	qDebug() << "Restored mesh from recycle bin:" << mesh->getName()
		<< "uuid:" << uuid << "at index:" << newIndex;

	return true;
}

void GLWidget::permanentlyDeleteFromBin(const QUuid& uuid)
{
	if (!_recycleBin.contains(uuid))
		return;

	RecycleBinEntry entry = _recycleBin.take(uuid);
	TriangleMesh* mesh = entry.mesh;

	qDebug() << "Permanently deleting mesh:" << mesh->getName()
		<< "uuid:" << uuid;

	// Actually destroy the mesh
	delete mesh;
}

bool GLWidget::isInRecycleBin(const QUuid& uuid) const
{
	return _recycleBin.contains(uuid);
}

QVector<QUuid> GLWidget::getRecycleBinUuids() const
{
	return _recycleBin.keys().toVector();
}

TriangleMesh* GLWidget::getMeshByUuid(const QUuid& uuid) const
{
	// Check in _meshStore first
	for (TriangleMesh* mesh : _meshStore)
	{
		if (mesh->uuid() == uuid)
			return mesh;
	}

	// Check in recycle bin
	if (_recycleBin.contains(uuid))
		return _recycleBin[uuid].mesh;

	return nullptr;
}

TriangleMesh* GLWidget::getMeshByIndex(int index) const
{
	return getMeshByUuid(getUuidByIndex(index));
}

int GLWidget::getIndexByUuid(const QUuid& uuid) const
{
	for (size_t i = 0; i < _meshStore.size(); ++i)
	{
		if (_meshStore[i]->uuid() == uuid)
			return static_cast<int>(i);
	}

	return -1;  // Not found or in recycle bin
}

QUuid GLWidget::getUuidByIndex(int index) const
{
	if (index >= 0 && index < static_cast<int>(_meshStore.size()))
		return _meshStore[index]->uuid();

	return QUuid();  // Invalid
}

void GLWidget::initializeGL()
{
	initializeOpenGLFunctions();

	int maxSamples = 0;
	glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);	
	ModelViewerApplication::setSupportedMSAASamples(maxSamples);

	GLfloat maxAniso = 0.0f;
	glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
	ModelViewerApplication::setSupportedAnisotropicFilteringLevel(maxAniso);
	
	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
	// Set Anisotropic Filtering Level
	int anIsoVals[] = { 1, 2, 4, 8, 16, 32 };
	_anisotropicFilteringLevel = anIsoVals[settings.value("anisotropyComboBox", 4).toInt()];

	_userShowAxisOverride = settings.value("showCenterTrihedronCheckBox", true).toBool();
	_userShowCornerAxisOverride = settings.value("showCornerTrihedronCheckBox", true).toBool();
	_cornerAxisPosition = static_cast<CornerAxisPosition>(settings.value("comboBoxCornerTrihedronPosition", 1).toInt());	
		
	makeCurrent();

	createShaderPrograms();
	createFullscreenTriangle();

	qRegisterMetaType<AssImpMeshDataBatch>("AssImpMeshDataBatch");
	qRegisterMetaType<const aiScene*>("const aiScene*");
	qRegisterMetaType<std::vector<GPULight>>("std::vector<GPULight>");

	if (!_ktx2Loader.initializeOpenGL())
	{
		qWarning() << "GLWidget::initializeGL - Failed to initialize KTX2 loader";
	}
	_gpuCapabilities = KTX2Loader::detectGPUCapabilities();

	_assimpModelLoader = new AssImpModelLoader();
	_assimpModelLoader->setImageTextureUploader(
		[this](GLMaterial::Texture& texture, const QImage& image) -> unsigned int {
			TextureSamplerSettings samplers{ texture.wrapS, texture.wrapT, texture.minFilter, texture.magFilter };
			if (QThread::currentThread() != this->thread())
			{
				unsigned int result = 0;
				const QImage imageCopy = image;
				QMetaObject::invokeMethod(this, [this, &result, imageCopy, samplers]() {
					result = uploadDecodedTextureImage(imageCopy, samplers);
					}, Qt::BlockingQueuedConnection);
				return result;
			}
			return uploadDecodedTextureImage(image, samplers);
		});
	_assimpModelLoader->setKtx2TextureUploader(
		[this](const QString& path, const std::string& mapType, GLMaterial::Texture& texture) -> unsigned int {
			TextureSamplerSettings samplers{ texture.wrapS, texture.wrapT, texture.minFilter, texture.magFilter };
			if (QThread::currentThread() != this->thread())
			{
				unsigned int result = 0;
				const QString pathCopy = path;
				const std::string mapTypeCopy = mapType;
				QMetaObject::invokeMethod(this, [this, &result, pathCopy, mapTypeCopy, samplers]() {
					result = uploadKtx2TextureImage(pathCopy, mapTypeCopy, samplers);
					}, Qt::BlockingQueuedConnection);
				return result;
			}
			return uploadKtx2TextureImage(path, mapType, samplers);
		});
	_assimpModelLoader->setUVDecisionCallback(
		[this](int totalTriangles, UVMethod currentMethod) -> UVMethod {
			if (QThread::currentThread() != this->thread())
			{
				UVMethod result = currentMethod;
				QMetaObject::invokeMethod(this, [this, totalTriangles, currentMethod, &result]() {
					result = promptLargeModelUVDecision(totalTriangles, currentMethod);
					}, Qt::BlockingQueuedConnection);
				return result;
			}
			return promptLargeModelUVDecision(totalTriangles, currentMethod);
		});
	connect(_assimpModelLoader, &AssImpModelLoader::fileReadProcessed, this, &GLWidget::showFileReadingProgress);
	connect(_assimpModelLoader, &AssImpModelLoader::verticesProcessed, this, &GLWidget::showMeshLoadingProgress);
	connect(_assimpModelLoader, &AssImpModelLoader::nodeMeshProgressUpdated, this, &GLWidget::showNodeMeshLoadingProgress);
	connect(this, &GLWidget::loadingAssImpModelCancelled, _assimpModelLoader, &AssImpModelLoader::cancelLoading);

	glLights = std::make_unique<GLLights>();
	// Connect lights loading
	connect(_assimpModelLoader, &AssImpModelLoader::lightsLoaded,
		this, [this](const std::vector<GPULight>& lights) {
			_originalParsedLights.clear();
			_currentRepositionedLights.clear();
			_originalParsedLights = lights;				

			_fgShader->bind();
			if (!lights.empty())
			{
				_fgShader->setUniformValue("lightCount", (int)lights.size());
				_fgShader->setUniformValue("hasPunctualLights", true);
			}
			else
			{
				_fgShader->setUniformValue("lightCount", 1);
				_fgShader->setUniformValue("hasPunctualLights", false);
			}
		});

	const std::string path = PathUtils::getDataDirectory().toStdString() + "/";
	// Text rendering
	_textShader->bind();
	_textRenderer = new TextRenderer(_textShader.get(), width(), height());
	_textRenderer->Load(path + "fonts/arial.ttf", 20);
	_axisTextRenderer = new TextRenderer(_textShader.get(), width(), height());
	_axisTextRenderer->Load(path + "fonts/arialbd.ttf", 16);
	_textShader->release();

	createCappingPlanes();

	createLights();

	// Environment Mapping
	loadEnvMap();
	// IBL Map
	loadIrradianceMap();
	// Shadow mapping
	loadFloor();

	createWhiteTexture();
	initTransmissionBuffer();

	float size = 15;
	_axisCone = new Cone(_axisShader.get(), _viewRange / size / 15, _viewRange / size / 5, 8.0f, 1.0f);

	// Set lighting information
	_fgShader->bind();
	_fgShader->setUniformValue("lightSource.ambient", _ambientLight.toVector3D());
	_fgShader->setUniformValue("lightSource.diffuse", _diffuseLight.toVector3D());
	_fgShader->setUniformValue("lightSource.specular", _specularLight.toVector3D());
	_fgShader->setUniformValue("lightSource.position", _lightPosition + QVector3D(_lightOffsetX, _lightOffsetY, _lightOffsetZ));
	_fgShader->setUniformValue("lightModel.ambient", QVector3D(0.2f, 0.2f, 0.2f));
	_fgShader->setUniformValue("Line.Width", 0.75f);
	_fgShader->setUniformValue("Line.Color", QVector4D(0.05f, 0.0f, 0.05f, 1.0f));
	_fgShader->setUniformValue("texUnit", 0);
	_fgShader->setUniformValue("envMap", 1);
	_fgShader->setUniformValue("shadowMap", 2);
	_fgShader->setUniformValue("irradianceMap", 3);
	_fgShader->setUniformValue("prefilterMap", 4);
	_fgShader->setUniformValue("brdfLUT", 5);
	_fgShader->setUniformValue("transmissionSceneTexture", 7);
	_fgShader->setUniformValue("transmissionDepthTexture", 8);
	_fgShader->setUniformValue("shadowSamples", 27.0f);
	_fgShader->setUniformValue("displayMode", static_cast<int>(_displayMode));
	_fgShader->setUniformValue("renderingMode", static_cast<int>(_renderingMode));	
	_fgShader->setUniformValue("selectionHighlighting", _selectionHighlighting);

	QMatrix4x4 envMapRot;
	envMapRot.rotate(-90, 1, 0, 0);
	_fgShader->setUniformValue("envMapRotationMatrix", envMapRot.toGenericMatrix<3, 3>());

	_debugShader->bind();
	_debugShader->setUniformValue("depthMap", 0);

	_viewMatrix.setToIdentity();
	glEnable(GL_DEPTH_TEST);

	glClearColor(0.0f, 0.0f, 0.0f, 1.f);
}

void GLWidget::resizeGL(int width, int height)
{
	float w = (float)width;
	float h = (float)height;

	// Invalidate selection FBO buffers so they're recreated with new dimensions
	if (_selectionRBO != 0)
	{
		glDeleteRenderbuffers(1, &_selectionRBO);
		_selectionRBO = 0;
	}
	if (_selectionDBO != 0)
	{
		glDeleteRenderbuffers(1, &_selectionDBO);
		_selectionDBO = 0;
	}
	if (_selectionFBO != 0)
	{
		glDeleteFramebuffers(1, &_selectionFBO);
		_selectionFBO = 0;
	}

	glViewport(0, 0, w, h);
	_viewportMatrix = QMatrix4x4(w / 2, 0.0f, 0.0f, 0.0f,
		0.0f, h / 2, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		w / 2 + 0, h / 2 + 0, 0.0f, 1.0f);

	_projectionMatrix.setToIdentity();
	_primaryCamera->setScreenSize(w, h);
	_primaryCamera->setViewRange(_viewRange);
	if (_projection == ViewProjection::ORTHOGRAPHIC)
	{
		_primaryCamera->setProjectionType(GLCamera::ProjectionType::ORTHOGRAPHIC);		
	}
	else
	{
		_primaryCamera->setProjectionType(GLCamera::ProjectionType::PERSPECTIVE);		
	}
	_projectionMatrix = _primaryCamera->getProjectionMatrix();
	_viewMatrix = _primaryCamera->getViewMatrix();

	// Resize the text frame
	_textRenderer->setWidth(width);
	_textRenderer->setHeight(height);
	QMatrix4x4 projection;
	projection.ortho(QRect(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)));
	_textShader->bind();
	_textShader->setUniformValue("projection", projection);
	_textShader->release();

	resizeTransmissionBuffer(width, height);

	update();
}

void GLWidget::paintGL()
{
	QColor topColor = !_visibleSwapped ? _bgTopColor : QColor::fromRgbF(1.0f - _bgTopColor.redF(),
		1.0f - _bgTopColor.greenF(), 1.0f - _bgTopColor.blueF(),
		_bgTopColor.alphaF());
	QColor botColor = !_visibleSwapped ? _bgBotColor : QColor::fromRgbF(1.0f - _bgBotColor.redF(),
		1.0f - _bgBotColor.greenF(), 1.0f - _bgBotColor.blueF(),
		_bgBotColor.alphaF());
	try
	{
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

		gradientBackground(topColor.redF(), topColor.greenF(), topColor.blueF(), topColor.alphaF(),
			botColor.redF(), botColor.greenF(), botColor.blueF(), botColor.alphaF(), _gradientStyle);

		_fgShader->bind();
		glLights->bind(_fgShader->programId());
		_fgShader->setUniformValue("lightCount", glLights->getLightCount());

		_modelMatrix.setToIdentity();
		if (_multiViewActive)
		{
			renderMultiView(topColor, botColor);
		}
		else
		{
			renderSingleView(topColor, botColor);
		}

		// Text rendering
		if (_meshStore.size() != 0)
		{
			const std::vector<int>& objectIds = _visibleSwapped ? _hiddenObjectsIds : _displayedObjectsIds;
			if (objectIds.size() > 0)
				_textRenderer->RenderText(_labelNumMeshes.arg(objectIds.size()).toStdString(), 4, 4, 1, QVector3D(1.0f, 1.0f, 0.0f));
		}
	}
	catch (const std::exception& ex)
	{
		std::cout << "Exception raised in GLWidget::paintGL\n" << ex.what() << std::endl;
	}

	// For testing rendered shadow map
	/*_debugShader->bind();
	_debugShader->setUniformValue("near_plane", 1.0f);
	_debugShader->setUniformValue("far_plane", _viewRange);
	_debugShader->setUniformValue("screenSize", QVector2D(width(), height()));
	_debugShader->setUniformValue("transmissionColorTexture", 8);
	_debugShader->setUniformValue("transmissionDepthTexture", 9);	
	renderQuad();*/

	//_brdfShader->bind();
	//renderQuad();
}

void GLWidget::updateView()
{
	update();
}

void GLWidget::setTexture(const std::vector<int>& ids, const QImage& texImage)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->setTexureImage(texImage.convertToFormat(QImage::Format_RGBA8888).mirrored());
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception raised in GLWidget::setTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::setSkyBoxTextureFolder(QString folder)
{
	QApplication::setOverrideCursor(Qt::WaitCursor);

	// File pattern map: match flexible identifiers to cube map indices
	QMap<QString, int> faceMap = {
		{"right", 0}, {"posx", 0}, {"px", 0}, {"rt", 0},
		{"left", 1},  {"negx", 1}, {"nx", 1}, {"lt", 1},
		{"top", 2},   {"posy", 2}, {"py", 2}, {"up", 2},
		{"bottom", 3},{"negy", 3}, {"ny", 3}, {"dn", 3}, {"down", 3},
		{"front", 4}, {"posz", 4}, {"pz", 4}, {"ft", 4},
		{"back", 5},  {"negz", 5}, {"nz", 5}, {"bk", 5}
	};

	QStringList faceNames = { "right", "left", "top", "bottom", "front", "back" };
	QStringList supportedFormats = { "jpeg", "jpg", "png", "bmp", "psd", "tga", "gif", "hdr", "pic", "pnm" };

	QStringList files = QDir(folder).entryList(QDir::Files | QDir::Readable, QDir::Name);

	if (files.isEmpty())
	{
		QMessageBox::critical(this, tr("Error"), tr("No files found in selected folder."));
		QApplication::restoreOverrideCursor();
		return;
	}

	makeCurrent();
	glBindTexture(GL_TEXTURE_CUBE_MAP, _environmentMap);

	// Temp holders
	QString skyboxImages[6];
	bool loadedFaces[6] = { false };

	// Try to match each file name with a face using the map
	for (const QString& file : files)
	{
		QString name = QFileInfo(file).baseName().toLower();
		for (auto it = faceMap.constBegin(); it != faceMap.constEnd(); ++it)
		{
			if (name.contains(it.key()) && !loadedFaces[it.value()])
			{
				skyboxImages[it.value()] = folder + "/" + file;
				loadedFaces[it.value()] = true;
				break;
			}
		}
	}

	// Check if all faces were loaded
	bool allFacesLoaded = std::all_of(std::begin(loadedFaces), std::end(loadedFaces),
		[](bool b) { return b; });

	if (!allFacesLoaded)
	{
		// Fallback: try single HDR cubemap image
		QStringList hdrFiles = QDir(folder).entryList(QStringList() << "*.hdr", QDir::Files);
		if (!hdrFiles.isEmpty())
		{
			QString fallbackHDR = folder + "/" + hdrFiles.first();
			if (loadCubemapFromSingleHDR(fallbackHDR))
			{				
				loadIrradianceMap();
				update();
				QApplication::restoreOverrideCursor();
				return;
			}
			else
			{
				QMessageBox::critical(this, tr("Error"),
					tr("Failed to load fallback HDR cubemap from:\n") + fallbackHDR);
			}
		}
		else
		{
			QMessageBox::critical(this, tr("Error"),
				tr("No valid 6-face skybox images or fallback HDR file found in folder."));
		}

		QApplication::restoreOverrideCursor();
		return;
	}

	// Ensure all 6 faces are found
	for (int i = 0; i < 6; ++i)
	{
		if (!loadedFaces[i])
		{
			QString missingFace = faceNames[i];
			QString title = tr("Error");
			QString message = tr("Missing skybox face: %1\nExpected files should include identifiers like posx/negx or right/left, etc.")
				.arg(missingFace);
			QMessageBox::critical(this, title, message);

			QApplication::restoreOverrideCursor();
			return;
		}
	}

	// Load and upload each face
	for (int i = 0; i < 6; ++i)
	{
		int width, height, nrComponents;
		void* data = nullptr;
		std::string fileName = skyboxImages[i].toStdString();

		stbi_set_flip_vertically_on_load(false);
		if (_skyBoxTextureHDRI)
		{
			data = static_cast<float*>(stbi_loadf(fileName.c_str(), &width, &height, &nrComponents, 0));
			if (!data) goto failure;
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB32F,
				width, height, 0, GL_RGB, GL_FLOAT, data);
		}
		else
		{
			data = static_cast<unsigned char*>(stbi_load(fileName.c_str(), &width, &height, &nrComponents, 0));
			if (!data) goto failure;
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
				width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
		}
				
		stbi_image_free(data);
		continue;

	failure:
		{
			QMessageBox::critical(this, tr("Error"), tr("Failed to load skybox face:\n") + QString::fromStdString(fileName));
			QApplication::restoreOverrideCursor();
			return;
		}
	}

	// Generate mipmaps ONCE after all 6 faces are loaded
	glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

	// Setup sampler parameters
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

	loadIrradianceMap();
	update();
	QApplication::restoreOverrideCursor();
}

bool GLWidget::loadCubemapFromSingleHDR(const QString& filePath)
{
	int imgWidth, imgHeight, channels;
	stbi_set_flip_vertically_on_load(false);
	float* data = stbi_loadf(filePath.toStdString().c_str(), &imgWidth, &imgHeight, &channels, 0);
	if (!data)
	{
		qWarning() << "Failed to load HDR file:" << filePath;
		return false;
	}

	// Check for equirectangular first (2:1 aspect ratio)
	if (imgWidth == 2 * imgHeight)
	{		
		stbi_image_free(data); // Free, we'll reload in conversion function
		return convertEquirectangularToCubemap(filePath);
	}

	glBindTexture(GL_TEXTURE_CUBE_MAP, _environmentMap);

	int faceSize = 0;
	QPoint faceOffsets[6];
	bool validLayout = false;

	// --- Detect 6x1 strip ---
	if (imgWidth % 6 == 0 && imgHeight == imgWidth / 6)
	{
		faceSize = imgHeight;
		for (int i = 0; i < 6; ++i)
			faceOffsets[i] = QPoint(i * faceSize, 0);
		qDebug() << "Detected layout: 6x1 horizontal strip";
		validLayout = true;
	}

	// --- Detect 1x6 vertical strip ---
	else if (imgHeight % 6 == 0 && imgWidth == imgHeight / 6)
	{
		faceSize = imgWidth;
		for (int i = 0; i < 6; ++i)
			faceOffsets[i] = QPoint(0, i * faceSize);
		qDebug() << "Detected layout: 1x6 vertical strip";
		validLayout = true;
	}

	// --- Detect 3x2 grid ---
	else if (imgWidth % 3 == 0 && imgHeight % 2 == 0 && imgWidth / 3 == imgHeight / 2)
	{
		faceSize = imgWidth / 3;
		QPoint gridOffsets[6] = {
			{0, 0}, // +X
			{1, 0}, // -X
			{2, 0}, // +Y
			{0, 1}, // -Y
			{1, 1}, // +Z
			{2, 1}  // -Z
		};
		for (int i = 0; i < 6; ++i)
			faceOffsets[i] = QPoint(gridOffsets[i].x() * faceSize, gridOffsets[i].y() * faceSize);
		qDebug() << "Detected layout: 3x2 grid";
		validLayout = true;
	}

	// --- Detect 4x3 or 3x4 cross layout ---
	else if ((imgWidth % 4 == 0 && imgHeight % 3 == 0 && imgWidth / 4 == imgHeight / 3) ||
		(imgWidth % 3 == 0 && imgHeight % 4 == 0 && imgWidth / 3 == imgHeight / 4))
	{
		// Handle 4x3 cross layout
		if (imgWidth / 4 == imgHeight / 3)
		{
			faceSize = imgWidth / 4;
			QPoint crossOffsets[6] = {
				{2, 1}, // +X
				{0, 1}, // -X
				{1, 0}, // +Y
				{1, 2}, // -Y
				{1, 1}, // +Z
				{3, 1}  // -Z
			};
			for (int i = 0; i < 6; ++i)
				faceOffsets[i] = QPoint(crossOffsets[i].x() * faceSize, crossOffsets[i].y() * faceSize);
			qDebug() << "Detected layout: 4x3 cross";
			validLayout = true;
		}
		// Handle 3x4 cross layout (rotated cross)
		else if (imgWidth / 3 == imgHeight / 4)
		{
			faceSize = imgWidth / 3;
			QPoint crossOffsets[6] = {
				{2, 1}, // +X
				{0, 1}, // -X
				{1, 0}, // +Y
				{1, 2}, // -Y
				{1, 1}, // +Z
				{1, 3}  // -Z
			};
			for (int i = 0; i < 6; ++i)
				faceOffsets[i] = QPoint(crossOffsets[i].x() * faceSize, crossOffsets[i].y() * faceSize);
			qDebug() << "Detected layout: 3x4 cross";
			validLayout = true;
		}
	}

	// --- Fallback: 1 face only (not a cubemap) ---
	if (!validLayout)
	{
		qWarning() << "Unsupported cubemap layout. Cannot determine layout from image dimensions.";
		stbi_image_free(data);
		return false;
	}

	// --- Upload faces to OpenGL ---
	for (int i = 0; i < 6; ++i)
	{
		const QPoint& offset = faceOffsets[i];
		float* facePixels = new float[faceSize * faceSize * channels];

		for (int y = 0; y < faceSize; ++y)
		{
			const float* src = data + ((offset.y() + y) * imgWidth + offset.x()) * channels;
			float* dst = facePixels + y * faceSize * channels;
			memcpy(dst, src, sizeof(float) * faceSize * channels);
		}

		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB32F,
			faceSize, faceSize, 0, GL_RGB, GL_FLOAT, facePixels);
		delete[] facePixels;
	}

	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

	stbi_image_free(data);
	return true;
}

bool GLWidget::convertEquirectangularToCubemap(const QString& filePath)
{
	// 1. Load equirectangular HDR as 2D texture
	int imgWidth, imgHeight, channels;
	stbi_set_flip_vertically_on_load(true);
	float* data = stbi_loadf(filePath.toStdString().c_str(), &imgWidth, &imgHeight, &channels, 0);

	if (!data || imgWidth != 2 * imgHeight)
	{
		qWarning() << "Invalid equirectangular HDR file:" << filePath;
		if (data) stbi_image_free(data);
		return false;
	}

	// Validate and sanitize pixel data
	size_t totalPixels = imgWidth * imgHeight * channels;
	int invalidCount = 0;

	for (size_t i = 0; i < totalPixels; i++)
	{
		if (!std::isfinite(data[i]) || data[i] < 0.0f)
		{
			// Replace invalid values with nearby valid pixel or small positive value
			data[i] = (i > 0 && std::isfinite(data[i - 1])) ? data[i - 1] : 0.001f;
			invalidCount++;
		}
		// Clamp extremely bright values that cause issues
		else if (data[i] > 65504.0f)
		{ // Half-float max
			data[i] = 65504.0f;
			invalidCount++;
		}
	}

	if (invalidCount > 0)
	{
		qDebug() << "Fixed" << invalidCount << "invalid pixels in" << filePath;
	}

	// Create temporary 2D texture for equirectangular source
	GLuint equirectTexture;
	glGenTextures(1, &equirectTexture);
	glBindTexture(GL_TEXTURE_2D, equirectTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, imgWidth, imgHeight, 0, GL_RGB, GL_FLOAT, data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	stbi_image_free(data);

	// 2. Create cubemap texture
	int cubeSize = 1024; // Adjust resolution as needed
	for (int mip = 0; mip < static_cast<int>(std::log2(cubeSize)) + 1; ++mip)
	{
		int mipSize = cubeSize >> mip;
	for (int i = 0; i < 6; ++i)
	{
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, mip, GL_RGB32F,
				mipSize, mipSize, 0, GL_RGB, GL_FLOAT, nullptr);
	}
	}

	// 3. Setup framebuffer
	GLuint framebuffer, depthBuffer;
	glGenFramebuffers(1, &framebuffer);
	glGenRenderbuffers(1, &depthBuffer);

	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
	glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, cubeSize, cubeSize);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBuffer);
	
	// 4. Render each face
	_equirectToCubeShader->bind();
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, equirectTexture);
	_equirectToCubeShader->setUniformValue("equirectangularMap", 0);

	glViewport(0, 0, cubeSize, cubeSize);

	// View matrices for each cubemap face
	QMatrix4x4 captureViews[] = {
		// Use QMatrix4x4::lookAt for each face direction
		[]() { QMatrix4x4 m; m.lookAt(QVector3D(0.0f, 0.0f, 0.0f), QVector3D(1,  0,  0), QVector3D(0, -1,  0)); return m; }(), // +X
		[]() { QMatrix4x4 m; m.lookAt(QVector3D(0.0f, 0.0f, 0.0f), QVector3D(-1,  0,  0), QVector3D(0, -1,  0)); return m; }(), // -X
		[]() { QMatrix4x4 m; m.lookAt(QVector3D(0.0f, 0.0f, 0.0f), QVector3D(0,  1,  0), QVector3D(0,  0,  1)); return m; }(), // +Y
		[]() { QMatrix4x4 m; m.lookAt(QVector3D(0.0f, 0.0f, 0.0f), QVector3D(0, -1,  0), QVector3D(0,  0, -1)); return m; }(), // -Y
		[]() { QMatrix4x4 m; m.lookAt(QVector3D(0.0f, 0.0f, 0.0f), QVector3D(0,  0,  1), QVector3D(0, -1,  0)); return m; }(), // +Z
		[]() { QMatrix4x4 m; m.lookAt(QVector3D(0.0f, 0.0f, 0.0f), QVector3D(0,  0, -1), QVector3D(0, -1,  0)); return m; }()  // -Z
	};

	QMatrix4x4 captureProjection;
	captureProjection.perspective(90.0f, 1.0f, 0.1f, 10.0f);
	_equirectToCubeShader->setUniformValue("projection", captureProjection);

	for (int i = 0; i < 6; ++i)
	{
		_equirectToCubeShader->setUniformValue("view", captureViews[i]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, _environmentMap, 0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// Render the conversion cube
		renderConversionCube();		
	}

	// 5. Setup cubemap parameters
	glBindTexture(GL_TEXTURE_CUBE_MAP, _environmentMap);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

	// 6. Cleanup
	glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
	glDeleteFramebuffers(1, &framebuffer);
	glDeleteRenderbuffers(1, &depthBuffer);
	glDeleteTextures(1, &equirectTexture);

	return true;
}


bool GLWidget::convertEquirectangularToCubemapQuad(const QString& filePath)
{
	// 1. Load equirectangular HDR as 2D texture
	int imgWidth, imgHeight, channels;
	stbi_set_flip_vertically_on_load(true);
	float* data = stbi_loadf(filePath.toStdString().c_str(), &imgWidth, &imgHeight, &channels, 0);

	if (!data || imgWidth != 2 * imgHeight)
	{
		qWarning() << "Invalid equirectangular HDR file:" << filePath;
		if (data) stbi_image_free(data);
		return false;
	}

	// Create temporary 2D texture for equirectangular source
	GLuint equirectTexture;
	glGenTextures(1, &equirectTexture);
	glBindTexture(GL_TEXTURE_2D, equirectTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, imgWidth, imgHeight, 0, GL_RGB, GL_FLOAT, data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	stbi_image_free(data);

	/// 2. Create cubemap texture - allocate mip 0 for all faces first
	int cubeSize = 1024;  // Base resolution - adjust if needed
	glBindTexture(GL_TEXTURE_CUBE_MAP, _environmentMap);

	for (int i = 0; i < 6; ++i)
	{
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB32F,
			cubeSize, cubeSize, 0, GL_RGB, GL_FLOAT, nullptr);
	}

	qDebug() << "Converting equirectangular to cubemap" << cubeSize << "x" << cubeSize;

	// 3. Create simple full-screen quad
	float quadVertices[] = {
		-1.0f, -1.0f,
		 1.0f, -1.0f,
		 1.0f,  1.0f,
		-1.0f,  1.0f
	};

	unsigned int quadIndices[] = {
		0, 1, 2,
		0, 2, 3
	};

	GLuint quadVAO, quadVBO, quadEBO;
	glGenVertexArrays(1, &quadVAO);
	glGenBuffers(1, &quadVBO);
	glGenBuffers(1, &quadEBO);

	glBindVertexArray(quadVAO);
	glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quadEBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIndices), quadIndices, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	// 4. Setup framebuffer
	GLuint framebuffer, depthBuffer;
	glGenFramebuffers(1, &framebuffer);
	glGenRenderbuffers(1, &depthBuffer);

	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
	glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, cubeSize, cubeSize);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBuffer);

	_equirectToCubeQuadShader->bind();
	glViewport(0, 0, cubeSize, cubeSize);

	// Bind equirectangular texture
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, equirectTexture);
	_equirectToCubeQuadShader->setUniformValue("equirectangularMap", 0);

	// 6. Render each cubemap face
	for (int i = 0; i < 6; ++i)
	{
		_equirectToCubeQuadShader->setUniformValue("faceIndex", i);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, _environmentMap, 0);

		GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE)
		{
			qWarning() << "Framebuffer incomplete for face" << i;
			continue;
		}

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glBindVertexArray(quadVAO);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
	}

	// 7. Setup cubemap parameters and generate mipmaps
	glBindTexture(GL_TEXTURE_CUBE_MAP, _environmentMap);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

	// 8. Cleanup
	glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
	glDeleteFramebuffers(1, &framebuffer);
	glDeleteRenderbuffers(1, &depthBuffer);
	glDeleteTextures(1, &equirectTexture);
	glDeleteBuffers(1, &quadVBO);
	glDeleteBuffers(1, &quadEBO);
	glDeleteVertexArrays(1, &quadVAO);

	qDebug() << "Equirectangular to cubemap conversion complete";
	return true;
}
void GLWidget::renderConversionCube()
{
	if (_conversionCubeVAO == 0)
	{
		float vertices[] = {
			// positions          
			-1.0f,  1.0f, -1.0f,
			-1.0f, -1.0f, -1.0f,
			 1.0f, -1.0f, -1.0f,
			 1.0f, -1.0f, -1.0f,
			 1.0f,  1.0f, -1.0f,
			-1.0f,  1.0f, -1.0f,

			-1.0f, -1.0f,  1.0f,
			-1.0f, -1.0f, -1.0f,
			-1.0f,  1.0f, -1.0f,
			-1.0f,  1.0f, -1.0f,
			-1.0f,  1.0f,  1.0f,
			-1.0f, -1.0f,  1.0f,

			 1.0f, -1.0f, -1.0f,
			 1.0f, -1.0f,  1.0f,
			 1.0f,  1.0f,  1.0f,
			 1.0f,  1.0f,  1.0f,
			 1.0f,  1.0f, -1.0f,
			 1.0f, -1.0f, -1.0f,

			-1.0f, -1.0f,  1.0f,
			-1.0f,  1.0f,  1.0f,
			 1.0f,  1.0f,  1.0f,
			 1.0f,  1.0f,  1.0f,
			 1.0f, -1.0f,  1.0f,
			-1.0f, -1.0f,  1.0f,

			-1.0f,  1.0f, -1.0f,
			 1.0f,  1.0f, -1.0f,
			 1.0f,  1.0f,  1.0f,
			 1.0f,  1.0f,  1.0f,
			-1.0f,  1.0f,  1.0f,
			-1.0f,  1.0f, -1.0f,

			-1.0f, -1.0f, -1.0f,
			-1.0f, -1.0f,  1.0f,
			 1.0f, -1.0f, -1.0f,
			 1.0f, -1.0f, -1.0f,
			-1.0f, -1.0f,  1.0f,
			 1.0f, -1.0f,  1.0f
		};

		glGenVertexArrays(1, &_conversionCubeVAO);
		glGenBuffers(1, &_conversionCubeVBO);

		glBindBuffer(GL_ARRAY_BUFFER, _conversionCubeVBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

		glBindVertexArray(_conversionCubeVAO);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);
	}

	glBindVertexArray(_conversionCubeVAO);
	glDrawArrays(GL_TRIANGLES, 0, 36);
	glBindVertexArray(0);
}

QVector3D GLWidget::getLightPosition() const
{
	return _lightPosition;
}

void GLWidget::setLightOffset(const QVector3D& offset)
{
	_lightOffsetX = offset.x();
	_lightOffsetY = offset.y();
	_lightOffsetZ = offset.z();
	_shadowMapNeedsInitialization = true;
}

QVector4D GLWidget::getSpecularLight() const
{
	return _specularLight;
}

void GLWidget::setSpecularLight(const QVector4D& specularLight)
{
	_specularLight = specularLight;
	_fgShader->bind();
	_fgShader->setUniformValue("lightSource.specular", _specularLight.toVector3D());
	_fgShader->release();
}

QVector4D GLWidget::getDiffuseLight() const
{
	return _diffuseLight;
}

void GLWidget::setDiffuseLight(const QVector4D& diffuseLight)
{
	_diffuseLight = diffuseLight;
	_fgShader->bind();
	_fgShader->setUniformValue("lightSource.diffuse", _diffuseLight.toVector3D());
	_fgShader->release();
}

QVector4D GLWidget::getAmbientLight() const
{
	return _ambientLight;
}

void GLWidget::setAmbientLight(const QVector4D& ambientLight)
{
	_ambientLight = ambientLight;
	_fgShader->bind();
	_fgShader->setUniformValue("lightSource.ambient", _ambientLight.toVector3D());
	_fgShader->release();
}

void GLWidget::setViewMode(ViewMode mode)
{
	if (!_animateViewTimer->isActive())
	{
		_keyboardNavTimer->stop();
		_animateViewTimer->start(5);
		_viewMode = mode;
		_slerpStep = 0.0f;
	}
}

void GLWidget::fitAll()
{
	_viewBoundingSphereDia = _boundingSphere.getRadius() * 2;

	if (!_animateFitAllTimer->isActive())
	{
	_keyboardNavTimer->stop();
	_animateFitAllTimer->start(5);
	_slerpStep = 0.0f;
}
}

void GLWidget::setAutoFitViewOnUpdate(bool update)
{
	_autoFitViewOnUpdate = update;
}

void GLWidget::setSelectionHighlighting(bool highlight)
{
	_selectionHighlighting = highlight;
	_fgShader->setUniformValue("selectionHighlighting", _selectionHighlighting);
	update();
}

void GLWidget::beginWindowZoom()
{
	_windowZoomActive = true;
	setCursor(QCursor(QPixmap(":/icons/res/window-zoom-cursor.png"), 12, 12));
}

void GLWidget::performWindowZoom()
{
	_windowZoomActive = false;

	QVector3D Z(0, 0, 0); // instead of 0 for x and y we need worldPosition.x() and worldPosition.y() ....
    Z = Z.project(_viewMatrix * _modelMatrix, _projectionMatrix, getViewportFromPoint(_rubberBand->geometry().center()));

    QRect clientRect = getClientRectFromPoint(_rubberBand->geometry().center());
	QPoint clientWinCen = clientRect.center();
	QVector3D o(clientWinCen.x(), height() - clientWinCen.y(), Z.z());
    QVector3D O = o.unproject(_viewMatrix * _modelMatrix, _projectionMatrix, getViewportFromPoint(_rubberBand->geometry().center()));

    QRect zoomRect = _rubberBand->geometry();
	if (zoomRect.width() == 0 || zoomRect.height() == 0)
	{
		emit windowZoomEnded();
		return;
	}
	QPoint zoomWinCen = zoomRect.center();
	QVector3D p(zoomWinCen.x(), height() - zoomWinCen.y(), Z.z());
    QVector3D P = p.unproject(_viewMatrix * _modelMatrix, _projectionMatrix, getViewportFromPoint(_rubberBand->geometry().center()));

	double widthRatio = static_cast<double>(clientRect.width() / zoomRect.width());
	double heightRatio = static_cast<double>(clientRect.height() / zoomRect.height());
	_rubberBandZoomRatio = (heightRatio < widthRatio) ? heightRatio : widthRatio;
	_rubberBandPan = P - O;

	if (!_animateWindowZoomTimer->isActive())
	{
		_keyboardNavTimer->stop();
		_animateWindowZoomTimer->start(5);
		_slerpStep = 0.0f;
	}
	emit windowZoomEnded();
}

void GLWidget::setProjection(ViewProjection proj)
{
	_projection = proj;	
	resizeGL(width(), height());
}

void GLWidget::setCameraMode(GLCamera::CameraMode mode)
{
	if (mode == GLCamera::CameraMode::Fly || mode == GLCamera::CameraMode::FirstPerson)
	{
		if (_primaryCamera->getProjectionType() != GLCamera::ProjectionType::PERSPECTIVE)
		{
			_previousProjection = GLCamera::ProjectionType::ORTHOGRAPHIC;			
			setProjection(ViewProjection::PERSPECTIVE);
		}
	}
	else if (mode == GLCamera::CameraMode::Orbit)
	{		
		setProjection(_previousProjection == GLCamera::ProjectionType::PERSPECTIVE ? ViewProjection::PERSPECTIVE : ViewProjection::ORTHOGRAPHIC);
	}

	_primaryCamera->setMode(mode);

	resizeGL(width(), height());
	update();
}

void GLWidget::setRotationActive(bool active)
{
	_viewPanning = false;
	_viewZooming = false;
	_viewRotating = active;
	setCursor(QCursor(QPixmap(":/icons/res/rotatecursor.png")));
	MainWindow::showStatusMessage(tr("Press Esc to deactivate rotation mode"));
}

void GLWidget::setPanningActive(bool active)
{
	_viewRotating = false;
	_viewZooming = false;
	_viewPanning = active;
	setCursor(QCursor(QPixmap(":/icons/res/pancursor.png")));
	MainWindow::showStatusMessage(tr("Press Esc to deactivate panning mode"));
}

void GLWidget::setZoomingActive(bool active)
{
	_viewPanning = false;
	_viewRotating = false;
	_viewZooming = active;
	setCursor(QCursor(QPixmap(":/icons/res/zoomcursor.png")));
	MainWindow::showStatusMessage(tr("Press Esc to deactivate zooming mode"));
}

void GLWidget::setDisplayList(const std::vector<int>& ids)
{
	_displayedObjectsIds = ids;

	std::vector<int> allObjectIDs;
	for (size_t i = 0; i < _meshStore.size(); i++)
	{
		allObjectIDs.push_back(static_cast<int>(i));
	}
	_hiddenObjectsIds.clear();
	std::set_difference(
		allObjectIDs.begin(), allObjectIDs.end(),
		_displayedObjectsIds.begin(), _displayedObjectsIds.end(),
		std::back_inserter(_hiddenObjectsIds)
	);

	if (_hiddenObjectsIds.size() == 0 && _visibleSwapped)
	{
		_visibleSwapped = false;
		emit visibleSwapped(_visibleSwapped);
	}

	_currentTranslation = _primaryCamera->getPosition();
	_boundingSphere.setCenter(0, 0, 0);

	// Recompute all visible-scene aggregates in one pass.
	recalculateVisibleSceneStats(true);

	// ===== CAPTURE BASELINE HERE - NOW BOUNDING SPHERE IS REAL =====
	// Only capture if lights are present and baseline not yet set
	if (!_originalParsedLights.empty() && _lightRepoBasis.baselineRadius <= 0.0f)
	{
		_lightRepoBasis.baselineCenter = glm::vec3(
			static_cast<float>(_boundingSphere.getCenter().x()),
			static_cast<float>(_boundingSphere.getCenter().y()),
			static_cast<float>(_boundingSphere.getCenter().z())
		);
		_lightRepoBasis.baselineRadius = _boundingSphere.getRadius();
		_lightRepoBasis.accumulatedRotation = glm::mat4(1.0f);

		qDebug() << "Light repositioning basis captured in setDisplayList:";
		qDebug() << "  Baseline center: (" << _lightRepoBasis.baselineCenter.x
			<< ", " << _lightRepoBasis.baselineCenter.y
			<< ", " << _lightRepoBasis.baselineCenter.z << ")";
		qDebug() << "  Baseline radius: " << _lightRepoBasis.baselineRadius;
	}
	// ================================================================

	// Now update lights based on baseline
	updatePunctualLights();

	triggerShadowRecomputation();
	updateFloorPlane();

	if (_autoFitViewOnUpdate)
		fitAll();

	update();

	emit displayListSet();
}

void GLWidget::recalculateVisibleSceneStats(bool updateMemorySize)
{
	_currentTranslation = _primaryCamera->getPosition();
	_boundingSphere.setCenter(0, 0, 0);
	_boundingSphere.setRadius(0.0f);
	_boundingBox.setLimits(-0.001, -0.001, -0.001, 0.001, 0.001, 0.001);
	_visibleLowestZ = -1.0f;
	_visibleHighestZ = 1.0f;

	const std::vector<int>& visibleIds = _visibleSwapped ? _hiddenObjectsIds : _displayedObjectsIds;
	if (updateMemorySize)
	{
		_displayedObjectsMemSize = 0;
	}

	if (visibleIds.empty())
	{
		_primaryCamera->setPosition(0, 0, 0);
		_currentTranslation = _primaryCamera->getPosition();
		_boundingSphere.setRadius(1.0f);
		return;
	}

	bool firstBox = true;
	float lowestZ = std::numeric_limits<float>::max();
	float highestZ = std::numeric_limits<float>::lowest();
	unsigned long long memSize = 0;

	for (int i : visibleIds)
	{
		try
		{
			TriangleMesh* mesh = _meshStore.at(i);
			if (updateMemorySize)
			{
				memSize += mesh->memorySize();
			}

			_boundingSphere.addSphere(mesh->getBoundingSphere());

			const BoundingBox meshBox = mesh->getBoundingBox();
			if (firstBox)
			{
				_boundingBox = meshBox;
				firstBox = false;
			}
			else
			{
				_boundingBox.addBox(meshBox);
			}

			lowestZ = std::min(lowestZ, static_cast<float>(meshBox.zMin()));
			highestZ = std::max(highestZ, static_cast<float>(meshBox.zMax()));
		}
		catch (const std::out_of_range& ex)
		{
			std::cout << ex.what() << std::endl;
		}
	}

	if (updateMemorySize)
	{
		_displayedObjectsMemSize = memSize;
	}

	if (!firstBox)
	{
		_visibleLowestZ = lowestZ;
		_visibleHighestZ = highestZ;
	}
}

void GLWidget::triggerShadowRecomputation()
{
	if (!_fgShader)
	{
		_shadowMapNeedsInitialization = true;
		return;
	}

	float boundingRadius = _boundingSphere.getRadius();
	_viewBoundingSphereDia = boundingRadius * 2;

	float lightDistance = calculateLightDistance();
	float shadowFactor = shadowMapper.calculateShadowFactor(boundingRadius, lightDistance);
	shadowFactor = std::clamp(shadowFactor, 1.0f, 8.0f);

	_shadowWidth = static_cast<int>(1024 * shadowFactor);
	_shadowHeight = static_cast<int>(1024 * shadowFactor);

	// Get SIZE-AWARE shadow parameters
	auto shadowParams = shadowMapper.getShadowQualityParamsSmooth(boundingRadius);
	float shadowSoftness = shadowMapper.calculateShadowSoftness(_viewBoundingSphereDia);
	float sizeScale = shadowMapper.calculateSizeQualityScale(boundingRadius);

	_fgShader->bind();

	// Existing uniforms
	_fgShader->setUniformValue("shadowSoftness", shadowSoftness);
	_fgShader->setUniformValue("shadowSamples", static_cast<float>(shadowParams.shadowSamples));

	// Size-aware uniforms
	_fgShader->setUniformValue("shadowMaxKernelSize", shadowParams.maxKernelSize);
	_fgShader->setUniformValue("shadowSoftnessScale", shadowParams.softnessScale);
	_fgShader->setUniformValue("shadowMaxSoftnessClamp", shadowParams.maxSoftnessClamp);
	_fgShader->setUniformValue("shadowBiasMin", shadowParams.biasMin);
	_fgShader->setUniformValue("shadowBiasMax", shadowParams.biasMax);
	_fgShader->setUniformValue("shadowTransitionRange", shadowParams.transitionRange);
	_fgShader->setUniformValue("shadowGammaCorrection", shadowParams.gammaCorrection);
	_fgShader->setUniformValue("shadowSizeScale", sizeScale);

	_fgShader->release();

	_shadowMapNeedsInitialization = true;
	makeCurrent();
	loadFloor();	
}

void GLWidget::setShadowQuality(AdaptiveShadowMapper::QualityLevel quality)
{
	shadowMapper.setQuality(quality);
	triggerShadowRecomputation();
	updateFloorPlane();
}

float GLWidget::calculateLightDistance()
{
	QVector3D lightPos = _lightPosition + QVector3D(_lightOffsetX, _lightOffsetY, _lightOffsetZ);
	QVector3D center = _boundingSphere.getCenter();
	return (lightPos - center).length();
}

QVector<QUuid> GLWidget::duplicateObjects(const std::vector<int>& ids)
{
	QVector<QUuid> duplicatedUuids;

	makeCurrent();

	for (int id : ids)
	{
		TriangleMesh* originalMesh = _meshStore.at(id);
		if (originalMesh)
		{
			// Clone the mesh
			TriangleMesh* newMesh = originalMesh->clone();
			if (newMesh)
			{
				// Generate unique name with suffix
				QString uniqueName = generateUniqueMeshName(originalMesh->getName());
				newMesh->setName(uniqueName);

				// Add to display
				addToDisplay(newMesh);

				// Store the UUID of the duplicated mesh
				duplicatedUuids.append(newMesh->uuid());

				qDebug() << "Duplicated mesh:" << originalMesh->getName()
					<< "->" << uniqueName
					<< "uuid:" << newMesh->uuid();
			}
		}
	}

	doneCurrent();

	return duplicatedUuids;
}

void GLWidget::updateBoundingSphere()
{
	recalculateVisibleSceneStats(false);
}

void GLWidget::updateBoundingBox()
{	
	recalculateVisibleSceneStats(false);
}

void GLWidget::updateFloorPlane()
{
	if (!_floorPlane || !_fgShader)
		return;

	// Use helper to update floor geometry
	float halfObjectSize = updateFloorGeometry();

	// Use helper to set main light position (now consistent with loadFloor)
	updateMainLightPosition(halfObjectSize);

	_floorPlane->setPlane(_fgShader.get(), _floorCenter, _floorSize * _floorSizeFactor, _floorSize * _floorSizeFactor, 1, 1, lowestModelZ() - (_floorSize * _floorOffsetPercent), _floorTexRepeatS, _floorTexRepeatT);

	// Use helper to apply common material/texture settings
	applyFloorPlaneMaterialSettings();

	// Create fallback light if no punctual lights available
	if (_originalParsedLights.empty())
	{
		glLights->createFallbackLight(glm::vec3(
			static_cast<float>(_lightPosition.x()),
			static_cast<float>(_lightPosition.y()),
			static_cast<float>(_lightPosition.z())
		));
	}

	updateClippingPlane();
}

void GLWidget::updateClippingPlane()
{
	float xside = _clipXFlipped || _clipXCoeff > 0 ? -1.0f : 1.0f;
	float yside = _clipYFlipped || _clipYCoeff > 0 ? 1.0f : -1.0f;
	float zside = _clipZFlipped || _clipZCoeff > 0 ? -1.0f : 1.0f;
	_clippingPlaneXY->setPlane(_clippingPlaneShader.get(), _floorCenter, _floorSize * 100.0f, _floorSize * 100.0f, 1, 1, -_clipZCoeff * zside, _floorSize, _floorSize);
	_clippingPlaneYZ->setPlane(_clippingPlaneShader.get(), _floorCenter, _floorSize * 100.0f, _floorSize * 100.0f, 1, 1, -_clipXCoeff * xside, _floorSize, _floorSize);
	_clippingPlaneZX->setPlane(_clippingPlaneShader.get(), _floorCenter, _floorSize * 100.0f, _floorSize * 100.0f, 1, 1, -_clipYCoeff * yside, _floorSize, _floorSize);
	_clippingPlanesEditor->setCoefficientLimits(-_boundingBox.getXSize()/2, _boundingBox.getXSize()/2, 
		-_boundingBox.getYSize() / 2, _boundingBox.getYSize() / 2,
		-_boundingBox.getZSize() / 2, _boundingBox.getZSize() / 2);
}

void GLWidget::showClippingPlaneEditor(bool show)
{
	show ? _clippingPlanesEditor->show() : _clippingPlanesEditor->hide();
}

QWidget* GLWidget::attachOverlayPanel(QWidget* contentWidget, const QRect& geometry,
                                      Qt::Alignment, const QString& objectName)
{
	if (!contentWidget)
		return nullptr;

	auto* wrapper = new QWidget(this);
	const QString wrapperName = objectName.isEmpty()
		? QStringLiteral("glOverlayPanel")
		: objectName;
	wrapper->setObjectName(wrapperName);
	wrapper->setAttribute(Qt::WA_StyledBackground, true);
	wrapper->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
	applyOverlayPanelStyle(wrapper, wrapperName);

	QVBoxLayout* layout = new QVBoxLayout(wrapper);
	layout->setContentsMargins(6, 6, 6, 6);
	layout->addWidget(contentWidget);

	wrapper->setGeometry(geometry);
	wrapper->show();
	contentWidget->show();
	wrapper->raise();
	if (wrapperName == QLatin1String("navigationOverlayPanel"))
		_navigationOverlayPanel = wrapper;
	return wrapper;
}

QWidget* GLWidget::takeOverlayPanel(QWidget* contentWidget)
{
	if (!contentWidget)
		return nullptr;

	QWidget* wrapper = contentWidget->parentWidget();
	if (!wrapper || wrapper == this)
		return nullptr;

	if (QLayout* layout = wrapper->layout())
		layout->removeWidget(contentWidget);
	contentWidget->setParent(nullptr);

	if (wrapper == _navigationOverlayPanel)
		_navigationOverlayPanel = nullptr;

	wrapper->deleteLater();
	return wrapper;
}

void GLWidget::applyOverlayPanelStyle(QWidget* wrapper, const QString& objectName)
{
	if (!wrapper)
		return;

	const QColor contrastColor = (_bgTopColor.lightnessF() < 0.5)
		? QColor(255, 255, 255)
		: QColor(0, 0, 0);
	const bool darkBackground = _bgTopColor.lightnessF() < 0.5;
	const QColor panelFieldColor = darkBackground
		? QColor(24, 24, 24, 210)
		: QColor(255, 255, 255, 215);
	const QColor panelFieldBorderColor = darkBackground
		? QColor(255, 255, 255, 85)
		: QColor(0, 0, 0, 65);
	const QColor treeBaseColor = darkBackground
		? QColor(255, 255, 255, 190)
		: QColor(32, 32, 32, 165);
	const QColor treeAlternateColor = darkBackground
		? QColor(245, 245, 245, 190)
		: QColor(52, 52, 52, 165);

	wrapper->setStyleSheet(QString(
		"QWidget#%1 {"
		"  background-color: rgba(255, 255, 255, 25%);"
		"  border: 1px solid rgba(255, 255, 255, 40);"
		"  border-radius: 6px;"
		"}"
		"QWidget#%1 QLineEdit {"
		"  background-color: rgba(%5, %6, %7, %8);"
		"  border: 1px solid rgba(%9, %10, %11, %12);"
		"  border-radius: 4px;"
		"  padding: 2px 6px;"
		"}"
		"QWidget#%1 QTreeWidget {"
		"  background-color: rgba(%13, %14, %15, %16);"
		"  alternate-background-color: rgba(%17, %18, %19, %20);"
		"}"
		"QWidget#%1 QLabel,"
		"QWidget#%1 QCheckBox,"
		"QWidget#%1 QRadioButton,"
		"QWidget#%1 QGroupBox,"
		"QWidget#%1 QPushButton,"
		"QWidget#%1 QToolButton,"
		"QWidget#%1 QLineEdit,"
		"QWidget#%1 QSpinBox,"
		"QWidget#%1 QDoubleSpinBox,"
		"QWidget#%1 QTreeWidget,"
		"QWidget#%1 QComboBox {"
		"  color: rgb(%2, %3, %4);"
		"}")
		.arg(objectName)
		.arg(contrastColor.red())
		.arg(contrastColor.green())
		.arg(contrastColor.blue())
		.arg(panelFieldColor.red())
		.arg(panelFieldColor.green())
		.arg(panelFieldColor.blue())
		.arg(panelFieldColor.alpha())
		.arg(panelFieldBorderColor.red())
		.arg(panelFieldBorderColor.green())
		.arg(panelFieldBorderColor.blue())
		.arg(panelFieldBorderColor.alpha())
		.arg(treeBaseColor.red())
		.arg(treeBaseColor.green())
		.arg(treeBaseColor.blue())
		.arg(treeBaseColor.alpha())
		.arg(treeAlternateColor.red())
		.arg(treeAlternateColor.green())
		.arg(treeAlternateColor.blue())
		.arg(treeAlternateColor.alpha()));
}

void GLWidget::refreshNavigationOverlayStyle()
{
	if (_navigationOverlayPanel)
		applyOverlayPanelStyle(_navigationOverlayPanel, QStringLiteral("navigationOverlayPanel"));
}

void GLWidget::setClippingPlaneHatchMode(ClippingPlaneHatchMode mode)
{
	_hatchMode = mode;
	update();
}

void GLWidget::setClippingPlaneHatchPattern(HatchPattern pattern)
{
	_hatchPattern = pattern;
	update();
}

void GLWidget::setHatchTiling(int tiling)
{
	_hatchTiling = tiling;
	update();
}

void GLWidget::setHatchLineThickness(float width)
{
	_hatchThickness = width;
	update();
}

void GLWidget::setHatchIntensity(float spacing)
{
	_hatchIntensity = spacing;
	update();
}

void GLWidget::setHatchLayers(int layers)
{
	_hatchLayers = layers;
	update();
}

void GLWidget::setHatchLineColor(const QColor& color)
{
	_hatchLineColor = QVector3D(color.redF(), color.greenF(), color.blueF());
}

void GLWidget::setHatchTexture(const QString& path)
{
	_hatchTexturePath = path;
	_cappingTexture = loadTextureFromFile(_hatchTexturePath.toStdString().c_str());
	glActiveTexture(GL_TEXTURE6);
	glBindTexture(GL_TEXTURE_2D, _cappingTexture);
	update();
}

void GLWidget::showAxis(bool show)
{
	_showAxis = show;
	_fgShader->bind();
	_fgShader->setUniformValue("showAxis", _showAxis);
	update();
}

void GLWidget::showShadows(bool show)
{
	_shadowsEnabled = show;
	_fgShader->bind();
	_fgShader->setUniformValue("shadowsEnabled", _shadowsEnabled);
	update();
}

void GLWidget::showSelfShadows(bool show)
{
	_selfShadowsEnabled = show;		
	_fgShader->bind();
	_fgShader->setUniformValue("selfShadowsEnabled", _selfShadowsEnabled);
	update();
}

void GLWidget::showEnvironment(bool show)
{
	_envMapEnabled = show;
	_fgShader->bind();
	_fgShader->setUniformValue("envMapEnabled", _envMapEnabled);
	update();
}

void GLWidget::showSkyBox(bool show)
{
	_skyBoxEnabled = show;
	_fgShader->bind();
	_fgShader->setUniformValue("skyBoxEnabled", _skyBoxEnabled);
	update();
}

void GLWidget::blurSkyBox(bool blur)
{
	_skyBoxBlurred = blur;
	_fgShader->bind();
	_fgShader->setUniformValue("skyBoxBlurred", _skyBoxBlurred);
	update();
}

void GLWidget::showReflections(bool show)
{
	_reflectionsEnabled = show;
	_fgShader->bind();
	_fgShader->setUniformValue("reflectionsEnabled", _reflectionsEnabled);
	update();
}

void GLWidget::showFloor(bool show)
{
	_floorDisplayed = show;
	update();
	emit floorShown(show);
}

void GLWidget::setFloorTexture(QImage img)
{
	_floorTexImage = convertToGLFormat(img);
	_floorPlane->setTexureImage(_floorTexImage);
	_floorPlane->markTexturesDirty();
}

void GLWidget::showFloorTexture(bool show)
{
	_floorTextureDisplayed = show;
	_floorPlane->enableTexture(_floorTextureDisplayed);
}

void GLWidget::addToDisplay(TriangleMesh* mesh)
{	
	if(mesh == nullptr)
	{
		qDebug() << "Error: Attempted to add a null mesh to display.";
		return;
	}
	_meshStore.push_back(mesh);
	_displayedObjectsIds.push_back(static_cast<int>(_meshStore.size() - 1));

	//if(_progressiveLoadingEnabled)
		//_viewer->updateDisplayList();	
}

void GLWidget::removeFromDisplay(int index)
{
	TriangleMesh* mesh = _meshStore[index];
	_meshStore.erase(_meshStore.begin() + index);
	delete mesh;
	if (_meshStore.size() == 0)
	{
		_displayedObjectsIds.clear();
		_hiddenObjectsIds.clear();
		if (_visibleSwapped)
		{
			_visibleSwapped = false;
			emit visibleSwapped(_visibleSwapped);
		}
	}
	// If display list is empty, clear punctual lights
	if (_displayedObjectsIds.empty())
	{
		_originalParsedLights.clear();
		_currentRepositionedLights.clear();
		_lightRepoBasis.baselineRadius = 0.0f;  // Reset baseline
		glLights->createFallbackLight(glm::vec3(
			static_cast<float>(_lightPosition.x()),
			static_cast<float>(_lightPosition.y()),
			static_cast<float>(_lightPosition.z())
		));		
	}
}

void GLWidget::centerScreen(std::vector<int> selectedIDs)
{
	_centerScreenObjectIDs.clear();
	_centerScreenObjectIDs = selectedIDs;
	_selectionBoundingSphere.setCenter(0, 0, 0);
	_selectionBoundingSphere.setRadius(0.0);
	if (_displayedObjectsMemSize > MAX_MODEL_SIZE_BYTES)
		_lowResEnabled = true;
	int count = 0;
	for (int id : _centerScreenObjectIDs)
	{
		TriangleMesh* mesh = _meshStore.at(id);
		if (mesh)
		{
			if (count == 0)
				_selectionBoundingSphere = mesh->getBoundingSphere();
			else
				_selectionBoundingSphere.addSphere(mesh->getBoundingSphere());
		}
		count++;
	}
	if (!_animateCenterScreenTimer->isActive())
	{
		_keyboardNavTimer->stop();
		_animateCenterScreenTimer->start(5);
		_slerpStep = 0.0f;
	}
}

void GLWidget::select(int id)
{
	try {
		_meshStore.at(id)->select();
	}
	catch (const std::exception& ex) {
		std::cout << "Exception raised in GLWidget::select\n" << ex.what() << std::endl;
	}
}

void GLWidget::deselect(int id)
{
	try {
		_meshStore.at(id)->deselect();
	}
	catch (const std::exception& ex) {
		std::cout << "Exception raised in GLWidget::select\n" << ex.what() << std::endl;
	}
}

bool GLWidget::loadAssImpModel(const QString& fileName, const UVMethod& uvMethod, QString& error, bool progressiveLoading)
{
	_progressiveLoadingEnabled = progressiveLoading;
	_cancelRequested = false;
	_loadCancelled = false;
	_pendingSceneUuids.clear();
	MainWindow::clearFileLoadCancel();
	bool success = false;

	makeCurrent();
	QString displayFileName = fileName;
	if (fileName.length() > 100)
	{
		// Extract just the filename from the full path
		QString fileOnly = fileName.section('/', -1);
		// Calculate how much of the path to truncate
		int remainingLength = 100 - fileOnly.length() - 3; // 3 for "..."
		QString truncatedPath = fileName.left(remainingLength).section('/', 0, -2);
		displayFileName = truncatedPath + "/.../" + fileOnly;
	}
	MainWindow::showStatusMessage(tr("Reading file: ") + displayFileName);
	MainWindow::showProgressBar();
	if (_assimpModelLoader)
	{
		AssImpModelLoader* loadingWorker = new AssImpModelLoader();
		loadingWorker->setUVDecisionCallback(
			[this](int totalTriangles, UVMethod currentMethod) -> UVMethod {
				if (QThread::currentThread() != this->thread())
				{
					UVMethod result = currentMethod;
					QMetaObject::invokeMethod(this, [this, totalTriangles, currentMethod, &result]() {
						result = promptLargeModelUVDecision(totalTriangles, currentMethod);
						}, Qt::BlockingQueuedConnection);
					return result;
				}
				return promptLargeModelUVDecision(totalTriangles, currentMethod);
			});

		QEventLoop waitLoop;
		QThread loadingThread;
		bool loadingCompleted = false;

		QMetaObject::Connection finishedConnection = connect(
			loadingWorker,
			&AssImpModelLoader::loadingFinished,
			this,
			[this, loadingWorker, &success, &error, &loadingCompleted, &waitLoop](bool successFlag, const aiScene* /*scene*/) {
				loadingCompleted = true;
				success = successFlag;
				error = loadingWorker->getErrorMessage();
				if (error == "Model loading cancelled by user.")
				{
					_loadCancelled = true;
				}
				waitLoop.quit();
			},
			Qt::QueuedConnection);

		QMetaObject::Connection cancelledConnection = connect(
			loadingWorker,
			&AssImpModelLoader::loadingCancelled,
			this,
			[this, &error]() {
				_loadCancelled = true;
				error = "Model loading cancelled by user.";
			},
			Qt::QueuedConnection);

		QMetaObject::Connection fileProgressConnection = connect(
			loadingWorker,
			&AssImpModelLoader::fileReadProcessed,
			this,
			&GLWidget::showFileReadingProgress,
			Qt::QueuedConnection);

		QMetaObject::Connection meshProgressConnection = connect(
			loadingWorker,
			&AssImpModelLoader::verticesProcessed,
			this,
			&GLWidget::showMeshLoadingProgress,
			Qt::QueuedConnection);

		QMetaObject::Connection nodeProgressConnection = connect(
			loadingWorker,
			&AssImpModelLoader::nodeMeshProgressUpdated,
			this,
			&GLWidget::showNodeMeshLoadingProgress,
			Qt::QueuedConnection);

		QMetaObject::Connection batchConnection = connect(
			loadingWorker,
			&AssImpModelLoader::meshBatchReady,
			this,
			&GLWidget::onMeshBatchReady,
			Qt::BlockingQueuedConnection);

		QMetaObject::Connection cancelRequestConnection = connect(
			this,
			&GLWidget::loadingAssImpModelCancelled,
			loadingWorker,
			&AssImpModelLoader::cancelLoading,
			Qt::QueuedConnection);

		QMetaObject::Connection lightsConnection = connect(
			loadingWorker,
			&AssImpModelLoader::lightsLoaded,
			this,
			[this](const std::vector<GPULight>& lights) {
				_originalParsedLights.clear();
				_currentRepositionedLights.clear();
				_originalParsedLights = lights;

				makeCurrent();
				_fgShader->bind();
				if (!lights.empty())
				{
					_fgShader->setUniformValue("lightCount", (int)lights.size());
					_fgShader->setUniformValue("hasPunctualLights", true);
				}
				else
				{
					_fgShader->setUniformValue("lightCount", 1);
					_fgShader->setUniformValue("hasPunctualLights", false);
				}
			},
			Qt::QueuedConnection);

		loadingWorker->moveToThread(&loadingThread);

		loadingThread.start();
		QMetaObject::invokeMethod(
			loadingWorker,
			[loadingWorker, fileName, uvMethod, progressiveLoading]() {
				loadingWorker->setUVGenerationMethod(uvMethod);
				loadingWorker->loadModel(fileName.toStdString(), progressiveLoading);
			},
			Qt::QueuedConnection);

		waitLoop.exec();

		loadingThread.quit();
		loadingThread.wait();

		if (!loadingCompleted)
		{
			success = false;
			if (error.isEmpty())
			{
				error = tr("Model loading did not finish correctly.");
			}
		}

		makeCurrent();
		std::vector<AssImpMeshData> meshes = loadingWorker->getMeshes();
		if (meshes.empty())
		{
			if (error.isEmpty())
			{
				error = loadingWorker->getErrorMessage();
			}
			success = false;
			if (error == "Model loading cancelled by user.")
			{
				_loadCancelled = true;
			}
		}
		else
		{
			success = true;
			if (!_progressiveLoadingEnabled)
			{
				for (const AssImpMeshData& meshData : meshes)
				{
					AssImpMesh* mesh = createMeshFromData(meshData);
					addToDisplay(mesh);
					_pendingSceneUuids.append(mesh->uuid());
				}
			}
		}

		_assimpScene = loadingWorker->getScene();
		_globalSceneTransform = loadingWorker->getGlobalSceneTransform();
		if (_assimpScene)
		{
			// Populate the SceneGraph before the scene is deep-copied and merged,
			// while the original aiNode* tree is still intact and unmodified.
			if (success && !_pendingSceneUuids.isEmpty())
			{
				_viewer->sceneGraph()->appendFromScene(
					_assimpScene, fileName, _pendingSceneUuids);
			}
			_pendingSceneUuids.clear();

			// Record how many meshes were in _globalScene BEFORE merging.
			// Each newly loaded TriangleMesh has a sceneIndex relative to its
			// own per-model aiScene (0-based).  After mergeScene() appends the
			// new model's meshes starting at oldMeshCount, those per-model
			// indices become stale.  We fix them up here so that every
			// TriangleMesh in _meshStore always holds its correct position in
			// _globalScene->mMeshes[], which syncSceneToMeshStore() relies on.
			const unsigned int oldMeshCount =
				_globalScene ? _globalScene->mNumMeshes : 0u;

			aiScene* copiedScene = SceneUtils::deepCopyScene(_assimpScene);
			SceneUtils::mergeScene(&_globalScene, copiedScene);

			// Offset the sceneIndices of the meshes that were just added.
			// They are the last meshes.size() entries in _meshStore because
			// addToDisplay() always appends.
			if (oldMeshCount > 0 && !meshes.empty())
			{
				const int newCount = static_cast<int>(meshes.size());
				const int storeSize = static_cast<int>(_meshStore.size());
				const int firstNew  = storeSize - newCount;
				for (int i = firstNew; i < storeSize; ++i)
				{
					TriangleMesh* tm = _meshStore[i];
					if (tm && tm->getSceneIndex() >= 0)
						tm->setSceneIndex(
							static_cast<int>(oldMeshCount) + tm->getSceneIndex());
				}
			}
		}
		_assimpScene = nullptr;

		disconnect(finishedConnection);
		disconnect(cancelledConnection);
		disconnect(fileProgressConnection);
		disconnect(meshProgressConnection);
		disconnect(nodeProgressConnection);
		disconnect(batchConnection);
		disconnect(cancelRequestConnection);
		disconnect(lightsConnection);

		loadingWorker->moveToThread(this->thread());
		delete loadingWorker;
	}

	if (_loadCancelled)
	{
		if (!_meshStore.empty())
		{
			success = true;
		}
		if (_meshStore.empty())
		{
			MainWindow::showStatusMessage(tr("Model loading cancelled"), 3000);
		}
		else
		{
			MainWindow::showStatusMessage(
				tr("Model loading cancelled after importing %1 meshes").arg(_meshStore.size()),
				4000);
		}
	}
	else
	{
		MainWindow::showStatusMessage("");
	}

	MainWindow::setProgressValue(0);
	MainWindow::hideProgressBar();
	_cancelRequested = false;

	return success;
}

bool GLWidget::generateUVsForMeshes(const std::vector<int>& ids, const UVMethod& uvMethod, const UVConfig& uvConfig, QString& error)
{
	int meshCnt = ids.size();
	if (meshCnt == 0)
		return false;
	bool success = true;
	makeCurrent();
	MainWindow::showProgressBar(false);
	MainWindow::setProgressValue(0);
	MainWindow::showStatusMessage(tr("Generating UVs for %1 meshes").arg(meshCnt));
	float count = 0;
	for (int id : ids)
	{
		try
		{
			AssImpMesh* mesh = dynamic_cast<AssImpMesh*>(_meshStore.at(id));
			if (mesh)
			{								
				success = _assimpModelLoader->regenerateUVs(mesh, uvMethod, uvConfig);
				if (success)
				{
					MainWindow::showStatusMessage(tr("Updating mesh: ") + mesh->getName());
					int progress = static_cast<int>((++count / meshCnt) * 100.0f);					
					MainWindow::setProgressValue(progress);					
				}
				else
				{
					error = _assimpModelLoader->getErrorMessage();
					success = false;
					break;
				}
			}
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::generateUVs\n" << ex.what() << std::endl;
		}
	}
	MainWindow::showStatusMessage("");
	MainWindow::setProgressValue(0);
	MainWindow::hideProgressBar();
	return success;
}


void GLWidget::showFileReadingProgress(float percent)
{
	MainWindow::setProgressValue((int)((float)percent * 100.0f));
	makeCurrent();
}

void GLWidget::showMeshLoadingProgress(float /*percent*/)
{
	makeCurrent();
}

void GLWidget::showNodeMeshLoadingProgress(int processedNodes, int totalNodes, int processedMeshes, int totalMeshes, bool uvProcessed)
{
	QString statusMessage = (uvProcessed) ? tr("Generating UVs... ") : "";
	statusMessage += QString(tr("Processing node: %1/%2  Mesh: %3/%4"))
		.arg(processedNodes)
		.arg(totalNodes)
		.arg(processedMeshes)
		.arg(totalMeshes);
	MainWindow::showStatusMessage(statusMessage);

	if (totalNodes > 0)
	{
		MainWindow::setProgressValue(static_cast<int>((static_cast<float>(processedNodes) / static_cast<float>(totalNodes)) * 100.0f));
	}

	makeCurrent();
}

void GLWidget::swapVisible(bool checked)
{
	_visibleSwapped = checked;
	recalculateVisibleSceneStats(false);
	triggerShadowRecomputation();
	updateFloorPlane();
	fitAll();

	emit visibleSwapped(checked);
}

void GLWidget::cancelAssImpModelLoading()
{
	if (_cancelRequested)
		return;

	_cancelRequested = true;
	MainWindow::requestFileLoadCancel();
	MainWindow::setCancelButtonEnabled(false);
	MainWindow::setCancelButtonText(tr("Cancelling..."));
	MainWindow::showStatusMessage(tr("Cancelling model load..."));
	emit loadingAssImpModelCancelled();	
}

void GLWidget::enableADSDiffuseTexMap(const std::vector<int>& ids, const bool& enable)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->enableDiffuseADSMap(enable);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::enableADSDiffuseTexMap\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::setADSDiffuseTexMap(const std::vector<int>& ids, const QString& path)
{
	unsigned int texId = loadTextureFromFile(path.toStdString().c_str());
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->setDiffuseADSMap(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::setADSDiffuseTexMap\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::clearADSDiffuseTexMap(const std::vector<int>& ids)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->clearDiffuseADSMap();
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::clearADSDiffuseTexMap\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::enableADSSpecularTexMap(const std::vector<int>& ids, const bool& enable)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->enableSpecularADSMap(enable);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::enableADSSpecularTexMap\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::setADSSpecularTexMap(const std::vector<int>& ids, const QString& path)
{
	unsigned int texId = loadTextureFromFile(path.toStdString().c_str());
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->setSpecularADSMap(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::setADSSpecularTexMap\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::clearADSSpecularTexMap(const std::vector<int>& ids)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->clearSpecularADSMap();
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::clearADSSpecularTexMap\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::enableADSEmissiveTexMap(const std::vector<int>& ids, const bool& enable)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->enableEmissiveADSMap(enable);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::enableADSEmissiveTexMap\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::setADSEmissiveTexMap(const std::vector<int>& ids, const QString& path)
{
	unsigned int texId = loadTextureFromFile(path.toStdString().c_str());
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->setEmissiveADSMap(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::setADSEmissiveTexMap\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::clearADSEmissiveTexMap(const std::vector<int>& ids)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->clearEmissiveADSMap();
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::clearADSEmissiveTexMap\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::enableADSNormalTexMap(const std::vector<int>& ids, const bool& enable)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->enableNormalADSMap(enable);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::enableADSNormalTexMap\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::setADSNormalTexMap(const std::vector<int>& ids, const QString& path)
{
	unsigned int texId = loadTextureFromFile(path.toStdString().c_str());
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->setNormalADSMap(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::setADSNormalTexMap\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::clearADSNormalTexMap(const std::vector<int>& ids)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->clearNormalADSMap();
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::clearADSNormalTexMap\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::enableADSHeightTexMap(const std::vector<int>& ids, const bool& enable)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->enableHeightADSMap(enable);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::enableADSHeightTexMap\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::setADSHeightTexMap(const std::vector<int>& ids, const QString& path)
{
	unsigned int texId = loadTextureFromFile(path.toStdString().c_str());
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->setHeightADSMap(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::setADSHeightTexMap\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::clearADSHeightTexMap(const std::vector<int>& ids)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->clearHeightADSMap();
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::clearADSHeightTexMap\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::enableADSOpacityTexMap(const std::vector<int>& ids, const bool& enable)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->enableOpacityADSMap(enable);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::enableADSOpacityTexMap\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::invertADSOpacityTexMap(const std::vector<int>& ids, const bool& inverted)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->invertOpacityADSMap(inverted);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::invertADSOpacityTexMap\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::setADSOpacityTexMap(const std::vector<int>& ids, const QString& path)
{
	unsigned int texId = loadTextureFromFile(path.toStdString().c_str());
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->setOpacityADSMap(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::setADSOpacityTexMap\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::clearADSOpacityTexMap(const std::vector<int>& ids)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->clearOpacityADSMap();
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::clearADSOpacityTexMap\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::clearADSTexMaps(const std::vector<int>& ids)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->clearAllADSMaps();
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::clearADSTexMaps\n" << ex.what() << std::endl;
		}
	}

	updateView();
}

void GLWidget::setMaterialToObjects(const std::vector<int>& ids, const GLMaterial& mat)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->setMaterial(mat);
			if(mat.hasTransmission())
				setTransmissionEnabled(true);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::setMaterialToObjects\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::setTexturesToObjects(const std::vector<int>& ids, const GLMaterial& mat)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			GLMaterial resolved = resolveMaterialTextures(this, mat);
			mesh->setTextureMaps(resolved);
			mesh->invertOpacityADSMap(resolved.isOpacityMapInverted());
			mesh->invertOpacityPBRMap(resolved.isOpacityMapInverted());
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::setTexturesToObjects\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::synchronizeTextureCache(const GLMaterial* material, GLMaterial::TextureType type)
{
	if (!material) return;

	const GLMaterial::Texture& matTex = material->texture(type);
	if (matTex.path.empty()) return;

	TextureSamplerSettings samplers{
		matTex.wrapS,
		matTex.wrapT,
		matTex.minFilter,
		matTex.magFilter
	};

	makeCurrent();
	getOrLoadTextureCached(QString::fromStdString(matTex.path), samplers);
}

void GLWidget::clearTextureCache()
{
	for (auto& entry : _texCache)
	{
		if (entry.second.lastGPUTexture != 0)
		{
			glDeleteTextures(1, &entry.second.lastGPUTexture);
		}
	}
	_texCache.clear();
	_texRefCount.clear();
}

void GLWidget::setPBRAlbedoColor(const std::vector<int>& ids, const QColor& col)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->setPBRAlbedoColor(col.red() / 256.0f, col.green() / 256.0f, col.blue() / 256.0f);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::setPBRAlbedoColor\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::setPBRMetallic(const std::vector<int>& ids, const float& val)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->setPBRMetallic(val);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::setPBRMetallic\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::setPBRRoughness(const std::vector<int>& ids, const float& val)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->setPBRRoughness(val);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::setPBRRoughness\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::clearPBRTexMaps(const std::vector<int>& ids)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->clearAllPBRMaps();
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::clearPBRTextures\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::enablePBRAlbedoTexMap(const std::vector<int>& ids, const bool& enable)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->enableAlbedoPBRMap(enable);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::enableAlbedoTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::setPBRAlbedoTexMap(const std::vector<int>& ids, const QString& path)
{
	unsigned int texId = getOrLoadTextureCached(path);
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->setAlbedoPBRMap(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::setAlbedoTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::clearPBRAlbedoTexMap(const std::vector<int>& ids)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			unsigned int texId = mesh->getAlbedoPBRMap();
			mesh->clearAlbedoPBRMap();
			releaseTexture(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::clearAlbedoTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::enablePBRMetallicTexMap(const std::vector<int>& ids, const bool& enable)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->enableMetallicPBRMap(enable);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::enableMetallicTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::setPBRMetallicTexMap(const std::vector<int>& ids, const QString& path)
{
	unsigned int texId = getOrLoadTextureCached(path);
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->setMetallicPBRMap(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::setMetallicTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::clearPBRMetallicTexMap(const std::vector<int>& ids)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			unsigned int texId = mesh->getMetallicPBRMap();
			mesh->clearMetallicPBRMap();
			releaseTexture(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::clearMetallicTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::enablePBRRoughnessTexMap(const std::vector<int>& ids, const bool& enable)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->enableRoughnessPBRMap(enable);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::enableRoughnessTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::setPBRRoughnessTexMap(const std::vector<int>& ids, const QString& path)
{
	unsigned int texId = getOrLoadTextureCached(path);
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->setRoughnessPBRMap(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::setRoughnessTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::clearPBRRoughnessTexMap(const std::vector<int>& ids)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			unsigned int texId = mesh->getRoughnessPBRMap();
			mesh->clearRoughnessPBRMap();
			releaseTexture(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::clearRoughnessTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::enablePBRNormalTexMap(const std::vector<int>& ids, const bool& enable)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->enableNormalPBRMap(enable);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::enableNormalTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::setPBRNormalTexMap(const std::vector<int>& ids, const QString& path)
{
	unsigned int texId = getOrLoadTextureCached(path);
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->setNormalPBRMap(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::setNormalTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::clearPBRNormalTexMap(const std::vector<int>& ids)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			unsigned int texId = mesh->getNormalPBRMap();
			mesh->clearNormalPBRMap();
			releaseTexture(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::clearNormalTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::enablePBRAOTexMap(const std::vector<int>& ids, const bool& enable)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->enableAOPBRMap(enable);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::enableAOTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::setPBRAOTexMap(const std::vector<int>& ids, const QString& path)
{
	unsigned int texId = getOrLoadTextureCached(path);
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->setAOPBRMap(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::setAOTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::clearPBRAOTexMap(const std::vector<int>& ids)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			unsigned int texId = mesh->getAOPBRMap();
			mesh->clearAOPBRMap();
			releaseTexture(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::clearAOTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::enablePBROpacityTexMap(const std::vector<int>& ids, const bool& enable)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->enableOpacityPBRMap(enable);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::enablePBROpacityTexMap\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::setPBROpacityTexMap(const std::vector<int>& ids, const QString& path)
{
	unsigned int texId = getOrLoadTextureCached(path);
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->setOpacityPBRMap(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::setPBROpacityTexMap\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::invertPBROpacityTexMap(const std::vector<int>& ids, const bool& inverted)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->invertOpacityPBRMap(inverted);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::invertPBROpacityTexMap\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::clearPBROpacityTexMap(const std::vector<int>& ids)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			unsigned int texId = mesh->getOpacityPBRMap();
			mesh->clearOpacityPBRMap();
			releaseTexture(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::clearPBROpacityTexMap\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::enablePBRHeightTexMap(const std::vector<int>& ids, const bool& enable)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->enableHeightPBRMap(enable);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::enableHeightTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::setPBRHeightTexMap(const std::vector<int>& ids, const QString& path)
{
	unsigned int texId = getOrLoadTextureCached(path);
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->setHeightPBRMap(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::setHeightTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::clearPBRHeightTexMap(const std::vector<int>& ids)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			unsigned int texId = mesh->getHeightPBRMap();
			mesh->clearHeightPBRMap();
			releaseTexture(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::clearHeightTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::setPBRHeightScale(const std::vector<int>& ids, const float& scale)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->setHeightPBRMapScale(scale);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::setHeightScale\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::enablePBRTransmissionTexMap(const std::vector<int>& ids, const bool& enable)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->enableTransmissionPBRMap(enable);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::enableTransmissionTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::setPBRTransmissionTexMap(const std::vector<int>& ids, const QString& path)
{
	unsigned int texId = getOrLoadTextureCached(path);
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->setTransmissionPBRMap(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::setTransmissionTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::clearPBRTransmissionTexMap(const std::vector<int>& ids)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			unsigned int texId = mesh->getTransmissionPBRMap();
			mesh->clearTransmissionPBRMap();
			releaseTexture(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::clearTransmissionTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::enablePBRIORTexMap(const std::vector<int>& ids, const bool& enable)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->enableIORPBRMap(enable);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::enableIORTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::setPBRIORTexMap(const std::vector<int>& ids, const QString& path)
{
	unsigned int texId = getOrLoadTextureCached(path);
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->setIORPBRMap(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::setIORTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::clearPBRIORTexMap(const std::vector<int>& ids)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			unsigned int texId = mesh->getIORPBRMap();
			mesh->clearIORPBRMap();
			releaseTexture(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::clearIORTexture\n" << ex.what() << std::endl;
		}
	}
}


void GLWidget::enablePBRSheenColorTexMap(const std::vector<int>& ids, const bool& enable)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->enableSheenColorPBRMap(enable);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::enableSheenColorTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::setPBRSheenColorTexMap(const std::vector<int>& ids, const QString& path)
{
	unsigned int texId = getOrLoadTextureCached(path);
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->setSheenColorPBRMap(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::setSheenColorTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::clearPBRSheenColorTexMap(const std::vector<int>& ids)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			unsigned int texId = mesh->getSheenColorPBRMap();
			mesh->clearSheenColorPBRMap();
			releaseTexture(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::clearSheenColorTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::enablePBRSheenRoughnessTexMap(const std::vector<int>& ids, const bool& enable)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->enableSheenRoughnessPBRMap(enable);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::enableSheenRoughnessTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::setPBRSheenRoughnessTexMap(const std::vector<int>& ids, const QString& path)
{
	unsigned int texId = getOrLoadTextureCached(path);
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->setSheenRoughnessPBRMap(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::setSheenRoughnessTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::clearPBRSheenRoughnessTexMap(const std::vector<int>& ids)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			unsigned int texId = mesh->getSheenRoughnessPBRMap();
			mesh->clearSheenRoughnessPBRMap();
			releaseTexture(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::clearSheenRoughnessTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::enablePBRClearcoatTexMap(const std::vector<int>& ids, const bool& enable)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->enableClearcoatPBRMap(enable);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::enableClearcoatTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::setPBRClearcoatTexMap(const std::vector<int>& ids, const QString& path)
{
	unsigned int texId = getOrLoadTextureCached(path);
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->setClearcoatPBRMap(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::setClearcoatTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::clearPBRClearcoatTexMap(const std::vector<int>& ids)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			unsigned int texId = mesh->getClearcoatPBRMap();
			mesh->clearClearcoatPBRMap();
			releaseTexture(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::clearClearcoatTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::enablePBRClearcoatRoughnessTexMap(const std::vector<int>& ids, const bool& enable)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->enableClearcoatRoughnessPBRMap(enable);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::enableClearcoatRoughnessTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::setPBRClearcoatRoughnessTexMap(const std::vector<int>& ids, const QString& path)
{
	unsigned int texId = getOrLoadTextureCached(path);
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->setClearcoatRoughnessPBRMap(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::setClearcoatRoughnessTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::clearPBRClearcoatRoughnessTexMap(const std::vector<int>& ids)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			unsigned int texId = mesh->getClearcoatRoughnessPBRMap();
			mesh->clearClearcoatRoughnessPBRMap();
			releaseTexture(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::clearClearcoatRoughnessTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::enablePBRClearcoatNormalTexMap(const std::vector<int>& ids, const bool& enable)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->enableClearcoatNormalPBRMap(enable);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::enableClearcoatNormalTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::setPBRClearcoatNormalTexMap(const std::vector<int>& ids, const QString& path)
{
	unsigned int texId = getOrLoadTextureCached(path);
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->setClearcoatNormalPBRMap(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::setClearcoatNormalTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::clearPBRClearcoatNormalTexMap(const std::vector<int>& ids)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			unsigned int texId = mesh->getClearcoatNormalPBRMap();
			mesh->clearClearcoatNormalPBRMap();
			releaseTexture(texId);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::clearClearcoatNormalTexture\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::setTransformation(const std::vector<int>& ids, const QVector3D& trans, const QVector3D& rot, const QVector3D& scale)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->setTranslation(trans);
			mesh->setRotation(rot);
			mesh->setScaling(scale);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::setTransformation\n" << ex.what() << std::endl;
		}
	}

	// If all meshes selected = model-level transformation
	bool isModelLevelTransform = (ids.size() == _meshStore.size());

	if (isModelLevelTransform)
	{
		// Build rotation matrix from Euler angles (same as mesh transformation)
		// rot is QVector3D(x, y, z) in degrees
		glm::mat4 rotX = glm::rotate(glm::mat4(1.0f), glm::radians(rot.x()), glm::vec3(1, 0, 0));
		glm::mat4 rotY = glm::rotate(glm::mat4(1.0f), glm::radians(rot.y()), glm::vec3(0, 1, 0));
		glm::mat4 rotZ = glm::rotate(glm::mat4(1.0f), glm::radians(rot.z()), glm::vec3(0, 0, 1));

		// Apply rotation in same order as meshes (Z * Y * X)
		_lightRepoBasis.accumulatedRotation = rotZ * rotY * rotX;		
	}

	recalculateVisibleSceneStats(false);
	updatePunctualLights();
	triggerShadowRecomputation();
	updateFloorPlane();
	fitAll();
}

void GLWidget::bakeTransformation(const std::vector<int>& ids)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->bakeTransformations();
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::bakeTransformation\n" << ex.what() << std::endl;
		}
	}
}

void GLWidget::resetTransformation(const std::vector<int>& ids)
{
	for (int id : ids)
	{
		try
		{
			TriangleMesh* mesh = _meshStore[id];
			mesh->resetTransformations();			
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in GLWidget::resetTransformation\n" << ex.what() << std::endl;
		}
	}

	// Reset light offsets when model transformations are reset
	_lightOffsetX = 0.0f;
	_lightOffsetY = 0.0f;
	_lightOffsetZ = 0.0f;
	_lightRepoBasis.accumulatedRotation = glm::mat4(1.0f);  // Reset to identity

	recalculateVisibleSceneStats(false);
	updatePunctualLights();
	fitAll();
	triggerShadowRecomputation();
}

void GLWidget::applyTransforms(const QMap<int, TransformState>& transforms)
{
	if (transforms.isEmpty())
		return;

	makeCurrent();

	// Apply all transformations to individual meshes
	for (auto it = transforms.begin(); it != transforms.end(); ++it)
	{
		int index = it.key();
		const TransformState& state = it.value();

		if (index >= 0 && index < static_cast<int>(_meshStore.size()))
		{
			TriangleMesh* mesh = _meshStore[index];
			if (mesh)
			{
				mesh->setTranslation(state.translation);
				mesh->setRotation(state.rotation);
				mesh->setScaling(state.scale);
			}
		}
	}

	// Check if this is a model-level transformation
	// (all meshes are being transformed)
	bool isModelLevelTransform = (transforms.size() == static_cast<int>(_meshStore.size()));

	if (isModelLevelTransform && !transforms.isEmpty())
	{
		// For model-level transforms, update light repositioning basis
		// Use the transformation from the first mesh (they should all be the same)
		const TransformState& state = transforms.first();

		// Build rotation matrix from Euler angles
		glm::mat4 rotX = glm::rotate(glm::mat4(1.0f),
			glm::radians(state.rotation.x()),
			glm::vec3(1, 0, 0));
		glm::mat4 rotY = glm::rotate(glm::mat4(1.0f),
			glm::radians(state.rotation.y()),
			glm::vec3(0, 1, 0));
		glm::mat4 rotZ = glm::rotate(glm::mat4(1.0f),
			glm::radians(state.rotation.z()),
			glm::vec3(0, 0, 1));

		// Apply rotation in same order as meshes (Z * Y * X)
		_lightRepoBasis.accumulatedRotation = rotZ * rotY * rotX;
	}

	// Update all dependent systems once
	recalculateVisibleSceneStats(false);
	updatePunctualLights();
	triggerShadowRecomputation();
	updateFloorPlane();
	fitAll();

	doneCurrent();
}

void GLWidget::createShaderPrograms()
{
    const QString path = PathUtils::getDataDirectory() + "/";
	// Foreground objects shader program
	// Per fragment lighting
	_fgShader = std::make_unique<ShaderProgram>(); _fgShader->setObjectName("_fgShader");
    _fgShader->loadCompileAndLinkShaderFromFile(path + "shaders/main_scene.vert",
        path + "shaders/main_scene.frag");
	// Axis
	_axisShader = std::make_unique<ShaderProgram>(); _axisShader->setObjectName("_axisShader");
	_axisShader->loadCompileAndLinkShaderFromFile(path + "shaders/axis.vert", path + "shaders/axis.frag");
	// Vertex Normal
	_vertexNormalShader = std::make_unique<ShaderProgram>(); _vertexNormalShader->setObjectName("_vertexNormalShader");
	_vertexNormalShader->loadCompileAndLinkShaderFromFile(path + "shaders/vertex_normal.vert",
        path + "shaders/vertex_normal.frag", path + "shaders/vertex_normal.geom");
	// Face Normal
	_faceNormalShader = std::make_unique<ShaderProgram>(); _faceNormalShader->setObjectName("_faceNormalShader");
	_faceNormalShader->loadCompileAndLinkShaderFromFile(path + "shaders/face_normal.vert",
        path + "shaders/face_normal.frag", path + "shaders/face_normal.geom");
	// Shadow mapping
	_shadowMappingShader = std::make_unique<ShaderProgram>(); _shadowMappingShader->setObjectName("_shadowMappingShader");
	_shadowMappingShader->loadCompileAndLinkShaderFromFile(path + "shaders/shadow_mapping_depth.vert",
        path + "shaders/shadow_mapping_depth.frag");
	// Sky Box
	_skyBoxShader = std::make_unique<ShaderProgram>(); _skyBoxShader->setObjectName("_skyBoxShader");
	_skyBoxShader->loadCompileAndLinkShaderFromFile(path + "shaders/skybox.vert", path + "shaders/skybox.frag");
	// Irradiance Map (now uses fullscreen triangle)
	_irradianceShader = std::make_unique<ShaderProgram>(); _irradianceShader->setObjectName("_irradianceShader");
	_irradianceShader->loadCompileAndLinkShaderFromFile(path + "shaders/fullscreen_triangle.vert", path + "shaders/irradiance_convolution.frag");
	// Prefilter Map (now uses fullscreen triangle)
	_prefilterShader = std::make_unique<ShaderProgram>(); _prefilterShader->setObjectName("_prefilterShader");
	_prefilterShader->loadCompileAndLinkShaderFromFile(path + "shaders/fullscreen_triangle.vert", path + "shaders/prefilter.frag");
	// BRDF LUT Map
	_brdfShader = std::make_unique<ShaderProgram>(); _brdfShader->setObjectName("_brdfShader");
	_brdfShader->loadCompileAndLinkShaderFromFile(path + "shaders/brdf.vert", path + "shaders/brdf.frag");
	// Text shader program
	_textShader = std::make_unique<ShaderProgram>(); _textShader->setObjectName("_textShader");
	_textShader->loadCompileAndLinkShaderFromFile(path + "shaders/text.vert", path + "shaders/text.frag");
	// Background gradient shader program
	_bgShader = std::make_unique<ShaderProgram>(); _bgShader->setObjectName("_bgShader");
	_bgShader->loadCompileAndLinkShaderFromFile(path + "shaders/background.vert", path + "shaders/background.frag");
	// Background split shader program
	_bgSplitShader = std::make_unique<ShaderProgram>(); _bgSplitShader->setObjectName("_bgSplitShader");
	_bgSplitShader->loadCompileAndLinkShaderFromFile(path + "shaders/splitScreen.vert", path + "shaders/splitScreen.frag");
	// Light Cube shader program
	_lightCubeShader = std::make_unique<ShaderProgram>(); _lightCubeShader->setObjectName("_lightCubeShader");
	_lightCubeShader->loadCompileAndLinkShaderFromFile(path + "shaders/light_cube.vert", path + "shaders/light_cube.frag");
	// Clipping Plane shader program
	_clippingPlaneShader = std::make_unique<ShaderProgram>(); _clippingPlaneShader->setObjectName("_clippingPlaneShader");
	_clippingPlaneShader->loadCompileAndLinkShaderFromFile(path + "shaders/clipping_plane.vert", path + "shaders/clipping_plane.frag");
	// Clipped Mesh shader program
	_clippedMeshShader = std::make_unique<ShaderProgram>(); _clippedMeshShader->setObjectName("_clippedMeshShader");
	_clippedMeshShader->loadCompileAndLinkShaderFromFile(path + "shaders/clipped_mesh.vert", path + "shaders/clipped_mesh.frag");
	// Selection shader program
	_selectionShader = std::make_unique<ShaderProgram>(); _selectionShader->setObjectName("_selectionShader");
	_selectionShader->loadCompileAndLinkShaderFromFile(path + "shaders/selection.vert", path + "shaders/selection.frag");
	// Equirectangular to Cube conversion shader
	_equirectToCubeShader = std::make_unique<ShaderProgram>();
	_equirectToCubeShader->setObjectName("_equirectToCubeShader");
	_equirectToCubeShader->loadCompileAndLinkShaderFromFile( path + "shaders/equirect_to_cube.vert", path + "shaders/equirect_to_cube.frag");
	// Equirectangular to Cube Quad conversion shader
	_equirectToCubeQuadShader = std::make_unique<ShaderProgram>();
	_equirectToCubeQuadShader->setObjectName("_equirectToCubeQuadShader");
	_equirectToCubeQuadShader->loadCompileAndLinkShaderFromFile(path + "shaders/equirect_to_cube_quad.vert", path + "shaders/equirect_to_cube_quad.frag");
	// Downsample shader program
	_downsampleShader = std::make_unique<ShaderProgram>();
	_downsampleShader->setObjectName("_downsampleShader");
	_downsampleShader->loadCompileAndLinkShaderFromFile(path + "shaders/downsample_cubemap.vert", path + "shaders/downsample_cubemap.frag");


	// Shadow Depth quad shader program - for debugging
	_debugShader = std::make_unique<ShaderProgram>(); _debugShader->setObjectName("_debugShader");
	_debugShader->loadCompileAndLinkShaderFromFile(path + "shaders/debug_quad.vert", path + "shaders/debug_quad_depth.frag");
}

void GLWidget::createCappingPlanes()
{
    const QString path = PathUtils::getDataDirectory() + "/";
	_clippingPlaneXY = new Plane(_clippingPlaneShader.get(), QVector3D(0, 0, 0), 1000, 1000, 1, 1);
	_clippingPlaneYZ = new Plane(_clippingPlaneShader.get(), QVector3D(0, 0, 0), 1000, 1000, 1, 1);
	_clippingPlaneZX = new Plane(_clippingPlaneShader.get(), QVector3D(0, 0, 0), 1000, 1000, 1, 1);
    _cappingTexture = loadTextureFromFile(QString(path + "textures/patterns/hatch_03.png").toStdString().c_str());
	glActiveTexture(GL_TEXTURE6);
	glBindTexture(GL_TEXTURE_2D, _cappingTexture);

	// Stable sampling for any scale
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glGenerateMipmap(GL_TEXTURE_2D);
	// (Optional) if supported:
	GLfloat aniso = 8.0f;
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso);
}

void GLWidget::createLights()
{
	_lightCube = new Cube(_lightCubeShader.get(), 10);
	_lightSphere = new Sphere(_lightCubeShader.get(), 1, 16, 16);
}

void GLWidget::createFullscreenTriangle()
{
	// Fullscreen triangle vertices in clip space
	// Forms a triangle that covers entire viewport
	const float verts[6] = {
		-1.0f, -1.0f,  // Bottom-left
		 3.0f, -1.0f,  // Bottom-right (extends past viewport)
		-1.0f,  3.0f   // Top-left (extends past viewport)
	};

	glGenVertexArrays(1, &_fsTriVAO);
	glGenBuffers(1, &_fsTriVBO);

	glBindVertexArray(_fsTriVAO);
	glBindBuffer(GL_ARRAY_BUFFER, _fsTriVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

	// Set up vertex attribute: 2D position at location 0
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	_fsTriInitialized = true;
}

void GLWidget::drawFullscreenTriangle()
{
	if (!_fsTriInitialized)
	{
		qWarning() << "Fullscreen triangle not initialized!";
		return;
	}

	glBindVertexArray(_fsTriVAO);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);
}

void GLWidget::setIBLFaceBasis(QOpenGLShaderProgram* prog, int faceIndex)
{
	auto setM = [prog](const QVector3D& U, const QVector3D& V, const QVector3D& W) {
		QMatrix3x3 m;
		m(0, 0) = U.x(); m(1, 0) = U.y(); m(2, 0) = U.z();
		m(0, 1) = V.x(); m(1, 1) = V.y(); m(2, 1) = V.z();
		m(0, 2) = W.x(); m(1, 2) = W.y(); m(2, 2) = W.z();
		prog->setUniformValue("faceBasis", m);
		};

	// Basis vectors with 90° X-axis rotation applied
	// (Same rotation as: model.rotate(90.0f, QVector3D(1.0f, 0.0f, 0.0f)))
	switch (faceIndex)
	{
	case 0: // Right (+X)
		setM(QVector3D(0.0f, 1.0f, 0.0f),
			QVector3D(0.0f, 0.0f, 1.0f),
			QVector3D(1.0f, 0.0f, 0.0f));
		break;

	case 1: // Left (-X)
		setM(QVector3D(0.0f, -1.0f, 0.0f),
			QVector3D(0.0f, 0.0f, 1.0f),
			QVector3D(-1.0f, 0.0f, 0.0f));
		break;

	case 2: // Top (+Y)
		setM(QVector3D(1.0f, 0.0f, 0.0f),
			QVector3D(0.0f, -1.0f, 0.0f),
			QVector3D(0.0f, 0.0f, -1.0f));
		break;

	case 3: // Bottom (-Y)
		setM(QVector3D(1.0f, 0.0f, 0.0f),
			QVector3D(0.0f, 1.0f, 0.0f),
			QVector3D(0.0f, 0.0f, 1.0f));
		break;

	case 4: // Front (+Z)
		setM(QVector3D(1.0f, 0.0f, 0.0f),
			QVector3D(0.0f, 0.0f, 1.0f),
			QVector3D(0.0f, -1.0f, 0.0f));
		break;

	case 5: // Back (-Z)
		setM(QVector3D(-1.0f, 0.0f, 0.0f),
			QVector3D(0.0f, 0.0f, 1.0f),
			QVector3D(0.0f, 1.0f, 0.0f));
		break;
	}
}

void GLWidget::loadFloor()
{
	// configure depth map FBO
	// -----------------------
	// create depth texture
	if (_shadowMap == 0 || _shadowMapNeedsInitialization)
	{
		if (_shadowMap != 0)
		{
			glDeleteTextures(1, &_shadowMap);
			_shadowMap = 0;
		}
		glGenTextures(1, &_shadowMap);
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, _shadowMap);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, _shadowWidth, _shadowHeight, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);		
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
		float borderColor[] = { 0.0, 0.0, 0.0, 0.0 };
		glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
		// attach depth texture as FBO's depth buffer
		if (_shadowMapFBO != 0)
		{
			glDeleteFramebuffers(1, &_shadowMapFBO);
			_shadowMapFBO = 0;
		}
		glGenFramebuffers(1, &_shadowMapFBO);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _shadowMapFBO);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, _shadowMap, 0);
		unsigned long status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE)
			std::cout << "Frame buffer creation failed!" << std::endl;
		glDrawBuffer(GL_NONE);
		glReadBuffer(GL_NONE);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, defaultFramebufferObject());
	}

	// Use helper to update floor geometry
	float halfObjectSize = updateFloorGeometry();

	// Use helper to set main light position
	updateMainLightPosition(halfObjectSize);

	float floorPlaneCoeff = _meshStore.empty() ? -_floorSize - (_floorSize * 0.05f) : lowestModelZ() - (_floorSize * _floorOffsetPercent);

	// FIX: Delete old floor plane to prevent memory leak
	if (_floorPlane != nullptr)
	{
		delete _floorPlane;
		_floorPlane = nullptr;
	}

	_floorPlane = new Plane(_fgShader.get(), _floorCenter, _floorSize * _floorSizeFactor, _floorSize * _floorSizeFactor, 1, 1, floorPlaneCoeff, 1, 1);

	// Use helper to apply common material/texture settings
	applyFloorPlaneMaterialSettings();
}

void GLWidget::applyFloorPlaneMaterialSettings()
{
	if (_floorPlane == nullptr)
		return;

	_floorPlane->setAmbientMaterial(QVector3D(0.0f, 0.0f, 0.0f));
	_floorPlane->setDiffuseMaterial(QVector3D(1.0f, 1.0f, 1.0f));
	_floorPlane->setSpecularMaterial(QVector3D(0.5f, 0.5f, 0.5f));
	_floorPlane->setShininess(16.0f);
	_floorPlane->enableTexture(_floorTextureDisplayed);
	_floorPlane->setTexureImage(_floorTexImage);
}

void GLWidget::updateMainLightPosition(float halfObjectSize)
{
	_lightPosition.setX(_floorCenter.x() + _floorSize * 1.25);
	_lightPosition.setY(_floorCenter.y() + _floorSize * 1.25);

	if (_meshStore.empty())
		_lightPosition.setZ(_floorSize);
	else
	{
		float highestZ = highestModelZ();
		_lightPosition.setZ(highestZ + halfObjectSize * 5.0f + (_floorSize * _floorOffsetPercent));
	}
}

float GLWidget::updateFloorGeometry()
{
	float halfObjectSize = _boundingSphere.getRadius();
	_floorCenter = _boundingSphere.getCenter();

	if (_boundingBox.getZSize() >= _boundingBox.getXSize() && _boundingBox.getZSize() >= _boundingBox.getYSize())
		_floorSize = _boundingBox.getZSize();
	else
		_floorSize = (std::max(_boundingBox.getYSize(), _boundingBox.getXSize())) / 1.25f;

	_lightCube->setSize(halfObjectSize * 0.1f);

	return halfObjectSize;
}

void GLWidget::updatePunctualLights()
{
	if (_originalParsedLights.empty() || _lightRepoBasis.baselineRadius <= 0.0f)
	{
		return;
	}

	// Start with original positions
	_currentRepositionedLights = _originalParsedLights;

	// Get current bounding sphere state
	glm::vec3 currentCenter(
		static_cast<float>(_boundingSphere.getCenter().x()),
		static_cast<float>(_boundingSphere.getCenter().y()),
		static_cast<float>(_boundingSphere.getCenter().z())
	);
	float currentRadius = _boundingSphere.getRadius();

	// Compute deltas from baseline
	glm::vec3 centerDelta = currentCenter - _lightRepoBasis.baselineCenter;
	float radiusDelta = currentRadius / _lightRepoBasis.baselineRadius;

	
	// Apply transformations to each light
	for (int i = 0; i < _currentRepositionedLights.size(); ++i)
	{
		auto& light = _currentRepositionedLights[i];

		// STEP 1: Get offset from baseline center
		glm::vec3 offsetFromBaseline = light.position - _lightRepoBasis.baselineCenter;

		// STEP 2: ROTATION - Apply rotation matrix to offset
		glm::vec3 rotatedOffset = glm::vec3(_lightRepoBasis.accumulatedRotation * glm::vec4(offsetFromBaseline, 0.0f));

		// STEP 3: RADIAL SCALING - Scale the rotated offset by radius delta
		glm::vec3 scaledOffset = rotatedOffset * radiusDelta;

		// STEP 4: TRANSLATION - Apply translation and return to world space
		light.position = _lightRepoBasis.baselineCenter + scaledOffset + centerDelta;
				
		// Scale range by radius delta (only scaling affects range)
		if (light.range > 0.0f)
		{
			light.range *= radiusDelta;
		}

		// Scale intensity (inverse-square law) for point and spot lights only
		if (light.type != static_cast<int>(LightType::Directional))
		{
			light.intensity *= (radiusDelta * radiusDelta);
		}
	}

	glLights->setLights(_currentRepositionedLights);
}

void GLWidget::loadEnvMap()
{	
    const QString path = PathUtils::getDataDirectory() + "/";

	_skyBox = new Cube(_skyBoxShader.get(), 1);
	_skyBoxShader->bind();
	_skyBoxShader->setUniformValue("skybox", 1);
	
	glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
	glGenTextures(1, &_environmentMap);
	
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_CUBE_MAP, _environmentMap);

	setSkyBoxTextureFolder(path + "textures/envmap/skyboxes/LDRI/@Default");	
}

void GLWidget::loadIrradianceMap()
{
	// Setup framebuffer for offscreen rendering
	unsigned int captureFBO;
	unsigned int captureRBO;
	glGenFramebuffers(1, &captureFBO);
	glGenRenderbuffers(1, &captureRBO);

	glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
	glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRBO);

	// Save GL state
	glDisable(GL_BLEND);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_STENCIL_TEST);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glDisable(GL_CULL_FACE);

	// Create irradiance cubemap
	if (_irradianceMap)
		glDeleteTextures(1, &_irradianceMap);
	glGenTextures(1, &_irradianceMap);
	//std::cout << "GLWidget::loadIrradianceMap : _irradianceMap = " << _irradianceMap << std::endl;
	glBindTexture(GL_TEXTURE_CUBE_MAP, _irradianceMap);

	constexpr int irradianceSize = 64;
	for (unsigned int i = 0; i < 6; ++i)
	{
		if (_skyBoxTextureHDRI)
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB32F,
				irradianceSize, irradianceSize, 0, GL_RGB, GL_FLOAT, nullptr);
		else
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
				irradianceSize, irradianceSize, 0, GL_RGB, GL_HALF_FLOAT, nullptr);
	}
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	// Setup framebuffer for irradiance rendering
	glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
	glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, irradianceSize, irradianceSize);

	// ==== IRRADIANCE PASS: Use fullscreen triangle ====
	_irradianceShader->bind();
	_irradianceShader->setUniformValue("environmentMap", 1);
	_irradianceShader->setUniformValue("resolution", QVector2D(irradianceSize, irradianceSize));

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_CUBE_MAP, _environmentMap);

	glViewport(0, 0, irradianceSize, irradianceSize);
	glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);

	for (unsigned int i = 0; i < 6; ++i)
	{
		// Bind this face to the framebuffer
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, _irradianceMap, 0);

		GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (fboStatus != GL_FRAMEBUFFER_COMPLETE)
		{
			qWarning() << "Irradiance FBO incomplete at face" << i << "Status:" << fboStatus;
			continue;
		}

		// Set per-face basis matrix
		_irradianceShader->bind();
		setIBLFaceBasis(_irradianceShader.get(), i);

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

		// Draw fullscreen triangle (not the cube!)
		drawFullscreenTriangle();
	}

	glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());

	// ==== PREFILTER PASS: Create prefilter cubemap ====
	if (_prefilterMap)
		glDeleteTextures(1, &_prefilterMap);
	glGenTextures(1, &_prefilterMap);
	glBindTexture(GL_TEXTURE_CUBE_MAP, _prefilterMap);

	constexpr int prefilterSize = 256;
	unsigned int maxMipLevels = static_cast<unsigned int>(std::log2(prefilterSize)) + 1;

	// Allocate all mip levels upfront
	for (unsigned int mip = 0; mip < maxMipLevels; ++mip)
	{
		unsigned int mipSize = static_cast<unsigned int>(prefilterSize * std::pow(0.5, mip));
		for (unsigned int i = 0; i < 6; ++i)
		{
			if (_skyBoxTextureHDRI)
				glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, mip, GL_RGB32F,
					mipSize, mipSize, 0, GL_RGB, GL_FLOAT, nullptr);
			else
				glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, mip, GL_RGB16F,
					mipSize, mipSize, 0, GL_RGB, GL_HALF_FLOAT, nullptr);
		}
	}

	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_ANISOTROPY_EXT, _anisotropicFilteringLevel);

	// Get environment map resolution for importance sampling
	GLint envMapWidth = 512;
	glBindTexture(GL_TEXTURE_CUBE_MAP, _environmentMap);
	glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_TEXTURE_WIDTH, &envMapWidth);

	// Render each mip level with fullscreen triangle
	_prefilterShader->bind();
	_prefilterShader->setUniformValue("environmentMap", 1);
	_prefilterShader->setUniformValue("environmentMapResolution", static_cast<float>(envMapWidth));

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_CUBE_MAP, _environmentMap);

	glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);

	for (unsigned int mip = 0; mip < maxMipLevels; ++mip)
	{
		unsigned int mipWidth = prefilterSize * std::pow(0.5, mip);
		unsigned int mipHeight = prefilterSize * std::pow(0.5, mip);

		// Resize renderbuffer for this mip level
		glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipWidth, mipHeight);
		glViewport(0, 0, mipWidth, mipHeight);

		// Set roughness for this mip level
		float roughness = std::max(0.04f, (float)mip / (float)(maxMipLevels - 1));
		_prefilterShader->bind();
		_prefilterShader->setUniformValue("roughness", roughness);
		_prefilterShader->setUniformValue("resolution", QVector2D(mipWidth, mipHeight));

		for (unsigned int i = 0; i < 6; ++i)
		{
			// Bind this mip level of this face to framebuffer
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, _prefilterMap, mip);

			GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
			if (fboStatus != GL_FRAMEBUFFER_COMPLETE)
			{
				qWarning() << "Prefilter FBO incomplete at mip" << mip << "face" << i
					<< "Status:" << fboStatus;
				continue;
			}

			// Set per-face basis matrix
			_prefilterShader->bind();
			setIBLFaceBasis(_prefilterShader.get(), i);

			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

			// Draw fullscreen triangle (not the cube!)
			drawFullscreenTriangle();
		}
	}

	glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());

	// PBR: generate a 2D LUT from the BRDF equations used.
	// ----------------------------------------------------
	if (_brdfLUTTexture)
		glDeleteTextures(1, &_brdfLUTTexture);
	glGenTextures(1, &_brdfLUTTexture);
	//std::cout << "GLWidget::loadIrradianceMap : _brdfLUTTexture = " << _brdfLUTTexture << std::endl;

	constexpr int lutTextureSize = 512;
	// pre-allocate enough memory for the LUT texture.
	glBindTexture(GL_TEXTURE_2D, _brdfLUTTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, lutTextureSize, lutTextureSize, 0, GL_RGB, GL_FLOAT, 0);
	// be sure to set wrapping mode to GL_CLAMP_TO_EDGE
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	// then re-configure capture framebuffer object and render screen-space quad with BRDF shader.
	glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
	glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, lutTextureSize, lutTextureSize);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _brdfLUTTexture, 0);

	glViewport(0, 0, lutTextureSize, lutTextureSize);
	_brdfShader->bind();
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	renderQuad();

	glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());

	// Bind pre-computed IBL data to texture units
	glActiveTexture(GL_TEXTURE3);
	glBindTexture(GL_TEXTURE_CUBE_MAP, _irradianceMap);
	glActiveTexture(GL_TEXTURE4);
	glBindTexture(GL_TEXTURE_CUBE_MAP, _prefilterMap);
	glActiveTexture(GL_TEXTURE5);
	glBindTexture(GL_TEXTURE_2D, _brdfLUTTexture);

	// Cleanup temporary FBO
	glDeleteFramebuffers(1, &captureFBO);
	glDeleteRenderbuffers(1, &captureRBO);
}

void GLWidget::renderSingleView(QColor& topColor, QColor& botColor)
{
	QMatrix4x4 projection;
	projection.ortho(QRect(0.0f, 0.0f, static_cast<float>(width()), static_cast<float>(height())));
	_textShader->bind();
	_textShader->setUniformValue("projection", projection);
	_textShader->release();
	glViewport(0, 0, width(), height());
	if (_shadowsEnabled)
		renderToShadowBuffer();

	if (_transmissionEnabled)
		renderToTransmissionBuffer(_primaryCamera, topColor, botColor);

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	gradientBackground(topColor.redF(), topColor.greenF(), topColor.blueF(), topColor.alphaF(),
		botColor.redF(), botColor.greenF(), botColor.blueF(), botColor.alphaF(), _gradientStyle);
	render(_primaryCamera);
	if(_userShowCornerAxisOverride)
		drawCornerAxis(_cornerAxisPosition);
}

void GLWidget::renderMultiView(QColor& topColor, QColor& botColor)
{
	glViewport(0, 0, width(), height());
	if (_shadowsEnabled)
		renderToShadowBuffer();

	if (_transmissionEnabled)
		renderToTransmissionBuffer(_primaryCamera, topColor, botColor);

	gradientBackground(topColor.redF(), topColor.greenF(), topColor.blueF(), topColor.alphaF(),
		botColor.redF(), botColor.greenF(), botColor.blueF(), botColor.alphaF(), _gradientStyle);
	// Render orthographic views with ortho view camera
	// Top View
	_orthoViewsCamera->setScreenSize(width() / 2, height() / 2);
	_orthoViewsCamera->setProjectionMatrix(_projectionMatrix);
	_orthoViewsCamera->setViewMatrix(_viewMatrix);
	_orthoViewsCamera->setPosition(_primaryCamera->getPosition());
	glViewport(0, 0, width() / 2, height() / 2);
	_orthoViewsCamera->setView(GLCamera::ViewProjection::TOP_VIEW);
	render(_orthoViewsCamera);
	_textRenderer->RenderText(_labelTop.toStdString(), -50, 5, 1.6f, QVector3D(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VTOP, TextRenderer::HAlignment::HRIGHT);

	// Front View
	glViewport(0, height() / 2, width() / 2, height() / 2);
	_orthoViewsCamera->setView(GLCamera::ViewProjection::FRONT_VIEW);
	render(_orthoViewsCamera);
	_textRenderer->RenderText(_labelFront.toStdString(), -50, 5, 1.6f, QVector3D(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VTOP, TextRenderer::HAlignment::HRIGHT);

	// Left View
	glViewport(width() / 2, height() / 2, width() / 2, height() / 2);
	_orthoViewsCamera->setView(GLCamera::ViewProjection::LEFT_VIEW);
	render(_orthoViewsCamera);
	_textRenderer->RenderText(_labelLeft.toStdString(), -50, 5, 1.6f, QVector3D(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VTOP, TextRenderer::HAlignment::HRIGHT);

	// Render isometric view with primary camera
	// Isometric View
	glViewport(width() / 2, 0, width() / 2, height() / 2);
	render(_primaryCamera);
	//std::string viewLabel = _viewMode == ViewMode::DIMETRIC ? "Dimetric" : _viewMode
		//== ViewMode::TRIMETRIC ? "Trimetric" : "Isometric";
	QString viewLabel;
	switch (_viewMode)
	{
	case ViewMode::DIMETRIC: viewLabel = _labelDimetric; break;
	case ViewMode::TRIMETRIC: viewLabel = _labelTrimetric; break;
	default: viewLabel = _labelIsometric; break;
	}
	_textRenderer->RenderText(viewLabel.toStdString(), -50, 5, 1.6f, QVector3D(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VTOP, TextRenderer::HAlignment::HRIGHT);

	// draw screen partitioning lines
	splitScreen();
}

void GLWidget::drawFloor(const bool& drawReflection)
{
	//https://open.gl/depthstencils
	glEnable(GL_STENCIL_TEST);
	glClear(GL_STENCIL_BUFFER_BIT);
	glStencilMask(0x0);
	glStencilFunc(GL_ALWAYS, 1, 0xFF);
	glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
	glStencilMask(0xFF);
	glDepthMask(GL_FALSE);
	glClear(GL_STENCIL_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_FRONT);

	// Draw floor
	_fgShader->bind();
	_fgShader->setUniformValue("envMapEnabled", false);
	_fgShader->setUniformValue("floorRendering", true);
	_fgShader->setUniformValue("isReflectedPass", true);
	_fgShader->setUniformValue("renderingMode", static_cast<int>(RenderingMode::ADS_BLINN_PHONG));
	_fgShader->setUniformValue("topColor", QVector4D(_bgTopColor.red(), _bgTopColor.green(), _bgTopColor.blue(), _bgTopColor.alpha()));
	_fgShader->setUniformValue("botColor", QVector4D(_bgBotColor.red(), _bgBotColor.green(), _bgBotColor.blue(), _bgBotColor.alpha()));
	_fgShader->setUniformValue("screenSize", QVector2D(width(), height()));
	_fgShader->setUniformValue("screenCenter", _boundingSphere.getCenter());
	_fgShader->setUniformValue("gradientStyle", _gradientStyle);
	_fgShader->setUniformValue("floorSize", _floorSize * _floorSizeFactor);
	_floorPlane->enableTexture(false);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	_floorPlane->setOpacity(0.1f);
	_floorPlane->render();
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);

	
	// Draw model reflection
	glStencilFunc(GL_EQUAL, 1, 0xFF);
	glStencilMask(0x00);
	glDepthMask(GL_TRUE);

	QMatrix4x4 model;

	// calculate zFighting offset based on model size
	float zFightingOffset = std::min((_boundingSphere.getRadius() / 100.0f), 0.001f);		
	// Position the model just below the floor plane to avoid Z-fighting
	float floorPos = lowestModelZ() - (_floorSize * _floorOffsetPercent);
	float floorGap = fabs(floorPos - lowestModelZ());
	float offset = (((lowestModelZ()) - floorGap) * 2.0f) - zFightingOffset; // Add offset to avoid Z fighting;	
	model.scale(1.0f, 1.0f, -1.0f);
	model.translate(0.0f, 0.0f, -offset);

	_fgShader->bind();
	_fgShader->setUniformValue("modelMatrix", model);
	if (_reflectionsEnabled && drawReflection)
	{
		_fgShader->setUniformValue("renderingMode", static_cast<int>(_renderingMode));
		drawMesh(_fgShader.get());
	}

	glStencilMask(0x00);
	glDisable(GL_STENCIL_TEST);
		
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_FRONT);
	_fgShader->bind();
	_fgShader->setUniformValue("envMapEnabled", false);	
	_fgShader->setUniformValue("renderingMode", static_cast<int>(RenderingMode::ADS_BLINN_PHONG));
	_fgShader->setUniformValue("shadowSamples", 18.0f);
	_fgShader->setUniformValue("isReflectedPass", false);
	_fgShader->setUniformValue("topColor", QVector4D(_bgTopColor.red(), _bgTopColor.green(), _bgTopColor.blue(), _bgTopColor.alpha()));
	_fgShader->setUniformValue("botColor", QVector4D(_bgBotColor.red(), _bgBotColor.green(), _bgBotColor.blue(), _bgBotColor.alpha()));
	_fgShader->setUniformValue("screenSize", QVector2D(width(), height()));
	_fgShader->setUniformValue("screenCenter", _boundingSphere.getCenter());
	_fgShader->setUniformValue("gradientStyle", _gradientStyle);
	_fgShader->setUniformValue("floorSize", _floorSize * _floorSizeFactor);
	_floorPlane->enableTexture(_floorTextureDisplayed);

	glActiveTexture(GL_TEXTURE5);
	glBindTexture(GL_TEXTURE_2D, _skyboxColorTexture);
	_fgShader->setUniformValue("skyboxColorTexture", 5);
	_floorPlane->setOpacity(0.95f);
	_floorPlane->render();
	glDisable(GL_CULL_FACE);
	_fgShader->bind();
	_fgShader->setUniformValue("floorRendering", false);
	_fgShader->setUniformValue("renderingMode", static_cast<int>(_renderingMode));
	glDisable(GL_BLEND);

	_fgShader->setUniformValue("envMapEnabled", _envMapEnabled);
}

void GLWidget::drawSkyBox()
{
	_skyBox->setProg(_skyBoxShader.get());
	_skyBoxShader->bind();
	_skyBoxShader->setUniformValue("skybox", _skyBoxBlurred ? 3 : 1);
	QMatrix4x4 projection;
	projection.perspective(_skyBoxFOV, (float)width() / (float)height(), 0.1f, 100.0f);
	QMatrix4x4 view = _viewMatrix;
	// Remove translation
	view.setColumn(3, QVector4D(0, 0, 0, 1));
	QMatrix4x4 model;
	if(!_skyBoxBlurred) 
		model.rotate(90.0f, QVector3D(1.0f, 0.0f, 0.0f));
	_skyBoxShader->setUniformValue("modelMatrix", model);
	_skyBoxShader->setUniformValue("viewMatrix", view);
	_skyBoxShader->setUniformValue("projectionMatrix", projection);
	_skyBoxShader->setUniformValue("hdrToneMapping", _hdrToneMapping);
	_skyBoxShader->setUniformValue("gammaCorrection", _gammaCorrection);
	_skyBoxShader->setUniformValue("screenGamma", _screenGamma);
	_skyBoxShader->setUniformValue("envMapExposure", _envMapExposure);
	_skyBoxShader->setUniformValue("iblExposure", _iblExposure);
	_skyBoxShader->setUniformValue("toneMapMode", static_cast<int>(_toneMappingMode));
	
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL); // change depth function so depth test passes when values are equal to depth buffer's content
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	_skyBox->render();
	glDepthFunc(GL_LESS); // set depth function back to default
	glDisable((GL_DEPTH_TEST));
}

void GLWidget::drawMesh(QOpenGLShaderProgram* prog, int activeCapPlaneIndex)
{
	QVector3D camPos = _primaryCamera->getPosition();
	setupClippingUniforms(prog, camPos);

	if (_meshStore.empty()) return;

	const std::vector<int>& objectIds = _visibleSwapped ? _hiddenObjectsIds : _displayedObjectsIds;

	// Split — applying cap-plane straddle culling during collection
	std::vector<int> opaqueIds;
	std::vector<std::pair<float, int>> transparent; // (distance, id)

	opaqueIds.reserve(objectIds.size());
	transparent.reserve(objectIds.size());

	for (int id : objectIds)
	{
		if (auto* mesh = _meshStore.at(id))
		{
			// Capping stencil pass: skip meshes outside frustum or that don't
			// intersect the active cap plane — they contribute nothing to stencil.
			if (activeCapPlaneIndex >= 0)
			{
				if (isMeshOutsideFrustum(mesh)) continue;
				if (!isMeshStraddlesCapPlane(mesh, activeCapPlaneIndex)) continue;
			}

			if (mesh->isTransparent())
			{
				// Use a stable distance metric (camera -> mesh bounds center in world space)
				const QVector3D c = mesh->getBoundingSphere().getCenter();   // return center in world space
				const float R = mesh->getBoundingSphere().getRadius();
				const float d = (c - camPos).length();     // squared is fine for sorting
				// farthest point distance
				float farthest = d + R;
				transparent.emplace_back(farthest, id);
			}
			else
			{
				opaqueIds.push_back(id);
			}
		}
	}

	// 1) OPAQUE PASS: depth test ON, depth writes ON, blending OFF
	glDisable(GL_BLEND);
	glDepthMask(GL_TRUE);

	for (int id : opaqueIds)
	{
		if (auto* mesh = _meshStore.at(id))
		{
			mesh->setProg(prog);
			//mesh->render();             // render must NOT disable depth writes here
			renderMeshWithDisplayMode(mesh, _displayMode);
		}
	}

	// 2) TRANSPARENT PASS: depth test ON, depth writes OFF, blending ON
	//    sort BACK-TO-FRONT (farthest first)
	std::sort(transparent.begin(), transparent.end(),
		[](const auto& a, const auto& b) { return a.first > b.first; });

	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_TRUE);

	for (auto& it : transparent)
	{
		if (auto* mesh = _meshStore.at(it.second))
		{
			mesh->setProg(prog);
			//mesh->render();             // render must preserve writes-off for this pass
			renderMeshWithDisplayMode(mesh, _displayMode);
		}
	}

	// restore baseline
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
}

void GLWidget::drawOpaqueMeshes(QOpenGLShaderProgram* prog, int activeClipPlaneIndex)
{
	QVector3D camPos = _primaryCamera->getPosition();
	setupClippingUniforms(prog, camPos);

	if (_meshStore.empty()) return;
	const std::vector<int>& objectIds = _visibleSwapped ? _hiddenObjectsIds : _displayedObjectsIds;

	glDisable(GL_BLEND);
	glDepthMask(GL_TRUE);

	// Bind shader and set uniforms that are identical for every opaque mesh once,
	// outside the loop, to avoid redundant driver calls per draw.
	prog->bind();
	// Suppress hover highlighting while Ctrl is held — avoids flashes during
	// Ctrl+drag view manipulation as the pointer crosses mesh boundaries.
	const bool ctrlHeld = QGuiApplication::queryKeyboardModifiers() & Qt::ControlModifier;
	const bool hoverHighlightingEnabled = !ctrlHeld &&
		(_selectionManager->getHoverMode() != HoverHighlightMode::Disabled);
	prog->setUniformValue("hoverHighlighting", hoverHighlightingEnabled);
	prog->setUniformValue("hoverColor", QVector3D(1.0f, 0.84f, 0.0f));

	// Collect visible opaque meshes, then sort by texture signature to
	// minimise GPU texture state changes across consecutive draw calls.
	std::vector<std::pair<uint64_t, int>> opaque;
	opaque.reserve(objectIds.size());
	for (int id : objectIds)
	{
		if (auto* mesh = _meshStore.at(id))
			if (!mesh->isTransparent() && isMeshVisible(mesh, activeClipPlaneIndex))
				opaque.emplace_back(mesh->getTextureSortKey(), id);
	}
	std::sort(opaque.begin(), opaque.end(),
		[](const auto& a, const auto& b) { return a.first < b.first; });

	for (auto& [key, id] : opaque)
	{
		if (auto* mesh = _meshStore.at(id))
		{
			mesh->setProg(prog);
			// Re-bind before setting the per-mesh varying uniform and drawing.
			// renderMeshWithDisplayMode may internally rebind a different shader
			// (e.g. wireframe overlay), so prog must be current here.
			prog->bind();
			prog->setUniformValue("hovered",
				hoverHighlightingEnabled && id == _selectionManager->getHoveredId());
			renderMeshWithDisplayMode(mesh, _displayMode);
		}
	}
}


void GLWidget::drawTransparentMeshes(QOpenGLShaderProgram* prog, int activeClipPlaneIndex)
{
	QVector3D camPos = _primaryCamera->getPosition();
	setupClippingUniforms(prog, camPos);

	if (_meshStore.empty()) return;
	const std::vector<int>& objectIds = _visibleSwapped ? _hiddenObjectsIds : _displayedObjectsIds;

	std::vector<std::pair<float, int>> transparent;
	transparent.reserve(objectIds.size());

	for (int id : objectIds)
	{
		if (auto* mesh = _meshStore.at(id))
		{
			if (mesh->isTransparent())
			{
				if (!isMeshVisible(mesh, activeClipPlaneIndex)) continue;
				// Use a stable distance metric (camera -> mesh bounds center in world space)
				const QVector3D c = mesh->getBoundingSphere().getCenter();   // return center in world space
				const float R = mesh->getBoundingSphere().getRadius();
				const float d = (c - camPos).length();     // squared is fine for sorting
				// farthest point distance
				float farthest = d + R;
				transparent.emplace_back(farthest, id);
			}
		}
	}

	// Sort far-to-near
	std::sort(transparent.begin(), transparent.end(),
		[](const auto& a, const auto& b) { return a.first > b.first; });

	glEnable(GL_BLEND);
	glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA,
		GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	// Bind once and set uniforms constant across all transparent meshes
	prog->bind();
	const bool ctrlHeldT = QGuiApplication::queryKeyboardModifiers() & Qt::ControlModifier;
	const bool hoverHighlightingEnabledT = !ctrlHeldT &&
		(_selectionManager->getHoverMode() != HoverHighlightMode::Disabled);
	prog->setUniformValue("hoverHighlighting", hoverHighlightingEnabledT);
	prog->setUniformValue("hoverColor", QVector3D(1.0f, 0.84f, 0.0f));

	for (auto& it : transparent)
	{
		if (auto* mesh = _meshStore.at(it.second))
		{
			const int id = it.second;
			mesh->setProg(prog);
			prog->bind();
			prog->setUniformValue("hovered",
				hoverHighlightingEnabledT && id == _selectionManager->getHoveredId());
			renderMeshWithDisplayMode(mesh, _displayMode);
		}
	}

	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
}

// ---------------------------------------------------------------------------
// Visibility culling helpers
// ---------------------------------------------------------------------------

void GLWidget::extractFrustumPlanes()
{
	// Gribb-Hartmann method: rows of the combined VP matrix define the 6 frustum
	// planes in world space. Vertices are already in world space (no model matrix),
	// so VP = projectionMatrix * viewMatrix suffices.
	const QMatrix4x4 vp = _projectionMatrix * _viewMatrix;
	const QVector4D r0 = vp.row(0);
	const QVector4D r1 = vp.row(1);
	const QVector4D r2 = vp.row(2);
	const QVector4D r3 = vp.row(3);

	_frustumPlanes[0] = r3 + r0;  // left
	_frustumPlanes[1] = r3 - r0;  // right
	_frustumPlanes[2] = r3 + r1;  // bottom
	_frustumPlanes[3] = r3 - r1;  // top
	_frustumPlanes[4] = r3 + r2;  // near
	_frustumPlanes[5] = r3 - r2;  // far

	// Normalize so that w is a true signed world-space distance
	for (int i = 0; i < 6; ++i)
	{
		const float len = QVector3D(_frustumPlanes[i].x(), _frustumPlanes[i].y(), _frustumPlanes[i].z()).length();
		if (len > 1e-6f)
			_frustumPlanes[i] /= len;
	}
}

bool GLWidget::isMeshOutsideFrustum(const TriangleMesh* mesh) const
{
	const BoundingBox& bb = mesh->getBoundingBox();
	for (int i = 0; i < 6; ++i)
	{
		const QVector4D& p = _frustumPlanes[i];
		// Support point: AABB corner furthest along the plane's inward normal.
		// If even this corner is on the outside (negative side), the whole AABB is outside.
		const float sx = p.x() >= 0.0f ? static_cast<float>(bb.xMax()) : static_cast<float>(bb.xMin());
		const float sy = p.y() >= 0.0f ? static_cast<float>(bb.yMax()) : static_cast<float>(bb.yMin());
		const float sz = p.z() >= 0.0f ? static_cast<float>(bb.zMax()) : static_cast<float>(bb.zMin());
		if (p.x() * sx + p.y() * sy + p.z() * sz + p.w() < 0.0f)
			return true;
	}
	return false;
}

bool GLWidget::isMeshFullyClipped_X(const TriangleMesh* mesh) const
{
	// ClipPlaneX (YZ plane): world-space threshold = _clipXCoeff + scene centre X.
	// Not flipped: keep side with x <= threshold → fully clipped when xMin > threshold.
	// Flipped:     keep side with x >= threshold → fully clipped when xMax < threshold.
	const float tx = _clipXCoeff + static_cast<float>(_boundingBox.center().getX());
	const BoundingBox& bb = mesh->getBoundingBox();
	return _clipXFlipped
		? static_cast<float>(bb.xMax()) < tx
		: static_cast<float>(bb.xMin()) > tx;
}

bool GLWidget::isMeshFullyClipped_Y(const TriangleMesh* mesh) const
{
	const float ty = _clipYCoeff + static_cast<float>(_boundingBox.center().getY());
	const BoundingBox& bb = mesh->getBoundingBox();
	return _clipYFlipped
		? static_cast<float>(bb.yMax()) < ty
		: static_cast<float>(bb.yMin()) > ty;
}

bool GLWidget::isMeshFullyClipped_Z(const TriangleMesh* mesh) const
{
	const float tz = _clipZCoeff + static_cast<float>(_boundingBox.center().getZ());
	const BoundingBox& bb = mesh->getBoundingBox();
	return _clipZFlipped
		? static_cast<float>(bb.zMax()) < tz
		: static_cast<float>(bb.zMin()) > tz;
}

bool GLWidget::isMeshFullyKept_X(const TriangleMesh* mesh) const
{
	// Entire mesh is on the KEEP side of the YZ clip plane — no intersection with the cap.
	// Not flipped: keep side is x <= threshold → fully kept when xMax <= threshold.
	// Flipped:     keep side is x >= threshold → fully kept when xMin >= threshold.
	const float tx = _clipXCoeff + static_cast<float>(_boundingBox.center().getX());
	const BoundingBox& bb = mesh->getBoundingBox();
	return _clipXFlipped
		? static_cast<float>(bb.xMin()) >= tx
		: static_cast<float>(bb.xMax()) <= tx;
}

bool GLWidget::isMeshFullyKept_Y(const TriangleMesh* mesh) const
{
	const float ty = _clipYCoeff + static_cast<float>(_boundingBox.center().getY());
	const BoundingBox& bb = mesh->getBoundingBox();
	return _clipYFlipped
		? static_cast<float>(bb.yMin()) >= ty
		: static_cast<float>(bb.yMax()) <= ty;
}

bool GLWidget::isMeshFullyKept_Z(const TriangleMesh* mesh) const
{
	const float tz = _clipZCoeff + static_cast<float>(_boundingBox.center().getZ());
	const BoundingBox& bb = mesh->getBoundingBox();
	return _clipZFlipped
		? static_cast<float>(bb.zMin()) >= tz
		: static_cast<float>(bb.zMax()) <= tz;
}

bool GLWidget::isMeshStraddlesCapPlane(const TriangleMesh* mesh, int planeIndex) const
{
	// A mesh must straddle the cap plane to contribute to the stencil count.
	// Fully clipped (discard side) or fully kept (keep side) → no cap intersection → skip.
	switch (planeIndex)
	{
	case 0: return !isMeshFullyClipped_X(mesh) && !isMeshFullyKept_X(mesh);
	case 1: return !isMeshFullyClipped_Y(mesh) && !isMeshFullyKept_Y(mesh);
	case 2: return !isMeshFullyClipped_Z(mesh) && !isMeshFullyKept_Z(mesh);
	default: return true;
	}
}

bool GLWidget::isMeshInvisibleInAllClipPasses(const TriangleMesh* mesh) const
{
	// Returns true only when every enabled clip plane fully clips this mesh,
	// meaning it contributes nothing to any pass of the union rendering.
	// If ANY enabled plane does NOT fully clip the mesh, it is visible in that pass.
	if (_clipYZEnabled && !isMeshFullyClipped_X(mesh)) return false;
	if (_clipZXEnabled && !isMeshFullyClipped_Y(mesh)) return false;
	if (_clipXYEnabled && !isMeshFullyClipped_Z(mesh)) return false;
	return true;
}

bool GLWidget::isMeshVisible(const TriangleMesh* mesh, int activeClipPlaneIndex) const
{
	// 1. Frustum cull — applied in every pass, clipping or not
	if (isMeshOutsideFrustum(mesh)) return false;

	// 2. No clip planes in this pass → frustum result is final
	if (activeClipPlaneIndex < 0) return true;

	// 3. Pre-pass elimination: if ALL active planes fully clip this mesh it is
	//    invisible across every union pass — skip it entirely
	if (isMeshInvisibleInAllClipPasses(mesh)) return false;

	// 4. Per-pass cull: skip if fully clipped by the one plane active in this pass
	if (activeClipPlaneIndex == 0 && isMeshFullyClipped_X(mesh)) return false;
	if (activeClipPlaneIndex == 1 && isMeshFullyClipped_Y(mesh)) return false;
	if (activeClipPlaneIndex == 2 && isMeshFullyClipped_Z(mesh)) return false;

	return true;
}

// ---------------------------------------------------------------------------

void GLWidget::drawMeshesWithClipping(QOpenGLShaderProgram* prog,
	bool transparentPass)
{
	//glPolygonMode(GL_FRONT_AND_BACK, _displayMode == DisplayMode::WIREFRAME ? GL_LINE : GL_FILL);
	//glLineWidth(_displayMode == DisplayMode::WIREFRAME ? 1.25 : 1.0);

	// https://stackoverflow.com/questions/16901829/how-to-clip-only-intersection-not-union-of-clipping-planes
	// If any clipping is active
	if (_clipYZEnabled || _clipZXEnabled || _clipXYEnabled)
	{
		// Then draw meshes with clip planes enabled.
		// Each pass activates one plane to produce the union of all half-spaces.
		// activeClipPlaneIndex (0/1/2) tells the draw functions which single plane
		// is active so per-pass AABB culling tests only that plane.
		if (_clipYZEnabled)
		{
			glEnable(GL_CLIP_DISTANCE0);
			if (transparentPass) drawTransparentMeshes(prog, 0);
			else                 drawOpaqueMeshes(prog, 0);
			glDisable(GL_CLIP_DISTANCE0);
		}
		if (_clipZXEnabled)
		{
			glEnable(GL_CLIP_DISTANCE1);
			if (transparentPass) drawTransparentMeshes(prog, 1);
			else                 drawOpaqueMeshes(prog, 1);
			glDisable(GL_CLIP_DISTANCE1);
		}
		if (_clipXYEnabled)
		{
			glEnable(GL_CLIP_DISTANCE2);
			if (transparentPass) drawTransparentMeshes(prog, 2);
			else                 drawOpaqueMeshes(prog, 2);
			glDisable(GL_CLIP_DISTANCE2);
		}
	}
	else
	{
		// No clipping at all — frustum culling only (activeClipPlaneIndex = -1)
		if (transparentPass) drawTransparentMeshes(prog);
		else                 drawOpaqueMeshes(prog);
	}
}


void GLWidget::setCommonUniforms(QOpenGLShaderProgram* prog, GLCamera* camera)
{
	QVector3D camPos = camera->getPosition();
	QVector3D camDir = camera->getViewDir();

	prog->setUniformValue("lightSource.position",
		_lightPosition + QVector3D(_lightOffsetX, _lightOffsetY, _lightOffsetZ));
	prog->setUniformValue("modelViewMatrix", _modelViewMatrix);
	prog->setUniformValue("normalMatrix", _modelViewMatrix.normalMatrix());
	prog->setUniformValue("projectionMatrix", _projectionMatrix);
	prog->setUniformValue("viewportMatrix", _viewportMatrix);

	QVector3D zDir(0.0, 0.0, 1.0);
	QVector3D viewDir = _primaryCamera->getViewDir();
	bool floorVisible = QVector3D::dotProduct(viewDir, zDir) < 0.0f;
	bool showShadows = (_shadowsEnabled && floorVisible && !_lowResEnabled && camera == _primaryCamera);

	prog->setUniformValue("shadowsEnabled", showShadows);
	prog->setUniformValue("selfShadowsEnabled", _selfShadowsEnabled);
	prog->setUniformValue("cameraPos", camPos);
	prog->setUniformValue("cameraDir", camDir);
	prog->setUniformValue("lightPos",
		_lightPosition + QVector3D(_lightOffsetX, _lightOffsetY, _lightOffsetZ));
	prog->setUniformValue("modelMatrix", _modelMatrix);
	prog->setUniformValue("viewMatrix", _viewMatrix);
	prog->setUniformValue("lightSpaceMatrix", _lightSpaceMatrix);
	prog->setUniformValue("lightFarPlane", _lightPosition.z() + _lightOffsetZ);
	prog->setUniformValue("hdrToneMapping", _hdrToneMapping);
	prog->setUniformValue("gammaCorrection", _gammaCorrection);
	prog->setUniformValue("screenGamma", _screenGamma);
	prog->setUniformValue("envMapExposure", _envMapExposure);
	prog->setUniformValue("iblExposure", _iblExposure);
	prog->setUniformValue("toneMapMode", static_cast<int>(_toneMappingMode));	
	prog->setUniformValue("selectionHighlighting", _selectionHighlighting);

	prog->setUniformValue("transmissionFramebufferSize",
		QVector2D(_transmissionTextureWidth, _transmissionTextureHeight));

	prog->setUniformValue("useDefaultLights", _useDefaultLights);
	prog->setUniformValue("usePunctualLights", _usePunctualLights);
	prog->setUniformValue("useIBL", _useIBL);

	bindIBLTextures();
}



void GLWidget::drawSectionCapping()
{
	// We use a lightweight shader without lighting and stuff for drawing the clipped mesh
	_clippedMeshShader->bind();
	_clippedMeshShader->setUniformValue("modelMatrix", _modelMatrix);
	_clippedMeshShader->setUniformValue("viewMatrix", _viewMatrix);
	_clippedMeshShader->setUniformValue("projectionMatrix", _projectionMatrix);

	QVector3D pos = _primaryCamera->getPosition();

	_clippedMeshShader->setUniformValue("clipPlaneX", QVector4D(_modelViewMatrix.map(QVector3D(_clipXFlipped ? 1 : -1, 0, 0) + pos),
		(_clipXFlipped ? 1 : -1) * (pos.x() - (_clipXCoeff + _boundingBox.center().getX()))));
	_clippedMeshShader->setUniformValue("clipPlaneY", QVector4D(_modelViewMatrix.map(QVector3D(0, _clipYFlipped ? 1 : -1, 0) + pos),
		(_clipYFlipped ? 1 : -1) * (pos.y() - (_clipYCoeff + _boundingBox.center().getY()))));
	_clippedMeshShader->setUniformValue("clipPlaneZ", QVector4D(_modelViewMatrix.map(QVector3D(0, 0, _clipZFlipped ? 1 : -1) + pos),
		(_clipZFlipped ? 1 : -1) * (pos.z() - (_clipZCoeff + _boundingBox.center().getZ()))));
	_clippedMeshShader->setUniformValue("clipPlane", QVector4D(_modelViewMatrix.map(QVector3D(_clipDX, _clipDY, _clipDZ) + pos),
		pos.x() * _clipDX + pos.y() * _clipDY + pos.z() * _clipDZ));

	for (int i = 0; i < 3; ++i)
	{
		// Clipping Planes
		if (_clipYZEnabled && i == 0)
			glEnable(GL_CLIP_DISTANCE0);
		if (_clipZXEnabled && i == 1)
			glEnable(GL_CLIP_DISTANCE1);
		if (_clipXYEnabled && i == 2)
			glEnable(GL_CLIP_DISTANCE2);

		// https://www.opengl.org/archives/resources/code/samples/advanced/advanced97/notes/node10.html
		// https://glbook.gamedev.net/GLBOOK/glbook.gamedev.net/moglgp/advclip.html
		// https://stackoverflow.com/questions/16901829/how-to-clip-only-intersection-not-union-of-clipping-planes
		// 1) The stencil buffer, color buffer, and depth buffer are cleared,
		glClear(GL_STENCIL_BUFFER_BIT);
		glStencilMask(0x0);
		glDisable(GL_DEPTH_TEST);
		// and color buffer writes are disabled.
		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

		glEnable(GL_STENCIL_TEST);
		glStencilMask(0xFF);
		glStencilFunc(GL_ALWAYS, 0, 0);

		// 2) The capping polygon is rendered into the depth buffer,
		// drawCappingPlane

		// then depth buffer writes are disabled.
		glDepthMask(GL_FALSE);

		// 3) The stencil operation is set to increment the stencil value where the depth test passes,
		glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);

		// and the model is drawn with glCullFace(GL FRONT).
		glEnable(GL_CULL_FACE);
		glCullFace(GL_FRONT);
		drawMesh(_clippedMeshShader.get(), i);

		// 4) The stencil operation is then set to decrement the stencil value where the depth test passes,
		glStencilOp(GL_KEEP, GL_KEEP, GL_DECR);

		// and the model is drawn with glCullFace(GL BACK)
		glCullFace(GL_BACK);
		drawMesh(_clippedMeshShader.get(), i);
		glDisable(GL_CULL_FACE);

		//At this point, the stencil buffer is 1 wherever the clipping plane is enclosed by
		// the frontfacing and backfacing surfaces of the object.
		// 5) The depth buffer is cleared, color buffer writes are enabled,
		//glClear(GL_DEPTH_BUFFER_BIT);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glEnable(GL_DEPTH_TEST);

		// and the polygon representing the clipping plane is now drawn using whatever material properties are desired,
		// with the stencil function set to GL EQUAL and the reference value set to 1.
		// This draws the color and depth values of the cap into the framebuffer only where the stencil values equal 1.
		glStencilFunc(GL_EQUAL, 1, 0xFF);		
		glDepthMask(GL_TRUE);
		glEnable(GL_DEPTH_TEST);
		// drawCappingPlane
		{
			QMatrix4x4 model;
			Point P = _boundingBox.center();
			QVector3D Pxyz(P.getX(), P.getY(), P.getZ());

			_clippingPlaneShader->bind();
			_clippingPlaneShader->setUniformValue("modelMatrix", model);
			_clippingPlaneShader->setUniformValue("viewMatrix", _viewMatrix);
			_clippingPlaneShader->setUniformValue("projectionMatrix", _projectionMatrix);
			glActiveTexture(GL_TEXTURE6);
			glBindTexture(GL_TEXTURE_2D, _cappingTexture);
			_clippingPlaneShader->setUniformValue("hatchMap", 6);
			float yAng = _clipXFlipped || _clipXCoeff > 0 ? 90.0f : -90.0f;
			float xAng = _clipYFlipped || _clipYCoeff > 0 ? 90.0f : -90.0f;
			float zAng = _clipZFlipped || _clipZCoeff > 0 ? 0.0f : 180.0f;

			bool wantTexture = _hatchMode == ClippingPlaneHatchMode::TEXTURE/* read from UI or stored flag */;
			bool wantFlipU = false/* read from UI or stored flag */;
			bool wantFlipV = false/* read from UI or stored flag */;

			// Pick a consistent density: e.g., ~3 tiles across the model diagonal
			const Point c = _boundingBox.center();			
			const float sceneDiag = _boundingBox.boundingRadius() * 2.0f;
			const float tilesAcross = wantTexture ? 3.0f : _hatchTiling;
			const float worldUnitsPerTile = sceneDiag / tilesAcross;

			_clippingPlaneShader->setUniformValue("worldUnitsPerTile", worldUnitsPerTile);
			// procedural hatch params (tweak to taste)
			_clippingPlaneShader->setUniformValue("hatchThickness", _hatchThickness);
			_clippingPlaneShader->setUniformValue("hatchIntensity", _hatchIntensity);
			_clippingPlaneShader->setUniformValue("hatchLayers", _hatchLayers);
			_clippingPlaneShader->setUniformValue("hatchLineColor", _hatchLineColor);			
			_clippingPlaneShader->setUniformValue("hatchPattern", static_cast<int>(_hatchPattern));
			
			_clippingPlaneShader->setUniformValue("useTexture", wantTexture);

			// texture flip control: (1,1) normal; (-1,1) flip U; (1,-1) flip V			
			QVector2D texFlip = QVector2D(wantFlipU ? -1.0f : 1.0f, wantFlipV ? -1.0f : 1.0f);
			_clippingPlaneShader->setUniformValue("textureFlip", texFlip);

			// YZ Plane			
			model.translate(QVector3D(P.getX(), P.getY(), P.getZ()));
			model.rotate(yAng, QVector3D(0.0f, 1.0f, 0.0f));
			_clippingPlaneShader->bind();
			_clippingPlaneShader->setUniformValue("modelMatrix", model);
			_clippingPlaneShader->setUniformValue("planeColor", QVector3D(0.20f, 0.5f, 0.5f));			
			if (_clipYZEnabled && i == 0)
			{
				// Plane position along X in world space
				const float xPlane = P.getX() + _clipXCoeff;
				// Origin at plane through bbox center
				_clippingPlaneShader->setUniformValue("hatchOrigin", QVector3D(xPlane, P.getY(), P.getZ()));
				// World-space basis on the plane: U=+Y, V=+Z
				_clippingPlaneShader->setUniformValue("uDir", QVector3D(0.f, 1.f, 0.f));
				_clippingPlaneShader->setUniformValue("vDir", QVector3D(0.f, 0.f, 1.f));
				_clippingPlaneYZ->render();
			}

			// ZX Plane
			model.setToIdentity();
			model.translate(QVector3D(P.getX(), P.getY(), P.getZ()));
			model.rotate(xAng, QVector3D(1.0f, 0.0f, 0.0f));
			_clippingPlaneShader->bind();
			_clippingPlaneShader->setUniformValue("modelMatrix", model);
			_clippingPlaneShader->setUniformValue("planeColor", QVector3D(0.5f, 0.20f, 0.5f));
			if (_clipZXEnabled && i == 1)
			{
				const float yPlane = P.getY() + _clipYCoeff;
				_clippingPlaneShader->setUniformValue("hatchOrigin", QVector3D(P.getX(), yPlane, P.getZ()));
				// U=+Z, V=+X
				_clippingPlaneShader->setUniformValue("uDir", QVector3D(0.f, 0.f, 1.f));
				_clippingPlaneShader->setUniformValue("vDir", QVector3D(1.f, 0.f, 0.f));
				_clippingPlaneZX->render();
			}

			// XY Plane
			model.setToIdentity();
			model.translate(QVector3D(P.getX(), P.getY(), P.getZ()));
			model.rotate(zAng, QVector3D(1.0f, 0.0f, 0.0f));
			_clippingPlaneShader->bind();
			_clippingPlaneShader->setUniformValue("modelMatrix", model);
			_clippingPlaneShader->setUniformValue("planeColor", QVector3D(0.5f, 0.5f, 0.20f));
			if (_clipXYEnabled && i == 2)
			{
				const float zPlane = P.getZ() + _clipZCoeff;
				_clippingPlaneShader->setUniformValue("hatchOrigin", QVector3D(P.getX(), P.getY(), zPlane));
				// U=+X, V=+Y
				_clippingPlaneShader->setUniformValue("uDir", QVector3D(1.f, 0.f, 0.f));
				_clippingPlaneShader->setUniformValue("vDir", QVector3D(0.f, 1.f, 0.f));
				_clippingPlaneXY->render();
			}
		}

		// Clipping Planes
		if (_clipYZEnabled && i == 0)
			glDisable(GL_CLIP_DISTANCE0);
		if (_clipZXEnabled && i == 1)
			glDisable(GL_CLIP_DISTANCE1);
		if (_clipXYEnabled && i == 2)
			glDisable(GL_CLIP_DISTANCE2);
	}

	// 6) Finally, stenciling is disabled, the OpenGL clipping plane is applied, and the
	// clipped object is drawn with color and depth enabled.
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_CULL_FACE);	
}

void GLWidget::drawVertexNormals()
{
	QVector3D pos = _primaryCamera->getPosition();
	setupClippingUniforms(_vertexNormalShader.get(), pos);

	if (_meshStore.size() != 0)
	{
		for (int i : (_visibleSwapped ? _hiddenObjectsIds : _displayedObjectsIds))
		{
			if (_showVertexNormals)
			{
				TriangleMesh* mesh = _meshStore.at(i);
				mesh->setProg(_vertexNormalShader.get());
				mesh->getVAO().bind();
				glDrawElements(GL_TRIANGLES, static_cast<int>(mesh->getPoints().size()), GL_UNSIGNED_INT, 0);
				mesh->getVAO().release();
			}
		}
	}
}

void GLWidget::drawFaceNormals()
{
	QVector3D pos = _primaryCamera->getPosition();
	setupClippingUniforms(_faceNormalShader.get(), pos);

	if (_meshStore.size() != 0)
	{
		for (int i : (_visibleSwapped ? _hiddenObjectsIds : _displayedObjectsIds))
		{
			if (_showFaceNormals)
			{
				TriangleMesh* mesh = _meshStore.at(i);
				mesh->setProg(_faceNormalShader.get());
				mesh->getVAO().bind();
				glDrawElements(GL_TRIANGLES, static_cast<int>(mesh->getPoints().size()), GL_UNSIGNED_INT, 0);
				mesh->getVAO().release();
			}
		}
	}
}

void GLWidget::drawAxis()
{
	float size = 15;
	// Labels
	QVector3D xAxis(_viewRange / size, 0, 0);
	xAxis = xAxis.project(_modelViewMatrix, _projectionMatrix, QRect(0, 0, width(), height()));
	_axisTextRenderer->RenderText(_labelAxisX.toStdString(), xAxis.x(), height() - xAxis.y(), 1, QVector3D(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VBOTTOM);

	QVector3D yAxis(0, _viewRange / size, 0);
	yAxis = yAxis.project(_modelViewMatrix, _projectionMatrix, QRect(0, 0, width(), height()));
	_axisTextRenderer->RenderText(_labelAxisY.toStdString(), yAxis.x(), height() - yAxis.y(), 1, QVector3D(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VBOTTOM);

	QVector3D zAxis(0, 0, _viewRange / size);
	zAxis = zAxis.project(_modelViewMatrix, _projectionMatrix, QRect(0, 0, width(), height()));
	_axisTextRenderer->RenderText(_labelAxisZ.toStdString(), zAxis.x(), height() - zAxis.y(), 1, QVector3D(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VBOTTOM);

	// Axes Lines
	if (!_axisVAO.isCreated())
	{
		_axisVAO.create();
		_axisVAO.bind();
	}

	// Vertex Buffer
	if (!_axisVBO.isCreated())
	{
		_axisVBO = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
		_axisVBO.create();
	}
	_axisVBO.bind();
	_axisVBO.setUsagePattern(QOpenGLBuffer::StaticDraw);
	std::vector<float> vertices = {
		0, 0, 0,
		_viewRange / size, 0, 0,
		0, 0, 0,
		0, _viewRange / size, 0,
		0, 0, 0,
		0, 0, _viewRange / size };
	_axisVBO.allocate(vertices.data(), static_cast<int>(vertices.size() * sizeof(float)));

	// Color Buffer
	if (!_axisCBO.isCreated())
	{
		_axisCBO = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
		_axisCBO.create();
	}
	_axisCBO.bind();
	_axisCBO.setUsagePattern(QOpenGLBuffer::StaticDraw);
	std::vector<float> colors = {
		1, 0, 0,
		1, 0, 0,
		0, 1, 0,
		0, 1, 0,
		0, 0, 1,
		0, 0, 1 };
	_axisCBO.allocate(colors.data(), static_cast<int>(colors.size() * sizeof(float)));

	_axisShader->bind();

	_axisVBO.bind();
	_axisShader->enableAttributeArray("vertexPosition");
	_axisShader->setAttributeBuffer("vertexPosition", GL_FLOAT, 0, 3);

	_axisCBO.bind();
	_axisShader->enableAttributeArray("vertexColor");
	_axisShader->setAttributeBuffer("vertexColor", GL_FLOAT, 0, 3);

	_axisShader->setUniformValue("modelViewMatrix", _modelViewMatrix);
	_axisShader->setUniformValue("projectionMatrix", _projectionMatrix);

	_axisShader->setUniformValue("renderCone", false);

	_axisVAO.bind();
	glLineWidth(2.5);
	glDrawArrays(GL_LINES, 0, 6);
	glLineWidth(1);

	// Axes Cones
	// X Axis
	_axisCone->setParameters(_viewRange / size / 15, _viewRange / size / 5, 8.0f, 1.0f);
	_axisShader->setUniformValue("renderCone", true);
	QMatrix4x4 model;
	model.translate(_viewRange / size, 0, 0);
	model.rotate(90, QVector3D(0, 1.0f, 0));
	_axisShader->setUniformValue("coneColor", QVector3D(1.0f, 0.0, 0.0));
	_axisShader->setUniformValue("modelViewMatrix", _viewMatrix * model);
	_axisCone->getVAO().bind();
	glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(_axisCone->getPoints().size()), GL_UNSIGNED_INT, 0);
	_axisCone->getVAO().release();

	// Y Axis
	model.setToIdentity();
	model.translate(0, _viewRange / size, 0);
	model.rotate(90, QVector3D(-1.0f, 0, 0));
	_axisShader->bind();
	_axisShader->setUniformValue("coneColor", QVector3D(0.0, 1.0f, 0.0));
	_axisShader->setUniformValue("modelViewMatrix", _viewMatrix * model);
	_axisCone->getVAO().bind();
	glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(_axisCone->getPoints().size()), GL_UNSIGNED_INT, 0);
	_axisCone->getVAO().release();

	// Z Axis
	model.setToIdentity();
	model.translate(0, 0, _viewRange / size);
	_axisShader->bind();
	_axisShader->setUniformValue("coneColor", QVector3D(0.0, 0.0, 1.0f));
	_axisShader->setUniformValue("modelViewMatrix", _viewMatrix * model);
	_axisCone->getVAO().bind();
	glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(_axisCone->getPoints().size()), GL_UNSIGNED_INT, 0);
	_axisCone->getVAO().release();

	_axisVAO.release();
	_axisShader->release();
}

void GLWidget::drawCornerAxis(CornerAxisPosition position)
{
	int viewportX = 0;
	int viewportY = 0;

	// Determine the viewport position based on the CornerAxisPosition
	switch (position)
	{
	case CornerAxisPosition::TOP_LEFT:
		viewportX = 0;
		viewportY = height() - height() / 10;
		break;
	case CornerAxisPosition::TOP_RIGHT:
		viewportX = width() - width() / 10;
		viewportY = height() - height() / 10;
		break;
	case CornerAxisPosition::BOTTOM_LEFT:
		viewportX = 0;
		viewportY = 0;
		break;
	case CornerAxisPosition::BOTTOM_RIGHT:
		viewportX = width() - width() / 10;
		viewportY = 0;
		break;
	}

	// Set the viewport for the corner axis
	glViewport(viewportX, viewportY, width() / 10, height() / 10);

	QMatrix4x4 mat = _modelViewMatrix;
	mat.setColumn(3, QVector4D(0, 0, 0, 1));
	mat.setRow(3, QVector4D(0, 0, 0, 1));

	float size = 3.5;

	// Labels
	QVector3D xAxis(_viewRange / size, 0, 0);
	xAxis = xAxis.project(mat, _projectionMatrix, QRect(0, 0, width(), height()));
	_axisTextRenderer->RenderText(_labelAxisX.toStdString(), xAxis.x(), height() - xAxis.y(), 7, QVector3D(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VBOTTOM);

	QVector3D yAxis(0, _viewRange / size, 0);
	yAxis = yAxis.project(mat, _projectionMatrix, QRect(0, 0, width(), height()));
	_axisTextRenderer->RenderText(_labelAxisY.toStdString(), yAxis.x(), height() - yAxis.y(), 7, QVector3D(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VBOTTOM);

	QVector3D zAxis(0, 0, _viewRange / size);
	zAxis = zAxis.project(mat, _projectionMatrix, QRect(0, 0, width(), height()));
	_axisTextRenderer->RenderText(_labelAxisZ.toStdString(), zAxis.x(), height() - zAxis.y(), 7, QVector3D(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VBOTTOM);

	// Axes
	if (!_axisVAO.isCreated())
	{
		_axisVAO.create();
		_axisVAO.bind();
	}

	// Vertex Buffer
	if (!_axisVBO.isCreated())
	{
		_axisVBO = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
		_axisVBO.create();
	}
	_axisVBO.bind();
	_axisVBO.setUsagePattern(QOpenGLBuffer::StaticDraw);
	std::vector<float> vertices = {
		0, 0, 0,
		_viewRange / size, 0, 0,
		0, 0, 0,
		0, _viewRange / size, 0,
		0, 0, 0,
		0, 0, _viewRange / size };
	_axisVBO.allocate(vertices.data(), static_cast<int>(vertices.size() * sizeof(float)));

	// Color Buffer
	if (!_axisCBO.isCreated())
	{
		_axisCBO = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
		_axisCBO.create();
	}
	_axisCBO.bind();
	_axisCBO.setUsagePattern(QOpenGLBuffer::StaticDraw);
	std::vector<float> colors = {
		1, 1, 1,
		1, 1, 1,
		1, 1, 1,
		1, 1, 1,
		1, 1, 1,
		1, 1, 1 };
	_axisCBO.allocate(colors.data(), static_cast<int>(colors.size() * sizeof(float)));

	_axisShader->bind();

	_axisVBO.bind();
	_axisShader->enableAttributeArray("vertexPosition");
	_axisShader->setAttributeBuffer("vertexPosition", GL_FLOAT, 0, 3);

	_axisCBO.bind();
	_axisShader->enableAttributeArray("vertexColor");
	_axisShader->setAttributeBuffer("vertexColor", GL_FLOAT, 0, 3);

	_axisShader->setUniformValue("modelViewMatrix", mat);
	_axisShader->setUniformValue("projectionMatrix", _projectionMatrix);

	_axisShader->setUniformValue("renderCone", false);

	_axisVAO.bind();
	glLineWidth(2.0);
	glDrawArrays(GL_LINES, 0, 6);
	glLineWidth(1);

	// Axes Cones
	// X Axis
	_axisCone->setParameters(_viewRange / size / 15, _viewRange / size / 5, 8.0f, 1.0f);
	_axisShader->setUniformValue("renderCone", true);
	mat.translate(_viewRange / size, 0, 0);
	mat.rotate(90, QVector3D(0, 1.0f, 0));
	_axisShader->bind();
	_axisShader->setUniformValue("coneColor", QVector3D(1.0f, 1.0f, 1.0f));
	_axisShader->setUniformValue("modelViewMatrix", mat);
	_axisCone->getVAO().bind();
	glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(_axisCone->getPoints().size()), GL_UNSIGNED_INT, 0);
	_axisCone->getVAO().release();

	// Y Axis
	mat = _modelViewMatrix;
	mat.setColumn(3, QVector4D(0, 0, 0, 1));
	mat.setRow(3, QVector4D(0, 0, 0, 1));
	mat.translate(0, _viewRange / size, 0);
	mat.rotate(90, QVector3D(-1.0f, 0, 0));
	_axisShader->bind();
	_axisShader->setUniformValue("modelViewMatrix", mat);
	_axisCone->getVAO().bind();
	glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(_axisCone->getPoints().size()), GL_UNSIGNED_INT, 0);
	_axisCone->getVAO().release();

	// Z Axis
	mat = _modelViewMatrix;
	mat.setColumn(3, QVector4D(0, 0, 0, 1));
	mat.setRow(3, QVector4D(0, 0, 0, 1));
	mat.translate(0, 0, _viewRange / size);
	_axisShader->bind();
	_axisShader->setUniformValue("modelViewMatrix", mat);
	_axisCone->getVAO().bind();
	glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(_axisCone->getPoints().size()), GL_UNSIGNED_INT, 0);
	_axisCone->getVAO().release();

	_axisVAO.release();
	_axisShader->release();

	glViewport(0, 0, width(), height());
}

void GLWidget::drawLights()
{
	QMatrix4x4 model;
	model.translate(_lightPosition + QVector3D(_lightOffsetX, _lightOffsetY, _lightOffsetZ));
	_lightCubeShader->bind();
	_lightCubeShader->setUniformValue("modelMatrix", model);
	QMatrix4x4 viewMat = _viewMatrix;	
	_lightCubeShader->setUniformValue("viewMatrix", viewMat);
	_lightCubeShader->setUniformValue("projectionMatrix", _projectionMatrix);
	_lightCubeShader->setUniformValue("lightColor", _diffuseLight.toVector3D());	
	_lightCubeShader->setUniformValue("intensity", 1.0f);
	_lightCubeShader->setUniformValue("intensityScale", 1.0f);  // Tune brightness
	_lightCube->render();

	// Draw punctual lights
	if (!_currentRepositionedLights.empty())
	{
		for (const auto& light : _currentRepositionedLights)
		{
			// === Apply intensity with log scale ===
			float normalizedIntensity = std::log10(light.intensity + 1.0f);
			normalizedIntensity = std::min(normalizedIntensity, 3.0f);

			// Multiply color * intensity in C++
			glm::vec3 emissiveColor = light.color * normalizedIntensity;

			QMatrix4x4 lightModel;
			lightModel.translate(light.position.x, light.position.y, light.position.z);
			lightModel.scale(_boundingSphere.getRadius() * 0.05f);

			_lightCubeShader->bind();
			_lightCubeShader->setUniformValue("modelMatrix", lightModel);
			_lightCubeShader->setUniformValue("viewMatrix", viewMat);
			_lightCubeShader->setUniformValue("projectionMatrix", _projectionMatrix);
			_lightCubeShader->setUniformValue("lightColor",
				QVector3D(light.color.x, light.color.y, light.color.z));
			_lightCubeShader->setUniformValue("intensity", normalizedIntensity);
			_lightCubeShader->setUniformValue("intensityScale", 1.0f);  // Tune brightness

			_lightSphere->render();
		}
	}
}

void GLWidget::bindIBLTextures()
{
	_fgShader->setUniformValue("irradianceMap", 3);
	glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_CUBE_MAP, _irradianceMap);
	_fgShader->setUniformValue("prefilterMap", 4);
	glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_CUBE_MAP, _prefilterMap);
	_fgShader->setUniformValue("brdfLUTTexture", 5);
	glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_2D, _brdfLUTTexture);
}


void GLWidget::render(GLCamera* camera)
{
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glFrontFace(GL_CCW);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glDepthMask(GL_TRUE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glStencilMask(0xFF);
	glDisable(GL_STENCIL_TEST);

	_viewMatrix.setToIdentity();
	_viewMatrix = camera->getViewMatrix();
	_projectionMatrix = camera->getProjectionMatrix();
	_modelViewMatrix = _viewMatrix * _modelMatrix;

	// Extract frustum planes once per frame for AABB culling
	extractFrustumPlanes();

	// --- 1) Skybox ---
	if (_skyBoxEnabled)
	{
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		drawSkyBox();
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);
	}

	// --- 2) Opaque meshes (with clipping) ---
	_fgShader->bind();
	setCommonUniforms(_fgShader.get(), camera);	
	drawMeshesWithClipping(_fgShader.get(), false); // opaque pass
	_fgShader->release();

	// --- 2.5) Section caps (after opaque, before floor & transparents) ---
	if (_cappingEnabled &&
		!_sectionCapsSuppressedDuringInteraction &&
		(_clipYZEnabled || _clipZXEnabled || _clipXYEnabled))
	{
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(1.0f, 1.0f); // pull forward
		drawSectionCapping();
		glDisable(GL_POLYGON_OFFSET_FILL);
	}

	// --- 3) Floor ---
	if (_displayMode == DisplayMode::REALSHADED &&
		_floorDisplayed && !_cappingEnabled &&
		!_meshStore.empty() &&
		camera != _orthoViewsCamera)
	{
		drawFloor();
	}

	if (camera == _primaryCamera)
	{
		// Bind transmission texture for shader sampling
		glActiveTexture(GL_TEXTURE7);  // Use a dedicated texture unit
		glBindTexture(GL_TEXTURE_2D, _transmissionColorTexture);

		glActiveTexture(GL_TEXTURE8);  // For depth-based calculations (Phase 2)
		glBindTexture(GL_TEXTURE_2D, _transmissionDepthTexture);
	}

	// --- 4) Transparent meshes (with clipping) ---
	_fgShader->bind();
	setCommonUniforms(_fgShader.get(), camera);
	drawMeshesWithClipping(_fgShader.get(), true); // transparent pass
	_fgShader->release();

	// --- 5) Overlays ---
	if (_showAxis && _userShowAxisOverride) drawAxis();
	if (_showLights) drawLights();
}



void GLWidget::renderToShadowBuffer()
{
	if (!_shadowMapNeedsInitialization)
		return;
	_shadowMapNeedsInitialization = false;

	// save current viewport
	int viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);

	/// Shadow Mapping
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glViewport(0, 0, _shadowWidth, _shadowHeight);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _shadowMapFBO);
	glClear(GL_DEPTH_BUFFER_BIT);
	glDisable(GL_CULL_FACE);

	// 1. render depth of scene to texture (from light's perspective)
	// --------------------------------------------------------------
	QMatrix4x4 lightProjection, lightView;	
	QVector3D center = _boundingSphere.getCenter();
	float radius = _boundingSphere.getRadius();
	QVector3D lightPos = _lightPosition + QVector3D(_lightOffsetX, _lightOffsetY, _lightOffsetZ);

	// Light looks at scene center
	QVector3D lightDir;
	
	lightDir = QVector3D(center.x(), center.y(), 0);	
	
	lightView.lookAt(lightPos, lightDir, QVector3D(0.0, 1.0, 0.0));

	// Use scene bounding sphere for orthographic projection
	// This ensures the frustum always encompasses the entire scene
	float orthoSize = radius * 4.0f;
	float margin = orthoSize * 3.0f; // 300% margin
	float totalSize = orthoSize + margin;

	lightProjection.ortho(
		-totalSize, totalSize,
		-totalSize, totalSize,
		-totalSize, totalSize  // Use consistent near/far planes
	);

	_lightSpaceMatrix = lightProjection * lightView;

	// render scene from light's point of view
	_shadowMappingShader->bind();
	_shadowMappingShader->setUniformValue("lightSpaceMatrix", _lightSpaceMatrix);
	_shadowMappingShader->setUniformValue("model", _modelMatrix);

	if (_meshStore.size() != 0)
	{
		for (int i : (_visibleSwapped ? _hiddenObjectsIds : _displayedObjectsIds))
		{
			try
			{
				TriangleMesh* mesh = _meshStore.at(i);
				if (mesh)
				{
					mesh->setProg(_shadowMappingShader.get());
					mesh->getVAO().bind();					
					mesh->renderShadow();
					mesh->getVAO().release();
				}
			}
			catch (const std::exception& ex)
			{
				std::cout << "Exception raised in GLWidget::renderToShadowBuffer\n" << ex.what() << std::endl;
			}
		}
	}

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, defaultFramebufferObject());
	// End Shadow Mapping
	// restore viewport
	glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

int GLWidget::processSelection(const QPoint& pixel)
{
	int id = -1;

	// Get the list of objects to render (all visible objects)
	const auto& visibleIds = _visibleSwapped ? _hiddenObjectsIds : _displayedObjectsIds;

	if (visibleIds.empty())
		return -1;

	// Note: Even with one visible object, we must perform FBO rendering and color picking
	// to determine if the click actually hit that object (vs empty space).
	// FBO rendering with depth testing will correctly return -1 if click missed.

	// Render all visible objects to FBO and perform color picking to determine topmost
	makeCurrent();

	// Validate GL context and dimensions
	if (width() <= 0 || height() <= 0)
		return -1;

	if(_selectionFBO == 0)
		glGenFramebuffers(1, &_selectionFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, _selectionFBO);
#ifdef GL_FRAMEBUFFER_DEFAULT_SAMPLES
		glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_SAMPLES, 0);
#else // MacOS
		glFramebufferParameteri(GL_FRAMEBUFFER, 0, 0);
#endif

	if(_selectionRBO == 0)
		glGenRenderbuffers(1, &_selectionRBO);
	glBindRenderbuffer(GL_RENDERBUFFER, _selectionRBO);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA, width(), height());
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _selectionRBO);
	GLenum DrawBuffers[1] = { GL_COLOR_ATTACHMENT0 };
	glDrawBuffers(1, DrawBuffers);
	if(_selectionDBO == 0)
		glGenRenderbuffers(1, &_selectionDBO);
	glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)_selectionDBO);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width(), height());
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, _selectionDBO);
	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		std::cout << "Failed to create selection framebuffer: " << status << std::endl;
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, defaultFramebufferObject());
		return -1;
	}

	// save current viewport
	int viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glViewport(0, 0, width(), height());
	glBindFramebuffer(GL_FRAMEBUFFER, _selectionFBO);
	glDrawBuffer(GL_COLOR_ATTACHMENT0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	_selectionShader->bind();
	_selectionShader->setUniformValue("projectionMatrix", _projectionMatrix);
	_selectionShader->setUniformValue("modelViewMatrix", _modelViewMatrix);

	// Render ALL visible objects to FBO (not just ray-hit ones)
	// This ensures color picking is a true fallback method, independent of ray test results
	for (int i : std::as_const(visibleIds))
	{
		try
		{
			TriangleMesh* mesh = _meshStore.at(i);
			if (mesh)
			{
				QColor pickColor = indexToColor(i + 1);
				_selectionShader->bind();

				const float r = pickColor.redF();
				const float g = pickColor.greenF();
				const float b = pickColor.blueF();
				const float a = pickColor.alphaF();

				_selectionShader->setUniformValue("pickingColor", QVector4D(r, g, b, a));
				mesh->setProg(_selectionShader.get());
				mesh->getVAO().bind();
				glDrawElements(GL_TRIANGLES, static_cast<int>(mesh->getPoints().size()), GL_UNSIGNED_INT, 0);
				mesh->getVAO().release();
				glFlush();
				glFinish();
			}
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception raised in GLWidget::renderToSelectionBuffer\n" << ex.what() << std::endl;
		}
	}

	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	int pixelWinSize = 2;

	// Calculate safe pixel read coordinates with bounds checking
	int readX = pixel.x() - pixelWinSize / 2;
	int readY = viewport[3] - pixel.y() + pixelWinSize / 2;

	// Clamp to viewport bounds
	int maxX = viewport[0] + viewport[2];
	int maxY = viewport[1] + viewport[3];

	if (readX < viewport[0]) readX = viewport[0];
	if (readY < viewport[1]) readY = viewport[1];
	if (readX + pixelWinSize > maxX) readX = maxX - pixelWinSize;
	if (readY + pixelWinSize > maxY) readY = maxY - pixelWinSize;

	// Ensure dimensions don't exceed bounds
	int readWidth = pixelWinSize;
	int readHeight = pixelWinSize;
	if (readX + readWidth > maxX) readWidth = maxX - readX;
	if (readY + readHeight > maxY) readHeight = maxY - readY;

	if (readWidth <= 0 || readHeight <= 0)
	{
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, defaultFramebufferObject());
		glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
		return -1; // Click outside viewport
	}

	std::vector<float> res(static_cast<size_t>(readWidth) * readHeight * 4);
	glReadPixels(readX, readY, readWidth, readHeight, GL_RGBA, GL_FLOAT, res.data());
	std::map<int, int> voteCount;
	for (size_t i = 0; i < res.size(); i += 4)
	{
		QColor col = QColor::fromRgbF(res[i + 0], res[i + 1], res[i + 2], res[i + 3]);
		qDebug() << "ReadPixel Color" << col;
		unsigned int colId = colorToIndex(col);
		if (colId != 0)
			voteCount[colId - 1]++;
	}
	if (!voteCount.empty())
		id = std::max_element(voteCount.begin(), voteCount.end(), voteCount.value_comp())->first;

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, defaultFramebufferObject());
	// restore viewport
	glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

	return id;
}

void GLWidget::renderQuad()
{
	if (_quadVAO == 0)
	{
		float quadVertices[] = {
			// positions        // texture Coords
			-1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
			-1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
			1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
			1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
		};
		// setup plane VAO
		glGenVertexArrays(1, &_quadVAO);
		glGenBuffers(1, &_quadVBO);
		glBindVertexArray(_quadVAO);
		glBindBuffer(GL_ARRAY_BUFFER, _quadVBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
	}
	glBindVertexArray(_quadVAO);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindVertexArray(0);
}

void GLWidget::renderMeshWithDisplayMode(TriangleMesh* mesh, DisplayMode mode)
{
	_fgShader->bind();
	GLint modelViewLoc;
	GLint prog = 0;
	switch (mode)
	{
		// ============================================
	case DisplayMode::SHADED:
	case DisplayMode::REALSHADED:
		// ============================================
		// SHADED: Solid rendering only
		// ============================================
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glLineWidth(1.0f);
		glDisable(GL_POLYGON_OFFSET_FILL);
		_fgShader->bind();
		mesh->render();
		break;

		// ============================================
	case DisplayMode::WIREFRAME:
		// ============================================
		// WIREFRAME: Lines only
		// ============================================
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glLineWidth(1.25f);
		glDisable(GL_POLYGON_OFFSET_FILL);
		_fgShader->bind();
		mesh->render();
		break;

		// ============================================
	case DisplayMode::WIRESHADED:
		// Pass 1: Solid
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glLineWidth(1.0f);
		_fgShader->bind();
		_fgShader->setUniformValue("isWireframePass", false);
		mesh->render();
				
		// Pass 2: Wireframe overlay
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glLineWidth(1.5f);
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(-1.0f, -1.0f);

		_fgShader->bind();		
		_fgShader->setUniformValue("isWireframePass", true);
		mesh->render();

		glDisable(GL_POLYGON_OFFSET_FILL);
		_fgShader->setUniformValue("isWireframePass", false);
		break;

	default:
		// Safety fallback: solid rendering
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glLineWidth(1.0f);
		glDisable(GL_POLYGON_OFFSET_FILL);
		_fgShader->bind();
		mesh->render();
		break;
	}

	// Reset to default state (important!)
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glLineWidth(1.0f);
	glDisable(GL_POLYGON_OFFSET_FILL);

	_fgShader->release();
}

void GLWidget::gradientBackground(float top_r, float top_g, float top_b, float top_a,
	float bot_r, float bot_g, float bot_b, float bot_a, int gradientStyle)
{
	glViewport(0, 0, width(), height());
	if (!_bgVAO.isCreated())
	{
		_bgVAO.create();
	}

	glDisable(GL_DEPTH_TEST);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	_bgShader->bind();		
	_bgShader->setUniformValue("top_color", QVector4D(top_r, top_g, top_b, top_a));
	_bgShader->setUniformValue("bot_color", QVector4D(bot_r, bot_g, bot_b, bot_a));
	_bgShader->setUniformValue("gradient_style", gradientStyle);  // Pass the gradient style

	_bgVAO.bind();
	glDrawArrays(GL_TRIANGLES, 0, 3);

	glEnable(GL_DEPTH_TEST);

	_bgVAO.release();
	_bgShader->release();
}

void GLWidget::loadBgColorSettings()
{
	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());

	// Retrieve and validate top color
	QVariant topColorValue = settings.value("Background/TopColor");
	if (topColorValue.isValid() && topColorValue.canConvert<QColor>())
	{
		_bgTopColor = topColorValue.value<QColor>();
	}
	else
	{
		_bgTopColor = QColor::fromRgbF(0.45f, 0.45f, 0.45f, 1.0f);
	}

	// Retrieve and validate bottom color
	QVariant bottomColorValue = settings.value("Background/BottomColor");
	if (bottomColorValue.isValid() && bottomColorValue.canConvert<QColor>())
	{
		_bgBotColor = bottomColorValue.value<QColor>();
	}
	else
	{		
		_bgBotColor = QColor::fromRgbF(0.9f, 0.9f, 0.9f, 1.0f);
		
	}

	// Retrieve and validate gradient style
	QVariant gradientStyleValue = settings.value("Background/GradientStyle");
	if (gradientStyleValue.isValid() && gradientStyleValue.canConvert<int>())
	{
		int style = gradientStyleValue.toInt();
		if (style >= 0 && style <= 3)
		{
			_gradientStyle = style;
		}
		else
		{
			_gradientStyle = 0; // Default to vertical gradient
		}
	}
	else
	{
		_gradientStyle = 0; // Default to vertical gradient
	}
}


void GLWidget::splitScreen()
{
	if (!_bgSplitVAO.isCreated())
	{
		_bgSplitVAO.create();
		_bgSplitVAO.bind();
	}

	if (!_bgSplitVBO.isCreated())
	{
		_bgSplitVBO = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
		_bgSplitVBO.create();
		_bgSplitVBO.bind();
		_bgSplitVBO.setUsagePattern(QOpenGLBuffer::StaticDraw);

		static const std::vector<float> vertices = {
			-static_cast<float>(width()) / 2,
			0,
			static_cast<float>(width()) / 2,
			0,
			0,
			-static_cast<float>(height()) / 2,
			0,
			static_cast<float>(height()) / 2,
		};

		_bgSplitVBO.allocate(vertices.data(), static_cast<int>(vertices.size() * sizeof(float)));

		_bgSplitShader->bind();
		_bgSplitShader->enableAttributeArray("vertexPosition");
		_bgSplitShader->setAttributeBuffer("vertexPosition", GL_FLOAT, 0, 2);

		_bgSplitVBO.release();
	}

	glViewport(0, 0, width(), height());

	glDisable(GL_DEPTH_TEST);

	_bgSplitVAO.bind();
	glLineWidth(0.5);
	glDrawArrays(GL_LINES, 0, 4);
	glLineWidth(1);

	glEnable(GL_DEPTH_TEST);

	_bgSplitVAO.release();
	_bgSplitShader->release();
}

void GLWidget::setupClippingUniforms(QOpenGLShaderProgram* prog, QVector3D pos)
{
	prog->bind();
	if (_clipYZEnabled || _clipZXEnabled || _clipXYEnabled || !(_clipDX == 0 && _clipDY == 0 && _clipDZ == 0))
	{
		_fgShader->setUniformValue("sectionActive", true);
	}
	else
	{
		_fgShader->setUniformValue("sectionActive", false);
	}
	prog->setUniformValue("modelViewMatrix", _modelViewMatrix);
	prog->setUniformValue("projectionMatrix", _projectionMatrix);
	prog->setUniformValue("clipPlaneX", QVector4D(_modelViewMatrix.map(QVector3D(_clipXFlipped ? 1 : -1, 0, 0) + pos),
		(_clipXFlipped ? 1 : -1) * (pos.x() - (_clipXCoeff + _boundingBox.center().getX()))));
	prog->setUniformValue("clipPlaneY", QVector4D(_modelViewMatrix.map(QVector3D(0, _clipYFlipped ? 1 : -1, 0) + pos),
		(_clipYFlipped ? 1 : -1) * (pos.y() - (_clipYCoeff + _boundingBox.center().getY()))));
	prog->setUniformValue("clipPlaneZ", QVector4D(_modelViewMatrix.map(QVector3D(0, 0, _clipZFlipped ? 1 : -1) + pos),
		(_clipZFlipped ? 1 : -1) * (pos.z() - (_clipZCoeff + _boundingBox.center().getZ()))));
	prog->setUniformValue("clipPlane", QVector4D(_modelViewMatrix.map(QVector3D(_clipDX, _clipDY, _clipDZ) + pos),
		pos.x() * _clipDX + pos.y() * _clipDY + pos.z() * _clipDZ));
}


AssImpMesh* GLWidget::createMeshFromData(const AssImpMeshData& meshData)
{
	std::vector<GLMaterial::Texture> textures = meshData.textures;
	for (GLMaterial::Texture& texture : textures)
	{
		const QString originalPath = QString::fromStdString(texture.path);

		TextureSamplerSettings samplers{ texture.wrapS, texture.wrapT, texture.minFilter, texture.magFilter };
		if (!texture.imageData.isNull())
		{
			const QString cacheKey = originalPath.isEmpty()
				? QStringLiteral("embedded://%1/%2/%3")
					.arg(meshData.name)
					.arg(QString::fromStdString(texture.type))
					.arg(textures.size())
				: originalPath;
			texture.id = getOrCreateTextureCached(cacheKey, texture.imageData, samplers);
		}
		else if (!texture.path.empty())
		{
			const QString texturePath = QString::fromStdString(texture.path);
			if (texturePath.endsWith(".ktx2", Qt::CaseInsensitive))
			{
				texture.id = getOrLoadKtx2TextureCached(texturePath, texture.type, samplers);
			}
			else
			{
				texture.id = getOrLoadTextureCached(texturePath, samplers);
			}
		}
		else if (texture.id != 0)
		{
			// Keep externally prepared IDs only when there is no path/image payload to re-resolve.
		}
	}

	GLMaterial resolvedMaterial = meshData.material;
	for (const GLMaterial::Texture& texture : textures)
	{
		const QString texturePath = QString::fromStdString(texture.path);
		if (texture.type == "albedoMap" || texture.type == "texture_diffuse")
		{
			resolvedMaterial.setAlbedoTextureId(texture.id);
			if (!texturePath.isEmpty()) resolvedMaterial.setAlbedoMap(texturePath);
		}
		else if (texture.type == "metallicMap" || texture.type == "texture_specular")
		{
			resolvedMaterial.setMetallicTextureId(texture.id);
			if (!texturePath.isEmpty()) resolvedMaterial.setMetallicMap(texturePath);
		}
		else if (texture.type == "roughnessMap")
		{
			resolvedMaterial.setRoughnessTextureId(texture.id);
			if (!texturePath.isEmpty()) resolvedMaterial.setRoughnessMap(texturePath);
		}
		else if (texture.type == "normalMap" || texture.type == "texture_normal")
		{
			resolvedMaterial.setNormalTextureId(texture.id);
			if (!texturePath.isEmpty()) resolvedMaterial.setNormalMap(texturePath);
		}
		else if (texture.type == "aoMap")
		{
			resolvedMaterial.setOcclusionTextureId(texture.id);
			if (!texturePath.isEmpty()) resolvedMaterial.setAOMap(texturePath);
		}
		else if (texture.type == "opacityMap" || texture.type == "texture_opacity")
		{
			resolvedMaterial.setOpacityTextureId(texture.id);
			if (!texturePath.isEmpty()) resolvedMaterial.setOpacityMap(texturePath);
		}
		else if (texture.type == "heightMap" || texture.type == "texture_height")
		{
			resolvedMaterial.setHeightTextureId(texture.id);
			if (!texturePath.isEmpty()) resolvedMaterial.setHeightMap(texturePath);
		}
		else if (texture.type == "emissiveMap" || texture.type == "texture_emissive")
		{
			resolvedMaterial.setEmissiveTextureId(texture.id);
			if (!texturePath.isEmpty()) resolvedMaterial.setEmissiveMap(texturePath);
		}
		else if (texture.type == "transmissionMap")
		{
			resolvedMaterial.setTransmissionTextureId(texture.id);
			if (!texturePath.isEmpty()) resolvedMaterial.setTransmissionMap(texturePath);
		}
		else if (texture.type == "iorMap")
		{
			resolvedMaterial.setIORTextureId(texture.id);
			if (!texturePath.isEmpty()) resolvedMaterial.setIORMap(texturePath);
		}
		else if (texture.type == "sheenColorMap")
		{
			resolvedMaterial.setSheenColorTextureId(texture.id);
			if (!texturePath.isEmpty()) resolvedMaterial.setSheenColorMap(texturePath);
		}
		else if (texture.type == "sheenRoughnessMap")
		{
			resolvedMaterial.setSheenRoughnessTextureId(texture.id);
			if (!texturePath.isEmpty()) resolvedMaterial.setSheenRoughnessMap(texturePath);
		}
		else if (texture.type == "clearcoatColorMap")
		{
			resolvedMaterial.setClearcoatColorTextureId(texture.id);
			if (!texturePath.isEmpty()) resolvedMaterial.setClearcoatColorMap(texturePath);
		}
		else if (texture.type == "clearcoatRoughnessMap")
		{
			resolvedMaterial.setClearcoatRoughnessTextureId(texture.id);
			if (!texturePath.isEmpty()) resolvedMaterial.setClearcoatRoughnessMap(texturePath);
		}
		else if (texture.type == "clearcoatNormalMap")
		{
			resolvedMaterial.setClearcoatNormalTextureId(texture.id);
			if (!texturePath.isEmpty()) resolvedMaterial.setClearcoatNormalMap(texturePath);
		}
		else if (texture.type == "iridescenceMap")
		{
			resolvedMaterial.setIridescenceTextureId(texture.id);
			if (!texturePath.isEmpty()) resolvedMaterial.setIridescenceMap(texturePath);
		}
		else if (texture.type == "iridescenceThicknessMap")
		{
			resolvedMaterial.setIridescenceThicknessTextureId(texture.id);
			if (!texturePath.isEmpty()) resolvedMaterial.setIridescenceThicknessMap(texturePath);
		}
		else if (texture.type == "specularFactorMap")
		{
			resolvedMaterial.setSpecularFactorTextureId(texture.id);
			if (!texturePath.isEmpty()) resolvedMaterial.setSpecularFactorMap(texturePath);
		}
		else if (texture.type == "specularColorMap")
		{
			resolvedMaterial.setSpecularColorTextureId(texture.id);
			if (!texturePath.isEmpty()) resolvedMaterial.setSpecularColorMap(texturePath);
		}
		else if (texture.type == "anisotropyMap")
		{
			resolvedMaterial.setAnisotropyTextureId(texture.id);
			if (!texturePath.isEmpty()) resolvedMaterial.setAnisotropyMap(texturePath);
		}
		else if (texture.type == "thicknessMap")
		{
			resolvedMaterial.setThicknessTextureId(texture.id);
			if (!texturePath.isEmpty()) resolvedMaterial.setThicknessMap(texturePath);
		}
		else if (texture.type == "diffuseMap")
		{
			resolvedMaterial.setDiffuseTextureId(texture.id);
			if (!texturePath.isEmpty()) resolvedMaterial.setDiffuseMap(texturePath);
		}
		else if (texture.type == "diffuseTransmissionMap")
		{
			resolvedMaterial.setDiffuseTransmissionTextureId(texture.id);
			if (!texturePath.isEmpty()) resolvedMaterial.setDiffuseTransmissionMap(texturePath);
		}
		else if (texture.type == "diffuseTransmissionColorMap")
		{
			resolvedMaterial.setDiffuseTransmissionColorTextureId(texture.id);
			if (!texturePath.isEmpty()) resolvedMaterial.setDiffuseTransmissionColorMap(texturePath);
		}
		else if (texture.type == "specularGlossinessMap")
		{
			resolvedMaterial.setSpecularGlossinessTextureId(texture.id);
			if (!texturePath.isEmpty()) resolvedMaterial.setSpecularGlossinessMap(texturePath);
		}
	}
	auto* mesh = new AssImpMesh(_fgShader.get(), meshData.name, meshData.vertices, meshData.indices, textures, resolvedMaterial);
	mesh->setHasNegativeScale(meshData.hasNegativeScale);
	mesh->setPrimitiveMode(meshData.primitiveMode);
	mesh->setSceneIndex(meshData.sceneIndex);
	if (_progressiveLoadingEnabled)
	{
		GLMaterial progressiveResolved = resolveMaterialTextures(this, mesh->getMaterial());
		mesh->setMaterial(progressiveResolved);
		mesh->setTextureMaps(progressiveResolved);
	}
	return mesh;
}

void GLWidget::onMeshBatchReady(const std::vector<AssImpMeshData>& batch)
{
	makeCurrent();
	for (const AssImpMeshData& meshData : batch)
	{
		AssImpMesh* mesh = createMeshFromData(meshData);
		addToDisplay(mesh);
		_pendingSceneUuids.append(mesh->uuid());
	}
	_viewer->updateDisplayList();
}

UVMethod GLWidget::promptLargeModelUVDecision(int totalTriangles, UVMethod currentMethod)
{
	QMessageBox msgBox(this);
	msgBox.setWindowTitle(tr("Performance Warning!"));
	msgBox.setText(tr("The model contains more than %1 triangles and the current method of UV generation is \"Smart UV\" which is time consuming.\nDo you want to continue generating the UV?")
		.arg(totalTriangles));
	msgBox.setIcon(QMessageBox::Question);

	QPushButton* yesButton = msgBox.addButton(QMessageBox::Yes);
	QPushButton* noButton = msgBox.addButton(QMessageBox::No);
	QPushButton* changeSettingsButton = msgBox.addButton(tr("Change Settings"), QMessageBox::ActionRole);

	msgBox.setDefaultButton(QMessageBox::Yes);
	msgBox.exec();

	if (msgBox.clickedButton() == noButton)
	{
		qDebug() << "User chose not to generate UVs, using None method.";
		return UVMethod::None;
	}

	if (msgBox.clickedButton() == changeSettingsButton)
	{
		return ModelViewer::askUserForUVMethod(this).method;
	}

	Q_UNUSED(yesButton);
	return currentMethod;
}

GLuint GLWidget::createGPUTextureFromImage(const QImage& image, const TextureSamplerSettings& samplers)
{
	if (image.isNull())
	{
		return 0;
	}

	GLenum internalFormat = GL_RGBA8;
	GLenum dataFormat = GL_RGBA;
	GLenum dataType = GL_UNSIGNED_BYTE;

	QImage glImage;

	switch (image.format())
	{
	case QImage::Format_RGB888:
		glImage = image;
		internalFormat = GL_RGB8;
		dataFormat = GL_RGB;
		break;

	case QImage::Format_RGBA8888:
	case QImage::Format_RGBA8888_Premultiplied:
		glImage = image;
		internalFormat = GL_RGBA8;
		dataFormat = GL_RGBA;
		break;

	case QImage::Format_Grayscale8:
		glImage = image;
		internalFormat = GL_R8;
		dataFormat = GL_RED;
		break;

	case QImage::Format_Indexed8:
		glImage = image.convertToFormat(QImage::Format_RGBA8888);
		internalFormat = GL_RGBA8;
		dataFormat = GL_RGBA;
		break;

	default:
		glImage = image.convertToFormat(QImage::Format_RGBA8888);
		internalFormat = GL_RGBA8;
		dataFormat = GL_RGBA;
		break;
	}

	GLuint textureID;
	glGenTextures(1, &textureID);

	glBindTexture(GL_TEXTURE_2D, textureID);
	glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, glImage.width(), glImage.height(), 0,
		dataFormat, dataType, glImage.constBits());
	glGenerateMipmap(GL_TEXTURE_2D);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, samplers.wrapS);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, samplers.wrapT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, samplers.minFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, samplers.magFilter);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, _anisotropicFilteringLevel);

	return textureID;
}

GLuint GLWidget::uploadDecodedTexture(GLMaterial::Texture& texture, const QImage& image)
{
	TextureSamplerSettings samplers{ texture.wrapS, texture.wrapT, texture.minFilter, texture.magFilter };
	GLuint textureId = uploadDecodedTextureImage(image, samplers);
	texture.id = textureId;
	return textureId;
}

GLuint GLWidget::uploadDecodedTextureImage(const QImage& image, const TextureSamplerSettings& samplers)
{
	makeCurrent();
	return createGPUTextureFromImage(image, samplers);
}

GLuint GLWidget::uploadKtx2TextureImage(const QString& path, const std::string& mapType, const TextureSamplerSettings& samplers)
{
	if (path.isEmpty())
	{
		return 0;
	}

	makeCurrent();

	TranscodedTexture transcodedTexture;
	if (!_ktx2Loader.loadKTX2(path.toStdString(), transcodedTexture, _gpuCapabilities, mapType))
	{
		qWarning() << "GLWidget::uploadKtx2Texture - Failed to load KTX2 file:" << path;
		return 0;
	}

	GLuint textureId = _ktx2Loader.uploadToGPU(transcodedTexture);
	if (textureId == 0)
	{
		qWarning() << "GLWidget::uploadKtx2Texture - Failed to upload KTX2 texture:" << path;
		return 0;
	}

	glBindTexture(GL_TEXTURE_2D, textureId);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, samplers.minFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, samplers.magFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, samplers.wrapS);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, samplers.wrapT);
	glBindTexture(GL_TEXTURE_2D, 0);

	return textureId;
}

GLuint GLWidget::uploadKtx2Texture(const QString& path, const std::string& mapType, GLMaterial::Texture& texture)
{
	TextureSamplerSettings samplers{ texture.wrapS, texture.wrapT, texture.minFilter, texture.magFilter };
	GLuint textureId = uploadKtx2TextureImage(path, mapType, samplers);
	texture.id = textureId;
	return textureId;
}

unsigned int GLWidget::getOrCreateTextureCached(
	const QString& cacheKey,
	const QImage& image,
	const TextureSamplerSettings& samplers)
{
	if (cacheKey.isEmpty() || image.isNull())
	{
		return 0;
	}

	auto it = _texCache.find(cacheKey);
	if (it != _texCache.end())
	{
		CachedTextureEntry& entry = it->second;
		if (entry.lastGPUTexture != 0 && entry.lastSamplerSettings == samplers)
		{
			retainTexture(entry.lastGPUTexture);
			return entry.lastGPUTexture;
		}

		if (!entry.image.isNull())
		{
			makeCurrent();
			GLuint newTexID = createGPUTextureFromImage(entry.image, samplers);
			if (newTexID != 0)
			{
				entry.lastGPUTexture = newTexID;
				entry.lastSamplerSettings = samplers;
				_texRefCount[newTexID] = 1;
				return newTexID;
			}
		}
	}

	makeCurrent();
	GLuint texID = createGPUTextureFromImage(image, samplers);
	if (texID == 0)
	{
		return 0;
	}

	CachedTextureEntry newEntry;
	newEntry.image = image;
	newEntry.lastGPUTexture = texID;
	newEntry.lastSamplerSettings = samplers;
	newEntry.imageWidth = image.width();
	newEntry.imageHeight = image.height();

	_texCache[cacheKey] = newEntry;
	_texRefCount[texID] = 1;
	return texID;
}

unsigned int GLWidget::getOrLoadKtx2TextureCached(
	const QString& path,
	const std::string& mapType,
	const TextureSamplerSettings& samplers)
{
	if (path.isEmpty())
	{
		return 0;
	}

	const QString cacheKey = QStringLiteral("ktx2://%1::%2")
		.arg(path, QString::fromStdString(mapType));

	auto it = _texCache.find(cacheKey);
	if (it != _texCache.end())
	{
		CachedTextureEntry& entry = it->second;
		if (entry.lastGPUTexture != 0 && entry.lastSamplerSettings == samplers)
		{
			retainTexture(entry.lastGPUTexture);
			return entry.lastGPUTexture;
		}
	}

	GLuint texID = uploadKtx2TextureImage(path, mapType, samplers);
	if (texID == 0)
	{
		return 0;
	}

	CachedTextureEntry& entry = _texCache[cacheKey];
	entry.lastGPUTexture = texID;
	entry.lastSamplerSettings = samplers;
	_texRefCount[texID] = 1;
	return texID;
}

unsigned int GLWidget::getOrLoadTextureCached(
	const QString& path,
	const TextureSamplerSettings& samplers)
{
	if (path.isEmpty()) return 0;

	auto it = _texCache.find(path);

	// Cache hit - image exists
	if (it != _texCache.end())
	{
		CachedTextureEntry& entry = it->second;

		// Exact match (same image + same samplers)
		if (entry.lastGPUTexture != 0 && entry.lastSamplerSettings == samplers)
		{
			retainTexture(entry.lastGPUTexture);
			return entry.lastGPUTexture;
		}

		// Same image, different samplers - create new GPU texture from cached image
		if (!entry.image.isNull())
		{
			makeCurrent();
			GLuint newTexID = createGPUTextureFromImage(entry.image, samplers);
			if (newTexID != 0)
			{
				entry.lastGPUTexture = newTexID;
				entry.lastSamplerSettings = samplers;
				_texRefCount[newTexID] = 1;
				return newTexID;
			}
		}
	}

	// Cache miss - image not cached, load from disk
	makeCurrent();
	GLuint texID = loadTextureFromFile(
		path.toStdString().c_str(),
		samplers.wrapS,
		samplers.wrapT,
		samplers.minFilter,
		samplers.magFilter,
		false);

	if (texID == 0) return 0;

	// Cache the image for future use with different samplers
	CachedTextureEntry newEntry;
	newEntry.image = QImage(path);  // Cache the image
	newEntry.lastGPUTexture = texID;
	newEntry.lastSamplerSettings = samplers;
	newEntry.imageWidth = newEntry.image.width();
	newEntry.imageHeight = newEntry.image.height();

	_texCache[path] = newEntry;
	_texRefCount[texID] = 1;

	return texID;
}

void GLWidget::retainTexture(unsigned int texId)
{
	if (texId == 0) return;
	auto it = _texRefCount.find(texId);
	if (it != _texRefCount.end()) it->second++;
	else _texRefCount[texId] = 1;
}

void GLWidget::releaseTexture(unsigned int texId)
{
	if (texId == 0) return;
	auto it = _texRefCount.find(texId);
	if (it == _texRefCount.end()) return;
	if (--(it->second) <= 0)
	{
		// remove from path map too
		for (auto pit = _texCache.begin(); pit != _texCache.end(); )
		{
			if (pit->second.lastGPUTexture == texId) pit = _texCache.erase(pit); else ++pit;
		}
		glDeleteTextures(1, &texId);
		_texRefCount.erase(texId);
	}
}

GLMaterial GLWidget::resolveMaterialTextures(GLWidget* w, const GLMaterial& src)
{
	GLMaterial m = src;
	auto resolveTexturePath = [w](const QString& path,
		const std::string& mapType,
		const TextureSamplerSettings& samplers) -> unsigned int
	{
		if (path.isEmpty())
		{
			return 0;
		}
		if (path.endsWith(".ktx2", Qt::CaseInsensitive))
		{
			return w->getOrLoadKtx2TextureCached(path, mapType, samplers);
		}
		return w->getOrLoadTextureCached(path, samplers);
	};

	if (m.hasAlbedoMap())
	{
		const GLMaterial::Texture& tex = m.texture(GLMaterial::TextureType::Albedo);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setAlbedoTextureId(resolveTexturePath(m.albedoMapPath(), "albedoMap", samplers));
	}
	if (m.hasMetallicMap())
	{
		const GLMaterial::Texture& tex = m.texture(GLMaterial::TextureType::Metallic);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setMetallicTextureId(resolveTexturePath(m.metallicMapPath(), "metallicMap", samplers));
	}
	if (m.hasRoughnessMap())
	{
		const GLMaterial::Texture& tex = m.texture(GLMaterial::TextureType::Roughness);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setRoughnessTextureId(resolveTexturePath(m.roughnessMapPath(), "roughnessMap", samplers));
	}
	if (m.hasNormalMap())
	{
		const GLMaterial::Texture& tex = m.texture(GLMaterial::TextureType::Normal);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setNormalTextureId(resolveTexturePath(m.normalMapPath(), "normalMap", samplers));
	}
	if (m.hasAOMap())
	{
		const GLMaterial::Texture& tex = m.texture(GLMaterial::TextureType::AmbientOcclusion);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setOcclusionTextureId(resolveTexturePath(m.aoMapPath(), "aoMap", samplers));
	}
	if (m.hasOpacityMap())
	{
		const GLMaterial::Texture& tex = m.texture(GLMaterial::TextureType::Opacity);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setOpacityTextureId(resolveTexturePath(m.opacityMapPath(), "opacityMap", samplers));
	}
	if (m.hasHeightMap())
	{
		const GLMaterial::Texture& tex = m.texture(GLMaterial::TextureType::Height);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setHeightTextureId(resolveTexturePath(m.heightMapPath(), "heightMap", samplers));
	}
	if (m.hasEmissiveMap())
	{
		const GLMaterial::Texture& tex = m.texture(GLMaterial::TextureType::Emissive);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setEmissiveTextureId(resolveTexturePath(m.emissiveMapPath(), "emissiveMap", samplers));
	}
	if (m.hasTransmissionMap())
	{
		const GLMaterial::Texture& tex = m.texture(GLMaterial::TextureType::Transmission);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setTransmissionTextureId(resolveTexturePath(m.transmissionMapPath(), "transmissionMap", samplers));
	}
	if (m.hasIORMap())
	{
		const GLMaterial::Texture& tex = m.texture(GLMaterial::TextureType::IOR);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setIORTextureId(resolveTexturePath(m.iorMapPath(), "iorMap", samplers));
	}
	if (m.hasSheenColorMap())
	{
		const GLMaterial::Texture& tex = m.texture(GLMaterial::TextureType::SheenColor);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setSheenColorTextureId(resolveTexturePath(m.sheenColorMapPath(), "sheenColorMap", samplers));
	}
	if (m.hasSheenRoughnessMap())
	{
		const GLMaterial::Texture& tex = m.texture(GLMaterial::TextureType::SheenRoughness);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setSheenRoughnessTextureId(resolveTexturePath(m.sheenRoughnessMapPath(), "sheenRoughnessMap", samplers));
	}
	if (m.hasClearcoatColorMap())
	{
		const GLMaterial::Texture& tex = m.texture(GLMaterial::TextureType::ClearcoatColor);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setClearcoatColorTextureId(resolveTexturePath(m.clearcoatColorMapPath(), "clearcoatColorMap", samplers));
	}
	if (m.hasClearcoatRoughnessMap())
	{
		const GLMaterial::Texture& tex = m.texture(GLMaterial::TextureType::ClearcoatRoughness);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setClearcoatRoughnessTextureId(resolveTexturePath(m.clearcoatRoughnessMapPath(), "clearcoatRoughnessMap", samplers));
	}
	if (m.hasClearcoatNormalMap())
	{
		const GLMaterial::Texture& tex = m.texture(GLMaterial::TextureType::ClearcoatNormal);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setClearcoatNormalTextureId(resolveTexturePath(m.clearcoatNormalMapPath(), "clearcoatNormalMap", samplers));
	}
	if (m.hasIridescenceMap())
	{
		const GLMaterial::Texture& tex = m.texture(GLMaterial::TextureType::Iridescence);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setIridescenceTextureId(resolveTexturePath(m.iridescenceMap(), "iridescenceMap", samplers));
	}
	if (m.hasIridescenceThicknessMap())
	{
		const GLMaterial::Texture& tex = m.texture(GLMaterial::TextureType::IridescenceThickness);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setIridescenceThicknessTextureId(resolveTexturePath(m.iridescenceThicknessMap(), "iridescenceThicknessMap", samplers));
	}
	if (m.hasSpecularColorMap())
	{
		const GLMaterial::Texture& tex = m.texture(GLMaterial::TextureType::SpecularColor);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setSpecularColorTextureId(resolveTexturePath(m.specularColorMap(), "specularColorMap", samplers));
	}
	if (m.hasSpecularFactorMap())
	{
		const GLMaterial::Texture& tex = m.texture(GLMaterial::TextureType::SpecularFactor);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setSpecularFactorTextureId(resolveTexturePath(m.specularFactorMap(), "specularFactorMap", samplers));
	}
	if (m.hasAnisotropyMap())
	{
		const GLMaterial::Texture& tex = m.texture(GLMaterial::TextureType::Anisotropy);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setAnisotropyTextureId(resolveTexturePath(m.anisotropyMap(), "anisotropyMap", samplers));
	}
	if (m.hasThicknessMap())
	{
		const GLMaterial::Texture& tex = m.texture(GLMaterial::TextureType::Thickness);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setThicknessTextureId(resolveTexturePath(m.thicknessMap(), "thicknessMap", samplers));
	}
	if (m.hasDiffuseMap())
	{
		const GLMaterial::Texture& tex = m.texture(GLMaterial::TextureType::Diffuse);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setDiffuseTextureId(resolveTexturePath(m.diffuseMap(), "diffuseMap", samplers));
	}
	if (m.hasDiffuseTransmissionMap())
	{
		const GLMaterial::Texture& tex = m.texture(GLMaterial::TextureType::DiffuseTransmission);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setDiffuseTransmissionTextureId(resolveTexturePath(m.diffuseTransmissionMap(), "diffuseTransmissionMap", samplers));
	}
	if (m.hasDiffuseTransmissionColorMap())
	{
		const GLMaterial::Texture& tex = m.texture(GLMaterial::TextureType::DiffuseTransmissionColor);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setDiffuseTransmissionColorTextureId(resolveTexturePath(m.diffuseTransmissionColorMap(), "diffuseTransmissionColorMap", samplers));
	}
	if (m.hasSpecularGlossinessMap())
	{
		const GLMaterial::Texture& tex = m.texture(GLMaterial::TextureType::SpecularGlossiness);
		TextureSamplerSettings samplers{ tex.wrapS, tex.wrapT, tex.minFilter, tex.magFilter };
		m.setSpecularGlossinessTextureId(resolveTexturePath(m.specularGlossinessMap(), "specularGlossinessMap", samplers));
	}

	m.syncTextureParameters();

	return m;
}

void GLWidget::initTransmissionBuffer()
{
	// Called once during widget initialization (e.g., in initializeGL or constructor)
	_transmissionTextureWidth = width();
	_transmissionTextureHeight = height();

	if (_transmissionFBO != 0)
		glDeleteFramebuffers(1, &_transmissionFBO);
	if (_transmissionColorTexture != 0)
		glDeleteTextures(1, &_transmissionColorTexture);
	if (_transmissionDepthTexture != 0)
		glDeleteTextures(1, &_transmissionDepthTexture);

	// Create FBO
	glGenFramebuffers(1, &_transmissionFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, _transmissionFBO);

	// --- Create COLOR texture (RGBA32F for precision) ---
	glGenTextures(1, &_transmissionColorTexture);
	glBindTexture(GL_TEXTURE_2D, _transmissionColorTexture);
	// Allocate storage with mipmaps
	// Calculate number of mip levels: log2(max(width, height)) + 1
	int maxDim = std::max(_transmissionTextureWidth, _transmissionTextureHeight);
	int numMips = (int)std::floor(std::log2(maxDim)) + 1;

	// Allocate texture storage with mipmaps
	glTexStorage2D(GL_TEXTURE_2D, numMips, GL_RGBA32F,
		_transmissionTextureWidth, _transmissionTextureHeight);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, numMips - 1);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_TEXTURE_2D, _transmissionColorTexture, 0);

	// --- Create DEPTH texture (DEPTH32F) ---
	glGenTextures(1, &_transmissionDepthTexture);
	glBindTexture(GL_TEXTURE_2D, _transmissionDepthTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F,
		_transmissionTextureWidth, _transmissionTextureHeight,
		0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
		GL_TEXTURE_2D, _transmissionDepthTexture, 0);

	GLenum drawBuffers[] = { GL_COLOR_ATTACHMENT0 };
	glDrawBuffers(1, drawBuffers);

	// --- Verify FBO is complete ---
	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		qWarning() << "Transmission FBO incomplete! Status:" << status;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void GLWidget::resizeTransmissionBuffer(int width, int height)
{
	// Called from resizeGL() whenever window size changes
	if (_transmissionTextureWidth == width && _transmissionTextureHeight == height)
		return; // No resize needed

	_transmissionTextureWidth = width;
	_transmissionTextureHeight = height;

	initTransmissionBuffer();
}

void GLWidget::createWhiteTexture()
{
	unsigned char white[] = { 255, 255, 255, 255 };
	glGenTextures(1, &_whiteTexture);
	glBindTexture(GL_TEXTURE_2D, _whiteTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

void GLWidget::generateCubemapMipmaps(GLuint cubemapTexture)
{
	// Calculate mip count based on environment map resolution
	// Query actual cubemap resolution at runtime
	GLint baseSize = 0;
	glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
	glGetTextureLevelParameteriv(cubemapTexture, 0, GL_TEXTURE_WIDTH, &baseSize);

	qDebug() << "Cubemap resolution:" << baseSize << "x" << baseSize;

	int maxMipLevels = static_cast<int>(std::log2(baseSize)) + 1;

	qDebug() << "Generating" << maxMipLevels << "mip levels for cubemap";

	// Create temporary FBO for rendering to mip levels
	GLuint mipmapFBO;
	GLuint mipmapRBO;
	glGenFramebuffers(1, &mipmapFBO);
	glGenRenderbuffers(1, &mipmapRBO);

	glBindFramebuffer(GL_FRAMEBUFFER, mipmapFBO);
	glBindRenderbuffer(GL_RENDERBUFFER, mipmapRBO);

	// Setup projection matrix (90 degree FOV for cubemap, 1:1 aspect ratio)
	QMatrix4x4 captureProjection;
	captureProjection.perspective(90.0f, 1.0f, 0.1f, 10.0f);

	// Setup view matrices for each cubemap face (must match environment capture order)
	QMatrix4x4 captureViews[] = {
		// +X face
		[]() {
			QMatrix4x4 m;
			m.lookAt(QVector3D(0.0f, 0.0f, 0.0f), QVector3D(1.0f, 0.0f, 0.0f), QVector3D(0.0f, -1.0f, 0.0f));
			return m;
		}(),
			// -X face
			[]() {
				QMatrix4x4 m;
				m.lookAt(QVector3D(0.0f, 0.0f, 0.0f), QVector3D(-1.0f, 0.0f, 0.0f), QVector3D(0.0f, -1.0f, 0.0f));
				return m;
			}(),
				// +Y face
				[]() {
					QMatrix4x4 m;
					m.lookAt(QVector3D(0.0f, 0.0f, 0.0f), QVector3D(0.0f, 1.0f, 0.0f), QVector3D(0.0f, 0.0f, 1.0f));
					return m;
				}(),
					// -Y face
					[]() {
						QMatrix4x4 m;
						m.lookAt(QVector3D(0.0f, 0.0f, 0.0f), QVector3D(0.0f, -1.0f, 0.0f), QVector3D(0.0f, 0.0f, -1.0f));
						return m;
					}(),
						// +Z face
						[]() {
							QMatrix4x4 m;
							m.lookAt(QVector3D(0.0f, 0.0f, 0.0f), QVector3D(0.0f, 0.0f, 1.0f), QVector3D(0.0f, -1.0f, 0.0f));
							return m;
						}(),
							// -Z face
							[]() {
								QMatrix4x4 m;
								m.lookAt(QVector3D(0.0f, 0.0f, 0.0f), QVector3D(0.0f, 0.0f, -1.0f), QVector3D(0.0f, -1.0f, 0.0f));
								return m;
							}()
	};

	_skyBox->setProg(_downsampleShader.get());

	// Bind shader and set static uniforms
	_downsampleShader->bind();
	_downsampleShader->setUniformValue("projection", captureProjection);

	// Bind source cubemap to texture unit 0
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
	_downsampleShader->setUniformValue("sourceMap", 0);

	// Generate each mip level
	for (int mip = 1; mip < maxMipLevels; ++mip)
	{
		int mipSize = baseSize >> mip;  // Bitshift divide by 2^mip

		qDebug() << "Generating mip level" << mip << "(" << mipSize << "x" << mipSize << ")";

		// Resize renderbuffer for depth attachment
		glBindRenderbuffer(GL_RENDERBUFFER, mipmapRBO);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipSize, mipSize);

		// Set viewport to target mip size
		glViewport(0, 0, mipSize, mipSize);

		// Set which source mip level to sample from
		_downsampleShader->setUniformValue("currentMipLevel", mip - 1);

		// Render to all 6 faces of this mip level
		for (int face = 0; face < 6; ++face)
		{
			// Attach this mip level of this face to framebuffer
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubemapTexture, mip);

			// Verify framebuffer is complete
			GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
			if (status != GL_FRAMEBUFFER_COMPLETE)
			{
				qWarning() << "Framebuffer incomplete at mip" << mip << "face" << face;
				continue;
			}

			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			// Set view matrix for this face
			_downsampleShader->setUniformValue("view", captureViews[face]);

			// Render the cube
			_skyBox->render();
		}
	}

	// Restore state - MORE AGGRESSIVE
	glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	glUseProgram(0);

	// Reset viewport to current widget size
	glViewport(0, 0, width(), height());

	// Force rebind environment map to ensure it's in correct state
	glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	qDebug() << "Mipmap generation complete";
}

QString GLWidget::generateUniqueMeshName(const QString& baseName)
{
	// Check if base name already exists
	bool nameExists = false;
	for (const TriangleMesh* mesh : _meshStore)
	{
		if (mesh->getName() == baseName)
		{
			nameExists = true;
			break;
		}
	}

	// If base name doesn't exist, use it as-is
	if (!nameExists)
		return baseName;

	// Find the next available number suffix
	int counter = 2;
	QString uniqueName;

	while (true)
	{
		uniqueName = QString("%1 (%2)").arg(baseName).arg(counter);

		bool exists = false;
		for (const TriangleMesh* mesh : _meshStore)
		{
			if (mesh->getName() == uniqueName)
			{
				exists = true;
				break;
			}
		}

		if (!exists)
			break;

		counter++;
	}

	return uniqueName;
}

void GLWidget::renderToTransmissionBuffer(GLCamera* camera, const QColor& topColor, const QColor& botColor)
{
	if (!_transmissionEnabled)
		return;

	resizeTransmissionBuffer(width(), height());

	// --- SETUP STATE ---
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glFrontFace(GL_CCW);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glDepthMask(GL_TRUE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glStencilMask(0xFF);
	glDisable(GL_STENCIL_TEST);

	// --- BIND FBO ---
	glBindFramebuffer(GL_FRAMEBUFFER, _transmissionFBO);
	glViewport(0, 0, _transmissionTextureWidth, _transmissionTextureHeight);

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// --- Setup matrices ---
	_viewMatrix.setToIdentity();
	_viewMatrix = _primaryCamera->getViewMatrix();
	_projectionMatrix = _primaryCamera->getProjectionMatrix();
	_modelViewMatrix = _viewMatrix * _modelMatrix;

	// --- RENDER 1: BACKGROUND (gradient or skybox) ---
	if (_skyBoxEnabled)
	{
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		drawSkyBox();
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);
	}
	else
	{
		// Render gradient background (same as main framebuffer)
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		gradientBackground(topColor.redF(), topColor.greenF(), topColor.blueF(), topColor.alphaF(),
			botColor.redF(), botColor.greenF(), botColor.blueF(), botColor.alphaF(), _gradientStyle);
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);
	}

	// --- RENDER 2: OPAQUE MESHES (with clipping) ---
	glActiveTexture(GL_TEXTURE7);
	glBindTexture(GL_TEXTURE_2D, _whiteTexture);  // Any valid texture works

	glActiveTexture(GL_TEXTURE8);
	glBindTexture(GL_TEXTURE_2D, _whiteTexture);  // Any valid texture works

	glActiveTexture(GL_TEXTURE0);

	_fgShader->bind();
	setCommonUniforms(_fgShader.get(), _primaryCamera);
	drawMeshesWithClipping(_fgShader.get(), false); // opaque pass only
	_fgShader->release();

	// --- RENDER 3: SECTION CAPS ---
	if (_cappingEnabled &&
		!_sectionCapsSuppressedDuringInteraction &&
		(_clipYZEnabled || _clipZXEnabled || _clipXYEnabled))
	{
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(1.0f, 1.0f);
		drawSectionCapping();
		glDisable(GL_POLYGON_OFFSET_FILL);
	}

	// --- RENDER 4: FLOOR ---
	if (_displayMode == DisplayMode::REALSHADED &&
		_floorDisplayed && !_cappingEnabled &&
		!_meshStore.empty() &&
		camera != _orthoViewsCamera)
	{
		drawFloor(false);
	}

	// IMPORTANT: After rendering, generate mipmaps
	glBindTexture(GL_TEXTURE_2D, _transmissionColorTexture);
	glHint(GL_GENERATE_MIPMAP_HINT, GL_NICEST);
	glGenerateMipmap(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, 0);

	// --- UNBIND FBO ---
	glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
	glViewport(0, 0, width(), height());
}

void GLWidget::cleanupTransmissionBuffer()
{
	if (_transmissionFBO != 0)
	{
		glDeleteFramebuffers(1, &_transmissionFBO);
		_transmissionFBO = 0;
	}
	if (_transmissionColorTexture != 0)
	{
		glDeleteTextures(1, &_transmissionColorTexture);
		_transmissionColorTexture = 0;
	}
	if (_transmissionDepthTexture != 0)
	{
		glDeleteTextures(1, &_transmissionDepthTexture);
		_transmissionDepthTexture = 0;
	}
}

void GLWidget::checkAndStopTimers()
{
	if (_animateViewTimer->isActive())
	{
		_animateViewTimer->stop();
		// Set all defaults
		_currentRotation = QQuaternion::fromRotationMatrix(_primaryCamera->getViewMatrix().toGenericMatrix<3, 3>());
		_currentTranslation = _primaryCamera->getPosition();
		_currentViewRange = _viewRange;
		_slerpStep = 0.0f;
		emit rotationsSet();
	}
	if (_animateFitAllTimer->isActive())
	{
		_animateFitAllTimer->stop();
		// Set all defaults
		_currentTranslation = _primaryCamera->getPosition();
		_currentViewRange = _viewRange;
		_slerpStep = 0.0f;
		emit zoomAndPanSet();
	}
	if (_animateWindowZoomTimer->isActive())
	{
		_animateWindowZoomTimer->stop();
		_animateFitAllTimer->stop();
		// Set all defaults
		_currentTranslation = _primaryCamera->getPosition();
		_currentViewRange = _viewRange;
		_slerpStep = 0.0f;
		emit zoomAndPanSet();
	}
	if (_animateCenterScreenTimer->isActive())
	{
		_animateCenterScreenTimer->stop();
		_animateFitAllTimer->stop();
		// Set all defaults
		_currentTranslation = _primaryCamera->getPosition();
		_currentViewRange = _viewRange;
		_slerpStep = 0.0f;
		emit zoomAndPanSet();
	}
}

void GLWidget::disableLowRes()
{
	_lowResEnabled = false;
	update();
}

void GLWidget::disableSectionCapsInteractionSuppression()
{
	setSectionCapsInteractionSuppressed(false);
}

void GLWidget::setSectionCapsInteractionSuppressed(bool suppressed)
{
	bool actual = suppressed && _dynamicCappingEnabled;
	if (_sectionCapsSuppressedDuringInteraction == actual)
		return;

	_sectionCapsSuppressedDuringInteraction = actual;
	update();
}

void GLWidget::setSectionCapsDynamicEnabled(bool enabled)
{
	_dynamicCappingEnabled = enabled;
	if (!enabled && _sectionCapsSuppressedDuringInteraction)
		setSectionCapsInteractionSuppressed(false);
}

void GLWidget::resizeEvent(QResizeEvent* event)
{
	if (_viewToolbar)
	{
		_viewToolbar->reposition(width(), height()); // Move completely below widget
	}
	QOpenGLWidget::resizeEvent(event);
}

void GLWidget::mousePressEvent(QMouseEvent* e)
{
	setFocus();
	checkAndStopTimers();

	// Reset inertia on new mouse press
	_inertiaPanVelocity = QVector2D();
	_inertiaZoomVelocity = 0.0f;
	_inertiaRotateVelocity = QVector2D();
	if (_inertiaTimer) _inertiaTimer->stop();

	// Reset movement tracking
	_mouseMovedSincePress = false;
	_lastMouseMoveTime = 0;

	if (e->button() & Qt::LeftButton)
	{
		_leftButtonPoint.setX(e->position().x());
		_leftButtonPoint.setY(e->position().y());

		// Track if Shift is held for drag selection mode
		_shiftDragActive = (e->modifiers() & Qt::ShiftModifier) != 0;
		_sweepStartPoint = QPoint(e->position().x(), e->position().y());

		if (!(e->modifiers() & Qt::ControlModifier) && !(e->modifiers() & Qt::ShiftModifier)
			&& !_windowZoomActive && !_viewRotating && !_viewPanning && !_viewZooming)
		{
			// Selection
			_selectionManager->clickSelect(QPoint(e->position().x(), e->position().y()));
		}


		_rubberBand->setGeometry(QRect(_leftButtonPoint, QSize()));
		_rubberBand->show();
	}

	if ((e->button() & Qt::RightButton) || ((e->button() & Qt::LeftButton) && _viewPanning))
	{
		_rightButtonPoint.setX(e->position().x());
		_rightButtonPoint.setY(e->position().y());
		_lastPanPoint = e->pos();
	}

	if (e->button() & Qt::MiddleButton || ((e->button() & Qt::LeftButton) && _viewRotating))
	{
		_middleButtonPoint.setX(e->position().x());
		_middleButtonPoint.setY(e->position().y());
	}
}

void GLWidget::mouseReleaseEvent(QMouseEvent* e)
{
	if (e->button() & Qt::LeftButton)
	{
        _rubberBand->hide();
		if (_windowZoomActive)
		{
			performWindowZoom();
		}
		else if (!(e->modifiers() & Qt::ControlModifier) && !_viewRotating && !_viewPanning && !_viewZooming)
		{
			// Sweep select: check shift status at release time to determine if we should add to selection
			bool shiftHeldAtRelease = (e->modifiers() & Qt::ShiftModifier) != 0;
			// Prefer the current shift state at release time over the state at press time
			bool addToSelection = shiftHeldAtRelease || _shiftDragActive;

			sweepSelect(e->pos(), addToSelection);
			_shiftDragActive = false;  // Reset the flag
		}
	}

	if (e->button() & Qt::RightButton)
	{
		_lastPanPoint = e->pos();
	}

	if (e->button() & Qt::MiddleButton)
	{
		if (e->modifiers() == Qt::NoModifier)
		{
			if (!_multiViewActive)
			{
				QPoint o(width() / 2, height() / 2);
				QPoint p = e->pos();

				QVector3D OP = get3dTranslationVectorFromMousePoints(o, p);
				_primaryCamera->move(OP.x(), OP.y(), OP.z());
				_currentTranslation = _primaryCamera->getPosition();
				update();
			}
		}
	}

	_lowResEnabled = false;
	if (!_viewRotating && !_viewPanning && !_viewZooming)
	{
		setCursor(QCursor(Qt::ArrowCursor));
	}

	// Only start inertia if mouse was moving recently
	qint64 now = e->timestamp();
	const qint64 maxIdleMs = 50; // adjust as needed
	bool recentMove = (_lastMouseMoveTime > 0) && ((now - _lastMouseMoveTime) < maxIdleMs);

	// Start inertia if velocity is significant
	if (_mouseMovedSincePress && recentMove &&
		(_inertiaPanVelocity.lengthSquared() > 1.0f ||
			std::abs(_inertiaZoomVelocity) > 0.01f ||
			_inertiaRotateVelocity.lengthSquared() > 1.0f))
	{
		if (_inertiaTimer) _inertiaTimer->start();
	}
	else if (_sectionCapsSuppressedDuringInteraction)
	{
		QTimer::singleShot(100, this, &GLWidget::disableSectionCapsInteractionSuppression);
	}

	update();
}

void GLWidget::mouseMoveEvent(QMouseEvent* e)
{
	_mouseMovedSincePress = true;
	_lastMouseMoveTime = e->timestamp();
	static QPoint lastPos = e->pos();
	static qint64 lastTime = e->timestamp();
	QPoint currentPos = e->pos();
	qint64 currentTime = e->timestamp();
	QPoint delta = currentPos - lastPos;
	float dt = (currentTime - lastTime) / 1000.0f; // seconds

	QPoint downPoint(e->position().x(), e->position().y());
	if (e->buttons() == Qt::LeftButton && !_viewPanning && !_viewZooming)
	{
		if (!(e->modifiers() & Qt::ControlModifier) && !_viewRotating && !_viewPanning && !_viewZooming)
		{
            _rubberBand->setGeometry(QRect(_leftButtonPoint, e->pos()).normalized());
		}
		if (_windowZoomActive)
		{
			setCursor(QCursor(QPixmap(":/icons/res/window-zoom-cursor.png"), 12, 12));
		}
		else if ((e->modifiers() & Qt::ControlModifier) || _viewRotating)
		{
			if (_displayedObjectsMemSize > MAX_MODEL_SIZE_BYTES)
				_lowResEnabled = true;
			setSectionCapsInteractionSuppressed(true);
			QPoint rotate = _leftButtonPoint - downPoint;

			if (_primaryCamera->getMode() == GLCamera::CameraMode::Orbit)
			{
				_primaryCamera->rotateX(rotate.y() / 2.0);
				_primaryCamera->rotateY(rotate.x() / 2.0);
			}
			else if (_primaryCamera->getMode() == GLCamera::CameraMode::Fly || _primaryCamera->getMode() == GLCamera::CameraMode::FirstPerson)
			{
				_primaryCamera->getYaw() += rotate.x() * 0.2f;
				_primaryCamera->getPitch() += rotate.y() * 0.2f;

				if (_primaryCamera->getMode() == GLCamera::CameraMode::FirstPerson)
					_primaryCamera->getPitch() = std::clamp(_primaryCamera->getPitch(), -60.0f, 60.0f);
				else
					_primaryCamera->getPitch() = std::clamp(_primaryCamera->getPitch(), -89.0f, 89.0f);

				_primaryCamera->updateFlyView();
			}


			_currentRotation = QQuaternion::fromRotationMatrix(_primaryCamera->getViewMatrix().toGenericMatrix<3, 3>());
			_leftButtonPoint = downPoint;
			setCursor(QCursor(QPixmap(":/icons/res/rotatecursor.png")));
			_viewMode = ViewMode::NONE;

			const float maxInertiaVelocity = 10.0f; // Adjust as needed
			if (dt > 0) {
				_inertiaRotateVelocity = -QVector2D(delta) / dt;
				if (_inertiaRotateVelocity.length() > maxInertiaVelocity)
					_inertiaRotateVelocity = _inertiaRotateVelocity.normalized() * maxInertiaVelocity;
			}
		}

		update();
	}
	else if ((e->buttons() == Qt::RightButton && e->modifiers() & Qt::ControlModifier) || (e->buttons() == Qt::LeftButton && _viewPanning))
	{
		if (_displayedObjectsMemSize > MAX_MODEL_SIZE_BYTES)
			_lowResEnabled = true;
		setSectionCapsInteractionSuppressed(true);
		QVector3D OP = get3dTranslationVectorFromMousePoints(downPoint, _rightButtonPoint);
		_primaryCamera->move(OP.x(), OP.y(), OP.z());
		_currentTranslation = _primaryCamera->getPosition();

		_rightButtonPoint = downPoint;
		setCursor(QCursor(QPixmap(":/icons/res/pancursor.png")));

		// Clamp pan inertia velocity
		const float maxPanInertiaVelocity = 20.0f; // Adjust as needed
		if (dt > 0) {
			_inertiaPanVelocity = QVector2D(delta) / dt;
			if (_inertiaPanVelocity.length() > maxPanInertiaVelocity)
				_inertiaPanVelocity = _inertiaPanVelocity.normalized() * maxPanInertiaVelocity;

			_inertiaZoomPanVelocity = OP;
		}

		update();
	}
	else if ((e->buttons() == Qt::MiddleButton && e->modifiers() & Qt::ControlModifier) || (e->buttons() == Qt::LeftButton && _viewZooming))
	{
		if (_displayedObjectsMemSize > MAX_MODEL_SIZE_BYTES)
			_lowResEnabled = true;
		setSectionCapsInteractionSuppressed(true);
		// Zoom
		if (downPoint.x() > _middleButtonPoint.x() || downPoint.y() < _middleButtonPoint.y()) {
			_viewRange /= 1.05f;
			_lastZoomDirection = 1;
		}
		else {
			_viewRange *= 1.05f;
			_lastZoomDirection = -1;
		}
		
		if (_viewRange < _boundingSphere.getRadius() / 100.0f)
			_viewRange = _boundingSphere.getRadius() / 100.0f;
		if (_viewRange > _boundingSphere.getRadius() * 100.0f)
			_viewRange = _boundingSphere.getRadius() * 100.0f;
		_currentViewRange = _viewRange;

		// Translate to focus on mouse center
		QPoint cen = getClientRectFromPoint(downPoint).center();
		float sign = (downPoint.x() > _middleButtonPoint.x() || downPoint.y() < _middleButtonPoint.y()) ? 1.0f : -1.0f;
		QVector3D OP = get3dTranslationVectorFromMousePoints(cen, _middleButtonPoint);
		OP *= sign * 0.05f;
		_primaryCamera->move(OP.x(), OP.y(), OP.z());
		_currentTranslation = _primaryCamera->getPosition();
		_lastZoomPanVector = OP; // Store for inertia

		if (dt > 0) {
			_inertiaZoomVelocity = _lastZoomDirection; // +1 or -1
		}

		resizeGL(width(), height());

		_middleButtonPoint = downPoint;
		setCursor(QCursor(QPixmap(":/icons/res/zoomcursor.png")));

		update();
	}
	else
	{
		_lowResEnabled = false;
	}


	// Auto-hide/show the view toolbar
	if (_viewToolbar && e->buttons() == Qt::NoButton)
	{
		const int revealMargin = 30; // e.g., 30 px threshold

		QRect hidden = _viewToolbar->hiddenRect();
		QRect revealArea(hidden.left(), hidden.top() - revealMargin, hidden.width(), revealMargin * 2);

		if (revealArea.contains(e->pos()) || _viewToolbar->underMouse())
		{
			_viewToolbar->showAnimated();
		}
		else
		{
			// Store the timer as a member (optional) to manage it better
			auto timer = new QTimer(this);
			timer->setSingleShot(true);
			connect(timer, &QTimer::timeout, this, [this, timer]() {
				if (!_viewToolbar)
				{
					timer->deleteLater(); // Clean up the timer
					return; // Exit safely
				}

				QPoint globalPos = QCursor::pos();
				QPoint localPos = mapFromGlobal(globalPos);
				QRect hidden = _viewToolbar->hiddenRect();
				QRect revealArea(hidden.left(), hidden.top() - 30, hidden.width(), 60);

				bool isFlyoutVisible = _viewToolbar->isFlyoutMenuVisible();

				if (!revealArea.contains(localPos) &&
					!_viewToolbar->underMouse() &&
					!isFlyoutVisible)
				{
					_viewToolbar->hideAnimated();
				}

				timer->deleteLater(); // Clean up the timer
				});

			// Start the timer
			timer->start(2000);

			// Ensure proper cleanup of the timer if the toolbar is deleted
			connect(_viewToolbar, &QObject::destroyed, timer, [timer]() {
				timer->stop();
				timer->deleteLater();
				});
		}
	}

	// Hover highlight feedback (visual preview, independent of actual selection)
	if (e->buttons() == Qt::NoButton && _selectionManager->getHoverMode() != HoverHighlightMode::Disabled)
	{
		// Compute hovered mesh (SelectionManager will emit hoverChanged signal if it changed)
		_selectionManager->hoverSelect(e->pos());
	}

	update();

	lastPos = currentPos;
	lastTime = currentTime;
}

void GLWidget::wheelEvent(QWheelEvent* e)
{
	// Stop any ongoing inertia when wheel zooming
	_inertiaRotateVelocity = QVector2D(0, 0);
	_inertiaPanVelocity = QVector2D(0, 0);
	_inertiaZoomVelocity = 0.0f;
	if (_inertiaTimer && _inertiaTimer->isActive())
		_inertiaTimer->stop();	

	if (_displayedObjectsMemSize > MAX_MODEL_SIZE_BYTES)
		_lowResEnabled = true;
	setSectionCapsInteractionSuppressed(true);
	// Zoom
	QPoint numDegrees = e->angleDelta() / 8;
	QPoint numSteps = numDegrees / 30;
	float zoomStep = numSteps.y();
	float zoomFactor = abs(zoomStep) + 0.05;

	if (zoomStep < 0)
		_viewRange *= zoomFactor;
	else
		_viewRange /= zoomFactor;

	if (_viewRange < _boundingSphere.getRadius() / 100.0f)
		_viewRange = _boundingSphere.getRadius() / 100.0f;
	if (_viewRange > _boundingSphere.getRadius() * 100.0f)
		_viewRange = _boundingSphere.getRadius() * 100.0f;

	_currentViewRange = _viewRange;

	// Translate to focus on mouse center
	QPoint cen = getClientRectFromPoint(e->position().toPoint()).center();
	float sign = (e->position().x() > cen.x() || e->position().y() < cen.y() ||
		(e->position().x() < cen.x() && e->position().y() > cen.y())) && (zoomStep > 0) ? 1.0f : -1.0f;
	QVector3D OP = get3dTranslationVectorFromMousePoints(cen, e->position().toPoint());
	OP *= sign * 0.05f;
	_primaryCamera->move(OP.x(), OP.y(), OP.z());
	_currentTranslation = _primaryCamera->getPosition();

	// Add inertia for wheel zoom
	_inertiaZoomVelocity = (e->angleDelta().y() / 120.0f) * 0.05f; // scale as needed
	if (_inertiaTimer) _inertiaTimer->start();

	_inertiaZoomPanVelocity += OP * sign * 0.05f;

	resizeGL(width(), height());
	update();
}

void GLWidget::keyPressEvent(QKeyEvent* event)
{
	QWidget::keyPressEvent(event);

	const auto key = event->key();

	if (key == Qt::Key_Escape)
	{
		_viewRotating = false;
		_viewPanning = false;
		_viewZooming = false;
		_windowZoomActive = false;
		setCursor(QCursor(Qt::ArrowCursor));
		MainWindow::showStatusMessage("");

		_selectedIDs.clear();  // Clear selection in GLWidget
		_viewer->deselectAll();  // Also clear in viewer
	}
	else if (key == Qt::Key_F)
		fitAll();
	else if (key == Qt::Key_Delete)
		_viewer->deleteSelectedItems();
	else if (key == Qt::Key_Space)
	{
		if (event->modifiers() & Qt::ShiftModifier)
			_viewer->showOnlySelectedItems();
		else
			_visibleSwapped ? _viewer->showSelectedItems() : _viewer->hideSelectedItems();
	}
	else if (key == Qt::Key_S && (event->modifiers() & Qt::AltModifier))
	{
		swapVisible(!_visibleSwapped);
	}
	else
		_keys.insert(key);

	// Camera mode switching
	if (key == Qt::Key_1) setCameraMode(GLCamera::CameraMode::Orbit);
	if (key == Qt::Key_2) setCameraMode(GLCamera::CameraMode::Fly);
	if (key == Qt::Key_3) setCameraMode(GLCamera::CameraMode::FirstPerson);


	update();
}

void GLWidget::keyReleaseEvent(QKeyEvent* event)
{
	_keys.remove(event->key());
	QWidget::keyReleaseEvent(event);
}

void GLWidget::performKeyboardNav()
{
	if (_keys.empty() == false && QApplication::keyboardModifiers() == Qt::NoModifier)
	{
		float factor = _viewRange * 0.01f;
		// https://forum.qt.io/topic/28327/big-issue-with-qt-key-inputs-for-gaming/4
		if (_primaryCamera->getMode() == GLCamera::CameraMode::Fly || _primaryCamera->getMode() == GLCamera::CameraMode::FirstPerson)
		{
			if (_keys.contains(Qt::Key_W))
				_primaryCamera->moveForward(factor);
			if (_keys.contains(Qt::Key_S))
				_primaryCamera->moveForward(-factor);
			if (_keys.contains(Qt::Key_A))
				_primaryCamera->moveAcross(factor);
			if (_keys.contains(Qt::Key_D))
				_primaryCamera->moveAcross(-factor);

			if (_primaryCamera->getMode() == GLCamera::CameraMode::Fly)
			{
				if (_keys.contains(Qt::Key_Q))
					_primaryCamera->moveUpward(-factor);
				if (_keys.contains(Qt::Key_E))
					_primaryCamera->moveUpward(factor);
			}
		}
		else
		{
			// Use Orbit-style orthographic nav (as before)
			if (_keys.contains(Qt::Key_A))
				_primaryCamera->moveAcross(factor);
			if (_keys.contains(Qt::Key_D))
				_primaryCamera->moveAcross(-factor);
			if (_keys.contains(Qt::Key_W))
				_primaryCamera->moveUpward(-factor);
			if (_keys.contains(Qt::Key_S))
				_primaryCamera->moveUpward(factor);
		}

		if (_keys.contains(Qt::Key_J))
			_primaryCamera->rotateY(2.0f);
		if (_keys.contains(Qt::Key_L))
			_primaryCamera->rotateY(-2.0f);
		if (_keys.contains(Qt::Key_I))
			_primaryCamera->rotateX(2.0f);
		if (_keys.contains(Qt::Key_K))
			_primaryCamera->rotateX(-2.0f);
		if (_keys.contains(Qt::Key_M))
			_primaryCamera->rotateZ(2.0f);
		if (_keys.contains(Qt::Key_N))
			_primaryCamera->rotateZ(-2.0f);
		if (_keys.contains(Qt::Key_X) || _keys.contains(Qt::Key_Z))
		{
			if(_primaryCamera->getMode() == GLCamera::CameraMode::Orbit)
			{
				// Zoom only if Orbit camera mode
				if (_keys.contains(Qt::Key_X))
					_viewRange /= 1.05f;
				else
					_viewRange *= 1.05f;
				if (_viewRange < _boundingSphere.getRadius() / 100.0f)
					_viewRange = _boundingSphere.getRadius() / 100.0f;
				if (_viewRange > _boundingSphere.getRadius() * 100.0f)
					_viewRange = _boundingSphere.getRadius() * 100.0f;
				// Translate to focus on mouse center
				QPoint pos = mapFromGlobal(QCursor::pos());
				QPoint cen = getClientRectFromPoint(pos).center();
				float sign = (pos.x() > cen.x() || pos.y() < cen.y() ||
					(pos.x() < cen.x() && pos.y() > cen.y())) && _keys.contains(Qt::Key_Q) ? 1.0f : -1.0f;
				QVector3D OP = get3dTranslationVectorFromMousePoints(cen, pos);
				OP *= sign * 0.05f;
				_primaryCamera->move(OP.x(), OP.y(), OP.z());
			}
		}

		_currentViewRange = _viewRange;
		_currentTranslation = _primaryCamera->getPosition();
		_currentRotation = QQuaternion::fromRotationMatrix(_primaryCamera->getViewMatrix().toGenericMatrix<3, 3>());
		resizeGL(width(), height());
		update();
	}
}

void GLWidget::animateViewChange()
{
	setSectionCapsInteractionSuppressed(true);
	if (_displayedObjectsMemSize > MAX_MODEL_SIZE_BYTES)
		_lowResEnabled = true;
	if (_viewMode == ViewMode::TOP)
	{
		setRotations(0.0f, 0.0f, 0.0f);
	}
	if (_viewMode == ViewMode::BOTTOM)
	{
		setRotations(0.0f, 180.0f, 0.0f);
	}
	if (_viewMode == ViewMode::LEFT)
	{
		setRotations(0.0f, -90.0f, 90.0f);
	}
	if (_viewMode == ViewMode::RIGHT)
	{
		setRotations(0.0f, -90.0f, -90.0f);
	}
	if (_viewMode == ViewMode::FRONT)
	{
		setRotations(0.0f, -90.0f, 0.0f);
	}
	if (_viewMode == ViewMode::BACK)
	{
		setRotations(0.0f, -90.0f, 180.0f);
	}
	if (_viewMode == ViewMode::ISOMETRIC)
	{
        setRotations(-45.0f, -54.7356f, 0.0f);
	}
	if (_viewMode == ViewMode::DIMETRIC)
	{
        setRotations(-20.7048f, -70.5288f, 0.0f);
	}
	if (_viewMode == ViewMode::TRIMETRIC)
	{
        setRotations(-30.0f, -55.0f, 0.0f);
	}

	resizeGL(width(), height());
}

void GLWidget::animateFitAll()
{
	setSectionCapsInteractionSuppressed(true);
	if (_displayedObjectsMemSize > MAX_MODEL_SIZE_BYTES)
		_lowResEnabled = true;

	setZoomAndPan(_viewBoundingSphereDia, -_currentTranslation + _boundingSphere.getCenter());
	//fitBoxToScreen(_boundingBox);

	resizeGL(width(), height());
}

void GLWidget::animateWindowZoom()
{
	setSectionCapsInteractionSuppressed(true);
	if (_displayedObjectsMemSize > MAX_MODEL_SIZE_BYTES)
		_lowResEnabled = true;
	setZoomAndPan(_currentViewRange / _rubberBandZoomRatio, _rubberBandPan);
	resizeGL(width(), height());
}

void GLWidget::animateCenterScreen()
{
	setSectionCapsInteractionSuppressed(true);
	setZoomAndPan(_selectionBoundingSphere.getRadius() * 2, -_currentTranslation + _selectionBoundingSphere.getCenter());
	resizeGL(width(), height());
}

void GLWidget::onInertiaTimer()
{
	bool active = false;

	// --- Pan inertia ---
	if (_inertiaPanVelocity.lengthSquared() > 0.01f) {
		// Apply pan inertia from the last pan point, in the same way as interactive panning
		QPointF panDelta(-_inertiaPanVelocity.x(), -_inertiaPanVelocity.y());
		QPoint newPanPoint = _lastPanPoint + panDelta.toPoint();
		QVector3D OP = get3dTranslationVectorFromMousePoints(_lastPanPoint, newPanPoint);
		_primaryCamera->move(OP.x(), OP.y(), OP.z());
		_currentTranslation = _primaryCamera->getPosition();
		_lastPanPoint = newPanPoint; // Update for next frame
		_inertiaPanVelocity *= _inertiaDamping;
		active = true;
	}

	// --- Zoom inertia ---
	if (std::abs(_inertiaZoomVelocity) > 0.001f) {
		float zoomFactor = 1.005f;
		if (_inertiaZoomVelocity > 0)
			_viewRange /= zoomFactor;
		else
			_viewRange *= zoomFactor;

		QPoint cen = getViewportFromPoint(mapFromGlobal(QCursor::pos())).center();
		QVector3D OP = get3dTranslationVectorFromMousePoints(cen, cen);
		OP *= -_inertiaZoomPanVelocity * 0.05f;
		_primaryCamera->move(OP.x(), OP.y(), OP.z());
		_currentTranslation = _primaryCamera->getPosition();

		// Decay inertia
		_inertiaZoomVelocity *= _inertiaDamping * 0.1f;
		_inertiaZoomPanVelocity *= _inertiaDamping * 0.1f;

		if (std::abs(_inertiaZoomVelocity) > 0.001f)
			active = true;
		else
			_inertiaZoomVelocity = 0.0f;

		resizeGL(width(), height());

		// Clamp _viewRange as before
		float minRange = _boundingSphere.getRadius() / 100.0f;
		float maxRange = _boundingSphere.getRadius() * 100.0f;
		if (_viewRange < minRange) _viewRange = minRange;
		if (_viewRange > maxRange) _viewRange = maxRange;
		_currentViewRange = _viewRange;

		active = true;
	}

	// --- Rotation inertia ---
	if (_inertiaRotateVelocity.lengthSquared() > 0.01f) {
		_primaryCamera->rotateX(_inertiaRotateVelocity.y() / 2.0);
		_primaryCamera->rotateY(_inertiaRotateVelocity.x() / 2.0);
		_currentRotation = QQuaternion::fromRotationMatrix(_primaryCamera->getViewMatrix().toGenericMatrix<3, 3>());
		_inertiaRotateVelocity *= _inertiaDamping;
		active = true;
	}

	if (!active) {
		_inertiaTimer->stop();
		_inertiaPanVelocity = QVector2D();
		_inertiaZoomVelocity = 0.0f;
		_inertiaRotateVelocity = QVector2D();
		QTimer::singleShot(100, this, &GLWidget::disableSectionCapsInteractionSuppression);
	}

	update();
}

void GLWidget::stopAnimations()
{
	_animateViewTimer->stop();
	_animateFitAllTimer->stop();
	_animateWindowZoomTimer->stop();
	_animateCenterScreenTimer->stop();
	_keyboardNavTimer->start();
	QTimer::singleShot(100, this, &GLWidget::disableLowRes);
	QTimer::singleShot(100, this, &GLWidget::disableSectionCapsInteractionSuppression);
}


// Note: convertClickToRay is now implemented in SelectionManager
// Keeping getViewportFromPoint and getClientRectFromPoint as they're still used by GLWidget

QRect GLWidget::getViewportFromPoint(const QPoint& pixel)
{
	QRect viewport;
	if (_multiViewActive)
	{
		// top view
		if (pixel.x() < width() / 2 && pixel.y() > height() / 2)
			viewport = QRect(0, 0, width() / 2, height() / 2);
		// front view
		if (pixel.x() < width() / 2 && pixel.y() < height() / 2)
			viewport = QRect(0, height() / 2, width() / 2, height() / 2);
		// left view
		if (pixel.x() > width() / 2 && pixel.y() < height() / 2)
			viewport = QRect(width() / 2, height() / 2, width() / 2, height() / 2);
		// isometric
		if (pixel.x() > width() / 2 && pixel.y() > height() / 2)
			viewport = QRect(width() / 2, 0, width() / 2, height() / 2);
	}
	else
	{
		// single viewport
		viewport = QRect(0, 0, width(), height());
	}

	return viewport;
}

QRect GLWidget::getClientRectFromPoint(const QPoint& pixel)
{
	QRect clientRect;
	if (_multiViewActive)
	{
		// top view
		if (pixel.x() < width() / 2 && pixel.y() > height() / 2)
			clientRect = QRect(0, height() / 2, width() / 2, height() / 2);
		// front view
		if (pixel.x() < width() / 2 && pixel.y() < height() / 2)
			clientRect = QRect(0, 0, width() / 2, height() / 2);
		// left view
		if (pixel.x() > width() / 2 && pixel.y() < height() / 2)
			clientRect = QRect(width() / 2, 0, width() / 2, height() / 2);
		// isometric
		if (pixel.x() > width() / 2 && pixel.y() > height() / 2)
			clientRect = QRect(width() / 2, height() / 2, width() / 2, height() / 2);
	}
	else
	{
		// single viewport
		clientRect = QRect(0, 0, width(), height());
	}

	return clientRect;
}


QVector3D GLWidget::get3dTranslationVectorFromMousePoints(const QPoint& start, const QPoint& end)
{
	// Determine viewport and camera
	QRect viewport = getViewportFromPoint(start);
	GLCamera* camera = _multiViewActive && (viewport.x() != viewport.width() || viewport.y() != 0)
		? _orthoViewsCamera
		: _primaryCamera;

	QVector3D viewCenter = _boundingSphere.getCenter();
	// Get view and projection matrices
	QMatrix4x4 view = camera->getViewMatrix();
	QMatrix4x4 projection = camera->getProjectionMatrix();
	QMatrix4x4 inv = (projection * view).inverted();

	float ndcZ = 0.0f;
	if (camera->getProjectionType() == GLCamera::ProjectionType::ORTHOGRAPHIC) {
		QVector4D refWorld(0, 0, viewCenter.z(), 1.0f);
		QVector4D refClip = projection * view * refWorld;
		ndcZ = refClip.w() != 0.0f ? refClip.z() / refClip.w() : 0.0f;
	}
	else {
		// Project the actual model center to get its NDC Z
		QVector4D modelCenterWorld(viewCenter.x(), viewCenter.y(), viewCenter.z(), 1.0f);
		QVector4D modelCenterClip = projection * view * modelCenterWorld;
		ndcZ = modelCenterClip.w() != 0.0f ? modelCenterClip.z() / modelCenterClip.w() : 0.0f;
		
		// Clamp NDC Z to between -0.99 and 0.99 to avoid 
		// extreme values causing panning direction inversion
		ndcZ = qBound(-0.99f, ndcZ, 0.99f);
	}

	// Convert screen points to NDC
	auto toNDC = [&](const QPoint& pt) {
		int yInverted = height() - pt.y() - 1;
		float ndcX = (2.0f * (pt.x() - viewport.x())) / viewport.width() - 1.0f;
		float ndcY = (2.0f * (yInverted - viewport.y())) / viewport.height() - 1.0f;
		return QVector2D(ndcX, ndcY);
		};

	QVector2D ndcStart2 = toNDC(start);
	QVector2D ndcEnd2 = toNDC(end);
	QVector4D ndcStart(ndcStart2.x(), ndcStart2.y(), ndcZ, 1.0f);
	QVector4D ndcEnd(ndcEnd2.x(), ndcEnd2.y(), ndcZ, 1.0f);

	QVector4D worldStart = inv * ndcStart;
	QVector4D worldEnd = inv * ndcEnd;

	// Check if not inf or NaN before homogeneous divide
	if (worldStart.w() == 0.0f || std::isnan(worldStart.w()) || std::isinf(worldStart.w()))
		return QVector3D(0, 0, 0);
	if (worldEnd.w() == 0.0f || std::isnan(worldEnd.w()) || std::isinf(worldEnd.w()))
		return QVector3D(0, 0, 0);
	
	if (worldStart.w() != 0.0f) worldStart /= worldStart.w();
	if (worldEnd.w() != 0.0f) worldEnd /= worldEnd.w();

	return worldEnd.toVector3D() - worldStart.toVector3D();
}


unsigned int GLWidget::loadTextureFromFile(
	const char* path,
	GLenum wrapS, GLenum wrapT,
	GLenum minFilter, GLenum magFilter,
	bool flipY)
{
	GLuint textureID = 0;

	// Load image using Qt
	QImageReader reader(path);
	reader.setAutoTransform(true); // respects EXIF orientation

	QImage image = reader.read();
	if (image.isNull())
	{
		qWarning() << "Texture failed to load:" << path
			<< reader.errorString();
		return 0;
	}

	// Optional vertical flip (OpenGL vs Qt coordinate difference)
	if (flipY)
		image = image.mirrored(false, true);

	GLenum internalFormat = GL_RGBA8;
	GLenum dataFormat = GL_RGBA;
	GLenum dataType = GL_UNSIGNED_BYTE;

	QImage glImage;

	switch (image.format())
	{
	case QImage::Format_RGB888:
		glImage = image;
		internalFormat = GL_RGB8;
		dataFormat = GL_RGB;
		break;

	case QImage::Format_RGBA8888:
	case QImage::Format_RGBA8888_Premultiplied:
		glImage = image;
		internalFormat = GL_RGBA8;
		dataFormat = GL_RGBA;
		break;

	case QImage::Format_Grayscale8:
		glImage = image;
		internalFormat = GL_R8;
		dataFormat = GL_RED;
		break;

	case QImage::Format_Indexed8:
		glImage = image.convertToFormat(QImage::Format_RGBA8888);
		internalFormat = GL_RGBA8;
		dataFormat = GL_RGBA;
		break;

	default:
		// Fallback for uncommon formats (ARGB32, RGB32, etc.)
		glImage = image.convertToFormat(QImage::Format_RGBA8888);
		internalFormat = GL_RGBA8;
		dataFormat = GL_RGBA;
		break;
	}

	// Create OpenGL texture
	glGenTextures(1, &textureID);
	glBindTexture(GL_TEXTURE_2D, textureID);

	glTexImage2D(
		GL_TEXTURE_2D,
		0,
		internalFormat,
		glImage.width(),
		glImage.height(),
		0,
		dataFormat,
		dataType,
		glImage.constBits()
	);

	glGenerateMipmap(GL_TEXTURE_2D);

	// Texture parameters
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);

	// Anisotropic filtering (if supported)
	bool hasAnisotropy =
		context()->hasExtension("GL_EXT_texture_filter_anisotropic");

	if (hasAnisotropy)
	{
		GLfloat maxAniso = 0.0f;
		glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);

		GLfloat aniso = qMin(_anisotropicFilteringLevel, maxAniso);

		glTexParameterf(
			GL_TEXTURE_2D,
			GL_TEXTURE_MAX_ANISOTROPY_EXT,
			aniso
		);
	}

	glBindTexture(GL_TEXTURE_2D, 0);
	return textureID;
}

QList<int> GLWidget::sweepSelect(const QPoint& pixel, bool addToSelection)
{
	const auto& ids = _visibleSwapped ? _hiddenObjectsIds : _displayedObjectsIds;

	// Check if there's actually a rubber band with non-null geometry (actual drag)
	bool hasRubberBand = !ids.empty() && _rubberBand && !_rubberBand->geometry().isNull();

	// If no rubber band, return without modifying selection (click without drag case)
	if (!hasRubberBand) {
		return _selectedIDs;
	}

	// Only clear selection if NOT adding to existing selection (Shift not held)
	if (!addToSelection) {
		_selectedIDs.clear();  // Regular sweep: replace selection
	}
	// If Shift is held (addToSelection=true), DON'T clear - preserve existing selection

	const QRect rubberRect = _rubberBand->geometry();
	const QRect viewport(0, 0, width(), height());
	const QMatrix4x4 projMatrix = _projectionMatrix;
	const QMatrix4x4 viewMatrix = _viewMatrix * _modelMatrix;
	constexpr float SELECTION_THRESHOLD = 0.5f;

	QApplication::setOverrideCursor(Qt::WaitCursor);
	_selectedIDs.reserve(ids.size());

	for (int i : ids) // Iterate through each ID in the 'ids' collection.
	{
		TriangleMesh* mesh = _meshStore.at(i);

		BoundingSphere sphere = mesh->getBoundingSphere();
		QVector3D center = sphere.getCenter();
		float radius = sphere.getRadius();

		// Convert the 3D center into a 4D vector (homogeneous coordinates).
		QVector4D center4(center, 1.0f);
		// Project the center from 3D object space into clip space.
		QVector4D projectedCenter = projMatrix * viewMatrix * center4;

		// Check if the projected center is behind the camera (negative w-coordinate).
		if (projectedCenter.w() <= 0.0f)
			continue;

		// Convert the center from clip space to normalized device coordinates (NDC).
		QVector3D ndcCenter = projectedCenter.toVector3DAffine();
		QPointF screenCenter(
			(ndcCenter.x() * 0.5f + 0.5f) * viewport.width(), // Map NDC to screen coordinates along the X-axis.
			(1.0f - (ndcCenter.y() * 0.5f + 0.5f)) * viewport.height() // Map NDC to screen coordinates along the Y-axis (invert Y for screen space).
		);

		// Project the edge of the bounding sphere in the X direction to determine its radius in screen space.
		QVector4D edge4 = projMatrix * viewMatrix * QVector4D(center + QVector3D(radius, 0, 0), 1.0f);
		if (edge4.w() <= 0.0f) // Check if the projected edge is behind the camera.
			continue;

		// Convert the edge from clip space to normalized device coordinates (NDC).
		QVector3D ndcEdge = edge4.toVector3DAffine();
		QPointF screenEdge(
			(ndcEdge.x() * 0.5f + 0.5f) * viewport.width(), // Map NDC to screen coordinates along the X-axis for the edge.
			(1.0f - (ndcEdge.y() * 0.5f + 0.5f)) * viewport.height() // Map NDC to screen coordinates along the Y-axis for the edge.
		);

		// Calculate the radius in pixels based on the distance between the center and edge in screen space.
		float radiusPixels = QLineF(screenCenter, screenEdge).length();
		QRectF projectedRect(
			screenCenter.x() - radiusPixels, // Top-left X coordinate of the rectangle.
			screenCenter.y() - radiusPixels, // Top-left Y coordinate of the rectangle.
			2 * radiusPixels, // Width of the rectangle (2 * radius).
			2 * radiusPixels  // Height of the rectangle (2 * radius).
		);

		// Check if the projected rectangle is completely within the rubber rectangle.
		if (rubberRect.contains(projectedRect.toRect()))
		{
			// Add ID if not already selected (avoid duplicates)
			if (!_selectedIDs.contains(i))
				_selectedIDs.push_back(i);
		}
		else if (rubberRect.intersects(projectedRect.toRect())) // Check if the projected rectangle intersects the rubber rectangle.
		{
			QRectF intersected = rubberRect.intersected(projectedRect.toRect()); // Calculate the intersection rect between the two rectangles.
			float intersectArea = intersected.width() * intersected.height(); // Compute the area of the intersection.
			float projectedArea = projectedRect.width() * projectedRect.height(); // Compute the area of the projected rectangle.

			// Select the ID if the intersection area is significant enough.
			if (projectedArea > 0 && (intersectArea / projectedArea) >= SELECTION_THRESHOLD) {
				// Add ID if not already selected (avoid duplicates)
				if (!_selectedIDs.contains(i))
					_selectedIDs.push_back(i);
			}
		}
	}

	QApplication::restoreOverrideCursor();

	// Sync SelectionManager internal state without emitting signal (avoids feedback loops)
	_selectionManager->syncSelectedIds(_selectedIDs);

	// Emit sweepSelectionDone with the complete accumulated selection
	emit sweepSelectionDone(_selectedIDs);

	return _selectedIDs;
}



unsigned int GLWidget::colorToIndex(const QColor& color)
{
	int alpha = color.alpha();
	int red = color.red();
	int green = color.green();
	int blue = color.blue();
	unsigned int index =  ((alpha << 24) | (red << 16) | (green << 8) | (blue));
	return index;
}

QColor GLWidget::indexToColor(const unsigned int& index)
{
	int red = ((index >> 16) & 0xFF);
	int green = ((index >> 8) & 0xFF);
	int blue = (index & 0xFF);
	int alpha = ((index >> 24) & 0xFF);
	return QColor(red, green, blue, alpha);
}

void GLWidget::setView(QVector3D viewPos, QVector3D viewDir, QVector3D upDir, QVector3D rightDir)
{
	_primaryCamera->setView(viewPos, viewDir, upDir, rightDir);
	emit viewSet();
}

// Improved approach based on rubberband zoom technique
void GLWidget::fitBoxToScreen(const BoundingBox& box)
{			
	// Project bounding box corners to screen space
	std::vector<Point> corners = box.corners();
	std::vector<QVector3D> vcorners =	
	{
	QVector3D(corners[0].getX(), corners[0].getY(), corners[0].getZ()),
	QVector3D(corners[1].getX(), corners[1].getY(), corners[1].getZ()),
	QVector3D(corners[2].getX(), corners[2].getY(), corners[2].getZ()),
	QVector3D(corners[3].getX(), corners[3].getY(), corners[3].getZ()),
	QVector3D(corners[4].getX(), corners[4].getY(), corners[4].getZ()),
	QVector3D(corners[5].getX(), corners[5].getY(), corners[5].getZ()),
	QVector3D(corners[6].getX(), corners[6].getY(), corners[6].getZ()),
	QVector3D(corners[7].getX(), corners[7].getY(), corners[7].getZ())
	};

	QRect screenBounds;
	bool firstPoint = true;

	for (const auto& corner : vcorners)
	{
		// Project point to screen coordinates
		QVector4D clipCoords = _projectionMatrix * _viewMatrix * QVector4D(corner, 1.0f);

		QVector3D screenPoint(clipCoords.x() / clipCoords.w(),
                           clipCoords.y() / clipCoords.w(), 
                           clipCoords.z() / clipCoords.w());
				
		QPoint pixelPoint(
			static_cast<int>((clipCoords.x() + 1.0f) * 0.5f * width()),
			static_cast<int>(height() - (clipCoords.y() + 1.0f) * 0.5f * height())
		);

		// Update screen bounds
		if (firstPoint) {
			screenBounds = QRect(pixelPoint, QSize(1, 1));
			firstPoint = false;
		}
		else {
			screenBounds = screenBounds.united(QRect(pixelPoint, QSize(1, 1)));
		}
	}

	// Calculate client rect (full viewport)
	QRect clientRect(0, 0, width(), height());

	// Calculate zoom ratio using the same approach as window zoom
	double widthRatio = static_cast<double>(clientRect.width()) / screenBounds.width();
	double heightRatio = static_cast<double>(clientRect.height()) / screenBounds.height();

	// Use the smaller ratio to ensure the box fits in both dimensions
	// Apply a factor of 0.95 to leave a small margin around the object
	double zoomRatio = std::min(widthRatio, heightRatio) * 0.95;
		
	// Get center points for screen and box
	QPoint screenCenter = screenBounds.center();
	QPoint viewportCenter = clientRect.center();

	// Convert pan offset to 3D space
	// First, get world coordinates at screen center with current Z
	QVector3D screenCenterWorld(screenCenter.x(), height() - screenCenter.y(), 0.5);
	QVector3D viewportCenterWorld(viewportCenter.x(), height() - viewportCenter.y(), 0.5);

	// Unproject both points to get world coordinates
	QVector3D screenCenterPoint = screenCenterWorld.unproject(_viewMatrix * _modelMatrix, _projectionMatrix, QRect(0, 0, width(), height()));
	QVector3D viewportCenterPoint = viewportCenterWorld.unproject(_viewMatrix * _modelMatrix, _projectionMatrix, QRect(0, 0, width(), height()));

	// Calculate the pan vector
	QVector3D panVector = screenCenterPoint - viewportCenterPoint;

	
	setZoomAndPan(_currentViewRange / zoomRatio, panVector);
}


void GLWidget::setRotations(float xRot, float yRot, float zRot)
{
	// Rotation
	QQuaternion targetRotation = QQuaternion::fromEulerAngles(yRot, zRot, xRot); //Pitch, Yaw, Roll
	QQuaternion curRot = QQuaternion::slerp(_currentRotation, targetRotation, _slerpStep += _slerpFrac);

	// Translation
	QVector3D curPos = _currentTranslation - (_slerpStep * _currentTranslation) + (_boundingSphere.getCenter() * _slerpStep);

	// Set camera vectors
	QMatrix4x4 rotMat = QMatrix4x4(curRot.toRotationMatrix());
	QVector3D viewDir = -rotMat.row(2).toVector3D();
	QVector3D upDir = rotMat.row(1).toVector3D();
	QVector3D rightDir = rotMat.row(0).toVector3D();
	_primaryCamera->setView(curPos, viewDir, upDir, rightDir);

	// Set zoom
	float scaleStep = (_currentViewRange - _viewBoundingSphereDia) * _slerpFrac;
	_viewRange -= scaleStep;

	if (qFuzzyCompare(_slerpStep, 1.0f))
	{
		// Set camera vectors
		QMatrix4x4 rotMat = QMatrix4x4(curRot.toRotationMatrix());
		QVector3D viewDir = -rotMat.row(2).toVector3D();
		QVector3D upDir = rotMat.row(1).toVector3D();
		QVector3D rightDir = rotMat.row(0).toVector3D();
		_primaryCamera->setView(curPos, viewDir, upDir, rightDir);

		// Set all defaults
		_currentRotation = QQuaternion::fromRotationMatrix(_primaryCamera->getViewMatrix().toGenericMatrix<3, 3>());
		_currentTranslation = _primaryCamera->getPosition();
		_currentViewRange = _viewRange;
		_slerpStep = 0.0f;

		emit rotationsSet();
	}
}

void GLWidget::setZoomAndPan(float zoom, QVector3D pan)
{
	_slerpStep += _slerpFrac;

	// Translation
	QVector3D curPos = pan * _slerpFrac;
	_primaryCamera->move(curPos.x(), curPos.y(), curPos.z());

	// Set zoom
	float scaleStep = (_currentViewRange - zoom) * _slerpFrac;
	_viewRange -= scaleStep;

	if (qFuzzyCompare(_slerpStep, 1.0f))
	{
		// Set all defaults
		_currentTranslation = _primaryCamera->getPosition();
		_currentViewRange = _viewRange;
		_slerpStep = 0.0f;

		emit zoomAndPanSet();
	}
}

void GLWidget::closeEvent(QCloseEvent* event)
{
	event->accept();
}

bool GLWidget::areLightsShown() const
{
	return _showLights;
}

void GLWidget::showLights(bool showLights)
{
	_showLights = showLights;
	update();
}

void GLWidget::useDefaultLights(bool useDefaultLights)
{
	_useDefaultLights = useDefaultLights;	
	update();
}

void GLWidget::usePunctualLights(bool usePunctualLights)
{
	_usePunctualLights = usePunctualLights;
	update();
}

void GLWidget::useIBL(bool useIBL)
{
	_useIBL = useIBL;
	update();
}

float GLWidget::getScreenGamma() const
{
	return _screenGamma;
}

void GLWidget::setScreenGamma(double screenGamma)
{
	_screenGamma = static_cast<float>(screenGamma);
	update();
}

void GLWidget::setHDRToneMappingMode(HDRToneMapMode mode)
{
	_toneMappingMode = mode;
	update();
}

void GLWidget::setEnvMapExposure(double exposure)
{
	_envMapExposure = pow(2.0f, exposure);
	update();
}

void GLWidget::setIBLExposure(double exposure)
{
	_iblExposure = pow(2.0f, exposure);
	update();
}

bool GLWidget::getGammaCorrection() const
{
	return _gammaCorrection;
}

void GLWidget::enableGammaCorrection(bool gammaCorrection)
{
	_gammaCorrection = gammaCorrection;
	update();
}

bool GLWidget::getHdrToneMapping() const
{
	return _hdrToneMapping;
}

void GLWidget::enableHDRToneMapping(bool hdrToneMapping)
{
	_hdrToneMapping = hdrToneMapping;
	update();
}

RenderingMode GLWidget::getRenderingMode() const
{
	return _renderingMode;
}

void GLWidget::setRenderingMode(const RenderingMode& renderingMode)
{
	_renderingMode = renderingMode;
	_fgShader->bind();
	_fgShader->setUniformValue("renderingMode", static_cast<int>(_renderingMode));

	// Mark textures as dirty to ensure they are reloaded
	for (auto mesh : _meshStore)
	{
		mesh->markTexturesDirty();
		mesh->markUniformsDirty();
	}

	_fgShader->release();
	update();
}

void GLWidget::setFloorTexRepeatT(double floorTexRepeatT)
{
	_floorTexRepeatT = static_cast<float>(floorTexRepeatT);
	updateFloorPlane();
	update();
}

void GLWidget::setFloorTexRepeatS(double floorTexRepeatS)
{
	_floorTexRepeatS = static_cast<float>(floorTexRepeatS);
	updateFloorPlane();
	update();
}

void GLWidget::setFloorOffsetPercent(double value)
{
	_floorOffsetPercent = static_cast<float>(value / 100.0f);
	updateFloorPlane();
	update();
}

void GLWidget::setSkyBoxFOV(double fov)
{
	_skyBoxFOV = static_cast<float>(fov);
	update();
}

void GLWidget::setSkyBoxTextureHDRI(bool hdrSet)
{
	_skyBoxTextureHDRI = hdrSet;
	update();
}

QColor GLWidget::getBgBotColor() const
{
	return _bgBotColor;
}

void GLWidget::setBgBotColor(const QColor& bgBotColor)
{
	_bgBotColor = bgBotColor;

	QColor contrastColor = (_bgBotColor.lightnessF() < 0.5)
						   ? QColor(255, 255, 255)
						   : QColor(0, 0, 0);
	_clippingPlanesEditor->applyContrastTheme(contrastColor);

	if (QTabWidget* tabs = _viewer->findChild<QTabWidget*>("tabWidget")) {
		const QString tabStyleSheet = QString("color: rgb(%1, %2, %3);")
									  .arg(contrastColor.red())
									  .arg(contrastColor.green())
									  .arg(contrastColor.blue()) +
									  "background-color: rgba(255, 255, 255, 0);";
		tabs->setStyleSheet(tabStyleSheet);
	}

	refreshNavigationOverlayStyle();

	update();
}

QColor GLWidget::getBgTopColor() const
{
	return _bgTopColor;
}

void GLWidget::setBgTopColor(const QColor& bgTopColor)
{
	_bgTopColor = bgTopColor;
	refreshNavigationOverlayStyle();
	update();
}

BoundingSphere GLWidget::getBoundingSphere() const
{
	return _boundingSphere;
}

std::vector<int> GLWidget::getDisplayedObjectsIds() const
{
	return _displayedObjectsIds;
}

bool GLWidget::isVisibleSwapped() const
{
	return _visibleSwapped;
}

void GLWidget::setShowFaceNormals(bool showFaceNormals)
{
	_showFaceNormals = showFaceNormals;
}

bool GLWidget::isFaceNormalsShown() const
{
	return _showFaceNormals;
}

bool GLWidget::isVertexNormalsShown() const
{
	return _showVertexNormals;
}

void GLWidget::setShowVertexNormals(bool showVertexNormals)
{
	_showVertexNormals = showVertexNormals;
}

bool GLWidget::isShaded() const
{
	return _displayMode == DisplayMode::SHADED;
}

DisplayMode GLWidget::getDisplayMode() const
{
	return _displayMode;
}

void GLWidget::setDisplayMode(DisplayMode mode)
{
	_displayMode = mode;

	if (_viewToolbar)
		_viewToolbar->setDefaultDisplayModeAction(static_cast<DisplayModeActions>(_displayMode));
		
	_fgShader->bind();
	_fgShader->setUniformValue("displayMode", static_cast<int>(_displayMode));
	_fgShader->release();
	emit displayModeChanged(static_cast<int>(_displayMode));
}

void GLWidget::setTransmissionEnabled(const bool& enabled)
{
	_transmissionEnabled = enabled;	
	initTransmissionBuffer();
	update();
}

float GLWidget::getZScale() const
{
	return _zScale;
}

void GLWidget::setZScale(const float& zScale)
{
	_zScale = zScale;
}

float GLWidget::getYScale() const
{
	return _yScale;
}

void GLWidget::setYScale(const float& yScale)
{
	_yScale = yScale;
}

float GLWidget::getXScale() const
{
	return _xScale;
}

void GLWidget::setXScale(const float& xScale)
{
	_xScale = xScale;
}

float GLWidget::getZRot() const
{
	return _zRot;
}

void GLWidget::setZRot(const float& zRot)
{
	_zRot = zRot;
}

float GLWidget::getYRot() const
{
	return _yRot;
}

void GLWidget::setYRot(const float& yRot)
{
	_yRot = yRot;
}

float GLWidget::getXRot() const
{
	return _xRot;
}

void GLWidget::setXRot(const float& xRot)
{
	_xRot = xRot;
}

float GLWidget::getZTran() const
{
	return _zTran;
}

void GLWidget::setZTran(const float& zTran)
{
	_zTran = zTran;
}

float GLWidget::getYTran() const
{
	return _yTran;
}

void GLWidget::setYTran(const float& yTran)
{
	_yTran = yTran;
}


float GLWidget::getXTran() const
{
	return _xTran;
}

void GLWidget::setXTran(const float& xTran)
{
	_xTran = xTran;
}

float GLWidget::highestModelZ()
{
	return _visibleHighestZ;
}

float GLWidget::lowestModelZ()
{
	return _visibleLowestZ;
}

void GLWidget::showContextMenu(const QPoint& pos)
{
	if (QApplication::keyboardModifiers() != Qt::ControlModifier)
	{
		// Create menu and insert some actions
		QMenu contextMenu;
		SceneTreeWidget* treeWidgetModel = _viewer->getTreeModel();
		if (treeWidgetModel->hasMeshSelection() &&
			(_visibleSwapped ? _hiddenObjectsIds.size() != 0 : _displayedObjectsIds.size() != 0))
		{
			contextMenu.addAction(tr("Center Screen"), _viewer, &ModelViewer::centerScreen);
			QList<QUuid> selUuids = treeWidgetModel->selectedMeshUuids();
			if (selUuids.count() <= 1)
			{
				// Show "Center Object List" only when the selected mesh is visible
				QSet<QUuid> visibleUuids = treeWidgetModel->getVisibleUuids();
				if (selUuids.isEmpty() || visibleUuids.contains(selUuids.first()))
					contextMenu.addAction(tr("Center Object List"), this, &GLWidget::centerDisplayList);
			}
			contextMenu.addSeparator();
			if (_visibleSwapped)
				contextMenu.addAction(tr("Show"), _viewer, &ModelViewer::showSelectedItems);
			else
				contextMenu.addAction(tr("Hide"), _viewer, &ModelViewer::hideSelectedItems);
			if (_displayedObjectsIds.size() > 1)
				contextMenu.addAction(tr("Show Only"), _viewer, &ModelViewer::showOnlySelectedItems);
			contextMenu.addSeparator();
			contextMenu.addAction(tr("Visualization Settings"), _viewer, &ModelViewer::showVisualizationModelPage);
			contextMenu.addAction(tr("Transformations"), _viewer, &ModelViewer::showTransformationsPage);			
			contextMenu.addSeparator();			
			contextMenu.addAction(tr("Generate UVs"), _viewer, &ModelViewer::generateUVsForSelectedItems);
			contextMenu.addAction(tr("Duplicate"), _viewer, &ModelViewer::duplicateSelectedItems);
			contextMenu.addAction(tr("Delete"), _viewer, &ModelViewer::deleteSelectedItems);			
			contextMenu.addSeparator();
			contextMenu.addAction(tr("Mesh Info"), _viewer, &ModelViewer::displaySelectedMeshInfo);
		}
		else
		{
			QAction* action = nullptr;
			if ((!_visibleSwapped && _displayedObjectsIds.size() != 0) || (_visibleSwapped && _hiddenObjectsIds.size() != 0))
			{
				contextMenu.addAction(QIcon(":/icons/res/fit-all.png"), tr("Fit All"), this, &GLWidget::fitAll);

				action = contextMenu.addAction(QIcon(":/icons/res/window-zoom.png"), tr("Zoom Area"));
				action->setCheckable(true);
				connect(action, &QAction::triggered, this, &GLWidget::beginWindowZoom);

				// View manipulation actions				
				contextMenu.addSeparator();

				// If any of the view modes are active, add a menu item named select to disable them
				if (_viewZooming || _viewPanning || _viewRotating)
				{
					contextMenu.addSeparator();
					action = contextMenu.addAction(QIcon(":/icons/res/select.png"), tr("Select"));
					connect(action, &QAction::triggered, this, [this]() {
						setZoomingActive(false);
						setPanningActive(false);
						setRotationActive(false);
						setCursor(QCursor(Qt::ArrowCursor));
						});
				}
				action = contextMenu.addAction(QIcon(":/icons/res/zoomview.png"), tr("Zoom"));
				connect(action, &QAction::triggered, this, [this]() {
					setZoomingActive(true);
					});

				action = contextMenu.addAction(QIcon(":/icons/res/panview.png"), tr("Pan"));
				connect(action, &QAction::triggered, this, [this]() {
					setPanningActive(true);
					});

				action = contextMenu.addAction(QIcon(":/icons/res/rotateview.png"), tr("Rotate"));
				connect(action, &QAction::triggered, this, [this]() {
					setRotationActive(true);
					});								

				contextMenu.addSeparator();
			}			

			if (_hiddenObjectsIds.size() != 0)
			{
				action = contextMenu.addAction(QIcon(":/icons/res/showall.png"), tr("Show All"), _viewer, &ModelViewer::showAllItems);
				action->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_A));
			}
			if (_displayedObjectsIds.size() != 0)
			{
				action = contextMenu.addAction(QIcon(":/icons/res/hideall.png"), tr("Hide All"), _viewer, &ModelViewer::hideAllItems);
				action->setShortcut(QKeySequence(Qt::ALT | Qt::Key_A));
			}
			if (_hiddenObjectsIds.size() != 0)
			{
				action = contextMenu.addAction(QIcon(":/icons/res/swapvisible.png"), tr("Swap Visible"));
				action->setCheckable(true);
				action->setShortcut(QKeySequence(Qt::ALT | Qt::Key_S));
				action->setChecked(_visibleSwapped);				
				connect(action, &QAction::triggered, this, [this](bool enabled) {
					swapVisible(enabled);
					});
			}
			contextMenu.addSeparator();
			contextMenu.addAction(tr("Background Color"), this, &GLWidget::setBackgroundColor);
		}
		// Show context menu at handling position
		contextMenu.exec(mapToGlobal(pos));
	}
}

void GLWidget::centerDisplayList()
{
	SceneTreeWidget* treeWidgetModel = _viewer->getTreeModel();
	if (treeWidgetModel && treeWidgetModel->hasMeshSelection())
		treeWidgetModel->scrollFirstSelectedToCenter();
}

#include "BackgroundColor.h"
void GLWidget::setBackgroundColor()
{
	BackgroundColor bgCol(this);
	bgCol.exec();
}

// ---------------------------------------------------------------------------
// MVF3 mesh loader
// ---------------------------------------------------------------------------

#include "MvfDocument.h"
#include <QFile>
#include <QTemporaryDir>
#include <cstring>

namespace
{
// Read count*componentsOf(type) floats from geometryChunk via an accessor index.
static std::vector<float> readFloatStream(const QByteArray& chunk,
                                           const QJsonArray& accessors,
                                           const QJsonArray& bufferViews,
                                           int accessorIndex)
{
    if (accessorIndex < 0 || accessorIndex >= accessors.size())
        return {};
    const QJsonObject acc = accessors[accessorIndex].toObject();
    const int bvIdx = acc[QStringLiteral("bufferView")].toInt(-1);
    if (bvIdx < 0 || bvIdx >= bufferViews.size())
        return {};
    const QJsonObject bv = bufferViews[bvIdx].toObject();
    if (bv[QStringLiteral("buffer")].toInt(-1) != 0)
        return {};  // not the GEOM buffer

    const int bvOffset   = (int)bv[QStringLiteral("byteOffset")].toDouble(0);
    const int accOffset  = (int)acc[QStringLiteral("byteOffset")].toDouble(0);
    const int byteOffset = bvOffset + accOffset;
    const int count      = (int)acc[QStringLiteral("count")].toDouble(0);

    const QString type = acc[QStringLiteral("type")].toString();
    int components = 1;
    if      (type == QLatin1String("VEC2")) components = 2;
    else if (type == QLatin1String("VEC3")) components = 3;
    else if (type == QLatin1String("VEC4")) components = 4;

    const int totalFloats = count * components;
    if (byteOffset + totalFloats * (int)sizeof(float) > chunk.size())
        return {};

    std::vector<float> result(totalFloats);
    std::memcpy(result.data(), chunk.constData() + byteOffset,
                totalFloats * sizeof(float));
    return result;
}

// Read count unsigned ints from geometryChunk via an accessor index.
static std::vector<unsigned int> readUIntStream(const QByteArray& chunk,
                                                 const QJsonArray& accessors,
                                                 const QJsonArray& bufferViews,
                                                 int accessorIndex)
{
    if (accessorIndex < 0 || accessorIndex >= accessors.size())
        return {};
    const QJsonObject acc = accessors[accessorIndex].toObject();
    const int bvIdx = acc[QStringLiteral("bufferView")].toInt(-1);
    if (bvIdx < 0 || bvIdx >= bufferViews.size())
        return {};
    const QJsonObject bv = bufferViews[bvIdx].toObject();
    if (bv[QStringLiteral("buffer")].toInt(-1) != 0)
        return {};

    const int bvOffset   = (int)bv[QStringLiteral("byteOffset")].toDouble(0);
    const int accOffset  = (int)acc[QStringLiteral("byteOffset")].toDouble(0);
    const int byteOffset = bvOffset + accOffset;
    const int count      = (int)acc[QStringLiteral("count")].toDouble(0);

    if (byteOffset + count * (int)sizeof(unsigned int) > chunk.size())
        return {};

    std::vector<unsigned int> result(count);
    std::memcpy(result.data(), chunk.constData() + byteOffset,
                count * sizeof(unsigned int));
    return result;
}

// Apply a texture info JSON object to the right GLMaterial path setter.
static void applyTextureRef(GLMaterial& mat,
                             GLMaterial::TextureType type,
                             const QJsonObject& texInfo,
                             const QHash<int, QString>& imagePaths,
                             const QJsonArray& textures,
                             const QJsonArray& samplers)
{
    const int texIndex = texInfo[QStringLiteral("index")].toInt(-1);
    if (texIndex < 0 || texIndex >= textures.size())
        return;
    const QJsonObject texObj   = textures[texIndex].toObject();
    const int imgIndex         = texObj[QStringLiteral("image")].toInt(-1);
    const int samplerIndex     = texObj[QStringLiteral("sampler")].toInt(-1);
    const QString path         = imagePaths.value(imgIndex);
    if (path.isEmpty())
        return;

    switch (type)
    {
    case GLMaterial::TextureType::Albedo:                   mat.setAlbedoMap(path); break;
    case GLMaterial::TextureType::Normal:                   mat.setNormalMap(path); break;
    case GLMaterial::TextureType::AmbientOcclusion:         mat.setAOMap(path); break;
    case GLMaterial::TextureType::Emissive:                 mat.setEmissiveMap(path); break;
    case GLMaterial::TextureType::Metallic:                 mat.setMetallicMap(path); break;
    case GLMaterial::TextureType::Roughness:                mat.setRoughnessMap(path); break;
    case GLMaterial::TextureType::Transmission:             mat.setTransmissionMap(path); break;
    case GLMaterial::TextureType::IOR:                      mat.setIORMap(path); break;
    case GLMaterial::TextureType::SheenColor:               mat.setSheenColorMap(path); break;
    case GLMaterial::TextureType::SheenRoughness:           mat.setSheenRoughnessMap(path); break;
    case GLMaterial::TextureType::ClearcoatColor:           mat.setClearcoatColorMap(path); break;
    case GLMaterial::TextureType::ClearcoatRoughness:       mat.setClearcoatRoughnessMap(path); break;
    case GLMaterial::TextureType::ClearcoatNormal:          mat.setClearcoatNormalMap(path); break;
    case GLMaterial::TextureType::Iridescence:              mat.setIridescenceMap(path); break;
    case GLMaterial::TextureType::IridescenceThickness:     mat.setIridescenceThicknessMap(path); break;
    case GLMaterial::TextureType::SpecularFactor:           mat.setSpecularFactorMap(path); break;
    case GLMaterial::TextureType::SpecularColor:            mat.setSpecularColorMap(path); break;
    case GLMaterial::TextureType::Anisotropy:               mat.setAnisotropyMap(path); break;
    case GLMaterial::TextureType::Thickness:                mat.setThicknessMap(path); break;
    case GLMaterial::TextureType::Diffuse:                  mat.setDiffuseMap(path); break;
    case GLMaterial::TextureType::DiffuseTransmission:      mat.setDiffuseTransmissionMap(path); break;
    case GLMaterial::TextureType::DiffuseTransmissionColor: mat.setDiffuseTransmissionColorMap(path); break;
    case GLMaterial::TextureType::SpecularGlossiness:       mat.setSpecularGlossinessMap(path); break;
    case GLMaterial::TextureType::Opacity:                  mat.setOpacityMap(path); break;
    case GLMaterial::TextureType::Height:                   mat.setHeightMap(path); break;
    default: return;
    }

    // Apply sampler settings to the internal Texture slot so
    // resolveMaterialTextures uses the correct GL wrap/filter.
    if (samplerIndex >= 0 && samplerIndex < samplers.size())
    {
        const QJsonObject samp = samplers[samplerIndex].toObject();
        // Retrieve the current slot (already has sensible defaults), patch it.
        GLMaterial::Texture tex = mat.texture(type);
        tex.magFilter = static_cast<GLenum>(samp[QStringLiteral("magFilter")].toInt(GL_LINEAR));
        tex.minFilter = static_cast<GLenum>(samp[QStringLiteral("minFilter")].toInt(GL_LINEAR_MIPMAP_LINEAR));
        tex.wrapS     = static_cast<GLenum>(samp[QStringLiteral("wrapS")].toInt(GL_REPEAT));
        tex.wrapT     = static_cast<GLenum>(samp[QStringLiteral("wrapT")].toInt(GL_REPEAT));
        tex.path      = path.toStdString();
        // There is no generic setTexture(type, tex) on GLMaterial;
        // the path setters above suffice and defaults cover filtering.
        (void)tex;
    }

    // Apply texCoord index where per-type setters exist.
    const int texCoord = texInfo[QStringLiteral("texCoord")].toInt(0);
    if (texCoord != 0)
    {
        switch (type)
        {
        case GLMaterial::TextureType::Albedo:               mat.setAlbedoTexCoord(texCoord); break;
        case GLMaterial::TextureType::Roughness:            mat.setRoughnessTexCoord(texCoord); break;
        case GLMaterial::TextureType::Emissive:             mat.setEmissiveTexCoord(texCoord); break;
        case GLMaterial::TextureType::Opacity:              mat.setOpacityTexCoord(texCoord); break;
        case GLMaterial::TextureType::ClearcoatColor:       mat.setClearcoatColorTexCoord(texCoord); break;
        case GLMaterial::TextureType::ClearcoatRoughness:   mat.setClearcoatRoughnessTexCoord(texCoord); break;
        case GLMaterial::TextureType::ClearcoatNormal:      mat.setClearcoatNormalTexCoord(texCoord); break;
        case GLMaterial::TextureType::SheenColor:           mat.setSheenColorTexCoord(texCoord); break;
        case GLMaterial::TextureType::SheenRoughness:       mat.setSheenRoughnessTexCoord(texCoord); break;
        case GLMaterial::TextureType::Transmission:         mat.setTransmissionTexCoord(texCoord); break;
        case GLMaterial::TextureType::SpecularFactor:       mat.setSpecularFactorTexCoord(texCoord); break;
        case GLMaterial::TextureType::SpecularColor:        mat.setSpecularColorTexCoord(texCoord); break;
        case GLMaterial::TextureType::Anisotropy:           mat.setAnisotropyTexCoord(texCoord); break;
        case GLMaterial::TextureType::Iridescence:          mat.setIridescenceTexCoord(texCoord); break;
        case GLMaterial::TextureType::IridescenceThickness: mat.setIridescenceThicknessTexCoord(texCoord); break;
        case GLMaterial::TextureType::Thickness:            mat.setThicknessTexCoord(texCoord); break;
        case GLMaterial::TextureType::DiffuseTransmission:  mat.setDiffuseTransmissionTexCoord(texCoord); break;
        case GLMaterial::TextureType::DiffuseTransmissionColor: mat.setDiffuseTransmissionColorTexCoord(texCoord); break;
        case GLMaterial::TextureType::SpecularGlossiness:   mat.setSpecularGlossinessTexCoord(texCoord); break;
        default: break;
        }
    }
}

// Reconstruct a GLMaterial from an MVF3 material JSON object.
static GLMaterial reconstructMvfMaterial(const QJsonObject& matObj,
                                          const QHash<int, QString>& imagePaths,
                                          const QJsonArray& textures,
                                          const QJsonArray& samplers)
{
    GLMaterial mat;
    mat.setName(matObj[QStringLiteral("name")].toString());

    const QString shadingModel = matObj[QStringLiteral("shadingModel")].toString();
    if      (shadingModel == QLatin1String("PBR"))        mat.setShadingModel(GLMaterial::ShadingModel::PBR);
    else if (shadingModel == QLatin1String("BlinnPhong")) mat.setShadingModel(GLMaterial::ShadingModel::BlinnPhong);
    else if (shadingModel == QLatin1String("Unlit"))      mat.setShadingModel(GLMaterial::ShadingModel::Unlit);
    else if (shadingModel == QLatin1String("Toon"))       mat.setShadingModel(GLMaterial::ShadingModel::Toon);

    const QString blendMode = matObj[QStringLiteral("blendMode")].toString();
    if      (blendMode == QLatin1String("Opaque"))   mat.setBlendMode(GLMaterial::BlendMode::Opaque);
    else if (blendMode == QLatin1String("Masked"))   mat.setBlendMode(GLMaterial::BlendMode::Masked);
    else if (blendMode == QLatin1String("Alpha"))    mat.setBlendMode(GLMaterial::BlendMode::Alpha);
    else if (blendMode == QLatin1String("Additive")) mat.setBlendMode(GLMaterial::BlendMode::Additive);
    else if (blendMode == QLatin1String("Multiply")) mat.setBlendMode(GLMaterial::BlendMode::Multiply);

    mat.setTwoSided(matObj[QStringLiteral("doubleSided")].toBool(false));
    mat.setAlphaThreshold((float)matObj[QStringLiteral("alphaCutoff")].toDouble(0.5));
    mat.setOpacity((float)matObj[QStringLiteral("opacity")].toDouble(1.0));

    const QJsonObject pbr = matObj[QStringLiteral("pbr")].toObject();
    {
        const QJsonArray bc = pbr[QStringLiteral("baseColorFactor")].toArray();
        if (bc.size() >= 3)
            mat.setAlbedoColor(QVector3D((float)bc[0].toDouble(),
                                          (float)bc[1].toDouble(),
                                          (float)bc[2].toDouble()));
    }
    mat.setMetalness((float)pbr[QStringLiteral("metallicFactor")].toDouble(0.0));
    mat.setRoughness((float)pbr[QStringLiteral("roughnessFactor")].toDouble(1.0));

    const QJsonObject exts = matObj[QStringLiteral("extensions")].toObject();

    if (exts.contains(QStringLiteral("MVF_material_ads")))
    {
        const QJsonObject ads = exts[QStringLiteral("MVF_material_ads")].toObject();
        auto v3 = [](const QJsonArray& a, const QVector3D& def = {}) -> QVector3D {
            return a.size() >= 3
                ? QVector3D((float)a[0].toDouble(), (float)a[1].toDouble(), (float)a[2].toDouble())
                : def;
        };
        mat.setAmbient (v3(ads[QStringLiteral("ambient")].toArray()));
        mat.setDiffuse (v3(ads[QStringLiteral("diffuse")].toArray()));
        mat.setSpecular(v3(ads[QStringLiteral("specular")].toArray()));
        mat.setEmissive(v3(ads[QStringLiteral("emissive")].toArray()));
        mat.setShininess((float)ads[QStringLiteral("shininess")].toDouble(32.0));
    }

    if (exts.contains(QStringLiteral("MVF_material_pbr")))
    {
        const QJsonObject mvfPbr = exts[QStringLiteral("MVF_material_pbr")].toObject();

        mat.setIOR((float)mvfPbr[QStringLiteral("ior")].toDouble(1.5));
        mat.setTransmission((float)mvfPbr[QStringLiteral("transmission")].toDouble(0.0));
        mat.setClearcoat((float)mvfPbr[QStringLiteral("clearcoat")].toDouble(0.0));
        mat.setClearcoatRoughness((float)mvfPbr[QStringLiteral("clearcoatRoughness")].toDouble(0.0));
        {
            const QJsonArray sc = mvfPbr[QStringLiteral("sheenColor")].toArray();
            if (sc.size() >= 3)
                mat.setSheenColor(QVector3D((float)sc[0].toDouble(),
                                             (float)sc[1].toDouble(),
                                             (float)sc[2].toDouble()));
        }
        mat.setSheenRoughness((float)mvfPbr[QStringLiteral("sheenRoughness")].toDouble(0.0));

        static const struct { const char* key; GLMaterial::TextureType type; } kTexKeys[] = {
            {"baseColorTexture",                GLMaterial::TextureType::Albedo},
            {"normalTexture",                   GLMaterial::TextureType::Normal},
            {"occlusionTexture",                GLMaterial::TextureType::AmbientOcclusion},
            {"emissiveTexture",                 GLMaterial::TextureType::Emissive},
            {"metallicTexture",                 GLMaterial::TextureType::Metallic},
            {"roughnessTexture",                GLMaterial::TextureType::Roughness},
            {"transmissionTexture",             GLMaterial::TextureType::Transmission},
            {"iorTexture",                      GLMaterial::TextureType::IOR},
            {"sheenColorTexture",               GLMaterial::TextureType::SheenColor},
            {"sheenRoughnessTexture",           GLMaterial::TextureType::SheenRoughness},
            {"clearcoatTexture",                GLMaterial::TextureType::ClearcoatColor},
            {"clearcoatRoughnessTexture",       GLMaterial::TextureType::ClearcoatRoughness},
            {"clearcoatNormalTexture",          GLMaterial::TextureType::ClearcoatNormal},
            {"iridescenceTexture",              GLMaterial::TextureType::Iridescence},
            {"iridescenceThicknessTexture",     GLMaterial::TextureType::IridescenceThickness},
            {"specularTexture",                 GLMaterial::TextureType::SpecularFactor},
            {"specularColorTexture",            GLMaterial::TextureType::SpecularColor},
            {"anisotropyTexture",               GLMaterial::TextureType::Anisotropy},
            {"thicknessTexture",                GLMaterial::TextureType::Thickness},
            {"diffuseTexture",                  GLMaterial::TextureType::Diffuse},
            {"diffuseTransmissionTexture",      GLMaterial::TextureType::DiffuseTransmission},
            {"diffuseTransmissionColorTexture", GLMaterial::TextureType::DiffuseTransmissionColor},
            {"specularGlossinessTexture",       GLMaterial::TextureType::SpecularGlossiness},
            {"opacityTexture",                  GLMaterial::TextureType::Opacity},
            {"heightTexture",                   GLMaterial::TextureType::Height},
        };

        for (const auto& entry : kTexKeys)
        {
            const QString key = QLatin1String(entry.key);
            if (mvfPbr.contains(key))
                applyTextureRef(mat, entry.type, mvfPbr[key].toObject(),
                                imagePaths, textures, samplers);
        }
    }

    return mat;
}
} // anonymous namespace

// ---------------------------------------------------------------------------
// prepareMvfMeshes — CPU-only, thread-safe
// ---------------------------------------------------------------------------
QVector<GLWidget::PreparedMvfMesh> GLWidget::prepareMvfMeshes(
    const Mvf::Document& document,
    const QByteArray& geometryChunk,
    const QByteArray& imageChunk)
{
    // Build image index -> resolved file path.
    QHash<int, QString> imagePaths;
    static QTemporaryDir s_embeddedImageDir;

    for (int i = 0; i < document.images.size(); ++i)
    {
        const QJsonObject imgObj  = document.images[i].toObject();
        const int bvIndex         = imgObj[QStringLiteral("bufferView")].toInt(-1);
        const QString origUri     = imgObj[QStringLiteral("originalUri")].toString();
        const QString mimeType    = imgObj[QStringLiteral("mimeType")].toString();

        if (bvIndex >= 0 && bvIndex < document.bufferViews.size() && !imageChunk.isEmpty())
        {
            const QJsonObject bv = document.bufferViews[bvIndex].toObject();
            if (bv[QStringLiteral("buffer")].toInt(-1) == 1)
            {
                const int offset = (int)bv[QStringLiteral("byteOffset")].toDouble(0);
                const int length = (int)bv[QStringLiteral("byteLength")].toDouble(0);
                if (length > 0 && offset + length <= imageChunk.size()
                    && s_embeddedImageDir.isValid())
                {
                    QString ext = QStringLiteral(".bin");
                    if      (mimeType == QLatin1String("image/png"))  ext = QStringLiteral(".png");
                    else if (mimeType == QLatin1String("image/jpeg")) ext = QStringLiteral(".jpg");
                    else if (mimeType == QLatin1String("image/webp")) ext = QStringLiteral(".webp");
                    else if (mimeType == QLatin1String("image/bmp"))  ext = QStringLiteral(".bmp");

                    const QString tempPath = s_embeddedImageDir.filePath(
                        QStringLiteral("img%1%2").arg(i).arg(ext));
                    QFile f(tempPath);
                    if (f.open(QIODevice::WriteOnly))
                    {
                        f.write(imageChunk.constData() + offset, length);
                        f.close();
                        imagePaths[i] = tempPath;
                        continue;
                    }
                }
            }
        }

        if (!origUri.isEmpty())
            imagePaths[i] = origUri;
    }

    QVector<PreparedMvfMesh> result;
    result.reserve(document.meshes.size());

    for (int meshIdx = 0; meshIdx < document.meshes.size(); ++meshIdx)
    {
        const QJsonObject meshObj  = document.meshes[meshIdx].toObject();
        const QJsonArray primitives = meshObj[QStringLiteral("primitives")].toArray();
        if (primitives.isEmpty())
            continue;

        const QJsonObject prim    = primitives[0].toObject();
        const QJsonObject attribs = prim[QStringLiteral("attributes")].toObject();
        const QJsonObject extras  = prim[QStringLiteral("extras")].toObject();

        const std::vector<float> positions = readFloatStream(
            geometryChunk, document.accessors, document.bufferViews,
            attribs[QStringLiteral("POSITION")].toInt(-1));
        if (positions.empty())
            continue;

        const std::vector<unsigned int> indices = readUIntStream(geometryChunk, document.accessors,
            document.bufferViews, prim[QStringLiteral("indices")].toInt(-1));
        if (indices.empty())
            continue;

        const std::vector<float> normals  = readFloatStream(geometryChunk, document.accessors,
            document.bufferViews, attribs[QStringLiteral("NORMAL")].toInt(-1));
        const std::vector<float> tangents = readFloatStream(geometryChunk, document.accessors,
            document.bufferViews, attribs[QStringLiteral("TANGENT")].toInt(-1));
        const std::vector<float> uv0      = readFloatStream(geometryChunk, document.accessors,
            document.bufferViews, attribs[QStringLiteral("TEXCOORD_0")].toInt(-1));
        const std::vector<float> uv1      = readFloatStream(geometryChunk, document.accessors,
            document.bufferViews, attribs[QStringLiteral("TEXCOORD_1")].toInt(-1));
        const std::vector<float> uv2      = readFloatStream(geometryChunk, document.accessors,
            document.bufferViews, attribs[QStringLiteral("TEXCOORD_2")].toInt(-1));
        const std::vector<float> uv3      = readFloatStream(geometryChunk, document.accessors,
            document.bufferViews, attribs[QStringLiteral("TEXCOORD_3")].toInt(-1));
        const std::vector<float> colors   = readFloatStream(geometryChunk, document.accessors,
            document.bufferViews, attribs[QStringLiteral("COLOR_0")].toInt(-1));

        const size_t vertexCount = positions.size() / 3;
        std::vector<Vertex> vertices(vertexCount);
        for (size_t vi = 0; vi < vertexCount; ++vi)
        {
            Vertex& v = vertices[vi];
            v.Position = glm::vec3(positions[vi*3], positions[vi*3+1], positions[vi*3+2]);

            if (normals.size()  >= vi*3+3) v.Normal  = glm::vec3(normals [vi*3], normals [vi*3+1], normals [vi*3+2]);
            if (tangents.size() >= vi*3+3) v.Tangent = glm::vec3(tangents[vi*3], tangents[vi*3+1], tangents[vi*3+2]);

            if (uv0.size() >= vi*2+2) v.TexCoords[0] = glm::vec2(uv0[vi*2], uv0[vi*2+1]);
            if (uv1.size() >= vi*2+2) v.TexCoords[1] = glm::vec2(uv1[vi*2], uv1[vi*2+1]);
            if (uv2.size() >= vi*2+2) v.TexCoords[2] = glm::vec2(uv2[vi*2], uv2[vi*2+1]);
            if (uv3.size() >= vi*2+2) v.TexCoords[3] = glm::vec2(uv3[vi*2], uv3[vi*2+1]);

            v.Color = colors.size() >= vi*4+4
                      ? glm::vec4(colors[vi*4], colors[vi*4+1], colors[vi*4+2], colors[vi*4+3])
                      : glm::vec4(1.0f);
        }

        const int materialIndex = prim[QStringLiteral("material")].toInt(-1);
        GLMaterial material;
        if (materialIndex >= 0 && materialIndex < document.materials.size())
            material = reconstructMvfMaterial(document.materials[materialIndex].toObject(),
                                              imagePaths, document.textures, document.samplers);

        PreparedMvfMesh prepared;
        prepared.name          = meshObj[QStringLiteral("name")].toString();
        prepared.primitiveMode = static_cast<GLenum>(prim[QStringLiteral("mode")].toInt(GL_TRIANGLES));
        prepared.sceneIndex    = extras[QStringLiteral("sceneIndex")].toInt(-1);
        const QString uuidStr  = extras[QStringLiteral("meshUuid")].toString();
        prepared.uuid          = uuidStr.isEmpty()
                                 ? QUuid::fromString(meshObj[QStringLiteral("id")].toString())
                                 : QUuid::fromString(uuidStr);
        prepared.vertices      = std::move(vertices);
        prepared.indices       = std::move(indices);
        prepared.material      = std::move(material);

        result.append(std::move(prepared));
    }

    return result;
}

// ---------------------------------------------------------------------------
// uploadPreparedMvfMeshes — GL-only, must be on main thread
// ---------------------------------------------------------------------------
bool GLWidget::uploadPreparedMvfMeshes(const QVector<PreparedMvfMesh>& meshes)
{
    makeCurrent();

    if (!_fgShader)
    {
        update();
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        makeCurrent();
    }
    if (!_fgShader)
        return false;

    for (TriangleMesh* m : _meshStore)
        delete m;
    _meshStore.clear();
    _displayedObjectsIds.clear();
    _hiddenObjectsIds.clear();
    if (_visibleSwapped)
    {
        _visibleSwapped = false;
        emit visibleSwapped(_visibleSwapped);
    }

    const int totalMeshes = meshes.size();
    QElapsedTimer yieldTimer;
    yieldTimer.start();

    for (int i = 0; i < totalMeshes; ++i)
    {
        const PreparedMvfMesh& pm = meshes[i];

        AssImpMesh* mesh = new AssImpMesh(_fgShader.get(), pm.name,
                                          {}, {}, {}, pm.material);
        mesh->setUuid(pm.uuid);
        mesh->setPrimitiveMode(pm.primitiveMode);
        mesh->setSceneIndex(pm.sceneIndex);
        mesh->setMeshData(pm.vertices, pm.indices);

        const GLMaterial resolved = resolveMaterialTextures(this, pm.material);
        mesh->setMaterial(resolved);
        mesh->setTextureMaps(resolved);
        mesh->invertOpacityADSMap(resolved.isOpacityMapInverted());
        mesh->invertOpacityPBRMap(resolved.isOpacityMapInverted());

        addToDisplay(mesh);

        // Yield periodically so the progress bar and event loop stay alive.
        if (yieldTimer.elapsed() >= 8)
        {
            const int pct = 50 + (i + 1) * 40 / totalMeshes;   // 50-90%
            MainWindow::setProgressValue(pct);
            MainWindow::showStatusMessage(
                tr("Uploading mesh %1 / %2").arg(i + 1).arg(totalMeshes));

            doneCurrent();
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            makeCurrent();
            yieldTimer.restart();
        }
    }

    updateView();
    return !_meshStore.empty();
}

// ---------------------------------------------------------------------------
// clearMeshStore — delete all meshes and clear display list
// ---------------------------------------------------------------------------
void GLWidget::clearMeshStore()
{
    makeCurrent();

    for (TriangleMesh* m : _meshStore)
        delete m;
    _meshStore.clear();
    _displayedObjectsIds.clear();
    _hiddenObjectsIds.clear();
    if (_visibleSwapped)
    {
        _visibleSwapped = false;
        emit visibleSwapped(_visibleSwapped);
    }
}

// ---------------------------------------------------------------------------
// uploadOneMvfMesh — single-mesh GL upload for BlockingQueuedConnection
// ---------------------------------------------------------------------------
void GLWidget::uploadOneMvfMesh(const PreparedMvfMesh& pm)
{
    makeCurrent();

    // Create mesh on main thread (GL context required)
    AssImpMesh* mesh = new AssImpMesh(_fgShader.get(), pm.name,
                                      {}, {}, {}, pm.material);
    mesh->setUuid(pm.uuid);
    mesh->setPrimitiveMode(pm.primitiveMode);
    mesh->setSceneIndex(pm.sceneIndex);

    // Upload VBO data
    mesh->setMeshData(pm.vertices, pm.indices);

    // Resolve textures and set material
    const GLMaterial resolved = resolveMaterialTextures(this, pm.material);
    mesh->setMaterial(resolved);
    mesh->setTextureMaps(resolved);
    mesh->invertOpacityADSMap(resolved.isOpacityMapInverted());
    mesh->invertOpacityPBRMap(resolved.isOpacityMapInverted());

    // Add to display list and track in pending UUIDs (like AssImp's onMeshBatchReady)
    addToDisplay(mesh);
    _pendingSceneUuids.append(mesh->uuid());
}

// ---------------------------------------------------------------------------
// loadMvfMeshes — legacy combined entry point
// ---------------------------------------------------------------------------
bool GLWidget::loadMvfMeshes(const Mvf::Document& document,
                               const QByteArray& geometryChunk,
                               const QByteArray& imageChunk)
{
    QVector<PreparedMvfMesh> prepared = prepareMvfMeshes(document, geometryChunk, imageChunk);
    return uploadPreparedMvfMeshes(prepared);
}
