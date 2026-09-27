#pragma once

#include <QVector3D>
#include <QColor>
#include <array>
#include <functional>

class PlaneRenderable;
class QOpenGLShaderProgram;

// A single draggable, translucent world-space plane/face - the shared
// primitive behind both the Clipping Planes gizmo (3 independent instances,
// one per axis) and the Filter by Bounding Box gizmo (6 instances, one per
// face, faces just happen to be positioned to look like a box - no shared/
// fused box mesh, overlap at edges is fine).
//
// Deliberately NOT a QObject/IGpuContextResource and NOT derived from
// TransformGizmo: TransformGizmo's drag-session state (ViewportInteractionController's
// per-mesh-id transform maps) writes straight into SceneMesh and isn't
// reusable for a clip coefficient or a dialog's spin box. This class only
// knows how to draw and hit-test ONE plane; it has no opinion about what a
// drag "means" to its owner.
//
// Owns one PlaneRenderable for its translucent fill quad plus 4 more for a
// thin solid border frame traced around its own edges (this codebase has no
// cheap "just draw a line loop" path - RenderableMesh::render() always issues
// GL_TRIANGLES draw calls, and the existing wireframe shader is wired to a
// mesh's own precomputed edge-index-buffer, not arbitrary points - so the
// border is 4 thin quad strips using the exact same PlaneRenderable/Material
// pipeline as the fill, not a true hairline). None of the 5 are constructed
// or GPU-registered by this class itself - the caller (ViewportWidget)
// constructs them all with the general scene shader (_renderCtrl.fgShader(),
// the same shader/opacity path _floorPlane already uses for its own
// translucent draw) and registers each via registerDecorationGpuResource()
// BEFORE handing them to this constructor, since that registry is private to
// ViewportWidget. This class owns and deletes all 5 afterward.
class PlaneGizmo
{
public:
	// Which world axis this plane's normal is fixed to - the one degree of
	// freedom a drag can move it along.
	enum class Axis { X, Y, Z };

	// Visual state, mutually exclusive - Dragging takes priority over
	// Hovered (set by ViewportWidget's drag begin/end), which takes priority
	// over Default. Each has its own fill/border color pair so the gizmo the
	// user is about to grab (Hovered) or is actively moving (Dragging) reads
	// clearly apart from the other, currently-idle gizmos (Default).
	enum class VisualState { Default, Hovered, Dragging };

	// shader: the SAME QOpenGLShaderProgram* every one of fillRenderable/
	// borderRenderables was already constructed/registered with - re-passed
	// here because PlaneRenderable exposes no getter for its own program,
	// and every setPlane() call (reposition() below) needs to supply it
	// again. defaultColor is the gizmo's own idle tint (e.g. matching this
	// axis's cap-fill hatch base color); hoverColor/dragColor are shown
	// instead while setHovered()/setDragging() are active.
	PlaneGizmo(Axis axis, PlaneRenderable* fillRenderable,
	           std::array<PlaneRenderable*, 4> borderRenderables,
	           QOpenGLShaderProgram* shader,
	           const QColor& defaultColor, const QColor& hoverColor, const QColor& dragColor);
	~PlaneGizmo();

	Axis axis() const { return _axis; }
	// World-space coordinate along axis(), as of the last reposition() call.
	float position() const { return _position; }
	// Unit world-space direction of axis() - (1,0,0)/(0,1,0)/(0,0,1).
	QVector3D axisDirection() const
	{
		return _axis == Axis::X ? QVector3D(1, 0, 0) : _axis == Axis::Y ? QVector3D(0, 1, 0) : QVector3D(0, 0, 1);
	}

	bool isVisible() const { return _visible; }
	void setVisible(bool visible) { _visible = visible; }

	// Hover/drag state - purely visual (color selection in render()), no
	// effect on hit-testing or geometry. Dragging implies (and visually
	// overrides) Hovered; ViewportWidget is expected to call setHovered(true)
	// once a drag begins anyway (the cursor is still over the gizmo), but
	// applyVisualState() resolves the priority either way.
	void setHovered(bool hovered);
	void setDragging(bool dragging);

	// Full world-space center of the quad, as of the last reposition() call
	// (average of its 4 corners) - unlike position(), which only carries the
	// ONE coordinate along axis(), this is needed as the drag-projection
	// pivot so a bounding-box face whose center sits away from the world
	// origin on its other two axes still projects correctly.
	QVector3D worldCenter() const { return (_corners[0] + _corners[1] + _corners[2] + _corners[3]) * 0.25f; }

