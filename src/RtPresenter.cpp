#include "RtPresenter.h"

#include <QDebug>
#include <QVector2D>

#ifdef MODELVIEWER_HAVE_OPTIX
#include <cuda_gl_interop.h>
#include <cuda_runtime.h>
#endif

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
			shaderBasePath + "shaders/ray_traced_present.frag"))
	{
		qWarning() << "RtPresenter: failed to load/link ray_traced_present shader:" << _shader->log();
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
#ifdef MODELVIEWER_HAVE_OPTIX
	if (_interopCudaResource)
	{
		cudaGraphicsUnregisterResource(static_cast<cudaGraphicsResource_t>(_interopCudaResource));
		_interopCudaResource = nullptr;
	}
#endif
	if (_interopPbo) { glDeleteBuffers(1, &_interopPbo); _interopPbo = 0; }
	_interopPboWidth  = 0;
	_interopPboHeight = 0;
	_interopUnavailable = false;
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

bool RtPresenter::uploadFromDevice(void* deviceRgba, int width, int height)
{
#ifdef MODELVIEWER_HAVE_OPTIX
	if (_interopUnavailable || !deviceRgba || width <= 0 || height <= 0)
		return false;

	const size_t requiredBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * sizeof(float) * 4;

	if (_interopPbo == 0 || _interopPboWidth != width || _interopPboHeight != height)
	{
		if (_interopCudaResource)
		{
			cudaGraphicsUnregisterResource(static_cast<cudaGraphicsResource_t>(_interopCudaResource));
			_interopCudaResource = nullptr;
		}
		if (_interopPbo)
		{
			glDeleteBuffers(1, &_interopPbo);
			_interopPbo = 0;
		}

		glGenBuffers(1, &_interopPbo);
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _interopPbo);
		glBufferData(GL_PIXEL_UNPACK_BUFFER, static_cast<GLsizeiptr>(requiredBytes), nullptr, GL_STREAM_DRAW);
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

		cudaGraphicsResource_t resource = nullptr;
		const cudaError_t regErr = cudaGraphicsGLRegisterBuffer(&resource, _interopPbo, cudaGraphicsRegisterFlagsWriteDiscard);
		if (regErr != cudaSuccess)
		{
			qWarning() << "RtPresenter::uploadFromDevice: cudaGraphicsGLRegisterBuffer failed ("
				<< cudaGetErrorString(regErr) << ") - interactive GPU-resident presentation unavailable this"
				<< " session, falling back to the CPU-vector upload() path.";
			glDeleteBuffers(1, &_interopPbo);
			_interopPbo = 0;
			_interopUnavailable = true;
			return false;
		}
		_interopCudaResource = resource;
		_interopPboWidth  = width;
		_interopPboHeight = height;
	}

	cudaGraphicsResource_t resource = static_cast<cudaGraphicsResource_t>(_interopCudaResource);
	if (cudaGraphicsMapResources(1, &resource, 0) != cudaSuccess)
		return false;

	void* mappedPtr = nullptr;
	size_t mappedBytes = 0;
	bool ok = cudaGraphicsResourceGetMappedPointer(&mappedPtr, &mappedBytes, resource) == cudaSuccess;
	if (ok && mappedBytes >= requiredBytes)
		ok = cudaMemcpy(mappedPtr, deviceRgba, requiredBytes, cudaMemcpyDeviceToDevice) == cudaSuccess;
	cudaGraphicsUnmapResources(1, &resource, 0);
	if (!ok)
		return false;

	if (_texture == 0)
		glGenTextures(1, &_texture);

	glBindTexture(GL_TEXTURE_2D, _texture);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _interopPbo);
	if (_texWidth != width || _texHeight != height)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		_texWidth  = width;
		_texHeight = height;
	}
	else
	{
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_FLOAT, nullptr);
	}
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);

	_hasFrame = true;
	return true;
#else
	(void)deviceRgba;
	(void)width;
	(void)height;
	return false;
#endif
}

void RtPresenter::draw(bool hdrToneMapping, bool gammaCorrection, float screenGamma, float iblExposure, int toneMapMode, bool forceOpaque)
{
	if (!_hasFrame || !_shader || _vao == 0)
		return;

	const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	// See this method's header doc comment for forceOpaque's purpose -
	// GL_ONE/GL_ZERO is a plain overwrite (this frame's alpha channel never
	// factors in), vs the normal GL_SRC_ALPHA/GL_ONE_MINUS_SRC_ALPHA blend
	// that lets raster show through this frame's own alpha=0 "miss" pixels.
	if (forceOpaque)
		glBlendFunc(GL_ONE, GL_ZERO);
	else
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	_shader->bind();
	// ray_traced_present.frag computes uv = gl_FragCoord.xy / resolution -
	// gl_FragCoord is in the CURRENT GL VIEWPORT's pixel coordinates, which
	// is NOT necessarily the same size as the uploaded texture anymore now
	// that RtInteractiveRenderer can render at a reduced internal resolution
	// under sustained GPU load (see its _resolutionScale doc comment) while
	// the viewport itself stays full-size. Using _texWidth/_texHeight here
	// (this frame's own, possibly-smaller, dimensions) would make uv exceed
	// 1.0 over most of the viewport, and CLAMP_TO_EDGE would then show only
	// a small clamped corner of the texture stretched across the whole
	// screen - a dramatic, resolution-scale-dependent "zoom" that has
	// nothing to do with the camera. Querying the actual viewport keeps uv
	// correctly spanning [0,1] across the full screen regardless of the
	// source texture's resolution, so a smaller texture just samples
	// (correctly) blurrier via the texture's own GL_LINEAR filtering instead.
	GLint viewport[4] = { 0, 0, _texWidth, _texHeight };
	glGetIntegerv(GL_VIEWPORT, viewport);
	_shader->setUniformValue("resolution", QVector2D(static_cast<float>(viewport[2]), static_cast<float>(viewport[3])));
	_shader->setUniformValue("rayTracedTexture", 0);
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
