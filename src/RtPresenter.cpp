#include "RtPresenter.h"

#include <QDebug>
#include <QVector2D>

bool RtPresenter::initialize(const QString& shaderBasePath)
{
	initializeOpenGLFunctions();

	const float verts[6] = {
		-1.0f, -1.0f,
		 3.0f, -1.0f,
		-1.0f,  3.0f
	};

	glGenVertexArrays(1, &_vao);
	glGenBuffers(1, &_vbo);

	glBindVertexArray(_vao);
	glBindBuffer(GL_ARRAY_BUFFER, _vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	_shader = std::make_unique<ShaderProgram>();
	_shader->setObjectName("_rtPresentShader");
	if (!_shader->loadCompileAndLinkShaderFromFile(
			shaderBasePath + "shaders/fullscreen_triangle.vert",
			shaderBasePath + "shaders/path_traced_present.frag"))
	{
		qWarning() << "RtPresenter: failed to load/link path_traced_present shader:" << _shader->log();
		_shader.reset();
		return false;
	}

	return true;
}

void RtPresenter::cleanup()
{
	if (_vao)     { glDeleteVertexArrays(1, &_vao); _vao = 0; }
	if (_vbo)     { glDeleteBuffers(1, &_vbo); _vbo = 0; }
	if (_texture) { glDeleteTextures(1, &_texture); _texture = 0; }
	_shader.reset();
	_hasFrame  = false;
	_texWidth  = 0;
	_texHeight = 0;
}

void RtPresenter::upload(const std::vector<glm::vec3>& rgb, int width, int height,
	const std::vector<float>* alpha)
{
	if (width <= 0 || height <= 0 || rgb.size() != static_cast<size_t>(width) * static_cast<size_t>(height))
		return;

	std::vector<glm::vec4> rgba(rgb.size(), glm::vec4(0.0f));
	const bool hasAlpha = alpha && alpha->size() == rgb.size();
	for (size_t i = 0; i < rgb.size(); ++i)
		rgba[i] = glm::vec4(rgb[i], hasAlpha ? (*alpha)[i] : 1.0f);

	if (_texture == 0)
		glGenTextures(1, &_texture);

	glBindTexture(GL_TEXTURE_2D, _texture);

	if (_texWidth != width || _texHeight != height)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, rgba.data());
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		_texWidth  = width;
		_texHeight = height;
	}
	else
	{
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_FLOAT, rgba.data());
	}

	glBindTexture(GL_TEXTURE_2D, 0);
	_hasFrame = true;
}

void RtPresenter::draw(bool hdrToneMapping, bool gammaCorrection, float screenGamma, float iblExposure, int toneMapMode)
{
	if (!_hasFrame || !_shader || _vao == 0)
		return;

	const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	_shader->bind();
	_shader->setUniformValue("resolution", QVector2D(static_cast<float>(_texWidth), static_cast<float>(_texHeight)));
	_shader->setUniformValue("pathTracedTexture", 0);
	_shader->setUniformValue("hdrToneMapping", hdrToneMapping);
	_shader->setUniformValue("gammaCorrection", gammaCorrection);
	_shader->setUniformValue("screenGamma", screenGamma);
	_shader->setUniformValue("iblExposure", iblExposure);
	_shader->setUniformValue("toneMapMode", toneMapMode);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, _texture);

	glBindVertexArray(_vao);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);

	glBindTexture(GL_TEXTURE_2D, 0);
	_shader->release();

	if (!blendWasEnabled)
		glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
}
