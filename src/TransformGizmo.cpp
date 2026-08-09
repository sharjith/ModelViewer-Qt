#include "TransformGizmo.h"

#include "ConeRenderable.h"
#include "Camera.h"
#include "ShaderProgram.h"

#include <QOpenGLContext>
#include <QVector2D>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
constexpr float kAxisLength = 1.0f;
constexpr int kArcSegments = 32;
constexpr float kQuarterTurnRadians = 1.57079632679f;
constexpr float kAxisHitPixels = 10.0f;
constexpr float kArcHitPixels = 12.0f;
constexpr float kScaleHitPixels = 14.0f;
constexpr float kArrowBaseScale = 0.86f;
constexpr float kScaleCorner = 0.26f;

float distancePointToSegment(const QVector2D& p, const QVector2D& a, const QVector2D& b)
{
	const QVector2D ab = b - a;
	const float abLenSq = QVector2D::dotProduct(ab, ab);
	if (abLenSq <= 1.0e-6f)
		return (p - a).length();

	const float t = std::clamp(QVector2D::dotProduct(p - a, ab) / abLenSq, 0.0f, 1.0f);
	const QVector2D projection = a + (ab * t);
	return (p - projection).length();
}

QVector3D projectToScreen(const QVector3D& worldPoint,
                          const QMatrix4x4& viewMatrix,
                          const QMatrix4x4& projectionMatrix,
                          const QRect& viewport)
{
	return worldPoint.project(viewMatrix, projectionMatrix, viewport);
}

void appendArc(std::vector<float>& vertices,
               std::vector<float>& colors,
               const QVector3D& color,
               const QVector3D& axisU,
               const QVector3D& axisV,
               float radius,
               int& offsetOut,
               int& countOut)
{
	offsetOut = static_cast<int>(vertices.size() / 3);
	for (int i = 0; i <= kArcSegments; ++i)
	{
		const float angle = (static_cast<float>(i) / static_cast<float>(kArcSegments)) * kQuarterTurnRadians;
		const QVector3D point = (std::cos(angle) * radius * axisU) + (std::sin(angle) * radius * axisV);
		vertices.push_back(point.x());
		vertices.push_back(point.y());
		vertices.push_back(point.z());

		colors.push_back(color.x());
		colors.push_back(color.y());
		colors.push_back(color.z());
	}
	countOut = static_cast<int>(vertices.size() / 3) - offsetOut;
}
}

TransformGizmo::TransformGizmo(QObject* parent)
	: QObject(parent),
	  _axesVertexBuffer(QOpenGLBuffer::VertexBuffer),
	  _axesColorBuffer(QOpenGLBuffer::VertexBuffer),
	  _arcsVertexBuffer(QOpenGLBuffer::VertexBuffer),
	  _arcsColorBuffer(QOpenGLBuffer::VertexBuffer),
	  _scaleVertexBuffer(QOpenGLBuffer::VertexBuffer),
	  _scaleColorBuffer(QOpenGLBuffer::VertexBuffer)
{
}

TransformGizmo::~TransformGizmo()
{
	releaseGpuResources();
}

void TransformGizmo::clearInteraction()
{
	const Handle oldHover = _hoveredHandle;
	const Handle oldActive = _activeHandle;
	_hoveredHandle = Handle::None;
	_activeHandle = Handle::None;
	if (oldHover != _hoveredHandle)
		emit hoveredHandleChanged(_hoveredHandle);
	if (oldActive != _activeHandle)
		emit activeHandleChanged(_activeHandle);
}

void TransformGizmo::setVisible(bool visible)
{
	_visible = visible;
	if (!_visible)
		clearInteraction();
}

bool TransformGizmo::updateHover(const QPoint& pixel, const Camera* camera,
                                 const QMatrix4x4& viewMatrix, const QMatrix4x4& projectionMatrix,
                                 const QRect& viewport, float fallbackScale)
{
	const Handle hit = hitTest(pixel, camera, viewMatrix, projectionMatrix, viewport, fallbackScale);
	if (hit == _hoveredHandle)
		return hit != Handle::None;

	_hoveredHandle = hit;
	emit hoveredHandleChanged(_hoveredHandle);
	return hit != Handle::None;
}

