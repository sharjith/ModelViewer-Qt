#include "AnnotationController.h"
#include "InPlaneText.h"
#include "OverlayLineWeights.h"

#include "AnnotationOffsetCommand.h"
#include "Camera.h"
#include "ModelViewer.h"
#include "SceneGraph.h"
#include "SceneMesh.h"
#include "SceneRenderController.h"
#include "SceneRuntime.h"
#include "SelectionManager.h"
#include "TextRenderer.h"

#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QRect>
#include <QSet>
#include <QStringList>
#include <algorithm>
#include <cmath>

AnnotationController::AnnotationController(SceneRuntime& sceneRuntime, ModelViewer* viewer,
    SceneRenderController& renderCtrl, QObject* parent)
    : QObject(parent)
    , _sceneRuntime(sceneRuntime)
    , _viewer(viewer)
    , _renderCtrl(renderCtrl)
{
}

SceneMesh* AnnotationController::getMeshByUuid(const QUuid& uuid) const
{
    return _sceneRuntime.getMeshByUuid(uuid);
}

void AnnotationController::restoreGpuResources()
{
    // Re-resolves this class's own QOpenGLFunctions_4_5_Core function
    // pointers against the new context - see
    // MeasurementController::restoreGpuResources()'s doc comment (same
    // "nothing to actually release/rebuild, just re-resolve pointers"
    // reasoning applies here).
    initializeOpenGLFunctions();
    _glFunctionsInitialized = true;
}

void AnnotationController::releaseGpuResources()
{
    _glFunctionsInitialized = false;
}

void AnnotationController::setAnnotationToolArmed(bool armed, SelectionManager* selectionManager)
{
    if (_annotationToolArmed == armed)
        return;

    if (selectionManager)
    {
        if (armed)
        {
            // Suppress the normal whole-mesh hover highlight while the tool
            // is armed - same reasoning as MeasurementController::
            // setMeasurementTool()'s identical block.
            _savedHoverHighlightModeBeforeAnnotation = selectionManager->getHoverMode();
            selectionManager->setHoverHighlightMode(HoverHighlightMode::Disabled);
        }
        else
        {
            selectionManager->setHoverHighlightMode(_savedHoverHighlightModeBeforeAnnotation);
        }
    }

    _annotationToolArmed = armed;
    _annotationClickCandidate = false;
    _annotationHoverAnchor = MeshSurfaceAnchor();
    emit annotationStateChanged();
    emit annotationToolArmedChanged(_annotationToolArmed);
}

QVector3D AnnotationController::resolveAnnotationAnchor(const MeasurementAnchorRef& ref) const
{
    SceneMesh* mesh = getMeshByUuid(ref.meshUuid);
    if (!mesh)
        return QVector3D();

    const std::vector<float>& trsfPoints = mesh->getTrsfPoints();
    auto vertexPos = [&trsfPoints](int vIdx) -> QVector3D {
        if (vIdx < 0)
            return QVector3D();
        const size_t p = static_cast<size_t>(vIdx) * 3;
        if (p + 2 >= trsfPoints.size())
            return QVector3D();
        return QVector3D(trsfPoints[p], trsfPoints[p + 1], trsfPoints[p + 2]);
    };

    if (ref.snappedVertexIndex >= 0)
        return vertexPos(ref.snappedVertexIndex);

    if (ref.triangleIndex < 0)
        return QVector3D();

    const std::vector<unsigned int> indices = mesh->indices();
    const size_t base = static_cast<size_t>(ref.triangleIndex) * 3;
    if (base + 2 >= indices.size())
        return QVector3D();

    const QVector3D p0 = vertexPos(static_cast<int>(indices[base]));
    const QVector3D p1 = vertexPos(static_cast<int>(indices[base + 1]));
    const QVector3D p2 = vertexPos(static_cast<int>(indices[base + 2]));
    return p0 * ref.barycentric.x() + p1 * ref.barycentric.y() + p2 * ref.barycentric.z();
}

void AnnotationController::handleAnnotationClick(const QPoint& clickPoint, SelectionManager* selectionManager, Camera* camera)
{
    if (!selectionManager || !_viewer || !_viewer->sceneGraph() || !_annotationToolArmed)
        return;

    const MeshSurfaceAnchor anchor = selectionManager->pickSurfaceAnchor(clickPoint);
    if (!anchor.isValid())
        return;  // clicked empty space - stay armed, don't cancel the tool

    Annotation a;
    a.id = QUuid::createUuid();
    a.text = tr("New Note");
    a.anchor.meshUuid           = anchor.meshUuid;
    a.anchor.triangleIndex      = anchor.triangleIndex;
    a.anchor.barycentric        = anchor.barycentric;
    a.anchor.snappedVertexIndex = anchor.snappedVertexIndex;
    // Captured once, here, at creation - see Annotation::offsetReferenceDir's
    // doc comment for why this must NOT be re-derived from the live camera
    // on every render frame.
    if (camera)
        a.offsetReferenceDir = camera->getViewDir();

    _viewer->addAnnotation(a);  // undoable - see AddAnnotationCommand

    // Select the just-placed note so AnnotationDialog's details pane shows
    // it immediately, ready to edit - the whole point of the default
    // "New Note" text is that the user types over it right away.
    _selectedAnnotationIds = { a.id };
    emit annotationSelectionChanged(_selectedAnnotationIds);
    emit annotationStateChanged();
}

