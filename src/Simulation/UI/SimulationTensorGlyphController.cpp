#include "SimulationTensorGlyphController.h"

#include "Camera.h"
#include "RenderableMesh.h"
#include "SceneRenderController.h"

#include <QMatrix4x4>
#include <QOpenGLShaderProgram>
#include <QVector3D>

#include <cmath>

namespace
{
	constexpr int kRings = 8;    // latitude divisions (poles included)
	constexpr int kSegments = 12; // longitude divisions

	void pushVertex(std::vector<float>& out, const QVector3D& p, const QVector3D& color)
	{
		out.insert(out.end(), { p.x(), p.y(), p.z(), color.x(), color.y(), color.z() });
	}

	QVector3D shaded(const QVector3D& color, const QVector3D& normal)
	{
		QVector3D n = normal;
		n.normalize();
		// Fixed world-space fill light keeps the vertex colours independent of the camera. The complete tessellated
		// overlay can therefore stay cached while orbiting and across compare panes with different cameras.
		return color * (0.4f + 0.6f * std::fabs(n.z()));
	}

	// A unit-sphere template, generated once: `positions[r][s]` is the point at ring r (0 = one pole, kRings = the
	// other), longitude segment s (wraps at kSegments). Reused for every ellipsoid: the world position and the
	// (approximate - see below) normal are both this local point transformed by the glyph's own axes.
	struct SphereTemplate
	{
		std::vector<QVector3D> points; // (kRings + 1) * (kSegments + 1), row-major by ring
		SphereTemplate()
		{
			points.reserve(static_cast<std::size_t>(kRings + 1) * (kSegments + 1));
			for (int r = 0; r <= kRings; ++r)
			{
				const float theta = 3.14159265358979323846f * static_cast<float>(r) / kRings; // 0..pi
				const float y = std::cos(theta), ring = std::sin(theta);
				for (int s = 0; s <= kSegments; ++s)
				{
					const float phi = 2.0f * 3.14159265358979323846f * static_cast<float>(s) / kSegments;
					points.emplace_back(ring * std::cos(phi), y, ring * std::sin(phi));
				}
			}
		}
		const QVector3D& at(int r, int s) const { return points[static_cast<std::size_t>(r) * (kSegments + 1) + static_cast<std::size_t>(s)]; }
	};

	const SphereTemplate& sphereTemplate()
	{
		static const SphereTemplate instance;
		return instance;
	}
}

SimulationTensorGlyphController::SimulationTensorGlyphController(SceneRenderController& renderCtrl, QObject* parent)
	: QObject(parent)
	, _renderCtrl(renderCtrl)
{
}

void SimulationTensorGlyphController::restoreGpuResources()
{
	initializeOpenGLFunctions();
	_glFunctionsInitialized = true;
}

void SimulationTensorGlyphController::releaseGpuResources()
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
	_bufferDirty = true;
	_glFunctionsInitialized = false;
}

void SimulationTensorGlyphController::setTensorGlyphs(const QUuid& meshUuid, TensorGlyphSet glyphs)
{
	if (glyphs.count() == 0)
		_sets.erase(meshUuid);
	else
		_sets[meshUuid] = std::move(glyphs);
	_cacheValid = false;
}

void SimulationTensorGlyphController::clearTensorGlyphs(const QUuid& meshUuid)
{
	_sets.erase(meshUuid);
	_cacheValid = false;
}

