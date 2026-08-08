#include "config.h"
#include "Point.h"
#include "TriangleBaldwinWeber.h"
#include "RenderableMesh.h"
#include "SceneMesh.h"
#include "TriangleMollerTrumbore.h"
#include "Utils.h"
#include "Logger.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <iostream>
#include <QApplication>
#include <QElapsedTimer>
#include <QQuaternion>
#include <QVector3D>

namespace
{
using TextureBindingCache = std::array<GLuint, 40>;
constexpr GLuint kUnknownTextureBinding = std::numeric_limits<GLuint>::max();
QHash<QOpenGLContext*, TextureBindingCache> s_textureBindingsByContext;
QHash<QOpenGLContext*, QOpenGLShaderProgram*> s_currentBoundPrograms;

struct RenderDiagnostics
{
	bool enabled = false;
	bool timerStarted = false;
	QElapsedTimer windowTimer;
	int frames = 0;
	qint64 drawCalls = 0;
	qint64 indexedDrawCalls = 0;
	qint64 arrayDrawCalls = 0;
	qint64 transparentDrawCalls = 0;
	qint64 rawProgramBindCalls = 0;
	qint64 cachedProgramBindCalls = 0;
	qint64 cachedProgramBindHits = 0;
	qint64 rawTextureBindCalls = 0;
	qint64 cachedTextureBindCalls = 0;
	qint64 cachedTextureBindHits = 0;
	qint64 vaoProgramReconfigures = 0;
	qint64 materialUniformRefreshes = 0;
	qint64 materialUniformExplicitDirtyRefreshes = 0;
	qint64 materialUniformShaderSwitchRefreshes = 0;
	qint64 materialUniformSignatureMismatchRefreshes = 0;
	qint64 materialUniformDebugOverrideRefreshes = 0;
	qint64 materialUniformReuses = 0;
	qint64 materialDirtyBySetProg = 0;
	qint64 transformUniformUploads = 0;
	qint64 jointUniformUploads = 0;
	double frameCpuMs = 0.0;
	double opaquePassCpuMs = 0.0;
	double transparentPassCpuMs = 0.0;
	double floorPassCpuMs = 0.0;
	double renderMeshWithDisplayModeCpuMs = 0.0;
	double assImpRenderCpuMs = 0.0;
	double textureCacheCpuMs = 0.0;
	double transformUniformCpuMs = 0.0;
	double materialUniformCpuMs = 0.0;
	double textureBindCpuMs = 0.0;
	double renderStateCpuMs = 0.0;
	double drawCpuMs = 0.0;
};

RenderDiagnostics s_renderDiagnostics;

QQuaternion meshEulerToQuaternion(const QVector3D& rotation)
{
	QMatrix4x4 matrix;
	matrix.setToIdentity();
	matrix.rotate(rotation.x(), QVector3D(1.0f, 0.0f, 0.0f));
	matrix.rotate(rotation.y(), QVector3D(0.0f, 1.0f, 0.0f));
	matrix.rotate(rotation.z(), QVector3D(0.0f, 0.0f, 1.0f));
	return QQuaternion::fromRotationMatrix(matrix.toGenericMatrix<3, 3>()).normalized();
}
}

QMatrix4x4 RenderableMesh::_currentGlobalModelMatrix;
QMatrix4x4 RenderableMesh::_currentViewMatrix;
bool       RenderableMesh::_lodPolicyUseLod1 = false;

void RenderableMesh::setCurrentRenderContext(const QMatrix4x4& globalModelMatrix,
                                           const QMatrix4x4& viewMatrix)
{
	_currentGlobalModelMatrix = globalModelMatrix;
	_currentViewMatrix = viewMatrix;
}

void RenderableMesh::clearCurrentRenderContext()
{
	_currentGlobalModelMatrix.setToIdentity();
	_currentViewMatrix.setToIdentity();
}

void RenderableMesh::setLodPolicy(bool useLod1)
{
	_lodPolicyUseLod1 = useLod1;
}

bool RenderableMesh::lodPolicyActive()
{
	return _lodPolicyUseLod1;
}

const QMatrix4x4& RenderableMesh::currentGlobalModelMatrix()
{
	return _currentGlobalModelMatrix;
}

const QMatrix4x4& RenderableMesh::currentViewMatrix()
{
	return _currentViewMatrix;
}

void RenderableMesh::bindTextureUnitCached(GLenum textureUnit, GLuint textureId)
{
	QOpenGLContext* context = QOpenGLContext::currentContext();
	if (!context)
	{
		return;
	}

	QOpenGLFunctions* funcs = context->functions();
	funcs->glActiveTexture(textureUnit);

	const int unitIndex = static_cast<int>(textureUnit - GL_TEXTURE0);
	if (unitIndex < 0 || unitIndex >= 40)
	{
		funcs->glBindTexture(GL_TEXTURE_2D, textureId);
		recordTextureBindCall(true);
		return;
	}

	auto it = s_textureBindingsByContext.find(context);
	if (it == s_textureBindingsByContext.end())
	{
		TextureBindingCache fresh;
		fresh.fill(kUnknownTextureBinding);
		it = s_textureBindingsByContext.insert(context, fresh);
	}
	TextureBindingCache& cache = it.value();
	if (cache[unitIndex] != textureId)
	{
		funcs->glBindTexture(GL_TEXTURE_2D, textureId);
		cache[unitIndex] = textureId;
		recordTextureBindCall(true);
	}
	else
	{
		recordTextureBindCall(false);
	}
}

void RenderableMesh::resetTextureBindingCacheForCurrentContext()
{
	if (QOpenGLContext* context = QOpenGLContext::currentContext())
		s_textureBindingsByContext.remove(context);
}

void RenderableMesh::bindProgramCached(QOpenGLShaderProgram* prog)
{
	QOpenGLContext* ctx = QOpenGLContext::currentContext();
	if (!ctx)
	{
		prog->bind();
		recordProgramBindCall(true);
		return;
	}
	auto it = s_currentBoundPrograms.find(ctx);
	if (it == s_currentBoundPrograms.end() || it.value() != prog)
	{
		prog->bind();
		s_currentBoundPrograms[ctx] = prog;
		recordProgramBindCall(true);
	}
	else
	{
		recordProgramBindCall(false);
	}
}

void RenderableMesh::notifyProgramBound(QOpenGLShaderProgram* prog)
{
	if (QOpenGLContext* ctx = QOpenGLContext::currentContext())
		s_currentBoundPrograms[ctx] = prog;
}

void RenderableMesh::resetBoundProgramCacheForCurrentContext()
{
	if (QOpenGLContext* ctx = QOpenGLContext::currentContext())
		s_currentBoundPrograms.remove(ctx);
}

bool RenderableMesh::renderDiagnosticsEnabled()
{
	return s_renderDiagnostics.enabled;
}

void RenderableMesh::beginRenderDiagnosticsFrame(bool enabled)
{
	if (!enabled)
	{
		s_renderDiagnostics = RenderDiagnostics{};
		return;
	}

	if (!s_renderDiagnostics.timerStarted)
	{
		s_renderDiagnostics.windowTimer.start();
		s_renderDiagnostics.timerStarted = true;
	}

	s_renderDiagnostics.enabled = true;
	++s_renderDiagnostics.frames;
}

void RenderableMesh::recordFrameCpuMs(double ms)
{
	if (s_renderDiagnostics.enabled)
		s_renderDiagnostics.frameCpuMs += ms;
}

void RenderableMesh::recordOpaquePassCpuMs(double ms)
{
	if (s_renderDiagnostics.enabled)
		s_renderDiagnostics.opaquePassCpuMs += ms;
}

void RenderableMesh::recordTransparentPassCpuMs(double ms)
{
	if (s_renderDiagnostics.enabled)
		s_renderDiagnostics.transparentPassCpuMs += ms;
}

void RenderableMesh::recordFloorPassCpuMs(double ms)
{
	if (s_renderDiagnostics.enabled)
		s_renderDiagnostics.floorPassCpuMs += ms;
}

void RenderableMesh::recordRenderMeshWithDisplayModeCpuMs(double ms)
{
	if (s_renderDiagnostics.enabled)
		s_renderDiagnostics.renderMeshWithDisplayModeCpuMs += ms;
}

void RenderableMesh::recordAssImpRenderCpuMs(double ms)
{
	if (s_renderDiagnostics.enabled)
		s_renderDiagnostics.assImpRenderCpuMs += ms;
}

void RenderableMesh::recordProgramBindCall(bool actualBind)
{
	if (!s_renderDiagnostics.enabled)
		return;
	++s_renderDiagnostics.cachedProgramBindCalls;
	if (actualBind)
		++s_renderDiagnostics.rawProgramBindCalls;
	else
		++s_renderDiagnostics.cachedProgramBindHits;
}

void RenderableMesh::recordTextureBindCall(bool actualBind)
{
	if (!s_renderDiagnostics.enabled)
		return;
	++s_renderDiagnostics.cachedTextureBindCalls;
	if (actualBind)
		++s_renderDiagnostics.rawTextureBindCalls;
	else
		++s_renderDiagnostics.cachedTextureBindHits;
}

void RenderableMesh::recordVaoProgramReconfigure()
{
	if (s_renderDiagnostics.enabled)
		++s_renderDiagnostics.vaoProgramReconfigures;
}

void RenderableMesh::recordMaterialUniformRefresh(bool /*explicitDirty*/)
{
	if (!s_renderDiagnostics.enabled)
		return;
	++s_renderDiagnostics.materialUniformRefreshes;
	// Reason breakdown (including explicit-dirty count) is recorded exclusively
	// by recordMaterialRefreshReason() to avoid double-counting.
}

void RenderableMesh::recordMaterialUniformReuse()
{
	if (s_renderDiagnostics.enabled)
		++s_renderDiagnostics.materialUniformReuses;
}

void RenderableMesh::recordMaterialRefreshReason(bool explicitDirty,
	bool shaderSwitch,
	bool signatureMismatch,
	bool debugOverridesBlockedReuse)
{
	if (!s_renderDiagnostics.enabled)
		return;
	if (explicitDirty)
		++s_renderDiagnostics.materialUniformExplicitDirtyRefreshes;
	if (shaderSwitch)
		++s_renderDiagnostics.materialUniformShaderSwitchRefreshes;
	if (signatureMismatch)
		++s_renderDiagnostics.materialUniformSignatureMismatchRefreshes;
	if (debugOverridesBlockedReuse)
		++s_renderDiagnostics.materialUniformDebugOverrideRefreshes;
}

void RenderableMesh::recordMaterialDirtyBySetProg()
{
	if (s_renderDiagnostics.enabled)
		++s_renderDiagnostics.materialDirtyBySetProg;
}

void RenderableMesh::recordTransformUniformUploads(int count)
{
	if (s_renderDiagnostics.enabled)
		s_renderDiagnostics.transformUniformUploads += count;
}

void RenderableMesh::recordJointUniformUploads(int count)
{
	if (s_renderDiagnostics.enabled)
		s_renderDiagnostics.jointUniformUploads += count;
}

void RenderableMesh::recordDrawCall(bool indexed, bool transparent)
{
	if (!s_renderDiagnostics.enabled)
		return;
	++s_renderDiagnostics.drawCalls;
	if (indexed)
		++s_renderDiagnostics.indexedDrawCalls;
	else
		++s_renderDiagnostics.arrayDrawCalls;
	if (transparent)
		++s_renderDiagnostics.transparentDrawCalls;
}

void RenderableMesh::recordTextureCacheCpuMs(double ms)
{
	if (s_renderDiagnostics.enabled)
		s_renderDiagnostics.textureCacheCpuMs += ms;
}

void RenderableMesh::recordTransformUniformCpuMs(double ms)
{
	if (s_renderDiagnostics.enabled)
		s_renderDiagnostics.transformUniformCpuMs += ms;
}

void RenderableMesh::recordMaterialUniformCpuMs(double ms)
{
	if (s_renderDiagnostics.enabled)
		s_renderDiagnostics.materialUniformCpuMs += ms;
}

void RenderableMesh::recordTextureBindCpuMs(double ms)
{
	if (s_renderDiagnostics.enabled)
		s_renderDiagnostics.textureBindCpuMs += ms;
}

void RenderableMesh::recordRenderStateCpuMs(double ms)
{
	if (s_renderDiagnostics.enabled)
		s_renderDiagnostics.renderStateCpuMs += ms;
}

void RenderableMesh::recordDrawCpuMs(double ms)
{
	if (s_renderDiagnostics.enabled)
		s_renderDiagnostics.drawCpuMs += ms;
}

