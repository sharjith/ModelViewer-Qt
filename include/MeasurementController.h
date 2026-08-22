#pragma once

#include "IGpuContextResource.h"
#include "MeasurementData.h"
#include "MeshSurfaceAnchor.h"
#include "MeshEdgeCircleAnchor.h"
#include "SelectionManager.h"  // HoverHighlightMode - used as a value field type below

#include <QObject>
#include <QOpenGLFunctions_4_5_Core>
#include <QPoint>
#include <QSet>
#include <QSize>
#include <QUuid>
#include <QVector>
#include <QVector2D>
#include <QVector3D>

class Camera;
class ModelViewer;
class SceneMesh;
class SceneRenderController;
class SceneRuntime;
class TextRenderer;
class ViewportWidget;

// ---------------------------------------------------------------------------
// MeasurementController
//
// Owns the entire "Measurement" toolset (15 tools) that used to live directly
// on ViewportWidget - picking dispatch, per-type geometry resolvers,
// rendering, hit-testing, dimension-line dragging, and creation/selection
// state. Mirrors the delegation pattern TransformGizmo already established in
// this codebase (ViewportWidget owns an instance, calls into it via plain
// method calls, no signal/slot wiring for the actual interaction), but - via
// real, independent dependencies (SceneRuntime, ModelViewer, not a
// ViewportWidget back-reference) - achieves true ownership rather than a
// "GLWidget alias" half-measure.
//
// Unlike TransformGizmo (whose own drag-and-apply logic stays on
// ViewportWidget because it needs complex per-axis SceneMesh-transform undo
// snapshots), Measurement's undo integration only ever mutates a Measurement
// struct's own fields or pushes/pops it from SceneGraph's own vector via 4
// small, already-decoupled Command classes - simple enough that this
// controller owns the FULL interaction loop, including creation and drag.
//
// Is a QObject (unlike TransformGizmo) purely because several of the moved
// methods emit signals mid-body (not just at simple setter boundaries) -
// ViewportWidget relays these to its own identically-named signals via
// connect(), the same pattern already used for SelectionManager's
// selectionChanged. measurementStateChanged() is a repaint-trigger signal
// with no ViewportWidget equivalent - callers connect it directly to update().
// ---------------------------------------------------------------------------
class MeasurementController : public QObject, protected QOpenGLFunctions_4_5_Core, public IGpuContextResource
{
	Q_OBJECT

public:
	// axisTextRenderer is deliberately NOT a constructor dependency, unlike
	// sceneRuntime/viewer/renderCtrl - ViewportWidget::initializeGL() can run
	// more than once (context recreation) and re-`new`s its TextRenderer
	// each time, so the pointer itself isn't stable for this controller's
	// whole lifetime the way the other three are. Passed per-call to
	// drawMeasurementOverlay() instead, same as Camera*.
	MeasurementController(SceneRuntime& sceneRuntime, ModelViewer* viewer,
		SceneRenderController& renderCtrl, QObject* parent = nullptr);

	// Owns no GL objects of its own (only calls into SceneRenderController's
	// already-existing measurement-overlay VAO/VBO/shader via _renderCtrl),
	// so the only thing that needs redoing after context recreation is
	// re-resolving this class's own QOpenGLFunctions_4_5_Core function
	// pointers - see GpuResourceRegistry/IGpuContextResource.h. Same
	// registration pattern as TransformGizmo (which fixed a real bug where
	// it never came back after context loss before being registered).
	void releaseGpuResources() override;
	void restoreGpuResources() override;

	// Which kind of draggable dimension geometry a hit corresponds to - the
	// two kinds need different drag math (see updateDimensionLineDrag()'s
	// doc comment), so callers need to know which one they grabbed.
	enum class DimensionDragKind { None, Linear, AngleRadius };
	struct DimensionHit { QUuid measurementId; DimensionDragKind kind = DimensionDragKind::None; };

	// ---- Tool state ------------------------------------------------------
	// "Measure Point"/"Measure Distance": while a tool is active, left-clicks
	// place measurement points (via SelectionManager::pickSurfaceAnchor())
	// instead of doing normal selection - see handleMeasurementClick() in
	// ViewportWidget::mousePressEvent(). v1 scope: static meshes, MVF-only
	// (see MeasurementData.h).
	void setMeasurementTool(MeasurementTool tool, SelectionManager* selectionManager);
	MeasurementTool measurementTool() const { return _measurementTool; }

	// Completes a variable-anchor-count measurement (PitchCircle/EdgeChain -
	// see measurementToolHasVariableAnchorCount()) with whatever's been
	// picked so far, instead of waiting for a fixed count to auto-complete
	// it. No-op if the armed tool isn't variable-length, or if fewer than
	// measurementToolRequiredAnchorCount() (the MINIMUM for such a tool)
	// anchors are pending yet. Called from the Measurement dialog's Finish
	// button and from keyPressEvent()'s Enter/Return handling.
	void finishVariableLengthMeasurement(Camera* camera);

