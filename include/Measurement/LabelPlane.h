#pragma once

#include <QMatrix4x4>
#include <QRect>
#include <QVector3D>
#include <cmath>

// How the label of a measurement or annotation is oriented.
//   ModelPlane - lies in the principal plane (XY, XZ or YZ) that faced the view it was made from: stays with the model's faces and
//                turns with the model.
//   ViewPlane  - lies in the plane of the camera view it was made from: fixed in space, turns with the model.
//   FaceCamera - always faces the screen.
enum class LabelOrientation : int
{
	ModelPlane = 0,
	ViewPlane = 1,
	FaceCamera = 2
};

namespace LabelPlane
{
// Both label sizes below are multiplied by the Overlay Text Scale setting (TextRenderer::globalScale()).

// Cap height, in pixels, of a label that faces the screen: it keeps this size on screen however the view is zoomed.
constexpr float BillboardCapPixels = 8.0f;

// Cap height of a label that lies in a plane, as a multiple of the marker size (1% of the view range), so it scales with the view.
constexpr float InPlaneCapScale = 1.25f;

// The world length of one screen pixel at `position`, for any projection.
inline float worldPerPixel(const QVector3D& position, const QMatrix4x4& view, const QMatrix4x4& projection, const QRect& viewport)
{
	const QVector3D here = position.project(view, projection, viewport);
	const QVector3D next = QVector3D(here.x() + 1.0f, here.y(), here.z()).unproject(view, projection, viewport);
	const float length = (next - position).length();
	return length > 1.0e-9f ? length : 1.0e-6f;
}

// A right-handed frame in a label's plane: unit vectors, x cross y = normal.
struct Basis
{
	QVector3D x, y, normal;
	bool valid = false;
};

// The screen-right direction of the view the label was made from, derived from the stored view direction with the model's Z axis as
// up (Y when looking along Z), so it is stable while orbiting and needs no extra stored data.
inline QVector3D creationRight(const QVector3D& viewDir)
{
	const QVector3D v = viewDir.normalized();
	QVector3D right = QVector3D::crossProduct(v, QVector3D(0.0f, 0.0f, 1.0f));
	if (right.lengthSquared() < 1.0e-6f)
		right = QVector3D::crossProduct(v, QVector3D(0.0f, 1.0f, 0.0f));
	return right.normalized();
}

// The principal plane that faces `viewDir` most, normal toward the viewer; text runs along the in-plane axis nearest the view's right.
inline Basis modelPlane(const QVector3D& viewDir)
{
	Basis b;
	if (viewDir.lengthSquared() < 1.0e-10f)
		return b;
	const QVector3D v = viewDir.normalized();
	int normalAxis = 0;
	for (int k = 1; k < 3; ++k)
		if (std::abs(v[k]) > std::abs(v[normalAxis]))
			normalAxis = k;
	const QVector3D right = creationRight(v);
	int textAxis = -1;
	for (int k = 0; k < 3; ++k)
		if (k != normalAxis && (textAxis < 0 || std::abs(right[k]) > std::abs(right[textAxis])))
			textAxis = k;
	b.x = QVector3D();
	b.x[textAxis] = right[textAxis] >= 0.0f ? 1.0f : -1.0f;
	b.normal = QVector3D();
	b.normal[normalAxis] = v[normalAxis] <= 0.0f ? 1.0f : -1.0f;
	b.y = QVector3D::crossProduct(b.normal, b.x);
	b.valid = true;
	return b;
}

// The plane of the creation view: normal toward the viewer, text along the view's right.
inline Basis viewPlane(const QVector3D& viewDir)
{
	Basis b;
	if (viewDir.lengthSquared() < 1.0e-10f)
		return b;
	b.normal = -viewDir.normalized();
	b.x = creationRight(viewDir);
	b.y = QVector3D::crossProduct(b.normal, b.x).normalized();
	b.valid = true;
	return b;
}

inline Basis forOrientation(LabelOrientation mode, const QVector3D& viewDir)
{
	switch (mode)
	{
	case LabelOrientation::ModelPlane: return modelPlane(viewDir);
	case LabelOrientation::ViewPlane: return viewPlane(viewDir);
	case LabelOrientation::FaceCamera: break;
	}
	return Basis();
}

// The same plane turned so the text reads for the viewer at `eye` looking at `position`: the normal faces the viewer and the text
// runs toward `liveRight` (screen right). Seen from behind, or when it would read upside down, the text rotates half a turn; it never mirrors.
inline Basis readable(const Basis& b, const QVector3D& eye, const QVector3D& position, const QVector3D& liveRight)
{
	Basis r;
	if (!b.valid)
		return r;
	r.normal = QVector3D::dotProduct(b.normal, eye - position) < 0.0f ? -b.normal : b.normal;
	r.x = QVector3D::dotProduct(b.x, liveRight) >= 0.0f ? b.x : -b.x;
	r.y = QVector3D::crossProduct(r.normal, r.x).normalized();
	r.valid = true;
	return r;
}
}
