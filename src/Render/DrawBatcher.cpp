#include "DrawBatcher.h"

#include "Logger.h"
#include "RenderableMesh.h"
#include "SceneMesh.h"

#include <QMatrix4x4>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QSettings>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
constexpr qint64 kMaxChunkVertices = 1000000;          // one group buffer never grows past this, so a build slice stays short
constexpr qint64 kMaxTotalVertices = 64000000;         // safety valve on the extra GPU memory a batch set may take
constexpr qint64 kBuildBudgetMicros = 6000;            // per frame, while building
constexpr qint64 kScanBudgetMicros = 4000;             // per frame, while scanning
constexpr int    kMissingFramesBeforeRebuild = 30;     // frames in a row with many meshes outside the batches before rebuilding
constexpr qint64 kMinMillisBetweenBuilds = 1500;
constexpr qint64 kMaxMillisBetweenBuilds = 30000;
constexpr qint64 kRebuildStormWindowMs = 15000;       // a rebuild this soon after the last one doubles the wait

// Parts are baked into the group, so the baked normals/tangents only match what the vertex shader would have
// produced for the part if the transform is a similarity (rotation, translation, uniform scale). CAD assembly
// placements are; anything else stays an individual draw.
bool isSimilarityTransform(const QMatrix4x4& m)
{
	const float* a = m.constData(); // column-major
	if (a[3] != 0.0f || a[7] != 0.0f || a[11] != 0.0f || std::fabs(a[15] - 1.0f) > 1e-6f)
		return false;
	const float l0 = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
	const float l1 = std::sqrt(a[4] * a[4] + a[5] * a[5] + a[6] * a[6]);
	const float l2 = std::sqrt(a[8] * a[8] + a[9] * a[9] + a[10] * a[10]);
	if (l0 <= 0.0f || l1 <= 0.0f || l2 <= 0.0f)
		return false;
	const float tol = 1e-3f;
	if (std::fabs(l0 - l1) > tol * l0 || std::fabs(l0 - l2) > tol * l0)
		return false;
	const float d01 = (a[0] * a[4] + a[1] * a[5] + a[2] * a[6]) / (l0 * l1);
	const float d02 = (a[0] * a[8] + a[1] * a[9] + a[2] * a[10]) / (l0 * l2);
	const float d12 = (a[4] * a[8] + a[5] * a[9] + a[6] * a[10]) / (l1 * l2);
	return std::fabs(d01) < tol && std::fabs(d02) < tol && std::fabs(d12) < tol;
}

bool sameTransform(const float* a, const QMatrix4x4& m)
{
	return std::memcmp(a, m.constData(), 16 * sizeof(float)) == 0;
}

quint64 specKey(quint64 key, bool col, bool tex, bool tan, bool bitan, bool transparent)
{
	const quint64 layout = (col ? 1u : 0u) | (tex ? 2u : 0u) | (tan ? 4u : 0u) | (bitan ? 8u : 0u) | (transparent ? 16u : 0u);
	return key ^ ((layout + 1) * 0x9E3779B97F4A7C15ULL);
}
} // namespace

DrawBatcher::DrawBatcher() = default;

DrawBatcher::~DrawBatcher()
{
	// The owner releases GL while its context is current (releaseGl()). If it did not, there is nothing safe to do
	// here: deleting names without the right context current could free another context's objects.
}

DrawBatcher::Settings DrawBatcher::readSettings()
{
	static QElapsedTimer clock;
	static Settings cached;
	if (!clock.isValid() || clock.elapsed() > 1000)
	{
		QSettings settings;
		cached.enabled = settings.value(QStringLiteral("batchedDrawing"), true).toBool();
		cached.minDisplayedMeshes = std::max(0, settings.value(QStringLiteral("batchMinMeshes"), 3000).toInt());
		cached.minGroupSize = std::max(2, settings.value(QStringLiteral("batchMinGroupSize"), 6).toInt());
		clock.restart();
	}
	return cached;
}

