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
#include "GpuResourceRegistry.h"
#include "IGpuContextResource.h"
#include "LambdaGpuResource.h"
#include "RenderableMeshGpuResourceAdapter.h"
#include "SceneRenderController.h"
#include "ViewportInteractionController.h"
#include "Camera.h"
#include "MeasurementData.h"
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
#include "RtInteractiveRenderer.h"
#include "RtInteractionController.h"
#include "RtOptixRayTracingSession.h"
#include "RtOptixSceneTracer.h"
#include "RtRayTracingSession.h"
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
// ClippingPlaneHatchMode, HatchPattern â†’ RenderEnums.h (Phase 11/12)
enum class DisplayMode { SHADED, HOLLOW_MESH, MESH_EDGES, WIREFRAME, SHADED_WITH_EDGES };

// User-facing ray-tracing render-engine choice (PT settings dropdown) -
// mirrors DenoiserDevicePreference's placement/style in RtDenoiser.h,
// including the Auto option. Auto resolves to a concrete CPU/GPU choice via
// ViewportWidget::effectiveRayTracingEnginePreference() - see that
// function's doc comment for how (a cheap check, not a new probe). Every
// render-path branch reads the EFFECTIVE preference, never this raw one
// directly, so Auto never needs handling at individual call sites - CPU and
// GPU remain the only two real backends as far as rendering code is concerned.
enum class RtRayTracingEnginePreference
{
	Auto,
	CPU,
	GPU
};

// ---------------------------------------------------------------------------
// TextureSlotInfo
// Describes one texture slot as seen by the GPU â€” used by TextureDebugPanel.
// Built inside ViewportWidget::requestTextureReadback() via glGetTexImage readback.
// ---------------------------------------------------------------------------
struct TextureSlotInfo
{
	QString  slotName;              // human-readable name ("albedoMap", "normalMap", â€¦)
	int      unitIndex  = -1;       // GL texture unit index (0, 6, 10â€“31)
	GLuint   textureId  = 0;        // GL object ID; 0 = slot not populated
	QPixmap  thumbnail;             // 64Ã—64 readback pixmap; null when textureId == 0
	bool     isActive        = false; // textureId != 0 (a texture is bound)
	bool     extensionEnabled = false;// the parent KHR extension is active (may be true even
	                                  // when no texture is bound â€” e.g. sheen colour factor set)
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
	// buildRayTracedSnapshot()'s own freshly-recomputed "keyLight"; see that
	// function's doc comment) is otherwise only ever refreshed when
	// updateFloorPlane() itself runs (scene load/resize/bounding-box change),
	// leaving it stale at the OLD light-offset position after a slider drag
	// while buildRayTracedSnapshot() still appends a second, correctly-
	// positioned keyLight on top - two point lights, casting two visibly
	// different shadow directions in CPU/GPU ray tracing, neither of which
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
	float getFloorOffsetPercent() const { return _renderCtrl.floorOffsetPercent(); }
	bool isOpenGLInitialized() const { return _renderCtrl.isOpenGLInitialized(); }
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

	// Snapshot the live Camera state into a GltfCameraEntry ("Capture View"
	// in the Cameras tab). The entry has needsNewNode = true and no source
	// file of its own - ModelViewer stores it under the synthetic
	// capturedViewsSourceFileKey() bucket in SceneGraph, so activation,
	// deletion, MVF save/load, and glTF/GLB export all reuse the exact same
	// per-file glTF camera machinery as an authored camera - notably
	// activateGltfCamera()'s system-camera save/restore latch, which a
	// separate bookmark-specific code path used to skip (bug: couldn't
	// return to System Camera after activating a captured view).
	GltfCameraEntry captureCurrentCameraEntry(const QString& name) const;

	// ---- Measurement tool ----------------------------------------------------
	// "Measure Point"/"Measure Distance": while a tool is active, left-clicks
	// place measurement points (via SelectionManager::pickSurfaceAnchor())
	// instead of doing normal selection - see handleMeasurementClick() in
	// mousePressEvent(). v1 scope: static meshes, MVF-only (see MeasurementData.h).
	void setMeasurementTool(MeasurementTool tool);
	MeasurementTool measurementTool() const { return _measurementTool; }

	// Resolves a saved anchor's CURRENT world position by re-deriving it from
	// the referenced mesh's live geometry (getTrsfPoints()), not a frozen
	// value - stays correct if the mesh's transform changes after the
	// measurement was taken. Returns a null QVector3D if the mesh no longer
	// exists (deleted) or the anchor is otherwise unresolvable.
	QVector3D resolveMeasurementAnchor(const MeasurementAnchorRef& ref) const;

	// Resolves an Edge Radius anchor's CURRENT analytic circle (center/axis/
	// radius), re-deriving it from the referenced mesh's precomputed OCC
	// edge data (SceneMesh::getOccEdgeCircles()) and its CURRENT world
	// transform (combinedRenderTransform()) - same "live, not frozen"
	// convention as resolveMeasurementAnchor(). Returns false (outputs left
	// untouched) if the mesh no longer exists, ref isn't an edge anchor, or
	// the referenced edge isn't a circle (shouldn't happen in practice -
	// pickEdgeCircleAnchor() only ever returns circular edges - but a saved
	// measurement could in principle outlive a mesh reload that changes
	// topology).
	bool resolveMeasurementEdgeCircle(const MeasurementAnchorRef& ref,
		QVector3D& outCenter, QVector3D& outAxis, float& outRadius) const;

	// Resolves a GENERAL edge anchor (see MeshEdgeCircleAnchor::edgeIndex's
	// doc comment - any OCC edge on a CAD mesh, or a heuristic feature edge
	// on a non-CAD one) to its chord endpoints and its true length (the sum
	// of its tessellated segment lengths, not just the straight chord
	// distance - works identically for a straight feature edge, a straight
	// OCC line, or a curved OCC edge, with zero curve-type-awareness
	// needed). Used by Edge Length directly, and by the chord endpoints for
	// Edge-to-Edge/Edge-to-Face/Edge-to-Vertex once those exist. Returns
	// false if the mesh no longer exists or ref isn't an edge anchor.
	bool resolveMeasurementEdgeGeometry(const MeasurementAnchorRef& ref,
		QVector3D& outStart, QVector3D& outEnd, float& outLength) const;

	// Resolves a face-pick anchor's CURRENT position + face normal (Face to
	// Face / Point to Face) - "face" here means the picked triangle's own
	// plane (cross product of two of its edges), not a grouped CAD face, so
	// this works on any mesh with no B-Rep topology needed. Same "live, not
	// frozen" convention as resolveMeasurementAnchor() (derives from
	// getTrsfPoints(), so it tracks transform-panel/exploded-view changes).
	// Returns false if ref has no triangle recorded (edge anchor, or a
	// vertex-only anchor with no triangle - shouldn't happen in practice
	// since pickSurfaceAnchor() always records the hit triangle even when it
	// also snaps to a vertex).
	bool resolveMeasurementAnchorPlane(const MeasurementAnchorRef& ref,
		QVector3D& outPosition, QVector3D& outNormal) const;

	// ---- Dimension-line drag (Distance/PointToFace/EdgeLength/EdgeToVertex/
	//      FaceToFace-parallel/EdgeToEdge-parallel/EdgeToFace-parallel, and
	//      FaceToFace/EdgeToEdge/EdgeToFace's shared angle/arc case) --------
	// The raw (un-offset) [a,b] a LINEAR dimension spans, resolved per
	// MeasurementType the same way drawMeasurementOverlay()'s render loop
	// does inline for each case - a small, deliberate duplication so this
	// stays a plain query usable outside the render loop (hit-testing,
	// dragging), rather than threading render-loop state through here.
	// EdgeLength's [a,b] is its chord (resolveMeasurementEdgeGeometry()) -
	// same offset/extension/drag treatment as every other linear dimension,
	// even though the edge itself is already real, visible geometry, for
	// consistency (and so the dimension doesn't have to compete for
	// legibility with the model's own edges/wireframe at the same position).
	// Returns false for types with no straight dimension line at all
	// (Point, both arc types, EdgeRadius, and FaceToFace/EdgeToEdge/
	// EdgeToFace's non-parallel/angle case - see
	// resolveMeasurementAngleGeometry() for that one instead).
	bool resolveMeasurementDimensionSegment(const Measurement& m, QVector3D& outA, QVector3D& outB) const;

	// The DEFAULT perpendicular direction a linear dimension's offset leans,
	// before the user has ever dragged it (Measurement::offsetVector still
	// zero) - given the raw segment and the measurement's captured
	// offsetReferenceDir (falls back to the live camera if unset - see
	// Measurement::offsetReferenceDir's doc comment).
	QVector3D dimensionLinePerp(const QVector3D& a, const QVector3D& b,
		const QVector3D& referenceDir, Camera* camera) const;

	// View-range-relative default magnitude shared by both the linear
	// dimension's default offset (dimensionLinePerp() direction times this)
	// and the angle dimension's default arc radius, so an as-yet-unplaced
	// dimension of either kind looks reasonable regardless of scene scale.
	float defaultDimensionOffsetMagnitude(Camera* camera) const;

	// The full world-space offset a LINEAR dimension line currently sits at
	// (see Measurement::offsetVector's doc comment) - the user's exact
	// dragged vector if they've ever dragged it (both direction AND
	// magnitude - "pivot and extend" combined), else
	// dimensionLinePerp()*defaultDimensionOffsetMagnitude(). Shared by
	// rendering and hit-testing/dragging so all three agree on where the
	// dimension line actually is.
	QVector3D resolveDimensionOffsetVector(const QVector3D& a, const QVector3D& b,
		const Measurement& m, Camera* camera) const;

