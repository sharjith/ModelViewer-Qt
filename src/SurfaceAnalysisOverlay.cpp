#include "SurfaceAnalysisOverlay.h"
#include "SceneMesh.h"

void SurfaceAnalysisOverlay::applyResult(
	SceneMesh* mesh,
	const std::vector<float>& scalarPerSample,
	const std::vector<bool>& validPerSample,
	const CacheKey& key,
	float rangeMin, float rangeMax,
	AnalysisColormap colormap)
{
	if (!mesh)
		return;

	Entry entry;
	entry.mesh = mesh;
	entry.key = key;
	entry.scalarPerSample = scalarPerSample;
	entry.validPerSample = validPerSample;
	entry.rangeMin = rangeMin;
	entry.rangeMax = rangeMax;
	entry.colormap = colormap;
	entry.isFlat = false;
	_entries[mesh] = entry;

	const std::vector<float> rgba = AnalysisColorRamp::mapToRGBA(scalarPerSample, validPerSample, rangeMin, rangeMax, colormap);
	mesh->setAnalysisOverlayColors(rgba);
}

void SurfaceAnalysisOverlay::applyFlatResult(
	SceneMesh* mesh,
	const std::vector<float>& scalarPerFace,
	const std::vector<bool>& validPerFace,
	const CacheKey& key,
	float rangeMin, float rangeMax,
	AnalysisColormap colormap)
{
	if (!mesh)
		return;

	Entry entry;
	entry.mesh = mesh;
	entry.key = key;
	entry.scalarPerSample = scalarPerFace;
	entry.validPerSample = validPerFace;
	entry.rangeMin = rangeMin;
	entry.rangeMax = rangeMax;
	entry.colormap = colormap;
	entry.isFlat = true;
	_entries[mesh] = entry;

	const std::vector<float> rgba = AnalysisColorRamp::mapToRGBA(scalarPerFace, validPerFace, rangeMin, rangeMax, colormap);
	mesh->setAnalysisOverlayFlatColors(rgba);
}

void SurfaceAnalysisOverlay::recolor(SceneMesh* mesh, float rangeMin, float rangeMax, AnalysisColormap colormap)
{
	if (!mesh)
		return;

	auto it = _entries.find(mesh);
	if (it == _entries.end())
		return;

	it->rangeMin = rangeMin;
	it->rangeMax = rangeMax;
	it->colormap = colormap;

	const std::vector<float> rgba = AnalysisColorRamp::mapToRGBA(it->scalarPerSample, it->validPerSample, rangeMin, rangeMax, colormap);
	if (it->isFlat)
		mesh->setAnalysisOverlayFlatColors(rgba);
	else
		mesh->setAnalysisOverlayColors(rgba);
}

bool SurfaceAnalysisOverlay::isValid(SceneMesh* mesh, const CacheKey& currentKey) const
{
	if (!mesh)
		return false;
	auto it = _entries.constFind(mesh);
	if (it == _entries.constEnd())
		return false;
	return it->key == currentKey;
}

bool SurfaceAnalysisOverlay::hasOverlay(SceneMesh* mesh) const
{
	return mesh && _entries.contains(mesh);
}

QList<SceneMesh*> SurfaceAnalysisOverlay::trackedMeshes() const
{
	return _entries.keys();
}

SurfaceAnalysisOverlay::CacheKey SurfaceAnalysisOverlay::storedKey(SceneMesh* mesh) const
{
	if (!mesh)
		return CacheKey();
	auto it = _entries.constFind(mesh);
	return it == _entries.constEnd() ? CacheKey() : it->key;
}

SurfaceAnalysisOverlay::CacheKey SurfaceAnalysisOverlay::computeCurrentKey(
	SceneMesh* mesh, const QVariantMap& parameters, SceneMesh* referenceMesh)
{
	CacheKey key;
	if (!mesh)
		return key;
	key.geometryRevision = mesh->geometryRevision();
	key.transform = mesh->combinedRenderTransform();
	key.parameters = parameters;
	if (referenceMesh)
	{
		key.referenceMesh = referenceMesh;
		key.referenceGeometryRevision = referenceMesh->geometryRevision();
		key.referenceTransform = referenceMesh->combinedRenderTransform();
	}
	return key;
}

void SurfaceAnalysisOverlay::clearOverlay(SceneMesh* mesh)
{
	if (!mesh)
		return;
	_entries.remove(mesh);
	mesh->clearAnalysisOverlay();
}

void SurfaceAnalysisOverlay::clearAll()
{
	for (auto it = _entries.constBegin(); it != _entries.constEnd(); ++it)
	{
		if (it->mesh)
			it->mesh->clearAnalysisOverlay();
	}
	_entries.clear();
}