void DrawBatcher::ensureGlFunctions()
{
	if (!_glReady)
	{
		initializeOpenGLFunctions();
		_glReady = true;
	}
}

void DrawBatcher::destroyGroup(Group& g)
{
	if (!_glReady)
		return;
	const unsigned buffers[] = { g.positionVbo, g.normalVbo, g.colorVbo, g.texCoordVbo, g.tangentVbo,
	                             g.bitangentVbo, g.indexBuffer, g.lodIndexBuffer };
	for (unsigned b : buffers)
		if (b)
			glDeleteBuffers(1, &b);
	if (g.vao)
		glDeleteVertexArrays(1, &g.vao);
	g = Group{};
}

void DrawBatcher::releaseGl()
{
	for (Group& g : _groups)
		destroyGroup(g);
	_groups.clear();
	_entries.clear();
	_scanIds.clear();
	_scanSpecs.clear();
	_buildQueue.clear();
	_scanPos = _buildPos = 0;
	_phase = Phase::Idle;
	_missingLastFrame = 0;
	_candidatesLastFrame = 0;
}

void DrawBatcher::invalidate()
{
	releaseGl();
	_sinceBuild.invalidate();
	_needsInitialBuild = true;
}

void DrawBatcher::startScan(const std::vector<int>& visibleIds)
{
	releaseGl();
	_scanIds = visibleIds;
	_scanPos = 0;
	_scanSpecs.clear();
	_buildVertices = _buildMeshes = _buildBytes = 0;
	_buildClock.start();
	_phase = Phase::Scanning;
}

bool DrawBatcher::advanceScan(const MeshLookup& meshAt, const Settings& settings, qint64 budgetMicros)
{
	QElapsedTimer slice;
	slice.start();
	while (_scanPos < _scanIds.size())
	{
		const int id = _scanIds[_scanPos++];
		SceneMesh* mesh = meshAt(id);
		const bool opaqueCandidate = mesh && mesh->isStaticBatchCandidate();
		const bool transparentCandidate = mesh && !opaqueCandidate && mesh->isBatchCandidate(true);
		if (opaqueCandidate || transparentCandidate)
		{
			// Remember every candidate, so one that is deliberately left out (group too small, transform not a
			// similarity) is not mistaken for a new or changed mesh on every later frame.
			if (static_cast<size_t>(id) >= _entries.size())
				_entries.resize(static_cast<size_t>(id) + 1);
			Entry& known = _entries[static_cast<size_t>(id)];
			known = Entry{};
			known.mesh = mesh;
			known.geometryRevision = mesh->geometryRevision();
			known.key = mesh->drawGroupKey();
			known.group = -2;
			known.transparent = transparentCandidate;
		}
		if ((opaqueCandidate || transparentCandidate) && mesh->cpuIndices().size() >= 3
			&& isSimilarityTransform(mesh->combinedRenderTransform())
			&& mesh->cpuPoints().size() == mesh->cpuNormals().size() && !mesh->cpuPoints().empty())
		{
			const size_t vertexCount = mesh->cpuPoints().size() / 3;
			const bool col = !mesh->cpuColors().empty();
			const bool tex = !mesh->cpuTexCoords().empty();
			const bool tan = !mesh->cpuTangents().empty();
			const bool bitan = !mesh->cpuBitangents().empty();
			const bool sizesOk = (!col || mesh->cpuColors().size() == vertexCount * 4)
				&& (!tex || mesh->cpuTexCoords().size() == vertexCount * 8)
				&& (!tan || mesh->cpuTangents().size() == vertexCount * 3)
				&& (!bitan || mesh->cpuBitangents().size() == vertexCount * 3);
			if (sizesOk)
			{
				Spec& spec = _scanSpecs[specKey(mesh->drawGroupKey(), col, tex, tan, bitan, transparentCandidate)];
				if (spec.ids.empty())
				{
					spec.transparent = transparentCandidate;
					spec.hasColors = col;
					spec.hasTexCoords = tex;
					spec.hasTangents = tan;
					spec.hasBitangents = bitan;
				}
				spec.ids.push_back(id);
			}
		}
		if ((_scanPos & 255u) == 0 && slice.nsecsElapsed() / 1000 > budgetMicros)
			return false;
	}

	// Scan finished: keep the groups big enough to be worth a buffer, split into chunks of bounded size.
	_buildQueue.clear();
	for (auto& [key, spec] : _scanSpecs)
	{
		if (static_cast<int>(spec.ids.size()) < settings.minGroupSize)
			continue;
		Spec chunk;
		chunk.transparent = spec.transparent;
		chunk.hasColors = spec.hasColors;
		chunk.hasTexCoords = spec.hasTexCoords;
		chunk.hasTangents = spec.hasTangents;
		chunk.hasBitangents = spec.hasBitangents;
		qint64 chunkVertices = 0;
		for (int id : spec.ids)
		{
			const SceneMesh* mesh = meshAt(id);
			const qint64 v = mesh ? static_cast<qint64>(mesh->cpuPoints().size() / 3) : 0;
			if (!chunk.ids.empty() && chunkVertices + v > kMaxChunkVertices)
			{
				_buildQueue.push_back(std::move(chunk));
				chunk = Spec{};
				chunk.transparent = spec.transparent;
				chunk.hasColors = spec.hasColors;
				chunk.hasTexCoords = spec.hasTexCoords;
				chunk.hasTangents = spec.hasTangents;
				chunk.hasBitangents = spec.hasBitangents;
				chunkVertices = 0;
			}
			chunk.ids.push_back(id);
			chunkVertices += v;
		}
		if (!chunk.ids.empty())
			_buildQueue.push_back(std::move(chunk));
	}
	_scanSpecs.clear();
	_scanIds.clear();
	_scanIds.shrink_to_fit();
	// Biggest groups first: they save the most draws.
	std::sort(_buildQueue.begin(), _buildQueue.end(),
		[](const Spec& a, const Spec& b) { return a.ids.size() > b.ids.size(); });
	_buildPos = 0;
	_phase = Phase::Building;
	return true;
}

