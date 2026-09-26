#include "SimulationStreamlineController.h"

#include "Camera.h"
#include "RenderableMesh.h"
#include "SceneRenderController.h"

#include <QMatrix4x4>
#include <QOpenGLShaderProgram>
#include <QVector3D>

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
	std::vector<float> data; // 6 floats per vertex: position, colour; two vertices per segment
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
	}
	const GLsizei vertices = static_cast<GLsizei>(data.size() / 6);
	if (vertices < 2)
		return;

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
	glDrawArrays(GL_LINES, 0, vertices);
	glLineWidth(1.0f);
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