void AnnotationController::updateHoverAnchor(const QPoint& pixel, SelectionManager* selectionManager)
{
    if (!_annotationToolArmed || !selectionManager)
        return;
    _annotationHoverAnchor = selectionManager->pickSurfaceAnchor(pixel);
}

QVector3D AnnotationController::defaultLeaderDirection(const QVector3D& referenceDir) const
{
    // A fixed direction perpendicular to the creation-time view direction -
    // mirrors MeasurementController::dimensionLinePerp()'s fallback chain,
    // simplified since there's no measured-segment direction to cross
    // against first (an annotation has only one anchor point).
    QVector3D dir = QVector3D::crossProduct(referenceDir, QVector3D(0.0f, 1.0f, 0.0f));
    if (dir.lengthSquared() < 1.0e-8f)
        dir = QVector3D::crossProduct(referenceDir, QVector3D(1.0f, 0.0f, 0.0f));
    if (dir.lengthSquared() < 1.0e-8f)
        return QVector3D(1.0f, 0.0f, 0.0f);
    return dir.normalized();
}

float AnnotationController::defaultLeaderOffsetMagnitude(Camera* camera) const
{
    const float markerSize = camera ? std::max(camera->getViewRange(), 0.0001f) * 0.01f : 0.01f;
    return markerSize * 6.0f;
}

QVector3D AnnotationController::resolveAnnotationLeaderOffset(const Annotation& a, Camera* camera) const
{
    const LabelPlane::Basis plane = annotationPlane(a);
    if (a.leaderOffset.lengthSquared() > 1.0e-10f)
    {
        if (!plane.valid)
            return a.leaderOffset;  // user has dragged this - use the exact vector (direction + magnitude)
        // In a plane the leader stays in it, however the offset was stored.
        return a.leaderOffset - plane.normal * QVector3D::dotProduct(a.leaderOffset, plane.normal);
    }
    if (plane.valid)
    {
        // Up and to the right in the plane, on the side of its axes that points up in the model (+Z, else +Y, else +X).
        QVector3D up = plane.y;
        for (int axis = 2; axis >= 0; --axis)
            if (std::abs(up[axis]) > 1.0e-3f)
            {
                if (up[axis] < 0.0f)
                    up = -up;
                break;
            }
        return (up + plane.x * 0.5f).normalized() * defaultLeaderOffsetMagnitude(camera);
    }
    return defaultLeaderDirection(a.offsetReferenceDir) * defaultLeaderOffsetMagnitude(camera);
}

LabelPlane::Basis AnnotationController::annotationPlane(const Annotation& a) const
{
    return LabelPlane::forOrientation(a.orientation, a.offsetReferenceDir);
}

AnnotationController::InPlaneNote AnnotationController::layoutInPlaneNote(const Annotation& a, const QVector3D& labelPos, Camera* camera,
    const QSize& viewportSize, float textScale) const
{
    InPlaneNote note;
    if (!camera)
        return note;
    const LabelPlane::Basis plane = annotationPlane(a);
    if (plane.valid)
    {
        // Orthographic rays remain parallel as zoom changes the camera's render position.
        const QVector3D towardViewer = camera->getProjectionType() == Camera::ProjectionType::ORTHOGRAPHIC
            ? -camera->getViewDir() : camera->getRenderPosition() - labelPos;
        const LabelPlane::Basis reading = LabelPlane::readable(plane, towardViewer, camera->getRightVector());
        note.x = reading.x;
        note.y = reading.y;
        note.capHeight = std::max(camera->getViewRange(), 0.0001f) * 0.01f * LabelPlane::InPlaneCapScale * textScale;
    }
    else
    {
        // A note that faces the screen lies in the camera's own plane, at a fixed size on screen.
        const QRect viewportRect(0, 0, viewportSize.width(), viewportSize.height());
        note.x = camera->getRightVector();
        note.y = camera->getUpVector();
        note.capHeight = LabelPlane::BillboardCapPixels * textScale
            * LabelPlane::worldPerPixel(labelPos, camera->getViewMatrix(), camera->getProjectionMatrix(), viewportRect);
    }

    const QStringList lines = a.text.split(QChar('\n'));
    float widest = 0.0f;
    for (const QString& line : lines)
    {
        const InPlaneText::Outline& o = InPlaneText::outline(line);
        widest = std::max(widest, o.right - o.left);
    }
    const float cap = note.capHeight;
    const float pad = 0.45f * cap;
    const float left = -pad, right = widest * cap + pad;
    const float bottom = -0.3f * cap - pad;
    const float top = cap * (1.0f + 1.6f * float(lines.size() - 1)) + pad;
    auto at = [&](float u, float v) { return labelPos + note.x * u + note.y * v; };
    note.corners = { at(left, top), at(right, top), at(right, bottom), at(left, bottom) };
    note.valid = true;
    return note;
}

