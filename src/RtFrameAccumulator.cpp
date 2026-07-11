#include "RtFrameAccumulator.h"

#include <algorithm>

void RtFrameAccumulator::resize(int width, int height)
{
	if (width == _width && height == _height)
		return;

	_width  = width;
	_height = height;
	_sum.assign(static_cast<size_t>(std::max(0, width)) * static_cast<size_t>(std::max(0, height)), glm::vec3(0.0f));
	_albedoSum.assign(_sum.size(), glm::vec3(0.0f));
	_normalSum.assign(_sum.size(), glm::vec3(0.0f));
	_hitCounts.assign(_sum.size(), 0u);
	_clearTransmissionCounts.assign(_sum.size(), 0u);
	_sampleCount = 0;
}

void RtFrameAccumulator::reset()
{
	std::fill(_sum.begin(), _sum.end(), glm::vec3(0.0f));
	std::fill(_albedoSum.begin(), _albedoSum.end(), glm::vec3(0.0f));
	std::fill(_normalSum.begin(), _normalSum.end(), glm::vec3(0.0f));
	std::fill(_hitCounts.begin(), _hitCounts.end(), 0u);
	std::fill(_clearTransmissionCounts.begin(), _clearTransmissionCounts.end(), 0u);
	_sampleCount = 0;
}

void RtFrameAccumulator::accumulate(const std::vector<glm::vec3>& sampleRadiance,
	const std::vector<uint8_t>* primaryHitMask,
	const std::vector<glm::vec3>* primaryAlbedo,
	const std::vector<glm::vec3>* primaryNormal,
	const std::vector<uint8_t>* clearTransmissionMask)
{
	if (sampleRadiance.size() != _sum.size())
		return; // resolution mismatch (e.g. a stale pass from before a resize) - drop it

	for (size_t i = 0; i < _sum.size(); ++i)
		_sum[i] += sampleRadiance[i];

	if (primaryHitMask && primaryHitMask->size() == _hitCounts.size())
	{
		for (size_t i = 0; i < _hitCounts.size(); ++i)
			_hitCounts[i] += (*primaryHitMask)[i];
	}

	if (primaryAlbedo && primaryAlbedo->size() == _albedoSum.size())
	{
		for (size_t i = 0; i < _albedoSum.size(); ++i)
			_albedoSum[i] += (*primaryAlbedo)[i];
	}

	if (primaryNormal && primaryNormal->size() == _normalSum.size())
	{
		for (size_t i = 0; i < _normalSum.size(); ++i)
			_normalSum[i] += (*primaryNormal)[i];
	}

	if (clearTransmissionMask && clearTransmissionMask->size() == _clearTransmissionCounts.size())
	{
		for (size_t i = 0; i < _clearTransmissionCounts.size(); ++i)
			_clearTransmissionCounts[i] += (*clearTransmissionMask)[i];
	}

	++_sampleCount;
}

std::vector<glm::vec3> RtFrameAccumulator::resolve() const
{
	std::vector<glm::vec3> result(_sum.size(), glm::vec3(0.0f));
	if (_sampleCount == 0)
		return result;

	const float invCount = 1.0f / static_cast<float>(_sampleCount);
	for (size_t i = 0; i < _sum.size(); ++i)
		result[i] = _sum[i] * invCount;
	return result;
}

std::vector<glm::vec3> RtFrameAccumulator::resolveAlbedo() const
{
	std::vector<glm::vec3> result(_albedoSum.size(), glm::vec3(0.0f));
	if (_sampleCount == 0)
		return result;

	const float invCount = 1.0f / static_cast<float>(_sampleCount);
	for (size_t i = 0; i < _albedoSum.size(); ++i)
		result[i] = _albedoSum[i] * invCount;
	return result;
}

std::vector<glm::vec3> RtFrameAccumulator::resolveNormal() const
{
	std::vector<glm::vec3> result(_normalSum.size(), glm::vec3(0.0f));
	if (_sampleCount == 0)
		return result;

	const float invCount = 1.0f / static_cast<float>(_sampleCount);
	for (size_t i = 0; i < _normalSum.size(); ++i)
		result[i] = _normalSum[i] * invCount;
	return result;
}
