#include "SimulationVolumeController.h"

#include "AnalysisColorRamp.h"
#include "Camera.h"
#include "RenderableMesh.h"
#include "SceneRenderController.h"
#include "ShaderProgram.h"

#include <QMatrix4x4>
#include <QVector2D>
#include <QVector3D>

#include <algorithm>
#include <cmath>

SimulationVolumeController::SimulationVolumeController(SceneRenderController& renderCtrl, QObject* parent)
	: QObject(parent)
	, _renderCtrl(renderCtrl)
{
}

void SimulationVolumeController::restoreGpuResources()
{
	initializeOpenGLFunctions();
	_glFunctionsInitialized = true;
	if (!contextsAreShared())
		for (auto& pair : _entries)
		{
			pair.second.dataDirty = true;
			pair.second.transferDirty = true;
		}
	// FBO names are never shared. The depth texture itself survives a shared-context recreation.
	_depthFramebuffer = 0;
	if (!contextsAreShared())
	{
		_depthTexture = 0;
		_depthSize = QSize();
		_retiredTextures.clear();
	}
}

void SimulationVolumeController::deleteTextures(Entry& entry)
{
	if (!_glFunctionsInitialized)
		return;
	for (unsigned int* texture : { &entry.valueTexture, &entry.validityTexture, &entry.transferTexture })
		if (*texture)
		{
			glDeleteTextures(1, texture);
			*texture = 0;
		}
}

void SimulationVolumeController::retireTextures(Entry& entry)
{
	for (unsigned int* texture : { &entry.valueTexture, &entry.validityTexture, &entry.transferTexture })
		if (*texture)
		{
			_retiredTextures.push_back(*texture);
			*texture = 0;
		}
}

void SimulationVolumeController::deleteRetiredTextures()
{
	if (!_glFunctionsInitialized || _retiredTextures.empty())
		return;
	glDeleteTextures(static_cast<GLsizei>(_retiredTextures.size()), _retiredTextures.data());
	_retiredTextures.clear();
}

void SimulationVolumeController::releaseGpuResources()
{
	if (_glFunctionsInitialized && _depthFramebuffer)
		glDeleteFramebuffers(1, &_depthFramebuffer);
	_depthFramebuffer = 0;
	if (!contextsAreShared())
	{
		for (auto& pair : _entries)
			deleteTextures(pair.second);
		deleteRetiredTextures();
		if (_glFunctionsInitialized && _depthTexture)
			glDeleteTextures(1, &_depthTexture);
		_depthTexture = 0;
		_depthSize = QSize();
	}
	_glFunctionsInitialized = false;
}

void SimulationVolumeController::setVolume(const QUuid& meshUuid, VolumeGrid grid, int colormap, QVector<QPointF> opacity)
{
	Entry& entry = _entries[meshUuid];
	entry.grid = std::move(grid);
	entry.colormap = colormap;
	entry.opacity = std::move(opacity);
	entry.dataDirty = true;
	entry.transferDirty = true;
}

void SimulationVolumeController::setTransferFunction(const QUuid& meshUuid, int colormap, QVector<QPointF> opacity)
{
	auto found = _entries.find(meshUuid);
	if (found == _entries.end())
		return;
	Entry& entry = found->second;
	if (entry.colormap == colormap && entry.opacity == opacity)
		return;
	entry.colormap = colormap;
	entry.opacity = std::move(opacity);
	entry.transferDirty = true;
}

void SimulationVolumeController::clearVolume(const QUuid& meshUuid)
{
	auto found = _entries.find(meshUuid);
	if (found == _entries.end())
		return;
	// UI actions call this without a current GL context. Defer deletion to drawOverlay()/releaseGpuResources().
	retireTextures(found->second);
	_entries.erase(found);
}

bool SimulationVolumeController::hasVisibleVolumes(const MeshResolver& resolve) const
{
	if (!resolve)
		return false;
	for (const auto& pair : _entries)
		if (resolve(pair.first))
			return true;
	return false;
}