QRectF AnnotationController::frameScreenRectForLabel(const QVector3D& labelPos, const QString& text,
    Camera* camera, const QSize& viewportSize, TextRenderer* axisTextRenderer) const
{
    if (!camera || !axisTextRenderer)
        return QRectF();

    const QRect viewportRect(0, 0, viewportSize.width(), viewportSize.height());
    const QVector3D projected = labelPos.project(camera->getViewMatrix(), camera->getProjectionMatrix(), viewportRect);
    const float baseX = projected.x();
    // Same OpenGL-bottom-up -> Qt/RenderText-top-down flip
    // MeasurementController::drawMeasurementOverlay()'s label loop uses -
    // this IS the exact (x, y) pair callers pass into TextRenderer::
    // RenderText(), so a rect built in this same space lines up with what
    // actually gets drawn, and inverts cleanly for drawAnnotationOverlay()'s
    // frame-border geometry (see that method).
    const float baseY = static_cast<float>(viewportSize.height()) - projected.y();

    return frameScreenOffsetRect(text, axisTextRenderer).translated(static_cast<qreal>(baseX), static_cast<qreal>(baseY));
}

QRectF AnnotationController::frameScreenOffsetRect(const QString& text, TextRenderer* axisTextRenderer) const
{
    if (!axisTextRenderer)
        return QRectF();

    const QStringList lines = text.split(QChar('\n'));
    const int lineCount = std::max(static_cast<int>(lines.size()), 1);
    float maxWidth = 0.0f;
    for (const QString& line : lines)
        maxWidth = std::max(maxWidth, axisTextRenderer->textWidth(line.toStdString()));

    // Matches drawAnnotationOverlay()'s own line-stacking step exactly (see
    // that method and MeasurementController::drawMeasurementOverlay()'s
    // identical lineHeight constant) - the frame must stack lines the same
    // way the text itself does, or a multi-line note's frame won't actually
    // enclose every line.
    const float lineHeight = static_cast<float>(axisTextRenderer->fontSize()) * 1.2f;
    constexpr float kFramePadding = 6.0f;

    // Real per-glyph ascent/descent for VBOTTOM (see TextRenderer::
    // textVerticalExtentVBottom()'s doc comment for why a generic fontSize
    // fraction guess doesn't work here - VBOTTOM's y does not sit at the
    // glyphs' visual bottom). Measuring the whole (possibly multi-line)
    // text at once is a deliberate, slightly loose simplification: the
    // result is applied uniformly to every line's top/bottom below rather
    // than tracked per-line, so the frame stays a hair more generous than
    // pixel-tight - acceptable since it's also the drag hit-target.
    float ascentAboveY = 0.0f, descentBelowY = 0.0f;
    axisTextRenderer->textVerticalExtentVBottom(text.toStdString(), 1.0f, ascentAboveY, descentBelowY);

    // Offsets from the label's own screen position (baseX, baseY) - NOT
    // symmetric: text grows entirely rightward from baseX, and mostly
    // upward (ascent) with a small amount below (descent) from baseY, same
    // asymmetric shape frameScreenRectForLabel() actually draws/hit-tests.
    // draggedFrameFootprints() needs these exact offsets (not just a
    // width/height) so Fit-to-Screen's frame estimate isn't a symmetric box
    // centred on the label - that under/over-shoots on whichever side the
    // real frame is lopsided toward.
    const float top    = -(lineHeight * static_cast<float>(lineCount - 1) + ascentAboveY + kFramePadding);
    const float bottom = descentBelowY + kFramePadding;
    const float left   = -kFramePadding;
    const float right  = maxWidth + kFramePadding;

    return QRectF(static_cast<qreal>(left), static_cast<qreal>(top),
                  static_cast<qreal>(right - left), static_cast<qreal>(bottom - top));
}

QVector<QVector3D> AnnotationController::frameWorldCorners(const QVector3D& labelPos, const QString& text,
    Camera* camera, const QSize& viewportSize, TextRenderer* axisTextRenderer) const
{
    const QRectF frame = frameScreenRectForLabel(labelPos, text, camera, viewportSize, axisTextRenderer);
    if (!frame.isValid() || !camera)
        return {};

    const QRect viewportRect(0, 0, viewportSize.width(), viewportSize.height());
    const QVector3D labelProjected = labelPos.project(camera->getViewMatrix(), camera->getProjectionMatrix(), viewportRect);
    auto worldAtScreen = [&](float qtX, float qtY) -> QVector3D {
        const float glY = static_cast<float>(viewportSize.height()) - qtY;
        return QVector3D(qtX, glY, labelProjected.z()).unproject(camera->getViewMatrix(), camera->getProjectionMatrix(), viewportRect);
    };

    QVector<QVector3D> corners;
    corners.reserve(4);
    corners.append(worldAtScreen(static_cast<float>(frame.left()),  static_cast<float>(frame.top())));     // topLeft
    corners.append(worldAtScreen(static_cast<float>(frame.right()), static_cast<float>(frame.top())));     // topRight
    corners.append(worldAtScreen(static_cast<float>(frame.right()), static_cast<float>(frame.bottom())));  // bottomRight
    corners.append(worldAtScreen(static_cast<float>(frame.left()),  static_cast<float>(frame.bottom())));  // bottomLeft
    return corners;
}

