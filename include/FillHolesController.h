#pragma once

#include "IGpuContextResource.h"
#include "SceneMesh.h" // DetectedHole

#include <QObject>
#include <QOpenGLFunctions_4_5_Core>
#include <QUuid>
#include <vector>

class Camera;
class SceneRenderController;

// ---------------------------------------------------------------------------
// FillHolesController
//
// Owns the Fill Holes dialog's detected-hole-loop overlay: a data cache + per-frame line-list
// draw, driven entirely by FillHolesDialog (setDetectedHoles()/setHighlightedHole()) rather than
// any viewport interaction of its own - unlike SeamMarkingController/AnnotationController,
// there is no tool-armed state, hover anchor, or press-vs-drag click disambiguation here (no
// viewport click-to-toggle in this first pass - see the Fill Holes plan's "explicitly out of
// scope" section). Also unlike SeamMarkingController (which re-resolves each mark's world
// position live every frame via SceneMesh::resolveEdgeMarkWorldEndpoints(), since a mark is just
// a meshUuid+edgeIndex), a DetectedHole already carries its own baked world-space loopPoints
// from the SceneMesh::detectHoles() call that produced it - no per-frame mesh lookup needed here.
// ---------------------------------------------------------------------------
class FillHolesController : public QObject, protected QOpenGLFunctions_4_5_Core, public IGpuContextResource
{
    Q_OBJECT

public:
    explicit FillHolesController(SceneRenderController& renderCtrl, QObject* parent = nullptr);

    // Owns no GL objects of its own (only calls into SceneRenderController's dedicated
    // fill-holes-overlay VAO/VBO via _renderCtrl) - same reasoning as
    // SeamMarkingController::restoreGpuResources()/releaseGpuResources().
    void releaseGpuResources() override;
    void restoreGpuResources() override;

    // FillHolesDialog pushes its current detection result here whenever its mesh list changes.
    void setDetectedHoles(std::vector<DetectedHole> holes);
    void clearDetectedHoles();

    // FillHolesDialog calls this from the holes list's itemSelectionChanged - loopId of -1
    // (the default) means "no highlight" without needing a separate clear call, but
    // clearHighlightedHole() is provided too for callers that prefer to be explicit.
    void setHighlightedHole(const QUuid& meshUuid, int loopId);
    void clearHighlightedHole();

    void drawOverlay(Camera* camera);

private:
    bool _glFunctionsInitialized = false;

    SceneRenderController& _renderCtrl;

    std::vector<DetectedHole> _holes;
    QUuid _highlightedMeshUuid;
    int _highlightedLoopId = -1;
};
