#include "FillHolesController.h"

#include "Camera.h"
#include "SceneRenderController.h"

#include <QMatrix4x4>
#include <QOpenGLShaderProgram>
#include <QVector3D>

FillHolesController::FillHolesController(SceneRenderController& renderCtrl, QObject* parent)
    : QObject(parent)
    , _renderCtrl(renderCtrl)
{
}

void FillHolesController::restoreGpuResources()
{
    // Re-resolves this class's own QOpenGLFunctions_4_5_Core function pointers against the new
    // context - nothing to actually release/rebuild, same reasoning as
    // SeamMarkingController's identical override.
    initializeOpenGLFunctions();
    _glFunctionsInitialized = true;
}

void FillHolesController::releaseGpuResources()
{
    _glFunctionsInitialized = false;
}

void FillHolesController::setDetectedHoles(std::vector<DetectedHole> holes)
{
    _holes = std::move(holes);
}

void FillHolesController::clearDetectedHoles()
{
    _holes.clear();
    clearHighlightedHole();
}

void FillHolesController::setHighlightedHole(const QUuid& meshUuid, int loopId)
{
    _highlightedMeshUuid = meshUuid;
    _highlightedLoopId = loopId;
}

void FillHolesController::clearHighlightedHole()
{
    _highlightedMeshUuid = QUuid();
    _highlightedLoopId = -1;
}

void FillHolesController::drawOverlay(Camera* camera)
{
    if (!_glFunctionsInitialized || !camera || !_renderCtrl.axisShader())
        return;
    if (_holes.empty())
        return;

    std::vector<float> lineVertices;
    auto addSegment = [&lineVertices](const glm::vec3& a, const glm::vec3& b, const QVector3D& color) {
        lineVertices.insert(lineVertices.end(), { a.x, a.y, a.z, color.x(), color.y(), color.z() });
        lineVertices.insert(lineVertices.end(), { b.x, b.y, b.z, color.x(), color.y(), color.z() });
    };

    const QVector3D detectedColor(0.25f, 1.0f, 0.35f);    // green - detected, not selected in the list
    const QVector3D highlightedColor(1.0f, 0.35f, 0.05f); // orange - selected in the dialog's holes list

    for (const DetectedHole& hole : _holes)
    {
        if (hole.loopPoints.size() < 2)
            continue;
        const bool highlighted = hole.meshUuid == _highlightedMeshUuid && hole.loopId == _highlightedLoopId;
        const QVector3D& color = highlighted ? highlightedColor : detectedColor;
        // A boundary cycle is closed - the last point connects back to the first, same as
        // walking any other face's halfedge cycle.
        for (std::size_t i = 0; i < hole.loopPoints.size(); ++i)
            addSegment(hole.loopPoints[i], hole.loopPoints[(i + 1) % hole.loopPoints.size()], color);
    }

    if (lineVertices.empty())
        return;

    // Same as SeamMarkingController::drawSeamOverlay() - a picker overlay should never hide
    // behind shaded surfaces. Saved/restored, not just force-disabled.
    const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_DEPTH_TEST);

    _renderCtrl.initFillHolesOverlayGeometry(lineVertices);
    glBindVertexArray(_renderCtrl.fillHolesOverlayVAO());
    glBindBuffer(GL_ARRAY_BUFFER, _renderCtrl.fillHolesOverlayVBO());
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(lineVertices.size() * sizeof(float)),
                 lineVertices.data(),
                 GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void*>(3 * sizeof(float)));

    _renderCtrl.axisShader()->bind();
    _renderCtrl.axisShader()->setUniformValue("modelViewMatrix", camera->getViewMatrix());
    _renderCtrl.axisShader()->setUniformValue("projectionMatrix", camera->getProjectionMatrix());
    _renderCtrl.axisShader()->setUniformValue("renderCone", false);
    glLineWidth(2.0f);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineVertices.size() / 6));
    glLineWidth(1.0f);
    _renderCtrl.axisShader()->release();

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    if (depthWasEnabled)
        glEnable(GL_DEPTH_TEST);
}
