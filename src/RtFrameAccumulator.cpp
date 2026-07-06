#include "RtFrameAccumulator.h"

#include <algorithm>

void RtFrameAccumulator::resize(int width, int height)
{
	if (width == _width && height == _height)
		return;

	_width  = width;
	_height = height;
	_sum.assign(static_cast<size_t>(std::max(0, width)) * static_cast<size_t>(std::max(0, height)), glm::vec3(0.0f));
	_hitCounts.assign(_sum.size(), 0u);
	_sampleCount = 0;
}

void RtFrameAccumulator::reset()
{
	std::fill(_sum.begin(), _sum.end(), glm::vec3(0.0f));
	std::fill(_hitCounts.begin(), _hitCounts.end(), 0u);
	_sampleCount = 0;
}

void RtFrameAccumulator::accumulate(const std::vector<glm::vec3>& sampleRadiance,
	const std::vector<uint8_t>* primaryHitMask)
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
