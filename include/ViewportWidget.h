#pragma once

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>

#include "AdaptiveShadowMapper.h"
#include "AnimationRuntimeController.h"
#include "VisibilityComputationHelper.h"
#include "BoundingSphere.h"
#include "ExplodedViewRuntimeController.h"
#include "SceneRenderController.h"
#include "ViewportInteractionController.h"
#include "Camera.h"
#include "MvfMeshPreparationWorker.h"
#include "PlaneRenderable.h"
#include "FloorPlane.h"
#include "SceneRuntime.h"
#include "RenderableMesh.h"
#include "TransformCommand.h"
#include "ShaderProgram.h"
#include "AssImpModelLoader.h"
#include "AssemblyRelationGraph.h"
#include <math.h>
#include <QColor>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFormLayout>
#include <QImage>
#include <QMultiHash>
#include <QOpenGLFunctions_4_5_Core>
#include <QOpenGLWidget>
#include <QPointer>
#include <QRubberBand>
#include <QSet>
#include <QString>
#include <array>
#include "ViewToolbar.h"
#include "SceneUtils.h"
#include "PunctualLights.h"
#include "KTX2Loader.h"
#include "SelectionManager.h"
#include "TransformGizmo.h"
#include "InteractivePtRenderer.h"
#include "RtOptixPathTracingSession.h"
#include "RtOptixSceneTracer.h"
#include "RtPathTracingSession.h"
#include "RtPresenter.h"

/* Custom OpenGL Viewer Widget */

namespace Mvf { struct Document; }

class TextRenderer;
class ClippingPlanesEditor;
class ExplodedViewPanel;
class ExplodedViewManager;

#include "RenderEnums.h"
class AssImpModelLoader;
class ConeRenderable;
class CubeRenderable;
class SphereRenderable;
class ViewCubeMesh;
class AssImpModelLoader;
struct SceneNode;

class ModelViewer;

// ViewMode, ViewProjection, CornerAxisPosition, RenderingMode,
// ClippingPlaneHatchMode, HatchPattern → RenderEnums.h (Phase 11/12)
enum class DisplayMode { SHADED, HOLLOW_MESH, MESH_EDGES, WIREFRAME, SHADED_WITH_EDGES };

// User-facing path-tracing render-engine choice (PT settings dropdown) -
// mirrors DenoiserDevicePreference's placement/style in RtDenoiser.h,
// including the Auto option. Auto resolves to a concrete CPU/GPU choice via
// ViewportWidget::effectivePathTracingEnginePreference() - see that
// function's doc comment for how (a cheap check, not a new probe). Every
// render-path branch reads the EFFECTIVE preference, never this raw one
// directly, so Auto never needs handling at individual call sites - CPU and
// GPU remain the only two real backends as far as rendering code is concerned.
enum class RtPathTracingEnginePreference
{
	Auto,
	CPU,
	GPU
};

// ---------------------------------------------------------------------------
// TextureSlotInfo
// Describes one texture slot as seen by the GPU — used by TextureDebugPanel.
// Built inside ViewportWidget::requestTextureReadback() via glGetTexImage readback.
// ---------------------------------------------------------------------------
struct TextureSlotInfo
{
	QString  slotName;              // human-readable name ("albedoMap", "normalMap", …)
	int      unitIndex  = -1;       // GL texture unit index (0, 6, 10–31)
	GLuint   textureId  = 0;        // GL object ID; 0 = slot not populated
	QPixmap  thumbnail;             // 64×64 readback pixmap; null when textureId == 0
	bool     isActive        = false; // textureId != 0 (a texture is bound)
	bool     extensionEnabled = false;// the parent KHR extension is active (may be true even
	                                  // when no texture is bound — e.g. sheen colour factor set)
	bool     isMarker        = false; // synthetic slot used only for scalar-driven activity
	                                  // detection; never shown in the thumbnail grid
};
Q_DECLARE_METATYPE(QVector<TextureSlotInfo>)

class ViewportWidget : public QOpenGLWidget, QOpenGLFunctions_4_5_Core
{
	Q_OBJECT
public:
	ViewportWidget(QWidget* parent = 0, const char* name = 0);
	~ViewportWidget();

	void retranslateUI();

	void updateView();

	void resizeView(int w, int h) { resizeGL(w, h); }
	void setViewMode(ViewMode mode);
	void setCameraUpAxisZUp(bool zUp, bool syncToolbar = true);
	bool isCameraUpAxisZUp() const { return _viewCtrl.cameraUpAxisZUp(); }
	void setProjection(ViewProjection proj);
	ViewProjection projection() const { return _viewCtrl.projection(); }
	void setCameraMode(Camera::CameraMode mode);
	Camera::CameraMode cameraMode() const;

	void setMultiView(bool active) { _viewCtrl.setMultiViewActive(active); }
	void setRotationActive(bool active);
	void setPanningActive(bool active);
	void setZoomingActive(bool active);

	void setShowCenterAxisOverride(bool show) { _viewCtrl.setUserShowAxisOverride(show); update(); }
	void setShowCornerAxisOverride(bool show) { _viewCtrl.setUserShowCornerAxisOverride(show); update(); }
	void setShowViewCubeOverride(bool show) { _viewCtrl.setShowViewCubeOverride(show); update(); }
	void setCornerAxisPosition(CornerAxisPosition position) { _viewCtrl.setCornerAxisPosition(position); update(); }

	void beginWindowZoom();
	void performWindowZoom();

	void setDisplayList(const std::vector<int>& ids);
	GltfCameraData cameraDataForMvfSave(const GltfCameraData& source) const;
	void triggerShadowRecomputation();
	void setShadowQuality(AdaptiveShadowMapper::QualityLevel quality);
	float calculateLightDistance();

	QVector<QUuid> duplicateObjects(const std::vector<int>& ids);

	void updateFloorPlane();
	// Extracted from updateFloorPlane() so setLightOffset() can also call it
	// directly - the persistent PunctualLights fallback light (distinct from
	// buildPathTracedSnapshot()'s own freshly-recomputed "keyLight"; see that
	// function's doc comment) is otherwise only ever refreshed when
	// updateFloorPlane() itself runs (scene load/resize/bounding-box change),
	// leaving it stale at the OLD light-offset position after a slider drag
	// while buildPathTracedSnapshot() still appends a second, correctly-
	// positioned keyLight on top - two point lights, casting two visibly
	// different shadow directions in CPU/GPU path tracing, neither of which
	// is what raster's own single, always-live shadow shows.
	void refreshFallbackLight();
	void updateBoundingSphere();

	void updateBoundingBox();

	int getModelNum() const
	{
		return _modelNum;
	}

	void updateClippingPlane();
	void showClippingPlaneEditor(bool show);
	void showExplodedViewPanel(bool show);
	ExplodedViewPanel* getExplodedViewPanel() const { return _explodedViewPanel; }
	void updateExplosion();
	QWidget* attachOverlayPanel(QWidget* contentWidget, const QRect& geometry,
	                            Qt::Alignment alignment = Qt::AlignTop | Qt::AlignLeft,
	                            const QString& objectName = QString());
	QWidget* takeOverlayPanel(QWidget* contentWidget);
	void refreshDetachedNavigationOverlayTheme();
	void setClippingPlaneHatchMode(ClippingPlaneHatchMode mode);
	void setClippingPlaneHatchPattern(HatchPattern pattern);
	void setHatchTiling(int tiling);
	void setHatchLineThickness(float width);
	void setHatchIntensity(float spacing);
	void setHatchLayers(int layers);
	void setHatchLineColor(const QColor& color);
	void setHatchTexture(const QString& path);

	void showAxis(bool show);
	void showTransformGizmoForSelection(bool show);
	bool beginExplodedViewManualPlacement(const QVector<QUuid>& selectionUuids = {});
	void finishExplodedViewManualPlacement();
	void clearExplodedViewManualPlacement();
	bool isExplodedViewManualPlacementActive() const { return _explodedViewCtrl.isManualPlacementActive(); }
	bool hasExplodedViewManualPlacement() const { return !_explodedViewCtrl.manualOriginalStates().isEmpty(); }
	bool hasExplodedViewManualTransformChanges() const;
	QSet<QUuid> explodedViewManualPlacementUuids() const;
	QVector3D explodedViewManualPlacementTranslationDelta() const;
	QVector3D explodedViewManualPlacementRotationDelta() const;
	void setExplodedViewManualPlacementTranslationDelta(const QVector3D& delta);
	void setExplodedViewManualPlacementRotationDelta(const QVector3D& delta);
	QMap<QUuid, TransformState> explodedViewManualStates() const;
	void restoreExplodedViewManualStates(const QMap<QUuid, TransformState>& states);
	bool userModelTransformForFile(const QString& sourceFile,
	                               QMatrix4x4& outTransform) const;

	void showShadows(bool show);
	void showSelfShadows(bool show);
	void showEnvironment(bool show);
	void showSkyBox(bool show);
	void blurSkyBox(bool blur) { setSkyBoxBlurPercent(blur ? 100 : 0); }
	void setSkyBoxBlurPercent(int percent) { _renderCtrl.setSkyBoxBlurPercent(std::clamp(percent, 0, 100)); update(); }
	void showReflections(bool show);
	void setShadowCatcherDarkness(float darkness);
	void setShadowCatcherBaseColor(const QVector3D& color);
	void setShadowCatcherMetalness(float metalness);
	void setShadowCatcherRoughness(float roughness);
	void setGroundMode(GroundMode mode);
	GroundMode groundMode() const { return _renderCtrl.groundMode(); }
	void showFloor(bool show) { setGroundMode(show ? GroundMode::Floor : GroundMode::None); }
	bool isFloorShown() { return _renderCtrl.groundMode() == GroundMode::Floor; }
	bool isGridShown() const { return _renderCtrl.groundMode() == GroundMode::Grid; }
	void showFloorTexture(bool show);
	void setFloorTexture(QImage img);

	std::vector<SceneMesh*> getMeshStore() const
	{
		return _sceneRuntime.meshPointers();
	}

	void addToDisplay(SceneMesh*);
	void removeFromDisplay(int index);
	void centerScreen(std::vector<int> selectedIDs);
	void select(int id)                    { if (_selectionManager) _selectionManager->select(id); }
	void deselect(int id)                  { if (_selectionManager) _selectionManager->deselect(id); }
	void syncMeshSelectionVisualState()    { if (_selectionManager) _selectionManager->syncMeshSelectionVisualState(); }

	bool loadAssImpModel(const QString& fileName, const UVMethod& uvMethod, QString& error, bool progressiveLoading = false);

	bool generateUVsForMeshes(const std::vector<int>& ids, const UVMethod& uvMethod, const UVConfig& uvConfig, QString& error);

	aiScene* getAssImpScene() const { return _sceneRuntime.globalScene(); }
	glm::mat4 getGlobalSceneTransform() const { return _sceneRuntime.globalSceneTransform(); }

	void invertADSOpacityTexMap(const std::vector<int>& ids, const bool& inverted) { _sceneRuntime.invertAdsOpacityMaps(ids, inverted); }

	void setMaterialToObjects(const std::vector<int>& ids, const Material& mat);
	void setTexturesToObjects(const std::vector<int>& ids, const Material& mat);
	void synchronizeTextureCache(const Material* material, Material::TextureType type);
	void clearTextureCache();

	void setTransformation(const std::vector<int>& ids, const QVector3D& trans, const QVector3D& rot, const QVector3D& scale);
	void resetTransformation(const std::vector<int>& ids);
	void applyTransforms(const QMap<int, TransformState>& transforms, bool fitView = true);
	void applyExplodedViewTransforms(const QMap<int, TransformState>& transforms, bool fitView = false);

	void setSkyBoxTextureFolder(QString folder);
	bool loadCubemapFromSingleHDR(const QString& filePath);
	bool convertEquirectangularToCubemap(const QString& filePath);
	bool convertEquirectangularToCubemapQuad(const QString& filePath);

	void renderConversionCube();

	void setAnisotropicFilteringLevel(int level) { _renderCtrl.setAnisotropicFilteringLevel(level); }
	int getAnisotropicFilteringLevel() const { return _renderCtrl.anisotropicFilteringLevel(); }

	void setTransmissionEnabled(const bool& enabled);
	bool isTransmissionEnabled() const { return _renderCtrl.transmissionEnabled(); }
	void setActiveAnimation(const QString& sourceFile, int clipIndex);
	void setAnimationPlaying(bool playing);