	// Diagonal of the quad's current corners - used by ViewportWidget's drag
	// code as the reference world-space distance for the screen-space-
	// projection drag formula (see updatePlaneGizmoDrag()'s own doc
	// comment). Scales naturally with the gizmo's own current size instead
	// of a fixed constant that would be wrong for a scene many orders of
	// magnitude larger or smaller than whatever it was tuned against.
	float dragScaleReference() const { return (_corners[2] - _corners[0]).length(); }

	// Rebuilds the quad (and its border frame) centered at worldCenter (only
	// the component along axis() matters for position() afterward - the
	// other two components place the quad's center on its own face, which
	// callers need for the Filter by Bounding Box case where that center
	// moves as other limits change). extentA/extentB size the quad on the
	// OTHER two axes, in fixed cyclic order (X->Y->Z->X): for Axis::X,
	// (Y-extent, Z-extent); for Axis::Y, (Z-extent, X-extent); for Axis::Z,
	// (X-extent, Y-extent).
	void reposition(const QVector3D& worldCenter, float extentA, float extentB);

	// Real ray-plane intersection, NOT a screen-space polygon test - with
	// multiple large, mutually-overlapping gizmo planes on screen at once
	// (the common case - several clipping axes enabled together), a
	// screen-space "does the 2D quad silhouette contain this pixel" test
	// can true-positive for several planes at the same click and had no way
	// to prefer whichever one the user actually meant (confirmed real bug -
	// it always returned the first array match regardless of depth).
	// Intersects the infinite plane (normal = axisDirection(), passing
	// through worldCenter()), rejects a hit outside this gizmo's own
	// current rectangular extent (checked directly against the last
	// reposition() call's corner bounds on the two in-plane axes - exact,
	// not an approximation, since these quads are always axis-aligned), and
	// reports the hit distance along the ray so the caller can pick the
	// CLOSEST of several valid hits, same as any normal z-buffered pick.
	bool hitTestRay(const QVector3D& rayOrigin, const QVector3D& rayDir, float& outDistance) const;

	// Draws the owned fill quad, then the 4 border strips. No caller-side
	// GL_BLEND bracket needed - RenderableMesh::render() (which
	// PlaneRenderable uses as-is) already enables/disables GL_BLEND itself
	// based on each one's material opacity, unlike most other transparent-
	// draw call sites in ViewportWidget.cpp that manage it by hand. Caller
	// is still responsible for binding the shader beforehand.
	void render();

	// Invoked by ViewportWidget's drag-update code with the gizmo's new
	// position (world units along axis()) as the drag progresses. Set by
	// whichever owner (Clipping Planes editor vs. Filter by Bounding Box
	// dialog) created this instance - e.g. the clipping-plane owner
	// translates this raw world position into the correct signed clip
	// coefficient (accounting for that axis's flip state) and calls
	// ViewportWidget::setClippingXCoeff(); the bounding-box owner clamps it
	// against the paired min/max spin box and calls setValue() on its own.
	// ViewportWidget's drag code never hardcodes either behavior itself.
	std::function<void(float)> onDragged;

	// Invoked once by ViewportWidget::beginPlaneGizmoDrag() right as a drag
	// begins (before the first onDragged call) - the owner uses this to
	// snapshot "the value before this drag" for an undo command it will push
	// in onDragFinished below. Kept separate from onDragged (which fires
	// every mouse-move frame with no undo involvement, for live preview -
	// same pattern as TransformGizmo's own drag mutation) so the owner isn't
	// left guessing which of many onDragged calls was the FIRST one.
	std::function<void()> onDragStarted;

	// Invoked once by ViewportWidget::finishPlaneGizmoDrag() right as a drag
	// ends (mouse release) - the owner pushes exactly one undo command here
	// (e.g. PlaneGizmoDragCommand), using the value captured in
	// onDragStarted and whatever onDragged last reported. A drag that never
	// actually changed the value (e.g. a click with no movement) is still
	// safe to push - QUndoCommand doesn't require the old/new values differ,
	// though an owner is free to skip the push itself if they're equal.
	std::function<void()> onDragFinished;

private:
	void applyVisualState();

	Axis _axis;
	PlaneRenderable* _fillRenderable; // owned
	std::array<PlaneRenderable*, 4> _borderRenderables; // owned - order: top, bottom, left, right (local axes, pre-rotation)
	QOpenGLShaderProgram* _shader; // not owned - re-supplied to setPlane() on every reposition()
	QColor _defaultColor;
	QColor _hoverColor;
	QColor _dragColor;
	VisualState _state = VisualState::Default;
	float _position = 0.0f;
	bool _visible = false;
	QVector3D _corners[4]; // world-space, set by reposition(), read by hitTest()
};
