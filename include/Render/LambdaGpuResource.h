#pragma once

#include "IGpuContextResource.h"
#include <functional>
#include <utility>

// Wraps two plain callbacks as an IGpuContextResource, for resources that
// already have a matching release/restore-shaped pair of free functions or
// methods (e.g. ViewportWidget::cleanupTransmissionBuffer()/
// initTransmissionBuffer()) but no class of their own to implement the
// interface directly.
class LambdaGpuResource : public IGpuContextResource
{
public:
	LambdaGpuResource(std::function<void()> release, std::function<void()> restore)
		: _release(std::move(release)), _restore(std::move(restore))
	{
	}

	void releaseGpuResources() override { _release(); }
	void restoreGpuResources() override { _restore(); }

private:
	std::function<void()> _release;
	std::function<void()> _restore;
};