	// The angle dimension's full construction - vertex, in-plane orthonormal
	// basis (u = the first direction, v completing the plane), the measured
	// angle in radians, AND the resolved arc radius (Measurement::
	// offsetDistance if the user has dragged it, else a default tied to the
	// geometry's own size) - the single authoritative source for all of it,
	// used by rendering, hit-testing, AND dragging alike so none of them can
	// ever disagree about where the arc actually is (unlike computing the
	// default radius independently in more than one place, which is exactly
	// the kind of thing that quietly drifts out of sync over time). Covers
	// every measurement type whose non-parallel case renders as a floating-
	// vertex angle arc - FaceToFace (u = anchor0's face normal), EdgeToEdge
	// (u = anchor0's edge direction), EdgeToFace (u = the edge direction,
	// vertex grounded at the edge's own start point rather than a floating
	// midpoint). The second leg's direction isn't returned separately - it's
	// always exactly u*cos(outAngleRad) + v*sin(outAngleRad), by
	// construction of v via Gram-Schmidt against the angle already computed
	// from the same two inputs. Returns false for any other MeasurementType,
	// for the parallel case of any of the three (that has a straight
	// dimension line instead - see resolveMeasurementDimensionSegment()), or
	// for degenerate input (no well-defined plane to sweep an arc in).
	bool resolveMeasurementAngleGeometry(const Measurement& m, Camera* camera, QVector3D& outVertex,
		QVector3D& outU, QVector3D& outV, float& outAngleRad, float& outRadius) const;

	// Which kind of draggable dimension geometry a hit corresponds to -
	// the two kinds need different drag math (see updateDimensionLineDrag()'s
	// doc comment), so callers need to know which one they grabbed.
	enum class DimensionDragKind { None, Linear, AngleRadius };
	struct DimensionHit { QUuid measurementId; DimensionDragKind kind = DimensionDragKind::None; };

	// Screen-space hit-test against every visible measurement's draggable
	// dimension geometry - a LINEAR dimension's offset line specifically
	// (not the raw measured segment, and not markers/labels), or an ANGLE
	// dimension's arc - whichever is closer wins. Returns a hit with
	// DimensionDragKind::None if nothing is within pixelRadius.
	DimensionHit hitTestDimensionLine(const QPoint& pixel, Camera* camera, int pixelRadius = 8) const;

	// Currently selected measurement in the viewport (independent of mesh
	// selection) - clicking near a measurement's marker/line while no tool
	// is armed selects it (see mousePressEvent()'s hitTestMeasurement()
	// call); Delete removes it (MainWindow's Key_Delete shortcut checks
	// this before falling back to normal mesh deletion). A null QUuid means
	// nothing is selected.
	QUuid selectedMeasurementId() const { return _selectedMeasurementId; }
	void setSelectedMeasurementId(const QUuid& id);

	// Human-readable result string for one measurement, e.g. "Distance:
	// 12.345" or "3-Point Arc Radius: 5.678" - shared by the in-viewport
	// label and the Measurement dialog's results list so both agree.
	QString measurementSummaryText(const Measurement& m) const;

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
	// notifySceneContentMutated(): the gradient style feeds the PT snapshot's
	// fallback-background scalars (RtEnvironment::fallbackGradientStyle) -
	// without a restart, an already-converged PT frame keeps showing the old
	// style. Camera-grade restart only (no scene-revision bump): env scalars
	// flow per-launch, no GPU rebuild needed - see RtOptixSceneTracer::
	// renderScene(). No-op when PT isn't armed.
	void setBgGradientStyle(int style) { _renderCtrl.setGradientStyle(style); _rtInteractionCtrl->notifySceneContentMutated(); update(); }
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

	// ---- Ray-traced rendering mode ----------------------------------------
	// Arms the "Ray Traced" mode: forces the raster shader to PBR (path
	// tracing never feeds RenderingMode::RAY_TRACED into the shader uniform
	// itself - see RenderEnums.h and the design note above onRenderingMode-
	// Selected() in ModelViewer.cpp) and starts the idle-detection timer.
	// While armed, camera interaction behaves differently per backend (see
	// RtInteractionController::notifyCameraInteracting()'s own doc comment
	// for the full split): on
	// CPU/Embree, any camera interaction still cancels the in-flight/
	// converged trace and falls back to the live PBR raster feed immediately.
	// On GPU/OptiX, camera interaction instead restarts a reduced-quality
	// INTERACTIVE trace (startInteractiveRayTracedGpuSession()) and keeps
	// compositing it live - raster is never shown for GPU PT while armed.
	// CPU/Embree still promotes to the full user-configured quality once the
	// camera settles (onRayTracedIdleTimeout()); GPU/OptiX has no such
	// promotion anymore - the same continuous interactive accumulator just
	// keeps converging/denoising in place once the camera holds still (see
	// RtInteractiveRenderer's class doc comment and onRayTracedIdleTimeout(),
	// which is a GPU no-op).
	//
	// startInteractiveSessionNow (default true) controls whether arming also
	// immediately starts/warms up the interactive session - pass false when
	// the caller (requestRayTracedRenderNow()) is about to immediately
	// replace it with the settled session anyway, so arming doesn't pay a
	// real GAS/IAS rebuild + synchronous warm-up launch just to have
	// startRayTracedSession() tear it back down a moment later.
	void armRayTracedRenderingMode(bool startInteractiveSessionNow = true);
	void disarmRayTracedRenderingMode();
	bool isRayTracedRenderingModeArmed() const { return _rtInteractionCtrl->armed(); }

	// Call when geometry/material/light/visibility changes for a reason other
	// than direct viewport interaction (undo/redo, a material/light panel
	// edit, transform typed into a field, etc.) - anything that isn't already
	// covered by mousePressEvent()/wheelEvent()/keyPressEvent()/inertia.
	// UNLIKE a camera-only event, this always falls back to the live raster
	// feed immediately on BOTH backends (cameraInteracting stays false/
	// default) - a material/light/geometry edit invalidates shading
	// correctness in a way camera movement doesn't (see
	// RtInteractionController::notifyCameraInteracting()'s doc comment for
	// why camera-only GPU restarts are safe to keep showing a
	// stale-but-still-correct frame through, and why this call site
	// deliberately doesn't get that treatment). The next startRayTracedSession()
	// call already rebuilds the RtSceneSnapshot from current scene state
	// unconditionally. The revision bump lets GPU PT distinguish real scene/
	// env changes from camera-only restarts so it can keep its GAS/IAS alive
	// across camera movement.
	//
	// RtInteractionController::notifySceneContentMutated() itself
	// (re)arms the debounced resume warm-up (see
	// onRayTracedResumeWarmUpTimeout()'s doc comment) whenever it tears the
	// interactive accumulator down - this is just one of several call sites
	// that share that same behavior, not a special case, so nothing
	// scene-mutation-specific needs to happen here
	// beyond the revision bump.
	void notifyRayTracedSceneMutated();
	// Animation playback/scrubbing is also a scene mutation, but for the GPU
	// backend we want to drive the live interactive PT path with those new
	// revisions instead of unconditionally dropping to raster/PBR. Falls back
	// to notifyRayTracedSceneMutated()'s original teardown behavior if the
	// interactive GPU path can't be restarted/refreshed.
	void notifyRayTracedAnimationMutated();

	// User-adjustable PT quality settings (RtRenderDialog) - stored here
	// rather than pushed straight into _rtSession/CpuPathTracer::Settings so
	// they survive across arm/disarm and apply to the NEXT
	// startRayTracedSession() call, same lifecycle as every other snapshot
	// input it already reads fresh from _renderCtrl/_viewCtrl each call.
	void setRayTracingMaxSamples(uint32_t maxSamples) { _ptMaxSamples = maxSamples > 0 ? maxSamples : 1; }
	void setRayTracingMaxBounces(int maxBounces) { _ptMaxBounces = std::max(1, maxBounces); }
	uint32_t rayTracingMaxSamples() const { return _ptMaxSamples; }
	int rayTracingMaxBounces() const { return _ptMaxBounces; }