void SimulationTensorGlyphController::drawOverlay(Camera* camera, const MeshResolver& resolve)
{
	if (!_glFunctionsInitialized || !camera || _sets.empty() || !_renderCtrl.axisShader() || !resolve)
		return;

	const QMatrix4x4 view = camera->getViewMatrix();
	const SphereTemplate& sphere = sphereTemplate();
	std::vector<std::pair<const std::pair<const QUuid, TensorGlyphSet>*, const RenderableMesh*>> resolved;
	std::vector<MeshCacheKey> keys;
	std::size_t glyphCount = 0;
	for (const auto& entry : _sets)
	{
		const RenderableMesh* mesh = resolve(entry.first);
		if (!mesh)
			continue;
		resolved.emplace_back(&entry, mesh);
		keys.push_back({ entry.first, mesh, mesh->geometryRevision(), mesh->combinedRenderTransform() });
		glyphCount += entry.second.count();
	}
	bool rebuild = !_cacheValid || _cachedMeshes.size() != keys.size();
	for (std::size_t i = 0; !rebuild && i < keys.size(); ++i)
		rebuild = _cachedMeshes[i].uuid != keys[i].uuid || _cachedMeshes[i].mesh != keys[i].mesh
			|| _cachedMeshes[i].geometryRevision != keys[i].geometryRevision || _cachedMeshes[i].transform != keys[i].transform;

	if (rebuild)
	{
		_cachedTriangles.clear();
		// Each glyph has kRings*kSegments quads, two triangles per quad, three vertices per triangle and six floats
		// per vertex. Reserving once avoids the repeated reallocations the old per-frame builder incurred.
		_cachedTriangles.reserve(glyphCount * kRings * kSegments * 2u * 3u * 6u);
		for (const auto& resolvedEntry : resolved)
		{
			const auto& entry = *resolvedEntry.first;
			const RenderableMesh* mesh = resolvedEntry.second;
			const std::vector<float>& points = mesh->getTrsfPoints();
			const QMatrix4x4 frame = mesh->combinedRenderTransform();
			const TensorGlyphSet& set = entry.second;
			for (std::size_t i = 0; i < set.count(); ++i)
			{
			QVector3D center;
			bool ok = true;
			for (std::size_t k = 0; k < 3 && ok; ++k)
			{
				const std::size_t p = static_cast<std::size_t>(set.anchors[i * 3 + k]) * 3;
				ok = p + 2 < points.size();
				if (ok)
					center += QVector3D(points[p], points[p + 1], points[p + 2]);
			}
			if (!ok)
				continue;
			center /= 3.0f;

			// The glyph's own axes (already scaled to their radius, mesh frame), taken into the result's own
			// current transform - same convention as the arrows' `frame.mapVector(vector)`.
			const QVector3D a0 = frame.mapVector(QVector3D(set.axes[i * 9 + 0], set.axes[i * 9 + 1], set.axes[i * 9 + 2]));
			const QVector3D a1 = frame.mapVector(QVector3D(set.axes[i * 9 + 3], set.axes[i * 9 + 4], set.axes[i * 9 + 5]));
			const QVector3D a2 = frame.mapVector(QVector3D(set.axes[i * 9 + 6], set.axes[i * 9 + 7], set.axes[i * 9 + 8]));
			if (!(a0.lengthSquared() > 0.0f) || !(a1.lengthSquared() > 0.0f) || !(a2.lengthSquared() > 0.0f))
				continue;
			const QVector3D color = i * 3 + 2 < set.colors.size()
				? QVector3D(set.colors[i * 3], set.colors[i * 3 + 1], set.colors[i * 3 + 2])
				: QVector3D(1.0f, 1.0f, 1.0f);

			// The exact ellipsoid normal at local point (x,y,z) is (x/|a0|)*a0hat + (y/|a1|)*a1hat + (z/|a2|)*a2hat,
			// i.e. x*a0/|a0|^2 + y*a1/|a1|^2 + z*a2/|a2|^2 (a0/a1/a2 already carry their radius) - the headlight
			// shade below only needs its direction, not its exact length, so the 1/radius^2 weighting matters (it
			// is what makes a flattened ellipsoid shade like a disc rather than like a sphere), but not the final
			// normalization scale.
			const float inv0 = 1.0f / a0.lengthSquared(), inv1 = 1.0f / a1.lengthSquared(), inv2 = 1.0f / a2.lengthSquared();
			auto worldPoint = [&](const QVector3D& local) { return center + a0 * local.x() + a1 * local.y() + a2 * local.z(); };
			auto worldNormal = [&](const QVector3D& local) {
				QVector3D n = a0 * (local.x() * inv0) + a1 * (local.y() * inv1) + a2 * (local.z() * inv2);
				return n.length() > 1.0e-12f ? n.normalized() : local;
			};

			for (int r = 0; r < kRings; ++r)
				for (int s = 0; s < kSegments; ++s)
				{
					const QVector3D& l00 = sphere.at(r, s);
					const QVector3D& l01 = sphere.at(r, s + 1);
					const QVector3D& l10 = sphere.at(r + 1, s);
					const QVector3D& l11 = sphere.at(r + 1, s + 1);
					const QVector3D p00 = worldPoint(l00), p01 = worldPoint(l01), p10 = worldPoint(l10), p11 = worldPoint(l11);
					const QVector3D c00 = shaded(color, worldNormal(l00)), c01 = shaded(color, worldNormal(l01));
					const QVector3D c10 = shaded(color, worldNormal(l10)), c11 = shaded(color, worldNormal(l11));
					// Two triangles per quad; degenerate at the poles (l00==l01 or l10==l11) draw a zero-area
					// triangle there, harmless.
					pushVertex(_cachedTriangles, p00, c00);
					pushVertex(_cachedTriangles, p10, c10);
					pushVertex(_cachedTriangles, p11, c11);
					pushVertex(_cachedTriangles, p00, c00);
					pushVertex(_cachedTriangles, p11, c11);
					pushVertex(_cachedTriangles, p01, c01);
				}
			}
		}
		_cachedMeshes = std::move(keys);
		_cacheValid = true;
		_bufferDirty = true;
	}
	if (_cachedTriangles.empty())
		return;

	const GLsizei triangleVertices = static_cast<GLsizei>(_cachedTriangles.size() / 6);

	if (_vao == 0)
		glGenVertexArrays(1, &_vao);
	if (_vbo == 0)
		glGenBuffers(1, &_vbo);
	glBindVertexArray(_vao);
	glBindBuffer(GL_ARRAY_BUFFER, _vbo);
	if (_bufferDirty)
	{
		glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(_cachedTriangles.size() * sizeof(float)), _cachedTriangles.data(), GL_DYNAMIC_DRAW);
		_bufferDirty = false;
	}
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void*>(0));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void*>(3 * sizeof(float)));

	// Same reasoning as the vector arrows' cone: the hand-built triangulation's winding is not guaranteed consistent
	// enough to rely on back-face culling (an ellipsoid drawn with the wrong-facing half of its triangles discarded
	// would show gaps rather than a solid shape), so culling is off for this pass only, like the arrows.
	const GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);
	const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	// TextRenderer intentionally leaves depth testing disabled after drawing a hover label. Establish the desired
	// state explicitly so hovering cannot make back-side ellipsoids suddenly appear through the result mesh.
	glEnable(GL_DEPTH_TEST);

	_renderCtrl.axisShader()->bind();
	_renderCtrl.axisShader()->setUniformValue("modelViewMatrix", view);
	_renderCtrl.axisShader()->setUniformValue("projectionMatrix", camera->getProjectionMatrix());
	_renderCtrl.axisShader()->setUniformValue("renderCone", false);
	glDrawArrays(GL_TRIANGLES, 0, triangleVertices);
	_renderCtrl.axisShader()->release();

	if (cullWasEnabled)
		glEnable(GL_CULL_FACE);
	if (!depthWasEnabled)
		glDisable(GL_DEPTH_TEST);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
}