void SimulationVolumeController::upload(Entry& entry)
{
	if ((!entry.dataDirty && !entry.transferDirty) || entry.grid.empty())
		return;
	GLint unpackAlignment = 4;
	glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlignment);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	auto make3D = [&](unsigned int& texture, GLint internalFormat, GLenum format, GLenum type, const void* data) {
		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_3D, texture);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER);
		const float border[4] = { 0, 0, 0, 0 };
		glTexParameterfv(GL_TEXTURE_3D, GL_TEXTURE_BORDER_COLOR, border);
		glTexImage3D(GL_TEXTURE_3D, 0, internalFormat, entry.grid.dimX, entry.grid.dimY, entry.grid.dimZ, 0, format, type, data);
	};
	if (entry.dataDirty)
	{
		if (entry.valueTexture) glDeleteTextures(1, &entry.valueTexture);
		if (entry.validityTexture) glDeleteTextures(1, &entry.validityTexture);
		entry.valueTexture = entry.validityTexture = 0;
		const std::size_t count = entry.grid.voxelCount();
		std::vector<float> normalized(count, 0.0f);
		std::vector<unsigned char> valid(count, 0);
		const float span = entry.grid.fieldMax - entry.grid.fieldMin;
		for (std::size_t i = 0; i < count; ++i)
			if (std::isfinite(entry.grid.values[i]))
			{
				normalized[i] = std::clamp((entry.grid.values[i] - entry.grid.fieldMin) / span, 0.0f, 1.0f);
				valid[i] = 255;
			}
		make3D(entry.valueTexture, GL_R32F, GL_RED, GL_FLOAT, normalized.data());
		make3D(entry.validityTexture, GL_R8, GL_RED, GL_UNSIGNED_BYTE, valid.data());
		entry.dataDirty = false;
	}

	if (entry.transferDirty && entry.opacity.size() < 2)
		entry.opacity = { QPointF(0.0, 0.0), QPointF(1.0, 0.25) };
	if (entry.transferDirty)
	{
		if (entry.transferTexture)
			glDeleteTextures(1, &entry.transferTexture);
		entry.transferTexture = 0;
		std::sort(entry.opacity.begin(), entry.opacity.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
		std::vector<unsigned char> transfer(256 * 4);
		int segment = 0;
		for (int i = 0; i < 256; ++i)
		{
			const float x = i / 255.0f;
			while (segment + 2 < entry.opacity.size() && x > entry.opacity[segment + 1].x()) ++segment;
			const QPointF a = entry.opacity[segment],
			              b = entry.opacity[std::min<qsizetype>(segment + 1, entry.opacity.size() - 1)];
			const double width = b.x() - a.x();
			const double t = width > 0.0 ? std::clamp((x - a.x()) / width, 0.0, 1.0) : 0.0;
			const float opacity = static_cast<float>(std::clamp(a.y() + t * (b.y() - a.y()), 0.0, 1.0));
			const QColor color = AnalysisColorRamp::colorForNormalized(x, static_cast<AnalysisColormap>(entry.colormap));
			transfer[i * 4] = static_cast<unsigned char>(color.red());
			transfer[i * 4 + 1] = static_cast<unsigned char>(color.green());
			transfer[i * 4 + 2] = static_cast<unsigned char>(color.blue());
			transfer[i * 4 + 3] = static_cast<unsigned char>(std::lround(opacity * 255.0f));
		}
		glGenTextures(1, &entry.transferTexture);
		glBindTexture(GL_TEXTURE_1D, entry.transferTexture);
		glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA8, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, transfer.data());
		entry.transferDirty = false;
	}
	glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlignment);
}

bool SimulationVolumeController::captureDepth(const QSize& framebufferSize)
{
	if (framebufferSize.isEmpty())
		return false;
	GLint oldActiveTexture = GL_TEXTURE0, oldTexture = 0, oldReadFramebuffer = 0, oldDrawFramebuffer = 0;
	glGetIntegerv(GL_ACTIVE_TEXTURE, &oldActiveTexture);
	glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTexture);
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &oldReadFramebuffer);
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &oldDrawFramebuffer);
	if (_depthTexture == 0)
		glGenTextures(1, &_depthTexture);
	glBindTexture(GL_TEXTURE_2D, _depthTexture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	if (_depthSize != framebufferSize)
	{
		// The viewport explicitly requests a 24-bit depth + 8-bit stencil buffer. Matching that format is required
		// when a multisampled QOpenGLWidget buffer is resolved with glBlitFramebuffer.
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, framebufferSize.width(), framebufferSize.height(), 0,
		             GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
		_depthSize = framebufferSize;
	}
	if (_depthFramebuffer == 0)
		glGenFramebuffers(1, &_depthFramebuffer);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _depthFramebuffer);
	glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, _depthTexture, 0);
	glDrawBuffer(GL_NONE);
	const bool complete = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
	if (complete)
	{
		// This also resolves a multisampled QOpenGLWidget depth buffer into the single-sample texture.
		glBindFramebuffer(GL_READ_FRAMEBUFFER, oldReadFramebuffer);
		glBlitFramebuffer(0, 0, framebufferSize.width(), framebufferSize.height(),
		                  0, 0, framebufferSize.width(), framebufferSize.height(), GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	}
	glBindFramebuffer(GL_READ_FRAMEBUFFER, oldReadFramebuffer);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, oldDrawFramebuffer);
	glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(oldTexture));
	glActiveTexture(static_cast<GLenum>(oldActiveTexture));
	return complete;
}

