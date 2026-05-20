#ifndef GLWIDGET_H
#define GLWIDGET_H

#include "AdaptiveShadowMapper.h"
#include "BoundingSphere.h"
#include "GLCamera.h"
#include "TriangleMesh.h"
#include "TransformCommand.h"
#include "ShaderProgram.h"
#include "AssImpModelLoader.h"
#include <math.h>
#include <QColor>
#include <QFormLayout>
#include <QImage>
#include <QOpenGLFunctions_4_5_Core>
#include <QOpenGLWidget>
#include <QRubberBand>
#include "ViewToolbar.h"
#include "SceneUtils.h"
#include "GLLights.h"

/* Custom OpenGL Viewer Widget */

class TextRenderer;
class ClippingPlanesEditor;
class AssImpModelLoader;
class Plane;
class Cube;
class Cone;
class Sphere;

class AssImpModelLoader;

class ModelViewer;

enum class ViewMode { TOP, BOTTOM, LEFT, RIGHT, FRONT, BACK, ISOMETRIC, DIMETRIC, TRIMETRIC, NONE };
enum class ViewProjection { ORTHOGRAPHIC, PERSPECTIVE };
enum class DisplayMode { SHADED, WIREFRAME, WIRESHADED, REALSHADED };
enum class RenderingMode { ADS_BLINN_PHONG, PHYSICALLY_BASED_RENDERING };
enum class CornerAxisPosition { TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, BOTTOM_RIGHT };
enum class ClippingPlaneHatchMode { PROCEDURAL, TEXTURE };
enum class HatchPattern { DIAGONAL_45 = 0, DIAGONAL_135 = 1, HORIZONTAL = 2, VERTICAL = 3, GRID = 4, DIAGONAL_CROSS = 5 };
enum class HDRToneMapMode { ACES_Narkowicz, ACES_Hill, AECS_Hill_Exposure_Boost, KhronosPbrNeutral, Uncharted2ToneMapping, Reinhard };

struct TextureSamplerSettings
{
	GLenum wrapS = GL_REPEAT;
	GLenum wrapT = GL_REPEAT;
	GLenum minFilter = GL_LINEAR_MIPMAP_LINEAR;
	GLenum magFilter = GL_LINEAR;

	bool operator==(const TextureSamplerSettings& other) const
	{
		return wrapS == other.wrapS &&
			wrapT == other.wrapT &&
			minFilter == other.minFilter &&
			magFilter == other.magFilter;
	}
};

struct CachedTextureEntry
{
	QImage image;
	int imageWidth = 0;
	int imageHeight = 0;
	int imageComponents = 0;
	GLenum imageFormat = GL_RGBA;

	GLuint lastGPUTexture = 0;
	TextureSamplerSettings lastSamplerSettings;
	int refCount = 0;
};

class GLWidget : public QOpenGLWidget, QOpenGLFunctions_4_5_Core
{
	Q_OBJECT
public:
	GLWidget(QWidget* parent = 0, const char* name = 0);
	~GLWidget();

	void retranslateUI();

	void updateView();

	void resizeView(int w, int h) { resizeGL(w, h); }
	void setViewMode(ViewMode mode);
	void setProjection(ViewProjection proj);
	void setCameraMode(GLCamera::CameraMode mode);

	void setMultiView(bool active) { _multiViewActive = active; }
	void setRotationActive(bool active);
	void setPanningActive(bool active);
	void setZoomingActive(bool active);

	void setCornerAxisPosition(CornerAxisPosition position) { _cornerAxisPosition = position; }

	void beginWindowZoom();
	void performWindowZoom();

	void setDisplayList(const std::vector<int>& ids);
	void triggerShadowRecomputation();
	void setShadowQuality(AdaptiveShadowMapper::QualityLevel quality);
	float calculateLightDistance();

	QVector<QUuid> duplicateObjects(const std::vector<int>& ids);

	void updateFloorPlane();
	void updateBoundingSphere();

	void updateBoundingBox();

	int getModelNum() const
	{
		return _modelNum;
	}

	void updateClippingPlane();
	void showClippingPlaneEditor(bool show);
	void setClippingPlaneHatchMode(ClippingPlaneHatchMode mode);
	void setClippingPlaneHatchPattern(HatchPattern pattern);
	void setHatchTiling(int tiling);
	void setHatchLineThickness(float width);
	void setHatchIntensity(float spacing);
	void setHatchLayers(int layers);
	void setHatchLineColor(const QColor& color);
	void setHatchTexture(const QString& path);

