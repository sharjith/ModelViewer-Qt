#pragma once

#include "IGpuContextResource.h"
#include "AnnotationData.h"
#include "MeshSurfaceAnchor.h"
#include "SelectionManager.h"  // HoverHighlightMode - used as a value field type below

#include <QObject>
#include <QOpenGLFunctions_4_5_Core>
#include <QPoint>
#include <QRectF>
#include <QSet>
#include <QSize>
#include <QString>
#include <QUuid>
#include <QVector3D>

class Camera;
class ModelViewer;
class SceneMesh;
class SceneRenderController;
class SceneRuntime;
class TextRenderer;
class ViewportWidget;

// ---------------------------------------------------------------------------
// AnnotationController
//
// Owns the "Annotate" tool: free-text notes with a leader line, anchored to
// a point on a mesh's surface - picking, placement, rendering, hit-testing,
// leader-line dragging, and selection state. Mirrors MeasurementController's
// delegation pattern (ViewportWidget owns an instance, calls into it via
// plain method calls) and true-ownership shape (real, independent
// dependencies - SceneRuntime, ModelViewer, not a ViewportWidget
// back-reference), but with a much smaller surface: one anchor "type" (a
// plain surface pick, no edge/face/circle variants) and one drag kind (a
// free 3D leader offset, no angle-radius case), so there's no per-type
// resolver switch the way Measurement needs one.
//
// Is a QObject (like MeasurementController, unlike TransformGizmo) because
// several methods emit signals mid-body - ViewportWidget relays these to its
// own identically-named signals via connect().
// ---------------------------------------------------------------------------
class AnnotationController : public QObject, protected QOpenGLFunctions_4_5_Core, public IGpuContextResource
{
    Q_OBJECT

public:
    AnnotationController(SceneRuntime& sceneRuntime, ModelViewer* viewer,
        SceneRenderController& renderCtrl, QObject* parent = nullptr);

    // Owns no GL objects of its own (only calls into SceneRenderController's
    // dedicated annotation-overlay VAO/VBO via _renderCtrl) - same reasoning
    // as MeasurementController::restoreGpuResources()/releaseGpuResources().
    void releaseGpuResources() override;
    void restoreGpuResources() override;

    // ---- Tool state ---------------------------------------------------------
    // Arming the Annotate tool disarms an active Measure tool and vice versa
    // (mutual exclusivity - avoids ambiguous click routing when both could
    // in principle be armed at once). While armed, left-clicks place a note
    // (via SelectionManager::pickSurfaceAnchor()) instead of doing normal
    // selection - see AnnotationController::handleAnnotationClick().
    void setAnnotationToolArmed(bool armed, SelectionManager* selectionManager);
    bool annotationToolArmed() const { return _annotationToolArmed; }

    // ---- Anchor resolution ---------------------------------------------------
    // Re-derives the anchor's current world position from the mesh's CURRENT
    // geometry (triangle/barycentric or snapped-vertex) - see
    // MeasurementController::resolveMeasurementAnchor()'s doc comment for the
    // full "why not a frozen position" reasoning, identical here. edgeIndex
    // is not handled (v1 scope: a note anchors to a surface point, not an
    // edge - see AnnotationData.h).
    QVector3D resolveAnnotationAnchor(const MeasurementAnchorRef& ref) const;

    // ---- Picking / interaction ------------------------------------------------
    // Places one note at the clicked surface point (default text "New
    // Note", ready to edit in AnnotationDialog's details pane). Tool stays
    // armed afterward so several notes can be placed in a row - matches
    // Measurement's "stays armed until Escape/tool switch" convention.
    void handleAnnotationClick(const QPoint& clickPoint, SelectionManager* selectionManager, Camera* camera);
    // Hit-tests a note's rectangular frame (drawn around its text - see
    // drawAnnotationOverlay()) first, falling back to a pixelRadius-wide
    // proximity test against the anchor marker/connecting leader itself.
    // axisTextRenderer is needed to measure the frame's on-screen size
    // (see TextRenderer::textWidth()) - a null pointer just skips the
    // frame test and falls back to the marker/leader proximity test alone.
    QUuid hitTestAnnotation(const QPoint& pixel, Camera* camera, const QSize& viewportSize,
        TextRenderer* axisTextRenderer, int pixelRadius = 8) const;
    // Same test as hitTestAnnotation() - for a note, the frame IS the
    // draggable part (see hitTestAnnotation()'s doc comment and this
    // method's .cpp doc comment for why they're literally the same test).
    QUuid hitTestAnnotationLeader(const QPoint& pixel, Camera* camera, const QSize& viewportSize,
        TextRenderer* axisTextRenderer, int pixelRadius = 8) const;
    void beginAnnotationLeaderDrag(const QUuid& annotationId, Camera* camera);
    void updateAnnotationLeaderDrag(const QPoint& pixel, Camera* camera, const QSize& viewportSize);
    // viewportWidget is passed through only to construct AnnotationOffsetCommand
    // (needs it purely for the shared ModelViewerCommand base's undo/redo
    // repaint trigger) - not stored, same one exception MeasurementController
    // makes for finishDimensionLineDrag().
    void finishAnnotationLeaderDrag(ViewportWidget* viewportWidget);

    void drawAnnotationOverlay(Camera* camera, const QSize& viewportSize, TextRenderer* axisTextRenderer);

