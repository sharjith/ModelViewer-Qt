#include "PlaneGizmo.h"
#include "PlaneRenderable.h"
#include "Material.h"

#include <algorithm>
#include <cmath>

namespace
{
	// Fixed, sign-independent 90-degree reorientations from the plane's base
	// build orientation (Plane::Orientation::XY_ZNormal - local plane spans
	// local X/Y, local normal along local Z) to each world axis. Unlike the
	// existing clip-plane cap-fill rendering (ViewportWidget.cpp's
	// yAng/xAng/zAng), this gizmo has no front/back-facing concern - it's a
	// flat, double-sided translucent handle, not a stencil-technique cap
	// surface - so a fixed rotation per axis is enough; no flip-state
	// dependence needed.
	QMatrix4x4 axisRotation(PlaneGizmo::Axis axis)
	{
		QMatrix4x4 m;
		switch (axis)
		{
		case PlaneGizmo::Axis::Z:
			break; // base orientation's normal is already +Z
		case PlaneGizmo::Axis::Y:
			m.rotate(-90.0f, 1.0f, 0.0f, 0.0f); // +Z normal -> +Y normal
			break;
		case PlaneGizmo::Axis::X:
			m.rotate(90.0f, 0.0f, 1.0f, 0.0f); // +Z normal -> +X normal
			break;
		}
		return m;
	}

	// A low Material::opacity() alone only decides whether RenderableMesh::render()
	// enables GL_BLEND (see its opacity()<1.0f check) - the FRAGMENT SHADER
	// (main_scene.frag) has its own, separate gate that ignores the opacity
	// uniform entirely and forces finalAlpha=1.0 whenever blendMode==Opaque
	// (the default), so Alpha blend mode must be requested explicitly.
	Material makeMaterial(const QColor& color, float opacity, float emissiveScale)
	{
		const QVector3D colorVec(static_cast<float>(color.redF()),
		                          static_cast<float>(color.greenF()),
		                          static_cast<float>(color.blueF()));
		Material material(colorVec, colorVec, QVector3D(0, 0, 0), colorVec * emissiveScale,
		                   1.0f, false, opacity);
		material.setBlendMode(Material::BlendMode::Alpha);
		return material;
	}
}

PlaneGizmo::PlaneGizmo(Axis axis, PlaneRenderable* fillRenderable,
                       std::array<PlaneRenderable*, 4> borderRenderables,
                       QOpenGLShaderProgram* shader,
                       const QColor& defaultColor, const QColor& hoverColor, const QColor& dragColor)
	: _axis(axis)
	, _fillRenderable(fillRenderable)
	, _borderRenderables(borderRenderables)
	, _shader(shader)
	, _defaultColor(defaultColor)
	, _hoverColor(hoverColor)
	, _dragColor(dragColor)
{
	applyVisualState();
}

PlaneGizmo::~PlaneGizmo()
{
	delete _fillRenderable;
	for (PlaneRenderable* border : _borderRenderables)
		delete border;
}

void PlaneGizmo::setHovered(bool hovered)
{
	if (_state == VisualState::Dragging)
		return; // dragging always wins - see the enum's own doc comment
	const VisualState next = hovered ? VisualState::Hovered : VisualState::Default;
	if (next == _state)
		return;
	_state = next;
	applyVisualState();
}

void PlaneGizmo::setDragging(bool dragging)
{
	const VisualState next = dragging ? VisualState::Dragging
		: VisualState::Default; // caller (ViewportWidget) re-asserts Hovered afterward if the cursor is still over it
	if (next == _state)
		return;
	_state = next;
	applyVisualState();
}

void PlaneGizmo::applyVisualState()
{
	// Fill stays subtle (low opacity) in every state so the model behind it
	// always reads through; border is the primary state indicator (opaque
	// enough to trace the plane's own extent clearly), brightening further
	// for hover/drag on top of switching to that state's own color. The
	// border stays plain black in every state (a neutral outline, like a
	// CAD sketch/section line) rather than tracking the fill's own hue -
	// only its opacity/emissive step up for hover/drag, matching how a
	// selected outline gets bolder without changing color.
	QColor fillColor = _defaultColor;
	float fillOpacity = 0.12f;
	float fillEmissive = 0.15f;
	float borderOpacity = 0.55f;
	float borderEmissive = 0.0f;
	switch (_state)
	{
	case VisualState::Hovered:
		fillColor = _hoverColor;
		fillOpacity = 0.22f;
		borderOpacity = 0.75f;
		break;
	case VisualState::Dragging:
		fillColor = _dragColor;
		fillOpacity = 0.32f;
		borderOpacity = 0.95f;
		break;
	case VisualState::Default:
	default:
		break;
	}

	if (_fillRenderable)
		_fillRenderable->setMaterial(makeMaterial(fillColor, fillOpacity, fillEmissive));
	const Material borderMaterial = makeMaterial(Qt::black, borderOpacity, borderEmissive);
	for (PlaneRenderable* border : _borderRenderables)
	{
		if (border)
			border->setMaterial(borderMaterial);
	}
}