	void showAxis(bool show);

	void showShadows(bool show);
	void showSelfShadows(bool show);
	void showEnvironment(bool show);
	void showSkyBox(bool show);
	void blurSkyBox(bool blur);
	void showReflections(bool show);
	void showFloor(bool show);
	bool isFloorShown() { return _floorDisplayed; }
	void showFloorTexture(bool show);
	void setFloorTexture(QImage img);

	std::vector<TriangleMesh*> getMeshStore() const { return _meshStore; }

	void addToDisplay(TriangleMesh*);
	void removeFromDisplay(int index);
	void centerScreen(std::vector<int> selectedIDs);
	void select(int id);
	void deselect(int id);

	bool loadAssImpModel(const QString& fileName, const UVMethod& uvMethod, QString& error, bool progressiveLoading = false);

	bool generateUVsForMeshes(const std::vector<int>& ids, const UVMethod& uvMethod, const UVConfig& uvConfig, QString& error);

	aiScene* getAssImpScene() const { return _globalScene; }
	glm::mat4 getGlobalSceneTransform() const { return _globalSceneTransform; }

	void enableADSDiffuseTexMap(const std::vector<int>& ids, const bool& enable);
	void setADSDiffuseTexMap(const std::vector<int>& ids, const QString& path);
	void clearADSDiffuseTexMap(const std::vector<int>& ids);

	void enableADSSpecularTexMap(const std::vector<int>& ids, const bool& enable);
	void setADSSpecularTexMap(const std::vector<int>& ids, const QString& path);
	void clearADSSpecularTexMap(const std::vector<int>& ids);

	void enableADSEmissiveTexMap(const std::vector<int>& ids, const bool& enable);
	void setADSEmissiveTexMap(const std::vector<int>& ids, const QString& path);
	void clearADSEmissiveTexMap(const std::vector<int>& ids);

	void enableADSNormalTexMap(const std::vector<int>& ids, const bool& enable);
	void setADSNormalTexMap(const std::vector<int>& ids, const QString& path);
	void clearADSNormalTexMap(const std::vector<int>& ids);

	void enableADSHeightTexMap(const std::vector<int>& ids, const bool& enable);
	void setADSHeightTexMap(const std::vector<int>& ids, const QString& path);
	void clearADSHeightTexMap(const std::vector<int>& ids);

	void enableADSOpacityTexMap(const std::vector<int>& ids, const bool& enable);
	void invertADSOpacityTexMap(const std::vector<int>& ids, const bool& inverted);
	void setADSOpacityTexMap(const std::vector<int>& ids, const QString& path);
	void clearADSOpacityTexMap(const std::vector<int>& ids);

	void clearADSTexMaps(const std::vector<int>& ids);

	void setMaterialToObjects(const std::vector<int>& ids, const GLMaterial& mat);
	void setTexturesToObjects(const std::vector<int>& ids, const GLMaterial& mat);
	void synchronizeTextureCache(const GLMaterial* material, GLMaterial::TextureType type);
	void clearTextureCache();

	void setPBRAlbedoColor(const std::vector<int>& ids, const QColor& col);
	void setPBRMetallic(const std::vector<int>& ids, const float& val);
	void setPBRRoughness(const std::vector<int>& ids, const float& val);

	void clearPBRTexMaps(const std::vector<int>& ids);
	void enablePBRAlbedoTexMap(const std::vector<int>& ids, const bool& enable);
	void setPBRAlbedoTexMap(const std::vector<int>& ids, const QString& path);
	void clearPBRAlbedoTexMap(const std::vector<int>& ids);
	void enablePBRMetallicTexMap(const std::vector<int>& ids, const bool& enable);
	void setPBRMetallicTexMap(const std::vector<int>& ids, const QString& path);
	void clearPBRMetallicTexMap(const std::vector<int>& ids);
	void enablePBRRoughnessTexMap(const std::vector<int>& ids, const bool& enable);
	void setPBRRoughnessTexMap(const std::vector<int>& ids, const QString& path);
	void clearPBRRoughnessTexMap(const std::vector<int>& ids);
	void enablePBRNormalTexMap(const std::vector<int>& ids, const bool& enable);
	void setPBRNormalTexMap(const std::vector<int>& ids, const QString& path);
	void clearPBRNormalTexMap(const std::vector<int>& ids);
	void enablePBRAOTexMap(const std::vector<int>& ids, const bool& enable);
	void setPBRAOTexMap(const std::vector<int>& ids, const QString& path);
	void clearPBRAOTexMap(const std::vector<int>& ids);

