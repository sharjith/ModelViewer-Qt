#include "SeamMarkingController.h"

#include "Camera.h"
#include "SceneMesh.h"
#include "SceneRenderController.h"
#include "SceneRuntime.h"
#include "SelectionManager.h"

#include <QMatrix4x4>
#include <QOpenGLShaderProgram>

SeamMarkingController::SeamMarkingController(SceneRuntime& sceneRuntime,
    SceneRenderController& renderCtrl, QObject* parent)
    : QObject(parent)
    , _sceneRuntime(sceneRuntime)
    , _renderCtrl(renderCtrl)
{
}

SceneMesh* SeamMarkingController::getMeshByUuid(const QUuid& uuid) const
{
    return _sceneRuntime.getMeshByUuid(uuid);
}

void SeamMarkingController::restoreGpuResources()
{
    // Re-resolves this class's own QOpenGLFunctions_4_5_Core function pointers against the new
    // context - nothing to actually release/rebuild, same reasoning as AnnotationController's
    // identical override.
    initializeOpenGLFunctions();
    _glFunctionsInitialized = true;
}

void SeamMarkingController::releaseGpuResources()
{
    _glFunctionsInitialized = false;
}

void SeamMarkingController::setSeamToolArmed(bool armed, SelectionManager* selectionManager)
{
    if (_seamToolArmed == armed)
        return;

    if (selectionManager)
    {
        if (armed)
        {
            // Suppress the normal whole-mesh hover highlight while the tool is armed - same
            // reasoning as AnnotationController::setAnnotationToolArmed()'s identical block.
            _savedHoverHighlightModeBeforeSeamMarking = selectionManager->getHoverMode();
            selectionManager->setHoverHighlightMode(HoverHighlightMode::Disabled);
        }
        else
        {
            selectionManager->setHoverHighlightMode(_savedHoverHighlightModeBeforeSeamMarking);
        }
    }

    _seamToolArmed = armed;
    _seamClickCandidate = false;
    _seamHoverAnchor = MeshEdgeCircleAnchor();
    emit seamStateChanged();
    emit seamToolArmedChanged(_seamToolArmed);
}

void SeamMarkingController::handleSeamClick(const QPoint& clickPoint, SelectionManager* selectionManager)
{
    if (!selectionManager || !_seamToolArmed)
        return;

    const MeshEdgeCircleAnchor anchor = selectionManager->pickStraightEdgeAnchor(clickPoint);
    if (!anchor.isValid())
        return;  // clicked empty space - stay armed, don't cancel the tool

    const SeamEdgeMark mark{ anchor.meshUuid, anchor.edgeIndex };
    const int existingIndex = _marks.indexOf(mark);
    if (existingIndex >= 0)
        _marks.removeAt(existingIndex);
    else
        _marks.append(mark);

    emit marksChanged();
    emit seamStateChanged();
}

void SeamMarkingController::updateHoverAnchor(const QPoint& pixel, SelectionManager* selectionManager)
{
    if (!_seamToolArmed || !selectionManager)
        return;
    _seamHoverAnchor = selectionManager->pickStraightEdgeAnchor(pixel);
}

void SeamMarkingController::removeMarkAt(int index)
{
    if (index < 0 || index >= _marks.size())
        return;
    _marks.removeAt(index);
    emit marksChanged();
    emit seamStateChanged();
}

void SeamMarkingController::clearMarks()
{
    if (_marks.isEmpty())
        return;
    _marks.clear();
    emit marksChanged();
    emit seamStateChanged();
}

void SeamMarkingController::drawSeamOverlay(Camera* camera)
{
    if (!_glFunctionsInitialized || !camera || !_renderCtrl.axisShader())
        return;
    if (_marks.isEmpty() && !_seamHoverAnchor.isValid())
        return;

    std::vector<float> lineVertices;
    auto addSegment = [&lineVertices](const QVector3D& a, const QVector3D& b, const QVector3D& color) {
        lineVertices.insert(lineVertices.end(), { a.x(), a.y(), a.z(), color.x(), color.y(), color.z() });
        lineVertices.insert(lineVertices.end(), { b.x(), b.y(), b.z(), color.x(), color.y(), color.z() });
    };

    const QVector3D markedColor(0.95f, 0.15f, 0.15f);     // red - already-marked seam
    const QVector3D previewUnmarkColor(1.0f, 0.6f, 0.1f); // orange - hovering an already-marked edge (click would unmark)
    const QVector3D previewMarkColor(0.25f, 1.0f, 0.35f); // green - hovering an unmarked edge (click would mark)

    for (const SeamEdgeMark& mark : _marks)
    {
        SceneMesh* mesh = getMeshByUuid(mark.meshUuid);
        if (!mesh)
            continue;
        QVector3D start, end;
        if (mesh->resolveEdgeMarkWorldEndpoints(mark.edgeIndex, start, end))
            addSegment(start, end, markedColor);
    }

    if (_seamHoverAnchor.isValid())
    {
        SceneMesh* mesh = getMeshByUuid(_seamHoverAnchor.meshUuid);
        if (mesh)
        {
            QVector3D start, end;
            if (mesh->resolveEdgeMarkWorldEndpoints(_seamHoverAnchor.edgeIndex, start, end))
            {
                const bool alreadyMarked = _marks.contains(
                    SeamEdgeMark{ _seamHoverAnchor.meshUuid, _seamHoverAnchor.edgeIndex });
                addSegment(start, end, alreadyMarked ? previewUnmarkColor : previewMarkColor);
            }
        }
    }

    if (lineVertices.empty())
        return;

    // Dimension/measurement-style overlays never hide behind shaded surfaces - same reasoning as
    // MeasurementController::drawMeasurementOverlay(). Saved/restored, not just force-disabled.
    const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_DEPTH_TEST);

    _renderCtrl.initSeamOverlayGeometry(lineVertices);
    glBindVertexArray(_renderCtrl.seamOverlayVAO());
    glBindBuffer(GL_ARRAY_BUFFER, _renderCtrl.seamOverlayVBO());
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