bool DrawBatcher::buildSpec(const Spec& spec, const MeshLookup& meshAt)
{
	struct Part { int id; SceneMesh* mesh; };
	std::vector<Part> parts;
	parts.reserve(spec.ids.size());
	qint64 totalVertices = 0, totalIndices = 0, totalLodIndices = 0;
	for (int id : spec.ids)
	{
		SceneMesh* mesh = meshAt(id);
		if (!mesh)
			continue;
		parts.push_back({ id, mesh });
		totalVertices += static_cast<qint64>(mesh->cpuPoints().size() / 3);
		totalIndices += static_cast<qint64>(mesh->cpuIndices().size());
		if (mesh->hasLod1())
			totalLodIndices += mesh->cpuIndices().size(); // upper bound; the real count is smaller
	}
	if (parts.empty() || totalVertices <= 0 || totalVertices > kMaxChunkVertices * 2)
		return false;
	if (_buildVertices + totalVertices > kMaxTotalVertices)
		return false;

	std::vector<float> pos, nrm, col, tex, tan, bitan;
	std::vector<unsigned int> idx, lodIdx;
	pos.reserve(static_cast<size_t>(totalVertices) * 3);
	nrm.reserve(static_cast<size_t>(totalVertices) * 3);
	if (spec.hasColors)    col.reserve(static_cast<size_t>(totalVertices) * 4);
	if (spec.hasTexCoords) tex.reserve(static_cast<size_t>(totalVertices) * 8);
	if (spec.hasTangents)  tan.reserve(static_cast<size_t>(totalVertices) * 3);
	if (spec.hasBitangents) bitan.reserve(static_cast<size_t>(totalVertices) * 3);
	idx.reserve(static_cast<size_t>(totalIndices));
	lodIdx.reserve(static_cast<size_t>(totalLodIndices / 2));

	const int groupIndex = static_cast<int>(_groups.size());
	unsigned int vertexBase = 0;
	std::vector<unsigned int> lodTmp;
	qint64 meshesBuilt = 0;

	for (const Part& part : parts)
	{
		SceneMesh* mesh = part.mesh;
		const QMatrix4x4 transform = mesh->combinedRenderTransform();
		if (!isSimilarityTransform(transform))
			continue;
		const QMatrix3x3 normalMatrix = transform.normalMatrix();
		const float* m = transform.constData();
		const float* n = normalMatrix.constData();
		const std::vector<float>& p = mesh->cpuPoints();
		const std::vector<float>& nr = mesh->cpuNormals();
		const size_t vertexCount = p.size() / 3;

		for (size_t v = 0; v < vertexCount; ++v)
		{
			const float x = p[v * 3], y = p[v * 3 + 1], z = p[v * 3 + 2];
			pos.push_back(m[0] * x + m[4] * y + m[8] * z + m[12]);
			pos.push_back(m[1] * x + m[5] * y + m[9] * z + m[13]);
			pos.push_back(m[2] * x + m[6] * y + m[10] * z + m[14]);
			const float nx = nr[v * 3], ny = nr[v * 3 + 1], nz = nr[v * 3 + 2];
			nrm.push_back(n[0] * nx + n[3] * ny + n[6] * nz);
			nrm.push_back(n[1] * nx + n[4] * ny + n[7] * nz);
			nrm.push_back(n[2] * nx + n[5] * ny + n[8] * nz);
		}
		auto bakeDirections = [&](const std::vector<float>& src, std::vector<float>& dst)
		{
			for (size_t v = 0; v < vertexCount; ++v)
			{
				const float x = src[v * 3], y = src[v * 3 + 1], z = src[v * 3 + 2];
				dst.push_back(n[0] * x + n[3] * y + n[6] * z);
				dst.push_back(n[1] * x + n[4] * y + n[7] * z);
				dst.push_back(n[2] * x + n[5] * y + n[8] * z);
			}
		};
		if (spec.hasColors)
			col.insert(col.end(), mesh->cpuColors().begin(), mesh->cpuColors().end());
		if (spec.hasTexCoords)
			tex.insert(tex.end(), mesh->cpuTexCoords().begin(), mesh->cpuTexCoords().end());
		if (spec.hasTangents)
			bakeDirections(mesh->cpuTangents(), tan);
		if (spec.hasBitangents)
			bakeDirections(mesh->cpuBitangents(), bitan);

		Entry entry;
		entry.mesh = mesh;
		entry.geometryRevision = mesh->geometryRevision();
		entry.key = mesh->drawGroupKey();
		std::memcpy(entry.transform, transform.constData(), sizeof(entry.transform));
		entry.group = groupIndex;
		entry.transparent = spec.transparent;
		entry.firstIndex = static_cast<quint32>(idx.size());
		entry.indexCount = static_cast<quint32>(mesh->cpuIndices().size());
		for (unsigned int i : mesh->cpuIndices())
			idx.push_back(i + vertexBase);
		if (mesh->hasLod1() && mesh->readLod1Indices(lodTmp) && !lodTmp.empty())
		{
			entry.lodFirst = static_cast<quint32>(lodIdx.size());
			entry.lodCount = static_cast<quint32>(lodTmp.size());
			for (unsigned int i : lodTmp)
				lodIdx.push_back(i + vertexBase);
		}
		vertexBase += static_cast<unsigned int>(vertexCount);

		if (static_cast<size_t>(part.id) >= _entries.size())
			_entries.resize(static_cast<size_t>(part.id) + 1);
		_entries[static_cast<size_t>(part.id)] = entry;
		++meshesBuilt;
	}
	if (meshesBuilt == 0)
		return false;

	Group g;
	g.hasColors = spec.hasColors;
	g.hasTexCoords = spec.hasTexCoords;
	g.hasTangents = spec.hasTangents;
	g.hasBitangents = spec.hasBitangents;
	auto upload = [&](unsigned& name, unsigned target, const void* data, size_t bytes)
	{
		glGenBuffers(1, &name);
		glBindBuffer(target, name);
		glBufferData(target, static_cast<GLsizeiptr>(bytes), data, GL_STATIC_DRAW);
		g.bytes += static_cast<qint64>(bytes);
	};
	glGenVertexArrays(1, &g.vao);
	glBindVertexArray(g.vao);
	upload(g.positionVbo, GL_ARRAY_BUFFER, pos.data(), pos.size() * sizeof(float));
	upload(g.normalVbo, GL_ARRAY_BUFFER, nrm.data(), nrm.size() * sizeof(float));
	if (spec.hasColors)    upload(g.colorVbo, GL_ARRAY_BUFFER, col.data(), col.size() * sizeof(float));
	if (spec.hasTexCoords) upload(g.texCoordVbo, GL_ARRAY_BUFFER, tex.data(), tex.size() * sizeof(float));
	if (spec.hasTangents)  upload(g.tangentVbo, GL_ARRAY_BUFFER, tan.data(), tan.size() * sizeof(float));
	if (spec.hasBitangents) upload(g.bitangentVbo, GL_ARRAY_BUFFER, bitan.data(), bitan.size() * sizeof(float));
	upload(g.indexBuffer, GL_ELEMENT_ARRAY_BUFFER, idx.data(), idx.size() * sizeof(unsigned int));
	if (!lodIdx.empty())
		upload(g.lodIndexBuffer, GL_ELEMENT_ARRAY_BUFFER, lodIdx.data(), lodIdx.size() * sizeof(unsigned int));
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	_buildVertices += vertexBase;
	_buildMeshes += meshesBuilt;
	_buildBytes += g.bytes;
	_groups.push_back(std::move(g));
	return true;
}

