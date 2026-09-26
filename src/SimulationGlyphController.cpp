#include "SimulationGlyphController.h"

#include "Camera.h"
#include "RenderableMesh.h"
#include "SceneRenderController.h"

#include <QMatrix4x4>
#include <QOpenGLShaderProgram>
#include <QVector3D>

#include <algorithm>
#include <cmath>

namespace
{
	constexpr int kHeadSegments = 8;
	constexpr float kHeadLength = 0.30f; // of the arrow length
	constexpr float kHeadRadius = 0.11f;

	void pushVertex(std::vector<float>& out, const QVector3D& p, const QVector3D& color)
	{
		out.insert(out.end(), { p.x(), p.y(), p.z(), color.x(), color.y(), color.z() });
	}

	// A headlight shade: a face turned toward the viewer keeps its colour, one seen edge-on darkens to 40 %.
	QVector3D shaded(const QVector3D& color, const QMatrix4x4& view, const QVector3D& normal)
	{
		QVector3D n = view.mapVector(normal);
		n.normalize();
		return color * (0.4f + 0.6f * std::fabs(n.z()));
	}
}

SimulationGlyphController::SimulationGlyphController(SceneRenderController& renderCtrl, QObject* parent)
	: QObject(parent)
	, _renderCtrl(renderCtrl)
{
}

void SimulationGlyphController::restoreGpuResources()
{
	initializeOpenGLFunctions();
	_glFunctionsInitialized = true;
}

void SimulationGlyphController::releaseGpuResources()
{
	// The buffer and its VAO are re-created on the next draw, so both are simply dropped with the dying context.
	if (_glFunctionsInitialized)
	{
		if (_vbo != 0)
			glDeleteBuffers(1, &_vbo);
		if (_vao != 0)
			glDeleteVertexArrays(1, &_vao);
	}
	_vbo = 0;
	_vao = 0;
	_glFunctionsInitialized = false;
}

void SimulationGlyphController::setGlyphs(const QUuid& meshUuid, GlyphSet glyphs)
{
	if (glyphs.count() == 0)
		_sets.erase(meshUuid);
	else
		_sets[meshUuid] = std::move(glyphs);
}

void SimulationGlyphController::clearGlyphs(const QUuid& meshUuid)
{
	_sets.erase(meshUuid);
}

void SimulationGlyphController::drawOverlay(Camera* camera, const MeshResolver& resolve)
{
	if (!_glFunctionsInitialized || !camera || _sets.empty() || !_renderCtrl.axisShader() || !resolve)
		return;

	const QMatrix4x4 view = camera->getViewMatrix();
	std::vector<float> lines, triangles; // 6 floats per vertex: position, colour
	const float twoPi = 6.28318530718f;

	for (const auto& entry : _sets)
	{
		const RenderableMesh* mesh = resolve(entry.first);
		if (!mesh)
			continue;
		const std::vector<float>& points = mesh->getTrsfPoints();
		const QMatrix4x4 frame = mesh->combinedRenderTransform();
		const GlyphSet& set = entry.second;
		for (std::size_t i = 0; i < set.count(); ++i)
		{
			QVector3D base;
			bool ok = true;
			for (std::size_t k = 0; k < 3 && ok; ++k)
			{
				const std::size_t p = static_cast<std::size_t>(set.anchors[i * 3 + k]) * 3;
				ok = p + 2 < points.size();
				if (ok)
					base += QVector3D(points[p], points[p + 1], points[p + 2]);
			}
			if (!ok)
				continue;
			base /= 3.0f;

			const QVector3D arrow = frame.mapVector(QVector3D(set.vectors[i * 3], set.vectors[i * 3 + 1], set.vectors[i * 3 + 2]));
			const float length = arrow.length();
			if (!(length > 0.0f) || !std::isfinite(length))
				continue;
			const QVector3D dir = arrow / length;
			const QVector3D color = i * 3 + 2 < set.colors.size()
				? QVector3D(set.colors[i * 3], set.colors[i * 3 + 1], set.colors[i * 3 + 2])
				: QVector3D(1.0f, 1.0f, 1.0f);
			const float headLength = kHeadLength * length, headRadius = kHeadRadius * length;
			const QVector3D neck = base + dir * (length - headLength);
			const QVector3D tip = base + arrow;

			pushVertex(lines, base, color);
			pushVertex(lines, neck, color);

			// Two directions perpendicular to the arrow, to build the rim of the cone.
			const QVector3D helper = std::fabs(dir.x()) < 0.9f ? QVector3D(1.0f, 0.0f, 0.0f) : QVector3D(0.0f, 1.0f, 0.0f);
			const QVector3D u = QVector3D::crossProduct(dir, helper).normalized();
			const QVector3D w = QVector3D::crossProduct(dir, u);
			const QVector3D capColor = shaded(color, view, -dir);
			for (int s = 0; s < kHeadSegments; ++s)
			{
				const float a0 = twoPi * static_cast<float>(s) / kHeadSegments;
				const float a1 = twoPi * static_cast<float>(s + 1) / kHeadSegments;
				const float am = 0.5f * (a0 + a1);
				const QVector3D b0 = neck + (u * std::cos(a0) + w * std::sin(a0)) * headRadius;
				const QVector3D b1 = neck + (u * std::cos(a1) + w * std::sin(a1)) * headRadius;
				const QVector3D sideNormal = (u * std::cos(am) + w * std::sin(am)) * headLength + dir * headRadius;
				const QVector3D sideColor = shaded(color, view, sideNormal);
				pushVertex(triangles, tip, sideColor);
				pushVertex(triangles, b0, sideColor);
				pushVertex(triangles, b1, sideColor);
				pushVertex(triangles, neck, capColor);
				pushVertex(triangles, b1, capColor);
				pushVertex(triangles, b0, capColor);
			}
		}
	}
	if (lines.empty())
		return;

	const GLsizei lineVertices = static_cast<GLsizei>(lines.size() / 6);
	const GLsizei triangleVertices = static_cast<GLsizei>(triangles.size() / 6);
	lines.insert(lines.end(), triangles.begin(), triangles.end());

	if (_vao == 0)
		glGenVertexArrays(1, &_vao);
	if (_vbo == 0)
		glGenBuffers(1, &_vbo);
	glBindVertexArray(_vao);
	glBindBuffer(GL_ARRAY_BUFFER, _vbo);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(lines.size() * sizeof(float)), lines.data(), GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void*>(0));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void*>(3 * sizeof(float)));

	// Both sides of the cone are drawn (its cap faces the other way): back-face culling is off for this pass only.
	const GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);
	glDisable(GL_CULL_FACE);

	_renderCtrl.axisShader()->bind();
	_renderCtrl.axisShader()->setUniformValue("modelViewMatrix", view);
	_renderCtrl.axisShader()->setUniformValue("projectionMatrix", camera->getProjectionMatrix());
	_renderCtrl.axisShader()->setUniformValue("renderCone", false);
	glLineWidth(2.0f);
	glDrawArrays(GL_LINES, 0, lineVertices);
	glLineWidth(1.0f);
	if (triangleVertices > 0)
		glDrawArrays(GL_TRIANGLES, lineVertices, triangleVertices);
	_renderCtrl.axisShader()->release();

	if (cullWasEnabled)
		glEnable(GL_CULL_FACE);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
}
