#include "RtEnvironmentSampler.h"

#include <algorithm>
#include <cmath>

namespace
{
	// Must exactly match CpuPathTracer.cpp's selectCubemapFaceUV() - both
	// need to agree on which face/uv a given direction maps to, or pdf()
	// would evaluate the wrong texel for a direction sampleEnvironmentMiss()
	// actually read. face 0..5 = +X,-X,+Y,-Y,+Z,-Z; sc/tc are in [-1,1]
	// (unlike CpuPathTracer's local convention, this file works in that
	// [-1,1] space directly rather than remapping to [0,1] uv, since the
	// texel solid-angle formula below is naturally expressed in it).
	void directionToFaceSc(const glm::vec3& dir, int& face, float& sc, float& tc)
	{
		const float ax = std::abs(dir.x), ay = std::abs(dir.y), az = std::abs(dir.z);
		float ma;
		if (ax >= ay && ax >= az)
		{
			ma = ax;
			if (dir.x > 0.0f) { face = 0; sc = -dir.z; tc = -dir.y; }
			else              { face = 1; sc =  dir.z; tc = -dir.y; }
		}
		else if (ay >= ax && ay >= az)
		{
			ma = ay;
			if (dir.y > 0.0f) { face = 2; sc = dir.x; tc =  dir.z; }
			else              { face = 3; sc = dir.x; tc = -dir.z; }
		}
		else
		{
			ma = az;
			if (dir.z > 0.0f) { face = 4; sc =  dir.x; tc = -dir.y; }
			else              { face = 5; sc = -dir.x; tc = -dir.y; }
		}
		sc /= ma;
		tc /= ma;
	}

	// Inverse of directionToFaceSc() - reconstructs an (unnormalized)
	// direction from a face index and sc/tc in [-1,1].
	glm::vec3 faceScToDirection(int face, float sc, float tc)
	{
		switch (face)
		{
		case 0: return glm::vec3(1.0f, -tc, -sc);
		case 1: return glm::vec3(-1.0f, -tc, sc);
		case 2: return glm::vec3(sc, 1.0f, tc);
		case 3: return glm::vec3(sc, -1.0f, -tc);
		case 4: return glm::vec3(sc, -tc, 1.0f);
		default: return glm::vec3(-sc, -tc, -1.0f);
		}
	}

	// Exact cubemap texel solid angle - the standard AreaElement()-based
	// formula (as used by AMD's CubeMapGen and many renderers' cubemap
	// importance-sampling code) for the solid angle subtended by a texel
	// spanning [sc0,sc1] x [tc0,tc1] on a unit cube face.
	float areaElement(float x, float y)
	{
		return std::atan2(x * y, std::sqrt(x * x + y * y + 1.0f));
	}

	float texelSolidAngle(float sc0, float sc1, float tc0, float tc1)
	{
		return areaElement(sc0, tc0) - areaElement(sc0, tc1) - areaElement(sc1, tc0) + areaElement(sc1, tc1);
	}

	float luminance(const glm::vec3& c)
	{
		return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
	}
}