bool TransformGizmo::activateHandleAt(const QPoint& pixel, const Camera* camera,
                                      const QMatrix4x4& viewMatrix, const QMatrix4x4& projectionMatrix,
                                      const QRect& viewport, float fallbackScale)
{
	const Handle hit = hitTest(pixel, camera, viewMatrix, projectionMatrix, viewport, fallbackScale);
	if (hit == Handle::None)
		return false;

	if (_activeHandle != hit)
	{
		_activeHandle = hit;
		emit activeHandleChanged(_activeHandle);
	}
	if (_hoveredHandle != hit)
	{
		_hoveredHandle = hit;
		emit hoveredHandleChanged(_hoveredHandle);
	}
	return true;
}

void TransformGizmo::render(ShaderProgram* axisShader, ConeRenderable* axisCone, const Camera* camera,
                            const QMatrix4x4& viewMatrix, const QMatrix4x4& projectionMatrix,
                            float fallbackScale)
{
	if (!_visible || !axisShader || !axisCone || !camera)
		return;

	restoreGpuResources();
	if (!_initialized)
		return;

	const float worldScale = computeWorldScale(camera, fallbackScale);
	if (worldScale <= 0.0f)
		return;

	GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
	glDisable(GL_DEPTH_TEST);

	drawAxes(axisShader, axisCone, viewMatrix, projectionMatrix, worldScale);
	drawScaleHandle(axisShader, viewMatrix, projectionMatrix, worldScale);
	drawArcs(axisShader, viewMatrix, projectionMatrix, worldScale);

	if (depthWasEnabled)
		glEnable(GL_DEPTH_TEST);
}