void DrawBatcher::configureVao(Group& g, QOpenGLShaderProgram* prog)
{
	glBindVertexArray(g.vao);
	for (int location = 0; location < 12; ++location)
		glDisableVertexAttribArray(location);

	auto attach = [&](const char* name, unsigned vbo, int components, int strideFloats, int offsetFloats)
	{
		const int location = prog->attributeLocation(name);
		if (location < 0 || vbo == 0)
			return;
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glEnableVertexAttribArray(location);
		glVertexAttribPointer(location, components, GL_FLOAT, GL_FALSE,
			static_cast<GLsizei>(strideFloats * sizeof(float)),
			reinterpret_cast<const void*>(static_cast<quintptr>(offsetFloats * sizeof(float))));
	};
	attach("vertexPosition", g.positionVbo, 3, 0, 0);
	attach("vertexNormal", g.normalVbo, 3, 0, 0);
	if (g.hasColors)
		attach("vertexColor", g.colorVbo, 4, 0, 0);
	if (g.hasTexCoords)
	{
		attach("texCoord0", g.texCoordVbo, 2, 8, 0);
		attach("texCoord1", g.texCoordVbo, 2, 8, 2);
		attach("texCoord2", g.texCoordVbo, 2, 8, 4);
		attach("texCoord3", g.texCoordVbo, 2, 8, 6);
	}
	if (g.hasTangents)
		attach("vertexTangent", g.tangentVbo, 3, 0, 0);
	if (g.hasBitangents)
		attach("vertexBitangent", g.bitangentVbo, 3, 0, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.indexBuffer);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	g.configuredProgram = prog;
}