	// Drop the cached animation runtime for a file whose meshes were removed
	// from the document, stopping playback if that file was the active one.
	// Without this the stale runtime (old UUIDs) survives deletion and can be
	// picked up when the same file is imported again.
	void clearAnimationRuntimeForFile(const QString& sourceFile);
	void seekAnimation(double timeSeconds);
	void setAnimationLooping(bool looping) { _animCtrl.setLooping(looping); emit animationStateChanged(); }
	void setAnimationPlaybackSpeed(double speed);
	void syncRuntimeNodeTransforms(const QString& sourceFile);
	void refreshAnimationMaterialState(const QString& sourceFile);
	QString activeAnimationFile() const { return _animCtrl.activeAnimationFile(); }
	int activeAnimationClip() const { return _animCtrl.activeAnimationClip(); }
	double currentAnimationTimeSeconds() const { return _animCtrl.animationCurrentTimeSeconds(); }
	bool isAnimationPlaying() const { return _animCtrl.isPlaying(); }
	bool isAnimationLooping() const { return _animCtrl.isLooping(); }
	double animationPlaybackSpeed() const { return _animCtrl.playbackSpeed(); }

	// glTF camera switching
	void activateGltfCamera(const QString& sourceFile, int cameraIndex);
	void resetToSystemCamera();
	bool isGltfCameraActive()     const { return _animCtrl.activeGltfCameraIndex() >= 0; }
	QString activeGltfCameraFile()  const { return _animCtrl.activeGltfCameraFile(); }
	int     activeGltfCameraIndex() const { return _animCtrl.activeGltfCameraIndex(); }

public:
	QVector4D getDefaultLightColor() const;
	void setDefaultLightColor(const QVector4D& defaultLightColor);

	QVector3D getLightPosition() const;
	QVector3D getLightOffset() const { return _renderCtrl.lightOffset(); }
	void setLightOffset(const QVector3D& offset);

	float getFloorSize() const { return _floorSize; }

	bool isShaded() const;
	DisplayMode getDisplayMode() const;
	void setDisplayMode(DisplayMode mode);
	bool isRealismEnabled() const { return _realismEnabled; }
	void setRealismEnabled(bool enabled);
	ShadingNormalMode shadingNormalMode() const { return _shadingNormalMode; }
	void setShadingNormalMode(ShadingNormalMode mode);

	bool isVertexNormalsShown() const { return _renderCtrl.debugOverlayEnabled() && _renderCtrl.debugOverlayMode() == DebugOverlayMode::VertexNormals; }
	void setShowVertexNormals(bool showVertexNormals);
	bool isBoundingBoxShown() const { return _renderCtrl.debugOverlayEnabled() && _renderCtrl.debugOverlayMode() == DebugOverlayMode::BoundingBox; }
	void setShowBoundingBox(bool showBoundingBox);
	DebugOverlayMode debugOverlayMode() const { return _renderCtrl.debugOverlayMode(); }
	void setDebugOverlayMode(DebugOverlayMode mode);
	bool isDebugOverlayEnabled() const { return _renderCtrl.debugOverlayEnabled(); }
	void setDebugOverlayEnabled(bool enabled);
	void setDebugOverlayAvailability(bool boundingBox, bool vertexNormals, bool faceNormals);

	bool isFaceNormalsShown() const { return _renderCtrl.debugOverlayEnabled() && _renderCtrl.debugOverlayMode() == DebugOverlayMode::FaceNormals; }
	void setShowFaceNormals(bool showFaceNormals);

	std::vector<int> getDisplayedObjectsIds() const;
	std::vector<int> getHiddenObjectsIds() const;
	const std::vector<int>& currentVisibleObjectIds() const { return _sceneRuntime.currentVisibleObjectIds(); }

	bool isVisibleSwapped() const;

	BoundingSphere getBoundingSphere() const;

	QColor getBgTopColor() const;
	void setBgTopColor(const QColor& bgTopColor);


	QColor getBgBotColor() const;
	void setBgBotColor(const QColor& bgBotColor);

	int getBgGradientStyle() const { return _renderCtrl.gradientStyle(); }
	// resetPathTracedIdleTimer(): the gradient style feeds the PT snapshot's
	// fallback-background scalars (RtEnvironment::fallbackGradientStyle) -
	// without a restart, an already-converged PT frame keeps showing the old
	// style. Camera-grade restart only (no scene-revision bump): env scalars
	// flow per-launch, no GPU rebuild needed - see RtOptixSceneTracer::
	// renderScene(). No-op when PT isn't armed.
	void setBgGradientStyle(int style) { _renderCtrl.setGradientStyle(style); resetPathTracedIdleTimer(); }
	void loadBgColorSettings();
	void loadNavigationSettings();
	void loadRenderSettings();

	struct CameraPose
	{
		QVector3D position;
		QVector3D viewDir;
		QVector3D upVector;
		QVector3D rightVector;
		float     viewRange;
	};
	CameraPose saveCameraPose() const;
	void       restoreCameraPose(const CameraPose& pose);

	RenderingMode getRenderingMode() const { return _renderCtrl.renderingMode(); }
	void setRenderingMode(const RenderingMode& renderingMode);

	// ---- Path-traced rendering mode ----------------------------------------
	// Arms the "Path Traced" mode: forces the raster shader to PBR (path
	// tracing never feeds RenderingMode::PATH_TRACED into the shader uniform
	// itself - see RenderEnums.h and the design note above onRenderingMode-
	// Selected() in ModelViewer.cpp) and starts the idle-detection timer.
	// While armed, camera interaction behaves differently per backend (see
	// resetPathTracedIdleTimer()'s own doc comment for the full split): on
	// CPU/Embree, any camera interaction still cancels the in-flight/
	// converged trace and falls back to the live PBR raster feed immediately.
	// On GPU/OptiX, camera interaction instead restarts a reduced-quality
	// INTERACTIVE trace (startInteractivePathTracedGpuSession()) and keeps
	// compositing it live - raster is never shown for GPU PT while armed.
	// CPU/Embree still promotes to the full user-configured quality once the
	// camera settles (onPathTracedIdleTimeout()); GPU/OptiX has no such
	// promotion anymore - the same continuous interactive accumulator just
	// keeps converging/denoising in place once the camera holds still (see
	// InteractivePtRenderer's class doc comment and onPathTracedIdleTimeout(),
	// which is a GPU no-op).
	//
	// startInteractiveSessionNow (default true) controls whether arming also
	// immediately starts/warms up the interactive session - pass false when
	// the caller (requestPathTracedRenderNow()) is about to immediately
	// replace it with the settled session anyway, so arming doesn't pay a
	// real GAS/IAS rebuild + synchronous warm-up launch just to have
	// startPathTracedSession() tear it back down a moment later.
	void armPathTracedRenderingMode(bool startInteractiveSessionNow = true);
	void disarmPathTracedRenderingMode();
	bool isPathTracedRenderingModeArmed() const { return _pathTracedArmed; }

	// Call when geometry/material/light/visibility changes for a reason other
	// than direct viewport interaction (undo/redo, a material/light panel
	// edit, transform typed into a field, etc.) - anything that isn't already
	// covered by mousePressEvent()/wheelEvent()/keyPressEvent()/inertia.
	// UNLIKE a camera-only event, this always falls back to the live raster
	// feed immediately on BOTH backends (cameraInteracting stays false/
	// default) - a material/light/geometry edit invalidates shading
	// correctness in a way camera movement doesn't (see resetPathTracedIdleTimer()'s
	// doc comment for why camera-only GPU restarts are safe to keep showing a
	// stale-but-still-correct frame through, and why this call site
	// deliberately doesn't get that treatment). The next startPathTracedSession()
	// call already rebuilds the RtSceneSnapshot from current scene state
	// unconditionally. The revision bump lets GPU PT distinguish real scene/
	// env changes from camera-only restarts so it can keep its GAS/IAS alive
	// across camera movement.
	//
	// resetPathTracedIdleTimer() itself (re)arms the debounced resume warm-up
	// (see armPathTracedResumeWarmUp()/onPathTracedResumeWarmUpTimeout()'s doc
	// comments) whenever it tears the interactive accumulator down - this is
	// just one of several call sites that share that same behavior, not a
	// special case, so nothing scene-mutation-specific needs to happen here
	// beyond the revision bump.
	void notifyPathTracedSceneMutated();

	// User-adjustable PT quality settings (PathTracingDialog) - stored here
	// rather than pushed straight into _rtSession/CpuPathTracer::Settings so
	// they survive across arm/disarm and apply to the NEXT
	// startPathTracedSession() call, same lifecycle as every other snapshot
	// input it already reads fresh from _renderCtrl/_viewCtrl each call.
	void setPathTracingMaxSamples(uint32_t maxSamples) { _ptMaxSamples = maxSamples > 0 ? maxSamples : 1; }
	void setPathTracingMaxBounces(int maxBounces) { _ptMaxBounces = std::max(1, maxBounces); }
	uint32_t pathTracingMaxSamples() const { return _ptMaxSamples; }
	int pathTracingMaxBounces() const { return _ptMaxBounces; }

	// Advanced settings - see CpuPathTracer::Settings/RtPathTracingSession
	// for what each one actually controls.
	void setPathTracingDenoiserEnabled(bool enabled) { _ptDenoiserEnabled = enabled; }
	void setPathTracingDenoiserDevicePreference(DenoiserDevicePreference preference) { _ptDenoiserDevicePreference = preference; }
	DenoiserDevicePreference pathTracingDenoiserDevicePreference() const { return _ptDenoiserDevicePreference; }
	// Switching engines mid-session used to leave the OLD engine's already-
	// converged (and now stale) frame on screen - this setter used to be a
	// plain field assignment, never stopping the old session/invalidating
	// the presenter, and the idle timer had usually already fired and gone
	// quiet, so nothing repainted until the user happened to nudge the
	// camera. Worse, if that nudge (e.g. a zoom) landed before the switch
	// forced a restart, the stale frame (captured at the OLD zoom level)
	// stayed composited over the NEWLY-resized raster underneath - "two
	// models of different sizes" on screen at once. resetPathTracedIdleTimer()
	// immediately stops both sessions and invalidates the presenter (clearing
	// the stale frame, falling back to the live raster), then
	// startPathTracedSession() re-renders with the new engine right away
	// instead of waiting for the idle-settle countdown (that debounce exists
	// for rapid camera interaction, not a single explicit menu choice).
	void setPathTracingEnginePreference(RtPathTracingEnginePreference preference)
	{
		if (_ptEnginePreference == preference)
			return;
		_ptEnginePreference = preference;
		if (!_pathTracedArmed)
			return; // not in path-traced mode right now - just remember the preference for next time
		resetPathTracedIdleTimer();
		startPathTracedSession();
	}
	RtPathTracingEnginePreference pathTracingEnginePreference() const { return _ptEnginePreference; }
	// Resolves Auto to a concrete CPU/GPU choice - GPU if this document's
	// OptiX tracer initialized successfully, CPU otherwise. Cheap: _ptOptixSession's
	// RtOptixSceneTracer already ran the real cudaFree(0)/optixInit()/device-
	// context/pipeline setup unconditionally in its own constructor the
	// moment this ViewportWidget was created (see RtOptixSceneTracer's own
	// constructor), so isAvailable() here is just reading an already-computed
	// bool, not probing anything new. Every render-path branch below reads
	// THIS, never _ptEnginePreference directly, so Auto never needs handling
	// at individual call sites - CPU and GPU remain the only two real
	// backends as far as rendering code is concerned. pathTracingEnginePreference()
	// above still returns the RAW (possibly Auto) preference, since
	// PathTracingDialog's combo box needs to keep showing "Auto" as what the
	// user actually chose, not silently normalize it to whatever it resolved to.
	//
	// NOT the reference point for DenoiserDevicePreference::OptiX - unlike the
	// render engine choice above, the native OptiX denoiser (RtDenoiser) owns
	// its own standalone OptixDeviceContext and works regardless of which
	// engine actually produced the frame, so _ptDenoiserDevicePreference is
	// forwarded to both _rtSession and _ptOptixSession as-is (see the
	// setDenoiserDevicePreference() call sites in ViewportWidget.cpp).
	RtPathTracingEnginePreference effectivePathTracingEnginePreference() const
	{
		if (_ptEnginePreference == RtPathTracingEnginePreference::Auto)
			return _ptOptixSession.isAvailable() ? RtPathTracingEnginePreference::GPU : RtPathTracingEnginePreference::CPU;
		return _ptEnginePreference;
	}
	void setPathTracingEnvImportanceSamplingEnabled(bool enabled) { _ptEnvImportanceSamplingEnabled = enabled; }
	void setPathTracingFireflyClampThreshold(float threshold) { _ptFireflyClampThreshold = std::max(0.01f, threshold); }
	void setPathTracingMaxTransmissionBounces(int maxBounces) { _ptMaxTransmissionBounces = std::max(1, maxBounces); }
	void setPathTracingRussianRouletteStartDepth(int depth) { _ptRussianRouletteStartDepth = std::max(1, depth); }
	// KHR_materials_volume_scatter's free-flight random walk's own, separate
	// scatter-event budget - see CpuPathTracer::Settings::maxVolumeScatterBounces's
	// doc comment.
	void setPathTracingMaxVolumeScatterBounces(int maxBounces) { _ptMaxVolumeScatterBounces = std::max(1, maxBounces); }
	// CPU (Embree) only - see CpuPathTracer::Settings::maxShadowRayHits' doc
	// comment for why GPU (OptiX) has no equivalent setting.
	void setPathTracingMaxShadowRayHits(int hits) { _ptMaxShadowRayHits = std::max(1, hits); }
	bool pathTracingDenoiserEnabled() const { return _ptDenoiserEnabled; }
	bool pathTracingEnvImportanceSamplingEnabled() const { return _ptEnvImportanceSamplingEnabled; }
	float pathTracingFireflyClampThreshold() const { return _ptFireflyClampThreshold; }
	int pathTracingMaxTransmissionBounces() const { return _ptMaxTransmissionBounces; }
	int pathTracingRussianRouletteStartDepth() const { return _ptRussianRouletteStartDepth; }
	int pathTracingMaxShadowRayHits() const { return _ptMaxShadowRayHits; }
	int pathTracingMaxVolumeScatterBounces() const { return _ptMaxVolumeScatterBounces; }