TransformGizmo::Handle TransformGizmo::hitTest(const QPoint& pixel, const Camera* camera,
                                               const QMatrix4x4& viewMatrix, const QMatrix4x4& projectionMatrix,
                                               const QRect& viewport, float fallbackScale) const
{
	if (!_visible || !camera || !viewport.contains(pixel))
		return Handle::None;

	const float worldScale = computeWorldScale(camera, fallbackScale);
	const QVector2D screenPoint(pixel.x(), viewport.height() - pixel.y());
	Handle bestHandle = Handle::None;
	float bestDistance = (std::numeric_limits<float>::max)();

	const QVector3D pivotScreen3 = projectToScreen(_pivot, viewMatrix, projectionMatrix, viewport);
	const QVector2D pivotScreen(pivotScreen3.x(), pivotScreen3.y());

	const struct AxisCandidate
	{
		Handle handle;
		QVector3D endpoint;
	} axisCandidates[] = {
		{ Handle::TranslateX, _pivot + QVector3D(worldScale * kAxisLength, 0.0f, 0.0f) },
		{ Handle::TranslateY, _pivot + QVector3D(0.0f, worldScale * kAxisLength, 0.0f) },
		{ Handle::TranslateZ, _pivot + QVector3D(0.0f, 0.0f, worldScale * kAxisLength) }
	};

	for (const AxisCandidate& axis : axisCandidates)
	{
		const QVector3D endScreen3 = projectToScreen(axis.endpoint, viewMatrix, projectionMatrix, viewport);
		const QVector2D endScreen(endScreen3.x(), endScreen3.y());
		const QVector2D arrowBase = pivotScreen + ((endScreen - pivotScreen) * kArrowBaseScale);
		const float shaftDistance = distancePointToSegment(screenPoint, pivotScreen, arrowBase);
		const float arrowDistance = distancePointToSegment(screenPoint, arrowBase, endScreen);
		const float tipDistance = (screenPoint - endScreen).length();
		const float distance = (std::min)(shaftDistance, (std::min)(arrowDistance, tipDistance));
		if (distance <= kAxisHitPixels && distance < bestDistance)
		{
			bestDistance = distance;
			bestHandle = axis.handle;
		}
	}

	const struct ArcCandidate
	{
		Handle handle;
		QVector3D axisU;
		QVector3D axisV;
	} arcCandidates[] = {
		{ Handle::RotateXY, QVector3D(1.0f, 0.0f, 0.0f), QVector3D(0.0f, 1.0f, 0.0f) },
		{ Handle::RotateYZ, QVector3D(0.0f, 1.0f, 0.0f), QVector3D(0.0f, 0.0f, 1.0f) },
		{ Handle::RotateZX, QVector3D(0.0f, 0.0f, 1.0f), QVector3D(1.0f, 0.0f, 0.0f) }
	};

	for (const ArcCandidate& arc : arcCandidates)
	{
		QVector2D previousPoint;
		bool hasPreviousPoint = false;
		for (int i = 0; i <= kArcSegments; ++i)
		{
			const float angle = (static_cast<float>(i) / static_cast<float>(kArcSegments)) * kQuarterTurnRadians;
			const QVector3D worldPoint = _pivot +
				((std::cos(angle) * 0.72f * worldScale) * arc.axisU) +
				((std::sin(angle) * 0.72f * worldScale) * arc.axisV);
			const QVector3D screen3 = projectToScreen(worldPoint, viewMatrix, projectionMatrix, viewport);
			const QVector2D currentPoint(screen3.x(), screen3.y());

			if (hasPreviousPoint)
			{
				const float distance = distancePointToSegment(screenPoint, previousPoint, currentPoint);
				if (distance <= kArcHitPixels && distance < bestDistance)
				{
					bestDistance = distance;
					bestHandle = arc.handle;
				}
			}

			previousPoint = currentPoint;
			hasPreviousPoint = true;
		}
	}

	const QVector3D scalePointsWorld[] = {
		_pivot + QVector3D(kScaleCorner * worldScale, 0.0f, 0.0f),
		_pivot + QVector3D(kScaleCorner * worldScale, kScaleCorner * worldScale, 0.0f),
		_pivot + QVector3D(0.0f, kScaleCorner * worldScale, 0.0f),

		_pivot + QVector3D(0.0f, kScaleCorner * worldScale, 0.0f),
		_pivot + QVector3D(0.0f, kScaleCorner * worldScale, kScaleCorner * worldScale),
		_pivot + QVector3D(0.0f, 0.0f, kScaleCorner * worldScale),

		_pivot + QVector3D(kScaleCorner * worldScale, 0.0f, 0.0f),
		_pivot + QVector3D(kScaleCorner * worldScale, 0.0f, kScaleCorner * worldScale),
		_pivot + QVector3D(0.0f, 0.0f, kScaleCorner * worldScale),

		_pivot + QVector3D(kScaleCorner * worldScale, kScaleCorner * worldScale, 0.0f),
		_pivot + QVector3D(kScaleCorner * worldScale, kScaleCorner * worldScale, kScaleCorner * worldScale),
		_pivot + QVector3D(0.0f, kScaleCorner * worldScale, kScaleCorner * worldScale),

		_pivot + QVector3D(kScaleCorner * worldScale, 0.0f, kScaleCorner * worldScale),
		_pivot + QVector3D(kScaleCorner * worldScale, kScaleCorner * worldScale, kScaleCorner * worldScale),

		_pivot + QVector3D(kScaleCorner * worldScale, kScaleCorner * worldScale, 0.0f),
		_pivot + QVector3D(kScaleCorner * worldScale, kScaleCorner * worldScale, kScaleCorner * worldScale)
	};

	const int scaleSegmentIndices[][2] = {
		{0, 1}, {1, 2},
		{3, 4}, {4, 5},
		{6, 7}, {7, 8},
		{9, 10}, {11, 10}, {12, 13}, {14, 15}
	};

	for (const auto& segment : scaleSegmentIndices)
	{
		const QVector3D a3 = projectToScreen(scalePointsWorld[segment[0]], viewMatrix, projectionMatrix, viewport);
		const QVector3D b3 = projectToScreen(scalePointsWorld[segment[1]], viewMatrix, projectionMatrix, viewport);
		const QVector2D a(a3.x(), a3.y());
		const QVector2D b(b3.x(), b3.y());
		const float distance = distancePointToSegment(screenPoint, a, b);
		if (distance <= kScaleHitPixels && distance < bestDistance)
		{
			bestDistance = distance;
			bestHandle = Handle::UniformScale;
		}
	}

	return bestHandle;
}