void PlaneGizmo::reposition(const QVector3D& worldCenter, float extentA, float extentB)
{
	_position = _axis == Axis::X ? worldCenter.x() : _axis == Axis::Y ? worldCenter.y() : worldCenter.z();

	// World-space half-extents on the quad's own two in-plane axes, in the
	// fixed cyclic order documented on reposition()'s own declaration.
	const float halfA = extentA * 0.5f;
	const float halfB = extentB * 0.5f;
	switch (_axis)
	{
	case Axis::X:
		// In-plane axes are Y (A) and Z (B).
		_corners[0] = worldCenter + QVector3D(0, -halfA, -halfB);
		_corners[1] = worldCenter + QVector3D(0,  halfA, -halfB);
		_corners[2] = worldCenter + QVector3D(0,  halfA,  halfB);
		_corners[3] = worldCenter + QVector3D(0, -halfA,  halfB);
		break;
	case Axis::Y:
		// In-plane axes are Z (A) and X (B).
		_corners[0] = worldCenter + QVector3D(-halfB, 0, -halfA);
		_corners[1] = worldCenter + QVector3D(-halfB, 0,  halfA);
		_corners[2] = worldCenter + QVector3D( halfB, 0,  halfA);
		_corners[3] = worldCenter + QVector3D( halfB, 0, -halfA);
		break;
	case Axis::Z:
		// In-plane axes are X (A) and Y (B).
		_corners[0] = worldCenter + QVector3D(-halfA, -halfB, 0);
		_corners[1] = worldCenter + QVector3D( halfA, -halfB, 0);
		_corners[2] = worldCenter + QVector3D( halfA,  halfB, 0);
		_corners[3] = worldCenter + QVector3D(-halfA,  halfB, 0);
		break;
	}

	if (!_fillRenderable)
		return;

	// xsize/ysize passed to setPlane() are the base (pre-rotation)
	// XY_ZNormal orientation's own LOCAL extents (local X, local Y) - NOT
	// simply (extentA, extentB) in that order, because axisRotation() maps
	// local X/local Y onto DIFFERENT world axes depending on _axis, and that
	// mapping doesn't always line up with the (extentA, extentB) world-axis
	// order the corners above use:
	//  - Axis::Z (identity):       local X -> world X, local Y -> world Y
	//  - Axis::Y (-90 deg about X): local X -> world X, local Y -> world Z
	//  - Axis::X (+90 deg about Y): local X -> world Z, local Y -> world Y
	// For Z, local X/Y already line up with (extentA=X, extentB=Y). For X
	// and Y, local X/Y land on the OTHER world axis than a naive passthrough
	// would assume, so (xsize, ysize) must be swapped to (extentB, extentA)
	// - passing (extentA, extentB) unconditionally (as an earlier version of
	// this code did) silently swapped the rendered quad's two in-plane
	// dimensions relative to hitTestRay()'s corners for any non-cubical
	// scene, so clicks would hit/miss in the wrong places.
	const bool swapExtents = (_axis == Axis::X || _axis == Axis::Y);
	const float xsize = swapExtents ? extentB : extentA;
	const float ysize = swapExtents ? extentA : extentB;
	_fillRenderable->setPlane(_shader, QVector3D(0, 0, 0), xsize, ysize, 1, 1);
	QMatrix4x4 model;
	model.translate(worldCenter);
	model *= axisRotation(_axis);
	_fillRenderable->setSceneRenderTransformFast(model);

	// Border frame: 4 thin strips traced along the fill quad's own LOCAL
	// edges (same local pre-rotation space as the fill's own setPlane() call
	// above, same axisRotation()+translate transform, exactly coplanar with
	// the fill - zlevel=0 for both). An earlier version nudged the border a
	// hair along local Z to dodge z-fighting with the fill, but that nudge
	// is a FIXED world-space offset to one side of the plane, so the border
	// was only in front of (not occluded by) the fill when viewed from that
	// one side - from the other side the coplanar-but-behind border lost
	// the depth test and vanished entirely. The real fix is
	// ViewportWidget::renderPlaneGizmos() disabling depth WRITE (not test)
	// for this whole draw, so coincident fill/border fragments never
	// occlude each other regardless of view direction - see its own doc
	// comment.
	if (_borderRenderables[0] && _borderRenderables[1] && _borderRenderables[2] && _borderRenderables[3])
	{
		const float thickness = std::max(std::min(xsize, ysize) * 0.006f, 1.0e-4f);
		const float halfX = xsize * 0.5f;
		const float halfY = ysize * 0.5f;
		// top, bottom, left, right - matches the header's documented order.
		const QVector3D localCenters[4] = {
			QVector3D(0.0f,  halfY - thickness * 0.5f, 0.0f),
			QVector3D(0.0f, -halfY + thickness * 0.5f, 0.0f),
			QVector3D(-halfX + thickness * 0.5f, 0.0f, 0.0f),
			QVector3D( halfX - thickness * 0.5f, 0.0f, 0.0f),
		};
		const float stripXSize[4] = { xsize, xsize, thickness, thickness };
		const float stripYSize[4] = { thickness, thickness, ysize, ysize };
		for (int i = 0; i < 4; ++i)
		{
			_borderRenderables[i]->setPlane(_shader, QVector3D(localCenters[i].x(), localCenters[i].y(), 0.0f),
				stripXSize[i], stripYSize[i], 1, 1, 0.0f);
			_borderRenderables[i]->setSceneRenderTransformFast(model);
		}
	}
}