void AnnotationController::setSelectedAnnotationIds(const QSet<QUuid>& ids)
{
    if (_selectedAnnotationIds == ids)
        return;
    _selectedAnnotationIds = ids;
    emit annotationStateChanged();
    emit annotationSelectionChanged(_selectedAnnotationIds);
}

bool AnnotationController::isEffectivelyVisible(const Annotation& a) const
{
    return _sceneRuntime.visibleSwapped() ? !a.visible : a.visible;
}

bool AnnotationController::hasHiddenAnnotations() const
{
    if (!_viewer || !_viewer->sceneGraph())
        return false;
    for (const Annotation& a : _viewer->sceneGraph()->annotations())
    {
        if (!a.visible)
            return true;
    }
    return false;
}

QVector<QVector3D> AnnotationController::visibleBoundsPoints() const
{
    QVector<QVector3D> points;
    if (!_viewer || !_viewer->sceneGraph())
        return points;

    for (const Annotation& a : _viewer->sceneGraph()->annotations())
    {
        if (!isEffectivelyVisible(a))
            continue;

        const QVector3D anchorPos = resolveAnnotationAnchor(a.anchor);
        points.append(anchorPos);

        // The label's position, but ONLY once the user has actually
        // dragged it (a.leaderOffset explicitly set). The UNDRAGGED default
        // direction/magnitude scales with camera->getViewRange()
        // (defaultLeaderOffsetMagnitude()) - exactly what Fit-to-Screen
        // computes - so folding it into bounds created a feedback loop
        // (confirmed - repeated F presses kept "refining" instead of
        // settling). Deliberately NOT using frameWorldCorners() here even
        // for a dragged note: it unprojects a FIXED-PIXEL-SIZE rect back to
        // world space using the CAMERA'S CURRENT view/projection matrices,
        // so the returned corners' WORLD-SPACE size scales with whatever
        // zoom the previous fit happened to land on - the same feedback
        // loop, just relocated here (also confirmed - this was still live
        // after the offsetVector fix above). frameWorldCorners() stays
        // correct and unchanged for its actual purpose, rendering
        // (drawAnnotationOverlay() below), where a zoom-relative on-screen
        // size is exactly what's wanted. labelPos alone under-represents a
        // wide/tall note's true footprint (it's a single point, not the
        // frame's extent) - draggedFrameFootprints() below is what restores
        // the frame's actual size to Fit-to-Screen, via a fixed-point
        // iteration in ViewportWidget rather than this camera-independent
        // (but coarser) point set.
        if (a.leaderOffset.lengthSquared() > 1.0e-10f)
            points.append(anchorPos + a.leaderOffset);
    }
    return points;
}

QVector<AnnotationController::DraggedFrameFootprint> AnnotationController::draggedFrameFootprints(TextRenderer* axisTextRenderer) const
{
    QVector<DraggedFrameFootprint> footprints;
    if (!_viewer || !_viewer->sceneGraph() || !axisTextRenderer)
        return footprints;

    for (const Annotation& a : _viewer->sceneGraph()->annotations())
    {
        if (!isEffectivelyVisible(a) || a.leaderOffset.lengthSquared() <= 1.0e-10f)
            continue;

        const QRectF offsetRect = frameScreenOffsetRect(a.text, axisTextRenderer);
        if (!offsetRect.isValid())
            continue;

        DraggedFrameFootprint fp;
        fp.labelPos = resolveAnnotationAnchor(a.anchor) + a.leaderOffset;
        fp.offsetRect = offsetRect;
        footprints.append(fp);
    }
    return footprints;
}

