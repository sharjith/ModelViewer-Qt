#include "MeasurementController.h"

#include "Camera.h"
#include "MainWindow.h"
#include "MeasurementGeometry.h"
#include "MeasurementOffsetCommand.h"
#include "MeasurementOffsetVectorCommand.h"
#include "MeshImportAdaptor.h"
#include "ModelViewer.h"
#include "SceneGraph.h"
#include "SceneMesh.h"
#include "SceneRenderController.h"
#include "SceneRuntime.h"
#include "SelectionManager.h"
#include "TextRenderer.h"

// See resolveMeasurementCylindricalDiameterViaRegionGrowing()'s doc comment -
// CGAL Shape_detection Region Growing cylinder fit for CylindricalDiameter
// on non-CAD meshes.
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Named_function_parameters.h>
#include <CGAL/squared_distance_3.h>
#include <CGAL/Shape_detection/Region_growing/Region_growing.h>
#include <CGAL/Shape_detection/Region_growing/Point_set/Least_squares_cylinder_fit_region.h>
#include <boost/property_map/property_map.hpp>

#include <QDebug>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QRect>
#include <QSet>
#include <QtMath>
#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <unordered_set>

MeasurementController::MeasurementController(SceneRuntime& sceneRuntime, ModelViewer* viewer,
	SceneRenderController& renderCtrl, QObject* parent)
	: QObject(parent)
	, _sceneRuntime(sceneRuntime)
	, _viewer(viewer)
	, _renderCtrl(renderCtrl)
{
}

SceneMesh* MeasurementController::getMeshByUuid(const QUuid& uuid) const
{
	return _sceneRuntime.getMeshByUuid(uuid);
}

void MeasurementController::restoreGpuResources()
{
	// Re-resolves this class's own QOpenGLFunctions_4_5_Core function
	// pointers against the new context - see this method's doc comment
	// (MeasurementController.h). Safe to call unconditionally (not gated on
	// _glFunctionsInitialized like TransformGizmo's real GPU-object rebuild
	// is) since there's no actual resource to avoid double-allocating.
	initializeOpenGLFunctions();
	_glFunctionsInitialized = true;
}

void MeasurementController::releaseGpuResources()
{
	// Nothing to release - see this class's doc comment. Resetting the flag
	// is a conservative guard against drawMeasurementOverlay() running
	// (it shouldn't, mid-context-swap) with function pointers resolved
	// against the now-dying context.
	_glFunctionsInitialized = false;
}

void MeasurementController::updateHoverAnchor(const QPoint& pixel, SelectionManager* selectionManager)
{
	// Mirrors ViewportWidget::mouseMoveEvent()'s own picking dispatch in
	// handleMeasurementClick() - see that function's branch comments for the
	// full reasoning behind each case (kept in sync deliberately, not
	// factored into one shared helper, since the two differ in what they do
	// with a successful pick - commit a real anchor vs. just preview one).
	if (_measurementTool == MeasurementTool::None || !selectionManager)
		return;

	_measurementEdgeHoverIsCenterPick = false;
	if (_measurementTool == MeasurementTool::EdgeRadius || _measurementTool == MeasurementTool::Concentricity)
	{
		_measurementEdgeHoverAnchor = selectionManager->pickEdgeCircleAnchor(pixel);
		_measurementHoverAnchor = MeshSurfaceAnchor();
	}
	else if (_measurementTool == MeasurementTool::EdgeLength
		|| _measurementTool == MeasurementTool::EdgeToEdge
		|| _measurementTool == MeasurementTool::EdgeChain
		|| (_measurementTool == MeasurementTool::EdgeToVertex && _pendingMeasurementAnchors.isEmpty())
		|| (_measurementTool == MeasurementTool::EdgeToFace && _pendingMeasurementAnchors.isEmpty()))
	{
		_measurementEdgeHoverAnchor = selectionManager->pickStraightEdgeAnchor(pixel);
		_measurementHoverAnchor = MeshSurfaceAnchor();
	}
	else if (_measurementTool == MeasurementTool::FaceToFace
		|| _measurementTool == MeasurementTool::FaceArea
		|| _measurementTool == MeasurementTool::MinDistance
		|| _measurementTool == MeasurementTool::CylindricalDiameter
		|| ((_measurementTool == MeasurementTool::PointToFace || _measurementTool == MeasurementTool::EdgeToFace)
			&& !_pendingMeasurementAnchors.isEmpty())
		|| _measurementTool == MeasurementTool::ArcRadius3Point
		|| (_measurementTool == MeasurementTool::ArcRadiusCenterPoint && !_pendingMeasurementAnchors.isEmpty()))
	{
		// A FACE pick, or an ARC-RIM pick that must land on the circle
		// itself (not its center) - no circular-edge-center preview, same
		// reasoning as handleMeasurementClick()'s matching branch.
		_measurementHoverAnchor = selectionManager->pickSurfaceAnchor(pixel);
		_measurementEdgeHoverAnchor = MeshEdgeCircleAnchor();
	}
	else
	{
		// Every remaining pick genuinely wants an arbitrary POINT - prefer
		// the circular-edge-center preview; fall back to the ordinary
		// surface-hover preview if nothing's nearby - mirrors
		// handleMeasurementClick()'s own fallback.
		_measurementEdgeHoverAnchor = selectionManager->pickCircularEdgeCenterAnchor(pixel);
		if (_measurementEdgeHoverAnchor.isValid())
		{
			_measurementEdgeHoverIsCenterPick = true;
			_measurementHoverAnchor = MeshSurfaceAnchor();
		}
		else
		{
			_measurementHoverAnchor = selectionManager->pickSurfaceAnchor(pixel);
		}
	}
}

void MeasurementController::updateHoverMeasurement(const QPoint& pixel, Camera* camera, const QSize& viewportSize)
{
	// A dimension-line/arc hover is more specific than a whole-measurement
	// hover - it's telling the user exactly what they can grab and drag
	// (see beginDimensionLineDrag()) - so it takes priority; only fall back
	// to the whole-measurement preview when nothing more specific is under
	// the cursor. Caller (ViewportWidget::mouseMoveEvent()) is responsible
	// for the "no tool armed, not mid-navigation-drag, not over the gizmo/
	// view cube" gating - none of that is measurement-domain state.
	const DimensionHit hoveredDimension = hitTestDimensionLine(pixel, camera, viewportSize, 8);
	_hoveredDimensionId = hoveredDimension.measurementId;
	_hoveredDimensionKind = hoveredDimension.kind;
	_hoveredMeasurementId = (hoveredDimension.kind != DimensionDragKind::None)
		? QUuid()
		: hitTestMeasurement(pixel, camera, viewportSize, 8);
}

void MeasurementController::setMeasurementTool(MeasurementTool tool, SelectionManager* selectionManager)
{
	if (_measurementTool == tool)
		return;

	const bool wasArmed = (_measurementTool != MeasurementTool::None);
	const bool nowArmed = (tool != MeasurementTool::None);

	if (selectionManager)
	{
		if (nowArmed && !wasArmed)
		{
			// Suppress the normal whole-mesh hover highlight while a tool is
			// armed - it's ambiguous (doesn't say WHERE a click will land).
			// mouseMoveEvent() shows the actual snap-able point instead via
			// _measurementHoverAnchor. Reuses the existing hover-mode
			// mechanism rather than a parallel "don't highlight" flag, and
			// its own Disabled-transition handling clears any highlight
			// that was already showing right when the tool got armed.
			_savedHoverHighlightModeBeforeMeasurement = selectionManager->getHoverMode();
			selectionManager->setHoverHighlightMode(HoverHighlightMode::Disabled);
		}
		else if (!nowArmed && wasArmed)
		{
			selectionManager->setHoverHighlightMode(_savedHoverHighlightModeBeforeMeasurement);
		}
	}

	_measurementTool = tool;
	_pendingMeasurementAnchors.clear();
	_measurementClickCandidate = false;
	_measurementHoverAnchor = MeshSurfaceAnchor();
	_measurementEdgeHoverAnchor = MeshEdgeCircleAnchor();
	_measurementEdgeHoverIsCenterPick = false;
	_hoveredMeasurementId = QUuid();
	emit measurementStateChanged();
	emit measurementToolChanged(_measurementTool);
	emit measurementProgressChanged(0, measurementToolRequiredAnchorCount(_measurementTool));
}