void DrawBatcher::update(const std::vector<int>& visibleIds, const MeshLookup& meshAt, const Settings& settings)
{
	const bool wanted = settings.enabled && static_cast<int>(visibleIds.size()) >= settings.minDisplayedMeshes;
	if (!wanted)
	{
		if (visibleIds.size() >= 1000 && (!_stateLogClock.isValid() || _stateLogClock.elapsed() > 5000))
		{
			_stateLogClock.restart();
			Logger::instance().info(
				QStringLiteral("[DrawBatcher] not used: enabled=%1 visible=%2 minimum=%3")
					.arg(settings.enabled).arg(visibleIds.size()).arg(settings.minDisplayedMeshes),
				QStringLiteral("Performance"));
		}
		if (_enabledLastFrame)
		{
			releaseGl(); // setting turned off, or the model shrank below the threshold: give the memory back
			_needsInitialBuild = true;
		}
		_enabledLastFrame = false;
		return;
	}
	if (!_enabledLastFrame)
		_wantedSince.start();
	_enabledLastFrame = true;
	ensureGlFunctions();

	const quint64 revision = RenderableMesh::currentRuntimeBoundsRevision();
	if (revision == _lastRuntimeRevision)
		++_stableFrames;
	else
	{
		_stableFrames = 0;
		_lastRuntimeRevision = revision;
	}

	if (!_stateLogClock.isValid() || _stateLogClock.elapsed() > 5000)
	{
		_stateLogClock.restart();
		Logger::instance().info(
			QStringLiteral("[DrawBatcher] state visible=%1 phase=%2 stableFrames=%3 needsInitialBuild=%4 groups=%5 "
				"candidates=%6 missing=%7")
				.arg(visibleIds.size()).arg(static_cast<int>(_phase)).arg(_stableFrames).arg(_needsInitialBuild)
				.arg(_groups.size()).arg(_candidatesLastFrame).arg(_missingLastFrame),
			QStringLiteral("Performance"));
	}

	if (_phase == Phase::Idle)
	{
		// First build, or a rebuild: the batch has fallen behind (new meshes, moved parts, changed materials)
		// and the scene has been still for a while.
		const bool neverBuilt = _needsInitialBuild;
		const bool tooManyMissing = _candidatesLastFrame > 0
			&& _missingLastFrame > std::max(200, _candidatesLastFrame / 20);
		_missingStreak = tooManyMissing ? _missingStreak + 1 : 0;
		const bool cooledDown = !_sinceBuild.isValid() || _sinceBuild.elapsed() > _cooldownMs;
		// The first build does not wait for a perfectly still scene (something touches the scene's bounds revision
		// every frame, so it is never still): it starts after 2 stable frames or 1.5 s. A rebuild starts once many
		// meshes have been missing from the batches for a while; the wait between builds backs off if they keep recurring.
		const bool settled = neverBuilt
			? (_stableFrames >= 2 || (_wantedSince.isValid() && _wantedSince.elapsed() > 1500))
			: _missingStreak >= kMissingFramesBeforeRebuild;
		if ((neverBuilt || tooManyMissing) && settled && cooledDown)
		{
			if (!neverBuilt)
				_cooldownMs = (_sinceBuild.isValid() && _sinceBuild.elapsed() < kRebuildStormWindowMs)
					? std::min(_cooldownMs * 2, kMaxMillisBetweenBuilds) : kMinMillisBetweenBuilds;
			_missingStreak = 0;
			startScan(visibleIds);
			_needsInitialBuild = false;
		}
		if (_phase == Phase::Idle)
			return;
	}

	if (_phase == Phase::Scanning)
	{
		if (!advanceScan(meshAt, settings, kScanBudgetMicros))
			return;
	}

	if (_phase == Phase::Building)
	{
		QElapsedTimer slice;
		slice.start();
		while (_buildPos < _buildQueue.size() && slice.nsecsElapsed() / 1000 < kBuildBudgetMicros)
			buildSpec(_buildQueue[_buildPos++], meshAt);
		if (_buildPos >= _buildQueue.size())
		{
			Logger::instance().info(
				QStringLiteral("[DrawBatcher] built groups=%1 meshes=%2 vertices=%3 extraGpuMB=%4 in %5 ms")
					.arg(_groups.size()).arg(_buildMeshes).arg(_buildVertices)
					.arg(static_cast<double>(_buildBytes) / (1024.0 * 1024.0), 0, 'f', 1)
					.arg(_buildClock.elapsed()),
				QStringLiteral("Performance"));
			_buildQueue.clear();
			_buildQueue.shrink_to_fit();
			_phase = Phase::Idle;
			_sinceBuild.restart();
			_missingLastFrame = 0;
		}
	}
}