QColor TransformGizmo::resolveHandleColor(Handle handle, const QColor& baseColor) const
{
	if (handle == _activeHandle)
		return QColor::fromRgbF(1.0f, 1.0f, 1.0f, 1.0f);

	if (handle == _hoveredHandle)
		return baseColor.lighter(160);

	return baseColor;
}

void TransformGizmo::restoreGpuResources()
{
	if (_initialized)
		return;

	QOpenGLContext* context = QOpenGLContext::currentContext();
	if (!context)
		return;

	initializeOpenGLFunctions();

	std::vector<float> axisVertices = {
		0.0f, 0.0f, 0.0f,  kAxisLength, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f,  0.0f, kAxisLength, 0.0f,
		0.0f, 0.0f, 0.0f,  0.0f, 0.0f, kAxisLength
	};
	std::vector<float> axisColors = {
		1.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f,  0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f
	};

	_axesVao.create();
	_axesVao.bind();

	_axesVertexBuffer.create();
	_axesVertexBuffer.bind();
	_axesVertexBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
	_axesVertexBuffer.allocate(axisVertices.data(), static_cast<int>(axisVertices.size() * sizeof(float)));

	_axesColorBuffer.create();
	_axesColorBuffer.bind();
	_axesColorBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
	_axesColorBuffer.allocate(axisColors.data(), static_cast<int>(axisColors.size() * sizeof(float)));

	_axesVao.release();

	std::vector<float> arcVertices;
	std::vector<float> arcColors;
	appendArc(arcVertices, arcColors, QVector3D(1.0f, 1.0f, 0.0f),
	          QVector3D(1.0f, 0.0f, 0.0f), QVector3D(0.0f, 1.0f, 0.0f),
	          0.72f, _xyArcOffset, _xyArcCount);
	appendArc(arcVertices, arcColors, QVector3D(0.0f, 1.0f, 1.0f),
	          QVector3D(0.0f, 1.0f, 0.0f), QVector3D(0.0f, 0.0f, 1.0f),
	          0.72f, _yzArcOffset, _yzArcCount);
	appendArc(arcVertices, arcColors, QVector3D(1.0f, 0.0f, 1.0f),
	          QVector3D(0.0f, 0.0f, 1.0f), QVector3D(1.0f, 0.0f, 0.0f),
	          0.72f, _zxArcOffset, _zxArcCount);
	_arcVertexCount = static_cast<int>(arcVertices.size() / 3);

	_arcsVao.create();
	_arcsVao.bind();

	_arcsVertexBuffer.create();
	_arcsVertexBuffer.bind();
	_arcsVertexBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
	_arcsVertexBuffer.allocate(arcVertices.data(), static_cast<int>(arcVertices.size() * sizeof(float)));

	_arcsColorBuffer.create();
	_arcsColorBuffer.bind();
	_arcsColorBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
	_arcsColorBuffer.allocate(arcColors.data(), static_cast<int>(arcColors.size() * sizeof(float)));

	_arcsVao.release();

	std::vector<float> scaleVertices = {
		kScaleCorner, 0.0f, 0.0f,   kScaleCorner, kScaleCorner, 0.0f,
		kScaleCorner, kScaleCorner, 0.0f,   0.0f, kScaleCorner, 0.0f,

		0.0f, kScaleCorner, 0.0f,   0.0f, kScaleCorner, kScaleCorner,
		0.0f, kScaleCorner, kScaleCorner,   0.0f, 0.0f, kScaleCorner,

		kScaleCorner, 0.0f, 0.0f,   kScaleCorner, 0.0f, kScaleCorner,
		kScaleCorner, 0.0f, kScaleCorner,   0.0f, 0.0f, kScaleCorner,

		kScaleCorner, kScaleCorner, 0.0f,   kScaleCorner, kScaleCorner, kScaleCorner,
		0.0f, kScaleCorner, kScaleCorner,   kScaleCorner, kScaleCorner, kScaleCorner,
		kScaleCorner, 0.0f, kScaleCorner,   kScaleCorner, kScaleCorner, kScaleCorner
	};
	const QVector3D xyColor(1.0f, 1.0f, 0.0f);
	const QVector3D yzColor(0.0f, 1.0f, 1.0f);
	const QVector3D zxColor(1.0f, 0.0f, 1.0f);
	const QVector3D white(1.0f, 1.0f, 1.0f);
	std::vector<float> scaleColors;
	scaleColors.reserve(scaleVertices.size());
	const QVector3D vertexColors[] = {
		xyColor, xyColor, xyColor, xyColor,
		yzColor, yzColor, yzColor, yzColor,
		zxColor, zxColor, zxColor, zxColor,
		xyColor, white,
		yzColor, white,
		zxColor, white
	};
	for (const QVector3D& color : vertexColors)
	{
		scaleColors.push_back(color.x());
		scaleColors.push_back(color.y());
		scaleColors.push_back(color.z());
	}
	_scaleVertexCount = static_cast<int>(scaleVertices.size() / 3);

	_scaleVao.create();
	_scaleVao.bind();

	_scaleVertexBuffer.create();
	_scaleVertexBuffer.bind();
	_scaleVertexBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
	_scaleVertexBuffer.allocate(scaleVertices.data(), static_cast<int>(scaleVertices.size() * sizeof(float)));

	_scaleColorBuffer.create();
	_scaleColorBuffer.bind();
	_scaleColorBuffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);
	_scaleColorBuffer.allocate(scaleColors.data(), static_cast<int>(scaleColors.size() * sizeof(float)));

	_scaleVao.release();

	_initialized = true;
}

