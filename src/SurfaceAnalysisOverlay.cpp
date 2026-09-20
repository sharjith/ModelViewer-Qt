#include "SurfaceAnalysisOverlay.h"
#include "SceneMesh.h"

#include <algorithm>
#include <cmath>

void SurfaceAnalysisOverlay::applyResult(
	SceneMesh* mesh,
	const std::vector<float>& scalarPerSample,
	const std::vector<bool>& validPerSample,
	const CacheKey& key,
	float rangeMin, float rangeMax,
	AnalysisColormap colormap,
	AnalysisKind kind)
{
	if (!mesh)
		return;

	Entry entry;
	entry.mesh = mesh;
	entry.kind = kind;
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
	AnalysisColormap colormap,
	AnalysisKind kind)
{
	if (!mesh)
		return;

	Entry entry;
	entry.mesh = mesh;
	entry.kind = kind;
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

namespace
{
	// AnalysisColorRamp input for a refined field: valid wherever the sample has a value.
	std::vector<bool> refinedValidity(const std::vector<float>& values)
	{
		std::vector<bool> valid(values.size());
		for (size_t i = 0; i < values.size(); ++i)
			valid[i] = !std::isnan(values[i]);
		return valid;
	}
}

void SurfaceAnalysisOverlay::applyRefinedResult(
	SceneMesh* mesh,
	const std::vector<float>& scalarPerFace,
	const std::vector<bool>& validPerFace,
	const SubTriangleField& refined,
	const CacheKey& key,
	float rangeMin, float rangeMax,
	AnalysisColormap colormap,
	AnalysisKind kind)
{
	if (!mesh)
		return;
	if (refined.empty())
	{
		// Nothing finer than the triangle itself: the plain per-triangle display is the whole story.
		applyFlatResult(mesh, scalarPerFace, validPerFace, key, rangeMin, rangeMax, colormap, kind);
		return;
	}

	Entry entry;
	entry.mesh = mesh;
	entry.kind = kind;
	entry.key = key;
	entry.scalarPerSample = scalarPerFace;
	entry.validPerSample = validPerFace;
	entry.rangeMin = rangeMin;
	entry.rangeMax = rangeMax;
	entry.colormap = colormap;
	entry.isFlat = true;
	entry.refined = refined;
	_entries[mesh] = entry;

	const std::vector<float> rgba = AnalysisColorRamp::mapToRGBA(refined.values, refinedValidity(refined.values), rangeMin, rangeMax, colormap);
	mesh->setAnalysisOverlaySubTriangleColors(refined.gridN, refined.offset, rgba);
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

	if (!it->refined.empty())
	{
		const std::vector<float> refinedRgba = AnalysisColorRamp::mapToRGBA(
			it->refined.values, refinedValidity(it->refined.values), rangeMin, rangeMax, colormap);
		mesh->setAnalysisOverlaySubTriangleColors(it->refined.gridN, it->refined.offset, refinedRgba);
		return;
	}

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

bool SurfaceAnalysisOverlay::kindOf(SceneMesh* mesh, AnalysisKind& outKind) const
{
	if (!mesh)
		return false;
	const auto it = _entries.constFind(mesh);
	if (it == _entries.constEnd())
		return false;
	outKind = it->kind;
	return true;
}

const SubTriangleField* SurfaceAnalysisOverlay::refinedFieldOf(SceneMesh* mesh) const
{
	if (!mesh)
		return nullptr;
	const auto it = _entries.constFind(mesh);
	if (it == _entries.constEnd() || it->refined.empty())
		return nullptr;
	return &it->refined;
}

bool SurfaceAnalysisOverlay::scalarField(SceneMesh* mesh, std::vector<float>& outValues, std::vector<bool>& outValid) const
{
	if (!mesh)
		return false;
	const auto it = _entries.constFind(mesh);
	if (it == _entries.constEnd())
		return false;
	outValues = it->scalarPerSample;
	outValid = it->validPerSample;
	return true;
}

bool SurfaceAnalysisOverlay::scalarAt(SceneMesh* mesh, int triangleIndex, const QVector3D& barycentric,
                                       float& outValue, AnalysisKind& outKind) const
{
	if (!mesh || triangleIndex < 0)
		return false;
	const auto it = _entries.constFind(mesh);
	if (it == _entries.constEnd())
		return false;
	const Entry& entry = it.value();
	outKind = entry.kind;

	if (entry.isFlat)
	{
		const size_t idx = static_cast<size_t>(triangleIndex);
		if (idx >= entry.scalarPerSample.size())
			return false;
		if (idx < entry.validPerSample.size() && !entry.validPerSample[idx])
			return false;
		if (!entry.refined.empty() && idx < entry.refined.gridN.size() && entry.refined.gridN[idx] > 0)
		{
			// The value of the sub-triangle under the cursor - what the display shows there - not the
			// triangle's summary minimum.
			const int n = entry.refined.gridN[idx];
			const size_t sample = static_cast<size_t>(entry.refined.offset[idx])
				+ static_cast<size_t>(SubTriangleGrid::indexAt(n, barycentric.y(), barycentric.z()));
			if (sample >= entry.refined.values.size() || std::isnan(entry.refined.values[sample]))
				return false;
			outValue = entry.refined.values[sample];
			return true;
		}
		outValue = entry.scalarPerSample[idx];
		return true;
	}

	const std::vector<unsigned int> meshIndices = mesh->indices();
	const size_t base = static_cast<size_t>(triangleIndex) * 3;
	if (base + 2 >= meshIndices.size())
		return false;
	const unsigned int i0 = meshIndices[base];
	const unsigned int i1 = meshIndices[base + 1];
	const unsigned int i2 = meshIndices[base + 2];
	const size_t sampleCount = entry.scalarPerSample.size();
	if (i0 >= sampleCount || i1 >= sampleCount || i2 >= sampleCount)
		return false;
	if (!entry.validPerSample.empty())
	{
		const bool valid0 = i0 >= entry.validPerSample.size() || entry.validPerSample[i0];
		const bool valid1 = i1 >= entry.validPerSample.size() || entry.validPerSample[i1];
		const bool valid2 = i2 >= entry.validPerSample.size() || entry.validPerSample[i2];
		if (!valid0 || !valid1 || !valid2)
			return false;
	}
	outValue = entry.scalarPerSample[i0] * barycentric.x()
	         + entry.scalarPerSample[i1] * barycentric.y()
	         + entry.scalarPerSample[i2] * barycentric.z();
	return true;
}

bool SurfaceAnalysisOverlay::colorAt(SceneMesh* mesh, int triangleIndex, const QVector3D& barycentric,
                                      QColor& outColor) const
{
	float value = 0.0f;
	AnalysisKind kind = AnalysisKind::Curvature;
	if (!scalarAt(mesh, triangleIndex, barycentric, value, kind))
		return false;

	const auto it = _entries.constFind(mesh);
	if (it == _entries.constEnd())
		return false; // scalarAt() above already required this entry to exist - defensive only
	const Entry& entry = it.value();

	// Same normalization AnalysisColorRamp::mapToRGBA() itself uses - see
	// that function for the degenerate-range (rangeMin == rangeMax) case.
	const float range = entry.rangeMax - entry.rangeMin;
	const float t = (std::abs(range) < 1.0e-9f)
		? 0.5f
		: std::clamp((value - entry.rangeMin) / range, 0.0f, 1.0f);
	outColor = AnalysisColorRamp::colorForNormalized(t, entry.colormap);
	return true;
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