	void enablePBROpacityTexMap(const std::vector<int>& ids, const bool& enable);
	void setPBROpacityTexMap(const std::vector<int>& ids, const QString& path);
	void invertPBROpacityTexMap(const std::vector<int>& ids, const bool& inverted);
	void clearPBROpacityTexMap(const std::vector<int>& ids);

	void enablePBRHeightTexMap(const std::vector<int>& ids, const bool& enable);
	void setPBRHeightTexMap(const std::vector<int>& ids, const QString& path);
	void clearPBRHeightTexMap(const std::vector<int>& ids);
	void setPBRHeightScale(const std::vector<int>& ids, const float& scale);

	void enablePBRTransmissionTexMap(const std::vector<int>& ids, const bool& enable);
	void setPBRTransmissionTexMap(const std::vector<int>& ids, const QString& path);
	void clearPBRTransmissionTexMap(const std::vector<int>& ids);

	void enablePBRIORTexMap(const std::vector<int>& ids, const bool& enable);
	void setPBRIORTexMap(const std::vector<int>& ids, const QString& path);
	void clearPBRIORTexMap(const std::vector<int>& ids);

	void enablePBRSheenColorTexMap(const std::vector<int>& ids, const bool& enable);
	void setPBRSheenColorTexMap(const std::vector<int>& ids, const QString& path);
	void clearPBRSheenColorTexMap(const std::vector<int>& ids);

	void enablePBRSheenRoughnessTexMap(const std::vector<int>& ids, const bool& enable);
	void setPBRSheenRoughnessTexMap(const std::vector<int>& ids, const QString& path);
	void clearPBRSheenRoughnessTexMap(const std::vector<int>& ids);

	void enablePBRClearcoatTexMap(const std::vector<int>& ids, const bool& enable);
	void setPBRClearcoatTexMap(const std::vector<int>& ids, const QString& path);
	void clearPBRClearcoatTexMap(const std::vector<int>& ids);

	void enablePBRClearcoatRoughnessTexMap(const std::vector<int>& ids, const bool& enable);
	void setPBRClearcoatRoughnessTexMap(const std::vector<int>& ids, const QString& path);
	void clearPBRClearcoatRoughnessTexMap(const std::vector<int>& ids);

	void enablePBRClearcoatNormalTexMap(const std::vector<int>& ids, const bool& enable);
	void setPBRClearcoatNormalTexMap(const std::vector<int>& ids, const QString& path);
	void clearPBRClearcoatNormalTexMap(const std::vector<int>& ids);

	void setTransformation(const std::vector<int>& ids, const QVector3D& trans, const QVector3D& rot, const QVector3D& scale);
	void bakeTransformation(const std::vector<int>& ids);
	void resetTransformation(const std::vector<int>& ids);
	void applyTransforms(const QMap<int, TransformState>& transforms);

	void setTexture(const std::vector<int>& ids, const QImage& texImage);
	void setSkyBoxTextureFolder(QString folder);
	bool loadCubemapFromSingleHDR(const QString& filePath);
	bool convertEquirectangularToCubemap(const QString& filePath);
	bool convertEquirectangularToCubemapQuad(const QString& filePath);

	void renderConversionCube();

	void setAnisotropicFilteringLevel(int level) { _anisotropicFilteringLevel = level; }
	int getAnisotropicFilteringLevel() const { return _anisotropicFilteringLevel; }

	void setTransmissionEnabled(const bool& enabled);
	bool isTransmissionEnabled() const { return _transmissionEnabled; }

public:
	float getXTran() const;
	void setXTran(const float& xTran);

	float getYTran() const;
	void setYTran(const float& yTran);

	float getZTran() const;
	void setZTran(const float& zTran);

	float getXRot() const;
	void setXRot(const float& xRot);

	float getYRot() const;
	void setYRot(const float& yRot);

	float getZRot() const;
	void setZRot(const float& zRot);

	float getXScale() const;
	void setXScale(const float& xScale);

	float getYScale() const;
	void setYScale(const float& yScale);

	float getZScale() const;
	void setZScale(const float& zScale);

	QVector4D getAmbientLight() const;
	void setAmbientLight(const QVector4D& ambientLight);

	QVector4D getDiffuseLight() const;
	void setDiffuseLight(const QVector4D& diffuseLight);