	// Applies the user's persisted PT settings (QSettings "pathtracing/*"
	// keys - same keys PathTracingDialog::saveSettings() writes) on top of
	// whatever these members currently hold, narrowing/leaving each one
	// untouched if its key was never saved. Called once unconditionally from
	// the constructor - NOT only from PathTracingDialog::loadSettings() (that
	// still calls this too, so re-opening the dialog picks up any changes
	// made outside it) - because Path Tracing can trigger via the idle timer
	// without the dialog ever having been opened in the session, which
	// previously left every setting pinned to its hardcoded default (e.g.
	// _ptMaxSamples's 16) until the user happened to open it once.
	void loadPathTracingSettingsFromDisk();

	// True when the most recently built PT scene combines orthographic
	// projection with a thin-walled transmissive material (KHR_materials_
	// transmission without KHR_materials_volume) - a genuine mathematical
	// degenerate case (every pixel samples the same environment direction,
	// see startPathTracedSession()'s detection), not a bug. PathTracingDialog
	// surfaces this so the user understands why such glass looks flat
	// instead of assuming the renderer is broken.
	bool pathTracingOrthoThinWallWarningActive() const { return _ptOrthoThinWallWarningActive; }

	// Progress snapshot for PathTracingDialog's poll timer - current/target
	// sample counts and whether the worker is still running. Cheap (no frame
	// copy) - see RtPathTracingSession::currentSampleCount(). Reads from
	// whichever backend's session is actually the active one, so the
	// progress bar/elapsed-time display works identically for both engines.
	void pathTracingProgress(uint32_t& outCurrentSamples, uint32_t& outTargetSamples, bool& outRunning) const
	{
		const bool gpu = effectivePathTracingEnginePreference() == RtPathTracingEnginePreference::GPU;
		if (gpu && _pathTracedInteractiveActive)
		{
			outCurrentSamples = _interactivePtRenderer.currentSampleCount();
			outTargetSamples  = _interactivePtRenderer.maxSampleCount();
			outRunning        = _interactivePtRenderer.isFrameInFlight() || outCurrentSamples < outTargetSamples;
			return;
		}
		if (effectivePathTracingEnginePreference() == RtPathTracingEnginePreference::GPU)
		{
			outCurrentSamples = _ptOptixSession.currentSampleCount();
			outTargetSamples  = _ptOptixSession.maxSamples();
			outRunning        = _ptOptixSession.isRunning();
			return;
		}
		outCurrentSamples = _rtSession.currentSampleCount();
		outTargetSamples  = _rtSession.maxSamples();
		outRunning        = _rtSession.isRunning();
	}

	// Snapshot of everything PathTracingDialog's Diagnostics tab displays -
	// see the diagnostics-tab feature notes for the field list this mirrors
	// (Renderer/GPU/Traversal/Denoiser, Resolution/Triangles/BLAS-TLAS build
	// time/samples-per-sec/render time). Deliberately a single call rather
	// than several small accessors, so PathTracingDialog can gate ALL of it
	// behind "is the Diagnostics tab actually the visible one right now" at
	// one call site instead of several - every field read here is already
	// cheap/precomputed (see RtOptixSceneTracer's own diagnostics accessors'
	// doc comments), but there's still no reason to touch any of it, even
	// this cheaply, while the tab isn't on screen to show it.
	struct PathTracingDiagnostics
	{
		bool gpuEngineActive = false;
		QString rendererName;   // "OptiX (GPU)" or "Embree (CPU)"
		QString gpuDeviceName;  // physical GPU name, if this build has OptiX at all - empty otherwise
		bool traversalKnown = false; // false while the CPU engine is active - traversal mode is an OptiX-only concept
		bool hasHardwareRT = false;
		QString denoiserName;
		int width = 0;
		int height = 0;
		bool triangleCountKnown = false; // false while the CPU engine is active - see pathTracingDiagnostics()'s doc comment
		uint64_t triangleCount = 0;
		bool buildTimesKnown = false; // false while the CPU engine is active - BLAS/TLAS are OptiX-only concepts
		double gasBuildMs = 0.0;
		double iasBuildMs = 0.0;
		uint32_t currentSamples = 0;
		uint32_t targetSamples = 0;
		// Deliberately no elapsedMs field here: pathTracingElapsedMs() is a
		// live, never-reset session clock that keeps ticking after Stop is
		// pressed - callers computing a rate (samples/sec, render time) MUST
		// use PathTracingDialog's own frozen-on-stop elapsed value (see
		// onProgressTimer()'s _frozenElapsedMs) instead, or those rates would
		// keep sliding toward zero forever after rendering actually stops.
	};
	PathTracingDiagnostics pathTracingDiagnostics() const
	{
		PathTracingDiagnostics d;
		const bool gpu = effectivePathTracingEnginePreference() == RtPathTracingEnginePreference::GPU;
		const bool interactiveGpu = gpu && _pathTracedInteractiveActive;
		d.gpuEngineActive = gpu;
		d.rendererName    = gpu ? QStringLiteral("OptiX (GPU)") : QStringLiteral("Embree (CPU)");
		const RtOptixSceneTracer& gpuTracer = interactiveGpu ? _interactivePtTracer : _ptOptixSession.tracer();
		d.gpuDeviceName   = QString::fromLatin1(gpuTracer.deviceName());
		d.traversalKnown  = gpu && gpuTracer.isAvailable();
		d.hasHardwareRT   = gpuTracer.hasHardwareRT();
		d.denoiserName    = QString::fromLatin1(
			gpu ? (interactiveGpu ? _interactivePtRenderer.activeDenoiserName() : _ptOptixSession.activeDenoiserName())
			    : _rtSession.activeDenoiserName());
		d.width           = gpu ? (interactiveGpu ? _interactivePtRenderer.renderWidth() : _ptOptixSession.width())  : _rtSession.width();
		d.height          = gpu ? (interactiveGpu ? _interactivePtRenderer.renderHeight() : _ptOptixSession.height()) : _rtSession.height();
		d.triangleCountKnown = gpu;
		d.triangleCount   = gpu ? gpuTracer.lastTriangleCount() : 0;
		d.buildTimesKnown = gpu;
		d.gasBuildMs      = gpu ? gpuTracer.lastGasBuildMs() : 0.0;
		d.iasBuildMs      = gpu ? gpuTracer.lastIasBuildMs() : 0.0;
		bool running = false;
		pathTracingProgress(d.currentSamples, d.targetSamples, running);
		return d;
	}

	// Milliseconds since the CURRENTLY active PT session actually began -
	// _ptSessionElapsedTimer is (re)started at the single place a session
	// really starts (startPathTracedSession()/startOptixTestPathTracedSession()),
	// regardless of what triggered it: the dialog's own Render button, a
	// keyboard shortcut toggling path-traced mode directly, or an automatic
	// camera-settle restart. PathTracingDialog reads this directly instead of
	// running its own independent clock that only starts once the dialog
	// happens to be open and polling - a dialog opened AFTER a shortcut-
	// triggered session was already well underway previously had no way to
	// know that, and showed elapsed time counting up from 0 instead of the
	// session's real age.
	qint64 pathTracingElapsedMs() const { return _ptSessionElapsedTimer.isValid() ? _ptSessionElapsedTimer.elapsed() : 0; }

	// Whether whichever backend's session is CURRENTLY the active one
	// (per _ptEnginePreference) is running - used by the paintGL()/
	// applicationStateChanged watchdogs that self-heal a stuck-idle path-
	// traced mode. Checking only _rtSession.isRunning() unconditionally (an
	// earlier version of both watchdogs did) is permanently false whenever
	// GPU is selected (that backend never touches _rtSession at all), making
	// both watchdogs think a build-in-progress GPU session was "stuck idle"
	// and restart it on every single paint call - each restart calling
	// RtOptixPathTracingSession::stop() first, which kills whatever
	// buildScene()/first-chunk-render was already in flight, so the session
	// could never actually finish and publish its first frame. A real,
	// previously-unnoticed bug once the GPU path moved from one blocking
	// optixLaunch() (where hasFrame() flipped true near-instantly, before
	// paintGL() got a chance to see otherwise) to a background progressive
	// session with real build/first-chunk latency.
	// _pathTracedInteractiveActive covers the continuous interactive
	// accumulator (InteractivePtRenderer has no worker thread/isRunning() of
	// its own - it's driven from paintGL() - so "running" for it just means
	// "armed and already given at least one camera pose", the same flag
	// startInteractivePathTracedGpuSession() sets). Without this, the
	// app-reactivation/visibility-change watchdogs below would see
	// _ptOptixSession.isRunning()==false (correctly - it's no longer used
	// for auto-interaction, see resetPathTracedIdleTimer()'s GPU branch) and
	// wrongly conclude nothing is running, restarting the settled session
	// redundantly alongside the interactive one that's already live.
	bool pathTracedSessionRunning() const
	{
		return effectivePathTracingEnginePreference() == RtPathTracingEnginePreference::GPU
			? (_ptOptixSession.isRunning() || _pathTracedInteractiveActive)
			: _rtSession.isRunning();
	}

	// Raw linear HDR frame (un-tonemapped, optionally denoised) for fast EXR
	// export. In CPU mode this comes from _rtSession.latestFrame(); in settled
	// GPU mode from _ptOptixSession.latestFrame(); and while live interactive
	// GPU PT is active from InteractivePtRenderer's latest completed device
	// frame, read back on demand through _interactivePtTracer. RtPresenter's
	// tonemap only happens at PRESENT time in the display shader, so none of
	// these paths mutate the underlying linear radiance buffer.
	std::vector<glm::vec3> pathTracingRawFrame(int& outWidth, int& outHeight) const
	{
		uint32_t sampleCount = 0;
		if (effectivePathTracingEnginePreference() == RtPathTracingEnginePreference::GPU)
		{
			if (_pathTracedInteractiveActive)
			{
				RtCamera frameCamera;
				uint64_t generation = 0;
				if (void* deviceFrame = _interactivePtRenderer.pollCompletedFrame(outWidth, outHeight, frameCamera, generation))
				{
					std::vector<glm::vec3> hostFrame;
					std::vector<float> hostAlpha;
					if (_interactivePtTracer.readbackDeviceRGBABuffer(deviceFrame, outWidth, outHeight, hostFrame, hostAlpha))
						return hostFrame;
				}
				outWidth = 0;
				outHeight = 0;
				return {};
			}
			return _ptOptixSession.latestFrame(outWidth, outHeight, sampleCount);
		}
		return _rtSession.latestFrame(outWidth, outHeight, sampleCount);
	}

