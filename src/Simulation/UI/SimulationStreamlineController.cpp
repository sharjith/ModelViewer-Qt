#include "SimulationStreamlineController.h"

#include "Camera.h"
#include "RenderableMesh.h"
#include "SceneRenderController.h"

#include <QMatrix4x4>
#include <QOpenGLShaderProgram>
#include <QVector3D>

#include <cmath>

namespace
{
	constexpr int kHeadSegments = 8;
	constexpr float kHeadRadius = 0.35f; // of the head length

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

SimulationStreamlineController::SimulationStreamlineController(SceneRenderController& renderCtrl, QObject* parent)
	: QObject(parent)
	, _renderCtrl(renderCtrl)
{
}

void SimulationStreamlineController::restoreGpuResources()
{
	initializeOpenGLFunctions();
	_glFunctionsInitialized = true;
}

void SimulationStreamlineController::releaseGpuResources()
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

void SimulationStreamlineController::setLines(const QUuid& meshUuid, StreamlineDisplay lines)
{
	if (lines.segmentCount() == 0)
		_sets.erase(meshUuid);
	else
		_sets[meshUuid] = std::move(lines);
}

void SimulationStreamlineController::clearLines(const QUuid& meshUuid)
{
	_sets.erase(meshUuid);
}

void SimulationStreamlineController::drawOverlay(Camera* camera, const MeshResolver& resolve)
{
	if (!_glFunctionsInitialized || !camera || _sets.empty() || !_renderCtrl.axisShader() || !resolve)
		return;

	const QMatrix4x4 view = camera->getViewMatrix();
	std::vector<float> data, cones; // 6 floats per vertex: position, colour; two vertices per segment / three per cone triangle
	const float twoPi = 6.28318530718f;
	for (const auto& entry : _sets)
	{
		const RenderableMesh* mesh = resolve(entry.first);
		if (!mesh)
			continue;
		const QMatrix4x4 frame = mesh->combinedRenderTransform();
		const StreamlineDisplay& lines = entry.second;
		const std::size_t vertexCount = lines.positions.size() / 3;
		if (lines.colors.size() < vertexCount * 3)
			continue;
		std::vector<QVector3D> world(vertexCount);
		for (std::size_t v = 0; v < vertexCount; ++v)
			world[v] = frame.map(QVector3D(lines.positions[v * 3], lines.positions[v * 3 + 1], lines.positions[v * 3 + 2]));
		data.reserve(data.size() + lines.segments.size() * 6);
		for (std::size_t s = 0; s + 1 < lines.segments.size(); s += 2)
		{
			const std::uint32_t ids[2] = { lines.segments[s], lines.segments[s + 1] };
			if (ids[0] >= vertexCount || ids[1] >= vertexCount)
				continue;
			for (std::uint32_t id : ids)
				data.insert(data.end(), { world[id].x(), world[id].y(), world[id].z(), lines.colors[id * 3], lines.colors[id * 3 + 1], lines.colors[id * 3 + 2] });
		}

		// The arrowheads: a cone centred on each position, pointing along the flow.
		for (std::size_t a = 0; a < lines.arrowCount() && a * 3 + 2 < lines.arrowDirections.size() && a * 3 + 2 < lines.arrowColors.size(); ++a)
		{
			const QVector3D centre = frame.map(QVector3D(lines.arrowPositions[a * 3], lines.arrowPositions[a * 3 + 1], lines.arrowPositions[a * 3 + 2]));
			const QVector3D vector = frame.mapVector(QVector3D(lines.arrowDirections[a * 3], lines.arrowDirections[a * 3 + 1], lines.arrowDirections[a * 3 + 2])) * lines.arrowLength;
			const float length = vector.length();
			if (!(length > 0.0f) || !std::isfinite(length))
				continue;
			const QVector3D dir = vector / length;
			const QVector3D color(lines.arrowColors[a * 3], lines.arrowColors[a * 3 + 1], lines.arrowColors[a * 3 + 2]);
			const QVector3D tip = centre + dir * (0.5f * length), base = centre - dir * (0.5f * length);
			const float radius = kHeadRadius * length;
			const QVector3D helper = std::fabs(dir.x()) < 0.9f ? QVector3D(1.0f, 0.0f, 0.0f) : QVector3D(0.0f, 1.0f, 0.0f);
			const QVector3D u = QVector3D::crossProduct(dir, helper).normalized();
			const QVector3D w = QVector3D::crossProduct(dir, u);
			const QVector3D capColor = shaded(color, view, -dir);
			for (int k = 0; k < kHeadSegments; ++k)
			{
				const float a0 = twoPi * static_cast<float>(k) / kHeadSegments, a1 = twoPi * static_cast<float>(k + 1) / kHeadSegments, am = 0.5f * (a0 + a1);
				const QVector3D b0 = base + (u * std::cos(a0) + w * std::sin(a0)) * radius;
				const QVector3D b1 = base + (u * std::cos(a1) + w * std::sin(a1)) * radius;
				const QVector3D sideColor = shaded(color, view, (u * std::cos(am) + w * std::sin(am)) * length + dir * radius);
				pushVertex(cones, tip, sideColor);
				pushVertex(cones, b0, sideColor);
				pushVertex(cones, b1, sideColor);
				pushVertex(cones, base, capColor);
				pushVertex(cones, b1, capColor);
				pushVertex(cones, b0, capColor);
			}
		}
	}
	const GLsizei lineVertices = static_cast<GLsizei>(data.size() / 6);
	const GLsizei coneVertices = static_cast<GLsizei>(cones.size() / 6);
	if (lineVertices < 2 && coneVertices == 0)
		return;
	data.insert(data.end(), cones.begin(), cones.end());

	if (_vao == 0)
		glGenVertexArrays(1, &_vao);
	if (_vbo == 0)
		glGenBuffers(1, &_vbo);
	glBindVertexArray(_vao);
	glBindBuffer(GL_ARRAY_BUFFER, _vbo);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(float)), data.data(), GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void*>(0));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void*>(3 * sizeof(float)));

	// The lines lie inside the solid and must be hidden by it wherever it is still there: draw against the model's depth whatever an earlier pass
	// (capping, hover / pick) left behind, and restore it afterwards.
	const GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE), depthWasEnabled = glIsEnabled(GL_DEPTH_TEST), stencilWasEnabled = glIsEnabled(GL_STENCIL_TEST),
	                blendWasEnabled = glIsEnabled(GL_BLEND);
	GLint depthFunc = GL_LESS;
	GLboolean depthMask = GL_TRUE;
	glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
	glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
	glDisable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_BLEND);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	_renderCtrl.axisShader()->bind();
	_renderCtrl.axisShader()->setUniformValue("modelViewMatrix", view);
	_renderCtrl.axisShader()->setUniformValue("projectionMatrix", camera->getProjectionMatrix());
	_renderCtrl.axisShader()->setUniformValue("renderCone", false);
	glLineWidth(2.0f);
	if (lineVertices >= 2)
		glDrawArrays(GL_LINES, 0, lineVertices);
	glLineWidth(1.0f);
	if (coneVertices > 0)
		glDrawArrays(GL_TRIANGLES, lineVertices, coneVertices);
	_renderCtrl.axisShader()->release();

	glDepthFunc(static_cast<GLenum>(depthFunc));
	glDepthMask(depthMask);
	if (!depthWasEnabled)
		glDisable(GL_DEPTH_TEST);
	if (stencilWasEnabled)
		glEnable(GL_STENCIL_TEST);
	if (blendWasEnabled)
		glEnable(GL_BLEND);
	if (cullWasEnabled)
		glEnable(GL_CULL_FACE);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
}