float TransformGizmo::computeWorldScale(const Camera* camera, float fallbackScale) const
{
	if (!camera)
		return fallbackScale;

	if (camera->getProjectionType() == Camera::ProjectionType::ORTHOGRAPHIC)
		return (std::max)(camera->getViewRange() * 0.18f, fallbackScale);

	const float distance = (camera->getRenderPosition() - _pivot).length();
	return (std::max)(distance * 0.16f, fallbackScale);
}

void TransformGizmo::drawAxes(ShaderProgram* axisShader, ConeRenderable* axisCone,
                              const QMatrix4x4& viewMatrix, const QMatrix4x4& projectionMatrix,
                              float worldScale)
{
	QMatrix4x4 model;
	model.translate(_pivot);
	model.scale(worldScale);

	axisShader->bind();
	axisShader->setUniformValue("projectionMatrix", projectionMatrix);
	axisShader->setUniformValue("renderCone", false);
	axisShader->setUniformValue("modelViewMatrix", viewMatrix * model);

	_axesVao.bind();

	_axesVertexBuffer.bind();
	axisShader->enableAttributeArray("vertexPosition");
	axisShader->setAttributeBuffer("vertexPosition", GL_FLOAT, 0, 3);

	_axesColorBuffer.bind();
	{
		const QColor xColor = resolveHandleColor(Handle::TranslateX, QColor::fromRgbF(1.0f, 0.0f, 0.0f));
		const QColor yColor = resolveHandleColor(Handle::TranslateY, QColor::fromRgbF(0.0f, 1.0f, 0.0f));
		const QColor zColor = resolveHandleColor(Handle::TranslateZ, QColor::fromRgbF(0.0f, 0.0f, 1.0f));
		const std::vector<float> axisColors = {
			xColor.redF(), xColor.greenF(), xColor.blueF(), xColor.redF(), xColor.greenF(), xColor.blueF(),
			yColor.redF(), yColor.greenF(), yColor.blueF(), yColor.redF(), yColor.greenF(), yColor.blueF(),
			zColor.redF(), zColor.greenF(), zColor.blueF(), zColor.redF(), zColor.greenF(), zColor.blueF()
		};
		_axesColorBuffer.write(0, axisColors.data(), static_cast<int>(axisColors.size() * sizeof(float)));
	}
	axisShader->enableAttributeArray("vertexColor");
	axisShader->setAttributeBuffer("vertexColor", GL_FLOAT, 0, 3);

	const bool axisHovered = (_hoveredHandle == Handle::TranslateX || _hoveredHandle == Handle::TranslateY ||
		_hoveredHandle == Handle::TranslateZ || _activeHandle == Handle::TranslateX ||
		_activeHandle == Handle::TranslateY || _activeHandle == Handle::TranslateZ);
	glLineWidth(axisHovered ? 5.0f : 3.0f);
	glDrawArrays(GL_LINES, 0, 6);
	glLineWidth(1.0f);

	_axesVao.release();

	axisCone->setParameters(worldScale * 0.06f, worldScale * 0.22f, 8, 1);

	QMatrix4x4 arrowModel;
	arrowModel.translate(_pivot + QVector3D(worldScale * kAxisLength, 0.0f, 0.0f));
	arrowModel.rotate(90.0f, QVector3D(0.0f, 1.0f, 0.0f));
	axisShader->setUniformValue("renderCone", true);
	{
		const QColor color = resolveHandleColor(Handle::TranslateX, QColor::fromRgbF(1.0f, 0.0f, 0.0f));
		axisShader->setUniformValue("coneColor", QVector3D(color.redF(), color.greenF(), color.blueF()));
	}
	axisShader->setUniformValue("modelViewMatrix", viewMatrix * arrowModel);
	axisCone->getVAO().bind();
	glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(axisCone->getPoints().size()), GL_UNSIGNED_INT, 0);
	axisCone->getVAO().release();

	arrowModel.setToIdentity();
	arrowModel.translate(_pivot + QVector3D(0.0f, worldScale * kAxisLength, 0.0f));
	arrowModel.rotate(90.0f, QVector3D(-1.0f, 0.0f, 0.0f));
	{
		const QColor color = resolveHandleColor(Handle::TranslateY, QColor::fromRgbF(0.0f, 1.0f, 0.0f));
		axisShader->setUniformValue("coneColor", QVector3D(color.redF(), color.greenF(), color.blueF()));
	}
	axisShader->setUniformValue("modelViewMatrix", viewMatrix * arrowModel);
	axisCone->getVAO().bind();
	glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(axisCone->getPoints().size()), GL_UNSIGNED_INT, 0);
	axisCone->getVAO().release();

	arrowModel.setToIdentity();
	arrowModel.translate(_pivot + QVector3D(0.0f, 0.0f, worldScale * kAxisLength));
	{
		const QColor color = resolveHandleColor(Handle::TranslateZ, QColor::fromRgbF(0.0f, 0.0f, 1.0f));
		axisShader->setUniformValue("coneColor", QVector3D(color.redF(), color.greenF(), color.blueF()));
	}
	axisShader->setUniformValue("modelViewMatrix", viewMatrix * arrowModel);
	axisCone->getVAO().bind();
	glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(axisCone->getPoints().size()), GL_UNSIGNED_INT, 0);
	axisCone->getVAO().release();

	axisShader->release();
}