void RenderableMesh::flushRenderDiagnostics()
{
	if (!s_renderDiagnostics.enabled || !s_renderDiagnostics.timerStarted)
		return;

	const qint64 windowMs = s_renderDiagnostics.windowTimer.elapsed();
	if (windowMs < 1000 || s_renderDiagnostics.frames <= 0)
		return;

	const double frames = static_cast<double>(s_renderDiagnostics.frames);
	Logger::instance().info(
		QStringLiteral("[RenderDiagnostics] windowMs=%1 frames=%2 frameCpuMs/frame=%3 opaqueMs/frame=%4 transparentMs/frame=%5 floorMs/frame=%6 meshModeMs/frame=%7 assImpMs/frame=%8 textureCacheMs/frame=%9 transformUniformMs/frame=%10 materialUniformMs/frame=%11 textureBindMs/frame=%12 renderStateMs/frame=%13 drawMs/frame=%14 draws/frame=%15 indexed/frame=%16 arrays/frame=%17 transparentDraws/frame=%18 rawProgBinds/frame=%19 progBindObservations/frame=%20 progCacheHits/frame=%21 rawTexBinds/frame=%22 texBindObservations/frame=%23 texCacheHits/frame=%24 vaoReconfig/frame=%25 materialRefresh/frame=%26 materialDirtyRefresh/frame=%27 materialShaderSwitchRefresh/frame=%28 materialSignatureRefresh/frame=%29 materialDebugBlockedRefresh/frame=%30 materialDirtyBySetProg/frame=%31 materialReuse/frame=%32 transformUniforms/frame=%33 jointUniforms/frame=%34")
			.arg(windowMs)
			.arg(s_renderDiagnostics.frames)
			.arg(s_renderDiagnostics.frameCpuMs / frames, 0, 'f', 3)
			.arg(s_renderDiagnostics.opaquePassCpuMs / frames, 0, 'f', 3)
			.arg(s_renderDiagnostics.transparentPassCpuMs / frames, 0, 'f', 3)
			.arg(s_renderDiagnostics.floorPassCpuMs / frames, 0, 'f', 3)
			.arg(s_renderDiagnostics.renderMeshWithDisplayModeCpuMs / frames, 0, 'f', 3)
			.arg(s_renderDiagnostics.assImpRenderCpuMs / frames, 0, 'f', 3)
			.arg(s_renderDiagnostics.textureCacheCpuMs / frames, 0, 'f', 3)
			.arg(s_renderDiagnostics.transformUniformCpuMs / frames, 0, 'f', 3)
			.arg(s_renderDiagnostics.materialUniformCpuMs / frames, 0, 'f', 3)
			.arg(s_renderDiagnostics.textureBindCpuMs / frames, 0, 'f', 3)
			.arg(s_renderDiagnostics.renderStateCpuMs / frames, 0, 'f', 3)
			.arg(s_renderDiagnostics.drawCpuMs / frames, 0, 'f', 3)
			.arg(s_renderDiagnostics.drawCalls / frames, 0, 'f', 1)
			.arg(s_renderDiagnostics.indexedDrawCalls / frames, 0, 'f', 1)
			.arg(s_renderDiagnostics.arrayDrawCalls / frames, 0, 'f', 1)
			.arg(s_renderDiagnostics.transparentDrawCalls / frames, 0, 'f', 1)
			.arg(s_renderDiagnostics.rawProgramBindCalls / frames, 0, 'f', 1)
			.arg(s_renderDiagnostics.cachedProgramBindCalls / frames, 0, 'f', 1)
			.arg(s_renderDiagnostics.cachedProgramBindHits / frames, 0, 'f', 1)
			.arg(s_renderDiagnostics.rawTextureBindCalls / frames, 0, 'f', 1)
			.arg(s_renderDiagnostics.cachedTextureBindCalls / frames, 0, 'f', 1)
			.arg(s_renderDiagnostics.cachedTextureBindHits / frames, 0, 'f', 1)
			.arg(s_renderDiagnostics.vaoProgramReconfigures / frames, 0, 'f', 1)
			.arg(s_renderDiagnostics.materialUniformRefreshes / frames, 0, 'f', 1)
			.arg(s_renderDiagnostics.materialUniformExplicitDirtyRefreshes / frames, 0, 'f', 1)
			.arg(s_renderDiagnostics.materialUniformShaderSwitchRefreshes / frames, 0, 'f', 1)
			.arg(s_renderDiagnostics.materialUniformSignatureMismatchRefreshes / frames, 0, 'f', 1)
			.arg(s_renderDiagnostics.materialUniformDebugOverrideRefreshes / frames, 0, 'f', 1)
			.arg(s_renderDiagnostics.materialDirtyBySetProg / frames, 0, 'f', 1)
			.arg(s_renderDiagnostics.materialUniformReuses / frames, 0, 'f', 1)
			.arg(s_renderDiagnostics.transformUniformUploads / frames, 0, 'f', 1)
			.arg(s_renderDiagnostics.jointUniformUploads / frames, 0, 'f', 1),
		QStringLiteral("Performance"));

	s_renderDiagnostics = RenderDiagnostics{};
}

quint64 RenderableMesh::currentRuntimeBoundsRevision()
{
	return MeshInstanceState::currentRuntimeBoundsRevision();
}

void RenderableMesh::markRuntimeBoundsChanged()
{
	MeshInstanceState::markRuntimeBoundsChanged();
}

RenderableMesh::RenderableMesh(QOpenGLShaderProgram* prog, const QString name) : Drawable(prog),
_material(_materialState.material()),
_hasVertexColors(false)
{
	setAutoIncrName(name);
	_memorySize = 0;
	// Transform/bounds state lives in _instanceState; material state in _materialState.

	_indexBuffer = QOpenGLBuffer(QOpenGLBuffer::IndexBuffer);
	_positionBuffer = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
	_normalBuffer = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
	_colorBuffer = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
	_texCoord0Buffer = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
	_texCoord1Buffer = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
	_texCoord2Buffer = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
	_texCoord3Buffer = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
	_tangentBuf = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
	_bitangentBuf = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
	_jointIndexBuffer = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
	_jointWeightBuffer = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);

	_indexBuffer.create();
	_positionBuffer.create();
	_normalBuffer.create();
	_colorBuffer.create();
	_texCoord0Buffer.create();
	_texCoord1Buffer.create();
	_texCoord2Buffer.create();
	_texCoord3Buffer.create();
	_tangentBuf.create();
	_bitangentBuf.create();
	_jointIndexBuffer.create();
	_jointWeightBuffer.create();

	_vertexArrayObject.create();

	QImage dummy(128, 128, QImage::Format_ARGB32);
	dummy.fill(Qt::white);
	_fallbackTextureBuffer = dummy;
	_fallbackTextureImage = convertToGLFormat(_fallbackTextureBuffer);

	recreateFallbackTexture();
}

void RenderableMesh::cacheBaseVolumeProperties()
{
	_materialState.cacheBaseVolumeProperties();
}

void RenderableMesh::applyScaledVolumeProperties()
{
	_materialState.applyScaledVolumeProperties();
}

void RenderableMesh::uploadGeometry(const MeshGeometry& geom)
{
	auto idx = geom.indices();
	auto pts = geom.points();
	auto nrm = geom.normals();
	auto col = geom.colors();
	auto tex = geom.texCoords();
	auto tg  = geom.tangents();
	auto bt  = geom.bitangents();

	initBuffers(
		idx.empty() ? nullptr : &idx,
		pts.empty() ? nullptr : &pts,
		nrm.empty() ? nullptr : &nrm,
		col.empty() ? nullptr : &col,
		tex.empty() ? nullptr : &tex,
		tg.empty()  ? nullptr : &tg,
		bt.empty()  ? nullptr : &bt
	);

	_sMax            = geom.sMax();
	_tMax            = geom.tMax();
	_hasVertexColors = geom.hasVertexColors();
}

void RenderableMesh::initBuffers(
	std::vector<unsigned int>* indices,
	std::vector<float>* points,
	std::vector<float>* normals,
	std::vector<float>* colors,
	std::vector<float>* texCoords,
	std::vector<float>* tangents,
	std::vector<float>* bitangents,
	std::vector<float>* jointIndices,
	std::vector<float>* jointWeights)
{
	// Must have data for indices, points, and normals
	if (indices == nullptr || points == nullptr || normals == nullptr)
		return;

	_indices = *indices;
	_points = *points;
	_normals = *normals;

	if (colors)
	{
		_colors = *colors;
		_hasVertexColors = true;
	}

	if (texCoords)
		_texCoords = *texCoords;
	if (tangents)
		_tangents = *tangents;
	if (bitangents)
		_bitangents = *bitangents;
	if (jointIndices)
		_jointIndices = *jointIndices;
	if (jointWeights)
		_jointWeights = *jointWeights;

	_memorySize = 0;
	_memorySize = (_points.size() + _normals.size() + _indices.size()) * sizeof(float);

	_nVerts = indices->empty()
		? static_cast<unsigned int>(points->size() / 3)
		: static_cast<unsigned int>(indices->size());

	_buffers.push_back(_indexBuffer);
	_indexBuffer.bind();
	_indexBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
	_indexBuffer.allocate(indices->data(), static_cast<int>(indices->size() * sizeof(unsigned int)));

	_buffers.push_back(_positionBuffer);
	_positionBuffer.bind();
	_positionBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
	_positionBuffer.allocate(points->data(), static_cast<int>(points->size() * sizeof(float)));

	_buffers.push_back(_normalBuffer);
	_normalBuffer.bind();
	_normalBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
	_normalBuffer.allocate(normals->data(), static_cast<int>(normals->size() * sizeof(float)));


	if(_colors.size())
	{
		_buffers.push_back(_colorBuffer);
		_colorBuffer.bind();
		_colorBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
		_colorBuffer.allocate(_colors.data(), static_cast<int>(_colors.size() * sizeof(float)));
		_memorySize += _colors.size() * sizeof(float);
	}

	if (_tangents.size())
	{
		_buffers.push_back(_tangentBuf);
		_tangentBuf.bind();
		_tangentBuf.setUsagePattern(QOpenGLBuffer::StaticDraw);
		_tangentBuf.allocate(_tangents.data(), static_cast<int>(_tangents.size() * sizeof(float)));
		_memorySize += _tangents.size() * sizeof(float);
	}

	if (_bitangents.size())
	{
		_buffers.push_back(_bitangentBuf);
		_bitangentBuf.bind();
		_bitangentBuf.setUsagePattern(QOpenGLBuffer::StaticDraw);
		_bitangentBuf.allocate(_bitangents.data(), static_cast<int>(_bitangents.size() * sizeof(float)));
		_memorySize += _bitangents.size() * sizeof(float);
	}

	if (_jointIndices.size())
	{
		_buffers.push_back(_jointIndexBuffer);
		_jointIndexBuffer.bind();
		_jointIndexBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
		_jointIndexBuffer.allocate(_jointIndices.data(), static_cast<int>(_jointIndices.size() * sizeof(float)));
		_memorySize += _jointIndices.size() * sizeof(float);
	}

	if (_jointWeights.size())
	{
		_buffers.push_back(_jointWeightBuffer);
		_jointWeightBuffer.bind();
		_jointWeightBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
		_jointWeightBuffer.allocate(_jointWeights.data(), static_cast<int>(_jointWeights.size() * sizeof(float)));
		_memorySize += _jointWeights.size() * sizeof(float);
	}

	_vertexArrayObject.bind();

	_indexBuffer.bind();

	// _position
	_positionBuffer.bind();
	//glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
	//glEnableVertexAttribArray(0);  // Vertex position
	_prog->enableAttributeArray("vertexPosition");
	_prog->setAttributeBuffer("vertexPosition", GL_FLOAT, 0, 3);

	// Normal
	_normalBuffer.bind();
	//glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, 0);
	//glEnableVertexAttribArray(1);  // Normal
	_prog->enableAttributeArray("vertexNormal");
	_prog->setAttributeBuffer("vertexNormal", GL_FLOAT, 0, 3);

	if (_hasVertexColors)
	{
		_colorBuffer.bind();
		_prog->enableAttributeArray("vertexColor");
		_prog->setAttributeBuffer("vertexColor", GL_FLOAT, 0, 4);
	}

	if (_tangents.size())
	{
		_tangentBuf.bind();
		//glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 0, 0);
		//glEnableVertexAttribArray(3);  // Tangents
		_prog->enableAttributeArray("vertexTangent");
		_prog->setAttributeBuffer("vertexTangent", GL_FLOAT, 0, 3);
	}

	if (_bitangents.size())
	{
		_bitangentBuf.bind();
		_prog->enableAttributeArray("vertexBitangent");
		_prog->setAttributeBuffer("vertexBitangent", GL_FLOAT, 0, 3);
	}

	if (_jointIndices.size())
	{
		_jointIndexBuffer.bind();
		_prog->enableAttributeArray("jointIndices");
		_prog->setAttributeBuffer("jointIndices", GL_FLOAT, 0, 4);
	}

	if (_jointWeights.size())
	{
		_jointWeightBuffer.bind();
		_prog->enableAttributeArray("jointWeights");
		_prog->setAttributeBuffer("jointWeights", GL_FLOAT, 0, 4);
	}

	// Tex coords
	if (_texCoords.size())
	{
		size_t numVertices = _indices.size() > 0 ? _indices.size() : 0;
		// If we're here, texCoords was populated with 8 floats per vertex

		// Extract 4 separate texCoord sets from the interleaved buffer
		std::vector<float> texCoord0, texCoord1, texCoord2, texCoord3;
		texCoord0.reserve(numVertices * 2);
		texCoord1.reserve(numVertices * 2);
		texCoord2.reserve(numVertices * 2);
		texCoord3.reserve(numVertices * 2);

		// Deinterleave: every 8 floats = one vertex's 4 sets
		for (size_t i = 0; i < _texCoords.size(); i += 8)
		{
			// Set 0
			texCoord0.push_back(_texCoords[i + 0]);
			texCoord0.push_back(_texCoords[i + 1]);
			// Set 1
			texCoord1.push_back(_texCoords[i + 2]);
			texCoord1.push_back(_texCoords[i + 3]);
			// Set 2
			texCoord2.push_back(_texCoords[i + 4]);
			texCoord2.push_back(_texCoords[i + 5]);
			// Set 3
			texCoord3.push_back(_texCoords[i + 6]);
			texCoord3.push_back(_texCoords[i + 7]);
		}

		// Create separate VBOs for each texCoord set
		_buffers.push_back(_texCoord0Buffer);
		_texCoord0Buffer.bind();
		_texCoord0Buffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
		_texCoord0Buffer.allocate(texCoord0.data(), static_cast<int>(texCoord0.size() * sizeof(float)));
		_memorySize += texCoord0.size() * sizeof(float);

		_buffers.push_back(_texCoord1Buffer);
		_texCoord1Buffer.bind();
		_texCoord1Buffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
		_texCoord1Buffer.allocate(texCoord1.data(), static_cast<int>(texCoord1.size() * sizeof(float)));
		_memorySize += texCoord1.size() * sizeof(float);

		_buffers.push_back(_texCoord2Buffer);
		_texCoord2Buffer.bind();
		_texCoord2Buffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
		_texCoord2Buffer.allocate(texCoord2.data(), static_cast<int>(texCoord2.size() * sizeof(float)));
		_memorySize += texCoord2.size() * sizeof(float);

		_buffers.push_back(_texCoord3Buffer);
		_texCoord3Buffer.bind();
		_texCoord3Buffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
		_texCoord3Buffer.allocate(texCoord3.data(), static_cast<int>(texCoord3.size() * sizeof(float)));
		_memorySize += texCoord3.size() * sizeof(float);

		// Set up vertex attributes (stride = 0 for each, since they're separate)
		_texCoord0Buffer.bind();
		_prog->enableAttributeArray("texCoord0");
		_prog->setAttributeBuffer("texCoord0", GL_FLOAT, 0, 2, 0);

		_texCoord1Buffer.bind();
		_prog->enableAttributeArray("texCoord1");
		_prog->setAttributeBuffer("texCoord1", GL_FLOAT, 0, 2, 0);

		_texCoord2Buffer.bind();
		_prog->enableAttributeArray("texCoord2");
		_prog->setAttributeBuffer("texCoord2", GL_FLOAT, 0, 2, 0);

		_texCoord3Buffer.bind();
		_prog->enableAttributeArray("texCoord3");
		_prog->setAttributeBuffer("texCoord3", GL_FLOAT, 0, 2, 0);

	}

	_vertexArrayObject.release();

	// Initialize picking triangles and bounds for the newly loaded geometry.
	_instanceState.updateRuntimeBounds(_points, _normals, _tangents, _bitangents, _indices);
}

