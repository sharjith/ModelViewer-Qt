#pragma once

#include "IGpuContextResource.h"
#include "SeamEdgeMark.h"
#include "MeshEdgeCircleAnchor.h"
#include "SelectionManager.h"  // HoverHighlightMode - used as a value field type below

#include <QObject>
#include <QOpenGLFunctions_4_5_Core>
#include <QPoint>
#include <QSize>
#include <QVector>

class Camera;
class SceneMesh;
class SceneRenderController;
class SceneRuntime;

// ---------------------------------------------------------------------------
// SeamMarkingController
//
// Owns the "Mark Seams" tool for the Generate UVs dialog: click a mesh edge
// in the viewport to force it into UVGenerator::findSeams()'s seam set for
// Angle-Based/Angle-Based Smart UV/ARAP, independent of the automatic
// dihedral-angle detection. Structurally mirrors AnnotationController (tool-
// armed state, hover anchor, click-vs-drag disambiguation left to
// ViewportWidget, IGpuContextResource for the overlay draw), but deliberately
// smaller: no ModelViewer* dependency, no undo command, no SceneGraph
// backing store. Marks are SESSION state owned directly by this controller -
// added/toggled by clicking, edited via UVGenerationDialog's own list
// (Remove Selected/Clear All), and discarded when
// ViewportWidget::clearSeamMarks() is called (on the dialog's close/reject) -
// not persisted, not undoable, not saved to .mvf. See the Part B plan
// ("dialog-scoped design") for why: a seam mark has exactly one consumer
// (findSeams(), used by 3 of 9 UV methods), so document-level persistence/
// undo would be solving a problem nothing else in the app has.
//
// Reuses SelectionManager::pickStraightEdgeAnchor() as-is for picking (both
// CAD and non-CAD meshes already handled uniformly there) and
// SceneMesh::resolveEdgeMarkWorldEndpoints() for resolving a mark to its two
// endpoints for overlay drawing.
// ---------------------------------------------------------------------------
class SeamMarkingController : public QObject, protected QOpenGLFunctions_4_5_Core, public IGpuContextResource
{
    Q_OBJECT

public:
    SeamMarkingController(SceneRuntime& sceneRuntime, SceneRenderController& renderCtrl,
        QObject* parent = nullptr);

    // Owns no GL objects of its own (only calls into SceneRenderController's
    // dedicated seam-overlay VAO/VBO via _renderCtrl) - same reasoning as
    // AnnotationController::restoreGpuResources()/releaseGpuResources().
    void releaseGpuResources() override;
    void restoreGpuResources() override;

    // ---- Tool state ---------------------------------------------------------
    // Arming disarms an active Measure/Annotate tool and vice versa (3-way
    // mutual exclusivity, extending the existing Measure/Annotate pair - see
    // ViewportWidget::setSeamMarkingToolArmed()). While armed, left-clicks
    // toggle a seam mark (via SelectionManager::pickStraightEdgeAnchor())
    // instead of doing normal selection.
    void setSeamToolArmed(bool armed, SelectionManager* selectionManager);
    bool seamToolArmed() const { return _seamToolArmed; }

    // Picks the clicked edge and toggles it in/out of marks() - append if
    // not already marked, remove if it is (exact SeamEdgeMark equality, no
    // fuzzy matching needed since edgeIndex is already a stable identity).
    // A click on empty space is a no-op (stays armed, same convention
    // AnnotationController::handleAnnotationClick() follows).
    void handleSeamClick(const QPoint& clickPoint, SelectionManager* selectionManager);

    // Live hover preview while armed - mirrors AnnotationController::updateHoverAnchor().
    void updateHoverAnchor(const QPoint& pixel, SelectionManager* selectionManager);

    void drawSeamOverlay(Camera* camera);

    // ---- Mark list (session state - see this class's doc comment) ---------
    const QVector<SeamEdgeMark>& marks() const { return _marks; }
    void removeMarkAt(int index);
    // Clears the mark list only - does NOT disarm the tool (that's a
    // separate concern; ViewportWidget::clearSeamMarks() composes both for
    // the dialog's close/reject case).
    void clearMarks();

signals:
    void seamToolArmedChanged(bool armed);
    void marksChanged();
    // Pure repaint trigger (no ViewportWidget equivalent) - connect directly
    // to ViewportWidget::update(), same as AnnotationController::annotationStateChanged.
    void seamStateChanged();

private:
    SceneMesh* getMeshByUuid(const QUuid& uuid) const;

    bool _glFunctionsInitialized = false;

    SceneRuntime& _sceneRuntime;
    SceneRenderController& _renderCtrl;

    // ---- Tool-armed state --------------------------------------------------
    bool _seamToolArmed = false;
    MeshEdgeCircleAnchor _seamHoverAnchor;
    HoverHighlightMode _savedHoverHighlightModeBeforeSeamMarking = HoverHighlightMode::RaycastOnly;
    // Press-vs-drag disambiguation for the toggle click, same role as
    // AnnotationController's _annotationClickCandidate.
    bool _seamClickCandidate = false;
    QPoint _seamClickPressPos;

    QVector<SeamEdgeMark> _marks;

public:
    // Exposed for ViewportWidget's mouse-event dispatch (press-vs-drag
    // disambiguation lives there, matching Measure/Annotate's own
    // click-candidate state).
    bool seamClickCandidate() const { return _seamClickCandidate; }
    void setSeamClickCandidate(bool candidate) { _seamClickCandidate = candidate; }
    QPoint seamClickPressPos() const { return _seamClickPressPos; }
    void setSeamClickPressPos(const QPoint& pixel) { _seamClickPressPos = pixel; }
};