int DrawBatcher::drawBatches(QOpenGLShaderProgram* prog, const std::vector<int>& ids, int hoveredId,
                             const MeshLookup& meshAt, std::vector<int>& individual)
{
	individual.clear();
	individual.reserve(ids.size());
	if (_groups.empty() || !_glReady)
	{
		individual.assign(ids.begin(), ids.end());
		return 0;
	}

	for (Group& g : _groups)
	{
		g.counts.clear();
		g.offsets.clear();
		g.lodCounts.clear();
		g.lodOffsets.clear();
		g.frameRep = nullptr;
		g.frameTriangles = 0;
	}

	const bool lodActive = RenderableMesh::lodPolicyActive();
	int missing = 0;
	int candidates = 0;
	int batched = 0;
	for (int id : ids)
	{
		SceneMesh* mesh = meshAt(id);
		if (!mesh)
			continue; // the unbatched loop skips these too
		const bool candidate = mesh->isStaticBatchCandidate();
		if (candidate)
			++candidates;

		const Entry* entry = (static_cast<size_t>(id) < _entries.size()) ? &_entries[static_cast<size_t>(id)] : nullptr;
		const bool known = entry && entry->group == -2 && entry->mesh == mesh && !entry->transparent
			&& mesh->geometryRevision() == entry->geometryRevision && mesh->drawGroupKey() == entry->key;
		bool ok = entry && entry->group >= 0 && entry->mesh == mesh && !entry->transparent && candidate && id != hoveredId
			&& mesh->geometryRevision() == entry->geometryRevision
			&& mesh->drawGroupKey() == entry->key
			&& sameTransform(entry->transform, mesh->combinedRenderTransformRef());
		if (!ok)
		{
			if (candidate && id != hoveredId && !known)
				++missing;
			individual.push_back(id);
			continue;
		}

		Group& g = _groups[static_cast<size_t>(entry->group)];
		if (!g.frameRep)
			g.frameRep = mesh;
		if (lodActive && entry->lodCount > 0)
		{
			g.lodCounts.push_back(static_cast<int>(entry->lodCount));
			g.lodOffsets.push_back(reinterpret_cast<const void*>(static_cast<quintptr>(entry->lodFirst) * sizeof(unsigned int)));
			g.frameTriangles += entry->lodCount / 3;
		}
		else
		{
			g.counts.push_back(static_cast<int>(entry->indexCount));
			g.offsets.push_back(reinterpret_cast<const void*>(static_cast<quintptr>(entry->firstIndex) * sizeof(unsigned int)));
			g.frameTriangles += entry->indexCount / 3;
		}
		++batched;
	}
	_missingLastFrame = missing;
	_candidatesLastFrame = candidates;

	for (Group& g : _groups)
	{
		if (!g.frameRep || (g.counts.empty() && g.lodCounts.empty()))
			continue;
		g.frameRep->prepareBatchDraw(prog);
		if (g.configuredProgram != prog)
			configureVao(g, prog);

		glBindVertexArray(g.vao);
		if (!g.counts.empty())
		{
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.indexBuffer);
			glMultiDrawElements(GL_TRIANGLES, g.counts.data(), GL_UNSIGNED_INT, g.offsets.data(),
				static_cast<GLsizei>(g.counts.size()));
		}
		if (!g.lodCounts.empty() && g.lodIndexBuffer)
		{
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.lodIndexBuffer);
			glMultiDrawElements(GL_TRIANGLES, g.lodCounts.data(), GL_UNSIGNED_INT, g.lodOffsets.data(),
				static_cast<GLsizei>(g.lodCounts.size()));
		}
		glBindVertexArray(0);
		RenderableMesh::recordDrawCall(true, false, g.frameTriangles);
		RenderableMesh::recordBatchedDraw(static_cast<qint64>(g.counts.size() + g.lodCounts.size()));
	}
	return batched;
}

