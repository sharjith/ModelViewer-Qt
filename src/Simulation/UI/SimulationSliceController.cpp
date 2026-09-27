#include "SimulationSliceController.h"

#include "Camera.h"
#include "RenderableMesh.h"
#include "SceneRenderController.h"

#include <QMatrix4x4>
#include <QOpenGLShaderProgram>
#include <QVector3D>

#include <algorithm>
#include <cmath>

SimulationSliceController::SimulationSliceController(SceneRenderController& renderCtrl, QObject* parent)
	: QObject(parent)
	, _renderCtrl(renderCtrl)
{
}

void SimulationSliceController::restoreGpuResources()
{
	initializeOpenGLFunctions();
	_glFunctionsInitialized = true;
}

void SimulationSliceController::releaseGpuResources()
{
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

void SimulationSliceController::setSlices(const QUuid& meshUuid, std::vector<SliceDisplay> slices)
{
	if (slices.empty())
		_sets.erase(meshUuid);
	else
		_sets[meshUuid] = std::move(slices);
}

void SimulationSliceController::clearSlices(const QUuid& meshUuid)
{
	_sets.erase(meshUuid);
}

void SimulationSliceController::drawOverlay(Camera* camera, const MeshResolver& resolve)
{
	if (!_glFunctionsInitialized || !camera || _sets.empty() || !_renderCtrl.axisShader() || !resolve)
		return;

	const QMatrix4x4 view = camera->getViewMatrix();
	std::vector<float> litData, plainData; // 6 floats per vertex (position, colour): the plain sections and the lit surfaces, kept apart
	std::size_t plainVertices = 0;
	for (const auto& entry : _sets)
	{
		const RenderableMesh* mesh = resolve(entry.first);
		if (!mesh)
			continue;
		const QMatrix4x4 frame = mesh->combinedRenderTransform();
		for (const SliceDisplay& slice : entry.second)
		{
			std::vector<float>& target = slice.lit ? litData : plainData; // (the plain sections are drawn with a depth offset)
			const std::size_t vertexCount = slice.positions.size() / 3;
			if (slice.colors.size() < vertexCount * 3)
				continue;
			std::vector<QVector3D> world(vertexCount);
			for (std::size_t v = 0; v < vertexCount; ++v)
				world[v] = frame.map(QVector3D(slice.positions[v * 3], slice.positions[v * 3 + 1], slice.positions[v * 3 + 2]));
			for (std::size_t t = 0; t + 2 < slice.triangles.size(); t += 3)
			{
				const std::uint32_t ids[3] = { slice.triangles[t], slice.triangles[t + 1], slice.triangles[t + 2] };
				if (ids[0] >= vertexCount || ids[1] >= vertexCount || ids[2] >= vertexCount)
					continue;
				float shade = 1.0f;
				if (slice.lit)
				{
					QVector3D normal = QVector3D::crossProduct(world[ids[1]] - world[ids[0]], world[ids[2]] - world[ids[0]]);
					normal = view.mapVector(normal);
					normal.normalize();
					shade = 0.35f + 0.65f * std::fabs(normal.z()); // a headlight: facing the viewer = full colour
				}
				for (std::uint32_t id : ids)
					target.insert(target.end(), { world[id].x(), world[id].y(), world[id].z(), slice.colors[id * 3] * shade, slice.colors[id * 3 + 1] * shade,
					                              slice.colors[id * 3 + 2] * shade });
			}
		}
	}
	plainVertices = plainData.size() / 6;
	const std::size_t litVertices = litData.size() / 6;
	if (plainVertices + litVertices == 0)
		return;
	plainData.insert(plainData.end(), litData.begin(), litData.end()); // sections first, then the lit surfaces

	if (_vao == 0)
		glGenVertexArrays(1, &_vao);
	if (_vbo == 0)
		glGenBuffers(1, &_vbo);
	glBindVertexArray(_vao);
	glBindBuffer(GL_ARRAY_BUFFER, _vbo);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(plainData.size() * sizeof(float)), plainData.data(), GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void*>(0));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void*>(3 * sizeof(float)));

	const GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);
	glDisable(GL_CULL_FACE);
	// Draw against the model's depth whatever an earlier pass (capping, hover/pick) left behind: the surfaces lie inside the solid and must be hidden by it.
	const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST), stencilWasEnabled = glIsEnabled(GL_STENCIL_TEST), blendWasEnabled = glIsEnabled(GL_BLEND);
	GLint depthFunc = GL_LESS;
	GLboolean depthMask = GL_TRUE;
	glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
	glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
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
	if (plainVertices > 0)
	{
		// The cut lies in the Clipping Plane, where the model's own cap is drawn: a depth offset toward the viewer wins the tie.
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(-1.0f, -2.0f);
		glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(plainVertices));
		glDisable(GL_POLYGON_OFFSET_FILL);
	}
	if (litVertices > 0)
		glDrawArrays(GL_TRIANGLES, static_cast<GLint>(plainVertices), static_cast<GLsizei>(litVertices));
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