	// Arms Path Traced mode AND starts tracing immediately, rather than
	// waiting for the idle-settle countdown armPathTracedRenderingMode()
	// alone leaves running - PathTracingDialog's "Render" button wants the
	// press to visibly start work right away, not after a camera-idle delay
	// that may never arrive if the user isn't touching the viewport at all.
	void requestPathTracedRenderNow()
	{
		// See armPathTracedRenderingMode()'s doc comment for why false here -
		// startPathTracedSession() right below is about to start the real
		// settled session regardless of engine, tearing down anything the
		// interactive path just started.
		armPathTracedRenderingMode(/*startInteractiveSessionNow=*/false);
		startPathTracedSession();
	}

	// Renders and returns the current frame with the axis triad/view cube/
	// mesh-count HUD overlays suppressed - for PathTracingDialog's Export,
	// which wants exactly the composited raster+path-traced pixels, not a
	// viewport screenshot. Triggers a real synchronous re-paint (via
	// QOpenGLWidget::grabFramebuffer(), which paintGL() then sees
	// _capturingCleanFrame set for) rather than reading back whatever was
	// last on screen, then restores normal HUD-visible display afterward.
	QImage captureCleanPathTracedImage()
	{
		_capturingCleanFrame = true;
		QImage img = grabFramebuffer();
		_capturingCleanFrame = false;
		update(); // restore the normal HUD-visible view
		return img;
	}

	// Current on-screen device-pixel resolution - same fbWidth/fbHeight
	// computation startPathTracedSession() uses. PathTracingDialog compares
	// its requested export resolution against this to decide whether a
	// downscale of the already-converged frame is enough (fast path) or a
	// fresh renderPathTracedOffline() call is needed (requested resolution
	// exceeds this in either dimension).
	void pathTracingViewportResolution(int& outWidth, int& outHeight) const
	{
		const qreal dpr = devicePixelRatioF();
		outWidth  = static_cast<int>(width()  * dpr);
		outHeight = static_cast<int>(height() * dpr);
	}

	// Live tonemap settings - same values passed to _rtPresenter.draw() for
	// on-screen display. PathTracingDialog's offline export path needs
	// these directly (see RtTonemap.h) since it never touches the GPU/
	// RtPresenter at all, unlike the fast path which just grabs the
	// already-tonemapped framebuffer.
	void pathTracingToneMapSettings(bool& outHdrToneMapping, bool& outGammaCorrection,
		float& outScreenGamma, float& outIblExposure, int& outToneMapMode) const
	{
		outHdrToneMapping  = _renderCtrl.hdrToneMapping();
		outGammaCorrection = _renderCtrl.gammaCorrection();
		outScreenGamma     = _renderCtrl.screenGamma();
		outIblExposure     = _renderCtrl.iblExposure();
		outToneMapMode     = static_cast<int>(_renderCtrl.toneMappingMode());
	}

	// Blocking offline path-traced render at an arbitrary resolution,
	// decoupled entirely from the interactive session/viewport (see
	// buildPathTracedSnapshot()'s doc comment for the shared setup logic,
	// and PathTracingDialog::onExportClicked() for when this is used vs the
	// fast downscale-existing-frame path). Dispatches to CPU (RtEmbreeScene/
	// CpuPathTracer) or GPU (renderPathTracedOfflineGpu(), RtOptixSceneTracer)
	// based on _ptEnginePreference - same engine the interactive viewport is
	// currently using. Genuinely blocks the calling thread for the whole
	// render - no worker thread - per an explicit call that a blocking
	// offline export is acceptable; the caller is expected to pump
	// QApplication::processEvents() (WITHOUT ExcludeUserInputEvents - see
	// cancelPathTracedOfflineRender()'s doc comment for why) from onProgress
	// to keep the UI visually responsive and let a cancel request actually
	// reach this call. onProgress is called once per completed sample with
	// (currentSample, maxSamples). Returns false (outLinearRgb left
	// untouched) if the scene/camera isn't ready to render at all, or (GPU
	// only) if OptiX isn't available on this machine. outCancelled, if
	// non-null, is set true when the render stopped early because
	// cancelPathTracedOfflineRender() was called mid-render - a false
	// return with outCancelled set is not a failure and shouldn't be
	// reported as one.
	bool renderPathTracedOffline(int width, int height,
		const std::function<void(uint32_t currentSample, uint32_t maxSamples)>& onProgress,
		std::vector<glm::vec3>& outLinearRgb, bool* outCancelled = nullptr);

	// Requests that an in-progress renderPathTracedOffline() stop at the
	// next opportunity (next sample boundary on GPU, next scanline on CPU -
	// see CpuPathTracer::renderPass()'s own cancelFlag doc comment) rather
	// than running to completion. Safe to call from a Qt slot invoked via
	// QApplication::processEvents() while renderPathTracedOffline() is
	// still blocking the calling thread further up the same call stack -
	// this is the ONLY way a click can reach that call at all, since it
	// never returns to the event loop on its own until done or cancelled.
	// No-op if no offline render is currently in progress (the flag is
	// reset at the start of every renderPathTracedOffline() call, so a
	// stale request can't affect a later, unrelated one).
	void cancelPathTracedOfflineRender() { _ptOfflineCancelRequested.store(true, std::memory_order_release); }

	void setCappingPlanesEnabled(const bool& enabled) { _renderCtrl.setCappingEnabled(enabled); }
	bool cappingPlanesEnabled() const { return _renderCtrl.cappingEnabled(); }

	void setYZClippingEnabled(const bool& enabled) { _renderCtrl.setYZClippingEnabled(enabled); }
	bool yzClippingEnabled() const { return _renderCtrl.yzClippingEnabled(); }
	void setZXClippingEnabled(const bool& enabled) { _renderCtrl.setZXClippingEnabled(enabled); }
	bool zxClippingEnabled() const { return _renderCtrl.zxClippingEnabled(); }
	void setXYClippingEnabled(const bool& enabled) { _renderCtrl.setXYClippingEnabled(enabled); }
	bool xyClippingEnabled() const { return _renderCtrl.xyClippingEnabled(); }

	void setClippingXFlipped(const bool& flipped) { _renderCtrl.setClippingXFlipped(flipped); }
	bool clippingXFlipped() const { return _renderCtrl.clippingXFlipped(); }
	void setClippingYFlipped(const bool& flipped) { _renderCtrl.setClippingYFlipped(flipped); }
	bool clippingYFlipped() const { return _renderCtrl.clippingYFlipped(); }
	void setClippingZFlipped(const bool& flipped) { _renderCtrl.setClippingZFlipped(flipped); }
	bool clippingZFlipped() const { return _renderCtrl.clippingZFlipped(); }

	void setClippingXCoeff(const float& coeff) { _renderCtrl.setClippingXCoeff(coeff); }
	float clippingXCoeff() const { return _renderCtrl.clippingXCoeff(); }
	void setClippingYCoeff(const float& coeff) { _renderCtrl.setClippingYCoeff(coeff); }
	float clippingYCoeff() const { return _renderCtrl.clippingYCoeff(); }
	void setClippingZCoeff(const float& coeff) { _renderCtrl.setClippingZCoeff(coeff); }
	float clippingZCoeff() const { return _renderCtrl.clippingZCoeff(); }

	bool getHdrToneMapping() const { return _renderCtrl.hdrToneMapping(); }
	bool getGammaCorrection() const { return _renderCtrl.gammaCorrection(); }
	float getScreenGamma() const { return _renderCtrl.screenGamma(); }

	// Environment mapping accessors
	// index: 0 = ViewerIBL, 1 = Studio, 2 = Outdoor, 3 = Office
	GLuint getEnvironmentMap(int index = 0, bool regenerate = false);
	GLuint getIrradianceMap(int index = 0, bool regenerate = false);
	GLuint getPrefilterMap(int index = 0, bool regenerate = false);
	GLuint getSheenPrefilterMap(int index = 0, bool regenerate = false);
	unsigned int getPrefilterMipLevels() const { return _renderCtrl.prefilterMipLevels(); }
	unsigned int getSheenPrefilterMipLevels() const { return _renderCtrl.sheenPrefilterMipLevels(); }
	GLuint getBrdfLUT() const { return _renderCtrl.brdfLUTTexture(); }
	GLuint getCharlieLUT() const { return _renderCtrl.charlieLUTTexture(); }
	GLuint getSheenELUT() const { return _renderCtrl.sheenELUTTexture(); }
	bool isEnvironmentMapEnabled() const { return _renderCtrl.envMapEnabled(); }
	bool isIBLEnabled() const { return _renderCtrl.useIBL(); }
	float getIBLExposure() const { return _renderCtrl.iblExposure(); }
	float getEnvMapExposure() const { return _renderCtrl.envMapExposure(); }
	QString getCurrentSkyboxFolder() const { return _renderCtrl.currentSkyboxFolder(); }
	bool isSkyBoxShown() const { return _renderCtrl.skyBoxEnabled(); }
	bool isSkyBoxHDRIEnabled() const { return _renderCtrl.skyBoxTextureHDRI(); }
	int getSkyBoxBlurPercent() const { return _renderCtrl.skyBoxBlurPercent(); }
	float getSkyBoxFOV() const { return _renderCtrl.skyBoxFOV(); }
	float getPerspFOV()  const { return _viewCtrl.FOV(); }
	float getSkyBoxZRotationDegrees() const { return _renderCtrl.skyBoxZRotation(); }
	bool areReflectionsEnabled() const { return _renderCtrl.reflectionsEnabled(); }
	// Implied by GroundMode::InfinitePlane being selected - see that enum
	// value's own doc comment.
	bool isShadowCatcherEnabled() const { return _renderCtrl.groundMode() == GroundMode::InfinitePlane; }
	float shadowCatcherDarkness() const { return _renderCtrl.shadowCatcherDarkness(); }
	QVector3D shadowCatcherBaseColor() const { return _renderCtrl.shadowCatcherBaseColor(); }
	float shadowCatcherMetalness() const { return _renderCtrl.shadowCatcherMetalness(); }
	float shadowCatcherRoughness() const { return _renderCtrl.shadowCatcherRoughness(); }
	bool isFloorTextureShown() const { return _renderCtrl.floorTextureDisplayed(); }
	bool areShadowsEnabled() const { return _renderCtrl.shadowsEnabled(); }
	bool areSelfShadowsEnabled() const { return _renderCtrl.selfShadowsEnabled(); }
	bool areDefaultLightsEnabled() const { return _renderCtrl.useDefaultLights(); }
	bool arePunctualLightsEnabled() const { return _renderCtrl.usePunctualLights(); }
	bool areLightsShown() const { return _renderCtrl.showLights(); }

	ViewToolbar* getViewToolbar() const { return _viewToolbar; }

	void cleanUpShaders();

	// Recycle bin operations (used by DeleteCommand)
	void moveToRecycleBin(const QUuid& uuid, int originalIndex);
	bool restoreFromRecycleBin(const QUuid& uuid);
	void permanentlyDeleteFromBin(const QUuid& uuid);

	// Query methods
	bool isInRecycleBin(const QUuid& uuid) const;
	QVector<QUuid> getRecycleBinUuids() const;
	QList<QUuid> getPendingSceneUuids() const;

	// UUID lookup methods
	SceneMesh* getMeshByUuid(const QUuid& uuid) const;
	SceneMesh* getMeshByIndex(int index) const;
	int getIndexByUuid(const QUuid& uuid) const;
	QUuid getUuidByIndex(int index) const;

	// Generate a name that doesn't clash with any existing mesh name.
	QString generateUniqueMeshName(const QString& baseName) { return _sceneRuntime.generateUniqueMeshName(baseName); }

	// ---- MVF mesh loading ----

	using PreparedMvfMesh = ::PreparedMvfMesh;

	/// Clear the mesh store and display list (safe to call from main thread).
	/// Called before uploading new MVF meshes to replace any existing geometry.
	void clearMeshStore();

	/// Single-mesh GL upload for use with BlockingQueuedConnection.
	/// Called once per mesh from the main thread while worker waits.
	void uploadOneMvfMesh(const PreparedMvfMesh& pm);