QUuid AnnotationController::hitTestAnnotation(const QPoint& pixel, Camera* camera, const QSize& viewportSize,
    TextRenderer* axisTextRenderer, int pixelRadius) const
{
    if (!camera || !_viewer || !_viewer->sceneGraph())
        return QUuid();

    const QRect viewportRect(0, 0, viewportSize.width(), viewportSize.height());
    auto toScreen = [&](const QVector3D& worldPos) -> QVector2D {
        const QVector3D projected = worldPos.project(camera->getViewMatrix(), camera->getProjectionMatrix(), viewportRect);
        // Same y-flip as MeasurementController::hitTestMeasurement() -
        // project() is OpenGL (bottom-up), pixel is Qt (top-down).
        return QVector2D(projected.x(), static_cast<float>(viewportSize.height()) - projected.y());
    };
    auto distPointToSegment = [](const QVector2D& p, const QVector2D& a, const QVector2D& b) -> float {
        const QVector2D ab = b - a;
        const float abLenSq = QVector2D::dotProduct(ab, ab);
        float t = abLenSq > 1.0e-6f ? QVector2D::dotProduct(p - a, ab) / abLenSq : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        return (p - (a + ab * t)).length();
    };

    const QPointF clickPtF(static_cast<qreal>(pixel.x()), static_cast<qreal>(pixel.y()));
    const QVector2D clickPt2(static_cast<float>(pixel.x()), static_cast<float>(pixel.y()));

    for (const Annotation& a : _viewer->sceneGraph()->annotations())
    {
        if (!isEffectivelyVisible(a))
            continue;

        // A recorded (authored) note is tested where it was drawn, not at the geometry it was recorded on.
        if (a.recordedDrawing)
        {
            const auto strokes = _recordedStrokes.constFind(a.id);
            if (strokes != _recordedStrokes.constEnd())
            {
                for (size_t i = 0; i + 1 < strokes->size(); i += 2)
                    if (distPointToSegment(clickPt2, toScreen((*strokes)[i]), toScreen((*strokes)[i + 1])) < static_cast<float>(pixelRadius))
                        return a.id;
                continue;
            }
        }

        const QVector3D anchorPos = resolveAnnotationAnchor(a.anchor);
        const QVector3D labelPos = anchorPos + resolveAnnotationLeaderOffset(a, camera);

        // Primary target: the rectangular frame drawn around the note's
        // text (see drawAnnotationOverlay()) - a much more generous,
        // discoverable grab area than a thin line alone (the ask that
        // motivated this: the old thin-leader hit test made a note
        // difficult to reliably select or drag).
        {
            // The frame is a quad in space; test the click against its projection.
            const InPlaneNote note = layoutInPlaneNote(a, labelPos, camera, viewportSize,
                axisTextRenderer ? axisTextRenderer->globalScale() : 1.0f);
            if (note.valid)
            {
                QPolygonF quad;
                for (const QVector3D& corner : note.corners)
                {
                    const QVector2D s = toScreen(corner);
                    quad << QPointF(double(s.x()), double(s.y()));
                }
                if (quad.containsPoint(clickPtF, Qt::OddEvenFill))
                    return a.id;
                if (distPointToSegment(clickPt2, toScreen(anchorPos), toScreen(note.corners[3])) < static_cast<float>(pixelRadius))
                    return a.id;
            }
            continue;
        }
        const QRectF frame = frameScreenRectForLabel(labelPos, a.text, camera, viewportSize, axisTextRenderer);
        if (frame.isValid() && frame.contains(clickPtF))
            return a.id;

        // Secondary: proximity to the anchor marker or the connecting
        // leader itself, within pixelRadius - covers axisTextRenderer being
        // transiently unavailable, and lets a click right on the pin (not
        // yet inside the frame) still grab it. Tests against the frame's
        // bottom-left corner (where the leader actually terminates - see
        // drawAnnotationOverlay()), not labelPos, so this agrees with what's
        // drawn - falls back to labelPos only when there's no frame to
        // anchor to.
        const QVector2D leaderEnd = frame.isValid()
            ? QVector2D(static_cast<float>(frame.left()), static_cast<float>(frame.bottom()))
            : toScreen(labelPos);
        if (distPointToSegment(clickPt2, toScreen(anchorPos), leaderEnd) < static_cast<float>(pixelRadius))
            return a.id;
    }

    return QUuid();
}

QUuid AnnotationController::hitTestAnnotationLeader(const QPoint& pixel, Camera* camera, const QSize& viewportSize,
    TextRenderer* axisTextRenderer, int pixelRadius) const
{
    // Same hit region as hitTestAnnotation() - for a note, the frame IS the
    // draggable part (unlike Measurement's separate marker/dimension-line
    // distinction - see hitTestAnnotation()'s doc comment). Kept as its own
    // method to mirror ViewportWidget's existing press-vs-drag dispatch
    // shape and leave room for a future distinction.
    const QUuid hit = hitTestAnnotation(pixel, camera, viewportSize, axisTextRenderer, pixelRadius);
    // A recorded note keeps the layout its author drew: it can be hovered and selected but has no leader to drag.
    const int index = hit.isNull() ? -1 : _viewer->sceneGraph()->annotationIndexById(hit);
    if (index >= 0 && _viewer->sceneGraph()->annotations().at(index).recordedDrawing)
        return QUuid();
    return hit;
}

void AnnotationController::updateHoverAnnotation(const QPoint& pixel, Camera* camera, const QSize& viewportSize, TextRenderer* axisTextRenderer)
{
    const QUuid hoveredLeader = hitTestAnnotationLeader(pixel, camera, viewportSize, axisTextRenderer, 8);
    _hoveredLeaderId = hoveredLeader;
    _hoveredAnnotationId = !hoveredLeader.isNull() ? QUuid() : hitTestAnnotation(pixel, camera, viewportSize, axisTextRenderer, 8);
}

void AnnotationController::beginAnnotationLeaderDrag(const QUuid& annotationId, Camera* camera)
{
    if (!camera || !_viewer || !_viewer->sceneGraph())
        return;

    const int index = _viewer->sceneGraph()->annotationIndexById(annotationId);
    if (index < 0)
        return;
    const Annotation& a = _viewer->sceneGraph()->annotations().at(index);

    _leaderDragPivot = resolveAnnotationAnchor(a.anchor);
    // Plane normal = camera view direction at drag start - a plane parallel
    // to the screen, passing through the anchor. Unlike
    // MeasurementController::beginDimensionLineDrag()'s Linear case (plane
    // normal = the measured segment's own direction, keeping the offset
    // perpendicular to it), there is no measured segment here, so the drag
    // is simply free within the screen-parallel plane.
    // A note laid out in a plane slides within that plane instead.
    const LabelPlane::Basis plane = annotationPlane(a);
    _leaderDragAxis = plane.valid ? plane.normal : camera->getViewDir().normalized();
    _leaderDragStartOffset = resolveAnnotationLeaderOffset(a, camera);
    _leaderDragActive = true;
}