void RtEnvironmentSampler::build(const RtEnvironment& environment)
{
	_size = environment.faceSize;
	_flatCdf.clear();
	_texelPdf.clear();
	_totalWeight = 0.0f;

	if (_size <= 0)
		return;

	const size_t texelsPerFace = static_cast<size_t>(_size) * _size;
	for (int face = 0; face < 6; ++face)
	{
		if (environment.faces[face].size() != texelsPerFace * 3)
		{
			_size = 0; // malformed/missing face data - treat the whole environment as absent for importance sampling
			_flatCdf.clear();
			_texelPdf.clear();
			return;
		}
	}

	const size_t totalTexels = texelsPerFace * 6;
	_texelPdf.resize(totalTexels);
	_flatCdf.resize(totalTexels + 1);
	_flatCdf[0] = 0.0f;

	const float invSize = 1.0f / static_cast<float>(_size);
	size_t flatIndex = 0;
	for (int face = 0; face < 6; ++face)
	{
		const std::vector<float>& faceData = environment.faces[face];
		for (int y = 0; y < _size; ++y)
		{
			const float tc0 = 2.0f * (static_cast<float>(y) * invSize) - 1.0f;
			const float tc1 = 2.0f * (static_cast<float>(y + 1) * invSize) - 1.0f;
			for (int x = 0; x < _size; ++x)
			{
				const float sc0 = 2.0f * (static_cast<float>(x) * invSize) - 1.0f;
				const float sc1 = 2.0f * (static_cast<float>(x + 1) * invSize) - 1.0f;
				const float solidAngle = texelSolidAngle(sc0, sc1, tc0, tc1);

				const size_t rgbIdx = (static_cast<size_t>(y) * _size + x) * 3;
				const glm::vec3 rgb(faceData[rgbIdx], faceData[rgbIdx + 1], faceData[rgbIdx + 2]);
				const float weight = std::max(luminance(rgb), 0.0f) * solidAngle;

				_totalWeight += weight;
				_flatCdf[flatIndex + 1] = _totalWeight;
				// texelPdf is filled in a second pass below, once _totalWeight
				// (the normalization constant) is known.
				_texelPdf[flatIndex] = weight / std::max(solidAngle, 1e-12f);
				++flatIndex;
			}
		}
	}

	if (_totalWeight <= 0.0f)
	{
		// No emissive/bright content at all (e.g. a flat black environment) -
		// nothing meaningful to importance-sample toward.
		_size = 0;
		_flatCdf.clear();
		_texelPdf.clear();
		return;
	}

	for (float& p : _texelPdf)
		p /= _totalWeight; // now a genuine solid-angle PDF (integrates to 1 over the sphere)
}

void RtEnvironmentSampler::sample(float u0, float u1, float u2, glm::vec3& outDir, float& outPdf) const
{
	if (!isValid())
	{
		outDir = glm::vec3(0.0f, 1.0f, 0.0f);
		outPdf = 0.0f;
		return;
	}

	const float target = std::clamp(u0, 0.0f, 1.0f) * _totalWeight;
	const auto it = std::upper_bound(_flatCdf.begin(), _flatCdf.end(), target);
	size_t flatIndex = static_cast<size_t>(std::distance(_flatCdf.begin(), it)) - 1;
	flatIndex = std::min(flatIndex, _texelPdf.size() - 1);

	const size_t texelsPerFace = static_cast<size_t>(_size) * _size;
	const int face = static_cast<int>(flatIndex / texelsPerFace);
	const size_t rem = flatIndex % texelsPerFace;
	const int y = static_cast<int>(rem / _size);
	const int x = static_cast<int>(rem % _size);

	const float invSize = 1.0f / static_cast<float>(_size);
	const float sc = 2.0f * ((static_cast<float>(x) + u1) * invSize) - 1.0f;
	const float tc = 2.0f * ((static_cast<float>(y) + u2) * invSize) - 1.0f;

	outDir = glm::normalize(faceScToDirection(face, sc, tc));
	outPdf = _texelPdf[flatIndex];
}

float RtEnvironmentSampler::pdf(const glm::vec3& direction) const
{
	if (!isValid())
		return 0.0f;

	int face;
	float sc, tc;
	directionToFaceSc(glm::normalize(direction), face, sc, tc);

	const float u = std::clamp((sc + 1.0f) * 0.5f, 0.0f, 1.0f);
	const float v = std::clamp((tc + 1.0f) * 0.5f, 0.0f, 1.0f);
	const int x = std::min(static_cast<int>(u * static_cast<float>(_size)), _size - 1);
	const int y = std::min(static_cast<int>(v * static_cast<float>(_size)), _size - 1);

	const size_t flatIndex = static_cast<size_t>(face) * _size * _size + static_cast<size_t>(y) * _size + x;
	return _texelPdf[flatIndex];
}