	/// GL-only upload: creates SceneMesh objects, uploads VBOs and
	/// textures, and populates the display list.  Must run on the main
	/// (GL) thread.  Updates the progress bar between meshes.
	bool uploadPreparedMvfMeshes(const QVector<PreparedMvfMesh>& meshes);

	void setParsedLights(const GltfLightData& lights);

	/// Rebuilds the parsed light baseline from all SceneGraph-registered light data.
	/// Connected to SceneGraph::lightDataChanged to stay current on model add/remove.
	void onSceneLightDataChanged();

	/// Thin slot that delegates to SceneRuntime. Qt::UniqueConnection requires a
	/// member-function pointer — it does not work with lambda connections in Qt6.
	void onSceneStructureChanged() { _sceneRuntime.invalidateRuntimeVisibilityHierarchy(); }

	/// Accessor for the foreground shader (for pre-load shader validation).
	ShaderProgram* getShader() const { return _renderCtrl.fgShader(); }

	void setSectionCapsDynamicEnabled(bool enabled) { _renderCtrl.setDynamicCappingEnabled(enabled); if (!enabled && _renderCtrl.sectionCapsSuppressedDuringInteraction()) setSectionCapsInteractionSuppressed(false); }

	// Moved to AnimationRuntimeController (Phase 8); aliases preserve external access.
	using RuntimeNodeTransform       = AnimationRuntimeController::RuntimeNodeTransform;
	using RuntimeAnimationFileState  = AnimationRuntimeController::RuntimeAnimationFileState;

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
	void renderingModeChanged(int);
	void animationStateChanged();
	void explodedViewManualPlacementChanged();
	void backgroundColorChanged(const QColor& topColor, const QColor& bottomColor);
	// Forwarded from SelectionManager so external panels (e.g. TextureDebugPanel)
	// can react to mesh selection changes without needing access to SelectionManager.
	void selectionChanged(const QList<int>& selectedIds);
	// Emitted by requestTextureReadback() once the GL readback is complete.
	void textureReadbackReady(QVector<TextureSlotInfo> slots, QString meshName);
	void cameraUpAxisChanged(bool zUp);

public slots:
	void animateViewChange();
	void animateFitAll();
	void animateWindowZoom();
	void animateCenterScreen();
	void onInertiaTimer();
	void stopAnimations();
	void checkAndStopTimers();
	void fitAll();
	void fitAllImmediate();
	void setAutoFitViewOnUpdate(bool update) { _viewCtrl.setAutoFitViewOnUpdate(update); }
	void setSelectionHighlighting(bool highlight);
	void performKeyboardNav();
	void disableLowRes();
	void disableSectionCapsInteractionSuppression() { setSectionCapsInteractionSuppressed(false); }
	void setFloorTexRepeatS(double floorTexRepeatS);
	void setFloorTexRepeatT(double floorTexRepeatT);
	void setFloorOffsetPercent(double value);
	void setSkyBoxFOV(double fov) { _renderCtrl.setSkyBoxFOV(static_cast<float>(fov)); update(); }
	void setPerspFOV(int fovDegrees);
	void setSkyBoxZRotation(int index);
	// Direct continuous-angle setter - VisualizationEnvironmentPanel's fine
	// rotation slider combines a preset axis angle (still driven through
	// setSkyBoxZRotation(int)'s fixed 4-way table) with a +/-45 degree
	// offset, and needs to apply the COMBINED result directly rather than
	// snapping to one of the 4 presets. Does exactly what
	// setSkyBoxZRotation(int)'s body does, just parameterized on the raw
	// angle instead of a table index - see that method for why (skybox view
	// matrix rebuild + camera-grade PT restart).
	void setSkyBoxZRotationDegrees(float degrees);
	void setSkyBoxTextureHDRI(bool hdrSet) { _renderCtrl.setSkyBoxTextureHDRI(hdrSet); update(); }
	void enableHDRToneMapping(bool hdrToneMapping) { _renderCtrl.setHdrToneMapping(hdrToneMapping); update(); }
	void enableGammaCorrection(bool gammaCorrection) { _renderCtrl.setGammaCorrection(gammaCorrection); update(); }
	void setScreenGamma(double screenGamma) { _renderCtrl.setScreenGamma(static_cast<float>(screenGamma)); update(); }
	void setHDRToneMappingMode(HDRToneMapMode mode) { _renderCtrl.setToneMappingMode(mode); update(); }
	// resetPathTracedIdleTimer(): envMapExposure feeds the PT snapshot's
	// environment scalars - see setBgGradientStyle()'s identical reasoning.
	// (The neighboring tonemap/gamma/iblExposure setters deliberately DON'T
	// restart: those are present-time uniforms RtPresenter::draw() reads
	// live every paint, so update() alone already shows them immediately.)
	void setEnvMapExposure(double exposure) { _renderCtrl.setEnvMapExposure(std::pow(2.0f, static_cast<float>(exposure))); resetPathTracedIdleTimer(); update(); }
	void setIBLExposure(double exposure) { _renderCtrl.setIblExposure(std::pow(2.0f, static_cast<float>(exposure))); update(); }

	// Getters for tone mapping and gamma settings
	bool isHDRToneMappingEnabled() const { return _renderCtrl.hdrToneMapping(); }
	bool isGammaCorrectionEnabled() const { return _renderCtrl.gammaCorrection(); }
	HDRToneMapMode getHDRToneMappingMode() const { return _renderCtrl.toneMappingMode(); }
	void showLights(bool showLights);
	// notifyPathTracedSceneMutated() (NOT just resetPathTracedIdleTimer() -
	// see that function's own doc comment): both feed buildPathTracedSnapshot()'s
	// light list fresh on every call, which is enough for CPU (RtPathTracingSession::
	// start() unconditionally rebuilds its Embree scene every restart), but
	// RtOptixPathTracingSession::start() only re-uploads its lights buffer
	// (inside buildScene()) when snapshot->revisionId actually changed - a
	// bare idle-timer restart with the SAME revision reuses the GPU's stale
	// lights buffer. Bumping the revision forces both engines to pick up the
	// new light set. Toggling either previously only triggered a raster
	// update(), leaving a path-traced session showing a stale frame with the
	// old light set until some unrelated event (camera move, etc.) happened
	// to restart it.
	// refreshFallbackLight() re-evaluates the persistent PunctualLights
	// fallback entry against the new useDefaultLights() value (see its own
	// doc comment) - without this call, toggling the checkbox never
	// created/cleared that entry at all, only ever affecting
	// buildPathTracedSnapshot()'s separately-recomputed keyLight.
	void useDefaultLights(bool useDefaultLights) { _renderCtrl.setUseDefaultLights(useDefaultLights); refreshFallbackLight(); notifyPathTracedSceneMutated(); update(); }
	void usePunctualLights(bool usePunctualLights) { _renderCtrl.setUsePunctualLights(usePunctualLights); notifyPathTracedSceneMutated(); update(); }

	// Upload a new GPU light list (e.g. after a per-light checkbox toggle) and
	// sync the hasPunctualLights / lightCount shader uniforms in one call.
	void applyEnabledLightList(const std::vector<GPULight>& enabledLights);
	void useIBL(bool useIBL) { _renderCtrl.setUseIBL(useIBL); update(); }
	void showFileReadingProgress(float percent);
	void showMeshLoadingProgress(float percent);
	void showNodeMeshLoadingProgress(int processedNodes, int totalNodes, int processedMeshes, int totalMeshes, bool uvProcessed);
	void swapVisible(bool checked);
	void cancelAssImpModelLoading();
	void onAnimationTick();

	// Accessors for SelectionManager
	QMatrix4x4 getViewMatrix() const { return _viewCtrl.viewMatrix(); }
	QMatrix4x4 getProjectionMatrix() const { return _viewCtrl.projectionMatrix(); }
	QMatrix4x4 getModelViewMatrix() const { return _viewCtrl.modelViewMatrix(); }
	QMatrix4x4 getModelMatrix() const { return _viewCtrl.modelMatrix(); }
	bool isMultiViewActive() const { return _viewCtrl.multiViewActive(); }
	ShaderProgram* getSelectionShader() const { return _renderCtrl.selectionShader(); }
	SelectionManager* getSelectionManager() const { return _selectionManager; }
	bool isMeshAnimationVisibleForSelection(const SceneMesh* mesh) const { return isMeshAnimationVisible(mesh); }
	// Returns the camera configured for the viewport that contains 'pixel'.
	// In multi-view mode the ortho camera is set to the correct orientation
	// (Top/Front/Left) before being returned; the isometric viewport returns
	// the primary camera.  In single-view mode the primary camera is returned.
	Camera* getCameraForPoint(const QPoint& pixel);

	static Material resolveMaterialTextures(ViewportWidget* w, const Material& src);

	// Reads back all per-mesh texture slots for meshId via glGetTexImage and
	// emits textureReadbackReady().  Must be called on the GL thread (or the
	// method calls makeCurrent/doneCurrent internally).  meshId is a _meshStore
	// index; pass -1 to emit an empty result and clear the debug panel.
	void requestTextureReadback(int meshId);

	// Enable or disable a specific texture unit for meshId during rendering.
	// When disabled the unit is replaced with a neutral placeholder texture
	// (white for colour channels, tangent-space neutral for normal maps) so the
	// shader still runs but that channel contributes a neutral value.
	// Calls update() to trigger a repaint.
	void setDebugTextureEnabled(int meshId, int unitIndex, bool enabled);

	// Full-state apply for the checkbox panel.  enabledUnits = currently active
	// (not disabled by user); allUnits = all units with real textures on this mesh.
	// Handles the emissive-only special case (uses in-shader isolation automatically).
	void applyDebugTextureState(int meshId,
	                             const QSet<int>& enabledUnits,
	                             const QSet<int>& allUnits);

	// Global single-channel isolation for the channel dropdown.
	// Applies to every mesh in the scene — no selection required.
	// channelId matches shader IDs (1-9 = geometry, 10+ = texture units).
	// channelId == 0 restores normal rendering on all meshes.
	void setGlobalDebugChannel(int channelId);

	// Remove all debug texture overrides for meshId and repaint.
	void clearDebugTextureOverrides(int meshId);

	// Remove all debug texture AND uniform overrides for meshId and repaint.
	void clearAllDebugOverrides(int meshId);

	// Disable/re-enable an entire KHR extension for meshId by zeroing the
	// relevant scalar factor uniforms and neutral-binding its texture units.
	// extensionKey is one of: "Sheen", "Clearcoat", "Iridescence",
	// "Volume / SSS", "Specular", "Anisotropy", "Transmission",
	// "Diffuse Transmission".
	void setDebugExtensionEnabled(int meshId, const QString& extensionKey, bool enabled);

	// Remove all extension-level debug uniform+texture overrides for meshId.
	void clearDebugExtensionOverrides(int meshId);

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
	void applyOverlayPanelStyle(QWidget* wrapper, const QString& objectName);
	void refreshNavigationOverlayStyle();

	void resizeEvent(QResizeEvent* event);
	void showEvent(QShowEvent* event);
	void hideEvent(QHideEvent* event);
	void mousePressEvent(QMouseEvent*);
	void mouseReleaseEvent(QMouseEvent*);
	void mouseMoveEvent(QMouseEvent*);
	void wheelEvent(QWheelEvent*);
	void keyPressEvent(QKeyEvent* event);
	void keyReleaseEvent(QKeyEvent* event);
	void closeEvent(QCloseEvent* event);

private:

	void createShaderPrograms();
	void syncUniformsToFlatShader();
	void createLights();

	// Fullscreen triangle methods for IBL
	void createFullscreenTriangle();
	void drawFullscreenTriangle();
	void setIBLFaceBasis(QOpenGLShaderProgram* prog, int faceIndex);
	void updateEnvMapRotationMatrix();

	void loadEnvMap();
	void loadIrradianceMap();
	GLuint loadPresetEnvironmentMap(const QString& hdrFilePath);
	bool generatePresetIBLMaps(GLuint sourceCubemap, GLuint& outIrradianceMap, GLuint& outPrefilterMap, GLuint& outSheenPrefilterMap);
	void loadFloor();
	void ensureShadowMapResources();
	void loadGrid();
	void applyFloorPlaneMaterialSettings();
	void syncFloorPlaneAlbedoTexture();
	QVector3D effectiveWorldLightOffset() const;
	QVector3D effectiveWorldLightPosition() const;
	void updateMainLightPosition(float halfObjectSize);
	float updateFloorGeometry();
	void syncDefaultLightColorUniforms();
	void syncPunctualLightUniforms(int lightCount, bool hasPunctualLights);
	bool shouldUseFallbackLightForVisibleScene() const;