bool PlaneGizmo::hitTestRay(const QVector3D& rayOrigin, const QVector3D& rayDir, float& outDistance) const
{
	if (!_visible)
		return false;

	const QVector3D normal = axisDirection();
	const float denom = QVector3D::dotProduct(rayDir, normal);
	if (std::abs(denom) < 1.0e-6f)
		return false; // ray parallel to (or grazing) the plane - no well-defined hit

	const QVector3D planePoint = worldCenter();
	const float t = QVector3D::dotProduct(planePoint - rayOrigin, normal) / denom;
	if (t < 0.0f)
		return false; // plane is behind the ray origin

	const QVector3D hit = rayOrigin + rayDir * t;

	// Reject if outside this gizmo's own current rectangular extent - exact
	// min/max against the cached corners (always axis-aligned by
	// construction - see reposition()), checked on whichever two axes
	// aren't this gizmo's own fixed axis.
	auto within = [](float v, float a, float b, float c, float d) {
		const float lo = std::min({ a, b, c, d });
		const float hi = std::max({ a, b, c, d });
		return v >= lo && v <= hi;
	};
	bool inBounds = true;
	if (_axis != Axis::X)
		inBounds = inBounds && within(hit.x(), _corners[0].x(), _corners[1].x(), _corners[2].x(), _corners[3].x());
	if (_axis != Axis::Y)
		inBounds = inBounds && within(hit.y(), _corners[0].y(), _corners[1].y(), _corners[2].y(), _corners[3].y());
	if (_axis != Axis::Z)
		inBounds = inBounds && within(hit.z(), _corners[0].z(), _corners[1].z(), _corners[2].z(), _corners[3].z());
	if (!inBounds)
		return false;

	outDistance = t;
	return true;
}

void PlaneGizmo::render()
{
	if (!_visible)
		return;
	// _fillRenderable/_borderRenderables are plain RenderableMesh instances,
	// not SceneMesh - they don't participate in SceneMesh's shared material-
	// uniform-signature cache (see SceneMesh::resetSharedUniformStateCache()'s
	// existing call sites, e.g. ViewportWidget's floor-plane rendering).
	// Their own _uniformsDirty flag only forces a re-upload once (the first-
	// ever render()), so without this they'd silently reuse whatever
	// material another RenderableMesh/SceneMesh most recently uploaded into
	// the SAME shared fgShader - forcing them dirty every frame guarantees
	// they always republish their own current-state material instead of
	// inheriting a stale (often fully opaque) one.
	if (_fillRenderable)
	{
		_fillRenderable->markUniformsDirty();
		_fillRenderable->render();
	}
	for (PlaneRenderable* border : _borderRenderables)
	{
		if (!border)
			continue;
		border->markUniformsDirty();
		border->render();
	}
}