void RenderableMesh::setProg(QOpenGLShaderProgram* prog)
{
	const bool progChanged = (_prog != prog);
	_prog = prog;
	if (progChanged)
	{
		recordMaterialDirtyBySetProg();
		clearUniformLocationCache();
	}

	if (_vaoConfiguredProgram != prog)
	{
		recordVaoProgramReconfigure();
		_vertexArrayObject.bind();

		//_indexBuffer.bind();

		// Position
		_positionBuffer.bind();
		_prog->enableAttributeArray("vertexPosition");
		_prog->setAttributeBuffer("vertexPosition", GL_FLOAT, 0, 3);

		// Normal
		_normalBuffer.bind();
		_prog->enableAttributeArray("vertexNormal");
		_prog->setAttributeBuffer("vertexNormal", GL_FLOAT, 0, 3);

		// Color
		if (_colors.size())
		{
			_hasVertexColors = true;
			_colorBuffer.bind();
			_prog->enableAttributeArray("vertexColor");
			_prog->setAttributeBuffer("vertexColor", GL_FLOAT, 0, 4);
		}

		// Tex coords
		if (_texCoords.size())
		{
			_texCoord0Buffer.bind();
			_prog->enableAttributeArray("texCoord0");
			_prog->setAttributeBuffer("texCoord0", GL_FLOAT, 0, 2);

			_texCoord1Buffer.bind();
			_prog->enableAttributeArray("texCoord1");
			_prog->setAttributeBuffer("texCoord1", GL_FLOAT, 0, 2);

			_texCoord2Buffer.bind();
			_prog->enableAttributeArray("texCoord2");
			_prog->setAttributeBuffer("texCoord2", GL_FLOAT, 0, 2);
		
			_texCoord3Buffer.bind();
			_prog->enableAttributeArray("texCoord3");
			_prog->setAttributeBuffer("texCoord3", GL_FLOAT, 0, 2);
		}

		// Tangents
		if (_tangents.size())
		{
			_tangentBuf.bind();
			_prog->enableAttributeArray("vertexTangent");
			_prog->setAttributeBuffer("vertexTangent", GL_FLOAT, 0, 3);
		}

		// Bitangents
		if (_bitangents.size())
		{
			_bitangentBuf.bind();
			_prog->enableAttributeArray("vertexBitangent");
			_prog->setAttributeBuffer("vertexBitangent", GL_FLOAT, 0, 3);
		}

		if (_jointIndices.size())
		{
			_jointIndexBuffer.bind();
			_prog->enableAttributeArray("jointIndices");
			_prog->setAttributeBuffer("jointIndices", GL_FLOAT, 0, 4);
		}

		if (_jointWeights.size())
		{
			_jointWeightBuffer.bind();
			_prog->enableAttributeArray("jointWeights");
			_prog->setAttributeBuffer("jointWeights", GL_FLOAT, 0, 4);
		}

		_vertexArrayObject.release();
		_vaoConfiguredProgram = prog;
	}

	if (progChanged)
		markUniformsDirty();
}

int RenderableMesh::uniformLocationCached(const char* name) const
{
	return uniformLocationCached(QByteArray(name));
}

int RenderableMesh::uniformLocationCached(const QByteArray& name) const
{
	if (!_prog)
		return -1;

	const auto it = _uniformLocationCache.constFind(name);
	if (it != _uniformLocationCache.constEnd())
		return it.value();

	const int location = _prog->uniformLocation(name.constData());
	_uniformLocationCache.insert(name, location);
	return location;
}

int RenderableMesh::uniformLocationCached(const QString& name) const
{
	return uniformLocationCached(name.toUtf8());
}

void RenderableMesh::clearUniformLocationCache()
{
	_uniformLocationCache.clear();
}

void RenderableMesh::setupTextures()
{
	const bool hasClearcoatColorTex = _material.hasClearcoatColorMap() || _material.clearcoatColorTextureId() != 0;
	const bool hasClearcoatRoughnessTex = _material.hasClearcoatRoughnessMap() || _material.clearcoatRoughnessTextureId() != 0;
	const bool hasClearcoatNormalTex = _material.hasClearcoatNormalMap() || _material.clearcoatNormalTextureId() != 0;
	const bool hasSpecularFactorTex = _material.hasSpecularFactorMap() || _material.specularFactorTextureId() != 0;
	const bool hasSpecularColorTex = _material.hasSpecularColorMap() || _material.specularColorTextureId() != 0;
	const bool hasAnisotropyTex = _material.hasAnisotropyMap() || _material.anisotropyTextureId() != 0;
	const bool hasIridescenceTex = _material.hasIridescenceMap() || _material.iridescenceTextureId() != 0;
	const bool hasIridescenceThicknessTex = _material.hasIridescenceThicknessMap() || _material.iridescenceThicknessTextureId() != 0;
	const bool hasSheenColorTex = _material.hasSheenColorMap() || _material.sheenColorTextureId() != 0;
	const bool hasSheenRoughnessTex = _material.hasSheenRoughnessMap() || _material.sheenRoughnessTextureId() != 0;
	const bool hasTransmissionTex = _material.hasTransmissionMap() || _material.transmissionTextureId() != 0;
	const bool hasIORTex = _material.hasIORMap() || _material.iorTextureId() != 0;
	const bool hasThicknessTex = _material.hasThicknessMap() || _material.thicknessTextureId() != 0;
	const bool hasDiffuseTransmissionTex = _material.hasDiffuseTransmissionMap() || _material.diffuseTransmissionTextureId() != 0;
	const bool hasDiffuseTransmissionColorTex = _material.hasDiffuseTransmissionColorMap() || _material.diffuseTransmissionColorTextureId() != 0;

	// Unit 10 is shared by three shader paths — ADS (texture_diffuse / texture_specular /
	// etc.), PBR metallic-roughness (albedoMap), and PBR specular-glossiness (diffuseMap) —
	// all of which sample unit 10 for the base/diffuse/albedo colour.
	// Fall back to the legacy diffuse texture for specular-glossiness materials that carry
	// hasDiffuseMap but not hasAlbedoMap; using only hasAlbedoMap here would leave unit 10
	// as 0 and black out the diffuse colour for those materials.
	const GLuint baseColorTex = _material.hasAlbedoMap()
		? static_cast<GLuint>(_material.albedoTextureId())
		: (_material.hasDiffuseMap() ? _material.diffuseTextureId() : 0U);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, _fallbackTexture);
	if (_textureBindingsDirty && !_fallbackTextureImage.isNull())
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _fallbackTextureImage.width(), _fallbackTextureImage.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, _fallbackTextureImage.bits());
		glGenerateMipmap(GL_TEXTURE_2D);
		_textureBindingsDirty = false;
	}

	// Texture maps (units 10–17): single unified bind — serves ADS, PBR metallic-roughness,
	// and PBR specular-glossiness simultaneously.  Previously there were two consecutive bind
	// blocks for the same units (ADS then PBR), with the PBR block overwriting the ADS one;
	// the ADS-only block obscured the specular-glossiness bug and has been removed.
	bindTextureUnitCached(GL_TEXTURE10, baseColorTex);
	bindTextureUnitCached(GL_TEXTURE11, _material.hasMetallicMap() ? static_cast<GLuint>(_material.metallicTextureId()) : 0U);
	bindTextureUnitCached(GL_TEXTURE12, _material.hasEmissiveMap() ? static_cast<GLuint>(_material.emissiveTextureId()) : 0U);
	bindTextureUnitCached(GL_TEXTURE13, _material.hasNormalMap() ? static_cast<GLuint>(_material.normalTextureId()) : 0U);
	bindTextureUnitCached(GL_TEXTURE14, _material.hasHeightMap() ? static_cast<GLuint>(_material.heightTextureId()) : 0U);
	bindTextureUnitCached(GL_TEXTURE15, _material.hasOpacityMap() ? static_cast<GLuint>(_material.opacityTextureId()) : 0U);
	bindTextureUnitCached(GL_TEXTURE16, _material.hasRoughnessMap() ? static_cast<GLuint>(_material.roughnessTextureId()) : 0U);
	bindTextureUnitCached(GL_TEXTURE17, _material.hasAOMap() ? static_cast<GLuint>(_material.occlusionTextureId()) : 0U);
	
	// Mesh-owned material block (units 10–29). These are the feature-complete
	// material slots we keep inside the guaranteed 0..31 range.
	bindTextureUnitCached(GL_TEXTURE18, hasClearcoatColorTex ? static_cast<GLuint>(_material.clearcoatColorTextureId()) : 0U);
	bindTextureUnitCached(GL_TEXTURE19, hasClearcoatRoughnessTex ? static_cast<GLuint>(_material.clearcoatRoughnessTextureId()) : 0U);
	bindTextureUnitCached(GL_TEXTURE20, hasClearcoatNormalTex ? static_cast<GLuint>(_material.clearcoatNormalTextureId()) : 0U);
	bindTextureUnitCached(GL_TEXTURE21, hasSpecularFactorTex ? static_cast<GLuint>(_material.specularFactorTextureId()) : 0U);
	bindTextureUnitCached(GL_TEXTURE22, hasSpecularColorTex ? static_cast<GLuint>(_material.specularColorTextureId()) : 0U);
	bindTextureUnitCached(GL_TEXTURE23, hasAnisotropyTex ? static_cast<GLuint>(_material.anisotropyTextureId()) : 0U);
	bindTextureUnitCached(GL_TEXTURE24, hasIridescenceTex ? static_cast<GLuint>(_material.iridescenceTextureId()) : 0U);
	bindTextureUnitCached(GL_TEXTURE25, hasIridescenceThicknessTex ? static_cast<GLuint>(_material.iridescenceThicknessTextureId()) : 0U);
	bindTextureUnitCached(GL_TEXTURE26, hasSheenColorTex ? static_cast<GLuint>(_material.sheenColorTextureId()) : 0U);
	bindTextureUnitCached(GL_TEXTURE27, hasSheenRoughnessTex ? static_cast<GLuint>(_material.sheenRoughnessTextureId()) : 0U);
	bindTextureUnitCached(GL_TEXTURE28, hasTransmissionTex ? static_cast<GLuint>(_material.transmissionTextureId()) : 0U);
	bindTextureUnitCached(GL_TEXTURE29, hasIORTex ? static_cast<GLuint>(_material.iorTextureId()) : 0U);

	// Overflow material bundles (units 34+).
	if (hasDiffuseTransmissionTex) {
		bindTextureUnitCached(GL_TEXTURE0 + 34, static_cast<GLuint>(_material.diffuseTransmissionTextureId()));
	}
	if (hasDiffuseTransmissionColorTex) {
		bindTextureUnitCached(GL_TEXTURE0 + 35, static_cast<GLuint>(_material.diffuseTransmissionColorTextureId()));
	}
	if (hasThicknessTex) {
		bindTextureUnitCached(GL_TEXTURE30, static_cast<GLuint>(_material.thicknessTextureId()));
	}
}

