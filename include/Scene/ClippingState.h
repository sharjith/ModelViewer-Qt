#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QVector>

// ---------------------------------------------------------------------------
// ClippingState
//
// A snapshot of the Clipping Planes (the viewport's section views): the axis-aligned planes that are on, each at its world
// position and with the side that is kept, or the box clip with its six world limits and the in/out choice. Positions are
// absolute world coordinates (not the editor's coefficient from the scene centre), so a snapshot does not depend on the scene
// bounds at the time it is restored.
//
// Stored with captured camera views and scene states (GltfCameraEntry::clipping, so a SceneState's camera carries it too) and
// persisted in the MVF session next to the camera. Written/read here so every writer and reader uses the same encoding.
// ---------------------------------------------------------------------------
struct ClippingState
{
	struct Plane
	{
		int    axis = 0;                // 0 = X, 1 = Y, 2 = Z: the axis the plane is perpendicular to
		double position = 0.0;          // world coordinate on that axis
		bool   keepPositive = false;    // the model is kept on the +axis side (a flipped plane), else on the -axis side
	};

	QVector<Plane> planes;              // the axis-aligned planes that are on (at most one per axis)
	bool           boxEnabled = false;  // box clip on (mutually exclusive with the planes in the editor)
	bool           boxKeepInside = false; // true = keep only the inside of the box, false = cut a box-shaped hole
	double         boxLimits[6] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 }; // xMin, xMax, yMin, yMax, zMin, zMax (world)

	bool           showGizmo = true;    // the editor's Show Gizmo choice: the draggable plane/box gizmos were visible

	bool isActive() const { return !planes.isEmpty() || boxEnabled; }
};

inline QJsonObject clippingStateToJson(const ClippingState& state)
{
	QJsonArray planes;
	for (const ClippingState::Plane& plane : state.planes)
	{
		QJsonObject p;
		p.insert(QStringLiteral("axis"), plane.axis);
		p.insert(QStringLiteral("position"), plane.position);
		p.insert(QStringLiteral("keepPositive"), plane.keepPositive);
		planes.append(p);
	}
	QJsonArray limits;
	for (int i = 0; i < 6; ++i)
		limits.append(state.boxLimits[i]);

	QJsonObject box;
	box.insert(QStringLiteral("enabled"), state.boxEnabled);
	box.insert(QStringLiteral("keepInside"), state.boxKeepInside);
	box.insert(QStringLiteral("limits"), limits);

	QJsonObject obj;
	obj.insert(QStringLiteral("planes"), planes);
	obj.insert(QStringLiteral("box"), box);
	obj.insert(QStringLiteral("showGizmo"), state.showGizmo);
	return obj;
}

inline ClippingState clippingStateFromJson(const QJsonObject& obj)
{
	ClippingState state;
	for (const QJsonValue& value : obj.value(QStringLiteral("planes")).toArray())
	{
		const QJsonObject p = value.toObject();
		ClippingState::Plane plane;
		plane.axis = p.value(QStringLiteral("axis")).toInt(0);
		plane.position = p.value(QStringLiteral("position")).toDouble(0.0);
		plane.keepPositive = p.value(QStringLiteral("keepPositive")).toBool(false);
		if (plane.axis >= 0 && plane.axis <= 2)
			state.planes.append(plane);
	}
	state.showGizmo = obj.value(QStringLiteral("showGizmo")).toBool(true);
	const QJsonObject box = obj.value(QStringLiteral("box")).toObject();
	state.boxEnabled = box.value(QStringLiteral("enabled")).toBool(false);
	state.boxKeepInside = box.value(QStringLiteral("keepInside")).toBool(false);
	const QJsonArray limits = box.value(QStringLiteral("limits")).toArray();
	for (int i = 0; i < 6 && i < limits.size(); ++i)
		state.boxLimits[i] = limits.at(i).toDouble(0.0);
	return state;
}