	void updatePunctualLights();  // Update lights based on bounding sphere changes
	void setAnimatedLightVisibilityState(const QString& sourceFile, const QVector<bool>& visibleByParsedLight);
	void setAnimatedLightTransformState(const QString& sourceFile, const std::vector<GPULight>& animatedLights);
	void clearAnimatedLightTransformState(const QString& sourceFile);
	void clearAnimatedLightVisibilityState(const QString& sourceFile);
	void setAnimatedMeshVisibilityState(const QString& sourceFile, const QSet<QUuid>& hiddenMeshUuids);
	void clearAnimatedMeshVisibilityState(const QString& sourceFile);
	void recalculateVisibleSceneStats(bool updateMemorySize = false);

	// activeCapPlaneIndex: -1 = no culling, 0 = YZ, 1 = ZX, 2 = XY
	void drawMesh(QOpenGLShaderProgram* prog, int activeCapPlaneIndex = -1);

	// activeClipPlaneIndex: -1 = no clipping (frustum only), 0 = YZ, 1 = ZX, 2 = XY
	void drawOpaqueMeshes(QOpenGLShaderProgram* prog, int activeClipPlaneIndex = -1);
	void drawTransparentMeshes(QOpenGLShaderProgram* prog, int activeClipPlaneIndex = -1);
	void drawMeshesWithClipping(QOpenGLShaderProgram* prog, bool transparentPass);
	void drawSSSMeshesOnly(QOpenGLShaderProgram* prog, int activeClipPlaneIndex = -1);
	void setCommonUniforms(QOpenGLShaderProgram* prog, Camera* camera);

	// Visibility culling
	void extractFrustumPlanes();
	void rebuildClippingContext();
	float computeFullyVisibleMinMeshRadius() const;
	void  updateZoomInLimit();
	bool isMeshAnimationVisible(const SceneMesh* mesh) const;
	bool isMeshVisible(const SceneMesh* mesh, int activeClipPlaneIndex) const;
	bool sceneHasVisibleTransmissionMaterials() const;
	bool sceneHasVisibleSSSMaterials() const;
	void collectVisibleMeshIdsForPass(int nodeIndex,
	                                  int activeClipPlaneIndex,
	                                  bool wantTransparent,
	                                  std::vector<int>& out) const;

	void drawSectionCapping();
	void drawFloor(const bool& drawReflection = true);
	void drawGrid();
	void drawSkyBox(const QMatrix4x4* overrideViewMatrix = nullptr);
	void drawVertexNormals();
	void drawFaceNormals();
	void drawBoundingBoxOverlay();
	void drawDebugOverlay(Camera* camera);
	void drawAxis(Camera* camera);
	void drawCornerAxis(CornerAxisPosition position);
	void drawTransformGizmo(Camera* camera);
	void drawViewCube();
	void drawViewCubeLabels(const QMatrix4x4& viewMatrix, const QMatrix4x4& projectionMatrix, float cubeScale);
	BoundingSphere computeTransformGizmoSelectionSphere() const;
	QVector3D computeTransformGizmoPivot() const;
	std::vector<int> activeTransformGizmoSelectionIds() const;
	void applyExplodedViewManualPlacementSessionTransform();
	void syncTransformGizmoToSelection();
	bool beginTransformGizmoDrag(TransformGizmo::Handle handle, const QPoint& pixel);
	bool beginTransformGizmoTranslationDrag(TransformGizmo::Handle handle, const QPoint& pixel);
	void updateTransformGizmoTranslationDrag(const QPoint& pixel);
	void finishTransformGizmoTranslationDrag(bool commit);
	bool beginTransformGizmoScaleDrag(TransformGizmo::Handle handle, const QPoint& pixel, bool uniformScale);
	void updateTransformGizmoScaleDrag(const QPoint& pixel);
	void finishTransformGizmoScaleDrag(bool commit);
	bool beginTransformGizmoRotationDrag(TransformGizmo::Handle handle, const QPoint& pixel);
	void updateTransformGizmoRotationDrag(const QPoint& pixel);
	void finishTransformGizmoRotationDrag(bool commit);
	void drawLights();

	void bindIBLTextures();

	void render(Camera* camera);
	void renderToShadowBuffer();
	void renderQuad();
	void renderMeshWithDisplayMode(SceneMesh* mesh, DisplayMode mode);
	void renderMeshExploded(SceneMesh* mesh, DisplayMode mode);

	void gradientBackground(float top_r, float top_g, float top_b, float top_a,
		float bot_r, float bot_g, float bot_b, float bot_a, int gradientStyle);
	void syncCameraWorldUp();
	void rotateCurrentCameraAroundWorldX(float degrees);
	QString sceneUpAxisLabel(SceneUpAxis sceneUpAxis) const;
	void applyAutoOrientCameraConvention(SceneUpAxis sceneUpAxis);
	void warnOnConflictingImportedSceneUpAxis(const QString& fileName, SceneUpAxis sceneUpAxis);

	QRect viewCubeRect() const;
	QRect viewCubeScreenRect() const;
	void initializeViewCubeLabels();
	bool computeViewCubeRenderState(QRect& viewportRect,
	                               QMatrix4x4& viewMatrix,
	                               QMatrix4x4& projectionMatrix,
	                               QMatrix4x4& modelMatrix,
	                               float& cubeScale) const;
	bool pickViewCubeRegionAtPixel(const QPoint& pixel, QVector3D& outwardNormal, int* regionId = nullptr) const;
	bool handleViewCubeClick(const QPoint& pixel);
	void updateViewCubeHover(const QPoint& pixel, Qt::MouseButtons buttons);
	bool orientCameraToViewCubeNormal(const QVector3D& outwardNormal);

	void splitScreen();

	void animateToRotation(const QQuaternion& targetRotation);
	void setRotations(float xRot, float yRot, float zRot);
	void setZoomAndPan(float zoom, QVector3D pan);
	void setView(QVector3D viewPos, QVector3D viewDir, QVector3D upDir, QVector3D rightDir);
	void fitBoxToScreen(const BoundingBox& box);

	// Collect a sampled set of world-space vertex positions from every visible
	// mesh (≤ 1024 samples per mesh for performance).  Using actual vertices
	// instead of 8 per-mesh AABB corners gives a genuinely tight silhouette:
	// phantom corners from combining per-axis extremes that never coexist in
	// real geometry are eliminated, so the fit zooms in as tight as possible.
	std::vector<QVector3D> collectVisibleCorners() const;

	// Core fit computation on an explicit corner set + explicit view axes.
	// Separating axes from corners lets setViewMode() pass the *destination*
	// quaternion's axes so rotation and zoom animate concurrently.
	// If outCenter is non-null it receives the projected visual centre of the
	// geometry (midpoint of view-space extents), which callers should set as
	// the new orbit/pan target so the scene appears centred on screen.
	float computeFitViewRange(const std::vector<QVector3D>& corners,
	                          const QVector3D& right, const QVector3D& up,
	                          const QVector3D& viewDir,
	                          QVector3D* outCenter = nullptr) const;

	// Convenience: collects visible corners, then calls the core with the
	// provided axes (used by setViewMode with target-orientation axes).
	float computeFitViewRange(const QVector3D& right, const QVector3D& up,
	                          const QVector3D& viewDir,
	                          QVector3D* outCenter = nullptr) const;

	// Convenience: collects visible corners + reads axes from the current
	// view matrix (used by fitAll and projection-toggle).
	float computeFitViewRange(QVector3D* outCenter = nullptr) const;
	float computeOrthographicFitViewRangeForViewport(
		const std::vector<QVector3D>& corners,
		const QVector3D& right,
		const QVector3D& up,
		const QVector3D& viewDir,
		int viewportWidth,
		int viewportHeight,
		QVector3D* outCenter = nullptr,
		const QVector3D& eyePos = QVector3D(0, 0, 0)) const;
	QVector3D computeVisibleWorldCenter(const std::vector<QVector3D>& corners) const;
	float computeSharedOrthographicMultiViewRange(
		const std::vector<QVector3D>& corners,
		int viewportWidth,
		int viewportHeight,
		const QVector3D& eyePos = QVector3D(0, 0, 0)) const;
	void configureOrthoSubviewCamera(ViewMode viewMode,
		const std::vector<QVector3D>& corners,
		int viewportWidth,
		int viewportHeight,
		const QVector3D& sharedCenter,
		float sharedViewRange);

	float highestModelZ() { return _viewCtrl.visibleHighestZ(); }
	float lowestModelZ()  { return _viewCtrl.visibleLowestZ(); }
	bool positionGameplayCameraForScene(Camera::CameraMode mode);

	QList<int> sweepSelect(const QPoint& pixel, bool addToSelection = false);  // Sweep selection using rubber band
	QVector3D get3dTranslationVectorFromMousePoints(const QPoint& start, const QPoint& end);
	unsigned int loadTextureFromFile(const char* path,
		GLenum wrapS = GL_REPEAT, GLenum wrapT = GL_REPEAT,
		GLenum minFilter = GL_LINEAR_MIPMAP_LINEAR, GLenum magFilter = GL_LINEAR,
		bool flipY = false);
	void setupClippingUniforms(QOpenGLShaderProgram* prog, QVector3D pos);

	void onMeshBatchReady(const std::vector<AssImpMeshData>& batch);
	SceneMesh* createMeshFromData(const AssImpMeshData& meshData);
	void syncFileNodeTransforms(const QString& sourceFile);
	void reapplyGltfCameraAfterTransform();
	void applyNodeTransformsToMeshes(const QString& sourceFile,
		const AnimationRuntimeController::RuntimeAnimationFileState& runtime,
		AnimationRuntimeController::AnimationSampleResult& result,
		SceneNode* fileNode);
	void applyMorphTargetWeights(const QString& sourceFile,
		const AnimationRuntimeController::AnimationSampleResult& result);
	void applyAnimatedMaterialChanges(const AnimationRuntimeController::AnimationSampleResult& result);
	void applyAnimatedMeshVisibility(const QString& sourceFile,
		const AnimationRuntimeController::RuntimeAnimationFileState& runtime,
		const AnimationRuntimeController::AnimationSampleResult& result,
		SceneNode* fileNode);
	void applyAnimatedLightTransforms(const QString& sourceFile,
		const AnimationRuntimeController::RuntimeAnimationFileState& runtime,
		const AnimationRuntimeController::AnimationSampleResult& result,
		SceneNode* fileNode);
	void applyAnimatedCamera(const QString& sourceFile,
		const AnimationRuntimeController::RuntimeAnimationFileState& runtime,
		const AnimationRuntimeController::AnimationSampleResult& result);
	void applyAnimationPose(const QString& sourceFile, int clipIndex, double timeSeconds);
	void resetAnimationPose(const QString& sourceFile);
	void updateAnimatedMeshState(const QString& sourceFile,
		const QHash<QUuid, QMatrix4x4>& worldTransformsByNodeUuid);

	GLuint createGPUTextureFromImage(const QImage& image, const TextureSamplerSettings& samplers);
	GLuint uploadDecodedTextureImage(const QImage& image, const TextureSamplerSettings& samplers);
	GLuint uploadKtx2TextureImage(const QString& path, const std::string& mapType, const TextureSamplerSettings& samplers,
		QImage* outDecodedImage = nullptr);
	GLuint uploadDecodedTexture(Material::Texture& texture, const QImage& image);
	GLuint uploadKtx2Texture(const QString& path, const std::string& mapType, Material::Texture& texture);
	UVMethod promptLargeModelUVDecision(int totalTriangles, UVMethod currentMethod);
	void retainTexture(unsigned int texId);
	void releaseTexture(unsigned int texId);

public:
	unsigned int getOrCreateTextureCached(const QString& cacheKey,
		const QImage& image,
		const TextureSamplerSettings& samplers = TextureSamplerSettings());
	unsigned int getOrLoadKtx2TextureCached(const QString& path,
		const std::string& mapType,
		const TextureSamplerSettings& samplers = TextureSamplerSettings());
	unsigned int getOrLoadTextureCached(const QString& path,
		const TextureSamplerSettings& samplers = TextureSamplerSettings());

private:
		