QVector3D MeasurementController::resolveMeasurementAnchor(const MeasurementAnchorRef& ref) const
{
	if (ref.edgeIndex >= 0)
	{
		// A circular-edge-derived point anchor - any arbitrary-point pick
		// (Point, Distance, both arc tools, Point-to-Face's point anchor,
		// Edge-to-Vertex's vertex anchor) snapped to a nearby circular OCC
		// edge's exact analytic center instead of requiring a literal
		// surface hit at that position (a through-hole's center is empty
		// space - see SelectionManager::pickCircularEdgeCenterAnchor()'s
		// doc comment). Falls through to the ordinary triangle/vertex
		// resolution below if this isn't actually a circle (shouldn't
		// happen in practice, since only pickCircularEdgeCenterAnchor()
		// ever produces this kind of anchor, but a saved measurement could
		// in principle outlive a mesh reload that changes topology) - that
		// path correctly returns a null QVector3D since triangleIndex/
		// snappedVertexIndex are also unset for a pure edge anchor.
		QVector3D center, axis;
		float radius = 0.0f;
		if (resolveMeasurementEdgeCircle(ref, center, axis, radius))
			return center;
	}

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

QString MeasurementController::measurementSummaryText(const Measurement& m) const
{
	switch (m.type)
	{
	case MeasurementType::Point:
	{
		if (m.anchors.isEmpty())
			return QString();
		const QVector3D p = resolveMeasurementAnchor(m.anchors[0]);
		return tr("Point: (%1, %2, %3)")
			.arg(p.x(), 0, 'f', 3).arg(p.y(), 0, 'f', 3).arg(p.z(), 0, 'f', 3);
	}
	case MeasurementType::Distance:
	{
		if (m.anchors.size() < 2)
			return QString();
		const QVector3D a = resolveMeasurementAnchor(m.anchors[0]);
		const QVector3D b = resolveMeasurementAnchor(m.anchors[1]);
		return tr("Distance: %1").arg(a.distanceToPoint(b), 0, 'f', 3);
	}
	case MeasurementType::ArcRadius3Point:
	{
		if (m.anchors.size() < 3)
			return QString();
		const QVector3D p0 = resolveMeasurementAnchor(m.anchors[0]);
		const QVector3D p1 = resolveMeasurementAnchor(m.anchors[1]);
		const QVector3D p2 = resolveMeasurementAnchor(m.anchors[2]);
		QVector3D center, normal;
		float radius = 0.0f;
		if (!MeasurementGeometry::circumcircle3Point(p0, p1, p2, center, normal, radius))
			return tr("3-Point Arc Radius: (degenerate - points are collinear)");
		return tr("3-Point Arc Radius: %1").arg(radius, 0, 'f', 3);
	}
	case MeasurementType::ArcRadiusCenterPoint:
	{
		if (m.anchors.size() < 3)
			return QString();
		const QVector3D center = resolveMeasurementAnchor(m.anchors[0]);
		const QVector3D p1 = resolveMeasurementAnchor(m.anchors[1]);
		const QVector3D p2 = resolveMeasurementAnchor(m.anchors[2]);
		QVector3D normal;
		float radius = 0.0f;
		if (!MeasurementGeometry::circleFromCenterAndTwoPoints(center, p1, p2, normal, radius))
			return tr("Center + 2-Point Arc Radius: (degenerate - points are collinear)");
		return tr("Center + 2-Point Arc Radius: %1").arg(radius, 0, 'f', 3);
	}
	case MeasurementType::EdgeRadius:
	{
		if (m.anchors.isEmpty())
			return QString();
		QVector3D center, axis;
		float radius = 0.0f;
		if (!resolveMeasurementEdgeCircle(m.anchors[0], center, axis, radius))
			return tr("Edge Radius: (edge no longer available)");
		return tr("Edge Radius: %1").arg(radius, 0, 'f', 3);
	}
	case MeasurementType::FaceToFace:
	{
		if (m.anchors.size() < 2)
			return QString();
		QVector3D p1, n1, p2, n2;
		if (!resolveMeasurementAnchorPlane(m.anchors[0], p1, n1) || !resolveMeasurementAnchorPlane(m.anchors[1], p2, n2))
			return tr("Face to Face: (face no longer available)");
		const MeasurementGeometry::FaceToFaceResult result = MeasurementGeometry::compareFaces(p1, n1, p2, n2);
		return result.isParallel
			? tr("Face to Face: %1").arg(result.distance, 0, 'f', 3)
			: tr("Face to Face: %1°").arg(result.angleDegrees, 0, 'f', 2);
	}
	case MeasurementType::PointToFace:
	{
		if (m.anchors.size() < 2)
			return QString();
		const QVector3D point = resolveMeasurementAnchor(m.anchors[0]);
		QVector3D facePos, faceNormal;
		if (!resolveMeasurementAnchorPlane(m.anchors[1], facePos, faceNormal))
			return tr("Point to Face: (face no longer available)");
		return tr("Point to Face: %1").arg(MeasurementGeometry::pointToPlaneDistance(point, facePos, faceNormal), 0, 'f', 3);
	}
	case MeasurementType::EdgeLength:
	{
		if (m.anchors.isEmpty())
			return QString();
		QVector3D start, end;
		float length = 0.0f;
		if (!resolveMeasurementEdgeGeometry(m.anchors[0], start, end, length))
			return tr("Edge Length: (edge no longer available)");
		return tr("Edge Length: %1").arg(length, 0, 'f', 3);
	}
	case MeasurementType::EdgeToVertex:
	{
		if (m.anchors.size() < 2)
			return QString();
		QVector3D edgeStart, edgeEnd;
		float edgeLength = 0.0f;
		if (!resolveMeasurementEdgeGeometry(m.anchors[0], edgeStart, edgeEnd, edgeLength))
			return tr("Edge to Vertex: (edge no longer available)");
		const QVector3D point = resolveMeasurementAnchor(m.anchors[1]);
		return tr("Edge to Vertex: %1").arg(
			MeasurementGeometry::pointToLineDistance(point, edgeStart, edgeEnd - edgeStart), 0, 'f', 3);
	}
	case MeasurementType::EdgeToEdge:
	{
		if (m.anchors.size() < 2)
			return QString();
		QVector3D start1, end1, start2, end2;
		float len1 = 0.0f, len2 = 0.0f;
		if (!resolveMeasurementEdgeGeometry(m.anchors[0], start1, end1, len1)
			|| !resolveMeasurementEdgeGeometry(m.anchors[1], start2, end2, len2))
			return tr("Edge to Edge: (edge no longer available)");
		const MeasurementGeometry::EdgeToEdgeResult result =
			MeasurementGeometry::compareLines(start1, end1 - start1, start2, end2 - start2);
		return result.isParallel
			? tr("Edge to Edge: %1").arg(result.distance, 0, 'f', 3)
			: tr("Edge to Edge: %1°").arg(result.angleDegrees, 0, 'f', 2);
	}
	case MeasurementType::EdgeToFace:
	{
		if (m.anchors.size() < 2)
			return QString();
		QVector3D edgeStart, edgeEnd;
		float edgeLength = 0.0f;
		if (!resolveMeasurementEdgeGeometry(m.anchors[0], edgeStart, edgeEnd, edgeLength))
			return tr("Edge to Face: (edge no longer available)");
		QVector3D facePos, faceNormal;
		if (!resolveMeasurementAnchorPlane(m.anchors[1], facePos, faceNormal))
			return tr("Edge to Face: (face no longer available)");
		const MeasurementGeometry::EdgeToFaceResult result =
			MeasurementGeometry::compareEdgeToFace(edgeStart, edgeEnd - edgeStart, facePos, faceNormal);
		return result.isParallel
			? tr("Edge to Face: %1").arg(result.distance, 0, 'f', 3)
			: tr("Edge to Face: %1°").arg(result.angleDegrees, 0, 'f', 2);
	}
	case MeasurementType::PitchCircle:
	{
		if (m.anchors.size() < 3)
			return QString();
		QVector<QVector3D> points;
		points.reserve(m.anchors.size());
		for (const MeasurementAnchorRef& a : m.anchors)
			points.append(resolveMeasurementAnchor(a));
		const MeasurementGeometry::PitchCircleResult result = MeasurementGeometry::fitPitchCircle(points);
		if (!result.valid)
			return tr("Pitch Circle: (degenerate - points are collinear or coincident)");

		const int n = result.gapAnglesDegrees.size();
		float minGap = result.gapAnglesDegrees.first();
		float maxGap = minGap;
		for (float g : result.gapAnglesDegrees)
		{
			minGap = std::min(minGap, g);
			maxGap = std::max(maxGap, g);
		}
		// Tight on purpose: a bolt pattern can be deliberately keyed with
		// one hole shifted a few degrees so the part only assembles one
		// way - a loose tolerance would silently call that "uniform" and
		// hide exactly the thing this measurement exists to catch.
		constexpr float kUniformToleranceDegrees = 0.5f;
		const float diameter = result.radius * 2.0f;

		// Always a headline (diameter + hole count) plus a spacing detail
		// line, joined with '\n' - two short lines read far better than
		// one long one, both as a floating 3D label (see
		// drawMeasurementOverlay()'s label loop, which splits on '\n') and
		// as the Measurement dialog's list row (Qt's default item drawing
		// already honors an embedded '\n' as a line break).
		const QString headline = tr("Pitch Circle: %1, %2 holes").arg(diameter, 0, 'f', 3).arg(n);

		if (maxGap - minGap <= kUniformToleranceDegrees)
			return headline + "\n" + tr("@ %1° spacing").arg(360.0f / static_cast<float>(n), 0, 'f', 2);

		// Itemized gaps read fine for a small pattern but would grow
		// unbounded with hole count - a compact min-max range takes over
		// past a handful of holes.
		if (n <= 6)
		{
			QStringList gapStrs;
			for (float g : result.gapAnglesDegrees)
				gapStrs << QString::number(g, 'f', 2) + QChar(0xB0);
			return headline + "\n" + tr("gaps: %1").arg(gapStrs.join(", "));
		}

		return headline + "\n" + tr("gaps %1°-%2° (not uniform)").arg(minGap, 0, 'f', 2).arg(maxGap, 0, 'f', 2);
	}
	case MeasurementType::Concentricity:
	{
		if (m.anchors.size() < 2)
			return QString();
		QVector3D center1, axis1, center2, axis2;
		float radius1 = 0.0f, radius2 = 0.0f;
		if (!resolveMeasurementEdgeCircle(m.anchors[0], center1, axis1, radius1)
			|| !resolveMeasurementEdgeCircle(m.anchors[1], center2, axis2, radius2))
			return tr("Concentricity: (circle no longer available)");
		const MeasurementGeometry::ConcentricityResult result =
			MeasurementGeometry::compareCircles(center1, axis1, center2, axis2);
		return tr("Concentricity: %1 offset, %2° axis").arg(result.centerOffset, 0, 'f', 3).arg(result.axisAngleDegrees, 0, 'f', 2);
	}
	case MeasurementType::AngleThreePoint:
	{
		if (m.anchors.size() < 3)
			return QString();
		const QVector3D vertex = resolveMeasurementAnchor(m.anchors[0]);
		const QVector3D p1 = resolveMeasurementAnchor(m.anchors[1]);
		const QVector3D p2 = resolveMeasurementAnchor(m.anchors[2]);
		if ((p1 - vertex).lengthSquared() < 1.0e-8f || (p2 - vertex).lengthSquared() < 1.0e-8f)
			return tr("3-Point Angle: (degenerate - a ray point coincides with the vertex)");
		return tr("3-Point Angle: %1°").arg(MeasurementGeometry::angleBetweenRays(vertex, p1, p2), 0, 'f', 2);
	}
	case MeasurementType::EdgeChain:
	{
		if (m.anchors.size() < 2)
			return QString();
		float total = 0.0f;
		int resolvedCount = 0;
		for (const MeasurementAnchorRef& a : m.anchors)
		{
			QVector3D start, end;
			float length = 0.0f;
			if (resolveMeasurementEdgeGeometry(a, start, end, length))
			{
				total += length;
				++resolvedCount;
			}
		}
		if (resolvedCount == 0)
			return tr("Chain Length: (no edges available)");
		if (resolvedCount < m.anchors.size())
			return tr("Chain Length: %1, %2 of %3 edges available").arg(total, 0, 'f', 3).arg(resolvedCount).arg(m.anchors.size());
		return tr("Chain Length: %1, %2 edges").arg(total, 0, 'f', 3).arg(resolvedCount);
	}
	case MeasurementType::FaceArea:
	{
		if (m.anchors.isEmpty())
			return QString();
		QVector<int> triangles;
		float area = 0.0f;
		QVector3D centroid;
		if (!resolveMeasurementFaceArea(m.anchors[0], triangles, area, centroid))
			return tr("Face Area: (face no longer available)");
		return tr("Face Area: %1, %2 triangles").arg(area, 0, 'f', 3).arg(triangles.size());
	}
	case MeasurementType::MinDistance:
	{
		QVector3D pointA, pointB;
		float distance = 0.0f;
		if (!resolveMeasurementMinDistance(m, pointA, pointB, distance))
			return tr("Minimum Distance: (face no longer available)");
		return tr("Minimum Distance: %1").arg(distance, 0, 'f', 3);
	}
	case MeasurementType::CylindricalDiameter:
	{
		if (m.anchors.isEmpty())
			return QString();
		float diameter = 0.0f;
		QVector3D axisOrigin, axisDir, pickedPoint;
		bool isCone = false;
		if (!resolveMeasurementCylindricalDiameter(m.anchors[0], diameter, axisOrigin, axisDir, pickedPoint, isCone))
			return tr("Diameter: (face no longer available)");
		return isCone
			? tr("Diameter (at picked point): ⌀ %1").arg(diameter, 0, 'f', 3)
			: tr("Diameter: ⌀ %1").arg(diameter, 0, 'f', 3);
	}
	}
	return QString();
}

bool MeasurementController::resolveMeasurementEdgeCircle(const MeasurementAnchorRef& ref,
	QVector3D& outCenter, QVector3D& outAxis, float& outRadius) const
{
	if (ref.edgeIndex < 0)
		return false;

	SceneMesh* mesh = getMeshByUuid(ref.meshUuid);
	if (!mesh)
		return false;

	const std::vector<OccEdgeCircleInfo>& circles = mesh->getOccEdgeCircles();
	if (circles.empty())
	{
		// Non-CAD mesh - resolve against the detected-circular-loop table
		// instead (SceneMesh::getDetectedCircularLoops(), same dual-meaning
		// edgeIndex convention as resolveMeasurementEdgeGeometry()'s own
		// OCC/feature-edge split). Re-fits the circle fresh from CURRENT
		// world-space positions every call (getTrsfPoints() is already
		// world-space, no combinedRenderTransform() needed) rather than
		// trusting a value baked in at loop-detection time - same "live
		// geometry" convention every other resolver here follows, and
		// correctly reflects any transform-panel/exploded-view move made
		// since the loop was first detected.
		const std::vector<DetectedCircularLoop>& loops = mesh->getDetectedCircularLoops();
		if (ref.edgeIndex >= static_cast<int>(loops.size()))
			return false;

		const std::vector<uint32_t>& loopVerts = loops[static_cast<size_t>(ref.edgeIndex)].vertexIndices;
		const std::vector<float>& trsfPoints = mesh->getTrsfPoints();

		QVector<QVector3D> worldPoints;
		worldPoints.reserve(static_cast<int>(loopVerts.size()));
		for (uint32_t v : loopVerts)
		{
			const size_t p = static_cast<size_t>(v) * 3;
			if (p + 2 >= trsfPoints.size())
				continue;
			worldPoints.append(QVector3D(trsfPoints[p], trsfPoints[p + 1], trsfPoints[p + 2]));
		}
		if (worldPoints.size() < 3)
			return false;

		const MeasurementGeometry::PitchCircleResult fit = MeasurementGeometry::fitPitchCircle(worldPoints);
		if (!fit.valid)
			return false;

		outCenter = fit.center;
		outAxis = fit.normal.normalized();
		outRadius = fit.radius;
		return true;
	}

	if (ref.edgeIndex >= static_cast<int>(circles.size()) || !circles[ref.edgeIndex].isCircle)
		return false;

	const OccEdgeCircleInfo& c = circles[ref.edgeIndex];
	const QVector3D centerLocal(static_cast<float>(c.centerX), static_cast<float>(c.centerY), static_cast<float>(c.centerZ));
	const QVector3D axisLocal(static_cast<float>(c.axisX), static_cast<float>(c.axisY), static_cast<float>(c.axisZ));

	// Derive the world-space center/axis/radius purely via the mesh's
	// current combinedRenderTransform() - the same matrix getTrsfPoints()
	// uses for every other measurement anchor, so this stays correct under
	// mesh transform-panel edits/exploded view exactly like they do. Radius
	// is recovered by transforming an arbitrary in-plane rim point too and
	// measuring its distance from the transformed center, rather than just
	// scaling the local radius by a scalar - exact for the common uniform-
	// scale case, and degrades gracefully (an "effective" radius) under a
	// non-uniform scale, where a true circle wouldn't stay a circle anyway.
	const QVector3D axisNormalizedLocal = axisLocal.normalized();
	const QVector3D reference = (std::abs(QVector3D::dotProduct(axisNormalizedLocal, QVector3D(0.0f, 1.0f, 0.0f))) < 0.9f)
		? QVector3D(0.0f, 1.0f, 0.0f)
		: QVector3D(1.0f, 0.0f, 0.0f);
	const QVector3D u = QVector3D::crossProduct(axisNormalizedLocal, reference).normalized();
	const QVector3D rimLocal = centerLocal + u * static_cast<float>(c.radius);

	const QMatrix4x4 combined = mesh->combinedRenderTransform();
	outCenter = combined.map(centerLocal);
	const QVector3D rimWorld = combined.map(rimLocal);
	outRadius = outCenter.distanceToPoint(rimWorld);
	outAxis = combined.mapVector(axisLocal).normalized();
	return true;
}

bool MeasurementController::resolveMeasurementEdgeGeometry(const MeasurementAnchorRef& ref,
	QVector3D& outStart, QVector3D& outEnd, float& outLength) const
{
	if (ref.edgeIndex < 0)
		return false;

	SceneMesh* mesh = getMeshByUuid(ref.meshUuid);
	if (!mesh)
		return false;

	const std::vector<int>& occBounds = mesh->getOccEdgeBoundaries();
	if (!occBounds.empty())
	{
		// CAD mesh - sum the OCC edge's tessellated segment lengths (works
		// for any curve type, no classification needed).
		if (ref.edgeIndex + 1 >= static_cast<int>(occBounds.size()))
			return false;

		const std::vector<float>& segments = mesh->getOccEdgeSegments();
		const int startVec3 = occBounds[ref.edgeIndex];
		const int endVec3 = occBounds[ref.edgeIndex + 1];
		if (startVec3 < 0 || endVec3 <= startVec3 || static_cast<size_t>(endVec3) * 3 > segments.size())
			return false;

		const QMatrix4x4 combined = mesh->combinedRenderTransform();
		auto worldPointAt = [&](int v) -> QVector3D {
			const size_t p = static_cast<size_t>(v) * 3;
			return combined.map(QVector3D(segments[p], segments[p + 1], segments[p + 2]));
		};

		outStart = worldPointAt(startVec3);
		outEnd = worldPointAt(endVec3 - 1);
		outLength = 0.0f;
		for (int v = startVec3; v + 1 < endVec3; v += 2)
			outLength += worldPointAt(v).distanceToPoint(worldPointAt(v + 1));
		return true;
	}

	// Non-CAD mesh - a single straight feature edge (see
	// SceneMesh::getFeatureEdgeIndices()'s doc comment).
	const std::vector<uint32_t>& featureEdges = mesh->getFeatureEdgeIndices();
	const size_t base = static_cast<size_t>(ref.edgeIndex) * 2;
	if (base + 1 >= featureEdges.size())
		return false;

	const std::vector<float>& trsfPoints = mesh->getTrsfPoints();
	auto vertexPos = [&trsfPoints](uint32_t vIdx) -> QVector3D {
		const size_t p = static_cast<size_t>(vIdx) * 3;
		if (p + 2 >= trsfPoints.size())
			return QVector3D();
		return QVector3D(trsfPoints[p], trsfPoints[p + 1], trsfPoints[p + 2]);
	};

	outStart = vertexPos(featureEdges[base]);
	outEnd = vertexPos(featureEdges[base + 1]);
	outLength = outStart.distanceToPoint(outEnd);
	return true;
}

bool MeasurementController::resolveMeasurementEdgePolyline(const MeasurementAnchorRef& ref, QVector<QVector3D>& outPoints) const
{
	outPoints.clear();
	if (ref.edgeIndex < 0)
		return false;

	SceneMesh* mesh = getMeshByUuid(ref.meshUuid);
	if (!mesh)
		return false;

	const std::vector<int>& occBounds = mesh->getOccEdgeBoundaries();
	if (!occBounds.empty())
	{
		if (ref.edgeIndex + 1 >= static_cast<int>(occBounds.size()))
			return false;

		const std::vector<float>& segments = mesh->getOccEdgeSegments();
		const int startVec3 = occBounds[ref.edgeIndex];
		const int endVec3 = occBounds[ref.edgeIndex + 1];
		if (startVec3 < 0 || endVec3 <= startVec3 || static_cast<size_t>(endVec3) * 3 > segments.size())
			return false;

		const QMatrix4x4 combined = mesh->combinedRenderTransform();
		auto worldPointAt = [&](int v) -> QVector3D {
			const size_t p = static_cast<size_t>(v) * 3;
			return combined.map(QVector3D(segments[p], segments[p + 1], segments[p + 2]));
		};

		// Segments within one OCC edge's range are emitted in connected,
		// head-to-tail order along the curve by construction (see
		// BRepToAssimpConverter::extractEdgesFromFaceGroup()'s
		// GCPnts_TangentialDeflection walk) - segment N's second point and
		// segment N+1's first point are the same value, so one point per
		// pair plus a final closing point traces the whole path with no
		// duplicates.
		outPoints.reserve((endVec3 - startVec3) / 2 + 1);
		for (int v = startVec3; v + 1 < endVec3; v += 2)
			outPoints.append(worldPointAt(v));
		outPoints.append(worldPointAt(endVec3 - 1));
		return true;
	}

	// Non-CAD mesh - a single straight feature edge, so its "polyline" is
	// just its two endpoints.
	const std::vector<uint32_t>& featureEdges = mesh->getFeatureEdgeIndices();
	const size_t base = static_cast<size_t>(ref.edgeIndex) * 2;
	if (base + 1 >= featureEdges.size())
		return false;

	const std::vector<float>& trsfPoints = mesh->getTrsfPoints();
	auto vertexPos = [&trsfPoints](uint32_t vIdx) -> QVector3D {
		const size_t p = static_cast<size_t>(vIdx) * 3;
		if (p + 2 >= trsfPoints.size())
			return QVector3D();
		return QVector3D(trsfPoints[p], trsfPoints[p + 1], trsfPoints[p + 2]);
	};
	outPoints.append(vertexPos(featureEdges[base]));
	outPoints.append(vertexPos(featureEdges[base + 1]));
	return true;
}

bool MeasurementController::measurementChainEdgeConnects(const QVector<MeasurementAnchorRef>& chainSoFar,
	const MeasurementAnchorRef& candidate) const
{
	if (chainSoFar.isEmpty())
		return true;  // first edge always starts the chain

	QVector<QVector3D> candidatePts;
	if (!resolveMeasurementEdgePolyline(candidate, candidatePts) || candidatePts.size() < 2)
		return false;

	// "Same point" tolerance in world units for one edge, derived from the
	// real OCC B-Rep tolerance at its mesh's vertices (see
	// MeshImportAdaptor::occEdgeVertexTolerance()'s doc comment) rather
	// than an invented constant - scaled by the mesh's current transform
	// since the stored value is in local/model space (same "average axis
	// scale is good enough for an epsilon" reasoning as
	// resolveMeasurementEdgeCircle()'s radius recovery). Floored at a
	// small absolute constant, both as a safety margin against float
	// truncation in the tessellated data and as the whole answer for non-
	// CAD meshes (tolerance always 0 there - fine, since a non-CAD edge is
	// only ever its 2 raw vertex positions, with no curve-evaluation
	// drift to absorb in the first place).
	constexpr float kFallbackTol = 1.0e-4f;
	auto edgeTolerance = [&](const MeasurementAnchorRef& ref) -> float {
		SceneMesh* mesh = getMeshByUuid(ref.meshUuid);
		const double localTol = mesh ? mesh->getOccEdgeVertexTolerance() : 0.0;
		if (localTol <= 0.0 || !mesh)
			return kFallbackTol;
		const QMatrix4x4 t = mesh->combinedRenderTransform();
		const float scale = (t.mapVector(QVector3D(1, 0, 0)).length()
			+ t.mapVector(QVector3D(0, 1, 0)).length()
			+ t.mapVector(QVector3D(0, 0, 1)).length()) / 3.0f;
		return std::max(kFallbackTol, static_cast<float>(localTol) * scale);
	};
	const float candidateTol = edgeTolerance(candidate);

	// A chain edge's "loose ends" are whichever of its two true endpoints
	// isn't shared with a neighbor already in the chain - XOR across every
	// edge picked so far (a point touched by exactly two edges is
	// interior to the chain, not something a new edge could still attach
	// to). A single edge that's already a closed loop on its own (a full
	// circle, where its own two "ends" coincide) cancels itself out here,
	// correctly leaving zero loose ends - see this function's doc comment
	// for why that's exactly what blocks a second, unrelated circle from
	// silently joining the same chain.
	struct LooseEnd { QVector3D point; float tolerance; };
	QVector<LooseEnd> looseEnds;
	auto toggleEnd = [&looseEnds](const QVector3D& p, float tol) {
		for (int i = 0; i < looseEnds.size(); ++i)
		{
			const float t = std::max(tol, looseEnds[i].tolerance);
			if ((looseEnds[i].point - p).lengthSquared() < t * t)
			{
				looseEnds.removeAt(i);
				return;
			}
		}
		looseEnds.append({ p, tol });
	};

	for (const MeasurementAnchorRef& a : chainSoFar)
	{
		QVector<QVector3D> pts;
		if (!resolveMeasurementEdgePolyline(a, pts) || pts.size() < 2)
			continue;
		const float tol = edgeTolerance(a);
		toggleEnd(pts.first(), tol);
		toggleEnd(pts.last(), tol);
	}

	if (looseEnds.isEmpty())
		return false;  // chain already closed into a loop - nothing left to attach to

	for (const LooseEnd& end : looseEnds)
	{
		const float t = std::max(candidateTol, end.tolerance);
		if ((candidatePts.first() - end.point).lengthSquared() < t * t
			|| (candidatePts.last() - end.point).lengthSquared() < t * t)
			return true;
	}
	return false;
}

bool MeasurementController::resolveMeasurementAnchorPlane(const MeasurementAnchorRef& ref,
	QVector3D& outPosition, QVector3D& outNormal) const
{
	if (ref.triangleIndex < 0)
		return false;

	SceneMesh* mesh = getMeshByUuid(ref.meshUuid);
	if (!mesh)
		return false;

	const std::vector<float>& trsfPoints = mesh->getTrsfPoints();
	auto vertexPos = [&trsfPoints](int vIdx) -> QVector3D {
		if (vIdx < 0)
			return QVector3D();
		const size_t p = static_cast<size_t>(vIdx) * 3;
		if (p + 2 >= trsfPoints.size())
			return QVector3D();
		return QVector3D(trsfPoints[p], trsfPoints[p + 1], trsfPoints[p + 2]);
	};

	const std::vector<unsigned int> indices = mesh->indices();
	const size_t base = static_cast<size_t>(ref.triangleIndex) * 3;
	if (base + 2 >= indices.size())
		return false;

	const QVector3D p0 = vertexPos(static_cast<int>(indices[base]));
	const QVector3D p1 = vertexPos(static_cast<int>(indices[base + 1]));
	const QVector3D p2 = vertexPos(static_cast<int>(indices[base + 2]));

	const QVector3D normal = QVector3D::crossProduct(p1 - p0, p2 - p0);
	if (normal.lengthSquared() < 1.0e-12f)
		return false;  // degenerate (near-zero-area) triangle

	outPosition = resolveMeasurementAnchor(ref);  // respects vertex snap, same as every other tool
	outNormal = normal.normalized();
	return true;
}

bool MeasurementController::resolveMeasurementFaceArea(const MeasurementAnchorRef& ref,
	QVector<int>& outTriangleIndices, float& outArea, QVector3D& outCentroid) const
{
	outTriangleIndices.clear();
	outArea = 0.0f;
	outCentroid = QVector3D();

	if (ref.triangleIndex < 0)
		return false;

	SceneMesh* mesh = getMeshByUuid(ref.meshUuid);
	if (!mesh)
		return false;

	const std::vector<unsigned int> indices = mesh->indices();
	const size_t triCount = indices.size() / 3;
	if (static_cast<size_t>(ref.triangleIndex) >= triCount)
		return false;

	const std::vector<float>& trsfPoints = mesh->getTrsfPoints();
	auto vertexPos = [&trsfPoints](unsigned int vIdx) -> QVector3D {
		const size_t p = static_cast<size_t>(vIdx) * 3;
		if (p + 2 >= trsfPoints.size())
			return QVector3D();
		return QVector3D(trsfPoints[p], trsfPoints[p + 1], trsfPoints[p + 2]);
	};
	auto triangleNormal = [&](int t) -> QVector3D {
		const size_t base = static_cast<size_t>(t) * 3;
		const QVector3D p0 = vertexPos(indices[base]);
		const QVector3D p1 = vertexPos(indices[base + 1]);
		const QVector3D p2 = vertexPos(indices[base + 2]);
		return QVector3D::crossProduct(p1 - p0, p2 - p0);
	};

	const QVector3D seedNormalRaw = triangleNormal(ref.triangleIndex);
	if (seedNormalRaw.lengthSquared() < 1.0e-12f)
		return false;  // degenerate (near-zero-area) seed triangle
	const QVector3D seedNormal = seedNormalRaw.normalized();

	// Tight on purpose (see this function's doc comment in ViewportWidget.h) -
	// a genuinely flat CAD face's triangles agree to a small fraction of a
	// degree in practice; this needs to reliably stop at any REAL angle
	// change (even a shallow one) while still absorbing floating-point
	// tessellation noise, the opposite bias from buildAndUploadFeatureEdges()'s
	// much looser ~30 degree "is this a sharp edge" threshold.
	constexpr float kCoplanarToleranceDegrees = 2.0f;
	const float cosThreshold = std::cos(kCoplanarToleranceDegrees * 0.017453292519943295f);

	const std::vector<std::array<int, 3>>& adjacency = mesh->getTriangleAdjacency();
	if (static_cast<size_t>(ref.triangleIndex) >= adjacency.size())
		return false;  // adjacency build failed (non-triangle-mesh guard in buildTriangleAdjacency())

	std::vector<bool> visited(triCount, false);
	QVector<int> queue;
	queue.append(ref.triangleIndex);
	visited[static_cast<size_t>(ref.triangleIndex)] = true;

	QVector3D areaWeightedCentroidSum;
	for (int qi = 0; qi < queue.size(); ++qi)
	{
		const int t = queue[qi];
		const size_t base = static_cast<size_t>(t) * 3;
		const float area = triangleNormal(t).length() * 0.5f;
		const QVector3D centroid = (vertexPos(indices[base]) + vertexPos(indices[base + 1]) + vertexPos(indices[base + 2])) / 3.0f;

		outArea += area;
		areaWeightedCentroidSum += centroid * area;
		outTriangleIndices.append(t);

		// Every candidate is compared against the SEED's normal directly,
		// not its immediate predecessor in the flood-fill - comparing to
		// an ever-drifting "current" normal would let many small (each
		// individually within-tolerance) steps accumulate into a large
		// total misalignment across a big face; comparing to one fixed
		// reference keeps the whole region within the stated tolerance of
		// the point the user actually clicked.
		for (int neighbor : adjacency[static_cast<size_t>(t)])
		{
			if (neighbor < 0 || visited[static_cast<size_t>(neighbor)])
				continue;
			const QVector3D n = triangleNormal(neighbor);
			if (n.lengthSquared() < 1.0e-12f)
				continue;  // skip a degenerate triangle rather than letting it break the chain
			if (QVector3D::dotProduct(n.normalized(), seedNormal) < cosThreshold)
				continue;  // not coplanar enough - a real face boundary
			visited[static_cast<size_t>(neighbor)] = true;
			queue.append(neighbor);
		}
	}

	outCentroid = (outArea > 1.0e-9f)
		? areaWeightedCentroidSum / outArea
		: vertexPos(indices[static_cast<size_t>(ref.triangleIndex) * 3]);
	return true;
}

bool MeasurementController::resolveMeasurementFaceRegion(const MeasurementAnchorRef& ref,
	QVector<int>& outTriangleIndices) const
{
	outTriangleIndices.clear();

	if (ref.triangleIndex < 0)
		return false;

	SceneMesh* mesh = getMeshByUuid(ref.meshUuid);
	if (!mesh)
		return false;

	const std::vector<unsigned int> indices = mesh->indices();
	const size_t triCount = indices.size() / 3;
	if (static_cast<size_t>(ref.triangleIndex) >= triCount)
		return false;

	const std::vector<float>& trsfPoints = mesh->getTrsfPoints();
	auto vertexPos = [&trsfPoints](unsigned int vIdx) -> QVector3D {
		const size_t p = static_cast<size_t>(vIdx) * 3;
		if (p + 2 >= trsfPoints.size())
			return QVector3D();
		return QVector3D(trsfPoints[p], trsfPoints[p + 1], trsfPoints[p + 2]);
	};
	auto triangleNormal = [&](int t) -> QVector3D {
		const size_t base = static_cast<size_t>(t) * 3;
		const QVector3D p0 = vertexPos(indices[base]);
		const QVector3D p1 = vertexPos(indices[base + 1]);
		const QVector3D p2 = vertexPos(indices[base + 2]);
		return QVector3D::crossProduct(p1 - p0, p2 - p0);
	};

	const QVector3D seedNormalRaw = triangleNormal(ref.triangleIndex);
	if (seedNormalRaw.lengthSquared() < 1.0e-12f)
		return false;  // degenerate (near-zero-area) seed triangle

	// LOOSE on purpose (see this function's doc comment in ViewportWidget.h) -
	// matches SceneMesh::buildAndUploadFeatureEdges()'s ~30 degree "is this
	// a real sharp/dihedral edge" bias, the opposite of
	// resolveMeasurementFaceArea()'s tight ~2 degree coplanarity test.
	// Evaluated NEIGHBOR-vs-NEIGHBOR (the triangle being expanded from,
	// not the fixed seed) rather than resolveMeasurementFaceArea()'s
	// seed-relative test - a genuinely curved face's normal drifts
	// continuously across its span, so comparing everything back to one
	// fixed seed normal would incorrectly stop at the first bit of
	// curvature instead of following the whole face out to its real edges.
	constexpr float kFeatureEdgeToleranceDegrees = 30.0f;
	const float cosThreshold = std::cos(kFeatureEdgeToleranceDegrees * 0.017453292519943295f);

	const std::vector<std::array<int, 3>>& adjacency = mesh->getTriangleAdjacency();
	if (static_cast<size_t>(ref.triangleIndex) >= adjacency.size())
		return false;  // adjacency build failed (non-triangle-mesh guard in buildTriangleAdjacency())

	std::vector<bool> visited(triCount, false);
	QVector<int> queue;
	queue.append(ref.triangleIndex);
	visited[static_cast<size_t>(ref.triangleIndex)] = true;

	std::vector<QVector3D> normalCache(triCount);
	normalCache[static_cast<size_t>(ref.triangleIndex)] = seedNormalRaw.normalized();

	for (int qi = 0; qi < queue.size(); ++qi)
	{
		const int t = queue[qi];
		outTriangleIndices.append(t);
		const QVector3D tNormal = normalCache[static_cast<size_t>(t)];

		for (int neighbor : adjacency[static_cast<size_t>(t)])
		{
			if (neighbor < 0 || visited[static_cast<size_t>(neighbor)])
				continue;
			const QVector3D n = triangleNormal(neighbor);
			if (n.lengthSquared() < 1.0e-12f)
				continue;  // skip a degenerate triangle rather than letting it break the chain
			const QVector3D nNorm = n.normalized();
			if (QVector3D::dotProduct(nNorm, tNormal) < cosThreshold)
				continue;  // a real feature/dihedral edge - the face boundary
			visited[static_cast<size_t>(neighbor)] = true;
			normalCache[static_cast<size_t>(neighbor)] = nNorm;
			queue.append(neighbor);
		}
	}

	return true;
}

bool MeasurementController::resolveMeasurementMinDistance(const Measurement& m,
	QVector3D& outPointA, QVector3D& outPointB, float& outDistance) const
{
	if (m.anchors.size() < 2)
		return false;

	QVector<int> trianglesA, trianglesB;
	if (!resolveMeasurementFaceRegion(m.anchors[0], trianglesA) || trianglesA.isEmpty())
		return false;
	if (!resolveMeasurementFaceRegion(m.anchors[1], trianglesB) || trianglesB.isEmpty())
		return false;

	SceneMesh* meshA = getMeshByUuid(m.anchors[0].meshUuid);
	SceneMesh* meshB = getMeshByUuid(m.anchors[1].meshUuid);
	if (!meshA || !meshB)
		return false;

	const std::vector<unsigned int> indicesA = meshA->indices();
	const std::vector<unsigned int> indicesB = meshB->indices();
	const std::vector<float>& trsfA = meshA->getTrsfPoints();
	const std::vector<float>& trsfB = meshB->getTrsfPoints();

	auto vertexPosA = [&trsfA](unsigned int vIdx) -> QVector3D {
		const size_t p = static_cast<size_t>(vIdx) * 3;
		if (p + 2 >= trsfA.size())
			return QVector3D();
		return QVector3D(trsfA[p], trsfA[p + 1], trsfA[p + 2]);
	};
	auto vertexPosB = [&trsfB](unsigned int vIdx) -> QVector3D {
		const size_t p = static_cast<size_t>(vIdx) * 3;
		if (p + 2 >= trsfB.size())
			return QVector3D();
		return QVector3D(trsfB[p], trsfB[p + 1], trsfB[p + 2]);
	};

	// Unique vertex set per region (many triangles share vertices), each
	// tested against every triangle of the OTHER region via the exact
	// closest-point-on-triangle routine (MeasurementGeometry::
	// closestPointOnTriangle()) - brute-force, since no spatial
	// acceleration structure exists anywhere in this codebase, but bounded
	// by a single flood-filled face's triangle count rather than the
	// whole scene, so this is fine for a one-shot user action. Checking
	// both directions (A's vertices vs B's triangles, and B's vertices vs
	// A's triangles) covers every true closest-point case except a rare
	// skew edge-edge minimum that misses every vertex on both sides -
	// negligible at normal CAD/mesh tessellation density.
	QSet<unsigned int> vertsA, vertsB;
	for (int t : trianglesA)
	{
		const size_t base = static_cast<size_t>(t) * 3;
		vertsA.insert(indicesA[base]);
		vertsA.insert(indicesA[base + 1]);
		vertsA.insert(indicesA[base + 2]);
	}
	for (int t : trianglesB)
	{
		const size_t base = static_cast<size_t>(t) * 3;
		vertsB.insert(indicesB[base]);
		vertsB.insert(indicesB[base + 1]);
		vertsB.insert(indicesB[base + 2]);
	}

	float bestDistSq = std::numeric_limits<float>::max();
	QVector3D bestA, bestB;

	for (unsigned int vA : vertsA)
	{
		const QVector3D pA = vertexPosA(vA);
		for (int t : trianglesB)
		{
			const size_t base = static_cast<size_t>(t) * 3;
			const QVector3D closest = MeasurementGeometry::closestPointOnTriangle(pA,
				vertexPosB(indicesB[base]), vertexPosB(indicesB[base + 1]), vertexPosB(indicesB[base + 2]));
			const float d2 = (closest - pA).lengthSquared();
			if (d2 < bestDistSq)
			{
				bestDistSq = d2;
				bestA = pA;
				bestB = closest;
			}
		}
	}
	for (unsigned int vB : vertsB)
	{
		const QVector3D pB = vertexPosB(vB);
		for (int t : trianglesA)
		{
			const size_t base = static_cast<size_t>(t) * 3;
			const QVector3D closest = MeasurementGeometry::closestPointOnTriangle(pB,
				vertexPosA(indicesA[base]), vertexPosA(indicesA[base + 1]), vertexPosA(indicesA[base + 2]));
			const float d2 = (closest - pB).lengthSquared();
			if (d2 < bestDistSq)
			{
				bestDistSq = d2;
				bestA = closest;
				bestB = pB;
			}
		}
	}

	outPointA = bestA;
	outPointB = bestB;
	outDistance = std::sqrt(bestDistSq);
	return true;
}

bool MeasurementController::resolveMeasurementCylindricalDiameter(const MeasurementAnchorRef& ref,
	float& outDiameter, QVector3D& outAxisOrigin, QVector3D& outAxisDir,
	QVector3D& outPickedPoint, bool& outIsCone) const
{
	if (ref.triangleIndex < 0)
		return false;

	SceneMesh* mesh = getMeshByUuid(ref.meshUuid);
	if (!mesh)
		return false;

	// Sparse per-triangle lookup (lazily built, cached on the mesh) - NOT
	// a triangle-range table, since SceneMesh::optimizeMesh() reorders
	// triangles for GPU cache locality and a face's triangles are no
	// longer contiguous afterward (see MeshImportAdaptor::setOccFaceData()'s
	// doc comment).
	const int faceIdx = mesh->getOccTriangleFaceIndex(ref.triangleIndex);
	if (faceIdx < 0)
	{
		// No OCC B-Rep data at all (non-CAD mesh) - try a direct cylinder/
		// cone fit over a local mesh patch instead of failing outright.
		return resolveMeasurementCylindricalDiameterFromMeshFit(
			ref, mesh, outDiameter, outAxisOrigin, outAxisDir, outPickedPoint, outIsCone);
	}

	const std::vector<OccFaceAxisInfo>& faceAxes = mesh->getOccFaceAxes();
	if (static_cast<size_t>(faceIdx) >= faceAxes.size())
		return false;

	const OccFaceAxisInfo& axis = faceAxes[static_cast<size_t>(faceIdx)];
	if (!axis.isCylinder && !axis.isCone)
		return false;  // picked triangle's face isn't cylindrical/conical

	// Live world-space picked point (same "current, not frozen" convention
	// as every other resolver in this file) plus the axis, transformed by
	// the mesh's current world transform.
	outPickedPoint = resolveMeasurementAnchor(ref);
	const QMatrix4x4 combined = mesh->combinedRenderTransform();
	outAxisOrigin = combined.map(QVector3D(static_cast<float>(axis.originX), static_cast<float>(axis.originY), static_cast<float>(axis.originZ)));
	outAxisDir = combined.mapVector(QVector3D(static_cast<float>(axis.axisX), static_cast<float>(axis.axisY), static_cast<float>(axis.axisZ))).normalized();
	outIsCone = axis.isCone;

	// The diameter AT the picked point: twice its perpendicular distance
	// to the axis line - exact for a cylinder (constant everywhere on the
	// face) and a cone (genuinely varies with position) alike, so no
	// separate per-surface-type formula is needed - see OccFaceAxis's doc
	// comment (BRepToAssimpConverter.h) for why no radius is stored at all.
	const QVector3D toPoint = outPickedPoint - outAxisOrigin;
	const QVector3D radial = toPoint - outAxisDir * QVector3D::dotProduct(toPoint, outAxisDir);
	outDiameter = radial.length() * 2.0f;
	return true;
}

namespace {

// Minimal CGAL ReadablePropertyMap wrappers directly over SceneMesh's own
// flat world-space float buffers (getTrsfPoints() / a caller-supplied
// sign-corrected normal buffer in the same layout) - avoids copying the
// whole mesh into a CGAL point set just to hand a local patch to
// Region_growing. Satisfies boost::property_traits via the nested typedefs
// alone (no separate specialization needed); the free get() functions are
// found via ADL from this (anonymous) namespace, the standard pattern for
// a lightweight custom property map.
using CylFitKernel = CGAL::Exact_predicates_inexact_constructions_kernel;

// Both resolveMeasurementCylindricalDiameterViaRegionGrowing() and the
// legacy resolveMeasurementCylindricalDiameterFromMeshFit() run on every
// mouse-move (hover preview) and every render frame for each already-
// placed measurement, so their qDebug() diagnostic trails must stay off by
// default - flip to true only when actively investigating a detection
// issue, never leave it on in a committed build.
constexpr bool kCylFitVerbose = false;

struct MeshVertexPointMap
{
	using key_type = unsigned int;
	using value_type = CylFitKernel::Point_3;
	using reference = value_type;
	using category = boost::readable_property_map_tag;
	const std::vector<float>* positions = nullptr;
};
inline MeshVertexPointMap::value_type get(const MeshVertexPointMap& m, MeshVertexPointMap::key_type v)
{
	const size_t p = static_cast<size_t>(v) * 3;
	return MeshVertexPointMap::value_type((*m.positions)[p], (*m.positions)[p + 1], (*m.positions)[p + 2]);
}

// Sparse (candidate-set-only) normal map - deliberately NOT a flat buffer
// sized to the whole mesh's vertex count. This ran once per mouse-move and
// once per placed measurement per render frame; a full-mesh-sized
// std::vector<float> allocated and zero-filled on every one of those calls
// was a real, confirmed performance regression on any mesh with a
// non-trivial vertex count, independent of the candidate patch's own
// (small, bounded) size.
struct MeshVertexNormalMap
{
	using key_type = unsigned int;
	using value_type = CylFitKernel::Vector_3;
	using reference = value_type;
	using category = boost::readable_property_map_tag;
	const std::unordered_map<unsigned int, CylFitKernel::Vector_3>* normals = nullptr;
};
inline MeshVertexNormalMap::value_type get(const MeshVertexNormalMap& m, MeshVertexNormalMap::key_type v)
{
	const auto it = m.normals->find(v);
	return it != m.normals->end() ? it->second : MeshVertexNormalMap::value_type(0, 0, 1);
}

// NeighborQuery model (CGAL::Shape_detection concept) over a precomputed
// vertex-adjacency map restricted to the local candidate set gathered
// before growth starts - see the doc comment on
// resolveMeasurementCylindricalDiameterViaRegionGrowing() for why that
// pre-gather step exists (bounding cost, not correctness - CGAL's own
// region type does the actual correctness filtering).
class MeshVertexNeighborQuery
{
public:
	using Item = unsigned int;
	explicit MeshVertexNeighborQuery(const std::unordered_map<unsigned int, std::vector<unsigned int>>& adjacency)
		: m_adjacency(adjacency) {}
	void operator()(const Item query, std::vector<Item>& neighbors) const
	{
		neighbors.clear();
		const auto it = m_adjacency.find(query);
		if (it != m_adjacency.end())
			neighbors = it->second;
	}
private:
	const std::unordered_map<unsigned int, std::vector<unsigned int>>& m_adjacency;
};

} // namespace

// CGAL::Shape_detection::Region_growing, paired with
// Least_squares_cylinder_fit_region as the region type and a custom
// mesh-vertex-adjacency NeighborQuery, grown from the single vertex
// nearest the picked triangle. This is the primary, preferred cylinder fit
// for non-CAD meshes - see [[project_cgal_capabilities_reference]] (project
// memory) for how this was found: CGAL::Shape_detection::Efficient_RANSAC
// (tried first, this session) is architecturally wrong for a patch this
// small (octree-based candidate sampling degenerates below ~1000 points,
// confirmed by tracing its detect() loop directly), and a hand-rolled
// PCA-of-normals + project + circle-fit (tried second) only checks normal
// continuity between adjacent triangles while growing, with no way to
// notice it has wandered onto a different surface - which is exactly how
// it kept bleeding across tangent-continuous seams (a fillet blending into
// its adjacent flat face, or through an intersecting second bore) despite
// an escalating pile of after-the-fact statistical gates (residual, arc
// coverage, footprint, normal-azimuth, reach-jump) bolted on to catch the
// symptoms. Region_growing's is_part_of_region() checks DISTANCE from each
// growth candidate to the CURRENTLY-fitted cylinder surface (refit after
// every accepted point) and normal-radial alignment against the CURRENT
// axis - it can't wander the same way, because a point on an unrelated
// surface fails the distance check immediately regardless of how gradually
// growth got there.
//
// Cylinders only (Least_squares_cylinder_fit_region has no cone variant) -
// returns false (not an error, just "try something else") for cones, or
// for a seed not on/near a cylindrical surface at all; the caller
// (resolveMeasurementCylindricalDiameterFromMeshFit()) falls back to the
// legacy PCA+circle-fit path below on false, which still handles cones
// acceptably.
bool MeasurementController::resolveMeasurementCylindricalDiameterViaRegionGrowing(const MeasurementAnchorRef& ref, SceneMesh* mesh,
	float& outDiameter, QVector3D& outAxisOrigin, QVector3D& outAxisDir,
	QVector3D& outPickedPoint, bool& outIsCone) const
{
	const std::vector<std::array<int, 3>>& adjacency = mesh->getTriangleAdjacency();
	if (static_cast<size_t>(ref.triangleIndex) >= adjacency.size())
		return false;

	const std::vector<unsigned int> indices = mesh->indices();
	const std::vector<float>& trsfPoints = mesh->getTrsfPoints();
	const std::vector<float>& trsfNormals = mesh->getTrsfNormals();

	auto vertexPos = [&](unsigned int vIdx) -> QVector3D {
		const size_t p = static_cast<size_t>(vIdx) * 3;
		if (p + 2 >= trsfPoints.size())
			return QVector3D();
		return QVector3D(trsfPoints[p], trsfPoints[p + 1], trsfPoints[p + 2]);
	};
	auto triangleFaceNormal = [&](int t) -> QVector3D {
		const size_t base = static_cast<size_t>(t) * 3;
		if (base + 2 >= indices.size())
			return QVector3D();
		const QVector3D p0 = vertexPos(indices[base]);
		const QVector3D p1 = vertexPos(indices[base + 1]);
		const QVector3D p2 = vertexPos(indices[base + 2]);
		return QVector3D::crossProduct(p1 - p0, p2 - p0);
	};

	// Gather a bounded candidate set via plain topological BFS - no normal
	// filtering here at all, unlike the legacy grower's curvature-following
	// flood fill. This step exists ONLY to bound the cost of what follows
	// (so this doesn't scan an entire large mesh on every mouse-move); the
	// actual decision about where the real surface stops is left entirely
	// to Region_growing's own distance-to-fitted-cylinder condition below.
	constexpr size_t kMaxCandidateTriangles = 600;
	std::vector<bool> visitedTri(adjacency.size(), false);
	std::vector<int> candidateTriangles;
	std::vector<int> frontier;
	candidateTriangles.push_back(ref.triangleIndex);
	frontier.push_back(ref.triangleIndex);
	visitedTri[static_cast<size_t>(ref.triangleIndex)] = true;
	while (!frontier.empty() && candidateTriangles.size() < kMaxCandidateTriangles)
	{
		std::vector<int> nextFrontier;
		for (int t : frontier)
		{
			for (int neighbor : adjacency[static_cast<size_t>(t)])
			{
				if (neighbor < 0 || visitedTri[static_cast<size_t>(neighbor)])
					continue;
				visitedTri[static_cast<size_t>(neighbor)] = true;
				candidateTriangles.push_back(neighbor);
				nextFrontier.push_back(neighbor);
			}
		}
		frontier = std::move(nextFrontier);
	}

	// Build the candidate vertex set, sign-corrected per-vertex normals
	// (averaged across incident candidate triangles, each corrected against
	// ITS OWN winding-based face normal first - imported normals are not
	// guaranteed outward-facing, confirmed inverted on a real test asset
	// earlier this session), and a vertex-adjacency map restricted to this
	// candidate set (Region_growing must never be offered a neighbor
	// outside the set it was constructed with).
	std::unordered_map<unsigned int, QVector3D> vertexNormalSum;
	std::unordered_set<unsigned int> candidateVertexSet;
	std::unordered_map<unsigned int, std::vector<unsigned int>> vertexAdjacency;
	double edgeLengthSum = 0.0;
	size_t edgeCount = 0;

	for (int t : candidateTriangles)
	{
		const size_t base = static_cast<size_t>(t) * 3;
		if (base + 2 >= indices.size())
			continue;
		const QVector3D faceNormal = triangleFaceNormal(t);
		if (faceNormal.lengthSquared() < 1.0e-12f)
			continue;
		const QVector3D faceNormalUnit = faceNormal.normalized();
		const unsigned int vIdx[3] = { indices[base], indices[base + 1], indices[base + 2] };
		for (int c = 0; c < 3; ++c)
		{
			candidateVertexSet.insert(vIdx[c]);
			const size_t p = static_cast<size_t>(vIdx[c]) * 3;
			if (p + 2 >= trsfNormals.size())
				continue;
			QVector3D n(trsfNormals[p], trsfNormals[p + 1], trsfNormals[p + 2]);
			if (n.lengthSquared() < 1.0e-12f)
				continue;
			n.normalize();
			if (QVector3D::dotProduct(n, faceNormalUnit) < 0.0f)
				n = -n;
			vertexNormalSum[vIdx[c]] += n;
		}
		for (int e = 0; e < 3; ++e)
		{
			const unsigned int a = vIdx[e];
			const unsigned int b = vIdx[(e + 1) % 3];
			vertexAdjacency[a].push_back(b);
			vertexAdjacency[b].push_back(a);
			edgeLengthSum += (vertexPos(a) - vertexPos(b)).length();
			++edgeCount;
		}
	}

	// Need enough distinct points for a stable fit - matches the legacy
	// path's own floor.
	constexpr size_t kMinPatchPoints = 24;
	if (kCylFitVerbose) qDebug() << "[CylFitRG] candidate vertices" << candidateVertexSet.size() << "from" << candidateTriangles.size() << "triangles, seed" << ref.triangleIndex;
	if (candidateVertexSet.size() < kMinPatchPoints || edgeCount == 0)
		return false;

	// Averaged, sign-corrected normals, keyed sparsely by candidate vertex -
	// see MeshVertexNormalMap's doc comment for why this is a map, not a
	// full-mesh-sized flat buffer.
	std::unordered_map<unsigned int, CylFitKernel::Vector_3> correctedNormals;
	correctedNormals.reserve(vertexNormalSum.size());
	for (const auto& kv : vertexNormalSum)
	{
		QVector3D avg = kv.second;
		if (avg.lengthSquared() < 1.0e-12f)
			continue;
		avg.normalize();
		correctedNormals.emplace(kv.first, CylFitKernel::Vector_3(avg.x(), avg.y(), avg.z()));
	}

	const std::vector<unsigned int> candidateItems(candidateVertexSet.begin(), candidateVertexSet.end());
	const unsigned int seedVertex = indices[static_cast<size_t>(ref.triangleIndex) * 3];
	std::vector<unsigned int> seedItems{ seedVertex };

	MeshVertexPointMap pointMap;
	pointMap.positions = &trsfPoints;
	MeshVertexNormalMap normalMap;
	normalMap.normals = &correctedNormals;
	MeshVertexNeighborQuery neighborQuery(vertexAdjacency);

	using RegionType = CGAL::Shape_detection::Point_set::Least_squares_cylinder_fit_region<
		CylFitKernel, unsigned int, MeshVertexPointMap, MeshVertexNormalMap>;
	using RegionGrowing = CGAL::Shape_detection::Region_growing<MeshVertexNeighborQuery, RegionType>;

	// maximum_distance is an ABSOLUTE length, and this mesh's unit scale is
	// unknown in advance (mm, m, or arbitrary CAD units) - derive it from
	// the candidate patch's own average edge length instead of a fixed
	// constant, so this adapts to whatever scale/tessellation density the
	// mesh actually has.
	const double avgEdgeLength = edgeLengthSum / double(edgeCount);
	const double maxDistance = std::max(avgEdgeLength * 1.5, 1.0e-6);

	RegionType regionType(
		CGAL::parameters::point_map(pointMap)
			.normal_map(normalMap)
			.maximum_distance(maxDistance)
			.maximum_angle(35.0)
			.minimum_region_size(kMinPatchPoints));
	RegionGrowing regionGrowing(candidateItems, seedItems, neighborQuery, regionType);

	if (kCylFitVerbose) qDebug() << "[CylFitRG] maximum_distance" << maxDistance << "(avg edge length" << avgEdgeLength << ")";

	std::vector<std::pair<RegionType::Primitive, std::vector<unsigned int>>> results;
	regionGrowing.detect(std::back_inserter(results));
	if (results.empty())
	{
		if (kCylFitVerbose) qDebug() << "[CylFitRG] no region grown from seed - not a cylinder (or caller should fall back)";
		return false;
	}

	const RegionType::Primitive& primitive = results.front().first;
	const double radius = CGAL::to_double(primitive.radius);
	if (!(radius > 0.0))
	{
		if (kCylFitVerbose) qDebug() << "[CylFitRG] region grown but radius invalid:" << radius;
		return false;
	}
	// CGAL's own region-growing has two gaps that can let contamination
	// through despite the distance-to-fitted-cylinder growth condition:
	// is_part_of_region() unconditionally accepts the FIRST 6 points in a
	// region before any check runs at all ("need 6 points before the fit
	// means anything", per its own doc comment) - if the seed sits right at
	// a tangent-continuous seam (a fillet blending into its adjacent flat
	// face), those first few free points can come from both surfaces and
	// skew the very first fit - and propagate() never retroactively removes
	// an already-accepted point that fails a LATER refit's own consistency
	// check, it just stops growing and keeps whatever's already in the
	// region (confirmed directly against a real corner-fillet case that
	// still produced a wrong ~59mm-diameter circle after switching to
	// Region_growing). Close both gaps with one direct check: does every
	// point in the FINAL accepted region actually sit close to the FINAL
	// fitted cylinder surface, not just whatever intermediate fit existed
	// when each point was originally admitted.
	const std::vector<unsigned int>& regionItems = results.front().second;
	double maxResidual = 0.0;
	for (unsigned int item : regionItems)
	{
		const CylFitKernel::Point_3 pt = get(pointMap, item);
		const double distSq = CGAL::to_double(CGAL::squared_distance(pt, primitive.axis));
		maxResidual = std::max(maxResidual, std::fabs(std::sqrt(distSq) - radius));
	}
	constexpr double kResidualSlack = 1.5;
	if (maxResidual > maxDistance * kResidualSlack)
	{
		if (kCylFitVerbose) qDebug() << "[CylFitRG] rejected - final region residual" << maxResidual << "exceeds tolerance" << (maxDistance * kResidualSlack) << "(CGAL grace-period/no-retroactive-pruning gap)";
		return false;
	}
	if (kCylFitVerbose) qDebug() << "[CylFitRG] accepted, region size" << regionItems.size() << "radius" << radius << "max residual" << maxResidual;

	const CylFitKernel::Point_3 axisPoint = primitive.axis.point();
	const CylFitKernel::Vector_3 axisVector = primitive.axis.to_vector();
	QVector3D axisOrigin(
		float(CGAL::to_double(axisPoint.x())),
		float(CGAL::to_double(axisPoint.y())),
		float(CGAL::to_double(axisPoint.z())));
	QVector3D axisDir(
		float(CGAL::to_double(axisVector.x())),
		float(CGAL::to_double(axisVector.y())),
		float(CGAL::to_double(axisVector.z())));
	if (axisDir.lengthSquared() < 1.0e-12f)
		return false;
	axisDir.normalize();

	outPickedPoint = resolveMeasurementAnchor(ref);
	outAxisOrigin = axisOrigin;
	outAxisDir = axisDir;
	outIsCone = false;
	outDiameter = float(radius) * 2.0f;
	return true;
}

// Cached entry point - see CylMeshFitCacheEntry's doc comment in the header
// for why this exists (both the Region Growing and legacy paths below are
// too expensive to re-run from scratch on every mouse-move and every
// render frame for every placed measurement). Cache miss/staleness falls
// through to resolveMeasurementCylindricalDiameterFromMeshFitUncached(),
// which holds the actual resolve logic unchanged.
bool MeasurementController::resolveMeasurementCylindricalDiameterFromMeshFit(const MeasurementAnchorRef& ref, SceneMesh* mesh,
	float& outDiameter, QVector3D& outAxisOrigin, QVector3D& outAxisDir,
	QVector3D& outPickedPoint, bool& outIsCone) const
{
	outPickedPoint = resolveMeasurementAnchor(ref);

	const std::vector<float>& trsfPoints = mesh->getTrsfPoints();
	auto posOf = [&](unsigned int vIdx) -> QVector3D {
		const size_t p = static_cast<size_t>(vIdx) * 3;
		if (p + 2 >= trsfPoints.size())
			return QVector3D();
		return QVector3D(trsfPoints[p], trsfPoints[p + 1], trsfPoints[p + 2]);
	};

	const auto cacheKey = std::make_pair(static_cast<const SceneMesh*>(mesh), ref.triangleIndex);
	const auto cacheIt = m_cylMeshFitCache.find(cacheKey);
	if (cacheIt != m_cylMeshFitCache.end())
	{
		const CylMeshFitCacheEntry& entry = cacheIt->second;
		if (posOf(entry.seedVertexIndices[0]) == entry.seedPos[0]
			&& posOf(entry.seedVertexIndices[1]) == entry.seedPos[1]
			&& posOf(entry.seedVertexIndices[2]) == entry.seedPos[2])
		{
			if (!entry.valid)
				return false;
			outDiameter = entry.diameter;
			outAxisOrigin = entry.axisOrigin;
			outAxisDir = entry.axisDir;
			outIsCone = entry.isCone;
			return true;
		}
	}

	CylMeshFitCacheEntry entry;
	const std::vector<unsigned int> indices = mesh->indices();
	const size_t base = static_cast<size_t>(ref.triangleIndex) * 3;
	if (base + 2 < indices.size())
	{
		entry.seedVertexIndices[0] = indices[base];
		entry.seedVertexIndices[1] = indices[base + 1];
		entry.seedVertexIndices[2] = indices[base + 2];
		entry.seedPos[0] = posOf(entry.seedVertexIndices[0]);
		entry.seedPos[1] = posOf(entry.seedVertexIndices[1]);
		entry.seedPos[2] = posOf(entry.seedVertexIndices[2]);
	}

	entry.valid = resolveMeasurementCylindricalDiameterFromMeshFitUncached(
		ref, mesh, outDiameter, outAxisOrigin, outAxisDir, outPickedPoint, outIsCone);
	if (entry.valid)
	{
		entry.diameter = outDiameter;
		entry.axisOrigin = outAxisOrigin;
		entry.axisDir = outAxisDir;
		entry.isCone = outIsCone;
	}
	m_cylMeshFitCache[cacheKey] = entry;
	return entry.valid;
}

bool MeasurementController::resolveMeasurementCylindricalDiameterFromMeshFitUncached(const MeasurementAnchorRef& ref, SceneMesh* mesh,
	float& outDiameter, QVector3D& outAxisOrigin, QVector3D& outAxisDir,
	QVector3D& outPickedPoint, bool& outIsCone) const
{
	if (resolveMeasurementCylindricalDiameterViaRegionGrowing(
		ref, mesh, outDiameter, outAxisOrigin, outAxisDir, outPickedPoint, outIsCone))
		return true;

	// Grow a local patch via a curvature-aware flood fill - deliberately
	// NEITHER resolveMeasurementFaceRegion()'s ~30 degree neighbor-vs-
	// neighbor tolerance (too tight - stops after 1-2 triangles on a
	// coarsely-faceted cylinder/cone, e.g. an 8- or 12-facet non-CAD test
	// mesh, whose adjacent-facet normals alone can exceed 30 degrees) NOR
	// unfiltered N-ring adjacency (too loose - happily grows straight past
	// a boss/hole's real edge onto whatever flat plate it's mounted in,
	// which is extremely common mechanical-part geometry; a patch diluted
	// by a larger surrounding flat region pulls the normal-covariance axis
	// fit below toward "no consistent axis at all" and gets rejected
	// instead of fitting the smaller cylindrical feature the user actually
	// clicked). This threshold sits between the two: loose enough to keep
	// growing across typical coarse faceting within the SAME curved
	// surface, tight enough to still stop at a genuine sharp
	// boss-to-plate/hole-to-plate transition (usually close to 90 degrees).
	const std::vector<std::array<int, 3>>& adjacency = mesh->getTriangleAdjacency();
	if (static_cast<size_t>(ref.triangleIndex) >= adjacency.size())
		return false;

	const std::vector<unsigned int> patchIndices = mesh->indices();
	const std::vector<float>& patchTrsfPoints = mesh->getTrsfPoints();
	auto trianglePos = [&](unsigned int vIdx) -> QVector3D {
		const size_t p = static_cast<size_t>(vIdx) * 3;
		if (p + 2 >= patchTrsfPoints.size())
			return QVector3D();
		return QVector3D(patchTrsfPoints[p], patchTrsfPoints[p + 1], patchTrsfPoints[p + 2]);
	};
	auto triangleNormal = [&](int t) -> QVector3D {
		const size_t base = static_cast<size_t>(t) * 3;
		if (base + 2 >= patchIndices.size())
			return QVector3D();
		const QVector3D p0 = trianglePos(patchIndices[base]);
		const QVector3D p1 = trianglePos(patchIndices[base + 1]);
		const QVector3D p2 = trianglePos(patchIndices[base + 2]);
		return QVector3D::crossProduct(p1 - p0, p2 - p0);
	};

	constexpr float kPatchGrowToleranceDegrees = 65.0f;
	const float cosThreshold = std::cos(kPatchGrowToleranceDegrees * 0.017453292519943295f);
	constexpr int kMaxPatchTriangles = 300;  // safety cap, not a target - a real sharp edge should stop growth well before this

	// A tangent-continuous boundary (a fillet blending smoothly into an
	// adjacent flat face, with no real dihedral break) has no curvature
	// discontinuity for the neighbor-vs-neighbor angle test above to catch -
	// confirmed directly on a real test model: growth starting on a small
	// quarter-round notch reached 88.9mm from the seed, spanning nearly the
	// entire length of the part, because it bled straight through the seam
	// onto a large adjacent face. A genuinely local curved feature grows its
	// reach from the seed gradually, ring by ring, roughly in step with
	// mesh edge length; breaking through a tangent seam into a much larger
	// surrounding region shows up as a sudden, disproportionate jump in
	// reach within a SINGLE ring - stop growth right there rather than
	// accept whatever the seam happened to open onto.
	const size_t seedBase0 = static_cast<size_t>(ref.triangleIndex) * 3;
	QVector3D seedCentroid;
	float maxReachFromSeed = 0.0f;
	if (seedBase0 + 2 < patchIndices.size())
	{
		seedCentroid = (trianglePos(patchIndices[seedBase0]) + trianglePos(patchIndices[seedBase0 + 1]) + trianglePos(patchIndices[seedBase0 + 2])) / 3.0f;
		for (int c = 0; c < 3; ++c)
			maxReachFromSeed = std::max(maxReachFromSeed, (trianglePos(patchIndices[seedBase0 + c]) - seedCentroid).length());
	}
	// Calibrated directly from real log data: legitimate ring-to-ring reach
	// growth on real (later confirmed-good) features measured up to ~2.6x
	// in one ring (BFS growth over irregular mesh tessellation isn't
	// perfectly smooth even on a clean feature), while genuine tangent-seam
	// breaks measured 5.3x-11.75x - a wide, clean gap between the two
	// clusters. Sitting in the middle of that gap avoids rejecting real
	// small features while still catching the seam-break case.
	constexpr float kMaxGrowthJumpRatio = 4.0f;

	std::vector<bool> visited(adjacency.size(), false);
	QVector<int> patchTriangles;
	QVector<int> frontier;
	patchTriangles.append(ref.triangleIndex);
	frontier.append(ref.triangleIndex);
	visited[static_cast<size_t>(ref.triangleIndex)] = true;

	while (!frontier.isEmpty() && patchTriangles.size() < kMaxPatchTriangles)
	{
		QVector<int> nextFrontier;
		for (int t : frontier)
		{
			const QVector3D nT = triangleNormal(t);
			if (nT.lengthSquared() < 1.0e-12f)
				continue;
			const QVector3D nTNorm = nT.normalized();

			for (int neighbor : adjacency[static_cast<size_t>(t)])
			{
				if (neighbor < 0 || visited[static_cast<size_t>(neighbor)])
					continue;
				const QVector3D nN = triangleNormal(neighbor);
				if (nN.lengthSquared() < 1.0e-12f)
					continue;
				// Neighbor-vs-neighbor (the triangle being expanded from),
				// not a fixed seed - same "follow continuous curvature"
				// reasoning as resolveMeasurementFaceRegion(), just with a
				// looser threshold tuned for coarse non-CAD tessellation.
				if (QVector3D::dotProduct(nN.normalized(), nTNorm) < cosThreshold)
					continue;
				visited[static_cast<size_t>(neighbor)] = true;
				nextFrontier.append(neighbor);
			}
		}
		if (nextFrontier.isEmpty())
			break;

		float ringMaxReach = maxReachFromSeed;
		for (int t : nextFrontier)
		{
			const size_t base = static_cast<size_t>(t) * 3;
			if (base + 2 >= patchIndices.size())
				continue;
			for (int c = 0; c < 3; ++c)
				ringMaxReach = std::max(ringMaxReach, (trianglePos(patchIndices[base + c]) - seedCentroid).length());
		}
		if (maxReachFromSeed > 1.0e-4f && ringMaxReach > kMaxGrowthJumpRatio * maxReachFromSeed)
		{
			if (kCylFitVerbose) qDebug() << "[CylFit] growth stopped - reach jumped from" << maxReachFromSeed << "to" << ringMaxReach << "in one ring, likely broke through a tangent seam";
			break;
		}
		maxReachFromSeed = ringMaxReach;
		patchTriangles.append(nextFrontier);
		frontier = nextFrontier;
	}

	if (kCylFitVerbose) qDebug() << "[CylFit] patch grown to" << patchTriangles.size() << "triangles from seed" << ref.triangleIndex << "final reach from seed" << maxReachFromSeed;

	// Fit the axis directly from the patch's own point+normal data via a
	// small hand-rolled PCA - NOT CGAL Efficient_RANSAC. Traced directly into
	// CGAL's detect() loop: candidates are built from m_required_samples
	// points drawn from the tight LOCAL octree cell around one randomly
	// chosen point, and for a patch this small (order of 100-300 points,
	// capped by how much of the actual mesh even exists - OpenCylinder.obj's
	// entire 48-triangle/144-point mesh doesn't grow any further) those
	// local draws are nearly always near-parallel in their normals, making
	// the cylinder/cone axis (cross product of two sample normals)
	// indeterminate - candidate generation fails validity almost every
	// time, and with a best-so-far size of 0 ever found, CGAL's own
	// stop_probability() check is satisfied almost immediately, so detect()
	// returns having tried essentially nothing. Confirmed against a
	// mathematically perfect test cylinder (exact vertex positions, fine
	// 15-degree tessellation) via qDebug instrumentation: patch grows to the
	// full 144 points, but shapes() always comes back empty - an
	// algorithm/scale mismatch, not a tunable threshold.
	//
	// Direct fit instead: every point on a cylinder or cone has its surface
	// normal at one CONSTANT angle to the true axis (90 degrees for a
	// cylinder, some other fixed angle for a cone), so the patch's own
	// (sign-corrected) normals are confined to a band around the axis on the
	// unit sphere. The axis is therefore the eigenvector of SMALLEST
	// eigenvalue of sum(n_i * n_i^T) - normal scatter is smallest along the
	// axis, since no normal ever points that way. This uses every normal
	// once (no random sampling), so it doesn't inherit RANSAC's small-
	// sample-count failure mode.
	struct PatchSample { QVector3D pos; QVector3D normal; };
	std::vector<PatchSample> samples;
	samples.reserve(static_cast<size_t>(patchTriangles.size()) * 3);
	const std::vector<float>& trsfNormals = mesh->getTrsfNormals();
	for (int t : patchTriangles)
	{
		const size_t base = static_cast<size_t>(t) * 3;
		if (base + 2 >= patchIndices.size())
			continue;
		const QVector3D faceNormal = triangleNormal(t);
		if (faceNormal.lengthSquared() < 1.0e-12f)
			continue;
		for (size_t corner = 0; corner < 3; ++corner)
		{
			const unsigned int vIdx = patchIndices[base + corner];
			const size_t p = static_cast<size_t>(vIdx) * 3;
			if (p + 2 >= trsfNormals.size())
				continue;
			QVector3D n(trsfNormals[p], trsfNormals[p + 1], trsfNormals[p + 2]);
			if (n.lengthSquared() < 1.0e-12f)
				continue;
			n.normalize();
			// Re-orient against this triangle's own geometrically-computed
			// face normal (winding-based) rather than trusting the import's
			// stored sign, which is not guaranteed outward-facing (confirmed
			// inverted on at least one real non-CAD test asset) - sign
			// doesn't affect n*n^T itself, but does affect the cone/cylinder
			// angle check below.
			if (QVector3D::dotProduct(n, faceNormal) < 0.0f)
				n = -n;
			samples.push_back({ trianglePos(vIdx), n });
		}
	}

	// Need enough samples for a stable fit - arbitrary but reasonable floor,
	// may need tuning against real non-CAD test models.
	constexpr size_t kMinPatchPoints = 24;
	if (kCylFitVerbose) qDebug() << "[CylFit] point cloud size" << samples.size() << "(min required" << kMinPatchPoints << ")";
	if (samples.size() < kMinPatchPoints)
		return false;

	// Fit the axis (mean-CENTERED normal PCA, see below) and check that
	// every point's normal sits at one CONSISTENT angle to it - 90 degrees
	// for a cylinder, some other fixed angle for a cone. Returns false if
	// the eigen-structure is too flat/isotropic to define an axis at all
	// (e.g. a patch on a sphere/blob).
	auto fitAxisFromNormals = [](const std::vector<PatchSample>& pts, QVector3D& outAxis, double& outMeanCos, double& outCosStdDev) -> bool
	{
		// Mean-CENTER the normals before building the scatter matrix - a
		// cylinder's normals form a ring exactly 90 degrees from the axis
		// (a plane through the origin in normal-space), but a cone's
		// normals form a ring at some OTHER fixed angle, i.e. a plane
		// OFFSET from the origin. Using the raw (origin-referenced) second
		// moment sum(n*n^T) only finds the axis correctly for the 90-degree
		// case; this is the standard "fit a plane to a point cloud via PCA"
		// formulation instead (treating each sample's normal as a point on
		// the unit sphere), which recovers the axis regardless of the
		// ring's offset - handles cylinder and any cone half-angle
		// uniformly.
		QVector3D meanNormal;
		for (const PatchSample& s : pts)
			meanNormal += s.normal;
		meanNormal /= float(pts.size());

		double cov[3][3] = { {0,0,0}, {0,0,0}, {0,0,0} };
		for (const PatchSample& s : pts)
		{
			const double nx = double(s.normal.x()) - meanNormal.x();
			const double ny = double(s.normal.y()) - meanNormal.y();
			const double nz = double(s.normal.z()) - meanNormal.z();
			cov[0][0] += nx * nx; cov[0][1] += nx * ny; cov[0][2] += nx * nz;
			cov[1][1] += ny * ny; cov[1][2] += ny * nz;
			cov[2][2] += nz * nz;
		}
		cov[1][0] = cov[0][1]; cov[2][0] = cov[0][2]; cov[2][1] = cov[1][2];

		// Cyclic Jacobi eigenvalue decomposition - standard textbook
		// algorithm for small symmetric matrices, a handful of sweeps
		// fully converges 3x3.
		double eigenvectors[3][3] = { {1,0,0}, {0,1,0}, {0,0,1} };
		for (int sweep = 0; sweep < 50; ++sweep)
		{
			const double off = std::fabs(cov[0][1]) + std::fabs(cov[0][2]) + std::fabs(cov[1][2]);
			if (off < 1.0e-12)
				break;
			for (int p = 0; p < 2; ++p)
			{
				for (int q = p + 1; q < 3; ++q)
				{
					if (std::fabs(cov[p][q]) < 1.0e-15)
						continue;
					const double theta = (cov[q][q] - cov[p][p]) / (2.0 * cov[p][q]);
					const double t = (theta >= 0.0 ? 1.0 : -1.0) / (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
					const double c = 1.0 / std::sqrt(t * t + 1.0);
					const double s = t * c;
					const double app = cov[p][p], aqq = cov[q][q], apq = cov[p][q];
					cov[p][p] = app - t * apq;
					cov[q][q] = aqq + t * apq;
					cov[p][q] = cov[q][p] = 0.0;
					for (int i = 0; i < 3; ++i)
					{
						if (i != p && i != q)
						{
							const double aip = cov[i][p], aiq = cov[i][q];
							cov[i][p] = cov[p][i] = c * aip - s * aiq;
							cov[i][q] = cov[q][i] = s * aip + c * aiq;
						}
					}
					for (int i = 0; i < 3; ++i)
					{
						const double vip = eigenvectors[i][p], viq = eigenvectors[i][q];
						eigenvectors[i][p] = c * vip - s * viq;
						eigenvectors[i][q] = s * vip + c * viq;
					}
				}
			}
		}

		const double evals[3] = { cov[0][0], cov[1][1], cov[2][2] };
		int order[3] = { 0, 1, 2 };
		std::sort(order, order + 3, [&](int a, int b) { return evals[a] < evals[b]; });
		const int minIdx = order[0];
		const int midIdx = order[1];

		// Axis is ill-defined if the two smallest eigenvalues are too close
		// together (e.g. a patch on a sphere/blob, where normals scatter
		// evenly in every direction rather than banding around one axis) -
		// reject rather than report a meaningless axis.
		const double trace = evals[0] + evals[1] + evals[2];
		if (trace < 1.0e-9 || (evals[midIdx] - evals[minIdx]) < 0.05 * trace)
			return false;

		const float axisX = float(eigenvectors[0][minIdx]);
		const float axisY = float(eigenvectors[1][minIdx]);
		const float axisZ = float(eigenvectors[2][minIdx]);
		QVector3D axis(axisX, axisY, axisZ);
		if (axis.lengthSquared() < 1.0e-12f)
			return false;
		axis.normalize();

		double sumCos = 0.0, sumCosSq = 0.0;
		for (const PatchSample& s : pts)
		{
			const double c = QVector3D::dotProduct(s.normal, axis);
			sumCos += c;
			sumCosSq += c * c;
		}
		const double meanCos = sumCos / double(pts.size());
		const double cosStdDev = std::sqrt(std::max(0.0, sumCosSq / double(pts.size()) - meanCos * meanCos));

		outAxis = axis;
		outMeanCos = meanCos;
		outCosStdDev = cosStdDev;
		return true;
	};

	// Fit, then iteratively TRIM outliers and re-fit rather than rejecting
	// outright on the first inconsistent result. A patch can legitimately
	// pick up contamination from a DIFFERENT nearby surface of revolution -
	// e.g. two bores at different angles/axes that intersect each other,
	// or growth bleeding through a tangent-continuous seam onto an
	// adjacent surface, in either case without a clean stopping point for
	// the growth heuristics above to catch. Two INDEPENDENT signals matter
	// here, and a patch can pass one while still failing the other -
	// confirmed directly: a real contaminated patch survived normal-based
	// trimming down to a "consistent-looking" 474 points (every point's
	// outward direction agreed with the fitted axis), yet still showed a
	// 48%+ positional residual (those same points didn't actually SIT on
	// the resulting circle) - so both normal consistency and positional
	// (circle-residual) consistency need to converge together, trimming by
	// whichever signal is worse each iteration, before the fit is trusted.
	constexpr double kMaxCosStdDev = 0.17;
	constexpr double kMaxRmsResidualRatio = 0.10;
	// RMS alone lets a MINORITY of badly-contaminated points slide through
	// if the rest of the patch fits well - exactly what happens when growth
	// bleeds a little way past a tangent-continuous fillet boundary onto
	// the adjacent flat face (confirmed directly: a real rejected-looking
	// case measured rms 0.044 - comfortably under 0.10 - but max 0.224, a
	// single badly-off subset the RMS average buried). Gate on the worst
	// point too, not just the average.
	constexpr float kMaxSingleResidualRatio = 0.15f;
	constexpr int kMaxTrimIterations = 6;
	std::vector<PatchSample> inliers = samples;
	QVector3D axis;
	double meanCos = 0.0, cosStdDev = 0.0;
	QVector3D centroid;
	QVector<QVector3D> projected;
	MeasurementGeometry::PitchCircleResult circleFit;
	double rmsResidualRatio = 0.0;
	float maxResidualRatio = 0.0f;
	bool consistent = false;

	for (int iter = 0; iter < kMaxTrimIterations; ++iter)
	{
		if (inliers.size() < kMinPatchPoints)
			break;
		if (!fitAxisFromNormals(inliers, axis, meanCos, cosStdDev))
		{
			if (kCylFitVerbose) qDebug() << "[CylFit] axis ill-defined on iteration" << iter << "with" << inliers.size() << "points - not a surface of revolution";
			break;
		}
		if (kCylFitVerbose) qDebug() << "[CylFit] iter" << iter << "points" << inliers.size() << "mean cos" << meanCos << "stddev" << cosStdDev << "(max allowed" << kMaxCosStdDev << ")";
		if (cosStdDev > kMaxCosStdDev)
		{
			std::vector<PatchSample> trimmed;
			trimmed.reserve(inliers.size());
			for (const PatchSample& s : inliers)
			{
				const double c = QVector3D::dotProduct(s.normal, axis);
				if (std::fabs(c - meanCos) <= 2.0 * cosStdDev)
					trimmed.push_back(s);
			}
			if (trimmed.size() == inliers.size() || trimmed.size() < kMinPatchPoints)
				break;  // no progress, or trimmed away too much - stop rather than loop pointlessly
			inliers = std::move(trimmed);
			continue;
		}

		// Normals are consistent for this batch - now check whether the
		// points actually SIT on the resulting circle.
		centroid = QVector3D();
		for (const PatchSample& s : inliers)
			centroid += s.pos;
		centroid /= float(inliers.size());

		projected.clear();
		projected.reserve(static_cast<int>(inliers.size()));
		for (const PatchSample& s : inliers)
			projected.append(s.pos - axis * QVector3D::dotProduct(s.pos - centroid, axis));

		circleFit = MeasurementGeometry::fitPitchCircle(projected);
		if (!circleFit.valid || circleFit.radius <= 1.0e-6f)
		{
			if (kCylFitVerbose) qDebug() << "[CylFit] cross-section circle fit failed on iteration" << iter;
			break;
		}

		double residualSumSq = 0.0;
		maxResidualRatio = 0.0f;
		QVector<float> residualRatios;
		residualRatios.reserve(projected.size());
		for (const QVector3D& p : projected)
		{
			const float residualRatio = std::fabs((p - circleFit.center).length() - circleFit.radius) / circleFit.radius;
			residualRatios.append(residualRatio);
			residualSumSq += double(residualRatio) * double(residualRatio);
			maxResidualRatio = std::max(maxResidualRatio, residualRatio);
		}
		rmsResidualRatio = std::sqrt(residualSumSq / double(projected.size()));
		if (kCylFitVerbose) qDebug() << "[CylFit] iter" << iter << "cross-section radial residual rms" << rmsResidualRatio << "max" << maxResidualRatio << "(max rms allowed" << kMaxRmsResidualRatio << ", max single allowed" << kMaxSingleResidualRatio << ")";

		if (rmsResidualRatio <= kMaxRmsResidualRatio && maxResidualRatio <= kMaxSingleResidualRatio)
		{
			consistent = true;
			break;
		}

		// Trim the worst-residual points and re-fit - this re-fits the
		// AXIS too on the next iteration, not just the circle, since
		// removing contaminating points can shift the axis estimate as
		// well as the circle.
		std::vector<PatchSample> trimmed;
		trimmed.reserve(inliers.size());
		for (int i = 0; i < static_cast<int>(inliers.size()); ++i)
		{
			// Bounded by the fixed pass/fail bar itself, NOT scaled by the
			// current rms - a "2x rms" relative threshold guarantees zero
			// progress whenever rms is already above half the bar (this was
			// a real bug: a patch measuring rms 0.48 got a trim threshold
			// of 0.96, comfortably above even its worst single point at
			// 0.69, so nothing was ever removed and the loop gave up
			// immediately every time). Always cut anything past the bar.
			if (residualRatios[i] <= kMaxSingleResidualRatio)
				trimmed.push_back(inliers[static_cast<size_t>(i)]);
		}
		if (trimmed.size() == inliers.size() || trimmed.size() < kMinPatchPoints)
			break;  // no progress, or trimmed away too much
		inliers = std::move(trimmed);
	}

	if (!consistent)
	{
		if (kCylFitVerbose) qDebug() << "[CylFit] rejected - patch never converged to a consistent circle (by normal direction or position) even after trimming";
		return false;
	}

	// Trimming converging isn't by itself proof of a good fit - a handful
	// of coincidentally close-together points ALWAYS trims down to a
	// trivially self-consistent tiny circle (near-zero residual, since
	// there's nothing left to disagree), which is exactly how aggressive
	// residual-based trimming can collapse a large, genuinely contaminated
	// patch down to a spurious few-point remnant with a meaningless radius
	// (confirmed directly: introducing a hard residual cutoff, needed to
	// fix the trim loop never removing anything, immediately surfaced this
	// opposite failure - multiple different tiny, mutually-inconsistent
	// "diameters" a few mm apart on the same real boss). Require the
	// surviving inliers to still be a MEANINGFUL fraction of the original
	// grown patch, not just >= the absolute floor kMinPatchPoints.
	constexpr double kMinRetainedFraction = 0.5;
	if (double(inliers.size()) < kMinRetainedFraction * double(samples.size()))
	{
		if (kCylFitVerbose) qDebug() << "[CylFit] rejected - only" << inliers.size() << "of" << samples.size() << "points survived trimming, too little left to trust";
		return false;
	}

	const bool isCone = std::fabs(meanCos) > 0.15;  // ~81-99 degrees treated as "cylinder enough"

	// A near-FLAT patch (e.g. a nominally flat CAD face with just enough
	// tessellation/vertex-normal noise to trace a very shallow, but
	// statistically clean, band around SOME axis) can pass every check
	// above - the fit is genuinely self-consistent, just describing an
	// implausibly huge radius instead of "not curved at all". Every check
	// so far (residual ratio, position arc coverage, footprint ratio) is
	// scale-invariant relative to the fit's OWN radius/center, so none of
	// them can tell a small patch on a real small cylinder apart from a
	// huge patch that's barely curved. Cross-check against the normals
	// directly instead of positions: for a true cylinder/cone, moving
	// around the surface by some angle rotates the surface normal's
	// AZIMUTH (its direction around the axis) by that same angle - so the
	// normals' own azimuthal spread should independently corroborate the
	// position-based arc coverage above. A barely-curved "huge cylinder"
	// fit to a near-flat patch has normals that barely rotate at all, no
	// matter how large a position-based arc the (wrong) fit reports.
	QVector3D basisU = QVector3D::crossProduct(axis, QVector3D(0, 0, 1));
	if (basisU.lengthSquared() < 1.0e-6f)
		basisU = QVector3D::crossProduct(axis, QVector3D(0, 1, 0));
	basisU.normalize();
	const QVector3D basisV = QVector3D::crossProduct(axis, basisU);
	QVector<float> normalAzimuths;
	normalAzimuths.reserve(static_cast<int>(inliers.size()));
	for (const PatchSample& s : inliers)
	{
		const QVector3D radial = s.normal - axis * QVector3D::dotProduct(s.normal, axis);
		if (radial.lengthSquared() < 1.0e-8f)
			continue;
		normalAzimuths.append(std::atan2(QVector3D::dotProduct(radial, basisV), QVector3D::dotProduct(radial, basisU)));
	}
	std::sort(normalAzimuths.begin(), normalAzimuths.end());
	// M_PI isn't guaranteed available (MSVC needs _USE_MATH_DEFINES before
	// <cmath>) - same local-constant convention used elsewhere in this file.
	constexpr float kTwoPiLocal = 6.283185307179586f;
	float maxAzimuthGapRad = 0.0f;
	for (int i = 0; i < normalAzimuths.size(); ++i)
	{
		const float next = (i + 1 < normalAzimuths.size()) ? normalAzimuths[i + 1] : (normalAzimuths[0] + kTwoPiLocal);
		maxAzimuthGapRad = std::max(maxAzimuthGapRad, next - normalAzimuths[i]);
	}
	const float normalAzimuthCoverageDegrees = 360.0f - qRadiansToDegrees(maxAzimuthGapRad);
	constexpr float kMinNormalAzimuthCoverageDegrees = 90.0f;
	if (kCylFitVerbose) qDebug() << "[CylFit] normal azimuth coverage" << normalAzimuthCoverageDegrees << "degrees (min required" << kMinNormalAzimuthCoverageDegrees << ")";
	if (normalAzimuthCoverageDegrees < kMinNormalAzimuthCoverageDegrees)
	{
		if (kCylFitVerbose) qDebug() << "[CylFit] rejected - normals barely rotate around the fitted axis, patch is too close to flat for a trustworthy radius";
		return false;
	}

	// centroid/projected/circleFit were already established by the trim
	// loop above (the final iteration that satisfied both normal AND
	// positional consistency) - reused here rather than recomputed.

	// An algebraic least-squares circle fit is numerically ill-conditioned
	// on a SHORT arc - a shallow arc segment looks almost as good a fit to
	// a huge, wildly displaced circle as to the true one (the classic
	// failure mode of Kasa-style circle fits), so a patch that only wraps
	// PART of the way around a hole/boss (rather than the full loop) can
	// silently produce a wildly wrong radius instead of failing outright.
	// fitPitchCircle() already computes each point's angular gap around the
	// fitted center - the largest gap is exactly "the part of the circle
	// with no supporting points", so 360 minus it is the real angular
	// coverage actually observed. Require a solid majority of the circle to
	// be covered before trusting the fit.
	constexpr float kMinArcCoverageDegrees = 180.0f;
	const float maxGapDegrees = circleFit.gapAnglesDegrees.isEmpty()
		? 360.0f
		: *std::max_element(circleFit.gapAnglesDegrees.begin(), circleFit.gapAnglesDegrees.end());
	const float arcCoverageDegrees = 360.0f - maxGapDegrees;
	if (kCylFitVerbose) qDebug() << "[CylFit] cross-section arc coverage" << arcCoverageDegrees << "degrees (min required" << kMinArcCoverageDegrees << ") radius" << circleFit.radius;
	if (arcCoverageDegrees < kMinArcCoverageDegrees)
	{
		if (kCylFitVerbose) qDebug() << "[CylFit] rejected - patch only wraps a short arc, circle fit is unreliable";
		return false;
	}

	// Roundness (positional-residual) validation already happened inside
	// the trim loop above, jointly with normal consistency - reaching here
	// means the final settled `inliers`/`circleFit` already satisfied it.

	// A near-FLAT patch is a degenerate case none of the checks above can
	// catch: over a modest spatial extent, a very large circle is locally
	// almost indistinguishable from a plane, so a patch that's mostly flat
	// (with only a hint of real curvature, e.g. from growth bleeding a
	// little way past a tangent-continuous fillet onto the adjacent flat
	// face) can fit an enormous, WRONG radius with deceptively low residual
	// AND deceptively "good" angular coverage - both of those are measured
	// relative to the fit's own (potentially wrong) center, so a bad fit
	// can look self-consistent by its own bookkeeping. Cross-check against
	// something that ISN'T relative to that center: the patch's own actual
	// physical footprint. A genuine arc spanning >= kMinArcCoverageDegrees
	// of a circle of this radius has to physically SPAN close to that
	// circle's diameter (the two arc endpoints of an exactly-180-degree
	// arc are diametrically opposite, i.e. exactly 2*radius apart) - if the
	// patch's own points don't actually reach anywhere near that far apart,
	// the reported coverage/radius aren't describing this patch at all.
	float maxPairwiseDistSq = 0.0f;
	for (int i = 0; i < projected.size(); ++i)
		for (int j = i + 1; j < projected.size(); ++j)
			maxPairwiseDistSq = std::max(maxPairwiseDistSq, (projected[i] - projected[j]).lengthSquared());
	const float observedFootprint = std::sqrt(maxPairwiseDistSq);
	const float trueDiameter = circleFit.radius * 2.0f;
	constexpr float kMinFootprintToDiameterRatio = 0.85f;
	if (kCylFitVerbose) qDebug() << "[CylFit] patch footprint" << observedFootprint << "vs fitted diameter" << trueDiameter << "(min ratio allowed" << kMinFootprintToDiameterRatio << ")";
	if (observedFootprint < kMinFootprintToDiameterRatio * trueDiameter)
	{
		if (kCylFitVerbose) qDebug() << "[CylFit] rejected - patch's own physical footprint is too small for the fitted circle (likely a near-flat patch masquerading as a large radius)";
		return false;
	}

	if (kCylFitVerbose) qDebug() << "[CylFit] accepted as" << (isCone ? "Cone" : "Cylinder") << "radius" << circleFit.radius;

	outPickedPoint = resolveMeasurementAnchor(ref);
	outAxisOrigin = circleFit.center;
	outAxisDir = axis;
	outIsCone = isCone;

	{
		const QVector3D toPoint = outPickedPoint - outAxisOrigin;
		const QVector3D radial = toPoint - outAxisDir * QVector3D::dotProduct(toPoint, outAxisDir);
		if (kCylFitVerbose) qDebug() << "[CylFit] single-point diameter would be" << radial.length() * 2.0f << "vs whole-patch circle fit diameter" << circleFit.radius * 2.0f;
	}

	if (isCone)
	{
		// A cone's radius genuinely varies with position, so there's no
		// single whole-patch number to report - same formula as the OCC
		// path above: diameter is twice the picked point's perpendicular
		// distance to the fitted axis line, evaluated AT that point.
		const QVector3D toPoint = outPickedPoint - outAxisOrigin;
		const QVector3D radial = toPoint - outAxisDir * QVector3D::dotProduct(toPoint, outAxisDir);
		outDiameter = radial.length() * 2.0f;
	}
	else
	{
		// For a cylinder, prefer fitPitchCircle()'s own whole-patch radius
		// over re-deriving it from a single point against this fit's axis.
		// The fitted axis DIRECTION here is only approximate (unlike the
		// OCC path's exact B-Rep axis) - any small angular error in it
		// makes the apparent "distance from one point to the axis line"
		// drift with how far that point sits from the fit's reference
		// plane along the axis (a lever-arm effect), while circleFit.radius
		// already averages over every inlier point and isn't sensitive to
		// which single point happens to get clicked.
		outDiameter = circleFit.radius * 2.0f;
	}
	return true;
}

bool MeasurementController::resolveMeasurementDimensionSegment(const Measurement& m, QVector3D& outA, QVector3D& outB) const
{
	switch (m.type)
	{
	case MeasurementType::Distance:
	{
		if (m.anchors.size() < 2)
			return false;
		outA = resolveMeasurementAnchor(m.anchors[0]);
		outB = resolveMeasurementAnchor(m.anchors[1]);
		return true;
	}
	case MeasurementType::PointToFace:
	{
		if (m.anchors.size() < 2)
			return false;
		const QVector3D point = resolveMeasurementAnchor(m.anchors[0]);
		QVector3D facePos, faceNormal;
		if (!resolveMeasurementAnchorPlane(m.anchors[1], facePos, faceNormal))
			return false;
		outA = point;
		outB = point - faceNormal * QVector3D::dotProduct(point - facePos, faceNormal);
		return true;
	}
	case MeasurementType::FaceToFace:
	{
		if (m.anchors.size() < 2)
			return false;
		QVector3D p1, n1, p2, n2;
		if (!resolveMeasurementAnchorPlane(m.anchors[0], p1, n1) || !resolveMeasurementAnchorPlane(m.anchors[1], p2, n2))
			return false;
		const MeasurementGeometry::FaceToFaceResult result = MeasurementGeometry::compareFaces(p1, n1, p2, n2);
		if (!result.isParallel)
			return false;  // angle case has an arc, not a straight dimension line to drag
		outA = p1;
		outB = p1 + n1 * QVector3D::dotProduct(p2 - p1, n1);
		return true;
	}
	case MeasurementType::EdgeLength:
	{
		if (m.anchors.isEmpty())
			return false;
		// A draggable offset dimension line is only meaningful for a
		// genuinely straight edge - see drawMeasurementOverlay()'s
		// EdgeLength branch for why a curved/filleted one skips it
		// entirely (a straight line's own on-screen length wouldn't
		// match the curve-length value it would be labeled with).
		QVector<QVector3D> polyline;
		if (resolveMeasurementEdgePolyline(m.anchors[0], polyline) && polyline.size() > 2)
			return false;
		float length = 0.0f;
		return resolveMeasurementEdgeGeometry(m.anchors[0], outA, outB, length);
	}
	case MeasurementType::EdgeToVertex:
	{
		if (m.anchors.size() < 2)
			return false;
		QVector3D edgeStart, edgeEnd;
		float edgeLength = 0.0f;
		if (!resolveMeasurementEdgeGeometry(m.anchors[0], edgeStart, edgeEnd, edgeLength))
			return false;
		outA = resolveMeasurementAnchor(m.anchors[1]);
		outB = MeasurementGeometry::closestPointOnLine(outA, edgeStart, edgeEnd - edgeStart);
		return true;
	}
	case MeasurementType::EdgeToEdge:
	{
		if (m.anchors.size() < 2)
			return false;
		QVector3D start1, end1, start2, end2;
		float len1 = 0.0f, len2 = 0.0f;
		if (!resolveMeasurementEdgeGeometry(m.anchors[0], start1, end1, len1)
			|| !resolveMeasurementEdgeGeometry(m.anchors[1], start2, end2, len2))
			return false;
		const MeasurementGeometry::EdgeToEdgeResult result =
			MeasurementGeometry::compareLines(start1, end1 - start1, start2, end2 - start2);
		if (!result.isParallel)
			return false;  // angle case has legs+arc instead, not a straight dimension line to drag
		outA = start1;
		outB = MeasurementGeometry::closestPointOnLine(start1, start2, end2 - start2);
		return true;
	}
	case MeasurementType::EdgeToFace:
	{
		if (m.anchors.size() < 2)
			return false;
		QVector3D edgeStart, edgeEnd;
		float edgeLength = 0.0f;
		if (!resolveMeasurementEdgeGeometry(m.anchors[0], edgeStart, edgeEnd, edgeLength))
			return false;
		QVector3D facePos, faceNormal;
		if (!resolveMeasurementAnchorPlane(m.anchors[1], facePos, faceNormal))
			return false;
		const MeasurementGeometry::EdgeToFaceResult result =
			MeasurementGeometry::compareEdgeToFace(edgeStart, edgeEnd - edgeStart, facePos, faceNormal);
		if (!result.isParallel)
			return false;  // angle case has legs+arc instead, not a straight dimension line to drag
		outA = edgeStart;
		outB = edgeStart - faceNormal.normalized() * QVector3D::dotProduct(edgeStart - facePos, faceNormal.normalized());
		return true;
	}
	default:
		return false;
	}
}

QVector3D MeasurementController::dimensionLinePerp(const QVector3D& a, const QVector3D& b,
	const QVector3D& referenceDir, Camera* camera) const
{
	const QVector3D delta = b - a;
	if (delta.lengthSquared() < 1.0e-12f || !camera)
		return QVector3D(0.0f, 1.0f, 0.0f);
	const QVector3D dirN = delta.normalized();

	const bool haveReference = referenceDir.lengthSquared() > 1.0e-8f;
	QVector3D perp = QVector3D::crossProduct(dirN, haveReference ? referenceDir : camera->getViewDir());
	if (perp.lengthSquared() < 1.0e-8f)
		perp = QVector3D::crossProduct(dirN, camera->getUpVector());
	if (perp.lengthSquared() < 1.0e-8f)
		perp = QVector3D::crossProduct(dirN, QVector3D(0.0f, 1.0f, 0.0f));
	return perp.normalized();
}

float MeasurementController::defaultDimensionOffsetMagnitude(Camera* camera) const
{
	const float markerSize = camera ? std::max(camera->getViewRange(), 0.0001f) * 0.01f : 0.01f;
	return markerSize * 6.0f;
}

QVector3D MeasurementController::resolveDimensionOffsetVector(const QVector3D& a, const QVector3D& b,
	const Measurement& m, Camera* camera) const
{
	if (m.offsetVector.lengthSquared() > 1.0e-10f)
		return m.offsetVector;  // user has dragged this - use the exact vector (direction + magnitude)
	return dimensionLinePerp(a, b, m.offsetReferenceDir, camera) * defaultDimensionOffsetMagnitude(camera);
}

bool MeasurementController::resolveMeasurementAngleGeometry(const Measurement& m, Camera* camera, QVector3D& outVertex,
	QVector3D& outU, QVector3D& outV, float& outAngleRad, float& outRadius) const
{
	if (m.anchors.size() < 2)
		return false;

	// deg -> rad, same constant used throughout this file (kDegToRadLocal).
	constexpr float kDegToRad = 0.017453292519943295f;
	const float markerSize = camera ? std::max(camera->getViewRange(), 0.0001f) * 0.01f : 0.01f;

	// outRadius is the ARC's radius specifically (what hit-testing/dragging
	// treat as "the" draggable value) - the legs themselves extend a bit
	// further out than the arc (see drawMeasurementOverlay()'s
	// legLength = outRadius / 0.85f), matching the original fixed 85%
	// arc-inset-from-leg-tip look, now expressed the other way around so a
	// dragged value means exactly what the user grabbed (the arc). Shared by
	// all three cases below.
	auto finishBasis = [&](const QVector3D& u, const QVector3D& secondDir, float angleDegrees,
		float defaultLegLength) -> bool
	{
		outU = u;
		QVector3D v = secondDir - u * QVector3D::dotProduct(secondDir, u);
		if (v.lengthSquared() < 1.0e-8f)
			return false;  // degenerate - shouldn't happen given the parallel case was already ruled out
		outV = v.normalized();
		outAngleRad = angleDegrees * kDegToRad;
		const float defaultRadius = std::max(defaultLegLength, markerSize * 4.0f) * 0.85f;
		outRadius = (m.offsetDistance >= 0.0f) ? m.offsetDistance : defaultRadius;
		return true;
	};

	switch (m.type)
	{
	case MeasurementType::FaceToFace:
	{
		QVector3D p1, n1, p2, n2;
		if (!resolveMeasurementAnchorPlane(m.anchors[0], p1, n1) || !resolveMeasurementAnchorPlane(m.anchors[1], p2, n2))
			return false;
		const MeasurementGeometry::FaceToFaceResult result = MeasurementGeometry::compareFaces(p1, n1, p2, n2);
		if (result.isParallel)
			return false;  // parallel case has a straight dimension line instead - see resolveMeasurementDimensionSegment()

		outVertex = (p1 + p2) * 0.5f;
		const QVector3D n2Effective = (QVector3D::dotProduct(n1, n2) >= 0.0f) ? n2 : -n2;
		return finishBasis(n1, n2Effective, result.angleDegrees, (p1 - p2).length() * 0.5f);
	}
	case MeasurementType::EdgeToEdge:
	{
		QVector3D start1, end1, start2, end2;
		float len1 = 0.0f, len2 = 0.0f;
		if (!resolveMeasurementEdgeGeometry(m.anchors[0], start1, end1, len1)
			|| !resolveMeasurementEdgeGeometry(m.anchors[1], start2, end2, len2))
			return false;
		const QVector3D d1 = end1 - start1;
		const QVector3D d2 = end2 - start2;
		const MeasurementGeometry::EdgeToEdgeResult result = MeasurementGeometry::compareLines(start1, d1, start2, d2);
		if (result.isParallel)
			return false;  // parallel case has a straight dimension line instead - see resolveMeasurementDimensionSegment()

		outVertex = ((start1 + end1) * 0.5f + (start2 + end2) * 0.5f) * 0.5f;
		const QVector3D d1n = d1.normalized();
		const QVector3D d2nRaw = d2.normalized();
		const QVector3D d2n = (QVector3D::dotProduct(d1n, d2nRaw) >= 0.0f) ? d2nRaw : -d2nRaw;
		return finishBasis(d1n, d2n, result.angleDegrees, std::max(len1, len2) * 0.5f);
	}
	case MeasurementType::EdgeToFace:
	{
		QVector3D edgeStart, edgeEnd;
		float edgeLength = 0.0f;
		if (!resolveMeasurementEdgeGeometry(m.anchors[0], edgeStart, edgeEnd, edgeLength))
			return false;
		QVector3D facePos, faceNormal;
		if (!resolveMeasurementAnchorPlane(m.anchors[1], facePos, faceNormal))
			return false;
		const QVector3D edgeDir = edgeEnd - edgeStart;
		const MeasurementGeometry::EdgeToFaceResult result =
			MeasurementGeometry::compareEdgeToFace(edgeStart, edgeDir, facePos, faceNormal);
		if (result.isParallel)
			return false;  // parallel case has a straight dimension line instead - see resolveMeasurementDimensionSegment()

		// Grounded at the edge's own start point (a real point, unlike
		// FaceToFace/EdgeToEdge's "floating midpoint") - one leg along the
		// edge itself, the other along the edge's own projection onto the
		// face's plane, sweeping the angle between them.
		const QVector3D dN = edgeDir.normalized();
		const QVector3D nN = faceNormal.normalized();
		QVector3D projectedDir = dN - nN * QVector3D::dotProduct(dN, nN);
		if (projectedDir.lengthSquared() < 1.0e-8f)
		{
			// The edge is (very close to) exactly perpendicular to the
			// face - the 90-degree case, and a common one in practice (a
			// hole's axis edge square to the face it's drilled into isn't
			// a rare configuration). There's no uniquely-defined
			// "projection direction" within the face's plane at exactly
			// 90 degrees - every in-plane direction is equally valid - so
			// rather than bailing out with no arc at all (which is what
			// used to happen here), pick an arbitrary but deterministic
			// one, same construction as MeasurementGeometry::
			// orthonormalBasis(): whichever world axis is least parallel
			// to the face normal, cross product to land in-plane.
			const QVector3D reference = (std::abs(QVector3D::dotProduct(nN, QVector3D(0.0f, 1.0f, 0.0f))) < 0.9f)
				? QVector3D(0.0f, 1.0f, 0.0f)
				: QVector3D(1.0f, 0.0f, 0.0f);
			projectedDir = QVector3D::crossProduct(nN, reference).normalized();
		}
		else
		{
			projectedDir.normalize();
		}

		outVertex = edgeStart;
		return finishBasis(dN, projectedDir, result.angleDegrees, edgeLength * 0.5f);
	}
	case MeasurementType::AngleThreePoint:
	{
		if (m.anchors.size() < 3)
			return false;
		const QVector3D vertex = resolveMeasurementAnchor(m.anchors[0]);
		const QVector3D p1 = resolveMeasurementAnchor(m.anchors[1]);
		const QVector3D p2 = resolveMeasurementAnchor(m.anchors[2]);
		const QVector3D v1 = p1 - vertex;
		const QVector3D v2 = p2 - vertex;
		if (v1.lengthSquared() < 1.0e-8f || v2.lengthSquared() < 1.0e-8f)
			return false;  // a ray needs a real length - one of the picks landed on (or failed to resolve away from) the vertex itself

		outVertex = vertex;
		const QVector3D u = v1.normalized();
		const float angleDeg = MeasurementGeometry::angleBetweenRays(vertex, p1, p2);
		return finishBasis(u, v2, angleDeg, std::max(v1.length(), v2.length()) * 0.5f);
	}
	default:
		return false;
	}
}

void MeasurementController::handleMeasurementClick(const QPoint& clickPoint, SelectionManager* selectionManager, Camera* camera)
{
	if (!selectionManager || !_viewer || !_viewer->sceneGraph() || _measurementTool == MeasurementTool::None)
		return;

	MeasurementAnchorRef ref;

	if (_measurementTool == MeasurementTool::EdgeRadius || _measurementTool == MeasurementTool::Concentricity)
	{
		// A wholly different pick from every other tool - identifies a
		// topological B-Rep edge, not a surface point (see
		// MeshEdgeCircleAnchor's doc comment). Concentricity needs this for
		// BOTH its anchors, unconditionally (same as EdgeToEdge always
		// picking an edge for both of its anchors below) - it compares two
		// real analytic circles, not two arbitrary points.
		const MeshEdgeCircleAnchor edgeAnchor = selectionManager->pickEdgeCircleAnchor(clickPoint);
		if (!edgeAnchor.isValid())
			return;  // no circular edge under the cursor - stay armed, don't cancel the tool
		ref.meshUuid  = edgeAnchor.meshUuid;
		ref.edgeIndex = edgeAnchor.edgeIndex;
	}
	else if (_measurementTool == MeasurementTool::EdgeLength
		|| _measurementTool == MeasurementTool::EdgeToEdge
		|| _measurementTool == MeasurementTool::EdgeChain
		|| (_measurementTool == MeasurementTool::EdgeToVertex && _pendingMeasurementAnchors.isEmpty())
		|| (_measurementTool == MeasurementTool::EdgeToFace && _pendingMeasurementAnchors.isEmpty()))
	{
		// EdgeLength, EdgeToEdge, and EdgeChain always pick an edge (every
		// anchor - EdgeChain's count is variable, but every one of them is
		// still an edge, same as EdgeToEdge's fixed two); EdgeToVertex/
		// EdgeToFace only pick one for their FIRST anchor - the second (a
		// vertex/point or a face) falls through to the normal
		// pickSurfaceAnchor branch below, same as every other point/face pick.
		const MeshEdgeCircleAnchor edgeAnchor = selectionManager->pickStraightEdgeAnchor(clickPoint);
		if (!edgeAnchor.isValid())
			return;  // no edge under the cursor - stay armed, don't cancel the tool
		ref.meshUuid  = edgeAnchor.meshUuid;
		ref.edgeIndex = edgeAnchor.edgeIndex;
	}
	else if (_measurementTool == MeasurementTool::FaceToFace
		|| _measurementTool == MeasurementTool::FaceArea
		|| _measurementTool == MeasurementTool::MinDistance
		|| _measurementTool == MeasurementTool::CylindricalDiameter
		|| ((_measurementTool == MeasurementTool::PointToFace || _measurementTool == MeasurementTool::EdgeToFace)
			&& !_pendingMeasurementAnchors.isEmpty())
		|| _measurementTool == MeasurementTool::ArcRadius3Point
		|| (_measurementTool == MeasurementTool::ArcRadiusCenterPoint && !_pendingMeasurementAnchors.isEmpty()))
	{
		// Two different reasons land here, but both need the same plain
		// surface pick with no circular-edge-center snap attempted:
		//  - FACE picks (FaceToFace's two anchors; FaceArea's/MinDistance's/
		//    CylindricalDiameter's own anchors; PointToFace/EdgeToFace's
		//    second anchor - their first is a point/edge, already routed
		//    above) need real triangle/normal
		//    data to resolve a plane (or a flood-fill seed) from - a
		//    circle's center has none.
		//  - ARC-RIM picks (3-Point Arc Radius's 3 points; Center+2-Point
		//    Arc Radius's own p1/p2, as opposed to its CENTER anchor at
		//    index 0, handled below) must land on distinct points actually
		//    ON the circle - snapping any of them to the shared center
		//    instead would collapse the fit (circumcircle3Point() sees
		//    near-coincident points; circleFromCenterAndTwoPoints() sees a
		//    zero-length center-to-point vector) rather than measuring the
		//    circle at all.
		const MeshSurfaceAnchor anchor = selectionManager->pickSurfaceAnchor(clickPoint);
		if (!anchor.isValid())
			return;  // clicked empty space - stay armed, don't cancel the tool

		ref.meshUuid           = anchor.meshUuid;
		ref.triangleIndex      = anchor.triangleIndex;
		ref.barycentric        = anchor.barycentric;
		ref.snappedVertexIndex = anchor.snappedVertexIndex;
	}
	else
	{
		// Every remaining pick genuinely wants an arbitrary POINT with no
		// "must be a distinct point on this rim" constraint (Point,
		// Distance, Point-to-Face's point anchor, Edge-to-Vertex's vertex
		// anchor, Center+2-Point Arc Radius's own CENTER anchor
		// specifically - its p1/p2 are excluded above - every one of
		// Pitch Circle's hole-center picks, and every one of 3-Point
		// Angle's vertex/ray picks). Prefer snapping to a nearby
		// circular B-Rep edge's exact analytic center (see
		// SelectionManager::pickCircularEdgeCenterAnchor()'s doc comment) -
		// a hole/boss center is very often exactly the point actually
		// wanted, and there's no other way to land on one precisely (it's
		// often empty space, not real geometry a plain surface pick could
		// ever hit). Falls back to the ordinary triangle-surface pick if no
		// circular edge is nearby, preserving plain point-picking on
		// glTF/OBJ meshes and everywhere else on CAD parts.
		const MeshEdgeCircleAnchor centerAnchor = selectionManager->pickCircularEdgeCenterAnchor(clickPoint);
		if (centerAnchor.isValid())
		{
			ref.meshUuid  = centerAnchor.meshUuid;
			ref.edgeIndex = centerAnchor.edgeIndex;
		}
		else
		{
			const MeshSurfaceAnchor anchor = selectionManager->pickSurfaceAnchor(clickPoint);
			if (!anchor.isValid())
				return;  // clicked empty space - stay armed, don't cancel the tool

			ref.meshUuid           = anchor.meshUuid;
			ref.triangleIndex      = anchor.triangleIndex;
			ref.barycentric        = anchor.barycentric;
			ref.snappedVertexIndex = anchor.snappedVertexIndex;
		}
	}

	// Chain Length must stay one contiguous path/loop - reject a pick that
	// doesn't share an endpoint with the chain so far (see
	// measurementChainEdgeConnects()'s doc comment), e.g. two concentric
	// but otherwise unconnected circles.
	if (_measurementTool == MeasurementTool::EdgeChain
		&& !measurementChainEdgeConnects(_pendingMeasurementAnchors, ref))
	{
		MainWindow::showStatusMessage(tr("Edge doesn't connect to the chain - pick one sharing an endpoint with it"), 2500);
		return;  // stay armed, don't add this edge
	}

	// Cylindrical/Conical Diameter only accepts a pick that actually lands
	// on a captured cylindrical or conical face - reject anything else
	// (an ordinary flat/spline face) outright rather than creating a
	// measurement that can never resolve (see
	// resolveMeasurementCylindricalDiameter()) and would sit in the
	// results list forever reading "(face no longer available)".
	if (_measurementTool == MeasurementTool::CylindricalDiameter)
	{
		float diameter = 0.0f;
		QVector3D axisOrigin, axisDir, pickedPoint;
		bool isCone = false;
		if (!resolveMeasurementCylindricalDiameter(ref, diameter, axisOrigin, axisDir, pickedPoint, isCone))
		{
			MainWindow::showStatusMessage(tr("Not a cylindrical or conical face - pick again"), 2500);
			return;  // stay armed, don't create a measurement that can never resolve
		}
	}

	_pendingMeasurementAnchors.append(ref);

	const int required = measurementToolRequiredAnchorCount(_measurementTool);
	emit measurementProgressChanged(_pendingMeasurementAnchors.size(), required);

	// A variable-length tool (PitchCircle, EdgeChain) never auto-completes
	// at `required` - that's its MINIMUM, not a target - it stays armed
	// regardless of count until finishVariableLengthMeasurement() is
	// called explicitly (Enter, or the dialog's Finish button).
	if (measurementToolHasVariableAnchorCount(_measurementTool))
		return;

	if (_pendingMeasurementAnchors.size() >= required)
		finalizePendingMeasurement(camera);
}

void MeasurementController::finalizePendingMeasurement(Camera* camera)
{
	Measurement m;
	m.id = QUuid::createUuid();
	m.type = measurementTypeForTool(_measurementTool);
	m.anchors = _pendingMeasurementAnchors;
	// Captured once, here, at creation - see Measurement::offsetReferenceDir's
	// doc comment for why this must NOT be re-derived from the live
	// camera on every render frame.
	if (camera)
		m.offsetReferenceDir = camera->getViewDir();
	_viewer->addMeasurement(m);  // undoable - see AddMeasurementCommand
	_pendingMeasurementAnchors.clear();
	emit measurementProgressChanged(0, measurementToolRequiredAnchorCount(_measurementTool));
}

void MeasurementController::finishVariableLengthMeasurement(Camera* camera)
{
	if (!_viewer || !_viewer->sceneGraph() || !measurementToolHasVariableAnchorCount(_measurementTool))
		return;
	if (_pendingMeasurementAnchors.size() < measurementToolRequiredAnchorCount(_measurementTool))
		return;  // below the minimum - the Finish button should be disabled in this state anyway
	finalizePendingMeasurement(camera);
}

void MeasurementController::setSelectedMeasurementIds(const QSet<QUuid>& ids)
{
	if (_selectedMeasurementIds == ids)
		return;
	_selectedMeasurementIds = ids;
	emit measurementStateChanged();
	emit measurementSelectionChanged(_selectedMeasurementIds);
}

bool MeasurementController::isEffectivelyVisible(const Measurement& m) const
{
	return _sceneRuntime.visibleSwapped() ? !m.visible : m.visible;
}

bool MeasurementController::hasHiddenMeasurements() const
{
	if (!_viewer || !_viewer->sceneGraph())
		return false;
	for (const Measurement& m : _viewer->sceneGraph()->measurements())
	{
		if (!m.visible)
			return true;
	}
	return false;
}

QVector<QVector3D> MeasurementController::visibleBoundsPoints(Camera* camera) const
{
	QVector<QVector3D> points;
	if (!_viewer || !_viewer->sceneGraph())
		return points;

	for (const Measurement& m : _viewer->sceneGraph()->measurements())
	{
		if (!isEffectivelyVisible(m))
			continue;

		for (const MeasurementAnchorRef& ref : m.anchors)
		{
			QVector3D p = resolveMeasurementAnchor(ref);
			if (p.isNull() && ref.edgeIndex >= 0)
			{
				// A straight (non-circular) edge anchor - resolveMeasurementAnchor()
				// only resolves edgeIndex>=0 via the circular-edge path, so a
				// straight edge falls through it to a null point. Use the
				// edge's own midpoint instead.
				QVector3D start, end;
				float length = 0.0f;
				if (resolveMeasurementEdgeGeometry(ref, start, end, length))
					p = (start + end) * 0.5f;
			}
			if (!p.isNull())
				points.append(p);
		}

		// The RENDERED linear dimension line/angle arc - but ONLY once the
		// user has actually dragged it (m.offsetVector/offsetDistance
		// explicitly set - see those fields' own doc comments). The
		// UNDRAGGED default position scales with camera->getViewRange()
		// (defaultDimensionOffsetMagnitude()) - which is exactly what
		// Fit-to-Screen computes - so folding the default into bounds
		// created a feedback loop: each fit changes the view range, which
		// changes the default offset, which changes the next fit's bounds,
		// never converging (confirmed - repeated F presses kept "refining"
		// instead of settling). The raw anchors (already added above) are
		// enough to frame an undragged dimension line/arc correctly; a
		// genuinely dragged one has a fixed, camera-independent position
		// that legitimately needs to be included.
		if (camera)
		{
			QVector3D segA, segB;
			if (resolveMeasurementDimensionSegment(m, segA, segB) && m.offsetVector.lengthSquared() > 1.0e-10f)
			{
				points.append(segA + m.offsetVector);
				points.append(segB + m.offsetVector);
			}

			QVector3D vertex, u, v;
			float angleRad = 0.0f;
			float radius = 0.0f;
			if (m.offsetDistance >= 0.0f && resolveMeasurementAngleGeometry(m, camera, vertex, u, v, angleRad, radius))
			{
				points.append(vertex + u * radius);
				points.append(vertex + (u * std::cos(angleRad) + v * std::sin(angleRad)) * radius);
				points.append(vertex + (u * std::cos(angleRad * 0.5f) + v * std::sin(angleRad * 0.5f)) * radius);
			}
		}
	}
	return points;
}

QUuid MeasurementController::hitTestMeasurement(const QPoint& pixel, Camera* camera, const QSize& viewportSize, int pixelRadius) const
{
	if (!camera || !_viewer || !_viewer->sceneGraph())
		return QUuid();

	const QRect viewportRect(0, 0, viewportSize.width(), viewportSize.height());
	auto toScreen = [&](const QVector3D& worldPos) -> QVector2D {
		const QVector3D projected = worldPos.project(camera->getViewMatrix(), camera->getProjectionMatrix(), viewportRect);
		// Same y-flip as pickSurfaceAnchor()'s vertex-snap projection -
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

	// Minimum on-screen distance to an edge anchor's TRUE tessellated path
	// (see resolveMeasurementEdgePolyline()'s doc comment) rather than just
	// its chord - for a straight edge this is identical to before; for a
	// curved/filleted one, testing only the chord would miss clicks along
	// the actual highlighted curve. Returns false (leaving outDist
	// untouched) if the edge can't be resolved.
	auto minDistToEdgeTrace = [&](const MeasurementAnchorRef& ref, const QVector2D& p, float& outDist) -> bool {
		QVector<QVector3D> polyline;
		if (!resolveMeasurementEdgePolyline(ref, polyline) || polyline.size() < 2)
			return false;
		float best = std::numeric_limits<float>::max();
		for (int i = 0; i + 1 < polyline.size(); ++i)
			best = std::min(best, distPointToSegment(p, toScreen(polyline[i]), toScreen(polyline[i + 1])));
		outDist = best;
		return true;
	};

	const QVector2D clickPt(static_cast<float>(pixel.x()), static_cast<float>(pixel.y()));
	QUuid bestId;
	float bestDist = static_cast<float>(pixelRadius);

	for (const Measurement& m : _viewer->sceneGraph()->measurements())
	{
		if (!isEffectivelyVisible(m))
			continue;

		if (m.type == MeasurementType::Point && !m.anchors.isEmpty())
		{
			const QVector2D sp = toScreen(resolveMeasurementAnchor(m.anchors[0]));
			const float d = (clickPt - sp).length();
			if (d < bestDist)
			{
				bestDist = d;
				bestId = m.id;
			}
		}
		else if (m.type == MeasurementType::Distance && m.anchors.size() >= 2)
		{
			const QVector2D sa = toScreen(resolveMeasurementAnchor(m.anchors[0]));
			const QVector2D sb = toScreen(resolveMeasurementAnchor(m.anchors[1]));
			const float d = distPointToSegment(clickPt, sa, sb);
			if (d < bestDist)
			{
				bestDist = d;
				bestId = m.id;
			}
		}
		else if (m.type == MeasurementType::ArcRadius3Point && m.anchors.size() >= 3)
		{
			const QVector3D p0 = resolveMeasurementAnchor(m.anchors[0]);
			const QVector3D p1 = resolveMeasurementAnchor(m.anchors[1]);
			const QVector3D p2 = resolveMeasurementAnchor(m.anchors[2]);
			QVector3D center, normal;
			float radius = 0.0f;
			if (MeasurementGeometry::circumcircle3Point(p0, p1, p2, center, normal, radius))
			{
				const QVector<QVector3D> circle = MeasurementGeometry::circlePolyline(center, normal, radius);
				for (int i = 0; i < circle.size(); ++i)
				{
					const float d = distPointToSegment(clickPt, toScreen(circle[i]), toScreen(circle[(i + 1) % circle.size()]));
					if (d < bestDist)
					{
						bestDist = d;
						bestId = m.id;
					}
				}
				const float dc = (clickPt - toScreen(center)).length();
				if (dc < bestDist)
				{
					bestDist = dc;
					bestId = m.id;
				}
			}
		}
		else if (m.type == MeasurementType::ArcRadiusCenterPoint && m.anchors.size() >= 3)
		{
			const QVector3D center = resolveMeasurementAnchor(m.anchors[0]);
			const QVector3D p1 = resolveMeasurementAnchor(m.anchors[1]);
			const QVector3D p2 = resolveMeasurementAnchor(m.anchors[2]);
			QVector3D normal;
			float radius = 0.0f;
			if (MeasurementGeometry::circleFromCenterAndTwoPoints(center, p1, p2, normal, radius))
			{
				const QVector<QVector3D> circle = MeasurementGeometry::circlePolyline(center, normal, radius);
				for (int i = 0; i < circle.size(); ++i)
				{
					const float d = distPointToSegment(clickPt, toScreen(circle[i]), toScreen(circle[(i + 1) % circle.size()]));
					if (d < bestDist)
					{
						bestDist = d;
						bestId = m.id;
					}
				}
				// Also test the two center-to-point spokes and the center
				// marker itself - clicking directly on a spoke or the center
				// dot should select the measurement too, not just the rim.
				const float dSpoke1 = distPointToSegment(clickPt, toScreen(center), toScreen(p1));
				const float dSpoke2 = distPointToSegment(clickPt, toScreen(center), toScreen(p2));
				const float dCenter = (clickPt - toScreen(center)).length();
				const float dBest = std::min({ dSpoke1, dSpoke2, dCenter });
				if (dBest < bestDist)
				{
					bestDist = dBest;
					bestId = m.id;
				}
			}
		}
		else if (m.type == MeasurementType::EdgeRadius && !m.anchors.isEmpty())
		{
			QVector3D center, axis;
			float radius = 0.0f;
			if (resolveMeasurementEdgeCircle(m.anchors[0], center, axis, radius))
			{
				const QVector<QVector3D> circle = MeasurementGeometry::circlePolyline(center, axis, radius);
				for (int i = 0; i < circle.size(); ++i)
				{
					const float d = distPointToSegment(clickPt, toScreen(circle[i]), toScreen(circle[(i + 1) % circle.size()]));
					if (d < bestDist)
					{
						bestDist = d;
						bestId = m.id;
					}
				}
				const float dCenter = (clickPt - toScreen(center)).length();
				if (dCenter < bestDist)
				{
					bestDist = dCenter;
					bestId = m.id;
				}
			}
		}
		else if (m.type == MeasurementType::FaceToFace && m.anchors.size() >= 2)
		{
			QVector3D p1, n1, p2, n2;
			if (resolveMeasurementAnchorPlane(m.anchors[0], p1, n1) && resolveMeasurementAnchorPlane(m.anchors[1], p2, n2))
			{
				const float d1 = (clickPt - toScreen(p1)).length();
				const float d2 = (clickPt - toScreen(p2)).length();
				const float dSeg = distPointToSegment(clickPt, toScreen(p1), toScreen(p2));
				const float dBest = std::min({ d1, d2, dSeg });
				if (dBest < bestDist)
				{
					bestDist = dBest;
					bestId = m.id;
				}
			}
		}
		else if (m.type == MeasurementType::PointToFace && m.anchors.size() >= 2)
		{
			const QVector3D point = resolveMeasurementAnchor(m.anchors[0]);
			QVector3D facePos, faceNormal;
			if (resolveMeasurementAnchorPlane(m.anchors[1], facePos, faceNormal))
			{
				const float dSeg = distPointToSegment(clickPt, toScreen(point), toScreen(facePos));
				if (dSeg < bestDist)
				{
					bestDist = dSeg;
					bestId = m.id;
				}
			}
		}
		else if (m.type == MeasurementType::EdgeLength && !m.anchors.isEmpty())
		{
			// True tessellated path, not just the chord (see
			// resolveMeasurementEdgePolyline()'s doc comment) - for a
			// straight edge this is the same 2-point segment as before, so
			// no change there; for a curved/filleted one, hit-testing
			// against just the chord would miss clicks along the actual
			// highlighted curve (see the render branch below).
			QVector<QVector3D> polyline;
			if (resolveMeasurementEdgePolyline(m.anchors[0], polyline) && polyline.size() >= 2)
			{
				for (int i = 0; i + 1 < polyline.size(); ++i)
				{
					const float d = distPointToSegment(clickPt, toScreen(polyline[i]), toScreen(polyline[i + 1]));
					if (d < bestDist)
					{
						bestDist = d;
						bestId = m.id;
					}
				}
			}
		}
		else if (m.type == MeasurementType::EdgeToVertex && m.anchors.size() >= 2)
		{
			QVector3D edgeStart, edgeEnd;
			float edgeLength = 0.0f;
			if (resolveMeasurementEdgeGeometry(m.anchors[0], edgeStart, edgeEnd, edgeLength))
			{
				const QVector3D point = resolveMeasurementAnchor(m.anchors[1]);
				float dEdge = distPointToSegment(clickPt, toScreen(edgeStart), toScreen(edgeEnd));
				minDistToEdgeTrace(m.anchors[0], clickPt, dEdge);
				const float dPoint = (clickPt - toScreen(point)).length();
				const float dBest = std::min(dEdge, dPoint);
				if (dBest < bestDist)
				{
					bestDist = dBest;
					bestId = m.id;
				}
			}
		}
		else if (m.type == MeasurementType::EdgeToEdge && m.anchors.size() >= 2)
		{
			QVector3D start1, end1, start2, end2;
			float len1 = 0.0f, len2 = 0.0f;
			if (resolveMeasurementEdgeGeometry(m.anchors[0], start1, end1, len1)
				&& resolveMeasurementEdgeGeometry(m.anchors[1], start2, end2, len2))
			{
				float d1 = distPointToSegment(clickPt, toScreen(start1), toScreen(end1));
				minDistToEdgeTrace(m.anchors[0], clickPt, d1);
				float d2 = distPointToSegment(clickPt, toScreen(start2), toScreen(end2));
				minDistToEdgeTrace(m.anchors[1], clickPt, d2);
				const float dBest = std::min(d1, d2);
				if (dBest < bestDist)
				{
					bestDist = dBest;
					bestId = m.id;
				}
			}
		}
		else if (m.type == MeasurementType::EdgeToFace && m.anchors.size() >= 2)
		{
			QVector3D edgeStart, edgeEnd;
			float edgeLength = 0.0f;
			if (resolveMeasurementEdgeGeometry(m.anchors[0], edgeStart, edgeEnd, edgeLength))
			{
				QVector3D facePos, faceNormal;
				if (resolveMeasurementAnchorPlane(m.anchors[1], facePos, faceNormal))
				{
					float dEdge = distPointToSegment(clickPt, toScreen(edgeStart), toScreen(edgeEnd));
					minDistToEdgeTrace(m.anchors[0], clickPt, dEdge);
					const float dFace = (clickPt - toScreen(facePos)).length();
					const float dBest = std::min(dEdge, dFace);
					if (dBest < bestDist)
					{
						bestDist = dBest;
						bestId = m.id;
					}
				}
			}
		}
		else if (m.type == MeasurementType::PitchCircle && m.anchors.size() >= 3)
		{
			QVector<QVector3D> points;
			points.reserve(m.anchors.size());
			for (const MeasurementAnchorRef& a : m.anchors)
				points.append(resolveMeasurementAnchor(a));
			const MeasurementGeometry::PitchCircleResult result = MeasurementGeometry::fitPitchCircle(points);
			if (result.valid)
			{
				const QVector<QVector3D> circle = MeasurementGeometry::circlePolyline(result.center, result.normal, result.radius);
				for (int i = 0; i < circle.size(); ++i)
				{
					const float d = distPointToSegment(clickPt, toScreen(circle[i]), toScreen(circle[(i + 1) % circle.size()]));
					if (d < bestDist)
					{
						bestDist = d;
						bestId = m.id;
					}
				}
				const float dc = (clickPt - toScreen(result.center)).length();
				if (dc < bestDist)
				{
					bestDist = dc;
					bestId = m.id;
				}
			}
		}
		else if (m.type == MeasurementType::Concentricity && m.anchors.size() >= 2)
		{
			QVector3D center1, axis1, center2, axis2;
			float radius1 = 0.0f, radius2 = 0.0f;
			if (resolveMeasurementEdgeCircle(m.anchors[0], center1, axis1, radius1)
				&& resolveMeasurementEdgeCircle(m.anchors[1], center2, axis2, radius2))
			{
				const QVector<QVector3D> circle1 = MeasurementGeometry::circlePolyline(center1, axis1, radius1);
				for (int i = 0; i < circle1.size(); ++i)
				{
					const float d = distPointToSegment(clickPt, toScreen(circle1[i]), toScreen(circle1[(i + 1) % circle1.size()]));
					if (d < bestDist)
					{
						bestDist = d;
						bestId = m.id;
					}
				}
				const QVector<QVector3D> circle2 = MeasurementGeometry::circlePolyline(center2, axis2, radius2);
				for (int i = 0; i < circle2.size(); ++i)
				{
					const float d = distPointToSegment(clickPt, toScreen(circle2[i]), toScreen(circle2[(i + 1) % circle2.size()]));
					if (d < bestDist)
					{
						bestDist = d;
						bestId = m.id;
					}
				}
				const float dc1 = (clickPt - toScreen(center1)).length();
				if (dc1 < bestDist)
				{
					bestDist = dc1;
					bestId = m.id;
				}
				const float dc2 = (clickPt - toScreen(center2)).length();
				if (dc2 < bestDist)
				{
					bestDist = dc2;
					bestId = m.id;
				}
			}
		}
		else if (m.type == MeasurementType::AngleThreePoint && m.anchors.size() >= 3)
		{
			const QVector3D vertex = resolveMeasurementAnchor(m.anchors[0]);
			const QVector3D p1 = resolveMeasurementAnchor(m.anchors[1]);
			const QVector3D p2 = resolveMeasurementAnchor(m.anchors[2]);
			const float d1 = distPointToSegment(clickPt, toScreen(vertex), toScreen(p1));
			const float d2 = distPointToSegment(clickPt, toScreen(vertex), toScreen(p2));
			const float dBest = std::min(d1, d2);
			if (dBest < bestDist)
			{
				bestDist = dBest;
				bestId = m.id;
			}
		}
		else if (m.type == MeasurementType::EdgeChain && m.anchors.size() >= 2)
		{
			// Same true-path tracing as the render branch below (see
			// resolveMeasurementEdgePolyline()'s doc comment) - hit-testing
			// against just the chord would miss clicks along a curved or
			// filleted edge's actual (highlighted) path.
			for (const MeasurementAnchorRef& a : m.anchors)
			{
				QVector<QVector3D> polyline;
				if (!resolveMeasurementEdgePolyline(a, polyline) || polyline.size() < 2)
					continue;
				for (int i = 0; i + 1 < polyline.size(); ++i)
				{
					const float d = distPointToSegment(clickPt, toScreen(polyline[i]), toScreen(polyline[i + 1]));
					if (d < bestDist)
					{
						bestDist = d;
						bestId = m.id;
					}
				}
			}
		}
		else if (m.type == MeasurementType::FaceArea && !m.anchors.isEmpty())
		{
			// Same boundary-outline computation as the render loop below
			// (see its comment) - tests proximity to the region's actual
			// boundary, not just the centroid label.
			QVector<int> triangles;
			float area = 0.0f;
			QVector3D centroid;
			SceneMesh* mesh = getMeshByUuid(m.anchors[0].meshUuid);
			if (mesh && resolveMeasurementFaceArea(m.anchors[0], triangles, area, centroid))
			{
				const std::vector<unsigned int> faceIndices = mesh->indices();
				const std::vector<float>& faceTrsfPoints = mesh->getTrsfPoints();
				auto faceVertexPos = [&faceTrsfPoints](unsigned int vIdx) -> QVector3D {
					const size_t p = static_cast<size_t>(vIdx) * 3;
					if (p + 2 >= faceTrsfPoints.size())
						return QVector3D();
					return QVector3D(faceTrsfPoints[p], faceTrsfPoints[p + 1], faceTrsfPoints[p + 2]);
				};
				const std::vector<std::array<int, 3>>& adjacency = mesh->getTriangleAdjacency();

				QSet<int> triangleSet;
				for (int t : triangles)
					triangleSet.insert(t);

				for (int t : triangles)
				{
					if (static_cast<size_t>(t) >= adjacency.size())
						continue;
					const size_t base = static_cast<size_t>(t) * 3;
					for (int e = 0; e < 3; ++e)
					{
						const int neighbor = adjacency[static_cast<size_t>(t)][e];
						if (neighbor >= 0 && triangleSet.contains(neighbor))
							continue;  // interior edge, not on the boundary
						const QVector3D pA = faceVertexPos(faceIndices[base + static_cast<size_t>(e)]);
						const QVector3D pB = faceVertexPos(faceIndices[base + static_cast<size_t>((e + 1) % 3)]);
						const float d = distPointToSegment(clickPt, toScreen(pA), toScreen(pB));
						if (d < bestDist)
						{
							bestDist = d;
							bestId = m.id;
						}
					}
				}
			}
		}
		else if (m.type == MeasurementType::MinDistance && m.anchors.size() >= 2)
		{
			// Same boundary-outline test as FaceArea above, run once per
			// region (both anchors' flood-filled faces are click targets;
			// the offset dimension line itself is handled generically by
			// hitTestDimensionLine() elsewhere, same as every other linear
			// dimension).
			auto testRegionBoundary = [&](const MeasurementAnchorRef& ref) {
				QVector<int> triangles;
				SceneMesh* mesh = getMeshByUuid(ref.meshUuid);
				if (!mesh || !resolveMeasurementFaceRegion(ref, triangles))
					return;
				const std::vector<unsigned int> faceIndices = mesh->indices();
				const std::vector<float>& faceTrsfPoints = mesh->getTrsfPoints();
				auto faceVertexPos = [&faceTrsfPoints](unsigned int vIdx) -> QVector3D {
					const size_t p = static_cast<size_t>(vIdx) * 3;
					if (p + 2 >= faceTrsfPoints.size())
						return QVector3D();
					return QVector3D(faceTrsfPoints[p], faceTrsfPoints[p + 1], faceTrsfPoints[p + 2]);
				};
				const std::vector<std::array<int, 3>>& adjacency = mesh->getTriangleAdjacency();

				QSet<int> triangleSet;
				for (int t : triangles)
					triangleSet.insert(t);

				for (int t : triangles)
				{
					if (static_cast<size_t>(t) >= adjacency.size())
						continue;
					const size_t base = static_cast<size_t>(t) * 3;
					for (int e = 0; e < 3; ++e)
					{
						const int neighbor = adjacency[static_cast<size_t>(t)][e];
						if (neighbor >= 0 && triangleSet.contains(neighbor))
							continue;  // interior edge, not on the boundary
						const QVector3D pA = faceVertexPos(faceIndices[base + static_cast<size_t>(e)]);
						const QVector3D pB = faceVertexPos(faceIndices[base + static_cast<size_t>((e + 1) % 3)]);
						const float d = distPointToSegment(clickPt, toScreen(pA), toScreen(pB));
						if (d < bestDist)
						{
							bestDist = d;
							bestId = m.id;
						}
					}
				}
			};
			testRegionBoundary(m.anchors[0]);
			testRegionBoundary(m.anchors[1]);
		}
		else if (m.type == MeasurementType::CylindricalDiameter && !m.anchors.isEmpty())
		{
			// Same geometry as the render loop below (see its comment) -
			// tests proximity to the diameter line AND the full circle
			// outline, same "whole circle is a click target" convention
			// PitchCircle's own hit-test already uses.
			float diameter = 0.0f;
			QVector3D axisOrigin, axisDir, pickedPoint;
			bool isCone = false;
			if (resolveMeasurementCylindricalDiameter(m.anchors[0], diameter, axisOrigin, axisDir, pickedPoint, isCone))
			{
				const QVector3D toPoint = pickedPoint - axisOrigin;
				const QVector3D center = axisOrigin + axisDir * QVector3D::dotProduct(toPoint, axisDir);
				const float radius = diameter * 0.5f;
				const QVector3D mirrored = center * 2.0f - pickedPoint;

				const float dLine = distPointToSegment(clickPt, toScreen(mirrored), toScreen(pickedPoint));
				if (dLine < bestDist)
				{
					bestDist = dLine;
					bestId = m.id;
				}

				const QVector<QVector3D> circle = MeasurementGeometry::circlePolyline(center, axisDir, radius);
				for (int i = 0; i < circle.size(); ++i)
				{
					const float d = distPointToSegment(clickPt, toScreen(circle[i]), toScreen(circle[(i + 1) % circle.size()]));
					if (d < bestDist)
					{
						bestDist = d;
						bestId = m.id;
					}
				}
			}
		}
	}

	return bestId;
}

MeasurementController::DimensionHit MeasurementController::hitTestDimensionLine(const QPoint& pixel, Camera* camera, const QSize& viewportSize, int pixelRadius) const
{
	DimensionHit hit;
	if (!camera || !_viewer || !_viewer->sceneGraph())
		return hit;

	const QRect viewportRect(0, 0, viewportSize.width(), viewportSize.height());
	auto toScreen = [&](const QVector3D& worldPos) -> QVector2D {
		const QVector3D projected = worldPos.project(camera->getViewMatrix(), camera->getProjectionMatrix(), viewportRect);
		return QVector2D(projected.x(), static_cast<float>(viewportSize.height()) - projected.y());
	};
	auto distPointToSegment = [](const QVector2D& p, const QVector2D& a, const QVector2D& b) -> float {
		const QVector2D ab = b - a;
		const float abLenSq = QVector2D::dotProduct(ab, ab);
		float t = abLenSq > 1.0e-6f ? QVector2D::dotProduct(p - a, ab) / abLenSq : 0.0f;
		t = std::clamp(t, 0.0f, 1.0f);
		return (p - (a + ab * t)).length();
	};

	const QVector2D clickPt(static_cast<float>(pixel.x()), static_cast<float>(pixel.y()));
	float bestDist = static_cast<float>(pixelRadius);

	for (const Measurement& m : _viewer->sceneGraph()->measurements())
	{
		if (!isEffectivelyVisible(m))
			continue;

		QVector3D a, b;
		if (resolveMeasurementDimensionSegment(m, a, b))
		{
			const QVector3D offsetVec = resolveDimensionOffsetVector(a, b, m, camera);
			const QVector3D aOff = a + offsetVec;
			const QVector3D bOff = b + offsetVec;

			const float d = distPointToSegment(clickPt, toScreen(aOff), toScreen(bOff));
			if (d < bestDist)
			{
				bestDist = d;
				hit.measurementId = m.id;
				hit.kind = DimensionDragKind::Linear;
			}
			continue;
		}

		QVector3D vertex, u, v;
		float angleRad = 0.0f;
		float radius = 0.0f;
		if (resolveMeasurementAngleGeometry(m, camera, vertex, u, v, angleRad, radius))
		{
			// Walk just the arc segment [0, angleRad] directly in the (u,v)
			// basis - circlePolyline() sweeps a full circle in a basis with
			// no relation to u/v's angular origin, so it isn't reusable here.
			constexpr int arcSegments = 24;
			QVector3D prevPoint = vertex + u * radius;
			for (int i = 1; i <= arcSegments; ++i)
			{
				const float t = angleRad * (static_cast<float>(i) / static_cast<float>(arcSegments));
				const QVector3D nextPoint = vertex + (u * std::cos(t) + v * std::sin(t)) * radius;
				const float d = distPointToSegment(clickPt, toScreen(prevPoint), toScreen(nextPoint));
				if (d < bestDist)
				{
					bestDist = d;
					hit.measurementId = m.id;
					hit.kind = DimensionDragKind::AngleRadius;
				}
				prevPoint = nextPoint;
			}
		}
	}

	return hit;
}

void MeasurementController::beginDimensionLineDrag(const QUuid& measurementId, DimensionDragKind kind, Camera* camera)
{
	if (!camera || !_viewer || !_viewer->sceneGraph() || kind == DimensionDragKind::None)
		return;

	const int index = _viewer->sceneGraph()->measurementIndexById(measurementId);
	if (index < 0)
		return;
	const Measurement& m = _viewer->sceneGraph()->measurements().at(index);

	_dimensionDragKind = kind;

	if (kind == DimensionDragKind::Linear)
	{
		QVector3D a, b;
		if (!resolveMeasurementDimensionSegment(m, a, b))
		{
			_dimensionDragKind = DimensionDragKind::None;
			return;
		}
		_dimensionDragPivot = (a + b) * 0.5f;
		// The dimension line's own direction - the NORMAL of the plane the
		// drag freely repositions the offset within (see
		// updateDimensionLineDrag()'s ray/plane intersection).
		_dimensionDragAxis = (b - a).normalized();
		_dimensionDragStartOffsetVector = resolveDimensionOffsetVector(a, b, m, camera);
	}
	else  // AngleRadius
	{
		QVector3D vertex, u, v;
		float angleRad = 0.0f;
		float radius = 0.0f;
		if (!resolveMeasurementAngleGeometry(m, camera, vertex, u, v, angleRad, radius))
		{
			_dimensionDragKind = DimensionDragKind::None;
			return;
		}
		_dimensionDragPivot = vertex;
		// Bisector of the two legs - the 1D direction the radius drag
		// measures magnitude along.
		const QVector3D bisector = (u * std::cos(angleRad * 0.5f) + v * std::sin(angleRad * 0.5f));
		_dimensionDragAxis = bisector.lengthSquared() > 1.0e-8f ? bisector.normalized() : u;
		_dimensionDragStartOffsetScalar = radius;
		// Reference length for the world-per-screen-pixel ratio (same
		// technique as updateTransformGizmoTranslationDrag()'s dragScale).
		_dimensionDragRefLength = std::max(_dimensionDragStartOffsetScalar, 0.01f);
	}

	_dimensionDragActive = true;
}

void MeasurementController::updateDimensionLineDrag(const QPoint& pixel, Camera* camera, const QSize& viewportSize)
{
	if (!camera || !_viewer || !_viewer->sceneGraph() || !_dimensionDragActive)
		return;

	const QRect viewport(0, 0, viewportSize.width(), viewportSize.height());
	const QMatrix4x4 viewMatrix = camera->getViewMatrix();
	const QMatrix4x4 projMatrix = camera->getProjectionMatrix();

	if (_dimensionDragKind == DimensionDragKind::Linear)
	{
		// True ray/plane intersection: cast a ray from the camera through
		// the current mouse pixel, intersect it with the plane through
		// _dimensionDragPivot whose normal is the dimension line's own
		// direction (_dimensionDragAxis) - i.e. the plane containing every
		// valid perpendicular offset. The intersection point minus the
		// pivot IS the new offset vector directly - this is what lets the
		// drag both "pivot" (change direction) and "extend" (change
		// magnitude) in one continuous motion, unlike a single-axis ratio
		// drag which can only ever change magnitude along one fixed axis.
		const int glX = pixel.x();
		const int glY = viewportSize.height() - pixel.y() - 1;  // Qt top-down -> GL bottom-up, same convention used elsewhere for unproject()
		const QVector3D rayOrigin = QVector3D(static_cast<float>(glX), static_cast<float>(glY), 0.0f).unproject(viewMatrix, projMatrix, viewport);
		QVector3D rayDir = QVector3D(static_cast<float>(glX), static_cast<float>(glY), 1.0f).unproject(viewMatrix, projMatrix, viewport) - rayOrigin;
		if (rayDir.lengthSquared() < 1.0e-12f)
			return;
		rayDir.normalize();

		const float denom = QVector3D::dotProduct(rayDir, _dimensionDragAxis);
		if (std::abs(denom) < 1.0e-6f)
			return;  // ray parallel to the plane (viewing exactly edge-on) - leave the offset unchanged this frame rather than divide by ~0

		const float t = QVector3D::dotProduct(_dimensionDragPivot - rayOrigin, _dimensionDragAxis) / denom;
		if (t < 0.0f)
			return;  // intersection behind the camera - degenerate, ignore this frame

		const QVector3D hitPoint = rayOrigin + rayDir * t;
		QVector3D newOffset = hitPoint - _dimensionDragPivot;
		// Project out any residual component along the dimension-line axis
		// (should already be ~0 since hitPoint lies in the plane, but keep
		// this exact against floating-point drift) so the dimension line
		// stays exactly parallel to the measured segment.
		newOffset -= _dimensionDragAxis * QVector3D::dotProduct(newOffset, _dimensionDragAxis);

		// Floored magnitude, not allowed to collapse to ~0 - that would put
		// the dimension line on top of the actual measured geometry,
		// defeating the point of having one. Direction is preserved.
		const float mag = newOffset.length();
		constexpr float kMinOffsetMagnitude = 0.01f;
		if (mag < kMinOffsetMagnitude)
			newOffset = (mag > 1.0e-8f ? newOffset / mag : _dimensionDragAxis) * kMinOffsetMagnitude;

		_viewer->sceneGraph()->setMeasurementOffsetVector(_dimensionDragCandidateId, newOffset);
	}
	else  // AngleRadius
	{
		// Screen-space axis projection + world-per-pixel rescaling -
		// identical technique to updateTransformGizmoTranslationDrag()'s
		// single-axis translate drag: project two known points on the fixed
		// drag axis to screen space, dot the mouse's pixel delta against
		// that 2D direction, then rescale by (refLength / axisScreenLength)
		// to recover a world-space distance. Extension only, along the
		// fixed bisector - no plane/pivot freedom for the angle case.
		const QVector3D pivotScreen3 = _dimensionDragPivot.project(viewMatrix, projMatrix, viewport);
		const QVector3D axisEndWorld = _dimensionDragPivot + _dimensionDragAxis * _dimensionDragRefLength;
		const QVector3D axisEndScreen3 = axisEndWorld.project(viewMatrix, projMatrix, viewport);

		const QVector2D pivotScreen(pivotScreen3.x(), pivotScreen3.y());
		const QVector2D axisScreen = QVector2D(axisEndScreen3.x(), axisEndScreen3.y()) - pivotScreen;
		const float axisScreenLength = axisScreen.length();
		if (axisScreenLength <= 1.0e-4f)
			return;

		const QVector2D axisScreenDir = axisScreen / axisScreenLength;
		const QVector2D mouseDelta(static_cast<float>(pixel.x() - _dimensionDragStartPixel.x()),
			static_cast<float>(_dimensionDragStartPixel.y() - pixel.y()));  // Y-flip, same convention as updateTransformGizmoTranslationDrag()
		const float projectedPixels = QVector2D::dotProduct(mouseDelta, axisScreenDir);
		const float worldDelta = (projectedPixels / axisScreenLength) * _dimensionDragRefLength;

		const float newOffset = std::max(_dimensionDragStartOffsetScalar + worldDelta, 0.01f);
		_viewer->sceneGraph()->setMeasurementOffsetDistance(_dimensionDragCandidateId, newOffset);
	}

	emit measurementStateChanged();
}

void MeasurementController::finishDimensionLineDrag(ViewportWidget* viewportWidget)
{
	if (_dimensionDragActive && _viewer && _viewer->sceneGraph())
	{
		const int index = _viewer->sceneGraph()->measurementIndexById(_dimensionDragCandidateId);
		const Measurement* mm = (index >= 0) ? &_viewer->sceneGraph()->measurements().at(index) : nullptr;

		// Redundant re-apply of the same final value on redo() (it's already
		// live from the drag) but establishes the undo edge - same "one
		// command on release" shape as TransformCommand's gizmo-drag pattern.
		// The Command classes take a ViewportWidget* purely for the shared
		// ModelViewerCommand base's repaint trigger on undo/redo (unrelated
		// to measurement state) - passed through as a parameter here rather
		// than stored, the one unavoidable exception to this controller
		// otherwise depending on no ViewportWidget back-reference at all.
		if (_dimensionDragKind == DimensionDragKind::Linear)
		{
			const QVector3D finalOffset = mm ? mm->offsetVector : _dimensionDragStartOffsetVector;
			if ((finalOffset - _dimensionDragStartOffsetVector).lengthSquared() > 1.0e-10f && _viewer->getUndoStack())
			{
				_viewer->getUndoStack()->push(new MeasurementOffsetVectorCommand(_viewer, viewportWidget,
					_dimensionDragCandidateId, _dimensionDragStartOffsetVector, finalOffset));
			}
		}
		else if (_dimensionDragKind == DimensionDragKind::AngleRadius)
		{
			const float finalOffset = mm ? mm->offsetDistance : _dimensionDragStartOffsetScalar;
			if (std::abs(finalOffset - _dimensionDragStartOffsetScalar) > 1.0e-5f && _viewer->getUndoStack())
			{
				_viewer->getUndoStack()->push(new MeasurementOffsetCommand(_viewer, viewportWidget,
					_dimensionDragCandidateId, _dimensionDragStartOffsetScalar, finalOffset));
			}
		}
	}

	_dimensionDragActive = false;
	_dimensionDragCandidate = false;
	_dimensionDragCandidateId = QUuid();
	_dimensionDragKind = DimensionDragKind::None;
}

void MeasurementController::drawMeasurementOverlay(Camera* camera, const QSize& viewportSize, TextRenderer* axisTextRenderer)
{
	if (!_glFunctionsInitialized || !camera || !axisTextRenderer || !_renderCtrl.axisShader() || !_viewer || !_viewer->sceneGraph())
		return;

	const QVector<Measurement>& measurements = _viewer->sceneGraph()->measurements();
	if (measurements.isEmpty() && _pendingMeasurementAnchors.isEmpty()
		&& !_measurementHoverAnchor.isValid() && !_measurementEdgeHoverAnchor.isValid())
		return;

	struct LabelEntry { QVector3D worldPos; QString text; };
	std::vector<float> lineVertices;
	std::vector<float> triangleVertices;  // dimension-line arrowhead cones - see addCone() below
	QVector<LabelEntry> labels;

	// M_PI isn't guaranteed available (MSVC needs _USE_MATH_DEFINES before
	// <cmath>) - same local-constant convention as MeasurementGeometry.cpp.
	constexpr float kTwoPiLocal = 6.283185307179586f;

	const QVector3D pointColor(0.15f, 0.85f, 1.0f);
	const QVector3D distanceColor(1.0f, 0.82f, 0.15f);
	const QVector3D pendingColor(1.0f, 1.0f, 1.0f);
	const QVector3D hoverSnapColor(0.25f, 1.0f, 0.35f);  // will snap to this vertex
	const QVector3D hoverRawColor(0.65f, 0.65f, 0.65f);  // raw surface pick, no snap nearby
	const QVector3D selectedColor(1.0f, 0.35f, 0.05f);   // orange - already selected

	auto addSegment = [&lineVertices](const QVector3D& a, const QVector3D& b, const QVector3D& color) {
		lineVertices.insert(lineVertices.end(), { a.x(), a.y(), a.z(), color.x(), color.y(), color.z() });
		lineVertices.insert(lineVertices.end(), { b.x(), b.y(), b.z(), color.x(), color.y(), color.z() });
	};

	// Traces an edge anchor's TRUE tessellated path (see
	// resolveMeasurementEdgePolyline()'s doc comment) rather than a chord
	// between its two ends - for a straight edge this reduces to the exact
	// same single segment as before, so this is a pure correctness upgrade
	// for any caller that was previously drawing a chord unconditionally.
	auto addEdgeTrace = [&](const MeasurementAnchorRef& ref, const QVector3D& segColor) {
		QVector<QVector3D> polyline;
		if (resolveMeasurementEdgePolyline(ref, polyline) && polyline.size() >= 2)
		{
			for (int i = 0; i + 1 < polyline.size(); ++i)
				addSegment(polyline[i], polyline[i + 1], segColor);
		}
	};

	// Marker cross size scales with the camera's current view range so it
	// stays a sensible on-screen size whether the user is zoomed in on a
	// small detail or looking at the whole model. sizeMultiplier gives
	// hover/selection extra visual weight beyond just a color change.
	const float markerSize = std::max(camera->getViewRange(), 0.0001f) * 0.01f;

	// Arrowhead cones use a per-point constant-screen-size scale instead -
	// mirrors TransformGizmo::computeWorldScale()'s exact technique (same
	// ortho-vs-perspective split, same idea of reacting to THIS point's own
	// depth rather than a single scene-wide value). markerSize above is
	// scene-wide (camera->getViewRange() alone), which is fine for small
	// point-cross markers but wrong for a dimension's arrowheads under
	// perspective projection: a dimension sitting much closer to the
	// camera than the current orbit pivot would get an oversized cone,
	// and one much farther away an undersized one, even though the
	// overall "zoom" (viewRange) hasn't changed at all.
	auto coneScaleAt = [&](const QVector3D& worldPos) -> float {
		if (camera->getProjectionType() == Camera::ProjectionType::ORTHOGRAPHIC)
			return markerSize;
		const float distance = (camera->getRenderPosition() - worldPos).length();
		return std::max(distance * 0.01f, 0.0001f);
	};

	auto addMarker = [&](const QVector3D& p, const QVector3D& color, float sizeMultiplier = 1.0f) {
		const float s = markerSize * sizeMultiplier;
		addSegment(p - QVector3D(s, 0, 0), p + QVector3D(s, 0, 0), color);
		addSegment(p - QVector3D(0, s, 0), p + QVector3D(0, s, 0), color);
		addSegment(p - QVector3D(0, 0, s), p + QVector3D(0, 0, s), color);
	};

	auto addCircleOutline = [&](const QVector3D& center, const QVector3D& normal, float radius, const QVector3D& color) {
		const QVector<QVector3D> circle = MeasurementGeometry::circlePolyline(center, normal, radius);
		for (int i = 0; i < circle.size(); ++i)
			addSegment(circle[i], circle[(i + 1) % circle.size()], color);
	};

	// CAD-style dimension-line arrowhead: a solid cone with its apex at
	// `apex`, pointing along `direction` (base sits behind the apex, at
	// apex - direction*height). Emits triangleVertices, not lineVertices -
	// a flat-shaded cone needs real triangles, not GL_LINES, so this is
	// drawn in a separate pass/buffer below (see the GL_TRIANGLES block near
	// the end of this function).
	auto addCone = [&](const QVector3D& apex, const QVector3D& direction, float radius, float height, const QVector3D& color) {
		const QVector3D dir = direction.normalized();
		const QVector3D reference = (std::abs(QVector3D::dotProduct(dir, QVector3D(0.0f, 1.0f, 0.0f))) < 0.9f)
			? QVector3D(0.0f, 1.0f, 0.0f)
			: QVector3D(1.0f, 0.0f, 0.0f);
		const QVector3D u = QVector3D::crossProduct(dir, reference).normalized();
		const QVector3D v = QVector3D::crossProduct(dir, u).normalized();
		const QVector3D base = apex - dir * height;

		constexpr int coneSegments = 10;
		auto ringPoint = [&](int i) {
			const float theta = (kTwoPiLocal * static_cast<float>(i)) / static_cast<float>(coneSegments);
			return base + (u * std::cos(theta) + v * std::sin(theta)) * radius;
		};
		auto pushTri = [&](const QVector3D& a, const QVector3D& b, const QVector3D& c) {
			triangleVertices.insert(triangleVertices.end(), { a.x(), a.y(), a.z(), color.x(), color.y(), color.z() });
			triangleVertices.insert(triangleVertices.end(), { b.x(), b.y(), b.z(), color.x(), color.y(), color.z() });
			triangleVertices.insert(triangleVertices.end(), { c.x(), c.y(), c.z(), color.x(), color.y(), color.z() });
		};
		for (int i = 0; i < coneSegments; ++i)
		{
			const QVector3D b0 = ringPoint(i);
			const QVector3D b1 = ringPoint(i + 1);
			pushTri(apex, b0, b1);   // lateral surface
			pushTri(base, b1, b0);   // base cap (visible when viewed from behind)
		}
	};

	// A measured segment rendered with CAD-style arrowheads: the connecting
	// line plus a cone at each end, tip touching the endpoint and pointing
	// outward, base set back toward the opposite end. 1:3 radius:height
	// ratio per CAD convention (a slender arrow, not a fat one). Capped so
	// arrowheads on a very short dimension don't overlap each other.
	auto addDimensionLine = [&](const QVector3D& a, const QVector3D& b, const QVector3D& color) {
		addSegment(a, b, color);
		const QVector3D delta = b - a;
		const float len = delta.length();
		if (len < 1.0e-6f)
			return;
		const QVector3D dirN = delta / len;
		// One shared scale for both cones (evaluated at the line's own
		// midpoint) rather than one per end - a dimension line is short
		// relative to camera distance in practice, so the two ends'
		// individual depths rarely differ enough to matter, and matching
		// cone sizes at both ends reads better than two subtly different
		// ones on the same line.
		// 0.6f = half of the original 1.2f base radius factor (1:3
		// radius:height ratio preserved below, since coneHeight is derived
		// straight from coneRadius).
		const float coneRadius = coneScaleAt((a + b) * 0.5f) * 0.6f;
		const float coneHeight = std::min(coneRadius * 3.0f, len * 0.4f);
		addCone(a, -dirN, coneRadius, coneHeight, color);
		addCone(b, dirN, coneRadius, coneHeight, color);
	};

	// Full CAD-style linear dimension: the dimension line itself is offset
	// away from the actual measured points [a,b] (not coincident with them),
	// connected back via thin extension ("witness") lines - the standard CAD
	// drafting convention, and the reason a dimension floats clear of the
	// part instead of embedding in/behind it. The offset vector comes from
	// resolveDimensionOffsetVector() - the same query hitTestDimensionLine()/
	// the drag interaction use, so rendering and interaction can never
	// disagree about where the dimension line actually is (and, once
	// dragged, the extension lines "pivot" to match - see
	// Measurement::offsetVector's doc comment). Returns the offset
	// dimension line's midpoint, for label placement.
	auto addOffsetDimension = [&](const QVector3D& a, const QVector3D& b, const QVector3D& color, const Measurement& mm) -> QVector3D {
		const QVector3D delta = b - a;
		const float len = delta.length();
		if (len < 1.0e-6f)
		{
			addMarker(a, color);
			return a;
		}

		const QVector3D offsetVec = resolveDimensionOffsetVector(a, b, mm, camera);
		const QVector3D aOff = a + offsetVec;
		const QVector3D bOff = b + offsetVec;

		addSegment(a, aOff, color);  // extension line at a
		addSegment(b, bOff, color);  // extension line at b
		addDimensionLine(aOff, bOff, color);
		return (aOff + bOff) * 0.5f;
	};

	// Curved analogue of addOffsetDimension(), for a curved EdgeLength pick
	// whose underlying curve is a true circle (fillets/rounds - by far the
	// common case, via resolveMeasurementEdgeCircle()). A straight offset
	// line's own on-screen length wouldn't match the curve-length value
	// it's labeled with (see the EdgeLength render branch below), so
	// instead the dimension line is concentric with the edge itself,
	// offset radially outward by the same default magnitude a straight
	// dimension uses, with extension lines running radially from each true
	// endpoint (exactly "normal to the edge" there, since a circle's
	// radius direction is always perpendicular to its own tangent) out to
	// the offset arc. `polyline` must already be the edge's true
	// tessellated path in order (see resolveMeasurementEdgePolyline()) -
	// each of its points is projected to the same angular position at the
	// larger radius, so the offset arc traces the exact same path (and
	// winds the same way around the circle) rather than a generic sweep
	// between just the two ends. Static offset magnitude for now (not yet
	// draggable, matching Chain Length/Pitch Circle/Concentricity's own
	// "static report" precedent). Returns an on-arc label position.
	auto addOffsetArcDimension = [&](const QVector<QVector3D>& polyline, const QVector3D& center,
		const QVector3D& axis, const QVector3D& color) -> QVector3D {
		if (polyline.size() < 2)
			return polyline.isEmpty() ? QVector3D() : polyline.first();

		const QVector3D n = axis.normalized();
		const float offsetMag = defaultDimensionOffsetMagnitude(camera);

		auto radialOffset = [&](const QVector3D& p) -> QVector3D {
			const QVector3D toP = p - center;
			const QVector3D alongAxis = n * QVector3D::dotProduct(toP, n);
			const QVector3D inPlane = toP - alongAxis;
			const float r = inPlane.length();
			return (r < 1.0e-6f) ? p : center + alongAxis + inPlane * ((r + offsetMag) / r);
		};

		QVector<QVector3D> offsetPts;
		offsetPts.reserve(polyline.size());
		for (const QVector3D& p : polyline)
			offsetPts.append(radialOffset(p));

		addSegment(polyline.first(), offsetPts.first(), color);  // extension line at the start
		addSegment(polyline.last(), offsetPts.last(), color);    // extension line at the end
		for (int i = 0; i + 1 < offsetPts.size(); ++i)
			addSegment(offsetPts[i], offsetPts[i + 1], color);

		// Arrowheads tangent to the arc at each end, pointing outward (away
		// from the arc's middle) - same "tip touches the endpoint, base
		// set back toward the middle" convention as addDimensionLine(),
		// using the LOCAL tangent direction there rather than the overall
		// chord (visually correct even for a tight arc where the two
		// diverge a lot).
		const float coneRadius = coneScaleAt((offsetPts.first() + offsetPts.last()) * 0.5f) * 0.6f;
		const QVector3D dirStart = offsetPts[1] - offsetPts[0];
		if (dirStart.lengthSquared() > 1.0e-10f)
		{
			const QVector3D dirN = dirStart.normalized();
			const float coneHeight = std::min(coneRadius * 3.0f, dirStart.length() * 0.4f);
			addCone(offsetPts.first(), -dirN, coneRadius, coneHeight, color);
		}
		const QVector3D dirEnd = offsetPts[offsetPts.size() - 1] - offsetPts[offsetPts.size() - 2];
		if (dirEnd.lengthSquared() > 1.0e-10f)
		{
			const QVector3D dirN = dirEnd.normalized();
			const float coneHeight = std::min(coneRadius * 3.0f, dirEnd.length() * 0.4f);
			addCone(offsetPts.last(), dirN, coneRadius, coneHeight, color);
		}

		return offsetPts[offsetPts.size() / 2];
	};

	// The floating-vertex angular dimension's full visual: two legs from
	// `vertex` (one along `u`, the other along whatever direction is
	// exactly `angleRad` around from `u` toward `v` - by construction that's
	// the original second direction the angle was measured against, so it
	// doesn't need to be passed in separately), a swept arc at `radius`
	// between them, tangent arrowhead cones at each end, and the angle text
	// at the arc's midpoint. Shared by every measurement type whose non-
	// parallel case renders this way (FaceToFace, EdgeToEdge, EdgeToFace) -
	// geometry comes from resolveMeasurementAngleGeometry(), the same query
	// hitTestDimensionLine()/the drag interaction use, so none of them can
	// ever disagree about where the arc actually is. Returns the label
	// position.
	auto addAngleArc = [&](const QVector3D& vertex, const QVector3D& u, const QVector3D& v,
		float angleRad, float radius, const QVector3D& color) -> QVector3D {
		const float legLength = radius / 0.85f;
		const QVector3D secondDir = u * std::cos(angleRad) + v * std::sin(angleRad);
		addSegment(vertex, vertex + u * legLength, color);
		addSegment(vertex, vertex + secondDir * legLength, color);

		constexpr int arcSegments = 24;
		QVector3D prevPoint = vertex + u * radius;
		for (int i = 1; i <= arcSegments; ++i)
		{
			const float t = angleRad * (static_cast<float>(i) / static_cast<float>(arcSegments));
			const QVector3D nextPoint = vertex + (u * std::cos(t) + v * std::sin(t)) * radius;
			addSegment(prevPoint, nextPoint, color);
			prevPoint = nextPoint;
		}

		// Arrowheads tangent to the arc at each end, pointing outward (away
		// from the arc's middle) - mirrors addDimensionLine()'s "tips touch
		// the endpoints, pointing away from the middle" convention. Scaled
		// at the arc's own vertex (both cones share it, same reasoning as
		// addDimensionLine()'s shared midpoint scale).
		// 0.6f = half of the original 1.2f base radius factor, same as
		// addDimensionLine()'s cones (1:3 ratio preserved below).
		const float coneRadius = coneScaleAt(vertex) * 0.6f;
		const float coneHeight = std::min(coneRadius * 3.0f, radius * 0.3f);
		const QVector3D startPoint = vertex + u * radius;
		addCone(startPoint, -v, coneRadius, coneHeight, color);  // derivative at t=0 is +v; outward is reversed
		const QVector3D endPoint = vertex + secondDir * radius;
		const QVector3D endTangentOutward = -std::sin(angleRad) * u + std::cos(angleRad) * v;  // derivative at t=angleRad
		addCone(endPoint, endTangentOutward, coneRadius, coneHeight, color);

		const float midT = angleRad * 0.5f;
		return vertex + (u * std::cos(midT) + v * std::sin(midT)) * (radius * 1.15f);
	};

	for (const Measurement& m : measurements)
	{
		if (!isEffectivelyVisible(m))
			continue;

		const bool isSelected = _selectedMeasurementIds.contains(m.id);
		// Selection is the stronger cue and wins if somehow both apply
		// (shouldn't normally happen - hover-select only runs while nothing
		// new is being placed - but a stale hover from just before a click
		// landed is a real possibility for one frame).
		const bool isHovered = !isSelected && (m.id == _hoveredMeasurementId);

		QVector3D color = isSelected ? selectedColor
			: (m.type == MeasurementType::Point ? pointColor : distanceColor);
		if (isHovered)
			color = color * 0.5f + QVector3D(1.0f, 1.0f, 1.0f) * 0.5f;  // blend toward white - a lighter preview than full selection
		const float sizeMultiplier = isSelected ? 1.5f : (isHovered ? 1.25f : 1.0f);
		// The bundled viewport font (TextRenderer, fonts/arialbd.ttf) has no
		// glyph for '⌀' (U+2300 DIAMETER SIGN, a rare "Miscellaneous
		// Technical" character many fonts skip - confirmed missing; '°'
		// U+00B0 is a much more common glyph and renders fine) - substitute
		// 'Ø' (U+00D8, Latin-1 "O with stroke") for on-screen labels only,
		// a real, commonly-used CAD/drafting stand-in, visually close (a
		// circle with a diagonal stroke) and confirmed renderable (same
		// Latin-1 Supplement range the degree sign already uses). The
		// Measurement dialog keeps the exact '⌀' - Qt's own text rendering
		// has no such font limitation.
		QString summary = measurementSummaryText(m);
		summary.replace(QChar(0x2300), QChar(0x00D8));

		// Separate, stronger hover cue for the draggable dimension line/arc
		// specifically (see mouseMoveEvent()'s _hoveredDimensionId update) -
		// distinct from `color` above (used for markers/legs/normal-
		// indicators, which don't change) so the exact grabbable part reads
		// clearly, not the whole measurement.
		const bool isDimensionHovered = !isSelected && (m.id == _hoveredDimensionId);
		const QVector3D dimensionColor = isDimensionHovered
			? (color * 0.4f + QVector3D(1.0f, 1.0f, 1.0f) * 0.6f)
			: color;

		if (m.type == MeasurementType::Point && !m.anchors.isEmpty())
		{
			const QVector3D p = resolveMeasurementAnchor(m.anchors[0]);
			addMarker(p, color, sizeMultiplier);
			labels.append({ p, summary });
		}
		else if (m.type == MeasurementType::Distance && m.anchors.size() >= 2)
		{
			const QVector3D a = resolveMeasurementAnchor(m.anchors[0]);
			const QVector3D b = resolveMeasurementAnchor(m.anchors[1]);
			addMarker(a, color, sizeMultiplier);
			addMarker(b, color, sizeMultiplier);
			const QVector3D labelPos = addOffsetDimension(a, b, dimensionColor, m);
			labels.append({ labelPos, summary });
		}
		else if (m.type == MeasurementType::ArcRadius3Point && m.anchors.size() >= 3)
		{
			const QVector3D p0 = resolveMeasurementAnchor(m.anchors[0]);
			const QVector3D p1 = resolveMeasurementAnchor(m.anchors[1]);
			const QVector3D p2 = resolveMeasurementAnchor(m.anchors[2]);
			QVector3D center, normal;
			float radius = 0.0f;
			if (MeasurementGeometry::circumcircle3Point(p0, p1, p2, center, normal, radius))
			{
				addMarker(p0, color, sizeMultiplier);
				addMarker(p1, color, sizeMultiplier);
				addMarker(p2, color, sizeMultiplier);
				addMarker(center, color, sizeMultiplier * 0.6f);
				addCircleOutline(center, normal, radius, color);
				labels.append({ center, summary });
			}
		}
		else if (m.type == MeasurementType::ArcRadiusCenterPoint && m.anchors.size() >= 3)
		{
			const QVector3D center = resolveMeasurementAnchor(m.anchors[0]);
			const QVector3D p1 = resolveMeasurementAnchor(m.anchors[1]);
			const QVector3D p2 = resolveMeasurementAnchor(m.anchors[2]);
			QVector3D normal;
			float radius = 0.0f;
			if (MeasurementGeometry::circleFromCenterAndTwoPoints(center, p1, p2, normal, radius))
			{
				addMarker(center, color, sizeMultiplier * 0.6f);
				addMarker(p1, color, sizeMultiplier);
				addMarker(p2, color, sizeMultiplier);
				addSegment(center, p1, color);
				addSegment(center, p2, color);
				addCircleOutline(center, normal, radius, color);
				labels.append({ center, summary });
			}
		}
		else if (m.type == MeasurementType::EdgeRadius && !m.anchors.isEmpty())
		{
			QVector3D center, axis;
			float radius = 0.0f;
			if (resolveMeasurementEdgeCircle(m.anchors[0], center, axis, radius))
			{
				addMarker(center, color, sizeMultiplier * 0.6f);
				addCircleOutline(center, axis, radius, color);
				labels.append({ center, summary });
			}
		}
		else if (m.type == MeasurementType::FaceToFace && m.anchors.size() >= 2)
		{
			QVector3D p1, n1, p2, n2;
			if (resolveMeasurementAnchorPlane(m.anchors[0], p1, n1) && resolveMeasurementAnchorPlane(m.anchors[1], p2, n2))
			{
				const float normalLen = markerSize * 3.0f;
				addMarker(p1, color, sizeMultiplier);
				addMarker(p2, color, sizeMultiplier);
				addSegment(p1, p1 + n1 * normalLen, color);
				addSegment(p2, p2 + n2 * normalLen, color);

				const MeasurementGeometry::FaceToFaceResult result = MeasurementGeometry::compareFaces(p1, n1, p2, n2);
				if (result.isParallel)
				{
					// Dimension line between the two (near-)parallel planes:
					// from p1, straight along n1, to the point that's
					// coplanar with p2 - offset + extension lines +
					// arrowheads via addOffsetDimension().
					const QVector3D projected = p1 + n1 * QVector3D::dotProduct(p2 - p1, n1);
					const QVector3D labelPos = addOffsetDimension(p1, projected, dimensionColor, m);
					labels.append({ labelPos, summary });
				}
				else
				{
					// Angular dimension: since two arbitrary faces have no
					// natural shared vertex/edge, the angle is shown
					// "floating" at the midpoint between the two picks.
					// Vertex/basis/angle/radius all come from
					// resolveMeasurementAngleGeometry() - the same query
					// hitTestDimensionLine()/the drag interaction use, so
					// this can't disagree with either about where the arc
					// actually is.
					QVector3D vertex, u, v;
					float angleRad = 0.0f;
					float arcRadius = 0.0f;
					if (resolveMeasurementAngleGeometry(m, camera, vertex, u, v, angleRad, arcRadius))
					{
						const QVector3D labelPos = addAngleArc(vertex, u, v, angleRad, arcRadius, dimensionColor);
						labels.append({ labelPos, summary });
					}
				}
			}
		}
		else if (m.type == MeasurementType::PointToFace && m.anchors.size() >= 2)
		{
			const QVector3D point = resolveMeasurementAnchor(m.anchors[0]);
			QVector3D facePos, faceNormal;
			if (resolveMeasurementAnchorPlane(m.anchors[1], facePos, faceNormal))
			{
				const float normalLen = markerSize * 3.0f;
				addMarker(point, color, sizeMultiplier);
				addMarker(facePos, color, sizeMultiplier);
				addSegment(facePos, facePos + faceNormal * normalLen, color);

				// Dimension line from the point straight down to its
				// projection onto the face's plane, offset + extension
				// lines + arrowheads via addOffsetDimension().
				const QVector3D projected = point - faceNormal * QVector3D::dotProduct(point - facePos, faceNormal);
				const QVector3D labelPos = addOffsetDimension(point, projected, dimensionColor, m);
				labels.append({ labelPos, summary });
			}
		}
		else if (m.type == MeasurementType::EdgeLength && !m.anchors.isEmpty())
		{
			QVector3D start, end;
			float length = 0.0f;
			if (resolveMeasurementEdgeGeometry(m.anchors[0], start, end, length))
			{
				// Same offset + extension-line + drag treatment as every
				// other linear dimension (see addOffsetDimension()) -
				// consistent with Distance/Point-to-Face/Face-to-Face even
				// though the edge itself is already visible geometry, so
				// the dimension doesn't have to sit exactly on top of the
				// model's own edge to be measured. That reasoning only
				// holds for a genuinely STRAIGHT edge, though: its offset
				// line's own on-screen length equals the value it's
				// labeled with. For a curved or filleted edge that's no
				// longer true (the reported length is the CURVE's length,
				// not the straight chord an offset line would show), so a
				// floating STRAIGHT dimension line would visually
				// misrepresent its own label. If the edge is a true circle
				// (fillets/rounds - resolveMeasurementEdgeCircle()), a
				// curved dimension line concentric with the edge itself
				// solves that (see addOffsetArcDimension()); anything else
				// (a general spline edge, rare in practice) falls back to
				// just tracing the TRUE tessellated path (see
				// resolveMeasurementEdgePolyline()'s doc comment) directly
				// on the part with the label at its chord midpoint - the
				// same treatment Chain Length uses. Gated to
				// polyline.size() > 2 (more than one segment, i.e.
				// genuinely non-linear) so a straight edge's look and
				// draggable dimension line are unchanged from before.
				QVector<QVector3D> polyline;
				const bool isCurved = resolveMeasurementEdgePolyline(m.anchors[0], polyline) && polyline.size() > 2;
				QVector3D labelPos;
				if (isCurved)
				{
					for (int i = 0; i + 1 < polyline.size(); ++i)
						addSegment(polyline[i], polyline[i + 1], color);

					QVector3D circCenter, circAxis;
					float circRadius = 0.0f;
					labelPos = resolveMeasurementEdgeCircle(m.anchors[0], circCenter, circAxis, circRadius)
						? addOffsetArcDimension(polyline, circCenter, circAxis, dimensionColor)
						: (start + end) * 0.5f;
				}
				else
				{
					labelPos = addOffsetDimension(start, end, dimensionColor, m);
				}
				addMarker(start, color, sizeMultiplier);
				addMarker(end, color, sizeMultiplier);
				labels.append({ labelPos, summary });
			}
		}
		else if (m.type == MeasurementType::EdgeToVertex && m.anchors.size() >= 2)
		{
			QVector3D edgeStart, edgeEnd;
			float edgeLength = 0.0f;
			if (resolveMeasurementEdgeGeometry(m.anchors[0], edgeStart, edgeEnd, edgeLength))
			{
				const QVector3D point = resolveMeasurementAnchor(m.anchors[1]);
				const QVector3D projected = MeasurementGeometry::closestPointOnLine(point, edgeStart, edgeEnd - edgeStart);

				// The edge itself, highlighted as reference context (not
				// draggable - only the point-to-edge dimension line is).
				// Traced via its true tessellated path so a curved or
				// filleted edge isn't shown as a straight chord.
				addEdgeTrace(m.anchors[0], color);
				addMarker(edgeStart, color, sizeMultiplier);
				addMarker(edgeEnd, color, sizeMultiplier);
				addMarker(point, color, sizeMultiplier);

				// Dimension line from the point to its perpendicular foot on
				// the edge's infinite line, offset + extension lines +
				// arrowheads via addOffsetDimension().
				const QVector3D labelPos = addOffsetDimension(point, projected, dimensionColor, m);
				labels.append({ labelPos, summary });
			}
		}
		else if (m.type == MeasurementType::EdgeToEdge && m.anchors.size() >= 2)
		{
			QVector3D start1, end1, start2, end2;
			float len1 = 0.0f, len2 = 0.0f;
			if (resolveMeasurementEdgeGeometry(m.anchors[0], start1, end1, len1)
				&& resolveMeasurementEdgeGeometry(m.anchors[1], start2, end2, len2))
			{
				// Both edges, highlighted as reference context (not
				// draggable themselves - only the resulting dimension is).
				// Traced via their true tessellated paths so a curved or
				// filleted edge isn't shown as a straight chord.
				addEdgeTrace(m.anchors[0], color);
				addEdgeTrace(m.anchors[1], color);
				addMarker(start1, color, sizeMultiplier);
				addMarker(end1, color, sizeMultiplier);
				addMarker(start2, color, sizeMultiplier);
				addMarker(end2, color, sizeMultiplier);

				const QVector3D d1 = end1 - start1;
				const QVector3D d2 = end2 - start2;
				const MeasurementGeometry::EdgeToEdgeResult result =
					MeasurementGeometry::compareLines(start1, d1, start2, d2);

				if (result.isParallel)
				{
					// Dimension line from a point on edge1 to its
					// projection onto edge2's infinite line - offset +
					// extension lines + arrowheads via addOffsetDimension().
					const QVector3D projected = MeasurementGeometry::closestPointOnLine(start1, start2, d2);
					const QVector3D labelPos = addOffsetDimension(start1, projected, dimensionColor, m);
					labels.append({ labelPos, summary });
				}
				else
				{
					// Angular dimension - same visual language as Face to
					// Face's angle case (floating vertex + legs + arc +
					// tangent arrowheads), now drag-adjustable the same way
					// too. Vertex/basis/angle/radius all come from
					// resolveMeasurementAngleGeometry() - the same query
					// hitTestDimensionLine()/the drag interaction use.
					QVector3D vertex, u, v;
					float angleRad = 0.0f;
					float arcRadius = 0.0f;
					if (resolveMeasurementAngleGeometry(m, camera, vertex, u, v, angleRad, arcRadius))
					{
						const QVector3D labelPos = addAngleArc(vertex, u, v, angleRad, arcRadius, dimensionColor);
						labels.append({ labelPos, summary });
					}
				}
			}
		}
		else if (m.type == MeasurementType::EdgeToFace && m.anchors.size() >= 2)
		{
			QVector3D edgeStart, edgeEnd;
			float edgeLength = 0.0f;
			if (resolveMeasurementEdgeGeometry(m.anchors[0], edgeStart, edgeEnd, edgeLength))
			{
				QVector3D facePos, faceNormal;
				if (resolveMeasurementAnchorPlane(m.anchors[1], facePos, faceNormal))
				{
					const float normalLen = markerSize * 3.0f;

					// Both the edge and the face, highlighted as reference
					// context. The edge is traced via its true tessellated
					// path so a curved or filleted edge isn't shown as a
					// straight chord.
					addEdgeTrace(m.anchors[0], color);
					addMarker(edgeStart, color, sizeMultiplier);
					addMarker(edgeEnd, color, sizeMultiplier);
					addMarker(facePos, color, sizeMultiplier);
					addSegment(facePos, facePos + faceNormal.normalized() * normalLen, color);

					const QVector3D edgeDir = edgeEnd - edgeStart;
					const MeasurementGeometry::EdgeToFaceResult result =
						MeasurementGeometry::compareEdgeToFace(edgeStart, edgeDir, facePos, faceNormal);

					if (result.isParallel)
					{
						// Dimension line from the edge straight down to its
						// projection onto the face's plane, offset +
						// extension lines + arrowheads via addOffsetDimension().
						const QVector3D nN = faceNormal.normalized();
						const QVector3D projected = edgeStart - nN * QVector3D::dotProduct(edgeStart - facePos, nN);
						const QVector3D labelPos = addOffsetDimension(edgeStart, projected, dimensionColor, m);
						labels.append({ labelPos, summary });
					}
					else
					{
						// Angular dimension: grounded at the edge's own
						// start point (a real point, unlike Face-to-Face/
						// Edge-to-Edge's "floating midpoint") - one leg
						// along the edge itself, the other along the
						// edge's own projection onto the face's plane,
						// sweeping the angle between them. Vertex/basis/
						// angle/radius all come from
						// resolveMeasurementAngleGeometry() - the same
						// query hitTestDimensionLine()/the drag
						// interaction use.
						QVector3D vertex, u, v;
						float angleRad = 0.0f;
						float arcRadius = 0.0f;
						if (resolveMeasurementAngleGeometry(m, camera, vertex, u, v, angleRad, arcRadius))
						{
							const QVector3D labelPos = addAngleArc(vertex, u, v, angleRad, arcRadius, dimensionColor);
							labels.append({ labelPos, summary });
						}
					}
				}
			}
		}
		else if (m.type == MeasurementType::PitchCircle && m.anchors.size() >= 3)
		{
			QVector<QVector3D> points;
			points.reserve(m.anchors.size());
			for (const MeasurementAnchorRef& a : m.anchors)
				points.append(resolveMeasurementAnchor(a));
			const MeasurementGeometry::PitchCircleResult result = MeasurementGeometry::fitPitchCircle(points);
			if (result.valid)
			{
				for (const QVector3D& p : points)
					addMarker(p, color, sizeMultiplier);
				addMarker(result.center, color, sizeMultiplier * 0.6f);
				addCircleOutline(result.center, result.normal, result.radius, color);
				labels.append({ result.center, summary });
			}
		}
		else if (m.type == MeasurementType::Concentricity && m.anchors.size() >= 2)
		{
			QVector3D center1, axis1, center2, axis2;
			float radius1 = 0.0f, radius2 = 0.0f;
			if (resolveMeasurementEdgeCircle(m.anchors[0], center1, axis1, radius1)
				&& resolveMeasurementEdgeCircle(m.anchors[1], center2, axis2, radius2))
			{
				addCircleOutline(center1, axis1, radius1, color);
				addCircleOutline(center2, axis2, radius2, color);
				addMarker(center1, color, sizeMultiplier * 0.6f);
				addMarker(center2, color, sizeMultiplier * 0.6f);
				// The connecting line between the two centers IS the
				// measured quantity (its length is the reported offset) -
				// dimensionColor/hover-highlighted like every other
				// measurement's actual result, not just reference context.
				addSegment(center1, center2, dimensionColor);
				labels.append({ (center1 + center2) * 0.5f, summary });
			}
		}
		else if (m.type == MeasurementType::AngleThreePoint && m.anchors.size() >= 3)
		{
			const QVector3D vertex = resolveMeasurementAnchor(m.anchors[0]);
			const QVector3D p1 = resolveMeasurementAnchor(m.anchors[1]);
			const QVector3D p2 = resolveMeasurementAnchor(m.anchors[2]);

			// The actual picked rays, as reference context - not the
			// dimension arc's own legs below, which are drawn at a fixed
			// CAD-style radius (see resolveMeasurementAngleGeometry()), not
			// the true pick-to-pick distance.
			addMarker(vertex, color, sizeMultiplier);
			addMarker(p1, color, sizeMultiplier);
			addMarker(p2, color, sizeMultiplier);
			addSegment(vertex, p1, color);
			addSegment(vertex, p2, color);

			QVector3D angleVertex, u, v;
			float angleRad = 0.0f, arcRadius = 0.0f;
			if (resolveMeasurementAngleGeometry(m, camera, angleVertex, u, v, angleRad, arcRadius))
			{
				const QVector3D labelPos = addAngleArc(angleVertex, u, v, angleRad, arcRadius, dimensionColor);
				labels.append({ labelPos, summary });
			}
		}
		else if (m.type == MeasurementType::EdgeChain && m.anchors.size() >= 2)
		{
			// No single "dimension line" to offset+drag the way EdgeLength's
			// one edge gets (see addOffsetDimension()) - a sum over N edges
			// has no one line to put it on, so each edge is just highlighted
			// directly, and the label sits at the centroid of all their
			// midpoints. Traces each edge's TRUE tessellated path (see
			// resolveMeasurementEdgePolyline()'s doc comment), not a
			// straight chord between its two ends - unlike EdgeLength's own
			// offset dimension line (deliberately straight, floating clear
			// of the part), this draws directly on/near the part, where a
			// chord across a curved or filleted edge reads as if the wrong
			// edge got picked even though the length is correct.
			QVector3D labelCentroid;
			int resolvedCount = 0;
			for (const MeasurementAnchorRef& a : m.anchors)
			{
				QVector<QVector3D> polyline;
				if (!resolveMeasurementEdgePolyline(a, polyline) || polyline.size() < 2)
					continue;

				for (int i = 0; i + 1 < polyline.size(); ++i)
					addSegment(polyline[i], polyline[i + 1], color);
				addMarker(polyline.first(), color, sizeMultiplier);
				addMarker(polyline.last(), color, sizeMultiplier);
				labelCentroid += (polyline.first() + polyline.last()) * 0.5f;
				++resolvedCount;
			}
			if (resolvedCount > 0)
			{
				labelCentroid /= static_cast<float>(resolvedCount);
				labels.append({ labelCentroid, summary });
			}
		}
		else if (m.type == MeasurementType::FaceArea && !m.anchors.isEmpty())
		{
			QVector<int> triangles;
			float area = 0.0f;
			QVector3D centroid;
			if (resolveMeasurementFaceArea(m.anchors[0], triangles, area, centroid))
			{
				// No single dimension line here either (same reasoning as
				// Chain Length above) - the region's own boundary outline
				// IS the measured quantity, so it's drawn directly rather
				// than as a separate reference-vs-dimension pair. The
				// boundary is a direct byproduct of the same adjacency
				// data resolveMeasurementFaceArea() used to flood-fill:
				// an included triangle's edge is on the boundary exactly
				// when its neighbor across that edge is NOT also included
				// (or doesn't exist - a genuine mesh boundary).
				if (SceneMesh* mesh = getMeshByUuid(m.anchors[0].meshUuid))
				{
					const std::vector<unsigned int> faceIndices = mesh->indices();
					const std::vector<float>& faceTrsfPoints = mesh->getTrsfPoints();
					auto faceVertexPos = [&faceTrsfPoints](unsigned int vIdx) -> QVector3D {
						const size_t p = static_cast<size_t>(vIdx) * 3;
						if (p + 2 >= faceTrsfPoints.size())
							return QVector3D();
						return QVector3D(faceTrsfPoints[p], faceTrsfPoints[p + 1], faceTrsfPoints[p + 2]);
					};
					const std::vector<std::array<int, 3>>& adjacency = mesh->getTriangleAdjacency();

					QSet<int> triangleSet;
					for (int t : triangles)
						triangleSet.insert(t);

					for (int t : triangles)
					{
						if (static_cast<size_t>(t) >= adjacency.size())
							continue;
						const size_t base = static_cast<size_t>(t) * 3;
						for (int e = 0; e < 3; ++e)
						{
							const int neighbor = adjacency[static_cast<size_t>(t)][e];
							if (neighbor >= 0 && triangleSet.contains(neighbor))
								continue;  // interior edge, not on the boundary
							const QVector3D pA = faceVertexPos(faceIndices[base + static_cast<size_t>(e)]);
							const QVector3D pB = faceVertexPos(faceIndices[base + static_cast<size_t>((e + 1) % 3)]);
							addSegment(pA, pB, color);
						}
					}
				}
				labels.append({ centroid, summary });
			}
		}
		else if (m.type == MeasurementType::MinDistance && m.anchors.size() >= 2)
		{
			QVector3D pointA, pointB;
			float distance = 0.0f;
			if (resolveMeasurementMinDistance(m, pointA, pointB, distance))
			{
				// Both regions' boundary outlines, highlighted as reference
				// context (same technique as FaceArea above - a direct
				// byproduct of the same adjacency data
				// resolveMeasurementFaceRegion() flood-filled with), plus
				// the standard offset dimension line for the distance
				// itself (same convention every other linear dimension in
				// this file uses - see addOffsetDimension()).
				auto drawRegionOutline = [&](const MeasurementAnchorRef& ref) {
					QVector<int> triangles;
					SceneMesh* mesh = getMeshByUuid(ref.meshUuid);
					if (!mesh || !resolveMeasurementFaceRegion(ref, triangles))
						return;
					const std::vector<unsigned int> faceIndices = mesh->indices();
					const std::vector<float>& faceTrsfPoints = mesh->getTrsfPoints();
					auto faceVertexPos = [&faceTrsfPoints](unsigned int vIdx) -> QVector3D {
						const size_t p = static_cast<size_t>(vIdx) * 3;
						if (p + 2 >= faceTrsfPoints.size())
							return QVector3D();
						return QVector3D(faceTrsfPoints[p], faceTrsfPoints[p + 1], faceTrsfPoints[p + 2]);
					};
					const std::vector<std::array<int, 3>>& adjacency = mesh->getTriangleAdjacency();

					QSet<int> triangleSet;
					for (int t : triangles)
						triangleSet.insert(t);

					for (int t : triangles)
					{
						if (static_cast<size_t>(t) >= adjacency.size())
							continue;
						const size_t base = static_cast<size_t>(t) * 3;
						for (int e = 0; e < 3; ++e)
						{
							const int neighbor = adjacency[static_cast<size_t>(t)][e];
							if (neighbor >= 0 && triangleSet.contains(neighbor))
								continue;  // interior edge, not on the boundary
							const QVector3D pA = faceVertexPos(faceIndices[base + static_cast<size_t>(e)]);
							const QVector3D pB = faceVertexPos(faceIndices[base + static_cast<size_t>((e + 1) % 3)]);
							addSegment(pA, pB, color);
						}
					}
				};
				drawRegionOutline(m.anchors[0]);
				drawRegionOutline(m.anchors[1]);

				const QVector3D labelPos = addOffsetDimension(pointA, pointB, dimensionColor, m);
				labels.append({ labelPos, summary });
			}
		}
		else if (m.type == MeasurementType::CylindricalDiameter && !m.anchors.isEmpty())
		{
			float diameter = 0.0f;
			QVector3D axisOrigin, axisDir, pickedPoint;
			bool isCone = false;
			if (resolveMeasurementCylindricalDiameter(m.anchors[0], diameter, axisOrigin, axisDir, pickedPoint, isCone))
			{
				// The full circular cross-section through the picked point
				// (MeasurementGeometry::circlePolyline(), same helper
				// PitchCircle/EdgeRadius already use) plus a diameter line
				// straight through the axis (mirrored point -> picked
				// point) via addDimensionLine() directly - unlike every
				// other linear dimension in this file, a diameter line
				// canonically passes THROUGH the part (real CAD
				// convention), so no offset/extension-line treatment.
				const QVector3D toPoint = pickedPoint - axisOrigin;
				const QVector3D center = axisOrigin + axisDir * QVector3D::dotProduct(toPoint, axisDir);
				const float radius = diameter * 0.5f;
				const QVector3D mirrored = center * 2.0f - pickedPoint;

				const QVector<QVector3D> circle = MeasurementGeometry::circlePolyline(center, axisDir, radius);
				for (int i = 0; i < circle.size(); ++i)
					addSegment(circle[i], circle[(i + 1) % circle.size()], color);

				addMarker(pickedPoint, color, sizeMultiplier);
				addMarker(mirrored, color, sizeMultiplier);
				addDimensionLine(mirrored, pickedPoint, dimensionColor);

				labels.append({ pickedPoint, summary });
			}
		}
	}

	// In-progress measurement: N of the required anchors already picked,
	// waiting on the next click. Works uniformly for every tool - a plain
	// marker (or, for an edge pick, the edge's own highlighted chord) per
	// pick already made, a straight preview line connecting consecutive
	// picks (useful feedback even for a 3rd arc point that hasn't landed
	// yet), and a prompt for what to click next.
	if (!_pendingMeasurementAnchors.isEmpty())
	{
		// Mirrors handleMeasurementClick()'s own tool/anchor-index dispatch
		// for which picks came from pickStraightEdgeAnchor() rather than
		// pickSurfaceAnchor()/pickCircularEdgeCenterAnchor() - an edge
		// anchor has no single "point" to resolve (resolveMeasurementAnchor()
		// would only find one for a CIRCULAR edge, via its center; a
		// straight edge anchor has neither triangleIndex/snappedVertexIndex
		// nor a circle to resolve, so it would silently render at the
		// origin instead of showing the edge that was actually picked).
		auto isEdgeChordAnchor = [](MeasurementTool tool, int anchorIndex) -> bool {
			switch (tool)
			{
			case MeasurementTool::EdgeLength:
			case MeasurementTool::EdgeToEdge:
			case MeasurementTool::EdgeChain:
				return true;  // every anchor is an edge
			case MeasurementTool::EdgeToVertex:
			case MeasurementTool::EdgeToFace:
				return anchorIndex == 0;  // only the first anchor is an edge; the second is a point/face
			default:
				return false;
			}
		};

		QVector3D lastPicked;
		for (int i = 0; i < _pendingMeasurementAnchors.size(); ++i)
		{
			const MeasurementAnchorRef& pendingRef = _pendingMeasurementAnchors[i];
			QVector3D p;
			if (isEdgeChordAnchor(_measurementTool, i))
			{
				// True tessellated path, not just the chord (see
				// resolveMeasurementEdgePolyline()'s doc comment) - a
				// curved or filleted edge should preview as itself while
				// still being picked, same reasoning as Chain Length's
				// completed-measurement rendering below.
				QVector<QVector3D> polyline;
				if (resolveMeasurementEdgePolyline(pendingRef, polyline) && polyline.size() >= 2)
				{
					for (int seg = 0; seg + 1 < polyline.size(); ++seg)
						addSegment(polyline[seg], polyline[seg + 1], pendingColor);
					addMarker(polyline.first(), pendingColor);
					addMarker(polyline.last(), pendingColor);
					p = (polyline.first() + polyline.last()) * 0.5f;
				}
			}
			else
			{
				p = resolveMeasurementAnchor(pendingRef);
				addMarker(p, pendingColor);
			}

			if (i > 0)
				addSegment(lastPicked, p, pendingColor);
			lastPicked = p;
		}
		labels.append({ lastPicked, measurementToolPickPrompt(_measurementTool, _pendingMeasurementAnchors.size()) });
	}

	// Live hover preview: the exact point a click would place right now,
	// including vertex snap (see mouseMoveEvent()'s _measurementHoverAnchor
	// update). Distinct color and a larger cross when it WILL snap, so the
	// snap itself reads unambiguously rather than looking like just another
	// raw surface point.
	if (_measurementHoverAnchor.isValid())
	{
		const bool snapped = _measurementHoverAnchor.snappedVertexIndex >= 0;
		const QVector3D hp = _measurementHoverAnchor.worldPosition;
		const QVector3D hoverColor = snapped ? hoverSnapColor : hoverRawColor;
		const float hoverMarkerSize = markerSize * (snapped ? 1.6f : 1.0f);
		addSegment(hp - QVector3D(hoverMarkerSize, 0, 0), hp + QVector3D(hoverMarkerSize, 0, 0), hoverColor);
		addSegment(hp - QVector3D(0, hoverMarkerSize, 0), hp + QVector3D(0, hoverMarkerSize, 0), hoverColor);
		addSegment(hp - QVector3D(0, 0, hoverMarkerSize), hp + QVector3D(0, 0, hoverMarkerSize), hoverColor);
	}

	// Live hover preview for edge-based picks and circular-edge-center
	// point picks alike (see mouseMoveEvent()'s _measurementEdgeHoverAnchor/
	// _measurementEdgeHoverIsCenterPick update). Edge Radius and
	// Concentricity (both circular-edge picks) preview the resolved circle;
	// any POINT pick that's snapping to a circular edge's center (Point,
	// Distance, both arc tools, Point-to-Face's point anchor, Edge-to-
	// Vertex's vertex anchor) previews just the resolved center point;
	// every genuine edge-target tool (EdgeLength/EdgeToEdge/EdgeToVertex's
	// first anchor/EdgeToFace's first anchor) previews the edge's own
	// chord as a straight line instead.
	if (_measurementEdgeHoverAnchor.isValid())
	{
		MeasurementAnchorRef hoverRef;
		hoverRef.meshUuid  = _measurementEdgeHoverAnchor.meshUuid;
		hoverRef.edgeIndex = _measurementEdgeHoverAnchor.edgeIndex;

		if (_measurementTool == MeasurementTool::EdgeRadius || _measurementTool == MeasurementTool::Concentricity)
		{
			QVector3D center, axis;
			float radius = 0.0f;
			if (resolveMeasurementEdgeCircle(hoverRef, center, axis, radius))
			{
				addCircleOutline(center, axis, radius, hoverSnapColor);
				const float s = markerSize * 1.6f;
				addSegment(center - QVector3D(s, 0, 0), center + QVector3D(s, 0, 0), hoverSnapColor);
				addSegment(center - QVector3D(0, s, 0), center + QVector3D(0, s, 0), hoverSnapColor);
				addSegment(center - QVector3D(0, 0, s), center + QVector3D(0, 0, s), hoverSnapColor);
			}
		}
		else if (_measurementEdgeHoverIsCenterPick)
		{
			QVector3D center, axis;
			float radius = 0.0f;
			if (resolveMeasurementEdgeCircle(hoverRef, center, axis, radius))
			{
				const float s = markerSize * 1.6f;
				addSegment(center - QVector3D(s, 0, 0), center + QVector3D(s, 0, 0), hoverSnapColor);
				addSegment(center - QVector3D(0, s, 0), center + QVector3D(0, s, 0), hoverSnapColor);
				addSegment(center - QVector3D(0, 0, s), center + QVector3D(0, 0, s), hoverSnapColor);
			}
		}
		else
		{
			// True tessellated path, not just the chord - same reasoning as
			// the pending-pick and completed-measurement previews (see
			// resolveMeasurementEdgePolyline()'s doc comment).
			QVector<QVector3D> polyline;
			if (resolveMeasurementEdgePolyline(hoverRef, polyline) && polyline.size() >= 2)
			{
				for (int i = 0; i + 1 < polyline.size(); ++i)
					addSegment(polyline[i], polyline[i + 1], hoverSnapColor);
			}
		}
	}

	// Dimension geometry (lines + arrowhead cones) must never be hidden
	// behind shaded model surfaces - the whole point of a CAD-style
	// dimension is that it stays legible regardless of what's in front of
	// it at that depth. Saved/restored (not just force-disabled) so this
	// doesn't leak into whatever renders after this function.
	const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
	glDisable(GL_DEPTH_TEST);

	if (!lineVertices.empty())
	{
		// Mirrors drawBoundingBoxOverlay()'s exact upload/draw pattern.
		_renderCtrl.initMeasurementOverlayGeometry(lineVertices);
		glBindVertexArray(_renderCtrl.measurementOverlayVAO());
		glBindBuffer(GL_ARRAY_BUFFER, _renderCtrl.measurementOverlayVBO());
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
	}

	if (!triangleVertices.empty())
	{
		// Dimension-line arrowhead cones - same shader/upload pattern as the
		// line pass above, separate buffer/draw call since these are solid
		// GL_TRIANGLES, not GL_LINES (see addCone()). Cull state is saved/
		// restored rather than assumed, matching this file's existing
		// convention elsewhere (e.g. drawSelectionOutline()) - addCone()'s
		// winding isn't guaranteed consistent from every possible viewing
		// direction, and the shader is fully unlit/flat-color regardless of
		// facing, so there's no correctness reason to cull either face here.
		const GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);
		glDisable(GL_CULL_FACE);

		_renderCtrl.initMeasurementConeGeometry(triangleVertices);
		glBindVertexArray(_renderCtrl.measurementConeVAO());
		glBindBuffer(GL_ARRAY_BUFFER, _renderCtrl.measurementConeVBO());
		glBufferData(GL_ARRAY_BUFFER,
		             static_cast<GLsizeiptr>(triangleVertices.size() * sizeof(float)),
		             triangleVertices.data(),
		             GL_DYNAMIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void*>(0));
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void*>(3 * sizeof(float)));

		_renderCtrl.axisShader()->bind();
		_renderCtrl.axisShader()->setUniformValue("modelViewMatrix", camera->getViewMatrix());
		_renderCtrl.axisShader()->setUniformValue("projectionMatrix", camera->getProjectionMatrix());
		_renderCtrl.axisShader()->setUniformValue("renderCone", false);
		glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(triangleVertices.size() / 6));
		_renderCtrl.axisShader()->release();

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);

		if (cullWasEnabled)
			glEnable(GL_CULL_FACE);
	}

	if (depthWasEnabled)
		glEnable(GL_DEPTH_TEST);

	if (axisTextRenderer)
	{
		const QRect viewportRect(0, 0, viewportSize.width(), viewportSize.height());
		// RenderText() has no concept of a line break (see its doc comment
		// in TextRenderer.h) - a label containing '\n' (currently just
		// Pitch Circle's headline/detail summary) is split here and its
		// lines stacked upward from the anchor point, one fontSize()-tall
		// step apart, so the LAST line lands exactly where a single-line
		// label always has (VBOTTOM's usual anchor) and earlier lines sit
		// above it - single-line labels render identically to before
		// (the loop below just runs once, at zero offset).
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
				axisTextRenderer->RenderText(textLines[i].toStdString(),
					projected.x(), y, 1,
					QVector3D(1.0f, 1.0f, 1.0f), TextRenderer::VAlignment::VBOTTOM);
			}
		}
	}
}