	QVector4D getSpecularLight() const;
	void setSpecularLight(const QVector4D& specularLight);

	QVector3D getLightPosition() const;
	void setLightOffset(const QVector3D& offset);

	std::vector<GPULight> getParsedLights() const { return _originalParsedLights; }

	float getFloorSize() const { return _floorSize; }

	bool isShaded() const;
	DisplayMode getDisplayMode() const;
	void setDisplayMode(DisplayMode mode);

	bool isVertexNormalsShown() const;
	void setShowVertexNormals(bool showVertexNormals);

	bool isFaceNormalsShown() const;
	void setShowFaceNormals(bool showFaceNormals);

	std::vector<int> getDisplayedObjectsIds() const;

	bool isVisibleSwapped() const;

	BoundingSphere getBoundingSphere() const;

	QColor getBgTopColor() const;
	void setBgTopColor(const QColor& bgTopColor);


	QColor getBgBotColor() const;
	void setBgBotColor(const QColor& bgBotColor);

	int getBgGradientStyle() const { return _gradientStyle; }
	void setBgGradientStyle(int style) { _gradientStyle = style; }

	RenderingMode getRenderingMode() const;
	void setRenderingMode(const RenderingMode& renderingMode);

	void setCappingPlanesEnabled(const bool& enabled) { _cappingEnabled = enabled; }
	bool cappingPlanesEnabled() const { return _cappingEnabled; }

	void setYZClippingEnabled(const bool& enabled) { _clipYZEnabled = enabled; }
	bool yzClippingEnabled() const { return _clipYZEnabled; }
	void setZXClippingEnabled(const bool& enabled) { _clipZXEnabled = enabled; }
	bool zxClippingEnabled() const { return _clipZXEnabled; }
	void setXYClippingEnabled(const bool& enabled) { _clipXYEnabled = enabled; }
	bool xyClippingEnabled() const { return _clipXYEnabled; }

	void setClippingXFlipped(const bool& flipped) { _clipXFlipped = flipped; }
	bool clippingXFlipped() const { return _clipXFlipped; }
	void setClippingYFlipped(const bool& flipped) { _clipYFlipped = flipped; }
	bool clippingYFlipped() const { return _clipYFlipped; }
	void setClippingZFlipped(const bool& flipped) { _clipZFlipped = flipped; }
	bool clippingZFlipped() const { return _clipZFlipped; }

	void setClippingXCoeff(const float& coeff) { _clipXCoeff = coeff; }
	float clippingXCoeff() const { return _clipXCoeff; }
	void setClippingYCoeff(const float& coeff) { _clipYCoeff = coeff; }
	float clippingYCoeff() const { return _clipYCoeff; }
	void setClippingZCoeff(const float& coeff) { _clipZCoeff = coeff; }
	float clippingZCoeff() const { return _clipZCoeff; }

	bool getHdrToneMapping() const;
	bool getGammaCorrection() const;
	float getScreenGamma() const;

	bool areLightsShown() const;

	void cleanUpShaders();

	// Recycle bin operations (used by DeleteCommand)
	void moveToRecycleBin(const QUuid& uuid, int originalIndex);
	bool restoreFromRecycleBin(const QUuid& uuid);
	void permanentlyDeleteFromBin(const QUuid& uuid);

	// Query methods
	bool isInRecycleBin(const QUuid& uuid) const;
	QVector<QUuid> getRecycleBinUuids() const;

	// UUID lookup methods
	TriangleMesh* getMeshByUuid(const QUuid& uuid) const;
	TriangleMesh* getMeshByIndex(int index) const;
	int getIndexByUuid(const QUuid& uuid) const;
	QUuid getUuidByIndex(int index) const;