void SimulationVolumeController::drawOverlay(Camera* camera, const QSize& framebufferSize,
	const SimulationVolumeClipState& clipping, const MeshResolver& resolve)
{
	ShaderProgram* shader = _renderCtrl.volumeShader();
	if (!_glFunctionsInitialized)
		return;
	deleteRetiredTextures();
	if (!camera || !shader || !resolve || _entries.empty() || !captureDepth(framebufferSize))
		return;

	const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
	const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
	const GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);
	const GLboolean stencilWasEnabled = glIsEnabled(GL_STENCIL_TEST);
	GLboolean depthMask = GL_TRUE;
	glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
	GLint blendSrcRgb = GL_ONE, blendDstRgb = GL_ZERO, blendSrcAlpha = GL_ONE, blendDstAlpha = GL_ZERO;
	GLint blendEquationRgb = GL_FUNC_ADD, blendEquationAlpha = GL_FUNC_ADD;
	glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb);
	glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb);
	glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha);
	glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha);
	glGetIntegerv(GL_BLEND_EQUATION_RGB, &blendEquationRgb);
	glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blendEquationAlpha);
	GLint oldActiveTexture = GL_TEXTURE0;
	glGetIntegerv(GL_ACTIVE_TEXTURE, &oldActiveTexture);
	GLint viewport[4] = { 0, 0, framebufferSize.width(), framebufferSize.height() };
	glGetIntegerv(GL_VIEWPORT, viewport);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_STENCIL_TEST);
	glDepthMask(GL_FALSE);

	bool invertible = false;
	const QMatrix4x4 inverseViewProjection = (camera->getProjectionMatrix() * camera->getViewMatrix()).inverted(&invertible);
	if (invertible)
		for (auto& pair : _entries)
		{
			const RenderableMesh* mesh = resolve(pair.first);
			if (!mesh)
				continue;
			Entry& entry = pair.second;
			upload(entry);
			if (!entry.valueTexture || !entry.validityTexture || !entry.transferTexture)
				continue;
			const QMatrix4x4 inverseModel = mesh->combinedRenderTransform().inverted(&invertible);
			if (!invertible)
				continue;

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_3D, entry.valueTexture);
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_3D, entry.validityTexture);
			glActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_1D, entry.transferTexture);
			glActiveTexture(GL_TEXTURE3);
			glBindTexture(GL_TEXTURE_2D, _depthTexture);
			shader->bind();
			shader->setUniformValue("volumeValues", 0);
			shader->setUniformValue("volumeValidity", 1);
			shader->setUniformValue("transferFunction", 2);
			shader->setUniformValue("sceneDepth", 3);
			shader->setUniformValue("inverseViewProjection", inverseViewProjection);
			shader->setUniformValue("inverseModel", inverseModel);
			shader->setUniformValue("boxMinimum", QVector3D(entry.grid.origin[0], entry.grid.origin[1], entry.grid.origin[2]));
			shader->setUniformValue("boxMaximum", QVector3D(entry.grid.origin[0] + entry.grid.voxelSize[0] * entry.grid.dimX,
			                                                    entry.grid.origin[1] + entry.grid.voxelSize[1] * entry.grid.dimY,
			                                                    entry.grid.origin[2] + entry.grid.voxelSize[2] * entry.grid.dimZ));
			shader->setUniformValue("voxelSize", QVector3D(entry.grid.voxelSize[0], entry.grid.voxelSize[1], entry.grid.voxelSize[2]));
			shader->setUniformValue("framebufferSize", QVector2D(framebufferSize.width(), framebufferSize.height()));
			shader->setUniformValue("viewportOrigin", QVector2D(viewport[0], viewport[1]));
			shader->setUniformValue("viewportSize", QVector2D(viewport[2], viewport[3]));
			shader->setUniformValue("axisClipEnabled", clipping.axisEnabled);
			shader->setUniformValue("axisClipThreshold", clipping.axisThreshold);
			shader->setUniformValue("axisClipSign", clipping.axisSign);
			shader->setUniformValue("boxClipEnabled", clipping.boxEnabled);
			shader->setUniformValue("boxClipKeepInside", clipping.boxKeepInside);
			shader->setUniformValue("boxClipMinimum", clipping.boxMinimum);
			shader->setUniformValue("boxClipMaximum", clipping.boxMaximum);
			_renderCtrl.drawFullscreenTriangle();
			shader->release();
		}

	glActiveTexture(oldActiveTexture);
	glDepthMask(depthMask);
	glBlendFuncSeparate(static_cast<GLenum>(blendSrcRgb), static_cast<GLenum>(blendDstRgb),
	                    static_cast<GLenum>(blendSrcAlpha), static_cast<GLenum>(blendDstAlpha));
	glBlendEquationSeparate(static_cast<GLenum>(blendEquationRgb), static_cast<GLenum>(blendEquationAlpha));
	if (cullWasEnabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
	if (depthWasEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
	if (stencilWasEnabled) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
	if (blendWasEnabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
}