void AnnotationController::updateAnnotationLeaderDrag(const QPoint& pixel, Camera* camera, const QSize& viewportSize)
{
    if (!camera || !_viewer || !_viewer->sceneGraph() || !_leaderDragActive)
        return;

    const QRect viewport(0, 0, viewportSize.width(), viewportSize.height());
    const QMatrix4x4 viewMatrix = camera->getViewMatrix();
    const QMatrix4x4 projMatrix = camera->getProjectionMatrix();

    // True ray/plane intersection - same unproject()/ray-cast technique as
    // MeasurementController::updateDimensionLineDrag()'s Linear case, but
    // no projecting-out step: the intersection point minus the pivot IS the
    // new offset vector directly, with full freedom within the screen-
    // parallel plane (see beginAnnotationLeaderDrag()'s doc comment).
    const int glX = pixel.x();
    const int glY = viewportSize.height() - pixel.y() - 1;  // Qt top-down -> GL bottom-up
    const QVector3D rayOrigin = QVector3D(static_cast<float>(glX), static_cast<float>(glY), 0.0f).unproject(viewMatrix, projMatrix, viewport);
    QVector3D rayDir = QVector3D(static_cast<float>(glX), static_cast<float>(glY), 1.0f).unproject(viewMatrix, projMatrix, viewport) - rayOrigin;
    if (rayDir.lengthSquared() < 1.0e-12f)
        return;
    rayDir.normalize();

    const float denom = QVector3D::dotProduct(rayDir, _leaderDragAxis);
    if (std::abs(denom) < 1.0e-6f)
        return;  // ray parallel to the plane - leave the offset unchanged this frame

    const float t = QVector3D::dotProduct(_leaderDragPivot - rayOrigin, _leaderDragAxis) / denom;
    if (t < 0.0f)
        return;  // intersection behind the camera - degenerate, ignore this frame

    const QVector3D hitPoint = rayOrigin + rayDir * t;
    QVector3D newOffset = hitPoint - _leaderDragPivot;

    // Floored magnitude, not allowed to collapse to ~0 - same reasoning as
    // updateDimensionLineDrag()'s equivalent clamp (a zero offset would put
    // the label on top of the anchor, and would also read back as
    // "undragged" on the next load, silently discarding the drag).
    const float mag = newOffset.length();
    constexpr float kMinOffsetMagnitude = 0.01f;
    if (mag < kMinOffsetMagnitude)
        newOffset = (mag > 1.0e-8f ? newOffset / mag : _leaderDragAxis) * kMinOffsetMagnitude;

    _viewer->sceneGraph()->setAnnotationLeaderOffset(_leaderDragCandidateId, newOffset);
    emit annotationStateChanged();
}

void AnnotationController::finishAnnotationLeaderDrag(ViewportWidget* viewportWidget)
{
    if (_leaderDragActive && _viewer && _viewer->sceneGraph())
    {
        const int index = _viewer->sceneGraph()->annotationIndexById(_leaderDragCandidateId);
        const Annotation* aa = (index >= 0) ? &_viewer->sceneGraph()->annotations().at(index) : nullptr;

        // Redundant re-apply of the same final value on redo() (it's already
        // live from the drag) but establishes the undo edge - same "one
        // command on release" shape as MeasurementController::
        // finishDimensionLineDrag(). viewportWidget is passed through purely
        // for the shared ModelViewerCommand base's repaint trigger - not
        // stored, same one exception MeasurementController makes.
        const QVector3D finalOffset = aa ? aa->leaderOffset : _leaderDragStartOffset;
        if ((finalOffset - _leaderDragStartOffset).lengthSquared() > 1.0e-10f && _viewer->getUndoStack())
        {
            _viewer->getUndoStack()->push(new AnnotationOffsetCommand(_viewer, viewportWidget,
                _leaderDragCandidateId, _leaderDragStartOffset, finalOffset));
        }
    }

    _leaderDragActive = false;
    _leaderDragCandidate = false;
    _leaderDragCandidateId = QUuid();
}

bool AnnotationController::captureGraphic(const QUuid& id, Camera* camera, const QSize& viewportSize, OverlayGraphic* out)
{
    if (!out || !camera)
        return false;
    const auto savedStrokes = _recordedStrokes;   // the draw refills the live viewport's hit-testing state; keep it as it was

    OverlayGraphic graphic;
    drawAnnotationOverlay(camera, viewportSize, nullptr, &graphic, id);

    _recordedStrokes = savedStrokes;
    if (graphic.lines.empty() && graphic.triangles.empty())
        return false;
    graphic.id = id;
    graphic.annotation = true;
    *out = std::move(graphic);
    return true;
}