	void serializeScene(QDataStream& out) const;
	void deserializeScene(QDataStream& in);

signals:
	void windowZoomEnded();
	void rotationsSet();
	void zoomAndPanSet();
	void viewSet();
	void displayListSet();
	void singleSelectionDone(int);
	void sweepSelectionDone(QList<int>);
	void floorShown(bool);
	void visibleSwapped(bool);
	void loadingAssImpModelCancelled();
	void displayModeChanged(int);

public slots:
	void animateViewChange();
	void animateFitAll();
	void animateWindowZoom();
	void animateCenterScreen();
	void onInertiaTimer();
	void stopAnimations();
	void checkAndStopTimers();
	void fitAll();
	void setAutoFitViewOnUpdate(bool update);
	void setSelectionHighlighting(bool highlight);
	void performKeyboardNav();
	void disableLowRes();
	void setFloorTexRepeatS(double floorTexRepeatS);
	void setFloorTexRepeatT(double floorTexRepeatT);
	void setFloorOffsetPercent(double value);
	void setSkyBoxFOV(double fov);
	void setSkyBoxTextureHDRI(bool hdrSet);
	void enableHDRToneMapping(bool hdrToneMapping);
	void enableGammaCorrection(bool gammaCorrection);
	void setScreenGamma(double screenGamma);
	void setHDRToneMappingMode(HDRToneMapMode mode);
	void setEnvMapExposure(double exposure);
	void setIBLExposure(double exposure);
	void showLights(bool showLights);
	void useDefaultLights(bool useDefaultLights);
	void usePunctualLights(bool usePunctualLights);
	void useIBL(bool useIBL);
	void showFileReadingProgress(float percent);
	void showMeshLoadingProgress(float percent);
	void showModelLoadingProgress(int nodeNum, int totalNodes, int totalMeshes, bool uvProcessed);
	void swapVisible(bool checked);
	void cancelAssImpModelLoading();

private slots:
	void showContextMenu(const QPoint& pos);
	void centerDisplayList();
	void setBackgroundColor();

protected:
	void initializeGL();
	void createCappingPlanes();
	void resizeGL(int width, int height);
	void paintGL();

	void renderSingleView(QColor& topColor, QColor& botColor);

	void renderMultiView(QColor& topColor, QColor& botColor);

	void resizeEvent(QResizeEvent* event);
	void mousePressEvent(QMouseEvent*);
	void mouseReleaseEvent(QMouseEvent*);
	void mouseMoveEvent(QMouseEvent*);
	void wheelEvent(QWheelEvent*);
	void keyPressEvent(QKeyEvent* event);
	void keyReleaseEvent(QKeyEvent* event);
	void closeEvent(QCloseEvent* event);

private:

	void createShaderPrograms();
	void createLights();

	// Fullscreen triangle methods for IBL
	void createFullscreenTriangle();
	void drawFullscreenTriangle();
	void setIBLFaceBasis(QOpenGLShaderProgram* prog, int faceIndex);

	void loadEnvMap();
	void loadIrradianceMap();
	void loadFloor();
	void applyFloorPlaneMaterialSettings();
	void updateMainLightPosition(float halfObjectSize);
	float updateFloorGeometry();

	void updatePunctualLights();  // Update lights based on bounding sphere changes

	void drawMesh(QOpenGLShaderProgram* prog);

	void drawOpaqueMeshes(QOpenGLShaderProgram* prog);
	void drawTransparentMeshes(QOpenGLShaderProgram* prog);
	void drawMeshesWithClipping(QOpenGLShaderProgram* prog, bool transparentPass);
	void setCommonUniforms(QOpenGLShaderProgram* prog, GLCamera* camera);

	void drawSectionCapping();
	void drawFloor(const bool& drawReflection = true);
	void drawSkyBox();
	void drawVertexNormals();
	void drawFaceNormals();
	void drawAxis();
	void drawCornerAxis(CornerAxisPosition position);
	void drawLights();

	void bindIBLTextures();

	void render(GLCamera* camera);
	void renderToShadowBuffer();
	int processSelection(const QPoint& pixel);
	void renderQuad();
	void renderMeshWithDisplayMode(TriangleMesh* mesh, DisplayMode mode);

	void gradientBackground(float top_r, float top_g, float top_b, float top_a,
		float bot_r, float bot_g, float bot_b, float bot_a, int gradientStyle);

	void loadBgColorSettings();

	void splitScreen();

	void setRotations(float xRot, float yRot, float zRot);
	void setZoomAndPan(float zoom, QVector3D pan);
	void setView(QVector3D viewPos, QVector3D viewDir, QVector3D upDir, QVector3D rightDir);
	void fitBoxToScreen(const BoundingBox& box);


	void convertClickToRay(const QPoint& pixel, const QRect& viewport, GLCamera* camera, QVector3D& orig, QVector3D& dir);
	int clickSelect(const QPoint& pixel);
	QList<int> sweepSelect(const QPoint& pixel);
	unsigned int colorToIndex(const QColor& color);
	QColor indexToColor(const unsigned int& index);

	float highestModelZ();
	float lowestModelZ();