	// ---- Resolvers (all const, re-derive from CURRENT geometry - see each
	//      one's original doc comment, unchanged from before extraction) ---
	QVector3D resolveMeasurementAnchor(const MeasurementAnchorRef& ref) const;
	bool resolveMeasurementEdgeCircle(const MeasurementAnchorRef& ref,
		QVector3D& outCenter, QVector3D& outAxis, float& outRadius) const;
	bool resolveMeasurementEdgeGeometry(const MeasurementAnchorRef& ref,
		QVector3D& outStart, QVector3D& outEnd, float& outLength) const;
	bool resolveMeasurementEdgePolyline(const MeasurementAnchorRef& ref,
		QVector<QVector3D>& outPoints) const;
	bool measurementChainEdgeConnects(const QVector<MeasurementAnchorRef>& chainSoFar,
		const MeasurementAnchorRef& candidate) const;
	bool resolveMeasurementAnchorPlane(const MeasurementAnchorRef& ref,
		QVector3D& outPosition, QVector3D& outNormal) const;
	bool resolveMeasurementFaceArea(const MeasurementAnchorRef& ref,
		QVector<int>& outTriangleIndices, float& outArea, QVector3D& outCentroid) const;
	bool resolveMeasurementFaceRegion(const MeasurementAnchorRef& ref,
		QVector<int>& outTriangleIndices) const;
	bool resolveMeasurementMinDistance(const Measurement& m,
		QVector3D& outPointA, QVector3D& outPointB, float& outDistance) const;
	bool resolveMeasurementCylindricalDiameter(const MeasurementAnchorRef& ref,
		float& outDiameter, QVector3D& outAxisOrigin, QVector3D& outAxisDir,
		QVector3D& outPickedPoint, bool& outIsCone) const;
	bool resolveMeasurementDimensionSegment(const Measurement& m, QVector3D& outA, QVector3D& outB) const;
	QVector3D dimensionLinePerp(const QVector3D& a, const QVector3D& b,
		const QVector3D& referenceDir, Camera* camera) const;
	float defaultDimensionOffsetMagnitude(Camera* camera) const;
	QVector3D resolveDimensionOffsetVector(const QVector3D& a, const QVector3D& b,
		const Measurement& m, Camera* camera) const;
	bool resolveMeasurementAngleGeometry(const Measurement& m, Camera* camera, QVector3D& outVertex,
		QVector3D& outU, QVector3D& outV, float& outAngleRad, float& outRadius) const;

	// ---- Picking / interaction --------------------------------------------
	void handleMeasurementClick(const QPoint& clickPoint, SelectionManager* selectionManager, Camera* camera);
	QUuid hitTestMeasurement(const QPoint& pixel, Camera* camera, const QSize& viewportSize, int pixelRadius) const;
	DimensionHit hitTestDimensionLine(const QPoint& pixel, Camera* camera, const QSize& viewportSize, int pixelRadius = 8) const;
	void beginDimensionLineDrag(const QUuid& measurementId, DimensionDragKind kind, Camera* camera);
	void updateDimensionLineDrag(const QPoint& pixel, Camera* camera, const QSize& viewportSize);
	// viewportWidget is passed through only to construct MeasurementOffsetCommand/
	// MeasurementOffsetVectorCommand (which need it purely for the shared
	// ModelViewerCommand base's undo/redo repaint trigger, unrelated to
	// measurement state) - not stored, the one unavoidable exception to this
	// controller otherwise depending on no ViewportWidget back-reference.
	void finishDimensionLineDrag(ViewportWidget* viewportWidget);

	void drawMeasurementOverlay(Camera* camera, const QSize& viewportSize, TextRenderer* axisTextRenderer);

	// ---- Hover preview (called from ViewportWidget::mouseMoveEvent) ------
	// Live hover preview while a pick tool is armed - the specific point/
	// edge a click would land on, including vertex snap, so the user sees
	// exactly what they're about to pick.
	void updateHoverAnchor(const QPoint& pixel, SelectionManager* selectionManager);
	// Hover preview for an already-placed measurement (no tool armed) - both
	// its whole-measurement highlight and its draggable dimension line/arc
	// specifically, each a lighter preview than the corresponding selection/
	// drag-active state.
	void updateHoverMeasurement(const QPoint& pixel, Camera* camera, const QSize& viewportSize);