QVector<QUuid> AnnotationController::visibleAnnotationIds() const
{
    QVector<QUuid> ids;
    if (!_viewer || !_viewer->sceneGraph())
        return ids;
    for (const Annotation& a : _viewer->sceneGraph()->annotations())
    {
        if (isEffectivelyVisible(a))
            ids.append(a.id);
    }
    return ids;
}

void AnnotationController::drawAnnotationOverlay(Camera* camera, const QSize& viewportSize, TextRenderer* axisTextRenderer,
    OverlayGraphic* capture, const QUuid& captureId)
{
    _recordedStrokes.clear();
    if (!camera || !_viewer || !_viewer->sceneGraph())
        return;
    // a capture touches no OpenGL state, so it needs neither the GL functions nor the text renderer
    if (!capture && (!_glFunctionsInitialized || !axisTextRenderer || !_renderCtrl.axisShader()))
        return;

    const QVector<Annotation>& annotations = _viewer->sceneGraph()->annotations();
    if (annotations.isEmpty() && !(_annotationToolArmed && _annotationHoverAnchor.isValid()))
        return;

    struct LabelEntry { QVector3D worldPos; QString text; };
    std::vector<float> lineVertices;
    std::vector<float> fillVertices;  // filled letters of recorded notes, drawn as triangles after the lines
    QVector<LabelEntry> labels;

    const QVector3D noteColor(1.0f, 0.85f, 0.3f);       // sticky-note yellow - visually distinct from Measurement's palette
    const QVector3D hoverSnapColor(0.25f, 1.0f, 0.35f); // will place here (placement preview)
    const QVector3D selectedColor(1.0f, 0.35f, 0.05f);  // orange - same convention as Measurement's selectedColor

    auto addSegment = [&lineVertices](const QVector3D& a, const QVector3D& b, const QVector3D& color) {
        lineVertices.insert(lineVertices.end(), { a.x(), a.y(), a.z(), color.x(), color.y(), color.z() });
        lineVertices.insert(lineVertices.end(), { b.x(), b.y(), b.z(), color.x(), color.y(), color.z() });
    };

    // Marker cross size scales with the camera's current view range - same
    // technique as MeasurementController::drawMeasurementOverlay().
    const float markerSize = std::max(camera->getViewRange(), 0.0001f) * 0.01f;
    auto addMarker = [&](const QVector3D& p, const QVector3D& color, float sizeMultiplier = 1.0f) {
        const float s = markerSize * sizeMultiplier;
        addSegment(p - QVector3D(s, 0, 0), p + QVector3D(s, 0, 0), color);
        addSegment(p - QVector3D(0, s, 0), p + QVector3D(0, s, 0), color);
        addSegment(p - QVector3D(0, 0, s), p + QVector3D(0, 0, s), color);
    };

    for (const Annotation& a : annotations)
    {
        if (capture ? a.id != captureId : !isEffectivelyVisible(a))
            continue;

        const bool isSelected = !capture && _selectedAnnotationIds.contains(a.id);
        // Selection is the stronger cue - see MeasurementController::
        // drawMeasurementOverlay()'s identical reasoning.
        const bool isHovered = !capture && !isSelected && (a.id == _hoveredAnnotationId || a.id == _hoveredLeaderId);

        QVector3D color = isSelected ? selectedColor : noteColor;
        if (isHovered)
            color = color * 0.5f + QVector3D(1.0f, 1.0f, 1.0f) * 0.5f;  // blend toward white - lighter preview than full selection
        const float sizeMultiplier = isSelected ? 1.5f : (isHovered ? 1.25f : 1.0f);

        // A recorded annotation is drawn as its author drew it, in its own annotation plane (leader, symbol and text strokes
        // from the file), instead of with the viewer's marker, frame, leader and camera-facing label.
        if (a.recordedDrawing)
        {
            if (SceneMesh* strokeOwner = getMeshByUuid(a.recordedDrawing->meshUuid))
            {
                const size_t strokeStart = lineVertices.size();
                // The author's colour, unless the item is selected (selection must read); hover lightens it as it does any colour.
                QVector3D strokeColor = color;
                if (a.recordedDrawing->hasColor && !isSelected)
                    strokeColor = isHovered ? a.recordedDrawing->color * 0.5f + QVector3D(1.0f, 1.0f, 1.0f) * 0.5f : a.recordedDrawing->color;
                RecordedPmi::appendStrokes(*a.recordedDrawing, strokeOwner->combinedRenderTransform(), camera->getRenderPosition(),
                    strokeColor, lineVertices, &fillVertices);  // authored size: a symbol's text must stay inside its circle
                _recordedStrokes.insert(a.id, RecordedPmi::strokeSegments(lineVertices, strokeStart));
                continue;
            }
        }

        const QVector3D anchorPos = resolveAnnotationAnchor(a.anchor);
        const QVector3D labelPos = anchorPos + resolveAnnotationLeaderOffset(a, camera);

        addMarker(anchorPos, color, sizeMultiplier);

        {
            // Frame, leader and text all lie in the note's plane (the camera's own plane for a note that faces the screen).
            const InPlaneNote note = layoutInPlaneNote(a, labelPos, camera, viewportSize,
                axisTextRenderer ? axisTextRenderer->globalScale() : 1.0f);
            if (note.valid)
            {
                addSegment(anchorPos, note.corners[3], color);
                for (int i = 0; i < 4; ++i)
                    addSegment(note.corners[i], note.corners[(i + 1) % 4], color);
                const QStringList noteLines = a.text.split(QChar('\n'));
                for (int i = 0; i < noteLines.size(); ++i)
                {
                    const float lift = note.capHeight * 1.6f * float(noteLines.size() - 1 - i);
                    InPlaneText::appendHaloText(noteLines[i], labelPos + note.y * lift, note.x, note.y, note.capHeight, color,
                        QVector3D(0.06f, 0.06f, 0.08f), fillVertices, 1.0f);
                }
                continue;
            }
        }

        // Rectangular frame around the note's text - the primary grab
        // target for repositioning it (see hitTestAnnotation()'s doc
        // comment), via frameWorldCorners(). NOT used by
        // visibleBoundsPoints() - see that method's doc comment for why.
        //
        // The leader terminates exactly at the frame's bottom-left corner
        // (not at labelPos, which sits near but not exactly there) - a
        // clean CAD-callout look.
        const QVector<QVector3D> frameCorners = frameWorldCorners(labelPos, a.text, camera, viewportSize, axisTextRenderer);
        if (frameCorners.size() == 4)
        {
            const QVector3D& topLeft     = frameCorners.at(0);
            const QVector3D& topRight    = frameCorners.at(1);
            const QVector3D& bottomRight = frameCorners.at(2);
            const QVector3D& bottomLeft  = frameCorners.at(3);
            addSegment(anchorPos, bottomLeft, color);
            addSegment(topLeft, topRight, color);
            addSegment(topRight, bottomRight, color);
            addSegment(bottomRight, bottomLeft, color);
            addSegment(bottomLeft, topLeft, color);
        }
        else
        {
            // No text renderer available this frame (rare) - no frame to
            // anchor to, fall back to the label's own reference point.
            addSegment(anchorPos, labelPos, color);
        }

        labels.append({ labelPos, a.text });
    }

    if (capture)
    {
        // the geometry of the one annotation (filled letters included), no live placement preview and no drawing
        capture->lines = std::move(lineVertices);
        capture->triangles = std::move(fillVertices);
        return;
    }

    // Live placement preview while the tool is armed - mirrors
    // MeasurementController's hover-anchor preview marker.
    if (_annotationToolArmed && _annotationHoverAnchor.isValid())
        addMarker(_annotationHoverAnchor.worldPosition, hoverSnapColor, 1.25f);

    // Never hidden behind shaded model surfaces - same reasoning as
    // MeasurementController::drawMeasurementOverlay(): a note pointing at a
    // spot needs to stay legible regardless of what's in front of it.
    const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_DEPTH_TEST);

    if (!lineVertices.empty())
    {
        // Mirrors MeasurementController::drawMeasurementOverlay()'s exact
        // upload/draw pattern, using the dedicated annotation-overlay buffer.
        const GLsizei lineCount = static_cast<GLsizei>(lineVertices.size() / 6);
        const GLsizei fillCount = static_cast<GLsizei>(fillVertices.size() / 6);
        lineVertices.insert(lineVertices.end(), fillVertices.begin(), fillVertices.end());
        _renderCtrl.initAnnotationOverlayGeometry(lineVertices);
        glBindVertexArray(_renderCtrl.annotationOverlayVAO());
        glBindBuffer(GL_ARRAY_BUFFER, _renderCtrl.annotationOverlayVBO());
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
        glLineWidth(OverlayLineWeights::thinLine);
        glDrawArrays(GL_LINES, 0, lineCount);
        if (fillCount > 0)
            glDrawArrays(GL_TRIANGLES, lineCount, fillCount);
        glLineWidth(1.0f);
        _renderCtrl.axisShader()->release();

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    if (depthWasEnabled)
        glEnable(GL_DEPTH_TEST);

    if (axisTextRenderer)
    {
        const QRect viewportRect(0, 0, viewportSize.width(), viewportSize.height());
        // Multi-line notes stack upward from the label position, same
        // pattern as MeasurementController::drawMeasurementOverlay() uses
        // for Pitch Circle's two-line summary - see TextRenderer.h's doc
        // comment (RenderText() has no built-in line-break support).
        const float lineHeight = static_cast<float>(axisTextRenderer->fontSize()) * 1.2f;
        for (const LabelEntry& entry : labels)
        {
            const QVector3D projected = entry.worldPos.project(
                camera->getViewMatrix(), camera->getProjectionMatrix(), viewportRect);
            const float baseY = viewportSize.height() - projected.y();

            const QStringList textLines = entry.text.split(QChar('\n'));
            for (int i = 0; i < textLines.size(); ++i)
            {
                const float y = baseY - lineHeight * static_cast<float>(textLines.size() - 1 - i);
                axisTextRenderer->RenderHaloText(textLines[i].toStdString(),
                    projected.x(), y, 1,
                    QVector3D(1.0f, 1.0f, 1.0f), TextRenderer::VAlignment::VBOTTOM);
            }
        }
    }
}