	QRect getViewportFromPoint(const QPoint& pixel);
	QRect getClientRectFromPoint(const QPoint& pixel);
	QVector3D get3dTranslationVectorFromMousePoints(const QPoint& start, const QPoint& end);
	unsigned int loadTextureFromFile(const char* path,
		GLenum wrapS = GL_REPEAT, GLenum wrapT = GL_REPEAT,
		GLenum minFilter = GL_LINEAR_MIPMAP_LINEAR, GLenum magFilter = GL_LINEAR,
		bool flipY = false);
	void setupClippingUniforms(QOpenGLShaderProgram* prog, QVector3D pos);

	void onMeshBatchReady(const std::vector<AssImpMesh*>& batch);

	GLuint createGPUTextureFromImage(const QImage& image, const TextureSamplerSettings& samplers);
	unsigned int getOrLoadTextureCached(const QString& path,
		const TextureSamplerSettings& samplers = TextureSamplerSettings());
	void retainTexture(unsigned int texId);
	void releaseTexture(unsigned int texId);

	static GLMaterial resolveMaterialTextures(GLWidget* w, const GLMaterial& src);

	// --- Transmission Buffer Methods ---
	void initTransmissionBuffer();
	void renderToTransmissionBuffer(GLCamera* camera, const QColor& topColor, const QColor& botColor);
	void cleanupTransmissionBuffer();
	void resizeTransmissionBuffer(int width, int height);

	GLuint _whiteTexture = 0;
	void createWhiteTexture();

	void generateCubemapMipmaps(GLuint cubemapTexture);

	QString generateUniqueMeshName(const QString& baseName);

private:
	ViewToolbar* _viewToolbar;

	QSet<int> _keys;
	DisplayMode _displayMode;
	RenderingMode _renderingMode;
	QColor      _bgTopColor;
	QColor      _bgBotColor;
	int _gradientStyle = 0; // 0=Vertical, 1=Horizontal, 2=TopLeftToBottomRight, 3=TopRightToBottomLeft
	bool _windowZoomActive;
	bool _viewZooming;
	bool _viewPanning;
	bool _viewRotating;
	int _modelNum;
	QImage _texImage, _texBuffer;
	float _floorTexRepeatS, _floorTexRepeatT;
	TextRenderer* _textRenderer;
	TextRenderer* _axisTextRenderer;
	QString _labelTop, _labelFront, _labelLeft, _labelIsometric, _labelDimetric, _labelTrimetric;
	QString _labelAxisX, _labelAxisY, _labelAxisZ;
	QString _labelNumMeshes;
	QString _modelName;

	QVector3D _currentTranslation;
	QQuaternion _currentRotation;
	float _slerpStep;
	float _slerpFrac;

	float _currentViewRange;
	float _scaleFrac;

	float _viewRange;
	float _viewBoundingSphereDia;
	float _FOV;

	bool _autoFitViewOnUpdate;
	bool _selectionHighlighting;

	QPoint _leftButtonPoint;
	QPoint _rightButtonPoint;
	QPoint _middleButtonPoint;

	QPoint _lastPanPoint;
	int _lastZoomDirection = 0; // +1 for zoom in, -1 for zoom out, 0 for none
	float _lastZoomStep = 1.05f;
	QVector3D _lastZoomPanVector;
	QVector3D _inertiaZoomPanVelocity = QVector3D(0, 0, 0);

	// Inertia state for mouse actions
	QVector2D _inertiaPanVelocity;
	float _inertiaZoomVelocity = 0.0f;
	QVector2D _inertiaRotateVelocity;
	QTimer* _inertiaTimer = nullptr;
	float _inertiaDamping = 0.8f; // Damping factor (tweak as needed)

	bool _mouseMovedSincePress = false;
	qint64 _lastMouseMoveTime = 0;

	QRubberBand* _rubberBand;
	QRubberBand* _selectRect;
	QVector3D _rubberBandPan;
	GLfloat _rubberBandZoomRatio;
	float _rubberBandRadius;
	QVector3D _rubberBandCenter;
	QList<int> _selectedIDs;
	unsigned int _selectionFBO;
	unsigned int _selectionRBO;
	unsigned int _selectionDBO;

	enum class SelectionMode
	{
		RayOnly,
		ColorOnly,
		Hybrid // Try ray, fallback to color
	};

	SelectionMode _selectionMode = SelectionMode::RayOnly; // Default

	bool _multiViewActive;

	bool _showAxis;
	bool _userShowAxisOverride;
	bool _userShowCornerAxisOverride;