	// --- Transmission Buffer Methods ---
	void initTransmissionBuffer();
	void renderToTransmissionBuffer(Camera* camera, const QColor& topColor, const QColor& botColor);
	void cleanupTransmissionBuffer();
	void resizeTransmissionBuffer(int width, int height);

	// --- SSS (Subsurface Scattering) Buffer Methods ---
	void initSSSBuffer();
	void renderToSSSBuffer(Camera* camera);
	void resizeSSSBuffer(int width, int height);
	void cleanupSSSBuffer();

	void createWhiteTexture();

	void generateCubemapMipmaps(GLuint cubemapTexture);

	void setSectionCapsInteractionSuppressed(bool suppressed);
private:
	SceneRuntime _sceneRuntime;

	AnimationRuntimeController _animCtrl;

	ExplodedViewRuntimeController _explodedViewCtrl;

	// Render-pipeline resources — owned here; ViewportWidget aliases every field by
	// reference so all existing call sites in ViewportWidget.cpp remain unchanged.
	// Declaration order: _renderCtrl must come before all its aliases.
	SceneRenderController _renderCtrl;

	// Viewport interaction state — owned here; ViewportWidget aliases every field by
	// reference so all existing call sites in ViewportWidget.cpp remain unchanged.
	// Declaration order: _viewCtrl must come before all its aliases.
	ViewportInteractionController _viewCtrl;

	// Cached per-frame culling contexts — rebuilt in extractFrustumPlanes() /
	// rebuildClippingContext(). Avoids repeated look-ups inside tight render loops.
	VisibilityComputationHelper::FrustumContext  _frustumCtx;
	VisibilityComputationHelper::ClippingContext _clippingCtx;

	ViewToolbar* _viewToolbar;

	QSet<int> _keys;
	DisplayMode _displayMode;
	bool _realismEnabled = false;
	ShadingNormalMode _shadingNormalMode = ShadingNormalMode::SMOOTH;
	// _renderingMode, _bgTopColor, _bgBotColor, _gradientStyle → SceneRenderController (Phase 12)
	int _modelNum;
	QImage _texImage, _texBuffer;
	// _floorTexRepeatS/T → SceneRenderController (Phase 12)
	TextRenderer* _textRenderer;
	TextRenderer* _axisTextRenderer;
	QString _labelTop, _labelFront, _labelLeft, _labelIsometric, _labelDimetric, _labelTrimetric;
	QString _labelAxisX, _labelAxisY, _labelAxisZ;
	QString _labelNumMeshes;
	QString _modelName;

	bool _selectionHighlighting;

	QRubberBand* _rubberBand;
	QRubberBand* _selectRect;
	QTimer* _inertiaTimer        = nullptr;

	// ---- Path-traced rendering mode -----------------------------------------
	// _rtSession/_rtPresenter own the actual background tracing/presentation;
	// this widget only arms/disarms them and feeds them a fresh RtSceneSnapshot
	// on settle - see armPathTracedRenderingMode()/onPathTracedIdleTimeout().
	RtPathTracingSession _rtSession;
	RtPresenter          _rtPresenter;
	bool     _pathTracedArmed        = false; // user selected "Path Traced" mode
	QTimer*  _pathTracedIdleTimer    = nullptr; // reset on every camera-affecting event
	QTimer*  _pathTracedRefreshTimer = nullptr; // periodically repaints while a trace is running
	// Debounced GAS/IAS rebuild + interactive warm-up, armed by
	// armPathTracedResumeWarmUp() every time the interactive accumulator gets
	// torn down while path tracing stays armed (a scene mutation, a scripted
	// view animation, a real resize, hiding/showing this widget's MDI
	// document, ...) - single-shot, restarted on every teardown so a burst of
	// rapid events (e.g. a slider being dragged in VisualizationEnvironmentPanel)
	// only pays the rebuild once, after the burst actually settles, rather
	// than once per tick. See onPathTracedResumeWarmUpTimeout()'s own doc
	// comment for the full rationale and kPathTracedResumeWarmUpDebounceMs in
	// ViewportWidget.cpp for the debounce interval.
	QTimer*  _pathTracedResumeWarmUpTimer = nullptr;
	uint64_t _pathTracedSceneRevision = 1;
	int      _pathTracedFramebufferWidth = 0;
	int      _pathTracedFramebufferHeight = 0;
	bool     _preservePtPresenterOnNextStart = false;
	RtCamera _interactivePtPreviewCamera;
	bool     _interactivePtPreviewCameraValid = false;

	// True while _interactivePtRenderer is the live GPU/OptiX continuous
	// accumulator (see InteractivePtRenderer's class doc comment) - set by
	// startInteractivePathTracedGpuSession(), which resetPathTracedIdleTimer()
	// calls on every camera-affecting event for GPU. There is no more
	// "promote to a different, full-quality session on settle" - the same
	// accumulator just keeps converging/denoising in place once the camera
	// holds still (onPathTracedIdleTimeout() is a GPU no-op now). Cleared
	// false by resetPathTracedIdleTimer()'s hard-invalidate branch (a scene
	// mutation, or switching to the CPU/Embree engine) and by hideEvent()
	// (stopInteractivePtRenderer() releases the renderer's resources but
	// doesn't clear this itself - see hideEvent()'s own doc comment for why
	// leaving it true there was a real bug, not just an oversight).
	// GPU/OptiX only - CPU/Embree never sets this.
	bool  _pathTracedInteractiveActive  = false;
	// Throttles startInteractivePathTracedGpuSession()'s SLOW path only (a
	// real ensureSceneResources()+resize() with a rebuilt snapshot - the
	// first tick of a new interactive burst, or a mid-drag resolution
	// change) - the fast path (InteractivePtRenderer::updateCamera(), used
	// on every other tick) is cheap enough to call unthrottled. See
	// kInteractiveGpuRestartMinIntervalMs in ViewportWidget.cpp.
	qint64 _lastInteractiveGpuRestartMs = 0;
	// Wall-clock timestamp (QDateTime::currentMSecsSinceEpoch()) of the last
	// genuine cameraInteracting=true call into resetPathTracedIdleTimer() -
	// see onPathTracedIdleTimeout()'s doc comment for why this exists: Qt's
	// QTimer can be throttled/coalesced by the OS under heavy GUI-thread load
	// (dragging + GPU launches + presenter uploads is exactly that), so the
	// 450ms single-shot idle timer firing is not, by itself, reliable proof
	// that 450ms of genuine idleness actually passed - it can fire late AND,
	// under coalescing, effectively "early" relative to the last real
	// interaction once the event loop catches up. onPathTracedIdleTimeout()
	// cross-checks against this before treating a timeout as a real settle.
	qint64 _lastCameraInteractionMs = 0;

	// User-adjustable PT quality settings - see setPathTracingMaxSamples()/
	// setPathTracingMaxBounces()'s doc comments. Defaults match
	// RtPathTracingSession/CpuPathTracer::Settings's own defaults exactly, so
	// behavior is unchanged until a user actually opens PathTracingDialog and
	// changes them. _ptMaxSamples deliberately lower (16, not the old 128) -
	// a fast, responsive default that still lets the live viewport refine
	// quickly; the same spinBoxMaxSamples value also drives offline Export
	// (see PathTracingDialog::onExportClicked()), so users doing a final
	// high-quality render should raise it there before exporting.
	uint32_t _ptMaxSamples = 16;
	int      _ptMaxBounces = 6;
	bool     _ptDenoiserEnabled = true;
	bool     _ptEnvImportanceSamplingEnabled = true;
	float    _ptFireflyClampThreshold = 3.0f;
	int      _ptMaxTransmissionBounces = 32;
	int      _ptRussianRouletteStartDepth = 3;
	int      _ptMaxVolumeScatterBounces = 64;
	int      _ptMaxShadowRayHits = 8; // CPU (Embree) only - matches CpuPathTracer::Settings::maxShadowRayHits' default
	DenoiserDevicePreference _ptDenoiserDevicePreference = DenoiserDevicePreference::Auto;
	RtPathTracingEnginePreference _ptEnginePreference = RtPathTracingEnginePreference::Auto;
	RtOptixPathTracingSession _ptOptixSession; // GPU-backend counterpart to _rtSession above - see startOptixTestPathTracedSession()

	// Same-frame, GPU-resident, non-blocking-submission interactive PT
	// renderer (see its own doc comment) - this is the ONLY interactive GPU
	// PT path; _ptOptixSession above is used exclusively for the settled/
	// full-quality session now (its own former interactive half was retired
	// once this renderer was proven - see RtOptixPathTracingSession.h's doc
	// comment). Owns a SEPARATE RtOptixSceneTracer instance (its own
	// GAS/IAS/texture uploads, its own VRAM) rather than sharing
	// _ptOptixSession's internal tracer - RtOptixPathTracingSession only
	// exposes that as const (tracer(), a read-only diagnostics accessor),
	// and even if it exposed a mutable one, having two independent
	// revision-gated callers (_ptOptixSession's own workerLoop() and this
	// renderer) mutate the SAME scene/acceleration-structure state from
	// different threads/call patterns would be a real race - a second,
	// separate tracer avoids that entirely at the cost of ~2x GPU scene
	// memory while a settled session and an interactive one are both live.
	// _interactivePtTracer MUST be declared before _interactivePtRenderer
	// (member construction order, not initializer-list order) since the
	// renderer holds a reference to it.
	RtOptixSceneTracer   _interactivePtTracer;
	InteractivePtRenderer _interactivePtRenderer{ _interactivePtTracer };
	// Retained snapshot from the last slow-path rebuild (see
	// startInteractivePathTracedGpuSession()) - unlike RtOptixPathTracingSession
	// (which stores its own snapshot internally for workerLoop() to read),
	// InteractivePtRenderer is driven entirely from paintGL() and takes a
	// snapshot only transiently (ensureSceneResources()), so ViewportWidget
	// itself must hold onto this to keep supplying tick()'s environment/
	// shadow-setting parameters on every paint without rebuilding.
	std::shared_ptr<const RtSceneSnapshot> _interactivePtRendererSnapshot;
	// Last InteractivePtRenderer::pollCompletedFrame() generation this widget
	// consumed - paintGL()'s interactive-pull block skips re-uploading a frame
	// whose generation it has already seen.
	uint64_t _lastConsumedInteractivePtRendererGeneration = 0;

	bool     _ptOrthoThinWallWarningActive = false; // see pathTracingOrthoThinWallWarningActive()'s doc comment
	QElapsedTimer _ptSessionElapsedTimer; // see pathTracingElapsedMs()'s doc comment

	// See cancelPathTracedOfflineRender()'s doc comment - reset to false at
	// the start of every renderPathTracedOffline() call, checked between
	// samples/chunks by that call and its GPU counterpart. Atomic even
	// though everything touching it currently runs on the same (UI) thread
	// - the whole point is that it's set from a Qt slot invoked via
	// QApplication::processEvents() while a call further up the SAME
	// thread's stack is still blocking, which is a real (if same-thread)
	// concurrent-access pattern worth being explicit and correct about.
	std::atomic<bool> _ptOfflineCancelRequested{ false };

	// Set for the duration of a captureCleanPathTracedImage() call -
	// paintGL() checks this to suppress the axis triad/view cube/mesh-count
	// HUD overlays (see their call sites) so an exported render contains
	// only the composited raster+path-traced scene, matching what a render-
	// to-file export should look like rather than a viewport screenshot.
	bool _capturingCleanFrame = false;