    // ---- Hover preview (called from ViewportWidget::mouseMoveEvent) ------
    // Live hover preview of where a click would place a note, while the
    // tool is armed - mirrors MeasurementController::updateHoverAnchor().
    void updateHoverAnchor(const QPoint& pixel, SelectionManager* selectionManager);
    // Hover preview for an already-placed annotation (no tool armed) - both
    // its whole-note highlight and its draggable frame specifically -
    // mirrors MeasurementController::updateHoverMeasurement().
    void updateHoverAnnotation(const QPoint& pixel, Camera* camera, const QSize& viewportSize, TextRenderer* axisTextRenderer);

    // ---- Selection ---------------------------------------------------------
    const QSet<QUuid>& selectedAnnotationIds() const { return _selectedAnnotationIds; }
    bool hasSelectedAnnotations() const { return !_selectedAnnotationIds.isEmpty(); }
    void setSelectedAnnotationIds(const QSet<QUuid>& ids);
    QUuid hoveredAnnotationId() const { return _hoveredAnnotationId; }

signals:
    // Relayed 1:1 by ViewportWidget to its own identically-named signals -
    // see this class's doc comment.
    void annotationToolArmedChanged(bool armed);
    void annotationSelectionChanged(const QSet<QUuid>& ids);
    // Pure repaint trigger (no ViewportWidget equivalent) - connect directly
    // to ViewportWidget::update().
    void annotationStateChanged();

private:
    SceneMesh* getMeshByUuid(const QUuid& uuid) const;

    // Default leader direction for an undragged note (Annotation::leaderOffset
    // is zero-length) - a fixed direction perpendicular to the creation-time
    // view direction (Annotation::offsetReferenceDir), so it reads as
    // "off to the side" and stays stable while orbiting, same non-swimming
    // reasoning as MeasurementController::dimensionLinePerp(). Unlike that
    // function there is no measured-segment direction to cross against
    // first - referenceDir alone determines it.
    QVector3D defaultLeaderDirection(const QVector3D& referenceDir) const;
    float defaultLeaderOffsetMagnitude(Camera* camera) const;
    // Mirrors MeasurementController::resolveDimensionOffsetVector() for the
    // single-point case - returns the dragged leaderOffset as-is once set,
    // otherwise the default direction/magnitude above.
    QVector3D resolveAnnotationLeaderOffset(const Annotation& a, Camera* camera) const;

    // The on-screen rectangle drawn around a note's text (see
    // drawAnnotationOverlay()) and used to hit-test/drag it (see
    // hitTestAnnotation()) - one shared computation so rendering and
    // interaction can never disagree about where the frame actually is,
    // same "one authoritative source" reasoning as MeasurementController's
    // resolveDimensionOffsetVector(). Returns an invalid (default) QRectF
    // if camera or axisTextRenderer is null. Coordinates are in the same
    // top-down pixel space RenderText() itself takes (see this method's
    // .cpp doc comment for the full baseY/y-direction derivation) - NOT
    // raw OpenGL/project() bottom-up space.
    QRectF frameScreenRectForLabel(const QVector3D& labelPos, const QString& text,
        Camera* camera, const QSize& viewportSize, TextRenderer* axisTextRenderer) const;

    bool _glFunctionsInitialized = false;

    SceneRuntime& _sceneRuntime;
    ModelViewer* _viewer;
    SceneRenderController& _renderCtrl;

    // ---- Tool-armed state --------------------------------------------------
    bool _annotationToolArmed = false;
    MeshSurfaceAnchor _annotationHoverAnchor;
    HoverHighlightMode _savedHoverHighlightModeBeforeAnnotation = HoverHighlightMode::RaycastOnly;
    // Press-vs-drag disambiguation for PLACING a new note, same role as
    // MeasurementController's _measurementClickCandidate.
    bool _annotationClickCandidate = false;
    QPoint _annotationClickPressPos;

    // ---- Selection / hover state --------------------------------------------
    QSet<QUuid> _selectedAnnotationIds;
    QUuid _hoveredAnnotationId;

    // ---- Leader-line drag state -----------------------------------------
    bool _leaderDragCandidate = false;
    QPoint _leaderDragStartPixel;
    QUuid _leaderDragCandidateId;
    bool _leaderDragActive = false;
    QVector3D _leaderDragPivot;              // the anchor's resolved world position
    QVector3D _leaderDragAxis;                // plane normal (camera view dir at drag start) - drag is free within this screen-parallel plane
    QVector3D _leaderDragStartOffset;
    QUuid _hoveredLeaderId;

public:
    // Exposed for ViewportWidget's mouse-event dispatch (press-vs-drag
    // disambiguation lives there, alongside the general-purpose equivalents
    // for camera rotate/pan/sweep gestures and Measurement's own dimension-
    // drag candidate state).
    bool leaderDragCandidate() const { return _leaderDragCandidate; }
    void setLeaderDragCandidate(bool candidate) { _leaderDragCandidate = candidate; }
    QPoint leaderDragStartPixel() const { return _leaderDragStartPixel; }
    void setLeaderDragStartPixel(const QPoint& pixel) { _leaderDragStartPixel = pixel; }
    QUuid leaderDragCandidateId() const { return _leaderDragCandidateId; }
    void setLeaderDragCandidateId(const QUuid& id) { _leaderDragCandidateId = id; }
    bool leaderDragActive() const { return _leaderDragActive; }
    QUuid hoveredLeaderId() const { return _hoveredLeaderId; }

    bool annotationClickCandidate() const { return _annotationClickCandidate; }
    void setAnnotationClickCandidate(bool candidate) { _annotationClickCandidate = candidate; }
    QPoint annotationClickPressPos() const { return _annotationClickPressPos; }
    void setAnnotationClickPressPos(const QPoint& pixel) { _annotationClickPressPos = pixel; }
};