void TransformGizmo::drawArcs(ShaderProgram* axisShader, const QMatrix4x4& viewMatrix,
                              const QMatrix4x4& projectionMatrix, float worldScale)
{
	QMatrix4x4 model;
	model.translate(_pivot);
	model.scale(worldScale);

	axisShader->bind();
	axisShader->setUniformValue("projectionMatrix", projectionMatrix);
	axisShader->setUniformValue("renderCone", false);
	axisShader->setUniformValue("modelViewMatrix", viewMatrix * model);

	const struct ArcDraw
	{
		Handle handle;
		int offset;
		int count;
		QColor color;
	} arcDraws[] = {
		{ Handle::RotateXY, _xyArcOffset, _xyArcCount, QColor::fromRgbF(1.0f, 1.0f, 0.0f) },
		{ Handle::RotateYZ, _yzArcOffset, _yzArcCount, QColor::fromRgbF(0.0f, 1.0f, 1.0f) },
		{ Handle::RotateZX, _zxArcOffset, _zxArcCount, QColor::fromRgbF(1.0f, 0.0f, 1.0f) }
	};

	for (const ArcDraw& arc : arcDraws)
	{
		std::vector<float> arcColors;
		arcColors.reserve(static_cast<size_t>(arc.count) * 3);
		const QColor resolved = resolveHandleColor(arc.handle, arc.color);
		for (int i = 0; i < arc.count; ++i)
		{
			arcColors.push_back(resolved.redF());
			arcColors.push_back(resolved.greenF());
			arcColors.push_back(resolved.blueF());
		}

		_arcsVao.bind();
		_arcsVertexBuffer.bind();
		axisShader->enableAttributeArray("vertexPosition");
		axisShader->setAttributeBuffer("vertexPosition", GL_FLOAT, 0, 3);

		_arcsColorBuffer.bind();
		_arcsColorBuffer.write(static_cast<int>(arc.offset * 3 * sizeof(float)),
			arcColors.data(), static_cast<int>(arcColors.size() * sizeof(float)));
		axisShader->enableAttributeArray("vertexColor");
		axisShader->setAttributeBuffer("vertexColor", GL_FLOAT, 0, 3);

		glLineWidth((arc.handle == _hoveredHandle || arc.handle == _activeHandle) ? 5.0f : 3.0f);
		glDrawArrays(GL_LINE_STRIP, arc.offset, arc.count);
		_arcsVao.release();
	}
	glLineWidth(1.0f);
	axisShader->release();
}