void RenderableMesh::setupUniforms()
{
	if (!_uniformsDirty) return;
	_prog->bind();
	const bool hasTransmissionTex = _material.hasTransmissionMap() || _material.transmissionTextureId() != 0;
	const bool hasIORTex = _material.hasIORMap() || _material.iorTextureId() != 0;
	const bool hasSheenColorTex = _material.hasSheenColorMap() || _material.sheenColorTextureId() != 0;
	const bool hasSheenRoughnessTex = _material.hasSheenRoughnessMap() || _material.sheenRoughnessTextureId() != 0;
	const bool hasClearcoatColorTex = _material.hasClearcoatColorMap() || _material.clearcoatColorTextureId() != 0;
	const bool hasClearcoatRoughnessTex = _material.hasClearcoatRoughnessMap() || _material.clearcoatRoughnessTextureId() != 0;
	const bool hasClearcoatNormalTex = _material.hasClearcoatNormalMap() || _material.clearcoatNormalTextureId() != 0;
	const bool hasSpecularFactorTex = _material.hasSpecularFactorMap() || _material.specularFactorTextureId() != 0;
	const bool hasSpecularColorTex = _material.hasSpecularColorMap() || _material.specularColorTextureId() != 0;
	const bool hasAnisotropyTex = _material.hasAnisotropyMap() || _material.anisotropyTextureId() != 0;
	const bool hasIridescenceTex = _material.hasIridescenceMap() || _material.iridescenceTextureId() != 0;
	const bool hasIridescenceThicknessTex = _material.hasIridescenceThicknessMap() || _material.iridescenceThicknessTextureId() != 0;
	const bool hasThicknessTex = _material.hasThicknessMap() || _material.thicknessTextureId() != 0;
	const bool hasDiffuseTransmissionTex = _material.hasDiffuseTransmissionMap() || _material.diffuseTransmissionTextureId() != 0;
	const bool hasDiffuseTransmissionColorTex = _material.hasDiffuseTransmissionColorMap() || _material.diffuseTransmissionColorTextureId() != 0;
	GLint modeValue = 0;
	switch (_primitiveMode)
	{
	case GL_POINTS:         modeValue = 0; break;
	case GL_LINES:          modeValue = 1; break;
	case GL_LINE_LOOP:      modeValue = 2; break;
	case GL_LINE_STRIP:     modeValue = 3; break;
	case GL_TRIANGLES:      modeValue = 4; break;
	case GL_TRIANGLE_STRIP: modeValue = 5; break;
	case GL_TRIANGLE_FAN:   modeValue = 6; break;
	default:                modeValue = 4; break;
	}
	_prog->setUniformValue("primitiveMode", modeValue);
	_prog->setUniformValue("hasVertexColors", _hasVertexColors);
	_prog->setUniformValue("hasNegativeScale", hasNegativeScale());
	_prog->setUniformValue("material.ambient", _material.ambient());
	// For specular-glossiness materials the authoritative diffuse colour is
	// diffuseColorFactor (stored in _diffuseColor / diffuseColor()), not the
	// albedo-derived _diffuse.  Mirror the same logic used for diffuseFactor in
	// the PBR path: use (1,1,1) when a diffuse texture is present (the texture
	// provides the colour), otherwise use the factor directly.  Without this,
	// ADS mode multiplies the correctly bound diffuse texture by the wrong colour
	// and the specular-glossiness material renders black.
	const QVector3D adsDiffuse = _material.getUseSpecularGlossiness()
		? (_material.hasDiffuseMap() ? QVector3D(1.0f, 1.0f, 1.0f) : _material.diffuseColor())
		: _material.diffuse();
	_prog->setUniformValue("material.diffuse", adsDiffuse);
	_prog->setUniformValue("material.specular", _material.specular());
	_prog->setUniformValue("material.emission", _material.emissive());
	_prog->setUniformValue("material.shininess", _material.shininess());
	_prog->setUniformValue("material.metallic", _material.metallic());
	_prog->setUniformValue("opacity", _material.opacity());
	// ADS light texture maps
	_prog->setUniformValue("hasDiffuseTexture", _material.hasAlbedoMap() || _material.hasDiffuseMap());
	_prog->setUniformValue("hasSpecularTexture", _material.hasMetallicMap());
	_prog->setUniformValue("hasEmissiveTexture", _material.hasEmissiveMap());
	_prog->setUniformValue("hasNormalTexture", _material.hasNormalMap());
	_prog->setUniformValue("hasHeightTexture", _material.hasHeightMap());
	_prog->setUniformValue("hasOpacityTexture", _material.hasOpacityMap());
	_prog->setUniformValue("opacityTextureInverted", _material.isOpacityMapInverted());

	_prog->setUniformValue("texture_diffuse", 10);
	_prog->setUniformValue("texture_specular", 11);
	_prog->setUniformValue("texture_emissive", 12);
	_prog->setUniformValue("texture_normal", 13);
	_prog->setUniformValue("texture_height", 14);
	_prog->setUniformValue("texture_opacity", 15);
	// PBR Direct Lighting
	_prog->setUniformValue("pbrLighting.albedo", _material.albedoColor());
	_prog->setUniformValue("pbrLighting.metallic", _material.metalness());
	_prog->setUniformValue("pbrLighting.roughness", _material.roughness());
	_prog->setUniformValue("pbrLighting.normalScale", _material.normalScale());
	_prog->setUniformValue("pbrLighting.ambientOcclusion", 1.0f);
	_prog->setUniformValue("pbrLighting.occlusionStrength", _material.occlusionStrength());
	_prog->setUniformValue("pbrLighting.transmission", _material.transmission());
	_prog->setUniformValue("pbrLighting.ior", _material.ior());
	_prog->setUniformValue("pbrLighting.sheenColor", _material.sheenColor());
	_prog->setUniformValue("pbrLighting.sheenRoughness", _material.sheenRoughness());
	_prog->setUniformValue("pbrLighting.clearcoat", _material.clearcoat());
	_prog->setUniformValue("pbrLighting.clearcoatRoughness", _material.clearcoatRoughness());

	// Alpha transparency mode and cuttoff
	_prog->setUniformValue("alphaThreshold", _material.alphaThreshold());
	_prog->setUniformValue("blendMode", static_cast<int>(_material.blendMode()));

	_prog->setUniformValue("twoSided", _material.twoSided());
	
	// PBR Texture Maps
	_prog->setUniformValue("albedoMap", 10);
	_prog->setUniformValue("metallicMap", 11);
	_prog->setUniformValue("emissiveMap", 12);
	_prog->setUniformValue("normalMap", 13);
	_prog->setUniformValue("heightMap", 14);
	_prog->setUniformValue("opacityMap", 15);
	_prog->setUniformValue("roughnessMap", 16);
	_prog->setUniformValue("aoMap", 17);

	// Upload channel-packing parameters so the shader reads the correct channel
	// with the correct scale/bias from each packed texture (e.g. ORM: R=AO, G=roughness, B=metallic).
	// Material initialises sensible defaults (roughness→G, metallic→B, AO→R, all scale=1)
	// and detectAndAssignPacking() updates them when multiple maps share the same file.
	// Without this upload the GLSL uniforms zero-initialise (scale=0) and texture
	// contributions are silently discarded, making all textured materials appear roughness≈0.
	{
		const auto& rp = _material.packingFor("roughness");
		_prog->setUniformValue("roughnessChannel", rp.channel);
		_prog->setUniformValue("roughnessInvert",  (int)rp.invert);
		_prog->setUniformValue("roughnessScale",   rp.scale);
		_prog->setUniformValue("roughnessBias",    rp.bias);

		const auto& mp = _material.packingFor("metallic");
		_prog->setUniformValue("metallicChannel", mp.channel);
		_prog->setUniformValue("metallicInvert",  (int)mp.invert);
		_prog->setUniformValue("metallicScale",   mp.scale);
		_prog->setUniformValue("metallicBias",    mp.bias);

		const auto& ap = _material.packingFor("ao");
		_prog->setUniformValue("aoChannel", ap.channel);
		_prog->setUniformValue("aoInvert",  (int)ap.invert);
		_prog->setUniformValue("aoScale",   ap.scale);
		_prog->setUniformValue("aoBias",    ap.bias);

		const auto& op = _material.packingFor("opacity");
		_prog->setUniformValue("opacityChannel", op.channel);
		_prog->setUniformValue("opacityInvert",  (int)op.invert);
		_prog->setUniformValue("opacityScale",   op.scale);
		_prog->setUniformValue("opacityBias",    op.bias);
	}
	// Advanced PBR Maps
	_prog->setUniformValue("clearcoatColorMap", 18);
	_prog->setUniformValue("clearcoatRoughnessMap", 19);
	_prog->setUniformValue("clearcoatNormalMap", 20);
	_prog->setUniformValue("specularFactorMap", 21);
	_prog->setUniformValue("specularColorMap", 22);
	_prog->setUniformValue("anisotropyMap", 23);
	_prog->setUniformValue("iridescenceMap", 24);
	_prog->setUniformValue("iridescenceThicknessMap", 25);
	_prog->setUniformValue("sheenColorMap", 26);
	_prog->setUniformValue("sheenRoughnessMap", 27);
	_prog->setUniformValue("transmissionMap", 28);
	_prog->setUniformValue("iorMap", 29);

	// KHR_materials_specular
	_prog->setUniformValue("hasSpecularFactorMap", hasSpecularFactorTex);
	_prog->setUniformValue("hasSpecularColorMap", hasSpecularColorTex);

	// KHR_materials_pbrSpecularGlossiness
	_prog->setUniformValue("diffuseMap", 10);
	_prog->setUniformValue("specularGlossinessMap", 21);
	_prog->setUniformValue("hasDiffuseMap", _material.hasDiffuseMap());
	_prog->setUniformValue("hasSpecularGlossinessMap", _material.hasSpecularGlossinessMap());
	_prog->setUniformValue("useSpecularGlossiness", _material.getUseSpecularGlossiness());

	// KHR_materials_pbrSpecularGlossiness - Factor values
	QVector3D diffuseFactor = _material.hasDiffuseMap() ?
		QVector3D(1.0f, 1.0f, 1.0f) : _material.diffuseColor();
	QVector3D specularFactor = _material.hasSpecularGlossinessMap() ?
		QVector3D(1.0f, 1.0f, 1.0f) : _material.specularColor();
	float glossinessFactor = _material.glossinessFactor();

	_prog->setUniformValue("diffuseFactor", diffuseFactor);
	_prog->setUniformValue("specularColor", specularFactor);
	_prog->setUniformValue("glossinessFactor", glossinessFactor);

	// KHR_materials_anisotropy
	_prog->setUniformValue("anisotropyMap", 23);
	_prog->setUniformValue("hasAnisotropyMap", hasAnisotropyTex);

	// KHR_materials_iridescence
	_prog->setUniformValue("iridescenceMap", 24);
	_prog->setUniformValue("hasIridescenceMap", hasIridescenceTex);

	_prog->setUniformValue("iridescenceThicknessMap", 25);
	_prog->setUniformValue("hasIridescenceThicknessMap", hasIridescenceThicknessTex);

	// KHR_materials_volume
	_prog->setUniformValue("thicknessMap", 30);
	_prog->setUniformValue("hasThicknessMap", hasThicknessTex);
	_prog->setUniformValue("hasThicknessAlpha", _material.hasThicknessAlpha());

	// KHR_materials_scattering
	_prog->setUniformValue("multiScatterColor", _material.multiScatterColor());
	_prog->setUniformValue("hasVolumeScattering", _material.hasVolumeScattering());

	// KHR_materials_specular (2 uniforms)
	_prog->setUniformValue("pbrLighting.specularFactor",
		_material.specularFactor());
	_prog->setUniformValue("pbrLighting.specularColorFactor",
		_material.specularColorFactor());

	// KHR_materials_anisotropy (2 uniforms)
	_prog->setUniformValue("pbrLighting.anisotropyStrength",
		_material.anisotropyStrength());
	_prog->setUniformValue("pbrLighting.anisotropyRotation",
		_material.anisotropyRotation());

	// KHR_materials_iridescence (4 uniforms)
	_prog->setUniformValue("pbrLighting.iridescenceFactor",
		_material.iridescenceFactor());
	_prog->setUniformValue("pbrLighting.iridescenceIor",
		_material.iridescenceIor());
	_prog->setUniformValue("pbrLighting.iridescenceThicknessMin",
		_material.iridescenceThicknessMin());
	_prog->setUniformValue("pbrLighting.iridescenceThicknessMax",
		_material.iridescenceThicknessMax());

	// KHR_materials_volume (3 uniforms)
	_prog->setUniformValue("pbrLighting.thicknessFactor",
		_material.thicknessFactor());
	_prog->setUniformValue("pbrLighting.attenuationDistance",
		_material.attenuationDistance());
	_prog->setUniformValue("pbrLighting.attenuationColor",
		_material.attenuationColor());

	// KHR_materials_dispersion (1 uniform)
	_prog->setUniformValue("pbrLighting.dispersion",
		_material.dispersion());

	// KHR_materials_unlit (1 uniform)
	_prog->setUniformValue("pbrLighting.unlit",
		_material.isUnlit());

	// KHR_materials_emissive_strength (1 uniform)
	_prog->setUniformValue("pbrLighting.emissiveStrength",
		_material.emissiveStrength());

	// KHR_materials_diffues_transmission
	_prog->setUniformValue("pbrLighting.diffuseTransmissionFactor",
		_material.diffuseTransmissionFactor());
	_prog->setUniformValue("pbrLighting.diffuseTransmissionColorFactor",
		_material.diffuseTransmissionColorFactor());
	// Diffuse Transmission Map
	_prog->setUniformValue("hasDiffuseTransmissionMap",
		hasDiffuseTransmissionTex);
	_prog->setUniformValue("diffuseTransmissionMap", 34);
	
	// Diffuse Transmission Color Map
	_prog->setUniformValue("hasDiffuseTransmissionColorMap",
		hasDiffuseTransmissionColorTex);
	_prog->setUniformValue("diffuseTransmissionColorMap", 35);

	// Texture transform uniforms
	_prog->setUniformValue("albedoTexTransform.texCoordIndex", _material.albedoTexCoord());
	_prog->setUniformValue("albedoTexTransform.offset", _material.albedoTexOffset());
	_prog->setUniformValue("albedoTexTransform.scale", _material.albedoTexScale());
	_prog->setUniformValue("albedoTexTransform.rotation", _material.albedoTexRotation());

	// Legacy diffuse map transform
	_prog->setUniformValue("diffuseTextureTransform.texCoordIndex", _material.albedoTexCoord());
	_prog->setUniformValue("diffuseTextureTransform.offset", _material.albedoTexOffset());
	_prog->setUniformValue("diffuseTextureTransform.scale", _material.albedoTexScale());
	_prog->setUniformValue("diffuseTextureTransform.rotation", _material.albedoTexRotation());

	// Metallic map transform
	_prog->setUniformValue("metallicTexTransform.texCoordIndex", _material.metallicTexCoord());
	_prog->setUniformValue("metallicTexTransform.offset", _material.metallicTexOffset());
	_prog->setUniformValue("metallicTexTransform.scale", _material.metallicTexScale());
	_prog->setUniformValue("metallicTexTransform.rotation", _material.metallicTexRotation());

	// Legacy specular map transform
	_prog->setUniformValue("specularTextureTransform.texCoordIndex", _material.metallicTexCoord());
	_prog->setUniformValue("specularTextureTransform.offset", _material.metallicTexOffset());
	_prog->setUniformValue("specularTextureTransform.scale", _material.metallicTexScale());
	_prog->setUniformValue("specularTextureTransform.rotation", _material.metallicTexRotation());

	// Roughness map transform
	_prog->setUniformValue("roughnessTexTransform.texCoordIndex", _material.roughnessTexCoord());
	_prog->setUniformValue("roughnessTexTransform.offset", _material.roughnessTexOffset());
	_prog->setUniformValue("roughnessTexTransform.scale", _material.roughnessTexScale());
	_prog->setUniformValue("roughnessTexTransform.rotation", _material.roughnessTexRotation());

	// Normal map transform
	_prog->setUniformValue("normalTexTransform.texCoordIndex", _material.normalTexCoord());
	_prog->setUniformValue("normalTexTransform.offset", _material.normalTexOffset());
	_prog->setUniformValue("normalTexTransform.scale", _material.normalTexScale());
	_prog->setUniformValue("normalTexTransform.rotation", _material.normalTexRotation());

	// Legacy normal map transform
	_prog->setUniformValue("normalTextureTransform.texCoordIndex", _material.normalTexCoord());
	_prog->setUniformValue("normalTextureTransform.offset", _material.normalTexOffset());
	_prog->setUniformValue("normalTextureTransform.scale", _material.normalTexScale());
	_prog->setUniformValue("normalTextureTransform.rotation", _material.normalTexRotation());

	// Occlusion map transform
	_prog->setUniformValue("aoTexTransform.texCoordIndex", _material.occlusionTexCoord());
	_prog->setUniformValue("aoTexTransform.offset", _material.occlusionTexOffset());
	_prog->setUniformValue("aoTexTransform.scale", _material.occlusionTexScale());
	_prog->setUniformValue("aoTexTransform.rotation", _material.occlusionTexRotation());

	// Emissive map transform
	_prog->setUniformValue("emissiveTexTransform.texCoordIndex", _material.emissiveTexCoord());
	_prog->setUniformValue("emissiveTexTransform.offset", _material.emissiveTexOffset());
	_prog->setUniformValue("emissiveTexTransform.scale", _material.emissiveTexScale());
	_prog->setUniformValue("emissiveTexTransform.rotation", _material.emissiveTexRotation());

	// Legacy emissive map transform
	_prog->setUniformValue("emissiveTextureTransform.texCoordIndex", _material.emissiveTexCoord());
	_prog->setUniformValue("emissiveTextureTransform.offset", _material.emissiveTexOffset());
	_prog->setUniformValue("emissiveTextureTransform.scale", _material.emissiveTexScale());
	_prog->setUniformValue("emissiveTextureTransform.rotation", _material.emissiveTexRotation());

	// Height map transform
	_prog->setUniformValue("heightTexTransform.texCoordIndex", _material.heightTexCoord());
	_prog->setUniformValue("heightTexTransform.offset", _material.heightTexOffset());
	_prog->setUniformValue("heightTexTransform.scale", _material.heightTexScale());
	_prog->setUniformValue("heightTexTransform.rotation", _material.heightTexRotation());

	// Legacy height map transform
	_prog->setUniformValue("heightTextureTransform.texCoordIndex", _material.heightTexCoord());
	_prog->setUniformValue("heightTextureTransform.offset", _material.heightTexOffset());
	_prog->setUniformValue("heightTextureTransform.scale", _material.heightTexScale());
	_prog->setUniformValue("heightTextureTransform.rotation", _material.heightTexRotation());

	// Opacity map transform
	_prog->setUniformValue("opacityTexTransform.texCoordIndex", _material.opacityTexCoord());
	_prog->setUniformValue("opacityTexTransform.offset", _material.opacityTexOffset());
	_prog->setUniformValue("opacityTexTransform.scale", _material.opacityTexScale());
	_prog->setUniformValue("opacityTexTransform.rotation", _material.opacityTexRotation());

	// Legacy opacity map transform
	_prog->setUniformValue("opacityTextureTransform.texCoordIndex", _material.opacityTexCoord());
	_prog->setUniformValue("opacityTextureTransform.offset", _material.opacityTexOffset());
	_prog->setUniformValue("opacityTextureTransform.scale", _material.opacityTexScale());
	_prog->setUniformValue("opacityTextureTransform.rotation", _material.opacityTexRotation());

	// Transmission map transform
	_prog->setUniformValue("transmissionTexTransform.texCoordIndex", _material.transmissionTexCoord());
	_prog->setUniformValue("transmissionTexTransform.offset", _material.transmissionTexOffset());
	_prog->setUniformValue("transmissionTexTransform.scale", _material.transmissionTexScale());
	_prog->setUniformValue("transmissionTexTransform.rotation", _material.transmissionTexRotation());

	// IOR map transform
	_prog->setUniformValue("iorTexTransform.texCoordIndex", _material.iorTexCoord());
	_prog->setUniformValue("iorTexTransform.offset", _material.iorTexOffset());
	_prog->setUniformValue("iorTexTransform.scale", _material.iorTexScale());
	_prog->setUniformValue("iorTexTransform.rotation", _material.iorTexRotation());

	// Sheen map transforms
	_prog->setUniformValue("sheenColorTexTransform.texCoordIndex", _material.sheenColorTexCoord());
	_prog->setUniformValue("sheenColorTexTransform.offset", _material.sheenColorTexOffset());
	_prog->setUniformValue("sheenColorTexTransform.scale", _material.sheenColorTexScale());
	_prog->setUniformValue("sheenColorTexTransform.rotation", _material.sheenColorTexRotation());

	// Sheen roughness map transform
	_prog->setUniformValue("sheenRoughnessTexTransform.texCoordIndex", _material.sheenRoughnessTexCoord());
	_prog->setUniformValue("sheenRoughnessTexTransform.offset", _material.sheenRoughnessTexOffset());
	_prog->setUniformValue("sheenRoughnessTexTransform.scale", _material.sheenRoughnessTexScale());
	_prog->setUniformValue("sheenRoughnessTexTransform.rotation", _material.sheenRoughnessTexRotation());

	// Clearcoat map transforms
	_prog->setUniformValue("clearcoatTexTransform.texCoordIndex", _material.clearcoatColorTexCoord());
	_prog->setUniformValue("clearcoatTexTransform.offset", _material.clearcoatColorTexOffset());
	_prog->setUniformValue("clearcoatTexTransform.scale", _material.clearcoatColorTexScale());
	_prog->setUniformValue("clearcoatTexTransform.rotation", _material.clearcoatColorTexRotation());

	// Clearcoat roughness map transform
	_prog->setUniformValue("clearcoatRoughnessTexTransform.texCoordIndex", _material.clearcoatRoughnessTexCoord());
	_prog->setUniformValue("clearcoatRoughnessTexTransform.offset", _material.clearcoatRoughnessTexOffset());
	_prog->setUniformValue("clearcoatRoughnessTexTransform.scale", _material.clearcoatRoughnessTexScale());
	_prog->setUniformValue("clearcoatRoughnessTexTransform.rotation", _material.clearcoatRoughnessTexRotation());

	// Clearcoat normal map transform
	_prog->setUniformValue("clearcoatNormalTexTransform.texCoordIndex", _material.clearcoatNormalTexCoord());
	_prog->setUniformValue("clearcoatNormalTexTransform.offset", _material.clearcoatNormalTexOffset());
	_prog->setUniformValue("clearcoatNormalTexTransform.scale", _material.clearcoatNormalTexScale());
	_prog->setUniformValue("clearcoatNormalTexTransform.rotation", _material.clearcoatNormalTexRotation());

	// KHR_materials_specular
	_prog->setUniformValue("specularFactorTexTransform.texCoordIndex", _material.specularFactorTexCoord());
	_prog->setUniformValue("specularFactorTexTransform.offset", _material.specularFactorTexOffset());
	_prog->setUniformValue("specularFactorTexTransform.scale", _material.specularFactorTexScale());
	_prog->setUniformValue("specularFactorTexTransform.rotation", _material.specularFactorTexRotation());
	
	// KHR_materials_specular
	_prog->setUniformValue("specularColorTexTransform.texCoordIndex", _material.specularColorTexCoord());
	_prog->setUniformValue("specularColorTexTransform.offset", _material.specularColorTexOffset());
	_prog->setUniformValue("specularColorTexTransform.scale", _material.specularColorTexScale());
	_prog->setUniformValue("specularColorTexTransform.rotation", _material.specularColorTexRotation());

	// KHR_materials_pbrSpecularGlossiness - Diffuse map transform
	_prog->setUniformValue("diffuseTexTransform.texCoordIndex", _material.diffuseTexCoord());
	_prog->setUniformValue("diffuseTexTransform.offset", _material.diffuseTexOffset());
	_prog->setUniformValue("diffuseTexTransform.scale", _material.diffuseTexScale());
	_prog->setUniformValue("diffuseTexTransform.rotation", _material.diffuseTexRotation());

	// KHR_materials_pbrSpecularGlossiness - Specular-Glossiness map transform
	_prog->setUniformValue("specularGlossinessTexTransform.texCoordIndex", _material.specularGlossinessTexCoord());
	_prog->setUniformValue("specularGlossinessTexTransform.offset", _material.specularGlossinessTexOffset());
	_prog->setUniformValue("specularGlossinessTexTransform.scale", _material.specularGlossinessTexScale());
	_prog->setUniformValue("specularGlossinessTexTransform.rotation", _material.specularGlossinessTexRotation());

	// KHR_materials_anisotropy
	_prog->setUniformValue("anisotropyTexTransform.texCoordIndex", _material.anisotropyTexCoord());
	_prog->setUniformValue("anisotropyTexTransform.offset", _material.anisotropyTexOffset());
	_prog->setUniformValue("anisotropyTexTransform.scale", _material.anisotropyTexScale());
	_prog->setUniformValue("anisotropyTexTransform.rotation", _material.anisotropyTexRotation());

	// KHR_materials_iridescence
	_prog->setUniformValue("iridescenceTexTransform.texCoordIndex", _material.iridescenceTexCoord());
	_prog->setUniformValue("iridescenceTexTransform.offset", _material.iridescenceTexOffset());
	_prog->setUniformValue("iridescenceTexTransform.scale", _material.iridescenceTexScale());
	_prog->setUniformValue("iridescenceTexTransform.rotation", _material.iridescenceTexRotation());
	
	// KHR_materials_iridescence
	_prog->setUniformValue("iridescenceThicknessTexTransform.texCoordIndex", _material.iridescenceThicknessTexCoord());
	_prog->setUniformValue("iridescenceThicknessTexTransform.offset", _material.iridescenceThicknessTexOffset());
	_prog->setUniformValue("iridescenceThicknessTexTransform.scale", _material.iridescenceThicknessTexScale());
	_prog->setUniformValue("iridescenceThicknessTexTransform.rotation", _material.iridescenceThicknessTexRotation());
	
	// KHR_materials_volume
	_prog->setUniformValue("thicknessTexTransform.texCoordIndex", _material.thicknessTexCoord());
	_prog->setUniformValue("thicknessTexTransform.offset", _material.thicknessTexOffset());
	_prog->setUniformValue("thicknessTexTransform.scale", _material.thicknessTexScale());
	_prog->setUniformValue("thicknessTexTransform.rotation", _material.thicknessTexRotation());

	// KHR_materials_diffues_transmission
	_prog->setUniformValue("diffuseTransmissionTexTransform.texCoordIndex", _material.diffuseTransmissionTexCoord());
	_prog->setUniformValue("diffuseTransmissionTexTransform.offset", _material.diffuseTransmissionTexOffset());
	_prog->setUniformValue("diffuseTransmissionTexTransform.scale", _material.diffuseTransmissionTexScale());
	_prog->setUniformValue("diffuseTransmissionTexTransform.rotation", _material.diffuseTransmissionTexRotation());
	_prog->setUniformValue("diffuseTransmissionColorTexTransform.texCoordIndex", _material.diffuseTransmissionColorTexCoord());
	_prog->setUniformValue("diffuseTransmissionColorTexTransform.offset", _material.diffuseTransmissionColorTexOffset());
	_prog->setUniformValue("diffuseTransmissionColorTexTransform.scale", _material.diffuseTransmissionColorTexScale());
	_prog->setUniformValue("diffuseTransmissionColorTexTransform.rotation", _material.diffuseTransmissionColorTexRotation());


	_prog->setUniformValue("heightScale", _material.heightScale());
	_prog->setUniformValue("clearcoatNormalScale", _material.clearcoatNormalScale());
	_prog->setUniformValue("hasAlbedoMap", _material.hasAlbedoMap());
	_prog->setUniformValue("hasMetallicMap", _material.hasMetallicMap());
	_prog->setUniformValue("hasEmissiveMap", _material.hasEmissiveMap());
	_prog->setUniformValue("hasRoughnessMap", _material.hasRoughnessMap());
	_prog->setUniformValue("hasNormalMap", _material.hasNormalMap());
	_prog->setUniformValue("hasAOMap", _material.hasAOMap());
	_prog->setUniformValue("hasOpacityMap", _material.hasOpacityMap());
	_prog->setUniformValue("opacityMapInverted", _material.isOpacityMapInverted());
	_prog->setUniformValue("hasHeightMap", _material.hasHeightMap());
	_prog->setUniformValue("hasTransmissionMap", hasTransmissionTex);
	_prog->setUniformValue("hasIORMap", hasIORTex);
	_prog->setUniformValue("hasSheenColorMap", hasSheenColorTex);
	_prog->setUniformValue("hasSheenRoughnessMap", hasSheenRoughnessTex);
	_prog->setUniformValue("hasClearcoatMap", hasClearcoatColorTex);
	_prog->setUniformValue("hasClearcoatRoughnessMap", hasClearcoatRoughnessTex);
	_prog->setUniformValue("hasClearcoatNormalMap", hasClearcoatNormalTex);

	// Extension-presence flags for debug channel gating (mirrors Khronos #ifdef MATERIAL_XXX).
	// True when the extension is active regardless of whether a texture is present.
	_prog->setUniformValue("extClearcoat",    _material.hasClearcoat()
	                                              || hasClearcoatColorTex
	                                              || hasClearcoatRoughnessTex
	                                              || hasClearcoatNormalTex);
	_prog->setUniformValue("extSheen",        _material.hasSheen()
	                                              || hasSheenColorTex
	                                              || hasSheenRoughnessTex);
	_prog->setUniformValue("extTransmission", _material.hasTransmission()
	                                              || hasTransmissionTex);
	_prog->setUniformValue("extSpecular",     hasSpecularFactorTex
	                                              || hasSpecularColorTex
	                                              || _material.specularFactor() != 1.0f
	                                              || _material.specularColorFactor() != QVector3D(1.0f, 1.0f, 1.0f));
	_prog->setUniformValue("extAnisotropy",   _material.anisotropyStrength() != 0.0f
	                                              || hasAnisotropyTex);
	_prog->setUniformValue("extIridescence",  _material.iridescenceFactor() > 0.0f
	                                              || hasIridescenceTex
	                                              || hasIridescenceThicknessTex);
	_prog->setUniformValue("extVolume",       _material.thicknessFactor() > 0.0f
	                                              || hasThicknessTex
	                                              || _material.hasVolumeScattering());
	_prog->setUniformValue("extDiffuseTrans", _material.diffuseTransmissionFactor() > 0.0f
	                                              || hasDiffuseTransmissionTex
	                                              || hasDiffuseTransmissionColorTex);

	_prog->setUniformValue("isGLTFMaterial", _material.isGLTFMaterial());

	// send channel-packing uniforms now that samplers are bound to units
	// Uniform naming: <base>Channel, <base>Invert, <base>Scale, <base>Bias
	auto sendPackingUniform = [this](const QString& key, const char* base) {
		Material::ChannelPacking p = _material.packingFor(key);
		int ch = p.channel;
		if (ch < -1) ch = -1;
		if (ch > 3) ch = 3;

		// Note: _prog must be bound at this point (it is in render())
		const QByteArray channelName = QString("%1Channel").arg(base).toUtf8();
		const QByteArray invertName = QString("%1Invert").arg(base).toUtf8();
		const QByteArray scaleName = QString("%1Scale").arg(base).toUtf8();
		const QByteArray biasName = QString("%1Bias").arg(base).toUtf8();

		int locChan = uniformLocationCached(channelName);
		if (locChan != -1) _prog->setUniformValue(locChan, ch);

		int locInv = uniformLocationCached(invertName);
		if (locInv != -1) _prog->setUniformValue(locInv, p.invert ? 1 : 0);

		int locScale = uniformLocationCached(scaleName);
		if (locScale != -1) _prog->setUniformValue(locScale, p.scale);

		int locBias = uniformLocationCached(biasName);
		if (locBias != -1) _prog->setUniformValue(locBias, p.bias);
		};

	// send for all packable maps
	sendPackingUniform("metallic", "metallic");
	sendPackingUniform("roughness", "roughness");
	sendPackingUniform("ao", "ao");
	sendPackingUniform("opacity", "opacity");

	_prog->setUniformValue("selected", isSelected());
	_uniformsDirty = false;
}