	// ---- Selection ---------------------------------------------------------
	// Currently selected measurement(s) in the viewport (independent of mesh
	// selection) - see the original ViewportWidget::selectedMeasurementIds()
	// doc comment (unchanged) for the full picture.
	const QSet<QUuid>& selectedMeasurementIds() const { return _selectedMeasurementIds; }
	bool hasSelectedMeasurements() const { return !_selectedMeasurementIds.isEmpty(); }
	void setSelectedMeasurementIds(const QSet<QUuid>& ids);
	QUuid hoveredMeasurementId() const { return _hoveredMeasurementId; }

	// Human-readable result string for one measurement, e.g. "Distance:
	// 12.345" - shared by the in-viewport label and the Measurement dialog's
	// results list so both agree.
	QString measurementSummaryText(const Measurement& m) const;

signals:
	// Relayed 1:1 by ViewportWidget to its own identically-named signals -
	// see this class's doc comment.
	void measurementToolChanged(MeasurementTool tool);
	void measurementProgressChanged(int picked, int required);
	void measurementSelectionChanged(const QSet<QUuid>& ids);
	// Pure repaint trigger (no ViewportWidget equivalent) - connect directly
	// to ViewportWidget::update().
	void measurementStateChanged();

private:
	void finalizePendingMeasurement(Camera* camera);

	SceneMesh* getMeshByUuid(const QUuid& uuid) const;

	bool _glFunctionsInitialized = false;

	SceneRuntime& _sceneRuntime;
	ModelViewer* _viewer;
	SceneRenderController& _renderCtrl;

	// ---- Tool-armed state --------------------------------------------------
	MeasurementTool _measurementTool = MeasurementTool::None;
	QVector<MeasurementAnchorRef> _pendingMeasurementAnchors;
	MeshSurfaceAnchor _measurementHoverAnchor;
	MeshEdgeCircleAnchor _measurementEdgeHoverAnchor;
	bool _measurementEdgeHoverIsCenterPick = false;
	HoverHighlightMode _savedHoverHighlightModeBeforeMeasurement = HoverHighlightMode::RaycastOnly;
	// Press-vs-drag disambiguation for PLACING a new point (mouseReleaseEvent
	// commits it as handleMeasurementClick() if the mouse hasn't moved past a
	// small pixel threshold since the press - otherwise the gesture was a
	// camera drag, not a click). Read/written from ViewportWidget's mouse
	// event handlers via the accessors below.
	bool _measurementClickCandidate = false;
	QPoint _measurementClickPressPos;

	// ---- Selection / hover state --------------------------------------------
	QSet<QUuid> _selectedMeasurementIds;
	QUuid _hoveredMeasurementId;

	// ---- Dimension-line drag state -----------------------------------------
	bool _dimensionDragCandidate = false;
	QPoint _dimensionDragStartPixel;
	QUuid _dimensionDragCandidateId;
	DimensionDragKind _dimensionDragKind = DimensionDragKind::None;
	bool _dimensionDragActive = false;
	QVector3D _dimensionDragPivot;
	QVector3D _dimensionDragAxis;
	float _dimensionDragRefLength = 1.0f;
	QVector3D _dimensionDragStartOffsetVector;  // Linear
	float _dimensionDragStartOffsetScalar = 0.0f;  // AngleRadius
	QUuid _hoveredDimensionId;
	DimensionDragKind _hoveredDimensionKind = DimensionDragKind::None;

public:
	// Exposed for ViewportWidget's mouse-event dispatch (press-vs-drag
	// disambiguation lives there, alongside the general-purpose equivalents
	// for camera rotate/pan/sweep gestures it already has to arbitrate
	// between).
	bool dimensionDragCandidate() const { return _dimensionDragCandidate; }
	void setDimensionDragCandidate(bool candidate) { _dimensionDragCandidate = candidate; }
	QPoint dimensionDragStartPixel() const { return _dimensionDragStartPixel; }
	void setDimensionDragStartPixel(const QPoint& pixel) { _dimensionDragStartPixel = pixel; }
	QUuid dimensionDragCandidateId() const { return _dimensionDragCandidateId; }
	void setDimensionDragCandidateId(const QUuid& id) { _dimensionDragCandidateId = id; }
	DimensionDragKind dimensionDragCandidateKind() const { return _dimensionDragKind; }
	void setDimensionDragCandidateKind(DimensionDragKind kind) { _dimensionDragKind = kind; }
	bool dimensionDragActive() const { return _dimensionDragActive; }
	QUuid hoveredDimensionId() const { return _hoveredDimensionId; }
	DimensionDragKind hoveredDimensionKind() const { return _hoveredDimensionKind; }

	bool measurementClickCandidate() const { return _measurementClickCandidate; }
	void setMeasurementClickCandidate(bool candidate) { _measurementClickCandidate = candidate; }
	QPoint measurementClickPressPos() const { return _measurementClickPressPos; }
	void setMeasurementClickPressPos(const QPoint& pixel) { _measurementClickPressPos = pixel; }
};