void TransformGizmo::drawScaleHandle(ShaderProgram* axisShader, const QMatrix4x4& viewMatrix,
                                     const QMatrix4x4& projectionMatrix, float worldScale)
{
	QMatrix4x4 model;
	model.translate(_pivot);
	model.scale(worldScale);

	axisShader->bind();
	axisShader->setUniformValue("projectionMatrix", projectionMatrix);
	axisShader->setUniformValue("renderCone", false);
	axisShader->setUniformValue("modelViewMatrix", viewMatrix * model);

	const QColor xyResolved = resolveHandleColor(Handle::UniformScale, QColor::fromRgbF(1.0f, 1.0f, 0.0f));
	const QColor yzResolved = resolveHandleColor(Handle::UniformScale, QColor::fromRgbF(0.0f, 1.0f, 1.0f));
	const QColor zxResolved = resolveHandleColor(Handle::UniformScale, QColor::fromRgbF(1.0f, 0.0f, 1.0f));
	const QColor whiteResolved = resolveHandleColor(Handle::UniformScale, QColor::fromRgbF(1.0f, 1.0f, 1.0f));
	std::vector<float> scaleColors;
	scaleColors.reserve(static_cast<size_t>(_scaleVertexCount) * 3);
	const QColor vertexColors[] = {
		xyResolved, xyResolved, xyResolved, xyResolved,
		yzResolved, yzResolved, yzResolved, yzResolved,
		zxResolved, zxResolved, zxResolved, zxResolved,
		xyResolved, whiteResolved,
		yzResolved, whiteResolved,
		zxResolved, whiteResolved
	};
	for (const QColor& color : vertexColors)
	{
		scaleColors.push_back(color.redF());
		scaleColors.push_back(color.greenF());
		scaleColors.push_back(color.blueF());
	}

	_scaleVao.bind();
	_scaleVertexBuffer.bind();
	axisShader->enableAttributeArray("vertexPosition");
	axisShader->setAttributeBuffer("vertexPosition", GL_FLOAT, 0, 3);

	_scaleColorBuffer.bind();
	_scaleColorBuffer.write(0, scaleColors.data(), static_cast<int>(scaleColors.size() * sizeof(float)));
	axisShader->enableAttributeArray("vertexColor");
	axisShader->setAttributeBuffer("vertexColor", GL_FLOAT, 0, 3);

	glLineWidth((_hoveredHandle == Handle::UniformScale || _activeHandle == Handle::UniformScale) ? 5.0f : 3.0f);
	glDrawArrays(GL_LINES, 0, _scaleVertexCount);
	glLineWidth(1.0f);
	_scaleVao.release();
	axisShader->release();
}

void TransformGizmo::releaseGpuResources()
{
	if (_axesVertexBuffer.isCreated())
		_axesVertexBuffer.destroy();
	if (_axesColorBuffer.isCreated())
		_axesColorBuffer.destroy();
	if (_axesVao.isCreated())
		_axesVao.destroy();

	if (_arcsVertexBuffer.isCreated())
		_arcsVertexBuffer.destroy();
	if (_arcsColorBuffer.isCreated())
		_arcsColorBuffer.destroy();
	if (_arcsVao.isCreated())
		_arcsVao.destroy();
	if (_scaleVertexBuffer.isCreated())
		_scaleVertexBuffer.destroy();
	if (_scaleColorBuffer.isCreated())
		_scaleColorBuffer.destroy();
	if (_scaleVao.isCreated())
		_scaleVao.destroy();

	_initialized = false;
}