void RenderableMesh::invertOpacityADSMap(bool invert)
{
	_material.setInvertOpacityMap(invert);
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::setOpacityADSMap(unsigned int opacityTex)
{
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::setHeightADSMap(unsigned int heightTex)
{
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::setNormalADSMap(unsigned int normalTex)
{
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::setSpecularADSMap(unsigned int specularTex)
{
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::setEmissiveADSMap(unsigned int emissiveTex)
{
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::setDiffuseADSMap(unsigned int diffuseTex)
{
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::clearDiffuseADSMap()
{
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::clearSpecularADSMap()
{
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::clearEmissiveADSMap()
{
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::clearNormalADSMap()
{
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::clearHeightADSMap()
{
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::clearOpacityADSMap()
{
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::clearAllADSMaps()
{
	markTexturesDirty();
	markUniformsDirty();
}

Material RenderableMesh::getMaterial() const
{
	return _material;
}

void RenderableMesh::setMaterial(const Material& material)
{
	_material = material;
	cacheBaseVolumeProperties();
	applyScaledVolumeProperties();
	markUniformsDirty();
}

void RenderableMesh::setTextureMaps(const Material& material)
{
	// Resolved Material instances can carry shared texture ids from ViewportWidget's
	// cache. Treat this call as a state sync only; deleting/recreating ids here
	// can invalidate the very cached textures we are about to keep using.
	_material = material;
	cacheBaseVolumeProperties();
	applyScaledVolumeProperties();

	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::renderWireframeFast(QOpenGLShaderProgram* /*wireProg*/)
{
	// Base fallback: full render. SceneMesh overrides with the lightweight path.
	render();
}

void RenderableMesh::render()
{
	if (!_vertexArrayObject.isCreated())
		return;

	const QMatrix4x4& globalModelMatrix = currentGlobalModelMatrix();
	const QMatrix4x4 modelMatrix = globalModelMatrix * combinedRenderTransform();
	const QMatrix4x4& viewMatrix = currentViewMatrix();
	const QMatrix4x4 modelViewMatrix = viewMatrix * modelMatrix;

	if (uniformLocationCached("modelMatrix") >= 0)
		_prog->setUniformValue("modelMatrix", modelMatrix);
	if (uniformLocationCached("modelViewMatrix") >= 0)
		_prog->setUniformValue("modelViewMatrix", modelViewMatrix);
	if (uniformLocationCached("normalMatrix") >= 0)
		_prog->setUniformValue("normalMatrix", modelViewMatrix.normalMatrix());
	if (uniformLocationCached("hasSkinning") >= 0)
		_prog->setUniformValue("hasSkinning", hasSkinning());
	if (uniformLocationCached("jointCount") >= 0)
		_prog->setUniformValue("jointCount", static_cast<int>(jointPalette().size()));
	if (hasSkinning() && !jointPalette().isEmpty())
	{
		const int maxJoints = std::min(static_cast<int>(jointPalette().size()), 128);
		for (int i = 0; i < maxJoints; ++i)
		{
			const QString uniformName = QStringLiteral("jointMatrices[%1]").arg(i);
			const int jointLocation = uniformLocationCached(uniformName);
			if (jointLocation >= 0)
				_prog->setUniformValue(jointLocation, jointPalette()[i]);
		}
	}

	setupTextures();
	applyDebugTextureOverrides();

	setupUniforms();
	applyDebugUniformOverrides();

	if(_material.opacity() < 1.0f ||
		_material.hasOpacityMap() || _material.hasTransmissionMap() ||
		_material.transmission() > 0.0f ||
		_material.blendMode() == Material::BlendMode::Alpha ||
		_material.alphaThreshold() > 0.0f || _materialState.hasTextureAlpha())
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_LINE_SMOOTH);
		glEnable(GL_POLYGON_SMOOTH);
	}
	else
	{
		glDisable(GL_BLEND);
	}

	// Handle lighting normal for negative scaling
	glFrontFace(hasNegativeScale() ? GL_CW : GL_CCW);
	_vertexArrayObject.bind();
	const bool drawLod1 = _hasLod1 && lodPolicyActive();
	if (drawLod1)
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _lodIndexBuffer.bufferId());
	else
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _indexBuffer.bufferId());
	glDrawElements(GL_TRIANGLES, drawLod1 ? _nVertsLod1 : _nVerts, GL_UNSIGNED_INT, 0);
	_vertexArrayObject.release();

	glBindTexture(GL_TEXTURE_2D, 0);
	glDisable(GL_BLEND);
}

// Render the mesh for shadow mapping
void RenderableMesh::renderShadow()
{
	if (!_vertexArrayObject.isCreated())
		return;

	const QMatrix4x4& globalModelMatrix = currentGlobalModelMatrix();
	if (uniformLocationCached("model") >= 0)
		_prog->setUniformValue("model", globalModelMatrix * combinedRenderTransform());
	if (uniformLocationCached("hasSkinning") >= 0)
		_prog->setUniformValue("hasSkinning", hasSkinning());
	if (uniformLocationCached("jointCount") >= 0)
		_prog->setUniformValue("jointCount", static_cast<int>(jointPalette().size()));
	if (hasSkinning() && !jointPalette().isEmpty())
	{
		const int maxJoints = std::min(static_cast<int>(jointPalette().size()), 128);
		for (int i = 0; i < maxJoints; ++i)
		{
			const QString uniformName = QStringLiteral("jointMatrices[%1]").arg(i);
			const int jointLocation = uniformLocationCached(uniformName);
			if (jointLocation >= 0)
				_prog->setUniformValue(jointLocation, jointPalette()[i]);
		}
	}

	// Handle negative scaling (important for shadow mapping!)
	glFrontFace(hasNegativeScale() ? GL_CW : GL_CCW);

	_vertexArrayObject.bind();
	glDrawElements(GL_TRIANGLES, _nVerts, GL_UNSIGNED_INT, 0);
	_vertexArrayObject.release();
}

void RenderableMesh::deleteTextures()
{
	if (_fallbackTexture != 0)
	{
		glDeleteTextures(1, &_fallbackTexture);
		_fallbackTexture = 0;
	}

	// Material texture IDs are resolved through ViewportWidget's shared texture cache
	// and may be referenced by multiple meshes or UI previews. Deleting them here
	// lets one mesh teardown invalidate another mesh's live bindings.
}

void RenderableMesh::releaseContextBoundGpuResources()
{
	const bool shareContexts = QCoreApplication::testAttribute(Qt::AA_ShareOpenGLContexts);
	if (shareContexts)
	{
		if (_vertexArrayObject.isCreated())
			_vertexArrayObject.destroy();
		_vaoConfiguredProgram = nullptr;
		_textureBindingsDirty = true;
		_uniformsDirty = true;
		return;
	}

	deleteBuffers();
	deleteTextures();
	_vaoConfiguredProgram = nullptr;
	_textureBindingsDirty = true;
	_uniformsDirty = true;
}

void RenderableMesh::restoreContextBoundGpuResources(QOpenGLShaderProgram* prog)
{
	const bool shareContexts = QCoreApplication::testAttribute(Qt::AA_ShareOpenGLContexts);
	_prog = prog;
	clearUniformLocationCache();

	if (shareContexts)
	{
		if (!_vertexArrayObject.isCreated())
			_vertexArrayObject.create();
		_vaoConfiguredProgram = nullptr;
		setProg(prog);
		_textureBindingsDirty = true;
		_uniformsDirty = true;
		return;
	}

	recreateContextBoundBufferObjects();
	recreateFallbackTexture();

	if (!_points.empty() && !_normals.empty())
	{
		initBuffers(
			&_indices,
			&_points,
			&_normals,
			_colors.empty() ? nullptr : &_colors,
			_texCoords.empty() ? nullptr : &_texCoords,
			_tangents.empty() ? nullptr : &_tangents,
			_bitangents.empty() ? nullptr : &_bitangents,
			_jointIndices.empty() ? nullptr : &_jointIndices,
			_jointWeights.empty() ? nullptr : &_jointWeights
		);
	}

	_textureBindingsDirty = true;
	_uniformsDirty = true;
}

RenderableMesh::~RenderableMesh()
{
	deleteBuffers();
	deleteTextures();
}

void RenderableMesh::deleteBuffers()
{
	if (_buffers.size() > 0)
	{
		for (QOpenGLBuffer& buff : _buffers)
		{
			buff.destroy();
		}
		_buffers.clear();
	}

	if (_vertexArrayObject.isCreated())
	{
		_vertexArrayObject.destroy();
	}
}

void RenderableMesh::recreateContextBoundBufferObjects()
{
	_indexBuffer = QOpenGLBuffer(QOpenGLBuffer::IndexBuffer);
	_positionBuffer = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
	_normalBuffer = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
	_colorBuffer = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
	_texCoord0Buffer = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
	_texCoord1Buffer = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
	_texCoord2Buffer = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
	_texCoord3Buffer = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
	_tangentBuf = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
	_bitangentBuf = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
	_jointIndexBuffer = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
	_jointWeightBuffer = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
	_coordBuf = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);

	_indexBuffer.create();
	_positionBuffer.create();
	_normalBuffer.create();
	_colorBuffer.create();
	_texCoord0Buffer.create();
	_texCoord1Buffer.create();
	_texCoord2Buffer.create();
	_texCoord3Buffer.create();
	_tangentBuf.create();
	_bitangentBuf.create();
	_jointIndexBuffer.create();
	_jointWeightBuffer.create();
	_coordBuf.create();

	if (_vertexArrayObject.isCreated())
		_vertexArrayObject.destroy();
	_vertexArrayObject.create();
	_vaoConfiguredProgram = nullptr;
}

void RenderableMesh::recreateFallbackTexture()
{
	if (_fallbackTexture != 0)
		glDeleteTextures(1, &_fallbackTexture);

	glGenTextures(1, &_fallbackTexture);
	bindTextureUnitCached(GL_TEXTURE0, _fallbackTexture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
}

void RenderableMesh::computeBounds()
{
	// Bounds now computed inside MeshInstanceState::updateRuntimeBounds().
}

float RenderableMesh::getHighestXValue() const { return _instanceState.getHighestXValue(); }
float RenderableMesh::getLowestXValue()  const { return _instanceState.getLowestXValue();  }
float RenderableMesh::getHighestYValue() const { return _instanceState.getHighestYValue(); }
float RenderableMesh::getLowestYValue()  const { return _instanceState.getLowestYValue();  }
float RenderableMesh::getHighestZValue() const { return _instanceState.getHighestZValue(); }
float RenderableMesh::getLowestZValue()  const { return _instanceState.getLowestZValue();  }

QRect RenderableMesh::projectedRect(const QMatrix4x4& modelView, const QMatrix4x4& projection,
	const QRect& viewport, const QRect& window) const
{
	return _instanceState.projectedRect(modelView, projection, viewport, window);
}

std::vector<float> RenderableMesh::getNormals() const
{
	return _normals;
}

std::vector<float> RenderableMesh::getTexCoords() const
{
	return _texCoords;
}

const std::vector<float>& RenderableMesh::getTrsfPoints() const
{
	return _instanceState.getTrsfPoints();
}

void RenderableMesh::resetTransformations()
{
	_instanceState.resetTransformations(_points, _normals, _tangents, _bitangents, _indices);
	applyScaledVolumeProperties();
}

void RenderableMesh::resetExplodedViewTransformations()
{
	_instanceState.resetExplodedViewTransformations(_points, _normals, _tangents, _bitangents, _indices);
}

std::vector<unsigned int> RenderableMesh::getIndices() const
{
	return _indices;
}

std::vector<float> RenderableMesh::getPoints() const
{
	return _points;
}

QVector3D RenderableMesh::getTranslation() const { return _instanceState.getTranslation(); }

void RenderableMesh::setTranslation(const QVector3D& trans)
{
	_instanceState.setTranslation(trans, _points, _normals, _tangents, _bitangents, _indices);
}

QVector3D RenderableMesh::getRotation() const { return _instanceState.getRotation(); }

void RenderableMesh::setRotation(const QVector3D& rota)
{
	_instanceState.setRotation(rota, _points, _normals, _tangents, _bitangents, _indices);
}

QQuaternion RenderableMesh::getRotationQuaternion() const { return _instanceState.getRotationQuaternion(); }

void RenderableMesh::setRotationQuaternion(const QQuaternion& quat, const QVector3D& displayEuler)
{
	_instanceState.setRotationQuaternion(quat, displayEuler, _points, _normals, _tangents, _bitangents, _indices);
}

QVector3D RenderableMesh::getScaling() const { return _instanceState.getScaling(); }

void RenderableMesh::setScaling(const QVector3D& scale)
{
	_instanceState.setScaling(scale, _points, _normals, _tangents, _bitangents, _indices);
	applyScaledVolumeProperties();
}

QVector3D RenderableMesh::getExplodedViewTranslation() const { return _instanceState.getExplodedViewTranslation(); }

void RenderableMesh::setExplodedViewTranslation(const QVector3D& trans)
{
	_instanceState.setExplodedViewTranslation(trans, _points, _normals, _tangents, _bitangents, _indices);
}

QVector3D RenderableMesh::getExplodedViewRotation() const { return _instanceState.getExplodedViewRotation(); }

void RenderableMesh::setExplodedViewRotation(const QVector3D& rota)
{
	_instanceState.setExplodedViewRotation(rota, _points, _normals, _tangents, _bitangents, _indices);
}

QQuaternion RenderableMesh::getExplodedViewRotationQuaternion() const { return _instanceState.getExplodedViewRotationQuaternion(); }

void RenderableMesh::setExplodedViewRotationQuaternion(const QQuaternion& quat, const QVector3D& displayEuler)
{
	_instanceState.setExplodedViewRotationQuaternion(quat, displayEuler, _points, _normals, _tangents, _bitangents, _indices);
}

QVector3D RenderableMesh::getExplodedViewScaling() const { return _instanceState.getExplodedViewScaling(); }

void RenderableMesh::setExplodedViewScaling(const QVector3D& scale)
{
	_instanceState.setExplodedViewScaling(scale, _points, _normals, _tangents, _bitangents, _indices);
}

QMatrix4x4 RenderableMesh::getTransformation() const { return _instanceState.getTransformation(); }
QMatrix4x4 RenderableMesh::getExplodedViewTransformation() const { return _instanceState.getExplodedViewTransformation(); }

QVector3D RenderableMesh::getStableTransformCenter() const { return _instanceState.getStableTransformCenter(); }
float RenderableMesh::getStableTransformRadius() const { return _instanceState.getStableTransformRadius(_points); }

QMatrix4x4 RenderableMesh::getSceneRenderTransform() const { return _instanceState.getSceneRenderTransform(); }

QMatrix4x4 RenderableMesh::combinedRenderTransform() const { return _instanceState.combinedRenderTransform(); }

void RenderableMesh::invalidateCombinedRenderTransformCache() const
{
	_instanceState.invalidateCombinedRenderTransformCache();
}

void RenderableMesh::setSceneRenderTransform(const QMatrix4x4& trsf)
{
	_instanceState.setSceneRenderTransform(trsf, _points, _normals, _tangents, _bitangents, _indices);
}

void RenderableMesh::setSceneRenderTransformFast(const QMatrix4x4& trsf)
{
	_instanceState.setSceneRenderTransformFast(trsf);
}

void RenderableMesh::setupTransformation()
{
	updateRuntimeBounds();
}

void RenderableMesh::fastUpdateWorldBounds()
{
	// Real work in MeshInstanceState; kept for legacy call site compatibility.
}

void RenderableMesh::setTranslationFast(const QVector3D& trans)    { _instanceState.setTranslationFast(trans); }
void RenderableMesh::setRotationFast(const QVector3D& rota)        { _instanceState.setRotationFast(rota); }
void RenderableMesh::setRotationQuaternionFast(const QQuaternion& quat, const QVector3D& displayEuler)
    { _instanceState.setRotationQuaternionFast(quat, displayEuler); }
void RenderableMesh::setScalingFast(const QVector3D& scale)        { _instanceState.setScalingFast(scale); }

void RenderableMesh::setExplodedViewTranslationFast(const QVector3D& trans)
    { _instanceState.setExplodedViewTranslationFast(trans); }
void RenderableMesh::setExplodedViewRotationFast(const QVector3D& rota)
    { _instanceState.setExplodedViewRotationFast(rota); }
void RenderableMesh::setExplodedViewRotationQuaternionFast(const QQuaternion& quat, const QVector3D& displayEuler)
    { _instanceState.setExplodedViewRotationQuaternionFast(quat, displayEuler); }
void RenderableMesh::setExplodedViewScalingFast(const QVector3D& scale)
    { _instanceState.setExplodedViewScalingFast(scale); }

void RenderableMesh::fullUpdateRuntimeBounds()
{
	_instanceState.fullUpdateRuntimeBounds(_points, _normals, _tangents, _bitangents, _indices);
}

void RenderableMesh::updateRuntimeBounds()
{
	_instanceState.updateRuntimeBounds(_points, _normals, _tangents, _bitangents, _indices);
}

float RenderableMesh::shininess() const
{
	return _material.shininess();
}

void RenderableMesh::setShininess(const float& shine)
{
	_material.setShininess(shine);
	markUniformsDirty();
}

float RenderableMesh::opacity() const
{
	return _material.opacity();
}

void RenderableMesh::setOpacity(const float& opacity)
{
	_material.setOpacity(opacity);	
	if (opacity < 1.0f)
		_material.setBlendMode(Material::BlendMode::Alpha);
	else
		_material.setBlendMode(Material::BlendMode::Opaque);
	markUniformsDirty();
}

QVector3D RenderableMesh::emmissiveMaterial() const
{
	return _material.emissive();
}

void RenderableMesh::setEmmissiveMaterial(const QVector3D& emissive)
{
	_material.setEmissive(emissive);
	markUniformsDirty();
}

QVector3D RenderableMesh::specularMaterial() const
{
	return _material.specular();
}

void RenderableMesh::setSpecularMaterial(const QVector3D& specular)
{
	_material.setSpecular(specular);
	markUniformsDirty();
}

QVector3D RenderableMesh::diffuseMaterial() const
{
	return _material.diffuse();
}

void RenderableMesh::setDiffuseMaterial(const QVector3D& diffuse)
{
	_material.setDiffuse(diffuse);
	markUniformsDirty();
}

QVector3D RenderableMesh::ambientMaterial() const
{
	return _material.ambient();
}

void RenderableMesh::setAmbientMaterial(const QVector3D& ambient)
{
	_material.setAmbient(ambient);
	markUniformsDirty();
}

bool RenderableMesh::isMetallic() const
{
	return _material.metallic();
}

void RenderableMesh::setMetallic(bool metallic)
{
	_material.setMetallic(metallic);
	_material.setMetalness(metallic ? 1.0f : 0.0f);
	markUniformsDirty();
}

void RenderableMesh::setPBRAlbedoColor(const float& r, const float& g, const float& b)
{
	_material.setAlbedoColor(QVector3D(r, g, b));
	markUniformsDirty();
}

void RenderableMesh::setPBRMetallic(const float& val)
{
	_material.setMetalness(val);
	markUniformsDirty();
}

void RenderableMesh::setPBRRoughness(const float& val)
{
	_material.setRoughness(val);
	markUniformsDirty();
}

QOpenGLVertexArrayObject& RenderableMesh::getVAO()
{
	return _vertexArrayObject;
}

unsigned long long RenderableMesh::memorySize() const
{
	return _memorySize + sizeof(SceneMesh);
}


bool RenderableMesh::intersectsWithRay(const QVector3D& rayPos, const QVector3D& rayDir, QVector3D& outIntersectionPoint)
{
	return _instanceState.intersectsWithRay(rayPos, rayDir, outIntersectionPoint);
}



bool RenderableMesh::hasAlbedoPBRMap() const
{
	return _material.hasAlbedoMap();
}

void RenderableMesh::enableAlbedoPBRMap(bool hasAlbedoMap)
{
	if (!hasAlbedoMap)
		_material.setAlbedoTextureId(0);
	markUniformsDirty();
}

bool RenderableMesh::hasMetallicPBRMap() const
{
	return _material.hasMetallicMap();
}

void RenderableMesh::enableMetallicPBRMap(bool hasMetallicMap)
{
	if (!hasMetallicMap)
		_material.setMetallicTextureId(0);
	markUniformsDirty();
}

bool RenderableMesh::hasEmissivePBRMap() const
{
	return _material.hasEmissiveMap();
}

void RenderableMesh::enableEmissivePBRMap(bool hasEmissiveMap)
{
	if (!hasEmissiveMap)
		_material.setEmissiveTextureId(0);
	markUniformsDirty();
}

bool RenderableMesh::hasRoughnessPBRMap() const
{
	return _material.hasRoughnessMap();
}

void RenderableMesh::enableRoughnessPBRMap(bool hasRoughnessMap)
{
	if (!hasRoughnessMap)
		_material.setRoughnessTextureId(0);
	markUniformsDirty();
}

bool RenderableMesh::hasHeightPBRMap() const
{
	return _material.hasHeightMap();
}

void RenderableMesh::enableHeightPBRMap(bool hasHeightMap)
{
	if (!hasHeightMap)
		_material.setHeightTextureId(0);
	markUniformsDirty();
}

bool RenderableMesh::hasAOPBRMap() const
{
	return _material.hasAOMap();
}

void RenderableMesh::enableAOPBRMap(bool hasAOMap)
{
	if (!hasAOMap)
		_material.setOcclusionTextureId(0);
	markUniformsDirty();
}

bool RenderableMesh::hasNormalPBRMap() const
{
	return _material.hasNormalMap();
}

void RenderableMesh::enableNormalPBRMap(bool hasNormalMap)
{
	if (!hasNormalMap)
		_material.setNormalTextureId(0);
	markUniformsDirty();
}

bool RenderableMesh::hasOpacityPBRMap() const
{
	return _material.hasOpacityMap();
}

void RenderableMesh::enableOpacityPBRMap(bool hasOpacityMap)
{
	if (!hasOpacityMap)
		_material.setOpacityTextureId(0);
	markUniformsDirty();
}

bool RenderableMesh::hasTransmissionPBRMap() const
{
	return _material.hasTransmissionMap();
}

void RenderableMesh::enableTransmissionPBRMap(bool hasTransmissionMap)
{
	if (!hasTransmissionMap)
		_material.setTransmissionTextureId(0);
	markUniformsDirty();
}

void RenderableMesh::setTransmissionPBRMap(unsigned int transmissionMap)
{
	GLuint existing = static_cast<GLuint>(_material.transmissionTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setTransmissionTextureId(transmissionMap);
	markTexturesDirty();
	markUniformsDirty();
}

bool RenderableMesh::hasIORPBRMap() const
{
	return _material.hasIORMap();
}

void RenderableMesh::enableIORPBRMap(bool hasIORMap)
{
	if (!hasIORMap)
		_material.setIORTextureId(0);
	markUniformsDirty();
}

void RenderableMesh::setIORPBRMap(unsigned int iorMap)
{
	GLuint existing = static_cast<GLuint>(_material.iorTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setIORTextureId(iorMap);
	markTexturesDirty();
	markUniformsDirty();
}

bool RenderableMesh::hasSheenColorPBRMap() const
{
	return _material.hasSheenColorMap();
}

void RenderableMesh::enableSheenColorPBRMap(bool hasSheenColorMap)
{
	if (!hasSheenColorMap)
		_material.setSheenColorTextureId(0);
	markUniformsDirty();
}

void RenderableMesh::setSheenColorPBRMap(unsigned int sheenColorMap)
{
	GLuint existing = static_cast<GLuint>(_material.sheenColorTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setSheenColorTextureId(sheenColorMap);
	markTexturesDirty();
	markUniformsDirty();
}

bool RenderableMesh::hasSheenRoughnessPBRMap() const
{
	return _material.hasSheenRoughnessMap();
}

void RenderableMesh::enableSheenRoughnessPBRMap(bool hasSheenRoughnessMap)
{
	if (!hasSheenRoughnessMap)
		_material.setSheenRoughnessTextureId(0);
	markUniformsDirty();
}

void RenderableMesh::setSheenRoughnessPBRMap(unsigned int sheenRoughnessMap)
{
	GLuint existing = static_cast<GLuint>(_material.sheenRoughnessTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setSheenRoughnessTextureId(sheenRoughnessMap);
	markTexturesDirty();
	markUniformsDirty();
}

bool RenderableMesh::hasClearcoatPBRMap() const
{
	return _material.hasClearcoatColorMap();
}

void RenderableMesh::enableClearcoatPBRMap(bool hasClearcoatMap)
{
	if (!hasClearcoatMap)
		_material.setClearcoatColorTextureId(0);
	markUniformsDirty();
}

void RenderableMesh::setClearcoatPBRMap(unsigned int clearcoatColorMap)
{
	GLuint existing = static_cast<GLuint>(_material.clearcoatColorTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setClearcoatColorTextureId(clearcoatColorMap);
	markTexturesDirty();
	markUniformsDirty();
}

bool RenderableMesh::hasClearcoatRoughnessPBRMap() const
{
	return _material.hasClearcoatRoughnessMap();
}

void RenderableMesh::enableClearcoatRoughnessPBRMap(bool hasClearcoatRoughnessMap)
{
	if (!hasClearcoatRoughnessMap)
		_material.setClearcoatRoughnessTextureId(0);
	markUniformsDirty();
}

void RenderableMesh::setClearcoatRoughnessPBRMap(unsigned int clearcoatRoughnessMap)
{
	GLuint existing = static_cast<GLuint>(_material.clearcoatRoughnessTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setClearcoatRoughnessTextureId(clearcoatRoughnessMap);
	markTexturesDirty();
	markUniformsDirty();
}

bool RenderableMesh::hasClearcoatNormalPBRMap() const
{
	return _material.hasClearcoatNormalMap();
}

void RenderableMesh::enableClearcoatNormalPBRMap(bool hasClearcoatNormalMap)
{
	if (!hasClearcoatNormalMap)
		_material.setClearcoatNormalTextureId(0);
	markUniformsDirty();
}

void RenderableMesh::setClearcoatNormalPBRMap(unsigned int clearcoatNormalMap)
{
	GLuint existing = static_cast<GLuint>(_material.clearcoatNormalTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setClearcoatNormalTextureId(clearcoatNormalMap);
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::setAlbedoPBRMap(unsigned int albedoMap)
{
	GLuint existing = static_cast<GLuint>(_material.albedoTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setAlbedoTextureId(albedoMap);
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::setMetallicPBRMap(unsigned int metallicMap)
{
	GLuint existing = static_cast<GLuint>(_material.metallicTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setMetallicTextureId(metallicMap);
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::setEmissivePBRMap(unsigned int emissiveMap)
{
	GLuint existing = static_cast<GLuint>(_material.emissiveTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setEmissiveTextureId(emissiveMap);
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::setRoughnessPBRMap(unsigned int roughnessMap)
{
	GLuint existing = static_cast<GLuint>(_material.roughnessTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setRoughnessTextureId(roughnessMap);
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::setNormalPBRMap(unsigned int normalMap)
{
	GLuint existing = static_cast<GLuint>(_material.normalTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setNormalTextureId(normalMap);
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::setAOPBRMap(unsigned int aoMap)
{
	GLuint existing = static_cast<GLuint>(_material.occlusionTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setOcclusionTextureId(aoMap);
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::setHeightPBRMap(unsigned int heightMap)
{
	GLuint existing = static_cast<GLuint>(_material.heightTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setHeightTextureId(heightMap);
	markTexturesDirty();
	markUniformsDirty();
}

float RenderableMesh::getHeightPBRMapScale() const
{
	return _material.heightScale();
}

void RenderableMesh::setHeightPBRMapScale(float heightScale)
{
	_material.setHeightScale(heightScale);
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::setOpacityPBRMap(unsigned int opacityMap)
{
	GLuint existing = static_cast<GLuint>(_material.opacityTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setOpacityTextureId(opacityMap);
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::invertOpacityPBRMap(bool invert)
{
	_material.setInvertOpacityMap(invert);
	markTexturesDirty();
	markUniformsDirty();
}

// KHR_materials_pbrSpecularGlossiness
void RenderableMesh::setSpecularGlossinessMap(unsigned int sgMap)
{
	GLuint existing = static_cast<GLuint>(_material.specularGlossinessTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setSpecularGlossinessTextureId(sgMap);
	markTexturesDirty();
	markUniformsDirty();
}

bool RenderableMesh::isTransparent() const
{	
	// If it has transmission, it's ALWAYS transparent (exclude from FBO)
	if (_material.transmission() > 0.0f)
		return true;
	// If it's OPAQUE, it's NOT transparent
	if (_material.blendMode() == Material::BlendMode::Opaque)
		return false;
	// If it has BLEND mode (not MASK or OPAQUE), it's transparent
	if (_material.blendMode() == Material::BlendMode::Alpha)  // BLEND mode
		return true;
	// If it's masked, it's NOT transparent (exclude from FBO)
	if (_material.blendMode() == Material::BlendMode::Masked)
		return false;

	return (_material.opacity() < 0.999f) ||
		_materialState.hasTextureAlpha() || _material.hasOpacityMap() ||
		_material.hasTransmissionMap() || _material.transmission() > 0.0f;
}

bool RenderableMesh::needsDepthMaskOff() const
{
	return (_material.opacity() < 0.999f);  // uniform-only transparency
}


void RenderableMesh::clearAlbedoPBRMap()
{
	GLuint existing = static_cast<GLuint>(_material.albedoTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setAlbedoTextureId(0);
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::clearMetallicPBRMap()
{
	GLuint existing = static_cast<GLuint>(_material.metallicTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setMetallicTextureId(0);
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::clearRoughnessPBRMap()
{
	GLuint existing = static_cast<GLuint>(_material.roughnessTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setRoughnessTextureId(0);
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::clearNormalPBRMap()
{
	GLuint existing = static_cast<GLuint>(_material.normalTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setNormalTextureId(0);
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::clearAOPBRMap()
{
	GLuint existing = static_cast<GLuint>(_material.occlusionTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setOcclusionTextureId(0);
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::clearHeightPBRMap()
{
	GLuint existing = static_cast<GLuint>(_material.heightTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setHeightTextureId(0);
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::clearOpacityPBRMap()
{
	GLuint existing = static_cast<GLuint>(_material.opacityTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setOpacityTextureId(0);
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::clearTransmissionPBRMap()
{
	GLuint existing = static_cast<GLuint>(_material.transmissionTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setTransmissionTextureId(0);
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::clearIORPBRMap()
{
	GLuint existing = static_cast<GLuint>(_material.iorTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setIORTextureId(0);
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::clearSheenColorPBRMap()
{
	GLuint existing = static_cast<GLuint>(_material.sheenColorTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setSheenColorTextureId(0);
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::clearSheenRoughnessPBRMap()
{
	GLuint existing = static_cast<GLuint>(_material.sheenRoughnessTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setSheenRoughnessTextureId(0);
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::clearClearcoatPBRMap()
{
	GLuint existing = static_cast<GLuint>(_material.clearcoatColorTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setClearcoatColorTextureId(0);
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::clearClearcoatRoughnessPBRMap()
{
	GLuint existing = static_cast<GLuint>(_material.clearcoatRoughnessTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setClearcoatRoughnessTextureId(0);
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::clearClearcoatNormalPBRMap()
{
	GLuint existing = static_cast<GLuint>(_material.clearcoatNormalTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setClearcoatNormalTextureId(0);
	markTexturesDirty();
	markUniformsDirty();
}

void RenderableMesh::clearSpecularGlossinessMap()
{
	GLuint existing = static_cast<GLuint>(_material.specularGlossinessTextureId());
	if (existing != 0)
		glDeleteTextures(1, &existing);
	_material.setSpecularGlossinessTextureId(0);
	markTexturesDirty();
	markUniformsDirty();
}


void RenderableMesh::clearAllPBRMaps()
{
	GLuint pbrIds[] = {
		static_cast<GLuint>(_material.albedoTextureId()),
		static_cast<GLuint>(_material.metallicTextureId()),
		static_cast<GLuint>(_material.roughnessTextureId()),
		static_cast<GLuint>(_material.normalTextureId()),
		static_cast<GLuint>(_material.occlusionTextureId()),
		static_cast<GLuint>(_material.heightTextureId()),
		static_cast<GLuint>(_material.emissiveTextureId()),
		static_cast<GLuint>(_material.transmissionTextureId()),
		static_cast<GLuint>(_material.iorTextureId()),
		static_cast<GLuint>(_material.sheenColorTextureId()),
		static_cast<GLuint>(_material.sheenRoughnessTextureId()),
		static_cast<GLuint>(_material.clearcoatColorTextureId()),
		static_cast<GLuint>(_material.clearcoatRoughnessTextureId()),
		static_cast<GLuint>(_material.clearcoatNormalTextureId()),
		static_cast<GLuint>(_material.specularGlossinessTextureId())
	};
	for (GLuint id : pbrIds)
	{
		if (id != 0)
			glDeleteTextures(1, &id);
	}
	_material.setAlbedoTextureId(0);
	_material.setMetallicTextureId(0);
	_material.setRoughnessTextureId(0);
	_material.setNormalTextureId(0);
	_material.setOcclusionTextureId(0);
	_material.setHeightTextureId(0);
	_material.setEmissiveTextureId(0);
	_material.setTransmissionTextureId(0);
	_material.setIORTextureId(0);
	_material.setSheenColorTextureId(0);
	_material.setSheenRoughnessTextureId(0);
	_material.setClearcoatColorTextureId(0);
	_material.setClearcoatRoughnessTextureId(0);
	_material.setClearcoatNormalTextureId(0);
	_material.setSpecularGlossinessTextureId(0);

	markTexturesDirty();
	markUniformsDirty();
}

const Material* RenderableMesh::materialForVariant(int variantIndex) const
{
	return _materialState.materialForVariant(variantIndex, getOriginalMaterialIndex());
}

// ---------------------------------------------------------------------------
// Debug texture overrides (TextureDebugPanel)
// ---------------------------------------------------------------------------
void RenderableMesh::setDebugTextureOverride(int unit, GLuint replaceTex)
{
	_debugTextureOverrides[unit] = replaceTex;
}

void RenderableMesh::clearDebugTextureOverride(int unit)
{
	_debugTextureOverrides.remove(unit);
}

void RenderableMesh::clearAllDebugTextureOverrides()
{
	_debugTextureOverrides.clear();
}

void RenderableMesh::applyDebugTextureOverrides()
{
	if (_debugTextureOverrides.isEmpty())
		return;

	for (auto it = _debugTextureOverrides.constBegin();
	     it != _debugTextureOverrides.constEnd(); ++it)
	{
		bindTextureUnitCached(GL_TEXTURE0 + it.key(), it.value());
	}
	// Restore the active texture to unit 0 so subsequent code is not confused.
	glActiveTexture(GL_TEXTURE0);
}

void RenderableMesh::setDebugUniformOverride(const QString& name, const QVariant& value)
{
	_debugUniformOverrides[name] = value;
}

void RenderableMesh::clearDebugUniformOverride(const QString& name)
{
	_debugUniformOverrides.remove(name);
}

void RenderableMesh::clearAllDebugUniformOverrides()
{
	_debugUniformOverrides.clear();
}

void RenderableMesh::applyDebugUniformOverrides()
{
	if (_debugUniformOverrides.isEmpty() || !_prog)
		return;

	for (auto it = _debugUniformOverrides.constBegin();
	     it != _debugUniformOverrides.constEnd(); ++it)
	{
		const QByteArray nameBytes = it.key().toUtf8();
		const char* uName = nameBytes.constData();

		const int typeId = it.value().userType();
		if (typeId == qMetaTypeId<QVector3D>())
			_prog->setUniformValue(uName, it.value().value<QVector3D>());
		else if (typeId == QMetaType::Bool)
			_prog->setUniformValue(uName, it.value().toBool());
		else if (typeId == QMetaType::Int)
			_prog->setUniformValue(uName, it.value().toInt());
		else
			_prog->setUniformValue(uName, it.value().toFloat());
	}
}