void DrawBatcher::drawTransparentSequence(QOpenGLShaderProgram* prog, const std::vector<int>& orderedIds, int hoveredId,
                                          const MeshLookup& meshAt, const DrawOne& drawIndividual)
{
	if (_groups.empty() || !_glReady)
	{
		for (int id : orderedIds)
			drawIndividual(id);
		return;
	}

	const bool lodActive = RenderableMesh::lodPolicyActive();
	int runGroup = -1;
	bool runLod = false;
	SceneMesh* runRep = nullptr;
	qint64 runTriangles = 0;
	_runCounts.clear();
	_runOffsets.clear();

	const auto flush = [&]()
	{
		if (runGroup >= 0 && runRep && !_runCounts.empty())
		{
			Group& g = _groups[static_cast<size_t>(runGroup)];
			runRep->prepareBatchDraw(prog);
			if (g.configuredProgram != prog)
				configureVao(g, prog);
			// Same depth-write rule SceneMesh::render() applies to a transparent mesh; the meshes of a run share a material.
			const bool depthWritesOff = runRep->isTransparent() && runRep->needsDepthMaskOff();
			if (depthWritesOff)
				glDepthMask(GL_FALSE);
			glBindVertexArray(g.vao);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, runLod ? g.lodIndexBuffer : g.indexBuffer);
			glMultiDrawElements(GL_TRIANGLES, _runCounts.data(), GL_UNSIGNED_INT, _runOffsets.data(),
				static_cast<GLsizei>(_runCounts.size()));
			glBindVertexArray(0);
			if (depthWritesOff)
				glDepthMask(GL_TRUE);
			RenderableMesh::recordDrawCall(true, true, runTriangles);
			RenderableMesh::recordBatchedDraw(static_cast<qint64>(_runCounts.size()));
		}
		runGroup = -1;
		runRep = nullptr;
		runTriangles = 0;
		_runCounts.clear();
		_runOffsets.clear();
	};

	for (int id : orderedIds)
	{
		SceneMesh* mesh = meshAt(id);
		if (!mesh)
			continue; // the unbatched loop skips these too

		const Entry* entry = (static_cast<size_t>(id) < _entries.size()) ? &_entries[static_cast<size_t>(id)] : nullptr;
		const bool valid = entry && entry->group >= 0 && entry->transparent && entry->mesh == mesh && id != hoveredId
			&& mesh->isBatchCandidate(true)
			&& mesh->geometryRevision() == entry->geometryRevision
			&& mesh->drawGroupKey() == entry->key
			&& sameTransform(entry->transform, mesh->combinedRenderTransformRef());
		if (!valid)
		{
			flush();
			drawIndividual(id);
			continue;
		}

		const bool useLod = lodActive && entry->lodCount > 0;
		if (entry->group != runGroup || useLod != runLod)
		{
			flush();
			runGroup = entry->group;
			runLod = useLod;
			runRep = mesh;
		}
		if (useLod)
		{
			_runCounts.push_back(static_cast<int>(entry->lodCount));
			_runOffsets.push_back(reinterpret_cast<const void*>(static_cast<quintptr>(entry->lodFirst) * sizeof(unsigned int)));
			runTriangles += entry->lodCount / 3;
		}
		else
		{
			_runCounts.push_back(static_cast<int>(entry->indexCount));
			_runOffsets.push_back(reinterpret_cast<const void*>(static_cast<quintptr>(entry->firstIndex) * sizeof(unsigned int)));
			runTriangles += entry->indexCount / 3;
		}
	}
	flush();
}