	float _clipXCoeff;
	float _clipYCoeff;
	float _clipZCoeff;

	float _clipDX;
	float _clipDY;
	float _clipDZ;

	bool _clipYZEnabled;
	bool _clipZXEnabled;
	bool _clipXYEnabled;

	bool _clipXFlipped;
	bool _clipYFlipped;
	bool _clipZFlipped;

	bool _showVertexNormals;
	bool _showFaceNormals;

	bool _envMapEnabled;
	bool _shadowsEnabled;
	bool _selfShadowsEnabled;
	bool _reflectionsEnabled;
	bool _floorDisplayed;
	bool _floorTextureDisplayed;
	bool _skyBoxEnabled;
	bool _skyBoxBlurred;

	bool _lowResEnabled;

	unsigned int _shadowWidth;
	unsigned int _shadowHeight;

	bool _shadowMapNeedsInitialization = true;

	float _xTran;
	float _yTran;
	float _zTran;

	float _xRot;
	float _yRot;
	float _zRot;

	float _xScale;
	float _yScale;
	float _zScale;

	QVector4D _ambientLight;
	QVector4D _diffuseLight;
	QVector4D _specularLight;

	QVector3D _lightPosition;
	float _lightOffsetX;
	float _lightOffsetY;
	float _lightOffsetZ;

	QMatrix4x4 _lightSpaceMatrix;

	QMatrix4x4 _projectionMatrix, _viewMatrix, _modelMatrix;
	QMatrix4x4 _modelViewMatrix;
	QMatrix4x4 _viewportMatrix;

	std::unique_ptr<ShaderProgram> _fgShader;
	std::unique_ptr<ShaderProgram> _axisShader;
	std::unique_ptr<ShaderProgram> _vertexNormalShader;
	std::unique_ptr<ShaderProgram> _faceNormalShader;
	std::unique_ptr<ShaderProgram> _shadowMappingShader;
	std::unique_ptr<ShaderProgram> _skyBoxShader;
	std::unique_ptr<ShaderProgram> _irradianceShader;
	std::unique_ptr<ShaderProgram> _prefilterShader;
	std::unique_ptr<ShaderProgram> _brdfShader;
	std::unique_ptr<ShaderProgram> _lightCubeShader;
	std::unique_ptr<ShaderProgram> _clippingPlaneShader;
	std::unique_ptr<ShaderProgram> _clippedMeshShader;
	std::unique_ptr<ShaderProgram> _selectionShader;
	std::unique_ptr<ShaderProgram> _equirectToCubeShader;
	std::unique_ptr<ShaderProgram> _equirectToCubeQuadShader;
	std::unique_ptr<ShaderProgram> _downsampleShader;

	unsigned int             _environmentMap;
	unsigned int             _shadowMap;
	unsigned int             _shadowMapFBO;
	unsigned int			 _irradianceMap;
	unsigned int             _prefilterMap;
	unsigned int             _brdfLUTTexture;
	unsigned int			 _skyboxFBO = 0;
	unsigned int			 _skyboxColorTexture = 0;
	unsigned int			 _skyboxDepthBuffer = 0;

	// --- Transmission Buffer Resources ---
	GLuint _transmissionFBO = 0;              // Framebuffer object
	GLuint _transmissionColorTexture = 0;     // RGBA32F: opaque scene capture
	GLuint _transmissionDepthTexture = 0;     // DEPTH32F: for Phase 2 calculations
	int _transmissionTextureWidth = 0;        // Current FBO width
	int _transmissionTextureHeight = 0;       // Current FBO height
	int _transmissionMipLevels = 0;			  // Number of mip levels
	bool _transmissionEnabled = true;         // Toggle for feature

	QImage					 _floorTexImage;
	float                    _floorSize;
	float 					 _floorSizeFactor;
	float					 _floorOffsetPercent;
	QVector3D                _floorCenter;

	std::unique_ptr<ShaderProgram> _textShader;

	std::unique_ptr<ShaderProgram> _bgShader;
	QOpenGLVertexArrayObject _bgVAO;

	std::unique_ptr<ShaderProgram> _bgSplitShader;
	QOpenGLVertexArrayObject _bgSplitVAO;
	QOpenGLBuffer _bgSplitVBO;

	QOpenGLVertexArrayObject _axisVAO;
	QOpenGLBuffer _axisVBO;
	QOpenGLBuffer _axisCBO;