	// Advanced settings - see CpuPathTracer::Settings/RtRayTracingSession
	// for what each one actually controls.
	void setRayTracingDenoiserEnabled(bool enabled) { _ptDenoiserEnabled = enabled; }
	void setRayTracingDenoiserDevicePreference(DenoiserDevicePreference preference) { _ptDenoiserDevicePreference = preference; }
	DenoiserDevicePreference rayTracingDenoiserDevicePreference() const { return _ptDenoiserDevicePreference; }
	// Switching engines mid-session used to leave the OLD engine's already-
	// converged (and now stale) frame on screen - this setter used to be a
	// plain field assignment, never stopping the old session/invalidating
	// the presenter, and the idle timer had usually already fired and gone
	// quiet, so nothing repainted until the user happened to nudge the
	// camera. Worse, if that nudge (e.g. a zoom) landed before the switch
	// forced a restart, the stale frame (captured at the OLD zoom level)
	// stayed composited over the NEWLY-resized raster underneath - "two
	// models of different sizes" on screen at once. notifyEngineSwitch()
	// immediately stops both sessions and invalidates the presenter (clearing
	// the stale frame, falling back to the live raster), then
	// startRayTracedSession() re-renders with the new engine right away
	// instead of waiting for the idle-settle countdown (that debounce exists
	// for rapid camera interaction, not a single explicit menu choice).
	void setRayTracingEnginePreference(RtRayTracingEnginePreference preference)
	{
		if (_ptEnginePreference == preference)
			return;
		_ptEnginePreference = preference;
		if (!_rtInteractionCtrl->armed())
			return; // not in ray-traced mode right now - just remember the preference for next time
		_rtInteractionCtrl->notifyEngineSwitch();
		update();
	}
	RtRayTracingEnginePreference rayTracingEnginePreference() const { return _ptEnginePreference; }
	// Resolves Auto to a concrete CPU/GPU choice - GPU if this document's
	// OptiX tracer initialized successfully, CPU otherwise. Cheap: _ptOptixSession's
	// RtOptixSceneTracer already ran the real cudaFree(0)/optixInit()/device-
	// context/pipeline setup unconditionally in its own constructor the
	// moment this ViewportWidget was created (see RtOptixSceneTracer's own
	// constructor), so isAvailable() here is just reading an already-computed
	// bool, not probing anything new. Every render-path branch below reads
	// THIS, never _ptEnginePreference directly, so Auto never needs handling
	// at individual call sites - CPU and GPU remain the only two real
	// backends as far as rendering code is concerned. rayTracingEnginePreference()
	// above still returns the RAW (possibly Auto) preference, since
	// RtRenderDialog's combo box needs to keep showing "Auto" as what the
	// user actually chose, not silently normalize it to whatever it resolved to.
	//
	// NOT the reference point for DenoiserDevicePreference::OptiX - unlike the
	// render engine choice above, the native OptiX denoiser (RtDenoiser) owns
	// its own standalone OptixDeviceContext and works regardless of which
	// engine actually produced the frame, so _ptDenoiserDevicePreference is
	// forwarded to both _rtSession and _ptOptixSession as-is (see the
	// setDenoiserDevicePreference() call sites in ViewportWidget.cpp).
	RtRayTracingEnginePreference effectiveRayTracingEnginePreference() const
	{
		if (_ptEnginePreference == RtRayTracingEnginePreference::Auto)
			return _ptOptixSession.isAvailable() ? RtRayTracingEnginePreference::GPU : RtRayTracingEnginePreference::CPU;
		return _ptEnginePreference;
	}
	void setRayTracingEnvImportanceSamplingEnabled(bool enabled) { _ptEnvImportanceSamplingEnabled = enabled; }
	void setRayTracingFireflyClampThreshold(float threshold) { _ptFireflyClampThreshold = std::max(0.01f, threshold); }
	void setRayTracingMaxTransmissionBounces(int maxBounces) { _ptMaxTransmissionBounces = std::max(1, maxBounces); }
	void setRayTracingRussianRouletteStartDepth(int depth) { _ptRussianRouletteStartDepth = std::max(1, depth); }
	// KHR_materials_volume_scatter's free-flight random walk's own, separate
	// scatter-event budget - see CpuPathTracer::Settings::maxVolumeScatterBounces's
	// doc comment.
	void setRayTracingMaxVolumeScatterBounces(int maxBounces) { _ptMaxVolumeScatterBounces = std::max(1, maxBounces); }
	// CPU (Embree) only - see CpuPathTracer::Settings::maxShadowRayHits' doc
	// comment for why GPU (OptiX) has no equivalent setting.
	void setRayTracingMaxShadowRayHits(int hits) { _ptMaxShadowRayHits = std::max(1, hits); }
	bool rayTracingDenoiserEnabled() const { return _ptDenoiserEnabled; }
	bool rayTracingEnvImportanceSamplingEnabled() const { return _ptEnvImportanceSamplingEnabled; }
	float rayTracingFireflyClampThreshold() const { return _ptFireflyClampThreshold; }
	int rayTracingMaxTransmissionBounces() const { return _ptMaxTransmissionBounces; }
	int rayTracingRussianRouletteStartDepth() const { return _ptRussianRouletteStartDepth; }
	int rayTracingMaxShadowRayHits() const { return _ptMaxShadowRayHits; }
	int rayTracingMaxVolumeScatterBounces() const { return _ptMaxVolumeScatterBounces; }

	// Applies the user's persisted PT settings (QSettings "raytracing/*"
	// keys - same keys RtRenderDialog::saveSettings() writes) on top of
	// whatever these members currently hold, narrowing/leaving each one
	// untouched if its key was never saved. Called once unconditionally from
	// the constructor - NOT only from RtRenderDialog::loadSettings() (that
	// still calls this too, so re-opening the dialog picks up any changes
	// made outside it) - because Ray Tracing can trigger via the idle timer
	// without the dialog ever having been opened in the session, which
	// previously left every setting pinned to its hardcoded default (e.g.
	// _ptMaxSamples's 16) until the user happened to open it once.
	void loadRayTracingSettingsFromDisk();

	// True when the most recently built PT scene combines orthographic
	// projection with a thin-walled transmissive material (KHR_materials_
	// transmission without KHR_materials_volume) - a genuine mathematical
	// degenerate case (every pixel samples the same environment direction,
	// see startRayTracedSession()'s detection), not a bug. RtRenderDialog
	// surfaces this so the user understands why such glass looks flat
	// instead of assuming the renderer is broken.
	bool rayTracingOrthoThinWallWarningActive() const { return _ptOrthoThinWallWarningActive; }