	// cameraInteracting: false (default) is today's original behavior - stop
	// both sessions, invalidate the presenter, fall back to raster, restart
	// the settle countdown. true is passed from call sites representing
	// genuine, USER-DRIVEN camera movement whose per-tick delta naturally
	// decays toward zero as it ends - mouseMoveEvent's orbit/pan/zoom
	// branches, wheelEvent, onInertiaTimer()'s coasting (velocity decays via
	// inertiaDamping() every tick), performKeyboardNav()'s held-key
	// navigation - NOT resize or any scene-mutation call site, which all
	// keep the default. For the GPU/OptiX backend only, true keeps a
	// reduced-quality INTERACTIVE trace (startInteractivePathTracedGpuSession())
	// live and tracking the camera instead of tearing down to raster.
	//
	// The per-frame PROGRAMMATIC animation callbacks - animateViewChange()/
	// animateFitAll()/animateWindowZoom() (Home/standard-view/axonometric/
	// fit/window-zoom transitions) - deliberately pass false instead, even
	// though they're also genuine camera movement: unlike a live drag or an
	// inertia coast, their per-tick delta (a slerp/interpolation step)
	// does NOT decay toward zero as the animation ends, so the interactive
	// trace's inherent one-tick-behind lag (see InteractivePtRenderer's
	// design notes) surfaces as a full-sized, objectionable jerk right at
	// the finish instead of the imperceptible one inertia's decay masks -
	// plain raster/PBR for the whole animation, settling into full-quality
	// PT once it's actually done, was judged smoother in practice. CPU/Embree
	// ignores this flag entirely regardless of any of the above and always
	// takes the original path, since it has no hardware RT acceleration to
	// make a per-frame interactive trace realistic. See
	// armPathTracedRenderingMode()'s doc comment for the user-visible summary.
	void resetPathTracedIdleTimer(bool cameraInteracting = false);
	void onPathTracedIdleTimeout();
	void onPathTracedRefreshTimer();
	void startPathTracedSession();
	// GPU/OptiX-only reduced-quality trace kicked off while the camera is
	// actively moving - see resetPathTracedIdleTimer()'s doc comment. The
	// FIRST call of a new interactive burst does a real (throttled, see
	// _lastInteractiveGpuRestartMs) InteractivePtRenderer::ensureSceneResources()
	// +resize(), reusing that class's own revision-gated GAS/IAS rebuild-skip
	// (camera-only movement leaves _pathTracedSceneRevision unchanged); every
	// subsequent call while that renderer is already at the right resolution
	// instead takes a much cheaper path - InteractivePtRenderer::
	// updateCamera(), which needs neither a rebuilt scene snapshot nor any
	// GPU work of its own - so it's cheap enough to call unthrottled on every
	// mouse-move event. See InteractivePtRenderer::updateCamera()'s doc
	// comment for why that distinction matters (buildPathTracedSnapshot()'s
	// synchronous environment-cubemap GPU readback, not the trace itself, was
	// the real per-tick cost a naive always-restart approach used to pay).
	void startInteractivePathTracedGpuSession();

	// Called unconditionally from the END of startInteractivePathTracedGpuSession()'s
	// slow path - i.e. every time that path runs, regardless of which caller
	// triggered it (armPathTracedRenderingMode(), the debounced
	// onPathTracedResumeWarmUpTimeout(), or a genuine mouse-move/wheel event
	// resuming the accumulator after some other teardown). Forces the FIRST
	// interactive launch's one-time costs - GAS/IAS build (ensureSceneResources()
	// only rebuilds when the scene snapshot's revision actually changed, which
	// is unconditionally true the first time through a fresh slow path) plus
	// OptiX pipeline/SBT warm-up and cold-BVH-cache traversal - to happen
	// synchronously, right there, before the triggering call returns, rather
	// than letting that cost race whatever happens next.
	//
	// This used to only be called from two of the three possible callers
	// (arm-time and the debounced timeout), on the theory that a real drag
	// event would just submit its first launch asynchronously via paintGL()'s
	// tick() like normal. That left a real, if intermittent, gap: if the user
	// started dragging before the debounce timer fired, the mouse-move-driven
	// call was the one that actually ran the slow path, and it never warmed
	// up - the eventual publish showed a camera pose captured back when the
	// rebuild started (stale by the whole build+warmup duration) while the
	// still-live raster skybox/mesh had kept tracking the mouse the whole
	// time, read as a lag/snap. Calling this unconditionally from the slow
	// path itself - rather than trusting each caller to remember to call it -
	// closes that gap structurally: a short synchronous pause exactly when a
	// rebuild happens, never a stale-pose snap once it's done. No-op if
	// startInteractivePathTracedGpuSession() didn't actually start anything
	// (e.g. OptiX unavailable).
	void warmUpInteractivePathTracedGpuSession();

	// Tears down whichever GPU PT session(s) are currently active/converging
	// - shared by resetPathTracedIdleTimer()'s hard-invalidate branch,
	// hideEvent(), and disarmPathTracedRenderingMode(), which previously each
	// reimplemented this teardown slightly differently (see hideEvent()'s own
	// doc comment for the bug that divergence caused). Does NOT touch
	// _pathTracedArmed, _pathTracedIdleTimer, or _pathTracedResumeWarmUpTimer
	// - callers decide those independently based on their own context.
	void teardownActivePathTracedSessions();

	// (Re)arms _pathTracedResumeWarmUpTimer for GPU - call right after
	// teardownActivePathTracedSessions() from any context where path tracing
	// stays armed and is expected to resume later (a scene mutation, a
	// scripted view animation, a real resize, hiding this widget's MDI
	// document, ...). A no-op for CPU/Embree or while not armed at all.
	void armPathTracedResumeWarmUp();

	// _pathTracedResumeWarmUpTimer's single-shot timeout - fires once a burst
	// of teardowns (see armPathTracedResumeWarmUp()'s call sites) goes quiet
	// for kPathTracedResumeWarmUpDebounceMs. Re-enters
	// startInteractivePathTracedGpuSession()'s slow path (rebuilding whatever
	// the teardown invalidated) plus warmUpInteractivePathTracedGpuSession(),
	// but ONLY when nothing is already running (!pathTracedSessionRunning())
	// - if the user resumed dragging before this timer fired, the normal
	// mouse-move path already restarted the interactive session itself (fast
	// or slow path as appropriate), and re-triggering here would be redundant
	// at best, a wasted duplicate GAS rebuild at worst. Also bails if path
	// tracing was disarmed or switched to CPU/Embree in the meantime. Safe to
	// fire while this widget is hidden (see hideEvent()'s call site) - the
	// warm-up is pure CUDA/OptiX work with no GL-context/visibility
	// dependency.
	void onPathTracedResumeWarmUpTimeout();

	// Releases _interactivePtRenderer's GPU resources (stream, per-slot
	// device/host buffers) and forgets its retained snapshot/generation
	// state. Called alongside EVERY _ptOptixSession.stop() call site (see
	// those call sites' own comments) so the "never run two GPU PT backends
	// at once" invariant holds: the settled session (_ptOptixSession) and
	// the interactive renderer are separate objects with separate
	// RtOptixSceneTracer instances now, unlike the old architecture where
	// "interactive" and "settled" were just two modes of the SAME
	// _ptOptixSession (so starting one always implicitly stopped the other
	// for free) - without this call, both could end up alive/holding GPU
	// resources at the same time. Safe/cheap to call even when the renderer
	// was never started this session (releaseResources() is idempotent).
	void stopInteractivePtRenderer();

	// Phase 2a GPU-engine path - see RtOptixSceneParams.h's doc comment for
	// exactly what this does and doesn't render yet (real geometry/
	// instancing/camera, flat-normal-as-color shading, no materials/lights/
	// bounces). Rebuilds the GPU acceleration structure from the current
	// snapshot (see buildPathTracedSnapshot()) and renders it through the
	// real camera at the current framebuffer size, uploading the result
	// through the same _rtPresenter the CPU path uses, so switching the
	// Render Engine dropdown is directly comparable in the same viewport.
	// Called from startPathTracedSession() instead of the real _rtSession
	// setup when the GPU engine is selected.
	void startOptixTestPathTracedSession(int fbWidth, int fbHeight);

	// GPU-engine counterpart to renderPathTracedOffline()'s CPU path -
	// same blocking-until-done, own-fresh-scene-independent-of-the-live-
	// session contract, just built on RtOptixSceneTracer instead of
	// RtEmbreeScene/CpuPathTracer. Mirrors RtOptixPathTracingSession::
	// workerLoop()'s exact chunked running-mean accumulation + final-pass-
	// only denoise (see that function's own doc comment for the numerics
	// rationale), just as a plain synchronous loop in the calling thread
	// rather than a background worker - offline export already blocks the
	// whole application per renderPathTracedOffline()'s own contract, so
	// there is no reason to pay for a worker thread + polling wait here.
	bool renderPathTracedOfflineGpu(int width, int height, const RtSceneSnapshot& snapshot,
		const std::function<void(uint32_t currentSample, uint32_t maxSamples)>& onProgress,
		std::vector<glm::vec3>& outLinearRgb, bool* outCancelled);

	// Builds a fresh RtSceneSnapshot for the given OUTPUT resolution -
	// shared by startPathTracedSession() (the interactive session) and
	// renderPathTracedOffline() (blocking export at an arbitrary
	// resolution), so the light/environment/floor snapshot-building logic
	// isn't duplicated between them. aspectRatio is recomputed from
	// width/height rather than reusing the camera's own configured aspect,
	// so an offline export at a different aspect ratio than the live
	// viewport frames correctly instead of stretching the same framing.
	// Also updates _ptOrthoThinWallWarningActive as a side effect (see its
	// doc comment) - both callers want this detection to run.
	std::shared_ptr<const RtSceneSnapshot> buildPathTracedSnapshot(int width, int height);

	// Selection manager instance (owns all selection logic and state)
	SelectionManager* _selectionManager = nullptr;

	// _defaultLightColor → SceneRenderController (Phase 12)
	QVector4D _ambientLight;
	QVector4D _diffuseLight;
	QVector4D _specularLight;

	QVector3D _lightPosition;
	// _lightOffsetX/Y/Z → SceneRenderController._lightOffset (Phase 12)

	QMatrix4x4 _lightSpaceMatrix;



	QImage					 _floorTexImage;
	float                    _floorSize;
	float 					 _floorSizeFactor;
	// _floorOffsetPercent → SceneRenderController (Phase 12)
	float                    _floorPlaneZ;
	QVector3D                _floorCenter;


	QPointer<QWidget> _navigationOverlayPanel;

	QVBoxLayout* _editorLayout;
	QFormLayout* _lowerLayout;
	QFormLayout* _upperLayout;

	ClippingPlanesEditor* _clippingPlanesEditor;
	ExplodedViewPanel*    _explodedViewPanel;
	PlaneRenderable* _clippingPlaneXY;
	PlaneRenderable* _clippingPlaneYZ;
	PlaneRenderable* _clippingPlaneZX;


	Camera* _primaryCamera;
	Camera* _orthoViewsCamera;

	QTimer* _keyboardNavTimer;
	QTimer* _animateViewTimer;
	QTimer* _animateFitAllTimer;
	QTimer* _animateWindowZoomTimer;
	QTimer* _animateCenterScreenTimer;


	FloorPlane* _floorPlane;
	PlaneRenderable* _gridPlane;
	CubeRenderable* _skyBox;
	// _fsTriVAO/VBO, _skyBoxFaces, _skyBoxFOV/_skyBoxZRotation, gamma/HDR/tone-map settings,

	ConeRenderable* _axisCone;
	ViewCubeMesh* _viewCube = nullptr;
	TransformGizmo* _transformGizmo = nullptr;
	CubeRenderable* _lightCube;
	SphereRenderable* _lightSphere;
	// _showLights → SceneRenderController (Phase 12)

	ModelViewer* _viewer;


	unsigned long long _displayedObjectsMemSize;

	AssImpModelLoader* _assimpModelLoader;
	KTX2Loader _ktx2Loader;
	GPUCapabilities _gpuCapabilities;


	// _hatch* fields → SceneRenderController (Phase 12)

	AdaptiveShadowMapper shadowMapper;

	float _originalBoundingRadius = 1.0f;

	// Navigation settings (from SettingsDialog / QSettings)
	bool  _invertZoom           = false;
	bool  _invertYAxis          = false;
	bool  _smoothNavigation     = true;
	float _mouseSensitivity     = 1.0f; // 1.0 = default (slider 5/10)
	float _wheelSensitivity     = 1.0f; // 1.0 = default (slider 5/10)

	// Derive the user model transform for one file directly from its meshes'
	// TRS state.  Returns true (and fills outTransform) only when every mesh
	// of the file carries the same non-identity transformation — i.e. the
	// user applied a model-level transform.  Lights and glTF cameras of that
	// file follow this exact matrix; the visible-scene bounding sphere plays
	// no role, so hide/show/delete of other models cannot disturb them.
	void updateOverlayEditorTheme();

	void applyGltfCameraEntryTransform(const GltfCameraEntry& cam);
};