	CornerAxisPosition _cornerAxisPosition = CornerAxisPosition::TOP_RIGHT;

	std::vector<TriangleMesh*> _meshStore;
	std::vector<int> _displayedObjectsIds;
	std::vector<int> _hiddenObjectsIds;
	std::vector<int> _centerScreenObjectIDs;
	bool _visibleSwapped;

	QVBoxLayout* _editorLayout;
	QFormLayout* _lowerLayout;
	QFormLayout* _upperLayout;

	ClippingPlanesEditor* _clippingPlanesEditor;
	Plane* _clippingPlaneXY;
	Plane* _clippingPlaneYZ;
	Plane* _clippingPlaneZX;
	bool _cappingEnabled;
	unsigned int _cappingTexture;

	ViewMode _viewMode;
	ViewProjection _projection;
	GLCamera::ProjectionType _previousProjection;

	GLCamera* _primaryCamera;
	GLCamera* _orthoViewsCamera;

	QTimer* _keyboardNavTimer;
	QTimer* _animateViewTimer;
	QTimer* _animateFitAllTimer;
	QTimer* _animateWindowZoomTimer;
	QTimer* _animateCenterScreenTimer;

	BoundingSphere _boundingSphere;
	BoundingSphere _selectionBoundingSphere;

	BoundingBox _boundingBox;

	Plane* _floorPlane;
	Cube* _skyBox;
	GLuint _fsTriVAO = 0;          // Fullscreen triangle VAO
	GLuint _fsTriVBO = 0;          // Fullscreen triangle VBO
	bool _fsTriInitialized = false; // Track initialization state
	std::vector<QString> _skyBoxFaces;
	float _skyBoxFOV;
	bool  _skyBoxTextureHDRI;
	bool  _gammaCorrection;
	float _screenGamma;
	bool  _hdrToneMapping;
	float _envMapExposure;
	float _iblExposure;

	GLuint _conversionCubeVAO = 0;
	GLuint _conversionCubeVBO = 0;

	HDRToneMapMode _toneMappingMode;

	bool _openGLInitialized = false;
	float _anisotropicFilteringLevel = 16.0f;

	Cone* _axisCone;

	Cube* _lightCube;
	Sphere* _lightSphere;
	bool _showLights;
	bool _useDefaultLights;
	bool _usePunctualLights;
	bool _useIBL;

	std::unique_ptr<ShaderProgram> _debugShader;

	ModelViewer* _viewer;

	unsigned int _quadVAO;
	unsigned int _quadVBO;

	unsigned long long _displayedObjectsMemSize;

	AssImpModelLoader* _assimpModelLoader;
	const aiScene* _assimpScene = nullptr;
	aiScene* _globalScene = nullptr; // Merged scene from multiple files
	glm::mat4 _globalSceneTransform = glm::mat4(1.0f);
	bool _progressiveLoadingEnabled = false;

	// --- Texture cache: path -> GL texture id (per GLWidget context)
	std::unordered_map<QString, CachedTextureEntry> _texCache;
	// Reference counts so we can release when maps are cleared
	std::unordered_map<unsigned int, int> _texRefCount;


	ClippingPlaneHatchMode _hatchMode = ClippingPlaneHatchMode::PROCEDURAL;
	HatchPattern _hatchPattern = HatchPattern::DIAGONAL_45;
	int   _hatchTiling = 50;
	float _hatchThickness = 0.05f;
	float _hatchIntensity = 1.0f;
	int _hatchLayers = 3;
	QVector3D _hatchLineColor = QVector3D(0.0f, 0.0f, 0.0f);
	QString _hatchTexturePath;

	AdaptiveShadowMapper shadowMapper;

	std::unique_ptr<GLLights> glLights;
	std::vector<GPULight> _originalParsedLights;      // ORIGINAL - never modified
	std::vector<GPULight> _currentRepositionedLights; // Working copy
	float _originalBoundingRadius = 1.0f;

	// Light repositioning based on model transformation
	struct LightRepositioningBasis
	{
		glm::vec3 baselineCenter;
		float baselineRadius;
		glm::mat4 accumulatedRotation;
	} _lightRepoBasis;

	// Recycle bin (internal only - not exposed to user)
	struct RecycleBinEntry
	{
		TriangleMesh* mesh;
		int originalIndex;      // For potential smart restoration
		QDateTime deletedAt;    // For debugging/logging
	};

	QMap<QUuid, RecycleBinEntry> _recycleBin;
};

#endif