	// Progress snapshot for RtRenderDialog's poll timer - current/target
	// sample counts and whether the worker is still running. Cheap (no frame
	// copy) - see RtRayTracingSession::currentSampleCount(). Reads from
	// whichever backend's session is actually the active one, so the
	// progress bar/elapsed-time display works identically for both engines.
	void rayTracingProgress(uint32_t& outCurrentSamples, uint32_t& outTargetSamples, bool& outRunning) const
	{
		const bool gpu = effectiveRayTracingEnginePreference() == RtRayTracingEnginePreference::GPU;
		if (gpu && _rayTracedInteractiveActive)
		{
			outCurrentSamples = _rtInteractiveRenderer.currentSampleCount();
			outTargetSamples  = _rtInteractiveRenderer.maxSampleCount();
			outRunning        = _rtInteractiveRenderer.isFrameInFlight() || outCurrentSamples < outTargetSamples;
			return;
		}
		if (effectiveRayTracingEnginePreference() == RtRayTracingEnginePreference::GPU)
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

	// Snapshot of everything RtRenderDialog's Diagnostics tab displays -
	// see the diagnostics-tab feature notes for the field list this mirrors
	// (Renderer/GPU/Traversal/Denoiser, Resolution/Triangles/BLAS-TLAS build
	// time/samples-per-sec/render time). Deliberately a single call rather
	// than several small accessors, so RtRenderDialog can gate ALL of it
	// behind "is the Diagnostics tab actually the visible one right now" at
	// one call site instead of several - every field read here is already
	// cheap/precomputed (see RtOptixSceneTracer's own diagnostics accessors'
	// doc comments), but there's still no reason to touch any of it, even
	// this cheaply, while the tab isn't on screen to show it.
	struct RayTracingDiagnostics
	{
		bool gpuEngineActive = false;
		QString rendererName;   // "OptiX (GPU)" or "Embree (CPU)"
		QString gpuDeviceName;  // physical GPU name, if this build has OptiX at all - empty otherwise
		bool traversalKnown = false; // false while the CPU engine is active - traversal mode is an OptiX-only concept
		bool hasHardwareRT = false;
		QString denoiserName;
		int width = 0;
		int height = 0;
		bool triangleCountKnown = false; // false while the CPU engine is active - see rayTracingDiagnostics()'s doc comment
		uint64_t triangleCount = 0;
		bool buildTimesKnown = false; // false while the CPU engine is active - BLAS/TLAS are OptiX-only concepts
		double gasBuildMs = 0.0;
		double iasBuildMs = 0.0;
		uint32_t currentSamples = 0;
		uint32_t targetSamples = 0;
		// Deliberately no elapsedMs field here: rayTracingElapsedMs() is a
		// live, never-reset session clock that keeps ticking after Stop is
		// pressed - callers computing a rate (samples/sec, render time) MUST
		// use RtRenderDialog's own frozen-on-stop elapsed value (see
		// onProgressTimer()'s _frozenElapsedMs) instead, or those rates would
		// keep sliding toward zero forever after rendering actually stops.
	};
	RayTracingDiagnostics rayTracingDiagnostics() const
	{
		RayTracingDiagnostics d;
		const bool gpu = effectiveRayTracingEnginePreference() == RtRayTracingEnginePreference::GPU;
		const bool interactiveGpu = gpu && _rayTracedInteractiveActive;
		d.gpuEngineActive = gpu;
		d.rendererName    = gpu ? QStringLiteral("OptiX (GPU)") : QStringLiteral("Embree (CPU)");
		const RtOptixSceneTracer& gpuTracer = interactiveGpu ? _rtInteractiveTracer : _ptOptixSession.tracer();
		d.gpuDeviceName   = QString::fromLatin1(gpuTracer.deviceName());
		d.traversalKnown  = gpu && gpuTracer.isAvailable();
		d.hasHardwareRT   = gpuTracer.hasHardwareRT();
		d.denoiserName    = QString::fromLatin1(
			gpu ? (interactiveGpu ? _rtInteractiveRenderer.activeDenoiserName() : _ptOptixSession.activeDenoiserName())
			    : _rtSession.activeDenoiserName());
		d.width           = gpu ? (interactiveGpu ? _rtInteractiveRenderer.renderWidth() : _ptOptixSession.width())  : _rtSession.width();
		d.height          = gpu ? (interactiveGpu ? _rtInteractiveRenderer.renderHeight() : _ptOptixSession.height()) : _rtSession.height();
		d.triangleCountKnown = gpu;
		d.triangleCount   = gpu ? gpuTracer.lastTriangleCount() : 0;
		d.buildTimesKnown = gpu;
		d.gasBuildMs      = gpu ? gpuTracer.lastGasBuildMs() : 0.0;
		d.iasBuildMs      = gpu ? gpuTracer.lastIasBuildMs() : 0.0;
		bool running = false;
		rayTracingProgress(d.currentSamples, d.targetSamples, running);
		return d;
	}

	// Milliseconds since the CURRENTLY active PT session actually began -
	// _ptSessionElapsedTimer is (re)started at the single place a session
	// really starts (startRayTracedSession()/startOptixTestRayTracedSession()),
	// regardless of what triggered it: the dialog's own Render button, a
	// keyboard shortcut toggling ray-traced mode directly, or an automatic
	// camera-settle restart. RtRenderDialog reads this directly instead of
	// running its own independent clock that only starts once the dialog
	// happens to be open and polling - a dialog opened AFTER a shortcut-
	// triggered session was already well underway previously had no way to
	// know that, and showed elapsed time counting up from 0 instead of the
	// session's real age.
	qint64 rayTracingElapsedMs() const { return _ptSessionElapsedTimer.isValid() ? _ptSessionElapsedTimer.elapsed() : 0; }

	// Whether whichever backend's session is CURRENTLY the active one
	// (per _ptEnginePreference) is running - used by the paintGL()/
	// applicationStateChanged watchdogs that self-heal a stuck-idle path-
	// traced mode. Checking only _rtSession.isRunning() unconditionally (an
	// earlier version of both watchdogs did) is permanently false whenever
	// GPU is selected (that backend never touches _rtSession at all), making
	// both watchdogs think a build-in-progress GPU session was "stuck idle"
	// and restart it on every single paint call - each restart calling
	// RtOptixRayTracingSession::stop() first, which kills whatever
	// buildScene()/first-chunk-render was already in flight, so the session
	// could never actually finish and publish its first frame. A real,
	// previously-unnoticed bug once the GPU path moved from one blocking
	// optixLaunch() (where hasFrame() flipped true near-instantly, before
	// paintGL() got a chance to see otherwise) to a background progressive
	// session with real build/first-chunk latency.
	// _rayTracedInteractiveActive covers the continuous interactive
	// accumulator (RtInteractiveRenderer has no worker thread/isRunning() of
	// its own - it's driven from paintGL() - so "running" for it just means
	// "armed and already given at least one camera pose", the same flag
	// startInteractiveRayTracedGpuSession() sets). Without this, the
	// app-reactivation/visibility-change watchdogs below would see
	// _ptOptixSession.isRunning()==false (correctly - it's no longer used
	// for auto-interaction, see RtInteractionController::notifyCameraInteracting()'s
	// GPU branch) and
	// wrongly conclude nothing is running, restarting the settled session
	// redundantly alongside the interactive one that's already live.
	bool rayTracedSessionRunning() const
	{
		return effectiveRayTracingEnginePreference() == RtRayTracingEnginePreference::GPU
			? (_ptOptixSession.isRunning() || _rayTracedInteractiveActive)
			: _rtSession.isRunning();
	}

	// Raw linear HDR frame (un-tonemapped, optionally denoised) for fast EXR
	// export. In CPU mode this comes from _rtSession.latestFrame(); in settled
	// GPU mode from _ptOptixSession.latestFrame(); and while live interactive
	// GPU PT is active from RtInteractiveRenderer's latest completed device
	// frame, read back on demand through _rtInteractiveTracer. RtPresenter's
	// tonemap only happens at PRESENT time in the display shader, so none of
	// these paths mutate the underlying linear radiance buffer.
	std::vector<glm::vec3> rayTracingRawFrame(int& outWidth, int& outHeight) const
	{
		uint32_t sampleCount = 0;
		if (effectiveRayTracingEnginePreference() == RtRayTracingEnginePreference::GPU)
		{
			if (_rayTracedInteractiveActive)
			{
				RtCamera frameCamera;
				uint64_t generation = 0;
				if (void* deviceFrame = _rtInteractiveRenderer.pollCompletedFrame(outWidth, outHeight, frameCamera, generation))
				{
					std::vector<glm::vec3> hostFrame;
					std::vector<float> hostAlpha;
					if (_rtInteractiveTracer.readbackDeviceRGBABuffer(deviceFrame, outWidth, outHeight, hostFrame, hostAlpha))
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

	// Arms Ray Traced mode AND starts tracing immediately, rather than
	// waiting for the idle-settle countdown armRayTracedRenderingMode()
	// alone leaves running - RtRenderDialog's "Render" button wants the
	// press to visibly start work right away, not after a camera-idle delay
	// that may never arrive if the user isn't touching the viewport at all.
	void requestRayTracedRenderNow()
	{
		// See armRayTracedRenderingMode()'s doc comment for why false here -
		// startRayTracedSession() right below is about to start the real
		// settled session regardless of engine, tearing down anything the
		// interactive path just started.
		armRayTracedRenderingMode(/*startInteractiveSessionNow=*/false);
		startRayTracedSession();
	}

	// Renders and returns the current frame with the axis triad/view cube/
	// mesh-count HUD overlays suppressed - for RtRenderDialog's Export,
	// which wants exactly the composited raster+ray-traced pixels, not a
	// viewport screenshot. Triggers a real synchronous re-paint (via
	// QOpenGLWidget::grabFramebuffer(), which paintGL() then sees
	// _capturingCleanFrame set for) rather than reading back whatever was
	// last on screen, then restores normal HUD-visible display afterward.
	QImage captureCleanRayTracedImage()
	{
		_capturingCleanFrame = true;
		QImage img = grabFramebuffer();
		_capturingCleanFrame = false;
		update(); // restore the normal HUD-visible view
		return img;
	}

	// Current on-screen device-pixel resolution - same fbWidth/fbHeight
	// computation startRayTracedSession() uses. RtRenderDialog compares
	// its requested export resolution against this to decide whether a
	// downscale of the already-converged frame is enough (fast path) or a
	// fresh renderRayTracedOffline() call is needed (requested resolution
	// exceeds this in either dimension).
	void rayTracingViewportResolution(int& outWidth, int& outHeight) const
	{
		const qreal dpr = devicePixelRatioF();
		outWidth  = static_cast<int>(width()  * dpr);
		outHeight = static_cast<int>(height() * dpr);
	}

	// Live tonemap settings - same values passed to _rtPresenter.draw() for
	// on-screen display. RtRenderDialog's offline export path needs
	// these directly (see RtTonemap.h) since it never touches the GPU/
	// RtPresenter at all, unlike the fast path which just grabs the
	// already-tonemapped framebuffer.
	void rayTracingToneMapSettings(bool& outHdrToneMapping, bool& outGammaCorrection,
		float& outScreenGamma, float& outIblExposure, int& outToneMapMode) const
	{
		outHdrToneMapping  = _renderCtrl.hdrToneMapping();
		outGammaCorrection = _renderCtrl.gammaCorrection();
		outScreenGamma     = _renderCtrl.screenGamma();
		outIblExposure     = _renderCtrl.iblExposure();
		outToneMapMode     = static_cast<int>(_renderCtrl.toneMappingMode());
	}

	// Blocking offline ray-traced render at an arbitrary resolution,
	// decoupled entirely from the interactive session/viewport (see
	// buildRayTracedSnapshot()'s doc comment for the shared setup logic,
	// and RtRenderDialog::onExportClicked() for when this is used vs the
	// fast downscale-existing-frame path). Dispatches to CPU (RtEmbreeScene/
	// CpuPathTracer) or GPU (renderRayTracedOfflineGpu(), RtOptixSceneTracer)
	// based on _ptEnginePreference - same engine the interactive viewport is
	// currently using. Genuinely blocks the calling thread for the whole
	// render - no worker thread - per an explicit call that a blocking
	// offline export is acceptable; the caller is expected to pump
	// QApplication::processEvents() (WITHOUT ExcludeUserInputEvents - see
	// cancelRayTracedOfflineRender()'s doc comment for why) from onProgress
	// to keep the UI visually responsive and let a cancel request actually
	// reach this call. onProgress is called once per completed sample with
	// (currentSample, maxSamples). Returns false (outLinearRgb left
	// untouched) if the scene/camera isn't ready to render at all, or (GPU
	// only) if OptiX isn't available on this machine. outCancelled, if
	// non-null, is set true when the render stopped early because
	// cancelRayTracedOfflineRender() was called mid-render - a false
	// return with outCancelled set is not a failure and shouldn't be
	// reported as one.
	bool renderRayTracedOffline(int width, int height,
		const std::function<void(uint32_t currentSample, uint32_t maxSamples)>& onProgress,
		std::vector<glm::vec3>& outLinearRgb, bool* outCancelled = nullptr);

	// Requests that an in-progress renderRayTracedOffline() stop at the
	// next opportunity (next sample boundary on GPU, next scanline on CPU -
	// see CpuPathTracer::renderPass()'s own cancelFlag doc comment) rather
	// than running to completion. Safe to call from a Qt slot invoked via
	// QApplication::processEvents() while renderRayTracedOffline() is
	// still blocking the calling thread further up the same call stack -
	// this is the ONLY way a click can reach that call at all, since it
	// never returns to the event loop on its own until done or cancelled.
	// No-op if no offline render is currently in progress (the flag is
	// reset at the start of every renderRayTracedOffline() call, so a
	// stale request can't affect a later, unrelated one).
	void cancelRayTracedOfflineRender() { _ptOfflineCancelRequested.store(true, std::memory_order_release); }

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

	// Releases every GPU-context-bound resource's GL handles (via
	// _gpuResourceRegistry.releaseAll() - see IGpuContextResource.h) without
	// deleting the owning C++ objects, which survive for the next
	// initializeGL()'s restorePhase() calls to reuse. Shared between
	// ~ViewportWidget() (widget going away for good - see
	// deleteGpuOwnedObjects() for the object-deletion step that follows) and
	// the QOpenGLContext::aboutToBeDestroyed() handler wired up in
	// initializeGL() (widget survives, only its GL context is being replaced
	// - see that connection's own comment for why this is needed there too).
	void releaseGLSceneResources();

	// initializeGL()'s QOpenGLContext::aboutToBeDestroyed() connection (see
	// its own comment there) - must be disconnected at the very top of
	// ~ViewportWidget(), before that destructor's own explicit cleanup runs,
	// otherwise the base QOpenGLWidget destructor's later context teardown
	// fires this a second time and calls releaseGLSceneResources() again on
	// already-freed GL objects (e.g. SceneRenderController::releaseGpuResources()
	// double-destroying a QOpenGLBuffer).
	QMetaObject::Connection _glContextAboutToBeDestroyedConnection;

	void invalidateTextureCacheGpuResources();
	void releaseLoadedMeshGpuResources();
	void restoreLoadedMeshGpuResources();

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
	/// member-function pointer â€” it does not work with lambda connections in Qt6.
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
	// Emitted whenever the armed measurement tool changes - including from
	// inside this widget (Escape cancels it) - so the Measurement dialog's
	// combo box can stay in sync without it having to be the ONLY thing that
	// ever sets the tool.
	void measurementToolChanged(MeasurementTool tool);
	// Fires whenever the in-progress pick count changes for the active
	// tool (a click added an anchor, or the pending set was cleared/reset)
	// so the Measurement dialog can show "click the 2nd point" etc.
	// `picked`/`required` are both 0 when no tool is armed.
	void measurementProgressChanged(int picked, int required);
	// Fires whenever the selected measurement changes - including from
	// clicking one directly in the viewport (setSelectedMeasurementId()'s
	// other caller besides the Measurement dialog) - so the dialog's results
	// list can keep its highlighted row in sync either way.
	void measurementSelectionChanged(const QUuid& id);
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
	bool autoFitViewOnUpdate() const { return _viewCtrl.autoFitViewOnUpdate(); }
	void setSelectionHighlighting(bool highlight);
	bool isSelectionHighlighting() const { return _selectionHighlighting; }
	void performKeyboardNav();
	void disableLowRes();
	void disableSectionCapsInteractionSuppression() { setSectionCapsInteractionSuppressed(false); }
	void setFloorTexRepeatS(double floorTexRepeatS);
	void setFloorTexRepeatT(double floorTexRepeatT);
	void setFloorOffsetPercent(double value);
	// notifySceneContentMutated(): skyBoxFOV feeds the PT snapshot's
	// environment scalars - see setBgGradientStyle()'s identical reasoning.
	void setSkyBoxFOV(double fov) { _renderCtrl.setSkyBoxFOV(static_cast<float>(fov)); _rtInteractionCtrl->notifySceneContentMutated(); update(); }
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
	// notifySceneContentMutated(): envMapExposure feeds the PT snapshot's
	// environment scalars - see setBgGradientStyle()'s identical reasoning.
	// (The neighboring tonemap/gamma/iblExposure setters deliberately DON'T
	// restart: those are present-time uniforms RtPresenter::draw() reads
	// live every paint, so update() alone already shows them immediately.)
	void setEnvMapExposure(double exposure) { _renderCtrl.setEnvMapExposure(std::pow(2.0f, static_cast<float>(exposure))); _rtInteractionCtrl->notifySceneContentMutated(); update(); }
	void setIBLExposure(double exposure) { _renderCtrl.setIblExposure(std::pow(2.0f, static_cast<float>(exposure))); update(); }

	// Getters for tone mapping and gamma settings
	bool isHDRToneMappingEnabled() const { return _renderCtrl.hdrToneMapping(); }
	bool isGammaCorrectionEnabled() const { return _renderCtrl.gammaCorrection(); }
	HDRToneMapMode getHDRToneMappingMode() const { return _renderCtrl.toneMappingMode(); }
	void showLights(bool showLights);
	// notifyRayTracedSceneMutated() (NOT just notifyCameraInteracting()/
	// notifySceneContentMutated()): both feed buildRayTracedSnapshot()'s
	// light list fresh on every call, which is enough for CPU (RtRayTracingSession::
	// start() unconditionally rebuilds its Embree scene every restart), but
	// RtOptixRayTracingSession::start() only re-uploads its lights buffer
	// (inside buildScene()) when snapshot->revisionId actually changed - a
	// bare idle-timer restart with the SAME revision reuses the GPU's stale
	// lights buffer. Bumping the revision forces both engines to pick up the
	// new light set. Toggling either previously only triggered a raster
	// update(), leaving a ray-traced session showing a stale frame with the
	// old light set until some unrelated event (camera move, etc.) happened
	// to restart it.
	// refreshFallbackLight() re-evaluates the persistent PunctualLights
	// fallback entry against the new useDefaultLights() value (see its own
	// doc comment) - without this call, toggling the checkbox never
	// created/cleared that entry at all, only ever affecting
	// buildRayTracedSnapshot()'s separately-recomputed keyLight.
	void useDefaultLights(bool useDefaultLights) { _renderCtrl.setUseDefaultLights(useDefaultLights); refreshFallbackLight(); notifyRayTracedSceneMutated(); update(); }
	void usePunctualLights(bool usePunctualLights) { _renderCtrl.setUsePunctualLights(usePunctualLights); notifyRayTracedSceneMutated(); update(); }

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
	// Applies to every mesh in the scene â€” no selection required.
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

	// allowCacheReuse=true only from initializeGL()'s context-recreation
	// path - see the identical reasoning on loadIrradianceMap() below;
	// getEnvironmentMap(regenerate=true) (used to load a newly selected
	// skybox folder) must always re-upload regardless of context sharing.
	void loadEnvMap(bool allowCacheReuse = false);
	// allowCacheReuse=true only from initializeGL()'s context-recreation
	// path, where skipping regeneration when the IBL maps already survived
	// a shared context is correct. Every other call site (skybox/environment
	// changes, explicit regenerate=true requests from getIrradianceMap()/
	// getPrefilterMap()/getSheenPrefilterMap()) means "the environment
	// itself changed, or the caller explicitly wants fresh data" - the old
	// irradiance/prefilter maps having non-zero handles there doesn't mean
	// they're still correct, just that they haven't been freed yet, so
	// those callers must always regenerate regardless of context sharing.
	void loadIrradianceMap(bool allowCacheReuse = false);
	GLuint loadPresetEnvironmentMap(const QString& hdrFilePath);
	bool generatePresetIBLMaps(GLuint sourceCubemap, GLuint& outIrradianceMap, GLuint& outPrefilterMap, GLuint& outSheenPrefilterMap);
	void loadFloor();
	void ensureShadowMapResources();
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
	void drawAxis(Camera* camera, const QMatrix4x4* overrideViewMatrix = nullptr);
	void drawCornerAxis(CornerAxisPosition position, const QMatrix4x4* overrideRotationMatrix = nullptr);
	void drawTransformGizmo(Camera* camera);
	void drawViewCube(const QMatrix4x4* overrideRotationMatrix = nullptr);
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
	                               float& cubeScale,
	                               const QMatrix4x4* overrideRotationMatrix = nullptr) const;
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
	// mesh (â‰¤ 1024 samples per mesh for performance).  Using actual vertices
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

	void generateCubemapMipmaps(GLuint cubemapTexture);

	void setSectionCapsInteractionSuppressed(bool suppressed);
private:
	SceneRuntime _sceneRuntime;

	AnimationRuntimeController _animCtrl;

	ExplodedViewRuntimeController _explodedViewCtrl;

	// Render-pipeline resources â€” owned here; ViewportWidget aliases every field by
	// reference so all existing call sites in ViewportWidget.cpp remain unchanged.
	// Declaration order: _renderCtrl must come before all its aliases.
	SceneRenderController _renderCtrl;

	// Viewport interaction state â€” owned here; ViewportWidget aliases every field by
	// reference so all existing call sites in ViewportWidget.cpp remain unchanged.
	// Declaration order: _viewCtrl must come before all its aliases.
	ViewportInteractionController _viewCtrl;

	// Cached per-frame culling contexts â€” rebuilt in extractFrustumPlanes() /
	// rebuildClippingContext(). Avoids repeated look-ups inside tight render loops.
	VisibilityComputationHelper::FrustumContext  _frustumCtx;
	VisibilityComputationHelper::ClippingContext _clippingCtx;

	ViewToolbar* _viewToolbar;

	QSet<int> _keys;
	DisplayMode _displayMode;
	bool _realismEnabled = false;
	ShadingNormalMode _shadingNormalMode = ShadingNormalMode::SMOOTH;
	// _renderingMode, _bgTopColor, _bgBotColor, _gradientStyle â†’ SceneRenderController (Phase 12)
	int _modelNum;
	QImage _texImage, _texBuffer;
	// _floorTexRepeatS/T â†’ SceneRenderController (Phase 12)
	TextRenderer* _textRenderer;
	TextRenderer* _axisTextRenderer;
	QString _labelTop, _labelFront, _labelLeft, _labelIsometric, _labelDimetric, _labelTrimetric;
	QString _labelAxisX, _labelAxisY, _labelAxisZ;
	QString _modelName;

	bool _selectionHighlighting;

	QRubberBand* _rubberBand;
	QRubberBand* _selectRect;
	QTimer* _inertiaTimer        = nullptr;

	// ---- Ray-traced rendering mode -----------------------------------------
	// _rtSession/_rtPresenter own the actual background tracing/presentation;
	// this widget only arms/disarms them and feeds them a fresh RtSceneSnapshot
	// on settle - see armRayTracedRenderingMode()/onRayTracedIdleTimeout().
	RtRayTracingSession _rtSession;
	RtPresenter          _rtPresenter;
	QTimer*  _rayTracedIdleTimer    = nullptr; // reset on every camera-affecting event
	QTimer*  _rayTracedRefreshTimer = nullptr; // periodically repaints while a trace is running
	// Debounced GAS/IAS rebuild + interactive warm-up, armed by
	// RtInteractionController every time the interactive accumulator
	// gets torn down while ray tracing stays armed (a scene mutation, a scripted
	// view animation, a real resize, hiding/showing this widget's MDI
	// document, ...) - single-shot, restarted on every teardown so a burst of
	// rapid events (e.g. a slider being dragged in VisualizationEnvironmentPanel)
	// only pays the rebuild once, after the burst actually settles, rather
	// than once per tick. See onRayTracedResumeWarmUpTimeout()'s own doc
	// comment for the full rationale and kRayTracedResumeWarmUpDebounceMs in
	// ViewportWidget.cpp for the debounce interval.
	QTimer*  _rayTracedResumeWarmUpTimer = nullptr;
	// Owns the GPU interactive-PT settle/resume STATE MACHINE - see its own
	// class doc comment. Sole source of truth for "is ray tracing armed"
	// (isRayTracedRenderingModeArmed() now just forwards to armed()); the
	// old standalone _rayTracedArmed bool was removed so the two could never
	// drift apart. Constructed in the ctor body (not the initializer list)
	// once _rayTracedIdleTimer/_rayTracedResumeWarmUpTimer actually exist -
	// raw owning pointer, deleted in ~ViewportWidget(), since
	// RtInteractionController is a plain (non-QObject) class and so
	// can't be parented into Qt's object tree the way the two timers are.
	RtInteractionController* _rtInteractionCtrl = nullptr;
	uint64_t _rayTracedSceneRevision = 1;
	int      _rayTracedFramebufferWidth = 0;
	int      _rayTracedFramebufferHeight = 0;
	bool     _preservePtPresenterOnNextStart = false;
	RtCamera _rtInteractivePreviewCamera;
	bool     _rtInteractivePreviewCameraValid = false;

	// True while _rtInteractiveRenderer is the live GPU/OptiX continuous
	// accumulator (see RtInteractiveRenderer's class doc comment) - set by
	// startInteractiveRayTracedGpuSession(), which RtInteractionController
	// calls on every camera-affecting event for GPU. There is no more
	// "promote to a different, full-quality session on settle" - the same
	// accumulator just keeps converging/denoising in place once the camera
	// holds still (onRayTracedIdleTimeout() is a GPU no-op now). Cleared
	// false by RtInteractionController::notifySceneContentMutated()'s
	// hard-invalidate branch (a scene mutation, or switching to the
	// CPU/Embree engine) and by hideEvent()
	// (stopRtInteractiveRenderer() releases the renderer's resources but
	// doesn't clear this itself - see hideEvent()'s own doc comment for why
	// leaving it true there was a real bug, not just an oversight).
	// GPU/OptiX only - CPU/Embree never sets this.
	bool  _rayTracedInteractiveActive  = false;
	// Throttles startInteractiveRayTracedGpuSession()'s SLOW path only (a
	// real ensureSceneResources()+resize() with a rebuilt snapshot - the
	// first tick of a new interactive burst, or a mid-drag resolution
	// change) - the fast path (RtInteractiveRenderer::updateCamera(), used
	// on every other tick) is cheap enough to call unthrottled. See
	// kInteractiveGpuRestartMinIntervalMs in ViewportWidget.cpp.
	qint64 _lastInteractiveGpuRestartMs = 0;
	// Wall-clock timestamp (QDateTime::currentMSecsSinceEpoch()) of the last
	// genuine RtInteractionController::notifyCameraInteracting() call -
	// see onRayTracedIdleTimeout()'s doc comment for why this exists: Qt's
	// QTimer can be throttled/coalesced by the OS under heavy GUI-thread load
	// (dragging + GPU launches + presenter uploads is exactly that), so the
	// 450ms single-shot idle timer firing is not, by itself, reliable proof
	// that 450ms of genuine idleness actually passed - it can fire late AND,
	// under coalescing, effectively "early" relative to the last real
	// interaction once the event loop catches up. onRayTracedIdleTimeout()
	// cross-checks against this before treating a timeout as a real settle.
	qint64 _lastCameraInteractionMs = 0;

	// User-adjustable PT quality settings - see setRayTracingMaxSamples()/
	// setRayTracingMaxBounces()'s doc comments. Defaults match
	// RtRayTracingSession/CpuPathTracer::Settings's own defaults exactly, so
	// behavior is unchanged until a user actually opens RtRenderDialog and
	// changes them. _ptMaxSamples deliberately lower (16, not the old 128) -
	// a fast, responsive default that still lets the live viewport refine
	// quickly; the same spinBoxMaxSamples value also drives offline Export
	// (see RtRenderDialog::onExportClicked()), so users doing a final
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
	RtRayTracingEnginePreference _ptEnginePreference = RtRayTracingEnginePreference::Auto;
	RtOptixRayTracingSession _ptOptixSession; // GPU-backend counterpart to _rtSession above - see startOptixTestRayTracedSession()

	// Same-frame, GPU-resident, non-blocking-submission interactive PT
	// renderer (see its own doc comment) - this is the ONLY interactive GPU
	// PT path; _ptOptixSession above is used exclusively for the settled/
	// full-quality session now (its own former interactive half was retired
	// once this renderer was proven - see RtOptixRayTracingSession.h's doc
	// comment). Owns a SEPARATE RtOptixSceneTracer instance (its own
	// GAS/IAS/texture uploads, its own VRAM) rather than sharing
	// _ptOptixSession's internal tracer - RtOptixRayTracingSession only
	// exposes that as const (tracer(), a read-only diagnostics accessor),
	// and even if it exposed a mutable one, having two independent
	// revision-gated callers (_ptOptixSession's own workerLoop() and this
	// renderer) mutate the SAME scene/acceleration-structure state from
	// different threads/call patterns would be a real race - a second,
	// separate tracer avoids that entirely at the cost of ~2x GPU scene
	// memory while a settled session and an interactive one are both live.
	// _rtInteractiveTracer MUST be declared before _rtInteractiveRenderer
	// (member construction order, not initializer-list order) since the
	// renderer holds a reference to it.
	RtOptixSceneTracer   _rtInteractiveTracer;
	RtInteractiveRenderer _rtInteractiveRenderer{ _rtInteractiveTracer };
	// Retained snapshot from the last slow-path rebuild (see
	// startInteractiveRayTracedGpuSession()) - unlike RtOptixRayTracingSession
	// (which stores its own snapshot internally for workerLoop() to read),
	// RtInteractiveRenderer is driven entirely from paintGL() and takes a
	// snapshot only transiently (ensureSceneResources()), so ViewportWidget
	// itself must hold onto this to keep supplying tick()'s environment/
	// shadow-setting parameters on every paint without rebuilding.
	std::shared_ptr<const RtSceneSnapshot> _rtInteractiveRendererSnapshot;
	// Last RtInteractiveRenderer::pollCompletedFrame() generation this widget
	// consumed - paintGL()'s interactive-pull block skips re-uploading a frame
	// whose generation it has already seen.
	uint64_t _lastConsumedRtInteractiveRendererGeneration = 0;

	bool     _ptOrthoThinWallWarningActive = false; // see rayTracingOrthoThinWallWarningActive()'s doc comment
	QElapsedTimer _ptSessionElapsedTimer; // see rayTracingElapsedMs()'s doc comment

	// See cancelRayTracedOfflineRender()'s doc comment - reset to false at
	// the start of every renderRayTracedOffline() call, checked between
	// samples/chunks by that call and its GPU counterpart. Atomic even
	// though everything touching it currently runs on the same (UI) thread
	// - the whole point is that it's set from a Qt slot invoked via
	// QApplication::processEvents() while a call further up the SAME
	// thread's stack is still blocking, which is a real (if same-thread)
	// concurrent-access pattern worth being explicit and correct about.
	std::atomic<bool> _ptOfflineCancelRequested{ false };

	// Set for the duration of a captureCleanRayTracedImage() call -
	// paintGL() checks this to suppress the axis triad/view cube/mesh-count
	// HUD overlays (see their call sites) so an exported render contains
	// only the composited raster+ray-traced scene, matching what a render-
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
	// reduced-quality INTERACTIVE trace (startInteractiveRayTracedGpuSession())
	// live and tracking the camera instead of tearing down to raster.
	//
	// The per-frame PROGRAMMATIC animation callbacks - animateViewChange()/
	// animateFitAll()/animateWindowZoom() (Home/standard-view/axonometric/
	// fit/window-zoom transitions) - deliberately pass false instead, even
	// though they're also genuine camera movement: unlike a live drag or an
	// inertia coast, their per-tick delta (a slerp/interpolation step)
	// does NOT decay toward zero as the animation ends, so the interactive
	// trace's inherent one-tick-behind lag (see RtInteractiveRenderer's
	// design notes) surfaces as a full-sized, objectionable jerk right at
	// the finish instead of the imperceptible one inertia's decay masks -
	// plain raster/PBR for the whole animation, settling into full-quality
	// PT once it's actually done, was judged smoother in practice. CPU/Embree
	// ignores this flag entirely regardless of any of the above and always
	// takes the original path, since it has no hardware RT acceleration to
	// make a per-frame interactive trace realistic. See
	// armRayTracedRenderingMode()'s doc comment for the user-visible summary.
	void onRayTracedIdleTimeout();
	void onRayTracedRefreshTimer();
	void startRayTracedSession();
	// GPU/OptiX-only reduced-quality trace kicked off while the camera is
	// actively moving - see RtInteractionController::notifyCameraInteracting()'s
	// doc comment. The
	// FIRST call of a new interactive burst does a real (throttled, see
	// _lastInteractiveGpuRestartMs) RtInteractiveRenderer::ensureSceneResources()
	// +resize(), reusing that class's own revision-gated GAS/IAS rebuild-skip
	// (camera-only movement leaves _rayTracedSceneRevision unchanged); every
	// subsequent call while that renderer is already at the right resolution
	// instead takes a much cheaper path - RtInteractiveRenderer::
	// updateCamera(), which needs neither a rebuilt scene snapshot nor any
	// GPU work of its own - so it's cheap enough to call unthrottled on every
	// mouse-move event. See RtInteractiveRenderer::updateCamera()'s doc
	// comment for why that distinction matters (buildRayTracedSnapshot()'s
	// synchronous environment-cubemap GPU readback, not the trace itself, was
	// the real per-tick cost a naive always-restart approach used to pay).
	// forceSceneRefresh=true is the animation path: if an interactive PT
	// session is ALREADY live at the current resolution, rebuilds (or queues,
	// if a launch is still in flight) a NEW scene snapshot against that SAME
	// renderer instance instead of tearing it down and starting over.
	void startInteractiveRayTracedGpuSession(bool forceSceneRefresh = false);

	// Called unconditionally from the END of startInteractiveRayTracedGpuSession()'s
	// slow path - i.e. every time that path runs, regardless of which caller
	// triggered it (armRayTracedRenderingMode(), the debounced
	// onRayTracedResumeWarmUpTimeout(), or a genuine mouse-move/wheel event
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
	// startInteractiveRayTracedGpuSession() didn't actually start anything
	// (e.g. OptiX unavailable).
	void warmUpInteractiveRayTracedGpuSession();

	// Tears down whichever GPU PT session(s) are currently active/converging
	// - shared by RtInteractionController's Recovering-entry teardown,
	// hideEvent(), and disarmRayTracedRenderingMode(), which previously each
	// reimplemented this teardown slightly differently (see hideEvent()'s own
	// doc comment for the bug that divergence caused). Does NOT touch
	// _rayTracedArmed, _rayTracedIdleTimer, or _rayTracedResumeWarmUpTimer
	// - callers decide those independently based on their own context.
	void teardownActiveRayTracedSessions();

	// _rayTracedResumeWarmUpTimer's single-shot timeout - fires once a burst
	// of teardowns goes quiet
	// for kRayTracedResumeWarmUpDebounceMs. Re-enters
	// startInteractiveRayTracedGpuSession()'s slow path (rebuilding whatever
	// the teardown invalidated) plus warmUpInteractiveRayTracedGpuSession(),
	// but ONLY when nothing is already running (!rayTracedSessionRunning())
	// - if the user resumed dragging before this timer fired, the normal
	// mouse-move path already restarted the interactive session itself (fast
	// or slow path as appropriate), and re-triggering here would be redundant
	// at best, a wasted duplicate GAS rebuild at worst. Also bails if path
	// tracing was disarmed or switched to CPU/Embree in the meantime. Safe to
	// fire while this widget is hidden (see hideEvent()'s call site) - the
	// warm-up is pure CUDA/OptiX work with no GL-context/visibility
	// dependency.
	void onRayTracedResumeWarmUpTimeout();

	// Releases _rtInteractiveRenderer's GPU resources (stream, per-slot
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
	void stopRtInteractiveRenderer();

	// Phase 2a GPU-engine path - see RtOptixSceneParams.h's doc comment for
	// exactly what this does and doesn't render yet (real geometry/
	// instancing/camera, flat-normal-as-color shading, no materials/lights/
	// bounces). Rebuilds the GPU acceleration structure from the current
	// snapshot (see buildRayTracedSnapshot()) and renders it through the
	// real camera at the current framebuffer size, uploading the result
	// through the same _rtPresenter the CPU path uses, so switching the
	// Render Engine dropdown is directly comparable in the same viewport.
	// Called from startRayTracedSession() instead of the real _rtSession
	// setup when the GPU engine is selected.
	void startOptixTestRayTracedSession(int fbWidth, int fbHeight);

	// GPU-engine counterpart to renderRayTracedOffline()'s CPU path -
	// same blocking-until-done, own-fresh-scene-independent-of-the-live-
	// session contract, just built on RtOptixSceneTracer instead of
	// RtEmbreeScene/CpuPathTracer. Mirrors RtOptixRayTracingSession::
	// workerLoop()'s exact chunked running-mean accumulation + final-pass-
	// only denoise (see that function's own doc comment for the numerics
	// rationale), just as a plain synchronous loop in the calling thread
	// rather than a background worker - offline export already blocks the
	// whole application per renderRayTracedOffline()'s own contract, so
	// there is no reason to pay for a worker thread + polling wait here.
	bool renderRayTracedOfflineGpu(int width, int height, const RtSceneSnapshot& snapshot,
		const std::function<void(uint32_t currentSample, uint32_t maxSamples)>& onProgress,
		std::vector<glm::vec3>& outLinearRgb, bool* outCancelled);

	// Builds a fresh RtSceneSnapshot for the given OUTPUT resolution -
	// shared by startRayTracedSession() (the interactive session) and
	// renderRayTracedOffline() (blocking export at an arbitrary
	// resolution), so the light/environment/floor snapshot-building logic
	// isn't duplicated between them. aspectRatio is recomputed from
	// width/height rather than reusing the camera's own configured aspect,
	// so an offline export at a different aspect ratio than the live
	// viewport frames correctly instead of stretching the same framing.
	// Also updates _ptOrthoThinWallWarningActive as a side effect (see its
	// doc comment) - both callers want this detection to run.
	std::shared_ptr<const RtSceneSnapshot> buildRayTracedSnapshot(int width, int height,
		const RtEnvironment* reusedEnvironment = nullptr);

	// Live (not cached) camera-vs-floor-plane test, computed the same way
	// RtSceneBuilder::build() decides whether to include the floor at all
	// (camera's up-axis coordinate vs _floorPlaneZ). Used to compare against
	// _rtLastBuildCameraAboveFloor on every camera-only fast-path update, to
	// detect when the camera has actually crossed the floor's plane and a
	// real rebuild is needed to hide/show it - see that member's own doc
	// comment for the full rationale. Returns true (never hide) if there's
	// no primary camera yet.
	bool isCameraAboveFloorPlane() const
	{
		if (!_primaryCamera)
			return true;
		const float cameraUpCoord = _viewCtrl.cameraUpAxisZUp()
			? _primaryCamera->getRenderPosition().z()
			: _primaryCamera->getRenderPosition().y();
		return cameraUpCoord >= _floorPlaneZ;
	}

	// Selection manager instance (owns all selection logic and state)
	SelectionManager* _selectionManager = nullptr;

	// _defaultLightColor â†’ SceneRenderController (Phase 12)
	QVector4D _ambientLight;
	QVector4D _diffuseLight;
	QVector4D _specularLight;

	QVector3D _lightPosition;
	// _lightOffsetX/Y/Z â†’ SceneRenderController._lightOffset (Phase 12)

	QMatrix4x4 _lightSpaceMatrix;



	QImage					 _floorTexImage;
	float                    _floorSize;
	float 					 _floorSizeFactor;
	// _floorOffsetPercent â†’ SceneRenderController (Phase 12)
	float                    _floorPlaneZ;
	QVector3D                _floorCenter;

	// Whether the camera was above (vs below) the floor's plane the last
	// time buildRayTracedSnapshot() actually ran RtSceneBuilder::build() -
	// see isCameraAboveFloorPlane()'s own doc comment for why this exists:
	// RtSceneBuilder::build() decides whether to include the PT floor at
	// all based on camera position, but ordinary camera-only interaction
	// (startInteractiveRayTracedGpuSession()'s fast path) deliberately
	// never calls build() again, reusing the existing snapshot/GAS - so
	// without this, orbiting the camera below the floor mid-drag would
	// never actually hide it (build() ran once, before the crossing,
	// and never runs again for a pure camera move). Updated every time
	// buildRayTracedSnapshot() runs; compared against the LIVE camera
	// position on every fast-path camera update to detect a genuine
	// crossing and force a real rebuild only then.
	bool _rtLastBuildCameraAboveFloor = true;


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
	CubeRenderable* _skyBox;
	// _fsTriVAO/VBO, _skyBoxFaces, _skyBoxFOV/_skyBoxZRotation, gamma/HDR/tone-map settings,

	ConeRenderable* _axisCone;
	ViewCubeMesh* _viewCube = nullptr;
	TransformGizmo* _transformGizmo = nullptr;

	// ---- Measurement tool state -----------------------------------------
	MeasurementTool _measurementTool = MeasurementTool::None;
	// Distance measurement's first click, waiting on the second.
	QVector<MeasurementAnchorRef> _pendingMeasurementAnchors;
	// Live hover preview (updated in mouseMoveEvent while a tool is armed) -
	// the specific point a click would place, including vertex snap, so the
	// user sees exactly where they're about to click instead of an
	// ambiguous whole-mesh highlight (see setMeasurementTool()'s
	// hover-highlight-mode save/restore for why the normal one is suppressed).
	MeshSurfaceAnchor _measurementHoverAnchor;
	// Same idea as _measurementHoverAnchor above, but for the Edge Radius
	// tool - previews the nearest circular edge (as a full circle outline,
	// see drawMeasurementOverlay()) rather than a single point, since the
	// whole edge IS the pick target for this tool.
	MeshEdgeCircleAnchor _measurementEdgeHoverAnchor;
	// Which of the two pick functions that both populate
	// _measurementEdgeHoverAnchor produced the current value -
	// pickCircularEdgeCenterAnchor() (true, a POINT preview at the
	// resolved center) vs pickStraightEdgeAnchor()/pickEdgeCircleAnchor()
	// (false, an EDGE preview - chord or full circle). Needed because the
	// same edgeIndex-bearing anchor type is reused for both, and a
	// circular edge's own segments resolve to a real (but wrong-for-this-
	// preview) chord, so drawMeasurementOverlay() can't tell them apart
	// from the anchor's contents alone.
	bool _measurementEdgeHoverIsCenterPick = false;
	HoverHighlightMode _savedHoverHighlightModeBeforeMeasurement = HoverHighlightMode::RaycastOnly;
	// Press-vs-drag disambiguation: a plain left-press while a tool is armed
	// only arms a pending click, which mouseReleaseEvent commits (as
	// handleMeasurementClick()) if the mouse hasn't moved past a small pixel
	// threshold since - otherwise the gesture was a drag (rotate/pan/sweep),
	// not a click, and gets ignored. Committing immediately on press instead
	// (the first cut) placed a spurious point every time the user started an
	// unrelated drag gesture.
	bool _measurementClickCandidate = false;
	QPoint _measurementClickPressPos;
	// Selected measurement (independent of tool-armed state and of mesh
	// selection) - see selectedMeasurementId()'s doc comment.
	QUuid _selectedMeasurementId;
	// Hovered-but-not-yet-selected measurement (no tool armed, mouse not
	// pressed) - a lighter preview than the selection highlight, so the
	// user can see what a click will select/delete before committing to it.
	// Updated in mouseMoveEvent(), drawn in drawMeasurementOverlay().
	QUuid _hoveredMeasurementId;

	// ---- Dimension-line drag state ---------------------------------------
	// Press-vs-drag disambiguation, same idea as _measurementClickCandidate
	// above, but for grabbing an already-PLACED measurement's dimension line
	// or angle arc (no tool armed) and repositioning it, instead of placing
	// a new point.
	bool _dimensionDragCandidate = false;
	QPoint _dimensionDragStartPixel;
	QUuid _dimensionDragCandidateId;
	DimensionDragKind _dimensionDragKind = DimensionDragKind::None;
	// True once the candidate press has moved past the click threshold and a
	// real drag is underway - distinct from the candidate flag above so
	// mouseMoveEvent() can tell "might become a drag" from "is dragging".
	bool _dimensionDragActive = false;
	// Fixed world-space pivot both drag kinds share (segment midpoint for
	// Linear, angle vertex for AngleRadius).
	QVector3D _dimensionDragPivot;
	// Linear: the dimension-line's own direction (a-to-b), i.e. the NORMAL
	// of the plane the drag freely repositions the offset within - this is
	// what makes the drag "pivot AND extend" rather than slide along one
	// fixed axis. AngleRadius: the bisector direction the 1D radius drag
	// measures magnitude along (screen-space-ratio technique, same as
	// updateTransformGizmoTranslationDrag() - no plane/pivot freedom needed
	// since an angle's plane is already fixed).
	QVector3D _dimensionDragAxis;
	// AngleRadius only: world-per-screen-pixel reference length for the
	// ratio-based 1D drag (see _dimensionDragAxis's doc comment) - unused
	// for Linear, which uses a true ray/plane intersection instead.
	float _dimensionDragRefLength = 1.0f;
	// Starting values at drag-begin, for the undo command pushed at drag-end.
	QVector3D _dimensionDragStartOffsetVector;  // Linear
	float _dimensionDragStartOffsetScalar = 0.0f;  // AngleRadius

	// Hover preview for the drag interaction above (mouse not pressed) -
	// lets the user see exactly what a click-drag would grab before
	// committing to it, same "preview before you act" idea as
	// _hoveredMeasurementId. Updated in mouseMoveEvent(), drawn in
	// drawMeasurementOverlay() as a color highlight on the specific
	// dimension line/arc (not the whole measurement).
	QUuid _hoveredDimensionId;
	DimensionDragKind _hoveredDimensionKind = DimensionDragKind::None;

	CubeRenderable* _lightCube;
	SphereRenderable* _lightSphere;
	// _showLights â†’ SceneRenderController (Phase 12)

	// GPU-context-recreation-survival registry (see IGpuContextResource.h) -
	// _transformGizmo/&_renderCtrl implement the interface directly and are
	// registered from this widget's constructor; the RenderableMesh-derived
	// decoration objects above register via a RenderableMeshGpuResourceAdapter
	// (owned by _gpuResourceAdapters) the first time each is constructed, in
	// their own existing initializeGL() call sites.
	GpuResourceRegistry _gpuResourceRegistry;
	std::vector<std::unique_ptr<IGpuContextResource>> _gpuResourceAdapters;
	// Guards the one-time LambdaGpuResource registration for the
	// transmission/SSS buffer pairs - see their call site in initializeGL().
	bool _bufferGpuResourcesRegistered = false;
	void deleteGpuOwnedObjects();
	// Wraps mesh's inherited releaseContextBoundGpuResources()/
	// restoreContextBoundGpuResources(shader) pair and registers the
	// adapter into GpuResourcePhase::Decorations - call exactly once, the
	// first time each decoration object is constructed.
	void registerDecorationGpuResource(RenderableMesh* mesh, std::function<QOpenGLShaderProgram*()> shaderResolver);

	ModelViewer* _viewer;


	unsigned long long _displayedObjectsMemSize;

	AssImpModelLoader* _assimpModelLoader;
	KTX2Loader _ktx2Loader;
	GPUCapabilities _gpuCapabilities;


	// _hatch* fields â†’ SceneRenderController (Phase 12)

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
	// of the file carries the same non-identity transformation â€” i.e. the
	// user applied a model-level transform.  Lights and glTF cameras of that
	// file follow this exact matrix; the visible-scene bounding sphere plays
	// no role, so hide/show/delete of other models cannot disturb them.
	void updateOverlayEditorTheme();

	void applyGltfCameraEntryTransform(const GltfCameraEntry& cam);

	void handleMeasurementClick(const QPoint& clickPoint);
	void drawMeasurementOverlay(Camera* camera);
	// Screen-space hit test against every saved measurement's marker(s) -
	// for Point, distance to the single anchor; for Distance, distance to
	// the line segment between its two anchors (so clicking anywhere along
	// the dimension line selects it, not just its endpoints). Returns a
	// null QUuid if nothing is within pixelRadius of pixel.
	QUuid hitTestMeasurement(const QPoint& pixel, Camera* camera, int pixelRadius) const;

	// Begins a dimension-drag session (either kind) once
	// _dimensionDragCandidate's press has moved past the click threshold -
	// resolves and FIXES the pivot/axis/starting-value for the rest of the
	// drag (recomputed once here, not every move, so they don't wander even
	// though their screen projection naturally changes as the mouse moves).
	void beginDimensionLineDrag(const QUuid& measurementId, DimensionDragKind kind, Camera* camera);
	// Called from mouseMoveEvent() while dragging - dispatches on
	// _dimensionDragKind:
	//  - Linear: a true ray/camera-through-mouse-pixel intersection against
	//    the plane (pivot, normal=_dimensionDragAxis) - the resulting point
	//    minus the pivot IS the new offset vector directly, so this
	//    naturally captures both direction ("pivot") and magnitude
	//    ("extend") from wherever the mouse actually points, in one step.
	//  - AngleRadius: the same screen-space-projection ratio technique as
	//    TransformGizmo's single-axis translate drag (see
	//    updateTransformGizmoTranslationDrag()) - extension only, along the
	//    fixed bisector axis, no pivot freedom (the angle's plane is
	//    already fixed by the two face normals).
	// Either way, live-writes the result via SceneGraph::
	// setMeasurementOffsetVector()/setMeasurementOffsetDistance() for
	// immediate visual feedback.
	void updateDimensionLineDrag(const QPoint& pixel, Camera* camera);
	// Ends the drag - pushes one MeasurementOffsetCommand capturing the
	// offset from before the drag to its final value (only if it actually
	// changed), mirroring TransformCommand's "one command on release" pattern.
	void finishDimensionLineDrag();
};
